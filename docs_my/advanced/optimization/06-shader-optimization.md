# Filament Shader 优化

## 📖 概述

Shader（着色器）是 GPU 上执行的程序，直接决定渲染性能。Shader 优化是移动端和复杂场景性能优化的关键。本文档深入讲解 Shader 性能分析、优化技术和实战案例。

**Shader 性能瓶颈**：
- **ALU（算术逻辑单元）**：复杂的数学运算
- **带宽（Bandwidth）**：纹理采样、内存读写
- **寄存器（Register）**：寄存器不足导致 Spill
- **分支（Branch）**：条件判断导致 Warp Divergence

**优化目标**：
- 减少 ALU 指令数 30-50%
- 降低纹理采样次数 20-40%
- 优化寄存器使用，避免 Spill
- 消除分支，提升并行度

本文档将介绍 RenderDoc、Mali Offline Compiler、Nsight Graphics 等工具的使用，以及 Shader 优化的实战技巧。

---

## 1. Shader 性能分析工具

### 1.1 RenderDoc 分析

**安装和使用**：

```bash
# Linux/Windows/macOS
# 下载：https://renderdoc.org/

# 启动应用并 Capture Frame
renderdoccmd capture ./myapp
```

**分析 Shader 性能**：

1. **Capture Frame**：按 F12 或 Print Screen
2. **Pipeline State**：查看当前 Shader
3. **Shader Debugger**：逐指令调试
4. **Statistics**：查看 ALU、纹理采样次数

**示例输出**：

```
Fragment Shader: pbr_material.frag
  ALU Instructions: 542
  Texture Samples: 8
  Registers Used: 32/64
  Estimated Cost: High
```

### 1.2 Mali Offline Compiler

**适用于 ARM Mali GPU**（Android 大部分设备）：

```bash
# 安装
# https://developer.arm.com/tools-and-software/graphics-and-gaming/mali-offline-compiler

# 编译 Shader
malioc --core Mali-G76 --fragment shader.frag

# 输出示例：
# Mali Offline Compiler v7.4.0
# ===========================
# Hardware: Mali-G76
# Driver: r25p0-00rel0
# 
# Work registers: 32
# Uniform registers: 16
# 
# A: 45 cycles
# LS: 12 cycles (Load/Store)
# V: 8 cycles (Varying)
# T: 24 cycles (Texture)
# 
# Total: 89 cycles per pixel
```

**关键指标**：
- **Work registers**：工作寄存器数量（越少越好）
- **A cycles**：算术指令周期
- **T cycles**：纹理采样周期
- **Total cycles**：每像素总周期（越少越好）

### 1.3 NVIDIA Nsight Graphics

**适用于 NVIDIA GPU**（桌面平台）：

1. 启动 Nsight Graphics
2. 连接应用并 Capture Frame
3. 选择 Shader → Shader Profiler
4. 查看：
   - **Occupancy**：占用率（越高越好）
   - **Register Usage**：寄存器使用
   - **Instruction Mix**：指令类型分布

---

## 2. 减少 ALU 指令

### 2.1 避免复杂数学运算

```glsl
// ❌ 不好：pow 非常昂贵
float specular = pow(max(dot(R, V), 0.0), shininess);

// ✅ 好：预计算或使用近似
// 方法 1：Lookup Table（预计算存储在纹理中）
float specular = texture(specularLUT, vec2(dotRV, shininess)).r;

// 方法 2：低次多项式近似（对于特定范围）
// pow(x, 32) ≈ x^5 * x^5 (两次平方)
float x2 = dotRV * dotRV;
float x4 = x2 * x2;
float x8 = x4 * x4;
float specular = x8 * x8;  // x^16，接近 pow(x, shininess)
```

**常见昂贵操作及替代**：

| 操作 | 周期 | 替代方案 | 周期节省 |
|------|------|----------|----------|
| `pow(x, y)` | 20-30 | 查找表 / 近似 | 80-90% |
| `exp(x)` | 15-20 | 近似 | 70% |
| `log(x)` | 15-20 | 近似 | 70% |
| `sin/cos(x)` | 10-15 | 查找表 | 60% |
| `sqrt(x)` | 5-10 | `inversesqrt` + 倒数 | 30% |
| `normalize(v)` | 15 | 预归一化 | 100% |

### 2.2 向量化计算

```glsl
// ❌ 不好：逐分量计算
float r = texture(tex, uv).r * color.r;
float g = texture(tex, uv).g * color.g;
float b = texture(tex, uv).b * color.b;
vec3 result = vec3(r, g, b);

// ✅ 好：向量化
vec3 result = texture(tex, uv).rgb * color.rgb;
```

### 2.3 预计算

