# GPU资源管理

## 概述

Filament 的 GPU 资源管理系统为纹理、缓冲区、着色器和管线状态提供高效的跨平台抽象。本文档探讨了不同 GPU 架构下的资源生命周期、内存管理策略和性能优化。

## 资源句柄架构

### 统一句柄系统

```cpp
// 具有强类型的类型安全资源句柄
template<typename T>
class Handle {
    uint32_t object = 0;  // 0 表示空句柄

public:
    Handle() = default;
    explicit Handle(uint32_t id) : object(id) {}

    bool operator==(const Handle& rhs) const { return object == rhs.object; }
    operator bool() const { return object != 0; }
    uint32_t getId() const { return object; }
};

// 每种资源类型的强类型句柄
using TextureHandle         = Handle<class HwTexture>;
using BufferObjectHandle    = Handle<class HwBufferObject>;
using ProgramHandle         = Handle<class HwProgram>;
using RenderTargetHandle    = Handle<class HwRenderTarget>;
using SwapChainHandle       = Handle<class HwSwapChain>;
using SamplerGroupHandle    = Handle<class HwSamplerGroup>;
using TimerQueryHandle      = Handle<class HwTimerQuery>;
using VertexBufferHandle    = Handle<class HwVertexBuffer>;
using IndexBufferHandle     = Handle<class HwIndexBuffer>;
```

### 资源生命周期管理

```cpp
// 通用资源分配模式
template<typename ResourceType>
class ResourceManager {
    HandleAllocator<ResourceType> mAllocator;
    std::unordered_map<Handle<ResourceType>, std::unique_ptr<ResourceType>> mResources;

public:
    Handle<ResourceType> create() {
        auto handle = mAllocator.allocate();
        mResources[handle] = std::make_unique<ResourceType>();
        return handle;
    }

    void destroy(Handle<ResourceType> handle) {
        auto it = mResources.find(handle);
        if (it != mResources.end()) {
            // 后端特定的清理
            it->second->terminate();
            mResources.erase(it);
            mAllocator.deallocate(handle);
        }
    }

    ResourceType* get(Handle<ResourceType> handle) {
        auto it = mResources.find(handle);
        return it != mResources.end() ? it->second.get() : nullptr;
    }
};
```

## 纹理管理

### 纹理创建和配置

```cpp
// 综合纹理描述符
struct TextureDescriptor {
    SamplerType target;      // 2D, Cube, Array 等
    uint8_t levels;          // 多级纹理级别
    TextureFormat format;    // 像素格式
    uint8_t samples;         // MSAA 采样数
    uint32_t width, height, depth;
    TextureUsage usage;      // 使用标志
};

// 跨平台纹理创建
class Driver {
public:
    virtual TextureHandle createTextureS() noexcept = 0;

    virtual void createTexture(TextureHandle th, SamplerType target, uint8_t levels,
                              TextureFormat format, uint8_t samples,
                              uint32_t w, uint32_t h, uint32_t depth,
                              TextureUsage usage) = 0;
};
```

### 后端特定的纹理实现

#### OpenGL 纹理管理

```cpp
// OpenGL 纹理实现
struct GLTexture : public HwTexture {
    GLuint id = 0;
    GLenum target;
    GLenum format;
    GLenum type;
    uint32_t width, height, depth;
    uint8_t levels;

    void terminate() {
        if (id) {
            glDeleteTextures(1, &id);
            id = 0;
        }
    }
};

class OpenGLDriver {
    void createTexture(TextureHandle th, SamplerType target, uint8_t levels,
                      TextureFormat format, uint8_t samples,
                      uint32_t w, uint32_t h, uint32_t depth,
                      TextureUsage usage) override {

        auto* texture = handle_cast<GLTexture>(th);
        texture->target = getGLTextureTarget(target);
        texture->format = getGLInternalFormat(format);
        texture->width = w;
        texture->height = h;
        texture->depth = depth;
        texture->levels = levels;

        glBindTexture(texture->target, texture->id);

        // 根据目标类型分配存储
        switch (texture->target) {
            case GL_TEXTURE_2D:
                for (uint8_t level = 0; level < levels; ++level) {
                    uint32_t levelWidth = std::max(1u, w >> level);
                    uint32_t levelHeight = std::max(1u, h >> level);
                    glTexImage2D(GL_TEXTURE_2D, level, texture->format,
                               levelWidth, levelHeight, 0,
                               getGLFormat(format), getGLType(format), nullptr);
                }
                break;

            case GL_TEXTURE_CUBE_MAP:
                for (uint8_t face = 0; face < 6; ++face) {
                    for (uint8_t level = 0; level < levels; ++level) {
                        uint32_t levelSize = std::max(1u, w >> level);
                        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level,
                                   texture->format, levelSize, levelSize, 0,
                                   getGLFormat(format), getGLType(format), nullptr);
                    }
                }
                break;
        }
    }
};
```

