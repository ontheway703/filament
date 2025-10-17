# 材质定义语法

本文档详细说明 `.mat` 文件的完整语法和所有可用选项。

---

## .mat 文件结构

一个 `.mat` 文件包含三个主要部分：

```glsl
material {
    // 材质配置块
}

fragment {
    // 片段着色器代码
}

vertex {
    // 顶点着色器代码（可选）
}
```

---

## material 块配置

### 基本配置

#### name - 材质名称

```glsl
material {
    name : MaterialName
}
```

- **类型**: 字符串
- **必需**: 是
- **用途**: 材质的唯一标识符

#### shadingModel - 着色模型

```glsl
material {
    shadingModel : lit | unlit | subsurface | cloth | specularGlossiness
}
```

- **类型**: 枚举
- **默认**: lit
- **选项**:
  - `lit`: 标准 PBR 光照
  - `unlit`: 无光照（用于 UI、粒子、天空盒）
  - `subsurface`: 次表面散射（用于皮肤、蜡、玉石）
  - `cloth`: 布料（特殊的光照模型）
  - `specularGlossiness`: 镜面光泽工作流

#### materialDomain - 材质域

```glsl
material {
    materialDomain : surface | postProcess | compute
}
```

- **类型**: 枚举
- **默认**: surface
- **选项**:
  - `surface`: 表面材质（3D 物体）
  - `postProcess`: 后处理材质（全屏效果）
  - `compute`: 计算着色器材质

#### featureLevel - 特性级别

```glsl
material {
    featureLevel : 0 | 1 | 2 | 3
}
```

- **类型**: 整数 (0-3)
- **默认**: 1
- **说明**:
  - `0`: OpenGL ES 3.0 / GL 4.1 (最低)
  - `1`: OpenGL ES 3.1 / GL 4.5
  - `2`: 更高级特性
  - `3`: 最高特性级别

---

### 参数定义

#### parameters - 材质参数

```glsl
material {
    parameters : [
        {
            type : float | float2 | float3 | float4 | int | int2 | int3 | int4 |
                   bool | bool2 | bool3 | bool4 | mat3 | mat4 |
                   sampler2d | sampler2dArray | samplerCubemap | sampler3d | samplerExternal,
            name : parameterName,
            precision : default | low | medium | high  // 可选
        },
        // 更多参数...
    ]
}
```

**Uniform 参数示例**:

```glsl
parameters : [
    { type : float3, name : baseColor },
    { type : float, name : roughness },
    { type : float, name : metallic },
    { type : float4, name : emissive }
]
```

**Sampler 参数示例**:

```glsl
parameters : [
    { type : sampler2d, name : albedoMap },
    { type : sampler2d, name : normalMap },
    { type : sampler2d, name : roughnessMap },
    { type : samplerCubemap, name : envMap }
]
```

**精度修饰符**:

```glsl
parameters : [
    { type : float, name : time, precision : high },
    { type : float3, name : color, precision : medium },
    { type : float, name : alpha, precision : low }
]
```

#### constants - 常量参数

编译时常量，可在运行时特化：

```glsl
material {
    constants : [
        { type : float, name : pi, default : 3.14159 },
        { type : int, name : maxIterations, default : 10 },
        { type : bool, name : useNormalMap, default : true }
    ]
}
```

---

### 顶点属性

#### requires - 必需的顶点属性

```glsl
material {
    requires : [
        uv0,           // UV 坐标 0
        uv1,           // UV 坐标 1
        color,         // 顶点颜色
        tangents       // 切线和副切线
    ]
}
```

**可用属性**:

| 属性 | 说明 | 自动提供 |
|-----|------|---------|
| `position` | 顶点位置 | 是（总是） |
| `normal` | 顶点法线 | Lit 材质自动 |
| `tangents` | 切线、副切线 | 否 |
| `uv0` | UV 坐标 0 | 否 |
| `uv1` | UV 坐标 1 | 否 |
| `color` | 顶点颜色 | 否 |
| `bone_indices` | 骨骼索引 | 骨骼动画时 |
| `bone_weights` | 骨骼权重 | 骨骼动画时 |

---

### 混合模式

#### blending - 混合模式

```glsl
material {
    blending : opaque | transparent | add | multiply | masked | fade | screen
}
```

