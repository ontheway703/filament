# Filament 数据流架构分析

## 📖 概述

本文档深入分析 Filament 渲染引擎的数据流架构，从应用层 API 调用到最终的 GPU 命令执行，揭示整个渲染管线中数据的流转过程。理解数据流是优化性能、调试问题和扩展功能的关键。

**关键概念**：
- **多线程架构**：主线程（应用）和渲染线程（Backend）分离
- **命令缓冲区**：CommandStream 作为线程间通信的核心
- **资源生命周期**：从创建到销毁的完整管理
- **延迟执行**：API 调用和实际 GPU 执行的异步特性

---

## 1. Filament 数据流总览

### 1.1 整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        Application Layer                        │
│  (用户代码: Scene, View, Camera, Materials, Renderables)        │
└────────────────────────────┬────────────────────────────────────┘
                             │ API Calls
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│                         Engine Layer                            │
│  FEngine, FRenderer, FView, FScene (Frontend 实现)              │
└────────────────────────────┬────────────────────────────────────┘
                             │ Commands
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│                      Command Stream                             │
│  CircularBuffer + CommandQueue (线程安全的命令缓冲区)           │
└────────────────────────────┬────────────────────────────────────┘
                             │ Thread Boundary
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│                       Backend Thread                            │
│  Driver::execute() - 处理命令队列                               │
└────────────────────────────┬────────────────────────────────────┘
                             │ Backend API
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│                    Platform Backend                             │
│  (Vulkan/Metal/OpenGL Driver 实现)                              │
└────────────────────────────┬────────────────────────────────────┘
                             │ Native API
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│                           GPU                                   │
│  (实际的硬件渲染执行)                                           │
└─────────────────────────────────────────────────────────────────┘
```

### 1.2 核心数据流路径

**典型的一帧渲染流程**：

1. **应用层**：更新场景对象（位置、材质参数等）
2. **Engine 层**：将更新转换为内部数据结构
3. **命令记录**：FRenderer::render() 生成渲染命令到 CommandStream
4. **命令提交**：flush() 提交命令到渲染线程队列
5. **命令执行**：Backend 线程从队列中取出并执行命令
6. **GPU 执行**：平台驱动将命令翻译为 GPU 指令

---

## 2. 主线程数据流

### 2.1 Engine 初始化流程

Engine 是 Filament 的入口点，负责管理所有资源和线程。

```cpp
/**
 * Engine 创建和初始化
 * 位置: filament/src/details/Engine.cpp
 */
class FEngine : public Engine {
public:
    static Engine* create(Builder const& builder) {
        // 1. 创建 Backend Driver (根据平台选择 Vulkan/Metal/OpenGL)
        backend::Driver* driver = Platform::create(&builder);

        // 2. 初始化命令流
        CommandStream* commandStream = new CommandStream(*driver,
            FILAMENT_COMMAND_BUFFER_SIZE_IN_MB * 1024 * 1024);

        // 3. 启动渲染线程
        std::thread renderThread([driver, commandStream]() {
            // 渲染线程主循环
            while (!shouldQuit) {
                // 从命令队列中取出命令并执行
                driver->execute(commandStream);
            }
        });

        // 4. 创建 Engine 实例
        FEngine* engine = new FEngine(driver, commandStream);

        // 5. 初始化默认资源
        engine->createDefaultResources();

        return engine;
    }

private:
    // 核心组件
    backend::Driver* mDriver;                    // Backend 驱动
    DriverApi mDriverApi;                        // 命令流包装器
    utils::JobSystem* mJobSystem;                // 任务调度系统

    // 组件管理器 (ECS 模式)
    FRenderableManager mRenderableManager;       // 可渲染对象管理
    FTransformManager mTransformManager;         // 变换组件管理
    FLightManager mLightManager;                 // 光源管理
    FCameraManager mCameraManager;               // 相机管理

    // 资源列表
    ResourceList<FTexture> mTextures;
    ResourceList<FMaterial> mMaterials;
    ResourceList<FVertexBuffer> mVertexBuffers;
    ResourceList<FIndexBuffer> mIndexBuffers;

    // 内存管理
    LinearAllocatorArena mPerRenderPassArena;    // 每帧临时内存
    HeapAllocatorArena mHeapArena;               // 持久内存
};
```

### 2.2 资源创建流程

以 Texture 创建为例：

```cpp
/**
 * 纹理创建流程
 * 展示了从 API 调用到 Backend 命令的完整路径
 */