#### Vulkan 纹理管理

```cpp
// 带内存管理的 Vulkan 纹理实现
struct VulkanTexture : public HwTexture {
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VulkanStagePool::Handle stagePoolHandle;

    void terminate(VkDevice device) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, imageView, nullptr);
        }
        if (image != VK_NULL_HANDLE) {
            vkDestroyImage(device, image, nullptr);
        }
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, memory, nullptr);
        }
    }
};

class VulkanDriver {
    void createTexture(TextureHandle th, SamplerType target, uint8_t levels,
                      TextureFormat format, uint8_t samples,
                      uint32_t w, uint32_t h, uint32_t depth,
                      TextureUsage usage) override {

        auto* texture = handle_cast<VulkanTexture>(th);

        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = getVkImageType(target);
        imageInfo.format = getVkFormat(format);
        imageInfo.extent = {w, h, depth};
        imageInfo.mipLevels = levels;
        imageInfo.arrayLayers = (target == SamplerType::SAMPLER_CUBEMAP) ? 6 : 1;
        imageInfo.samples = getVkSampleCount(samples);
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = getVkImageUsage(usage);
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult result = vkCreateImage(mDevice, &imageInfo, nullptr, &texture->image);
        ASSERT_POSTCONDITION(result == VK_SUCCESS, "vkCreateImage failed");

        // 为图像分配内存
        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(mDevice, texture->image, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        result = vkAllocateMemory(mDevice, &allocInfo, nullptr, &texture->memory);
        ASSERT_POSTCONDITION(result == VK_SUCCESS, "vkAllocateMemory failed");

        vkBindImageMemory(mDevice, texture->image, texture->memory, 0);

        // 创建图像视图
        createImageView(texture, format, levels);
    }
};
```

## 缓冲区管理

### 缓冲区类型和使用模式

```cpp
// 不同用例的缓冲区绑定类型
enum class BufferObjectBinding : uint8_t {
    VERTEX,           // 顶点属性数据
    UNIFORM,          // 统一/常量缓冲区
    SHADER_STORAGE,   // 着色器存储缓冲区 (SSBO)
};

// 内存分配的缓冲区使用提示
enum class BufferUsage : uint8_t {
    STATIC,           // 写入一次，读取多次
    DYNAMIC,          // 频繁更新
    STREAM            // 每帧写入一次
};
```

### 统一缓冲区接口

```cpp
// 跨平台缓冲区实现
class Driver {
public:
    virtual BufferObjectHandle createBufferObjectS() noexcept = 0;

    virtual void createBufferObject(BufferObjectHandle boh, uint32_t byteCount,
                                   BufferObjectBinding bindingType,
                                   BufferUsage usage) = 0;

    virtual void updateBufferObject(BufferObjectHandle boh,
                                   BufferDescriptor&& buffer,
                                   uint32_t byteOffset) = 0;

    virtual void updateBufferObjectUnsynchronized(BufferObjectHandle boh,
                                                  BufferDescriptor&& buffer,
                                                  uint32_t byteOffset) = 0;
};
```

### 内存管理策略

#### OpenGL 缓冲区管理

```cpp
struct GLBuffer : public HwBufferObject {
    GLuint id = 0;
    GLenum target;
    GLsizeiptr size;
    GLenum usage;

    void terminate() {
        if (id) {
            glDeleteBuffers(1, &id);
            id = 0;
        }
    }
};

class OpenGLDriver {
    void updateBufferObject(BufferObjectHandle boh, BufferDescriptor&& buffer,
                           uint32_t byteOffset) override {
        auto* glBuffer = handle_cast<GLBuffer>(boh);

        glBindBuffer(glBuffer->target, glBuffer->id);

        if (byteOffset == 0 && buffer.size == glBuffer->size) {
            // 完整缓冲区更新 - 使用孤立化获得更好性能
            glBufferData(glBuffer->target, buffer.size, buffer.buffer, glBuffer->usage);
        } else {
            // 部分更新
            glBufferSubData(glBuffer->target, byteOffset, buffer.size, buffer.buffer);
        }
    }
};
```

