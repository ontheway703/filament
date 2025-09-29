# 后端抽象与多API支持

## 概述

Filament 通过一个复杂的后端抽象层实现跨平台图形渲染，支持 OpenGL/ES、Vulkan、Metal、WebGPU 以及用于测试的空操作后端。本文档探讨 Filament 的架构如何在保持高性能和清晰抽象的同时实现无缝的多API支持。

## 后端选择架构

### 后端枚举

```cpp
// backend/include/backend/DriverEnums.h
enum class Backend : uint8_t {
    DEFAULT = 0,  // 自动选择合适的驱动
    OPENGL = 1,   // OpenGL/ES 驱动（Android 默认）
    VULKAN = 2,   // Vulkan 驱动（Linux/Windows 默认）
    METAL = 3,    // Metal 驱动（macOS/iOS 默认）
    WEBGPU = 4,   // WebGPU 驱动（Web 平台）
    NOOP = 5,     // 空操作驱动（测试用）
};
```

### 平台特定的默认选择

```cpp
// 基于平台能力的自动后端选择
Backend Engine::getDefaultBackend() {
#if defined(__APPLE__)
    return Backend::METAL;      // macOS/iOS 优先选择 Metal
#elif defined(__ANDROID__)
    return Backend::OPENGL;     // Android 使用 OpenGL ES
#elif defined(FILAMENT_SUPPORTS_VULKAN)
    return Backend::VULKAN;     // 桌面 Linux/Windows 优先选择 Vulkan
#else
    return Backend::OPENGL;     // 回退到 OpenGL
#endif
}
```

## 驱动接口抽象

### 核心驱动接口

`Driver` 类提供基本的抽象层：

```cpp
// backend/include/private/backend/Driver.h
class Driver {
public:
    virtual ~Driver() noexcept;

    // 核心能力
    virtual ShaderModel getShaderModel() const noexcept = 0;
    virtual ShaderLanguage getShaderLanguage() const noexcept = 0;
    virtual Dispatcher getDispatcher() const noexcept = 0;

    // 命令执行框架
    virtual void execute(std::function<void(void)> const& fn);
    virtual void purge() noexcept = 0;

    // 调试接口
    virtual void debugCommandBegin(CommandStream* cmds, bool synchronous,
                                  const char* methodName) noexcept = 0;
    virtual void debugCommandEnd(CommandStream* cmds, bool synchronous,
                                const char* methodName) noexcept = 0;

    // 驱动 API - 从 DriverAPI.inc 生成
#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params) \
    virtual RetType methodName(paramsDecl) = 0;
#include "private/backend/DriverAPI.inc"
};
```

### 驱动 API 代码生成

Filament 使用代码生成系统来维护 API 一致性：

```cpp
// DriverAPI.inc - 基于宏的 API 定义
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

// 纹理操作
DECL_DRIVER_API(createTexture,
    SamplerType target, uint8_t levels, TextureFormat format,
    uint8_t samples, uint32_t w, uint32_t h, uint32_t depth,
    TextureUsage usage, (target, levels, format, samples, w, h, depth, usage))

// 缓冲区操作
DECL_DRIVER_API(createBufferObject,
    uint32_t byteCount, BufferObjectBinding bindingType, BufferUsage usage,
    (byteCount, bindingType, usage))

// 着色器操作
DECL_DRIVER_API(createProgram,
    Program&& program, (program))
```

## 基于命令的架构

### CommandStream 设计

Filament 使用基于命令的架构实现线程安全的 GPU 操作：

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
    // 记录命令以便后续执行
    template<typename... ARGS>
    void queueCommand(void(*cmd)(Driver&, ARGS...), ARGS&&... args) {
        using Cmd = Command<ARGS...>;
        void* const p = allocate<Cmd>();
        new(p) Cmd(cmd, std::forward<ARGS>(args)...);
    }

    // 执行所有排队的命令
    void execute(Driver& driver);
};
```

### 异步与同步命令

```cpp
// 两种命令类型用于不同场景

