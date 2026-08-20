# 颜色空间与色彩管理详解

本文档详细说明计算机图形学中的颜色空间、Gamma校正、色彩管理流程以及HDR渲染等核心概念。

---

## 目录

1. [颜色空间基础](#颜色空间基础)
2. [sRGB vs Linear空间](#srgb-vs-linear空间)
3. [Gamma校正原理](#gamma校正原理)
4. [色彩管理流程](#色彩管理流程)
5. [HDR与色调映射](#hdr与色调映射)
6. [颜色编码格式](#颜色编码格式)
7. [Filament色彩管理](#filament色彩管理)

---

## 颜色空间基础

### 什么是颜色空间

**定义**: 颜色空间是一种数学模型,用特定方式表示颜色的范围和编码方式。

**常见颜色空间**:
- **RGB**: 用红、绿、蓝三原色表示(显示器、GPU)
- **sRGB**: 标准RGB,带Gamma编码(Web、纹理)
- **Linear RGB**: 线性RGB(物理计算)
- **HSV/HSL**: 色相、饱和度、亮度(艺术家友好)
- **CIE XYZ**: 基于人眼感知的绝对颜色空间
- **ACES**: 电影工业标准

### 为什么需要不同颜色空间

```
输入(纹理)        计算(Shader)      输出(显示)
   sRGB    →    Linear RGB    →    sRGB (Gamma校正)
   [0-255]      [0.0-1.0]          [0-255]
    非线性          线性              非线性
```

**原因**:
1. **纹理存储**: sRGB节省空间,符合人眼感知
2. **光照计算**: 必须在线性空间,否则结果错误
3. **显示输出**: 显示器期望sRGB输入

---

## sRGB vs Linear空间

### sRGB颜色空间

**特点**: 非线性编码,符合人眼对亮度的感知

```cpp
// sRGB -> Linear (解码/去Gamma)
float sRGBToLinear(float srgb) {
    if (srgb <= 0.04045) {
        return srgb / 12.92;
    } else {
        return pow((srgb + 0.055) / 1.055, 2.4);
    }
}

// Linear -> sRGB (编码/Gamma校正)
float linearToSRGB(float linear) {
    if (linear <= 0.0031308) {
        return linear * 12.92;
    } else {
        return 1.055 * pow(linear, 1.0 / 2.4) - 0.055;
    }
}
```

```glsl
// GLSL内置函数
vec3 linear = pow(srgbColor, vec3(2.2));  // 近似解码
vec3 srgb = pow(linearColor, vec3(1.0/2.2));  // 近似编码
```

### Linear颜色空间

**特点**: 物理正确,光照计算必须在此空间

```cpp
// 错误：在sRGB空间混合颜色
vec3 srgbA = vec3(0.5, 0.0, 0.0);  // 暗红
vec3 srgbB = vec3(0.0, 0.5, 0.0);  // 暗绿
vec3 wrongMix = (srgbA + srgbB) * 0.5;  // 错误！过暗

// 正确：在Linear空间混合
vec3 linearA = sRGBToLinear(srgbA);
vec3 linearB = sRGBToLinear(srgbB);
vec3 correctMix = (linearA + linearB) * 0.5;
vec3 result = linearToSRGB(correctMix);  // 转回sRGB输出
```

### 视觉对比

```
值(sRGB)   Linear   实际亮度
  0.0       0.0        0%
  0.25      0.05       5%    ← sRGB节省暗部精度
  0.5       0.21      21%    ← 中间值不是50%亮度!
  0.75      0.52      52%
  1.0       1.0      100%
```

**示例: 纹理采样错误**

```glsl
// 错误：直接使用sRGB纹理值计算
vec3 albedo = texture(albedoMap, uv).rgb;  // sRGB编码
vec3 lighting = albedo * lightColor;  // 错误！在非线性空间计算

// 正确：转换到线性空间
vec3 albedo_sRGB = texture(albedoMap, uv).rgb;
vec3 albedo_linear = pow(albedo_sRGB, vec3(2.2));  // 解码
vec3 lighting = albedo_linear * lightColor;  // 正确的线性计算
fragColor = vec4(pow(lighting, vec3(1.0/2.2)), 1.0);  // 编码回sRGB
```

---

## Gamma校正原理

### 为什么需要Gamma

**历史原因**: CRT显示器的物理特性

```
输入电压 V → 实际亮度 L = V^2.2
```

**人眼感知**: 人眼对暗部变化更敏感

```
物理亮度:   0   25   50   75  100  (线性)
人眼感知:   0   50   71   87  100  (非线性)
```

**存储效率**: sRGB用8bit表示时,暗部有更多精度

```
Linear 8bit:  0-255均匀分布 → 暗部精度低,浪费亮部精度
sRGB 8bit:    0-255按感知分布 → 暗部精度高,亮部足够
```

### Gamma校正流程

```
[纹理资源 (sRGB)]
        ↓ 解码 (^2.2)
[线性空间 (Linear)]
        ↓ 光照计算
[线性结果 (Linear)]
        ↓ 编码 (^1/2.2)
[帧缓冲 (sRGB)]
        ↓ 显示器解码 (^2.2)
[屏幕亮度 (Linear)]
```

### 硬件自动Gamma校正

**方法1: sRGB纹理格式**

```cpp
// OpenGL自动解码sRGB纹理
glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8,  // sRGB格式
             width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

// Shader中采样自动得到线性值
vec3 albedo = texture(albedoMap, uv).rgb;  // 已经是线性!
```

**方法2: sRGB帧缓冲**

```cpp
// OpenGL自动编码输出
glEnable(GL_FRAMEBUFFER_SRGB);  // 启用sRGB帧缓冲

// Shader直接输出线性值,硬件自动转换为sRGB
fragColor = vec4(linearColor, 1.0);  // 硬件会自动Gamma校正
```

### 完整的Gamma正确工作流

```glsl
// Vertex Shader
out vec2 vUV;

void main() {
    vUV = aUV;
    gl_Position = ...;
}

// Fragment Shader
uniform sampler2D albedoMap;      // GL_SRGB8 格式 (硬件解码)
uniform sampler2D normalMap;      // GL_RGBA8 格式 (线性数据,不解码)
uniform sampler2D roughnessMap;   // GL_R8 格式 (线性数据,不解码)

out vec4 fragColor;

void main() {
    // 1. 采样纹理
    vec3 albedo = texture(albedoMap, vUV).rgb;  // 自动sRGB->Linear
    vec3 normal = texture(normalMap, vUV).rgb;  // 已经是线性
    float roughness = texture(roughnessMap, vUV).r;  // 已经是线性
    
    // 2. 光照计算(在线性空间)
    vec3 lighting = calculatePBR(albedo, normal, roughness);
    
    // 3. 输出(硬件自动Linear->sRGB)
    fragColor = vec4(lighting, 1.0);  // GL_FRAMEBUFFER_SRGB启用时自动转换
}
```

---

## 色彩管理流程

### 渲染管线中的颜色空间

```
[Asset阶段]
  纹理贴图 (sRGB, PNG/JPG)
  法线贴图 (Linear, 不需要Gamma)
  粗糙度/金属度 (Linear)
        ↓
[加载阶段]
  sRGB纹理 → GL_SRGB8/GL_SRGB8_ALPHA8
  Linear纹理 → GL_RGB8/GL_RGBA8
        ↓
[Shader计算]
  采样纹理 → 自动解码为线性值
  PBR光照计算 (线性空间)
  后处理效果 (线性空间)
        ↓
[输出阶段]
  写入sRGB帧缓冲 → 自动编码
  或手动Gamma校正 → pow(color, 1/2.2)
        ↓
[显示器]
  接收sRGB信号
  应用显示器Gamma (通常2.2)
  发出物理光线
```

### 纹理导入规则

**需要sRGB编码的纹理**:
- BaseColor/Albedo (固有色)
- Emissive (自发光颜色)
- LUT (Color Grading)

**必须保持Linear的纹理**:
- Normal Map (法线方向)
- Height/Displacement (几何数据)
- Roughness/Metallic (物理参数)
- AO (Ambient Occlusion)
- 任何用于数值计算的数据

```cpp
// C++纹理加载
void loadTexture(const char* path, TextureType type) {
    // 读取图片数据
    int width, height, channels;
    unsigned char* data = stbi_load(path, &width, &height, &channels, 0);
    
    // 根据类型选择格式
    GLenum internalFormat;
    if (type == TextureType::Albedo || type == TextureType::Emissive) {
        internalFormat = GL_SRGB8_ALPHA8;  // sRGB格式
    } else {
        internalFormat = GL_RGBA8;  // Linear格式
    }
    
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat,
                 width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    
    stbi_image_free(data);
}
```

### Uniform颜色处理

```cpp
// 错误：直接传递sRGB值
vec3 lightColor = vec3(1.0, 0.5, 0.0);  // 橙色光
glUniform3fv(lightColorLoc, 1, &lightColor[0]);  // 错误!

// 正确：手动转换为线性值
vec3 lightColor_sRGB = vec3(1.0, 0.5, 0.0);
vec3 lightColor_linear = pow(lightColor_sRGB, vec3(2.2));
glUniform3fv(lightColorLoc, 1, &lightColor_linear[0]);
```

```cpp
// 或使用转换函数
vec3 sRGBToLinear(vec3 srgb) {
    return vec3(
        sRGBToLinear(srgb.r),
        sRGBToLinear(srgb.g),
        sRGBToLinear(srgb.b)
    );
}

// Hex颜色转换
vec3 hexToLinear(uint32_t hex) {
    float r = ((hex >> 16) & 0xFF) / 255.0;
    float g = ((hex >> 8) & 0xFF) / 255.0;
    float b = (hex & 0xFF) / 255.0;
    return sRGBToLinear(vec3(r, g, b));
}

// 使用示例
vec3 lightColor = hexToLinear(0xFF8000);  // #FF8000 橙色
```

---

## HDR与色调映射

### 什么是HDR

**HDR (High Dynamic Range)**: 支持亮度范围超过[0, 1]的渲染

```
LDR (Low Dynamic Range):  [0.0, 1.0]  (8-bit, 256级)
HDR (High Dynamic Range): [0.0, ∞)    (16/32-bit float)
```

**现实世界亮度范围**:
```
星光:       0.001 lux
室内:         100 lux
阴天:      10,000 lux
晴天:     100,000 lux
阳光直射: 120,000 lux
```

### HDR渲染管线

```cpp
// 1. 创建HDR帧缓冲
GLuint hdrFBO, hdrTexture;
glGenFramebuffers(1, &hdrFBO);
glGenTextures(1, &hdrTexture);

// 使用16位浮点格式
glBindTexture(GL_TEXTURE_2D, hdrTexture);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F,  // 16-bit float
             width, height, 0, GL_RGBA, GL_FLOAT, nullptr);

glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
                       GL_TEXTURE_2D, hdrTexture, 0);

// 2. 渲染到HDR缓冲
glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

// Shader可以输出>1.0的值
// fragColor = vec4(10.0, 5.0, 2.0, 1.0);  // 非常亮的颜色

renderScene();

// 3. 色调映射到LDR
glBindFramebuffer(GL_FRAMEBUFFER, 0);  // 默认帧缓冲
bindTonemapShader();
bindTexture(hdrTexture);
renderFullscreenQuad();
```

### 色调映射算法

**1. Reinhard Tone Mapping**

```glsl
// 简单但有效
vec3 reinhardTonemap(vec3 hdrColor) {
    return hdrColor / (hdrColor + vec3(1.0));
}

// 带白点的版本
vec3 reinhardExtended(vec3 hdrColor, float maxWhite) {
    vec3 numerator = hdrColor * (1.0 + (hdrColor / (maxWhite * maxWhite)));
    return numerator / (1.0 + hdrColor);
}
```

**2. ACES Filmic**

```glsl
// 电影行业标准,色彩自然
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
```

**3. Uncharted 2 (Filmic)**

```glsl
vec3 Uncharted2Tonemap(vec3 x) {
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 Uncharted2(vec3 color) {
    float exposureBias = 2.0;
    vec3 curr = Uncharted2Tonemap(color * exposureBias);
    
    vec3 W = vec3(11.2);  // 白点
    vec3 whiteScale = 1.0 / Uncharted2Tonemap(W);
    return curr * whiteScale;
}
```

**4. 简单曝光调整**

```glsl
// 最简单的方法
vec3 exposureTonemap(vec3 hdrColor, float exposure) {
    return vec3(1.0) - exp(-hdrColor * exposure);
}
```

### 完整HDR后处理

```glsl
// Fragment Shader
uniform sampler2D hdrBuffer;
uniform float exposure;
uniform float gamma;

in vec2 vUV;
out vec4 fragColor;

void main() {
    // 1. 采样HDR颜色
    vec3 hdrColor = texture(hdrBuffer, vUV).rgb;
    
    // 2. 曝光调整
    vec3 exposed = hdrColor * exposure;
    
    // 3. 色调映射 (HDR -> LDR)
    vec3 ldrColor = ACESFilm(exposed);
    
    // 4. Gamma校正
    vec3 gammaCorrected = pow(ldrColor, vec3(1.0 / gamma));
    
    fragColor = vec4(gammaCorrected, 1.0);
}
```

### 自动曝光 (Eye Adaptation)

```glsl
// 计算场景平均亮度
float calculateAverageLuminance(sampler2D hdrBuffer) {
    // 使用Mipmap链
    // Level 0: 原始分辨率
    // Level N: 1x1 (平均值)
    
    // 采样最小的mip level
    vec3 avgColor = textureLod(hdrBuffer, vec2(0.5), 100.0).rgb;
    
    // 转换为亮度
    float luminance = dot(avgColor, vec3(0.2126, 0.7152, 0.0722));
    return luminance;
}

// 自适应曝光
float adaptExposure(float currentExposure, float targetLuminance, float dt) {
    float targetExposure = 1.0 / (targetLuminance + 0.001);
    
    // 平滑过渡 (模拟人眼适应)
    float adaptationSpeed = 1.5;  // 适应速度
    return mix(currentExposure, targetExposure, 1.0 - exp(-dt * adaptationSpeed));
}
```

---

## 颜色编码格式

### 整数格式

**8-bit (UNORM)**
```cpp
// GL_RGBA8: 每通道8位,范围[0, 255] → [0.0, 1.0]
// sRGB: GL_SRGB8_ALPHA8

// 优点: 节省内存,兼容性好
// 缺点: 精度低,HDR不适用
```

**10-bit RGB (RGB10_A2)**
```cpp
// GL_RGB10_A2: RGB各10位,A 2位
// 更高的颜色精度,减少色带

glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB10_A2, ...);
```

### 浮点格式

**16-bit Float (Half Float)**
```cpp
// GL_RGBA16F: 每通道16位半精度浮点
// 范围: ±65504, 精度: ~3位小数

// 最常用的HDR格式
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 
             width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
```

**32-bit Float**
```cpp
// GL_RGBA32F: 每通道32位全精度浮点
// 范围: ±3.4e38, 精度: ~7位小数

// 用于高精度计算(G-Buffer, Compute Shader)
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, ...);
```

### 压缩格式的颜色空间

```cpp
// BC1/DXT1
GL_COMPRESSED_RGB_S3TC_DXT1_EXT        // Linear
GL_COMPRESSED_SRGB_S3TC_DXT1_EXT      // sRGB

// BC3/DXT5
GL_COMPRESSED_RGBA_S3TC_DXT5_EXT      // Linear
GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT // sRGB

// BC7 (最高质量)
GL_COMPRESSED_RGBA_BPTC_UNORM         // Linear
GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM   // sRGB

// ETC2 (移动端)
GL_COMPRESSED_RGB8_ETC2               // Linear
GL_COMPRESSED_SRGB8_ETC2              // sRGB

// ASTC
GL_COMPRESSED_RGBA_ASTC_4x4_KHR       // Linear
GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR // sRGB
```

---

## Filament色彩管理

### Filament材质中的颜色

```cpp
// Material定义
material {
    parameters : [
        {
            type : float3,
            name : baseColor
        }
    ]
}

// C++设置颜色(需要传递线性值)
auto matInstance = material->createInstance();

// 错误: 传递sRGB值
matInstance->setParameter("baseColor", RgbType::sRGB, {1.0f, 0.5f, 0.0f});

// 正确: 传递线性值
matInstance->setParameter("baseColor", RgbType::LINEAR, {1.0f, 0.218f, 0.0f});

// 或让Filament自动转换
matInstance->setParameter("baseColor", RgbType::sRGB, {1.0f, 0.5f, 0.0f});
// Filament内部会转换为线性值
```

### Filament纹理导入

```cpp
// 使用Filament的TextureLoader
auto texture = TextureLoader::create(engine)
    .fromFile("albedo.png")
    .sRGB()  // 标记为sRGB纹理
    .build();

// 或手动创建
auto texture = Texture::Builder()
    .width(width)
    .height(height)
    .format(Texture::InternalFormat::SRGB8_A8)  // sRGB格式
    .build(*engine);
```

### Filament色调映射

```cpp
// 在View中设置色调映射
view->setColorGrading(colorGrading);

// 创建ColorGrading
auto colorGrading = ColorGrading::Builder()
    .toneMapping(ColorGrading::ToneMapping::ACES)  // ACES, FILMIC, LINEAR
    .exposure(0.0f)      // EV曝光补偿
    .contrast(1.0f)      // 对比度
    .saturation(1.0f)    // 饱和度
    .temperature(0.0f)   // 色温 (-1: 冷, +1: 暖)
    .tint(0.0f)         // 色调偏移
    .build(*engine);
```

### Filament后处理

```glsl
// Filament内置后处理流程
[Scene渲染 (Linear HDR)]
        ↓
[Bloom (可选)]
        ↓
[色调映射 (HDR → LDR)]
        ↓
[Color Grading]
        ↓
[Dithering (减少色带)]
        ↓
[Gamma校正 (Linear → sRGB)]
        ↓
[输出到屏幕]
```

---

## 常见错误与调试

### 错误1: 颜色过暗

```glsl
// 问题: 纹理被当作线性加载,但实际是sRGB
uniform sampler2D albedoMap;  // 错误格式

vec3 albedo = texture(albedoMap, uv).rgb;  // 过暗!

// 解决: 使用sRGB纹理格式
// C++: glTexImage2D(..., GL_SRGB8_ALPHA8, ...);
```

### 错误2: 混合结果不正确

```glsl
// 问题: 在sRGB空间插值
vec3 colorA_sRGB = vec3(0.5, 0.0, 0.0);
vec3 colorB_sRGB = vec3(0.0, 0.5, 0.0);
vec3 blend = mix(colorA_sRGB, colorB_sRGB, 0.5);  // 错误!

// 解决: 转换到线性空间
vec3 colorA_linear = pow(colorA_sRGB, vec3(2.2));
vec3 colorB_linear = pow(colorB_sRGB, vec3(2.2));
vec3 blend_linear = mix(colorA_linear, colorB_linear, 0.5);
vec3 blend_sRGB = pow(blend_linear, vec3(1.0/2.2));
```

### 错误3: 法线贴图颜色错误

```cpp
// 问题: 法线贴图使用sRGB格式
glTexImage2D(..., GL_SRGB8_ALPHA8, ...);  // 错误!

// 解决: 法线是几何数据,必须用线性格式
glTexImage2D(..., GL_RGBA8, ...);  // 正确
```

### 错误4: 光照过亮

```glsl
// 问题: 双重Gamma校正
vec3 albedo = texture(albedoMap, uv).rgb;  // sRGB纹理自动解码
vec3 albedo_decoded = pow(albedo, vec3(2.2));  // 再次解码!过亮!

// 解决: 不需要手动解码
vec3 albedo = texture(albedoMap, uv).rgb;  // 已经是线性值
```

### 调试工具

```glsl
// Shader调试: 可视化颜色空间
out vec4 fragColor;

void main() {
    vec3 color = texture(albedoMap, vUV).rgb;
    
    // 显示原始值
    fragColor = vec4(color, 1.0);
    
    // 显示是否在[0,1]范围
    if (any(greaterThan(color, vec3(1.0))) || any(lessThan(color, vec3(0.0)))) {
        fragColor = vec4(1.0, 0.0, 1.0, 1.0);  // 紫色=超出范围
    }
    
    // 显示亮度
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    fragColor = vec4(vec3(luminance), 1.0);
}
```

---

## 最佳实践

### 纹理导入清单

```cpp
struct TextureImportSettings {
    enum class ColorSpace {
        sRGB,    // Albedo, Emissive
        Linear   // Normal, Roughness, Metallic, AO, Height
    };
    
    ColorSpace colorSpace;
    bool generateMipmaps;
    bool compress;
};

// Albedo纹理
TextureImportSettings albedoSettings = {
    .colorSpace = ColorSpace::sRGB,
    .generateMipmaps = true,
    .compress = true  // BC7/ASTC with sRGB
};

// Normal纹理
TextureImportSettings normalSettings = {
    .colorSpace = ColorSpace::Linear,  // 关键!
    .generateMipmaps = true,
    .compress = true  // BC5/BC3 (Linear)
};
```

### Shader模板

```glsl
// 标准PBR Shader
uniform sampler2D albedoMap;      // sRGB
uniform sampler2D normalMap;      // Linear
uniform sampler2D metallicMap;    // Linear
uniform sampler2D roughnessMap;   // Linear
uniform sampler2D aoMap;          // Linear
uniform sampler2D emissiveMap;    // sRGB

out vec4 fragColor;

void main() {
    // 1. 采样纹理 (自动sRGB解码)
    vec3 albedo = texture(albedoMap, vUV).rgb;
    vec3 normal = texture(normalMap, vUV).rgb * 2.0 - 1.0;
    float metallic = texture(metallicMap, vUV).r;
    float roughness = texture(roughnessMap, vUV).r;
    float ao = texture(aoMap, vUV).r;
    vec3 emissive = texture(emissiveMap, vUV).rgb;
    
    // 2. PBR计算 (线性空间)
    vec3 lighting = calculatePBR(albedo, normal, metallic, roughness);
    lighting *= ao;
    lighting += emissive;
    
    // 3. 输出 (自动sRGB编码,如果启用GL_FRAMEBUFFER_SRGB)
    fragColor = vec4(lighting, 1.0);
}
```

### 颜色管理规范

1. **资产阶段**:
   - 艺术家在sRGB监视器上创作
   - 保存为PNG/JPG (sRGB编码)

2. **导入阶段**:
   - Albedo → `GL_SRGB8_ALPHA8`
   - Normal → `GL_RGBA8` (Linear)
   - PBR参数 → `GL_R8/GL_RG8` (Linear)

3. **渲染阶段**:
   - 启用 `GL_FRAMEBUFFER_SRGB` (自动Gamma)
   - 或使用HDR管线+手动色调映射

4. **显示阶段**:
   - 输出sRGB信号
   - 显示器应用Gamma 2.2
   - 用户看到正确颜色

---

## 总结

**关键概念**:
- **sRGB**: 非线性编码,用于存储和显示
- **Linear**: 线性编码,用于物理计算
- **Gamma校正**: sRGB ↔ Linear 转换
- **HDR**: 超出[0,1]范围的亮度值
- **色调映射**: HDR → LDR 转换

**工作流程**:
```
sRGB纹理 → Linear计算 → Linear结果 → sRGB输出 → sRGB显示
```

**常见陷阱**:
- ❌ 在sRGB空间做光照计算
- ❌ 法线贴图使用sRGB格式
- ❌ 双重Gamma校正
- ❌ Uniform颜色未转换为线性

**检查清单**:
- [ ] Albedo纹理使用sRGB格式
- [ ] 法线/PBR参数使用Linear格式
- [ ] 启用GL_FRAMEBUFFER_SRGB或手动Gamma
- [ ] Uniform颜色转换为线性值
- [ ] HDR管线使用FP16纹理
- [ ] 正确的色调映射算法

**参考资源**:
- [GPU Gems 3: The Importance of Being Linear](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-24-importance-being-linear)
- [Filament Documentation: Color Grading](https://google.github.io/filament/Filament.html#imagingpipeline/colorgrading)
- [ACES Central](https://acescentral.com/)
