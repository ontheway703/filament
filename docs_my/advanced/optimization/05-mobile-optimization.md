# Filament 移动端优化

## 📖 概述

移动设备（Android 和 iOS）有严格的性能、功耗、内存限制。与桌面 GPU 相比，移动 GPU 有独特的架构特性（Tile-Based Rendering、低带宽、热节流）。本文档详细讲解移动端优化的完整技术栈。

**移动端特点**：
- **GPU 架构差异**：Tile-Based Deferred Rendering (TBDR)
- **功耗敏感**：高性能导致发热和电池消耗
- **内存带宽低**：通常 < 20 GB/s（桌面 GPU > 400 GB/s）
- **热节流（Thermal Throttling）**：持续高负载会降频
- **分辨率高**：视网膜屏（2K-4K）但 GPU 算力有限

**优化目标**：
- 稳定达到目标帧率（30/60 FPS）
- 降低功耗，延长电池寿命
- 避免热节流，保持性能稳定
- 减少内存占用（< 500MB）
- 快速启动和加载

本文档将深入讲解 Android（Vulkan、OpenGL ES）和 iOS（Metal）平台的优化技术，包括纹理压缩、LOD 系统、动态分辨率、功耗优化等。

---

## 1. 移动 GPU 架构理解

### 1.1 Tile-Based Rendering (TBR)

**桌面 GPU（Immediate Mode Rendering）**：
```
For each draw call:
    Transform vertices
    Rasterize
    Execute fragment shader
    Write to framebuffer (全屏)
```

**移动 GPU（Tile-Based Deferred Rendering）**：
```
Phase 1: Geometry (Binning)
    For each draw call:
        Transform vertices
        Tile Assignment (分配到哪些 Tile)

Phase 2: Rasterization (Per-Tile)
    For each tile (e.g., 16x16 pixels):
        Load tile to on-chip memory
        Rasterize all geometry in this tile
        Execute fragment shaders
        Write back to main memory
```

**优势**：
- **带宽节省**：On-chip memory 带宽极高（> 1TB/s）
- **Early-Z 自动优化**：Tile 内自动排序
- **功耗低**：减少主内存访问

**优化建议**：
- 避免 Clear 整个帧缓冲（使用 LoadOp::CLEAR）
- 避免中途 resolve（等 RenderPass 结束）
- 使用 Transient Attachments（iOS Metal memoryless）

### 1.2 PowerVR、Adreno、Mali 对比

| GPU 系列 | 厂商 | 架构 | 优化重点 |
|----------|------|------|----------|
| **PowerVR** | Imagination (Apple A 系列) | TBDR | Hidden Surface Removal 自动优化，避免 overdraw |
| **Adreno** | Qualcomm (Snapdragon) | TBDR | FlexRender（混合模式），优化 bandwidth |
| **Mali** | ARM (中低端 Android) | TBDR | Transaction Elimination，避免小三角形 |

---

## 2. Android 优化

### 2.1 Vulkan vs OpenGL ES

**Vulkan 优势**：
- **显式控制**：精确控制内存、同步、命令缓冲
- **多线程**：多核 CPU 并行记录命令
- **低开销**：驱动开销减少 50-80%

**Vulkan 劣势**：
- **复杂度高**：需要手动管理资源生命周期
- **兼容性**：Android 7.0+ (API 24)

**OpenGL ES 优势**：
- **简单**：易于开发和调试
- **兼容性**：几乎所有 Android 设备

**选择策略**：

```cpp
filament::Engine::Backend selectBackend() {
    // 检查 Vulkan 支持
    if (android_get_device_api_level() >= 24 && hasVulkanSupport()) {
        // 高端设备优先 Vulkan
        if (isHighEndDevice()) {
            return filament::Engine::Backend::VULKAN;
        }
    }
    
    // 回退到 OpenGL ES
    return filament::Engine::Backend::OPENGL;
}

bool isHighEndDevice() {
    // 检查 GPU 型号
    std::string gpuVendor = getGPUVendor();
    
    if (gpuVendor.find("Adreno") != std::string::npos) {
        // Adreno 6xx+ 使用 Vulkan
        int adrenoVersion = getAdrenoVersion();
        return adrenoVersion >= 600;
    }
    
    if (gpuVendor.find("Mali") != std::string::npos) {
        // Mali-G 系列使用 Vulkan
        return gpuVendor.find("Mali-G") != std::string::npos;
    }
    
    return false;
}
```

