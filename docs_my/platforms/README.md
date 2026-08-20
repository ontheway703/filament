# Filament 平台集成完整文档

## 概述

Filament 是一个跨平台渲染引擎，支持 Android、iOS、Windows、macOS、Linux 和 Web。本目录详细介绍如何在各个平台上集成和使用 Filament，包括平台特定的配置、窗口系统集成、SwapChain 创建和生命周期管理。

### 平台支持概览

```
┌─────────────────────────────────────────────────────────┐
│                  Filament Engine (跨平台)                │
└─────────────────────────────────────────────────────────┘
                          ↓
        ┌─────────────────┴─────────────────┐
        │        Platform 抽象层              │
        └─────────────────┬─────────────────┘
                          ↓
    ┌──────┬──────┬──────┬──────┬──────┬──────┐
    │Android│ iOS │Windows│macOS │Linux │ Web  │
    │(NDK) │Metal│OpenGL │Metal │Vulkan│WebGL │
    └──────┴──────┴──────┴──────┴──────┴──────┘
```

---

## 📚 文档结构

### 平台概览部分

1. **[01-platform-overview.md](01-platform-overview.md)** - 平台支持概览
   - 支持的平台和 Backend
   - 平台特性对比矩阵
   - 平台选择指南
   - 最低系统要求

2. **[02-android-integration.md](02-android-integration.md)** - Android 集成
   - NDK 开发环境配置
   - JNI 绑定层
   - SurfaceView/TextureView 集成
   - Vulkan vs OpenGL ES 选择
   - 完整 Android 示例

3. **[03-ios-integration.md](03-ios-integration.md)** - iOS 集成
   - Xcode 项目配置
   - Metal 后端集成
   - UIView/CAMetalLayer 集成
   - Objective-C++/Swift 互操作
   - 完整 iOS 示例

### 桌面平台部分

4. **[04-desktop-windows.md](04-desktop-windows.md)** - Windows 集成
   - Visual Studio 项目配置
   - Win32 窗口集成
   - Vulkan/OpenGL 选择
   - GLFW/SDL2 集成
   - 完整 Windows 示例

5. **[05-desktop-macos.md](05-desktop-macos.md)** - macOS 集成
   - Xcode 命令行工具
   - NSView/CAMetalLayer
   - Metal 后端优化
   - Cocoa 窗口系统
   - 完整 macOS 示例

6. **[06-desktop-linux.md](06-desktop-linux.md)** - Linux 集成
   - GCC/Clang 编译配置
   - X11/Wayland 窗口系统
   - Vulkan/OpenGL 配置
   - GLFW/SDL2 集成
   - 完整 Linux 示例

### Web 和 SwapChain 部分

7. **[07-web-integration.md](07-web-integration.md)** - Web/Emscripten 集成
   - Emscripten 工具链配置
   - WebGL 后端
   - Canvas 元素集成
   - WebAssembly 优化
   - 完整 Web 示例

8. **[08-swapchain-creation.md](08-swapchain-creation.md)** - SwapChain 创建详解
   - SwapChain 概念和作用
   - 各平台 SwapChain 创建
   - 帧同步和 VSync
   - 多窗口支持
   - 性能优化

9. **[09-lifecycle-management.md](09-lifecycle-management.md)** - 应用生命周期管理
   - 平台生命周期事件
   - 资源管理策略
   - 后台/前台切换
   - 内存警告处理
   - 完整生命周期示例

---

## 🎯 学习路径

### 路径 1: Android 开发者

如果你开发 Android 应用：

1. **平台概览**: 阅读 `01-platform-overview.md` 了解 Filament 平台支持
2. **Android 集成**: 深入学习 `02-android-integration.md`
3. **SwapChain**: 理解 `08-swapchain-creation.md` 中的 Android 部分
4. **生命周期**: 掌握 `09-lifecycle-management.md` 的 Android 生命周期
5. **实践**: 运行 Android 示例项目

**推荐阅读顺序**: 01 → 02 → 08 → 09

**适合人群**: Android 应用开发者、移动游戏开发者

### 路径 2: iOS 开发者

如果你开发 iOS 应用：

1. **平台概览**: 阅读 `01-platform-overview.md`
2. **iOS 集成**: 深入学习 `03-ios-integration.md`
3. **Metal 优化**: 理解 Metal 后端特性
4. **生命周期**: 掌握 iOS 应用生命周期管理
5. **实践**: 运行 iOS 示例项目

**推荐阅读顺序**: 01 → 03 → 08 → 09

**适合人群**: iOS 应用开发者、移动游戏开发者

### 路径 3: 桌面应用开发者

如果你开发桌面应用（游戏/工具）：

1. **平台概览**: 先学习 `01-platform-overview.md`
2. **选择平台**: 根据目标平台选择：
   - Windows: `04-desktop-windows.md`
   - macOS: `05-desktop-macos.md`
   - Linux: `06-desktop-linux.md`
