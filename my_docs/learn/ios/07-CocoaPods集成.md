# Filament CocoaPods 集成

## CocoaPods 集成概述

Filament 通过 CocoaPods 提供了便捷的 iOS 项目集成方案，将复杂的构建过程封装为简单的依赖管理。

## Podspec 配置详解

### 1. 主要配置文件

**位置**: `ios/CocoaPods/Filament.podspec`

```ruby
Pod::Spec.new do |spec|
  # 基本信息
  spec.name = "Filament"
  spec.version = "1.65.0"
  spec.license = { :type => "Apache 2.0", :file => "LICENSE" }
  spec.homepage = "https://google.github.io/filament"
  spec.authors = "Google LLC."
  spec.summary = "Filament is a real-time physically based rendering engine for Android, iOS, Windows, Linux, macOS, and WASM/WebGL."

  # 平台要求
  spec.platform = :ios, "11.0"

  # 源码位置
  spec.source = {
    :http => "https://github.com/google/filament/releases/download/v1.65.0/filament-v1.65.0-ios.tgz"
  }

  # Xcode 12 兼容性配置
  spec.pod_target_xcconfig = {
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'arm64'
  }
  spec.user_target_xcconfig = {
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'arm64'
  }
end
```

### 2. 模块化子规格设计

#### 核心引擎模块
```ruby
spec.subspec "filament" do |ss|
  ss.source_files =
      "include/filament/*.h",
      "include/backend/*.h",
      "include/filament/MaterialChunkType.h",
      "include/filament/MaterialEnums.h",
      "include/ibl/*.h",
      "include/geometry/*.h"

  ss.header_mappings_dir = "include"

  ss.vendored_libraries =
      "lib/universal/libfilament.a",          # 核心引擎
      "lib/universal/libbackend.a",           # 图形后端
      "lib/universal/libfilabridge.a",        # 桥接库
      "lib/universal/libfilaflat.a",          # 序列化
      "lib/universal/libibl.a",               # 环境光照
      "lib/universal/libgeometry.a"           # 几何处理

  # 依赖关系
  ss.dependency "Filament/utils"
  ss.dependency "Filament/math"
end
```

#### 材质编译模块
```ruby
spec.subspec "filamat" do |ss|
  ss.source_files =
      "include/filamat/*.h",
      "include/filament/MaterialChunkType.h",
      "include/filament/MaterialEnums.h"

  ss.header_mappings_dir = "include"

  ss.vendored_libraries =
    "lib/universal/libfilamat.a",             # 材质编译器
    "lib/universal/libshaders.a",             # 着色器系统
    "lib/universal/libsmol-v.a",              # SPIR-V 编译
    "lib/universal/libfilabridge.a"           # 桥接库

  ss.dependency "Filament/utils"
  ss.dependency "Filament/math"
end
```

#### glTF 加载模块
```ruby
spec.subspec "gltfio_core" do |ss|
  ss.source_files = "include/gltfio/**/*.h"
  ss.header_mappings_dir = "include"

  ss.vendored_libraries =
    "lib/universal/libgltfio_core.a",         # glTF 核心
    "lib/universal/libdracodec.a",            # Draco 压缩
    "lib/universal/libuberarchive.a",         # 压缩存档
    "lib/universal/libstb.a"                  # STB 图像库

  ss.dependency "Filament/filament"
  ss.dependency "Filament/ktxreader"
  ss.dependency "Filament/uberz"
end
```

#### 工具库模块
```ruby
# 数学库
spec.subspec "math" do |ss|
  ss.source_files = "include/math/*.h"
  ss.header_dir = "math"
end

# 工具库
spec.subspec "utils" do |ss|
  ss.source_files = "include/utils/**/*.h"
  ss.header_mappings_dir = "include"
  ss.vendored_libraries = "lib/universal/libutils.a"
  ss.dependency "Filament/tsl"
end

# 图像处理
spec.subspec "image" do |ss|
  ss.source_files = "include/image/*.h"
  ss.vendored_libraries = "lib/universal/libimage.a"
  ss.header_dir = "image"
  ss.dependency "Filament/filament"
end

# 相机工具
spec.subspec "camutils" do |ss|
  ss.source_files = "include/camutils/*.h"
  ss.vendored_libraries = "lib/universal/libcamutils.a"
  ss.header_dir = "camutils"
  ss.dependency "Filament/math"
end
```