### 2.2 Android GPU 特性检测

```cpp
// include/mobile/AndroidGPUInfo.h
#pragma once

#include <string>
#include <GLES3/gl3.h>

namespace myproject::mobile {

struct AndroidGPUInfo {
    std::string vendor;
    std::string renderer;
    std::string version;
    
    // 扩展支持
    bool supportsASTCTextures;
    bool supportsETC2Textures;
    bool supportsFloatTextures;
    bool supportsInstancing;
    bool supportsComputeShaders;
    
    // 限制
    int maxTextureSize;
    int maxVertexUniforms;
    int maxFragmentUniforms;
};

class AndroidGPUDetector {
public:
    static AndroidGPUInfo detect() {
        AndroidGPUInfo info;
        
        info.vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        info.renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        info.version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        
        // 检查扩展
        info.supportsASTCTextures = hasExtension("GL_KHR_texture_compression_astc_ldr");
        info.supportsETC2Textures = true;  // OpenGL ES 3.0+
        info.supportsFloatTextures = hasExtension("GL_OES_texture_float");
        info.supportsInstancing = true;  // OpenGL ES 3.0+
        info.supportsComputeShaders = hasExtension("GL_ES_VERSION_3_1");
        
        // 查询限制
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &info.maxTextureSize);
        glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &info.maxVertexUniforms);
        glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &info.maxFragmentUniforms);
        
        return info;
    }
    
private:
    static bool hasExtension(const char* name) {
        const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        return strstr(extensions, name) != nullptr;
    }
};

}  // namespace myproject::mobile
```

**使用**：

```cpp
auto gpuInfo = AndroidGPUDetector::detect();

LOG_INFO("GPU: %s %s", gpuInfo.vendor.c_str(), gpuInfo.renderer.c_str());
LOG_INFO("Max Texture Size: %d", gpuInfo.maxTextureSize);

if (gpuInfo.supportsASTCTextures) {
    // 使用 ASTC 纹理压缩
    textureFormat = Texture::InternalFormat::RGBA_ASTC_4x4;
} else if (gpuInfo.supportsETC2Textures) {
    // 回退到 ETC2
    textureFormat = Texture::InternalFormat::RGBA8_ETC2_EAC;
}
```

---

## 3. iOS 优化

### 3.1 Metal 专属优化

**Memoryless Render Targets**（iOS 专属特性）：

```cpp
// 创建 Memoryless Depth Buffer（不占用主内存）
#if defined(__IPHONE_OS_VERSION_MIN_REQUIRED)
    // iOS Metal 支持 memoryless
    filament::Texture* depthTexture = filament::Texture::Builder()
        .width(width)
        .height(height)
        .format(filament::Texture::InternalFormat::DEPTH24)
        .usage(filament::Texture::Usage::DEPTH_ATTACHMENT)
        .build(*engine);
    
    // Filament 内部会自动使用 MTLStorageModeMemoryless
#endif
```

**优势**：
- **节省内存**：Depth/Stencil 不占用主内存
- **节省带宽**：无需 load/store 主内存
- **性能提升**：10-20% 帧率提升

**适用场景**：
- Depth Buffer（不需要读取）
- GBuffer 中的临时纹理
- MSAA Resolve 前的 MSAA 纹理

### 3.2 Metal Performance Shaders (MPS)

使用 Apple 优化的图像处理 API：

```cpp
// 使用 MPS 实现高斯模糊（比自定义 Shader 快 2-3 倍）
#if defined(__IPHONE_OS_VERSION_MIN_REQUIRED)
    #import <MetalPerformanceShaders/MetalPerformanceShaders.h>
    
    void applyGaussianBlur(id<MTLTexture> input, id<MTLTexture> output) {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        
        MPSImageGaussianBlur* blur = [[MPSImageGaussianBlur alloc]
            initWithDevice:device sigma:5.0f];
        
        [blur encodeToCommandBuffer:commandBuffer
                        sourceTexture:input
                   destinationTexture:output];
        
        [commandBuffer commit];
    }
#endif
```

