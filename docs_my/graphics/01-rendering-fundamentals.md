# 渲染基础

本文档介绍实时渲染的核心概念,包括渲染方程、光的物理特性、渲染管线总览等基础知识。

---

## 目录

1. [什么是渲染](#什么是渲染)
2. [光的物理特性](#光的物理特性)
3. [渲染方程](#渲染方程)
4. [实时渲染vs离线渲染](#实时渲染vs离线渲染)
5. [光照类型](#光照类型)
6. [渲染管线概览](#渲染管线概览)
7. [渲染流程示例](#渲染流程示例)

---

## 什么是渲染

**渲染(Rendering)**是将3D场景转换为2D图像的过程。

```
3D场景数据        渲染器         2D图像
   ↓               ↓             ↓
(模型、材质、    (物理模拟、    (屏幕上的
 光照、相机)      光线追踪)      像素颜色)
```

### 渲染的本质

渲染本质上是回答一个问题:**这个像素应该是什么颜色?**

```
对于屏幕上的每个像素:
  1. 从相机发出一条射线穿过该像素
  2. 计算射线与场景中物体的交点
  3. 在交点处计算光照(直接光+间接光)
  4. 根据材质属性计算反射颜色
  5. 返回该像素的最终颜色
```

---

## 光的物理特性

### 光的电磁波特性

光是电磁波,由**波长(λ)**决定颜色:

```
可见光谱:
380nm ←────────────────────→ 750nm
紫色    蓝   绿   黄   橙    红色
```

- **波长**: 380nm-750nm (纳米)
- **频率**: c/λ (光速/波长)
- **能量**: h×f (普朗克常数×频率)

### 辐射度量学(Radiometry)

描述光的物理量:

#### 1. 辐射能(Radiant Energy) - Q
单位:**焦耳 (J)**

光携带的总能量。

#### 2. 辐射通量(Radiant Flux/Power) - Φ
单位:**瓦特 (W)** = J/s

单位时间内的辐射能:
```
Φ = dQ/dt
```

**例子**:
- 100W白炽灯泡: Φ ≈ 15W (可见光)
- 太阳: Φ ≈ 3.8×10²⁶ W

#### 3. 辐射强度(Radiant Intensity) - I
单位:**坎德拉 (cd)** = W/sr (瓦特/球面度)

单位立体角内的辐射通量:
```
I = dΦ/dω

ω: 立体角 (steradians)
```

**立体角**:
```
完整球面: 4π steradians
半球面: 2π steradians
```

#### 4. 辐照度(Irradiance) - E
单位:**W/m²**

单位面积接收的辐射通量:
```
E = dΦ/dA

dA: 表面微元面积
```

**例子**:
- 太阳在地球表面的辐照度: E ≈ 1000 W/m²

#### 5. 辐射率(Radiance) - L
单位:**W/(m²·sr)**

单位面积、单位立体角的辐射通量:
```
L = d²Φ / (dA·dω·cosθ)

θ: 光线与法线的夹角
```

**含义**:
- **最重要的量**:描述光线在特定方向的强度
- 光线传播过程中**Radiance不变**(在真空中)
- 我们在shader中计算的就是Radiance

### 光度学(Photometry)

考虑人眼感知的光度量:

| 辐射度量 | 光度量 | 单位 | 关系 |
|---------|--------|------|-----|
| Radiant Flux | Luminous Flux | 流明 (lm) | 加权积分 |
| Radiant Intensity | Luminous Intensity | 坎德拉 (cd) | V(λ)权重 |
| Irradiance | Illuminance | 勒克斯 (lux) | lm/m² |
| Radiance | Luminance | cd/m² | - |

**人眼视觉敏感度曲线 V(λ)**:
```
     1.0 ┤    峰值@555nm(绿色)
         │    ╱‾‾‾╲
         │   ╱     ╲
     0.5 ┤  ╱       ╲
         │ ╱         ╲
         │╱___________╲___
     0.0 ┴────────────────
        400nm  555nm  700nm
```

---

## 渲染方程

### 反射方程(Rendering Equation)

**Kajiya, 1986**提出的渲染方程是图形学的核心:

```
Lo(p, ωo) = Le(p, ωo) + ∫Ω fr(p, ωi, ωo) × Li(p, ωi) × (n·ωi) dωi
```

**符号说明**:
- `Lo(p, ωo)`: 点p沿ωo方向的**出射辐射率**
- `Le(p, ωo)`: 点p的**自发光辐射率**
- `fr(p, ωi, ωo)`: **BRDF** (双向反射分布函数)
- `Li(p, ωi)`: 点p来自ωi方向的**入射辐射率**
- `n·ωi`: 入射方向与法线的**余弦项**
- `Ω`: 半球面所有方向
- `dωi`: 微分立体角

### 物理含义

```
出射光 = 自发光 + 所有入射光经BRDF反射后的积分
```

#### 1. 自发光项 Le

```
Le(p, ωo) = emissive color
```

- LED灯、霓虹灯、火焰等自己发光
- 在Filament中通过`material.emissive`设置

#### 2. 反射项

```
∫Ω fr × Li × cosθ dωi
```

**分解**:
- `Li(p, ωi)`: 入射光的强度和颜色
- `fr(p, ωi, ωo)`: BRDF决定光如何反射
- `cosθ = n·ωi`: Lambert余弦定律(倾斜入射能量降低)
- `∫Ω ... dωi`: 对半球面所有方向积分

### 简化理解

对于单个光源:
```
Lo = Le + fr × Li × max(0, n·l)

l: 光源方向
```

对于多个光源:
```
Lo = Le + Σ (fr × Li × max(0, n·l))  // 直接光照
       + ∫Ω fr × Lenv × cosθ dω      // 环境光(间接光照)
```

---

## 实时渲染vs离线渲染

### 实时渲染(Real-Time Rendering)

**目标**:达到交互帧率(30-60+ FPS)

**特点**:
- ✅ **快速**:每帧预算 16-33ms
- ✅ **可交互**:游戏、VR、AR
- ❌ **近似**:使用各种技巧近似真实光照
- ❌ **有限反射**:通常只计算1-2次反射

**技术**:
- 光栅化(Rasterization)
- Z-Buffer深度缓冲
- 屏幕空间技术(SSAO, SSR)
- 预计算(IBL、光照贴图)

**应用**:
- 游戏引擎(Unity, Unreal, Filament)
- CAD/CAM实时预览
- VR/AR应用

### 离线渲染(Offline Rendering)

**目标**:生成照片级真实图像,不考虑时间

**特点**:
- ✅ **精确**:物理正确的光线传输
- ✅ **全局光照**:无限次反射
- ✅ **高质量**:电影级画质
- ❌ **慢**:每帧数分钟到数小时

**技术**:
- 路径追踪(Path Tracing)
- 光子映射(Photon Mapping)
- 双向路径追踪(BDPT)
- Metropolis Light Transport

**应用**:
- 电影特效(Pixar RenderMan)
- 建筑可视化
- 产品渲染

### 对比表

| 特性 | 实时渲染 | 离线渲染 |
|-----|---------|---------|
| **帧率** | 30-60+ FPS | 1帧/小时 |
| **算法** | 光栅化 | 光线追踪 |
| **反射** | 1-2次 | 无限次 |
| **全局光照** | 近似(IBL) | 精确(路径追踪) |
| **质量** | 中等 | 照片级 |
| **交互性** | ✅ | ❌ |

### 实时光线追踪(RTX)

**现代GPU**支持硬件加速光线追踪:
- NVIDIA RTX系列
- AMD RDNA 2
- DirectX Raytracing (DXR)

```
混合渲染:
  光栅化(主要渲染) + 光线追踪(反射、阴影、GI)
```

仍无法达到离线渲染质量,但大幅提升实时画质。

---

## 光照类型

### 直接光照(Direct Lighting)

光从光源**直接**照射到表面:

```
     光源
      ★
      │
      ↓
   ───┴───  ← 表面
```

**计算公式**:
```glsl
vec3 L = normalize(lightPos - worldPos);  // 光线方向
float NoL = max(0.0, dot(N, L));          // 余弦项
vec3 radiance = lightColor * lightIntensity * NoL;
```

**光源类型**:

#### 1. 方向光(Directional Light)
```
太阳光,无限远,平行光线

    ↓ ↓ ↓ ↓ ↓
  ─────────────
```

```glsl
vec3 L = -lightDirection;  // 固定方向
float NoL = max(0.0, dot(N, L));
vec3 radiance = lightColor * NoL;
```

#### 2. 点光源(Point Light)
```
灯泡,向四周发射光

      ★
    ↙ ↓ ↘
   ────────
```

```glsl
vec3 L = normalize(lightPos - worldPos);
float distance = length(lightPos - worldPos);
float attenuation = 1.0 / (distance * distance);  // 平方衰减
vec3 radiance = lightColor * attenuation * max(0, dot(N, L));
```

#### 3. 聚光灯(Spot Light)
```
手电筒,锥形光束

      ★
      ╲│╱
       V
```

```glsl
vec3 L = normalize(lightPos - worldPos);
float theta = dot(L, normalize(-lightDirection));
float epsilon = innerCone - outerCone;
float intensity = clamp((theta - outerCone) / epsilon, 0.0, 1.0);
vec3 radiance = lightColor * intensity * attenuation * max(0, dot(N, L));
```

### 间接光照(Indirect Lighting / Global Illumination)

光经过**多次反射**后照射到表面:

```
  光源
   ★
   │
   ↓
 ──┴──  ← 墙壁1 (反射)
   ↓
   ↓
 ──┴──  ← 墙壁2 (接收间接光)
```

**特点**:
- 产生柔和的环境光
- 物体之间的颜色渗透(Color Bleeding)
- 增加场景真实感

**实时渲染中的近似方法**:

#### 1. 环境光(Ambient Light) - 最简单
```glsl
vec3 ambient = ambientColor * baseColor;
```

#### 2. 图像光照(IBL - Image Based Lighting) - Filament使用
```glsl
// 使用环境贴图预计算的辐照度
vec3 irradiance = texture(irradianceMap, N).rgb;
vec3 diffuse = irradiance * baseColor;

// 镜面反射
vec3 R = reflect(-V, N);
vec3 prefilteredColor = textureLod(specularMap, R, roughness * maxLOD).rgb;
vec3 specular = prefilteredColor * F;
```

#### 3. 光照贴图(Lightmap)
预计算并存储在纹理中:
```glsl
vec3 lightmap = texture(lightmapTexture, lightmapUV).rgb;
vec3 color = baseColor * lightmap;
```

#### 4. 屏幕空间环境光遮蔽(SSAO)
近似小范围遮挡:
```glsl
float ao = texture(ssaoTexture, screenUV).r;
vec3 color = baseColor * ao;
```

---

## 渲染管线概览

### 完整渲染流程

```
应用阶段 (CPU)
    ↓
  场景管理
  可见性剔除
  提交渲染命令
    ↓
═══════════════════ GPU开始 ═══════════════════
    ↓
顶点着色器 (Vertex Shader)
  - 模型变换 (Model)
  - 视图变换 (View)
  - 投影变换 (Projection)
  - 输出裁剪空间坐标
    ↓
图元装配 (Primitive Assembly)
  - 组装三角形
    ↓
裁剪 (Clipping)
  - 裁剪视锥体外的三角形
    ↓
光栅化 (Rasterization)
  - 三角形 → 片段(像素候选)
  - 插值顶点属性
    ↓
片段着色器 (Fragment Shader)
  - 计算像素颜色
  - 纹理采样
  - 光照计算
    ↓
深度测试 (Depth Test)
  - Z-Buffer深度比较
    ↓
模板测试 (Stencil Test)
  - 模板缓冲比较
    ↓
混合 (Blending)
  - Alpha混合
  - 加法/乘法混合
    ↓
帧缓冲 (Framebuffer)
  - 输出到屏幕
═══════════════════ GPU结束 ═══════════════════
```

### 可编程阶段 vs 固定阶段

**可编程阶段**(你可以控制):
- ✅ **顶点着色器**: 顶点变换、动画
- ✅ **片段着色器**: 光照、材质、纹理

**固定阶段**(硬件控制):
- ❌ 图元装配
- ❌ 光栅化
- ❌ 深度/模板测试(可配置但不可编程)

### 坐标空间变换

```
局部空间(Local Space)
  ↓ 模型矩阵 (Model Matrix)
世界空间(World Space)
  ↓ 视图矩阵 (View Matrix)
视图空间(View/Camera Space)
  ↓ 投影矩阵 (Projection Matrix)
裁剪空间(Clip Space)
  ↓ 透视除法 (w除法)
归一化设备坐标(NDC, Normalized Device Coordinates)
  ↓ 视口变换 (Viewport Transform)
屏幕空间(Screen Space)
```

**完整变换**:
```glsl
// 顶点着色器
vec4 worldPos = modelMatrix * vec4(localPos, 1.0);
vec4 viewPos = viewMatrix * worldPos;
vec4 clipPos = projectionMatrix * viewPos;

gl_Position = clipPos;  // 输出裁剪空间坐标

// GPU自动进行透视除法
vec3 ndcPos = clipPos.xyz / clipPos.w;  // [-1,1]范围

// GPU自动进行视口变换
vec2 screenPos = (ndcPos.xy * 0.5 + 0.5) * screenSize;  // [0,screenSize]
```

### MVP矩阵

**组合矩阵**(常见优化):
```glsl
mat4 MVP = projectionMatrix * viewMatrix * modelMatrix;
gl_Position = MVP * vec4(localPos, 1.0);
```

---

## 渲染流程示例

### 简单场景渲染

**场景**:一个立方体,一个方向光,一个相机

#### 1. CPU端准备(应用阶段)

```cpp
// 1.1 设置相机
Camera* camera = engine->createCamera();
camera->setProjection(45.0f, aspectRatio, 0.1f, 100.0f);  // 透视投影
camera->lookAt({0, 2, 5}, {0, 0, 0}, {0, 1, 0});          // 视图矩阵

// 1.2 创建光源
utils::EntityManager& em = utils::EntityManager::get();
Entity light = em.create();
LightManager::Builder(LightManager::Type::DIRECTIONAL)
    .color({1.0f, 1.0f, 1.0f})
    .intensity(100000)
    .direction({0, -1, -0.5})
    .build(*engine, light);
scene->addEntity(light);

// 1.3 加载模型(立方体)
FilamentAsset* asset = assetLoader->createAsset(cubeData, cubeSize);
resourceLoader->loadResources(asset);
scene->addEntities(asset->getEntities(), asset->getEntityCount());

// 1.4 设置变换
auto& tcm = engine->getTransformManager();
auto ti = tcm.getInstance(asset->getRoot());
tcm->setTransform(ti, mat4f::rotation(angle, float3{0, 1, 0}));
```

#### 2. GPU端渲染(渲染阶段)

**2.1 顶点着色器**:
```glsl
#version 300 es
precision highp float;

// Uniform (常量)
uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

// Attribute (输入)
in vec3 position;    // 局部空间位置
in vec3 normal;      // 局部空间法线
in vec2 uv;          // UV坐标

// Varying (输出)
out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec2 vUV;

void main() {
    // 世界空间位置
    vec4 worldPos = modelMatrix * vec4(position, 1.0);
    vWorldPos = worldPos.xyz;

    // 世界空间法线
    mat3 normalMatrix = mat3(transpose(inverse(modelMatrix)));
    vWorldNormal = normalize(normalMatrix * normal);

    // 传递UV
    vUV = uv;

    // 裁剪空间位置(最终输出)
    gl_Position = projectionMatrix * viewMatrix * worldPos;
}
```

**2.2 光栅化**:
GPU自动将三角形分解为片段(像素候选),并插值varying变量。

**2.3 片段着色器**:
```glsl
#version 300 es
precision mediump float;

// Uniform
uniform vec3 lightDirection;
uniform vec3 lightColor;
uniform vec3 cameraPosition;
uniform sampler2D baseColorMap;

// Varying (插值后的输入)
in vec3 vWorldPos;
in vec3 vWorldNormal;
in vec2 vUV;

// Output
out vec4 fragColor;

void main() {
    // 采样纹理
    vec4 baseColor = texture(baseColorMap, vUV);

    // 归一化向量
    vec3 N = normalize(vWorldNormal);
    vec3 L = normalize(-lightDirection);
    vec3 V = normalize(cameraPosition - vWorldPos);

    // Lambertian漫反射
    float NoL = max(0.0, dot(N, L));
    vec3 diffuse = baseColor.rgb * lightColor * NoL;

    // 简单镜面高光(Blinn-Phong)
    vec3 H = normalize(L + V);
    float NoH = max(0.0, dot(N, H));
    float specular = pow(NoH, 32.0);  // 32 = 光泽度

    // 最终颜色
    vec3 color = diffuse + specular * lightColor;

    fragColor = vec4(color, baseColor.a);
}
```

**2.4 深度测试**:
```
如果 fragDepth < depthBuffer[x,y]:
    depthBuffer[x,y] = fragDepth
    colorBuffer[x,y] = fragColor
否则:
    丢弃该片段(被遮挡)
```

**2.5 输出到帧缓冲**:
最终图像显示在屏幕上。

---

## 总结

### 核心要点

1. **渲染方程**是图形学的理论基础
   ```
   Lo = Le + ∫ fr × Li × cosθ dω
   ```

2. **实时渲染**通过近似和优化达到交互帧率

3. **光照分类**:
   - 直接光照:光源直接照射
   - 间接光照:经过反射的光

4. **渲染管线**:
   - CPU:场景管理、剔除
   - GPU:顶点着色器 → 光栅化 → 片段着色器

5. **坐标变换**:
   ```
   局部 → 世界 → 视图 → 裁剪 → NDC → 屏幕
   ```

### 下一步学习

- **[02-pbr-theory.md](02-pbr-theory.md)**: 深入PBR理论和BRDF
- **[03-gpu-pipeline.md](03-gpu-pipeline.md)**: GPU管线详细流程
- **[04-shader-programming.md](04-shader-programming.md)**: Shader编程实践

### 相关文档

- `../material/05-graphics-concepts.md`: Filament中的图形学应用
- `../material/02-compilation-pipeline.md`: 材质编译流程

---

**理解渲染基础是一切的开始** - 从光的物理特性到GPU管线,这些是计算机图形学的基石!