```glsl
// ❌ 不好：每像素计算
vec3 lightDir = normalize(lightPos - fragPos);
float NdotL = max(dot(normal, lightDir), 0.0);

// ✅ 好：在 Vertex Shader 预计算
// Vertex Shader:
out vec3 vLightDir;
void main() {
    vLightDir = normalize(lightPos - worldPos);
    // ...
}

// Fragment Shader:
in vec3 vLightDir;
void main() {
    float NdotL = max(dot(normal, vLightDir), 0.0);
    // 节省 normalize 和减法
}
```

---

## 3. 优化纹理采样

### 3.1 减少采样次数

```glsl
// ❌ 不好：多次采样同一纹理
vec3 albedo = texture(baseColorMap, uv).rgb;
float metallic = texture(metallicMap, uv).r;
float roughness = texture(roughnessMap, uv).r;

// ✅ 好：打包到一张纹理（通道复用）
// RGB: Albedo, A: Metallic
vec4 baseColorMetallic = texture(baseColorMap, uv);
vec3 albedo = baseColorMetallic.rgb;
float metallic = baseColorMetallic.a;

// R: Roughness, G: AO
vec2 roughnessAO = texture(roughnessAOMap, uv).rg;
float roughness = roughnessAO.r;
float ao = roughnessAO.g;
```

**性能对比**：

```
Before: 5 texture samples × 4 cycles = 20 cycles
After:  2 texture samples × 4 cycles = 8 cycles
Savings: 60%
```

### 3.2 使用合适的 Mipmap 级别

```glsl
// ❌ 不好：总是采样最高级别（Mip 0）
vec4 color = textureLod(tex, uv, 0.0);

// ✅ 好：让硬件自动选择 Mip 级别
vec4 color = texture(tex, uv);  // 自动根据 ddx/ddy 选择

// 或显式计算
float mipLevel = textureQueryLod(tex, uv).y;
vec4 color = textureLod(tex, uv, mipLevel);
```

### 3.3 Anisotropic Filtering 优化

```cpp
// 移动端降低各向异性过滤级别
filament::TextureSampler sampler;

#if defined(__ANDROID__) || defined(__IPHONE_OS_VERSION_MIN_REQUIRED)
    // 移动端：2x 或 4x
    sampler.setAnisotropy(4.0f);
#else
    // 桌面：8x 或 16x
    sampler.setAnisotropy(16.0f);
#endif
```

---

## 4. 精度优化

### 4.1 移动端精度策略

```glsl
precision mediump float;  // 默认中精度

// 需要高精度的场景
uniform highp mat4 modelViewProjection;  // 矩阵变换
in highp vec3 position;                  // 顶点位置
out highp float depth;                   // 深度值

// 中精度足够的场景
in mediump vec3 normal;
in mediump vec2 uv;
uniform mediump vec3 lightColor;
out mediump vec4 fragColor;

// 低精度足够的场景
uniform lowp sampler2D tex;
mediump vec3 normalizedVec = normalize(vec);  // 结果已归一化
```

**精度性能影响**：

```
移动 GPU（Mali-G76）：
  highp: 1.0x (baseline)
  mediump: 2.0x faster
  lowp: 4.0x faster
```

### 4.2 避免精度问题

```glsl
// ❌ 不好：大数减法导致精度丢失
highp float depth = gl_FragCoord.z;
highp float linearDepth = (zFar * zNear) / (zFar - depth * (zFar - zNear));

// ✅ 好：使用对数深度
highp float logDepth = log2(gl_FragCoord.w + 1.0) / log2(zFar + 1.0);
```

---

## 5. 消除分支

### 5.1 条件分支优化

```glsl
// ❌ 不好：动态分支（Warp Divergence）
if (useDiffuseMap) {
    color = texture(diffuseMap, uv);
} else {
    color = baseColor;
}

// ✅ 好：使用 mix（无分支）
color = mix(baseColor, texture(diffuseMap, uv), float(useDiffuseMap));

// 或者使用三元运算符（编译器可能优化为 CMOV）
color = useDiffuseMap ? texture(diffuseMap, uv) : baseColor;
```

### 5.2 循环优化

```glsl
// ❌ 不好：动态循环长度
for (int i = 0; i < lightCount; ++i) {
    // ...
}

// ✅ 好：固定循环长度 + 早退
#define MAX_LIGHTS 8
for (int i = 0; i < MAX_LIGHTS; ++i) {
    if (i >= lightCount) break;  // 编译器可能优化
    // ...
}

// 更好：完全展开（小循环）
// Light 0
vec3 L0 = normalize(lights[0].position - fragPos);
color += computeLight(L0, lights[0].color);

// Light 1
vec3 L1 = normalize(lights[1].position - fragPos);
color += computeLight(L1, lights[1].color);

// ...（手动或宏展开）
```

### 5.3 Uber Shader 优化

**问题**：大量 `#ifdef` 导致变体爆炸。

**方案**：使用 Uniform 分支 + 运行时常量。