**MPS 提供的功能**：
- 图像滤镜（模糊、锐化、边缘检测）
- 卷积（自定义卷积核）
- 图像变换（缩放、旋转）
- 神经网络（CNN 推理）

---

## 4. 纹理压缩

### 4.1 压缩格式对比

| 格式 | 平台 | 压缩比 | 质量 | 带宽节省 |
|------|------|--------|------|----------|
| **ASTC 4x4** | Android 5.0+, iOS 8+ | 8:1 | 高 | 87.5% |
| **ASTC 6x6** | Android 5.0+, iOS 8+ | 18:1 | 中 | 94.4% |
| **ASTC 8x8** | Android 5.0+, iOS 8+ | 32:1 | 低 | 96.9% |
| **ETC2** | Android (OpenGL ES 3.0+) | 4:1 | 中高 | 75% |
| **PVRTC** | iOS (旧设备) | 8:1 | 中 | 87.5% |
| **BC7** | Windows, Xbox | 4:1 | 高 | 75% |

**ASTC（Adaptive Scalable Texture Compression）**：
- **最佳选择**：质量和压缩比平衡最好
- **灵活性**：支持多种块大小（4x4 到 12x12）
- **Alpha 支持**：透明纹理无需额外通道

### 4.2 运行时纹理压缩选择

```cpp
// include/mobile/TextureCompression.h
#pragma once

#include <filament/Texture.h>

namespace myproject::mobile {

class TextureCompressionSelector {
public:
    static filament::Texture::InternalFormat selectFormat(bool hasAlpha) {
#if defined(__ANDROID__)
        // Android 优先 ASTC
        if (AndroidGPUDetector::detect().supportsASTCTextures) {
            return hasAlpha ? 
                filament::Texture::InternalFormat::RGBA_ASTC_4x4 :
                filament::Texture::InternalFormat::SRGB8_ALPHA8_ASTC_4x4;
        }
        // 回退 ETC2
        return hasAlpha ?
            filament::Texture::InternalFormat::RGBA8_ETC2_EAC :
            filament::Texture::InternalFormat::RGB8_ETC2;
            
#elif defined(__IPHONE_OS_VERSION_MIN_REQUIRED)
        // iOS 全部支持 ASTC
        return hasAlpha ?
            filament::Texture::InternalFormat::RGBA_ASTC_4x4 :
            filament::Texture::InternalFormat::SRGB8_ALPHA8_ASTC_4x4;
#else
        // 桌面回退未压缩
        return hasAlpha ?
            filament::Texture::InternalFormat::RGBA8 :
            filament::Texture::InternalFormat::RGB8;
#endif
    }
    
    // 根据设备性能选择压缩级别
    static filament::Texture::InternalFormat selectASTCLevel(bool hasAlpha) {
        if (isHighEndDevice()) {
            // 高端设备：4x4（最高质量）
            return filament::Texture::InternalFormat::RGBA_ASTC_4x4;
        } else if (isMidRangeDevice()) {
            // 中端设备：6x6（平衡）
            return filament::Texture::InternalFormat::RGBA_ASTC_6x6;
        } else {
            // 低端设备：8x8（最高压缩）
            return filament::Texture::InternalFormat::RGBA_ASTC_8x8;
        }
    }
};

}  // namespace myproject::mobile
```

### 4.3 离线纹理压缩

**使用 ARM Mali Texture Compression Tool**：

```bash
# 安装
# https://developer.arm.com/tools-and-software/graphics-and-gaming/mali-texture-compression-tool

# 压缩单个纹理
astcenc -cl input.png output.astc 4x4 -medium

# 批量压缩
for file in textures/*.png; do
    astcenc -cl "$file" "${file%.png}.astc" 4x4 -medium
done
```

**集成到构建流程**：

```cmake
# CMakeLists.txt
find_program(ASTCENC astcenc)

if(ASTCENC)
    file(GLOB TEXTURES "assets/textures/*.png")
    
    foreach(TEXTURE ${TEXTURES})
        get_filename_component(TEXTURE_NAME ${TEXTURE} NAME_WE)
        set(OUTPUT "${CMAKE_BINARY_DIR}/assets/textures/${TEXTURE_NAME}.astc")
        
        add_custom_command(
            OUTPUT ${OUTPUT}
            COMMAND ${ASTCENC} -cl ${TEXTURE} ${OUTPUT} 4x4 -medium
            DEPENDS ${TEXTURE}
            COMMENT "Compressing texture ${TEXTURE_NAME}"
        )
        
        list(APPEND COMPRESSED_TEXTURES ${OUTPUT})
    endforeach()
    
    add_custom_target(CompressTextures ALL DEPENDS ${COMPRESSED_TEXTURES})
endif()
```

