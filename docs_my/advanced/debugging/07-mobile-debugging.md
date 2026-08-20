# 移动端调试

## 📖 概述

移动平台(Android 和 iOS)的调试相比桌面平台有其特殊性:硬件多样性、性能限制、功耗约束、触摸交互等。本文档介绍移动端特有的调试技术和最佳实践。

**本文涵盖**:
- Android 调试工具和技术
- iOS 调试工具和技术
- 移动端性能分析
- 功耗优化
- 碎片化问题处理

**目标**:
- 掌握移动端特有调试技术
- 解决设备碎片化问题
- 优化移动端性能和功耗
- 处理移动端特有问题

---

## 1. Android 调试

### 1.1 Android Studio 集成调试

```cpp
// AndroidDebugBridge.h
#pragma once

#ifdef __ANDROID__

#include <android/log.h>
#include <string>

/**
 * Android 调试桥接
 *
 * 提供 Android 特有的调试功能
 */
class AndroidDebugBridge {
public:
    /**
     * 日志级别
     */
    enum class LogLevel {
        VERBOSE,
        DEBUG,
        INFO,
        WARN,
        ERROR
    };

    /**
     * 输出日志
     */
    static void log(LogLevel level, const char* tag, const char* message) {
        android_LogPriority priority;

        switch (level) {
            case LogLevel::VERBOSE: priority = ANDROID_LOG_VERBOSE; break;
            case LogLevel::DEBUG:   priority = ANDROID_LOG_DEBUG; break;
            case LogLevel::INFO:    priority = ANDROID_LOG_INFO; break;
            case LogLevel::WARN:    priority = ANDROID_LOG_WARN; break;
            case LogLevel::ERROR:   priority = ANDROID_LOG_ERROR; break;
            default:                priority = ANDROID_LOG_INFO;
        }

        __android_log_print(priority, tag, "%s", message);
    }

    /**
     * 格式化日志
     */
    template<typename... Args>
    static void logf(LogLevel level, const char* tag, const char* format, Args... args) {
        android_LogPriority priority = ANDROID_LOG_INFO;

        switch (level) {
            case LogLevel::VERBOSE: priority = ANDROID_LOG_VERBOSE; break;
            case LogLevel::DEBUG:   priority = ANDROID_LOG_DEBUG; break;
            case LogLevel::INFO:    priority = ANDROID_LOG_INFO; break;
            case LogLevel::WARN:    priority = ANDROID_LOG_WARN; break;
            case LogLevel::ERROR:   priority = ANDROID_LOG_ERROR; break;
        }

        __android_log_print(priority, tag, format, args...);
    }

    /**
     * 获取设备信息
     */
    static std::string getDeviceInfo();

    /**
     * 获取 GPU 信息
     */
    static std::string getGPUInfo();

    /**
     * 检查 OpenGL ES 版本
     */
    static std::string getGLESVersion();
};

// 便捷宏
#define ALOG_V(tag, msg) AndroidDebugBridge::log(AndroidDebugBridge::LogLevel::VERBOSE, tag, msg)
#define ALOG_D(tag, msg) AndroidDebugBridge::log(AndroidDebugBridge::LogLevel::DEBUG, tag, msg)
#define ALOG_I(tag, msg) AndroidDebugBridge::log(AndroidDebugBridge::LogLevel::INFO, tag, msg)
#define ALOG_W(tag, msg) AndroidDebugBridge::log(AndroidDebugBridge::LogLevel::WARN, tag, msg)
#define ALOG_E(tag, msg) AndroidDebugBridge::log(AndroidDebugBridge::LogLevel::ERROR, tag, msg)

#endif // __ANDROID__
```

