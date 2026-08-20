# 图形学概念

本文档介绍材质系统相关的计算机图形学核心概念。

---

## PBR 基础

### 什么是 PBR?

**PBR (Physically Based Rendering)** - 基于物理的渲染

- 模拟真实世界光的物理行为
- 基于能量守恒原则
- 在不同光照条件下表现一致
- 使用物理参数而非美术参数

### 为什么使用 PBR?

**传统渲染的问题**:
- 参数不直观（环境、高光、漫反射系数等）
- 在不同光照下表现不一致
- 难以获得真实感

**PBR 的优势**:
- 参数直观（金属度、粗糙度）
- 物理正确，光照一致
- 易于资产共享和复用

---

## BRDF - 双向反射分布函数

### 定义

**BRDF (Bidirectional Reflectance Distribution Function)**

```
BRDF(ωi, ωo, n) = dLo(ωo) / dEi(ωi)
```

- `ωi`: 入射光方向
- `ωo`: 出射光方向（视线方向）
- `n`: 表面法线
- `dLo`: 出射辐射度
- `dEi`: 入射辐照度

**物理含义**: 描述光线如何从表面反射

### Filament 使用的 BRDF

**Cook-Torrance 微面元 BRDF**:

```
f(l, v) = D(h) G(l, v, h) F(v, h) / (4 (n·l) (n·v))
```

其中：
- `D`: 法线分布函数 (NDF) - 微面元朝向分布
- `G`: 几何函数 - 微面元遮挡
- `F`: 菲涅尔项 - 反射率随角度变化
- `l`: 光线方向
- `v`: 视线方向
- `h`: 半向量 (l + v) / |l + v|

### 能量守恒

```
漫反射 + 镜面反射 ≤ 入射光
```

Filament 实现：

```glsl
vec3 diffuseColor = baseColor * (1.0 - metallic);
vec3 f0 = mix(vec3(0.04), baseColor, metallic);  // 基础反射率

// 菲涅尔项确保能量守恒
vec3 F = fresnelSchlick(f0, VdotH);
vec3 kD = (1.0 - F) * (1.0 - metallic);  // 漫反射系数

vec3 color = kD * diffuse + specular;
```

---

## 材质参数详解

### baseColor - 基础颜色

**物理含义**: 物体的固有颜色（反照率）

```glsl
material.baseColor = vec4(0.8, 0.2, 0.2, 1.0);  // 红色
```

**值范围**: RGB [0, 1]

**物理约束**:
- 非金属: 通常 > 0.02 (避免纯黑)
- 金属: 通常 > 0.5 (金属有高反射率)

**常见材质的 baseColor**:

| 材质 | sRGB 值 | Linear 值 |
|-----|---------|----------|
| 煤炭 | (50, 50, 50) | (0.02, 0.02, 0.02) |
| 混凝土 | (130, 130, 130) | (0.25, 0.25, 0.25) |
| 铁 | (196, 199, 199) | (0.56, 0.57, 0.57) |
| 金 | (255, 226, 155) | (1.0, 0.86, 0.57) |
| 铜 | (250, 208, 192) | (0.95, 0.64, 0.54) |

### metallic - 金属度

**物理含义**: 材质是金属还是非金属（电介质）

```glsl
material.metallic = 0.0;  // 非金属（绝缘体）
material.metallic = 1.0;  // 金属（导体）
```

**值范围**: [0, 1]

**物理背景**:
- 非金属: 光被散射（漫反射）
- 金属: 光被吸收或反射（无漫反射）

**影响**:
- metallic = 0: baseColor 用于漫反射
- metallic = 1: baseColor 用于镜面反射（F0）

**常见材质**:

| 材质类型 | metallic 值 |
|---------|------------|
| 木头、石头、塑料 | 0.0 |
| 生锈金属 | 0.3 - 0.7 |
| 纯金属 | 0.9 - 1.0 |

### roughness - 粗糙度

**物理含义**: 表面的微观粗糙程度