```glsl
// ❌ 不好：编译时分支（需要多个变体）
#ifdef USE_NORMAL_MAP
    vec3 normal = texture(normalMap, uv).xyz * 2.0 - 1.0;
#else
    vec3 normal = vertexNormal;
#endif

// ✅ 好：运行时常量（单一变体）
uniform bool useNormalMap;

vec3 normal = vertexNormal;
if (useNormalMap) {  // 编译器知道 useNormalMap 是常量
    normal = texture(normalMap, uv).xyz * 2.0 - 1.0;
}
```

---

## 6. 寄存器优化

### 6.1 减少临时变量

```glsl
// ❌ 不好：过多临时变量
vec3 L = normalize(lightPos - fragPos);
vec3 V = normalize(viewPos - fragPos);
vec3 H = normalize(L + V);
float NdotL = dot(normal, L);
float NdotH = dot(normal, H);
float VdotH = dot(V, H);
float NdotV = dot(normal, V);

// ✅ 好：复用变量
vec3 L = normalize(lightPos - fragPos);
vec3 V = normalize(viewPos - fragPos);
vec3 H = normalize(L + V);

// 直接在表达式中使用，避免存储
float specular = pow(max(dot(normal, H), 0.0), shininess) 
               * fresnel(dot(V, H));
```

### 6.2 数据打包

```glsl
// ❌ 不好：独立变量
float metallic;
float roughness;
float ao;
float emissive;

// ✅ 好：打包到 vec4
vec4 materialParams;  // (metallic, roughness, ao, emissive)
```

---

## 7. 优化 PBR Shader

### 7.1 原始 PBR（昂贵）

```glsl
// 标准 PBR BRDF
vec3 F0 = mix(vec3(0.04), albedo, metallic);

// Fresnel (Schlick)
vec3 F = F0 + (1.0 - F0) * pow(1.0 - max(dot(H, V), 0.0), 5.0);

// Distribution (GGX)
float a = roughness * roughness;
float a2 = a * a;
float NdotH = max(dot(N, H), 0.0);
float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
float D = a2 / (PI * denom * denom);

// Geometry (Smith)
float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
float G_L = NdotL / (NdotL * (1.0 - k) + k);
float G_V = NdotV / (NdotV * (1.0 - k) + k);
float G = G_L * G_V;

// Cook-Torrance
vec3 specular = (D * G * F) / (4.0 * NdotL * NdotV + 0.0001);
```

### 7.2 优化版 PBR

```glsl
// 预计算和近似

// Fresnel 近似（移动端）
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

// GGX 使用查找表
uniform sampler2D brdfLUT;
vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;
vec3 specular = (F0 * brdf.x + brdf.y);

// Image-Based Lighting（预过滤）
vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_LOD).rgb;
vec3 irradiance = texture(irradianceMap, N).rgb;

vec3 ambient = (kD * irradiance * albedo + prefilteredColor * specular) * ao;
```

**性能对比**：

```
Original PBR: 250 ALU instructions
Optimized PBR: 80 ALU instructions
Savings: 68%
```

---

## 8. Compute Shader 优化

### 8.1 Work Group 大小优化

```glsl
// ❌ 不好：未对齐
layout(local_size_x = 7, local_size_y = 7) in;

// ✅ 好：对齐到 Warp/Wave 大小
// NVIDIA: 32 threads (Warp)
// AMD: 64 threads (Wave)
// Mali: 16 threads (Quad)

layout(local_size_x = 8, local_size_y = 8) in;  // 64 threads
// 或
layout(local_size_x = 16, local_size_y = 16) in;  // 256 threads
```

### 8.2 Shared Memory 使用

```glsl
// 使用 Shared Memory 减少全局内存访问

layout(local_size_x = 16, local_size_y = 16) in;

shared vec3 sharedData[16][16];

void main() {
    uvec2 localID = gl_LocalInvocationID.xy;
    uvec2 globalID = gl_GlobalInvocationID.xy;
    
    // 加载到 Shared Memory
    sharedData[localID.y][localID.x] = imageLoad(inputImage, ivec2(globalID)).rgb;
    
    barrier();  // 同步
    
    // 从 Shared Memory 计算
    vec3 result = vec3(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            int sy = clamp(int(localID.y) + y, 0, 15);
            int sx = clamp(int(localID.x) + x, 0, 15);
            result += sharedData[sy][sx];
        }
    }
    result /= 9.0;
    
    imageStore(outputImage, ivec2(globalID), vec4(result, 1.0));
}
```

---

## 9. 实战案例：优化草地渲染

### 9.1 问题描述

- **场景**：10 万株草（Instancing）
- **Shader**：每株草 Vertex Shader 200 指令，Fragment Shader 150 指令
- **问题**：帧率 20 FPS（GPU bound）

### 9.2 优化前 Vertex Shader

