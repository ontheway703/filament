# 平台抽象层

本文档详细讲解 Filament Platform 接口的设计，包括 SwapChain 管理、窗口系统集成、平台特定优化，以及不同平台的实现差异。

---

## Platform 接口概述

**文件位置**: `filament/backend/include/backend/Platform.h`

Platform 接口是 Filament Backend 与操作系统窗口系统的桥梁，负责：
- **SwapChain 管理**: 创建和管理交换链
- **上下文管理**: 创建和管理图形 API 上下文
- **窗口集成**: 与平台窗口系统集成
- **平台特性**: 暴露平台特定的功能

### 设计目标

1. **平台无关**: 上层代码无需关心平台差异
2. **灵活性**: 支持各种窗口系统（X11, Wayland, Win32, Cocoa, Android）
3. **可扩展**: 易于添加新平台支持

---

## Platform 接口定义

### 核心接口

```cpp
class Platform {
public:
    virtual ~Platform() noexcept;

    // SwapChain 管理
    virtual SwapChain* createSwapChain(
        void* nativeWindow,
        uint64_t flags = 0
    ) noexcept = 0;

    virtual SwapChain* createSwapChain(
        uint32_t width,
        uint32_t height,
        uint64_t flags = 0
    ) noexcept = 0;

    virtual void destroySwapChain(SwapChain* swapChain) noexcept = 0;

    // 上下文创建
    virtual Driver* createDriver(
        void* sharedContext,
        const DriverConfig& config
    ) noexcept = 0;

    // 帧同步
    virtual void commit(SwapChain* swapChain) noexcept;

    // OpenGL 特定（如果是 OpenGL Platform）
    virtual void makeCurrent(SwapChain* swapChain) noexcept;
    virtual void swapBuffers(SwapChain* swapChain) noexcept;

protected:
    // 子类实现的辅助方法
    virtual bool isValid() const noexcept;
};
```

---

## SwapChain 概念

### SwapChain 的作用

**SwapChain（交换链）** 是渲染目标的抽象，包含多个缓冲区，用于双缓冲/三缓冲渲染：

```
┌─────────────────────────────────────────────────────────────┐
│                       SwapChain                              │
│                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │  Back Buffer │  │  Back Buffer │  │  Back Buffer │      │
│  │   (渲染中)   │  │   (准备中)   │  │   (显示中)   │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
│         ↑                 ↑                 ↓                │
│         └─────────────────┴─────────────────┘                │
│                    循环轮转                                   │
└─────────────────────────────────────────────────────────────┘

帧 N:   渲染到 Buffer 0
        ↓
        Present Buffer 0 -> 显示
        ↓
帧 N+1: 渲染到 Buffer 1 (Buffer 0 正在显示)
        ↓
        Present Buffer 1 -> 显示
        ↓
帧 N+2: 渲染到 Buffer 2 (Buffer 1 正在显示)
```

### SwapChain 创建

```cpp
// 方式1: 从原生窗口创建
void* nativeWindow = /* 平台窗口句柄 */;
SwapChain* swapChain = platform->createSwapChain(nativeWindow);

// 方式2: 离屏渲染（无窗口）
SwapChain* offscreenSwapChain = platform->createSwapChain(1920, 1080);
```

---

## 不同平台的 Platform 实现

### OpenGL Platform

#### PlatformGLX (Linux X11)

```cpp
class PlatformGLX : public OpenGLPlatform {
public:
    SwapChain* createSwapChain(
        void* nativeWindow,
        uint64_t flags
    ) noexcept override {
        // nativeWindow 是 X11 Window
        Window xwindow = reinterpret_cast<Window>(nativeWindow);

        // 创建 GLX 上下文
        GLXContext context = glXCreateContext(
            mDisplay,
            mVisualInfo,
            mSharedContext,
            GL_TRUE
        );

        // 创建 SwapChain
        SwapChainGLX* swapChain = new SwapChainGLX();
        swapChain->xwindow = xwindow;
        swapChain->context = context;

        return swapChain;
    }

    void makeCurrent(SwapChain* swapChain) noexcept override {
        SwapChainGLX* sc = static_cast<SwapChainGLX*>(swapChain);
        glXMakeCurrent(mDisplay, sc->xwindow, sc->context);
    }

    void swapBuffers(SwapChain* swapChain) noexcept override {
        SwapChainGLX* sc = static_cast<SwapChainGLX*>(swapChain);
        glXSwapBuffers(mDisplay, sc->xwindow);
    }

private:
    Display* mDisplay;
    XVisualInfo* mVisualInfo;
    GLXContext mSharedContext;

    struct SwapChainGLX : SwapChain {
        Window xwindow;
        GLXContext context;
    };
};
```

