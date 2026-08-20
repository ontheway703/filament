# 优化案例分析

## 📖 概述

通过真实案例学习性能优化。本文档深入分析多个 Filament 优化案例，涵盖移动游戏、桌面渲染、VR 应用等场景，展示完整的问题分析、优化实施和效果验证流程。

**优化目标**:
- 学习实战优化经验
- 避免常见性能陷阱
- 掌握系统化分析方法
- 量化优化效果并总结规律

**案例覆盖**:
- 移动 3D 游戏性能优化
- 桌面产品可视化渲染
- VR 应用帧率优化
- 开放世界场景渲染
- 移动 AR 应用优化

---

## 案例 1: 移动 ARPG 游戏优化

### 1.1 项目背景

**游戏类型**: 动作角色扮演游戏（ARPG）
**目标平台**: Android (Mali-G76)
**目标帧率**: 60 FPS (16.7 ms/frame)
**初始性能**: 30-40 FPS (25-33 ms/frame)

### 1.2 问题分析

**使用工具**: Mali Offline Compiler, RenderDoc, Android Profiler

**性能瓶颈识别**:

```
Frame Time Breakdown (33 ms):
  CPU Time: 12 ms
    - Logic Update: 3 ms
    - Rendering Submit: 9 ms ← 瓶颈
  GPU Time: 21 ms ← 主要瓶颈
    - Vertex Shader: 3 ms
    - Fragment Shader: 16 ms ← 最严重
    - Post Processing: 2 ms
```

**详细分析**:

1. **Fragment Shader 过重** (16 ms):
   - 纹理采样过多（每像素 5-7 次）
   - 复杂的 PBR 光照计算
   - 高精度计算（precision highp float）

2. **Draw Call 过多** (9 ms CPU):
   - 2000+ Draw Calls
   - 大量小物体（树木、石头、草）
   - 频繁的状态切换（Shader、Texture）

3. **内存占用高** (1.2 GB):
   - 未压缩纹理占用 800 MB
   - 大量重复网格数据

### 1.3 优化方案

#### 优化 1: Fragment Shader 优化

**Before**:

```glsl
precision highp float;  // ❌ 全局高精度

uniform sampler2D baseColorMap;
uniform sampler2D normalMap;
uniform sampler2D metallicRoughnessMap;
uniform sampler2D aoMap;
uniform sampler2D emissiveMap;

void main() {
    vec3 albedo = texture(baseColorMap, uv).rgb;
    vec3 normal = texture(normalMap, uv).xyz * 2.0 - 1.0;
    vec2 metalRough = texture(metallicRoughnessMap, uv).rg;
    float ao = texture(aoMap, uv).r;
    vec3 emissive = texture(emissiveMap, uv).rgb;
    
    // 复杂 PBR 计算
    vec3 lighting = computePBR(albedo, normal, metalRough.r, metalRough.g);
    
    fragColor = vec4(lighting * ao + emissive, 1.0);
}

// Mali OC 输出: 542 ALU instructions, 24 texture cycles
```

**After**:

```glsl
precision mediump float;  // ✅ 降低默认精度

uniform sampler2D baseColorMetallicMap;  // RGB: Albedo, A: Metallic
uniform sampler2D normalRoughnessMap;    // RGB: Normal, A: Roughness
uniform sampler2D aoEmissiveMap;         // R: AO, GB: Emissive

void main() {
    vec4 baseMetallic = texture(baseColorMetallicMap, uv);
    vec3 albedo = baseMetallic.rgb;
    mediump float metallic = baseMetallic.a;
    
    vec4 normalRough = texture(normalRoughnessMap, uv);
    mediump vec3 normal = normalRough.xyz * 2.0 - 1.0;
    mediump float roughness = normalRough.a;
    
    vec3 aoEmissive = texture(aoEmissiveMap, uv).rgb;
    mediump float ao = aoEmissive.r;
    mediump vec3 emissive = aoEmissive.gb;
    
    // 简化 PBR（使用查找表）
    mediump vec3 lighting = computePBR_Fast(albedo, normal, metallic, roughness);
    
    fragColor = vec4(lighting * ao + emissive, 1.0);
}

// Mali OC 输出: 180 ALU instructions, 12 texture cycles
// 改善: 67% ALU reduction, 50% texture reduction
```

#### 优化 2: Draw Call 批处理

