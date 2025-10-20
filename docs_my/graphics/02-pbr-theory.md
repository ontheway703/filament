# PBR理论详解

基于物理的渲染(Physically Based Rendering, PBR)是现代实时渲染的核心技术。本文详细讲解PBR的数学原理和实现细节。

---

## 目录

1. [什么是PBR](#什么是pbr)
2. [BRDF详解](#brdf详解)
3. [Cook-Torrance BRDF](#cook-torrance-brdf)
4. [能量守恒](#能量守恒)
5. [菲涅尔效应](#菲涅尔效应)
6. [在Filament中的实现](#在filament中的实现)

---

## 什么是PBR

### PBR的定义

**基于物理的渲染**是一套基于物理光学的渲染技术,遵循能量守恒和真实世界的物理规律。

### 传统渲染的问题

```glsl
// 传统Phong模型(不物理正确)
vec3 ambient = ka * ambientColor;
vec3 diffuse = kd * lightColor * max(0, dot(N, L));
vec3 specular = ks * lightColor * pow(max(0, dot(R, V)), shininess);

vec3 color = ambient + diffuse + specular;
```

**问题**:
- ❌ ka, kd, ks参数不直观
- ❌ 不同光照条件下表现不一致
- ❌ 不遵循能量守恒
- ❌ 难以获得真实感

### PBR的优势

- ✅ **物理正确**:基于光的物理传播规律
- ✅ **参数直观**:金属度、粗糙度等物理参数
- ✅ **光照一致**:在不同光照条件下表现一致
- ✅ **易于共享**:资产可在不同引擎间复用

### PBR的三大原则

#### 1. 能量守恒(Energy Conservation)

```
反射光 + 吸收光 = 入射光
```

反射的光不能超过入射的光。

#### 2. 微表面理论(Microfacet Theory)

宏观粗糙表面是由微观镜面组成:

```
宏观视图(粗糙表面):          微观视图(微小镜面):
╱‾‾╲_╱‾╲__╱‾‾‾╲           ╱│  │╲ │╲│  ╱│
                            微面元法线随机分布
```

#### 3. 菲涅尔效应(Fresnel Effect)

掠射角(grazing angle)的反射率高于垂直入射:

```
垂直看(低反射):    倾斜看(高反射):
    👁️                   👁️
    │                      ╲
    ↓                       ╲
 ───┴───                  ───┴───

水面:  5%反射              水面: 100%反射
```

---

## BRDF详解

### BRDF定义

**双向反射分布函数**(Bidirectional Reflectance Distribution Function):

```
        fr(ωi, ωo)
           ↑
    ωi ↗   │   ↖ ωo
         ┌─┴─┐
       ──┴───┴──
      表面法线 n
```

**数学定义**:
```
fr(ωi, ωo) = dLo(ωo) / dEi(ωi) = dLo(ωo) / [Li(ωi) cosθi dωi]

单位: 1/sr (1/球面度)
```

**物理含义**: BRDF描述光从ωi方向入射,ωo方向出射的比例。

### BRDF的性质

#### 1. Helmholtz互易性(Reciprocity)

```
fr(ωi, ωo) = fr(ωo, ωi)
```

光路可逆:入射和出射方向互换,BRDF值不变。

#### 2. 能量守恒

```
∫Ω fr(ωi, ωo) cosθi dωi ≤ 1

对所有出射方向ωo成立
```

反射能量不能超过入射能量。

### BRDF的分类

```
BRDF
├─ 漫反射BRDF (Diffuse BRDF)
│   └─ Lambert: fd = albedo / π
│
└─ 镜面反射BRDF (Specular BRDF)
    ├─ Phong (不物理正确)
    ├─ Blinn-Phong (不物理正确)
    └─ Cook-Torrance (PBR,物理正确) ✅
```

---

## Cook-Torrance BRDF

Filament和大多数PBR引擎使用的BRDF模型。

### 完整公式

```
fr(l, v) = (kd × fd) + (ks × fs)

fd = diffuse BRDF
fs = specular BRDF
kd = diffuse系数
ks = specular系数
l  = 光线方向
v  = 视线方向
```

### 镜面BRDF

```
fs = D(h) × G(l,v,h) × F(v,h)
     ─────────────────────────
       4 × (n·l) × (n·v)

D = Normal Distribution Function (法线分布函数)
G = Geometry Function (几何函数)
F = Fresnel Term (菲涅尔项)
h = 半向量 = normalize(l + v)
```

#### 1. D - 法线分布函数(NDF)

**GGX/Trowbridge-Reitz** (Filament使用):

```glsl
float D_GGX(float NoH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = (NoH * NoH) * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}
```

**物理含义**:
- 描述微面元法线分布
- roughness↑ → 分布更宽 → 高光更大更模糊
- roughness↓ → 分布更窄 → 高光更小更锐利

```
roughness = 0.1         roughness = 0.9
(光滑)                  (粗糙)

    │ 高                 ─┐
    │╱│╲                  │
   ─┴─┴─┴─              ──┴──
   窄尖峰              宽平台
```

#### 2. G - 几何函数

**Smith's Schlick-GGX** (Filament使用):

```glsl
float G_Smith(float NoV, float NoL, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;

    float GL = NoL / (NoL * (1.0 - k) + k);
    float GV = NoV / (NoV * (1.0 - k) + k);

    return GL * GV;
}
```

**物理含义**:
- 描述微面元的自遮挡(Shadowing)和自掩蔽(Masking)
- 粗糙表面微面元互相遮挡

```
Shadowing (光被遮挡):    Masking (视线被遮挡):
  光线                      👁️ 视线
   ↓                         ↓
   X───╲                    X───╲
 ──┴────┴──                ──┴────┴──
 被前面挡住                被前面挡住
```

#### 3. F - 菲涅尔项

**Fresnel-Schlick近似** (Filament使用):

```glsl
vec3 F_Schlick(float VoH, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
}
```

**F0的计算**:

对于**电介质**(非金属):
```glsl
float F0 = 0.16 * reflectance * reflectance;
// 默认reflectance=0.5 → F0=0.04 (4%反射)
```

对于**金属**:
```glsl
vec3 F0 = baseColor;  // 金属用baseColor作为F0
```

**混合**:
```glsl
vec3 F0 = mix(vec3(0.04), baseColor, metallic);
```

### 漫反射BRDF

**Lambertian**:
```glsl
vec3 fd = baseColor / PI;
```

**Disney Diffuse** (Filament可选):
```glsl
float Fd_Burley(float NoV, float NoL, float LoH, float roughness) {
    float f90 = 0.5 + 2.0 * roughness * LoH * LoH;
    float lightScatter = F_Schlick(NoL, 1.0, f90);
    float viewScatter = F_Schlick(NoV, 1.0, f90);
    return lightScatter * viewScatter / PI;
}
```

---

## 能量守恒

### 漫反射和镜面反射的关系

```
kd + ks = 1  (能量守恒)

kd: 漫反射系数
ks: 镜面反射系数
```

### Filament实现

```glsl
void material(inout MaterialInputs material) {
    prepareMaterial(material);

    // 基础参数
    vec4 baseColor = texture(materialParams_baseColorMap, getUV0());
    material.baseColor = baseColor;
    material.metallic = materialParams.metallic;
    material.roughness = materialParams.roughness;
}

// Filament内部计算(简化版)
vec3 computeLighting(MaterialInputs material, vec3 V, vec3 L) {
    vec3 H = normalize(V + L);
    float NoV = abs(dot(N, V)) + 1e-5;
    float NoL = clamp(dot(N, L), 0.0, 1.0);
    float NoH = clamp(dot(N, H), 0.0, 1.0);
    float VoH = clamp(dot(V, H), 0.0, 1.0);

    // F0计算
    vec3 F0 = mix(vec3(0.04), material.baseColor.rgb, material.metallic);

    // 镜面BRDF
    float D = D_GGX(NoH, material.roughness);
    float G = G_Smith(NoV, NoL, material.roughness);
    vec3 F = F_Schlick(VoH, F0);

    vec3 specular = (D * G * F) / (4.0 * NoV * NoL + 1e-5);

    // 能量守恒的漫反射系数
    vec3 kD = (1.0 - F) * (1.0 - material.metallic);
    vec3 diffuse = kD * material.baseColor.rgb / PI;

    // 最终光照
    return (diffuse + specular) * lightColor * NoL;
}
```

**关键点**:
1. `kD = (1.0 - F) * (1.0 - metallic)`:
   - `(1.0 - F)`: 未被镜面反射的能量用于漫反射
   - `(1.0 - metallic)`: 金属没有漫反射

2. 金属的特殊性:
   - `metallic = 1`: `kD = 0`,无漫反射
   - `metallic = 0`: 正常漫反射

---

## 菲涅尔效应

### 物理原理

**Fresnel方程**描述不同入射角的反射率:

```
垂直入射(θ=0°):          掠射(θ=90°):
      👁️                      👁️
      │                        ↘
      ↓                         ↘
   ───┴───                    ───┴───

反射率: F0 (4%对于水)    反射率: 100%
```

### 精确Fresnel方程

```
F(θ) = 1/2 × [(Rs + Rp)]

Rs = |n1 cosθi - n2 cosθt|²
     |n1 cosθi + n2 cosθt|

Rp = |n1 cosθt - n2 cosθi|²
     |n1 cosθt + n2 cosθi|

n1, n2: 两种介质的折射率
θi: 入射角
θt: 折射角
```

### Schlick近似(实时渲染)

```glsl
F(θ) = F0 + (1 - F0) × (1 - cosθ)⁵

F0: 垂直入射反射率
cosθ = V·H (视线与半向量的点积)
```

**优势**:
- 计算快速(仅一次pow)
- 精度足够(误差<2%)
- 适合实时渲染

### 常见材质的F0值

| 材质 | F0 (线性) | F0 (sRGB) | 说明 |
|-----|----------|----------|------|
| 水 | 0.02 | 50 | 垂直看几乎透明 |
| 塑料 | 0.04 | 60 | 默认值 |
| 玻璃 | 0.04-0.05 | 60-65 | |
| 宝石 | 0.05-0.17 | 65-110 | 高折射率 |
| 铁 | 0.56 | 190 | 金属 |
| 铜 | 0.95, 0.64, 0.54 | RGB | 有色金属 |
| 金 | 1.0, 0.86, 0.57 | RGB | 有色金属 |

**IOR与F0的关系**:
```
F0 = ((IOR - 1) / (IOR + 1))²

例如玻璃: IOR=1.5
F0 = ((1.5-1)/(1.5+1))² = 0.04
```

---

## 在Filament中的实现

### 材质定义

```glsl
material {
    name : PBR_Material,
    shadingModel : lit,  // PBR光照
    parameters : [
        { type : sampler2d, name : baseColorMap },
        { type : sampler2d, name : metallicRoughnessMap },
        { type : sampler2d, name : normalMap },
        { type : float, name : metallicFactor, default : 1.0 },
        { type : float, name : roughnessFactor, default : 1.0 }
    ],
    requires : [uv0, tangents]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        // BaseColor
        vec4 baseColor = texture(materialParams_baseColorMap, getUV0());
        material.baseColor = baseColor;

        // Metallic + Roughness
        vec4 mr = texture(materialParams_metallicRoughnessMap, getUV0());
        material.metallic = mr.b * materialParams.metallicFactor;
        material.roughness = mr.g * materialParams.roughnessFactor;

        // Normal
        vec3 normal = texture(materialParams_normalMap, getUV0()).xyz * 2.0 - 1.0;
        material.normal = normal;
    }
}
```

### 光照计算(内部)

```glsl
// Filament shading_model_standard.glsl 简化版
vec3 surfaceShading(
    const MaterialInputs material,
    const ShadingData shading,
    const Light light)
{
    vec3 h = normalize(shading.view + light.l);

    float NoV = shading.NoV;
    float NoL = saturate(dot(shading.normal, light.l));
    float NoH = saturate(dot(shading.normal, h));
    float LoH = saturate(dot(light.l, h));

    // F0
    vec3 f0 = computeF0(material.baseColor, material.metallic, material.reflectance);

    // Cook-Torrance
    float D = distribution(material.roughness, NoH, h);
    float V = visibility(material.roughness, NoV, NoL);
    vec3  F = fresnel(f0, LoH);

    // 镜面项
    vec3 Fr = (D * V) * F;

    // 漫反射系数(能量守恒)
    vec3 diffuseColor = (1.0 - material.metallic) * material.baseColor.rgb;
    vec3 Fd = diffuseColor * diffuse(material.roughness, NoV, NoL, LoH);

    // 合并
    return (Fd + Fr) * light.colorIntensity.rgb * light.attenuation * NoL;
}
```

### 环境光(IBL)

```glsl
vec3 evaluateIBL(const MaterialInputs material, const ShadingData shading) {
    vec3 n = shading.normal;
    vec3 v = shading.view;
    vec3 r = reflect(-v, n);

    // 漫反射IBL
    vec3 irradiance = texture(irradianceMap, n).rgb;
    vec3 diffuseIBL = irradiance * material.baseColor.rgb;

    // 镜面IBL
    float lod = perceptualRoughnessToLod(material.roughness);
    vec3 prefilteredColor = textureLod(specularMap, r, lod).rgb;

    vec2 dfg = texture(dfgLUT, vec2(shading.NoV, material.roughness)).rg;
    vec3 f0 = computeF0(material.baseColor, material.metallic, material.reflectance);
    vec3 specularIBL = prefilteredColor * (f0 * dfg.x + dfg.y);

    // 能量守恒
    vec3 kD = (1.0 - f0) * (1.0 - material.metallic);
    return kD * diffuseIBL + specularIBL;
}
```

---

## 总结

### PBR核心公式

```
BRDF = diffuse + specular

diffuse = (1-F) × (1-metallic) × baseColor / π

specular = D × G × F
           ───────────
           4(n·l)(n·v)

D = GGX法线分布
G = Smith几何函数
F = Fresnel菲涅尔项
```

### 关键要点

1. **能量守恒**: 反射 ≤ 入射
2. **微表面理论**: 宏观粗糙 = 微观镜面
3. **菲涅尔效应**: 掠射角反射率高
4. **金属vs非金属**:
   - 金属: 无漫反射,F0=baseColor
   - 非金属: 有漫反射,F0≈0.04

### 下一步

- **[05-material-properties.md](05-material-properties.md)**: 材质参数详解
- **[04-shader-programming.md](04-shader-programming.md)**: 在Shader中实现PBR
- `../material/05-graphics-concepts.md`: Filament材质系统中的PBR应用

---

**PBR是现代渲染的基石** - 理解物理原理,创造逼真视觉效果!