#### PlatformWGL (Windows)

```cpp
class PlatformWGL : public OpenGLPlatform {
public:
    SwapChain* createSwapChain(
        void* nativeWindow,
        uint64_t flags
    ) noexcept override {
        // nativeWindow 是 HWND
        HWND hwnd = reinterpret_cast<HWND>(nativeWindow);

        // 获取 Device Context
        HDC hdc = GetDC(hwnd);

        // 设置像素格式
        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        pfd.cStencilBits = 8;

        int pixelFormat = ChoosePixelFormat(hdc, &pfd);
        SetPixelFormat(hdc, pixelFormat, &pfd);

        // 创建 OpenGL 上下文
        HGLRC hglrc = wglCreateContext(hdc);
        if (mSharedContext) {
            wglShareLists(mSharedContext, hglrc);
        }

        // 创建 SwapChain
        SwapChainWGL* swapChain = new SwapChainWGL();
        swapChain->hwnd = hwnd;
        swapChain->hdc = hdc;
        swapChain->hglrc = hglrc;

        return swapChain;
    }

    void makeCurrent(SwapChain* swapChain) noexcept override {
        SwapChainWGL* sc = static_cast<SwapChainWGL*>(swapChain);
        wglMakeCurrent(sc->hdc, sc->hglrc);
    }

    void swapBuffers(SwapChain* swapChain) noexcept override {
        SwapChainWGL* sc = static_cast<SwapChainWGL*>(swapChain);
        SwapBuffers(sc->hdc);
    }

private:
    HGLRC mSharedContext;

    struct SwapChainWGL : SwapChain {
        HWND hwnd;
        HDC hdc;
        HGLRC hglrc;
    };
};
```

#### PlatformCocoaGL (macOS)

```objc
class PlatformCocoaGL : public OpenGLPlatform {
public:
    SwapChain* createSwapChain(
        void* nativeWindow,
        uint64_t flags
    ) noexcept override {
        // nativeWindow 是 NSView*
        NSView* view = (__bridge NSView*)nativeWindow;

        // 创建 NSOpenGLContext
        NSOpenGLPixelFormatAttribute attributes[] = {
            NSOpenGLPFADoubleBuffer,
            NSOpenGLPFADepthSize, 24,
            NSOpenGLPFAStencilSize, 8,
            NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion4_1Core,
            0
        };

        NSOpenGLPixelFormat* pixelFormat =
            [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];

        NSOpenGLContext* context =
            [[NSOpenGLContext alloc] initWithFormat:pixelFormat
                                       shareContext:mSharedContext];

        [context setView:view];

        // 创建 SwapChain
        SwapChainCocoaGL* swapChain = new SwapChainCocoaGL();
        swapChain->view = view;
        swapChain->context = context;

        return swapChain;
    }

    void makeCurrent(SwapChain* swapChain) noexcept override {
        SwapChainCocoaGL* sc = static_cast<SwapChainCocoaGL*>(swapChain);
        [sc->context makeCurrentContext];
    }

    void swapBuffers(SwapChain* swapChain) noexcept override {
        SwapChainCocoaGL* sc = static_cast<SwapChainCocoaGL*>(swapChain);
        [sc->context flushBuffer];
    }

private:
    NSOpenGLContext* mSharedContext;

    struct SwapChainCocoaGL : SwapChain {
        NSView* view;
        NSOpenGLContext* context;
    };
};
```

#### PlatformEGL (Android/Linux)

