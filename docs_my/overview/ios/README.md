# Filament iOS 开发指南

本目录包含 Filament 渲染引擎在 iOS 平台上的完整开发指南和技术文档。

## 文档概览

- [iOS 平台架构](./01-iOS平台架构.md) - Filament 在 iOS 上的架构设计和技术实现
- [构建系统与工具链](./02-构建系统与工具链.md) - iOS 平台的构建配置和工具链使用
- [示例应用详解](./03-示例应用详解.md) - 8 个官方示例应用的功能和实现分析
- [Metal 后端实现](./04-Metal后端实现.md) - Metal 图形 API 后端的详细实现
- [OpenGL ES 后端实现](./05-OpenGL ES后端实现.md) - OpenGL ES 后端的平台适配实现
- [桥接层与绑定机制](./06-桥接层与绑定机制.md) - Objective-C++ 桥接和平台抽象层
- [CocoaPods 集成](./07-CocoaPods集成.md) - 包管理和模块化集成方案
- [开发实战指南](./08-开发实战指南.md) - 项目集成、调试技巧和最佳实践

## 快速开始

### 系统要求

- **iOS 版本**: iOS 11.0+ (推荐 iOS 13.0+)
- **Xcode**: 最新版本 (推荐 Xcode 15.0+)
- **开发环境**: macOS 10.15+
- **图形后端**: Metal (推荐) 或 OpenGL ES 3.0

### 构建 Filament iOS 库

```bash
# 1. 构建桌面工具
./build.sh -p desktop -i release

# 2. 构建 iOS 库 (设备 + 模拟器)
./build.sh -s -p ios -i release

# 3. 仅构建设备版本
./build.sh -p ios -i release
```

### 集成方式

#### 方式一：CocoaPods（推荐）
```ruby
# Podfile
pod 'Filament', '~> 1.65.0'
```

#### 方式二：手动集成
直接使用构建生成的静态库文件：
```
out/ios-release/filament/lib/universal/
├── libfilament.a
├── libbackend.a
├── libutils.a
└── ...
```

## 核心特性

### 图形后端支持
- **Metal**: iOS 11.0+，推荐使用
- **OpenGL ES 3.0**: iOS 7.0+，兼容性后备方案

### 主要功能
- 物理基础渲染 (PBR)
- 实时光照和阴影
- glTF 2.0 模型加载
- 材质系统和着色器编译
- 纹理压缩和优化
- 后处理效果

### 平台特定优化
- Metal Performance Shaders 集成
- iOS 内存管理优化
- 设备方向和屏幕适配
- 后台/前台状态处理

## 示例应用

| 应用名称 | 功能描述 | 技术重点 |
|---------|----------|----------|
| hello-triangle | 基础三角形渲染 | 入门教程 |
| hello-pbr | PBR 材质演示 | 材质系统 |
| hello-gltf | glTF 模型加载 | 资产管道 |
| hello-ar | ARKit 集成 | 增强现实 |
| gltf-viewer | glTF 模型查看器 | 完整应用 |
| transparent-rendering | 透明渲染 | 渲染技术 |
| backend-test | 后端兼容性测试 | 平台测试 |
| HelloCocoaPods | CocoaPods 集成示例 | 包管理 |

## 目录结构

```
ios/
├── CocoaPods/
│   └── Filament.podspec          # CocoaPods 规格文件
├── samples/                      # 示例应用
│   ├── hello-triangle/           # 基础三角形渲染
│   ├── hello-pbr/               # PBR 材质演示
│   ├── hello-gltf/              # glTF 模型加载
│   ├── hello-ar/                # ARKit 集成
│   ├── gltf-viewer/             # glTF 查看器
│   ├── transparent-rendering/    # 透明渲染
│   ├── backend-test/            # 后端测试
│   ├── HelloCocoaPods/          # CocoaPods 示例
│   ├── README.md                # 示例应用说明
│   ├── app-template.yml         # XcodeGen 模板
│   └── generate-samples.sh      # 项目生成脚本
└── README.md                    # 本文件
```

## 技术架构

```
iOS 应用层
    ↓
Objective-C++ 桥接层
    ↓
Filament 引擎核心
    ↓
图形后端抽象层
    ↓
Metal / OpenGL ES
```

## 开发流程

1. **环境搭建** - 安装 Xcode 和必要工具
2. **构建库文件** - 编译 Filament iOS 静态库
3. **创建项目** - 新建 iOS 项目或使用示例
4. **集成 Filament** - 添加库依赖和头文件
5. **编写渲染代码** - 实现 Filament 渲染逻辑
6. **测试和优化** - 在设备上测试性能

## 常见问题

### 构建问题
- 确保使用最新版本的 Xcode
- 检查 iOS SDK 和工具链版本
- 清理 CMake 缓存：`rm -rf out/`

### 运行时问题
- Metal 兼容性：确保目标设备支持 Metal
- 内存管理：注意 Filament 对象的生命周期
- 线程安全：在主线程调用 UI 相关的 Filament API

### 性能优化
- 使用 Metal 后端以获得最佳性能
- 合理设置渲染分辨率和质量参数
- 利用 iOS 的内存和电源管理特性

## 版本兼容性

| Filament 版本 | iOS 最低版本 | Metal 支持 | OpenGL ES 支持 |
|--------------|-------------|-----------|---------------|
| 1.65.0+ | iOS 11.0 | ✅ | ✅ |
| 1.50.0+ | iOS 11.0 | ✅ | ✅ |
| 1.40.0+ | iOS 10.0 | ✅ | ✅ |

## 社区资源

- [Filament 官方文档](https://google.github.io/filament/)
- [GitHub 仓库](https://github.com/google/filament)
- [示例代码](https://github.com/google/filament/tree/main/ios/samples)
- [API 参考](https://google.github.io/filament/reference/)

更多详细信息请查看各个子文档。