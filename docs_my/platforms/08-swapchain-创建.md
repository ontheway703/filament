# SwapChain 创建详解

## 概述

SwapChain 是连接渲染引擎和窗口系统的桥梁,负责管理帧缓冲和显示同步。本文档详细介绍各平台 SwapChain 的创建方法、配置选项和最佳实践。

**主要内容**:
- ✅ SwapChain 概念和工作原理
- ✅ 各平台创建方法 (Android/iOS/Windows/macOS/Linux/Web)
- ✅ 配置选项详解
- ✅ HDR 和多缓冲支持
- ✅ 性能优化技巧
- ✅ 常见问题和解决方案

## SwapChain 基础

### 什么是 SwapChain

SwapChain 管理一组帧缓冲,用于实现双缓冲或三缓冲渲染:

```
┌─────────────┐
│   Engine    │
└──────┬──────┘
       │
       │ render()
       ↓
┌─────────────┐     ┌──────────────┐
│  SwapChain  │ ──→ │  Back Buffer │ (正在渲染)
└─────────────┘     ├──────────────┤
       │            │ Front Buffer │ (正在显示)
       │            └──────────────┘
       ↓
┌─────────────┐
│   Display   │
└─────────────┘
```

### 核心 API

```cpp
// 创建 SwapChain
SwapChain* swapChain = engine->createSwapChain(nativeWindow);

// 带配置创建
SwapChain* swapChain = engine->createSwapChain(nativeWindow, flags);

// 开始渲染帧
if (renderer->beginFrame(swapChain)) {
    renderer->render(view);
    renderer->endFrame();
}

// 销毁
engine->destroy(swapChain);
```

## Android SwapChain

### 从 Surface 创建

```java
// Java 层
SurfaceView surfaceView = findViewById(R.id.surface_view);
Surface surface = surfaceView.getHolder().getSurface();

// JNI 传递到 Native
nativeCreateSwapChain(nativeEngine, surface);
```

```cpp
// Native 层 (C++)
JNIEXPORT jlong JNICALL
Java_...nativeCreateSwapChain(JNIEnv* env, jobject, jlong nativeEngine, jobject surface) {
    Engine* engine = reinterpret_cast<Engine*>(nativeEngine);

    // 获取 ANativeWindow
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);

    // 创建 SwapChain
    SwapChain* swapChain = engine->createSwapChain(window);

    return reinterpret_cast<jlong>(swapChain);
}
```

### Android 配置选项

```cpp
// 透明窗口
uint64_t flags = SwapChain::CONFIG_TRANSPARENT;
SwapChain* swapChain = engine->createSwapChain(window, flags);

// 可读 SwapChain (用于截图)
flags = SwapChain::CONFIG_READABLE;

// 启用 sRGB
flags = SwapChain::CONFIG_SRGB_COLORSPACE;
```

## iOS/macOS SwapChain

### 从 CAMetalLayer 创建

```objc
// Objective-C
CAMetalLayer* metalLayer = (CAMetalLayer*)view.layer;

// 配置 Metal Layer
metalLayer.device = MTLCreateSystemDefaultDevice();
metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
metalLayer.framebufferOnly = YES;

// 传递给 Filament
void* nativeLayer = (__bridge void*)metalLayer;
SwapChain* swapChain = engine->createSwapChain(nativeLayer);
```

### iOS 特殊配置

```objc
// ProMotion 支持 (120Hz)
metalLayer.displaySyncEnabled = YES;

// HDR 支持
if (@available(iOS 16.0, *)) {
    metalLayer.wantsExtendedDynamicRangeContent = YES;
    metalLayer.pixelFormat = MTLPixelFormatRGBA16Float;
}

// Retina 缩放
CGFloat scale = UIScreen.mainScreen.nativeScale;
metalLayer.contentsScale = scale;
metalLayer.drawableSize = CGSizeMake(width * scale, height * scale);
```

## Windows SwapChain

### 从 HWND 创建

```cpp
// Win32 窗口句柄
HWND hwnd = /* ... */;

// 创建 SwapChain
void* nativeWindow = (void*)hwnd;
SwapChain* swapChain = engine->createSwapChain(nativeWindow);
```

