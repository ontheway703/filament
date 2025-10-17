# Backend Abstraction and Multi-API Support

## Overview

Filament achieves cross-platform graphics rendering through a sophisticated backend abstraction layer that supports OpenGL/ES, Vulkan, Metal, WebGPU, and a no-op backend for testing. This document explores how Filament's architecture enables seamless multi-API support while maintaining high performance and clean abstraction.

## Backend Selection Architecture

### Backend Enumeration

```cpp
// backend/include/backend/DriverEnums.h
enum class Backend : uint8_t {
    DEFAULT = 0,  // Automatically selects appropriate driver
    OPENGL = 1,   // OpenGL/ES driver (default on Android)
    VULKAN = 2,   // Vulkan driver (default on Linux/Windows)
    METAL = 3,    // Metal driver (default on MacOS/iOS)
    WEBGPU = 4,   // WebGPU driver for web platforms
    NOOP = 5,     // No-op driver for testing
};
```

### Platform-Specific Default Selection

```cpp
// Automatic backend selection based on platform capabilities
Backend Engine::getDefaultBackend() {
#if defined(__APPLE__)
    return Backend::METAL;      // macOS/iOS prefer Metal
#elif defined(__ANDROID__)
    return Backend::OPENGL;     // Android uses OpenGL ES
#elif defined(FILAMENT_SUPPORTS_VULKAN)
    return Backend::VULKAN;     // Desktop Linux/Windows prefer Vulkan
#else
    return Backend::OPENGL;     // Fallback to OpenGL
#endif
}
```

## Driver Interface Abstraction

### Core Driver Interface

The `Driver` class provides the fundamental abstraction layer:

```cpp
// backend/include/private/backend/Driver.h
class Driver {
public:
    virtual ~Driver() noexcept;

    // Core capabilities
    virtual ShaderModel getShaderModel() const noexcept = 0;
    virtual ShaderLanguage getShaderLanguage() const noexcept = 0;
    virtual Dispatcher getDispatcher() const noexcept = 0;

    // Command execution framework
    virtual void execute(std::function<void(void)> const& fn);
    virtual void purge() noexcept = 0;

    // Debug interface
    virtual void debugCommandBegin(CommandStream* cmds, bool synchronous,
                                  const char* methodName) noexcept = 0;
    virtual void debugCommandEnd(CommandStream* cmds, bool synchronous,
                                const char* methodName) noexcept = 0;

    // Driver API - generated from DriverAPI.inc
#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params) \
    virtual RetType methodName(paramsDecl) = 0;
#include "private/backend/DriverAPI.inc"
};
```

### Driver API Code Generation

Filament uses a code generation system to maintain API consistency:

```cpp
// DriverAPI.inc - Macro-based API definition
DECL_DRIVER_API_SYNCHRONOUS(bool, isTextureFormatSupported,
    TextureFormat format, TextureUsage usage, (format, usage))

DECL_DRIVER_API_SYNCHRONOUS(bool, isTextureSwizzleSupported, , ())

DECL_DRIVER_API_SYNCHRONOUS(bool, isWorkaroundNeeded, Workaround workaround, (workaround))

DECL_DRIVER_API(createSwapChain,
    void* nativeWindow, uint64_t flags, (nativeWindow, flags))

DECL_DRIVER_API(createSwapChainHeadless,
    uint32_t width, uint32_t height, uint64_t flags, (width, height, flags))

DECL_DRIVER_API(destroySwapChain,
    SwapChainHandle sch, (sch))

// Texture operations
DECL_DRIVER_API(createTexture,
    SamplerType target, uint8_t levels, TextureFormat format,
    uint8_t samples, uint32_t w, uint32_t h, uint32_t depth,
    TextureUsage usage, (target, levels, format, samples, w, h, depth, usage))

// Buffer operations
DECL_DRIVER_API(createBufferObject,
    uint32_t byteCount, BufferObjectBinding bindingType, BufferUsage usage,
    (byteCount, bindingType, usage))

// Shader operations
DECL_DRIVER_API(createProgram,
    Program&& program, (program))
```

## Command-Based Architecture

### CommandStream Design

Filament uses a command-based architecture for thread-safe GPU operations:

```cpp
// backend/include/private/backend/CommandStream.h
class CommandStream {
    CircularBuffer mCurrentBuffer;

    template<typename T>
    T* allocate(size_t count = 1) noexcept {
        size_t size = sizeof(T) * count;
        return static_cast<T*>(mCurrentBuffer.allocate(size, alignof(T)));
    }

public:
    // Record commands for later execution
    template<typename... ARGS>
    void queueCommand(void(*cmd)(Driver&, ARGS...), ARGS&&... args) {
        using Cmd = Command<ARGS...>;
        void* const p = allocate<Cmd>();
        new(p) Cmd(cmd, std::forward<ARGS>(args)...);
    }

    // Execute all queued commands
    void execute(Driver& driver);
};
```