**混合模式详解**:

| 模式 | 说明 | 用途 |
|-----|------|------|
| `opaque` | 不透明（默认） | 实心物体 |
| `transparent` | 透明混合 | 玻璃、水 |
| `add` | 加法混合 | 光晕、粒子 |
| `multiply` | 乘法混合 | 阴影、覆盖 |
| `masked` | Alpha 遮罩 | 树叶、栅栏 |
| `fade` | 淡入淡出 | 渐变消失 |
| `screen` | 屏幕混合 | 高光 |

#### maskThreshold - 遮罩阈值

```glsl
material {
    blending : masked,
    maskThreshold : 0.4  // Alpha < 0.4 的片段被丢弃
}
```

- **类型**: 浮点数 (0.0-1.0)
- **默认**: 0.4
- **仅用于**: masked 模式

#### transparencyMode - 透明模式

```glsl
material {
    blending : transparent,
    transparencyMode : default | twoPassesOneSide | twoPassesTwoSides
}
```

- `default`: 单次渲染
- `twoPassesOneSide`: 两次渲染（先背面，后正面）
- `twoPassesTwoSides`: 两次渲染双面

---

### 剔除和深度

#### culling - 面剔除

```glsl
material {
    culling : none | back | front | frontAndBack
}
```

- **默认**: back
- **说明**:
  - `none`: 不剔除（双面渲染）
  - `back`: 剔除背面
  - `front`: 剔除正面
  - `frontAndBack`: 剔除所有

#### doubleSided - 双面材质

```glsl
material {
    doubleSided : true
}
```

- 等价于 `culling : none`
- 但启用双面光照

#### depthWrite - 深度写入

```glsl
material {
    depthWrite : true | false
}
```

- **默认**: opaque 材质为 true，transparent 为 false

#### depthCulling - 深度测试

```glsl
material {
    depthCulling : true | false
}
```

- **默认**: true

#### colorWrite - 颜色写入

```glsl
material {
    colorWrite : true | false
}
```

- **默认**: true
- **用途**: 只写深度的材质设为 false

---

### 高级特性

#### specularAntiAliasing - 镜面抗锯齿

```glsl
material {
    specularAntiAliasing : true,
    specularAntiAliasingVariance : 0.15,    // 屏幕空间方差
    specularAntiAliasingThreshold : 0.2     // 阈值
}
```

- 减少低粗糙度材质的闪烁
- 仅 lit 材质

#### clearCoatIorChange - 透明涂层 IoR 变化

```glsl
material {
    clearCoatIorChange : true | false
}
```

- **默认**: true
- 透明涂层层的折射率变化

#### flipUV - 翻转 UV

```glsl
material {
    flipUV : true | false
}
```

- **默认**: true (翻转 V 坐标)
- OpenGL 约定

#### vertexDomain - 顶点域

```glsl
material {
    vertexDomain : object | world | view | device
}
```

- **默认**: object
- **说明**:
  - `object`: 对象空间
  - `world`: 世界空间
  - `view`: 观察空间
  - `device`: 设备空间（NDC）

#### interpolation - 插值模式

```glsl
material {
    interpolation : smooth | flat
}
```

- **默认**: smooth
- **说明**:
  - `smooth`: 平滑插值
  - `flat`: 平坦着色（无插值）

#### refractionMode - 折射模式

```glsl
material {
    refractionMode : none | cubemap | screenSpace
}
```

- **默认**: none
- **说明**:
  - `none`: 无折射
  - `cubemap`: 使用环境贴图折射
  - `screenSpace`: 屏幕空间折射

#### refractionType - 折射类型

```glsl
material {
    refractionMode : screenSpace,
    refractionType : solid | thin
}
```

- `solid`: 实心物体（两次折射）
- `thin`: 薄物体（单次折射）

#### reflectionMode - 反射模式

```glsl
material {
    reflectionMode : default | screenSpace
}
```

- `default`: 使用环境贴图
- `screenSpace`: 屏幕空间反射（SSR）

---

### 变体过滤

#### variantFilter - 变体过滤

```glsl
material {
    variantFilter : [
        directionalLighting,
        dynamicLighting,
        shadowReceiver,
        skinning,
        fog,
        vsm,
        ssr,
        picking
    ]
}
```