---

## 5. LOD 系统

### 5.1 距离 LOD

```cpp
// include/mobile/LODManager.h
#pragma once

#include <filament/RenderableManager.h>
#include <vector>

namespace myproject::mobile {

class LODManager {
public:
    struct LODLevel {
        float distance;           // LOD 切换距离
        float triangleReduction;  // 三角形减少比例
        utils::Entity entity;     // 对应的 Entity
    };
    
    struct LODGroup {
        std::vector<LODLevel> levels;
        math::float3 position;
    };
    
    void addLODGroup(const LODGroup& group) {
        mGroups.push_back(group);
    }
    
    void update(const math::float3& cameraPos) {
        for (auto& group : mGroups) {
            float distance = length(cameraPos - group.position);
            
            // 根据距离选择 LOD
            size_t selectedLOD = 0;
            for (size_t i = 0; i < group.levels.size(); ++i) {
                if (distance < group.levels[i].distance) {
                    selectedLOD = i;
                    break;
                }
                selectedLOD = group.levels.size() - 1;
            }
            
            // 隐藏其他 LOD，显示选中的
            for (size_t i = 0; i < group.levels.size(); ++i) {
                auto& rcm = mEngine->getRenderableManager();
                auto instance = rcm.getInstance(group.levels[i].entity);
                
                if (instance) {
                    rcm.setLayerMask(instance, i == selectedLOD ? 0x1 : 0x0);
                }
            }
        }
    }
    
private:
    filament::Engine* mEngine;
    std::vector<LODGroup> mGroups;
};

}  // namespace myproject::mobile
```

**使用示例**：

```cpp
LODManager lodMgr(engine);

// 创建树的 LOD 组
LODManager::LODGroup treeGroup;
treeGroup.position = {10, 0, 0};

// LOD 0: 高精度（1000 三角形）
treeGroup.levels.push_back({
    .distance = 50.0f,
    .triangleReduction = 1.0f,
    .entity = treeLOD0Entity
});

// LOD 1: 中精度（500 三角形）
treeGroup.levels.push_back({
    .distance = 100.0f,
    .triangleReduction = 0.5f,
    .entity = treeLOD1Entity
});

// LOD 2: 低精度（100 三角形）
treeGroup.levels.push_back({
    .distance = 200.0f,
    .triangleReduction = 0.1f,
    .entity = treeLOD2Entity
});

lodMgr.addLODGroup(treeGroup);

// 每帧更新
lodMgr.update(camera->getPosition());
```

### 5.2 屏幕空间 LOD

```cpp
float calculateScreenSpaceSize(const math::float3& objectPos,
                               float objectRadius,
                               const Camera& camera) {
    float distance = length(camera.getPosition() - objectPos);
    
    // 投影到屏幕
    float fov = camera.getFieldOfView(Camera::Fov::VERTICAL);
    float screenHeight = 1080.0f;  // 假设分辨率
    
    float angularSize = 2.0f * atan(objectRadius / distance);
    float screenSize = (angularSize / fov) * screenHeight;
    
    return screenSize;
}

// 根据屏幕大小选择 LOD
size_t selectLODByScreenSize(float screenSize) {
    if (screenSize > 200.0f) return 0;  // 高精度
    if (screenSize > 50.0f)  return 1;  // 中精度
    if (screenSize > 10.0f)  return 2;  // 低精度
    return 3;  // 极低精度或剔除
}
```

---

## 6. 动态分辨率

### 6.1 自适应分辨率调整