## 项目集成方法

### 1. 基本集成

#### Podfile 配置
```ruby
platform :ios, '11.0'
use_frameworks!

target 'YourApp' do
  # 基础集成 - 仅核心引擎
  pod 'Filament/filament'

  # 如果需要 glTF 支持
  pod 'Filament/gltfio_core'

  # 如果需要相机控制
  pod 'Filament/camutils'

  # 如果需要图像处理
  pod 'Filament/image'
end
```

#### 安装和配置
```bash
# 安装依赖
pod install

# 打开工作空间
open YourApp.xcworkspace
```

### 2. 高级集成选项

#### 完整功能集成
```ruby
target 'YourApp' do
  # 完整的 Filament 功能
  pod 'Filament/filament'        # 核心引擎
  pod 'Filament/filamat'         # 材质编译
  pod 'Filament/gltfio_core'     # glTF 加载
  pod 'Filament/viewer'          # 模型查看器
  pod 'Filament/camutils'        # 相机工具
  pod 'Filament/image'           # 图像处理
  pod 'Filament/ktxreader'       # KTX 纹理
  pod 'Filament/filameshio'      # 网格 I/O
end
```

#### 按需集成
```ruby
target 'BasicRenderer' do
  # 最小集成 - 仅基础渲染
  pod 'Filament/filament'
  pod 'Filament/math'
  pod 'Filament/utils'
end

target 'AdvancedRenderer' do
  # 高级功能
  pod 'Filament/filament'
  pod 'Filament/filamat'
  pod 'Filament/gltfio_core'
  pod 'Filament/viewer'
end
```

### 3. 版本管理

#### 固定版本
```ruby
# 使用特定版本
pod 'Filament', '1.65.0'

# 使用版本范围
pod 'Filament', '~> 1.65.0'

# 使用最新版本（不推荐生产环境）
pod 'Filament'
```

#### 开发版本
```ruby
# 使用本地构建版本
pod 'Filament', :path => '../filament-local-build'

# 使用 Git 分支
pod 'Filament', :git => 'https://github.com/google/filament.git', :branch => 'main'
```

## 构建配置优化

### 1. Xcode 项目设置

#### 必要的构建设置
```ruby
# 在 Podfile 中添加
post_install do |installer|
  installer.pods_project.targets.each do |target|
    target.build_configurations.each do |config|
      # C++ 标准设置
      config.build_settings['CLANG_CXX_LANGUAGE_STANDARD'] = 'c++14'
      config.build_settings['CLANG_CXX_LIBRARY'] = 'libc++'

      # iOS 版本要求
      config.build_settings['IPHONEOS_DEPLOYMENT_TARGET'] = '11.0'

      # 优化设置
      config.build_settings['ENABLE_BITCODE'] = 'NO'
      config.build_settings['ONLY_ACTIVE_ARCH'] = 'YES' if config.name == 'Debug'

      # ARM64 模拟器兼容性
      if target.name == 'Filament'
        config.build_settings['EXCLUDED_ARCHS[sdk=iphonesimulator*]'] = 'arm64'
      end
    end
  end
end
```

### 2. 链接器配置

#### 必要的系统框架
```ruby
target 'YourApp' do
  pod 'Filament/filament'

  # 系统框架依赖会自动添加，但可以手动指定
  # Metal.framework - Metal 后端
  # OpenGLES.framework - OpenGL ES 后端
  # QuartzCore.framework - CALayer 支持
  # UIKit.framework - iOS UI 框架
end
```

#### 自定义链接设置
```objc
// 在项目设置中添加链接器标志
Other Linker Flags:
-ObjC                    // 支持 Objective-C 分类
-lc++                    // C++ 标准库
```

## 使用示例

### 1. 最小集成示例

**Podfile**:
```ruby
platform :ios, '11.0'
use_frameworks!

target 'MinimalFilament' do
  pod 'Filament/filament'
end
```

