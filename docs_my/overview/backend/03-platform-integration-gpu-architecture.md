# Platform Integration and GPU Architecture

## Overview

This document explores Filament's platform integration layer, fundamental GPU architecture concepts, and backend-specific optimizations. Understanding these concepts is crucial for effective cross-platform graphics programming and performance optimization.

## Platform Abstraction Layer

### Platform Interface Design

```cpp
// backend/include/backend/Platform.h
class Platform {
public:
    virtual ~Platform() noexcept = default;

    // SwapChain management
    virtual SwapChain* createSwapChain(void* nativeWindow, uint64_t flags) noexcept = 0;
    virtual SwapChain* createSwapChain(uint32_t width, uint32_t height, uint64_t flags) noexcept = 0;
    virtual void destroySwapChain(SwapChain* swapChain) noexcept = 0;

    // Platform capabilities
    virtual bool isExtraContextSupported() const noexcept { return false; }
    virtual void createContext(bool shared) {}
    virtual void releaseContext() noexcept {}
    virtual void makeCurrent(SwapChain* drawSwapChain, SwapChain* readSwapChain) noexcept {}

    // Debug and profiling
    virtual bool canCreateFence() noexcept { return false; }
    virtual Fence* createFence() noexcept { return nullptr; }
    virtual void destroyFence(Fence* fence) noexcept {}
    virtual FenceStatus waitFence(Fence* fence, uint64_t timeout) noexcept {
        return FenceStatus::ERROR;
    }
};
```

### SwapChain Management

#### Universal SwapChain Interface

```cpp
// Cross-platform swap chain abstraction
class SwapChain {
public:
    virtual ~SwapChain() noexcept = default;

    // Core swap chain operations
    virtual void swap(uint64_t& frameId) noexcept = 0;
    virtual void makeCurrent() noexcept = 0;
    virtual void commit() noexcept = 0;

    // Configuration
    virtual bool resize(uint32_t width, uint32_t height) noexcept = 0;
    virtual void setFrameScheduledCallback(FrameScheduledCallback&& callback) noexcept {}
    virtual void setFrameCompletedCallback(FrameCompletedCallback&& callback) noexcept {}

    // Platform-specific extensions
    virtual void* getNativeWindow() const noexcept { return nullptr; }
};
```

#### Platform-Specific SwapChain Implementations

**OpenGL SwapChain (Cross-Platform)**

```cpp
// OpenGL swap chain with platform-specific context management
class OpenGLSwapChain : public SwapChain {
    void* mNativeWindow;
    EGLDisplay mEglDisplay;
    EGLSurface mEglSurface;
    EGLContext mEglContext;

public:
    OpenGLSwapChain(void* nativeWindow, uint64_t flags)
        : mNativeWindow(nativeWindow) {

        // Platform-specific initialization
#ifdef __ANDROID__
        mEglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        eglInitialize(mEglDisplay, nullptr, nullptr);
#elif defined(__APPLE__)
        // Use native Cocoa/UIKit context
        createCocoaContext();
#elif defined(__linux__)
        // Use X11 or Wayland
        createX11Context();
#endif
    }

    void swap(uint64_t& frameId) noexcept override {
        eglSwapBuffers(mEglDisplay, mEglSurface);
        frameId++;
    }
};
```

**Metal SwapChain (Apple Platforms)**

```cpp
// Metal swap chain with CAMetalLayer integration
class MetalSwapChain : public SwapChain {
    id<MTLDevice> mDevice;
    CAMetalLayer* mMetalLayer;
    id<CAMetalDrawable> mCurrentDrawable;

public:
    MetalSwapChain(void* nativeWindow, uint64_t flags) {
        if (flags & SWAP_CHAIN_CONFIG_APPLE_CVPIXELBUFFER) {
            // Use CVPixelBuffer as render target
            setupCVPixelBuffer(static_cast<CVPixelBufferRef>(nativeWindow));
        } else {
            // Use CAMetalLayer
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
        // Extract CAMetalLayer from native window
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

**Vulkan SwapChain (Cross-Platform)**

```cpp
// Vulkan swap chain with platform-specific surface creation
class VulkanSwapChain : public SwapChain {
    VkSurfaceKHR mSurface;
    VkSwapchainKHR mSwapchain;
    std::vector<VkImage> mSwapchainImages;
    std::vector<VkImageView> mSwapchainImageViews;
    uint32_t mCurrentImageIndex;

public:
    VulkanSwapChain(VkInstance instance, VkDevice device, void* nativeWindow, uint64_t flags) {
        // Create platform-specific surface
        createSurface(instance, nativeWindow);

        // Query surface capabilities
        VkSurfaceCapabilitiesKHR surfaceCaps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mPhysicalDevice, mSurface, &surfaceCaps);

