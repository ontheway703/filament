# 平台调试工具

## 📖 概述

不同平台提供了专门的图形调试工具,它们深度集成了各自的图形 API 和硬件特性,能够提供比通用工具更详细的性能和错误信息。本文档介绍如何使用主流平台的专用调试工具来调试 Filament 应用。

**本文涵盖**:
- NVIDIA Nsight Graphics (Windows/Linux)
- Microsoft PIX (Windows/Xbox)
- Apple Metal Debugger (macOS/iOS)
- Android GPU Inspector (Android)
- Intel Graphics Performance Analyzers
- AMD Radeon GPU Profiler

**适用场景**:
- 平台特定问题诊断
- 深度性能分析
- GPU 硬件特性调试
- 驱动级别问题定位

---

## 1. NVIDIA Nsight Graphics

### 1.1 工具介绍

Nsight Graphics 是 NVIDIA 提供的图形调试和性能分析工具,支持 DirectX、Vulkan 和 OpenGL。

**主要功能**:
- Frame Debugger: 帧级别的调试
- GPU Trace: 详细的 GPU 性能追踪
- Shader Profiler: 着色器性能分析
- Memory Profiler: GPU 内存使用分析

### 1.2 集成到 Filament 项目

```cpp
// NsightIntegration.h
#pragma once

#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

/**
 * Nsight Graphics 集成类
 *
 * 功能:
 * - 启用 Nsight 标记
 * - 添加性能标记
 * - 触发帧捕获
 */
class NsightIntegration {
public:
    /**
     * 初始化 Nsight 集成
     */
    static bool initialize();

    /**
     * 检查 Nsight 是否可用
     */
    static bool isAvailable();

    /**
     * 开始性能标记
     */
    static void beginEvent(const char* name);

    /**
     * 结束性能标记
     */
    static void endEvent();

    /**
     * 添加标记
     */
    static void setMarker(const char* name);

    /**
     * 触发帧捕获
     */
    static void triggerCapture();

    /**
     * RAII 风格的事件标记
     */
    class ScopedEvent {
    public:
        explicit ScopedEvent(const char* name) : mName(name) {
            NsightIntegration::beginEvent(name);
        }

        ~ScopedEvent() {
            NsightIntegration::endEvent();
        }

    private:
        const char* mName;
    };

private:
    static bool sInitialized;
    static bool sAvailable;
};

// 便捷宏
#ifdef NSIGHT_ENABLED
#define NSIGHT_EVENT(name) NsightIntegration::ScopedEvent __nsight_event__(name)
#define NSIGHT_MARKER(name) NsightIntegration::setMarker(name)
#else
#define NSIGHT_EVENT(name)
#define NSIGHT_MARKER(name)
#endif
```

```cpp
// NsightIntegration.cpp
#include "NsightIntegration.h"

#ifdef NSIGHT_ENABLED
#ifdef _WIN32
#include <nvtx3/nvToolsExt.h>
#pragma comment(lib, "nvToolsExt64_1.lib")
#endif
#endif

bool NsightIntegration::sInitialized = false;
bool NsightIntegration::sAvailable = false;

bool NsightIntegration::initialize() {
    if (sInitialized) {
        return sAvailable;
    }

    sInitialized = true;

#ifdef NSIGHT_ENABLED
    // 检查 Nsight 是否正在运行
    // 通过环境变量检测
#ifdef _WIN32
    char* nsightAttached = nullptr;
    size_t len = 0;
    _dupenv_s(&nsightAttached, &len, "NSIGHT_CUDA_DEBUGGER");

    if (nsightAttached != nullptr) {
        sAvailable = true;
        free(nsightAttached);
    }
#endif
#endif

    return sAvailable;
}

bool NsightIntegration::isAvailable() {
    return sAvailable;
}

void NsightIntegration::beginEvent(const char* name) {
#ifdef NSIGHT_ENABLED
    if (!sAvailable) return;

#ifdef _WIN32
    nvtxEventAttributes_t eventAttrib = {0};
    eventAttrib.version = NVTX_VERSION;
    eventAttrib.size = NVTX_EVENT_ATTRIB_STRUCT_SIZE;
    eventAttrib.messageType = NVTX_MESSAGE_TYPE_ASCII;
    eventAttrib.message.ascii = name;
    nvtxRangePushEx(&eventAttrib);
#endif
#endif
}

void NsightIntegration::endEvent() {
#ifdef NSIGHT_ENABLED
    if (!sAvailable) return;

#ifdef _WIN32
    nvtxRangePop();
#endif
#endif
}

void NsightIntegration::setMarker(const char* name) {
#ifdef NSIGHT_ENABLED
    if (!sAvailable) return;

#ifdef _WIN32
    nvtxMarkA(name);
#endif
#endif
}

void NsightIntegration::triggerCapture() {
#ifdef NSIGHT_ENABLED
    if (!sAvailable) return;

    // Nsight 捕获通常通过 UI 触发
    // 这里可以添加自定义标记以便识别关键帧
    setMarker("CAPTURE_FRAME");
#endif
}
```

