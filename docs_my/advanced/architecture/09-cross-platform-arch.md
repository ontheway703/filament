# Filament 跨平台架构设计

## 📖 概述

跨平台是 Filament 的核心优势之一——它支持 Windows、macOS、Linux、Android、iOS 和 WebGL，覆盖桌面、移动和 Web 三大生态。实现高质量的跨平台架构极具挑战，需要在统一接口和平台特性之间找到平衡。

**跨平台的核心挑战**：
- **渲染后端差异**：Vulkan、Metal、OpenGL/ES、WebGL 的 API 差异
- **窗口系统差异**：Win32、Cocoa、X11、Android Surface、HTML Canvas
- **文件系统差异**：路径分隔符、资源打包方式、权限模型
- **输入系统差异**：鼠标、触摸、手柄、键盘布局
- **性能特性差异**：GPU 能力、内存带宽、CPU 核心数
- **构建系统差异**：CMake、Gradle、Xcode、Emscripten

**设计目标**：
1. **统一 API**：应用代码 90%+ 可跨平台复用
2. **平台优化**：充分利用各平台特性（Metal 的 memoryless render target、Vulkan 的异步计算）
3. **最小条件编译**：平台差异封装在底层，上层代码无感知
4. **性能一致性**：各平台渲染质量和性能接近

本文档将系统讲解 Filament 跨平台架构的设计原理、实现细节和最佳实践。

---

## 1. 跨平台架构分层

### 1.1 分层架构

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│              (100% Platform-Independent)                     │
│  - Game Logic                                                │
│  - Scene Management                                          │
│  - Resource Loading                                          │
└────────────────────┬────────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────────┐
│                  Filament Engine API                         │
│              (99% Platform-Independent)                      │
│  - View, Scene, Camera                                       │
│  - Entity, TransformManager, RenderableManager               │
│  - Material, Texture, VertexBuffer                           │
└────────────────────┬────────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────────┐
│               Platform Abstraction Layer                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │   Window     │  │  FileSystem  │  │      Input       │  │
│  │  (Abstract)  │  │  (Abstract)  │  │   (Abstract)     │  │
│  └──────┬───────┘  └──────┬───────┘  └──────────┬───────┘  │
└─────────┼──────────────────┼──────────────────────┼──────────┘
          │                  │                      │
┌─────────▼──────────────────▼──────────────────────▼──────────┐
│              Platform-Specific Implementations                │
│  ┌────────────┐  ┌───────────┐  ┌───────────┐  ┌─────────┐ │
│  │  Windows   │  │   macOS   │  │  Android  │  │  WebGL  │ │
│  │ (Win32 +   │  │ (Cocoa +  │  │ (Android  │  │ (GLES + │ │
│  │  Vulkan/   │  │  Metal/   │  │  Surface+ │  │ Canvas) │ │
│  │   D3D12)   │  │  Vulkan)  │  │ GLES/Vk)  │  │         │ │
│  └────────────┘  └───────────┘  └───────────┘  └─────────┘ │
└───────────────────────────────────────────────────────────────┘
```

### 1.2 关键设计原则

**抽象接口**：
- 定义与平台无关的接口（如 `IPlatform`、`IWindow`）
- 各平台实现具体子类

**条件编译最小化**：
```cpp
// ❌ 不好：到处都是 #ifdef
void render() {
#ifdef _WIN32
    renderWin32();
#elif defined(__APPLE__)
    renderMacOS();
#elif defined(__ANDROID__)
    renderAndroid();
#endif
}

// ✅ 好：通过多态隐藏差异
void render() {
    mPlatform->render();  // 运行时多态
}
```

**编译时多态（模板）**：
```cpp
template<typename PlatformTraits>
class Window {
    typename PlatformTraits::NativeWindow mNativeWindow;
    
    void resize(int width, int height) {
        PlatformTraits::resizeWindow(mNativeWindow, width, height);
    }
};
```

---

## 2. 平台抽象层

### 2.1 平台接口

```cpp
// src/platform/include/platform/IPlatform.h
#pragma once