**ViewController.mm**:
```objc
#import <Filament/filament.h>
#import <Metal/Metal.h>

@interface ViewController ()
@property (strong, nonatomic) CAMetalLayer* metalLayer;
@end

@implementation ViewController {
    filament::Engine* engine;
    filament::Renderer* renderer;
    filament::SwapChain* swapChain;
}

- (void)viewDidLoad {
    [super viewDidLoad];

    // 创建 Metal 层
    self.metalLayer = [CAMetalLayer layer];
    self.metalLayer.device = MTLCreateSystemDefaultDevice();
    self.metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    self.metalLayer.frame = self.view.bounds;
    [self.view.layer addSublayer:self.metalLayer];

    // 初始化 Filament
    engine = filament::Engine::create();
    renderer = engine->createRenderer();
    swapChain = engine->createSwapChain((__bridge void*)self.metalLayer);
}

- (void)dealloc {
    engine->destroy(renderer);
    engine->destroy(swapChain);
    filament::Engine::destroy(engine);
}

@end
```

### 2. glTF 查看器示例

**Podfile**:
```ruby
platform :ios, '11.0'
use_frameworks!

target 'GLTFViewer' do
  pod 'Filament/filament'
  pod 'Filament/gltfio_core'
  pod 'Filament/camutils'
end
```

**使用代码**:
```objc
#import <gltfio/gltfio.h>
#import <camutils/camutils.h>

// 加载 glTF 模型
gltfio::AssetLoader* loader = gltfio::AssetLoader::create({
    .engine = engine,
    .materials = gltfio::createJitShaderProvider(engine)
});

NSData* modelData = [NSData dataWithContentsOfFile:@"model.glb"];
gltfio::FilamentAsset* asset = loader->createAsset(
    (const uint8_t*)modelData.bytes,
    modelData.length
);

// 相机控制
filament::camutils::Manipulator* manipulator =
    filament::camutils::Manipulator::Builder()
        .orbitHomePosition(0, 0, 4)
        .viewport(width, height)
        .build(filament::camutils::Mode::ORBIT);
```

## 故障排除

### 1. 常见构建错误

#### 架构不匹配
```bash
# 错误信息
ld: building for iOS Simulator, but linking in object file built for iOS

# 解决方案：在 Podfile 中添加
post_install do |installer|
  installer.pods_project.targets.each do |target|
    target.build_configurations.each do |config|
      config.build_settings['EXCLUDED_ARCHS[sdk=iphonesimulator*]'] = 'arm64'
    end
  end
end
```

#### C++ 标准版本冲突
```bash
# 错误信息
'memory' file not found

# 解决方案：设置正确的 C++ 标准
config.build_settings['CLANG_CXX_LANGUAGE_STANDARD'] = 'c++14'
config.build_settings['CLANG_CXX_LIBRARY'] = 'libc++'
```

#### BitCode 错误
```bash
# 错误信息
does not contain bitcode

# 解决方案：禁用 BitCode
config.build_settings['ENABLE_BITCODE'] = 'NO'
```

### 2. 运行时问题

#### Metal 不可用
```objc
// 检查 Metal 支持
if (!MTLCreateSystemDefaultDevice()) {
    // 回退到 OpenGL ES
    engine = filament::Engine::create(filament::Engine::Backend::OPENGL);
}
```

#### 内存不足
```objc
// 监听内存警告
[[NSNotificationCenter defaultCenter]
    addObserver:self
       selector:@selector(handleMemoryWarning:)
           name:UIApplicationDidReceiveMemoryWarningNotification
         object:nil];

- (void)handleMemoryWarning:(NSNotification*)notification {
    // 清理 Filament 缓存
    engine->flushAndWait();
}
```

### 3. 性能优化

#### 减少包大小
```ruby
# 只集成需要的模块
target 'YourApp' do
  pod 'Filament/filament'     # 必需
  # pod 'Filament/filamat'   # 如果不需要运行时材质编译可注释
  # pod 'Filament/viewer'    # 如果不需要查看器功能可注释
end
```

#### 优化启动时间
```objc
// 延迟初始化 Filament
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];

    // 在主线程空闲时初始化
    dispatch_async(dispatch_get_main_queue(), ^{
        [self initializeFilament];
    });
}
```

## 版本迁移指南

### 从 1.50.0 升级到 1.65.0

#### API 变更
```objc
// 旧版本
engine = filament::Engine::create(filament::Engine::Backend::METAL, nullptr);

// 新版本
engine = filament::Engine::create(filament::Engine::Backend::METAL);
```

#### 依赖变更
```ruby
# 检查 podspec 变更
pod update Filament

# 清理并重新安装
pod deintegrate
pod install
```

这个 CocoaPods 集成方案大大简化了 Filament 在 iOS 项目中的使用，提供了灵活的模块化配置选项。