```glsl
#version 310 es
precision highp float;

uniform mat4 viewProjection;
uniform vec3 cameraPos;
uniform float time;

in vec3 position;
in vec3 normal;
in vec2 uv;
in vec4 instanceTransform;  // (x, y, z, scale)

out vec3 vNormal;
out vec2 vUV;
out vec3 vWorldPos;

void main() {
    // 构建实例变换矩阵（昂贵）
    mat4 instanceMatrix = mat4(
        instanceTransform.w, 0, 0, 0,
        0, instanceTransform.w, 0, 0,
        0, 0, instanceTransform.w, 0,
        instanceTransform.x, instanceTransform.y, instanceTransform.z, 1
    );
    
    // 风吹动画（昂贵）
    float windStrength = sin(time + instanceTransform.x) * 0.1;
    vec3 windOffset = vec3(windStrength, 0, 0) * position.y;
    
    vec3 worldPos = (instanceMatrix * vec4(position + windOffset, 1.0)).xyz;
    vWorldPos = worldPos;
    vNormal = mat3(instanceMatrix) * normal;
    vUV = uv;
    
    gl_Position = viewProjection * vec4(worldPos, 1.0);
}
```

### 9.3 优化后 Vertex Shader

```glsl
#version 310 es
precision mediump float;  // 降低精度

uniform highp mat4 viewProjection;
uniform mediump float time;

in highp vec3 position;
in mediump vec3 normal;
in mediump vec2 uv;
in highp vec4 instanceTransform;

out mediump vec3 vNormal;
out mediump vec2 vUV;

void main() {
    // 简化变换：只有缩放和平移
    highp vec3 worldPos = position * instanceTransform.w + instanceTransform.xyz;
    
    // 简化风动画：使用 LUT
    mediump float windPhase = instanceTransform.x * 0.1;
    mediump float wind = sin(time + windPhase) * 0.1 * position.y;
    worldPos.x += wind;
    
    vNormal = normal;  // 法线不变换（草是 Billboard）
    vUV = uv;
    
    gl_Position = viewProjection * vec4(worldPos, 1.0);
}
```

### 9.4 优化结果

| 指标 | Before | After | 改善 |
|------|--------|-------|------|
| Vertex Shader 指令 | 200 | 50 | 75% |
| Fragment Shader 指令 | 150 | 80 | 47% |
| 帧率 | 20 FPS | 55 FPS | 175% |
| GPU 占用 | 95% | 60% | 37% |

---

## 10. Shader 调试技术

### 10.1 可视化调试

```glsl
// 调试法线
out vec4 fragColor;
void main() {
    // 将法线从 [-1, 1] 映射到 [0, 1]
    fragColor = vec4(normal * 0.5 + 0.5, 1.0);
}

// 调试 UV
fragColor = vec4(uv, 0.0, 1.0);

// 调试深度
float depth = gl_FragCoord.z;
fragColor = vec4(vec3(depth), 1.0);

// 调试 Mipmap 级别
float mipLevel = textureQueryLod(tex, uv).y;
vec3 mipColor = mix(vec3(0, 0, 1), vec3(1, 0, 0), mipLevel / 8.0);
fragColor = vec4(mipColor, 1.0);
```

### 10.2 Shader 性能计数器

```cpp
// Filament 中查询 GPU 时间
#include <utils/GPUTimer.h>

using namespace filament;

class ShaderProfiler {
public:
    void beginFrame(Engine& engine) {
        mTimer.start(engine);
    }

    void beginPass(const char* name) {
        mTimer.beginMarker(name);
    }

    void endPass() {
        mTimer.endMarker();
    }

    void endFrame() {
        mTimer.stop();

        // 获取结果
        auto results = mTimer.getResults();
        for (const auto& [name, time] : results) {
            printf("%s: %.2f ms\n", name.c_str(), time * 1000.0);
        }
    }

private:
    utils::GPUTimer mTimer;
};

// 使用
ShaderProfiler profiler;

profiler.beginFrame(*engine);
{
    profiler.beginPass("Shadow Pass");
    renderer->renderShadows(view, scene);
    profiler.endPass();

    profiler.beginPass("Main Pass");
    renderer->render(view);
    profiler.endPass();

    profiler.beginPass("Post Processing");
    postProcessor->render();
    profiler.endPass();
}
profiler.endFrame();

// 输出示例：
// Shadow Pass: 2.35 ms
// Main Pass: 8.67 ms
// Post Processing: 1.23 ms
```

### 10.3 运行时 Shader 热重载