// 异步：排队等待后续执行
#define DECL_DRIVER_API(methodName, paramsDecl, params) \
    void methodName(paramsDecl) {                        \
        mCommandStream.queueCommand(                     \
            [](Driver& driver, auto... args) {          \
                driver.methodName(args...);              \
            }, params);                                  \
    }

// 同步：需要立即执行
#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params) \
    RetType methodName(paramsDecl) {                     \
        flush();  /* 先执行待处理的命令 */              \
        return mDriver.methodName(params);               \
    }
```

### 命令执行管线

```cpp
// 带适当同步的命令执行
void CommandStream::execute(Driver& driver) {
    while (!mBuffers.empty()) {
        auto& buffer = mBuffers.front();

        // 按顺序执行命令
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

## 资源管理的句柄系统

### 类型安全的资源句柄

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

// 不同资源类型的强类型句柄
using TextureHandle         = Handle<class HwTexture>;
using BufferObjectHandle    = Handle<class HwBufferObject>;
using ProgramHandle         = Handle<class HwProgram>;
using SwapChainHandle       = Handle<class HwSwapChain>;
using RenderTargetHandle    = Handle<class HwRenderTarget>;
```

### 句柄分配策略

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
        // 寻找第一个可用插槽
        for (auto& arena : mArenas) {
            for (size_t i = 0; i < ARENA_SIZE; ++i) {
                if (!arena->used[i]) {
                    arena->used[i] = true;
                    return Handle<T>(calculateId(arena.get(), i));
                }
            }
        }

        // 如果需要，分配新的存储区域
        auto newArena = std::make_unique<HandleArena>();
        newArena->used[0] = true;
        mArenas.push_back(std::move(newArena));
        return Handle<T>(calculateId(mArenas.back().get(), 0));
    }

    void deallocate(Handle<T> handle) {
        auto [arena, index] = decodeHandle(handle);
        arena->used[index] = false;
        // 可选择销毁 arena->storage[index] 处的对象
    }
};
```

## 特定后端实现

### OpenGL 后端架构

```cpp
// backend/src/opengl/OpenGLDriverFactory.h
class OpenGLDriver final : public OpenGLDriverBase {
    OpenGLContext mContext;
    OpenGLBlobCache mBlobCache;

public:
    // OpenGL 特定的资源创建
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
        // 配置纹理参数...
    }
};
```

### Vulkan 后端架构

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

### Metal 后端架构

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

## 线程安全与同步

### 前端-后端线程分离

```cpp
// 前端（主线程）和后端（渲染线程）的清晰分离
class Engine {
    std::unique_ptr<Driver> mDriver;
    CommandStream mCommandStream;
    std::thread mRenderThread;

public:
    // 主线程：排队命令
    void createTexture(/* params */) {
        mCommandStream.createTexture(/* params */);
    }

    // 渲染线程：执行命令
    void renderThreadLoop() {
        while (mRunning) {
            mCommandStream.execute(*mDriver);
            std::this_thread::sleep_for(std::chrono::microseconds(16667)); // ~60fps
        }
    }
};
```

### 基于围栏的同步

```cpp
// 使用围栏进行 GPU-CPU 同步
class Driver {
public:
    virtual FenceHandle createFenceS() noexcept = 0;
    virtual FenceStatus getFenceStatus(FenceHandle fh) noexcept = 0;

    // 等待 GPU 操作完成
    virtual void waitFence(FenceHandle fh, uint64_t timeout) = 0;
};

// 用于同步的使用模式
void Engine::flush() {
    auto fence = mDriver.createFence();
    mDriver.waitFence(fence, FENCE_TIMEOUT_INFINITE);
    mDriver.destroyFence(fence);
}
```

## 性能优化策略

### 命令批处理

```cpp
// 批处理多个命令以高效执行
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
            // 执行批处理的绘制命令
            mDriver.drawMulti(mDrawCommands.data(), mDrawCommands.size());
            mDrawCommands.clear();
            mBatchSize = 0;
        }
    }
};
```

### 状态变更最小化

```cpp
// 跟踪并最小化昂贵的状态变更
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

这个后端抽象架构使 Filament 能够在所有支持的平台上提供一致的高性能渲染，同时在高级渲染 API 和平台特定的 GPU 接口之间保持清晰的分离。