- 排除不需要的变体，减少编译时间和包大小

---

## fragment 块

### 材质函数签名

**Surface Domain**:

```glsl
fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);  // 必须调用！

        // 设置材质属性...
    }
}
```

**PostProcess Domain**:

```glsl
fragment {
    void postProcess(inout PostProcessInputs postProcess) {
        // 设置后处理输出...
        postProcess.color = vec4(1.0);
    }
}
```

### MaterialInputs 可用字段

#### 所有 Shading Model

```glsl
material.baseColor : vec4      // 基础颜色 + Alpha
```

#### Lit Shading Model

```glsl
// PBR 核心参数
material.roughness : float           // 粗糙度 [0, 1]
material.metallic : float            // 金属度 [0, 1]
material.reflectance : float         // 反射率 [0, 1]（默认 0.5）

// 额外光照信息
material.ambientOcclusion : float    // 环境光遮蔽 [0, 1]
material.emissive : vec4             // 自发光 (RGB + 强度)

// 法线
material.normal : vec3               // 切线空间法线

// 透明涂层
material.clearCoat : float           // 透明涂层强度 [0, 1]
material.clearCoatRoughness : float  // 透明涂层粗糙度 [0, 1]
material.clearCoatNormal : vec3      // 透明涂层法线

// 各向异性
material.anisotropy : float          // 各向异性强度 [-1, 1]
material.anisotropyDirection : vec3  // 各向异性方向

// 次表面散射/透射
material.thickness : float           // 厚度
material.subsurfacePower : float     // 次表面功率
material.subsurfaceColor : vec3      // 次表面颜色

// Specular 工作流
material.specularColor : vec3        // 镜面反射颜色
material.glossiness : float          // 光泽度
```

#### Cloth Shading Model

```glsl
material.sheenColor : vec3           // 光泽颜色
material.sheenRoughness : float      // 光泽粗糙度
material.subsurfaceColor : vec3      // 次表面颜色
```

### 内置辅助函数

#### prepareMaterial

```glsl
void prepareMaterial(inout MaterialInputs material);
```

- **必须**在材质函数中调用
- 初始化材质系统

#### 顶点属性访问

```glsl
vec2 getUV0()                  // UV 坐标 0
vec2 getUV1()                  // UV 坐标 1
vec4 getColor()                // 顶点颜色
vec3 getPosition()             // 顶点位置（对象空间）
vec3 getWorldPosition()        // 世界空间位置
vec3 getWorldNormal()          // 世界空间法线
vec3 getWorldTangent()         // 世界空间切线
vec3 getWorldBitangent()       // 世界空间副切线
vec3 getWorldViewVector()      // 世界空间视向量
```

#### 纹理采样

```glsl
vec4 texture(sampler2D s, vec2 uv)
vec4 texture(samplerCubemap s, vec3 dir)
vec4 texture(sampler2DArray s, vec3 uvw)
```

#### 颜色空间转换

```glsl
vec3 sRGBToLinear(vec3 srgb)   // sRGB → Linear
vec3 linearToSRGB(vec3 linear) // Linear → sRGB
```

#### 数学辅助

```glsl
float saturate(float x)        // clamp(x, 0, 1)
vec3 saturate(vec3 v)
```

---

## vertex 块

### 顶点函数签名

**Surface Domain**:

```glsl
vertex {
    void materialVertex(inout MaterialVertexInputs material) {
        // 修改顶点属性...
    }
}
```

### MaterialVertexInputs 可用字段

```glsl
// 输入（只读）
material.worldPosition : vec3      // 世界空间位置

// 输出（可修改）
material.clipSpacePosition : vec4  // 裁剪空间位置（gl_Position）

// 自定义 varying（需要在 material 块中声明）
material.variable[0..4] : vec4     // 自定义变量
```

### Custom Variables

声明自定义 varying 变量：

```glsl
material {
    variables : [
        { name : myCustomData, type : float4 },
        { name : anotherData, type : float2 }
    ]
}

vertex {
    void materialVertex(inout MaterialVertexInputs material) {
        material.myCustomData = vec4(1.0, 2.0, 3.0, 4.0);
    }
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        vec4 data = variable_myCustomData;  // 访问自定义变量
        material.baseColor = data;
    }
}
```

