# 平台集成与GPU架构

## 概述

本文档探讨 Filament 的平台集成层、基础 GPU 架构概念和特定后端优化。理解这些概念对于有效的跨平台图形编程和性能优化至关重要。

## 平台抽象层

### 平台接口设计

```cpp
// backend/include/backend/Platform.h
class Platform {
public:
    virtual ~Platform() noexcept = default;

    // SwapChain 管理
    virtual SwapChain* createSwapChain(void* nativeWindow, uint64_t flags) noexcept = 0;
    virtual SwapChain* createSwapChain(uint32_t width, uint32_t height, uint64_t flags) noexcept = 0;
    virtual void destroySwapChain(SwapChain* swapChain) noexcept = 0;

    // 平台能力
    virtual bool isExtraContextSupported() const noexcept { return false; }
    virtual void createContext(bool shared) {}
    virtual void releaseContext() noexcept {}
    virtual void makeCurrent(SwapChain* drawSwapChain, SwapChain* readSwapChain) noexcept {}

    // 调试和性能分析
    virtual bool canCreateFence() noexcept { return false; }
    virtual Fence* createFence() noexcept { return nullptr; }
    virtual void destroyFence(Fence* fence) noexcept {}
    virtual FenceStatus waitFence(Fence* fence, uint64_t timeout) noexcept {
        return FenceStatus::ERROR;
    }
};
```

### SwapChain 管理

#### 通用 SwapChain 接口

```cpp
// 跨平台交换链抽象
class SwapChain {
public:
    virtual ~SwapChain() noexcept = default;

    // 核心交换链操作
    virtual void swap(uint64_t& frameId) noexcept = 0;
    virtual void makeCurrent() noexcept = 0;
    virtual void commit() noexcept = 0;

    // 配置
    virtual bool resize(uint32_t width, uint32_t height) noexcept = 0;
    virtual void setFrameScheduledCallback(FrameScheduledCallback&& callback) noexcept {}
    virtual void setFrameCompletedCallback(FrameCompletedCallback&& callback) noexcept {}

    // 平台特定扩展
    virtual void* getNativeWindow() const noexcept { return nullptr; }
};
```

#### 平台特定的 SwapChain 实现

**OpenGL SwapChain（跨平台）**

```cpp
// 带有平台特定上下文管理的 OpenGL 交换链
class OpenGLSwapChain : public SwapChain {
    void* mNativeWindow;
    EGLDisplay mEglDisplay;
    EGLSurface mEglSurface;
    EGLContext mEglContext;

public:
    OpenGLSwapChain(void* nativeWindow, uint64_t flags)
        : mNativeWindow(nativeWindow) {

        // 平台特定初始化
#ifdef __ANDROID__
        mEglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        eglInitialize(mEglDisplay, nullptr, nullptr);
#elif defined(__APPLE__)
        // 使用原生 Cocoa/UIKit 上下文
        createCocoaContext();
#elif defined(__linux__)
        // 使用 X11 或 Wayland
        createX11Context();
#endif
    }

    void swap(uint64_t& frameId) noexcept override {
        eglSwapBuffers(mEglDisplay, mEglSurface);
        frameId++;
    }
};
```

**Metal SwapChain（Apple 平台）**

```cpp
// 带有 CAMetalLayer 集成的 Metal 交换链
class MetalSwapChain : public SwapChain {
    id<MTLDevice> mDevice;
    CAMetalLayer* mMetalLayer;
    id<CAMetalDrawable> mCurrentDrawable;

public:
    MetalSwapChain(void* nativeWindow, uint64_t flags) {
        if (flags & SWAP_CHAIN_CONFIG_APPLE_CVPIXELBUFFER) {
            // 使用 CVPixelBuffer 作为渲染目标
            setupCVPixelBuffer(static_cast<CVPixelBufferRef>(nativeWindow));
        } else {
            // 使用 CAMetalLayer
            setupMetalLayer(nativeWindow);
        }
    }

    void swap(uint64_t& frameId) noexcept override {
        if (mCurrentDrawable) {
            [mCurrentDrawable present];
            mCurrentDrawable = nil;
            frameId++;
        }
    }

private:
    void setupMetalLayer(void* nativeWindow) {
        // 从原生窗口提取 CAMetalLayer
#ifdef TARGET_OS_IOS
        UIView* view = (__bridge UIView*)nativeWindow;
        mMetalLayer = (CAMetalLayer*)view.layer;
#else
        NSView* view = (__bridge NSView*)nativeWindow;
        mMetalLayer = (CAMetalLayer*)view.layer;
#endif

        mMetalLayer.device = mDevice;
        mMetalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
        mMetalLayer.framebufferOnly = YES;
    }
};
```