#include <string>
#include <vector>

namespace myproject::platform {

// 平台类型
enum class PlatformType {
    Windows,
    macOS,
    Linux,
    Android,
    iOS,
    Web
};

// 平台接口
class IPlatform {
public:
    virtual ~IPlatform() = default;
    
    // 平台信息
    virtual PlatformType getType() const = 0;
    virtual std::string getName() const = 0;
    virtual std::string getVersion() const = 0;
    
    // 文件系统
    virtual std::string getExecutablePath() const = 0;
    virtual std::string getDocumentsPath() const = 0;
    virtual std::string getTempPath() const = 0;
    virtual std::string normalizePath(const std::string& path) const = 0;
    
    // 窗口系统
    virtual void* createNativeWindow(int width, int height, const char* title) = 0;
    virtual void destroyNativeWindow(void* window) = 0;
    virtual void getWindowSize(void* window, int& width, int& height) = 0;
    
    // 输入
    virtual bool isKeyPressed(int keyCode) const = 0;
    virtual void getMousePosition(int& x, int& y) const = 0;
    
    // 系统特性
    virtual bool isHighDPI() const = 0;
    virtual float getDPIScale() const = 0;
    virtual int getCPUCoreCount() const = 0;
    virtual uint64_t getPhysicalMemory() const = 0;
    
    // 时间
    virtual double getTime() const = 0;
    virtual void sleep(uint32_t milliseconds) const = 0;
};

// 工厂函数
IPlatform* createPlatform();

}  // namespace myproject::platform
```

### 2.2 Windows 实现

```cpp
// src/platform/src/WindowsPlatform.cpp
#include <platform/IPlatform.h>

#if defined(_WIN32)

#include <windows.h>
#include <shlobj.h>

namespace myproject::platform {

class WindowsPlatform : public IPlatform {
public:
    PlatformType getType() const override {
        return PlatformType::Windows;
    }
    
    std::string getName() const override {
        return "Windows";
    }
    
    std::string getVersion() const override {
        OSVERSIONINFOA osvi;
        ZeroMemory(&osvi, sizeof(OSVERSIONINFOA));
        osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
        GetVersionExA(&osvi);
        
        return std::to_string(osvi.dwMajorVersion) + "." +
               std::to_string(osvi.dwMinorVersion);
    }
    
    std::string getExecutablePath() const override {
        char buffer[MAX_PATH];
        GetModuleFileNameA(nullptr, buffer, MAX_PATH);
        return normalizePath(std::string(buffer));
    }
    
    std::string getDocumentsPath() const override {
        char buffer[MAX_PATH];
        SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, buffer);
        return normalizePath(std::string(buffer));
    }
    
    std::string getTempPath() const override {
        char buffer[MAX_PATH];
        GetTempPathA(MAX_PATH, buffer);
        return normalizePath(std::string(buffer));
    }
    
    std::string normalizePath(const std::string& path) const override {
        std::string normalized = path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        return normalized;
    }
    
    void* createNativeWindow(int width, int height, const char* title) override {
        WNDCLASSEXA wc = {};
        wc.cbSize = sizeof(WNDCLASSEXA);
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = "FilamentWindow";
        RegisterClassExA(&wc);
        
        HWND hwnd = CreateWindowExA(
            0,
            "FilamentWindow",
            title,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            width, height,
            nullptr, nullptr,
            GetModuleHandle(nullptr),
            nullptr
        );
        
        ShowWindow(hwnd, SW_SHOW);
        return hwnd;
    }
    
    void destroyNativeWindow(void* window) override {
        DestroyWindow(static_cast<HWND>(window));
    }
    
    void getWindowSize(void* window, int& width, int& height) override {
        RECT rect;
        GetClientRect(static_cast<HWND>(window), &rect);
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
    }
    
    bool isHighDPI() const override {
        return getDPIScale() > 1.0f;
    }
    
    float getDPIScale() const override {
        HDC hdc = GetDC(nullptr);
        int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
        return dpi / 96.0f;
    }
    