### Vulkan Backend 配置

```cpp
// 启用 VSync
SwapChain* swapChain = engine->createSwapChain(
    nativeWindow,
    SwapChain::CONFIG_DEFAULT
);

// 禁用 VSync (最大性能)
// 需要在 Renderer 中配置
Renderer::DisplayInfo displayInfo;
displayInfo.presentationDeadline = 0;
renderer->setDisplayInfo(displayInfo);
```

### HDR 支持 (Windows 10+)

```cpp
// 检查 HDR 支持
bool isHDRSupported = /* 查询显示器能力 */;

if (isHDRSupported) {
    uint64_t flags = SwapChain::CONFIG_ENABLE_HDR;
    SwapChain* swapChain = engine->createSwapChain(nativeWindow, flags);
}
```

## Linux SwapChain

### X11 窗口

```cpp
// X11 Display 和 Window
Display* display = XOpenDisplay(nullptr);
Window window = /* 创建的窗口 */;

// 创建 SwapChain
void* nativeWindow = (void*)(uintptr_t)window;
SwapChain* swapChain = engine->createSwapChain(nativeWindow);
```

### Wayland

```cpp
// Wayland surface
struct wl_surface* surface = /* ... */;

// 创建 EGL Window
struct wl_egl_window* eglWindow = wl_egl_window_create(surface, width, height);

// 创建 SwapChain
void* nativeWindow = (void*)eglWindow;
SwapChain* swapChain = engine->createSwapChain(nativeWindow);
```

## Web SwapChain

### 从 Canvas 创建

```javascript
// JavaScript
const canvas = document.getElementById('canvas');
const engine = Filament.Engine.create(canvas);

// SwapChain 自动创建
const swapChain = engine.createSwapChain();
```

### WebGL 配置

```javascript
// 透明背景
const canvas = document.getElementById('canvas');
const gl = canvas.getContext('webgl2', {
    alpha: true,
    premultipliedAlpha: false
});

// Filament 会自动使用这些配置
```

## 配置选项详解

### CONFIG_DEFAULT

```cpp
// 默认配置: 双缓冲, 不透明, 不可读
SwapChain* swapChain = engine->createSwapChain(
    nativeWindow,
    SwapChain::CONFIG_DEFAULT
);
```

### CONFIG_TRANSPARENT

```cpp
// 透明窗口 (用于浮动窗口、overlay 等)
uint64_t flags = SwapChain::CONFIG_TRANSPARENT;
SwapChain* swapChain = engine->createSwapChain(nativeWindow, flags);

// 注意: 需要窗口系统支持透明
// Android: android:windowIsTranslucent="true"
// iOS: view.opaque = NO
```

### CONFIG_READABLE

```cpp
// 可读 SwapChain (允许读取像素,用于截图)
uint64_t flags = SwapChain::CONFIG_READABLE;
SwapChain* swapChain = engine->createSwapChain(nativeWindow, flags);

// 读取像素
renderer->readPixels(/* ... */);
```

### CONFIG_ENABLE_HDR

```cpp
// HDR 支持 (需要硬件支持)
uint64_t flags = SwapChain::CONFIG_ENABLE_HDR;
SwapChain* swapChain = engine->createSwapChain(nativeWindow, flags);

// 设置色调映射
ColorGrading* colorGrading = ColorGrading::Builder()
    .toneMapping(ColorGrading::ToneMapping::ACES)
    .build(*engine);
view->setColorGrading(colorGrading);
```

### 组合配置

```cpp
// 透明 + 可读
uint64_t flags = SwapChain::CONFIG_TRANSPARENT |
                 SwapChain::CONFIG_READABLE;
SwapChain* swapChain = engine->createSwapChain(nativeWindow, flags);
```

## 多缓冲配置

### 双缓冲 vs 三缓冲

```cpp
// 双缓冲 (默认)
// - 优点: 内存占用小
// - 缺点: 可能有输入延迟

// 三缓冲
// - 优点: 减少输入延迟, 更流畅
// - 缺点: 内存占用大

// Filament 默认使用双缓冲
// Metal/iOS 可以配置:
metalLayer.maximumDrawableCount = 2;  // 双缓冲
metalLayer.maximumDrawableCount = 3;  // 三缓冲
```