**Vulkan SwapChain（跨平台）**

```cpp
// 带有平台特定表面创建的 Vulkan 交换链
class VulkanSwapChain : public SwapChain {
    VkSurfaceKHR mSurface;
    VkSwapchainKHR mSwapchain;
    std::vector<VkImage> mSwapchainImages;
    std::vector<VkImageView> mSwapchainImageViews;
    uint32_t mCurrentImageIndex;

public:
    VulkanSwapChain(VkInstance instance, VkDevice device, void* nativeWindow, uint64_t flags) {
        // 创建平台特定表面
        createSurface(instance, nativeWindow);

        // 查询表面能力
        VkSurfaceCapabilitiesKHR surfaceCaps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mPhysicalDevice, mSurface, &surfaceCaps);

        // 创建交换链
        VkSwapchainCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = mSurface;
        createInfo.minImageCount = getOptimalImageCount(surfaceCaps);
        createInfo.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
        createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        createInfo.imageExtent = surfaceCaps.currentExtent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        createInfo.preTransform = surfaceCaps.currentTransform;
        createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;

        vkCreateSwapchainKHR(device, &createInfo, nullptr, &mSwapchain);
    }

private:
    void createSurface(VkInstance instance, void* nativeWindow) {
#ifdef VK_USE_PLATFORM_ANDROID_KHR
        VkAndroidSurfaceCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        createInfo.window = static_cast<ANativeWindow*>(nativeWindow);
        vkCreateAndroidSurfaceKHR(instance, &createInfo, nullptr, &mSurface);
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
        VkWin32SurfaceCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        createInfo.hwnd = static_cast<HWND>(nativeWindow);
        createInfo.hinstance = GetModuleHandle(nullptr);
        vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &mSurface);
#elif defined(VK_USE_PLATFORM_METAL_EXT)
        VkMetalSurfaceCreateInfoEXT createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
        createInfo.pLayer = static_cast<CAMetalLayer*>(nativeWindow);
        vkCreateMetalSurfaceEXT(instance, &createInfo, nullptr, &mSurface);
#endif
    }
};
```

## GPU 架构基础

### 图形管线概览

```cpp
// 图形管线的概念表示
enum class PipelineStage {
    INPUT_ASSEMBLER,     // 顶点输入和图元装配
    VERTEX_SHADER,       // 顶点处理
    TESSELLATION,        // 细分（可选）
    GEOMETRY_SHADER,     // 几何处理（可选）
    RASTERIZATION,       // 片段生成
    FRAGMENT_SHADER,     // 片段/像素处理
    PER_FRAGMENT_OPS,    // 深度/模板测试、混合
    FRAMEBUFFER          // 最终输出
};

// GPU 管线状态表示
struct GraphicsPipelineState {
    // 顶点输入状态
    struct VertexInputState {
        std::vector<VertexAttributeDescription> attributes;
        std::vector<VertexBindingDescription> bindings;
    } vertexInput;

    // 输入装配状态
    struct InputAssemblyState {
        PrimitiveTopology topology = PrimitiveTopology::TRIANGLES;
        bool primitiveRestartEnable = false;
    } inputAssembly;

    // 视口状态
    struct ViewportState {
        std::vector<Viewport> viewports;
        std::vector<Rect2D> scissors;
    } viewport;

    // 光栅化状态
    struct RasterizationState {
        bool depthClampEnable = false;
        bool rasterizerDiscardEnable = false;
        PolygonMode polygonMode = PolygonMode::FILL;
        CullModeFlags cullMode = CullMode::BACK;
        FrontFace frontFace = FrontFace::COUNTER_CLOCKWISE;
    } rasterization;

    // 多重采样状态
    struct MultisampleState {
        SampleCountFlagBits rasterizationSamples = SampleCount::COUNT_1;
        bool sampleShadingEnable = false;
        float minSampleShading = 1.0f;
    } multisample;

    // 深度/模板状态
    struct DepthStencilState {
        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        CompareOp depthCompareOp = CompareOp::LESS;
        bool stencilTestEnable = false;
    } depthStencil;

    // 颜色混合状态
    struct ColorBlendState {
        bool logicOpEnable = false;
        LogicOp logicOp = LogicOp::COPY;
        std::vector<ColorBlendAttachmentState> attachments;
    } colorBlend;
};
```