---

## 完整示例

### 示例 1: 简单 PBR 材质

```glsl
material {
    name : SimplePBR,
    shadingModel : lit,
    parameters : [
        { type : float3, name : baseColor },
        { type : float, name : roughness },
        { type : float, name : metallic }
    ]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        material.baseColor.rgb = materialParams.baseColor;
        material.roughness = materialParams.roughness;
        material.metallic = materialParams.metallic;
    }
}
```

### 示例 2: 纹理 PBR 材质

```glsl
material {
    name : TexturedPBR,
    shadingModel : lit,
    parameters : [
        { type : sampler2d, name : albedoMap },
        { type : sampler2d, name : normalMap },
        { type : sampler2d, name : roughnessMap },
        { type : sampler2d, name : aoMap }
    ],
    requires : [uv0, tangents]
}

fragment {
    void material(inout MaterialInputs material) {
        vec2 uv = getUV0();

        // 法线贴图（在 prepareMaterial 之前）
        material.normal = texture(materialParams_normalMap, uv).xyz * 2.0 - 1.0;

        prepareMaterial(material);

        // 其他贴图
        material.baseColor = texture(materialParams_albedoMap, uv);
        material.roughness = texture(materialParams_roughnessMap, uv).r;
        material.ambientOcclusion = texture(materialParams_aoMap, uv).r;
    }
}
```

### 示例 3: 透明材质

```glsl
material {
    name : Transparent,
    shadingModel : lit,
    blending : transparent,
    transparencyMode : twoPassesOneSide,
    depthWrite : false,
    parameters : [
        { type : float3, name : color },
        { type : float, name : alpha }
    ]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        material.baseColor = vec4(materialParams.color, materialParams.alpha);
    }
}
```

### 示例 4: 自定义顶点着色器

```glsl
material {
    name : VertexAnimation,
    shadingModel : lit,
    parameters : [
        { type : float, name : time }
    ]
}

vertex {
    void materialVertex(inout MaterialVertexInputs material) {
        // 波浪动画
        float wave = sin(material.worldPosition.x * 2.0 + materialParams.time) * 0.5;
        material.worldPosition.y += wave;
    }
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        material.baseColor.rgb = vec3(0.8);
    }
}
```

### 示例 5: Unlit 材质（UI）

```glsl
material {
    name : UIElement,
    shadingModel : unlit,
    blending : transparent,
    depthWrite : false,
    depthCulling : false,
    parameters : [
        { type : sampler2d, name : texture },
        { type : float4, name : tint }
    ],
    requires : [uv0, color]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        vec4 texColor = texture(materialParams_texture, getUV0());
        material.baseColor = texColor * materialParams.tint * getColor();
    }
}
```

---

## 最佳实践

1. **总是调用 prepareMaterial()**: 在设置其他属性之前
2. **法线贴图在前**: 在 prepareMaterial() 之前设置 material.normal
3. **使用精度修饰符**: 移动平台上使用 medium/low 精度提升性能
4. **避免分支**: shader 中避免 if/else，使用 mix()
5. **纹理采样**: 尽量减少纹理采样次数
6. **变体过滤**: 只编译需要的变体

---

## 语法速查表

| 配置项 | 类型 | 默认值 | 说明 |
|-------|------|--------|------|
| `name` | string | - | 材质名称（必需） |
| `shadingModel` | enum | lit | 着色模型 |
| `blending` | enum | opaque | 混合模式 |
| `culling` | enum | back | 面剔除 |
| `depthWrite` | bool | true | 深度写入 |
| `depthCulling` | bool | true | 深度测试 |
| `doubleSided` | bool | false | 双面 |
| `transparencyMode` | enum | default | 透明模式 |
| `maskThreshold` | float | 0.4 | 遮罩阈值 |
| `specularAntiAliasing` | bool | false | 镜面抗锯齿 |
| `vertexDomain` | enum | object | 顶点域 |
| `featureLevel` | int | 1 | 特性级别 |

---

## 参考资源

- [Filament Materials Guide](https://google.github.io/filament/Materials.html)
- [PBR 参数说明](https://google.github.io/filament/Material%20Properties.html)
