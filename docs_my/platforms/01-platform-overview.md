# 平台支持概览

## 概述

Filament 是一个真正的跨平台渲染引擎，支持从移动设备到桌面，再到 Web 的各种平台。本文档提供平台支持的全面概览，包括特性对比、Backend 选择和最低系统要求。

## 支持的平台

### 移动平台

#### Android
- **图形 API**: OpenGL ES 3.0+, Vulkan 1.0+
- **最低版本**: Android 5.0 (API 21)
- **推荐版本**: Android 7.0+ (API 24+) for Vulkan
- **架构**: armeabi-v7a, arm64-v8a, x86, x86_64
- **NDK**: r25+

#### iOS
- **图形 API**: Metal
- **最低版本**: iOS 11.0+
- **推荐版本**: iOS 13.0+
- **架构**: arm64, arm64e
- **Xcode**: 12.0+

### 桌面平台

#### Windows
- **图形 API**: Vulkan 1.0+, OpenGL 4.1+
- **最低版本**: Windows 7 SP1
- **推荐版本**: Windows 10+
- **架构**: x86, x86_64
- **编译器**: Visual Studio 2019+

#### macOS
- **图形 API**: Metal
- **最低版本**: macOS 10.13 (High Sierra)
- **推荐版本**: macOS 11.0+
- **架构**: x86_64, arm64 (Apple Silicon)
- **Xcode**: 12.0+

#### Linux
- **图形 API**: Vulkan 1.0+, OpenGL 4.1+
- **发行版**: Ubuntu 18.04+, Fedora 30+, Arch
- **架构**: x86_64, arm64
- **编译器**: GCC 9+, Clang 10+

### Web 平台

#### WebAssembly + WebGL
- **图形 API**: WebGL 2.0
- **浏览器**: Chrome 57+, Firefox 51+, Safari 15+
- **Emscripten**: 3.1.0+

## 平台特性对比矩阵

| 特性 | Android | iOS | Windows | macOS | Linux | Web |
|------|---------|-----|---------|-------|-------|-----|
| **Vulkan** | ✅ (API 24+) | ❌ | ✅ | ❌ | ✅ | ❌ |
| **Metal** | ❌ | ✅ | ❌ | ✅ | ❌ | ❌ |
| **OpenGL** | ✅ (ES 3.0) | ❌ | ✅ (4.1+) | ⚠️ (已弃用) | ✅ (4.1+) | ❌ |
| **WebGL** | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ (2.0) |
| **多线程渲染** | ✅ | ✅ | ✅ | ✅ | ✅ | ⚠️ (受限) |
| **计算着色器** | ✅ (Vulkan) | ✅ | ✅ | ✅ | ✅ | ❌ |
| **光线追踪** | ⚠️ (RTX手机) | ❌ | ✅ (RTX) | ❌ | ✅ (RTX) | ❌ |

## Backend 选择指南

### Android

```cpp
// 推荐配置
Engine::Backend backend = Engine::Backend::VULKAN;  // Android 7.0+
// 回退
if (Build.VERSION.SDK_INT < 24) {
    backend = Engine::Backend::OPENGL;
}

Engine* engine = Engine::create(backend);
```

**选择建议：**
- **Vulkan**: API 24+ (Android 7.0+)，性能最佳
- **OpenGL ES**: API 21+ 兼容性好，但性能较低

### iOS

```cpp
// 唯一选择
Engine* engine = Engine::create(Engine::Backend::METAL);
```

**说明：**
- Metal 是 iOS 唯一支持的现代图形 API
- OpenGL ES 已在 iOS 12 中弃用

### Windows

```cpp
// 推荐 Vulkan
Engine* engine = Engine::create(Engine::Backend::VULKAN);

// 回退到 OpenGL
// Engine* engine = Engine::create(Engine::Backend::OPENGL);
```

**选择建议：**
- **Vulkan**: 现代 GPU，性能最佳
- **OpenGL**: 兼容性好，适合旧硬件

### macOS

```cpp
// 推荐 Metal
Engine* engine = Engine::create(Engine::Backend::METAL);
```

**说明：**
- Metal 性能最佳
- OpenGL 在 macOS 10.14 后已弃用

### Linux

```cpp
// 优先 Vulkan
Engine* engine = Engine::create(Engine::Backend::VULKAN);

// 回退 OpenGL
// Engine* engine = Engine::create(Engine::Backend::OPENGL);
```

### Web

```cpp
// WebGL 是唯一选择
Engine* engine = Engine::create(Engine::Backend::OPENGL);  // WebGL 2.0
```

## 最低系统要求

### GPU 要求

| 平台 | 最低 GPU | 推荐 GPU |
|------|---------|---------|
| **Android** | Adreno 418, Mali-T760 | Adreno 630+, Mali-G76+ |
| **iOS** | A9 (iPhone 6s) | A12+ (iPhone XS+) |
| **Windows** | GTX 660, AMD HD 7950 | GTX 1060+, RX 580+ |
| **macOS** | Intel HD 5000 | AMD Radeon Pro 560+ |
| **Linux** | GTX 660, AMD HD 7950 | GTX 1060+, RX 580+ |

### 内存要求

| 平台 | 最低 RAM | 推荐 RAM |
|------|---------|---------|
| **移动端** | 2 GB | 4 GB+ |
| **桌面端** | 4 GB | 8 GB+ |
| **Web** | - | - (依赖浏览器) |

## 性能对比

### 移动平台性能

基于 Pixel 4a (Snapdragon 730G):

| Backend | FPS | 功耗 | 发热 |
|---------|-----|------|------|
| Vulkan | 60 | 中 | 低 |
| OpenGL ES | 45 | 高 | 中 |

### 桌面平台性能

基于 RTX 3060 (1080p):

| Backend | FPS | CPU 占用 | GPU 占用 |
|---------|-----|----------|----------|
| Vulkan | 300+ | 15% | 60% |
| OpenGL | 200+ | 25% | 65% |

## 平台限制和注意事项

### Android 限制

```java
// 纹理大小限制
int maxTextureSize = 4096;  // 大多数设备

// Uniform Buffer 大小限制
int maxUniformBlockSize = 16384;  // 16KB (OpenGL ES)

// 顶点属性数量
int maxVertexAttributes = 16;
```

### iOS 限制

```objc
// Metal 特定限制
size_t maxTextureSize = 16384;  // iPhone 11+
size_t maxBufferLength = 256 * 1024 * 1024;  // 256MB
```

### Web 限制

```javascript
// WebGL 2.0 限制
const MAX_TEXTURE_SIZE = gl.getParameter(gl.MAX_TEXTURE_SIZE);  // 通常 4096-16384
const MAX_VERTEX_ATTRIBS = gl.getParameter(gl.MAX_VERTEX_ATTRIBS);  // 通常 16
```

## 相关文档

- [02-android-integration.md](./02-android-integration.md) - Android 详细集成
- [03-ios-integration.md](./03-ios-integration.md) - iOS 详细集成
- [../backend/09-backend-comparison.md](../backend/09-backend-comparison.md) - Backend 技术对比