```cpp
// AndroidDebugBridge.cpp
#ifdef __ANDROID__

#include "AndroidDebugBridge.h"
#include <GLES3/gl3.h>
#include <sys/system_properties.h>

std::string AndroidDebugBridge::getDeviceInfo() {
    char manufacturer[PROP_VALUE_MAX];
    char model[PROP_VALUE_MAX];
    char version[PROP_VALUE_MAX];

    __system_property_get("ro.product.manufacturer", manufacturer);
    __system_property_get("ro.product.model", model);
    __system_property_get("ro.build.version.release", version);

    std::string info = "Device: ";
    info += manufacturer;
    info += " ";
    info += model;
    info += " (Android ";
    info += version;
    info += ")";

    return info;
}

std::string AndroidDebugBridge::getGPUInfo() {
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* vendor = (const char*)glGetString(GL_VENDOR);

    std::string info = "GPU: ";
    info += vendor;
    info += " - ";
    info += renderer;

    return info;
}

std::string AndroidDebugBridge::getGLESVersion() {
    const char* version = (const char*)glGetString(GL_VERSION);
    return std::string("OpenGL ES: ") + version;
}

#endif // __ANDROID__
```

### 1.2 使用 Android Studio Profiler

```cpp
// AndroidPerformanceMonitor.h
#pragma once

#ifdef __ANDROID__

#include <chrono>
#include <string>

/**
 * Android 性能监控
 *
 * 与 Android Studio Profiler 集成
 */
class AndroidPerformanceMonitor {
public:
    /**
     * 开始性能追踪
     */
    static void beginTrace(const char* name);

    /**
     * 结束性能追踪
     */
    static void endTrace();

    /**
     * 记录计数器
     */
    static void setCounter(const char* name, int64_t value);

    /**
     * RAII 追踪
     */
    class ScopedTrace {
    public:
        explicit ScopedTrace(const char* name) {
            AndroidPerformanceMonitor::beginTrace(name);
        }

        ~ScopedTrace() {
            AndroidPerformanceMonitor::endTrace();
        }
    };
};

#define ANDROID_TRACE_SCOPE(name) AndroidPerformanceMonitor::ScopedTrace __trace__(name)

#endif // __ANDROID__
```

```cpp
// AndroidPerformanceMonitor.cpp
#ifdef __ANDROID__

#include "AndroidPerformanceMonitor.h"
#include <android/trace.h>

#if __ANDROID_API__ >= 23

void AndroidPerformanceMonitor::beginTrace(const char* name) {
    ATrace_beginSection(name);
}

void AndroidPerformanceMonitor::endTrace() {
    ATrace_endSection();
}

void AndroidPerformanceMonitor::setCounter(const char* name, int64_t value) {
    ATrace_setCounter(name, value);
}

#else

// Fallback for older Android versions
void AndroidPerformanceMonitor::beginTrace(const char* name) {
    // No-op on older Android versions
}

void AndroidPerformanceMonitor::endTrace() {
    // No-op
}

void AndroidPerformanceMonitor::setCounter(const char* name, int64_t value) {
    // No-op
}

#endif

#endif // __ANDROID__
```

### 1.3 在渲染代码中使用