### 1.3 在渲染循环中使用

```cpp
// FilamentRenderer.cpp
#include "NsightIntegration.h"

class FilamentRenderer {
public:
    void initialize() {
        // 初始化 Nsight 集成
        NsightIntegration::initialize();

        // 初始化 Filament
        mEngine = Engine::create();
        mSwapChain = mEngine->createSwapChain(nativeWindow);
        mRenderer = mEngine->createRenderer();

        setupScene();
    }

    void renderFrame() {
        NSIGHT_EVENT("FilamentFrame");

        // 开始帧
        {
            NSIGHT_EVENT("BeginFrame");
            if (mRenderer->beginFrame(mSwapChain)) {

                // 更新场景
                {
                    NSIGHT_EVENT("UpdateScene");
                    updateScene();
                }

                // 渲染
                {
                    NSIGHT_EVENT("RenderView");
                    mRenderer->render(mView);
                }

                // 结束帧
                {
                    NSIGHT_EVENT("EndFrame");
                    mRenderer->endFrame();
                }
            }
        }

        mFrameCount++;

        // 每 100 帧触发一次捕获标记
        if (mFrameCount % 100 == 0) {
            NSIGHT_MARKER("Frame_100");
        }
    }

private:
    void updateScene() {
        NSIGHT_EVENT("AnimationUpdate");
        // 更新动画

        NSIGHT_EVENT("PhysicsUpdate");
        // 更新物理

        NSIGHT_EVENT("CullingUpdate");
        // 更新裁剪
    }

    Engine* mEngine;
    SwapChain* mSwapChain;
    Renderer* mRenderer;
    View* mView;
    Scene* mScene;
    uint64_t mFrameCount = 0;
};
```

### 1.4 Nsight 使用流程

**1. 启动应用并附加**:
```bash
# 方式 1: 直接在 Nsight Graphics 中启动应用
# File > Launch Activity > Browse to your .exe

# 方式 2: 附加到运行中的应用
# File > Attach to Process
```

**2. 捕获帧**:
- 点击 "Capture Frame" 按钮
- 或按快捷键 (默认 Ctrl+Z)
- 或通过代码中的 `NSIGHT_MARKER` 标记关键帧

**3. 分析捕获数据**:

**Frame Debugger**:
```
1. 查看所有 Draw Call 列表
2. 点击某个 Draw Call 查看:
   - Input Assembler 状态
   - Vertex Shader 输入/输出
   - Fragment Shader 输入/输出
   - Texture 绑定
   - Framebuffer 状态
3. 查看每个阶段的输出纹理
```

**GPU Trace**:
```
1. 切换到 "GPU Trace" 标签
2. 查看每个 Draw Call 的耗时
3. 识别性能瓶颈:
   - GPU Idle 时间 (表示 CPU bound)
   - Long Draw Calls (表示 GPU bound)
   - Memory Transfer 时间
```

**Shader Profiler**:
```
1. 选择一个 Draw Call
2. 点击 "Profile Shader"
3. 查看:
   - 每条指令的执行时间
   - Register 使用情况
   - Memory 访问模式
   - Occupancy 信息
```

### 1.5 常见问题诊断

**问题 1: Draw Call 耗时异常长**

```cpp
// 诊断步骤:
// 1. 在 Nsight 中查看 GPU Trace
// 2. 定位耗时长的 Draw Call
// 3. 查看 Shader Profiler 分析着色器性能

// 可能原因及解决方案:
class DrawCallOptimizer {
public:
    // 原因 1: 复杂的 Fragment Shader
    void optimizeFragmentShader() {
        // 减少纹理采样
        // 简化光照计算
        // 使用 LOD 系统
    }

    // 原因 2: 过多的顶点
    void optimizeMesh() {
        // 使用 LOD
        // 合并小网格
        // 优化索引缓冲
    }

    // 原因 3: Overdraw 过高
    void reduceOverdraw() {
        // 启用深度预通道
        // 优化绘制顺序
        // 使用遮挡剔除
    }
};
```

**问题 2: GPU 内存带宽瓶颈**