**Before**:

```cpp
// 逐个渲染物体
for (const auto& tree : trees) {  // 500 trees
    renderer->render(tree);
}
for (const auto& rock : rocks) {  // 300 rocks
    renderer->render(rock);
}
// ... 其他物体

// Draw Calls: 2000+
```

**After**:

```cpp
// GPU Instancing
InstanceManager instanceMgr(*engine);

// 树木 Instancing
auto treeGroup = instanceMgr.createInstancedGroup(
    treeMesh->vertexBuffer, treeMesh->indexBuffer,
    treeMaterial, 500
);

std::vector<InstanceData> treeInstances;
for (const auto& tree : trees) {
    treeInstances.push_back({tree.transform, tree.color});
}
instanceMgr.updateInstances(treeGroup, treeInstances);

// 石头 Instancing
auto rockGroup = instanceMgr.createInstancedGroup(
    rockMesh->vertexBuffer, rockMesh->indexBuffer,
    rockMaterial, 300
);
// ...

// Draw Calls: 150 (93% reduction)
```

#### 优化 3: 纹理压缩

**Before**:

```
Textures (Uncompressed RGBA8):
  Albedo: 1024×1024 × 4 bytes = 4 MB
  Normal: 1024×1024 × 4 bytes = 4 MB
  MetallicRoughness: 1024×1024 × 4 bytes = 4 MB
  AO: 1024×1024 × 4 bytes = 4 MB
  Emissive: 1024×1024 × 4 bytes = 4 MB
  
  Total per material: 20 MB
  50 materials: 1 GB
```

**After**:

```
Textures (ASTC 4x4 + Channel Packing):
  BaseColorMetallic: 1024×1024 ASTC 4x4 = 512 KB
  NormalRoughness: 1024×1024 ASTC 4x4 = 512 KB
  AOEmissive: 512×512 ASTC 4x4 = 128 KB (降低分辨率)
  
  Total per material: 1.15 MB
  50 materials: 57.5 MB (94% reduction)
```

#### 优化 4: LOD 系统

```cpp
class LODManager {
public:
    void updateLOD(const math::float3& cameraPos) {
        for (auto& obj : objects) {
            float distance = length(obj.position - cameraPos);
            
            if (distance < 10.0f) {
                obj.setLOD(0);  // 高细节
            } else if (distance < 30.0f) {
                obj.setLOD(1);  // 中细节 (50% triangles)
            } else if (distance < 100.0f) {
                obj.setLOD(2);  // 低细节 (25% triangles)
            } else {
                obj.setVisible(false);  // 剔除
            }
        }
    }
};

// Triangle Reduction:
// Before: 5M triangles
// After: 1.2M triangles (76% reduction)
```

### 1.4 优化结果

| 指标 | Before | After | 改善 |
|------|--------|-------|------|
| 帧率 | 30-40 FPS | 58-60 FPS | +75% |
| Frame Time | 25-33 ms | 16-17 ms | 49% faster |
| GPU Time | 21 ms | 9 ms | 57% faster |
| CPU Time | 12 ms | 6 ms | 50% faster |
| Draw Calls | 2000+ | 150 | 93% reduction |
| Memory | 1.2 GB | 320 MB | 73% reduction |
| Triangle Count | 5M | 1.2M | 76% reduction |
| Texture Samples/Pixel | 5-7 | 3 | 50% reduction |

**用户体验改善**:
- 流畅度显著提升（30 FPS → 60 FPS）
- 发热降低 ~30%
- 电池续航延长 ~40%

---

## 案例 2: 桌面产品可视化渲染

### 2.1 项目背景

**应用类型**: 汽车配置器（高品质渲染）
**目标平台**: Desktop (NVIDIA RTX 3060)
**目标**: 4K 分辨率 @ 60 FPS，真实感渲染
**初始性能**: 1080p @ 45 FPS

### 2.2 问题分析

**性能瓶颈**:

```
Frame Time (22 ms @ 1080p):
  Shadow Map Rendering: 8 ms
  Main Pass: 10 ms
  Post Processing (SSAO, Bloom): 4 ms
```

**详细分析**:

1. **Shadow Map 过大**: 4096×4096 (16 MB)
2. **SSAO 全分辨率**: 昂贵的屏幕空间计算
3. **材质复杂**: 多层车漆、清漆 PBR