```cpp
// include/mobile/DynamicResolution.h
#pragma once

#include <filament/View.h>
#include <atomic>

namespace myproject::mobile {

class DynamicResolution {
public:
    DynamicResolution(filament::View* view, float targetFPS = 60.0f)
        : mView(view)
        , mTargetFPS(targetFPS)
        , mCurrentScale(1.0f) {
        
        // 设置初始分辨率范围
        view->setDynamicResolutionOptions({
            .enabled = true,
            .minScale = {0.5f, 0.5f},   // 最低 50%
            .maxScale = {1.0f, 1.0f},   // 最高 100%
            .quality = filament::View::QualityLevel::MEDIUM
        });
    }
    
    void update(float currentFPS, float deltaTime) {
        const float tolerance = 0.1f;
        
        if (currentFPS < mTargetFPS * (1.0f - tolerance)) {
            // 帧率低，降低分辨率
            lowerResolution();
        } else if (currentFPS > mTargetFPS * (1.0f + tolerance)) {
            // 帧率高，提升分辨率
            raiseResolution();
        }
        
        // 平滑调整（每秒最多调整 10%）
        float maxChange = 0.1f * deltaTime;
        if (mTargetScale < mCurrentScale) {
            mCurrentScale = std::max(mTargetScale, mCurrentScale - maxChange);
        } else if (mTargetScale > mCurrentScale) {
            mCurrentScale = std::min(mTargetScale, mCurrentScale + maxChange);
        }
        
        // 应用新分辨率
        mView->setDynamicResolutionOptions({
            .enabled = true,
            .minScale = {mCurrentScale, mCurrentScale},
            .maxScale = {mCurrentScale, mCurrentScale},
            .quality = filament::View::QualityLevel::MEDIUM
        });
    }
    
private:
    void lowerResolution() {
        mTargetScale = std::max(0.5f, mTargetScale - 0.05f);
        LOG_DEBUG("Lowering resolution to %.2f", mTargetScale);
    }
    
    void raiseResolution() {
        mTargetScale = std::min(1.0f, mTargetScale + 0.05f);
        LOG_DEBUG("Raising resolution to %.2f", mTargetScale);
    }
    
    filament::View* mView;
    float mTargetFPS;
    float mCurrentScale;
    float mTargetScale = 1.0f;
};

}  // namespace myproject::mobile
```

**使用**：

```cpp
DynamicResolution dynRes(view, 60.0f);

// 主循环
while (running) {
    float currentFPS = calculateFPS();
    float deltaTime = calculateDeltaTime();
    
    dynRes.update(currentFPS, deltaTime);
    
    renderer->render(view);
}
```

**效果**：
- **低端设备**：自动降到 50-70% 分辨率，保持 60 FPS
- **高端设备**：保持 100% 分辨率
- **动态场景**：复杂场景自动降低分辨率，简单场景恢复

---

## 7. 功耗优化

### 7.1 帧率自适应

```cpp
// include/mobile/PowerManager.h
#pragma once

#include <chrono>

namespace myproject::mobile {

class PowerManager {
public:
    enum class PowerMode {
        HIGH_PERFORMANCE,  // 60 FPS
        BALANCED,          // 30 FPS
        POWER_SAVER        // 20 FPS
    };
    
    PowerManager()
        : mCurrentMode(PowerMode::BALANCED)
        , mTargetFrameTime(1.0 / 30.0) {}
    
    void setPowerMode(PowerMode mode) {
        mCurrentMode = mode;
        
        switch (mode) {
            case PowerMode::HIGH_PERFORMANCE:
                mTargetFrameTime = 1.0 / 60.0;
                break;
            case PowerMode::BALANCED:
                mTargetFrameTime = 1.0 / 30.0;
                break;
            case PowerMode::POWER_SAVER:
                mTargetFrameTime = 1.0 / 20.0;
                break;
        }
    }
    
    bool shouldRenderFrame() {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - mLastFrameTime).count();
        
        if (elapsed >= mTargetFrameTime) {
            mLastFrameTime = now;
            return true;
        }
        
        return false;
    }
    
    // 根据电池电量自动调整
    void updateFromBatteryLevel(float batteryLevel) {
        if (batteryLevel < 0.15f) {
            setPowerMode(PowerMode::POWER_SAVER);
        } else if (batteryLevel < 0.5f) {
            setPowerMode(PowerMode::BALANCED);
        } else {
            setPowerMode(PowerMode::HIGH_PERFORMANCE);
        }
    }
    
private:
    PowerMode mCurrentMode;
    double mTargetFrameTime;
    std::chrono::steady_clock::time_point mLastFrameTime;
};

}  // namespace myproject::mobile
```