Texture* Engine::createTexture(Texture::Builder const& builder) {
    // 1. 在主线程创建前端对象 (立即返回)
    FTexture* texture = mHeapArena.make<FTexture>(*this, builder);

    // 2. 生成后端句柄
    backend::Handle<backend::HwTexture> handle = mResourceAllocator.allocHandle();
    texture->mHandle = handle;

    // 3. 向命令流写入创建命令 (异步执行)
    mDriverApi.createTexture(
        handle,
        builder.mTarget,
        builder.mLevels,
        builder.mFormat,
        builder.mSamples,
        builder.mWidth,
        builder.mHeight,
        builder.mDepth,
        builder.mUsage
    );

    // 4. 添加到资源跟踪列表
    mTextures.insert(texture);

    // 注意: 此时 GPU 资源尚未创建，只是记录了创建命令
    return texture;
}

/**
 * DriverApi 包装器
 * 位置: filament/backend/include/private/backend/DriverApi.h
 */
class DriverApi {
    CommandStream& mCommandStream;

public:
    void createTexture(
        Handle<HwTexture> handle,
        TextureTarget target,
        uint8_t levels,
        TextureFormat format,
        // ... 其他参数
    ) {
        // 使用 placement new 在 CommandStream 中构造命令
        mCommandStream.queueCommand(
            &Driver::createTexture,
            handle, target, levels, format, // ...
        );
    }
};
```

### 2.3 渲染命令生成

```cpp
/**
 * 一帧的渲染命令生成
 * 位置: filament/src/Renderer.cpp
 */
void FRenderer::render(FView* view) {
    // 1. 准备渲染上下文
    RenderPass renderPass;
    renderPass.camera = view->getCamera();
    renderPass.scene = view->getScene();

    // 2. 视锥剔除 - 确定可见对象
    FScene::RenderableSoa const& renderables = scene->getRenderableData();
    Frustum const& frustum = camera->getCullingFrustum();

    Range visible = renderables.cull(
        frustum,
        renderPass.visibleRenderables,
        RENDERABLE_MANAGER_BUCKET_COUNT
    );

    // 3. 排序渲染对象 (前后排序、材质批次等)
    renderPass.sortRenderables(visible);

    // 4. 记录渲染命令
    for (auto const& renderable : renderPass.visibleRenderables) {
        // 4.1 设置渲染状态
        mDriver.bindPipeline(renderable.pipelineState);

        // 4.2 设置 Uniform 缓冲区
        mDriver.bindUniformBuffer(0, renderable.perObjectUbo);

        // 4.3 绑定材质资源
        FMaterialInstance* mi = renderable.materialInstance;
        for (auto const& sampler : mi->getSamplers()) {
            mDriver.bindSampler(sampler.binding, sampler.texture);
        }

        // 4.4 执行绘制
        mDriver.draw(
            renderable.primitiveType,
            renderable.offset,
            renderable.count,
            renderable.instanceCount
        );
    }

    // 5. 提交命令到渲染线程
    mDriver.flush();
}

/**
 * 渲染状态封装示例
 */
struct RenderableData {
    // 几何数据
    Handle<HwRenderPrimitive> primitiveHandle;
    uint32_t offset;
    uint32_t count;

    // 材质和管线
    FMaterialInstance* materialInstance;
    PipelineState pipelineState;

    // 变换数据
    math::mat4f worldTransform;
    math::mat4f worldFromModelMatrix;

    // 排序键 (用于最小化状态切换)
    uint64_t sortingKey;
};
```

---

## 3. CommandStream 详解

### 3.1 命令流架构

CommandStream 是 Filament 实现线程分离的核心，它使用循环缓冲区实现高效的命令记录和回放。

```cpp
/**
 * CommandStream 核心实现
 * 位置: filament/backend/include/private/backend/CommandStream.h
 */
class CommandStream {
    // 循环缓冲区 (无锁设计)
    CircularBuffer mCircularBuffer;

    // 当前写入位置
    void* mCurrent;
    void* mEnd;

public:
    /**
     * 将命令写入流
     * 使用模板元编程实现零开销的参数传递
     */
    template<typename MethodPtr, typename... ARGS>
    void queueCommand(MethodPtr method, ARGS&&... args) {
        using Cmd = CommandType<MethodPtr>::template Command<method>;

        // 1. 分配命令内存 (在循环缓冲区中)
        void* const p = allocateCommand(CommandBase::align(sizeof(Cmd)));

        // 2. 使用 placement new 构造命令对象
        new(p) Cmd(std::forward<ARGS>(args)...);

        // 命令参数被完美转发并存储在 Cmd 的 tuple 成员中
    }

