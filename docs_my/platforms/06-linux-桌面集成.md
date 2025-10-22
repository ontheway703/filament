# Linux 桌面集成

## 概述

Filament 在 Linux 平台上支持 **Vulkan** 和 **OpenGL** 图形 API,提供高性能的桌面渲染能力。本文档详细介绍如何在 Linux 应用中集成 Filament,包括 CMake/Makefile 构建配置、X11/Wayland 窗口集成、以及 GLFW/SDL2 跨平台窗口系统使用。

**主要内容**:
- ✅ 开发环境配置 (Ubuntu/Fedora/Arch)
- ✅ CMake 构建系统集成
- ✅ X11 窗口集成
- ✅ Wayland 窗口集成
- ✅ GLFW/SDL2 跨平台窗口支持
- ✅ Vulkan/OpenGL Backend 选择
- ✅ 包管理器集成 (apt/dnf/pacman)
- ✅ 性能优化和调试技巧

**支持发行版**:
- Ubuntu 20.04+ (推荐 22.04+)
- Fedora 35+
- Arch Linux (rolling)
- Debian 11+
- openSUSE Leap 15.4+

**支持架构**:
- x86_64 (主要)
- arm64 (实验性, Raspberry Pi 4+)

## 环境配置

### Ubuntu/Debian

```bash
# 更新包列表
sudo apt update

# 安装必需工具
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    git

# 安装图形库
sudo apt install -y \
    libx11-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev \
    libgl1-mesa-dev \
    libglu1-mesa-dev

# 安装 Vulkan SDK
sudo apt install -y \
    libvulkan-dev \
    vulkan-tools \
    vulkan-validationlayers

# 验证 Vulkan
vulkaninfo --summary

# 安装可选库
sudo apt install -y \
    libglfw3-dev \    # GLFW 窗口库
    libsdl2-dev       # SDL2 窗口库
```

### Fedora

```bash
# 安装开发工具
sudo dnf groupinstall "Development Tools"
sudo dnf install -y cmake ninja-build git

# 安装图形库
sudo dnf install -y \
    libX11-devel \
    libXrandr-devel \
    libXinerama-devel \
    libXcursor-devel \
    libXi-devel \
    mesa-libGL-devel

# 安装 Vulkan
sudo dnf install -y \
    vulkan-devel \
    vulkan-tools \
    vulkan-validation-layers

# 安装 GLFW/SDL2
sudo dnf install -y glfw-devel SDL2-devel
```

### Arch Linux

```bash
# 安装基础工具
sudo pacman -S base-devel cmake ninja git

# 安装图形库
sudo pacman -S \
    libx11 libxrandr libxinerama libxcursor libxi \
    mesa

# 安装 Vulkan
sudo pacman -S \
    vulkan-icd-loader \
    vulkan-headers \
    vulkan-validation-layers \
    vulkan-tools

# 安装 GLFW/SDL2
sudo pacman -S glfw-x11 sdl2
```

### 获取 Filament 库

#### 方式 1: 下载预编译库

```bash
# 下载 Linux 版本
cd ~/Downloads
wget https://github.com/google/filament/releases/download/v1.51.5/filament-v1.51.5-linux.tgz

# 解压
tar -xzf filament-v1.51.5-linux.tgz

# 移动到项目目录
mv filament ~/my-project/third_party/
```

#### 方式 2: 从源码编译

```bash
# 克隆仓库
git clone https://github.com/google/filament.git
cd filament

# 编译 (Release)
./build.sh release

# 或使用 CMake
mkdir out/cmake-release
cd out/cmake-release
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ../..
ninja

# 编译产物
# out/cmake-release/filament/
#   ├── lib/
#   │   ├── x86_64/
#   │   │   ├── libfilament.so
#   │   │   ├── libbackend.so
#   │   │   └── ...
#   ├── include/
#   │   ├── filament/
#   │   └── ...
```

## CMake 项目集成

