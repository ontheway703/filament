# 材质属性详解

本文档详细讲解PBR材质系统中各个属性的物理含义、取值范围、使用方法和实际应用。

---

## 目录

1. [PBR材质参数总览](#pbr材质参数总览)
2. [BaseColor/Albedo](#basecoloralbedo)
3. [Metallic](#metallic)
4. [Roughness](#roughness)
5. [Reflectance](#reflectance)
6. [Normal](#normal)
7. [Ambient Occlusion](#ambient-occlusion)
8. [Emissive](#emissive)
9. [特殊材质属性](#特殊材质属性)
10. [常见材质配方](#常见材质配方)

---

## PBR材质参数总览

### 核心参数(Metallic-Roughness工作流)

| 参数 | 类型 | 范围 | 默认值 | 用途 |
|-----|------|------|--------|------|
| **baseColor** | vec4 | [0,1] | (1,1,1,1) | 基础颜色/反照率 |
| **metallic** | float | [0,1] | 0.0 | 金属度 |
| **roughness** | float | [0,1] | 1.0 | 粗糙度 |
| **reflectance** | float | [0.35,1] | 0.5 | 非金属反射率 |
| **normal** | vec3 | [-1,1] | (0,0,1) | 法线方向 |
| **ambientOcclusion** | float | [0,1] | 1.0 | 环境光遮蔽 |
| **emissive** | vec3/vec4 | [0,∞] | (0,0,0) | 自发光 |

### 扩展参数

| 参数 | Shading Model | 用途 |
|-----|--------------|------|
| **clearCoat** | lit | 清漆层强度 |
| **clearCoatRoughness** | lit | 清漆层粗糙度 |
| **clearCoatNormal** | lit | 清漆层法线 |
| **anisotropy** | lit | 各向异性强度 |
| **anisotropyDirection** | lit | 各向异性方向 |
| **subsurfaceColor** | subsurface/cloth | 次表面颜色 |
| **thickness** | subsurface | 厚度 |
| **subsurfacePower** | subsurface | 散射强度 |
| **sheenColor** | cloth | 光泽颜色 |
| **sheenRoughness** | cloth | 光泽粗糙度 |

---

## BaseColor/Albedo

### 物理含义

**反照率(Albedo)**: 材质固有颜色,表示材质反射的光的颜色和强度。

```
baseColor = 材质吸收某些波长后反射的颜色
```

### 值范围和约束

**RGB值**: [0, 1] (线性空间)

**物理约束**:
- 非金属: 通常 > 0.02 (sRGB: 50/255)
- 金属: 通常 > 0.5 (sRGB: 186/255)

**为什么有下限?**
```
纯黑(0,0,0)在现实世界不存在
最黑的材质(木炭): ~0.02 (反射2%的光)
```

### 常见材质的BaseColor值

#### 非金属

| 材质 | Linear RGB | sRGB (0-255) | 备注 |
|-----|-----------|--------------|------|
| **煤炭** | (0.02, 0.02, 0.02) | (50, 50, 50) | 最暗的自然材质 |
| **橡胶** | (0.04, 0.04, 0.04) | (60, 60, 60) | |
| **混凝土** | (0.25, 0.25, 0.25) | (137, 137, 137) | |
| **红砖** | (0.35, 0.14, 0.09) | (163, 98, 79) | |
| **木头(松木)** | (0.62, 0.47, 0.26) | (210, 183, 139) | |
| **雪** | (0.81, 0.81, 0.81) | (240, 240, 240) | 最亮的自然材质 |

#### 金属

| 材质 | Linear RGB | sRGB (0-255) | 备注 |
|-----|-----------|--------------|------|
| **铁** | (0.56, 0.57, 0.58) | (200, 201, 203) | 灰白色 |
| **铝** | (0.91, 0.92, 0.92) | (245, 246, 246) | 高反射 |
| **铜** | (0.95, 0.64, 0.54) | (250, 208, 192) | 橙红色 |
| **金** | (1.00, 0.86, 0.57) | (255, 235, 200) | 黄色 |
| **银** | (0.97, 0.96, 0.91) | (252, 250, 245) | 接近白色 |
| **铬** | (0.55, 0.56, 0.55) | (198, 199, 198) | 灰色 |

### 颜色空间注意事项

```glsl
// ❌ 错误: 直接使用sRGB值
material.baseColor = vec4(0.8, 0.2, 0.2, 1.0);  // 错误!

// ✅ 正确: 转换到线性空间
vec3 srgbColor = vec3(0.8, 0.2, 0.2);
vec3 linearColor = pow(srgbColor, vec3(2.2));  // Gamma校正
material.baseColor = vec4(linearColor, 1.0);

// ✅ 更好: 使用sRGB纹理(GPU自动转换)
material.baseColor = texture(materialParams_baseColorMap, getUV0());
```

### 纹理格式

```
BaseColor纹理应使用sRGB格式:
- PNG/JPEG: sRGB
- OpenGL: GL_SRGB8_ALPHA8
- Vulkan: VK_FORMAT_R8G8B8A8_SRGB
- Metal: MTLPixelFormatRGBA8Unorm_sRGB

GPU会自动转换到线性空间供Shader使用
```

### 使用示例

```glsl
// Filament材质定义
material {
    name : MyMaterial,
    shadingModel : lit,
    parameters : [
        { type : sampler2d, name : baseColorMap },
        { type : float3, name : baseColorFactor, default : [1.0, 1.0, 1.0] }
    ]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        
        vec4 baseColor = texture(materialParams_baseColorMap, getUV0());
        material.baseColor = baseColor * vec4(materialParams.baseColorFactor, 1.0);
    }
}
```

---

## Metallic

### 物理含义

**金属度**: 描述材质是金属(导体)还是非金属(电介质/绝缘体)。

```
metallic = 0: 电介质(塑料、石头、木头)
metallic = 1: 导体(铁、铜、金)
```

### 物理背景

**金属和非金属的根本区别**:

#### 非金属(metallic=0)
```
入射光 → 表面
    ↓
部分镜面反射(~4%) + 部分进入材质
    ↓
内部散射 → 漫反射
吸收特定波长 → 颜色
```

特点:
- 有漫反射
- 镜面反射无色(白色)
- F0 ≈ 0.04

#### 金属(metallic=1)
```
入射光 → 表面
    ↓
全部镜面反射(60-95%)
吸收特定波长 → 有色反射
    ↓
无漫反射(光无法进入内部)
```

特点:
- 无漫反射
- 镜面反射有色
- F0 = baseColor

### 值范围

**离散值**(推荐):
- `0.0`: 纯非金属
- `1.0`: 纯金属

**中间值**(特殊情况):
- `0.3-0.7`: 生锈金属、氧化金属
- 少用,因为物理上材质要么是金属要么不是

### 影响

```glsl
// metallic如何影响光照

// 1. 漫反射颜色
vec3 diffuseColor = baseColor * (1.0 - metallic);
// metallic=0: 使用baseColor
// metallic=1: 无漫反射(黑色)

// 2. F0(0°入射反射率)
vec3 F0 = mix(vec3(0.04), baseColor, metallic);
// metallic=0: F0=0.04(4%,无色)
// metallic=1: F0=baseColor(60-95%,有色)

// 3. 漫反射系数
vec3 kD = (1.0 - F) * (1.0 - metallic);
// metallic=1: kD=0(完全无漫反射)
```

### 纹理格式

```
Metallic纹理应使用线性格式:
- 单通道灰度图
- PNG(转为线性) / EXR
- OpenGL: GL_R8
- 存储在metallicRoughnessMap的B通道(glTF规范)
```

### 使用示例

```glsl
// 纯金属材质
material.baseColor = vec4(1.0, 0.86, 0.57, 1.0);  // 金色
material.metallic = 1.0;
material.roughness = 0.2;  // 抛光金属

// 纯非金属材质
material.baseColor = vec4(0.8, 0.1, 0.1, 1.0);  // 红色塑料
material.metallic = 0.0;
material.roughness = 0.5;

// 生锈金属(混合)
material.baseColor = texture(baseColorMap, uv);
material.metallic = texture(metallicMap, uv).r * 0.7;  // 部分金属
material.roughness = 0.8;  // 粗糙
```

### 常见错误

```glsl
// ❌ 错误: 非金属使用金属的baseColor
material.baseColor = vec4(0.95, 0.64, 0.54, 1.0);  // 铜色
material.metallic = 0.0;  // 但标记为非金属
// 结果: 看起来像铜色的塑料(不真实)

// ✅ 正确: 非金属使用暗淡颜色
material.baseColor = vec4(0.5, 0.3, 0.2, 1.0);  // 暗橙色
material.metallic = 0.0;

// ✅ 正确: 金属使用高亮颜色
material.baseColor = vec4(0.95, 0.64, 0.54, 1.0);  // 铜色
material.metallic = 1.0;
```

---

## Roughness

### 物理含义

**粗糙度**: 表面微观几何的粗糙程度。

```
roughness = 0: 完美镜面(微面元完全对齐)
roughness = 1: 完全粗糙(微面元随机分布)
```

### 微表面理论

```
roughness = 0.0          roughness = 0.5          roughness = 1.0
(光滑)                   (中等)                   (粗糙)

|||||||                  ||  | || |               | |  |  | |
镜面反射                 半镜面                   漫反射
```

### 视觉效果

**镜面高光形状**:
```
低roughness (0.0-0.3):   中roughness (0.3-0.7):   高roughness (0.7-1.0):
      *                       ***                      *****
     ***                     *****                    *******
      *                       ***                      *****

锐利集中                   较柔和                    宽阔柔和
```

### 感知线性

Filament使用**感知线性粗糙度**:

```glsl
// 实际使用的是 roughness²
float alpha = roughness * roughness;

// GGX法线分布函数
float D_GGX(float NoH, float roughness) {
    float a = roughness * roughness;  // alpha
    float a2 = a * a;
    float denom = (NoH * NoH) * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}
```

**为什么?**
```
线性粗糙度 0.5 看起来像 0.25 和 0.75 的中间
感知粗糙度 0.5 看起来像 0.0 和 1.0 的中间 ✓
```

### 值范围和约束

**范围**: [0, 1]

**实际建议**:
- 完美镜面(0.0): 仅理论值,现实中不存在
- 实用最小值: 0.04-0.1
- 常用范围: 0.2-0.8
- 完全粗糙(1.0): 类似Lambert漫反射

### 常见材质的Roughness值

| 材质 | Roughness | 视觉效果 |
|-----|-----------|---------|
| **镜子** | 0.0-0.05 | 完美反射 |
| **水** | 0.0-0.1 | 清晰反射 |
| **抛光金属** | 0.1-0.3 | 明显高光 |
| **塑料** | 0.3-0.6 | 柔和高光 |
| **磨砂金属** | 0.5-0.7 | 宽泛高光 |
| **石头** | 0.7-0.9 | 几乎无高光 |
| **粗糙木头** | 0.8-1.0 | 完全漫反射 |

### 纹理格式

```
Roughness纹理应使用线性格式:
- 单通道灰度图
- OpenGL: GL_R8
- 存储在metallicRoughnessMap的G通道(glTF规范)

注意: 有些工具导出的是Glossiness(光泽度)
需要转换: roughness = 1.0 - glossiness
```

### 粗糙度与光泽度

```glsl
// Glossiness工作流(旧)
float glossiness = texture(glossinessMap, uv).r;
float roughness = 1.0 - glossiness;

// Roughness工作流(现代PBR标准)
float roughness = texture(roughnessMap, uv).r;
```

### 使用示例

```glsl
// 动态粗糙度
material.roughness = materialParams.roughnessFactor;

// 纹理粗糙度
material.roughness = texture(materialParams_roughnessMap, getUV0()).g;

// 混合
material.roughness = texture(materialParams_roughnessMap, getUV0()).g 
                   * materialParams.roughnessFactor;

// Clamp到合理范围
material.roughness = clamp(material.roughness, 0.045, 1.0);
```

### 视觉调整技巧

```glsl
// 技巧1: 基于距离调整粗糙度(避免远处闪烁)
float distanceToCamera = length(cameraPos - worldPos);
material.roughness = max(material.roughness, distanceToCamera * 0.01);

// 技巧2: 防止纯镜面的数值问题
material.roughness = max(material.roughness, 0.045);

// 技巧3: 基于视角调整(模拟粗糙表面的方向性)
float NoV = abs(dot(normal, view)) + 1e-5;
material.roughness = material.roughness * mix(1.0, 0.5, NoV);
```

---

## Reflectance

### 物理含义

**反射率**: 非金属材质在0°入射角的镜面反射率。

```
reflectance控制非金属的F0值:

F0 = 0.16 × reflectance²

reflectance = 0.5 → F0 = 0.04 (4%反射,默认值)
```

### 物理背景

**Fresnel方程简化**:

对于电介质(非金属):
```
F0 = ((IOR - 1) / (IOR + 1))²

IOR: 折射率(Index of Refraction)
```

**reflectance与IOR的关系**:
```glsl
// IOR → reflectance
float reflectance = sqrt(F0) / 0.16;

// reflectance → F0
float F0 = 0.16 * reflectance * reflectance;

// reflectance → IOR (近似)
float IOR = (1.0 + 0.4 * reflectance) / (1.0 - 0.4 * reflectance);
```

### 值范围

**范围**: [0, 1]
**实用范围**: [0.35, 1.0]

```
reflectance < 0.35: 物理上不合理(IOR < 1.0)
reflectance = 0.5: 默认值(塑料、玻璃)
reflectance > 0.5: 高折射率材质(宝石、水晶)
```

### 常见材质的Reflectance值

| 材质 | IOR | Reflectance | F0 | 说明 |
|-----|-----|------------|-----|------|
| **空气/真空** | 1.0 | 0.0 | 0.0 | 理论最小值 |
| **水** | 1.33 | 0.35 | 0.02 | 最低实用值 |
| **眼睛(角膜)** | 1.38 | 0.39 | 0.024 | |
| **塑料** | 1.46 | 0.5 | 0.04 | **默认值** |
| **玻璃** | 1.52 | 0.56 | 0.05 | |
| **蓝宝石** | 1.77 | 0.79 | 0.10 | |
| **钻石** | 2.42 | 1.0 | 0.16 | 最高实用值 |

### 影响

```glsl
// reflectance仅影响非金属的F0

// 对于金属(metallic=1):
vec3 F0 = baseColor;  // reflectance被忽略

// 对于非金属(metallic=0):
float f0 = 0.16 * reflectance * reflectance;
vec3 F0 = vec3(f0);  // 无色反射

// 混合情况:
vec3 dielectricF0 = vec3(0.16 * reflectance * reflectance);
vec3 F0 = mix(dielectricF0, baseColor, metallic);
```

### 使用示例

```glsl
// 塑料(默认)
material.reflectance = 0.5;

// 玻璃
material.reflectance = 0.56;
material.roughness = 0.0;  // 光滑
material.baseColor.a = 0.1;  // 透明

// 宝石
material.reflectance = 0.8;
material.roughness = 0.05;  // 接近镜面

// 水
material.reflectance = 0.35;
material.roughness = 0.0;
material.baseColor.a = 0.05;  // 几乎透明
```

### 何时调整Reflectance

**大多数情况保持默认**(0.5):
- 塑料
- 橡胶
- 一般非金属

**需要调整的情况**:
- 水(0.35)
- 玻璃/水晶(0.5-0.6)
- 宝石(0.7-1.0)
- 眼睛/皮肤(0.35-0.4)

### 常见错误

```glsl
// ❌ 错误: 对金属设置reflectance
material.metallic = 1.0;
material.reflectance = 0.8;  // 无效,被忽略

// ✅ 正确: 金属不需要reflectance
material.metallic = 1.0;
material.baseColor = vec4(1.0, 0.86, 0.57, 1.0);  // 金色F0

// ❌ 错误: reflectance过低
material.reflectance = 0.2;  // 物理上不可能(IOR<1)

// ✅ 正确: 使用合理范围
material.reflectance = clamp(reflectance, 0.35, 1.0);
```

---

## Normal

### 物理含义

**法线**: 表面在某点的朝向,决定光照计算的方向。

```
        N (法线)
        ↑
        |
    ────┴────
      表面
```

### 法线贴图(Normal Map)

**用途**: 在不增加几何复杂度的情况下增加表面细节。

```
低模几何 + 法线贴图 = 高细节视觉效果

简单平面:          法线贴图:         结果(看起来凹凸):
─────────          ↗↑↖              凹凸不平的视觉
                   →↑←
                   ↘↑↙
```

### 坐标空间

**切线空间(Tangent Space)** - 标准存储格式:
```
X轴: Tangent(切线,红色通道)
Y轴: Bitangent(副切线,绿色通道)
Z轴: Normal(法线,蓝色通道)

纹理存储: RGB = (X,Y,Z) * 0.5 + 0.5
范围: [0,1] → [-1,1]
```

**为什么使用切线空间?**
- 可复用:同一法线贴图可用于不同模型
- 可旋转:模型旋转时法线贴图仍正确
- 压缩友好:大部分法线接近(0,0,1),蓝色通道可省略

### TBN矩阵

**切线空间 → 世界空间转换**:

```glsl
// 顶点着色器
in vec3 normal;
in vec4 tangent;  // xyz=切线方向, w=副切线方向(±1)

out mat3 vTBN;

void main() {
    vec3 N = normalize(normalMatrix * normal);
    vec3 T = normalize(normalMatrix * tangent.xyz);
    vec3 B = cross(N, T) * tangent.w;  // 副切线
    
    vTBN = mat3(T, B, N);  // 列向量: [T|B|N]
}

// 片段着色器
in mat3 vTBN;

void main() {
    // 采样法线贴图
    vec3 tangentNormal = texture(normalMap, uv).xyz * 2.0 - 1.0;
    
    // 转换到世界空间
    vec3 worldNormal = normalize(vTBN * tangentNormal);
    
    // 使用worldNormal进行光照计算
}
```

### 法线贴图格式

**OpenGL格式** (Filament使用):
```
R: Tangent X (右)
G: Tangent Y (上)
B: Tangent Z (指向观察者)

未扰动法线: RGB(128, 128, 255) 或 (0.5, 0.5, 1.0)
```

**DirectX格式** (需要转换):
```
R: Tangent X (右)
G: Tangent Y (下,翻转!)
B: Tangent Z

转换: G' = 1.0 - G
```

### 法线强度调整

```glsl
// 方法1: 缩放XY分量
vec3 tangentNormal = texture(normalMap, uv).xyz * 2.0 - 1.0;
tangentNormal.xy *= normalScale;  // normalScale: 0-2
tangentNormal = normalize(tangentNormal);

// 方法2: 线性插值
vec3 tangentNormal = texture(normalMap, uv).xyz * 2.0 - 1.0;
tangentNormal = normalize(mix(vec3(0,0,1), tangentNormal, normalStrength));
```

### 使用示例

```glsl
// Filament材质
material {
    name : NormalMapped,
    shadingModel : lit,
    parameters : [
        { type : sampler2d, name : normalMap },
        { type : float, name : normalScale, default : 1.0 }
    ],
    requires : [tangents]  // 必需切线属性
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        
        // 采样法线贴图
        vec3 normal = texture(materialParams_normalMap, getUV0()).xyz;
        normal = normal * 2.0 - 1.0;
        
        // 调整强度
        normal.xy *= materialParams.normalScale;
        
        // Filament会自动应用TBN变换
        material.normal = normalize(normal);
    }
}
```

### 法线重建(BC5压缩)

BC5格式仅存储RG通道,需要重建B通道:

```glsl
vec2 normalRG = texture(normalMap, uv).rg * 2.0 - 1.0;
float normalB = sqrt(1.0 - saturate(dot(normalRG, normalRG)));
vec3 normal = vec3(normalRG, normalB);
```

### 细节法线混合

混合多个法线贴图:

```glsl
// 方法1: Reoriented Normal Mapping (RNM)
vec3 blendNormals(vec3 n1, vec3 n2) {
    n1 = n1 * vec3( 2,  2, 2) + vec3(-1, -1,  0);
    n2 = n2 * vec3(-2, -2, 2) + vec3( 1,  1, -1);
    return normalize(n1 * dot(n1, n2) - n2 * n1.z);
}

// 使用
vec3 baseNormal = texture(normalMap1, uv).xyz;
vec3 detailNormal = texture(normalMap2, uv * 10.0).xyz;  // 平铺10次
vec3 combined = blendNormals(baseNormal, detailNormal);
material.normal = combined * 2.0 - 1.0;
```

### 常见问题

**1. 法线翻转**:
```glsl
// 问题: 凹陷变凸起
// 解决: 翻转Y通道
normal.y = 1.0 - normal.y;
```

**2. 接缝可见**:
```
原因: UV接缝处切线不连续
解决: 使用Mikk tangent space (Filament默认)
```

**3. 法线贴图太强/太弱**:
```glsl
// 调整normalScale参数
material.normal.xy *= normalScale;
```

---

## Ambient Occlusion

### 物理含义

**环境光遮蔽(AO)**: 描述环境光被几何遮挡的程度。

```
开阔处(AO=1):          缝隙处(AO=0):
    ⬇⬇⬇                  ⬇⬇⬇
  ─────────               ▕ ▏
   明亮                  ▕█▏
                         暗
```

### 作用

**增强细节阴影**:
```
无AO:                   有AO:
□□□□□                  □□□□□
□□□□□                  □▓▓▓□
□□□□□                  □▓█▓□
□□□□□                  □▓▓▓□
                        缝隙更暗
```

### 值范围

**范围**: [0, 1]
```
AO = 0: 完全遮蔽(深缝隙、角落)
AO = 0.5: 部分遮蔽
AO = 1: 无遮蔽(开阔表面)
```

### AO类型

#### 1. 预烘焙AO(离线)
```
- 质量最高
- 无运行时开销
- 存储在纹理或顶点颜色
- 不能移动物体
```

#### 2. SSAO(屏幕空间AO)
```
- 动态,实时计算
- 基于深度缓冲
- 有性能开销
- 仅局部遮挡
```

#### 3. HBAO+/GTAO(高级)
```
- 质量更高的SSAO
- 考虑法线
- 更大性能开销
```

### 纹理格式

```
AO贴图:
- 单通道灰度图(R通道)
- 线性空间
- OpenGL: GL_R8
- glTF: 可与metallicRoughness打包(R通道)
```

### 应用方式

```glsl
// 方法1: 直接相乘(常用)
vec3 finalColor = (diffuse + specular) * ao;

// 方法2: 仅影响间接光照
vec3 directLight = ...;
vec3 indirectLight = ... * ao;
vec3 finalColor = directLight + indirectLight;

// 方法3: 强度控制
float aoStrength = materialParams.aoStrength;
float ao = mix(1.0, aoValue, aoStrength);
vec3 finalColor = (diffuse + specular) * ao;
```

### 使用示例

```glsl
material {
    name : AOExample,
    shadingModel : lit,
    parameters : [
        { type : sampler2d, name : aoMap },
        { type : float, name : aoStrength, default : 1.0 }
    ]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        
        float ao = texture(materialParams_aoMap, getUV0()).r;
        
        // 强度调整
        ao = mix(1.0, ao, materialParams.aoStrength);
        
        material.ambientOcclusion = ao;
    }
}
```

### AO与其他通道的打包

glTF标准打包格式:
```
R: Ambient Occlusion
G: Roughness
B: Metallic
A: 未使用 或 高度/透明度
```

```glsl
vec4 orm = texture(ormMap, uv);
material.ambientOcclusion = orm.r;
material.roughness = orm.g;
material.metallic = orm.b;
```

### 常见错误

```glsl
// ❌ 错误: AO影响自发光
vec3 emissive = material.emissive * ao;  // 错误!

// ✅ 正确: AO不影响自发光
vec3 emissive = material.emissive;

// ❌ 错误: AO在sRGB空间
// 应使用线性纹理格式

// ✅ 正确: AO在线性空间
uniform sampler2D aoMap;  // 不使用sRGB格式
```

---

## Emissive

### 物理含义

**自发光**: 材质自己发出的光,不依赖外部光源。

```
自发光材质发出光子:
     ☀️
    ↗↑↖
   → ● ←
    ↘↓↙
```

### 值范围

**范围**: [0, ∞]
**实用范围**: [0, 10] (HDR)

```
emissive = (0,0,0): 不发光
emissive = (1,1,1): 白色,强度1
emissive = (5,0,0): 红色,强度5(HDR)
```

### Emissive vs Light

**重要**: 自发光**不照亮其他物体**!

```
自发光:                  光源:
   ●                      💡
  发光                   ↙↓↘
 但不照亮               照亮周围
  其他物体                ███
```

如需照亮周围,需要:
- 光源对象(Directional/Point/Spot Light)
- 实时全局光照(RTX GI)
- 烘焙光照贴图

### Emissive强度(HDR)

```glsl
// 低动态范围(LDR): [0,1]
material.emissive = vec3(1.0, 0.5, 0.0);

// 高动态范围(HDR): [0,∞]
material.emissive = vec3(10.0, 5.0, 0.0);  // 非常亮
```

### glTF扩展: KHR_materials_emissive_strength

```json
{
  "emissiveFactor": [1.0, 0.5, 0.0],
  "emissiveTexture": { "index": 0 },
  "extensions": {
    "KHR_materials_emissive_strength": {
      "emissiveStrength": 5.0
    }
  }
}
```

```glsl
// 实现
vec3 emissive = emissiveFactor * texture(emissiveTexture, uv).rgb;
emissive *= emissiveStrength;  // 扩展强度
```

### 纹理格式

```
Emissive贴图:
- RGB颜色
- sRGB格式(会转为线性)
- 支持HDR格式(EXR, RGBE)
```

### 使用示例

```glsl
// 示例1: 纯颜色自发光
material.emissive = vec4(1.0, 0.5, 0.0, 1.0);  // 橙色光

// 示例2: 纹理自发光
vec3 emissive = texture(materialParams_emissiveMap, getUV0()).rgb;
material.emissive = vec4(emissive, 0.0);

// 示例3: 带强度控制
vec3 emissive = texture(materialParams_emissiveMap, getUV0()).rgb;
emissive *= materialParams.emissiveStrength;
material.emissive = vec4(emissive, 0.0);

// 示例4: 动画自发光(脉冲效果)
float pulse = sin(frameUniforms.time * 5.0) * 0.5 + 0.5;
material.emissive = vec4(baseEmissive * pulse, 0.0);
```

### Bloom效果

自发光常配合Bloom后处理:

```cpp
// Filament启用Bloom
View::Builder viewBuilder;
viewBuilder.bloom(BloomOptions{
    .enabled = true,
    .strength = 0.2f,
    .threshold = 1.0f  // 仅emissive>1的部分发光
});
```

### 应用场景

**1. 霓虹灯**:
```glsl
material.baseColor = vec4(0.0);  // 黑色基底
material.emissive = vec4(0.0, 1.0, 1.0, 0.0) * 5.0;  // 青色霓虹
```

**2. LED显示屏**:
```glsl
vec3 screenColor = texture(screenTexture, uv).rgb;
material.emissive = vec4(screenColor * 2.0, 0.0);  // 明亮屏幕
```

**3. 魔法效果**:
```glsl
float glow = texture(glowMask, uv).r;
material.emissive = vec4(magicColor * glow * 10.0, 0.0);  // 强烈魔法光
```

**4. 火焰**:
```glsl
vec3 flameColor = texture(flameTexture, uv).rgb;
material.emissive = vec4(flameColor * 3.0, 0.0);
```

### 常见错误

```glsl
// ❌ 错误: 期望自发光照亮其他物体
material.emissive = vec4(10.0, 10.0, 10.0, 0.0);
// 结果: 仅自身发光,不影响周围

// ✅ 正确: 需要配合光源
Entity lightEntity = entityManager.create();
LightManager::Builder(LightManager::Type::POINT)
    .position({x, y, z})
    .color({1.0f, 1.0f, 1.0f})
    .intensity(10000.0f)
    .build(engine, lightEntity);

// ❌ 错误: 自发光受光照影响
vec3 finalColor = emissive * lighting;  // 错误!

// ✅ 正确: 自发光直接加到最终颜色
vec3 finalColor = lighting + emissive;
```

---

## 特殊材质属性

### ClearCoat(清漆层)

**用途**: 汽车漆、木器漆、碳纤维

```
        空气
      ┌─────┐
清漆层│透明│  ← clearCoat, clearCoatRoughness
      └─────┘
      ┌─────┐
基层  │彩色│  ← baseColor, metallic, roughness
      └─────┘
```

**参数**:
```glsl
material.clearCoat = 1.0;              // 清漆层强度 [0,1]
material.clearCoatRoughness = 0.1;     // 清漆粗糙度 [0,1]
material.clearCoatNormal = normalMap;  // 清漆法线(可选)
```

**效果**:
- 双层高光
- 更丰富的反射
- 真实的车漆效果

### Anisotropy(各向异性)

**用途**: 拉丝金属、毛发、缎面

```
各向同性(roughness):     各向异性(anisotropy):
      *                        ═══
     ***                       ═══
      *                        ═══
   圆形高光                  椭圆形高光
```

**参数**:
```glsl
material.anisotropy = 0.5;  // 各向异性强度 [-1,1]
material.anisotropyDirection = vec3(1,0,0);  // 方向
```

**方向**:
- 切线空间
- 通常沿U或V方向
- 可用纹理控制

### Subsurface(次表面散射)

**用途**: 皮肤、蜡烛、大理石、玉石

```
      光线
       ↓
   ════╧════  表面
    散射→ ←散射
       ↓
    内部传输
       ↓
   ════╧════  出射
```

**参数**:
```glsl
material.subsurfaceColor = vec3(0.8, 0.4, 0.3);  // 散射颜色
material.thickness = 0.5;                        // 厚度 [0,1]
material.subsurfacePower = 12.234;               // 散射强度
```

**Shading Model**:
```glsl
material {
    shadingModel : subsurface  // 必须!
}
```

### Sheen(光泽/织物)

**用途**: 天鹅绒、绒布、地毯

```
微纤维边缘反射:
    \|/  ← 光泽
   ─┴─┴─  织物表面
```

**参数**:
```glsl
material.sheenColor = vec3(1.0, 0.8, 0.6);  // 光泽颜色
material.sheenRoughness = 0.5;              // 光泽粗糙度
```

**Shading Model**:
```glsl
material {
    shadingModel : cloth  // 必须!
}
```

---

## 常见材质配方

### 金属材质

#### 抛光金
```glsl
material.baseColor = vec4(1.0, 0.86, 0.57, 1.0);  // 金色
material.metallic = 1.0;
material.roughness = 0.2;  // 抛光
material.reflectance = 0.5;  // 被忽略(metallic=1)
```

#### 磨砂铜
```glsl
material.baseColor = vec4(0.95, 0.64, 0.54, 1.0);  // 铜色
material.metallic = 1.0;
material.roughness = 0.6;  // 磨砂
```

#### 生锈铁
```glsl
vec4 albedo = texture(albedoMap, uv);  // 铁灰+铁锈橙混合
float metal = texture(metallicMap, uv).r;
material.baseColor = albedo;
material.metallic = metal * 0.5;  // 部分金属
material.roughness = 0.8;  // 粗糙
material.ambientOcclusion = texture(aoMap, uv).r;
```

### 非金属材质

#### 塑料
```glsl
material.baseColor = vec4(0.8, 0.1, 0.1, 1.0);  // 红色
material.metallic = 0.0;
material.roughness = 0.5;
material.reflectance = 0.5;  // 标准塑料
```

#### 橡胶
```glsl
material.baseColor = vec4(0.02, 0.02, 0.02, 1.0);  // 深色
material.metallic = 0.0;
material.roughness = 0.9;  // 几乎无高光
material.reflectance = 0.5;
```

#### 木头
```glsl
material.baseColor = texture(woodAlbedo, uv);
material.metallic = 0.0;
material.roughness = texture(woodRoughness, uv).r;  // 纹理变化
material.normal = texture(woodNormal, uv).xyz * 2.0 - 1.0;
material.ambientOcclusion = texture(woodAO, uv).r;
```

### 特殊材质

#### 汽车漆
```glsl
material.baseColor = vec4(0.8, 0.0, 0.0, 1.0);  // 红色车漆
material.metallic = 0.0;
material.roughness = 0.4;
material.clearCoat = 1.0;  // 清漆层
material.clearCoatRoughness = 0.1;  // 光滑清漆
```

#### 皮肤
```glsl
material.baseColor = vec4(0.95, 0.76, 0.70, 1.0);  // 肤色
material.metallic = 0.0;
material.roughness = 0.6;
material.subsurfaceColor = vec3(0.8, 0.4, 0.3);  // 红色散射
material.thickness = 0.5;
material.subsurfacePower = 12.234;
// shadingModel: subsurface
```

#### 玻璃
```glsl
material.baseColor = vec4(1.0, 1.0, 1.0, 0.05);  // 几乎透明
material.metallic = 0.0;
material.roughness = 0.0;  // 光滑
material.reflectance = 0.56;  // 玻璃IOR=1.5
// 需要启用透明混合
```

#### 天鹅绒
```glsl
material.baseColor = vec4(0.3, 0.1, 0.5, 1.0);  // 紫色
material.metallic = 0.0;
material.roughness = 1.0;
material.sheenColor = vec3(0.8, 0.6, 1.0);  // 淡紫光泽
material.sheenRoughness = 0.5;
// shadingModel: cloth
```

---

## 总结

### 核心参数速查

| 参数 | 用途 | 关键值 |
|-----|------|--------|
| **baseColor** | 固有颜色 | 非金属:暗色, 金属:亮色 |
| **metallic** | 金属/非金属 | 0 或 1 (少用中间值) |
| **roughness** | 光滑/粗糙 | 0.0-1.0 (建议>0.04) |
| **reflectance** | 非金属反射率 | 默认0.5 (范围0.35-1.0) |
| **normal** | 表面细节 | 切线空间,范围[-1,1] |
| **ao** | 环境遮蔽 | 0=遮蔽, 1=开阔 |
| **emissive** | 自发光 | HDR范围[0,∞] |

### 材质制作流程

1. **确定材质类型**: 金属 vs 非金属
2. **设置baseColor**: 参考真实材质
3. **设置metallic**: 0 或 1
4. **调整roughness**: 视觉效果
5. **添加法线**: 表面细节
6. **添加AO**: 增强深度
7. **特殊效果**: clearCoat, subsurface等

### 下一步学习

- **[06-texture-system.md](06-texture-system.md)**: 纹理采样和优化
- **[02-pbr-theory.md](02-pbr-theory.md)**: PBR数学原理
- `../material/05-graphics-concepts.md`: Filament材质系统应用

---

**理解材质属性,创造逼真视觉效果!**
