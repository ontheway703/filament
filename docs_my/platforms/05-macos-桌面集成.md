# macOS 桌面集成

## 概述

Filament 在 macOS 平台上通过 **Metal** 图形 API 提供高性能的桌面渲染能力。本文档详细介绍如何在 macOS 应用中集成 Filament,包括 Xcode 项目配置、Cocoa/AppKit 窗口集成、SwiftUI 支持,以及 Apple Silicon (M系列) 优化。

**主要内容**:
- ✅ Xcode 项目配置和依赖管理
- ✅ Cocoa/AppKit 窗口集成 (NSView + CAMetalLayer)
- ✅ SwiftUI 集成
- ✅ GLFW 跨平台窗口支持
- ✅ Apple Silicon (M1/M2/M3) 优化
- ✅ Universal Binary 构建
- ✅ 性能优化和调试技巧

**适用系统**:
- macOS 10.13+ (High Sierra, 最低要求)
- macOS 11.0+ (Big Sur, 推荐)
- macOS 14.0+ (Sonoma, 最新)

**支持架构**:
- x86_64 (Intel Mac)
- arm64 (Apple Silicon: M1/M2/M3/M4)
- Universal Binary (x86_64 + arm64)

**前置知识**:
- Objective-C 或 Swift 基础
- Metal 图形 API 基础
- Cocoa/AppKit 或 SwiftUI 框架

## 环境配置

### 系统要求

```bash
# macOS 版本
macOS 11.0+ (Big Sur 或更高)

# Xcode 版本
Xcode 12.0+ (推荐 Xcode 15+)

# Metal SDK
包含在 Xcode 中,无需单独安装

# 架构
arm64 (Apple Silicon)
x86_64 (Intel Mac)
```

### Xcode 安装

```bash
# 方式 1: Mac App Store 安装
# 搜索 "Xcode" 并安装

# 方式 2: 命令行工具
xcode-select --install

# 验证安装
xcodebuild -version
# 输出: Xcode 15.0
#      Build version 15A240d

# 检查 Command Line Tools
xcode-select -p
# 输出: /Applications/Xcode.app/Contents/Developer
```

### 获取 Filament 库

#### 方式 1: 下载预编译库 (推荐)

```bash
# 下载 macOS 版本
cd ~/Downloads
curl -L -O https://github.com/google/filament/releases/download/v1.51.5/filament-v1.51.5-mac.tgz

# 解压
tar -xzf filament-v1.51.5-mac.tgz

# 移动到项目目录
mv filament ~/MyMacApp/Frameworks/
```

#### 方式 2: 从源码编译

```bash
# 克隆仓库
git clone https://github.com/google/filament.git
cd filament

# 编译 macOS 版本 (自动检测架构)
./build.sh release

# 或指定架构编译
# Intel:
./build.sh -p desktop release

# Apple Silicon:
./build.sh -p desktop -a arm64 release

# Universal Binary:
./build.sh -p desktop -a universal release

# 编译产物
# out/cmake-release/filament/
#   ├── lib/
#   │   ├── universal/           # Universal Binary
#   │   │   ├── libfilament.a
#   │   │   ├── libbackend.a
#   │   │   └── ...
#   │   ├── arm64/               # Apple Silicon only
#   │   └── x86_64/              # Intel only
#   ├── include/
#   │   ├── filament/
#   │   ├── backend/
#   │   └── ...
```

#### 库文件结构

```
filament-mac/
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
│   └── universal/                # Universal Binary (推荐)
│       ├── libfilament.a         # 核心引擎
│       ├── libbackend.a          # Metal backend
│       ├── libfilamat.a          # 材质编译器
│       ├── libgltfio_core.a      # glTF 加载器
│       ├── libutils.a            # 工具库
│       └── libmath.a             # 数学库
```

## Xcode 项目配置

### 创建新项目

```bash
# 在 Xcode 中:
# File -> New -> Project
# 选择 macOS -> App
# Interface: Storyboard 或 SwiftUI
# Language: Objective-C 或 Swift
```