### 基本 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.19)
project(FilamentLinuxApp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Filament 路径
set(FILAMENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/filament")

# 添加 Filament 包含目录
include_directories(${FILAMENT_DIR}/include)

# 添加 Filament 库目录
link_directories(${FILAMENT_DIR}/lib/x86_64)

# 可执行文件
add_executable(FilamentLinuxApp
    src/main.cpp
)

# 链接 Filament 库
target_link_libraries(FilamentLinuxApp
    filament
    backend
    utils
    filamat
    gltfio_core
)

# 链接系统库
target_link_libraries(FilamentLinuxApp
    GL          # OpenGL (如果使用 OpenGL backend)
    X11         # X11 窗口
    dl          # 动态链接
    pthread     # 多线程
)
```

### 构建项目

```bash
# 创建构建目录
mkdir build
cd build

# 生成构建文件 (使用 Ninja)
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..

# 构建
ninja

# 运行
./FilamentLinuxApp
```

## X11 窗口集成

### 纯 X11 应用

**main_x11.cpp:**

```cpp
#include <X11/Xlib.h>
#include <cstdio>
#include <cstdlib>

#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/SwapChain.h>
#include <utils/EntityManager.h>

using namespace filament;

class FilamentX11App {
public:
    FilamentX11App(Display* display, Window window, int width, int height)
        : m_display(display), m_window(window) {
        setupFilament(width, height);
    }

    ~FilamentX11App() {
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
            45.0, (double)width / (double)height,
            0.1, 100.0, Camera::Fov::VERTICAL
        );
    }

private:
    void setupFilament(int width, int height) {
        // 创建 Engine (Vulkan 优先)
        m_engine = Engine::create(Engine::Backend::VULKAN);
        if (!m_engine) {
            m_engine = Engine::create(Engine::Backend::OPENGL);
        }

        // 创建 SwapChain (传递 X11 Window)
        void* nativeWindow = (void*)(uintptr_t)m_window;
        m_swapChain = m_engine->createSwapChain(nativeWindow, SwapChain::CONFIG_DEFAULT);

        m_renderer = m_engine->createRenderer();
        m_scene = m_engine->createScene();
        m_view = m_engine->createView();
        m_view->setScene(m_scene);

        m_cameraEntity = utils::EntityManager::get().create();
        m_camera = m_engine->createCamera(m_cameraEntity);
        m_view->setCamera(m_camera);

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
    Display* m_display;
    Window m_window;
    Engine* m_engine = nullptr;
    Renderer* m_renderer = nullptr;
    Scene* m_scene = nullptr;
    View* m_view = nullptr;
    Camera* m_camera = nullptr;
    SwapChain* m_swapChain = nullptr;
    utils::Entity m_cameraEntity;
};

int main() {
    // 打开 X11 显示
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        fprintf(stderr, "Cannot open X display\n");
        return -1;
    }

    int screen = DefaultScreen(display);
    Window rootWindow = RootWindow(display, screen);

    // 创建窗口
    int width = 1280, height = 720;
    Window window = XCreateSimpleWindow(
        display, rootWindow,
        0, 0, width, height, 1,
        BlackPixel(display, screen),
        WhitePixel(display, screen)
    );

    // 设置窗口属性
    XStoreName(display, window, "Filament Linux App");
    XSelectInput(display, window, ExposureMask | KeyPressMask | StructureNotifyMask);
    XMapWindow(display, window);

    // 创建 Filament 应用
    FilamentX11App app(display, window, width, height);

    // 事件循环
    XEvent event;
    bool running = true;

    while (running) {
        while (XPending(display)) {
            XNextEvent(display, &event);

            switch (event.type) {
            case Expose:
                app.render();
                break;

            case ConfigureNotify:
                if (event.xconfigure.width != width || event.xconfigure.height != height) {
                    width = event.xconfigure.width;
                    height = event.xconfigure.height;
                    app.resize(width, height);
                }
                break;

            case KeyPress:
                running = false;
                break;
            }
        }

        app.render();
    }

    // 清理
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return 0;
}
```

## GLFW 窗口集成

### GLFW 应用 (推荐)

**main_glfw.cpp:**

```cpp
#include <GLFW/glfw3.h>

#if defined(__linux__)
    #define GLFW_EXPOSE_NATIVE_X11
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

class FilamentGLFWApp {
public:
    FilamentGLFWApp(GLFWwindow* window) : m_window(window) {
        setupFilament();
    }

    ~FilamentGLFWApp() {
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
            45.0, (double)width / (double)height,
            0.1, 100.0, Camera::Fov::VERTICAL
        );
    }

private:
    void setupFilament() {
        // 尝试 Vulkan, 回退到 OpenGL
        m_engine = Engine::create(Engine::Backend::VULKAN);
        if (!m_engine) {
            printf("Vulkan not available, using OpenGL\n");
            m_engine = Engine::create(Engine::Backend::OPENGL);
        }

        // 获取 native window (X11 Window)
        void* nativeWindow = (void*)(uintptr_t)glfwGetX11Window(m_window);
        m_swapChain = m_engine->createSwapChain(nativeWindow);

        m_renderer = m_engine->createRenderer();
        m_scene = m_engine->createScene();
        m_view = m_engine->createView();
        m_view->setScene(m_scene);

        m_cameraEntity = utils::EntityManager::get().create();
        m_camera = m_engine->createCamera(m_cameraEntity);
        m_view->setCamera(m_camera);

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
    GLFWwindow* m_window;
    Engine* m_engine = nullptr;
    Renderer* m_renderer = nullptr;
    Scene* m_scene = nullptr;
    View* m_view = nullptr;
    Camera* m_camera = nullptr;
    SwapChain* m_swapChain = nullptr;
    utils::Entity m_cameraEntity;
};

static void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto* app = static_cast<FilamentGLFWApp*>(glfwGetWindowUserPointer(window));
    if (app) {
        app->resize(width, height);
    }
}

