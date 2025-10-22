# Windows 桌面集成

## 概述

Filament 在 Windows 平台上支持 **Vulkan** 和 **OpenGL** 图形 API,提供高性能的桌面渲染能力。本文档详细介绍如何在 Windows 应用中集成 Filament,包括 Visual Studio 项目配置、窗口系统集成 (GLFW/Win32)、以及完整的示例代码。

**主要内容**:
- ✅ Visual Studio 项目配置
- ✅ CMake 构建系统集成
- ✅ GLFW 窗口创建和管理
- ✅ Win32 原生窗口集成
- ✅ Vulkan/OpenGL Backend 选择
- ✅ SwapChain 创建和渲染循环
- ✅ HDR 和多显示器支持
- ✅ 性能优化和调试技巧

**适用系统**:
- Windows 7 SP1+ (最低要求)
- Windows 10/11 (推荐)
- Windows Server 2016+

**前置知识**:
- C/C++ 编程基础
- CMake 或 Visual Studio 项目管理
- Windows API 基础 (可选)

## 环境配置

### 系统要求

```powershell
# Windows 版本
Windows 10 版本 1809+ (推荐)
Windows 7 SP1 (最低要求)

# 编译器
Visual Studio 2019+ (MSVC v142+)
或 Clang for Windows

# CMake
CMake 3.19+

# 图形驱动
- NVIDIA: Driver 456.71+
- AMD: Adrenalin 20.11.2+
- Intel: Driver 27.20.100.8681+
```

### 安装 Visual Studio

**推荐组件**:

```
Visual Studio 2022 Community/Professional/Enterprise

必需工作负载:
✅ 使用 C++ 的桌面开发
✅ Windows 10/11 SDK (10.0.19041.0+)

可选组件:
□ CMake 工具
□ Clang 编译器 (for Windows)
□ C++ 分析工具
```

**命令行安装**:

```powershell
# 使用 winget 安装
winget install Microsoft.VisualStudio.2022.Community `
    --override "--add Microsoft.VisualStudio.Workload.NativeDesktop"

# 或使用 Chocolatey
choco install visualstudio2022community --params "--add Microsoft.VisualStudio.Workload.NativeDesktop"
```

### 安装 CMake

```powershell
# 使用 winget
winget install Kitware.CMake

# 使用 Chocolatey
choco install cmake

# 验证安装
cmake --version
# 输出: cmake version 3.27.0
```

### 安装 Vulkan SDK (推荐)

```powershell
# 下载 LunarG Vulkan SDK
# https://vulkan.lunarg.com/sdk/home#windows

# 使用 Chocolatey
choco install vulkan-sdk

# 设置环境变量
$env:VULKAN_SDK = "C:\VulkanSDK\1.3.268.0"
$env:Path += ";$env:VULKAN_SDK\Bin"

# 验证安装
vulkaninfo --summary
```

### 获取 Filament 库

#### 方式 1: 下载预编译库 (推荐)

```powershell
# 下载 Windows 版本
cd ~\Downloads
Invoke-WebRequest -Uri "https://github.com/google/filament/releases/download/v1.51.5/filament-v1.51.5-windows.tgz" -OutFile "filament-windows.tgz"

# 解压 (需要 7-Zip 或 tar)
tar -xzf filament-windows.tgz

# 移动到项目目录
Move-Item filament ~\MyWindowsApp\third_party\
```

#### 方式 2: 从源码编译

```powershell
# 克隆仓库
git clone https://github.com/google/filament.git
cd filament

# 编译 (使用 build.bat)
.\build.bat release

# 或使用 CMake 手动构建
mkdir out\cmake-release
cd out\cmake-release
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release ..\..
cmake --build . --config Release -j8

# 编译产物
# out\cmake-release\filament\
#   ├── bin\Release\
#   │   ├── filament.dll
#   │   ├── backend.dll
#   │   └── ...
#   ├── lib\Release\
#   │   ├── filament.lib
#   │   ├── backend.lib
#   │   └── ...
```

#### 库文件结构

```
filament-windows/
├── include/                      # 头文件
│   ├── filament/
│   │   ├── Engine.h
│   │   ├── Renderer.h
│   │   ├── Scene.h
│   │   └── ...
│   ├── backend/
│   ├── utils/
│   └── math/
├── lib/
│   └── x86_64/                   # 64位库
│       ├── md/                   # /MD (动态运行时)
│       │   ├── filament.lib
│       │   ├── backend.lib
│       │   └── ...
│       └── mt/                   # /MT (静态运行时)
│           ├── filament.lib
│           └── ...
└── bin/
    └── x86_64/
        ├── filament.dll
        ├── backend.dll
        └── ...