        // Create swap chain
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

## GPU Architecture Fundamentals

### Graphics Pipeline Overview

```cpp
// Conceptual representation of the graphics pipeline
enum class PipelineStage {
    INPUT_ASSEMBLER,     // Vertex input and primitive assembly
    VERTEX_SHADER,       // Vertex processing
    TESSELLATION,        // Tessellation (optional)
    GEOMETRY_SHADER,     // Geometry processing (optional)
    RASTERIZATION,       // Fragment generation
    FRAGMENT_SHADER,     // Fragment/pixel processing
    PER_FRAGMENT_OPS,    // Depth/stencil testing, blending
    FRAMEBUFFER          // Final output
};

// GPU pipeline state representation
struct GraphicsPipelineState {
    // Vertex input state
    struct VertexInputState {
        std::vector<VertexAttributeDescription> attributes;
        std::vector<VertexBindingDescription> bindings;
    } vertexInput;

    // Input assembly state
    struct InputAssemblyState {
        PrimitiveTopology topology = PrimitiveTopology::TRIANGLES;
        bool primitiveRestartEnable = false;
    } inputAssembly;

    // Viewport state
    struct ViewportState {
        std::vector<Viewport> viewports;
        std::vector<Rect2D> scissors;
    } viewport;

    // Rasterization state
    struct RasterizationState {
        bool depthClampEnable = false;
        bool rasterizerDiscardEnable = false;
        PolygonMode polygonMode = PolygonMode::FILL;
        CullModeFlags cullMode = CullMode::BACK;
        FrontFace frontFace = FrontFace::COUNTER_CLOCKWISE;
    } rasterization;

    // Multisample state
    struct MultisampleState {
        SampleCountFlagBits rasterizationSamples = SampleCount::COUNT_1;
        bool sampleShadingEnable = false;
        float minSampleShading = 1.0f;
    } multisample;

    // Depth/stencil state
    struct DepthStencilState {
        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        CompareOp depthCompareOp = CompareOp::LESS;
        bool stencilTestEnable = false;
    } depthStencil;

    // Color blend state
    struct ColorBlendState {
        bool logicOpEnable = false;
        LogicOp logicOp = LogicOp::COPY;
        std::vector<ColorBlendAttachmentState> attachments;
    } colorBlend;
};
```

### GPU Memory Architecture

```cpp
// GPU memory hierarchy and management
enum class MemoryType {
    DEVICE_LOCAL,        // High bandwidth, GPU-only memory (VRAM)
    HOST_VISIBLE,        // CPU-accessible, slower for GPU
    HOST_COHERENT,       // CPU-GPU coherent, no manual sync needed
    HOST_CACHED,         // CPU-cached for better CPU performance
    DEVICE_LOCAL_HOST_VISIBLE  // Ideal: fast GPU access + CPU visible
};

// Memory usage patterns
enum class MemoryUsage {
    GPU_ONLY,           // Textures, render targets, static geometry
    CPU_TO_GPU,         // Dynamic vertex buffers, uniform buffers
    GPU_TO_CPU,         // Readback buffers, query results
    CPU_COPY,           // Staging buffers for uploads
    GPU_LAZILY_ALLOCATED // Tile-based mobile GPUs
};

// Memory allocation strategy
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

        // Find suitable block or create new one
        for (auto& block : mBlocks) {
            if (block.type == preferredType) {
                void* ptr = suballocate(block, size, alignment);
                if (ptr) return ptr;
            }
        }

        // Create new block
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

### Compute Pipeline Architecture

```cpp
// Compute shader pipeline for general-purpose GPU computing
class ComputePipeline {
    ProgramHandle mComputeShader;
    DescriptorSetLayout mDescriptorLayout;
    uint32_t mLocalSizeX, mLocalSizeY, mLocalSizeZ;

public:
    void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
        // Bind compute pipeline
        mDriver.useProgram(mComputeShader);

        // Bind descriptor sets (textures, buffers)
        mDriver.bindDescriptorSet(mDescriptorSet, 0);

        // Dispatch compute work
        mDriver.dispatchCompute(groupCountX, groupCountY, groupCountZ);