### 2.3 优化方案

#### 优化 1: Cascaded Shadow Maps

**Before**:

```cpp
// 单个大 Shadow Map
ShadowMap* shadow = ShadowMap::create(4096, 4096);

// Render Time: 8 ms
```

**After**:

```cpp
// 级联 Shadow Maps
struct CascadedShadowMap {
    ShadowMap* cascades[4];
    // Cascade 0: 2048×2048 (近距离)
    // Cascade 1: 1024×1024
    // Cascade 2: 512×512
    // Cascade 3: 512×512 (远距离)
};

void renderShadows() {
    for (int i = 0; i < 4; ++i) {
        renderCascade(i);  // 仅渲染可见物体
    }
}

// Render Time: 3.5 ms (56% faster)
```

#### 优化 2: Half-Resolution SSAO

**Before**:

```cpp
// 全分辨率 SSAO (1920×1080)
renderSSAO(mainRT, 1920, 1080);

// Time: 2.8 ms
```

**After**:

```cpp
// 半分辨率 SSAO (960×540) + 双线性上采样
RenderTarget* halfResRT = createRenderTarget(960, 540);

renderSSAO(halfResRT, 960, 540);
upsampleAndBlur(halfResRT, mainRT);

// Time: 0.8 ms (71% faster)
```

#### 优化 3: 优化车漆材质

**Before** (标准 PBR):

```glsl
// 多层车漆 Shader
vec3 clearCoat = computeClearCoat(normal, view, roughness);
vec3 basePaint = computePBR(albedo, normal, metallic, roughness);
vec3 flakes = computeFlakes(uv, normal);  // 金属颗粒

fragColor = vec4(clearCoat + basePaint + flakes, 1.0);

// Instruction Count: 450
```

**After** (预计算优化):

```glsl
// 使用查找表（LUT）优化
uniform sampler2D clearCoatLUT;
uniform sampler2D flakesTexture;  // 预计算金属颗粒

vec2 clearCoatUV = vec2(NdotV, roughness);
vec3 clearCoat = texture(clearCoatLUT, clearCoatUV).rgb;

vec3 basePaint = computePBR_Fast(albedo, normal, metallic, roughness);
vec3 flakes = texture(flakesTexture, uv * 100.0).rgb;  // Tiled

fragColor = vec4(clearCoat + basePaint + flakes, 1.0);

// Instruction Count: 180 (60% reduction)
```

### 2.4 优化结果

| 指标 | Before (1080p) | After (1080p) | After (4K) |
|------|----------------|---------------|------------|
| 帧率 | 45 FPS | 120 FPS | 65 FPS |
| Frame Time | 22 ms | 8.3 ms | 15.4 ms |
| Shadow Rendering | 8 ms | 3.5 ms | 5.2 ms |
| SSAO | 2.8 ms | 0.8 ms | 1.5 ms |
| Main Pass | 10 ms | 3.5 ms | 8.0 ms |

**视觉质量**: 无明显下降（Half-Res SSAO 上采样后难以察觉）

---

## 案例 3: VR 应用帧率优化

### 3.1 项目背景

**应用类型**: VR 房屋浏览
**目标平台**: Meta Quest 2 (Adreno 650)
**目标帧率**: 72 FPS (13.9 ms/frame) × 2 eyes
**初始性能**: 45 FPS (无法通过 VR 认证)

### 3.2 问题分析

**VR 特殊挑战**:
- 需要渲染两个视角（左眼 + 右眼）
- 延迟敏感（Motion-to-Photon < 20 ms）
- 分辨率高（单眼 1832×1920）

**性能瓶颈**:

```
Frame Time (22 ms × 2 = 44 ms total):
  Left Eye: 22 ms
    - Draw Calls: 1500 (CPU Bound)
    - Fragment Shader: 15 ms (GPU Bound)
  Right Eye: 22 ms
    - 重复渲染相同场景
```

### 3.3 优化方案

#### 优化 1: Multiview Rendering

**Before**:

```cpp
// 分别渲染两只眼睛
renderer->render(leftEyeView);  // 22 ms
renderer->render(rightEyeView); // 22 ms

// Total: 44 ms (22 FPS)
```

**After**:

```cpp
// Multiview Rendering（单 Pass 渲染双眼）
View* multiviewView = View::Builder()
    .stereoscopicOptions({
        .enabled = true,
        .eyeCount = 2
    })
    .build(*engine);

renderer->render(multiviewView);  // 14 ms (单 Pass)

// Total: 14 ms (71 FPS)
// 改善: 68% faster
```

**Shader 改动**:

```glsl
#extension GL_OVR_multiview2 : enable

layout(num_views = 2) in;

uniform mat4 viewProjection[2];  // 左右眼 VP 矩阵

void main() {
    int eye = int(gl_ViewID_OVR);
    gl_Position = viewProjection[eye] * vec4(position, 1.0);
    // ...
}
```

#### 优化 2: Foveated Rendering

**原理**: 中心视野高分辨率，边缘降低分辨率（人眼特性）。

```cpp
class FoveatedRenderer {
public:
    void render(View* view) {
        // 中心区域（50%）: 全分辨率
        RenderTarget* centerRT = createRT(1832 * 0.7, 1920 * 0.7);
        
        // 边缘区域（50%）: 50% 分辨率
        RenderTarget* peripheralRT = createRT(1832 * 0.5, 1920 * 0.5);
        
        // 渲染
        renderer->render(view, centerRT, Rect{center});
        renderer->render(view, peripheralRT, Rect{peripheral});
        
        // 合成
        composite(centerRT, peripheralRT, finalRT);
    }
};

// Pixel Shading Reduction: 30-40%
```

#### 优化 3: Fixed Foveated Rendering (FFR)

**Vulkan 扩展**:

```cpp
// 启用 FFR（Adreno GPU 支持）
vkCmdSetFragmentShadingRateKHR(cmdBuffer, &shadingRate);

// 中心: 1×1 (全分辨率)
// 中间: 2×2 (每 2×2 像素共享一次着色)
// 边缘: 4×4 (每 4×4 像素共享一次着色)

// Fragment Shading Reduction: 50%
// GPU Time: 15 ms → 8 ms
```

#### 优化 4: Occlusion Culling

```cpp
class VROcclusionCuller {
public:
    void performOcclusionQuery() {
        // 使用上一帧的深度缓冲
        for (auto& obj : objects) {
            if (isOccluded(obj, lastFrameDepth)) {
                obj.setVisible(false);
            }
        }
    }
};

// Draw Call Reduction: 1500 → 800 (47%)
```

### 3.4 优化结果

| 指标 | Before | After | 改善 |
|------|--------|-------|------|
| 帧率 | 45 FPS | 72 FPS | +60% |
| 单眼 Frame Time | 22 ms | 13.8 ms | 37% faster |
| Draw Calls | 1500 × 2 | 800 × 1 (Multiview) | 73% reduction |
| Fragment Shading | 100% pixels | 50% pixels (FFR) | 50% reduction |
| Motion-to-Photon Latency | 60 ms | 18 ms | 70% reduction |

**VR 认证**: ✅ 通过 Meta Quest Store 认证

---

## 案例 4: 开放世界场景渲染

### 4.1 项目背景

**应用类型**: 开放世界探索游戏
**目标平台**: PC (Mid-Range GPU)
**场景规模**: 10 km² 地图，100K+ 可见物体
**初始性能**: 20 FPS (CPU Bound)

### 4.2 问题分析

**挑战**:

```
100,000 objects in scene:
  Frustum Culling (CPU): 5 ms
  Draw Call Submit: 50 ms ← 主要瓶颈
  GPU Rendering: 15 ms

Total: 70 ms (14 FPS)
```

**瓶颈**: CPU 提交 Draw Call 的开销远超 GPU 渲染时间。

### 4.3 优化方案

#### 优化 1: GPU-Driven Rendering

**Multi-Draw Indirect + Compute Shader Culling**:

```cpp
// Compute Shader 进行视锥剔除
class GPUCulling {
public:
    void cullObjects(const std::vector<Object>& objects, const Frustum& frustum) {
        // 上传物体数据到 GPU
        uploadObjectData(objects);
        
        // Compute Shader 执行剔除
        computeShader->dispatch(objects.size() / 256, 1, 1);
        
        // 生成 Draw Commands（GPU 端）
        // 结果存储在 indirectDrawBuffer
    }
    
    void render() {
        // Multi-Draw Indirect (1 Draw Call for all objects)
        glMultiDrawElementsIndirect(
            GL_TRIANGLES, GL_UNSIGNED_INT,
            indirectDrawBuffer, drawCount, 0
        );
    }
};

// CPU Time: 50 ms → 0.5 ms (99% reduction)
```