```cpp
class PlatformEGL : public OpenGLPlatform {
public:
    SwapChain* createSwapChain(
        void* nativeWindow,
        uint64_t flags
    ) noexcept override {
        // nativeWindow 是 ANativeWindow* (Android) 或 EGLNativeWindowType (Linux)
        EGLNativeWindowType window = reinterpret_cast<EGLNativeWindowType>(nativeWindow);

        // 创建 EGL Surface
        EGLSurface surface = eglCreateWindowSurface(
            mDisplay,
            mConfig,
            window,
            nullptr
        );

        // 创建 EGL Context (共享)
        if (!mContext) {
            EGLint contextAttribs[] = {
                EGL_CONTEXT_CLIENT_VERSION, 3,
                EGL_NONE
            };

            mContext = eglCreateContext(
                mDisplay,
                mConfig,
                EGL_NO_CONTEXT,
                contextAttribs
            );
        }

        // 创建 SwapChain
        SwapChainEGL* swapChain = new SwapChainEGL();
        swapChain->surface = surface;

        return swapChain;
    }

    void makeCurrent(SwapChain* swapChain) noexcept override {
        SwapChainEGL* sc = static_cast<SwapChainEGL*>(swapChain);
        eglMakeCurrent(mDisplay, sc->surface, sc->surface, mContext);
    }

    void swapBuffers(SwapChain* swapChain) noexcept override {
        SwapChainEGL* sc = static_cast<SwapChainEGL*>(swapChain);
        eglSwapBuffers(mDisplay, sc->surface);
    }

private:
    EGLDisplay mDisplay;
    EGLConfig mConfig;
    EGLContext mContext;

    struct SwapChainEGL : SwapChain {
        EGLSurface surface;
    };
};
```

---

### Vulkan Platform

```cpp
class PlatformVulkan : public Platform {
public:
    SwapChain* createSwapChain(
        void* nativeWindow,
        uint64_t flags
    ) noexcept override {
        // 创建 Vulkan Surface
        VkSurfaceKHR surface = createVulkanSurface(nativeWindow);

        // 查询 Surface 能力
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            mPhysicalDevice,
            surface,
            &capabilities
        );

        // 选择交换链格式
        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat();

        // 选择呈现模式
        VkPresentModeKHR presentMode = chooseSwapPresentMode();

        // 确定交换链图像数量
        uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 &&
            imageCount > capabilities.maxImageCount) {
            imageCount = capabilities.maxImageCount;
        }

        // 创建 Vulkan SwapChain
        VkSwapchainCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = capabilities.currentExtent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        VkSwapchainKHR swapchain;
        vkCreateSwapchainKHR(mDevice, &createInfo, nullptr, &swapchain);

        // 获取交换链图像
        vkGetSwapchainImagesKHR(mDevice, swapchain, &imageCount, nullptr);
        std::vector<VkImage> swapchainImages(imageCount);
        vkGetSwapchainImagesKHR(mDevice, swapchain, &imageCount,
                               swapchainImages.data());

        // 创建 SwapChain
        SwapChainVulkan* sc = new SwapChainVulkan();
        sc->surface = surface;
        sc->swapchain = swapchain;
        sc->images = std::move(swapchainImages);
        sc->format = surfaceFormat.format;

        return sc;
    }

    void commit(SwapChain* swapChain) noexcept override {
        SwapChainVulkan* sc = static_cast<SwapChainVulkan*>(swapChain);

        // 获取下一个图像索引
        uint32_t imageIndex;
        vkAcquireNextImageKHR(
            mDevice,
            sc->swapchain,
            UINT64_MAX,
            sc->imageAvailableSemaphore,
            VK_NULL_HANDLE,
            &imageIndex
        );

        // 提交命令缓冲（在 VulkanDriver 中完成）
        // ...

        // 呈现图像
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &sc->renderFinishedSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &sc->swapchain;
        presentInfo.pImageIndices = &imageIndex;

        vkQueuePresentKHR(mPresentQueue, &presentInfo);
    }

private:
    VkInstance mInstance;
    VkPhysicalDevice mPhysicalDevice;
    VkDevice mDevice;
    VkQueue mPresentQueue;

    struct SwapChainVulkan : SwapChain {
        VkSurfaceKHR surface;
        VkSwapchainKHR swapchain;
        std::vector<VkImage> images;
        VkFormat format;
        VkSemaphore imageAvailableSemaphore;
        VkSemaphore renderFinishedSemaphore;
    };

    VkSurfaceKHR createVulkanSurface(void* nativeWindow) {
        VkSurfaceKHR surface;

        #if defined(__ANDROID__)
            // Android
            VkAndroidSurfaceCreateInfoKHR createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
            createInfo.window = reinterpret_cast<ANativeWindow*>(nativeWindow);
            vkCreateAndroidSurfaceKHR(mInstance, &createInfo, nullptr, &surface);

        #elif defined(_WIN32)
            // Windows
            VkWin32SurfaceCreateInfoKHR createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
            createInfo.hwnd = reinterpret_cast<HWND>(nativeWindow);
            createInfo.hinstance = GetModuleHandle(nullptr);
            vkCreateWin32SurfaceKHR(mInstance, &createInfo, nullptr, &surface);

        #elif defined(__APPLE__)
            // macOS/iOS (使用 MoltenVK)
            VkMetalSurfaceCreateInfoEXT createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
            createInfo.pLayer = /* CAMetalLayer */;
            vkCreateMetalSurfaceEXT(mInstance, &createInfo, nullptr, &surface);

        #elif defined(__linux__)
            // Linux (X11)
            VkXlibSurfaceCreateInfoKHR createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
            createInfo.dpy = /* X11 Display */;
            createInfo.window = reinterpret_cast<Window>(nativeWindow);
            vkCreateXlibSurfaceKHR(mInstance, &createInfo, nullptr, &surface);
        #endif

        return surface;
    }
};
```