```cpp
// 在 Nsight 中查看 Memory Profiler:
// - L2 Cache Hit Rate
// - DRAM 访问量
// - Texture 访问模式

class BandwidthOptimizer {
public:
    void optimizeTextureAccess() {
        // 使用纹理压缩
        TextureUsage usage = TextureUsage::SAMPLEABLE;
        Format compressedFormat = Format::ETC2_EAC_RGBA8; // Android

        Texture* texture = Texture::Builder()
            .width(width)
            .height(height)
            .levels(mipLevels)
            .format(compressedFormat)
            .usage(usage)
            .build(*mEngine);
    }

    void improveDataLocality() {
        // 优化数据布局
        // 使用 Struct of Arrays (SoA) 而不是 Array of Structs (AoS)

        // 不好的布局 (AoS)
        struct Vertex_Bad {
            float3 position;
            float3 normal;
            float2 uv;
        };

        // 好的布局 (SoA)
        struct VertexBuffer_Good {
            std::vector<float3> positions;
            std::vector<float3> normals;
            std::vector<float2> uvs;
        };
    }
};
```

---

## 2. Microsoft PIX

### 2.1 工具介绍

PIX 是 Microsoft 提供的 DirectX 调试和性能分析工具,支持 Windows 和 Xbox。

**主要功能**:
- GPU Capture: 捕获 GPU 工作负载
- Timing Capture: 详细的 CPU/GPU 时间分析
- Memory Analysis: 内存使用分析
- Callgraph: 函数调用图

### 2.2 PIX 标记集成

```cpp
// PIXIntegration.h
#pragma once

#ifdef _WIN32
#include <pix3.h>
#endif

/**
 * PIX 集成类
 */
class PIXIntegration {
public:
    static void beginEvent(const char* name);
    static void endEvent();
    static void setMarker(const char* name);

    class ScopedEvent {
    public:
        explicit ScopedEvent(const char* name) {
            PIXIntegration::beginEvent(name);
        }

        ~ScopedEvent() {
            PIXIntegration::endEvent();
        }
    };
};

#ifdef PIX_ENABLED
#define PIX_EVENT(name) PIXIntegration::ScopedEvent __pix_event__(name)
#define PIX_MARKER(name) PIXIntegration::setMarker(name)
#else
#define PIX_EVENT(name)
#define PIX_MARKER(name)
#endif
```

```cpp
// PIXIntegration.cpp
#include "PIXIntegration.h"

void PIXIntegration::beginEvent(const char* name) {
#ifdef PIX_ENABLED
#ifdef _WIN32
    PIXBeginEvent(PIX_COLOR_DEFAULT, name);
#endif
#endif
}

void PIXIntegration::endEvent() {
#ifdef PIX_ENABLED
#ifdef _WIN32
    PIXEndEvent();
#endif
#endif
}

void PIXIntegration::setMarker(const char* name) {
#ifdef PIX_ENABLED
#ifdef _WIN32
    PIXSetMarker(PIX_COLOR_DEFAULT, name);
#endif
#endif
}
```

### 2.3 使用 PIX 分析性能

```cpp
class PerformanceAnalyzer {
public:
    void analyzeFrame() {
        PIX_EVENT("PerformanceAnalysis");

        // CPU 侧性能分析
        {
            PIX_EVENT("CPUWork");

            PIX_EVENT("SceneUpdate");
            updateScene();

            PIX_EVENT("CullingAndSorting");
            performCulling();
            sortRenderables();
        }

        // GPU 侧渲染
        {
            PIX_EVENT("GPUWork");

            PIX_EVENT("ShadowPass");
            renderShadows();

            PIX_EVENT("GBufferPass");
            renderGBuffer();

            PIX_EVENT("LightingPass");
            renderLighting();

            PIX_EVENT("PostProcessing");
            renderPostProcess();
        }
    }

private:
    void updateScene();
    void performCulling();
    void sortRenderables();
    void renderShadows();
    void renderGBuffer();
    void renderLighting();
    void renderPostProcess();
};
```

### 2.4 PIX 使用流程

**1. GPU Capture (帧捕获)**:
```
1. 启动 PIX
2. 选择 "GPU Capture"
3. 选择要调试的可执行文件
4. 点击 "Launch" 启动应用
5. 按 Print Screen 捕获当前帧
6. 分析捕获的数据
```

**2. Timing Capture (性能分析)**:
```
1. 选择 "Timing Capture"
2. 启动应用
3. 捕获一段时间的性能数据 (通常 1-2 秒)
4. 查看 CPU/GPU 时间线
5. 识别性能热点
```

**3. 查看详细信息**:
```
- Pipeline: 查看管线状态
- Resources: 查看资源使用情况
- Events: 查看事件层次结构
- Timing: 查看详细时间信息
```

---

## 3. Apple Metal Debugger

### 3.1 工具介绍