3. **窗口系统**: 学习 GLFW/SDL2 集成
4. **SwapChain**: 理解桌面平台的 SwapChain 创建
5. **实践**: 运行跨平台桌面示例

**推荐阅读顺序**: 01 → 04/05/06 → 08

**适合人群**: 桌面游戏开发者、工具开发者

### 路径 4: Web 开发者

如果你开发 Web 应用：

1. **平台概览**: 阅读 `01-platform-overview.md`
2. **Web 集成**: 深入学习 `07-web-integration.md`
3. **WebGL 优化**: 理解 WebGL 后端限制和优化
4. **实践**: 运行 Web 示例

**推荐阅读顺序**: 01 → 07 → 08

**适合人群**: Web 3D 开发者、WebGL 开发者

---

## 🔗 与 docs_my 其他文档的关系

本目录的平台知识连接引擎理论和实际应用：

### backend 目录 (后端抽象)
- `backend/08-platform-abstraction.md` - **底层原理**: Platform 接口设计
- `backend/05-opengl-backend.md` - **OpenGL 实现**: 平台使用的 OpenGL 后端
- `backend/06-vulkan-backend.md` - **Vulkan 实现**: Android/Windows/Linux 的 Vulkan
- `backend/07-metal-backend.md` - **Metal 实现**: iOS/macOS 的 Metal

### engine 目录 (引擎架构)
- `engine/01-core-concepts.md` - **Engine 创建**: 平台无关的 Engine 初始化
- `engine/07-render-loop.md` - **渲染循环**: SwapChain 在渲染循环中的使用
- `engine/03-resource-management.md` - **资源管理**: 平台特定的资源加载

### tools 目录 (工具链)
- `tools/02-matc-compiler.md` - **平台编译**: 为不同平台编译材质
- `tools/08-asset-pipeline.md` - **多平台构建**: CI/CD 集成

---

## 🌟 核心概念速查

### Filament 平台架构

```
应用层 (Platform-Specific Code)
    ↓
    创建 Window/Surface
    ↓
Filament Platform Layer
    ├─ Android: ANativeWindow
    ├─ iOS: CAMetalLayer
    ├─ Windows: HWND + SwapChain
    ├─ macOS: NSView + CAMetalLayer
    ├─ Linux: X11 Window + EGL
    └─ Web: HTMLCanvasElement
    ↓
SwapChain (Filament)
    ↓
Backend (OpenGL/Vulkan/Metal)
    ↓
GPU
```

### 平台支持矩阵

| 平台 | 图形 API | 最低版本 | 推荐 Backend |
|------|---------|---------|-------------|
| **Android** | OpenGL ES 3.0 | Android 5.0 (API 21) | Vulkan (API 24+) |
| | Vulkan 1.0 | Android 7.0 (API 24) | |
| **iOS** | Metal | iOS 11+ | Metal |
| **Windows** | Vulkan 1.0 | Windows 7+ | Vulkan |
| | OpenGL 4.1 | Windows 7+ | |
| **macOS** | Metal | macOS 10.13+ | Metal |
| | OpenGL 4.1 | macOS 10.9+ | (deprecated) |
| **Linux** | Vulkan 1.0 | - | Vulkan |
| | OpenGL 4.1 | - | OpenGL |
| **Web** | WebGL 2.0 | - | WebGL |

### SwapChain 创建快速参考

**Android (Java/Kotlin):**
```java
// SurfaceView
SurfaceView surfaceView = findViewById(R.id.surface_view);
Surface surface = surfaceView.getHolder().getSurface();

// Native
nativeSwapChain = nCreateSwapChain(surface, width, height);
```

**iOS (Objective-C/Swift):**
```objc
// CAMetalLayer
CAMetalLayer* metalLayer = [CAMetalLayer layer];
[view.layer addSublayer:metalLayer];

// Filament
swapChain = engine->createSwapChain((__bridge void*)metalLayer);
```

**Windows (C++):**
```cpp
// Win32 Window
HWND hwnd = CreateWindowEx(...);

// Filament
swapChain = engine->createSwapChain(hwnd, flags);
```

**Web (C++/Emscripten):**
```cpp
// Canvas element ID
const char* canvasId = "filament-canvas";

// Filament
swapChain = engine->createSwapChain((void*)canvasId);
```

---

## ⚡ 快速开始示例

### 最简 Android 集成

```java
// MainActivity.java
public class MainActivity extends AppCompatActivity {
    static {
        System.loadLibrary("filament-jni");
    }

    private SurfaceView surfaceView;
    private long nativeEngine;
    private long nativeSwapChain;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        surfaceView = new SurfaceView(this);
        setContentView(surfaceView);

        surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                Surface surface = holder.getSurface();
                nativeEngine = nCreateEngine();
                nativeSwapChain = nCreateSwapChain(nativeEngine, surface);
                nRenderLoop(nativeEngine, nativeSwapChain);
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                nDestroySwapChain(nativeSwapChain);
                nDestroyEngine(nativeEngine);
            }
        });
    }

    private native long nCreateEngine();
    private native long nCreateSwapChain(long engine, Surface surface);
    private native void nRenderLoop(long engine, long swapChain);
}
```