---

### Metal Platform

```objc
class PlatformMetal : public Platform {
public:
    SwapChain* createSwapChain(
        void* nativeWindow,
        uint64_t flags
    ) noexcept override {
        // nativeWindow 是 CAMetalLayer* 或 UIView*/NSView*
        CAMetalLayer* metalLayer = nil;

        #if TARGET_OS_IOS
            UIView* view = (__bridge UIView*)nativeWindow;
            metalLayer = (CAMetalLayer*)view.layer;
            metalLayer.device = mDevice;
        #else
            NSView* view = (__bridge NSView*)nativeWindow;
            view.wantsLayer = YES;
            metalLayer = [CAMetalLayer layer];
            metalLayer.device = mDevice;
            view.layer = metalLayer;
        #endif

        // 配置 Metal Layer
        metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        metalLayer.framebufferOnly = YES;

        // 创建 SwapChain
        SwapChainMetal* swapChain = new SwapChainMetal();
        swapChain->metalLayer = metalLayer;

        return swapChain;
    }

    void commit(SwapChain* swapChain) noexcept override {
        SwapChainMetal* sc = static_cast<SwapChainMetal*>(swapChain);

        // 获取下一个 Drawable
        id<CAMetalDrawable> drawable = [sc->metalLayer nextDrawable];

        // 渲染到 drawable.texture
        // (在 MetalDriver 中完成)

        // 呈现
        [sc->currentCommandBuffer presentDrawable:drawable];
        [sc->currentCommandBuffer commit];
    }

private:
    id<MTLDevice> mDevice;

    struct SwapChainMetal : SwapChain {
        CAMetalLayer* metalLayer;
        id<MTLCommandBuffer> currentCommandBuffer;
    };
};
```

---

## 平台特定优化

### VSync 控制

```cpp
// OpenGL (GLX)
void PlatformGLX::setSwapInterval(int interval) {
    if (glXSwapIntervalEXT) {
        glXSwapIntervalEXT(mDisplay, glXGetCurrentDrawable(), interval);
    }
    // interval = 0: 无 VSync (无限帧率)
    // interval = 1: VSync (60 FPS)
    // interval = 2: 半速 VSync (30 FPS)
}

// Vulkan
VkPresentModeKHR chooseSwapPresentMode() {
    // VK_PRESENT_MODE_IMMEDIATE_KHR: 无 VSync
    // VK_PRESENT_MODE_FIFO_KHR: VSync (保证支持)
    // VK_PRESENT_MODE_MAILBOX_KHR: 三缓冲，低延迟
    return VK_PRESENT_MODE_FIFO_KHR;
}

// Metal
metalLayer.displaySyncEnabled = YES;  // 启用 VSync
```

### HDR 支持

```cpp
// Vulkan
VkSurfaceFormatKHR chooseSwapSurfaceFormat() {
    for (const auto& format : availableFormats) {
        if (format.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 &&
            format.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) {
            // HDR10 支持
            return format;
        }
    }

    // 回退到 SDR
    return {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
}

// Metal
#if TARGET_OS_IOS
    if (@available(iOS 14.0, *)) {
        metalLayer.wantsExtendedDynamicRangeContent = YES;
        metalLayer.pixelFormat = MTLPixelFormatRGBA16Float;
    }
#else
    if (@available(macOS 10.15, *)) {
        metalLayer.wantsExtendedDynamicRangeContent = YES;
        metalLayer.pixelFormat = MTLPixelFormatRGBA16Float;
    }
#endif
```