Metal Debugger 集成在 Xcode 中,是调试 macOS 和 iOS 上 Metal 应用的主要工具。

**主要功能**:
- Frame Capture: 帧捕获和分析
- Shader Debugger: 着色器调试
- Dependency Viewer: 依赖关系可视化
- Performance Analysis: 性能分析

### 3.2 Metal 标记集成

```cpp
// MetalDebugIntegration.h
#pragma once

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

/**
 * Metal 调试集成
 */
class MetalDebugIntegration {
public:
    /**
     * 初始化 Metal 调试组
     */
    static void initialize();

    /**
     * 推入调试组
     */
    static void pushDebugGroup(const char* name);

    /**
     * 弹出调试组
     */
    static void popDebugGroup();

    /**
     * 插入调试标记
     */
    static void insertDebugMarker(const char* name);

    /**
     * RAII 风格的调试组
     */
    class ScopedDebugGroup {
    public:
        explicit ScopedDebugGroup(const char* name) {
            MetalDebugIntegration::pushDebugGroup(name);
        }

        ~ScopedDebugGroup() {
            MetalDebugIntegration::popDebugGroup();
        }
    };
};

#ifdef METAL_DEBUG_ENABLED
#define METAL_DEBUG_GROUP(name) MetalDebugIntegration::ScopedDebugGroup __metal_group__(name)
#define METAL_DEBUG_MARKER(name) MetalDebugIntegration::insertDebugMarker(name)
#else
#define METAL_DEBUG_GROUP(name)
#define METAL_DEBUG_MARKER(name)
#endif
```

```cpp
// MetalDebugIntegration.mm (Objective-C++)
#include "MetalDebugIntegration.h"

#ifdef __APPLE__
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

// 全局命令缓冲区引用 (用于添加调试标记)
static id<MTLCommandBuffer> gCurrentCommandBuffer = nil;

void MetalDebugIntegration::initialize() {
    // Metal 调试功能会自动启用
    // 确保在 Xcode Scheme 中启用 Metal API Validation
}

void MetalDebugIntegration::pushDebugGroup(const char* name) {
#ifdef METAL_DEBUG_ENABLED
    if (gCurrentCommandBuffer != nil) {
        NSString* nsName = [NSString stringWithUTF8String:name];
        [gCurrentCommandBuffer pushDebugGroup:nsName];
    }
#endif
}

void MetalDebugIntegration::popDebugGroup() {
#ifdef METAL_DEBUG_ENABLED
    if (gCurrentCommandBuffer != nil) {
        [gCurrentCommandBuffer popDebugGroup];
    }
#endif
}

void MetalDebugIntegration::insertDebugMarker(const char* name) {
#ifdef METAL_DEBUG_ENABLED
    if (gCurrentCommandBuffer != nil) {
        NSString* nsName = [NSString stringWithUTF8String:name];
        [gCurrentCommandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
            NSLog(@"Metal Marker: %@", nsName);
        }];
    }
#endif
}

// 辅助函数: 设置当前命令缓冲区
void setCurrentMetalCommandBuffer(id<MTLCommandBuffer> buffer) {
    gCurrentCommandBuffer = buffer;
}

#endif // __APPLE__
```

### 3.3 在 Filament 中使用

```cpp
// FilamentMetalRenderer.cpp
class FilamentMetalRenderer {
public:
    void renderFrame() {
        METAL_DEBUG_GROUP("FilamentFrame");

        if (mRenderer->beginFrame(mSwapChain)) {

            // Shadow Pass
            {
                METAL_DEBUG_GROUP("ShadowPass");
                renderShadows();
            }

            // Main Pass
            {
                METAL_DEBUG_GROUP("MainPass");

                METAL_DEBUG_GROUP("OpaqueGeometry");
                renderOpaqueGeometry();

                METAL_DEBUG_GROUP("Skybox");
                renderSkybox();

                METAL_DEBUG_GROUP("TransparentGeometry");
                renderTransparentGeometry();
            }

            // Post Processing
            {
                METAL_DEBUG_GROUP("PostProcessing");

                METAL_DEBUG_GROUP("Bloom");
                renderBloom();

                METAL_DEBUG_GROUP("ToneMapping");
                renderToneMapping();
            }

            mRenderer->endFrame();
        }
    }

private:
    Engine* mEngine;
    Renderer* mRenderer;
    SwapChain* mSwapChain;
    View* mView;
};
```

### 3.4 Xcode Metal Debugger 使用

**1. 启用 Frame Capture**:
```
1. 在 Xcode 中运行应用
2. 点击工具栏的相机图标 (Capture GPU Frame)
3. 或使用快捷键 Cmd+Option+G
```