```cpp
// FilamentAndroidRenderer.cpp
#ifdef __ANDROID__

#include "AndroidDebugBridge.h"
#include "AndroidPerformanceMonitor.h"

class FilamentAndroidRenderer {
public:
    void initialize() {
        ANDROID_TRACE_SCOPE("FilamentInit");

        // 输出设备信息
        ALOG_I("Filament", AndroidDebugBridge::getDeviceInfo().c_str());
        ALOG_I("Filament", AndroidDebugBridge::getGPUInfo().c_str());
        ALOG_I("Filament", AndroidDebugBridge::getGLESVersion().c_str());

        // 初始化 Filament
        mEngine = Engine::create();
        mRenderer = mEngine->createRenderer();

        // 检查纹理格式支持
        checkTextureFormatSupport();
    }

    void renderFrame() {
        ANDROID_TRACE_SCOPE("RenderFrame");

        // 更新场景
        {
            ANDROID_TRACE_SCOPE("UpdateScene");
            updateScene();
        }

        // 渲染
        if (mRenderer->beginFrame(mSwapChain)) {
            ANDROID_TRACE_SCOPE("Render");
            mRenderer->render(mView);
            mRenderer->endFrame();
        }

        // 记录性能计数器
        mFrameCount++;
        AndroidPerformanceMonitor::setCounter("FrameCount", mFrameCount);
        AndroidPerformanceMonitor::setCounter("DrawCalls", getDrawCallCount());
    }

private:
    void checkTextureFormatSupport() {
        // 检查 ETC2 支持
        GLint numFormats;
        glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &numFormats);

        std::vector<GLint> formats(numFormats);
        glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, formats.data());

        bool supportsETC2 = false;
        bool supportsASTC = false;

        for (GLint format : formats) {
            if (format == GL_COMPRESSED_RGB8_ETC2) {
                supportsETC2 = true;
            }
            if (format == GL_COMPRESSED_RGBA_ASTC_4x4_KHR) {
                supportsASTC = true;
            }
        }

        ALOG_I("Filament", supportsETC2 ? "ETC2 supported" : "ETC2 NOT supported");
        ALOG_I("Filament", supportsASTC ? "ASTC supported" : "ASTC NOT supported");
    }

    Engine* mEngine;
    Renderer* mRenderer;
    SwapChain* mSwapChain;
    View* mView;
    uint64_t mFrameCount = 0;
};

#endif // __ANDROID__
```

### 1.4 Android logcat 使用

```bash
# 查看 Filament 日志
adb logcat -s Filament:V

# 查看所有日志,过滤 Filament
adb logcat | grep Filament

# 清除日志缓冲
adb logcat -c

# 保存日志到文件
adb logcat -d > logcat.txt

# 实时查看并保存
adb logcat | tee logcat.txt

# 查看 OpenGL 错误
adb logcat -s libGLESv2:E

# 查看系统性能信息
adb shell dumpsys gfxinfo <package_name>
```

---

## 2. iOS 调试

### 2.1 Xcode Instruments

```cpp
// IOSPerformanceMonitor.h
#pragma once

#ifdef __APPLE__
#include <TargetConditionals.h>

/**
 * iOS 性能监控
 *
 * 与 Xcode Instruments 集成
 */
class IOSPerformanceMonitor {
public:
    /**
     * 开始性能追踪
     */
    static void beginTrace(const char* name);

    /**
     * 结束性能追踪
     */
    static void endTrace();

    /**
     * 记录信号点
     */
    static void recordSignpost(const char* name, const char* message);

    /**
     * RAII 追踪
     */
    class ScopedTrace {
    public:
        explicit ScopedTrace(const char* name);
        ~ScopedTrace();

    private:
        const char* mName;
    };
};

#define IOS_TRACE_SCOPE(name) IOSPerformanceMonitor::ScopedTrace __trace__(name)

#endif // __APPLE__
```

```objective-c
// IOSPerformanceMonitor.mm
#ifdef __APPLE__

#import "IOSPerformanceMonitor.h"
#import <os/log.h>
#import <os/signpost.h>

static os_log_t gLog = nullptr;

void IOSPerformanceMonitor::beginTrace(const char* name) {
    if (@available(iOS 12.0, *)) {
        if (gLog == nullptr) {
            gLog = os_log_create("com.filament.app", "Performance");
        }

        os_signpost_id_t signpostID = os_signpost_id_generate(gLog);
        os_signpost_interval_begin(gLog, signpostID, "FilamentTrace", "%s", name);
    }
}

void IOSPerformanceMonitor::endTrace() {
    if (@available(iOS 12.0, *)) {
        if (gLog != nullptr) {
            os_signpost_id_t signpostID = os_signpost_id_generate(gLog);
            os_signpost_interval_end(gLog, signpostID, "FilamentTrace");
        }
    }
}

void IOSPerformanceMonitor::recordSignpost(const char* name, const char* message) {
    if (@available(iOS 12.0, *)) {
        if (gLog == nullptr) {
            gLog = os_log_create("com.filament.app", "Performance");
        }

        os_signpost_event_emit(gLog, OS_SIGNPOST_ID_EXCLUSIVE, name, "%s", message);
    }
}

IOSPerformanceMonitor::ScopedTrace::ScopedTrace(const char* name) : mName(name) {
    IOSPerformanceMonitor::beginTrace(name);
}

IOSPerformanceMonitor::ScopedTrace::~ScopedTrace() {
    IOSPerformanceMonitor::endTrace();
}

#endif // __APPLE__
```

