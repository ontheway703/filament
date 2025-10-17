# gltfio 总体架构

## 概述

gltfio 是 Filament 渲染引擎中用于加载和管理 glTF 2.0 资源的核心库。它负责将 glTF 文件解析为 Filament 可以渲染的对象，包括网格、材质、纹理、动画和骨骼蒙皮。

## 核心组件

### 1. AssetLoader (libs/gltfio/include/gltfio/AssetLoader.h)

**职责**: 解析 glTF 文件并创建 FilamentAsset 对象

**关键功能**:
- 解析 glTF JSON 或 GLB 二进制格式
- 创建 VertexBuffer 和 IndexBuffer
- 构建实体层次结构
- 管理材质缓存

**主要 API**:
```cpp
AssetLoader* AssetLoader::create(const AssetConfiguration& config);
FilamentAsset* createAsset(const uint8_t* bytes, uint32_t nbytes);
FilamentAsset* createInstancedAsset(const uint8_t* bytes, uint32_t numBytes,
        FilamentInstance** instances, size_t numInstances);
FilamentInstance* createInstance(FilamentAsset* asset);
void destroyAsset(const FilamentAsset* asset);
```

**代码位置**: `libs/gltfio/src/AssetLoader.cpp`

### 2. ResourceLoader (libs/gltfio/include/gltfio/ResourceLoader.h)

**职责**: 加载外部资源并上传到 GPU

**关键功能**:
- 加载纹理和缓冲区数据
- 处理 URI 资源（文件系统、data URI、用户提供）
- 异步纹理解码
- 计算切线空间（tangents）
- 上传顶点和索引数据到 GPU

**主要 API**:
```cpp
ResourceLoader(const ResourceConfiguration& config);
void addResourceData(const char* uri, BufferDescriptor&& buffer);
void addTextureProvider(const char* mimeType, TextureProvider* provider);
bool loadResources(FilamentAsset* asset);  // 同步
bool asyncBeginLoad(FilamentAsset* asset);  // 异步
void asyncUpdateLoad();
float asyncGetLoadProgress() const;
```

**代码位置**: `libs/gltfio/src/ResourceLoader.cpp`

### 3. FilamentAsset (libs/gltfio/include/gltfio/FilamentAsset.h)

**职责**: 资源容器，拥有所有 Filament 对象

**包含的资源**:
- VertexBuffer 和 IndexBuffer
- Texture 对象
- MaterialInstance
- Entity 列表（渲染对象、灯光、相机）
- 骨骼蒙皮数据
- 动画数据

**关键特性**:
- 单个 Asset 可以创建多个 Instance
- Asset 拥有共享资源（mesh、texture）
- Instance 拥有独立的 entities 和 transforms

**代码位置**: `libs/gltfio/src/FFilamentAsset.h`

### 4. FilamentInstance (libs/gltfio/include/gltfio/FilamentInstance.h)

**职责**: 资源实例，可以独立控制和渲染

**包含的数据**:
- Entity 列表（独立的实体集）
- MaterialInstance 列表
- 骨骼蒙皮目标（Skin targets）
- Animator（动画控制器）
- 变换组件

**实例化优势**:
- 共享 VertexBuffer、IndexBuffer、Texture
- 独立的变换和动画状态
- 独立的材质参数
- 节省内存和 GPU 资源

**代码位置**: `libs/gltfio/src/FFilamentInstance.h`

### 5. Animator (libs/gltfio/include/gltfio/Animator.h)

**职责**: 更新动画和骨骼蒙皮

**关键功能**:
- 应用关键帧动画（TRS: Translation, Rotation, Scale）
- 更新变形目标权重（Morph Targets）
- 计算骨骼矩阵（Bone Matrices）
- 支持动画混合（CrossFade）

**主要 API**:
```cpp
void applyAnimation(size_t animationIndex, float time) const;
void updateBoneMatrices();
void applyCrossFade(size_t previousAnimIndex, float previousAnimTime, float alpha);
void resetBoneMatrices();
size_t getAnimationCount() const;
float getAnimationDuration(size_t animationIndex) const;
```