**2. 查看捕获的帧**:
```
- 左侧面板: 显示所有 Command Buffer 和 Encoder
- 中间区域: 显示选中对象的详细信息
- 右侧面板: 显示资源和状态
```

**3. 调试着色器**:
```
1. 选择一个 Draw Call
2. 点击顶点着色器或片段着色器
3. 查看输入/输出数据
4. 设置断点单步调试
```

**4. 查看依赖关系**:
```
1. 切换到 "View Frame By Dependencies"
2. 查看每个 Draw Call 依赖的资源
3. 优化资源访问模式
```

### 3.5 Metal API Validation

```cpp
// 启用 Metal 验证层
// 在 Xcode Scheme 中设置:
// Edit Scheme > Run > Diagnostics > Metal API Validation

/**
 * Metal 验证助手
 */
class MetalValidation {
public:
    /**
     * 设置对象标签 (便于在调试器中识别)
     */
    static void setObjectLabel(void* metalObject, const char* label) {
#ifdef __APPLE__
        // 实际使用时需要将 void* 转换为对应的 Metal 对象类型
        // 例如: [(id<MTLTexture>)metalObject setLabel:@"MyTexture"];
#endif
    }

    /**
     * 检查常见的 Metal 错误
     */
    static void checkCommonErrors() {
        // 1. 检查纹理格式是否支持
        // 2. 检查缓冲区大小是否对齐
        // 3. 检查采样器状态是否有效
    }
};
```

---

## 4. Android GPU Inspector

### 4.1 工具介绍

Android GPU Inspector (AGI) 是 Google 提供的 Android 图形调试工具,支持 OpenGL ES 和 Vulkan。

**主要功能**:
- Frame Profiling: 帧性能分析
- Memory Tracking: GPU 内存跟踪
- Shader Profiling: 着色器性能分析
- State Inspection: 渲染状态检查

### 4.2 AGI 集成

```cpp
// AGIIntegration.h
#pragma once

#ifdef __ANDROID__
#include <string>

/**
 * Android GPU Inspector 集成
 */
class AGIIntegration {
public:
    /**
     * 初始化 AGI 标记
     */
    static void initialize();

    /**
     * 推入事件
     */
    static void pushEvent(const char* name);

    /**
     * 弹出事件
     */
    static void popEvent();

    /**
     * 设置标记
     */
    static void setMarker(const char* name);

    /**
     * RAII 事件
     */
    class ScopedEvent {
    public:
        explicit ScopedEvent(const char* name) {
            AGIIntegration::pushEvent(name);
        }

        ~ScopedEvent() {
            AGIIntegration::popEvent();
        }
    };
};

#ifdef AGI_ENABLED
#define AGI_EVENT(name) AGIIntegration::ScopedEvent __agi_event__(name)
#define AGI_MARKER(name) AGIIntegration::setMarker(name)
#else
#define AGI_EVENT(name)
#define AGI_MARKER(name)
#endif

#endif // __ANDROID__
```

```cpp
// AGIIntegration.cpp
#include "AGIIntegration.h"

#ifdef __ANDROID__
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

void AGIIntegration::initialize() {
    // AGI 会自动拦截 OpenGL ES 和 Vulkan 调用
    // 确保在设备上安装了 AGI 服务
}

void AGIIntegration::pushEvent(const char* name) {
#ifdef AGI_ENABLED
    // 使用 OpenGL ES 的调试扩展
    if (glPushDebugGroupKHR) {
        glPushDebugGroupKHR(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
    }
#endif
}

void AGIIntegration::popEvent() {
#ifdef AGI_ENABLED
    if (glPopDebugGroupKHR) {
        glPopDebugGroupKHR();
    }
#endif
}

void AGIIntegration::setMarker(const char* name) {
#ifdef AGI_ENABLED
    if (glInsertEventMarkerEXT) {
        glInsertEventMarkerEXT(0, name);
    }
#endif
}

#endif // __ANDROID__
```

### 4.3 Android 渲染循环集成