### GPU 内存架构

```cpp
// GPU 内存层次结构和管理
enum class MemoryType {
    DEVICE_LOCAL,        // 高带宽、仅限 GPU 的内存（显存）
    HOST_VISIBLE,        // CPU 可访问，GPU 速度较慢
    HOST_COHERENT,       // CPU-GPU 一致，无需手动同步
    HOST_CACHED,         // CPU 缓存以获得更好的 CPU 性能
    DEVICE_LOCAL_HOST_VISIBLE  // 理想：快速 GPU 访问 + CPU 可见
};

// 内存使用模式
enum class MemoryUsage {
    GPU_ONLY,           // 纹理、渲染目标、静态几何
    CPU_TO_GPU,         // 动态顶点缓冲区、统一缓冲区
    GPU_TO_CPU,         // 回读缓冲区、查询结果
    CPU_COPY,           // 上传暂存缓冲区
    GPU_LAZILY_ALLOCATED // 基于瓦片的移动 GPU
};

// 内存分配策略
class GPUMemoryAllocator {
    struct MemoryBlock {
        void* memory;
        size_t size;
        size_t used;
        MemoryType type;
        std::vector<bool> freeSlots;
    };

public:
    void* allocate(size_t size, size_t alignment, MemoryUsage usage) {
        MemoryType preferredType = getPreferredMemoryType(usage);

        // 寻找合适的块或创建新块
        for (auto& block : mBlocks) {
            if (block.type == preferredType) {
                void* ptr = suballocate(block, size, alignment);
                if (ptr) return ptr;
            }
        }

        // 创建新块
        return createNewBlock(size, alignment, preferredType);
    }

private:
    MemoryType getPreferredMemoryType(MemoryUsage usage) {
        switch (usage) {
            case MemoryUsage::GPU_ONLY:
                return MemoryType::DEVICE_LOCAL;
            case MemoryUsage::CPU_TO_GPU:
                return MemoryType::HOST_VISIBLE;
            case MemoryUsage::GPU_TO_CPU:
                return MemoryType::HOST_VISIBLE | MemoryType::HOST_CACHED;
            default:
                return MemoryType::DEVICE_LOCAL;
        }
    }
};
```

### 计算管线架构

```cpp
// 用于通用 GPU 计算的计算着色器管线
class ComputePipeline {
    ProgramHandle mComputeShader;
    DescriptorSetLayout mDescriptorLayout;
    uint32_t mLocalSizeX, mLocalSizeY, mLocalSizeZ;

public:
    void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
        // 绑定计算管线
        mDriver.useProgram(mComputeShader);

        // 绑定描述符集（纹理、缓冲区）
        mDriver.bindDescriptorSet(mDescriptorSet, 0);

        // 分派计算工作
        mDriver.dispatchCompute(groupCountX, groupCountY, groupCountZ);

        // 如果需要，插入内存屏障
        mDriver.memoryBarrier(MemoryBarrierType::SHADER_STORAGE);
    }
};

// 示例：基于 GPU 的图像处理
void blurImage(TextureHandle input, TextureHandle output, float sigma) {
    ComputePipeline blurPipeline;
    blurPipeline.setShader(loadComputeShader("blur.comp"));

    // 设置描述符集
    DescriptorSet descriptorSet;
    descriptorSet.bindTexture(0, input, TextureAccess::READ_ONLY);
    descriptorSet.bindTexture(1, output, TextureAccess::WRITE_ONLY);
    descriptorSet.bindUniformBuffer(2, createUniformBuffer(&sigma, sizeof(sigma)));

    // 分派计算线程
    uint32_t width = getTextureWidth(input);
    uint32_t height = getTextureHeight(input);
    uint32_t groupsX = (width + 15) / 16;   // 16x16 本地工作组大小
    uint32_t groupsY = (height + 15) / 16;

    blurPipeline.dispatch(groupsX, groupsY, 1);
}
```

