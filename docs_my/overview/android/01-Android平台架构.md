# Filament Android 平台架构

## 架构概述

Filament 在 Android 平台上采用分层架构设计，通过 JNI 桥接层将 Java/Kotlin 应用层与 C++ 渲染引擎连接，实现高性能的实时渲染。

```
┌─────────────────────────────────────────────────────────┐
│                 Android Application                     │
│              (Java/Kotlin)                             │
├─────────────────────────────────────────────────────────┤
│                   Filament SDK                          │
│           (Java/Kotlin API Wrapper)                    │
├─────────────────────────────────────────────────────────┤
│                   JNI Bridge                            │
│              (libfilament-jni.so)                      │
├─────────────────────────────────────────────────────────┤
│                 Filament Engine                         │
│                  (C++ Core)                            │
├─────────────────────────────────────────────────────────┤
│               Graphics Backend                          │
│         (OpenGL ES / Vulkan / WebGPU)                  │
├─────────────────────────────────────────────────────────┤
│              Android Graphics                           │
│          (SurfaceView / TextureView)                   │
└─────────────────────────────────────────────────────────┘
```

## 核心组件详解

### 1. Android 应用层

#### SurfaceView 集成
```kotlin
class MainActivity : Activity() {
    private lateinit var surfaceView: SurfaceView
    private lateinit var uiHelper: UiHelper

    private fun setupSurfaceView() {
        surfaceView = SurfaceView(this)
        setContentView(surfaceView)

        // Filament 提供的 UI 帮助类
        uiHelper = UiHelper(UiHelper.ContextErrorPolicy.DONT_CHECK)
        uiHelper.renderCallback = SurfaceCallback()
        uiHelper.attachTo(surfaceView)
    }
}
```

#### TextureView 支持
```kotlin
// 用于需要 View 层级的场景
class TextureViewRenderer {
    private fun setupTextureView() {
        val textureView = TextureView(context)
        textureView.surfaceTextureListener = object : TextureView.SurfaceTextureListener {
            override fun onSurfaceTextureAvailable(surface: SurfaceTexture, width: Int, height: Int) {
                val nativeSurface = Surface(surface)
                swapChain = engine.createSwapChain(nativeSurface)
            }
        }
    }
}
```

### 2. Filament SDK (Java/Kotlin 层)

#### 引擎管理
```kotlin
// Engine.java - 核心引擎管理类
class Engine {
    companion object {
        init {
            Filament.init() // 加载 native 库
        }
    }

    // 引擎配置
    class Config {
        var commandBufferSizeMB: Int = 1
        var perRenderPassArenaSizeMB: Int = 1
        var jobSystemThreadCount: Int = 0
        var forceGLES2Context: Boolean = false
        // ... 其他配置项
    }

    // 创建引擎实例
    class Builder {
        fun backend(backend: Backend): Builder
        fun config(config: Config): Builder
        fun featureLevel(level: FeatureLevel): Builder
        fun build(): Engine
    }
}
```

#### 资源管理
```kotlin
// 顶点缓冲区
class VertexBuffer {
    class Builder {
        fun bufferCount(bufferCount: Int): Builder
        fun vertexCount(vertexCount: Int): Builder
        fun attribute(attribute: VertexAttribute, bufferIndex: Int,
                     attributeType: AttributeType, byteOffset: Int, byteStride: Int): Builder
        fun build(engine: Engine): VertexBuffer
    }
}

// 材质系统
class Material {
    class Builder {
        fun payload(buffer: ByteBuffer, size: Int): Builder
        fun build(engine: Engine): Material
    }

    fun compile(priority: CompilerPriorityQueue,
               userVariantFilter: Int,
               handler: Handler,
               callback: CompilerCallback)
}
```

### 3. JNI 桥接层

#### 引擎桥接实现
```cpp
// Engine.cpp - JNI 桥接实现
extern "C" JNIEXPORT jlong JNICALL
Java_com_google_android_filament_Engine_nCreateSwapChain(JNIEnv* env,
        jclass klass, jlong nativeEngine, jobject surface, jlong flags) {
    Engine* engine = (Engine*) nativeEngine;
    void* win = getNativeWindow(env, klass, surface);
    return (jlong) engine->createSwapChain(win, (uint64_t) flags);
}

extern "C" JNIEXPORT void JNICALL
Java_com_google_android_filament_Engine_nDestroyEngine(JNIEnv*, jclass, jlong nativeEngine) {
    Engine* engine = (Engine*) nativeEngine;
    Engine::destroy(&engine);
}
```