**代码位置**: `libs/gltfio/src/Animator.cpp`

### 6. MaterialProvider (libs/gltfio/include/gltfio/MaterialProvider.h)

**职责**: 提供符合 glTF 规范的材质

**两种实现**:

#### JitShaderProvider
- 运行时动态生成 shader
- 需要 filamat 库
- 生成优化的 shader 代码
- 适合桌面平台

#### UbershaderProvider
- 使用预编译的 ubershader
- 不需要 filamat
- 通过 #define 控制功能
- 适合移动平台和 Web

**MaterialKey**: 20 字节的结构，描述材质需求
- 纹理类型（baseColor, metallic, normal 等）
- UV 集索引
- 双面、透明模式
- PBR 扩展（clearcoat, transmission, sheen 等）

**代码位置**: `libs/gltfio/src/MaterialProvider.cpp`

## 主要工作流程

### 完整加载流程

```
1. 创建 AssetLoader
   └─ AssetLoader::create(config)
      ├─ 配置 Engine
      ├─ 配置 MaterialProvider
      └─ 配置 EntityManager

2. 加载 glTF 文件
   └─ AssetLoader::createAsset(bytes, size)
      ├─ cgltf_parse() 解析 glTF
      ├─ createRootAsset()
      │  ├─ 遍历所有 mesh
      │  ├─ createPrimitives()
      │  │  ├─ 创建 VertexBuffer
      │  │  ├─ 创建 IndexBuffer
      │  │  └─ 缓存到 MeshCache
      │  └─ 收集 ResourceURI
      └─ createInstances()
         ├─ recurseEntities() 创建实体层次
         ├─ createRenderable() 创建可渲染组件
         ├─ createMaterialInstance()
         └─ importSkins() 导入骨骼

3. 加载资源
   └─ ResourceLoader::loadResources(asset)
      ├─ loadCgltfBuffers() 加载缓冲区数据
      ├─ uploadBuffers() 上传顶点/索引数据
      ├─ computeTangents() 计算切线
      ├─ createSkins() 创建骨骼数据
      └─ createTextures() 创建并解码纹理
         ├─ TextureProvider::pushTexture()
         └─ 异步解码队列

4. 添加到场景
   └─ scene->addEntities(asset->getEntities(), count)

5. 动画循环
   └─ 每帧更新
      ├─ animator->applyAnimation(index, time)
      │  └─ 更新 TransformManager
      └─ animator->updateBoneMatrices()
         └─ RenderableManager::setBones()
```

### 实例化流程

```
创建额外实例:
└─ AssetLoader::createInstance(asset)
   ├─ 创建新的 root entity
   ├─ 复用 Asset 的 MeshCache
   ├─ 创建新的 MaterialInstance
   ├─ recurseEntities() 创建新的实体树
   └─ importSkins() 导入骨骼（新的 entities）
```

## 关键设计理念

### 1. 资源共享与实例化

**Asset vs Instance**:
- **Asset**: 拥有共享的 GPU 资源（VertexBuffer, IndexBuffer, Texture）
- **Instance**: 拥有独立的 CPU 资源（Entity, Transform, MaterialInstance）

这种设计允许高效地渲染同一模型的多个副本。

### 2. 两阶段加载

**阶段 1: AssetLoader**
- 解析 glTF 结构
- 创建 Filament 对象（Buffer、Material）
- 构建实体层次结构
- **不加载外部数据**

**阶段 2: ResourceLoader**
- 加载纹理和缓冲区数据
- 上传到 GPU
- 计算派生数据（tangents）
- 支持异步加载

这种分离允许:
- 灵活的资源获取策略
- 异步加载和渐进式渲染
- 不同平台的不同加载方式

### 3. 依赖图和渐进式加载

**DependencyGraph** (`libs/gltfio/src/DependencyGraph.h`):
- 跟踪 Entity 对 MaterialInstance 和 Texture 的依赖
- 支持 `popRenderable()` API
- 允许在纹理加载完成时逐步添加实体到场景

