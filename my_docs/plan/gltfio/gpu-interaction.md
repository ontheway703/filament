# GPU 交互

## 概述

gltfio 通过 Filament 的 Backend 抽象层与 GPU 交互，主要涉及：
1. **BufferObject** - 顶点和索引数据上传
2. **Texture** - 纹理数据上传和解码
3. **DependencyGraph** - 渐进式加载管理

## BufferObject

### 创建和上传

**代码位置**: `libs/gltfio/src/ResourceLoader.cpp:256-267`

```cpp
// 创建 BufferObject
BufferObject* bo = BufferObject::Builder()
    .size(size)
    .build(engine);

// 上传数据
bo->setBuffer(engine, BufferDescriptor(
    data,              // CPU 数据指针
    size,              // 数据大小
    uploadCallback,    // 完成回调
    uploadUserdata     // 用户数据
));

// 绑定到 VertexBuffer
vertexBuffer->setBufferObjectAt(engine, slot, bo);
```

### BufferDescriptor

**组成**:
```cpp
struct BufferDescriptor {
    void* buffer;                // CPU 数据
    size_t size;                 // 字节数
    Callback callback;           // 回调函数
    void* user;                  // 用户数据
};
```

**回调时机**: 数据上传到 GPU 后，可以安全释放 CPU 内存

### UploadEvent

**代码位置**: `libs/gltfio/src/ResourceLoader.cpp:117-129`

```cpp
struct UploadEvent {
    FFilamentAsset::SourceHandle handle;        // 保持对 cgltf_data 的引用
    UriDataCacheHandle dataCacheHandle;         // 保持对 URI 数据的引用
};

UploadEvent* uploadUserdata(FFilamentAsset* asset, UriDataCacheHandle dataCache) {
    return new UploadEvent({ asset->mSourceAsset, dataCache });
}

void uploadCallback(void* buffer, size_t size, void* user) {
    auto event = (UploadEvent*) user;
    delete event;  // 释放引用，允许数据被回收
}
```

**目的**: 确保在 GPU 上传完成前，源数据不被释放

### 数据类型转换

**需要转换的情况**:
1. 8 位索引 → 16 位索引
2. 归一化整数 → 浮点数
3. 其他非原生格式

**示例** (`libs/gltfio/src/ResourceLoader.cpp:270-277`):
```cpp
if (accessor->component_type == cgltf_component_type_r_8u) {
    // 8 位索引不被支持，转换为 16 位
    const size_t size16 = size * 2;
    uint16_t* data16 = (uint16_t*) malloc(size16);
    utility::convertBytesToShorts(data16, data, size);

    IndexBuffer::BufferDescriptor bd(data16, size16, FREE_CALLBACK);
    slot.indexBuffer->setBuffer(engine, std::move(bd));
}
```

## Texture 上传

### TextureProvider

**接口**:
```cpp
class TextureProvider {
public:
    virtual Texture* pushTexture(
        const uint8_t* data,
        size_t dataSize,
        const char* mimeType,
        TextureFlags flags
    ) = 0;

    virtual Texture* popTexture() = 0;
    virtual size_t getPushedCount() const = 0;
    virtual size_t getPoppedCount() const = 0;
    virtual void waitForCompletion() = 0;
    virtual void updateQueue() = 0;
    virtual void cancelDecoding() = 0;
    virtual const char* getPushMessage() const = 0;
};
```

### StbProvider

**实现位置**: `libs/gltfio/src/StbProvider.cpp`

**工作流程**:
```
1. pushTexture()
   ├─ 解码图像头部，获取尺寸和格式
   ├─ 创建 Filament Texture 对象（空壳）
   ├─ 创建解码 Job
   └─ 返回 Texture*

2. [Background Thread] 解码 Job
   └─ stbi_load_from_memory() 解码图像
      └─ 将解码后的数据推入完成队列

3. [Main Thread] updateQueue()
   └─ 遍历完成队列
      └─ Texture::setImage() 上传数据到 GPU
         └─ 将 Texture 推入 ready 队列

4. [Main Thread] popTexture()
   └─ 从 ready 队列弹出 Texture
```

### Ktx2Provider

**实现位置**: `libs/gltfio/src/Ktx2Provider.cpp`

**特性**:
- 支持压缩纹理格式 (ETC2, ASTC, BC7 等)
- 支持 mipmap
- 支持 Basis Universal 转码

**优势**:
- 更小的文件体积
- 更快的 GPU 上传（已压缩）
- 节省 GPU 内存

### 异步纹理解码

**多线程架构**:
```
Main Thread                 Worker Threads
    │
    ├─ pushTexture()  ───────> [Job Queue]
    │                              │
    │                              ├─ Thread 1: decode image
    │                              ├─ Thread 2: decode image
    │                              └─ Thread 3: decode image
    │                                    │
    │                        [Completion Queue] <──┘
    │                              │
    ├─ updateQueue()  <────────────┘
    │   └─ Texture::setImage()  // 上传 GPU
    │           │
    │   [Ready Queue] <─────┘
    │           │
    └─ popTexture()  <──────┘
```