## 后端特定优化

### 移动 GPU 优化（基于瓦片的渲染）

```cpp
// 针对移动设备的基于瓦片的延迟渲染优化
class TileBasedRenderer {
public:
    // 针对基于瓦片的架构优化（Mali、Adreno、PowerVR）
    void beginTilePass(RenderTargetHandle rt) {
        // 加载操作：清除瓦片而不是从内存加载
        RenderPassDescriptor renderPass;
        renderPass.colorAttachments[0].loadOp = LoadOp::CLEAR;
        renderPass.colorAttachments[0].storeOp = StoreOp::STORE;
        renderPass.depthStencilAttachment.loadOp = LoadOp::CLEAR;
        renderPass.depthStencilAttachment.storeOp = StoreOp::DONT_CARE;

        mDriver.beginRenderPass(rt, renderPass);
    }

    void endTilePass() {
        // 在瓦片内存中解析多重采样（无带宽成本）
        mDriver.endRenderPass();
    }

    // 基于子通道的渲染以高效使用瓦片
    void renderWithSubpasses(const std::vector<RenderPass>& passes) {
        for (size_t i = 0; i < passes.size(); ++i) {
            if (i > 0) {
                mDriver.nextSubpass();  // 保持在瓦片内存中
            }

            executeRenderPass(passes[i]);
        }
    }
};

// 移动设备的帧缓冲区压缩
void enableFrameBufferCompression() {
    // 平台特定优化
#ifdef __ANDROID__
    // 在支持的设备上启用 AFBC（ARM 帧缓冲区压缩）
    if (isAFBCSupported()) {
        setTextureFormat(TextureFormat::R8G8B8A8_UNORM_AFBC);
    }
#endif

#ifdef TARGET_OS_IOS
    // 使用 Apple 的无损压缩
    if (@available(iOS 10.0, *)) {
        [mMetalTexture setCompressionType:MTLTextureCompressionTypeLossless];
    }
#endif
}
```

### 桌面 GPU 优化

```cpp
// 高性能桌面 GPU 优化
class DesktopRenderer {
public:
    // 多线程命令生成
    void parallelCommandGeneration() {
        // 并行记录命令缓冲区
        std::vector<std::future<CommandBuffer>> futures;

        for (const auto& renderQueue : mRenderQueues) {
            futures.push_back(std::async(std::launch::async, [&]() {
                return recordCommandBuffer(renderQueue);
            }));
        }

        // 按顺序提交
        for (auto& future : futures) {
            auto commandBuffer = future.get();
            mDriver.submitCommands(commandBuffer);
        }
    }

    // GPU 驱动的渲染用于高三角形数量
    void gpuDrivenRendering() {
        // 使用计算着色器进行剔除和 LOD 选择
        ComputePipeline cullingPipeline;
        cullingPipeline.setShader(loadComputeShader("frustum_culling.comp"));

        // 输入：实例数据缓冲区
        // 输出：可见实例缓冲区 + 间接绘制命令
        cullingPipeline.bindBuffer(0, mInstanceBuffer, BufferAccess::READ_ONLY);
        cullingPipeline.bindBuffer(1, mVisibleInstanceBuffer, BufferAccess::WRITE_ONLY);
        cullingPipeline.bindBuffer(2, mIndirectDrawBuffer, BufferAccess::WRITE_ONLY);

        cullingPipeline.dispatch(mInstanceCount / 64, 1, 1);

        // 使用间接绘制避免 CPU-GPU 同步
        mDriver.drawIndirect(mIndirectDrawBuffer, mMaxDrawCount);
    }
};
```