### 2.2 Metal 调试

```objective-c
// MetalDebugHelper.h
#pragma once

#ifdef __APPLE__
#import <Metal/Metal.h>

/**
 * Metal 调试助手
 */
class MetalDebugHelper {
public:
    /**
     * 启用 Metal 调试
     */
    static void enableMetalValidation();

    /**
     * 设置 Metal 对象标签
     */
    static void setLabel(id<MTLTexture> texture, const char* label);
    static void setLabel(id<MTLBuffer> buffer, const char* label);

    /**
     * 捕获 Metal 帧
     */
    static void captureFrame(id<MTLCommandQueue> commandQueue);
};

#endif // __APPLE__
```

```objective-c
// MetalDebugHelper.mm
#ifdef __APPLE__

#import "MetalDebugHelper.h"

void MetalDebugHelper::enableMetalValidation() {
    // Metal 验证层通过 Xcode Scheme 启用:
    // Edit Scheme > Run > Diagnostics > Metal API Validation

    NSLog(@"Metal API Validation should be enabled in Xcode Scheme");
}

void MetalDebugHelper::setLabel(id<MTLTexture> texture, const char* label) {
    if (texture != nil) {
        texture.label = [NSString stringWithUTF8String:label];
    }
}

void MetalDebugHelper::setLabel(id<MTLBuffer> buffer, const char* label) {
    if (buffer != nil) {
        buffer.label = [NSString stringWithUTF8String:label];
    }
}

void MetalDebugHelper::captureFrame(id<MTLCommandQueue> commandQueue) {
    // 程序化触发 Metal 帧捕获
    if (@available(iOS 13.0, *)) {
        MTLCaptureManager* captureManager = [MTLCaptureManager sharedCaptureManager];

        MTLCaptureDescriptor* descriptor = [[MTLCaptureDescriptor alloc] init];
        descriptor.captureObject = commandQueue;

        NSError* error;
        if ([captureManager startCaptureWithDescriptor:descriptor error:&error]) {
            NSLog(@"Metal capture started");
        } else {
            NSLog(@"Failed to start Metal capture: %@", error);
        }
    }
}

#endif // __APPLE__
```

### 2.3 使用 Instruments 分析性能

```
1. Time Profiler (CPU 性能分析):
   - Xcode > Product > Profile (Cmd+I)
   - 选择 "Time Profiler"
   - Record > 运行应用
   - 查看热点函数

2. Metal System Trace (GPU 性能):
   - 选择 "Metal System Trace"
   - 查看 GPU 时间线
   - 分析 Shader 性能
   - 识别 CPU/GPU 同步点

3. Allocations (内存分析):
   - 选择 "Allocations"
   - 跟踪内存分配
   - 查找内存泄漏

4. Leaks (内存泄漏检测):
   - 选择 "Leaks"
   - 自动检测内存泄漏
   - 查看泄漏调用栈
```

---

## 3. 移动端特有问题

### 3.1 设备碎片化处理