        // Insert memory barrier if needed
        mDriver.memoryBarrier(MemoryBarrierType::SHADER_STORAGE);
    }
};

// Example: GPU-based image processing
void blurImage(TextureHandle input, TextureHandle output, float sigma) {
    ComputePipeline blurPipeline;
    blurPipeline.setShader(loadComputeShader("blur.comp"));

    // Set up descriptor set
    DescriptorSet descriptorSet;
    descriptorSet.bindTexture(0, input, TextureAccess::READ_ONLY);
    descriptorSet.bindTexture(1, output, TextureAccess::WRITE_ONLY);
    descriptorSet.bindUniformBuffer(2, createUniformBuffer(&sigma, sizeof(sigma)));

    // Dispatch compute threads
    uint32_t width = getTextureWidth(input);
    uint32_t height = getTextureHeight(input);
    uint32_t groupsX = (width + 15) / 16;   // 16x16 local work group size
    uint32_t groupsY = (height + 15) / 16;

    blurPipeline.dispatch(groupsX, groupsY, 1);
}
```

## Backend-Specific Optimizations

### Mobile GPU Optimizations (Tile-Based Rendering)

```cpp
// Tile-based deferred rendering optimizations for mobile
class TileBasedRenderer {
public:
    // Optimize for tile-based architectures (Mali, Adreno, PowerVR)
    void beginTilePass(RenderTargetHandle rt) {
        // Load action: clear tiles instead of loading from memory
        RenderPassDescriptor renderPass;
        renderPass.colorAttachments[0].loadOp = LoadOp::CLEAR;
        renderPass.colorAttachments[0].storeOp = StoreOp::STORE;
        renderPass.depthStencilAttachment.loadOp = LoadOp::CLEAR;
        renderPass.depthStencilAttachment.storeOp = StoreOp::DONT_CARE;

        mDriver.beginRenderPass(rt, renderPass);
    }

    void endTilePass() {
        // Resolve multisampling in tile memory (no bandwidth cost)
        mDriver.endRenderPass();
    }

    // Subpass-based rendering for efficient tile usage
    void renderWithSubpasses(const std::vector<RenderPass>& passes) {
        for (size_t i = 0; i < passes.size(); ++i) {
            if (i > 0) {
                mDriver.nextSubpass();  // Stay in tile memory
            }

            executeRenderPass(passes[i]);
        }
    }
};

// Frame buffer compression for mobile
void enableFrameBufferCompression() {
    // Platform-specific optimizations
#ifdef __ANDROID__
    // Enable AFBC (ARM Frame Buffer Compression) on supported devices
    if (isAFBCSupported()) {
        setTextureFormat(TextureFormat::R8G8B8A8_UNORM_AFBC);
    }
#endif

#ifdef TARGET_OS_IOS
    // Use Apple's lossless compression
    if (@available(iOS 10.0, *)) {
        [mMetalTexture setCompressionType:MTLTextureCompressionTypeLossless];
    }
#endif
}
```

### Desktop GPU Optimizations

```cpp
// High-performance desktop GPU optimizations
class DesktopRenderer {
public:
    // Multi-threaded command generation
    void parallelCommandGeneration() {
        // Record command buffers in parallel
        std::vector<std::future<CommandBuffer>> futures;

        for (const auto& renderQueue : mRenderQueues) {
            futures.push_back(std::async(std::launch::async, [&]() {
                return recordCommandBuffer(renderQueue);
            }));
        }

        // Submit in order
        for (auto& future : futures) {
            auto commandBuffer = future.get();
            mDriver.submitCommands(commandBuffer);
        }
    }