#### 原生窗口获取
```cpp
// nativewindow/Android.cpp
extern "C" {
void* getNativeWindow(JNIEnv* env, jclass, jobject surface) {
    return ANativeWindow_fromSurface(env, surface);
}
}
```

#### 内存管理桥接
```cpp
// NioUtils.cpp - ByteBuffer 处理
extern "C" JNIEXPORT jlong JNICALL
Java_com_google_android_filament_NioUtils_getDirectBufferAddress(JNIEnv* env, jclass,
        jobject buffer) {
    return (jlong) env->GetDirectBufferAddress(buffer);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_google_android_filament_NioUtils_getDirectBufferCapacity(JNIEnv* env, jclass,
        jobject buffer) {
    return (jlong) env->GetDirectBufferCapacity(buffer);
}
```

### 4. 渲染后端架构

#### 后端选择机制
```cpp
// Engine.cpp - 后端创建
Engine* Engine::create(Backend backend, Platform* platform, void* sharedContext) {
    FEngine* instance = FEngine::create(backend, platform, sharedContext);
    return instance;
}

// 支持的后端类型
enum class Backend : uint8_t {
    DEFAULT = 0,
    OPENGL = 1,
    VULKAN = 2,
    WEBGPU = 3,
    NOOP = 4,
};
```

#### Android 特定的 Vulkan 实现
```cpp
// VulkanPlatformAndroid.cpp
VulkanPlatform::SurfaceBundle VulkanPlatformAndroid::createVkSurfaceKHR(
        void* nativeWindow, VkInstance instance, uint64_t flags) const noexcept {
    VkAndroidSurfaceCreateInfoKHR const createInfo = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .window = (ANativeWindow*) nativeWindow,
    };

    VkSurfaceKHR surface;
    VkResult const result = vkCreateAndroidSurfaceKHR(instance, &createInfo, VKALLOC, &surface);

    return { surface, extent };
}
```

## 线程模型

### 单线程渲染设计

```kotlin
class FrameCallback : Choreographer.FrameCallback {
    override fun doFrame(frameTimeNanos: Long) {
        // 调度下一帧
        choreographer.postFrameCallback(this)

        // 检查 SwapChain 是否准备就绪
        if (uiHelper.isReadyToRender) {
            // 开始帧渲染
            if (renderer.beginFrame(swapChain!!, frameTimeNanos)) {
                renderer.render(view)
                renderer.endFrame()
            }
        }
    }
}
```

### 引擎线程安全
```kotlin
// 引擎必须在单一线程中访问
class FilamentRenderer {
    private val renderHandler = Handler(renderThread.looper)

    fun render() {
        renderHandler.post {
            // 所有 Filament 操作必须在同一线程执行
            if (renderer.beginFrame(swapChain!!, frameTimeNanos)) {
                renderer.render(view)
                renderer.endFrame()
            }
        }
    }
}
```

## 内存管理架构

### 原生内存管理
```kotlin
class ResourceManager {
    private val nativeResources = mutableListOf<Long>()

    fun destroyResources() {
        // 按正确顺序销毁资源
        engine.destroyRenderer(renderer)
        engine.destroyVertexBuffer(vertexBuffer)
        engine.destroyIndexBuffer(indexBuffer)
        engine.destroyMaterial(material)
        engine.destroyView(view)
        engine.destroyScene(scene)
        engine.destroyCameraComponent(camera.entity)

        // 销毁实体
        val entityManager = EntityManager.get()
        entityManager.destroy(renderable)
        entityManager.destroy(camera.entity)

        // 最后销毁引擎
        engine.destroy()
    }
}
```

### Java 层内存优化
```kotlin
// 使用 WeakReference 避免循环引用
class MaterialInstance {
    private val materialRef: WeakReference<Material>

    fun setParameter(name: String, value: Float) {
        materialRef.get()?.let { material ->
            nSetFloatParameter(getNativeObject(), name, value)
        }
    }
}
```

## 表面管理系统