```cpp
// AndroidFilamentRenderer.cpp
#ifdef __ANDROID__

class AndroidFilamentRenderer {
public:
    void renderFrame() {
        AGI_EVENT("AndroidFrame");

        // 更新场景
        {
            AGI_EVENT("SceneUpdate");
            updateScene();
        }

        // 渲染
        if (mRenderer->beginFrame(mSwapChain)) {

            // 阴影渲染
            {
                AGI_EVENT("ShadowRendering");
                for (int i = 0; i < mShadowCascadeCount; ++i) {
                    AGI_EVENT("ShadowCascade");
                    renderShadowCascade(i);
                }
            }

            // 主渲染
            {
                AGI_EVENT("MainRendering");
                mRenderer->render(mView);
            }

            // UI 渲染
            {
                AGI_EVENT("UIRendering");
                renderUI();
            }

            mRenderer->endFrame();
        }

        mFrameCount++;
    }

private:
    void updateScene() {
        AGI_EVENT("AnimationUpdate");
        // 更新动画

        AGI_EVENT("LODUpdate");
        // 更新 LOD

        AGI_EVENT("CullingUpdate");
        // 更新裁剪
    }

    void renderShadowCascade(int index) {
        // 渲染单个阴影级联
    }

    void renderUI() {
        // 渲染 UI 元素
    }

    Engine* mEngine;
    Renderer* mRenderer;
    SwapChain* mSwapChain;
    View* mView;
    int mShadowCascadeCount = 3;
    uint64_t mFrameCount = 0;
};

#endif // __ANDROID__
```

### 4.4 AGI 使用流程

**1. 设置设备**:
```bash
# 1. 在 Android 设备上安装 AGI 服务
adb install -r AndroidGPUInspector.apk

# 2. 启动 AGI 服务
adb shell am start -n com.google.android.gapid/.DeviceSetupActivity

# 3. 确保设备开启了 Developer Options 和 USB Debugging
```

**2. 连接并捕获**:
```
1. 打开 Android GPU Inspector
2. 选择连接的设备
3. 选择要调试的应用
4. 点击 "Capture" 捕获当前帧
5. 或配置连续捕获进行性能分析
```

**3. 分析捕获数据**:
```
- Commands: 查看所有 OpenGL/Vulkan 命令
- Framebuffer: 查看渲染目标
- Textures: 查看纹理资源
- Shaders: 查看着色器源码
- Memory: 查看 GPU 内存使用
```

### 4.5 常见 Android 问题诊断

```cpp
/**
 * Android 特定问题诊断器
 */
class AndroidDebugger {
public:
    /**
     * 检查纹理格式兼容性
     */
    static void checkTextureFormats() {
        // 不是所有 Android 设备都支持所有纹理格式

        // 检查 ETC2 支持 (OpenGL ES 3.0+)
        GLint compressed;
        glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &compressed);

        std::vector<GLint> formats(compressed);
        glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, formats.data());

        bool supportsETC2 = false;
        for (GLint format : formats) {
            if (format == GL_COMPRESSED_RGB8_ETC2 ||
                format == GL_COMPRESSED_RGBA8_ETC2_EAC) {
                supportsETC2 = true;
                break;
            }
        }

        if (!supportsETC2) {
            utils::slog.w << "Device does not support ETC2 compression" << utils::io::endl;
        }
    }

    /**
     * 检查 GPU 性能级别
     */
    static void checkPerformanceTier() {
        AGI_EVENT("PerformanceCheck");

        // 获取 GPU 渲染器信息
        const char* renderer = (const char*)glGetString(GL_RENDERER);
        const char* vendor = (const char*)glGetString(GL_VENDOR);

        utils::slog.i << "GPU Vendor: " << vendor << utils::io::endl;
        utils::slog.i << "GPU Renderer: " << renderer << utils::io::endl;

        // 根据 GPU 型号调整渲染设置
        if (strstr(renderer, "Adreno")) {
            // Qualcomm Adreno GPU
            utils::slog.i << "Detected Adreno GPU" << utils::io::endl;
        } else if (strstr(renderer, "Mali")) {
            // ARM Mali GPU
            utils::slog.i << "Detected Mali GPU" << utils::io::endl;
        } else if (strstr(renderer, "PowerVR")) {
            // PowerVR GPU
            utils::slog.i << "Detected PowerVR GPU" << utils::io::endl;
        }
    }

    /**
     * 检查内存限制
     */
    static void checkMemoryConstraints() {
        AGI_EVENT("MemoryCheck");

        // Android 设备通常有较小的 GPU 内存
        // 检查纹理内存使用

        GLint maxTextureSize;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
        utils::slog.i << "Max Texture Size: " << maxTextureSize << utils::io::endl;

        GLint maxCubeMapSize;
        glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maxCubeMapSize);
        utils::slog.i << "Max Cubemap Size: " << maxCubeMapSize << utils::io::endl;
    }
};
```

---

## 5. 跨平台调试策略

### 5.1 统一调试接口