### 最简 iOS 集成

```objc
// ViewController.mm
@implementation ViewController {
    filament::Engine* engine;
    filament::SwapChain* swapChain;
    CAMetalLayer* metalLayer;
}

- (void)viewDidLoad {
    [super viewDidLoad];

    // 创建 Metal Layer
    metalLayer = [CAMetalLayer layer];
    metalLayer.frame = self.view.bounds;
    [self.view.layer addSublayer:metalLayer];

    // 创建 Filament Engine
    engine = filament::Engine::create(filament::Engine::Backend::METAL);

    // 创建 SwapChain
    swapChain = engine->createSwapChain((__bridge void*)metalLayer);

    // 渲染循环
    [self startRenderLoop];
}

- (void)startRenderLoop {
    CADisplayLink* displayLink = [CADisplayLink displayLinkWithTarget:self
                                                             selector:@selector(render)];
    [displayLink addToRunLoop:[NSRunLoop mainRunLoop]
                      forMode:NSRunLoopCommonModes];
}

- (void)render {
    if (renderer->beginFrame(swapChain)) {
        renderer->render(view);
        renderer->endFrame();
    }
}
@end
```

### 最简 Desktop (GLFW) 集成

```cpp
// main.cpp
#include <filament/Engine.h>
#include <GLFW/glfw3.h>

int main() {
    // 初始化 GLFW
    glfwInit();
    GLFWwindow* window = glfwCreateWindow(800, 600, "Filament", nullptr, nullptr);

    // 创建 Filament Engine
    filament::Engine* engine = filament::Engine::create(
        filament::Engine::Backend::VULKAN
    );

    // 创建 SwapChain
    void* nativeWindow = glfwGetX11Window(window);  // Linux
    // void* nativeWindow = glfwGetWin32Window(window);  // Windows
    // void* nativeWindow = glfwGetCocoaWindow(window);  // macOS

    filament::SwapChain* swapChain = engine->createSwapChain(nativeWindow);

    // 渲染循环
    while (!glfwWindowShouldClose(window)) {
        if (renderer->beginFrame(swapChain)) {
            renderer->render(view);
            renderer->endFrame();
        }
        glfwPollEvents();
    }

    // 清理
    engine->destroy(swapChain);
    filament::Engine::destroy(&engine);
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
```

---

## 📖 推荐学习资源

### 官方资源

- **[Filament 文档](https://google.github.io/filament/)**: 官方平台集成指南
- **[Filament 示例](https://github.com/google/filament/tree/main/samples)**: 各平台示例代码
- **[Android Samples](https://github.com/google/filament/tree/main/android)**: Android 完整示例

### 平台特定资源

**Android:**
- [Android NDK 文档](https://developer.android.com/ndk)
- [Vulkan on Android](https://developer.android.com/ndk/guides/graphics/getting-started)

**iOS:**
- [Metal 编程指南](https://developer.apple.com/metal/)
- [Core Animation 文档](https://developer.apple.com/documentation/quartzcore)

**Windows:**
- [Win32 API 文档](https://docs.microsoft.com/en-us/windows/win32/)
- [Vulkan SDK](https://vulkan.lunarg.com/)

**Linux:**
- [X11 编程](https://www.x.org/releases/current/doc/)
- [Wayland 文档](https://wayland.freedesktop.org/)

**Web:**
- [Emscripten 文档](https://emscripten.org/docs/)
- [WebGL 2.0 规范](https://www.khronos.org/webgl/)

---

## 🎓 使用建议

### 对于移动开发者

1. **从简单开始**: 先运行官方示例，理解基本流程
2. **理解生命周期**: 移动平台的生命周期管理至关重要
3. **性能优化**: 关注功耗和发热
4. **测试多设备**: 在不同硬件上测试性能

### 对于桌面开发者

1. **选择窗口库**: GLFW 简单易用，SDL2 功能丰富
2. **Backend 选择**: 优先使用 Vulkan（性能更好）
3. **调试工具**: 熟悉 RenderDoc/Nsight
4. **跨平台**: 尽量使用跨平台的窗口系统

### 对于 Web 开发者

1. **文件大小**: 关注 WASM 文件大小，使用压缩
2. **性能限制**: WebGL 性能低于原生平台
3. **异步加载**: 资源异步加载，避免阻塞
4. **浏览器兼容**: 测试多种浏览器

---

## 📝 版本信息

- **文档版本**: 1.0
- **创建日期**: 2025-10-20
- **适用 Filament 版本**: 最新主分支
- **更新策略**: 随 Filament 平台支持更新同步维护

---

## 🚀 开始学习

选择适合你的学习路径，从第一篇文档开始，系统掌握 Filament 在各平台的集成方法！

> "掌握平台集成，是将 Filament 应用到实际项目的第一步！"