    int getCPUCoreCount() const override {
        SYSTEM_INFO sysinfo;
        GetSystemInfo(&sysinfo);
        return sysinfo.dwNumberOfProcessors;
    }
    
    uint64_t getPhysicalMemory() const override {
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);
        return memInfo.ullTotalPhys;
    }
    
    double getTime() const override {
        static LARGE_INTEGER frequency;
        static bool initialized = false;
        
        if (!initialized) {
            QueryPerformanceFrequency(&frequency);
            initialized = true;
        }
        
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        return static_cast<double>(counter.QuadPart) / frequency.QuadPart;
    }
    
    void sleep(uint32_t milliseconds) const override {
        Sleep(milliseconds);
    }
    
    bool isKeyPressed(int keyCode) const override {
        return (GetAsyncKeyState(keyCode) & 0x8000) != 0;
    }
    
    void getMousePosition(int& x, int& y) const override {
        POINT pt;
        GetCursorPos(&pt);
        x = pt.x;
        y = pt.y;
    }
};

IPlatform* createPlatform() {
    return new WindowsPlatform();
}

}  // namespace myproject::platform

#endif  // _WIN32
```

### 2.3 macOS 实现

```cpp
// src/platform/src/MacOSPlatform.mm
#include <platform/IPlatform.h>

#if defined(__APPLE__) && !defined(__IPHONE_OS_VERSION_MIN_REQUIRED)

#import <Cocoa/Cocoa.h>
#include <mach-o/dyld.h>
#include <sys/sysctl.h>

namespace myproject::platform {

class MacOSPlatform : public IPlatform {
public:
    PlatformType getType() const override {
        return PlatformType::macOS;
    }
    
    std::string getName() const override {
        return "macOS";
    }
    
    std::string getVersion() const override {
        NSOperatingSystemVersion version = [[NSProcessInfo processInfo] operatingSystemVersion];
        return std::to_string(version.majorVersion) + "." +
               std::to_string(version.minorVersion) + "." +
               std::to_string(version.patchVersion);
    }
    
    std::string getExecutablePath() const override {
        char buffer[1024];
        uint32_t size = sizeof(buffer);
        _NSGetExecutablePath(buffer, &size);
        return std::string(buffer);
    }
    
    std::string getDocumentsPath() const override {
        NSArray* paths = NSSearchPathForDirectoriesInDomains(
            NSDocumentDirectory, NSUserDomainMask, YES);
        NSString* documentsDirectory = [paths firstObject];
        return std::string([documentsDirectory UTF8String]);
    }
    
    std::string getTempPath() const override {
        NSString* tempDir = NSTemporaryDirectory();
        return std::string([tempDir UTF8String]);
    }
    
    std::string normalizePath(const std::string& path) const override {
        return path;  // macOS uses forward slashes natively
    }
    
    void* createNativeWindow(int width, int height, const char* title) override {
        NSRect frame = NSMakeRect(0, 0, width, height);
        
        NSWindow* window = [[NSWindow alloc]
            initWithContentRect:frame
            styleMask:(NSWindowStyleMaskTitled |
                      NSWindowStyleMaskClosable |
                      NSWindowStyleMaskMiniaturizable |
                      NSWindowStyleMaskResizable)
            backing:NSBackingStoreBuffered
            defer:NO];
        
        [window setTitle:[NSString stringWithUTF8String:title]];
        [window center];
        [window makeKeyAndOrderFront:nil];
        
        return (__bridge_retained void*)window;
    }
    
    void destroyNativeWindow(void* window) override {
        NSWindow* nsWindow = (__bridge_transfer NSWindow*)window;
        [nsWindow close];
    }
    
    void getWindowSize(void* window, int& width, int& height) override {
        NSWindow* nsWindow = (__bridge NSWindow*)window;
        NSRect frame = [nsWindow contentRectForFrameRect:[nsWindow frame]];
        width = static_cast<int>(frame.size.width);
        height = static_cast<int>(frame.size.height);
    }
    
    bool isHighDPI() const override {
        return [[NSScreen mainScreen] backingScaleFactor] > 1.0;
    }
    