### 添加 Filament 库

**步骤 1: 添加 Header Search Paths**

```
Project Settings -> Build Settings -> Search Paths
Header Search Paths: $(PROJECT_DIR)/Frameworks/filament/include
```

**步骤 2: 添加 Library Search Paths**

```
Library Search Paths: $(PROJECT_DIR)/Frameworks/filament/lib/universal
```

**步骤 3: 链接 Filament 库**

```
Build Phases -> Link Binary With Libraries
点击 + 添加以下库:

# Filament 库 (静态库)
- libfilament.a
- libbackend.a
- libutils.a
- libmath.a
- libfilamat.a        (如需运行时材质编译)
- libgltfio_core.a    (如需 glTF 加载)

# 系统框架
- Metal.framework
- MetalKit.framework
- QuartzCore.framework
- Cocoa.framework
- CoreGraphics.framework
```

**步骤 4: C++ 编译选项**

```
Build Settings -> Apple Clang - Language - C++
C++ Language Dialect: GNU++17
C++ Standard Library: libc++
```

**步骤 5: 架构设置**

```
Build Settings -> Architectures
Architectures: Standard Architectures (arm64, x86_64)

# 或只支持 Apple Silicon:
Architectures: arm64
```

## Cocoa/AppKit 集成

### NSView + CAMetalLayer

**FilamentView.h (Objective-C++):**

```objc
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

@interface FilamentView : NSView

@property (nonatomic, readonly) CAMetalLayer* metalLayer;

@end
```

**FilamentView.mm:**

```objc
#import "FilamentView.h"

@implementation FilamentView

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        [self setupMetalLayer];
    }
    return self;
}

- (void)setupMetalLayer {
    // 设置 layer-backed view
    self.wantsLayer = YES;
    self.layer = [CAMetalLayer layer];

    CAMetalLayer* metalLayer = (CAMetalLayer*)self.layer;

    // 获取默认 Metal 设备
    metalLayer.device = MTLCreateSystemDefaultDevice();

    // 像素格式
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;

    // 帧缓冲设置
    metalLayer.framebufferOnly = YES;

    // 显示同步
    metalLayer.displaySyncEnabled = YES;

    // Retina 支持
    metalLayer.contentsScale = [NSScreen mainScreen].backingScaleFactor;

    // 颜色空间
    metalLayer.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
}

- (CAMetalLayer*)metalLayer {
    return (CAMetalLayer*)self.layer;
}

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];

    // 更新 drawable size
    CAMetalLayer* metalLayer = self.metalLayer;
    CGSize drawableSize = self.bounds.size;
    drawableSize.width *= metalLayer.contentsScale;
    drawableSize.height *= metalLayer.contentsScale;
    metalLayer.drawableSize = drawableSize;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

@end
```

### Filament 渲染器封装

**FilamentRenderer.h:**

```objc
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>

@interface FilamentRenderer : NSObject

- (instancetype)initWithMetalLayer:(CAMetalLayer*)layer;
- (void)render;
- (void)resizeWithWidth:(NSInteger)width height:(NSInteger)height;
- (void)cleanup;

@end
```

**FilamentRenderer.mm:**