**Android 电池电量获取**：

```java
// MainActivity.java
public float getBatteryLevel() {
    IntentFilter ifilter = new IntentFilter(Intent.ACTION_BATTERY_CHANGED);
    Intent batteryStatus = registerReceiver(null, ifilter);
    
    int level = batteryStatus.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
    int scale = batteryStatus.getIntExtra(BatteryManager.EXTRA_SCALE, -1);
    
    return level / (float)scale;
}

// JNI
extern "C" JNIEXPORT void JNICALL
Java_com_mycompany_myapp_MainActivity_nativeUpdateBattery(
    JNIEnv* env, jobject, jfloat batteryLevel) {
    
    gPowerManager->updateFromBatteryLevel(batteryLevel);
}
```

### 7.2 热节流监控

```cpp
// include/mobile/ThermalMonitor.h
#pragma once

#include <atomic>

namespace myproject::mobile {

class ThermalMonitor {
public:
    enum class ThermalState {
        NOMINAL,      // 正常
        LIGHT,        // 轻度发热
        MODERATE,     // 中度发热
        SEVERE,       // 严重发热（降频）
        CRITICAL      // 极严重（强制降频）
    };
    
    ThermalState getCurrentState() const {
        return mCurrentState.load();
    }
    
    // Android 热状态监控
    void updateThermalState(int thermalStatus) {
        // Android PowerManager.THERMAL_STATUS_*
        switch (thermalStatus) {
            case 0: // THERMAL_STATUS_NONE
                mCurrentState = ThermalState::NOMINAL;
                break;
            case 1: // THERMAL_STATUS_LIGHT
                mCurrentState = ThermalState::LIGHT;
                break;
            case 2: // THERMAL_STATUS_MODERATE
                mCurrentState = ThermalState::MODERATE;
                break;
            case 3: // THERMAL_STATUS_SEVERE
                mCurrentState = ThermalState::SEVERE;
                break;
            case 4: // THERMAL_STATUS_CRITICAL
                mCurrentState = ThermalState::CRITICAL;
                break;
        }
    }
    
    // 根据热状态调整质量
    void adjustQualityForThermal(filament::View* view) {
        switch (getCurrentState()) {
            case ThermalState::NOMINAL:
            case ThermalState::LIGHT:
                // 正常质量
                break;
                
            case ThermalState::MODERATE:
                // 降低阴影质量
                view->setShadowType(filament::View::ShadowType::PCF);
                break;
                
            case ThermalState::SEVERE:
                // 禁用阴影，降低分辨率
                view->setShadowingEnabled(false);
                view->setDynamicResolutionOptions({
                    .enabled = true,
                    .minScale = {0.7f, 0.7f},
                    .maxScale = {0.7f, 0.7f}
                });
                break;
                
            case ThermalState::CRITICAL:
                // 最低质量
                view->setShadowingEnabled(false);
                view->setPostProcessingEnabled(false);
                view->setDynamicResolutionOptions({
                    .enabled = true,
                    .minScale = {0.5f, 0.5f},
                    .maxScale = {0.5f, 0.5f}
                });
                break;
        }
    }
    
private:
    std::atomic<ThermalState> mCurrentState{ThermalState::NOMINAL};
};

}  // namespace myproject::mobile
```

---

## 8. Shader 优化

### 8.1 Precision 优化

```glsl
// ❌ 不好：全部 highp（高精度消耗大）
precision highp float;

uniform mat4 modelViewProjection;
uniform vec3 lightPosition;
uniform vec3 cameraPosition;

in vec3 position;
in vec3 normal;
in vec2 uv;

out vec4 fragColor;

// ✅ 好：按需使用精度
precision mediump float;  // 默认中精度

uniform highp mat4 modelViewProjection;  // 变换矩阵需要高精度
uniform mediump vec3 lightPosition;
uniform mediump vec3 cameraPosition;

in highp vec3 position;    // 顶点位置高精度
in mediump vec3 normal;    // 法线中精度即可
in mediump vec2 uv;        // UV 中精度

out mediump vec4 fragColor;
```

**精度对比**：

| 精度 | 位数 | 范围 | 精度 | 性能 |
|------|------|------|------|------|
| **lowp** | 9 | [-2, 2] | 1/256 | 最快 |
| **mediump** | 16 | [-2^14, 2^14] | 1/1024 | 中等 |
| **highp** | 32 | [-2^62, 2^62] | 极高 | 最慢 |

