# GPU Resource Management

## Overview

Filament's GPU resource management system provides efficient, cross-platform abstraction for textures, buffers, shaders, and pipeline state. This document explores the resource lifecycle, memory management strategies, and performance optimizations used across different GPU architectures.

## Resource Handle Architecture

### Unified Handle System

```cpp
// Type-safe resource handles with strong typing
template<typename T>
class Handle {
    uint32_t object = 0;  // 0 represents null handle

public:
    Handle() = default;
    explicit Handle(uint32_t id) : object(id) {}

    bool operator==(const Handle& rhs) const { return object == rhs.object; }
    operator bool() const { return object != 0; }
    uint32_t getId() const { return object; }
};

// Strongly-typed handles for each resource type
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

### Resource Lifecycle Management

```cpp
// Generic resource allocation pattern
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
            // Backend-specific cleanup
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

## Texture Management

### Texture Creation and Configuration

```cpp
// Comprehensive texture descriptor
struct TextureDescriptor {
    SamplerType target;      // 2D, Cube, Array, etc.
    uint8_t levels;          // Mipmap levels
    TextureFormat format;    // Pixel format
    uint8_t samples;         // MSAA samples
    uint32_t width, height, depth;
    TextureUsage usage;      // Usage flags
};

// Cross-platform texture creation
class Driver {
public:
    virtual TextureHandle createTextureS() noexcept = 0;

    virtual void createTexture(TextureHandle th, SamplerType target, uint8_t levels,
                              TextureFormat format, uint8_t samples,
                              uint32_t w, uint32_t h, uint32_t depth,
                              TextureUsage usage) = 0;
};
```

### Backend-Specific Texture Implementation

#### OpenGL Texture Management

```cpp
// OpenGL texture implementation
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

        // Allocate storage based on target type
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

#### Vulkan Texture Management

```cpp
// Vulkan texture implementation with memory management
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

        // Allocate memory for the image
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

        // Create image view
        createImageView(texture, format, levels);
    }
};
```

## Buffer Management

### Buffer Types and Usage Patterns

```cpp
// Buffer binding types for different use cases
enum class BufferObjectBinding : uint8_t {
    VERTEX,           // Vertex attribute data
    UNIFORM,          // Uniform/constant buffers
    SHADER_STORAGE,   // Shader storage buffers (SSBO)
};

// Buffer usage hints for memory allocation
enum class BufferUsage : uint8_t {
    STATIC,           // Written once, read many times
    DYNAMIC,          // Updated frequently
    STREAM            // Written once per frame
};
```

### Unified Buffer Interface

```cpp
// Cross-platform buffer implementation
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

### Memory Management Strategies

#### OpenGL Buffer Management

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
            // Full buffer update - use orphaning for better performance
            glBufferData(glBuffer->target, buffer.size, buffer.buffer, glBuffer->usage);
        } else {
            // Partial update
            glBufferSubData(glBuffer->target, byteOffset, buffer.size, buffer.buffer);
        }
    }
};
```

#### Vulkan Buffer Management with Memory Pools

```cpp
struct VulkanBuffer : public HwBufferObject {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size;
    void* mapped = nullptr;  // For persistent mapping

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
        // Find suitable pool or create new one
        for (auto& pool : mPools) {
            if (pool.size - pool.used >= size) {
                pool.used += size;
                return pool.memory;
            }
        }

        // Create new pool
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

## Pipeline State Management

### Graphics Pipeline State

```cpp
// Comprehensive pipeline state descriptor
struct PipelineState {
    // Rasterization state
    struct RasterState {
        CullingMode culling = CullingMode::BACK;
        bool frontFaceCounterClockwise = false;
        bool depthBiasEnable = false;
        float depthBias = 0.0f;
        float depthBiasSlope = 0.0f;
    } raster;

    // Depth and stencil state
    struct DepthStencilState {
        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        CompareFunction depthFunc = CompareFunction::LESS;

        bool stencilTestEnable = false;
        StencilOperation stencilOpFront;
        StencilOperation stencilOpBack;
    } depthStencil;

    // Blending state
    struct BlendState {
        bool blendEnable = false;
        BlendEquation colorBlendOp = BlendEquation::ADD;
        BlendFunction srcColorBlendFactor = BlendFunction::ONE;
        BlendFunction dstColorBlendFactor = BlendFunction::ZERO;
    } blend;

    // Vertex input layout
    VertexBufferHandle vertexBufferHandle;
    IndexBufferHandle indexBufferHandle;
};
```

### Pipeline Cache Management

```cpp
// Efficient pipeline caching system
class PipelineCache {
    struct PipelineKey {
        ProgramHandle program;
        uint64_t stateHash;  // Hash of all pipeline state

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
            return it->second;  // Cache hit
        }

        // Create new pipeline
        auto pipeline = mDriver.createPipeline(program, state);
        mCache[key] = pipeline;
        return pipeline;
    }

private:
    uint64_t hashPipelineState(const PipelineState& state) {
        // Fast hash of all pipeline state
        utils::hash::MurmurHashFn hasher;
        hasher(&state.raster, sizeof(state.raster));
        hasher(&state.depthStencil, sizeof(state.depthStencil));
        hasher(&state.blend, sizeof(state.blend));
        return hasher.get();
    }
};
```

## Descriptor Set Management

### Descriptor Set Layout

```cpp
// Descriptor set layout for resource binding
struct DescriptorSetLayout {
    struct Binding {
        DescriptorType type;     // Texture, buffer, sampler
        uint32_t binding;        // Binding point
        uint32_t count;          // Array size
        ShaderStageFlags stages; // Which shader stages use this
    };

    std::vector<Binding> bindings;
    uint32_t setIndex;  // Set index in pipeline layout
};

// Descriptor set with actual resources
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
        uint32_t offset = 0;  // Buffer offset
        uint32_t size = 0;    // Buffer range
    };
};
```

### Vulkan Descriptor Set Implementation

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
        // Try to allocate from existing pools
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

        // Create new pool if allocation failed
        createNewPool();
        return allocateDescriptorSet(layout);  // Recursive call
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

## Memory Management Optimization

### Memory Budget Management

```cpp
// Track memory usage across different types
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

        // Check for memory pressure
        if (usage.usedSize > usage.totalSize * 0.9f) {
            triggerMemoryCleanup(type);
        }
    }

    void deallocate(MemoryType type, size_t size) {
        mUsage[static_cast<size_t>(type)].usedSize -= size;
    }

private:
    void triggerMemoryCleanup(MemoryType type) {
        // Implement LRU cache eviction, texture compression, etc.
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

### Resource Streaming

```cpp
// Asynchronous resource loading and streaming
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
        // Queue loading on background thread
        mPendingRequests.push({handle, {}, false});

        // Background thread loads texture data
        mStreamingThread = std::thread([this, handle, path]() {
            auto data = loadTextureData(path);

            // Queue GPU upload on render thread
            mCommandStream.updateTexture(handle, std::move(data));
        });
    }

    void processCompletedUploads() {
        // Process completed uploads on render thread
        StreamingRequest request;
        while (mPendingRequests.tryPop(request)) {
            if (request.ready.load()) {
                // Texture is now available for rendering
                markTextureReady(request.handle);
            }
        }
    }
};
```

This comprehensive resource management system enables Filament to efficiently handle GPU resources across different platforms while maintaining optimal performance and memory usage patterns.