```cpp
// DeviceCapabilityManager.h
#pragma once

#include <string>
#include <unordered_map>

/**
 * 设备能力管理器
 *
 * 处理设备碎片化问题
 */
class DeviceCapabilityManager {
public:
    /**
     * 性能等级
     */
    enum class PerformanceTier {
        LOW,     // 低端设备
        MEDIUM,  // 中端设备
        HIGH     // 高端设备
    };

    /**
     * 设备能力
     */
    struct DeviceCapabilities {
        PerformanceTier tier;
        bool supportsETC2;
        bool supportsASTC;
        bool supportsHDR;
        int maxTextureSize;
        int maxAnisotropy;
        std::string gpuFamily;
    };

public:
    /**
     * 检测设备能力
     */
    static DeviceCapabilities detectCapabilities();

    /**
     * 根据设备能力调整渲染设置
     */
    static void applyQualitySettings(const DeviceCapabilities& caps);

    /**
     * 获取推荐纹理格式
     */
    static TextureFormat getRecommendedTextureFormat(const DeviceCapabilities& caps);

private:
    static PerformanceTier detectPerformanceTier();
    static std::string detectGPUFamily();
};
```

```cpp
// DeviceCapabilityManager.cpp
#include "DeviceCapabilityManager.h"
#include <GLES3/gl3.h>

DeviceCapabilityManager::DeviceCapabilities DeviceCapabilityManager::detectCapabilities() {
    DeviceCapabilities caps;

    // 检测性能等级
    caps.tier = detectPerformanceTier();

    // 检测纹理压缩格式
    GLint numFormats;
    glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &numFormats);

    std::vector<GLint> formats(numFormats);
    glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, formats.data());

    caps.supportsETC2 = false;
    caps.supportsASTC = false;

    for (GLint format : formats) {
        if (format == GL_COMPRESSED_RGB8_ETC2 ||
            format == GL_COMPRESSED_RGBA8_ETC2_EAC) {
            caps.supportsETC2 = true;
        }
        if (format == GL_COMPRESSED_RGBA_ASTC_4x4_KHR) {
            caps.supportsASTC = true;
        }
    }

    // 检测纹理大小限制
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &caps.maxTextureSize);

    // 检测各向异性过滤
    if (glGetFloatv) {
        GLfloat maxAniso;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        caps.maxAnisotropy = static_cast<int>(maxAniso);
    } else {
        caps.maxAnisotropy = 1;
    }

    // HDR 支持
    caps.supportsHDR = false;  // 需要检测具体实现

    // GPU 家族
    caps.gpuFamily = detectGPUFamily();

    return caps;
}

void DeviceCapabilityManager::applyQualitySettings(const DeviceCapabilities& caps) {
    switch (caps.tier) {
        case PerformanceTier::LOW:
            // 低端设备设置
            // - 降低分辨率 (720p)
            // - 禁用后处理
            // - 简化阴影
            // - 降低纹理质量
            utils::slog.i << "Applying LOW quality settings" << utils::io::endl;
            break;

        case PerformanceTier::MEDIUM:
            // 中端设备设置
            // - 中等分辨率 (1080p)
            // - 基础后处理
            // - 单级阴影
            // - 中等纹理质量
            utils::slog.i << "Applying MEDIUM quality settings" << utils::io::endl;
            break;

        case PerformanceTier::HIGH:
            // 高端设备设置
            // - 高分辨率 (1440p+)
            // - 完整后处理
            // - 多级阴影
            // - 高纹理质量
            utils::slog.i << "Applying HIGH quality settings" << utils::io::endl;
            break;
    }
}

DeviceCapabilityManager::PerformanceTier DeviceCapabilityManager::detectPerformanceTier() {
    const char* renderer = (const char*)glGetString(GL_RENDERER);

    // 根据 GPU 型号判断性能等级
    std::string rendererStr(renderer);

    // Qualcomm Adreno
    if (rendererStr.find("Adreno") != std::string::npos) {
        if (rendererStr.find("Adreno (TM) 6") != std::string::npos ||
            rendererStr.find("Adreno (TM) 7") != std::string::npos) {
            return PerformanceTier::HIGH;
        } else if (rendererStr.find("Adreno (TM) 5") != std::string::npos) {
            return PerformanceTier::MEDIUM;
        } else {
            return PerformanceTier::LOW;
        }
    }

    // ARM Mali
    if (rendererStr.find("Mali") != std::string::npos) {
        if (rendererStr.find("Mali-G7") != std::string::npos ||
            rendererStr.find("Mali-G") != std::string::npos) {
            return PerformanceTier::HIGH;
        } else {
            return PerformanceTier::MEDIUM;
        }
    }

    // Apple GPU
    if (rendererStr.find("Apple") != std::string::npos) {
        // Apple GPU 通常性能较好
        return PerformanceTier::HIGH;
    }

    // 默认中等
    return PerformanceTier::MEDIUM;
}

std::string DeviceCapabilityManager::detectGPUFamily() {
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    return std::string(renderer);
}

TextureFormat DeviceCapabilityManager::getRecommendedTextureFormat(const DeviceCapabilities& caps) {
    if (caps.supportsASTC) {
        // ASTC 提供更好的质量和压缩率
        return TextureFormat::ASTC_4x4;
    } else if (caps.supportsETC2) {
        // ETC2 是 OpenGL ES 3.0+ 的标准
        return TextureFormat::ETC2_RGB8;
    } else {
        // 回退到未压缩格式
        return TextureFormat::RGB8;
    }
}
```