### VSync 控制

```cpp
// 启用 VSync (60Hz 同步)
Renderer::DisplayInfo displayInfo;
displayInfo.presentationDeadline = 16666667;  // 纳秒 (60Hz)
renderer->setDisplayInfo(displayInfo);

// 禁用 VSync (最大帧率)
displayInfo.presentationDeadline = 0;
renderer->setDisplayInfo(displayInfo);

// 自适应 VSync
// 根据渲染时间动态调整
```

## 窗口大小变化

### 处理 Resize

```cpp
// 1. 销毁旧 SwapChain
engine->destroy(swapChain);

// 2. 重新创建 SwapChain
swapChain = engine->createSwapChain(nativeWindow);

// 3. 更新 Viewport
view->setViewport({0, 0, newWidth, newHeight});

// 4. 更新相机投影
camera->setProjection(
    Camera::Projection::PERSPECTIVE,
    45.0,
    (double)newWidth / (double)newHeight,
    0.1, 100.0,
    Camera::Fov::VERTICAL
);
```

### 避免频繁重建

```cpp
// 方法 1: 延迟重建
class ResizeHandler {
    std::chrono::steady_clock::time_point lastResize;
    const std::chrono::milliseconds debounceDelay{100};

    void onResize(int width, int height) {
        lastResize = std::chrono::steady_clock::now();
        pendingWidth = width;
        pendingHeight = height;
    }

    void update() {
        auto now = std::chrono::steady_clock::now();
        if (now - lastResize >= debounceDelay) {
            rebuildSwapChain(pendingWidth, pendingHeight);
        }
    }
};

// 方法 2: 仅更新 Viewport (某些平台)
// Metal/iOS 会自动调整 drawable size
view->setViewport({0, 0, newWidth, newHeight});
```

## 性能优化

### 1. 最小化 SwapChain 重建

```cpp
// 坏例子: 每次 resize 都重建
void onResize(int w, int h) {
    engine->destroy(swapChain);
    swapChain = engine->createSwapChain(nativeWindow);
}

// 好例子: 仅在必要时重建
void onResize(int w, int h) {
    // Metal/iOS 不需要重建
    #if defined(__APPLE__)
        // 仅更新 viewport
        view->setViewport({0, 0, (uint32_t)w, (uint32_t)h});
    #else
        // 其他平台重建
        engine->destroy(swapChain);
        swapChain = engine->createSwapChain(nativeWindow);
    #endif
}
```

### 2. 选择合适的缓冲数

```cpp
// 移动设备: 双缓冲 (节省内存)
metalLayer.maximumDrawableCount = 2;

// 桌面设备: 三缓冲 (更流畅)
// (Vulkan/OpenGL 自动管理)
```

### 3. 避免 Drawable 超时

```objc
// iOS/macOS
metalLayer.allowsNextDrawableTimeout = NO;

// 确保及时获取 drawable
id<CAMetalDrawable> drawable = [metalLayer nextDrawable];
if (!drawable) {
    // 处理获取失败
}
```

## 常见问题

### Q1: SwapChain 创建失败

**问题:**
```cpp
SwapChain* swapChain = engine->createSwapChain(nativeWindow);
// swapChain == nullptr
```

**解决方案:**

```cpp
// 1. 检查窗口句柄有效性
#ifdef _WIN32
    if (!IsWindow((HWND)nativeWindow)) {
        // 窗口无效
    }
#endif

// 2. 检查 Surface 有效性 (Android)
if (!ANativeWindow_getWidth(window)) {
    // Surface 无效
}

// 3. 检查 Metal Layer (iOS/macOS)
CAMetalLayer* layer = (__bridge CAMetalLayer*)nativeLayer;
if (!layer.device) {
    layer.device = MTLCreateSystemDefaultDevice();
}
```

### Q2: 渲染黑屏

**可能原因:**