### Vulkan 特定优化

```cpp
// 高级 Vulkan 优化技术
class VulkanOptimizations {
public:
    // 动态渲染（Vulkan 1.3）
    void useDynamicRendering() {
        VkRenderingInfo renderingInfo = {};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea = {{0, 0}, {mWidth, mHeight}};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &mColorAttachment;
        renderingInfo.pDepthAttachment = &mDepthAttachment;

        // 不需要渲染通道对象 - 减少驱动程序开销
        vkCmdBeginRendering(mCommandBuffer, &renderingInfo);

        // 渲染命令...

        vkCmdEndRendering(mCommandBuffer);
    }

    // 无绑定资源的描述符索引
    void bindlessResources() {
        // 创建大型描述符数组
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 10000;  // 大数组
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // 在着色器中：通过索引访问纹理
        // layout(set = 0, binding = 0) uniform sampler2D textures[];
        // vec4 color = texture(textures[materialIndex], uv);
    }

    // GPU 内存预算管理
    void manageMemoryBudget() {
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProps = {};
        budgetProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;

        VkPhysicalDeviceMemoryProperties2 memProps = {};
        memProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        memProps.pNext = &budgetProps;

        vkGetPhysicalDeviceMemoryProperties2(mPhysicalDevice, &memProps);

        // 监控内存使用并实施逐出策略
        for (uint32_t i = 0; i < memProps.memoryProperties.memoryHeapCount; ++i) {
            float usage = float(budgetProps.heapUsage[i]) / float(budgetProps.heapBudget[i]);

            if (usage > 0.8f) {
                // 触发内存清理
                evictLeastRecentlyUsedResources(i);
            }
        }
    }
};
```

## 同步与多线程

### CPU-GPU 同步

```cpp
// 高效的 CPU-GPU 同步策略
class SynchronizationManager {
    std::vector<FenceHandle> mFrameFences;
    uint32_t mCurrentFrame = 0;
    static constexpr uint32_t FRAMES_IN_FLIGHT = 2;

public:
    void beginFrame() {
        // 等待前一帧完成后再重用资源
        uint32_t frameToWait = (mCurrentFrame + FRAMES_IN_FLIGHT - 1) % FRAMES_IN_FLIGHT;
        FenceHandle fence = mFrameFences[frameToWait];

        if (fence) {
            mDriver.waitFence(fence, FENCE_TIMEOUT_INFINITE);
            mDriver.resetFence(fence);
        }
    }

    void endFrame() {
        // 为当前帧插入围栏
        FenceHandle fence = mDriver.createFence();
        mFrameFences[mCurrentFrame] = fence;

        mCurrentFrame = (mCurrentFrame + 1) % FRAMES_IN_FLIGHT;
    }

    // 双缓冲统一缓冲区
    BufferObjectHandle getCurrentUniformBuffer() {
        return mUniformBuffers[mCurrentFrame];
    }
};
```

### 多线程渲染

```cpp
// 多线程命令缓冲区记录
class MultiThreadedRenderer {
    std::vector<std::thread> mWorkerThreads;
    ThreadSafeQueue<RenderJob> mRenderQueue;

    struct RenderJob {
        RenderQueue queue;
        CommandBuffer* commandBuffer;
        std::promise<void> completion;
    };

public:
    void renderFrame(const std::vector<RenderQueue>& queues) {
        std::vector<std::future<void>> futures;

        // 在线程间分配工作
        for (const auto& queue : queues) {
            RenderJob job;
            job.queue = queue;
            job.commandBuffer = allocateCommandBuffer();

            auto future = job.completion.get_future();
            futures.push_back(std::move(future));

            mRenderQueue.push(std::move(job));
        }

        // 等待所有线程完成
        for (auto& future : futures) {
            future.wait();
        }

        // 按顺序提交命令缓冲区
        submitAllCommandBuffers();
    }

private:
    void workerThreadFunc() {
        while (mRunning) {
            RenderJob job;
            if (mRenderQueue.tryPop(job)) {
                recordRenderCommands(job.queue, job.commandBuffer);
                job.completion.set_value();
            }
        }
    }
};
```

