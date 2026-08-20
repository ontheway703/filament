# 纹理系统

详细讲解纹理采样、过滤、压缩、优化等纹理系统的各个方面。

---

## 目录

1. [纹理基础](#纹理基础)
2. [纹理过滤](#纹理过滤)
3. [Mipmap详解](#mipmap详解)
4. [纹理包裹模式](#纹理包裹模式)
5. [纹理压缩格式](#纹理压缩格式)
6. [纹理优化技巧](#纹理优化技巧)
7. [高级纹理技术](#高级纹理技术)

---

## 纹理基础

### 什么是纹理

**纹理(Texture)**是存储在GPU内存中的图像数据,用于为3D模型添加细节。

```
3D模型 + 纹理 = 丰富细节

简单几何:          纹理映射:         结果:
   □                  🎨              精美模型
  /|\                ↓                 /|\
   |              UV坐标映射            |
```

### 纹理类型

#### 1. 2D纹理(Texture2D)
最常用的纹理类型:

```glsl
uniform sampler2D albedoMap;
vec4 color = texture(albedoMap, uv);
```

#### 2. 立方体贴图(CubemapTexture)
用于环境反射和天空盒:

```
     +Y (上)
      │
  -X──┼──+X
      │
     -Y (下)
      │
     ±Z (前后)
```

```glsl
uniform samplerCube envMap;
vec3 direction = reflect(-V, N);
vec4 color = texture(envMap, direction);
```

#### 3. 3D纹理(Texture3D)
体积纹理,用于体积渲染:

```glsl
uniform sampler3D volumeTexture;
vec4 density = texture(volumeTexture, vec3(x, y, z));
```

#### 4. 纹理数组(Texture2DArray)
多层2D纹理:

```glsl
uniform sampler2DArray texArray;
vec4 color = texture(texArray, vec3(uv, layerIndex));
```

### UV坐标

**UV映射**将2D纹理映射到3D表面:

```
纹理空间 (0,0)-(1,1):    模型顶点:
  1 ┌─────────┐            ●───●
    │  🎨     │           /│  /│
  V │         │          ●─┼─● │
    │         │          │ ●─┼─●
  0 └─────────┘          │/  │/
    0    U    1          ●───●
                       每个顶点有UV坐标
```

**UV坐标范围**:
- U: 水平方向 [0, 1]
- V: 垂直方向 [0, 1]
- 原点: 左下角 (OpenGL) 或 左上角 (DirectX/Vulkan)

### 纹理采样

```glsl
// 基础采样
vec4 color = texture(sampler2D, vec2 uv);

// 带LOD的采样
vec4 color = textureLod(sampler2D, vec2 uv, float lod);

// 带梯度的采样
vec4 color = textureGrad(sampler2D, vec2 uv, vec2 dPdx, vec2 dPdy);

// 带偏移的采样
vec4 color = textureOffset(sampler2D, vec2 uv, ivec2 offset);
```

---

## 纹理过滤

### 过滤类型

当纹理映射到屏幕时,纹理像素(texel)与屏幕像素(pixel)通常不是1:1对应,需要过滤。

```
放大(Magnification):       缩小(Minification):
  纹理                        纹理
   ↓                           ↓
  ████                        ████████
  ████                        ████████
  (少)                        (多)
   ↓                           ↓
  █████████                  ██
  █████████                  ██
   屏幕                        屏幕
```

### 放大过滤(MagFilter)

**GL_NEAREST** (最近邻):
```
优点: 快速,像素风格
缺点: 锯齿,方块化

效果: ▓▓▓▓▓▓
      ▓▓▓▓▓▓
      ▓▓▓▓▓▓
```

```glsl
sampler.magFilter = TextureSampler::MagFilter::NEAREST;
```

**GL_LINEAR** (双线性插值):
```
优点: 平滑,无锯齿
缺点: 稍慢,可能模糊

效果: ▒▒▓▓▓▒
      ▒▓▓▓▓▓
      ▓▓▓▓▒▒
```

```glsl
sampler.magFilter = TextureSampler::MagFilter::LINEAR;
```

**双线性插值算法**:
```glsl
// 伪代码
vec4 bilinear(sampler2D tex, vec2 uv) {
    vec2 texSize = textureSize(tex, 0);
    vec2 texCoord = uv * texSize - 0.5;
    vec2 f = fract(texCoord);
    vec2 i = floor(texCoord);

    vec4 t00 = texelFetch(tex, ivec2(i), 0);
    vec4 t10 = texelFetch(tex, ivec2(i) + ivec2(1,0), 0);
    vec4 t01 = texelFetch(tex, ivec2(i) + ivec2(0,1), 0);
    vec4 t11 = texelFetch(tex, ivec2(i) + ivec2(1,1), 0);

    vec4 tx0 = mix(t00, t10, f.x);
    vec4 tx1 = mix(t01, t11, f.x);
    return mix(tx0, tx1, f.y);
}
```

### 缩小过滤(MinFilter)

当多个texel映射到一个pixel时,需要更复杂的过滤。

**GL_NEAREST**:
```
最近邻,快但有锯齿和闪烁
```

**GL_LINEAR**:
```
双线性,平滑但仍有闪烁
```

**GL_LINEAR_MIPMAP_LINEAR** (三线性):
```
最佳质量,无闪烁
推荐用于大多数情况
```

```glsl
sampler.minFilter = TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR;
```

---

## Mipmap详解

### 什么是Mipmap

**Mipmap**是预先计算的缩小版本纹理链:

```
Level 0: 1024×1024  (原始)
         ████████
         ████████

Level 1: 512×512    (1/4)
         ████
         ████

Level 2: 256×256    (1/16)
         ██
         ██

Level 3: 128×128
         █

...

Level 10: 1×1       (最小)
          •
```

### 为什么需要Mipmap

#### 问题1: 闪烁(Flickering)
```
无Mipmap:
  远处纹理细节密集
  采样不足
  → 闪烁/摩尔纹

有Mipmap:
  自动选择合适LOD
  → 稳定图像
```

#### 问题2: 性能
```
无Mipmap:
  每次采样1024×1024纹理
  缓存命中率低
  → 慢

有Mipmap:
  远处采样8×8纹理
  缓存命中率高
  → 快
```

### Mipmap级别计算

GPU自动计算LOD(Level of Detail):

```glsl
// 简化公式
float lod = log2(max(dUVdx, dUVdy) * textureSize);

dUVdx: UV坐标在X方向的变化率
dUVdy: UV坐标在Y方向的变化率
```

**手动LOD**:
```glsl
// 基于距离
float distance = length(cameraPos - worldPos);
float lod = log2(distance * 0.1);
vec4 color = textureLod(tex, uv, lod);
```

### Mipmap过滤模式

**GL_NEAREST_MIPMAP_NEAREST**:
```
选择最近的mipmap级别
使用最近邻采样
快,但质量差
```

**GL_LINEAR_MIPMAP_NEAREST**:
```
选择最近的mipmap级别
使用双线性采样
中等质量
```

**GL_NEAREST_MIPMAP_LINEAR**:
```
在两个mipmap级别间线性插值
每级使用最近邻
中等质量
```

**GL_LINEAR_MIPMAP_LINEAR** (三线性,推荐):
```
在两个mipmap级别间线性插值
每级使用双线性采样
最高质量,推荐
```

```glsl
sampler.minFilter = TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR;
```

### 三线性过滤算法

```glsl
// 伪代码
vec4 trilinear(sampler2D tex, vec2 uv) {
    float lod = computeLOD(uv);
    int lod0 = int(floor(lod));
    int lod1 = lod0 + 1;
    float f = fract(lod);

    vec4 sample0 = bilinear(tex, uv, lod0);  // 级别lod0
    vec4 sample1 = bilinear(tex, uv, lod1);  // 级别lod1

    return mix(sample0, sample1, f);  // 级别间插值
}
```

### Mipmap生成

**自动生成**:
```cpp
// Filament
Texture::Builder textureBuilder;
textureBuilder
    .width(width)
    .height(height)
    .levels(0xFF)  // 自动计算mipmap级别数
    .format(Texture::InternalFormat::RGBA8)
    .sampler(Texture::Sampler::SAMPLER_2D)
    .build(*engine);

// 生成mipmap
texture->generateMipmaps(*engine);
```

**手动提供**:
```cpp
// 预先生成的mipmap链
int levels = floor(log2(max(width, height))) + 1;
for (int i = 0; i < levels; i++) {
    int w = width >> i;
    int h = height >> i;
    Texture::PixelBufferDescriptor buffer(
        data[i], size[i],
        Texture::Format::RGBA, Texture::Type::UBYTE
    );
    texture->setImage(*engine, i, std::move(buffer));
}
```

### Mipmap内存开销

```
额外内存 = 原始大小 × 1/3

例如:
1024×1024 RGBA8 = 4MB
Mipmap链 = 4MB × (1 + 1/4 + 1/16 + ... + 1/4^10)
        = 4MB × 1.333
        = 5.33MB

增加33%内存,但性能和质量大幅提升
```

---

## 纹理包裹模式

### 包裹模式类型

当UV坐标超出[0,1]范围时的处理方式。

#### GL_REPEAT (重复)
```
UV = 1.5:
  ┌─────┬─────┐
  │ 🎨 │ 🎨 │
  └─────┴─────┘
  0     1     2

无缝平铺,适合地板/墙壁
```

```glsl
sampler.wrapS = TextureSampler::WrapMode::REPEAT;
sampler.wrapT = TextureSampler::WrapMode::REPEAT;
```

#### GL_CLAMP_TO_EDGE (边缘拉伸)
```
UV = 1.5:
  ┌─────┬──
  │ 🎨 │██  (边缘颜色拉伸)
  └─────┴──
  0     1

避免接缝,适合UI/天空盒
```

```glsl
sampler.wrapS = TextureSampler::WrapMode::CLAMP_TO_EDGE;
```

#### GL_MIRRORED_REPEAT (镜像重复)
```
UV = 2.5:
  ┌─────┬─────┬─────┐
  │ 🎨 │ 🎨 │ 🎨 │
  └─────┴─────┴─────┘
  0     1     2     3
        正    反    正

自动无缝,适合对称纹理
```

```glsl
sampler.wrapS = TextureSampler::WrapMode::MIRRORED_REPEAT;
```

#### GL_CLAMP_TO_BORDER (边框颜色)
```
UV = 1.5:
  ┌─────┐
  │ 🎨 │  (边框颜色)
  └─────┘
  0     1

可设置边框颜色(OpenGL)
Filament不直接支持
```

### UV平铺

```glsl
// 平铺10次
vec2 tiledUV = uv * 10.0;
vec4 color = texture(tex, tiledUV);  // 配合REPEAT模式

// 不同方向不同平铺
vec2 tiledUV = uv * vec2(5.0, 3.0);  // X平铺5次,Y平铺3次
```

### 使用示例

```glsl
// Filament材质
material {
    parameters : [
        { type : sampler2d, name : floorTexture },
        { type : float2, name : uvScale, default : [1.0, 1.0] }
    ]
}

fragment {
    void material(inout MaterialInputs material) {
        // UV平铺
        vec2 uv = getUV0() * materialParams.uvScale;

        // 采样(使用REPEAT包裹)
        vec4 color = texture(materialParams_floorTexture, uv);
        material.baseColor = color;
    }
}

// C++设置采样器
TextureSampler sampler(
    TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
    TextureSampler::MagFilter::LINEAR,
    TextureSampler::WrapMode::REPEAT
);
materialInstance->setParameter("floorTexture", texture, sampler);
```

---

## 纹理压缩格式

### 未压缩格式

| 格式 | 通道 | 每像素位数 | 用途 |
|-----|------|-----------|------|
| **RGBA8** | RGBA | 32 (4字节) | 通用,带Alpha |
| **RGB8** | RGB | 24 (3字节) | 通用,无Alpha |
| **RG8** | RG | 16 (2字节) | 法线贴图 |
| **R8** | R | 8 (1字节) | 单通道(AO, Roughness) |
| **RGBA16F** | RGBA | 64 (8字节) | HDR,浮点 |

**内存计算**:
```
1024×1024 RGBA8纹理:
= 1024 × 1024 × 4字节
= 4,194,304字节
= 4MB

带mipmap:
= 4MB × 1.333
= 5.33MB
```

### GPU压缩格式

**优势**:
- 减少GPU内存占用
- 减少带宽需求
- GPU可直接解码

#### PC/Console (Desktop)

**BC/DXTn压缩**:

| 格式 | 压缩比 | 用途 |
|-----|--------|------|
| **BC1/DXT1** | 6:1 | RGB,1位Alpha |
| **BC3/DXT5** | 4:1 | RGBA |
| **BC4** | 4:1 | 单通道 |
| **BC5** | 4:1 | 双通道(法线RG) |
| **BC6H** | 6:1 | HDR |
| **BC7** | 4:1 | 高质量RGBA |

```
1024×1024 RGBA8 → BC7:
  4MB → 1MB (节省75%)
```

#### Android

**ETC/ETC2压缩**:

| 格式 | 压缩比 | 用途 |
|-----|--------|------|
| **ETC2 RGB** | 6:1 | RGB |
| **ETC2 RGBA** | 4:1 | RGBA |
| **EAC R11** | 4:1 | 单通道 |
| **EAC RG11** | 4:1 | 双通道 |

**ASTC压缩** (更新,更好):

| 格式 | 压缩比 | 质量 |
|-----|--------|------|
| **ASTC 4×4** | 8:1 | 高 |
| **ASTC 6×6** | 3.6:1 | 中 |
| **ASTC 8×8** | 2:1 | 低 |

#### iOS

**PVRTC压缩** (老):

| 格式 | 压缩比 |
|-----|--------|
| **PVRTC 4bpp** | 8:1 |
| **PVRTC 2bpp** | 16:1 |

**ASTC** (新,iOS 8+推荐):
同Android ASTC。

### Basis Universal / KTX2

**跨平台超级压缩**:

```
Basis Universal编码 (.basis, .ktx2)
    ↓ GPU运行时转码
┌───┴────┬──────┬──────┬──────┐
BC7     ETC2   ASTC   PVRTC
(PC)   (Android)(iOS) (iOS old)
```

**优势**:
- 一个文件,所有平台
- 比PNG/JPEG更小
- GPU直接解码

**压缩比**:
```
PNG: 10MB
BC7: 2.5MB
Basis (ETC1S): 800KB (3倍压缩)
Basis (UASTC): 1.5MB (2倍压缩,更高质量)
```

**Filament使用**:
```cpp
#include <ktxreader/Ktx2Reader.h>

Ktx2Reader reader(*engine);
Texture* texture = reader.load(ktx2Data, ktx2Size,
    Ktx2Reader::TransferFunction::sRGB);  // 或LINEAR
```

### 压缩格式选择指南

| 纹理类型 | PC | Android | iOS | 备注 |
|---------|-----|---------|-----|------|
| **BaseColor (sRGB)** | BC7 | ASTC 6×6 | ASTC 6×6 | 高质量 |
| **Normal** | BC5 | EAC RG11 | ASTC 6×6 | 线性 |
| **Metallic/Roughness** | BC4 | EAC R11 | ASTC 6×6 | 线性 |
| **HDR** | BC6H | ASTC HDR | ASTC HDR | 浮点 |

**或使用KTX2 Basis Universal**:
```bash
# 生成跨平台纹理
toktx --bcmp --genmipmap texture.ktx2 texture.png
```

---

## 纹理优化技巧

### 1. 分辨率优化

**原则**: 使用能达到需求的最小分辨率。

```
屏幕占用面积 → 纹理分辨率

全屏UI: 2048×2048
角色主体: 2048×2048
角色配件: 1024×1024
小道具: 512×512
远景: 256×256
```

**动态调整**:
```cpp
// 基于设备内存调整
int maxTextureSize = 2048;
if (deviceMemory < 2GB) {
    maxTextureSize = 1024;  // 低端设备降低分辨率
}
```

### 2. 纹理通道打包

**打包多个数据到一张纹理**:

```
glTF ORM贴图:
R: Ambient Occlusion
G: Roughness
B: Metallic
A: (可用于其他数据,如Height)

节省: 4张纹理 → 1张纹理
```

```glsl
vec4 orm = texture(ormTexture, uv);
float ao = orm.r;
float roughness = orm.g;
float metallic = orm.b;
```

**自定义打包**:
```
自定义Mask纹理:
R: 金属区域mask
G: 发光区域mask
B: 湿润区域mask
A: 损坏区域mask
```

### 3. Texture Atlas(纹理图集)

**合并多个小纹理为一张大纹理**:

```
分离纹理 (4张):          Atlas (1张):
┌───┐ ┌───┐            ┌───┬───┐
│ A │ │ B │            │ A │ B │
└───┘ └───┘    →       ├───┼───┤
┌───┐ ┌───┐            │ C │ D │
│ C │ │ D │            └───┴───┘
└───┘ └───┘

4个Draw Call → 1个Draw Call
```

**UV调整**:
```glsl
// Atlas中的子纹理UV
vec2 atlasUV = subTextureOffset + uv * subTextureSize;

// 例如: A在左上(0,0.5)-(0.5,1), 大小0.5×0.5
vec2 atlasUV = vec2(0.0, 0.5) + uv * vec2(0.5, 0.5);
vec4 color = texture(atlas, atlasUV);
```

### 4. 虚拟纹理(Virtual Texturing)

**大场景使用超大纹理**:

```
16K×16K纹理 (1GB+):
  仅加载可见部分
  流式加载
  节省内存
```

Filament不直接支持,但可通过Clipmap等技术实现。

### 5. 纹理流送(Texture Streaming)

**按需加载LOD**:

```cpp
// 伪代码
if (cameraDistance < 10.0f) {
    loadTexture(highRes);  // 2048×2048
} else if (cameraDistance < 50.0f) {
    loadTexture(mediumRes);  // 1024×1024
} else {
    loadTexture(lowRes);  // 512×512
}
```

### 6. 减少纹理采样次数

```glsl
// ❌ 差: 重复采样
float r = texture(tex, uv).r;
float g = texture(tex, uv).g;
float b = texture(tex, uv).b;

// ✅ 好: 一次采样
vec3 rgb = texture(tex, uv).rgb;
float r = rgb.r;
float g = rgb.g;
float b = rgb.b;
```

### 7. 使用合适的精度

```glsl
// 移动端
precision mediump float;

uniform lowp sampler2D albedoMap;    // 颜色纹理低精度足够
uniform mediump sampler2D normalMap;  // 法线中精度
```

---

## 高级纹理技术

### 1. 各向异性过滤(Anisotropic Filtering)

**问题**: 倾斜表面的纹理模糊。

```
无各向异性:           有各向异性:
  相机                  相机
   ↓                     ↓
   ╲                     ╲
    ╲ 模糊               ╲ 清晰
     ═══                  ═══
```

**启用**:
```cpp
TextureSampler sampler;
sampler.setAnisotropy(16.0f);  // 1, 2, 4, 8, 16
```

**性能**:
- AF 16x比三线性慢约20%
- 但视觉提升明显,推荐启用

### 2. 三平面映射(Triplanar Mapping)

**用途**: 无UV坐标或避免拉伸。

```glsl
vec3 blendWeights = abs(normal);
blendWeights = blendWeights / (blendWeights.x + blendWeights.y + blendWeights.z);

vec4 xColor = texture(tex, worldPos.yz) * blendWeights.x;
vec4 yColor = texture(tex, worldPos.xz) * blendWeights.y;
vec4 zColor = texture(tex, worldPos.xy) * blendWeights.z;

vec4 color = xColor + yColor + zColor;
```

### 3. 视差贴图(Parallax Mapping)

**增强深度感**:

```glsl
vec2 parallaxMapping(vec2 texCoords, vec3 viewDir) {
    const float heightScale = 0.1;
    float height = texture(heightMap, texCoords).r;
    vec2 p = viewDir.xy / viewDir.z * (height * heightScale);
    return texCoords - p;
}

void main() {
    vec3 viewDir = normalize(vViewDir);
    vec2 texCoords = parallaxMapping(vUV, viewDir);
    vec4 color = texture(albedoMap, texCoords);
}
```

**Steep Parallax Mapping** (更好):
```glsl
const int numLayers = 32;
float layerDepth = 1.0 / numLayers;
float currentLayerDepth = 0.0;
vec2 deltaTexCoords = viewDir.xy / viewDir.z * heightScale / numLayers;

// 逐层搜索
while (currentLayerDepth < currentDepth) {
    currentLayerDepth += layerDepth;
    texCoords -= deltaTexCoords;
    currentDepth = texture(heightMap, texCoords).r;
}
```

### 4. 程序化纹理(Procedural Textures)

**在Shader中生成纹理**:

```glsl
// Checkerboard
float checkerboard(vec2 uv, float scale) {
    vec2 c = floor(uv * scale);
    return mod(c.x + c.y, 2.0);
}

// Noise
float noise(vec2 uv) {
    return fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453);
}

// Voronoi
float voronoi(vec2 uv) {
    vec2 i = floor(uv);
    vec2 f = fract(uv);
    float minDist = 1.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighbor = vec2(x, y);
            vec2 point = hash(i + neighbor);
            float dist = length(f - neighbor - point);
            minDist = min(minDist, dist);
        }
    }
    return minDist;
}
```

### 5. 纹理动画

```glsl
// UV滚动(传送带,水流)
vec2 animatedUV = uv + vec2(time * 0.1, 0.0);
vec4 color = texture(tex, animatedUV);

// UV旋转
float angle = time;
mat2 rotation = mat2(cos(angle), -sin(angle),
                     sin(angle),  cos(angle));
vec2 rotatedUV = rotation * (uv - 0.5) + 0.5;
vec4 color = texture(tex, rotatedUV);

// 帧动画(精灵表)
int frameCount = 16;
int currentFrame = int(time * 10.0) % frameCount;
vec2 frameUV = uv / vec2(4.0, 4.0);  // 4×4网格
frameUV += vec2(currentFrame % 4, currentFrame / 4) / vec2(4.0, 4.0);
vec4 color = texture(spriteSheet, frameUV);
```

### 6. 纹理混合

```glsl
// 高度混合
float heightBlend(float height1, float height2, float blend) {
    float h1 = height1 * blend;
    float h2 = height2 * (1.0 - blend);
    float maxHeight = max(h1, h2);
    float transition = 0.2;
    h1 = max(h1 - maxHeight, 0.0) / transition;
    h2 = max(h2 - maxHeight, 0.0) / transition;
    return h1 / (h1 + h2);
}

vec4 tex1 = texture(texture1, uv);
vec4 tex2 = texture(texture2, uv);
float h1 = texture(heightMap1, uv).r;
float h2 = texture(heightMap2, uv).r;
float blend = heightBlend(h1, h2, blendFactor);
vec4 color = mix(tex1, tex2, blend);
```

---

## 总结

### 纹理系统核心要点

1. **过滤**: LINEAR_MIPMAP_LINEAR (三线性)
2. **Mipmap**: 必须启用,提升性能和质量
3. **压缩**: 使用GPU压缩格式节省内存
4. **包裹**: 根据用途选择REPEAT/CLAMP_TO_EDGE
5. **优化**: 合理分辨率,通道打包,Atlas

### 最佳实践

```cpp
// 推荐配置
TextureSampler sampler(
    TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,  // 三线性
    TextureSampler::MagFilter::LINEAR,                 // 双线性
    TextureSampler::WrapMode::REPEAT                   // 或CLAMP_TO_EDGE
);
sampler.setAnisotropy(16.0f);  // 各向异性16x

// 纹理构建
Texture::Builder()
    .width(width)
    .height(height)
    .levels(0xFF)  // 自动生成mipmap
    .format(Texture::InternalFormat::RGBA8)
    .sampler(Texture::Sampler::SAMPLER_2D)
    .build(*engine);
```

### 下一步学习

- **[05-material-properties.md](05-material-properties.md)**: 纹理在材质中的应用
- **[09-gpu-optimization.md](09-gpu-optimization.md)**: 纹理性能优化
- `../gltf/08-performance-optimization.md`: glTF纹理优化

---

**掌握纹理系统,创造精美视觉!**