    /**
     * 从流中执行命令
     * 在渲染线程中调用
     */
    void execute(Driver& driver) {
        CommandBase* base = static_cast<CommandBase*>(mCircularBuffer.getReadPointer());

        while (base != nullptr) {
            // 执行命令并获取下一个命令
            base = base->execute(driver);
        }

        // 释放已执行的命令占用的内存
        mCircularBuffer.circularize();
    }

private:
    void* allocateCommand(size_t size) {
        // 检查缓冲区空间
        if (UTILS_UNLIKELY(mCurrent + size > mEnd)) {
            // 空间不足，刷新到渲染线程
            requestExecute();

            // 等待空间可用
            mCurrent = mCircularBuffer.allocate(size);
        }

        void* p = mCurrent;
        mCurrent += size;
        return p;
    }
};

/**
 * 命令基类
 * 每个命令都存储了执行函数指针和参数
 */
class CommandBase {
    using Execute = void(*)(Driver&, CommandBase*, intptr_t* next);

    Execute mExecute;  // 指向具体命令的执行函数

public:
    CommandBase* execute(Driver& driver) {
        intptr_t next;
        // 调用具体命令的执行逻辑
        mExecute(driver, this, &next);

        // 返回下一个命令的地址
        return reinterpret_cast<CommandBase*>(
            reinterpret_cast<intptr_t>(this) + next
        );
    }
};

/**
 * 具体命令示例: createTexture
 */
template<>
class Command<&Driver::createTexture> : public CommandBase {
    // 使用 tuple 存储所有参数
    std::tuple<
        Handle<HwTexture>,
        TextureTarget,
        uint8_t,  // levels
        TextureFormat,
        // ... 其他参数
    > mArgs;

public:
    static void execute(Driver& driver, CommandBase* base, intptr_t* next) {
        Command* self = static_cast<Command*>(base);
        *next = align(sizeof(Command));

        // 展开 tuple 并调用 Driver 方法
        std::apply(
            [&driver](auto&&... args) {
                driver.createTexture(std::forward<decltype(args)>(args)...);
            },
            std::move(self->mArgs)
        );

        // 销毁命令对象
        self->~Command();
    }
};
```

### 3.2 循环缓冲区设计

```cpp
/**
 * CircularBuffer - 无锁循环缓冲区
 * 位置: filament/backend/src/private/backend/CircularBuffer.h
 */
class CircularBuffer {
    uint8_t* const mBuffer;           // 缓冲区起始地址
    size_t const mSize;               // 总大小

    std::atomic<size_t> mWritePos;    // 写入位置 (主线程更新)
    std::atomic<size_t> mReadPos;     // 读取位置 (渲染线程更新)

public:
    CircularBuffer(size_t size)
        : mBuffer(new uint8_t[size])
        , mSize(size)
        , mWritePos(0)
        , mReadPos(0)
    {}

    /**
     * 分配空间 (主线程调用)
     */
    void* allocate(size_t bytes) {
        size_t writePos = mWritePos.load(std::memory_order_relaxed);
        size_t readPos = mReadPos.load(std::memory_order_acquire);

        // 计算可用空间
        size_t available = (readPos > writePos)
            ? (readPos - writePos - 1)
            : (mSize - writePos + readPos - 1);

        if (available < bytes) {
            // 空间不足，需要等待渲染线程消费
            return nullptr;
        }

        void* ptr = mBuffer + writePos;

        // 更新写入位置 (循环)
        writePos = (writePos + bytes) % mSize;
        mWritePos.store(writePos, std::memory_order_release);

        return ptr;
    }

    /**
     * 获取读取指针 (渲染线程调用)
     */
    void* getReadPointer() const {
        size_t readPos = mReadPos.load(std::memory_order_relaxed);
        size_t writePos = mWritePos.load(std::memory_order_acquire);

        if (readPos == writePos) {
            return nullptr;  // 没有数据
        }

        return mBuffer + readPos;
    }

    /**
     * 释放已消费的空间 (渲染线程调用)
     */
    void circularize() {
        size_t readPos = mReadPos.load(std::memory_order_relaxed);
        size_t writePos = mWritePos.load(std::memory_order_acquire);

        // 更新读取位置到写入位置 (所有命令已执行)
        mReadPos.store(writePos, std::memory_order_release);
    }
};
```

### 3.3 命令批处理和刷新

```cpp
/**
 * 命令批处理策略
 */
class FRenderer {
    static constexpr size_t COMMAND_BUFFER_SIZE = 2 * 1024 * 1024;  // 2MB

public:
    void render(FView* view) {
        // 记录大量渲染命令...

        // 显式刷新 (提交到渲染线程)
        mDriver.flush();
    }

