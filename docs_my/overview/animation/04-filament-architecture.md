# Filament 架构概述

本文档详细介绍 Filament 渲染引擎的整体架构，重点分析与动画系统相关的组件和设计模式。

## 目录

1. [Filament 总体架构](#filament-总体架构)
2. [核心组件系统](#核心组件系统)
3. [实体组件系统 (ECS)](#实体组件系统-ecs)
4. [渲染管线架构](#渲染管线架构)
5. [资源管理系统](#资源管理系统)
6. [多后端抽象](#多后端抽象)
7. [内存管理策略](#内存管理策略)

## Filament 总体架构

### 整体设计理念

Filament 采用现代 C++ 设计，遵循以下原则：
- **性能优先**: 针对移动设备和嵌入式系统优化
- **跨平台**: 支持 Android、iOS、Linux、macOS、Windows、WebGL
- **现代图形 API**: 抽象层支持 Vulkan、Metal、OpenGL、WebGL
- **基于物理的渲染**: 实现业界标准的 PBR 管线

### 架构层次

```
应用层 (Application Layer)
├── FilamentApp / Samples
└── 用户应用代码

高级 API 层 (High-level API)
├── gltfio (glTF 加载和动画)
├── viewer (查看器组件)
├── camutils (摄像机工具)
└── utils (通用工具)

核心引擎层 (Core Engine)
├── filament (渲染引擎)
├── filamat (材质编译器)
└── filabridge (桥接库)

底层抽象层 (Low-level Abstraction)
├── backend (图形后端抽象)
├── bluevk (Vulkan 绑定)
├── bluegl (OpenGL 绑定)
└── utils (底层工具)

数学和基础库 (Foundation)
├── math (数学库)
└── geometry (几何处理)
```

### 核心目录结构

```
filament/
├── filament/           # 核心渲染引擎
│   ├── include/        # 公共 API
│   ├── src/            # 实现代码
│   └── backend/        # 渲染后端
├── libs/
│   ├── gltfio/         # glTF 加载和动画
│   ├── filamat/        # 材质系统
│   ├── math/           # 数学库
│   ├── utils/          # 通用工具
│   └── viewer/         # 查看器组件
├── tools/              # 工具程序
└── samples/            # 示例程序
```

## 核心组件系统

### Engine 类层次结构

Engine 是 Filament 的核心类，负责管理所有渲染资源：

```cpp
// filament/include/filament/Engine.h
class Engine {
public:
    // 创建和销毁
    static Engine* create(Backend backend = Backend::DEFAULT);
    static void destroy(Engine** engine);

    // 管理器访问
    RenderableManager& getRenderableManager() noexcept;
    TransformManager& getTransformManager() noexcept;
    LightManager& getLightManager() noexcept;
    CameraManager& getCameraManager() noexcept;

    // 资源创建
    VertexBuffer* createVertexBuffer(const VertexBuffer::Builder& builder);
    IndexBuffer* createIndexBuffer(const IndexBuffer::Builder& builder);
    Material* createMaterial(const void* package, size_t size);
    Texture* createTexture(const Texture::Builder& builder);

    // 渲染控制
    void execute();  // 执行一帧渲染
    void flushAndWait();  // 等待 GPU 完成
};
```

### 管理器模式

Filament 使用管理器模式来组织不同类型的组件：

#### 1. RenderableManager
管理可渲染实体的几何和材质信息：

```cpp
class RenderableManager {
public:
    using Instance = ComponentManager<RenderableComponent>::Instance;

    // 构建器模式
    class Builder {
    public:
        Builder& geometry(size_t index, PrimitiveType type,
                         VertexBuffer* vertices, IndexBuffer* indices);
        Builder& material(size_t index, MaterialInstance* material);
        Builder& boundingBox(const Box& aabb);
        Builder& skinning(size_t boneCount, mat4f const* transforms);
        Builder& morphing(MorphTargetBuffer* morphTargetBuffer);
        void build(Engine& engine, Entity entity);
    };

    // 运行时操作
    void setMorphWeights(Instance instance, const float* weights, size_t count);
    void setBones(Instance instance, const mat4f* transforms, size_t count);
};
```

#### 2. TransformManager
管理实体的空间变换：

```cpp
class TransformManager {
public:
    using Instance = ComponentManager<TransformComponent>::Instance;

    // 变换操作
    void setTransform(Instance ci, const mat4f& model);
    mat4f getTransform(Instance ci) const;
    mat4f getWorldTransform(Instance ci) const;

    // 层次结构
    void setParent(Instance ci, Instance parent);
    Instance getParent(Instance ci) const;
    void getChildren(Instance ci, Entity* children, size_t* count) const;

    // 批量更新
    void openLocalTransformTransaction();
    void commitLocalTransformTransaction();
};
```

### 组件生命周期

```cpp
// 创建实体和组件的典型流程
Entity entity = entityManager.create();

// 添加变换组件
auto transform = engine.getTransformManager().getInstance(entity);
engine.getTransformManager().setTransform(transform, modelMatrix);

// 添加可渲染组件
RenderableManager::Builder()
    .geometry(0, PrimitiveType::TRIANGLES, vertexBuffer, indexBuffer)
    .material(0, materialInstance)
    .boundingBox({{-1, -1, -1}, {1, 1, 1}})
    .build(engine, entity);

// 添加到场景
scene.addEntity(entity);
```

## 实体组件系统 (ECS)

### ECS 架构设计

Filament 使用轻量级的 ECS 架构：

```
Entity (uint32_t)
├── TransformComponent    # 空间变换
├── RenderableComponent   # 渲染信息
├── LightComponent        # 光照属性
├── CameraComponent       # 摄像机属性
└── NameComponent         # 名称（可选）
```

#### Entity 类型定义
```cpp
// utils/include/utils/Entity.h
namespace utils {
    using Entity = uint32_t;

    class EntityManager {
    public:
        Entity create() noexcept;
        Entity create(size_t n, Entity* entities) noexcept;
        void destroy(Entity e) noexcept;
        void destroy(size_t n, const Entity* entities) noexcept;
        bool isAlive(Entity e) const noexcept;
    };
}
```

#### 组件管理器基类
```cpp
template<typename T>
class ComponentManager {
public:
    using Instance = ComponentManagerImpl::Instance;

    // 实例管理
    Instance getInstance(Entity e) noexcept;
    Entity getEntity(Instance ci) const noexcept;
    bool hasComponent(Entity e) const noexcept;

    // 批量操作
    void gc(utils::EntityManager& em) noexcept;  // 垃圾回收
    void clear() noexcept;

protected:
    // 子类实现
    virtual void terminate(Instance ci) noexcept = 0;
};
```

### 动画相关组件

#### 1. Transform 组件数据
```cpp
struct TransformComponent {
    mat4f local;          // 本地变换矩阵
    mat4f world;          // 世界变换矩阵
    Instance parent;      // 父节点
    uint16_t next;        // 链表结构用于遍历子节点
    uint16_t prev;
    uint16_t firstChild;
    uint16_t flags;       // 标志位（脏标记等）
};
```

#### 2. Renderable 组件数据
```cpp
struct RenderableComponent {
    uint32_t geometryCount;           // 几何体数量
    Primitive* primitives;            // 图元数组
    Box aabb;                        // 包围盒
    uint8_t layerMask;               // 层遮罩
    uint8_t priority;                // 渲染优先级
    uint8_t culling;                 // 裁剪模式
    bool skinning;                   // 是否启用蒙皮
    size_t boneCount;                // 骨骼数量
    mat4f* bones;                    // 骨骼矩阵
    MorphTargetBuffer* morphTargets; // 变形目标
    float* morphWeights;             // 变形权重
};
```

### 内存布局优化

#### 1. 结构体数组 (SOA)
```cpp
// 优化的组件存储 - 提高缓存友好性
class TransformManager {
    // 分别存储各个属性
    std::vector<mat4f> mLocalTransforms;
    std::vector<mat4f> mWorldTransforms;
    std::vector<Instance> mParents;
    std::vector<uint16_t> mFirstChild;
    std::vector<uint8_t> mFlags;

    // 通过 Instance 索引访问
    mat4f& getLocalTransform(Instance ci) {
        return mLocalTransforms[ci.getId()];
    }
};
```

#### 2. 内存池分配
```cpp
template<typename T>
class PoolAllocator {
    struct Block {
        T data;
        uint32_t next;  // 下一个空闲块
    };

    std::vector<Block> mPool;
    uint32_t mFreeHead;

public:
    uint32_t allocate() {
        if (mFreeHead == INVALID_INDEX) {
            // 扩展池
            mPool.resize(mPool.size() * 2);
            // 初始化新的空闲链表
        }

        uint32_t index = mFreeHead;
        mFreeHead = mPool[index].next;
        return index;
    }

    void deallocate(uint32_t index) {
        mPool[index].next = mFreeHead;
        mFreeHead = index;
    }
};
```

## 渲染管线架构

### 渲染流程概述

```
1. 场景收集 (Scene Collection)
   ├── 视锥裁剪 (Frustum Culling)
   ├── 遮挡裁剪 (Occlusion Culling)
   └── LOD 选择 (Level of Detail)

2. 渲染队列构建 (Render Queue)
   ├── 材质排序 (Material Sorting)
   ├── 深度排序 (Depth Sorting)
   └── 批次合并 (Batch Merging)

3. 渲染执行 (Render Execution)
   ├── 阴影贴图 (Shadow Maps)
   ├── Z-PrePass (可选)
   ├── 不透明物体 (Opaque Objects)
   ├── 天空盒 (Skybox)
   ├── 半透明物体 (Transparent Objects)
   └── 后处理 (Post-processing)
```

### View 和 Scene

#### View 配置
```cpp
class View {
public:
    // 摄像机设置
    void setCamera(Camera* camera);
    void setViewport(Viewport const& viewport);

    // 渲染目标
    void setRenderTarget(RenderTarget* renderTarget);

    // 裁剪和 LOD
    void setVisibleLayers(uint8_t select, uint8_t values);
    void setCulling(bool enabled);

    // 动画相关
    void setAmbientOcclusion(AmbientOcclusion options);
    void setBloom(Bloom options);
    void setFog(Fog options);
};
```

#### Scene 管理
```cpp
class Scene {
public:
    // 实体管理
    void addEntity(Entity entity);
    void addEntities(const Entity* entities, size_t count);
    void remove(Entity entity);

    // 环境设置
    void setSkybox(Skybox* skybox);
    void setIndirectLight(IndirectLight* ibl);

    // 查询接口
    size_t getRenderableCount() const;
    size_t getLightCount() const;
};
```

### 渲染队列和排序

```cpp
class RenderQueue {
    struct RenderItem {
        uint64_t sortKey;        // 排序键
        MaterialInstance* material;
        Primitive* primitive;
        mat4f transform;
        uint32_t entityId;
    };

    std::vector<RenderItem> mOpaqueItems;
    std::vector<RenderItem> mTransparentItems;

public:
    void add(const RenderableComponent& renderable,
             const mat4f& transform, uint32_t entityId) {
        // 构建排序键
        uint64_t sortKey = buildSortKey(renderable.material,
                                       renderable.priority);

        RenderItem item = {sortKey, renderable.material,
                          &renderable.primitive, transform, entityId};

        if (renderable.material->isOpaque()) {
            mOpaqueItems.push_back(item);
        } else {
            mTransparentItems.push_back(item);
        }
    }

    void sort() {
        // 不透明物体：前到后排序（减少 overdraw）
        std::sort(mOpaqueItems.begin(), mOpaqueItems.end(),
                 [](const RenderItem& a, const RenderItem& b) {
                     return a.sortKey < b.sortKey;
                 });

        // 半透明物体：后到前排序（正确混合）
        std::sort(mTransparentItems.begin(), mTransparentItems.end(),
                 [](const RenderItem& a, const RenderItem& b) {
                     return a.sortKey > b.sortKey;
                 });
    }
};
```

## 资源管理系统

### 资源生命周期

Filament 使用 RAII 和引用计数来管理资源：

```cpp
template<typename T>
class Resource {
    std::atomic<uint32_t> mRefCount{1};

public:
    void addRef() { ++mRefCount; }

    void release() {
        if (--mRefCount == 0) {
            delete this;
        }
    }

    uint32_t getRefCount() const { return mRefCount; }
};

// 智能指针封装
template<typename T>
class ResourcePtr {
    T* mPtr = nullptr;

public:
    ResourcePtr(T* ptr = nullptr) : mPtr(ptr) {
        if (mPtr) mPtr->addRef();
    }

    ~ResourcePtr() {
        if (mPtr) mPtr->release();
    }

    // 拷贝和移动语义
    ResourcePtr(const ResourcePtr& other) : mPtr(other.mPtr) {
        if (mPtr) mPtr->addRef();
    }

    ResourcePtr& operator=(const ResourcePtr& other) {
        if (this != &other) {
            if (mPtr) mPtr->release();
            mPtr = other.mPtr;
            if (mPtr) mPtr->addRef();
        }
        return *this;
    }
};
```

### 异步资源加载

```cpp
class ResourceLoader {
    struct LoadRequest {
        std::string path;
        std::function<void(void*)> callback;
        std::future<void*> future;
    };

    std::queue<LoadRequest> mPendingRequests;
    std::vector<std::thread> mWorkerThreads;

public:
    template<typename T>
    void loadAsync(const std::string& path,
                   std::function<void(T*)> callback) {
        LoadRequest request;
        request.path = path;
        request.callback = [callback](void* data) {
            callback(static_cast<T*>(data));
        };

        request.future = std::async(std::launch::async, [path]() {
            return loadFromDisk(path);
        });

        mPendingRequests.push(request);
    }

    void processCompletedRequests() {
        while (!mPendingRequests.empty()) {
            auto& request = mPendingRequests.front();

            if (request.future.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {

                void* data = request.future.get();
                request.callback(data);
                mPendingRequests.pop();
            } else {
                break;  // 还没完成，退出处理
            }
        }
    }
};
```

### 内存管理策略

#### 1. 线性分配器
```cpp
class LinearAllocator {
    uint8_t* mMemory;
    size_t mSize;
    size_t mOffset;

public:
    LinearAllocator(size_t size) : mSize(size), mOffset(0) {
        mMemory = static_cast<uint8_t*>(std::aligned_alloc(64, size));
    }

    ~LinearAllocator() {
        std::free(mMemory);
    }

    void* allocate(size_t size, size_t alignment = 8) {
        size_t alignedOffset = (mOffset + alignment - 1) & ~(alignment - 1);

        if (alignedOffset + size > mSize) {
            return nullptr;  // 内存不足
        }

        void* ptr = mMemory + alignedOffset;
        mOffset = alignedOffset + size;
        return ptr;
    }

    void reset() {
        mOffset = 0;  // 重置整个分配器
    }
};
```

#### 2. 对象池
```cpp
template<typename T>
class ObjectPool {
    struct Node {
        alignas(T) uint8_t data[sizeof(T)];
        Node* next;
    };

    Node* mFreeList;
    std::vector<std::unique_ptr<Node[]>> mBlocks;
    size_t mBlockSize;

public:
    ObjectPool(size_t blockSize = 1024) : mBlockSize(blockSize) {
        allocateNewBlock();
    }

    T* acquire() {
        if (!mFreeList) {
            allocateNewBlock();
        }

        Node* node = mFreeList;
        mFreeList = mFreeList->next;
        return reinterpret_cast<T*>(node->data);
    }

    void release(T* obj) {
        obj->~T();  // 显式析构

        Node* node = reinterpret_cast<Node*>(obj);
        node->next = mFreeList;
        mFreeList = node;
    }

private:
    void allocateNewBlock() {
        auto block = std::make_unique<Node[]>(mBlockSize);

        // 构建空闲链表
        for (size_t i = 0; i < mBlockSize - 1; ++i) {
            block[i].next = &block[i + 1];
        }
        block[mBlockSize - 1].next = mFreeList;
        mFreeList = &block[0];

        mBlocks.push_back(std::move(block));
    }
};
```

## 多后端抽象

### 后端接口设计

```cpp
// filament/backend/include/backend/Driver.h
class Driver {
public:
    // 资源创建
    virtual Handle<VertexBufferInfo> createVertexBuffer(...) = 0;
    virtual Handle<IndexBufferInfo> createIndexBuffer(...) = 0;
    virtual Handle<TextureInfo> createTexture(...) = 0;
    virtual Handle<ProgramInfo> createProgram(...) = 0;

    // 渲染命令
    virtual void draw(PrimitiveType type, size_t offset, size_t count) = 0;
    virtual void setVertexBuffer(Handle<VertexBufferInfo> vb) = 0;
    virtual void setIndexBuffer(Handle<IndexBufferInfo> ib) = 0;

    // 状态管理
    virtual void setViewport(int32_t left, int32_t bottom,
                            uint32_t width, uint32_t height) = 0;
    virtual void setClearColor(float r, float g, float b, float a) = 0;
};
```

### 后端实现

#### OpenGL 后端
```cpp
class OpenGLDriver : public Driver {
    Handle<VertexBufferInfo> createVertexBuffer(...) override {
        GLuint buffer;
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferData(GL_ARRAY_BUFFER, size, data, usage);

        return Handle<VertexBufferInfo>{buffer};
    }

    void draw(PrimitiveType type, size_t offset, size_t count) override {
        GLenum glType = getGLPrimitiveType(type);
        glDrawElements(glType, count, GL_UNSIGNED_SHORT,
                      reinterpret_cast<void*>(offset));
    }
};
```

#### Vulkan 后端
```cpp
class VulkanDriver : public Driver {
    Handle<VertexBufferInfo> createVertexBuffer(...) override {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        VkBuffer buffer;
        vkCreateBuffer(mDevice, &bufferInfo, nullptr, &buffer);

        // 分配内存并绑定
        // ...

        return Handle<VertexBufferInfo>{reinterpret_cast<uintptr_t>(buffer)};
    }

    void draw(PrimitiveType type, size_t offset, size_t count) override {
        vkCmdDrawIndexed(mCommandBuffer, count, 1, offset, 0, 0);
    }
};
```

### 命令缓冲区

```cpp
class CommandBuffer {
    enum class CommandType : uint8_t {
        SET_VERTEX_BUFFER,
        SET_INDEX_BUFFER,
        DRAW,
        SET_UNIFORM,
        // ...
    };

    struct Command {
        CommandType type;
        uint32_t size;
        // 命令数据跟在后面
    };

    uint8_t* mBuffer;
    size_t mSize;
    size_t mOffset;

public:
    template<typename T>
    void addCommand(CommandType type, const T& data) {
        Command* cmd = reinterpret_cast<Command*>(mBuffer + mOffset);
        cmd->type = type;
        cmd->size = sizeof(T);

        memcpy(cmd + 1, &data, sizeof(T));
        mOffset += sizeof(Command) + sizeof(T);
    }

    void execute(Driver& driver) {
        size_t offset = 0;
        while (offset < mOffset) {
            Command* cmd = reinterpret_cast<Command*>(mBuffer + offset);
            void* data = cmd + 1;

            switch (cmd->type) {
                case CommandType::DRAW:
                    driver.draw(*static_cast<DrawCommand*>(data));
                    break;
                // 处理其他命令...
            }

            offset += sizeof(Command) + cmd->size;
        }
    }
};
```

## 总结

Filament 的架构特点：

### 设计模式
1. **管理器模式**: 分离数据和行为
2. **构建器模式**: 简化复杂对象创建
3. **抽象工厂**: 多后端支持
4. **RAII**: 自动资源管理

### 性能优化
1. **ECS 架构**: 缓存友好的数据布局
2. **内存池**: 减少分配开销
3. **命令缓冲**: 批量提交渲染命令
4. **多线程**: 异步资源加载

### 扩展性
1. **插件架构**: 模块化设计
2. **抽象层**: 易于添加新后端
3. **组件系统**: 灵活的功能组合
4. **资源系统**: 统一的资源管理

这个架构为 Filament 的动画系统提供了坚实的基础，使其能够高效地处理复杂的动画场景。