```cpp
// Shader 热重载系统
class ShaderHotReloader {
public:
    ShaderHotReloader(Engine& engine) : mEngine(engine) {}

    void watchMaterial(const std::string& path, Material* material) {
        mWatchList[path] = {material, getFileModTime(path)};
    }

    void update() {
        for (auto& [path, info] : mWatchList) {
            auto currentTime = getFileModTime(path);
            if (currentTime > info.lastModTime) {
                printf("Reloading material: %s\n", path.c_str());
                reloadMaterial(path, info.material);
                info.lastModTime = currentTime;
            }
        }
    }

private:
    void reloadMaterial(const std::string& path, Material* material) {
        // 重新编译材质
        std::ifstream file(path, std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());

        // 更新材质
        material->setPackage(data.data(), data.size());
    }

    std::time_t getFileModTime(const std::string& path) {
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            return st.st_mtime;
        }
        return 0;
    }

    struct MaterialInfo {
        Material* material;
        std::time_t lastModTime;
    };

    Engine& mEngine;
    std::unordered_map<std::string, MaterialInfo> mWatchList;
};

// 使用
ShaderHotReloader reloader(*engine);
reloader.watchMaterial("materials/pbr.filamat", pbrMaterial);

// 每帧检查
void onFrame() {
    reloader.update();
    // ...
}
```

---

## 11. 高级优化模式

### 11.1 Half-Resolution Rendering

**用于昂贵效果**（如 SSR、SSAO）：

```cpp
// 创建半分辨率 Render Target
View* mainView = engine->createView();
mainView->setViewport({0, 0, width, height});

View* halfResView = engine->createView();
halfResView->setViewport({0, 0, width / 2, height / 2});

// Render Pass
RenderTarget* halfResRT = RenderTarget::Builder()
    .texture(RenderTarget::AttachmentPoint::COLOR, halfResTexture)
    .build(*engine);

halfResView->setRenderTarget(halfResRT);

// 渲染
renderer->render(halfResView);  // SSAO at half resolution

// 在主 Pass 中使用
// Fragment Shader:
uniform sampler2D ssaoTexture;
float ao = texture(ssaoTexture, uv).r;  // 自动双线性插值
```

**性能提升**：
```
Full resolution (1920x1080): 16.7 ms
Half resolution (960x540):   4.2 ms (75% faster)
```

### 11.2 Temporal Anti-Aliasing (TAA)

**原理**：复用上一帧结果，减少每帧采样。

```glsl
// Fragment Shader
uniform sampler2D currentFrame;
uniform sampler2D historyFrame;
uniform sampler2D velocityBuffer;  // Motion vectors
uniform float blendFactor;

void main() {
    vec2 uv = gl_FragCoord.xy / resolution;

    // 当前帧颜色
    vec3 current = texture(currentFrame, uv).rgb;

    // 获取运动向量
    vec2 velocity = texture(velocityBuffer, uv).rg;
    vec2 historyUV = uv - velocity;

    // 历史帧颜色
    vec3 history = texture(historyFrame, historyUV).rgb;

    // 色彩空间变换（减少 Ghosting）
    vec3 currentYCoCg = RGBToYCoCg(current);
    vec3 historyYCoCg = RGBToYCoCg(history);

    // Neighborhood Clamping（防止 Ghosting）
    vec3 minColor = vec3(1e10);
    vec3 maxColor = vec3(-1e10);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 offset = vec2(x, y) / resolution;
            vec3 neighbor = RGBToYCoCg(texture(currentFrame, uv + offset).rgb);
            minColor = min(minColor, neighbor);
            maxColor = max(maxColor, neighbor);
        }
    }
    historyYCoCg = clamp(historyYCoCg, minColor, maxColor);

    // 混合
    vec3 finalYCoCg = mix(historyYCoCg, currentYCoCg, blendFactor);
    vec3 finalRGB = YCoCgToRGB(finalYCoCg);

    fragColor = vec4(finalRGB, 1.0);
}

vec3 RGBToYCoCg(vec3 rgb) {
    float Y = dot(rgb, vec3(0.25, 0.5, 0.25));
    float Co = dot(rgb, vec3(0.5, 0.0, -0.5));
    float Cg = dot(rgb, vec3(-0.25, 0.5, -0.25));
    return vec3(Y, Co, Cg);
}

vec3 YCoCgToRGB(vec3 YCoCg) {
    float tmp = YCoCg.x - YCoCg.z;
    return vec3(
        tmp + YCoCg.y,
        YCoCg.x + YCoCg.z,
        tmp - YCoCg.y
    );
}
```

**性能提升**：
```
Without TAA (4xMSAA): 12 ms
With TAA (1x + history): 4 ms (67% faster)
```

### 11.3 Async Compute

**原理**：在 Fragment Shader 执行时并行运行 Compute Shader。

```cpp
// Vulkan 示例
void renderFrame() {
    // Graphics Queue: 主渲染
    vkCmdBeginRenderPass(graphicsCmdBuffer, ...);
    vkCmdDraw(graphicsCmdBuffer, ...);
    vkCmdEndRenderPass(graphicsCmdBuffer);

    // Compute Queue: 异步计算（如粒子更新）
    vkCmdBindPipeline(computeCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, particlePipeline);
    vkCmdDispatch(computeCmdBuffer, particleCount / 256, 1, 1);

    // Semaphore 同步
    VkSemaphore computeFinished;
    vkQueueSubmit(computeQueue, 1, &computeSubmitInfo, computeFinished);

    // Graphics 等待 Compute
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    graphicsSubmitInfo.pWaitSemaphores = &computeFinished;
    graphicsSubmitInfo.pWaitDstStageMask = &waitStage;

    vkQueueSubmit(graphicsQueue, 1, &graphicsSubmitInfo, renderFinished);
}
```