**Compute Shader**:

```glsl
// frustum_cull_and_lod.comp
layout(local_size_x = 256) in;

struct ObjectData {
    mat4 modelMatrix;
    vec4 boundingBox[2];  // min, max
    uint meshLOD[4];      // LOD 0/1/2/3 mesh IDs
};

layout(std430, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
};

layout(std430, binding = 1) writeonly buffer DrawCommandBuffer {
    DrawElementsIndirectCommand commands[];
};

layout(std430, binding = 2) buffer DrawCountBuffer {
    uint drawCount;
};

uniform mat4 viewProjection;
uniform vec4 frustumPlanes[6];
uniform vec3 cameraPosition;

void main() {
    uint objectID = gl_GlobalInvocationID.x;
    ObjectData obj = objects[objectID];
    
    // Frustum Culling
    if (!isVisible(obj.boundingBox, frustumPlanes)) {
        return;
    }
    
    // LOD Selection
    float distance = length(obj.modelMatrix[3].xyz - cameraPosition);
    uint lodLevel = selectLOD(distance);
    uint meshID = obj.meshLOD[lodLevel];
    
    // Add Draw Command
    uint drawIndex = atomicAdd(drawCount, 1);
    commands[drawIndex] = createDrawCommand(meshID, objectID);
}

// GPU Time: 0.2 ms (Compute) + 15 ms (Rendering)
```

#### 优化 2: Virtual Texturing

**问题**: 开放世界需要大量地形纹理（数 GB）。

**方案**: 仅加载当前可见区域的纹理（Virtual Texture / Mega Texture）。

```cpp
class VirtualTextureManager {
public:
    // 物理纹理缓存（256 MB）
    Texture* physicalTexture;  // 4096×4096 Atlas
    
    // 虚拟纹理（理论上无限大）
    struct VirtualTexture {
        uint32_t width, height;  // e.g., 65536×65536
        uint32_t tileSize;       // e.g., 256×256
    };
    
    void update(const math::float3& cameraPos) {
        // 查询可见 Tiles
        auto visibleTiles = queryVisibleTiles(cameraPos);
        
        // 加载 Tiles 到物理纹理
        for (auto& tile : visibleTiles) {
            if (!isLoaded(tile)) {
                loadTile(tile);
            }
        }
        
        // 更新 Indirection Texture（虚拟 → 物理映射）
        updateIndirection();
    }
};

// Memory: 8 GB (virtual) → 256 MB (physical)
// 改善: 96.8% reduction
```

#### 优化 3: Hierarchical Z-Buffer Occlusion

```cpp
// 生成 Hi-Z Mipmap（从前一帧深度缓冲）
void generateHiZ(Texture* depthBuffer) {
    // Mip 0: 原始深度
    // Mip 1: 2×2 max depth
    // Mip 2: 4×4 max depth
    // ...
}

// Occlusion Culling
bool isOccluded(const Box& box, Texture* hiZ) {
    // 投影 Box 到屏幕空间
    Rect screenRect = projectToScreen(box);
    
    // 选择合适的 Mip Level
    int mipLevel = computeMipLevel(screenRect);
    
    // 采样 Hi-Z
    float occluderDepth = textureLod(hiZ, screenRect.center, mipLevel).r;
    
    // 比较深度
    return box.minDepth > occluderDepth;
}

// Culled Objects: ~30% (in dense scenes)
```

### 4.4 优化结果

| 指标 | Before | After | 改善 |
|------|--------|-------|------|
| 帧率 | 14 FPS | 60 FPS | +329% |
| CPU Time | 55 ms | 2 ms | 96% reduction |
| Draw Calls | 100K | 1 (Multi-Draw Indirect) | 99.999% reduction |
| Frustum Culling | 5 ms (CPU) | 0.2 ms (GPU) | 96% faster |
| GPU Time | 15 ms | 15 ms | 持平 |
| Texture Memory | 8 GB (virtual) | 256 MB (physical) | 96.8% reduction |

---

## 案例 5: 移动 AR 应用优化

### 5.1 项目背景

