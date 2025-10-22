# iOS 集成详解

## 概述

Filament 在 iOS 平台上通过 **Metal** 图形 API 提供高性能的物理渲染能力。本文档详细介绍如何在 iOS 应用中集成 Filament，包括 Xcode 项目配置、Metal/CAMetalLayer 集成、Objective-C++/Swift 混编、以及完整的示例代码。

**主要内容**:
- ✅ Xcode 项目配置和依赖管理
- ✅ CAMetalLayer 创建和配置
- ✅ SwapChain 创建和渲染循环
- ✅ Objective-C++ 和 Swift 混编
- ✅ UIViewController 集成示例
- ✅ 生命周期管理和内存优化
- ✅ 性能优化和调试技巧

**适用平台**:
- iOS 11.0+ (推荐 iOS 13.0+)
- iPadOS 13.0+
- tvOS 11.0+
- macOS (通过 Mac Catalyst)

**前置知识**:
- Objective-C 或 Swift 基础
- Metal 图形 API 基础
- UIKit 框架

## 环境配置

### 系统要求

```bash
# macOS 版本
macOS 11.0+ (Big Sur 或更高)

# Xcode 版本
Xcode 12.0+ (推荐 Xcode 15+)

# iOS SDK
iOS SDK 11.0+ (推荐 13.0+)

# 架构支持
arm64 (iPhone 5s+, iPad Air+)
arm64e (iPhone XS+)
```

### Xcode 安装

```bash
# 通过 Mac App Store 安装 Xcode
# 或者从 Apple Developer 下载

# 安装 Command Line Tools
xcode-select --install

# 验证安装
xcodebuild -version
# 输出: Xcode 15.0
#      Build version 15A240d
```

### Filament iOS 库获取

#### 方式 1: 下载预编译库 (推荐)

```bash
# 下载 Filament iOS 版本
cd ~/Downloads
curl -L -O https://github.com/google/filament/releases/download/v1.51.5/filament-v1.51.5-ios.tgz

# 解压
tar -xzf filament-v1.51.5-ios.tgz

# 移动到项目目录
mv filament ~/MyiOSApp/Frameworks/
```

#### 方式 2: 从源码编译

```bash
# 克隆 Filament 仓库
git clone https://github.com/google/filament.git
cd filament

# 编译 iOS 版本
./build.sh -p ios release

# 编译产物位置
# out/ios-release/filament/
#   ├── lib/
#   │   ├── universal/
#   │   │   ├── libfilament.a
#   │   │   ├── libbackend.a
#   │   │   ├── libfilamat.a
#   │   │   ├── libgltfio_core.a
#   │   │   └── ...
#   ├── include/
#   │   ├── filament/
#   │   ├── backend/
#   │   ├── filamat/
#   │   └── ...
```

#### 库文件结构

```
filament-ios/
├── include/                      # 头文件
│   ├── filament/
│   │   ├── Engine.h
│   │   ├── Renderer.h
│   │   ├── Scene.h
│   │   ├── View.h
│   │   └── ...
│   ├── backend/
│   ├── utils/
│   ├── math/
│   └── filamat/
├── lib/
│   └── universal/                # Universal (arm64 + simulator)
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
# 选择 iOS -> App
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
- UIKit.framework
- CoreGraphics.framework
```

**步骤 4: 配置 C++ 编译选项**

```
Build Settings -> Apple Clang - Language - C++
C++ Language Dialect: GNU++17
C++ Standard Library: libc++
```

**步骤 5: 其他链接器标志**

```
Build Settings -> Linking
Other Linker Flags: -lc++
```

### Bridging Header (Swift 项目)

如果使用 Swift，需要创建 Bridging Header:

**FilamentBridge.h:**

```objc
#ifndef FilamentBridge_h
#define FilamentBridge_h

#import "FilamentHelper.h"

#endif /* FilamentBridge_h */
```

**Project Settings:**

```
Build Settings -> Swift Compiler - General
Objective-C Bridging Header: $(PROJECT_DIR)/FilamentBridge.h
```

## CAMetalLayer 集成

### UIView + CAMetalLayer

**FilamentView.h (Objective-C++):**

```objc
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>

@interface FilamentView : UIView

@property (nonatomic, readonly) CAMetalLayer* metalLayer;

@end
```

**FilamentView.mm:**