### 多显示器支持

```cpp
class MultiMonitorPlatform : public Platform {
public:
    SwapChain* createSwapChain(
        void* nativeWindow,
        uint64_t flags
    ) noexcept override {
        // 检测窗口所在的显示器
        int monitorIndex = getMonitorIndex(nativeWindow);

        // 为特定显示器创建 SwapChain
        SwapChain* swapChain = createSwapChainForMonitor(
            nativeWindow,
            monitorIndex
        );

        return swapChain;
    }

private:
    int getMonitorIndex(void* nativeWindow);
    SwapChain* createSwapChainForMonitor(void* window, int monitor);
};
```

---

## 平台能力查询

```cpp
struct PlatformCapabilities {
    bool supportsHDR;
    bool supportsVariableRefreshRate;  // FreeSync/G-Sync
    bool supportsExclusiveFullscreen;
    uint32_t maxTextureSize;
    uint32_t maxRenderTargetSize;
    // ...
};

PlatformCapabilities Platform::queryCapabilities() {
    PlatformCapabilities caps = {};

    #if defined(__ANDROID__)
        // Android 特定能力
        caps.maxTextureSize = 4096;  // 大多数移动 GPU

    #elif defined(_WIN32)
        // Windows 特定能力
        caps.supportsExclusiveFullscreen = true;
        caps.maxTextureSize = 16384;

    #elif defined(__APPLE__)
        // Apple 特定能力
        caps.supportsHDR = /* 检测 EDR 支持 */;
        caps.maxTextureSize = 16384;
    #endif

    return caps;
}
```

---

## 错误处理

```cpp
class Platform {
protected:
    enum class Error {
        NONE,
        WINDOW_SURFACE_CREATION_FAILED,
        CONTEXT_CREATION_FAILED,
        SWAPCHAIN_CREATION_FAILED,
        // ...
    };

    virtual void handleError(Error error, const char* message) {
        switch (error) {
            case Error::WINDOW_SURFACE_CREATION_FAILED:
                LOG(ERROR) << "Failed to create window surface: " << message;
                break;

            case Error::CONTEXT_CREATION_FAILED:
                LOG(ERROR) << "Failed to create context: " << message;
                break;

            case Error::SWAPCHAIN_CREATION_FAILED:
                LOG(ERROR) << "Failed to create swapchain: " << message;
                break;
        }
    }
};
```

---

## 使用示例

### 完整的渲染循环

```cpp
// 1. 创建 Platform
Platform* platform = Platform::create(Backend::VULKAN);

// 2. 创建 SwapChain
void* nativeWindow = /* 平台窗口句柄 */;
SwapChain* swapChain = platform->createSwapChain(nativeWindow);

// 3. 创建 Driver
Driver* driver = platform->createDriver(nullptr, config);

// 4. 渲染循环
while (running) {
    // 开始帧
    driver->beginFrame(timestamp, frameId);

    // 渲染命令
    driver->setRenderTarget(swapChainRenderTarget);
    driver->draw(pipelineState, primitive);

    // 结束帧
    driver->endFrame(frameId);

    // 提交到 SwapChain
    platform->commit(swapChain);

    frameId++;
}

// 5. 清理
platform->destroySwapChain(swapChain);
delete platform;
```

---

## 相关文档

- **[01-architecture-overview.md](01-architecture-overview.md)**: Backend 架构总览
- **[05-opengl-backend.md](05-opengl-backend.md)**: OpenGL Platform 实现
- **[06-vulkan-backend.md](06-vulkan-backend.md)**: Vulkan Platform 实现
- **[07-metal-backend.md](07-metal-backend.md)**: Metal Platform 实现

**上层使用**:
- `../engine/07-render-loop.md`: 渲染循环

---

## 总结

Platform 抽象层通过**统一的 SwapChain 接口**、**平台特定实现**、**能力查询机制**，成功地隐藏了不同操作系统和窗口系统的复杂性，为上层提供了简洁统一的渲染目标管理接口。

**核心技术**:
- ✅ SwapChain 抽象：统一的双缓冲/三缓冲管理
- ✅ 平台实现：支持 Windows/Linux/macOS/Android/iOS
- ✅ 窗口集成：与各平台窗口系统无缝集成
- ✅ 特性支持：VSync、HDR、多显示器等
- ✅ 错误处理：统一的错误报告机制