**使用场景**:
```cpp
// 开始异步加载
resourceLoader.asyncBeginLoad(asset);

// 渲染循环中逐步添加实体
while (Entity e = asset->popRenderable()) {
    scene->addEntity(e);
}

// 检查进度
float progress = resourceLoader.asyncGetLoadProgress();
```

### 4. cgltf 作为数据源

gltfio 使用 [cgltf](https://github.com/jkuhlmann/cgltf) 库解析 glTF：
- 单头文件库
- 零内存拷贝（指针直接指向原始数据）
- 支持所有 glTF 2.0 扩展

**生命周期管理**:
```cpp
struct SourceAsset {
    cgltf_data* hierarchy;
    DracoCache dracoCache;
    FixedCapacityVector<uint8_t> glbData;
};

// 通过 shared_ptr 管理
using SourceHandle = std::shared_ptr<SourceAsset>;
```

### 5. 材质系统的灵活性

**MaterialKey 缓存**:
- 每个独特的材质配置只创建一个 Material
- MaterialInstance 可以共享同一个 Material
- 支持材质变体（Material Variants）

**UvMap**:
- glTF 支持 8 个 UV 集
- Filament 只支持 2 个（UV0, UV1）
- MaterialProvider 自动将 glTF UV 映射到 Filament UV

## 内存管理

### 对象所有权

| 对象类型 | 拥有者 | 生命周期 |
|---------|--------|---------|
| VertexBuffer | FilamentAsset | 直到 destroyAsset() |
| IndexBuffer | FilamentAsset | 直到 destroyAsset() |
| Texture | FilamentAsset | 直到 destroyAsset() |
| Material | MaterialProvider | 需手动调用 destroyMaterials() |
| MaterialInstance | FilamentInstance | 直到 ~FilamentInstance() |
| Entity | FilamentAsset/Instance | 直到 destroyAsset() |
| BufferObject | FilamentAsset | 直到 destroyAsset() |
| cgltf_data | SourceAsset (shared_ptr) | 引用计数 |

### releaseSourceData()

调用 `asset->releaseSourceData()` 后：
- 释放 cgltf_data
- 释放 URI 字符串
- 释放原始动画数据
- **不能再创建新的 Instance**

时机：在 `ResourceLoader::loadResources()` 之后调用。

## 性能考虑

### MeshCache

- 每个 cgltf_mesh 只创建一次 VertexBuffer/IndexBuffer
- 多个 cgltf_node 引用同一个 mesh 时共享缓冲区
- 节省内存和 GPU 资源

### MaterialInstance Cache

- 在单次加载过程中缓存
- 避免为相同材质创建多个实例
- 缓存按 `cgltf_material*` 索引

### BufferObject

- 使用 BufferObject 包装 GPU 缓冲区
- 支持懒加载和异步上传
- 回调机制管理 CPU 内存释放

## 平台差异

### 文件系统支持

```cpp
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__) || defined(FILAMENT_IOS)
#define GLTFIO_USE_FILESYSTEM 0
#else
#define GLTFIO_USE_FILESYSTEM 1
#endif
```

- **有文件系统**: 直接从路径加载资源
- **无文件系统**: 需要通过 `addResourceData()` 提供数据

### 扩展加载算法

**AssetLoaderExtended** (`libs/gltfio/src/extended/AssetLoaderExtended.h`):
- 支持 mikktspace 切线计算
- 仅支持桌面平台（非 Android/iOS/Web）
- 需要本地文件系统访问

## 参考文档

- [glTF 2.0 规范](https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html)
- [Filament 文档](https://google.github.io/filament/)
- [cgltf GitHub](https://github.com/jkuhlmann/cgltf)

## 下一步阅读

- [资源加载流程](./resource-loading.md) - 详细的资源加载机制
- [Mesh 处理](./mesh-processing.md) - 顶点和索引缓冲区创建
- [材质系统](./material-system.md) - 材质和纹理管理
- [骨骼动画](./skeletal-animation.md) - 骨骼蒙皮和动画系统
- [GPU 交互](./gpu-interaction.md) - GPU 资源管理和上传
- [架构关键点](./architecture.md) - 深入的架构设计