```

## Visual Studio 项目配置

### 创建新项目

**方法 1: 使用 Visual Studio**

```
File -> New -> Project
选择: C++ 空项目
名称: FilamentApp
位置: C:\Projects\FilamentApp
```

**方法 2: 使用 CMake (推荐)**

参见下方 "CMake 集成" 章节。

### 项目属性配置

**包含目录**:

```
Configuration Properties -> C/C++ -> General -> Additional Include Directories

添加:
$(SolutionDir)third_party\filament\include
```

**库目录**:

```
Configuration Properties -> Linker -> General -> Additional Library Directories

添加:
$(SolutionDir)third_party\filament\lib\x86_64\md
```

**链接库**:

```
Configuration Properties -> Linker -> Input -> Additional Dependencies

添加:
filament.lib
backend.lib
utils.lib
filamat.lib         (如需运行时材质编译)
gltfio_core.lib     (如需 glTF)
```

**C++ 语言标准**:

```
Configuration Properties -> C/C++ -> Language -> C++ Language Standard

设置为: ISO C++17 Standard (/std:c++17)
```

**运行时库**:

```
Configuration Properties -> C/C++ -> Code Generation -> Runtime Library

- /MD (多线程 DLL) - 使用 lib/x86_64/md/
- /MT (多线程静态) - 使用 lib/x86_64/mt/
```

**后期生成事件** (复制 DLL):

```
Configuration Properties -> Build Events -> Post-Build Event -> Command Line