```objc
#import "FilamentRenderer.h"

#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/SwapChain.h>
#include <filament/LightManager.h>
#include <utils/EntityManager.h>

using namespace filament;
using namespace filament::math;

@interface FilamentRenderer() {
    Engine* _engine;
    Renderer* _renderer;
    Scene* _scene;
    View* _view;
    Camera* _camera;
    SwapChain* _swapChain;
    utils::Entity _cameraEntity;
    CAMetalLayer* _metalLayer;
}
@end

@implementation FilamentRenderer

- (instancetype)initWithMetalLayer:(CAMetalLayer*)layer {
    self = [super init];
    if (self) {
        _metalLayer = layer;
        [self setupFilament];
    }
    return self;
}

- (void)setupFilament {
    // 1. 创建 Engine (Metal backend)
    _engine = Engine::create(Engine::Backend::METAL);

    // 2. 创建 SwapChain
    void* nativeLayer = (__bridge void*)_metalLayer;
    _swapChain = _engine->createSwapChain(nativeLayer);

    // 3. 创建 Renderer
    _renderer = _engine->createRenderer();

    // 4. 创建 Scene
    _scene = _engine->createScene();

    // 5. 创建 View
    _view = _engine->createView();
    _view->setScene(_scene);

    // 6. 创建 Camera
    _cameraEntity = utils::EntityManager::get().create();
    _camera = _engine->createCamera(_cameraEntity);
    _view->setCamera(_camera);

    // 7. 设置相机参数
    CGSize size = _metalLayer.drawableSize;
    _camera->setProjection(
        Camera::Projection::PERSPECTIVE,
        45.0,
        size.width / size.height,
        0.1, 100.0,
        Camera::Fov::VERTICAL
    );

    _camera->lookAt({0, 0, 4}, {0, 0, 0}, {0, 1, 0});

    // 8. 设置 Viewport
    _view->setViewport({
        0, 0,
        (uint32_t)size.width,
        (uint32_t)size.height
    });

    // 9. 添加光源
    [self setupLighting];
}

- (void)setupLighting {
    utils::Entity light = utils::EntityManager::get().create();

    LightManager::Builder(LightManager::Type::DIRECTIONAL)
        .color({1.0f, 1.0f, 1.0f})
        .intensity(100000.0f)
        .direction({0.0f, -1.0f, 0.0f})
        .build(*_engine, light);

    _scene->addEntity(light);
}

- (void)render {
    if (!_renderer || !_swapChain || !_view) return;

    if (_renderer->beginFrame(_swapChain)) {
        _renderer->render(_view);
        _renderer->endFrame();
    }
}

- (void)resizeWithWidth:(NSInteger)width height:(NSInteger)height {
    _view->setViewport({0, 0, (uint32_t)width, (uint32_t)height});
    _camera->setProjection(
        Camera::Projection::PERSPECTIVE,
        45.0,
        (double)width / (double)height,
        0.1, 100.0,
        Camera::Fov::VERTICAL
    );
}

- (void)cleanup {
    if (_engine) {
        _engine->destroy(_cameraEntity);
        _engine->destroy(_view);
        _engine->destroy(_scene);
        _engine->destroy(_renderer);
        _engine->destroy(_swapChain);
        Engine::destroy(&_engine);
        _engine = nullptr;
    }
}

- (void)dealloc {
    [self cleanup];
}

@end
```

### NSViewController 集成

**FilamentViewController.h:**

```objc
#import <Cocoa/Cocoa.h>

@interface FilamentViewController : NSViewController

@end
```

**FilamentViewController.mm:**

```objc
#import "FilamentViewController.h"
#import "FilamentView.h"
#import "FilamentRenderer.h"

@interface FilamentViewController()

@property (nonatomic, strong) FilamentView* filamentView;
@property (nonatomic, strong) FilamentRenderer* filamentRenderer;
@property (nonatomic, strong) CVDisplayLink displayLink;

@end

@implementation FilamentViewController

- (void)loadView {
    // 创建 FilamentView
    self.filamentView = [[FilamentView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600)];
    self.view = self.filamentView;
}

- (void)viewDidLoad {
    [super viewDidLoad];

    // 创建 FilamentRenderer
    self.filamentRenderer = [[FilamentRenderer alloc]
        initWithMetalLayer:self.filamentView.metalLayer];
}

- (void)viewDidAppear {
    [super viewDidAppear];

    // 启动渲染循环
    [self startDisplayLink];
}

- (void)viewWillDisappear {
    [super viewWillDisappear];

    // 停止渲染循环
    [self stopDisplayLink];
}

// CVDisplayLink 回调
static CVReturn displayLinkCallback(
    CVDisplayLinkRef displayLink,
    const CVTimeStamp* now,
    const CVTimeStamp* outputTime,
    CVOptionFlags flagsIn,
    CVOptionFlags* flagsOut,
    void* context) {

    @autoreleasepool {
        FilamentViewController* controller = (__bridge FilamentViewController*)context;
        [controller renderFrame];
    }

    return kCVReturnSuccess;
}

- (void)startDisplayLink {
    // 创建 CVDisplayLink
    CVDisplayLinkCreateWithActiveCGDisplays(&_displayLink);

    // 设置回调
    CVDisplayLinkSetOutputCallback(_displayLink, &displayLinkCallback, (__bridge void*)self);

    // 启动
    CVDisplayLinkStart(_displayLink);
}

- (void)stopDisplayLink {
    if (_displayLink) {
        CVDisplayLinkStop(_displayLink);
        CVDisplayLinkRelease(_displayLink);
        _displayLink = nil;
    }
}

- (void)renderFrame {
    // 在主线程渲染
    dispatch_async(dispatch_get_main_queue(), ^{
        [self.filamentRenderer render];
    });
}

- (void)dealloc {
    [self stopDisplayLink];
    [self.filamentRenderer cleanup];
}

@end
```