    // GPU-driven rendering for high triangle counts
    void gpuDrivenRendering() {
        // Use compute shaders for culling and LOD selection
        ComputePipeline cullingPipeline;
        cullingPipeline.setShader(loadComputeShader("frustum_culling.comp"));

        // Input: instance data buffer
        // Output: visible instance buffer + indirect draw commands
        cullingPipeline.bindBuffer(0, mInstanceBuffer, BufferAccess::READ_ONLY);
        cullingPipeline.bindBuffer(1, mVisibleInstanceBuffer, BufferAccess::WRITE_ONLY);
        cullingPipeline.bindBuffer(2, mIndirectDrawBuffer, BufferAccess::WRITE_ONLY);

        cullingPipeline.dispatch(mInstanceCount / 64, 1, 1);

        // Use indirect drawing to avoid CPU-GPU sync
        mDriver.drawIndirect(mIndirectDrawBuffer, mMaxDrawCount);
    }
};
```

### Vulkan-Specific Optimizations

```cpp
// Advanced Vulkan optimization techniques
class VulkanOptimizations {
public:
    // Dynamic rendering (Vulkan 1.3)
    void useDynamicRendering() {
        VkRenderingInfo renderingInfo = {};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea = {{0, 0}, {mWidth, mHeight}};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &mColorAttachment;
        renderingInfo.pDepthAttachment = &mDepthAttachment;

        // No render pass objects needed - reduced driver overhead
        vkCmdBeginRendering(mCommandBuffer, &renderingInfo);

        // Render commands...

        vkCmdEndRendering(mCommandBuffer);
    }

    // Descriptor indexing for bindless resources
    void bindlessResources() {
        // Create large descriptor array
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 10000;  // Large array
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        // In shader: access textures by index
        // layout(set = 0, binding = 0) uniform sampler2D textures[];
        // vec4 color = texture(textures[materialIndex], uv);
    }

    // GPU memory budget management
    void manageMemoryBudget() {
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProps = {};
        budgetProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;

        VkPhysicalDeviceMemoryProperties2 memProps = {};
        memProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        memProps.pNext = &budgetProps;

        vkGetPhysicalDeviceMemoryProperties2(mPhysicalDevice, &memProps);

        // Monitor memory usage and implement eviction strategies
        for (uint32_t i = 0; i < memProps.memoryProperties.memoryHeapCount; ++i) {
            float usage = float(budgetProps.heapUsage[i]) / float(budgetProps.heapBudget[i]);

            if (usage > 0.8f) {
                // Trigger memory cleanup
                evictLeastRecentlyUsedResources(i);
            }
        }
    }
};
```

## Synchronization and Multi-Threading

### CPU-GPU Synchronization

```cpp
// Efficient CPU-GPU synchronization strategies
class SynchronizationManager {
    std::vector<FenceHandle> mFrameFences;
    uint32_t mCurrentFrame = 0;
    static constexpr uint32_t FRAMES_IN_FLIGHT = 2;

public:
    void beginFrame() {
        // Wait for previous frame to complete before reusing resources
        uint32_t frameToWait = (mCurrentFrame + FRAMES_IN_FLIGHT - 1) % FRAMES_IN_FLIGHT;
        FenceHandle fence = mFrameFences[frameToWait];

        if (fence) {
            mDriver.waitFence(fence, FENCE_TIMEOUT_INFINITE);
            mDriver.resetFence(fence);
        }
    }

    void endFrame() {
        // Insert fence for current frame
        FenceHandle fence = mDriver.createFence();
        mFrameFences[mCurrentFrame] = fence;

        mCurrentFrame = (mCurrentFrame + 1) % FRAMES_IN_FLIGHT;
    }

    // Double-buffered uniform buffers
    BufferObjectHandle getCurrentUniformBuffer() {
        return mUniformBuffers[mCurrentFrame];
    }
};
```

### Multi-Threaded Rendering

```cpp
// Multi-threaded command buffer recording
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

        // Distribute work across threads
        for (const auto& queue : queues) {
            RenderJob job;
            job.queue = queue;
            job.commandBuffer = allocateCommandBuffer();

            auto future = job.completion.get_future();
            futures.push_back(std::move(future));

            mRenderQueue.push(std::move(job));
        }

        // Wait for all threads to complete
        for (auto& future : futures) {
            future.wait();
        }

        // Submit command buffers in order
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

## Debugging and Profiling Tools

### GPU Debug Annotations

```cpp
// Cross-platform debug annotations
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

// RAII debug scope
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

### Performance Profiling

```cpp
// GPU timing and performance analysis
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

                double timeMs = double(endTime - beginTime) / 1000000.0;  // ns to ms

                // Update running average
                block.lastTime = timeMs;
                block.averageTime = (block.averageTime * block.sampleCount + timeMs) / (block.sampleCount + 1);
                block.sampleCount++;

                // Clean up queries
                mDriver.destroyTimerQuery(block.beginQuery);
                mDriver.destroyTimerQuery(block.endQuery);
            }
        }

        mBlocks.clear();
    }
};
```

This comprehensive platform integration and GPU architecture system enables Filament to deliver optimal performance across diverse hardware while providing developers with powerful tools for debugging and optimization.