**应用类型**: AR 家具放置（ARCore / ARKit）
**目标平台**: 中端手机（Adreno 618）
**目标帧率**: 30 FPS（AR 相机输出）
**初始性能**: 15-20 FPS

### 5.2 问题分析

**AR 特殊挑战**:
- 需要处理相机输入（AR 背景）
- 实时环境光估计和阴影
- 平面检测和跟踪开销

**性能瓶颈**:

```
Frame Time (50-66 ms):
  AR Tracking: 10 ms (ARCore/ARKit)
  Rendering: 40 ms
    - Shadow Mapping: 15 ms
    - Main Pass: 20 ms (Fragment Bound)
    - Compositing: 5 ms
```

### 5.3 优化方案

#### 优化 1: Soft Shadow 优化

**Before** (PCF Shadow):

```glsl
float shadow = 0.0;
for (int y = -2; y <= 2; ++y) {
    for (int x = -2; x <= 2; ++x) {
        vec2 offset = vec2(x, y) * texelSize;
        shadow += texture(shadowMap, uv + offset).r > depth ? 1.0 : 0.0;
    }
}
shadow /= 25.0;  // 25 samples

// Time: 15 ms
```

**After** (预计算 Shadow + Blur):

```glsl
// 降低 Shadow Map 分辨率 + 后期模糊
// Shadow Map: 2048×2048 → 512×512

// Fragment Shader:
float shadow = texture(shadowMap, uv).r > depth ? 1.0 : 0.0;

// 后处理: Gaussian Blur (5×5, separable)
// Time: 3 ms (80% faster)
```

#### 优化 2: 动态分辨率

**根据 GPU 负载动态调整渲染分辨率**:

```cpp
class DynamicResolution {
public:
    void update(float frameTime, float targetTime = 33.3f) {
        if (frameTime > targetTime * 1.1f) {
            // 降低分辨率
            mScale = std::max(0.5f, mScale - 0.05f);
        } else if (frameTime < targetTime * 0.9f) {
            // 提高分辨率
            mScale = std::min(1.0f, mScale + 0.02f);
        }
        
        updateRenderTarget();
    }
    
private:
    float mScale = 1.0f;  // 0.5 ~ 1.0
    
    void updateRenderTarget() {
        uint32_t width = uint32_t(mNativeWidth * mScale);
        uint32_t height = uint32_t(mNativeHeight * mScale);
        
        // 重新创建 Render Target
        mRenderTarget = createRenderTarget(width, height);
    }
};

// 帧率稳定性: 15-20 FPS (不稳定) → 28-30 FPS (稳定)
```

#### 优化 3: 环境光烘焙

**Before** (实时环境光估计):

```cpp
// 每帧计算环境光
vec3 ambientLight = estimateEnvironmentLight(arCamera);

// Time: 3 ms
```

**After** (预烘焙 + 轻量级更新):

```cpp
// 使用 ARCore/ARKit 提供的环境光方向和强度
vec3 ambientLight = arLightEstimate.ambient;
vec3 mainLightDir = arLightEstimate.direction;
float mainLightIntensity = arLightEstimate.intensity;

// 结合预烘焙的 Indirect Lighting
vec3 indirectLight = texture(irradianceMap, normal).rgb;

vec3 totalAmbient = ambientLight * indirectLight;

// Time: 0.5 ms (83% faster)
```

#### 优化 4: 减少 Overdraw

**AR 场景 Overdraw 分析**:

```
Overdraw 热力图:
  - AR 背景（相机图像）: 1x
  - 家具模型: 2-3x
  - 阴影: +1x
  - 平均 Overdraw: 3-4x ← 浪费
```

**优化**:

```cpp
// 1. 渲染顺序优化（前向后）
void renderAR() {
    // 先渲染 AR 背景（全屏 Quad）
    renderARBackground();
    
    // 启用 Early-Z
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    
    // 前向后渲染不透明物体
    sortFrontToBack(opaqueObjects);
    renderOpaque(opaqueObjects);
    
    // 最后渲染透明物体（后向前）
    sortBackToFront(transparentObjects);
    renderTransparent(transparentObjects);
}

// 2. 降低 Fragment Shader 复杂度（见案例 1）
```

**Overdraw 改善**: 3-4x → 1.5-2x

### 5.4 优化结果