```glsl
material.roughness = 0.0;  // 完全光滑（镜面）
material.roughness = 1.0;  // 完全粗糙（漫反射）
```

**值范围**: [0, 1]

**物理效果**:
- roughness 低: 镜面高光集中、锐利
- roughness 高: 高光分散、柔和

**感知线性**: Filament 使用感知线性的粗糙度
- roughness² 用于实际计算
- 0.5 看起来是 0.0 和 1.0 的中间

**常见材质**:

| 材质 | roughness 值 |
|-----|-------------|
| 镜子、水 | 0.0 - 0.1 |
| 抛光金属 | 0.2 - 0.4 |
| 磨砂金属 | 0.5 - 0.7 |
| 粗糙石头 | 0.8 - 1.0 |

### reflectance - 反射率

**物理含义**: 非金属的镜面反射强度

```glsl
material.reflectance = 0.5;  // 默认（塑料、玻璃）
```

**值范围**: [0, 1]，通常 [0.35, 1.0]

**物理背景**: 计算 F0（0° 入射角反射率）

```glsl
// 非金属 F0 计算
float f0 = 0.16 * reflectance * reflectance;
// 默认 reflectance=0.5 → f0=0.04 (4%)
```

**常见材质**:

| 材质 | reflectance 值 | F0 |
|-----|---------------|-----|
| 水 | 0.35 | 0.02 |
| 塑料、玻璃 | 0.5 | 0.04 |
| 宝石 | 0.7 - 1.0 | 0.08 - 0.16 |

### normal - 法线贴图

**物理含义**: 表面法线方向

```glsl
// 从法线贴图采样（切线空间）
vec3 normalMap = texture(normalMap, uv).xyz * 2.0 - 1.0;
material.normal = normalMap;  // 在 prepareMaterial() 之前设置
```

**坐标空间**: 切线空间 (Tangent Space)

**格式**:
- R: X 方向
- G: Y 方向
- B: Z 方向（向上）
- 值范围: [-1, 1]，贴图存储为 [0, 1]

**转换到世界空间**:

```glsl
mat3 TBN = mat3(tangent, bitangent, normal);
vec3 worldNormal = normalize(TBN * normalMap);
```

### ambientOcclusion - 环境光遮蔽

**物理含义**: 环境光被遮挡的程度

```glsl
material.ambientOcclusion = texture(aoMap, uv).r;
```

**值范围**: [0, 1]
- 0: 完全遮挡（缝隙、凹陷）
- 1: 无遮挡（开阔处）

**效果**: 增加细节阴影，提升真实感

### emissive - 自发光

**物理含义**: 材质自己发出的光

```glsl
material.emissive = vec4(1.0, 0.5, 0.0, 5.0);  // RGB + 强度
```

**值范围**: RGB [0, ∞]，强度 [0, ∞]

**用途**: 霓虹灯、LED、魔法效果

**注意**: 不会照亮其他物体（仅自身发光）

---

## Shading Models 详解

### Lit - 标准 PBR

**用途**: 大部分材质（金属、塑料、石头、木头）

**可用参数**:
```glsl
material.baseColor
material.roughness
material.metallic
material.reflectance
material.normal
material.ambientOcclusion
material.emissive
material.clearCoat          // 透明涂层
material.clearCoatRoughness
material.anisotropy         // 各向异性
```

**光照计算**: 完整 PBR，包括直接光和间接光（IBL）

### Unlit - 无光照

**用途**: UI、粒子、天空盒、视频纹理

**可用参数**:
```glsl
material.baseColor  // 直接输出颜色
```

**特点**:
- 不受光照影响
- 性能最高
- 适合 2D 元素

### Subsurface - 次表面散射

**用途**: 皮肤、蜡烛、玉石、大理石

**额外参数**:
```glsl
material.thickness          // 厚度
material.subsurfacePower    // 散射强度
material.subsurfaceColor    // 散射颜色
```

**物理原理**: 光线穿透表面，内部散射后再出射

**视觉效果**: 半透明、柔和、有深度感

### Cloth - 布料