```objc
#import "FilamentView.h"

@implementation FilamentView

+ (Class)layerClass {
    return [CAMetalLayer class];
}

- (CAMetalLayer*)metalLayer {
    return (CAMetalLayer*)self.layer;
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        [self setupMetalLayer];
    }
    return self;
}

- (void)setupMetalLayer {
    CAMetalLayer* metalLayer = self.metalLayer;

    // 获取设备
    metalLayer.device = MTLCreateSystemDefaultDevice();

    // 像素格式
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;

    // 帧缓冲设置
    metalLayer.framebufferOnly = YES;

    // 显示同步
    metalLayer.displaySyncEnabled = YES;

    // 内容缩放
    self.contentScaleFactor = [UIScreen mainScreen].nativeScale;
    metalLayer.contentsScale = self.contentScaleFactor;

    // 颜色空间
    metalLayer.colorspace = CGColorSpaceCreateDeviceRGB();
}

- (void)layoutSubviews {
    [super layoutSubviews];

    // 更新 drawable size
    CGSize drawableSize = self.bounds.size;
    drawableSize.width *= self.contentScaleFactor;
    drawableSize.height *= self.contentScaleFactor;
    self.metalLayer.drawableSize = drawableSize;
}

@end
```

### SwiftUI + CAMetalLayer

**FilamentViewRepresentable.swift:**

```swift
import SwiftUI
import MetalKit

struct FilamentViewRepresentable: UIViewRepresentable {
    let filamentRenderer: FilamentRenderer

    func makeUIView(context: Context) -> FilamentView {
        let view = FilamentView(frame: .zero)
        filamentRenderer.setupWithMetalLayer(view.metalLayer)
        return view
    }

    func updateUIView(_ uiView: FilamentView, context: Context) {
        // 更新视图大小
        filamentRenderer.resize(
            Int(uiView.bounds.width * uiView.contentScaleFactor),
            Int(uiView.bounds.height * uiView.contentScaleFactor)
        )
    }
}
```

## Filament 封装

### FilamentHelper (Objective-C++)

**FilamentHelper.h:**

```objc
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>

@interface FilamentHelper : NSObject

- (instancetype)initWithLayer:(CAMetalLayer*)layer;
- (void)render;
- (void)resizeWithWidth:(NSInteger)width height:(NSInteger)height;
- (void)cleanup;

@end
```

**FilamentHelper.mm:**

```objc
#import "FilamentHelper.h"

#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/SwapChain.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <filament/LightManager.h>
#include <utils/EntityManager.h>

using namespace filament;
using namespace filament::math;

@interface FilamentHelper() {
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

@implementation FilamentHelper

- (instancetype)initWithLayer:(CAMetalLayer*)layer {
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
        45.0,                           // FOV
        size.width / size.height,       // aspect
        0.1,                            // near
        100.0,                          // far
        Camera::Fov::VERTICAL
    );

    // 8. 设置相机位置
    _camera->lookAt(
        {0, 0, 4},    // eye
        {0, 0, 0},    // center
        {0, 1, 0}     // up
    );

    // 9. 设置 Viewport
    _view->setViewport({
        0, 0,
        (uint32_t)size.width,
        (uint32_t)size.height
    });

    // 10. 添加光源
    [self setupLighting];
}

- (void)setupLighting {
    // 创建定向光
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

    // 开始帧渲染
    if (_renderer->beginFrame(_swapChain)) {
        _renderer->render(_view);
        _renderer->endFrame();
    }
}

- (void)resizeWithWidth:(NSInteger)width height:(NSInteger)height {
    // 更新 viewport
    _view->setViewport({0, 0, (uint32_t)width, (uint32_t)height});

    // 更新相机投影
    _camera->setProjection(
        Camera::Projection::PERSPECTIVE,
        45.0,
        (double)width / (double)height,
        0.1,
        100.0,
        Camera::Fov::VERTICAL
    );
}

- (void)cleanup {
    // 销毁 Filament 资源
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

## UIViewController 集成

### Objective-C 实现

**FilamentViewController.h:**

```objc
#import <UIKit/UIKit.h>

@interface FilamentViewController : UIViewController

@end
```

**FilamentViewController.mm:**

```objc
#import "FilamentViewController.h"
#import "FilamentView.h"
#import "FilamentHelper.h"

@interface FilamentViewController()

@property (nonatomic, strong) FilamentView* filamentView;
@property (nonatomic, strong) FilamentHelper* filamentHelper;
@property (nonatomic, strong) CADisplayLink* displayLink;