    float getDPIScale() const override {
        return static_cast<float>([[NSScreen mainScreen] backingScaleFactor]);
    }
    
    int getCPUCoreCount() const override {
        int count;
        size_t size = sizeof(count);
        sysctlbyname("hw.ncpu", &count, &size, nullptr, 0);
        return count;
    }
    
    uint64_t getPhysicalMemory() const override {
        uint64_t memsize;
        size_t size = sizeof(memsize);
        sysctlbyname("hw.memsize", &memsize, &size, nullptr, 0);
        return memsize;
    }
    
    double getTime() const override {
        return CACurrentMediaTime();
    }
    
    void sleep(uint32_t milliseconds) const override {
        usleep(milliseconds * 1000);
    }
    
    bool isKeyPressed(int keyCode) const override {
        // macOS key checking requires event handling
        return false;
    }
    
    void getMousePosition(int& x, int& y) const override {
        NSPoint point = [NSEvent mouseLocation];
        x = static_cast<int>(point.x);
        y = static_cast<int>(point.y);
    }
};

IPlatform* createPlatform() {
    return new MacOSPlatform();
}

}  // namespace myproject::platform

#endif  // __APPLE__ && !__IPHONE_OS_VERSION_MIN_REQUIRED
```

### 2.4 Android 实现

```cpp
// src/platform/src/AndroidPlatform.cpp
#include <platform/IPlatform.h>

#if defined(__ANDROID__)

#include <android/native_window.h>
#include <android/log.h>
#include <unistd.h>
#include <sys/sysinfo.h>

namespace myproject::platform {

class AndroidPlatform : public IPlatform {
public:
    PlatformType getType() const override {
        return PlatformType::Android;
    }
    
    std::string getName() const override {
        return "Android";
    }
    
    std::string getVersion() const override {
        // Android API level
        return std::to_string(__ANDROID_API__);
    }
    
    std::string getExecutablePath() const override {
        char buffer[1024];
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len != -1) {
            buffer[len] = '\0';
            return std::string(buffer);
        }
        return "";
    }
    
    std::string getDocumentsPath() const override {
        // Android uses app-specific directory
        return "/data/data/com.mycompany.myapp/files";
    }
    
    std::string getTempPath() const override {
        return "/data/data/com.mycompany.myapp/cache";
    }
    
    std::string normalizePath(const std::string& path) const override {
        return path;  // Unix-style paths
    }
    
    void* createNativeWindow(int width, int height, const char* title) override {
        // Android windows are created from Java/Kotlin side
        return nullptr;
    }
    
    void destroyNativeWindow(void* window) override {
        // Managed by Android framework
    }
    
    void getWindowSize(void* window, int& width, int& height) override {
        if (window) {
            ANativeWindow* nativeWindow = static_cast<ANativeWindow*>(window);
            width = ANativeWindow_getWidth(nativeWindow);
            height = ANativeWindow_getHeight(nativeWindow);
        }
    }
    
    bool isHighDPI() const override {
        return true;  // Most Android devices are high-DPI
    }
    
    float getDPIScale() const override {
        // Typically retrieved from DisplayMetrics in Java
        return 2.0f;  // Placeholder
    }
    
    int getCPUCoreCount() const override {
        return sysconf(_SC_NPROCESSORS_ONLN);
    }
    
    uint64_t getPhysicalMemory() const override {
        struct sysinfo info;
        sysinfo(&info);
        return info.totalram * info.mem_unit;
    }
    
    double getTime() const override {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec + ts.tv_nsec / 1000000000.0;
    }
    
    void sleep(uint32_t milliseconds) const override {
        usleep(milliseconds * 1000);
    }
    
    bool isKeyPressed(int keyCode) const override {
        return false;  // Touch-based input
    }
    
    void getMousePosition(int& x, int& y) const override {
        x = y = 0;  // Touch events handled differently
    }
};

IPlatform* createPlatform() {
    return new AndroidPlatform();
}

}  // namespace myproject::platform