**用途**: 织物、天鹅绒、地毯

**额外参数**:
```glsl
material.sheenColor         // 光泽颜色
material.sheenRoughness     // 光泽粗糙度
material.subsurfaceColor    // 次表面颜色
```

**特点**:
- 边缘光泽（微纤维反射）
- 柔和的镜面高光
- 特殊的能量守恒模型

---

## Shader 基础

### 渲染管线

```
顶点数据 (CPU)
    ↓
顶点着色器 (GPU)
    ↓ 图元装配
    ↓ 光栅化
片段着色器 (GPU)
    ↓ 深度测试、混合
帧缓冲 (屏幕)
```

### 顶点着色器 vs 片段着色器

**顶点着色器** (Vertex Shader):
- 每个顶点执行一次
- 负责顶点变换
- 输出裁剪空间位置

```glsl
void materialVertex(inout MaterialVertexInputs material) {
    // 在世界空间修改顶点
    material.worldPosition += vec3(0, sin(time), 0);
}
```

**片段着色器** (Fragment Shader):
- 每个像素执行一次
- 负责计算颜色
- 输出最终像素颜色

```glsl
void material(inout MaterialInputs material) {
    prepareMaterial(material);
    material.baseColor = vec4(1, 0, 0, 1);  // 红色
}
```

### Varying 变量

在顶点和片段着色器间传递数据：

```
顶点着色器输出 → 插值 → 片段着色器输入
```

**自动 varying**:
- `vertex_uv0`: UV 坐标
- `vertex_color`: 顶点颜色
- `vertex_worldNormal`: 世界空间法线
- `vertex_worldPosition`: 世界空间位置

### Uniform vs Attribute

**Uniform** - 常量参数:
- 对所有顶点/片段相同
- 从 CPU 传递
- 例: 材质参数、光照参数

```glsl
uniform vec3 baseColor;  // 所有像素使用相同颜色
```

**Attribute** - 顶点属性:
- 每个顶点不同
- 存储在顶点缓冲区
- 例: 位置、法线、UV

```glsl
in vec3 position;  // 每个顶点有不同位置
```

### Sampler - 纹理采样器

```glsl
uniform sampler2D albedoMap;
vec4 color = texture(albedoMap, uv);
```

**采样参数**:
- 过滤模式 (nearest/linear/mipmap)
- 包裹模式 (repeat/clamp/mirror)
- 各向异性过滤

---

## 光照模型

### 直接光照

**方向光** (Directional Light):
```glsl
vec3 L = normalize(lightDirection);
float NoL = max(0.0, dot(N, L));
vec3 radiance = lightColor * lightIntensity * NoL;
```

**点光源** (Point Light):
```glsl
vec3 L = normalize(lightPosition - worldPosition);
float distance = length(lightPosition - worldPosition);
float attenuation = 1.0 / (distance * distance);
vec3 radiance = lightColor * lightIntensity * attenuation * NoL;
```

**聚光灯** (Spot Light):
```glsl
float theta = dot(L, normalize(-lightDirection));
float epsilon = innerCone - outerCone;
float intensity = clamp((theta - outerCone) / epsilon, 0.0, 1.0);
vec3 radiance = lightColor * lightIntensity * intensity * attenuation * NoL;
```

### 间接光照 (IBL)

**Image Based Lighting** - 基于图像的光照

**组成**:
1. **漫反射**: 使用环境贴图的辐照度卷积
2. **镜面反射**: 使用预滤波的环境贴图

```glsl
// 漫反射 IBL
vec3 irradiance = texture(irradianceMap, N).rgb;
vec3 diffuse = irradiance * albedo;

// 镜面 IBL
vec3 R = reflect(-V, N);
float lod = roughness * maxLOD;
vec3 prefilteredColor = textureLod(specularMap, R, lod).rgb;
vec2 envBRDF = texture(brdfLUT, vec2(NoV, roughness)).rg;
vec3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);
```

---

## 颜色空间

### sRGB vs Linear