## SwiftUI 集成

### FilamentView (SwiftUI)

**FilamentViewRepresentable.swift:**

```swift
import SwiftUI
import MetalKit

struct FilamentViewRepresentable: NSViewRepresentable {
    @StateObject private var renderer = FilamentRendererWrapper()

    func makeNSView(context: Context) -> FilamentNSView {
        let view = FilamentNSView()
        renderer.setup(with: view.metalLayer)
        return view
    }

    func updateNSView(_ nsView: FilamentNSView, context: Context) {
        let size = nsView.bounds.size
        let scale = nsView.window?.backingScaleFactor ?? 1.0
        renderer.resize(
            Int(size.width * scale),
            Int(size.height * scale)
        )
    }
}

class FilamentNSView: NSView {
    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setupMetalLayer()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupMetalLayer()
    }

    private func setupMetalLayer() {
        wantsLayer = true
        layer = CAMetalLayer()

        guard let metalLayer = layer as? CAMetalLayer else { return }

        metalLayer.device = MTLCreateSystemDefaultDevice()
        metalLayer.pixelFormat = .bgra8Unorm
        metalLayer.framebufferOnly = true
        metalLayer.displaySyncEnabled = true
    }

    var metalLayer: CAMetalLayer {
        return layer as! CAMetalLayer
    }

    override func setFrameSize(_ newSize: NSSize) {
        super.setFrameSize(newSize)

        guard let metalLayer = layer as? CAMetalLayer else { return }
        let scale = window?.backingScaleFactor ?? 1.0
        metalLayer.drawableSize = CGSize(
            width: newSize.width * scale,
            height: newSize.height * scale
        )
    }
}

class FilamentRendererWrapper: ObservableObject {
    private var renderer: FilamentRenderer?

    func setup(with metalLayer: CAMetalLayer) {
        renderer = FilamentRenderer(metalLayer: metalLayer)
        startRenderLoop()
    }

    func resize(_ width: Int, _ height: Int) {
        renderer?.resize(width, height: height)
    }

    private func startRenderLoop() {
        // 使用 CADisplayLink 或 Timer
        Timer.scheduledTimer(withTimeInterval: 1.0 / 60.0, repeats: true) { _ in
            self.renderer?.render()
        }
    }
}
```

### SwiftUI ContentView

**ContentView.swift:**

```swift
import SwiftUI

struct ContentView: View {
    var body: some View {
        FilamentViewRepresentable()
            .frame(minWidth: 800, minHeight: 600)
            .navigationTitle("Filament macOS App")
    }
}

@main
struct FilamentMacOSApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
        }
        .windowStyle(.hiddenTitleBar)
        .windowResizability(.contentSize)
    }
}
```

## GLFW 跨平台窗口

### 安装 GLFW (Homebrew)

```bash
# 安装 Homebrew (如果未安装)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 安装 GLFW
brew install glfw

# 验证安装
brew info glfw
```

### CMake 集成

**CMakeLists.txt:**