@end

@implementation FilamentViewController

- (void)viewDidLoad {
    [super viewDidLoad];

    // 创建 FilamentView
    self.filamentView = [[FilamentView alloc] initWithFrame:self.view.bounds];
    self.filamentView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:self.filamentView];

    // 创建 FilamentHelper
    self.filamentHelper = [[FilamentHelper alloc] initWithLayer:self.filamentView.metalLayer];
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];

    // 启动渲染循环
    [self startRenderLoop];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];

    // 停止渲染循环
    [self stopRenderLoop];
}

- (void)startRenderLoop {
    if (!self.displayLink) {
        self.displayLink = [CADisplayLink displayLinkWithTarget:self
                                                       selector:@selector(renderFrame:)];
        [self.displayLink addToRunLoop:[NSRunLoop mainRunLoop]
                               forMode:NSRunLoopCommonModes];
    }
}

- (void)stopRenderLoop {
    if (self.displayLink) {
        [self.displayLink invalidate];
        self.displayLink = nil;
    }
}

- (void)renderFrame:(CADisplayLink*)displayLink {
    [self.filamentHelper render];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];

    // 更新渲染尺寸
    CGSize size = self.filamentView.metalLayer.drawableSize;
    [self.filamentHelper resizeWithWidth:size.width height:size.height];
}

- (void)dealloc {
    [self stopRenderLoop];
    [self.filamentHelper cleanup];
}

@end
```

### Swift 实现

**FilamentViewController.swift:**

```swift
import UIKit
import QuartzCore

class FilamentViewController: UIViewController {

    private var filamentView: FilamentView!
    private var filamentHelper: FilamentHelper!
    private var displayLink: CADisplayLink?

    override func viewDidLoad() {
        super.viewDidLoad()

        // 创建 FilamentView
        filamentView = FilamentView(frame: view.bounds)
        filamentView.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        view.addSubview(filamentView)

        // 创建 FilamentHelper
        filamentHelper = FilamentHelper(layer: filamentView.metalLayer)
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        startRenderLoop()
    }

    override func viewWillDisappear(_ animated: Bool) {
        super.viewWillDisappear(animated)
        stopRenderLoop()
    }

    private func startRenderLoop() {
        guard displayLink == nil else { return }

        displayLink = CADisplayLink(target: self, selector: #selector(renderFrame))
        displayLink?.add(to: .main, forMode: .common)
    }

    private func stopRenderLoop() {
        displayLink?.invalidate()
        displayLink = nil
    }

    @objc private func renderFrame(_ displayLink: CADisplayLink) {
        filamentHelper.render()
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()

        let size = filamentView.metalLayer.drawableSize
        filamentHelper.resize(Int(size.width), height: Int(size.height))
    }

    deinit {
        stopRenderLoop()
        filamentHelper.cleanup()
    }
}
```

## 加载和渲染模型

### 添加 glTF 模型

**FilamentHelper.mm (扩展):**

```objc
#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/ResourceLoader.h>

using namespace gltfio;

@interface FilamentHelper() {
    // ...之前的变量...
    AssetLoader* _assetLoader;
    ResourceLoader* _resourceLoader;
    FilamentAsset* _asset;
}
@end

- (void)loadGLTFModel:(NSString*)path {
    // 1. 创建 AssetLoader
    _assetLoader = AssetLoader::create({_engine});

    // 2. 创建 ResourceLoader
    _resourceLoader = new ResourceLoader({.engine = _engine});

    // 3. 读取 glTF 文件
    NSData* data = [NSData dataWithContentsOfFile:path];
    if (!data) {
        NSLog(@"Failed to load glTF file: %@", path);
        return;
    }

    // 4. 加载 Asset
    _asset = _assetLoader->createAsset(
        (const uint8_t*)data.bytes,
        data.length
    );

    if (!_asset) {
        NSLog(@"Failed to parse glTF");
        return;
    }

    // 5. 加载外部资源 (纹理、buffers)
    NSString* dir = [path stringByDeletingLastPathComponent];
    [self loadResources:_asset baseDir:dir];

    // 6. 添加到场景
    _scene->addEntities(_asset->getEntities(), _asset->getEntityCount());

    // 7. 设置相机看向模型
    Aabb bounds = _asset->getBoundingBox();
    float3 center = bounds.center();
    float3 halfExtent = bounds.extent();
    float maxExtent = max(halfExtent) * 2.0f;

    _camera->lookAt(
        center + float3{0, 0, maxExtent * 1.5f},  // eye
        center,                                     // center
        {0, 1, 0}                                   // up
    );
}

- (void)loadResources:(FilamentAsset*)asset baseDir:(NSString*)dir {
    // 加载外部 URI 资源
    for (size_t i = 0; i < asset->getResourceUriCount(); i++) {
        const char* uri = asset->getResourceUri(i);
        NSString* filePath = [dir stringByAppendingPathComponent:@(uri)];
        NSData* data = [NSData dataWithContentsOfFile:filePath];

        if (data) {
            _resourceLoader->addResourceData(
                uri,
                ResourceLoader::BufferDescriptor(
                    data.bytes,
                    data.length
                )
            );
        }
    }

    // 异步加载资源
    _resourceLoader->asyncBeginLoad(asset);

    // 等待加载完成 (同步)
    // 实际应用中应使用异步回调
    while (!_resourceLoader->asyncUpdateLoad()) {
        usleep(1000);
    }
}
```

**在 ViewController 中使用:**

```objc
- (void)viewDidLoad {
    [super viewDidLoad];

    // ...之前的代码...

    // 加载模型
    NSString* modelPath = [[NSBundle mainBundle] pathForResource:@"model" ofType:@"gltf"];
    if (modelPath) {
        [self.filamentHelper loadGLTFModel:modelPath];
    }
}
```

## 生命周期管理

### 处理 App 后台/前台切换

```objc
- (void)viewDidLoad {
    [super viewDidLoad];

    // 监听应用生命周期事件
    [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(appDidEnterBackground:)
                                               name:UIApplicationDidEnterBackgroundNotification
                                             object:nil];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(appWillEnterForeground:)
                                               name:UIApplicationWillEnterForegroundNotification
                                             object:nil];
}