**sRGB**:
- Gamma 编码的颜色空间
- 接近人眼感知
- 纹理和屏幕输出使用

**Linear**:
- 线性颜色空间
- 物理正确的光照计算
- Shader 内部使用

**转换**:

```glsl
// sRGB → Linear
vec3 linearColor = pow(srgbColor, vec3(2.2));
// 或使用精确公式
vec3 sRGBToLinear(vec3 srgb) {
    return mix(
        srgb / 12.92,
        pow((srgb + 0.055) / 1.055, vec3(2.4)),
        step(0.04045, srgb)
    );
}

// Linear → sRGB
vec3 srgbColor = pow(linearColor, vec3(1.0/2.2));
```

**注意**:
- 纹理采样自动转换（sRGB 纹理）
- 常量颜色需要手动转换
- baseColor 应该是 Linear

---

## 性能优化

### 1. 精度选择

```glsl
// 移动平台
highp float worldPosition;   // 世界坐标需要高精度
mediump float normal;        // 法线可用中精度
lowp float color;            // 颜色可用低精度
```

### 2. 减少纹理采样

```glsl
// 差
vec3 color = texture(albedo, uv).rgb;
float rough = texture(roughness, uv).r;
float metal = texture(metallic, uv).r;

// 好：打包纹理
vec4 packed = texture(packedMap, uv);
vec3 color = packed.rgb;
float rough = packed.a;
```

### 3. 避免分支

```glsl
// 差
if (useNormalMap) {
    N = normalize(TBN * normalMap);
}

// 好：使用 mix
N = mix(N, normalize(TBN * normalMap), float(useNormalMap));
```

### 4. 预计算

```glsl
// 差：每帧计算
float pi = 3.14159265359;
float invPi = 1.0 / pi;

// 好：使用常量
const float PI = 3.14159265359;
const float INV_PI = 0.31830988618;
```

---

## 常见材质配方

### 金属

```glsl
material.baseColor = vec4(0.95, 0.64, 0.54, 1.0);  // 铜色
material.metallic = 1.0;
material.roughness = 0.3;  // 抛光金属
```

### 塑料

```glsl
material.baseColor = vec4(0.8, 0.1, 0.1, 1.0);  // 红色塑料
material.metallic = 0.0;
material.roughness = 0.5;
material.reflectance = 0.5;  // 标准塑料反射
```

### 玻璃

```glsl
material.baseColor = vec4(1.0, 1.0, 1.0, 0.1);  // 几乎透明
material.metallic = 0.0;
material.roughness = 0.0;  // 光滑
material.reflectance = 0.5;
// 需要 transparent blending 和 refraction
```

### 皮肤

```glsl
// 使用 subsurface shading model
material.baseColor = vec4(0.95, 0.76, 0.70, 1.0);
material.metallic = 0.0;
material.roughness = 0.5;
material.subsurfaceColor = vec3(0.8, 0.4, 0.3);  // 红色散射
material.thickness = 0.5;
material.subsurfacePower = 12.234;
```

---

## 调试技巧

### 1. 可视化法线

```glsl
material.baseColor.rgb = normal * 0.5 + 0.5;  // 映射到 [0,1]
```

### 2. 可视化 UV

```glsl
material.baseColor.rgb = vec3(fract(uv), 0.0);
```

### 3. 可视化粗糙度

```glsl
material.baseColor.rgb = vec3(roughness);
```

### 4. 禁用光照

```glsl
material.baseColor.rgb = baseColor;
material.emissive = baseColor;  // 自发光，忽略光照
```

---

## 总结

理解这些核心概念对于创建高质量材质至关重要：

1. **PBR 原理**: 物理正确的光照模型
2. **BRDF**: 描述光的反射行为
3. **材质参数**: 每个参数的物理含义
4. **Shading Models**: 不同材质类型的光照计算
5. **Shader 基础**: 顶点和片段着色器的角色
6. **颜色空间**: Linear 空间进行计算
7. **性能优化**: 精度、纹理、分支优化

掌握这些知识将帮助你创建逼真、高效的材质。