### Texture::setImage

**API**:
```cpp
void Texture::setImage(
    Engine& engine,
    size_t level,                // Mipmap level
    PixelBufferDescriptor&& buffer
);
```

**PixelBufferDescriptor**:
```cpp
struct PixelBufferDescriptor {
    void* buffer;
    size_t size;
    PixelDataFormat format;      // RGBA, RGB, RG, R, etc.
    PixelDataType type;          // UBYTE, FLOAT, etc.
    Callback callback;
    void* user;
    uint32_t left, top;          // 区域更新
    uint32_t stride;
};
```

## DependencyGraph

**代码位置**: `libs/gltfio/src/DependencyGraph.h`

**目的**: 实现渐进式加载 - 只有当实体的所有依赖（纹理、材质）准备好后，才将其添加到场景

### 数据结构

```cpp
class DependencyGraph {
public:
    // 添加依赖边
    void addEdge(Entity entity, MaterialInstance* mi);
    void addEdge(Entity entity, Texture* texture);
    void addEdge(MaterialInstance* mi, Texture* texture);

    // 标记资源为就绪
    void markAsReady(Texture* texture);

    // 弹出准备好的实体
    Entity popRenderable();
    size_t popRenderables(Entity* entities, size_t count);

    // 禁用渐进式加载
    void disableProgressiveReveal();

    // 提交边（不再接受新边）
    void commitEdges();
};
```

### 工作流程

```
1. AssetLoader 创建实体和材质
   └─ addEdge(entity, materialInstance)
      └─ addEdge(materialInstance, texture)

2. ResourceLoader 创建纹理
   └─ 纹理解码完成

3. asyncUpdateLoad()
   └─ popTexture() from TextureProvider
      └─ markAsReady(texture)
         └─ 检查依赖此纹理的 MaterialInstance
            └─ 如果 MaterialInstance 的所有纹理都就绪
               └─ 标记 MaterialInstance 为就绪
                  └─ 检查依赖此 MaterialInstance 的 Entity
                     └─ 如果 Entity 的所有 MaterialInstance 都就绪
                        └─ 将 Entity 加入 ready 队列

4. popRenderable()
   └─ 从 ready 队列弹出 Entity
      └─ scene->addEntity(entity)
```

### 实现细节

**节点类型**:
```cpp
enum class NodeType {
    ENTITY,
    MATERIAL_INSTANCE,
    TEXTURE
};
```

**依赖计数**:
```cpp
struct Node {
    NodeType type;
    void* pointer;              // Entity, MaterialInstance*, 或 Texture*
    int incomingEdgeCount;      // 依赖数
    std::vector<Node*> outgoingEdges;  // 被依赖的节点
};
```

**标记就绪算法**:
```cpp
void markAsReady(Texture* texture) {
    Node* node = findNode(texture);
    if (!node) return;

    // 遍历所有依赖此纹理的节点
    for (Node* dependent : node->outgoingEdges) {
        dependent->incomingEdgeCount--;

        // 如果所有依赖都满足，标记为就绪
        if (dependent->incomingEdgeCount == 0) {
            if (dependent->type == NodeType::ENTITY) {
                readyEntities.push(dependent->pointer);
            } else if (dependent->type == NodeType::MATERIAL_INSTANCE) {
                // 递归标记依赖此 MaterialInstance 的 Entity
                markAsReady((MaterialInstance*) dependent->pointer);
            }
        }
    }
}
```

## 内存管理

### CPU 内存释放

**策略**: 使用回调机制，在 GPU 上传完成后释放

**示例**:
```cpp
// 同步释放（简单）
const auto FREE_CALLBACK = [](void* mem, size_t, void*) { free(mem); };

// 异步释放（保持引用）
void uploadCallback(void* buffer, size_t size, void* user) {
    auto event = (UploadEvent*) user;
    delete event;  // 释放 shared_ptr，允许源数据被回收
}
```

### GPU 内存管理

**Filament 的责任**:
- VertexBuffer、IndexBuffer、Texture 等对象销毁时自动释放 GPU 内存
- BufferObject 销毁时释放 GPU 缓冲区

**gltfio 的责任**:
- 在 `destroyAsset()` 时销毁所有 Filament 对象
- 管理对象的生命周期（通过 std::vector 持有）

### SourceAsset 引用计数

**代码位置**: `libs/gltfio/src/FFilamentAsset.h:288-299`

```cpp
struct SourceAsset {
    ~SourceAsset() {
        cgltf_free(hierarchy);
    }
    cgltf_data* hierarchy;
    DracoCache dracoCache;
    utils::FixedCapacityVector<uint8_t> glbData;
};

using SourceHandle = std::shared_ptr<SourceAsset>;
```