## 调试和性能分析工具

### GPU 调试注解

```cpp
// 跨平台调试注解
class DebugMarker {
public:
    static void pushGroup(const char* name) {
#ifdef FILAMENT_DEBUG
        switch (mBackend) {
            case Backend::OPENGL:
                if (glPushDebugGroup) {
                    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
                }
                break;

            case Backend::VULKAN:
                if (vkCmdBeginDebugUtilsLabelEXT) {
                    VkDebugUtilsLabelEXT label = {};
                    label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
                    label.pLabelName = name;
                    vkCmdBeginDebugUtilsLabelEXT(mCommandBuffer, &label);
                }
                break;

            case Backend::METAL:
                [mCommandBuffer pushDebugGroup:@(name)];
                break;
        }
#endif
    }

    static void popGroup() {
#ifdef FILAMENT_DEBUG
        switch (mBackend) {
            case Backend::OPENGL:
                if (glPopDebugGroup) glPopDebugGroup();
                break;
            case Backend::VULKAN:
                if (vkCmdEndDebugUtilsLabelEXT) {
                    vkCmdEndDebugUtilsLabelEXT(mCommandBuffer);
                }
                break;
            case Backend::METAL:
                [mCommandBuffer popDebugGroup];
                break;
        }
#endif
    }
};

// RAII 调试作用域
class DebugScope {
    bool mActive;
public:
    DebugScope(const char* name) : mActive(true) {
        DebugMarker::pushGroup(name);
    }

    ~DebugScope() {
        if (mActive) {
            DebugMarker::popGroup();
        }
    }
};

#define DEBUG_SCOPE(name) DebugScope _scope(name)
```

### 性能分析

```cpp
// GPU 计时和性能分析
class GPUProfiler {
    struct ProfileBlock {
        std::string name;
        TimerQueryHandle beginQuery;
        TimerQueryHandle endQuery;
        double lastTime = 0.0;
        double averageTime = 0.0;
        uint32_t sampleCount = 0;
    };

    std::vector<ProfileBlock> mBlocks;

public:
    void beginBlock(const std::string& name) {
        ProfileBlock block;
        block.name = name;
        block.beginQuery = mDriver.createTimerQuery();

        mDriver.beginTimerQuery(block.beginQuery);
        mBlocks.push_back(block);
    }

    void endBlock() {
        if (!mBlocks.empty()) {
            auto& block = mBlocks.back();
            block.endQuery = mDriver.createTimerQuery();
            mDriver.endTimerQuery(block.endQuery);
        }
    }

    void updateTimings() {
        for (auto& block : mBlocks) {
            if (mDriver.getTimerQueryResult(block.beginQuery) == TimerQueryResult::AVAILABLE &&
                mDriver.getTimerQueryResult(block.endQuery) == TimerQueryResult::AVAILABLE) {

                uint64_t beginTime = mDriver.getTimerQueryValue(block.beginQuery);
                uint64_t endTime = mDriver.getTimerQueryValue(block.endQuery);

                double timeMs = double(endTime - beginTime) / 1000000.0;  // ns 转 ms

                // 更新运行平均值
                block.lastTime = timeMs;
                block.averageTime = (block.averageTime * block.sampleCount + timeMs) / (block.sampleCount + 1);
                block.sampleCount++;

                // 清理查询
                mDriver.destroyTimerQuery(block.beginQuery);
                mDriver.destroyTimerQuery(block.endQuery);
            }
        }

        mBlocks.clear();
    }
};
```

这个全面的平台集成和 GPU 架构系统使 Filament 能够在各种硬件上提供最佳性能，同时为开发者提供强大的调试和优化工具。