- (void)appDidEnterBackground:(NSNotification*)notification {
    // 停止渲染循环以节省电量
    [self stopRenderLoop];
}

- (void)appWillEnterForeground:(NSNotification*)notification {
    // 恢复渲染循环
    [self startRenderLoop];
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}
```

### 内存警告处理

```objc
- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];

    // 释放不必要的资源
    // 例如:清空纹理缓存、降低模型细节等
    [self.filamentHelper reduceMemoryUsage];
}
```

**FilamentHelper.mm:**

```objc
- (void)reduceMemoryUsage {
    // 清空纹理缓存
    // 降低材质质量
    // 卸载不可见的模型
    // ...
}
```

## 性能优化

### ProMotion (120Hz) 支持

**iOS 13.0+ 支持 ProMotion 显示**

```objc
- (void)startRenderLoop {
    if (!self.displayLink) {
        self.displayLink = [CADisplayLink displayLinkWithTarget:self
                                                       selector:@selector(renderFrame:)];

        // iOS 15+: 设置优选帧率范围
        if (@available(iOS 15.0, *)) {
            self.displayLink.preferredFrameRateRange = CAFrameRateRangeMake(80, 120, 120);
        } else {
            // iOS 10-14: 使用 preferredFramesPerSecond
            self.displayLink.preferredFramesPerSecond = 120;
        }

        [self.displayLink addToRunLoop:[NSRunLoop mainRunLoop]
                               forMode:NSRunLoopCommonModes];
    }
}
```

### Metal Performance HUD

**显示 Metal 性能统计:**

```objc
- (void)setupMetalLayer {
    // ...之前的代码...

#if DEBUG
    // 启用 Metal HUD (仅限调试)
    metalLayer.developerHUDProperties = @{
        @"mode": @(2),  // 显示性能统计
    };
#endif
}
```

### 减少 Drawable 等待时间

```objc
- (void)setupMetalLayer {
    CAMetalLayer* metalLayer = self.metalLayer;

    // ...

    // 允许下一帧在当前帧完成前开始准备
    metalLayer.allowsNextDrawableTimeout = NO;

    // 最大 drawable 数量 (2 或 3)
    metalLayer.maximumDrawableCount = 3;  // 默认是 3
}
```

### CPU/GPU 并行优化

```objc
- (void)renderFrame:(CADisplayLink*)displayLink {
    // 1. CPU 准备工作 (更新变换、动画等)
    [self updateAnimations:displayLink.timestamp];

    // 2. 提交渲染命令 (Filament 内部会优化 CPU/GPU 并行)
    [self.filamentHelper render];
}
```

## 调试技巧

### Metal Frame Capture

**在 Xcode 中捕获 Metal 帧:**

```objc
// 1. 在 Xcode 中运行应用
// 2. 点击 Debug navigator -> View Frame Graph
// 3. 或使用快捷键: Cmd + Option + M