### SurfaceView 生命周期
```kotlin
inner class SurfaceCallback : UiHelper.RendererCallback {
    override fun onNativeWindowChanged(surface: Surface) {
        // 销毁旧的 SwapChain
        swapChain?.let { engine.destroySwapChain(it) }

        // 创建新的 SwapChain
        var flags = uiHelper.swapChainFlags
        if (engine.activeFeatureLevel == Engine.FeatureLevel.FEATURE_LEVEL_0) {
            if (SwapChain.isSRGBSwapChainSupported(engine)) {
                flags = flags or SwapChainFlags.CONFIG_SRGB_COLORSPACE
            }
        }

        swapChain = engine.createSwapChain(surface, flags)
        displayHelper.attach(renderer, surfaceView.display)
    }

    override fun onDetachedFromSurface() {
        displayHelper.detach()
        swapChain?.let {
            engine.destroySwapChain(it)
            engine.flushAndWait() // 确保销毁完成
            swapChain = null
        }
    }

    override fun onResized(width: Int, height: Int) {
        val aspect = width.toDouble() / height.toDouble()
        camera.setProjection(Camera.Projection.PERSPECTIVE,
                45.0, aspect, 0.1, 20.0)
        view.viewport = Viewport(0, 0, width, height)
        FilamentHelper.synchronizePendingFrames(engine)
    }
}
```

### 硬件缓冲区集成
```cpp
// VulkanPlatformAndroid.cpp - AHardwareBuffer 支持
Platform::ExternalImageHandle VulkanPlatformAndroid::createExternalImage(
        AHardwareBuffer const* buffer, bool sRGB) noexcept {
    if (__builtin_available(android 26, *)) {
        auto bufferImpl = const_cast<AHardwareBuffer*>(buffer);
        AHardwareBuffer_acquire(bufferImpl);

        AHardwareBuffer_Desc hardwareBufferDescription = {};
        AHardwareBuffer_describe(buffer, &hardwareBufferDescription);

        auto* const p = new (std::nothrow) ExternalImageVulkanAndroid;
        p->aHardwareBuffer = const_cast<AHardwareBuffer*>(buffer);
        p->sRGB = sRGB;
        return Platform::ExternalImageHandle{ p };
    }

    return Platform::ExternalImageHandle{};
}
```

## 性能优化架构

### 显示同步
```kotlin
class DisplayHelper(private val activity: Activity) {
    fun attach(renderer: Renderer, display: Display) {
        // 获取显示刷新率
        val refreshRate = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            display.refreshRate
        } else {
            60.0f
        }

        // 设置渲染器刷新率
        renderer.setDisplayInfo(refreshRate, 0L)
    }
}
```

### 内存预分配
```kotlin
// Engine 配置优化
val config = Engine.Config().apply {
    commandBufferSizeMB = 4        // 增加命令缓冲区
    perRenderPassArenaSizeMB = 2   // 增加渲染通道内存
    jobSystemThreadCount = 2       // 限制线程数量
}

val engine = Engine.Builder()
    .config(config)
    .backend(Engine.Backend.VULKAN)
    .featureLevel(Engine.FeatureLevel.FEATURE_LEVEL_1)
    .build()
```

## 调试和监控

### 性能监控
```kotlin
class PerformanceMonitor {
    fun startFrame() {
        val frameStart = System.nanoTime()
        // 开始帧监控
    }

    fun endFrame() {
        val frameTime = System.nanoTime() - frameStart
        if (frameTime > 16_666_666L) { // 超过 16.67ms (60 FPS)
            Log.w("Performance", "Frame time: ${frameTime / 1_000_000}ms")
        }
    }
}
```

### 内存监控
```kotlin
class MemoryMonitor {
    fun checkMemoryUsage() {
        val runtime = Runtime.getRuntime()
        val usedMemory = runtime.totalMemory() - runtime.freeMemory()
        val maxMemory = runtime.maxMemory()

        if (usedMemory > maxMemory * 0.8) {
            Log.w("Memory", "High memory usage: ${usedMemory / 1024 / 1024}MB")
            // 触发内存清理
            engine.flushAndWait()
        }
    }
}
```

这个架构设计确保了 Filament 在 Android 平台上的高性能和稳定性，同时提供了灵活的 API 供开发者使用。