**时间线对比**：

```
Without Async Compute:
  [Graphics: 10ms] [Compute: 3ms] Total: 13ms

With Async Compute:
  [Graphics: 10ms]
       [Compute: 3ms (overlap)]
  Total: 10ms (23% faster)
```

### 11.4 Wave Intrinsics 优化

**适用于现代 GPU**（Shader Model 6.0+）：

```glsl
// GLSL (GL_KHR_shader_subgroup extension)
#extension GL_KHR_shader_subgroup_basic : enable
#extension GL_KHR_shader_subgroup_vote : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable

// 示例：光照剔除（Subgroup 内协作）
void main() {
    // 每个 Subgroup（32 或 64 threads）协作
    uint activeMask = subgroupBallot(true).x;

    // 检查 Subgroup 内是否有光源影响
    bool anyLitInSubgroup = subgroupAny(NdotL > 0.0);

    if (!anyLitInSubgroup) {
        // 整个 Subgroup 跳过光照计算
        fragColor = vec4(albedo * ambient, 1.0);
        return;
    }

    // 计算光照
    vec3 lighting = computeLighting();
    fragColor = vec4(lighting, 1.0);
}

// 示例：Subgroup Reduction
float maxLuminance = subgroupMax(luminance);
float minLuminance = subgroupMin(luminance);
float avgLuminance = subgroupAdd(luminance) / float(gl_SubgroupSize);
```

**性能提升**：
```
Without Wave Intrinsics: 150 ALU
With Wave Intrinsics: 95 ALU (37% faster)
```

---

## 12. Shader 变体管理

### 12.1 变体爆炸问题

**问题示例**：

```
Features:
  - Skinning (ON/OFF)
  - Shadow (ON/OFF)
  - Normal Map (ON/OFF)
  - AO Map (ON/OFF)
  - Emissive (ON/OFF)
  - Vertex Color (ON/OFF)

Total Variants: 2^6 = 64

With 3 light types (Directional, Point, Spot):
  64 * 3 = 192 variants

Disk space: 192 * 50KB = 9.6 MB (single material!)
```

### 12.2 变体减少策略

**方案 1：Feature Grouping**

```cpp
// 将低频特性组合
#define VARIANT_BASE                0x00
#define VARIANT_SKINNED             0x01
#define VARIANT_SKINNED_SHADOW      0x02
#define VARIANT_STATIC              0x03
#define VARIANT_STATIC_SHADOW       0x04

// 其他特性用 Uniform（运行时分支）
uniform bool hasNormalMap;
uniform bool hasAOMap;
uniform bool hasEmissive;

// Shader 中
if (hasNormalMap) {
    normal = texture(normalMap, uv).xyz * 2.0 - 1.0;
}
```

**变体数量**：64 → 5 (87.5% reduction)

**方案 2：Uber Shader + Specialization Constants**

```cpp
// Vulkan Specialization Constants
struct SpecializationData {
    uint32_t hasNormalMap;
    uint32_t hasAOMap;
    uint32_t lightType;
};

VkSpecializationMapEntry entries[] = {
    {0, offsetof(SpecializationData, hasNormalMap), sizeof(uint32_t)},
    {1, offsetof(SpecializationData, hasAOMap), sizeof(uint32_t)},
    {2, offsetof(SpecializationData, lightType), sizeof(uint32_t)},
};

VkSpecializationInfo specInfo = {
    .mapEntryCount = 3,
    .pMapEntries = entries,
    .dataSize = sizeof(SpecializationData),
    .pData = &specData,
};

// Shader 中
layout(constant_id = 0) const bool hasNormalMap = false;
layout(constant_id = 1) const bool hasAOMap = false;
layout(constant_id = 2) const int lightType = 0;

void main() {
    if (hasNormalMap) {  // 编译时优化掉
        // ...
    }
}
```

**优势**：
- 单个 SPIR-V 二进制
- 运行时快速特化（< 1ms）
- 编译器完全优化掉分支

---

## 13. Shader 编译优化

### 13.1 预编译 Shader

```cpp
// 离线编译 Shader（避免运行时卡顿）

// 使用 Filament matc 工具
// matc -p mobile -o material.filamat material.mat

// 运行时加载预编译材质
std::ifstream file("material.filamat", std::ios::binary);
std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());

Material* material = Material::Builder()
    .package(data.data(), data.size())
    .build(*engine);
```

### 13.2 减少变体数量