    void beginFrame() {
        // 帧开始标记
        mDriver.beginFrame(mFrameId++, mMonotonicClock);

        // 每帧自动刷新一次，确保及时处理
    }

    void endFrame() {
        // 帧结束，强制刷新所有待处理命令
        mDriver.flush();
        mDriver.endFrame(mFrameId);
    }
};

/**
 * Driver 刷新逻辑
 */
void DriverApi::flush() {
    // 1. 关闭当前命令缓冲区
    mCommandStream.flush();

    // 2. 唤醒渲染线程
    mCommandBufferQueue.flush();

    // 3. (可选) 等待渲染线程处理
    // 通常不等待，以保持流水线满载
}
```

---

## 4. 渲染线程数据流

### 4.1 Backend 主循环

```cpp
/**
 * 渲染线程主循环
 * 位置: filament/backend/src/Driver.cpp
 */
class BackendThread {
    Driver* mDriver;
    CommandBufferQueue* mCommandQueue;

public:
    void loop() {
        utils::ThreadUtils::setThreadName("FilamentBackend");
        utils::ThreadUtils::setThreadPriority(utils::ThreadPriority::DISPLAY);

        while (!mExitRequested) {
            // 1. 等待命令缓冲区可用
            CommandStream* commands = mCommandQueue->waitForCommands();

            if (!commands) {
                continue;  // 超时或退出信号
            }

            // 2. 执行所有命令
            mDriver->execute(commands);

            // 3. 归还缓冲区供主线程重用
            mCommandQueue->releaseBuffer(commands);
        }

        // 清理资源
        mDriver->terminate();
    }
};

/**
 * Driver 执行命令
 */
void Driver::execute(CommandStream* commands) {
    Systrace::beginSection("Driver::execute");

    CommandBase* base = commands->getReadPointer();

    while (base) {
        // 执行命令并移动到下一个
        base = base->execute(*this);
    }

    // 标记所有命令已执行
    commands->circularize();

    Systrace::endSection();
}
```

### 4.2 平台 Backend 实现

以 Vulkan 为例：

```cpp
/**
 * Vulkan Driver 实现示例
 * 位置: filament/backend/src/vulkan/VulkanDriver.cpp
 */
class VulkanDriver : public Driver {
    VkDevice mDevice;
    VkQueue mQueue;
    VkCommandPool mCommandPool;

    // 资源映射表 (Handle -> Vulkan 对象)
    HandleAllocator<HwTexture> mTextureHandles;
    HandleAllocator<HwRenderPrimitive> mPrimitiveHandles;

public:
    /**
     * 创建纹理 (Backend 线程中执行)
     */
    void createTexture(
        Handle<HwTexture> handle,
        TextureTarget target,
        uint8_t levels,
        TextureFormat format,
        uint8_t samples,
        uint32_t width,
        uint32_t height,
        uint32_t depth,
        TextureUsage usage
    ) override {
        // 1. 创建 Vulkan 图像
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = getVkImageType(target);
        imageInfo.format = getVkFormat(format);
        imageInfo.extent = {width, height, depth};
        imageInfo.mipLevels = levels;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = getVkSampleCount(samples);
        imageInfo.usage = getVkImageUsage(usage);

        VkImage image;
        vkCreateImage(mDevice, &imageInfo, nullptr, &image);

        // 2. 分配内存
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(mDevice, image, &memReqs);

        VkDeviceMemory memory = allocateMemory(memReqs);
        vkBindImageMemory(mDevice, image, memory, 0);

        // 3. 创建 ImageView
        VkImageView imageView;
        VkImageViewCreateInfo viewInfo = {};
        // ... 设置 viewInfo
        vkCreateImageView(mDevice, &viewInfo, nullptr, &imageView);

        // 4. 存储到句柄映射表
        VulkanTexture* texture = new VulkanTexture{image, memory, imageView};
        mTextureHandles.construct(handle, texture);
    }