**优化建议**：
- **lowp**: 颜色、归一化向量
- **mediump**: 纹理坐标、法线、光照计算
- **highp**: 顶点变换、深度计算

### 8.2 避免分支

```glsl
// ❌ 不好：条件分支
if (useDiffuseMap) {
    color = texture(diffuseMap, uv);
} else {
    color = baseColor;
}

// ✅ 好：使用 mix
color = mix(baseColor, texture(diffuseMap, uv), float(useDiffuseMap));
```

---

## 9. 移动端最佳实践

### 9.1 启动优化

```cpp
// 快速启动检查清单
void optimizeStartup() {
    // 1. 延迟加载非必要资源
    // 只加载首屏需要的资源
    
    // 2. 使用预编译 Shader
    // 避免首次运行时编译卡顿
    
    // 3. 异步初始化
    std::thread([]() {
        loadHeavyAssets();
    }).detach();
    
    // 4. 显示加载画面
    showSplashScreen();
}
```

### 9.2 内存优化

```cpp
// 移动端内存预算（示例）
MemoryBudget budget;
budget.setBudget("Textures", 200 * 1024 * 1024);  // 200MB
budget.setBudget("Meshes",   100 * 1024 * 1024);  // 100MB
budget.setBudget("Audio",     50 * 1024 * 1024);  // 50MB
budget.setBudget("Other",     50 * 1024 * 1024);  // 50MB
// Total: 400MB
```

---

## 10. 实战案例：移动 AR 应用优化

### 10.1 问题描述

- **平台**：Android（Snapdragon 865）、iOS（A14）
- **场景**：AR 家具展示（实时渲染 + 摄像头）
- **问题**：帧率 20-30 FPS，发热严重，电池 1 小时耗尽

### 10.2 优化方案

1. **降低分辨率**：从 1080p → 720p（-56% 像素）
2. **ASTC 压缩**：纹理从 RGBA8 → ASTC 4x4（-75% 内存）
3. **简化材质**：移除不必要的 PBR 特性（省电 20%）
4. **LOD 系统**：3 级 LOD（近、中、远）
5. **帧率限制**：30 FPS（省电 40%）

### 10.3 优化结果

| 指标 | Before | After | 改善 |
|------|--------|-------|------|
| 帧率 | 20-30 FPS | 稳定 30 FPS | +50% |
| 发热 | 45°C | 38°C | -16% |
| 电池寿命 | 1 小时 | 2.5 小时 | +150% |
| 内存 | 800 MB | 300 MB | -63% |

---

## 11. 常见问题

### Q1: Android 碎片化如何应对？

**A**: 
- 动态质量调整
- GPU 特性检测
- 按设备型号配置（高/中/低端）

### Q2: iOS 和 Android 性能差异？

**A**: 
- iOS 通常更快（Metal 优化好、硬件统一）
- Android 需针对低端设备优化

### Q3: 如何测试移动性能？

**A**: 
- Android Studio Profiler
- Xcode Instruments
- RenderDoc (Android)
- 真机测试（模拟器不准确）

---

## 12. 相关文档

- [optimization/01-texture-optimization.md](./01-texture-optimization.md)
- [optimization/04-memory-management.md](./04-memory-management.md)
- [optimization/06-shader-optimization.md](./06-shader-optimization.md)
- [architecture/09-cross-platform-arch.md](../architecture/09-cross-platform-arch.md)

---

## 13. 总结

移动端优化关键技术：

1. **理解 TBDR 架构**：利用 Tile-Based Rendering 特性
2. **纹理压缩**：ASTC 压缩节省 75-95% 内存
3. **LOD 系统**：距离和屏幕空间 LOD 结合
4. **动态分辨率**：保持稳定帧率
5. **功耗管理**：帧率限制、热节流监控
6. **Shader 优化**：合理使用精度、避免分支
7. **平台特性**：Metal memoryless、MPS

通过这些技术，可实现：
- **稳定帧率**：30/60 FPS
- **功耗降低**：30-50%
- **电池延长**：1.5-3 倍
- **内存节省**：50-70%

移动优化是持续过程，需在质量和性能间找到平衡。
