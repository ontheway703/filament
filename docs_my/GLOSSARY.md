# Filament 术语表 (Glossary)

本术语表包含 Filament 文档中使用的所有重要技术术语，按字母顺序排列。每个术语包含定义、相关文档链接和使用示例。

## 目录

- [A](#a) - [B](#b) - [C](#c) - [D](#d) - [E](#e) - [F](#f) - [G](#g) - [H](#h) - [I](#i) - [J](#j) - [K](#k) - [L](#l) - [M](#m)
- [N](#n) - [O](#o) - [P](#p) - [Q](#q) - [R](#r) - [S](#s) - [T](#t) - [U](#u) - [V](#v) - [W](#w) - [X](#x) - [Y](#y) - [Z](#z)

---

## A

### Albedo (反照率)
**定义**: 表面反射的光线比例，不包含镜面反射。在 PBR 材质中，baseColor 的 RGB 通道表示反照率。

**相关文档**: [material/02-pbr-theory.md](material/02-pbr-theory.md)

**示例**:
```glsl
material {
    parameters: [
        { type: float3, name: baseColor }
    ]
}
```

### Alpha Blending (透明度混合)
**定义**: 通过 alpha 值混合前景和背景颜色的技术。Filament 支持多种混合模式。

**相关文档**: [material/03-material-definition.md](material/03-material-definition.md)

**混合模式**:
- `opaque`: 不透明 (默认)
- `transparent`: 透明
- `fade`: 渐变
- `add`: 加法混合
- `multiply`: 乘法混合

### Anisotropy (各向异性)
**定义**: 材质在不同方向上的反射特性不同，如拉丝金属、头发。

**相关文档**: [material/04-shading-models.md](material/04-shading-models.md)

**参数**:
- `anisotropy`: 各向异性强度 [0, 1]
- `anisotropyDirection`: 各向异性方向向量

### AssetLoader
**定义**: gltfio 库中用于异步加载 glTF 资源的核心类。

**相关文档**: [gltfio/03-async-loading.md](gltfio/03-async-loading.md)

**使用**:
```cpp
AssetLoader* loader = AssetLoader::create({engine, materials});
FilamentAsset* asset = loader->createAsset(data, size);
loader->loadResources(asset);
```

---

## B

### Backend (渲染后端)
**定义**: Filament 的图形 API 抽象层，支持 OpenGL、Vulkan、Metal、WebGL。

**相关文档**: [backend/02-command-stream.md](backend/02-command-stream.md)

**支持的后端**:
- `OPENGL`: OpenGL 4.1+ / OpenGL ES 3.0+
- `VULKAN`: Vulkan 1.1+
- `METAL`: Metal (macOS/iOS)
- `NOOP`: 空后端 (测试用)

### Baking (烘焙)
**定义**: 将复杂光照计算预计算并存储到纹理中的技术。

**类型**:
- Light map: 光照图
- Ambient Occlusion: 环境光遮蔽
- Irradiance: 辐照度

**相关文档**: [graphics/03-lighting.md](graphics/03-lighting.md)

### Binding (绑定)
**定义**: 将资源（纹理、Buffer）绑定到着色器的特定位置。

**相关文档**: [backend/05-resource-binding.md](backend/05-resource-binding.md)

**示例**:
```cpp
builder.sampler(SamplerType::SAMPLER_2D,
                SamplerFormat::FLOAT,
                SamplerPrecision::DEFAULT);
```

### Bloom (辉光)
**定义**: 模拟相机过曝效果的后处理技术，使亮区产生发光效果。

**相关文档**: [engine/06-rendering-pipeline.md](engine/06-rendering-pipeline.md)

**配置**:
```cpp
view->setBloomOptions({
    .enabled = true,
    .strength = 0.2f,
    .resolution = 360,
    .levels = 6
});
```

### BRDF (Bidirectional Reflectance Distribution Function)
**定义**: 双向反射分布函数，描述光线如何从表面反射。Filament 使用 Cook-Torrance BRDF。

**相关文档**: [material/02-pbr-theory.md](material/02-pbr-theory.md)

**组成**:
```
f(l,v) = diffuse + specular
       = (baseColor / π) + D(h)·F(v,h)·G(l,v,h) / (4·(n·l)·(n·v))
```

### Buffer Object
**定义**: 存储顶点数据或索引数据的 GPU 内存对象。

**类型**:
- `VertexBuffer`: 顶点缓冲区
- `IndexBuffer`: 索引缓冲区
- `UniformBuffer`: Uniform 缓冲区

**相关文档**: [engine/03-resource-management.md](engine/03-resource-management.md)

---

## C

### Camera (相机)
**定义**: 定义视角和投影的组件。Filament 支持透视和正交投影。

**相关文档**: [engine/02-scene-management.md](engine/02-scene-management.md)

**创建**:
```cpp
Camera* camera = engine->createCamera(cameraEntity);
camera->setProjection(45.0, aspect, 0.1, 100.0);
camera->lookAt({0,2,5}, {0,0,0}, {0,1,0});
```

### Clear Coat (清漆层)
**定义**: 模拟表面透明涂层的材质特性，如车漆、木器漆。

**相关文档**: [material/04-shading-models.md](material/04-shading-models.md)

**参数**:
- `clearCoat`: 清漆层强度 [0, 1]
- `clearCoatRoughness`: 清漆层粗糙度 [0, 1]
- `clearCoatNormal`: 清漆层法线

### cmgen
**定义**: Filament 的 IBL 资源生成工具，从 HDR 环境贴图生成 Cubemap 和球谐系数。

**相关文档**: [tools/03-cmgen-ibl.md](tools/03-cmgen-ibl.md)

**使用**:
```bash
cmgen -x ./output \
      --format=ktx \
      --size=256 \
      environment.exr
```

### Color Space (色彩空间)
**定义**: 定义颜色表示方式的数学模型。

**常用色彩空间**:
- `sRGB`: 标准 RGB (非线性)
- `Linear RGB`: 线性 RGB
- `ACEScg`: ACES 色彩空间
- `Rec.709`: HDTV 标准

**相关文档**: [graphics/02-rendering-pipeline.md](graphics/02-rendering-pipeline.md)

### CommandStream
**定义**: Filament 后端的命令缓冲区，存储渲染命令序列。

**相关文档**: [backend/02-command-stream.md](backend/02-command-stream.md)

**工作流程**:
1. Frontend 生成命令 → CommandStream
2. CommandStream 批量传输到 Backend
3. Backend 线程执行命令

### Culling (剔除)
**定义**: 跳过不可见物体的渲染以提高性能。

**类型**:
- **Frustum Culling**: 视锥体剔除
- **Backface Culling**: 背面剔除
- **Occlusion Culling**: 遮挡剔除

**相关文档**: [engine/06-rendering-pipeline.md](engine/06-rendering-pipeline.md)

### Cubemap (立方体贴图)
**定义**: 由 6 张纹理组成的环境贴图，用于天空盒和 IBL。

**相关文档**: [graphics/03-lighting.md](graphics/03-lighting.md)

**面定义**:
- `+X, -X`: 右, 左
- `+Y, -Y`: 上, 下
- `+Z, -Z`: 前, 后

---

## D

### Depth Buffer (深度缓冲)
**定义**: 存储每个像素深度值的缓冲区，用于深度测试。

**相关文档**: [graphics/01-gpu-basics.md](graphics/01-gpu-basics.md)

**格式**:
- `DEPTH16`: 16 位深度
- `DEPTH24`: 24 位深度
- `DEPTH24_STENCIL8`: 24 位深度 + 8 位模板
- `DEPTH32F`: 32 位浮点深度

### Descriptor Set
**定义**: Vulkan 中资源绑定的集合，包含多个 Descriptor。

**相关文档**: [backend/05-resource-binding.md](backend/05-resource-binding.md)

**组成**:
- Uniform Buffer Descriptors
- Sampler Descriptors
- Image Descriptors

### Diffuse (漫反射)
**定义**: 光线在表面各方向均匀散射的反射类型。

**相关文档**: [material/02-pbr-theory.md](material/02-pbr-theory.md)

**计算**:
```glsl
vec3 diffuse = baseColor / PI;
```

### Directional Light (方向光)
**定义**: 模拟无限远光源（如太阳）的光照类型，所有光线平行。

**相关文档**: [graphics/03-lighting.md](graphics/03-lighting.md)

**创建**:
```cpp
utils::Entity light = utils::EntityManager::get().create();
LightManager::Builder(LightManager::Type::DIRECTIONAL)
    .color({1,1,1})
    .intensity(100000)
    .direction({0.6, -1, -0.8})
    .castShadows(true)
    .build(*engine, light);
```

### Double Buffering (双缓冲)
**定义**: 使用两个缓冲区交替渲染和显示，避免撕裂。

**相关文档**: [backend/03-synchronization.md](backend/03-synchronization.md)

**流程**:
1. 后台缓冲区渲染新帧
2. 前台缓冲区显示当前帧
3. VSync 时交换缓冲区

### Driver
**定义**: Filament 后端中特定图形 API 的实现层。

**相关文档**: [backend/04-platform-specific.md](backend/04-platform-specific.md)

**实现**:
- `OpenGLDriver`: OpenGL/ES 实现
- `VulkanDriver`: Vulkan 实现
- `MetalDriver`: Metal 实现

---

## E

### ECS (Entity Component System)
**定义**: Filament 使用的场景架构模式，将数据（Component）与逻辑分离。

**相关文档**: [engine/02-scene-management.md](engine/02-scene-management.md)

**核心概念**:
- **Entity**: 唯一标识符 (uint32_t)
- **Component**: 数据 (Transform, Renderable, Light)
- **System**: 逻辑 (RenderSystem, TransformSystem)

### Emissive (自发光)
**定义**: 材质自身发光的特性，不受外部光照影响。

**相关文档**: [material/03-material-definition.md](material/03-material-definition.md)

**参数**:
- `emissive`: 自发光颜色和强度 (float4)

### Engine
**定义**: Filament 的核心类，管理所有渲染资源和系统。

**相关文档**: [engine/01-architecture.md](engine/01-architecture.md)

**创建**:
```cpp
Engine* engine = Engine::create(Engine::Backend::VULKAN);
```

### Environment Map
**定义**: 环境贴图，用于 IBL 和反射。

**类型**:
- **Irradiance Map**: 漫反射环境光
- **Radiance Map (Prefiltered)**: 镜面反射环境光
- **BRDF LUT**: BRDF 查找表

**相关文档**: [graphics/03-lighting.md](graphics/03-lighting.md)

---

## F

### Fence (围栏)
**定义**: GPU 同步原语，用于等待 GPU 操作完成。

**相关文档**: [backend/03-synchronization.md](backend/03-synchronization.md)

**使用**:
```cpp
Fence* fence = engine->createFence();
engine->flush();
auto status = fence->wait(Fence::Mode::FLUSH, 1000000000);
engine->destroy(fence);
```

### filamesh
**定义**: Filament 的网格转换工具，将 OBJ/FBX 转换为 .filamesh 格式。

**相关文档**: [tools/04-filamesh.md](tools/04-filamesh.md)

**使用**:
```bash
filamesh --tangents --compress input.obj output.filamesh
```

### Framebuffer (帧缓冲)
**定义**: 渲染目标的集合，包含颜色、深度、模板附件。

**相关文档**: [graphics/01-gpu-basics.md](graphics/01-gpu-basics.md)

**附件类型**:
- Color Attachment: 颜色附件 (可多个)
- Depth Attachment: 深度附件
- Stencil Attachment: 模板附件

### Fresnel Effect (菲涅尔效应)
**定义**: 观察角度影响反射强度的现象，边缘反射更强。

**相关文档**: [material/02-pbr-theory.md](material/02-pbr-theory.md)

**Schlick 近似**:
```glsl
vec3 F_Schlick(vec3 f0, float VoH) {
    return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);
}
```

### Frustum (视锥体)
**定义**: 相机可见的金字塔形空间，由 6 个平面定义。

**相关文档**: [engine/02-scene-management.md](engine/02-scene-management.md)

**平面**:
- Near/Far: 近/远平面
- Left/Right: 左/右平面
- Top/Bottom: 上/下平面

---

## G

### Gamma Correction (伽马校正)
**定义**: 在线性色彩空间和 sRGB 之间转换的过程。

**相关文档**: [graphics/02-rendering-pipeline.md](graphics/02-rendering-pipeline.md)

**转换**:
```glsl
// Linear to sRGB
sRGB = pow(linear, 1.0/2.2);

// sRGB to Linear
linear = pow(sRGB, 2.2);
```

### Geometry Stage
**定义**: 渲染管线中处理顶点变换和裁剪的阶段。

**相关文档**: [graphics/02-rendering-pipeline.md](graphics/02-rendering-pipeline.md)

**步骤**:
1. Vertex Shader: 顶点变换
2. Tessellation (可选): 细分
3. Geometry Shader (可选): 几何处理
4. Clipping: 裁剪

### glTF (GL Transmission Format)
**定义**: Khronos 组织的 3D 资产标准格式，Filament 推荐使用。

**相关文档**: [gltf/01-format-spec.md](gltf/01-format-spec.md)

**文件类型**:
- `.gltf`: JSON + 外部资源
- `.glb`: 二进制打包格式

### gltfio
**定义**: Filament 的 glTF 加载库，支持 glTF 2.0 规范。

**相关文档**: [gltfio/01-overview.md](gltfio/01-overview.md)

**核心类**:
- `AssetLoader`: 资源加载器
- `FilamentAsset`: 资产对象
- `ResourceLoader`: 资源加载器
- `Animator`: 动画控制器

---

## H

### Handle<T>
**定义**: Filament 使用的轻量级资源句柄，指向实际资源。

**相关文档**: [engine/03-resource-management.md](engine/03-resource-management.md)

**示例**:
```cpp
Handle<HwTexture> handle = driver->createTexture(...);
driver->destroyTexture(handle);
```

### HDR (High Dynamic Range)
**定义**: 高动态范围，支持比标准 [0,1] 更大的亮度范围。

**相关文档**: [graphics/03-lighting.md](graphics/03-lighting.md)

**格式**:
- `.hdr` (Radiance HDR)
- `.exr` (OpenEXR)
- RGB16F/RGB32F 纹理

---

## I

### IBL (Image-Based Lighting)
**定义**: 基于图像的光照，使用环境贴图提供间接光照和反射。

**相关文档**: [graphics/03-lighting.md](graphics/03-lighting.md)

**组成**:
- Irradiance Map: 漫反射间接光 (SH 或 Cubemap)
- Radiance Map: 镜面反射 (Prefiltered Cubemap)
- BRDF LUT: 2D 查找表

**使用**:
```cpp
IndirectLight* ibl = IndirectLight::Builder()
    .reflections(cubemap)
    .irradiance(3, sh)  // 3 bands
    .intensity(30000.0f)
    .build(*engine);
scene->setIndirectLight(ibl);
```

### Index Buffer
**定义**: 存储顶点索引的缓冲区，用于索引绘制。

**相关文档**: [engine/03-resource-management.md](engine/03-resource-management.md)

**创建**:
```cpp
IndexBuffer* ib = IndexBuffer::Builder()
    .indexCount(36)
    .bufferType(IndexBuffer::IndexType::USHORT)
    .build(*engine);
ib->setBuffer(*engine, IndexBuffer::BufferDescriptor(
    indices, 36 * sizeof(uint16_t)));
```

### Indirect Light
**定义**: 间接光照，来自环境的漫反射和镜面反射。

**相关文档**: [engine/02-scene-management.md](engine/02-scene-management.md)

### Irradiance (辐照度)
**定义**: 单位面积接收的光通量，用于漫反射 IBL。

**表示方法**:
- Spherical Harmonics (SH): 球谐函数
- Irradiance Cubemap: 辐照度立方体贴图

**相关文档**: [tools/03-cmgen-ibl.md](tools/03-cmgen-ibl.md)

---

## J

### Job System
**定义**: Filament 的多线程任务调度系统。

**相关文档**: [engine/04-multi-threading.md](engine/04-multi-threading.md)

**线程**:
- Main Thread: 主线程
- Render Thread: 渲染线程
- Backend Thread: 后端线程
- Job Threads: 工作线程池

---

## K

### KTX (Khronos Texture)
**定义**: Khronos 的标准纹理格式，支持压缩和 Mipmap。

**相关文档**: [tools/05-mipgen.md](tools/05-mipgen.md)

**版本**:
- KTX 1.0: 传统格式
- KTX 2.0: 支持 Basis Universal 压缩

---

## L

### Light (光源)
**定义**: 场景中的光照来源。

**类型**:
- **Directional**: 方向光 (太阳)
- **Point**: 点光源 (灯泡)
- **Spot**: 聚光灯 (手电筒)

**相关文档**: [graphics/03-lighting.md](graphics/03-lighting.md)

### Linear Color Space
**定义**: 颜色值与实际光能量成线性关系的色彩空间。

**相关文档**: [graphics/02-rendering-pipeline.md](graphics/02-rendering-pipeline.md)

**重要性**: PBR 必须在线性空间计算，最后才转换到 sRGB 显示。

### LOD (Level of Detail)
**定义**: 根据距离使用不同精度模型的优化技术。

**相关文档**: [engine/03-resource-management.md](engine/03-resource-management.md)

**策略**:
- 几何 LOD: 不同三角形数量
- 纹理 LOD: Mipmap
- 材质 LOD: 简化着色器

---

## M

### Material (材质)
**定义**: 定义物体表面外观的资源，包含着色器代码和参数。

**相关文档**: [material/03-material-definition.md](material/03-material-definition.md)

**定义文件**:
```
material {
    name: MyMaterial,
    shadingModel: lit,
    parameters: [ ... ],
    ...
}
```

### MaterialInstance
**定义**: 材质的实例，设置具体参数值。

**相关文档**: [material/05-material-system.md](material/05-material-system.md)

**使用**:
```cpp
MaterialInstance* mi = material->createInstance();
mi->setParameter("baseColor", RgbType::sRGB, {1,0,0});
mi->setParameter("metallic", 1.0f);
```

### matc (Material Compiler)
**定义**: Filament 的材质编译器，将 .mat 编译为 .filamat。

**相关文档**: [tools/02-matc-compiler.md](tools/02-matc-compiler.md)

**使用**:
```bash
matc -p mobile -a vulkan -O -o output.filamat input.mat
```

### Metallic (金属度)
**定义**: 材质的金属特性，0=非金属，1=金属。

**相关文档**: [material/02-pbr-theory.md](material/02-pbr-theory.md)

**影响**:
- 金属: 无漫反射，强镜面反射（有色）
- 非金属: 有漫反射，弱镜面反射（无色）

### Mipmap (多级渐远纹理)
**定义**: 同一纹理的多个分辨率版本，用于优化采样和性能。

**相关文档**: [graphics/04-textures.md](graphics/04-textures.md)

**级别**:
- Level 0: 原始分辨率
- Level 1: 1/2 分辨率
- Level 2: 1/4 分辨率
- ...

### mipgen
**定义**: Filament 的 Mipmap 生成工具。

**相关文档**: [tools/05-mipgen.md](tools/05-mipgen.md)

**使用**:
```bash
mipgen --compression=uastc input.png output.ktx
```

---

## N

### Normal Mapping (法线贴图)
**定义**: 使用纹理存储表面法线信息，增加细节而不增加几何复杂度。

**相关文档**: [graphics/04-textures.md](graphics/04-textures.md)

**类型**:
- Tangent-space: 切线空间（常用）
- Object-space: 对象空间
- World-space: 世界空间

---

## O

### Occlusion (遮挡)
**定义**: 物体被其他物体遮挡的现象。

**相关文档**: [graphics/03-lighting.md](graphics/03-lighting.md)

**技术**:
- **AO (Ambient Occlusion)**: 环境光遮蔽
- **SSAO**: 屏幕空间环境光遮蔽

### ORM (Occlusion-Roughness-Metallic)
**定义**: 将 AO、Roughness、Metallic 打包到单张纹理的常见做法。

**通道分配**:
- R: Occlusion
- G: Roughness
- B: Metallic

**相关文档**: [material/03-material-definition.md](material/03-material-definition.md)

---

## P

### PBR (Physically Based Rendering)
**定义**: 基于物理的渲染，使用符合物理规律的光照模型。

**相关文档**: [material/02-pbr-theory.md](material/02-pbr-theory.md)

**核心原则**:
1. 能量守恒
2. 基于物理的 BRDF
3. 线性工作流
4. 真实世界测量的参数

### Pipeline State
**定义**: 渲染管线的完整配置状态。

**相关文档**: [backend/04-platform-specific.md](backend/04-platform-specific.md)

**包含**:
- Shader Program
- Vertex Input State
- Rasterization State
- Depth/Stencil State
- Blend State

### Point Light (点光源)
**定义**: 从一点向所有方向发光的光源，如灯泡。

**相关文档**: [graphics/03-lighting.md](graphics/03-lighting.md)

**参数**:
- Position: 位置
- Color: 颜色
- Intensity: 强度
- Falloff: 衰减半径

---

## R

### Radiance (辐射度)
**定义**: 单位立体角、单位投影面积的辐射通量，用于镜面反射 IBL。

**相关文档**: [tools/03-cmgen-ibl.md](tools/03-cmgen-ibl.md)

### Rasterization (光栅化)
**定义**: 将三角形转换为像素的过程。

**相关文档**: [graphics/02-rendering-pipeline.md](graphics/02-rendering-pipeline.md)

**步骤**:
1. 三角形设置
2. 边缘函数计算
3. 像素覆盖测试
4. 插值属性

### Renderable
**定义**: Filament 中可渲染物体的组件。

**相关文档**: [engine/02-scene-management.md](engine/02-scene-management.md)

**创建**:
```cpp
RenderableManager::Builder(1)
    .boundingBox({{-1,-1,-1}, {1,1,1}})
    .material(0, materialInstance)
    .geometry(0, PrimitiveType::TRIANGLES, vb, ib)
    .build(*engine, entity);
```

### Renderer
**定义**: 执行实际渲染操作的对象。

**相关文档**: [engine/01-architecture.md](engine/01-architecture.md)

**使用**:
```cpp
Renderer* renderer = engine->createRenderer();
renderer->render(view);
```

### Roughness (粗糙度)
**定义**: 表面微观粗糙程度，影响镜面反射模糊度。

**相关文档**: [material/02-pbr-theory.md](material/02-pbr-theory.md)

**值范围**: [0, 1]
- 0: 完美镜面
- 1: 完全漫反射

---

## S

### Sampler (采样器)
**定义**: 定义纹理采样方式的对象。

**相关文档**: [backend/05-resource-binding.md](backend/05-resource-binding.md)

**参数**:
- Filter: 过滤模式 (Nearest, Linear)
- Wrap: 包裹模式 (Repeat, Clamp, Mirror)
- Anisotropy: 各向异性过滤级别

### Scene (场景)
**定义**: 包含所有可渲染实体的容器。

**相关文档**: [engine/02-scene-management.md](engine/02-scene-management.md)

**创建**:
```cpp
Scene* scene = engine->createScene();
scene->addEntity(entity);
scene->setIndirectLight(ibl);
scene->setSkybox(skybox);
```

### Shading Model (着色模型)
**定义**: 材质的光照计算模型。

**相关文档**: [material/04-shading-models.md](material/04-shading-models.md)

**支持的模型**:
- `lit`: 标准 PBR (默认)
- `unlit`: 无光照
- `subsurface`: 次表面散射
- `cloth`: 布料
- `specularGlossiness`: 高光-光泽度工作流

### Shadow Mapping (阴影映射)
**定义**: 使用深度贴图生成阴影的技术。

**相关文档**: [graphics/03-lighting.md](graphics/03-lighting.md)

**类型**:
- Hard Shadows: 硬阴影
- PCF (Percentage Closer Filtering): 软阴影
- VSM (Variance Shadow Maps): 变分阴影
- CSM (Cascaded Shadow Maps): 级联阴影

### Skybox (天空盒)
**定义**: 使用 Cubemap 渲染的背景环境。

**相关文档**: [engine/02-scene-management.md](engine/02-scene-management.md)

**创建**:
```cpp
Skybox* skybox = Skybox::Builder()
    .environment(cubemap)
    .build(*engine);
scene->setSkybox(skybox);
```

### Specular (镜面反射)
**定义**: 光线在表面特定方向反射的反射类型。

**相关文档**: [material/02-pbr-theory.md](material/02-pbr-theory.md)

**计算**: Cook-Torrance 微表面模型
```
specular = D·F·G / (4·(n·l)·(n·v))
```

### Spherical Harmonics (球谐函数)
**定义**: 用于表示球面函数的正交基函数，Filament 用于 IBL。

**相关文档**: [tools/03-cmgen-ibl.md](tools/03-cmgen-ibl.md)

**Bands**:
- 1 band (L=0): 1 系数 - 非常模糊
- 2 bands (L=1): 4 系数 - 模糊
- 3 bands (L=2): 9 系数 - 良好 (推荐)

### Spot Light (聚光灯)
**定义**: 锥形光束的光源，如手电筒。

**相关文档**: [graphics/03-lighting.md](graphics/03-lighting.md)

**参数**:
- Position: 位置
- Direction: 方向
- Inner/Outer Cone: 内/外锥角
- Color/Intensity: 颜色/强度

### Subsurface Scattering (次表面散射)
**定义**: 光线穿透表面，在内部散射后再射出的现象，如皮肤、蜡烛。

**相关文档**: [material/04-shading-models.md](material/04-shading-models.md)

**参数**:
- `subsurfacePower`: 散射强度
- `subsurfaceColor`: 散射颜色
- `thickness`: 厚度

### SwapChain (交换链)
**定义**: 管理渲染目标缓冲区的对象，用于显示。

**相关文档**: [backend/04-platform-specific.md](backend/04-platform-specific.md)

**创建**:
```cpp
SwapChain* swapChain = engine->createSwapChain(nativeWindow);
```

---

## T

### Tangent Space (切线空间)
**定义**: 以表面切线、副切线、法线为基的局部坐标系。

**相关文档**: [graphics/04-textures.md](graphics/04-textures.md)

**用途**: 法线贴图通常存储在切线空间。

**基向量**:
- T: Tangent (切线)
- B: Bitangent (副切线)
- N: Normal (法线)

### Texture (纹理)
**定义**: 存储图像数据的 GPU 资源。

**相关文档**: [graphics/04-textures.md](graphics/04-textures.md)

**类型**:
- 2D Texture: 普通纹理
- Cubemap: 立方体贴图
- 3D Texture: 体积纹理
- 2D Array: 纹理数组

### Tone Mapping (色调映射)
**定义**: 将 HDR 颜色映射到 LDR 显示范围的过程。

**相关文档**: [graphics/02-rendering-pipeline.md](graphics/02-rendering-pipeline.md)

**算法**:
- Linear: 线性映射
- ACES: ACES Filmic
- Reinhard: Reinhard 算法
- Uncharted 2: Uncharted 2 Filmic

### TransformManager
**定义**: 管理实体变换（位置、旋转、缩放）的组件。

**相关文档**: [engine/02-scene-management.md](engine/02-scene-management.md)

**使用**:
```cpp
auto& tcm = engine->getTransformManager();
tcm.setTransform(tcm.getInstance(entity),
                 mat4f::translation(float3(0,1,0)));
```

---

## U

### Uniform Buffer
**定义**: 存储着色器 Uniform 变量的缓冲区。

**相关文档**: [backend/05-resource-binding.md](backend/05-resource-binding.md)

**使用场景**: 相机矩阵、光源数据、材质参数。

### UBO (Uniform Buffer Object)
**定义**: Uniform Buffer 的 OpenGL 术语。

---

## V

### Vertex Buffer
**定义**: 存储顶点属性数据的缓冲区。

**相关文档**: [engine/03-resource-management.md](engine/03-resource-management.md)

**属性**:
- Position: 位置
- Normal: 法线
- Tangent: 切线
- UV: 纹理坐标
- Color: 顶点颜色

**创建**:
```cpp
VertexBuffer* vb = VertexBuffer::Builder()
    .vertexCount(vertexCount)
    .bufferCount(1)
    .attribute(VertexAttribute::POSITION, 0,
               VertexBuffer::AttributeType::FLOAT3)
    .attribute(VertexAttribute::UV0, 0,
               VertexBuffer::AttributeType::FLOAT2)
    .build(*engine);
```

### Vertex Shader (顶点着色器)
**定义**: 渲染管线中处理顶点变换的可编程阶段。

**相关文档**: [graphics/02-rendering-pipeline.md](graphics/02-rendering-pipeline.md)

**职责**:
- 模型-视图-投影变换
- 法线变换
- 计算光照所需的向量

### View (视图)
**定义**: 定义渲染视角、视口、后处理等的对象。

**相关文档**: [engine/01-architecture.md](engine/01-architecture.md)

**创建**:
```cpp
View* view = engine->createView();
view->setScene(scene);
view->setCamera(camera);
view->setViewport({0, 0, width, height});
```

### Viewport (视口)
**定义**: 渲染输出的屏幕矩形区域。

**参数**: (x, y, width, height)

### VSync (Vertical Synchronization)
**定义**: 垂直同步，使帧率与显示器刷新率同步，避免撕裂。

**相关文档**: [backend/03-synchronization.md](backend/03-synchronization.md)

**配置**:
```cpp
swapChain->setVSyncEnabled(true);
```

### Vulkan
**定义**: Khronos 的现代低开销图形 API，Filament 支持的后端之一。

**相关文档**: [backend/04-platform-specific.md](backend/04-platform-specific.md)

**特性**:
- 显式内存管理
- 多线程命令生成
- 管线状态对象 (PSO)
- Descriptor Sets

---

## W

### World Space (世界空间)
**定义**: 场景的全局坐标系。

**变换顺序**:
```
Local Space → World Space → View Space → Clip Space → Screen Space
```

---

## 符号和缩写

### API (Application Programming Interface)
**定义**: 应用程序编程接口。

### CPU (Central Processing Unit)
**定义**: 中央处理器，主处理器。

### DCC (Digital Content Creation)
**定义**: 数字内容创作工具，如 Blender, Maya, 3ds Max。

### FBO (Framebuffer Object)
**定义**: OpenGL 的帧缓冲对象。

### FPS (Frames Per Second)
**定义**: 每秒帧数，衡量渲染性能的指标。

### GLSL (OpenGL Shading Language)
**定义**: OpenGL 的着色器语言。

### GPU (Graphics Processing Unit)
**定义**: 图形处理器，执行渲染计算。

### GUI (Graphical User Interface)
**定义**: 图形用户界面。

### HDR (High Dynamic Range)
**定义**: 高动态范围，见 [HDR](#hdr-high-dynamic-range)。

### LDR (Low Dynamic Range)
**定义**: 低动态范围，标准 [0,1] 颜色范围。

### LOD (Level of Detail)
**定义**: 细节层次，见 [LOD](#lod-level-of-detail)。

### MRT (Multiple Render Targets)
**定义**: 多渲染目标，同时渲染到多个颜色附件。

### MSAA (Multisample Anti-Aliasing)
**定义**: 多重采样抗锯齿。

### PBR (Physically Based Rendering)
**定义**: 基于物理的渲染，见 [PBR](#pbr-physically-based-rendering)。

### PSO (Pipeline State Object)
**定义**: 管线状态对象 (Vulkan/D3D12)。

### RGB (Red Green Blue)
**定义**: 红绿蓝颜色模型。

### RGBA (Red Green Blue Alpha)
**定义**: 红绿蓝 + Alpha 透明度。

### SDK (Software Development Kit)
**定义**: 软件开发工具包。

### SIMD (Single Instruction Multiple Data)
**定义**: 单指令多数据，并行计算技术。

### SSAO (Screen Space Ambient Occlusion)
**定义**: 屏幕空间环境光遮蔽。

### sRGB (Standard RGB)
**定义**: 标准 RGB 色彩空间，带 Gamma 编码。

### UBO (Uniform Buffer Object)
**定义**: Uniform 缓冲对象，见 [UBO](#ubo-uniform-buffer-object)。

### UI (User Interface)
**定义**: 用户界面。

### UV
**定义**: 纹理坐标轴 (U=横向, V=纵向)。

### VAO (Vertex Array Object)
**定义**: OpenGL 的顶点数组对象。

### VBO (Vertex Buffer Object)
**定义**: OpenGL 的顶点缓冲对象。

---

## 中文索引

为方便中文使用者，这里提供常用术语的中文索引：

### 材质相关
- 反照率 → [Albedo](#albedo-反照率)
- 金属度 → [Metallic](#metallic-金属度)
- 粗糙度 → [Roughness](#roughness-粗糙度)
- 法线贴图 → [Normal Mapping](#normal-mapping-法线贴图)
- 自发光 → [Emissive](#emissive-自发光)
- 清漆层 → [Clear Coat](#clear-coat-清漆层)
- 次表面散射 → [Subsurface Scattering](#subsurface-scattering-次表面散射)

### 光照相关
- 方向光 → [Directional Light](#directional-light-方向光)
- 点光源 → [Point Light](#point-light-点光源)
- 聚光灯 → [Spot Light](#spot-light-聚光灯)
- 环境光遮蔽 → [Occlusion](#occlusion-遮挡)
- 基于图像的光照 → [IBL](#ibl-image-based-lighting)
- 阴影映射 → [Shadow Mapping](#shadow-mapping-阴影映射)

### 渲染相关
- 光栅化 → [Rasterization](#rasterization-光栅化)
- 帧缓冲 → [Framebuffer](#framebuffer-帧缓冲)
- 深度缓冲 → [Depth Buffer](#depth-buffer-深度缓冲)
- 多级渐远纹理 → [Mipmap](#mipmap-多级渐远纹理)
- 色调映射 → [Tone Mapping](#tone-mapping-色调映射)
- 伽马校正 → [Gamma Correction](#gamma-correction-伽马校正)

### 工具相关
- 材质编译器 → [matc](#matc-material-compiler)
- IBL 生成器 → [cmgen](#cmgen)
- 网格转换器 → [filamesh](#filamesh)
- Mipmap 生成器 → [mipgen](#mipgen)

### 架构相关
- 引擎 → [Engine](#engine)
- 场景 → [Scene](#scene)
- 视图 → [View](#view)
- 渲染器 → [Renderer](#renderer)
- 渲染后端 → [Backend](#backend-渲染后端)

---

## 参考文档

本术语表涵盖以下文档的术语：

- **graphics/** - GPU 和图形学基础
- **backend/** - 渲染后端架构
- **engine/** - Filament 引擎核心
- **material/** - 材质系统
- **gltf/** - glTF 格式
- **gltfio/** - glTF 加载
- **tools/** - 工具链
- **platforms/** - 平台集成
- **samples/** - 示例项目

---

## 版本信息

- **文档版本**: v1.0
- **Filament 版本**: 1.51.5+
- **最后更新**: 2024-01

---

> **使用建议**: 按 `Ctrl+F` / `Cmd+F` 快速搜索术语。点击术语标题右侧的 ⚓ 图标获取直接链接。

---

## 🔥 高级主题术语 (Advanced Topics)

### ACMR (Average Cache Miss Ratio)
**定义**: 平均缓存未命中率，衡量顶点缓存优化效果的指标。

**相关文档**: [optimization/02-mesh-optimization.md](advanced/optimization/02-mesh-optimization.md)

**优化目标**:
- 未优化: 1.5 - 2.0
- 优化后: 0.5 - 0.8

### ACES (Academy Color Encoding System)
**定义**: 电影艺术与科学学院的色彩编码系统，常用的色调映射算子。

**相关文档**: [rendering/08-hdr-tone-mapping.md](advanced/rendering/08-hdr-tone-mapping.md)

**特点**:
- 电影级色彩
- 宽动态范围
- 行业标准

### ASTC (Adaptive Scalable Texture Compression)
**定义**: 自适应可缩放纹理压缩，移动端推荐的纹理压缩格式。

**相关文档**: [optimization/01-texture-optimization.md](advanced/optimization/01-texture-optimization.md)

**压缩比**:
- 4x4: 8bpp (高质量)
- 6x6: 3.56bpp (中等质量)
- 8x8: 2bpp (高压缩)

### Cascaded Shadow Maps (CSM)
**定义**: 级联阴影贴图，通过多个不同分辨率的阴影贴图覆盖不同距离范围。

**相关文档**: [rendering/01-shadow-techniques.md](advanced/rendering/01-shadow-techniques.md)

**典型配置**:
- Near cascade: 0-10m (2048x2048)
- Mid cascade: 10-50m (1024x1024)
- Far cascade: 50-200m (512x512)

### Draw Call Batching
**定义**: 将多个绘制调用合并为一个，减少 CPU 开销。

**相关文档**: [optimization/03-drawcall-reduction.md](advanced/optimization/03-drawcall-reduction.md)

**技术**:
- Static Batching: 静态批处理
- Dynamic Batching: 动态批处理
- GPU Instancing: GPU 实例化

### FXAA (Fast Approximate Anti-Aliasing)
**定义**: 快速近似抗锯齿，基于后处理的抗锯齿技术。

**相关文档**: [rendering/05-anti-aliasing.md](advanced/rendering/05-anti-aliasing.md)

**特点**:
- 性能开销小
- 适合移动端
- 可能模糊细节

### GPU Instancing
**定义**: 使用单个绘制调用渲染大量相同网格的技术。

**相关文档**: [optimization/03-drawcall-reduction.md](advanced/optimization/03-drawcall-reduction.md)

**应用场景**:
- 树木、草地
- 粒子系统
- 重复物体

### GTAO (Ground Truth Ambient Occlusion)
**定义**: 基于地面真实的环境光遮蔽算法，质量最高的 SSAO 变体。

**相关文档**: [rendering/03-ambient-occlusion.md](advanced/rendering/03-ambient-occlusion.md)

**特点**:
- 最高质量
- 性能开销大
- 适合高端平台

### HBAO (Horizon-Based Ambient Occlusion)
**定义**: 基于水平线的环境光遮蔽，SSAO 的改进版本。

**相关文档**: [rendering/03-ambient-occlusion.md](advanced/rendering/03-ambient-occlusion.md)

**优势**:
- 质量优于 SSAO
- 性能适中
- 适合桌面端

### HDR (High Dynamic Range)
**定义**: 高动态范围渲染，使用浮点纹理存储更大亮度范围。

**相关文档**: [rendering/08-hdr-tone-mapping.md](advanced/rendering/08-hdr-tone-mapping.md)

**格式**:
- RGBA16F: 半精度浮点
- RGBA32F: 全精度浮点
- RGB11F11F10F: 压缩浮点

### IBL (Image-Based Lighting)
**定义**: 基于图像的光照，使用环境贴图进行光照计算。

**相关文档**: [rendering/02-global-illumination.md](advanced/rendering/02-global-illumination.md)

**组成**:
- Irradiance Map: 辐照度图 (漫反射)
- Prefiltered Map: 预过滤图 (镜面反射)
- BRDF LUT: BRDF 查找表

### Indirect Drawing
**定义**: 使用 GPU buffer 中的参数进行绘制调用，支持 GPU 驱动的渲染。

**相关文档**: [optimization/03-drawcall-reduction.md](advanced/optimization/03-drawcall-reduction.md)

**优势**:
- GPU 驱动剔除
- 海量物体渲染
- 减少 CPU 开销

### LOD (Level of Detail)
**定义**: 细节层次，根据距离使用不同复杂度的模型。

**相关文档**: [optimization/02-mesh-optimization.md](advanced/optimization/02-mesh-optimization.md)

**典型设置**:
- LOD 0: 100% 三角形 (0-10m)
- LOD 1: 50% 三角形 (10-25m)
- LOD 2: 25% 三角形 (25-50m)
- LOD 3: 10% 三角形 (50m+)

### LTC (Linearly Transformed Cosines)
**定义**: 线性变换余弦，用于实时区域光源的算法。

**相关文档**: [rendering/04-advanced-lighting.md](advanced/rendering/04-advanced-lighting.md)

**应用**:
- 矩形光源
- 圆形光源
- 实时 GI

### Memory Pool
**定义**: 对象池，预分配内存以避免频繁分配。

**相关文档**: [optimization/04-memory-management.md](advanced/optimization/04-memory-management.md)

**优势**:
- 减少分配开销
- 避免内存碎片
- 提升缓存命中

### Octahedron Encoding
**定义**: 八面体编码，用于压缩法线向量的技术。

**相关文档**: [optimization/02-mesh-optimization.md](advanced/optimization/02-mesh-optimization.md)

**压缩**:
- float3 (12 bytes) → int16_t[2] (4 bytes)
- 3:1 压缩比
- 精度损失 < 1°

### Occlusion Culling
**定义**: 遮挡剔除，不渲染被其他物体遮挡的对象。

**相关文档**: [rendering/07-culling-techniques.md](advanced/rendering/07-culling-techniques.md)

**技术**:
- Hardware Occlusion Query
- Software Rasterization
- Hierarchical Z-Buffer

### PCF (Percentage Closer Filtering)
**定义**: 百分比邻近过滤，用于软化阴影边缘的技术。

**相关文档**: [rendering/01-shadow-techniques.md](advanced/rendering/01-shadow-techniques.md)

**采样模式**:
- 2x2: 4 采样
- 3x3: 9 采样
- 5x5: 25 采样

### PCSS (Percentage Closer Soft Shadows)
**定义**: 百分比邻近软阴影，自适应软阴影技术。

**相关文档**: [rendering/01-shadow-techniques.md](advanced/rendering/01-shadow-techniques.md)

**特点**:
- 接触硬，远处软
- 物理正确
- 性能开销大

### Render Graph
**定义**: 渲染图，描述渲染 Pass 依赖关系的数据结构。

**相关文档**: [architecture/03-pipeline-architecture.md](advanced/architecture/03-pipeline-architecture.md)

**优势**:
- 自动资源管理
- 自动同步
- 优化执行顺序

### RenderDoc
**定义**: 开源图形调试工具，用于捕获和分析渲染帧。

**相关文档**: [debugging/02-renderdoc-usage.md](advanced/debugging/02-renderdoc-usage.md)

**功能**:
- 帧捕获
- Shader 调试
- 性能分析
- 资源查看

### SMAA (Subpixel Morphological Anti-Aliasing)
**定义**: 亚像素形态抗锯齿，基于形态学的抗锯齿技术。

**相关文档**: [rendering/05-anti-aliasing.md](advanced/rendering/05-anti-aliasing.md)

**质量**:
- Low: 快速
- Medium: 平衡
- High: 高质量

### SSAO (Screen Space Ambient Occlusion)
**定义**: 屏幕空间环境光遮蔽，在屏幕空间计算 AO 的技术。

**相关文档**: [rendering/03-ambient-occlusion.md](advanced/rendering/03-ambient-occlusion.md)

**参数**:
- 采样数: 8-64
- 采样半径: 0.5-2.0
- 偏移值: 0.001-0.01

### Static Batching
**定义**: 静态批处理，离线合并静态网格。

**相关文档**: [optimization/03-drawcall-reduction.md](advanced/optimization/03-drawcall-reduction.md)

**限制**:
- 物体不能移动
- 必须同材质
- 增加内存占用

### TAA (Temporal Anti-Aliasing)
**定义**: 时域抗锯齿，利用历史帧信息的抗锯齿技术。

**相关文档**: [rendering/05-anti-aliasing.md](advanced/rendering/05-anti-aliasing.md)

**特点**:
- 最佳质量
- 可能产生重影
- 需要运动向量

### Texture Atlas
**定义**: 纹理图集，将多个小纹理合并到一张大纹理。

**相关文档**: [optimization/01-texture-optimization.md](advanced/optimization/01-texture-optimization.md)

**优势**:
- 减少 draw call
- 减少纹理切换
- 提升批处理效率

### Tone Mapping
**定义**: 色调映射，将 HDR 颜色映射到 LDR 显示范围。

**相关文档**: [rendering/08-hdr-tone-mapping.md](advanced/rendering/08-hdr-tone-mapping.md)

**算子**:
- Reinhard: 简单快速
- ACES: 电影级
- Filmic: 游戏常用
- Uncharted 2: 对比度强

### Triple Buffering
**定义**: 三缓冲，使用三个缓冲区减少 CPU-GPU 同步。

**相关文档**: [optimization/07-cpu-gpu-sync.md](advanced/optimization/07-cpu-gpu-sync.md)

**优势**:
- 减少等待
- 提升吞吐量
- 增加延迟

### Variance Shadow Maps (VSM)
**定义**: 方差阴影贴图，使用统计方法的软阴影技术。

**相关文档**: [rendering/01-shadow-techniques.md](advanced/rendering/01-shadow-techniques.md)

**优势**:
- 可过滤
- 性能稳定
- 可能漏光

### Vertex Cache Optimization
**定义**: 顶点缓存优化，重排索引以提高缓存命中率。

**相关文档**: [optimization/02-mesh-optimization.md](advanced/optimization/02-mesh-optimization.md)

**算法**:
- Tom Forsyth 算法
- Linear-Speed 算法
- meshoptimizer 库

### Volumetric Lighting
**定义**: 体积光，模拟光线在体积中散射的效果。

**相关文档**: [rendering/04-advanced-lighting.md](advanced/rendering/04-advanced-lighting.md)

**应用**:
- God Rays
- 雾效
- 水下光线

---

## 📚 文档更新

**更新日期**: 2025-10-22
**新增术语数**: 35
**覆盖主题**: 
- 渲染算法 (阴影、光照、抗锯齿、后处理)
- 性能优化 (纹理、网格、Draw Call、内存)
- 调试工具 (RenderDoc、性能分析)
- 架构设计 (渲染图、多线程)

---

**返回**: [主文档](README.md)