    /**
     * 绘制调用
     */
    void draw(
        PipelineState const& state,
        Handle<HwRenderPrimitive> primitiveHandle,
        uint32_t offset,
        uint32_t count,
        uint32_t instanceCount
    ) override {
        // 1. 绑定管线状态
        VkPipeline pipeline = getPipeline(state);
        vkCmdBindPipeline(mCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        // 2. 绑定顶点/索引缓冲区
        VulkanRenderPrimitive* primitive = mPrimitiveHandles.get(primitiveHandle);
        vkCmdBindVertexBuffers(mCommandBuffer, 0, 1, &primitive->vertexBuffer, &offset);
        vkCmdBindIndexBuffer(mCommandBuffer, primitive->indexBuffer, 0, VK_INDEX_TYPE_UINT16);

        // 3. 绑定描述符集 (Uniforms + Samplers)
        vkCmdBindDescriptorSets(
            mCommandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            mPipelineLayout,
            0, 1, &mDescriptorSet,
            0, nullptr
        );

        // 4. 执行绘制
        if (primitive->indexCount > 0) {
            vkCmdDrawIndexed(mCommandBuffer, count, instanceCount, offset, 0, 0);
        } else {
            vkCmdDraw(mCommandBuffer, count, instanceCount, offset, 0);
        }
    }

    /**
     * 帧结束 - 提交到 GPU
     */
    void endFrame(uint32_t frameId) override {
        // 1. 结束命令缓冲区记录
        vkEndCommandBuffer(mCommandBuffer);

        // 2. 提交到 GPU 队列
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &mCommandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &mRenderFinished;

        vkQueueSubmit(mQueue, 1, &submitInfo, mFence);

        // 3. 呈现到屏幕
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &mRenderFinished;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &mSwapchain;

        vkQueuePresentKHR(mQueue, &presentInfo);
    }
};
```

---

## 5. 资源生命周期管理

### 5.1 资源创建和引用计数

```cpp
/**
 * 资源生命周期管理
 */
class FTexture : public Texture {
    friend class FEngine;

    FEngine& mEngine;
    backend::Handle<backend::HwTexture> mHandle;
    uint32_t mWidth, mHeight, mDepth;
    TextureFormat mFormat;

    // 引用计数 (用于延迟销毁)
    std::atomic<uint32_t> mRefCount{1};

public:
    void addRef() {
        mRefCount.fetch_add(1, std::memory_order_relaxed);
    }

    void release() {
        if (mRefCount.fetch_sub(1, std::memory_order_release) == 1) {
            // 引用计数归零，请求销毁
            mEngine.destroy(this);
        }
    }
};

/**
 * 延迟销毁机制
 * 确保 GPU 不再使用资源时才真正释放
 */
class FEngine {
    // 待销毁资源队列
    struct PendingDestruction {
        void* resource;
        uint32_t frameId;  // 请求销毁时的帧 ID
    };

    std::vector<PendingDestruction> mPendingDestructions;
    uint32_t mCurrentFrameId = 0;

public:
    void destroy(Texture* texture) {
        FTexture* ftexture = downcast(texture);

        // 1. 从资源列表移除
        mTextures.remove(ftexture);

        // 2. 添加到待销毁队列 (延迟 N 帧)
        mPendingDestructions.push_back({
            ftexture,
            mCurrentFrameId
        });
    }