```cpp
// UnifiedDebugInterface.h
#pragma once

#include <string>

/**
 * 统一的调试接口
 *
 * 自动选择合适的平台调试工具
 */
class UnifiedDebugInterface {
public:
    /**
     * 初始化调试系统
     */
    static void initialize();

    /**
     * 推入调试事件
     */
    static void pushEvent(const char* name);

    /**
     * 弹出调试事件
     */
    static void popEvent();

    /**
     * 设置标记
     */
    static void setMarker(const char* name);

    /**
     * RAII 事件
     */
    class ScopedEvent {
    public:
        explicit ScopedEvent(const char* name) : mName(name) {
            UnifiedDebugInterface::pushEvent(name);
        }

        ~ScopedEvent() {
            UnifiedDebugInterface::popEvent();
        }

    private:
        const char* mName;
    };
};

// 统一宏定义
#define DEBUG_EVENT(name) UnifiedDebugInterface::ScopedEvent __debug_event__(name)
#define DEBUG_MARKER(name) UnifiedDebugInterface::setMarker(name)
```

```cpp
// UnifiedDebugInterface.cpp
#include "UnifiedDebugInterface.h"

#ifdef RENDERDOC_ENABLED
#include "RenderDocIntegration.h"
#endif

#ifdef NSIGHT_ENABLED
#include "NsightIntegration.h"
#endif

#ifdef PIX_ENABLED
#include "PIXIntegration.h"
#endif

#ifdef METAL_DEBUG_ENABLED
#include "MetalDebugIntegration.h"
#endif

#ifdef AGI_ENABLED
#include "AGIIntegration.h"
#endif

void UnifiedDebugInterface::initialize() {
    // 初始化所有可用的调试工具

#ifdef RENDERDOC_ENABLED
    RenderDocIntegration::initialize();
#endif

#ifdef NSIGHT_ENABLED
    NsightIntegration::initialize();
#endif

#ifdef METAL_DEBUG_ENABLED
    MetalDebugIntegration::initialize();
#endif

#ifdef AGI_ENABLED
    AGIIntegration::initialize();
#endif
}

void UnifiedDebugInterface::pushEvent(const char* name) {
#ifdef RENDERDOC_ENABLED
    RenderDocIntegration::beginEvent(name);
#endif

#ifdef NSIGHT_ENABLED
    NsightIntegration::beginEvent(name);
#endif

#ifdef PIX_ENABLED
    PIXIntegration::beginEvent(name);
#endif

#ifdef METAL_DEBUG_ENABLED
    MetalDebugIntegration::pushDebugGroup(name);
#endif

#ifdef AGI_ENABLED
    AGIIntegration::pushEvent(name);
#endif
}

void UnifiedDebugInterface::popEvent() {
#ifdef RENDERDOC_ENABLED
    RenderDocIntegration::endEvent();
#endif

#ifdef NSIGHT_ENABLED
    NsightIntegration::endEvent();
#endif

#ifdef PIX_ENABLED
    PIXIntegration::endEvent();
#endif

#ifdef METAL_DEBUG_ENABLED
    MetalDebugIntegration::popDebugGroup();
#endif

#ifdef AGI_ENABLED
    AGIIntegration::popEvent();
#endif
}

void UnifiedDebugInterface::setMarker(const char* name) {
#ifdef RENDERDOC_ENABLED
    RenderDocIntegration::setMarker(name);
#endif

#ifdef NSIGHT_ENABLED
    NsightIntegration::setMarker(name);
#endif

#ifdef PIX_ENABLED
    PIXIntegration::setMarker(name);
#endif

#ifdef METAL_DEBUG_ENABLED
    MetalDebugIntegration::insertDebugMarker(name);
#endif

#ifdef AGI_ENABLED
    AGIIntegration::setMarker(name);
#endif
}
```

### 5.2 使用统一接口

```cpp
// Application.cpp
#include "UnifiedDebugInterface.h"

class Application {
public:
    void initialize() {
        // 初始化调试系统
        UnifiedDebugInterface::initialize();

        // 初始化 Filament
        DEBUG_EVENT("FilamentInit");
        mEngine = Engine::create();
        mRenderer = mEngine->createRenderer();
        setupScene();
    }

    void renderFrame() {
        DEBUG_EVENT("Frame");

        // 更新
        {
            DEBUG_EVENT("Update");
            updateScene();
        }

        // 渲染
        if (mRenderer->beginFrame(mSwapChain)) {
            DEBUG_EVENT("Render");
            mRenderer->render(mView);
            mRenderer->endFrame();
        }
    }

private:
    Engine* mEngine;
    Renderer* mRenderer;
    SwapChain* mSwapChain;
    View* mView;
};
```

---

## 6. CMakeLists.txt 配置

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.19)
project(FilamentDebugTools)

# 平台检测
if(WIN32)
    set(PLATFORM_WINDOWS TRUE)
elseif(APPLE)
    set(PLATFORM_APPLE TRUE)
elseif(ANDROID)
    set(PLATFORM_ANDROID TRUE)
elseif(UNIX)
    set(PLATFORM_LINUX TRUE)