```cpp
// ❌ 不好：2^8 = 256 个变体
#define VARIANT_HAS_SKINNING           0x01
#define VARIANT_HAS_SHADOW             0x02
#define VARIANT_HAS_VERTEX_COLOR       0x04
#define VARIANT_HAS_NORMAL_MAP         0x08
#define VARIANT_HAS_AO_MAP             0x10
#define VARIANT_HAS_EMISSIVE_MAP       0x20
#define VARIANT_HAS_CLEARCOAT          0x40
#define VARIANT_HAS_SHEEN              0x80

// ✅ 好：仅保留高频使用的变体
#define VARIANT_HAS_SKINNING           0x01  // 常用
#define VARIANT_HAS_SHADOW             0x02  // 常用
#define VARIANT_HAS_VERTEX_COLOR       0x04  // 常用
// 其他特性使用 Uniform 控制（运行时分支）
```

---

## 14. 平台特定优化

### 14.1 移动端（Mali GPU）

```glsl
// Mali GPU 特性
precision mediump float;  // 默认中精度

// 避免 discard（会破坏 Early-Z）
// ❌ 不好
if (alpha < 0.5) discard;

// ✅ 好：使用 Alpha-to-Coverage
layout(location = 0) out vec4 fragColor;
void main() {
    fragColor = vec4(color, alpha);
    // 在 Render State 中启用 Alpha-to-Coverage
}

// 使用 framebuffer_fetch 优化混合（避免 Bandwidth）
#extension GL_EXT_shader_framebuffer_fetch : enable
inout vec4 fragColor;

void main() {
    vec4 srcColor = computeColor();
    // 直接在 Shader 中混合
    fragColor = mix(fragColor, srcColor, srcColor.a);
}
```

### 14.2 桌面端（NVIDIA）

```glsl
// NVIDIA 优化技巧

// 1. 使用 Warp Shuffle（更快的线程间通信）
#extension GL_NV_shader_thread_shuffle : enable

float neighborValue = shuffleXorNV(value, 1, 32);  // 获取相邻线程的值

// 2. 使用 Bindless Textures
#extension GL_NV_bindless_texture : enable

layout(bindless_sampler) uniform sampler2D textures[1000];

void main() {
    vec4 color = texture(textures[materialID], uv);
}

// 3. Conservative Rasterization（用于 Voxelization）
#extension GL_NV_conservative_raster : enable

layout(early_fragment_tests) in;  // 提前深度测试
```

### 14.3 WebGL

```glsl
// WebGL 限制和优化

precision mediump float;  // 必须声明精度

// 避免使用扩展（兼容性差）
// #extension GL_OES_standard_derivatives : enable  // 尽量避免

// 减少 Varying 变量数量（最多 8-16 个）
in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;
// 尽量不要更多

// 避免动态索引
// ❌ 不好
uniform vec3 colors[10];
vec3 color = colors[index];  // 可能很慢

// ✅ 好：展开
vec3 color = vec3(0);
if (index == 0) color = colors[0];
else if (index == 1) color = colors[1];
// ...
```

---

## 15. Shader 性能测试

### 15.1 微基准测试

```cpp
// Shader 性能微基准
class ShaderBenchmark {
public:
    struct Result {
        std::string name;
        double timeMs;
        uint32_t instructions;
    };

    void benchmarkShader(const std::string& name, Material* material) {
        auto view = engine->createView();
        view->setViewport({0, 0, 1920, 1080});

        // 渲染 100 帧
        std::vector<double> times;
        for (int i = 0; i < 100; ++i) {
            auto start = std::chrono::high_resolution_clock::now();

            renderer->beginFrame(swapChain);
            renderer->render(view);
            renderer->endFrame();

            auto end = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(end - start).count();
            times.push_back(ms);
        }

        // 计算中位数
        std::sort(times.begin(), times.end());
        double median = times[times.size() / 2];

        results.push_back({name, median, 0});
    }

    void printResults() {
        printf("Shader Benchmark Results:\n");
        printf("%-30s %10s\n", "Name", "Time (ms)");
        printf("%-30s %10s\n", "----", "---------");
        for (const auto& r : results) {
            printf("%-30s %10.2f\n", r.name.c_str(), r.timeMs);
        }
    }

private:
    std::vector<Result> results;
};

// 使用
ShaderBenchmark bench;
bench.benchmarkShader("PBR_Original", pbrOriginal);
bench.benchmarkShader("PBR_Optimized", pbrOptimized);
bench.printResults();

// 输出：
// Shader Benchmark Results:
// Name                           Time (ms)
// ----                           ---------
// PBR_Original                       15.67
// PBR_Optimized                       5.23
```

### 15.2 A/B 测试