#endif  // __ANDROID__
```

---

## 3. 渲染后端抽象

Filament 已经内置了渲染后端抽象，但我们可以进一步封装以适应项目需求。

### 3.1 后端选择

```cpp
// 根据平台自动选择最佳后端
filament::Engine::Backend selectBackend() {
#if defined(__ANDROID__)
    // Android 优先 Vulkan，回退 OpenGL ES
    if (isVulkanSupported()) {
        return filament::Engine::Backend::VULKAN;
    } else {
        return filament::Engine::Backend::OPENGL;
    }
    
#elif defined(__APPLE__)
    #if defined(__IPHONE_OS_VERSION_MIN_REQUIRED)
        // iOS: Metal only
        return filament::Engine::Backend::METAL;
    #else
        // macOS: Metal preferred, fallback to OpenGL
        return filament::Engine::Backend::METAL;
    #endif
    
#elif defined(_WIN32)
    // Windows: Vulkan preferred, fallback to D3D12/OpenGL
    if (isVulkanSupported()) {
        return filament::Engine::Backend::VULKAN;
    } else if (isD3D12Supported()) {
        return filament::Engine::Backend::D3D12;
    } else {
        return filament::Engine::Backend::OPENGL;
    }
    
#elif defined(__EMSCRIPTEN__)
    // WebGL
    return filament::Engine::Backend::OPENGL;
    
#else
    // Linux: Vulkan preferred, fallback to OpenGL
    if (isVulkanSupported()) {
        return filament::Engine::Backend::VULKAN;
    } else {
        return filament::Engine::Backend::OPENGL;
    }
#endif
}

// 创建引擎
auto backend = selectBackend();
auto engine = filament::Engine::create(backend);
```

### 3.2 SwapChain 创建

```cpp
// 跨平台 SwapChain 创建
filament::SwapChain* createSwapChain(filament::Engine* engine, void* nativeWindow) {
#if defined(_WIN32)
    // Windows: HWND
    return engine->createSwapChain(nativeWindow);
    
#elif defined(__APPLE__)
    #if defined(__IPHONE_OS_VERSION_MIN_REQUIRED)
        // iOS: CAMetalLayer
        return engine->createSwapChain(nativeWindow, 
            filament::SwapChain::CONFIG_HAS_STENCIL_BUFFER);
    #else
        // macOS: NSView/CAMetalLayer
        return engine->createSwapChain(nativeWindow,
            filament::SwapChain::CONFIG_TRANSPARENT);
    #endif
    
#elif defined(__ANDROID__)
    // Android: ANativeWindow
    return engine->createSwapChain(nativeWindow);
    
#elif defined(__EMSCRIPTEN__)
    // WebGL: Canvas
    return engine->createSwapChain(nativeWindow);
    
#else
    // Linux: X11 Window
    return engine->createSwapChain(nativeWindow);
#endif
}
```

---

## 4. 文件系统抽象

### 4.1 资源路径管理

```cpp
class ResourcePath {
public:
    static void initialize(IPlatform* platform) {
        sPlatform = platform;
        
#if defined(__ANDROID__)
        // Android: APK assets
        sAssetRoot = "";  // Accessed via AssetManager
        
#elif defined(__APPLE__)
        // macOS/iOS: App bundle
        NSBundle* mainBundle = [NSBundle mainBundle];
        NSString* resourcePath = [mainBundle resourcePath];
        sAssetRoot = std::string([resourcePath UTF8String]) + "/";
        
#elif defined(__EMSCRIPTEN__)
        // WebGL: Relative to HTML file
        sAssetRoot = "assets/";
        
#else
        // Desktop: Relative to executable
        sAssetRoot = sPlatform->getExecutablePath() + "/../assets/";
#endif
    }
    
    static std::string resolve(const std::string& relativePath) {
        return sPlatform->normalizePath(sAssetRoot + relativePath);
    }
    
private:
    static IPlatform* sPlatform;
    static std::string sAssetRoot;
};

// 使用
std::string modelPath = ResourcePath::resolve("models/car.glb");
```

### 4.2 Android AssetManager 集成

```cpp
#if defined(__ANDROID__)

#include <android/asset_manager.h>