int main() {
    if (!glfwInit()) {
        return -1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Filament Linux", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    FilamentGLFWApp app(window);
    glfwSetWindowUserPointer(window, &app);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        app.render();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
```

### CMakeLists.txt (GLFW)

```cmake
# 查找 GLFW
find_package(glfw3 REQUIRED)

add_executable(FilamentGLFWApp src/main_glfw.cpp)

target_link_libraries(FilamentGLFWApp
    filament backend utils
    glfw
    X11 dl pthread
)
```

## Wayland 支持

### Wayland 窗口集成

**CMakeLists.txt (Wayland):**

```cmake
# 查找 Wayland
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-egl)

add_executable(FilamentWaylandApp src/main_wayland.cpp)

target_include_directories(FilamentWaylandApp PRIVATE ${WAYLAND_INCLUDE_DIRS})
target_link_libraries(FilamentWaylandApp
    filament backend utils
    ${WAYLAND_LIBRARIES}
    EGL
    dl pthread
)
```

**main_wayland.cpp (基础示例):**

```cpp
#include <wayland-client.h>
#include <wayland-egl.h>
#include <filament/Engine.h>
// ... (详细实现参考 Wayland 文档)
```

## Backend 选择策略

### 运行时检测

```cpp
#include <vulkan/vulkan.h>

bool isVulkanAvailable() {
    VkInstance instance;
    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

    if (vkCreateInstance(&createInfo, nullptr, &instance) == VK_SUCCESS) {
        vkDestroyInstance(instance, nullptr);
        return true;
    }
    return false;
}

Engine* createBestEngine() {
    if (isVulkanAvailable()) {
        Engine* engine = Engine::create(Engine::Backend::VULKAN);
        if (engine) {
            printf("Using Vulkan backend\n");
            return engine;
        }
    }

    printf("Using OpenGL backend\n");
    return Engine::create(Engine::Backend::OPENGL);
}
```

### 环境变量控制

```cpp
#include <cstdlib>

Engine* createEngineFromEnv() {
    const char* backend = std::getenv("FILAMENT_BACKEND");

    if (backend) {
        if (strcmp(backend, "vulkan") == 0) {
            return Engine::create(Engine::Backend::VULKAN);
        } else if (strcmp(backend, "opengl") == 0) {
            return Engine::create(Engine::Backend::OPENGL);
        }
    }

    // 默认自动选择
    return createBestEngine();
}
```

```bash
# 使用
FILAMENT_BACKEND=vulkan ./FilamentApp
FILAMENT_BACKEND=opengl ./FilamentApp
```

## 性能优化

### Vulkan 验证层 (调试)

```bash
# 启用 Vulkan 验证层
export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
export VK_LOADER_DEBUG=all

./FilamentApp
```

### 多线程优化

```cpp
#include <thread>

Engine::Config config;
config.jobSystemThreadCount = std::thread::hardware_concurrency() - 1;

Engine* engine = Engine::create(Engine::Backend::VULKAN, nullptr, &config);
```

### 帧率限制

```cpp
#include <chrono>
#include <thread>

void limitFrameRate(double targetFPS) {
    static auto lastFrame = std::chrono::high_resolution_clock::now();

    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = now - lastFrame;

    double targetFrameTime = 1.0 / targetFPS;
    if (elapsed.count() < targetFrameTime) {
        std::this_thread::sleep_for(
            std::chrono::duration<double>(targetFrameTime - elapsed.count())
        );
    }

    lastFrame = std::chrono::high_resolution_clock::now();
}

// 使用
while (!glfwWindowShouldClose(window)) {
    app.render();
    limitFrameRate(60.0);  // 60 FPS
}
```

## 调试技巧

### RenderDoc

```bash
# 安装 RenderDoc
sudo apt install renderdoc  # Ubuntu
sudo dnf install renderdoc  # Fedora

# 启动 RenderDoc
renderdoc &

# 或在命令行捕获
renderdoccmd capture --wait-for-exit ./FilamentApp
```

### gdb 调试

```bash
# 编译 Debug 版本
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug ..
ninja

# 使用 gdb
gdb ./FilamentApp

# gdb 命令
(gdb) break main
(gdb) run
(gdb) backtrace
(gdb) continue
```

### Valgrind 内存检查

```bash
# 安装 Valgrind
sudo apt install valgrind

# 检查内存泄漏
valgrind --leak-check=full --show-leak-kinds=all ./FilamentApp
```

## 包管理器集成

### 创建 Debian 包

**debian/control:**

```
Source: filament-linux-app
Section: graphics
Priority: optional
Build-Depends: debhelper (>= 12), cmake, ninja-build, libx11-dev, libvulkan-dev

Package: filament-linux-app
Architecture: amd64
Depends: ${shlibs:Depends}, ${misc:Depends}, libx11-6, libvulkan1
Description: Filament Linux Application
 A 3D rendering application using Filament engine
```

```bash
# 构建包
dpkg-buildpackage -us -uc

# 安装
sudo dpkg -i ../filament-linux-app_*.deb
```

### Flatpak 打包

**org.example.FilamentApp.yml:**

```yaml
app-id: org.example.FilamentApp
runtime: org.freedesktop.Platform
runtime-version: '23.08'
sdk: org.freedesktop.Sdk
command: filament-app

modules:
  - name: filament-app
    buildsystem: cmake-ninja
    sources:
      - type: git
        url: https://github.com/example/filament-app.git
```

```bash
# 构建 Flatpak
flatpak-builder build-dir org.example.FilamentApp.yml

# 安装
flatpak-builder --install build-dir org.example.FilamentApp.yml
```

## 常见问题

### Q1: Vulkan 初始化失败

**问题:**
```
Failed to create Vulkan instance
```

**解决方案:**

```bash
# 检查 Vulkan 驱动
vulkaninfo

# 如果没有 Vulkan 支持:
# NVIDIA:
sudo apt install nvidia-driver-535

# AMD:
sudo apt install mesa-vulkan-drivers

# Intel:
sudo apt install mesa-vulkan-drivers intel-media-va-driver
```

### Q2: X11 窗口黑屏

**可能原因:**

1. **相机未设置**
```cpp
m_camera->lookAt({0, 0, 4}, {0, 0, 0}, {0, 1, 0});
```

2. **SwapChain 参数错误**
```cpp
// 确保 X11 Window 有效
void* nativeWindow = (void*)(uintptr_t)m_window;
```

### Q3: 共享库加载失败

**问题:**
```
error while loading shared libraries: libfilament.so
```

**解决方案:**

```bash
# 方法 1: 设置 LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/path/to/filament/lib/x86_64:$LD_LIBRARY_PATH

# 方法 2: 添加到 /etc/ld.so.conf
echo "/path/to/filament/lib/x86_64" | sudo tee /etc/ld.so.conf.d/filament.conf
sudo ldconfig

# 方法 3: 使用 RPATH (CMake)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
```

### Q4: Wayland 兼容性

**问题:**
```
X11 not available on Wayland-only system
```

**解决方案:**

```bash
# 使用 XWayland
export GDK_BACKEND=x11

# 或使用原生 Wayland (需要 Wayland 支持)
# 参考 Wayland 集成章节
```

### Q5: 性能问题

**优化检查清单:**

```cpp
// 1. 使用 Vulkan (而非 OpenGL)
Engine::create(Engine::Backend::VULKAN);

// 2. 启用多线程
config.jobSystemThreadCount = std::thread::hardware_concurrency() - 1;

// 3. 禁用 VSync (测试)
// 4. 降低分辨率
// 5. 减少材质复杂度
```

## 最佳实践

### 1. 错误处理

```cpp
Engine* engine = Engine::create(Engine::Backend::VULKAN);
if (!engine) {
    fprintf(stderr, "Failed to create Filament engine\n");
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
        if (m_engine) {
            Engine::destroy(&m_engine);
        }
    }

    FilamentContext(const FilamentContext&) = delete;
    FilamentContext& operator=(const FilamentContext&) = delete;

private:
    Engine* m_engine = nullptr;
};
```

### 3. 信号处理

```cpp
#include <signal.h>

volatile sig_atomic_t g_running = 1;

void signalHandler(int signum) {
    g_running = 0;
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    while (g_running) {
        app.render();
    }

    return 0;
}
```

### 4. 桌面集成

```bash
# 创建 .desktop 文件
cat > ~/.local/share/applications/filament-app.desktop <<EOF
[Desktop Entry]
Name=Filament App
Exec=/usr/local/bin/filament-app
Icon=/usr/local/share/icons/filament-app.png
Type=Application
Categories=Graphics;3DGraphics;
EOF
```

## 相关文档

- [01-platform-overview.md](./01-platform-overview.md) - 平台支持概览
- [04-windows-桌面集成.md](./04-windows-桌面集成.md) - Windows 集成 (类似流程)
- [08-swapchain-创建.md](./08-swapchain-创建.md) - SwapChain 详解
- [09-应用生命周期管理.md](./09-应用生命周期管理.md) - 生命周期管理
- [../backend/01-vulkan-backend.md](../backend/01-vulkan-backend.md) - Vulkan Backend
- [../backend/02-opengl-backend.md](../backend/02-opengl-backend.md) - OpenGL Backend
