# Filament Android 开发指南

这个目录包含 Filament 在 Android 平台上的全面开发文档，涵盖从基础架构到实际应用的所有方面。

## 📖 文档目录

### 核心架构
- **[01-Android平台架构](./01-Android平台架构.md)** - Android 平台整体架构设计
- **[02-JNI桥接层实现](./02-JNI桥接层实现.md)** - Java/Kotlin 与 C++ 之间的桥接机制
- **[03-构建系统与工具链](./03-构建系统与工具链.md)** - Gradle + CMake 构建系统详解

### 实际应用
- **[04-示例应用详解](./04-示例应用详解.md)** - 13个示例应用深度分析
- **[05-后端驱动实现](./05-后端驱动实现.md)** - OpenGL ES, Vulkan 后端实现
- **[06-AAR打包与发布](./06-AAR打包与发布.md)** - 库打包和 Maven 发布流程

### 开发实践
- **[07-开发实战指南](./07-开发实战指南.md)** - 完整的开发流程和最佳实践

## 🚀 快速开始

### 环境要求

- **Android Studio**: 最新稳定版本
- **NDK**: 27.0.11718014 (在 `build.gradle` 中指定)
- **CMake**: 3.19.0+
- **Java**: JDK 17
- **Android SDK**: API Level 21+ (最低支持), API 34 (目标版本)

### 构建 Filament Android

1. **构建原生库**
```bash
# 构建 Android 原生库
./build.sh -p android release

# 构建桌面工具 (matc, cmgen 等)
./build.sh -p desktop -i release
```

2. **构建 AAR**
```bash
cd android
./gradlew assembleRelease
```

3. **运行示例**
```bash
# 安装特定示例
./gradlew :samples:sample-hello-triangle:installDebug

# 或在 Android Studio 中打开 android 目录
```

### 核心组件概览

#### 📦 模块结构
```
android/
├── filament-android/           # 核心引擎 AAR
├── filamat-android/           # 材质编译器 AAR
├── gltfio-android/           # glTF 加载器 AAR
├── filament-utils-android/   # 工具库 AAR
├── samples/                  # 示例应用
└── common/                   # 共享 JNI 工具
```

#### 🔧 关键特性

- **多后端支持**: OpenGL ES 3.0+, Vulkan 1.0+, WebGPU (实验性)
- **硬件加速**: 利用 Android 硬件缓冲区和 GPU 优化
- **内存管理**: 自动的原生内存管理和 Java GC 集成
- **线程安全**: 单线程渲染引擎设计，避免并发问题
- **性能优化**: 针对移动设备的渲染优化

#### 📱 支持的 Android 版本

- **最低支持**: Android 5.0 (API 21)
- **目标版本**: Android 14 (API 34)
- **架构支持**: arm64-v8a, armeabi-v7a, x86_64, x86

### API 使用示例

#### 基础渲染设置

```kotlin
class MainActivity : Activity() {
    companion object {
        init {
            Filament.init() // 初始化 Filament
        }
    }

    private lateinit var engine: Engine
    private lateinit var renderer: Renderer
    private lateinit var scene: Scene
    private lateinit var view: View
    private lateinit var camera: Camera
    private var swapChain: SwapChain? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // 创建渲染表面
        val surfaceView = SurfaceView(this)
        setContentView(surfaceView)

        // 初始化 Filament 组件
        setupFilament()
        setupSurface(surfaceView)
    }

    private fun setupFilament() {
        engine = Engine.Builder()
            .featureLevel(Engine.FeatureLevel.FEATURE_LEVEL_1)
            .build()
        renderer = engine.createRenderer()
        scene = engine.createScene()
        view = engine.createView()
        camera = engine.createCamera(engine.entityManager.create())

        view.camera = camera
        view.scene = scene
    }
}
```

#### 材质和几何体

```kotlin
// 加载预编译材质
private fun loadMaterial(): Material {
    val buffer = readAsset("materials/my_material.filamat")
    return Material.Builder()
        .payload(buffer, buffer.remaining())
        .build(engine)
}

// 创建几何体
private fun createMesh(): Pair<VertexBuffer, IndexBuffer> {
    val vertexBuffer = VertexBuffer.Builder()
        .bufferCount(1)
        .vertexCount(3)
        .attribute(VertexBuffer.VertexAttribute.POSITION, 0,
                  VertexBuffer.AttributeType.FLOAT3, 0, 12)
        .build(engine)

    val indexBuffer = IndexBuffer.Builder()
        .indexCount(3)
        .bufferType(IndexBuffer.Builder.IndexType.USHORT)
        .build(engine)

    return Pair(vertexBuffer, indexBuffer)
}
```

## 📚 学习路径建议

### 初学者
1. 从 **[Android平台架构](./01-Android平台架构.md)** 开始了解整体设计
2. 学习 **[示例应用详解](./04-示例应用详解.md)** 中的 hello-triangle
3. 参考 **[开发实战指南](./07-开发实战指南.md)** 创建第一个应用

### 进阶开发者
1. 深入研究 **[JNI桥接层实现](./02-JNI桥接层实现.md)** 了解底层机制
2. 学习 **[后端驱动实现](./05-后端驱动实现.md)** 掌握图形 API 集成
3. 了解 **[构建系统与工具链](./03-构建系统与工具链.md)** 进行高级定制

### 企业开发
1. 掌握 **[AAR打包与发布](./06-AAR打包与发布.md)** 进行模块化开发
2. 参考所有示例应用了解最佳实践
3. 建立完整的 CI/CD 流程

## 🛠️ 常见问题

### 构建问题
- **NDK 版本不匹配**: 确保使用 `build.gradle` 中指定的 NDK 版本
- **CMake 版本过低**: 需要 3.19.0 或更高版本
- **内存不足**: 增加 Gradle 堆大小 `-Xmx4g`

### 运行时问题
- **OpenGL 上下文错误**: 检查设备的 OpenGL ES 支持
- **Vulkan 不可用**: 确保设备支持 Vulkan 1.0+
- **材质加载失败**: 验证 `.filamat` 文件是否正确编译

### 性能优化
- **帧率不稳定**: 使用 GPU 性能分析工具
- **内存泄漏**: 确保正确调用 `engine.destroy()` 方法
- **启动时间长**: 考虑异步初始化和预编译着色器

## 📖 相关资源

- **[Filament 官方文档](https://google.github.io/filament/)**
- **[Android NDK 指南](https://developer.android.com/ndk)**
- **[Vulkan on Android](https://developer.android.com/ndk/guides/graphics/vulkan)**
- **[OpenGL ES 参考](https://www.khronos.org/opengles/)**

---

**提示**: 这个文档集合会随着 Filament 的更新而持续更新。建议定期查看最新版本的变更。