class AndroidAssetLoader {
public:
    static void setAssetManager(AAssetManager* mgr) {
        sAssetManager = mgr;
    }
    
    static std::vector<uint8_t> loadAsset(const std::string& path) {
        AAsset* asset = AAssetManager_open(sAssetManager, path.c_str(), AASSET_MODE_BUFFER);
        if (!asset) {
            LOG_ERROR("Failed to open asset: %s", path.c_str());
            return {};
        }
        
        size_t size = AAsset_getLength(asset);
        std::vector<uint8_t> buffer(size);
        AAsset_read(asset, buffer.data(), size);
        AAsset_close(asset);
        
        return buffer;
    }
    
private:
    static AAssetManager* sAssetManager;
};

// JNI 初始化
extern "C" JNIEXPORT void JNICALL
Java_com_mycompany_myapp_MainActivity_nativeSetAssetManager(
    JNIEnv* env, jobject, jobject assetManager) {
    
    AAssetManager* mgr = AAssetManager_fromJava(env, assetManager);
    AndroidAssetLoader::setAssetManager(mgr);
}

#endif
```

---

## 5. 输入系统抽象

### 5.1 输入事件

```cpp
enum class InputEventType {
    KeyDown,
    KeyUp,
    MouseMove,
    MouseButtonDown,
    MouseButtonUp,
    TouchDown,
    TouchMove,
    TouchUp
};

struct InputEvent {
    InputEventType type;
    int keyCode;
    int x, y;
    int touchId;
};

class IInputHandler {
public:
    virtual ~IInputHandler() = default;
    virtual void handleEvent(const InputEvent& event) = 0;
};

class InputManager {
public:
    void registerHandler(IInputHandler* handler) {
        mHandlers.push_back(handler);
    }
    