**引用位置**:
1. `FFilamentAsset::mSourceAsset`
2. `UploadEvent` (每个上传的 BufferObject)

**释放时机**: 所有引用都释放后（通常在上传完成且 `releaseSourceData()` 被调用后）

## 压缩和解压

### Draco 网格压缩

**库**: Google Draco

**工作流程**:
```
1. loadResources()
   └─ utility::decodeDracoMeshes(gltf, prim, dracoCache)
      └─ draco::Decoder::DecodeMeshFromBuffer()
         └─ 将解压后的数据存储到临时缓冲区
            └─ cgltf_accessor 指向解压后的数据

2. uploadBuffers()
   └─ 从 cgltf_accessor 读取数据（已解压）
      └─ 上传到 GPU
```

**DracoCache**: 避免重复解压（如果多个 primitive 引用同一个压缩数据）

### meshopt 压缩

**库**: meshoptimizer

**特性**:
- 顶点缓冲区压缩
- 索引缓冲区压缩
- 支持流式解压

**工作流程**:
```cpp
utility::decodeMeshoptCompression((cgltf_data*) gltf);
// 遍历所有 buffer_view，解压 EXT_meshopt_compression
// 将解压后的数据存储到 buffer_view->data
```

## 纹理缓存

**目的**: 避免重复创建相同的纹理

### BufferTextureCache

**键**: 内存地址 (const void*)

**用途**: 缓存来自 BufferView、data URI、UriDataCache 的纹理

```cpp
tsl::robin_map<const void*, Texture*> mBufferTextureCache;
```

### FilepathTextureCache

**键**: 文件路径 (std::string)

**用途**: 缓存来自文件系统的纹理

```cpp
tsl::robin_map<std::string, Texture*> mFilepathTextureCache;
```

### 查找流程

**代码位置**: `libs/gltfio/src/ResourceLoader.cpp:525-625`

```
1. 检查 BufferView 缓存
2. 检查 data URI 缓存
3. 检查 UriDataCache 缓存
4. 检查文件路径缓存
5. 都没有 → 创建新纹理并缓存
```

## 性能优化

### 并行上传

**策略**: BufferObject 和 Texture 的上传是并行的

```cpp
// 创建多个 BufferObject，Filament 会并行上传
for (auto const& slot: slots) {
    BufferObject* bo = BufferObject::Builder().size(size).build(engine);
    bo->setBuffer(engine, BufferDescriptor(...));
    slot.vertexBuffer->setBufferObjectAt(engine, slot.bufferIndex, bo);
}
```

### 异步纹理解码

**优势**:
- 不阻塞主线程
- 充分利用多核 CPU
- 解码和上传可以并行（一边解码新纹理，一边上传已解码的纹理）

### 压缩纹理

**优势**:
- 减少 GPU 内存占用
- 减少带宽占用
- 加快上传速度（已压缩数据通常更小）

**支持的格式**:
- ETC2 (移动设备)
- ASTC (移动设备)
- BC7 (桌面)
- Basis Universal (跨平台转码)

## 渐进式加载示例

```cpp
// 开始异步加载
resourceLoader.asyncBeginLoad(asset);

// 渲染循环
while (progress < 1.0f) {
    // 更新纹理队列
    resourceLoader.asyncUpdateLoad();

    // 逐步添加准备好的实体
    while (Entity e = asset->popRenderable()) {
        scene->addEntity(e);
    }

    // 渲染
    if (renderer->beginFrame(swapChain)) {
        renderer->render(view);
        renderer->endFrame();
    }

    // 检查进度
    progress = resourceLoader.asyncGetLoadProgress();
}
```

**效果**: 纹理加载完成一个，就立即显示一个，用户体验更流畅

## 错误处理

### 上传失败

**检测**: BufferDescriptor 的回调不会被调用

**处理**: Filament 内部处理，gltfio 无需特殊处理

### 纹理解码失败

**检测**: `TextureProvider::pushTexture()` 返回 nullptr

**处理**:
```cpp
if (Texture* texture = provider->pushTexture(...); texture) {
    // 成功
} else {
    slog.e << "Unable to create texture: " << provider->getPushMessage() << io::endl;
}
```

### 内存不足

**BufferObject**:
- Filament 会在内部处理，可能会失败
- gltfio 无法检测，依赖 Filament 的错误处理

**Texture**:
- TextureProvider 可能返回 nullptr
- gltfio 记录错误日志

## 下一步阅读

- [资源加载](./resource-loading.md) - uploadBuffers 和 createTextures 的详细流程
- [Mesh 处理](./mesh-processing.md) - BufferSlot 系统
- [材质系统](./material-system.md) - 纹理绑定和依赖