```cmake
cmake_minimum_required(VERSION 3.19)
project(FilamentMacApp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# Filament 路径
set(FILAMENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Frameworks/filament")

include_directories(${FILAMENT_DIR}/include)
link_directories(${FILAMENT_DIR}/lib/universal)

# 查找 GLFW
find_package(glfw3 CONFIG REQUIRED)

# 可执行文件
add_executable(FilamentMacApp
    src/main.cpp
)

# 链接库
target_link_libraries(FilamentMacApp
    filament
    backend
    utils
    glfw
    "-framework Metal"
    "-framework MetalKit"
    "-framework QuartzCore"
    "-framework Cocoa"
)
```

### GLFW 应用示例

**main.cpp:**

```cpp
#include <GLFW/glfw3.h>

#if defined(__APPLE__)
    #define GLFW_EXPOSE_NATIVE_COCOA
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

class FilamentMacApp {
public:
    FilamentMacApp(GLFWwindow* window) : m_window(window) {
        setupFilament();
    }

    ~FilamentMacApp() {
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
        m_engine = Engine::create(Engine::Backend::METAL);

        // 获取 Cocoa NSWindow
        void* nativeWindow = glfwGetCocoaWindow(m_window);
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
    GLFWwindow* m_window = nullptr;
    Engine* m_engine = nullptr;
    Renderer* m_renderer = nullptr;
    Scene* m_scene = nullptr;
    View* m_view = nullptr;
    Camera* m_camera = nullptr;
    SwapChain* m_swapChain = nullptr;
    utils::Entity m_cameraEntity;
};

int main() {
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Filament macOS", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    FilamentMacApp app(window);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        app.render();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
```

## Apple Silicon 优化

### M系列芯片检测

```objc
// Objective-C
BOOL isAppleSilicon() {
    #if defined(__arm64__)
        return YES;
    #else
        return NO;
    #endif
}

// 运行时检测
NSProcessInfo* processInfo = [NSProcessInfo processInfo];
if ([processInfo isOperatingSystemAtLeastVersion:(NSOperatingSystemVersion){11, 0, 0}]) {
    // Big Sur+ (支持 Apple Silicon)
}
```

```cpp
// C++
#include <sys/sysctl.h>

bool isAppleSilicon() {
    char buffer[256];
    size_t size = sizeof(buffer);
    if (sysctlbyname("machdep.cpu.brand_string", &buffer, &size, nullptr, 0) == 0) {
        return std::string(buffer).find("Apple") != std::string::npos;
    }
    return false;
}
```

### Metal 性能优化

```objc
// 启用 Metal 特定优化
CAMetalLayer* metalLayer = self.metalLayer;

// 1. 减少帧缓冲数量 (Apple Silicon 推荐 2)
metalLayer.maximumDrawableCount = 2;  // 默认 3

// 2. 允许 drawable 超时 (减少延迟)
metalLayer.allowsNextDrawableTimeout = NO;

// 3. 颜色空间优化
if (@available(macOS 10.13, *)) {
    // 使用 Extended sRGB 支持 wide color
    metalLayer.wantsExtendedDynamicRangeContent = YES;
    metalLayer.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceExtendedSRGB);
}

// 4. ProMotion 支持 (M1 Pro/Max on external displays)
metalLayer.displaySyncEnabled = YES;
```

### Unified Memory 优化

```cpp
// Apple Silicon 使用统一内存架构
// 减少 CPU-GPU 数据传输

Engine::Config config;
config.jobSystemThreadCount = std::thread::hardware_concurrency();
Engine* engine = Engine::create(Engine::Backend::METAL, nullptr, &config);
```

## 性能优化

### Retina 显示支持

```objc
// 自动处理 Retina 缩放
CAMetalLayer* metalLayer = self.metalLayer;
CGFloat scale = [NSScreen mainScreen].backingScaleFactor;
metalLayer.contentsScale = scale;

// 更新 drawable size
CGSize size = self.bounds.size;
metalLayer.drawableSize = CGSizeMake(size.width * scale, size.height * scale);
```

### 帧率控制