endif()

# 调试工具选项
option(ENABLE_RENDERDOC "Enable RenderDoc integration" ON)
option(ENABLE_NSIGHT "Enable Nsight Graphics integration" OFF)
option(ENABLE_PIX "Enable PIX integration" OFF)
option(ENABLE_METAL_DEBUG "Enable Metal debugging" OFF)
option(ENABLE_AGI "Enable Android GPU Inspector" OFF)

# 源文件
set(SOURCES
    src/UnifiedDebugInterface.cpp
)

# RenderDoc
if(ENABLE_RENDERDOC)
    list(APPEND SOURCES src/RenderDocIntegration.cpp)
    add_compile_definitions(RENDERDOC_ENABLED)
endif()

# Nsight Graphics
if(ENABLE_NSIGHT AND PLATFORM_WINDOWS)
    list(APPEND SOURCES src/NsightIntegration.cpp)
    add_compile_definitions(NSIGHT_ENABLED)

    # 添加 NVTX 库
    find_library(NVTX_LIB nvToolsExt64_1)
    if(NVTX_LIB)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${NVTX_LIB})
    endif()
endif()

# PIX
if(ENABLE_PIX AND PLATFORM_WINDOWS)
    list(APPEND SOURCES src/PIXIntegration.cpp)
    add_compile_definitions(PIX_ENABLED)

    # 添加 PIX 头文件
    include_directories(${PIX_INCLUDE_DIR})
endif()

# Metal Debugger
if(ENABLE_METAL_DEBUG AND PLATFORM_APPLE)
    list(APPEND SOURCES src/MetalDebugIntegration.mm)
    add_compile_definitions(METAL_DEBUG_ENABLED)

    # 添加 Metal 框架
    find_library(METAL_FRAMEWORK Metal)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
    )
endif()

# Android GPU Inspector
if(ENABLE_AGI AND PLATFORM_ANDROID)
    list(APPEND SOURCES src/AGIIntegration.cpp)
    add_compile_definitions(AGI_ENABLED)
endif()

# 创建库
add_library(${PROJECT_NAME} STATIC ${SOURCES})

# Filament 依赖
target_link_libraries(${PROJECT_NAME} PUBLIC
    filament
    utils
)

# 包含目录
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
```

---

## 7. 常见问题

### Q1: 如何选择合适的调试工具?

**A**: 根据平台和问题类型选择:

| 平台 | 推荐工具 | 适用场景 |
|------|---------|----------|
| Windows (NVIDIA) | Nsight Graphics | GPU 性能分析、着色器调试 |
| Windows (AMD/Intel) | RenderDoc | 通用调试、帧分析 |
| Windows (DirectX) | PIX | DirectX 专用调试 |
| macOS/iOS | Metal Debugger | Metal 应用调试 |
| Android | AGI | Android 图形调试 |
| 跨平台 | RenderDoc | 通用帧捕获和分析 |

### Q2: 调试标记对性能有影响吗?

**A**: 有轻微影响,建议在发布版本中禁用:

```cpp
// 使用编译选项控制
#ifdef DEBUG
    #define ENABLE_DEBUG_MARKERS 1
#else
    #define ENABLE_DEBUG_MARKERS 0
#endif
```

### Q3: 如何在移动设备上使用调试工具?

**A**: 移动设备调试步骤:

```
Android:
1. 安装 AGI 服务
2. 通过 USB 连接设备
3. 在 PC 上运行 AGI 客户端
4. 选择设备和应用进行捕获

iOS:
1. 在 Xcode 中运行应用
2. 使用 Metal Debugger 捕获帧
3. 通过 Instruments 进行性能分析
```

---

## 8. 相关文档

- [debugging/01-debugging-workflow.md](./01-debugging-workflow.md) - 调试工作流程
- [debugging/02-renderdoc-usage.md](./02-renderdoc-usage.md) - RenderDoc 使用
- [debugging/04-performance-profiling.md](./04-performance-profiling.md) - 性能分析
- [debugging/07-mobile-debugging.md](./07-mobile-debugging.md) - 移动端调试

---

## 9. 总结

平台专用调试工具提供了深度的硬件集成和详细的性能数据:

- **NVIDIA Nsight**: 最强大的 GPU 调试和性能分析工具
- **Microsoft PIX**: DirectX 应用的首选工具
- **Metal Debugger**: Apple 平台的官方调试工具
- **Android GPU Inspector**: Android 图形调试的官方工具

建议在开发过程中:
1. 使用 RenderDoc 进行日常调试
2. 使用平台专用工具进行深度性能分析
3. 在发布前使用平台工具进行最终验证

合理使用这些工具可以大幅提升开发效率和应用质量。