### Asynchronous vs Synchronous Commands

```cpp
// Two command types for different use cases

// Asynchronous: Queued for later execution
#define DECL_DRIVER_API(methodName, paramsDecl, params) \
    void methodName(paramsDecl) {                        \
        mCommandStream.queueCommand(                     \
            [](Driver& driver, auto... args) {          \
                driver.methodName(args...);              \
            }, params);                                  \
    }

// Synchronous: Immediate execution required
#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params) \
    RetType methodName(paramsDecl) {                     \
        flush();  /* Execute pending commands first */   \
        return mDriver.methodName(params);               \
    }
```

### Command Execution Pipeline

```cpp
// Command execution with proper synchronization
void CommandStream::execute(Driver& driver) {
    while (!mBuffers.empty()) {
        auto& buffer = mBuffers.front();

        // Execute commands in order
        uint8_t* curr = buffer.begin();
        uint8_t* const end = buffer.end();

        while (curr < end) {
            CommandBase* cmd = reinterpret_cast<CommandBase*>(curr);
            cmd->execute(driver);
            curr += cmd->getSize();
        }

        mBuffers.pop_front();
    }
}
```

## Handle System for Resource Management

### Type-Safe Resource Handles

```cpp
// backend/include/backend/Handle.h
template<typename T>
class Handle {
    static constexpr uint32_t NULL_HANDLE = 0;
    uint32_t object = NULL_HANDLE;

public:
    Handle() noexcept = default;
    Handle(uint32_t id) noexcept : object(id) {}

    bool operator==(const Handle& rhs) const noexcept { return object == rhs.object; }
    bool operator!=(const Handle& rhs) const noexcept { return object != rhs.object; }

    explicit operator bool() const noexcept { return object != NULL_HANDLE; }
    uint32_t getId() const noexcept { return object; }
};

// Strongly-typed handles for different resource types
using TextureHandle         = Handle<class HwTexture>;
using BufferObjectHandle    = Handle<class HwBufferObject>;
using ProgramHandle         = Handle<class HwProgram>;
using SwapChainHandle       = Handle<class HwSwapChain>;
using RenderTargetHandle    = Handle<class HwRenderTarget>;
```

### Handle Allocation Strategy

```cpp
// backend/include/private/backend/HandleAllocator.h
template<typename T, size_t ARENA_SIZE = 256>
class HandleAllocator {
    struct HandleArena {
        std::bitset<ARENA_SIZE> used;
        std::array<T, ARENA_SIZE> storage;
    };

    std::vector<std::unique_ptr<HandleArena>> mArenas;

public:
    Handle<T> allocate() {
        // Find first available slot
        for (auto& arena : mArenas) {
            for (size_t i = 0; i < ARENA_SIZE; ++i) {
                if (!arena->used[i]) {
                    arena->used[i] = true;
                    return Handle<T>(calculateId(arena.get(), i));
                }
            }
        }

        // Allocate new arena if needed
        auto newArena = std::make_unique<HandleArena>();
        newArena->used[0] = true;
        mArenas.push_back(std::move(newArena));
        return Handle<T>(calculateId(mArenas.back().get(), 0));
    }

    void deallocate(Handle<T> handle) {
        auto [arena, index] = decodeHandle(handle);
        arena->used[index] = false;
        // Optionally destroy object at arena->storage[index]
    }
};
```

## Backend-Specific Implementations

### OpenGL Backend Architecture

```cpp
// backend/src/opengl/OpenGLDriverFactory.h
class OpenGLDriver final : public OpenGLDriverBase {
    OpenGLContext mContext;
    OpenGLBlobCache mBlobCache;

public:
    // OpenGL-specific resource creation
    TextureHandle createTextureS() noexcept override {
        GLuint texId;
        glGenTextures(1, &texId);
        return mResourceAllocator.allocate<GLTexture>(texId);
    }

    void createTexture(TextureHandle th, SamplerType target, uint8_t levels,
                      TextureFormat format, uint8_t samples,
                      uint32_t w, uint32_t h, uint32_t depth,
                      TextureUsage usage) override {

        auto* texture = handle_cast<GLTexture>(th);
        texture->target = getGLTextureTarget(target);
        texture->format = getGLFormat(format);

        glBindTexture(texture->target, texture->id);
        // Configure texture parameters...
    }
};
```

### Vulkan Backend Architecture