```objc
// 使用 CVDisplayLink 同步显示刷新
CVDisplayLinkSetOutputCallback(displayLink, &displayLinkCallback, (__bridge void*)self);

// 获取显示刷新率
CGDirectDisplayID displayID = (CGDirectDisplayID)[[[self.view.window.screen deviceDescription]
    objectForKey:@"NSScreenNumber"] unsignedIntValue];
CVDisplayLinkSetCurrentCGDisplay(displayLink, displayID);

// ProMotion 显示器会自动适配 120Hz
```

### Metal 调试

```bash
# 启用 Metal API Validation
# Product -> Scheme -> Edit Scheme -> Run -> Diagnostics
# 勾选: Metal API Validation

# 使用 Instruments
# Product -> Profile -> Metal System Trace

# GPU Frame Capture
# Xcode -> Debug -> Capture GPU Frame
```

## 常见问题

### Q1: SwapChain 创建失败

**问题:**
```
Engine::createSwapChain() returns nullptr
```

**解决方案:**

```objc
// 确保 CAMetalLayer 正确配置
CAMetalLayer* metalLayer = (CAMetalLayer*)self.view.layer;

// 必须设置 device
metalLayer.device = MTLCreateSystemDefaultDevice();
if (!metalLayer.device) {
    NSLog(@"Metal is not supported on this Mac");
    return;
}

// 设置像素格式
metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
```

### Q2: Retina 显示模糊

**解决方案:**

```objc
// 设置正确的 contentsScale
CGFloat scale = [NSScreen mainScreen].backingScaleFactor;
metalLayer.contentsScale = scale;

// 同步更新 drawable size
metalLayer.drawableSize = CGSizeMake(
    self.bounds.size.width * scale,
    self.bounds.size.height * scale
);
```

### Q3: Apple Silicon 兼容性问题

**解决方案:**

```bash
# 确保使用 Universal Binary 或 arm64 库
file libfilament.a
# 输出: Mach-O universal binary with 2 architectures
#       - x86_64
#       - arm64

# 如果只有 x86_64, 需要重新编译:
./build.sh -p desktop -a universal release
```

### Q4: CVDisplayLink 黑屏

**解决方案:**

```objc
// 确保在主线程更新 UI
- (void)renderFrame {
    dispatch_async(dispatch_get_main_queue(), ^{
        [self.filamentRenderer render];
    });
}
```

## 最佳实践

### 1. 资源管理

```objc
// 使用 ARC 管理 Objective-C 对象
@property (nonatomic, strong) FilamentRenderer* renderer;

// 手动管理 C++ 对象
- (void)dealloc {
    [self.renderer cleanup];  // 调用 C++ 析构
}
```

### 2. 线程安全

```objc
// Filament 不是线程安全的
// 始终在主线程调用 Filament API
dispatch_async(dispatch_get_main_queue(), ^{
    [self.renderer render];
});
```

### 3. 内存优化

```objc
// 监听内存警告 (macOS 通常不需要)
// 但可以监听压力通知
[[NSNotificationCenter defaultCenter] addObserverForName:NSProcessInfoPowerStateDidChangeNotification
    object:nil
    queue:nil
    usingBlock:^(NSNotification* note) {
        // 降低渲染质量以节省电量
    }];
```

### 4. Universal Binary 构建

```bash
# 方式 1: 使用 build.sh
./build.sh -p desktop -a universal release

# 方式 2: 使用 CMake
cmake -G "Xcode" -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" ..

# 验证
lipo -info libfilament.a
# 输出: Architectures in the fat file: libfilament.a are: x86_64 arm64
```

## 相关文档

- [01-platform-overview.md](./01-platform-overview.md) - 平台支持概览
- [03-ios-集成.md](./03-ios-集成.md) - iOS 集成详解 (类似 API)
- [08-swapchain-创建.md](./08-swapchain-创建.md) - SwapChain 详解
- [09-应用生命周期管理.md](./09-应用生命周期管理.md) - 生命周期管理
- [../backend/03-metal-backend.md](../backend/03-metal-backend.md) - Metal Backend 详解