    /**
     * 每帧清理安全的资源
     */
    void flushPendingDestructions() {
        constexpr uint32_t SAFETY_FRAMES = 3;  // 保证 GPU 已处理完

        auto it = mPendingDestructions.begin();
        while (it != mPendingDestructions.end()) {
            if (mCurrentFrameId - it->frameId >= SAFETY_FRAMES) {
                // 安全销毁
                FTexture* texture = static_cast<FTexture*>(it->resource);

                // 发送销毁命令到 Backend
                mDriverApi.destroyTexture(texture->mHandle);

                // 释放前端对象
                mHeapArena.destroy(texture);

                it = mPendingDestructions.erase(it);
            } else {
                ++it;
            }
        }
    }
};
```

### 5.2 数据上传流程

```cpp
/**
 * 纹理数据上传
 * 展示了异步数据传输的完整流程
 */
void Texture::setImage(
    Engine& engine,
    uint8_t level,
    PixelBufferDescriptor&& buffer
) {
    FEngine& fengine = downcast(engine);
    FTexture* ftexture = downcast(this);

    // 1. 验证数据格式和大小
    size_t expectedSize = computeTextureDataSize(
        ftexture->mFormat,
        ftexture->mWidth >> level,
        ftexture->mHeight >> level,
        ftexture->mDepth
    );
    assert(buffer.size >= expectedSize);

    // 2. 将数据发送到 Backend (异步)
    fengine.getDriverApi().update2DImage(
        ftexture->mHandle,
        level,
        0, 0,  // xoffset, yoffset
        ftexture->mWidth >> level,
        ftexture->mHeight >> level,
        std::move(buffer)  // 移动所有权到 Backend
    );

    // 注意: buffer 的内存在 Backend 线程中上传完成后自动释放
}

/**
 * Backend 处理数据上传
 */
void VulkanDriver::update2DImage(
    Handle<HwTexture> handle,
    uint8_t level,
    uint32_t xoffset,
    uint32_t yoffset,
    uint32_t width,
    uint32_t height,
    PixelBufferDescriptor&& data
) {
    VulkanTexture* texture = mTextureHandles.get(handle);

    // 1. 创建暂存缓冲区 (CPU 可见)
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    createBuffer(
        data.size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        stagingBuffer,
        stagingMemory
    );

    // 2. 拷贝数据到暂存缓冲区
    void* mapped;
    vkMapMemory(mDevice, stagingMemory, 0, data.size, 0, &mapped);
    memcpy(mapped, data.buffer, data.size);
    vkUnmapMemory(mDevice, stagingMemory);

    // 3. 记录拷贝命令 (暂存缓冲区 -> GPU 纹理)
    VkBufferImageCopy region = {};
    region.imageSubresource.mipLevel = level;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {(int32_t)xoffset, (int32_t)yoffset, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(
        mCommandBuffer,
        stagingBuffer,
        texture->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region
    );

    // 4. 延迟释放暂存缓冲区 (等待 GPU 完成拷贝)
    mPendingBufferDestructions.push_back({stagingBuffer, stagingMemory, mCurrentFrameId});

    // 5. 释放 PixelBufferDescriptor (调用用户提供的回调)
    data.callback(data.buffer, data.size, data.user);
}
```

---

## 6. 同步机制

### 6.1 Fence 同步

```cpp
/**
 * Fence - CPU/GPU 同步原语
 */
class FFence : public Fence {
    FEngine& mEngine;
    backend::Handle<backend::HwFence> mHandle;
    FenceStatus mStatus = FenceStatus::TIMEOUT_EXPIRED;

public:
    /**
     * 等待 GPU 完成
     */
    FenceStatus wait(Mode mode, uint64_t timeout) {
        if (mStatus == FenceStatus::CONDITION_SATISFIED) {
            return mStatus;
        }

        // 刷新命令到 Backend
        mEngine.flush();

        if (mode == Mode::FLUSH) {
            return FenceStatus::TIMEOUT_EXPIRED;
        }

        // 同步等待 (阻塞主线程)
        auto result = mEngine.getDriverApi().wait(mHandle, timeout);

        if (result == backend::FenceStatus::CONDITION_SATISFIED) {
            mStatus = FenceStatus::CONDITION_SATISFIED;
        }

        return mStatus;
    }
};

/**
 * 使用示例: 等待纹理上传完成
 */
void uploadTextureAndWait(Engine& engine, Texture* texture, void* data, size_t size) {
    // 1. 上传数据
    PixelBufferDescriptor buffer(data, size, PixelDataFormat::RGBA, PixelDataType::UBYTE);
    texture->setImage(engine, 0, std::move(buffer));

    // 2. 插入 Fence
    Fence* fence = engine.createFence();

    // 3. 等待上传完成
    auto status = fence->wait(Fence::Mode::FLUSH, FENCE_WAIT_FOR_EVER);

    if (status == Fence::FenceStatus::CONDITION_SATISFIED) {
        // 上传完成，纹理可以使用了
    }

    // 4. 销毁 Fence
    engine.destroy(fence);
}
```

### 6.2 帧同步策略

```cpp
/**
 * 帧流水线管理
 */
class FRenderer {
    static constexpr size_t MAX_FRAMES_IN_FLIGHT = 2;

    // 每帧的同步对象
    struct FrameSync {
        backend::Handle<backend::HwFence> fence;
        uint32_t frameId;
    };

    std::array<FrameSync, MAX_FRAMES_IN_FLIGHT> mFrameSync;
    uint32_t mCurrentFrame = 0;

public:
    void beginFrame() {
        // 1. 等待 N 帧前的帧完成 (控制帧延迟)
        uint32_t index = mCurrentFrame % MAX_FRAMES_IN_FLIGHT;
        FrameSync& sync = mFrameSync[index];

        if (sync.fence) {
            // 等待旧帧完成
            mDriver.wait(sync.fence, FENCE_WAIT_FOR_EVER);
            mDriver.destroyFence(sync.fence);
        }

        // 2. 开始新帧
        mDriver.beginFrame(mFrameId);
    }

    void endFrame() {
        // 1. 插入 Fence 标记帧结束
        uint32_t index = mCurrentFrame % MAX_FRAMES_IN_FLIGHT;
        mFrameSync[index].fence = mDriver.createFence();
        mFrameSync[index].frameId = mFrameId;

        // 2. 提交帧
        mDriver.endFrame(mFrameId);

        mCurrentFrame++;
        mFrameId++;
    }
};
```

---

## 7. 性能优化策略

### 7.1 命令批处理

```cpp
/**
 * 减少命令刷新次数
 */
class RenderPass {
    static constexpr size_t MIN_BATCH_SIZE = 100;

    std::vector<RenderableData> mRenderables;

public:
    void addRenderable(RenderableData const& data) {
        mRenderables.push_back(data);
    }

    void execute(DriverApi& driver) {
        // 1. 按材质和状态排序 (减少状态切换)
        std::sort(mRenderables.begin(), mRenderables.end(),
            [](auto const& a, auto const& b) {
                return a.sortingKey < b.sortingKey;
            });

        // 2. 批量提交绘制命令
        uint32_t batchStart = 0;
        for (size_t i = 1; i <= mRenderables.size(); ++i) {
            bool needFlush = (i == mRenderables.size()) ||
                             (mRenderables[i].materialInstance !=
                              mRenderables[i-1].materialInstance);

            if (needFlush && (i - batchStart) >= MIN_BATCH_SIZE) {
                // 批量绘制相同材质的对象
                drawBatch(driver, batchStart, i);
                batchStart = i;
            }
        }
    }
};
```

### 7.2 内存分配优化

```cpp
/**
 * 每帧临时内存分配器
 */
class LinearAllocatorArena {
    void* mBegin;
    void* mEnd;
    void* mCurrent;

public:
    /**
     * 快速线性分配 (无需 free)
     */
    template<typename T, typename... ARGS>
    T* make(ARGS&&... args) {
        void* p = allocate(sizeof(T), alignof(T));
        return new(p) T(std::forward<ARGS>(args)...);
    }

    void* allocate(size_t size, size_t alignment = 8) {
        // 对齐
        uintptr_t addr = (uintptr_t(mCurrent) + alignment - 1) & ~(alignment - 1);

        assert(addr + size <= uintptr_t(mEnd));

        mCurrent = (void*)(addr + size);
        return (void*)addr;
    }

    /**
     * 帧结束时一次性释放所有内存
     */
    void reset() {
        mCurrent = mBegin;
    }
};

/**
 * 使用示例
 */
void FRenderer::render(FView* view) {
    // 1. 在帧内存中分配临时数据
    auto& arena = mEngine.getPerRenderPassArena();

    RenderPass* pass = arena.make<RenderPass>();
    pass->prepare(view);

    // 2. 执行渲染
    pass->execute(mDriver);

    // 3. 帧结束时自动清理 (无需手动 delete)
}

void FRenderer::endFrame() {
    // 重置帧内存分配器
    mEngine.getPerRenderPassArena().reset();
}
```

---

## 8. 调试和诊断

### 8.1 命令流跟踪

```cpp
/**
 * 启用命令流调试
 * 在 CommandStream.h 中设置:
 * #define DEBUG_COMMAND_STREAM true
 */
#if DEBUG_COMMAND_STREAM
template<typename... ARGS>
void Command::log() noexcept {
    // 打印命令名称和参数
    utils::slog.d << "Command: " << typeid(*this).name() << utils::io::endl;
    log(std::index_sequence_for<ARGS...>{});
}

template<std::size_t... I>
void Command::log(std::index_sequence<I...>) noexcept {
    // 打印每个参数
    int dummy[] = {
        (utils::slog.d << "  arg" << I << ": " << std::get<I>(mArgs) << utils::io::endl, 0)...
    };
}
#endif

/**
 * 输出示例:
 * Command: createTexture
 *   arg0: Handle{0x1234}
 *   arg1: TextureTarget::TEXTURE_2D
 *   arg2: levels=8
 *   arg3: TextureFormat::RGBA8
 */
```

### 8.2 数据流可视化

```cpp
/**
 * 帧数据统计
 */
struct FrameStatistics {
    uint32_t commandCount;          // 命令数量
    size_t commandBufferSize;       // 命令缓冲区使用量

    uint32_t drawCallCount;         // Draw Call 数量
    uint32_t triangleCount;         // 三角形数量

    uint32_t textureBinds;          // 纹理绑定次数
    uint32_t stateChanges;          // 状态切换次数

    std::chrono::microseconds cpuTime;  // CPU 时间
    std::chrono::microseconds gpuTime;  // GPU 时间
};

class FRenderer {
    FrameStatistics mStats;

public:
    void render(FView* view) {
        auto start = std::chrono::high_resolution_clock::now();

        // 渲染逻辑...

        auto end = std::chrono::high_resolution_clock::now();
        mStats.cpuTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    }

    FrameStatistics const& getStatistics() const {
        return mStats;
    }
};

/**
 * 使用示例
 */
void printFrameStats(Renderer* renderer) {
    auto stats = renderer->getStatistics();

    std::cout << "Frame Statistics:\n"
              << "  Draw Calls: " << stats.drawCallCount << "\n"
              << "  Triangles:  " << stats.triangleCount << "\n"
              << "  CPU Time:   " << stats.cpuTime.count() << " µs\n"
              << "  GPU Time:   " << stats.gpuTime.count() << " µs\n";
}
```

---

## 9. 最佳实践

### 9.1 资源管理最佳实践

1. **批量创建资源**：在加载阶段集中创建资源，避免运行时频繁创建
2. **复用资源**：使用对象池管理临时资源
3. **异步加载**：使用后台线程加载资源，避免阻塞主线程
4. **预热管线**：提前创建常用的 Pipeline State 避免首次使用时卡顿

```cpp
/**
 * 资源预加载示例
 */
class ResourcePreloader {
    Engine& mEngine;
    std::vector<Texture*> mTextures;
    std::vector<Material*> mMaterials;

public:
    /**
     * 异步预加载
     */
    void preloadAsync(std::vector<std::string> const& assetPaths) {
        std::thread([this, assetPaths]() {
            for (auto const& path : assetPaths) {
                // 加载纹理数据
                ImageData image = loadImage(path);

                // 在主线程中创建 Filament 资源
                runOnMainThread([this, image]() {
                    Texture* texture = Texture::Builder()
                        .width(image.width)
                        .height(image.height)
                        .levels(1)
                        .format(Texture::InternalFormat::RGBA8)
                        .build(mEngine);

                    texture->setImage(mEngine, 0, std::move(image.data));
                    mTextures.push_back(texture);
                });
            }
        }).detach();
    }
};
```

### 9.2 性能优化检查清单

- [ ] 减少状态切换：按材质排序渲染对象
- [ ] 合并绘制调用：使用 Instancing 绘制相同几何体
- [ ] 优化命令缓冲区大小：避免频繁刷新
- [ ] 使用帧内存分配器：避免每帧 new/delete
- [ ] 延迟资源销毁：避免 GPU 使用中的资源被释放
- [ ] 控制帧延迟：使用 MAX_FRAMES_IN_FLIGHT 平衡延迟和吞吐量

---

## 10. 常见问题

### Q1: 为什么 API 调用后资源还没创建？

**A**: Filament 使用异步架构，API 调用只是记录命令到 CommandStream，实际创建在渲染线程中执行。如需确保资源可用，使用 Fence 同步。

```cpp
Texture* tex = engine->createTexture(...);
Fence* fence = engine->createFence();
fence->wait(Fence::Mode::FLUSH);  // 等待创建完成
// 现在 tex 在 GPU 上已创建
```

### Q2: 如何减少命令缓冲区刷新次数？

**A**:
1. 增加 `FILAMENT_COMMAND_BUFFER_SIZE_IN_MB`
2. 批量创建资源，一次性提交
3. 避免频繁调用 `flush()`

### Q3: 多线程环境下如何安全使用 Filament？

**A**:
- Engine 的大部分 API 是线程安全的（内部有锁）
- 同一个 Renderer 只能在一个线程中使用
- 使用 `utils::JobSystem` 并行处理数据，然后在主线程提交到 Engine

### Q4: 如何调试命令流问题？

**A**:
1. 启用 `DEBUG_COMMAND_STREAM` 查看每个命令
2. 使用 RenderDoc/Nsight 捕获帧
3. 检查 Backend 线程的日志输出

---

## 11. 相关文档

- [02-multithreading-design.md](./02-multithreading-design.md) - 多线程架构详解
- [03-pipeline-architecture.md](./03-pipeline-architecture.md) - 渲染管线设计
- [../debugging/01-debugging-workflow.md](../debugging/01-debugging-workflow.md) - 调试工作流
- [../optimization/03-drawcall-reduction.md](../optimization/03-drawcall-reduction.md) - DrawCall 优化

---

## 12. 总结

Filament 的数据流架构体现了现代渲染引擎的核心设计理念：

1. **异步执行**：主线程和渲染线程分离，充分利用 CPU 和 GPU 并行性
2. **命令缓冲**：使用 CircularBuffer 实现高效的无锁通信
3. **延迟销毁**：通过引用计数和帧延迟确保资源安全
4. **零拷贝设计**：CommandStream 使用 placement new 避免参数拷贝
5. **平台抽象**：Backend 层隔离平台差异，统一上层 API

理解数据流是掌握 Filament 的关键，它决定了：
- 如何正确使用 API
- 如何调试渲染问题
- 如何优化性能瓶颈
- 如何扩展引擎功能

通过深入理解数据流，您可以更高效地使用 Filament 构建高性能的渲染应用。