```cpp
// A/B 测试框架
class ShaderABTest {
public:
    void addVariant(const std::string& name, Material* material) {
        variants.push_back({name, material});
    }

    void run(int frameCount = 1000) {
        for (auto& variant : variants) {
            printf("Testing: %s\n", variant.name.c_str());

            double totalTime = 0;
            int frameCounter = 0;

            for (int i = 0; i < frameCount; ++i) {
                // 设置材质
                setMaterial(variant.material);

                // 渲染
                auto start = std::chrono::high_resolution_clock::now();
                renderFrame();
                auto end = std::chrono::high_resolution_clock::now();

                double ms = std::chrono::duration<double, std::milli>(end - start).count();
                totalTime += ms;
                frameCounter++;
            }

            double avgTime = totalTime / frameCounter;
            variant.avgTime = avgTime;
            printf("  Avg: %.2f ms (%.1f FPS)\n", avgTime, 1000.0 / avgTime);
        }

        // 对比
        if (variants.size() >= 2) {
            double baseline = variants[0].avgTime;
            printf("\nComparison to baseline (%s):\n", variants[0].name.c_str());
            for (size_t i = 1; i < variants.size(); ++i) {
                double improvement = (baseline - variants[i].avgTime) / baseline * 100.0;
                printf("  %s: %.1f%% %s\n", variants[i].name.c_str(),
                       std::abs(improvement),
                       improvement > 0 ? "faster" : "slower");
            }
        }
    }

private:
    struct Variant {
        std::string name;
        Material* material;
        double avgTime = 0;
    };

    std::vector<Variant> variants;
};

// 使用
ShaderABTest test;
test.addVariant("Baseline", materialBaseline);
test.addVariant("Optimized_v1", materialOptV1);
test.addVariant("Optimized_v2", materialOptV2);
test.run(1000);

// 输出：
// Testing: Baseline
//   Avg: 16.67 ms (60.0 FPS)
// Testing: Optimized_v1
//   Avg: 11.23 ms (89.0 FPS)
// Testing: Optimized_v2
//   Avg: 8.33 ms (120.1 FPS)
//
// Comparison to baseline (Baseline):
//   Optimized_v1: 32.6% faster
//   Optimized_v2: 50.0% faster
```

---

## 16. 最佳实践总结

1. **使用性能分析工具**：RenderDoc、Mali OC、Nsight
2. **减少 ALU**：避免 pow/exp/log，使用查找表
3. **优化纹理**：减少采样次数，打包通道
4. **精度优化**：移动端使用 mediump/lowp
5. **消除分支**：使用 mix、展开循环
6. **寄存器优化**：减少临时变量，数据打包
7. **预计算**：Vertex Shader 预计算，查找表
8. **预编译**：离线编译 Shader，减少变体

---

## 17. 常见问题

### Q1: 如何判断 Shader 是否是瓶颈？

**A**: 
- 降低分辨率，帧率显著提升 → Fragment Shader bound
- 减少三角形数量，帧率显著提升 → Vertex Shader bound
- 使用 GPU Profiler 查看 Shader 占用时间

### Q2: mediump 会导致精度问题吗？

**A**: 
- 大部分光照计算 mediump 足够
- 避免大数减法、深度计算使用 mediump
- 测试时注意检查视觉差异

### Q3: 如何优化透明物体 Shader？

**A**: 
- 按距离排序（后向前）
- 使用 Alpha Test 而非 Blend（能提前丢弃）
- 简化透明物体的光照计算

---

## 18. 相关文档

- [optimization/05-mobile-optimization.md](./05-mobile-optimization.md)
- [optimization/03-drawcall-reduction.md](./03-drawcall-reduction.md)
- [shaders/01-material-lang-reference.md](../shaders/01-material-lang-reference.md)

---

## 19. 总结

Shader 优化关键技术：

1. **性能分析**：使用工具定位瓶颈（RenderDoc、Mali OC、Nsight）
2. **ALU 优化**：避免昂贵操作（pow、exp、log），使用查找表和近似
3. **带宽优化**：减少纹理采样，打包数据，使用压缩纹理
4. **精度优化**：移动端使用 mediump/lowp，桌面使用 highp
5. **分支优化**：消除动态分支，使用 mix 和展开
6. **寄存器优化**：减少临时变量，数据打包
7. **调试技术**：可视化调试、性能计数器、热重载
8. **高级优化**：Half-Resolution、TAA、Async Compute、Wave Intrinsics
9. **变体管理**：Feature Grouping、Specialization Constants
10. **平台优化**：针对 Mali、NVIDIA、WebGL 的特定优化

通过这些技术，可实现：
- **Shader 指令减少 30-70%**
- **帧率提升 50-200%**
- **移动设备功耗降低 20-40%**
- **变体数量减少 80-90%**
- **编译时间降低 60-80%**

### 优化流程

1. **测量（Measure）**：使用 GPU Profiler 定位瓶颈
2. **分析（Analyze）**：使用 Mali OC / Nsight 查看指令数
3. **优化（Optimize）**：应用本文档技术
4. **验证（Verify）**：A/B 测试，确保性能提升
5. **迭代（Iterate）**：持续优化，监控回归

Shader 优化需要结合工具分析和实际测试，持续迭代优化。