#### 带内存池的 Vulkan 缓冲区管理

```cpp
struct VulkanBuffer : public HwBufferObject {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size;
    void* mapped = nullptr;  // 持久映射

    void terminate(VkDevice device) {
        if (mapped) {
            vkUnmapMemory(device, memory);
        }
        if (buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, buffer, nullptr);
        }
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, memory, nullptr);
        }
    }
};

class VulkanMemoryAllocator {
    struct MemoryPool {
        VkDeviceMemory memory;
        VkDeviceSize size;
        VkDeviceSize used;
        void* mapped;
    };

    std::vector<MemoryPool> mPools;

public:
    VkDeviceMemory allocate(VkDeviceSize size, uint32_t memoryTypeIndex) {
        // 寻找合适的池或创建新池
        for (auto& pool : mPools) {
            if (pool.size - pool.used >= size) {
                pool.used += size;
                return pool.memory;
            }
        }

        // 创建新池
        MemoryPool newPool;
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = std::max(size, POOL_SIZE);
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        vkAllocateMemory(mDevice, &allocInfo, nullptr, &newPool.memory);
        mPools.push_back(newPool);

        return newPool.memory;
    }
};
```

## 管线状态管理

### 图形管线状态

```cpp
// 综合管线状态描述符
struct PipelineState {
    // 光栅化状态
    struct RasterState {
        CullingMode culling = CullingMode::BACK;
        bool frontFaceCounterClockwise = false;
        bool depthBiasEnable = false;
        float depthBias = 0.0f;
        float depthBiasSlope = 0.0f;
    } raster;

    // 深度和模板状态
    struct DepthStencilState {
        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        CompareFunction depthFunc = CompareFunction::LESS;

        bool stencilTestEnable = false;
        StencilOperation stencilOpFront;
        StencilOperation stencilOpBack;
    } depthStencil;

    // 混合状态
    struct BlendState {
        bool blendEnable = false;
        BlendEquation colorBlendOp = BlendEquation::ADD;
        BlendFunction srcColorBlendFactor = BlendFunction::ONE;
        BlendFunction dstColorBlendFactor = BlendFunction::ZERO;
    } blend;

    // 顶点输入布局
    VertexBufferHandle vertexBufferHandle;
    IndexBufferHandle indexBufferHandle;
};
```

### 管线缓存管理

```cpp
// 高效的管线缓存系统
class PipelineCache {
    struct PipelineKey {
        ProgramHandle program;
        uint64_t stateHash;  // 所有管线状态的哈希

        bool operator==(const PipelineKey& other) const {
            return program == other.program && stateHash == other.stateHash;
        }
    };

    struct PipelineKeyHash {
        size_t operator()(const PipelineKey& key) const {
            return std::hash<uint64_t>{}(key.stateHash) ^
                   std::hash<uint32_t>{}(key.program.getId());
        }
    };

    std::unordered_map<PipelineKey, PipelineHandle, PipelineKeyHash> mCache;

public:
    PipelineHandle getOrCreatePipeline(ProgramHandle program, const PipelineState& state) {
        PipelineKey key = { program, hashPipelineState(state) };

        auto it = mCache.find(key);
        if (it != mCache.end()) {
            return it->second;  // 缓存命中
        }

        // 创建新管线
        auto pipeline = mDriver.createPipeline(program, state);
        mCache[key] = pipeline;
        return pipeline;
    }

private:
    uint64_t hashPipelineState(const PipelineState& state) {
        // 所有管线状态的快速哈希
        utils::hash::MurmurHashFn hasher;
        hasher(&state.raster, sizeof(state.raster));
        hasher(&state.depthStencil, sizeof(state.depthStencil));
        hasher(&state.blend, sizeof(state.blend));
        return hasher.get();
    }
};
```

## 描述符集管理

### 描述符集布局

```cpp
// 资源绑定的描述符集布局
struct DescriptorSetLayout {
    struct Binding {
        DescriptorType type;     // 纹理、缓冲区、采样器
        uint32_t binding;        // 绑定点
        uint32_t count;          // 数组大小
        ShaderStageFlags stages; // 哪些着色器阶段使用
    };

    std::vector<Binding> bindings;
    uint32_t setIndex;  // 管线布局中的集索引
};

// 包含实际资源的描述符集
struct DescriptorSet {
    DescriptorSetLayoutHandle layout;
    std::vector<ResourceBinding> bindings;

    struct ResourceBinding {
        union {
            TextureHandle texture;
            BufferObjectHandle buffer;
            SamplerGroupHandle samplerGroup;
        };
        DescriptorType type;
        uint32_t offset = 0;  // 缓冲区偏移
        uint32_t size = 0;    // 缓冲区范围
    };
};
```