    void dispatchEvent(const InputEvent& event) {
        for (auto* handler : mHandlers) {
            handler->handleEvent(event);
        }
    }
    
private:
    std::vector<IInputHandler*> mHandlers;
};
```

### 5.2 平台输入集成

**Windows**：
```cpp
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    InputEvent event;
    
    switch (msg) {
        case WM_KEYDOWN:
            event.type = InputEventType::KeyDown;
            event.keyCode = static_cast<int>(wParam);
            gInputManager->dispatchEvent(event);
            break;
            
        case WM_MOUSEMOVE:
            event.type = InputEventType::MouseMove;
            event.x = LOWORD(lParam);
            event.y = HIWORD(lParam);
            gInputManager->dispatchEvent(event);
            break;
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
```

**Android (JNI)**：
```java
// MainActivity.java
@Override
public boolean onTouchEvent(MotionEvent event) {
    int action = event.getActionMasked();
    int x = (int) event.getX();
    int y = (int) event.getY();
    
    switch (action) {
        case MotionEvent.ACTION_DOWN:
            nativeOnTouchDown(x, y);
            break;
        case MotionEvent.ACTION_MOVE:
            nativeOnTouchMove(x, y);
            break;
        case MotionEvent.ACTION_UP:
            nativeOnTouchUp(x, y);
            break;
    }
    
    return true;
}

private native void nativeOnTouchDown(int x, int y);
```

```cpp
// Native
extern "C" JNIEXPORT void JNICALL
Java_com_mycompany_myapp_MainActivity_nativeOnTouchDown(
    JNIEnv*, jobject, jint x, jint y) {
    
    InputEvent event;
    event.type = InputEventType::TouchDown;
    event.x = x;
    event.y = y;
    gInputManager->dispatchEvent(event);
}
```

---

## 6. 构建系统

### 6.1 CMake 跨平台配置

```cmake
# CMakeLists.txt

cmake_minimum_required(VERSION 3.19)
project(MyFilamentApp)

# 检测平台
if(WIN32)
    set(PLATFORM_NAME "Windows")
    add_definitions(-DPLATFORM_WINDOWS)
elseif(APPLE)
    if(IOS)
        set(PLATFORM_NAME "iOS")
        add_definitions(-DPLATFORM_IOS)
    else()
        set(PLATFORM_NAME "macOS")
        add_definitions(-DPLATFORM_MACOS)
    endif()
elseif(ANDROID)
    set(PLATFORM_NAME "Android")
    add_definitions(-DPLATFORM_ANDROID)
elseif(EMSCRIPTEN)
    set(PLATFORM_NAME "Web")
    add_definitions(-DPLATFORM_WEB)
elseif(UNIX)
    set(PLATFORM_NAME "Linux")
    add_definitions(-DPLATFORM_LINUX)
endif()

message(STATUS "Building for platform: ${PLATFORM_NAME}")

# 平台特定源文件
if(WIN32)
    set(PLATFORM_SOURCES src/platform/WindowsPlatform.cpp)
elseif(APPLE)
    if(IOS)
        set(PLATFORM_SOURCES src/platform/iOSPlatform.mm)
    else()
        set(PLATFORM_SOURCES src/platform/MacOSPlatform.mm)
    endif()
elseif(ANDROID)
    set(PLATFORM_SOURCES src/platform/AndroidPlatform.cpp)
elseif(EMSCRIPTEN)
    set(PLATFORM_SOURCES src/platform/WebPlatform.cpp)
else()
    set(PLATFORM_SOURCES src/platform/LinuxPlatform.cpp)
endif()

# 创建可执行文件
add_executable(MyApp
    src/main.cpp
    ${PLATFORM_SOURCES}
)

# 平台特定链接库
if(WIN32)
    target_link_libraries(MyApp PRIVATE user32 gdi32)
elseif(APPLE)
    target_link_libraries(MyApp PRIVATE "-framework Cocoa" "-framework Metal")
elseif(ANDROID)
    target_link_libraries(MyApp PRIVATE log android)
endif()

# Emscripten 特定设置
if(EMSCRIPTEN)
    set_target_properties(MyApp PROPERTIES
        SUFFIX ".html"
        LINK_FLAGS "-s USE_WEBGL2=1 -s ALLOW_MEMORY_GROWTH=1"
    )
endif()
```

### 6.2 Android Gradle 集成

```gradle
// app/build.gradle

android {
    compileSdk 33
    ndkVersion "25.1.8937393"
    
    defaultConfig {
        applicationId "com.mycompany.myapp"
        minSdk 24
        targetSdk 33
        
        externalNativeBuild {
            cmake {
                arguments "-DANDROID_STL=c++_shared"
                cppFlags "-std=c++17"
            }
        }
    }
    
    externalNativeBuild {
        cmake {
            path "src/main/cpp/CMakeLists.txt"
        }
    }
}

dependencies {
    implementation 'com.google.android.filament:filament-android:1.40.0'
}
```

---

## 7. 性能优化

### 7.1 平台特性检测

```cpp
struct PlatformCapabilities {
    bool supportsCompute;
    bool supportsRayTracing;
    bool supportsAsyncCompute;
    bool supportsMemorylessRenderTarget;  // Metal
    int maxTextureSize;
    int maxMSAA;
};

PlatformCapabilities detectCapabilities(filament::Engine* engine) {
    PlatformCapabilities caps{};
    
    auto backend = engine->getBackend();
    
    switch (backend) {
        case filament::Engine::Backend::VULKAN:
            caps.supportsCompute = true;
            caps.supportsAsyncCompute = true;
            caps.maxTextureSize = 16384;
            caps.maxMSAA = 8;
            break;
            
        case filament::Engine::Backend::METAL:
            caps.supportsCompute = true;
            caps.supportsMemorylessRenderTarget = true;  // iOS
            caps.maxTextureSize = 16384;
            caps.maxMSAA = 8;
            break;
            
        case filament::Engine::Backend::OPENGL:
            caps.supportsCompute = false;  // Depends on GL version
            caps.maxTextureSize = 8192;
            caps.maxMSAA = 4;
            break;
    }
    
    return caps;
}

// 根据能力调整设置
void configureQuality(const PlatformCapabilities& caps) {
    if (caps.supportsAsyncCompute) {
        // Enable async compute for shadows
        view->setShadowingEnabled(true);
    }
    
    if (caps.supportsMemorylessRenderTarget) {
        // Use memoryless render targets on iOS
        // ...
    }
}
```

### 7.2 平台优化

**iOS (Metal memoryless)**：
```cpp
#if defined(__IPHONE_OS_VERSION_MIN_REQUIRED)
    // 使用 memoryless render targets 节省带宽
    // (Filament 内部支持，无需额外代码)
#endif
```

**Android (GLES 优化)**：
```cpp
#if defined(__ANDROID__)
    // 减少 Draw Call
    view->setDynamicResolutionOptions({
        .enabled = true,
        .minScale = {0.5f, 0.5f},
        .maxScale = {1.0f, 1.0f},
        .quality = View::QualityLevel::MEDIUM
    });
    
    // 降低阴影质量
    view->setShadowType(View::ShadowType::PCF);
#endif
```

---

## 8. 调试和测试

### 8.1 平台特定调试

```cpp
void enablePlatformDebugging() {
#if defined(_WIN32)
    // Windows: Enable D3D12 debug layer
    #if defined(_DEBUG)
        // Enable D3D12 GPU-based validation
    #endif
    
#elif defined(__APPLE__)
    // macOS/iOS: Enable Metal validation
    #if defined(DEBUG)
        setenv("MTL_DEBUG_LAYER", "1", 1);
        setenv("MTL_SHADER_VALIDATION", "1", 1);
    #endif
    
#elif defined(__ANDROID__)
    // Android: Enable Vulkan validation layers
    #if defined(DEBUG)
        // VK_LAYER_KHRONOS_validation
    #endif
#endif
}
```

### 8.2 自动化测试

```cmake
# 跨平台测试
enable_testing()

add_test(NAME UnitTests COMMAND MyAppTests)

# 平台特定测试
if(ANDROID)
    # 使用 adb 运行 Android 测试
    add_test(NAME AndroidTests
        COMMAND adb shell am instrument -w com.mycompany.myapp.test/androidx.test.runner.AndroidJUnitRunner
    )
endif()
```

---

## 9. 最佳实践

1. **尽早抽象**：在项目初期就设计平台抽象层
2. **最小条件编译**：90% 代码应该是平台无关的
3. **利用平台特性**：不要为了统一而牺牲性能
4. **持续集成**：所有平台的 CI/CD pipeline
5. **真机测试**：模拟器无法完全代表真实设备

---

## 10. 常见问题

### Q1: 如何处理不同平台的着色器？

**A**: Filament 使用 `matc` 编译材质为所有平台的着色器，无需手动处理。

### Q2: Android 和 iOS 性能差异大？

**A**: 是的，Android 设备碎片化严重，需要动态调整质量设置。iOS 设备更统一，但要利用 Metal 特性（如 memoryless）。

### Q3: WebGL 性能如何？

**A**: WebGL 2.0 性能接近 OpenGL ES 3.0，但受浏览器和 JavaScript 限制。使用 WebAssembly 和 OffscreenCanvas 可提升性能。

---

## 11. 相关文档

- [05-large-project-structure.md](./05-large-project-structure.md) - 大型项目架构
- [02-multithreading-design.md](./02-multithreading-design.md) - 多线程设计

---

## 12. 总结

跨平台架构的成功取决于：

1. **清晰的抽象层**：IPlatform、IWindow、IInput
2. **最小条件编译**：集中在底层，上层代码通用
3. **充分利用平台特性**：Metal memoryless、Vulkan async compute
4. **统一构建系统**：CMake + 平台特定工具（Gradle、Xcode）
5. **持续测试**：所有平台的 CI/CD

通过本文档的架构设计，您可以构建一个健壮、高效的跨平台 Filament 应用，覆盖桌面、移动和 Web 全平台。

**关键技术**：
- 平台抽象接口
- 渲染后端自动选择
- 文件系统路径归一化
- 输入事件统一处理
- CMake 跨平台构建

跨平台不是妥协，而是机会——一次开发，处处运行，触达最广泛的用户。