### 3.2 功耗优化

```cpp
// PowerManagement.h
#pragma once

/**
 * 功耗管理
 *
 * 优化移动设备功耗
 */
class PowerManagement {
public:
    /**
     * 功耗模式
     */
    enum class PowerMode {
        PERFORMANCE,    // 性能优先
        BALANCED,       // 平衡
        POWER_SAVER     // 省电优先
    };

    /**
     * 设置功耗模式
     */
    static void setPowerMode(PowerMode mode);

    /**
     * 自适应帧率
     *
     * 根据场景动态调整目标帧率
     */
    static void enableAdaptiveFrameRate(bool enable);

    /**
     * 场景优化建议
     */
    struct OptimizationTips {
        bool reduceDrawCalls;
        bool simplifyShaders;
        bool reduceResolution;
        bool disablePostProcessing;
    };

    /**
     * 获取优化建议
     */
    static OptimizationTips getOptimizationTips(PowerMode mode);

private:
    static void applyPerformanceMode();
    static void applyBalancedMode();
    static void applyPowerSaverMode();
};
```

```cpp
// PowerManagement.cpp
#include "PowerManagement.h"

void PowerManagement::setPowerMode(PowerMode mode) {
    switch (mode) {
        case PowerMode::PERFORMANCE:
            applyPerformanceMode();
            break;

        case PowerMode::BALANCED:
            applyBalancedMode();
            break;

        case PowerMode::POWER_SAVER:
            applyPowerSaverMode();
            break;
    }
}

void PowerManagement::applyPerformanceMode() {
    // 性能模式设置:
    // - 目标 60 FPS
    // - 完整渲染质量
    // - 启用所有特效
    utils::slog.i << "Power Mode: PERFORMANCE" << utils::io::endl;
}

void PowerManagement::applyBalancedMode() {
    // 平衡模式设置:
    // - 自适应帧率 (30-60 FPS)
    // - 中等渲染质量
    // - 根据场景启用特效
    utils::slog.i << "Power Mode: BALANCED" << utils::io::endl;
}

void PowerManagement::applyPowerSaverMode() {
    // 省电模式设置:
    // - 目标 30 FPS
    // - 降低渲染质量
    // - 禁用非必要特效
    // - 降低分辨率
    utils::slog.i << "Power Mode: POWER_SAVER" << utils::io::endl;
}

PowerManagement::OptimizationTips PowerManagement::getOptimizationTips(PowerMode mode) {
    OptimizationTips tips{};

    switch (mode) {
        case PowerMode::PERFORMANCE:
            tips.reduceDrawCalls = false;
            tips.simplifyShaders = false;
            tips.reduceResolution = false;
            tips.disablePostProcessing = false;
            break;

        case PowerMode::BALANCED:
            tips.reduceDrawCalls = true;
            tips.simplifyShaders = false;
            tips.reduceResolution = false;
            tips.disablePostProcessing = false;
            break;

        case PowerMode::POWER_SAVER:
            tips.reduceDrawCalls = true;
            tips.simplifyShaders = true;
            tips.reduceResolution = true;
            tips.disablePostProcessing = true;
            break;
    }

    return tips;
}
```