// 代码中触发捕获:
#import <Metal/Metal.h>

id<MTLCaptureScope> captureScope = [[MTLCaptureManager sharedCaptureManager]
    newCaptureScopeWithDevice:_metalLayer.device];
captureScope.label = @"FilamentFrame";

[[MTLCaptureManager sharedCaptureManager] startCaptureWithScope:captureScope];
[self.filamentHelper render];
[[MTLCaptureManager sharedCaptureManager] stopCapture];
```

### Instruments 性能分析

```bash
# 1. Product -> Profile (Cmd + I)
# 2. 选择模板:
#    - Metal System Trace (GPU 性能)
#    - Time Profiler (CPU 性能)
#    - Allocations (内存分配)
#    - Leaks (内存泄漏)
```

### 日志和断言

```objc
// 在 FilamentHelper.mm 中添加调试日志

#if DEBUG
#define FILAMENT_LOG(fmt, ...) NSLog(@"[Filament] " fmt, ##__VA_ARGS__)
#else
#define FILAMENT_LOG(fmt, ...)
#endif

- (void)render {
    FILAMENT_LOG(@"Rendering frame");

    if (!_renderer || !_swapChain || !_view) {
        FILAMENT_LOG(@"ERROR: Missing rendering components");
        return;
    }

    if (_renderer->beginFrame(_swapChain)) {
        _renderer->render(_view);
        _renderer->endFrame();
    }
}
```

## 完整示例: 旋转立方体

### 添加立方体几何

**FilamentHelper.mm:**

```objc
#include <filament/IndexBuffer.h>
#include <filament/VertexBuffer.h>
#include <filament/Material.h>

- (void)setupCube {
    // 立方体顶点数据
    static const float3 vertices[] = {
        {-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1},  // front
        {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1},  // back
    };

    static const uint16_t indices[] = {
        0, 1, 2, 2, 3, 0,  // front
        1, 5, 6, 6, 2, 1,  // right
        5, 4, 7, 7, 6, 5,  // back
        4, 0, 3, 3, 7, 4,  // left
        3, 2, 6, 6, 7, 3,  // top
        4, 5, 1, 1, 0, 4,  // bottom
    };

    // 创建 VertexBuffer
    VertexBuffer* vb = VertexBuffer::Builder()
        .vertexCount(8)
        .bufferCount(1)
        .attribute(VertexAttribute::POSITION, 0,
                   VertexBuffer::AttributeType::FLOAT3, 0, sizeof(float3))
        .build(*_engine);

    vb->setBufferAt(*_engine, 0,
        VertexBuffer::BufferDescriptor(vertices, sizeof(vertices)));

    // 创建 IndexBuffer
    IndexBuffer* ib = IndexBuffer::Builder()
        .indexCount(36)
        .bufferType(IndexBuffer::IndexType::USHORT)
        .build(*_engine);

    ib->setBuffer(*_engine,
        IndexBuffer::BufferDescriptor(indices, sizeof(indices)));

    // 加载材质 (假设已编译为 .filamat)
    NSString* matPath = [[NSBundle mainBundle] pathForResource:@"default" ofType:@"filamat"];
    NSData* matData = [NSData dataWithContentsOfFile:matPath];
    Material* material = Material::Builder()
        .package(matData.bytes, matData.length)
        .build(*_engine);

    // 创建 Entity
    utils::Entity cubeEntity = utils::EntityManager::get().create();

    // 构建 Renderable
    RenderableManager::Builder(1)
        .boundingBox({{-1, -1, -1}, {1, 1, 1}})
        .material(0, material->getDefaultInstance())
        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb, ib)
        .culling(false)
        .build(*_engine, cubeEntity);

    _scene->addEntity(cubeEntity);

    // 保存 entity 以便旋转
    _cubeEntity = cubeEntity;
}