```cpp
// backend/src/vulkan/VulkanDriver.h
class VulkanDriver final : public Driver {
    VkInstance mInstance;
    VkDevice mDevice;
    VulkanContext mContext;
    VulkanMemory mMemoryAllocator;

public:
    TextureHandle createTextureS() noexcept override {
        return mResourceManager.allocate<VulkanTexture>();
    }

    void createTexture(TextureHandle th, SamplerType target, uint8_t levels,
                      TextureFormat format, uint8_t samples,
                      uint32_t w, uint32_t h, uint32_t depth,
                      TextureUsage usage) override {

        auto* texture = mResourceManager.handle_cast<VulkanTexture>(th);

        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = getVkImageType(target);
        imageInfo.format = getVkFormat(format);
        imageInfo.extent = {w, h, depth};
        imageInfo.mipLevels = levels;
        imageInfo.usage = getVkImageUsage(usage);

        VkResult result = vkCreateImage(mDevice, &imageInfo, nullptr, &texture->image);
        ASSERT_POSTCONDITION(result == VK_SUCCESS, "vkCreateImage failed");
    }
};
```

### Metal Backend Architecture

```cpp
// backend/src/metal/MetalDriver.h
class MetalDriver final : public Driver {
    id<MTLDevice> mDevice;
    id<MTLCommandQueue> mCommandQueue;
    MetalContext mContext;

public:
    TextureHandle createTextureS() noexcept override {
        return mResourceAllocator.allocate<MetalTexture>();
    }

    void createTexture(TextureHandle th, SamplerType target, uint8_t levels,
                      TextureFormat format, uint8_t samples,
                      uint32_t w, uint32_t h, uint32_t depth,
                      TextureUsage usage) override {

        auto* texture = handle_cast<MetalTexture>(th);

        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
        descriptor.textureType = getMetalTextureType(target);
        descriptor.pixelFormat = getMetalPixelFormat(format);
        descriptor.width = w;
        descriptor.height = h;
        descriptor.depth = depth;
        descriptor.mipmapLevelCount = levels;
        descriptor.usage = getMetalTextureUsage(usage);

        texture->texture = [mDevice newTextureWithDescriptor:descriptor];
    }
};
```

## Thread Safety and Synchronization

### Frontend-Backend Thread Separation

```cpp
// Clean separation between frontend (main thread) and backend (render thread)
class Engine {
    std::unique_ptr<Driver> mDriver;
    CommandStream mCommandStream;
    std::thread mRenderThread;

public:
    // Main thread: Queue commands
    void createTexture(/* params */) {
        mCommandStream.createTexture(/* params */);
    }

    // Render thread: Execute commands
    void renderThreadLoop() {
        while (mRunning) {
            mCommandStream.execute(*mDriver);
            std::this_thread::sleep_for(std::chrono::microseconds(16667)); // ~60fps
        }
    }
};
```

### Fence-Based Synchronization

```cpp
// GPU-CPU synchronization using fences
class Driver {
public:
    virtual FenceHandle createFenceS() noexcept = 0;
    virtual FenceStatus getFenceStatus(FenceHandle fh) noexcept = 0;

    // Wait for GPU operations to complete
    virtual void waitFence(FenceHandle fh, uint64_t timeout) = 0;
};

// Usage pattern for synchronization
void Engine::flush() {
    auto fence = mDriver.createFence();
    mDriver.waitFence(fence, FENCE_TIMEOUT_INFINITE);
    mDriver.destroyFence(fence);
}
```

## Performance Optimization Strategies

### Command Batching

```cpp
// Batch multiple commands for efficient execution
class CommandBatcher {
    std::vector<DrawCommand> mDrawCommands;
    uint32_t mBatchSize = 0;

public:
    void addDrawCommand(const DrawCommand& cmd) {
        mDrawCommands.push_back(cmd);
        mBatchSize += cmd.instanceCount;

        if (mBatchSize >= MAX_BATCH_SIZE) {
            flush();
        }
    }

    void flush() {
        if (!mDrawCommands.empty()) {
            // Execute batched draw commands
            mDriver.drawMulti(mDrawCommands.data(), mDrawCommands.size());
            mDrawCommands.clear();
            mBatchSize = 0;
        }
    }
};
```

### State Change Minimization

```cpp
// Track and minimize expensive state changes
class StateTracker {
    ProgramHandle mCurrentProgram;
    VertexBufferHandle mCurrentVertexBuffer;
    std::array<TextureHandle, MAX_SAMPLER_COUNT> mBoundTextures;

public:
    void useProgram(ProgramHandle program) {
        if (program != mCurrentProgram) {
            mDriver.useProgram(program);
            mCurrentProgram = program;
        }
    }

    void bindTexture(uint8_t unit, TextureHandle texture) {
        if (unit < MAX_SAMPLER_COUNT && mBoundTextures[unit] != texture) {
            mDriver.bindTexture(unit, texture);
            mBoundTextures[unit] = texture;
        }
    }
};
```

This backend abstraction architecture enables Filament to deliver consistent, high-performance rendering across all supported platforms while maintaining clean separation between the high-level rendering API and platform-specific GPU interfaces.