---

## 4. CMakeLists.txt 配置

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.19)
project(FilamentMobileDebug)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Android 特定源文件
if(ANDROID)
    set(PLATFORM_SOURCES
        src/AndroidDebugBridge.cpp
        src/AndroidPerformanceMonitor.cpp
    )

    # Android NDK 日志库
    find_library(ANDROID_LOG_LIB log)

    set(PLATFORM_LIBS ${ANDROID_LOG_LIB})
endif()

# iOS 特定源文件
if(APPLE AND NOT MACOSX)
    set(PLATFORM_SOURCES
        src/IOSPerformanceMonitor.mm
        src/MetalDebugHelper.mm
    )

    find_library(FOUNDATION_FRAMEWORK Foundation)
    find_library(METAL_FRAMEWORK Metal)

    set(PLATFORM_LIBS
        ${FOUNDATION_FRAMEWORK}
        ${METAL_FRAMEWORK}
    )
endif()

# 通用源文件
set(COMMON_SOURCES
    src/DeviceCapabilityManager.cpp
    src/PowerManagement.cpp
)

# 创建库
add_library(mobile_debug STATIC
    ${COMMON_SOURCES}
    ${PLATFORM_SOURCES}
)

target_link_libraries(mobile_debug PUBLIC
    filament
    utils
    ${PLATFORM_LIBS}
)

target_include_directories(mobile_debug PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
```

---

## 5. 常见问题

### Q1: Android 设备性能差异很大,如何处理?

**A**: 使用设备能力检测和自适应渲染:

```cpp
// 初始化时检测设备能力
auto caps = DeviceCapabilityManager::detectCapabilities();

// 应用适合的质量设置
DeviceCapabilityManager::applyQualitySettings(caps);

// 运行时动态调整
if (getCurrentFPS() < targetFPS) {
    lowerQualitySettings();
}
```

### Q2: 如何在移动设备上调试着色器?

**A**: 方法:

```
Android:
1. 使用 Android GPU Inspector 捕获帧
2. 查看着色器源码和输入/输出
3. 使用 RenderDoc (支持 Android)

iOS:
1. 使用 Xcode Metal Debugger
2. 捕获 GPU 帧 (Cmd+Option+G)
3. 单步调试着色器
4. 查看中间结果
```

### Q3: 如何优化移动端功耗?

**A**: 关键策略:

```cpp
// 1. 降低帧率
// 静态场景不需要 60 FPS
setTargetFrameRate(30);

// 2. 减少 Draw Call
enableBatching(true);

// 3. 简化着色器
useMobileShaders(true);

// 4. 降低分辨率
setRenderScale(0.75f);  // 75% 分辨率

// 5. 禁用非必要特效
disableBloom(true);
disableSSAO(true);

// 6. 使用纹理压缩
useCompressedTextures(true);
```

---

## 6. 相关文档

- [debugging/03-platform-debuggers.md](./03-platform-debuggers.md) - 平台调试工具
- [debugging/04-performance-profiling.md](./04-performance-profiling.md) - 性能分析
- [optimization/05-mobile-optimization.md](../optimization/05-mobile-optimization.md) - 移动端优化

---

## 7. 总结

移动端调试需要关注:

1. **工具使用**: 熟练使用 Android Studio Profiler 和 Xcode Instruments
2. **设备碎片化**: 建立设备能力检测和自适应渲染系统
3. **性能优化**: 关注 CPU、GPU 和功耗
4. **远程调试**: 使用日志和远程调试工具
5. **真机测试**: 模拟器性能不代表真机

通过系统化的移动端调试方法,你可以确保应用在各种移动设备上都能良好运行。