xcopy /y /d "$(SolutionDir)third_party\filament\bin\x86_64\*.dll" "$(OutDir)"
```

## CMake 集成

### CMakeLists.txt 配置

**项目根目录 CMakeLists.txt:**

```cmake
cmake_minimum_required(VERSION 3.19)
project(FilamentApp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Filament 路径
set(FILAMENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/filament")

# 添加 Filament 包含目录
include_directories(${FILAMENT_DIR}/include)

# 添加 Filament 库目录
link_directories(${FILAMENT_DIR}/lib/x86_64/md)

# 可执行文件
add_executable(FilamentApp
    src/main.cpp
    src/FilamentApp.cpp
    src/FilamentApp.h
)

# 链接 Filament 库
target_link_libraries(FilamentApp
    filament
    backend
    utils
    filamat
    gltfio_core
)

# Windows 系统库
if(WIN32)
    target_link_libraries(FilamentApp
        opengl32    # OpenGL (如果使用 OpenGL backend)
        gdi32
        user32
    )
endif()

# 复制 DLL 到输出目录
if(WIN32)
    add_custom_command(TARGET FilamentApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${FILAMENT_DIR}/bin/x86_64"
        $<TARGET_FILE_DIR:FilamentApp>
    )
endif()
```

### 生成项目

```powershell
# 生成 Visual Studio 项目
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A x64 ..

# 打开项目
start FilamentApp.sln

# 或直接构建
cmake --build . --config Release
```

## GLFW 窗口集成

### 安装 GLFW

```powershell
# 使用 vcpkg
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg install glfw3:x64-windows

# 或下载预编译库
# https://www.glfw.org/download.html
```

### CMakeLists.txt 添加 GLFW

```cmake
# 查找 GLFW
find_package(glfw3 CONFIG REQUIRED)

# 链接 GLFW
target_link_libraries(FilamentApp
    filament
    backend
    glfw
)
```

### 基本 GLFW 应用

**main.cpp:**

```cpp
#include <GLFW/glfw3.h>

#if defined(_WIN32)
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
#endif

#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/SwapChain.h>
#include <utils/EntityManager.h>

using namespace filament;

class FilamentApp {
public:
    FilamentApp(GLFWwindow* window)
        : m_window(window) {
        setupFilament();
    }

    ~FilamentApp() {
        cleanup();
    }

    void render() {
        if (m_renderer->beginFrame(m_swapChain)) {
            m_renderer->render(m_view);
            m_renderer->endFrame();
        }
    }

    void resize(int width, int height) {
        m_view->setViewport({0, 0, (uint32_t)width, (uint32_t)height});
        m_camera->setProjection(
            Camera::Projection::PERSPECTIVE,
            45.0,
            (double)width / (double)height,
            0.1, 100.0,
            Camera::Fov::VERTICAL
        );
    }

private:
    void setupFilament() {
        // 1. 创建 Engine
        m_engine = Engine::create(Engine::Backend::VULKAN);

        // 2. 获取 native window handle
        void* nativeWindow = glfwGetWin32Window(m_window);

        // 3. 创建 SwapChain
        m_swapChain = m_engine->createSwapChain(nativeWindow);

        // 4. 创建 Renderer
        m_renderer = m_engine->createRenderer();

        // 5. 创建 Scene
        m_scene = m_engine->createScene();

        // 6. 创建 View
        m_view = m_engine->createView();
        m_view->setScene(m_scene);

        // 7. 创建 Camera
        m_cameraEntity = utils::EntityManager::get().create();
        m_camera = m_engine->createCamera(m_cameraEntity);
        m_view->setCamera(m_camera);

        // 8. 设置相机
        int width, height;
        glfwGetWindowSize(m_window, &width, &height);
        resize(width, height);

        m_camera->lookAt({0, 0, 4}, {0, 0, 0}, {0, 1, 0});
    }

    void cleanup() {
        if (m_engine) {
            m_engine->destroy(m_cameraEntity);
            m_engine->destroy(m_view);
            m_engine->destroy(m_scene);
            m_engine->destroy(m_renderer);
            m_engine->destroy(m_swapChain);
            Engine::destroy(&m_engine);
        }
    }

private:
    GLFWwindow* m_window = nullptr;
    Engine* m_engine = nullptr;
    Renderer* m_renderer = nullptr;
    Scene* m_scene = nullptr;
    View* m_view = nullptr;
    Camera* m_camera = nullptr;
    SwapChain* m_swapChain = nullptr;
    utils::Entity m_cameraEntity;
};

// 回调函数
static void errorCallback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto* app = static_cast<FilamentApp*>(glfwGetWindowUserPointer(window));
    if (app) {
        app->resize(width, height);
    }
}

int main() {
    // 初始化 GLFW
    glfwSetErrorCallback(errorCallback);

    if (!glfwInit()) {
        return -1;
    }

    // 窗口提示
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // 不创建 OpenGL context
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // 创建窗口
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Filament App", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    // 创建 Filament 应用
    FilamentApp app(window);
    glfwSetWindowUserPointer(window, &app);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    // 主循环
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        app.render();
    }

    // 清理
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
```

### 编译和运行

```powershell
# 使用 CMake 构建
cmake --build build --config Release

# 运行
.\build\Release\FilamentApp.exe
```

## Win32 原生窗口集成

### 纯 Win32 应用

**Win32Main.cpp:**

```cpp
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/SwapChain.h>
#include <utils/EntityManager.h>

using namespace filament;

class FilamentWin32App {
public:
    FilamentWin32App(HWND hwnd) : m_hwnd(hwnd) {
        setupFilament();
    }

    ~FilamentWin32App() {
        cleanup();
    }

    void render() {
        if (m_renderer->beginFrame(m_swapChain)) {
            m_renderer->render(m_view);
            m_renderer->endFrame();
        }
    }

    void resize(int width, int height) {
        if (width == 0 || height == 0) return;

        m_view->setViewport({0, 0, (uint32_t)width, (uint32_t)height});
        m_camera->setProjection(
            Camera::Projection::PERSPECTIVE,
            45.0,
            (double)width / (double)height,
            0.1, 100.0,
            Camera::Fov::VERTICAL
        );
    }

private:
    void setupFilament() {
        // 创建 Engine
        m_engine = Engine::create(Engine::Backend::VULKAN);

        // 创建 SwapChain (传递 HWND)
        m_swapChain = m_engine->createSwapChain((void*)m_hwnd);

        // 创建其他组件
        m_renderer = m_engine->createRenderer();
        m_scene = m_engine->createScene();
        m_view = m_engine->createView();
        m_view->setScene(m_scene);

        m_cameraEntity = utils::EntityManager::get().create();
        m_camera = m_engine->createCamera(m_cameraEntity);
        m_view->setCamera(m_camera);

        // 初始化相机
        RECT rect;
        GetClientRect(m_hwnd, &rect);
        resize(rect.right - rect.left, rect.bottom - rect.top);

        m_camera->lookAt({0, 0, 4}, {0, 0, 0}, {0, 1, 0});
    }

    void cleanup() {
        if (m_engine) {
            m_engine->destroy(m_cameraEntity);
            m_engine->destroy(m_view);
            m_engine->destroy(m_scene);
            m_engine->destroy(m_renderer);
            m_engine->destroy(m_swapChain);
            Engine::destroy(&m_engine);
        }
    }

private:
    HWND m_hwnd = nullptr;
    Engine* m_engine = nullptr;
    Renderer* m_renderer = nullptr;
    Scene* m_scene = nullptr;
    View* m_view = nullptr;
    Camera* m_camera = nullptr;
    SwapChain* m_swapChain = nullptr;
    utils::Entity m_cameraEntity;
};

// 全局变量
static FilamentWin32App* g_app = nullptr;

// 窗口过程
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_app = new FilamentWin32App(hwnd);
        return 0;

    case WM_SIZE: {
        UINT width = LOWORD(lParam);
        UINT height = HIWORD(lParam);
        if (g_app) {
            g_app->resize(width, height);
        }
        return 0;
    }

    case WM_PAINT: {
        if (g_app) {
            g_app->render();
        }
        ValidateRect(hwnd, nullptr);
        return 0;
    }

    case WM_DESTROY:
        delete g_app;
        g_app = nullptr;
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {

    // 注册窗口类
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"FilamentWindowClass";

    if (!RegisterClassEx(&wc)) {
        return -1;
    }

    // 创建窗口
    HWND hwnd = CreateWindowEx(
        0,
        L"FilamentWindowClass",
        L"Filament Win32 App",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1280, 720,
        nullptr, nullptr,
        hInstance, nullptr
    );

    if (!hwnd) {
        return -1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // 消息循环
    MSG msg = {};
    while (true) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                return (int)msg.wParam;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // 渲染
        if (g_app) {
            g_app->render();
        }
    }

    return 0;
}
```

## Backend 选择

### Vulkan vs OpenGL

```cpp
// 方式 1: 自动选择最佳 backend
Engine* createBestEngine() {
    // 优先尝试 Vulkan
    Engine* engine = Engine::create(Engine::Backend::VULKAN);

    if (!engine) {
        // 回退到 OpenGL
        engine = Engine::create(Engine::Backend::OPENGL);
    }

    return engine;
}

// 方式 2: 运行时选择
Engine* createEngineFromConfig(const char* backendName) {
    if (strcmp(backendName, "vulkan") == 0) {
        return Engine::create(Engine::Backend::VULKAN);
    } else if (strcmp(backendName, "opengl") == 0) {
        return Engine::create(Engine::Backend::OPENGL);
    }
    return nullptr;
}

// 方式 3: 检测 GPU 能力
Engine* createEngineBasedOnGPU() {
    // 检查 Vulkan 支持
    // (需要 Vulkan SDK)
    #ifdef VULKAN_SDK_AVAILABLE
        VkInstance instance;
        VkInstanceCreateInfo createInfo = {};
        if (vkCreateInstance(&createInfo, nullptr, &instance) == VK_SUCCESS) {
            vkDestroyInstance(instance, nullptr);
            return Engine::create(Engine::Backend::VULKAN);
        }
    #endif

    // 回退到 OpenGL
    return Engine::create(Engine::Backend::OPENGL);
}
```

### 性能对比

**测试场景**: 100万三角形, 1080p, RTX 3060

| Backend | FPS | CPU 占用 | GPU 占用 | Draw Call Overhead |
|---------|-----|----------|----------|--------------------|
| Vulkan  | 300+ | 15% | 60% | 低 |
| OpenGL  | 220+ | 28% | 65% | 高 |

**推荐策略**:
- **Vulkan**: Windows 10+ with 现代 GPU (GTX 900+, RX 400+)
- **OpenGL**: 兼容性优先,旧硬件

## 高级特性

### HDR 支持

```cpp
// 检查 HDR 支持
bool isHDRSupported(HWND hwnd) {
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFOEX info = {};
    info.cbSize = sizeof(MONITORINFOEX);
    GetMonitorInfo(monitor, &info);

    DISPLAYCONFIG_PATH_INFO path;
    DISPLAYCONFIG_MODE_INFO mode;
    // ... 查询 HDR 能力 (需要 Windows 10 1709+)
    return false;  // 简化示例
}

// 启用 HDR SwapChain
SwapChain* createHDRSwapChain(Engine* engine, HWND hwnd) {
    SwapChain* swapChain = engine->createSwapChain(
        (void*)hwnd,
        SwapChain::CONFIG_READABLE | SwapChain::CONFIG_ENABLE_HDR
    );

    return swapChain;
}
```

### 多显示器支持

```cpp
// 获取窗口所在的显示器
HMONITOR getCurrentMonitor(HWND hwnd) {
    return MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
}

// 枚举所有显示器
BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor,
                              LPRECT lprcMonitor, LPARAM dwData) {
    MONITORINFOEX info = {};
    info.cbSize = sizeof(MONITORINFOEX);
    GetMonitorInfo(hMonitor, &info);

    printf("Monitor: %ls\n", info.szDevice);
    printf("  Rect: (%d, %d) - (%d, %d)\n",
        info.rcMonitor.left, info.rcMonitor.top,
        info.rcMonitor.right, info.rcMonitor.bottom);

    return TRUE;
}

EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, 0);
```

### 全屏模式

```cpp
// 切换全屏
void toggleFullscreen(HWND hwnd) {
    static WINDOWPLACEMENT g_wpPrev = { sizeof(g_wpPrev) };
    static bool g_isFullscreen = false;

    DWORD dwStyle = GetWindowLong(hwnd, GWL_STYLE);

    if (!g_isFullscreen) {
        // 进入全屏
        MONITORINFO mi = { sizeof(mi) };
        if (GetWindowPlacement(hwnd, &g_wpPrev) &&
            GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {

            SetWindowLong(hwnd, GWL_STYLE, dwStyle & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hwnd, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
        g_isFullscreen = true;
    } else {
        // 退出全屏
        SetWindowLong(hwnd, GWL_STYLE, dwStyle | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &g_wpPrev);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_isFullscreen = false;
    }
}
```

## 性能优化

### 垂直同步 (VSync)

```cpp
// 禁用 VSync (最大帧率)
Renderer::DisplayInfo displayInfo;
displayInfo.presentationDeadline = 0;  // 不等待 VSync
m_renderer->setDisplayInfo(displayInfo);

// 启用 VSync (60Hz)
displayInfo.presentationDeadline = 16666667;  // ~60 FPS (纳秒)
m_renderer->setDisplayInfo(displayInfo);
```

### 帧率限制

```cpp
#include <chrono>
#include <thread>

class FrameRateLimiter {
public:
    FrameRateLimiter(double targetFPS) {
        m_targetFrameTime = std::chrono::duration<double>(1.0 / targetFPS);
    }

    void beginFrame() {
        m_frameStart = std::chrono::high_resolution_clock::now();
    }

    void endFrame() {
        auto frameEnd = std::chrono::high_resolution_clock::now();
        auto elapsed = frameEnd - m_frameStart;

        if (elapsed < m_targetFrameTime) {
            std::this_thread::sleep_for(m_targetFrameTime - elapsed);
        }
    }

private:
    std::chrono::duration<double> m_targetFrameTime;
    std::chrono::high_resolution_clock::time_point m_frameStart;
};

// 使用
FrameRateLimiter limiter(60.0);  // 60 FPS

while (!glfwWindowShouldClose(window)) {
    limiter.beginFrame();

    glfwPollEvents();
    app.render();

    limiter.endFrame();
}
```

### 多线程优化

```cpp
#include <thread>

// 启用 Filament 后台线程
Engine::Config config;
config.jobSystemThreadCount = std::thread::hardware_concurrency() - 1;
Engine* engine = Engine::create(Engine::Backend::VULKAN, nullptr, &config);
```

### GPU 性能监控

```cpp
// 使用 Vulkan 查询性能计数器
#include <backend/platforms/VulkanPlatform.h>

// (需要 Vulkan SDK)
// 查询 GPU 时间戳
```

## 调试技巧

### RenderDoc 集成

```cpp
// 1. 下载 RenderDoc: https://renderdoc.org/

// 2. 启用 RenderDoc API
#include <renderdoc_app.h>

RENDERDOC_API_1_1_2* rdoc_api = nullptr;

void initRenderDoc() {
    HMODULE mod = GetModuleHandleA("renderdoc.dll");
    if (mod) {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI =
            (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
        RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void**)&rdoc_api);
    }
}

// 3. 捕获帧
if (rdoc_api) {
    rdoc_api->StartFrameCapture(nullptr, nullptr);
    app.render();
    rdoc_api->EndFrameCapture(nullptr, nullptr);
}
```

### Visual Studio Graphics Debugger

```cpp
// 1. 在 Visual Studio 中:
//    调试 -> 图形 -> 启动图形调试

// 2. 运行应用后按 Print Screen 捕获帧

// 3. 分析:
//    - 事件列表
//    - 管道阶段
//    - 像素历史
```

### 性能分析

```powershell
# Windows Performance Analyzer
wpr -start GeneralProfile -filemode

# 运行应用
.\FilamentApp.exe

# 停止记录
wpr -stop trace.etl

# 分析
wpa trace.etl
```

## 常见问题

### Q1: Vulkan 初始化失败

**问题:**
```
Engine::create(Backend::VULKAN) returns nullptr
```

**解决方案:**

```cpp
// 1. 检查 Vulkan SDK 安装
// 2. 更新显卡驱动
// 3. 检查 VK_LAYER_PATH 环境变量

// 回退到 OpenGL
Engine* engine = Engine::create(Engine::Backend::VULKAN);
if (!engine) {
    printf("Vulkan not available, falling back to OpenGL\n");
    engine = Engine::create(Engine::Backend::OPENGL);
}
```

### Q2: SwapChain 创建失败

**问题:**
```
createSwapChain() returns nullptr
```

**解决方案:**

```cpp
// 确保窗口句柄有效
if (!IsWindow(hwnd)) {
    // 窗口无效
}

// 使用正确的窗口句柄
void* nativeWindow = (void*)hwnd;  // Win32
// 或
void* nativeWindow = glfwGetWin32Window(window);  // GLFW
```

### Q3: 渲染黑屏

**可能原因:**

1. **相机未设置**
```cpp
m_camera->lookAt({0, 0, 4}, {0, 0, 0}, {0, 1, 0});
```

2. **Viewport 未设置**
```cpp
m_view->setViewport({0, 0, width, height});
```

3. **Scene 为空**
```cpp
// 添加光源和模型
```

### Q4: DLL 缺失错误

**问题:**
```
系统找不到 filament.dll
```

**解决方案:**

```powershell
# 方法 1: 复制 DLL 到 exe 目录
xcopy third_party\filament\bin\x86_64\*.dll build\Release\

# 方法 2: 添加到 PATH
$env:Path += ";C:\Projects\FilamentApp\third_party\filament\bin\x86_64"

# 方法 3: 使用 CMake 自动复制 (见上文 CMakeLists.txt)
```

### Q5: 链接错误

**问题:**
```
unresolved external symbol "Engine::create"
```

**解决方案:**

```
1. 检查库目录是否正确
2. 确保链接了所有必需的 .lib 文件
3. 检查运行时库设置 (/MD vs /MT)
4. 确保库和应用使用相同的编译器版本
```

## 最佳实践

### 1. 错误处理

```cpp
Engine* engine = Engine::create(Engine::Backend::VULKAN);
if (!engine) {
    MessageBox(nullptr, L"Failed to create Filament Engine",
               L"Error", MB_ICONERROR);
    return -1;
}
```

### 2. 资源管理 (RAII)

```cpp
class FilamentContext {
public:
    FilamentContext() {
        m_engine = Engine::create(Engine::Backend::VULKAN);
    }

    ~FilamentContext() {
        cleanup();
    }

    // 禁用拷贝
    FilamentContext(const FilamentContext&) = delete;
    FilamentContext& operator=(const FilamentContext&) = delete;

private:
    void cleanup() {
        if (m_engine) {
            Engine::destroy(&m_engine);
        }
    }

    Engine* m_engine = nullptr;
};
```

### 3. 窗口大小变化处理

```cpp
// 避免在 resize 时频繁重建资源
void onResize(int width, int height) {
    if (width == 0 || height == 0) return;  // 最小化时跳过

    // 只更新 viewport 和相机投影
    m_view->setViewport({0, 0, (uint32_t)width, (uint32_t)height});
    m_camera->setProjection(...);
}
```

### 4. DPI 感知

```cpp
// 启用 DPI 感知 (Windows 10+)
SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

// 获取窗口 DPI
UINT dpi = GetDpiForWindow(hwnd);
float scaleFactor = dpi / 96.0f;
```

## 相关文档

- [01-platform-overview.md](./01-platform-overview.md) - 平台支持概览
- [08-swapchain-创建.md](./08-swapchain-创建.md) - SwapChain 详解
- [09-应用生命周期管理.md](./09-应用生命周期管理.md) - 生命周期管理
- [../backend/01-vulkan-backend.md](../backend/01-vulkan-backend.md) - Vulkan Backend
- [../backend/02-opengl-backend.md](../backend/02-opengl-backend.md) - OpenGL Backend
- [../engine/01-engine-initialization.md](../engine/01-engine-initialization.md) - Engine 初始化