- (void)updateRotation:(float)angle {
    auto& tcm = _engine->getTransformManager();
    auto instance = tcm.getInstance(_cubeEntity);

    mat4f rotation = mat4f::rotation(angle, float3{0, 1, 0});
    tcm.setTransform(instance, rotation);
}
```

**ViewController 中更新:**

```objc
- (void)renderFrame:(CADisplayLink*)displayLink {
    // 计算旋转角度
    static CFTimeInterval startTime = 0;
    if (startTime == 0) {
        startTime = displayLink.timestamp;
    }

    float elapsed = displayLink.timestamp - startTime;
    float angle = elapsed * M_PI / 2.0;  // 每4秒旋转一周

    [self.filamentHelper updateRotation:angle];
    [self.filamentHelper render];
}
```

## 常见问题

### Q1: SwapChain 创建失败

**问题:**
```
Engine::createSwapChain() returns null
```

**解决方案:**

```objc
// 确保 CAMetalLayer 正确初始化
- (void)setupMetalLayer {
    CAMetalLayer* metalLayer = self.metalLayer;

    // 必须设置 device
    metalLayer.device = MTLCreateSystemDefaultDevice();
    if (!metalLayer.device) {
        NSLog(@"ERROR: Failed to create Metal device");
        return;
    }

    // 必须设置 pixelFormat
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;

    // ...
}
```

### Q2: 渲染黑屏

**可能原因:**

1. **相机位置不正确**
```objc
// 确保相机看向场景
_camera->lookAt({0, 0, 4}, {0, 0, 0}, {0, 1, 0});
```

2. **光源未添加**
```objc
[self setupLighting];
```

3. **Viewport 未设置**
```objc
_view->setViewport({0, 0, width, height});
```

### Q3: 应用崩溃在后台/前台切换

**解决方案:**

```objc
- (void)appDidEnterBackground:(NSNotification*)notification {
    // 停止渲染循环
    [self stopRenderLoop];

    // 等待当前帧完成
    if (_engine) {
        _engine->flushAndWait();
    }
}

- (void)appWillEnterForeground:(NSNotification*)notification {
    // 恢复渲染循环
    [self startRenderLoop];
}
```

### Q4: Metal Validation 错误

**启用 Metal API Validation:**

```
Product -> Scheme -> Edit Scheme -> Run -> Diagnostics
勾选: Metal API Validation
```

**常见错误:**

```
// 错误: Rendering to invalid drawable
// 原因: SwapChain 已销毁
// 解决: 确保生命周期管理正确
```

### Q5: 性能不佳 (FPS 低)

**优化检查清单:**

```objc
// 1. 启用 Vulkan/Metal (已在 iOS 上使用 Metal)
// 2. 减少 draw call
// 3. 启用 frustum culling
_view->setFrustumCullingEnabled(true);

// 4. 降低 shadow map 分辨率
// 5. 使用 LOD
// 6. 减少材质复杂度
```

## 最佳实践

### 1. 资源管理

```objc
// 使用 RAII 模式管理 Filament 资源
@interface FilamentHelper()
@property (nonatomic, assign) BOOL isInitialized;
@end

- (void)cleanup {
    if (!self.isInitialized) return;

    // 按相反顺序销毁资源
    if (_engine) {
        _engine->destroy(_cameraEntity);
        _engine->destroy(_view);
        _engine->destroy(_scene);
        _engine->destroy(_renderer);
        _engine->destroy(_swapChain);
        Engine::destroy(&_engine);
        _engine = nullptr;
    }

    self.isInitialized = NO;
}
```

### 2. 线程安全

```objc
// Filament 的 Engine 不是线程安全的
// 始终在主线程调用 Filament API
dispatch_async(dispatch_get_main_queue(), ^{
    [self.filamentHelper render];
});
```

### 3. 内存优化

```objc
// 使用纹理压缩 (ASTC for iOS)
// 使用 LOD
// 及时释放未使用的资源
```

### 4. 电池优化

```objc
// 在后台停止渲染
// 降低帧率 (如果不需要 120Hz)
self.displayLink.preferredFramesPerSecond = 60;
```

## 相关文档

- [01-platform-overview.md](./01-platform-overview.md) - 平台支持概览
- [08-swapchain-创建.md](./08-swapchain-创建.md) - SwapChain 详解
- [09-应用生命周期管理.md](./09-应用生命周期管理.md) - 生命周期管理
- [../backend/03-metal-backend.md](../backend/03-metal-backend.md) - Metal Backend 详解
- [../engine/01-engine-initialization.md](../engine/01-engine-initialization.md) - Engine 初始化