```cpp
// 1. SwapChain 未正确传递给 Renderer
if (!renderer->beginFrame(swapChain)) {
    // 帧开始失败
}

// 2. Viewport 未设置
view->setViewport({0, 0, width, height});

// 3. 相机未配置
camera->lookAt({0, 0, 4}, {0, 0, 0}, {0, 1, 0});
```

### Q3: 窗口 Resize 崩溃

**解决方案:**

```cpp
void onResize(int width, int height) {
    // 避免宽高为 0
    if (width == 0 || height == 0) return;

    // 等待渲染完成
    engine->flushAndWait();

    // 销毁旧 SwapChain
    engine->destroy(swapChain);

    // 创建新 SwapChain
    swapChain = engine->createSwapChain(nativeWindow);

    // 更新 Viewport
    view->setViewport({0, 0, (uint32_t)width, (uint32_t)height});
}
```

### Q4: HDR 不工作

**检查清单:**

```cpp
// 1. 硬件支持
bool isHDRSupported = /* 查询显示器 */;

// 2. SwapChain 配置
uint64_t flags = SwapChain::CONFIG_ENABLE_HDR;

// 3. 色彩空间
// iOS:
metalLayer.wantsExtendedDynamicRangeContent = YES;
metalLayer.pixelFormat = MTLPixelFormatRGBA16Float;

// 4. 色调映射
view->setColorGrading(colorGrading);
```

### Q5: VSync 不生效

**解决方案:**

```cpp
// 方法 1: 使用 DisplayInfo
Renderer::DisplayInfo displayInfo;
displayInfo.presentationDeadline = 16666667;  // 60Hz
renderer->setDisplayInfo(displayInfo);

// 方法 2: 平台特定设置
// Metal:
metalLayer.displaySyncEnabled = YES;

// Vulkan/OpenGL:
// 通过 SwapChain 创建参数控制
```

## 最佳实践

### 1. 生命周期管理

```cpp
class FilamentApp {
    Engine* engine = nullptr;
    SwapChain* swapChain = nullptr;

public:
    void onCreate(void* nativeWindow) {
        engine = Engine::create();
        swapChain = engine->createSwapChain(nativeWindow);
    }

    void onDestroy() {
        if (engine && swapChain) {
            engine->destroy(swapChain);
            swapChain = nullptr;
        }

        if (engine) {
            Engine::destroy(&engine);
        }
    }

    void onSurfaceChanged(void* nativeWindow) {
        // 重建 SwapChain
        if (swapChain) {
            engine->destroy(swapChain);
        }
        swapChain = engine->createSwapChain(nativeWindow);
    }
};
```

### 2. 错误处理

```cpp
SwapChain* createSwapChainSafely(Engine* engine, void* window) {
    SwapChain* swapChain = engine->createSwapChain(window);

    if (!swapChain) {
        // 尝试降级配置
        swapChain = engine->createSwapChain(
            window,
            SwapChain::CONFIG_DEFAULT
        );
    }

    if (!swapChain) {
        throw std::runtime_error("Failed to create SwapChain");
    }

    return swapChain;
}
```

### 3. 平台适配

```cpp
SwapChain* createPlatformSwapChain(Engine* engine, void* nativeWindow) {
    #if defined(__ANDROID__)
        // Android: ANativeWindow
        return engine->createSwapChain(nativeWindow);

    #elif defined(__APPLE__)
        // iOS/macOS: CAMetalLayer
        return engine->createSwapChain(nativeWindow);

    #elif defined(_WIN32)
        // Windows: HWND
        return engine->createSwapChain(nativeWindow);

    #elif defined(__linux__)
        // Linux: X11 Window 或 Wayland Surface
        return engine->createSwapChain(nativeWindow);

    #else
        #error "Unsupported platform"
    #endif
}
```

## 相关文档

- [02-android-integration.md](./02-android-integration.md) - Android SwapChain 详解
- [03-ios-集成.md](./03-ios-集成.md) - iOS SwapChain 详解
- [04-windows-桌面集成.md](./04-windows-桌面集成.md) - Windows SwapChain 详解
- [09-应用生命周期管理.md](./09-应用生命周期管理.md) - 生命周期管理
- [../engine/02-renderer-configuration.md](../engine/02-renderer-configuration.md) - Renderer 配置