| 指标 | Before | After | 改善 |
|------|--------|-------|------|
| 帧率 | 15-20 FPS | 28-30 FPS | +75% |
| Frame Time | 50-66 ms | 33-36 ms | 45% faster |
| Shadow Rendering | 15 ms | 3 ms | 80% faster |
| Main Pass | 20 ms | 12 ms | 40% faster |
| Overdraw | 3-4x | 1.5-2x | 50% reduction |
| 分辨率 | 固定 1080p | 动态 540p-1080p | 稳定性提升 |

**用户体验**: AR 跟踪更流畅，物体放置响应速度提升。

---

## 总结：优化方法论

### 1. 系统化分析流程

```
1. 测量基准 (Baseline)
   - 使用 Profiler（RenderDoc, Mali OC, Nsight, XCode Instruments）
   - 记录关键指标（FPS, Frame Time, Draw Calls, Memory）

2. 识别瓶颈 (Bottleneck Identification)
   - CPU Bound: Draw Call 提交、逻辑更新
   - GPU Bound: Vertex Shader, Fragment Shader, Bandwidth
   - Memory Bound: 纹理加载、Buffer 上传

3. 针对性优化 (Targeted Optimization)
   - CPU: 批处理、Instancing, Async, Multi-threading
   - GPU: Shader 优化、LOD、Culling
   - Memory: 压缩、流式加载、Virtual Texturing

4. 验证效果 (Validation)
   - A/B 测试
   - 回归测试（确保无视觉质量下降）
   - 量化改进（性能提升百分比）

5. 持续监控 (Continuous Monitoring)
   - CI/CD 集成性能测试
   - 防止性能回归
   - 定期 Profile
```

### 2. 常见优化技术对照表

| 瓶颈类型 | 优化技术 | 效果 | 适用场景 |
|---------|---------|------|----------|
| Draw Call 过多 | Static Batching | 80-95% 减少 | 静态物体 |
| | GPU Instancing | 90-99% 减少 | 大量相同物体 |
| | Multi-Draw Indirect | 99% 减少 | 海量物体 |
| Fragment Shader | 降低精度（mediump） | 30-50% 提速 | 移动端 |
| | 纹理打包 | 50% Texture 减少 | 所有平台 |
| | 使用 LUT | 60-80% ALU 减少 | 复杂计算 |
| Vertex Shader | LOD 系统 | 50-90% 三角形减少 | 大场景 |
| | Vertex Cache 优化 | 30-50% 提速 | 所有网格 |
| Shadow | Cascaded Shadow Maps | 40-60% 提速 | 大场景 |
| | 降低分辨率 + 模糊 | 70-80% 提速 | 移动端、VR |
| Overdraw | 前向后排序 | 30-50% 减少 | 复杂场景 |
| | Early-Z | 20-40% 减少 | 所有平台 |
| Memory | 纹理压缩（ASTC, BC7） | 75-95% 减少 | 所有平台 |
| | Virtual Texturing | 90-99% 减少 | 开放世界 |
| VR 特定 | Multiview Rendering | 40-70% 提速 | VR |
| | Foveated Rendering | 30-50% 提速 | VR |

### 3. 关键经验教训

1. **测量先于优化**: 不测量就优化是盲目的，可能优化错方向。
2. **80/20 原则**: 20% 的瓶颈导致 80% 的性能问题，优先解决大瓶颈。
3. **平衡权衡**: 性能、质量、复杂度需要平衡。
4. **平台特性**: 移动端和桌面优化策略不同（精度、压缩格式、功耗）。
5. **持续迭代**: 性能优化是持续过程，需要防止回归。
6. **工具至上**: 学会使用 Profiler 是最重要的技能。

---

## 相关文档

- [optimization/01-texture-optimization.md](./01-texture-optimization.md)
- [optimization/02-mesh-optimization.md](./02-mesh-optimization.md)
- [optimization/03-drawcall-reduction.md](./03-drawcall-reduction.md)
- [optimization/04-memory-management.md](./04-memory-management.md)
- [optimization/05-mobile-optimization.md](./05-mobile-optimization.md)
- [optimization/06-shader-optimization.md](./06-shader-optimization.md)
- [optimization/07-cpu-gpu-sync.md](./07-cpu-gpu-sync.md)
- [optimization/08-asset-pipeline-opt.md](./08-asset-pipeline-opt.md)