### Vulkan 描述符集实现

```cpp
class VulkanDescriptorSetCache {
    struct DescriptorPool {
        VkDescriptorPool pool;
        uint32_t remainingSets;
        std::array<uint32_t, VK_DESCRIPTOR_TYPE_RANGE_SIZE> remainingDescriptors;
    };

    std::vector<DescriptorPool> mPools;
    VkDevice mDevice;

public:
    VkDescriptorSet allocateDescriptorSet(VkDescriptorSetLayout layout) {
        // 尝试从现有池分配
        for (auto& pool : mPools) {
            if (pool.remainingSets > 0) {
                VkDescriptorSetAllocateInfo allocInfo = {};
                allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                allocInfo.descriptorPool = pool.pool;
                allocInfo.descriptorSetCount = 1;
                allocInfo.pSetLayouts = &layout;

                VkDescriptorSet descriptorSet;
                VkResult result = vkAllocateDescriptorSets(mDevice, &allocInfo, &descriptorSet);

                if (result == VK_SUCCESS) {
                    pool.remainingSets--;
                    return descriptorSet;
                }
            }
        }

        // 如果分配失败，创建新池
        createNewPool();
        return allocateDescriptorSet(layout);  // 递归调用
    }

private:
    void createNewPool() {
        std::array<VkDescriptorPoolSize, 3> poolSizes = {{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100}
        }};

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = poolSizes.size();
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1000;

        DescriptorPool newPool;
        vkCreateDescriptorPool(mDevice, &poolInfo, nullptr, &newPool.pool);
        newPool.remainingSets = 1000;

        mPools.push_back(newPool);
    }
};
```

## 内存管理优化

### 内存预算管理

```cpp
// 跟踪不同类型的内存使用
class MemoryTracker {
    struct MemoryUsage {
        size_t totalSize = 0;
        size_t usedSize = 0;
        size_t peakUsage = 0;
    };

    std::array<MemoryUsage, static_cast<size_t>(MemoryType::COUNT)> mUsage;

public:
    void allocate(MemoryType type, size_t size) {
        auto& usage = mUsage[static_cast<size_t>(type)];
        usage.usedSize += size;
        usage.peakUsage = std::max(usage.peakUsage, usage.usedSize);

        // 检查内存压力
        if (usage.usedSize > usage.totalSize * 0.9f) {
            triggerMemoryCleanup(type);
        }
    }

    void deallocate(MemoryType type, size_t size) {
        mUsage[static_cast<size_t>(type)].usedSize -= size;
    }

private:
    void triggerMemoryCleanup(MemoryType type) {
        // 实现 LRU 缓存驱逐、纹理压缩等
        switch (type) {
            case MemoryType::TEXTURE:
                evictUnusedTextures();
                break;
            case MemoryType::BUFFER:
                defragmentBuffers();
                break;
        }
    }
};
```

### 资源流式传输

```cpp
// 异步资源加载和流式传输
class ResourceStreamer {
    struct StreamingRequest {
        ResourceHandle handle;
        std::vector<uint8_t> data;
        std::atomic<bool> ready{false};
    };

    ThreadSafeQueue<StreamingRequest> mPendingRequests;
    std::thread mStreamingThread;

public:
    void streamTexture(TextureHandle handle, const std::string& path) {
        // 在后台线程排队加载
        mPendingRequests.push({handle, {}, false});

        // 后台线程加载纹理数据
        mStreamingThread = std::thread([this, handle, path]() {
            auto data = loadTextureData(path);

            // 在渲染线程排队 GPU 上传
            mCommandStream.updateTexture(handle, std::move(data));
        });
    }

    void processCompletedUploads() {
        // 在渲染线程处理完成的上传
        StreamingRequest request;
        while (mPendingRequests.tryPop(request)) {
            if (request.ready.load()) {
                // 纹理现在可用于渲染
                markTextureReady(request.handle);
            }
        }
    }
};
```

这个综合资源管理系统使 Filament 能够在不同平台上高效处理 GPU 资源，同时保持最佳的性能和内存使用模式。