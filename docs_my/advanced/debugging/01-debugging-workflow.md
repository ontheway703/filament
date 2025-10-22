# 渲染调试流程

## 概述

渲染调试是 3D 图形开发中最重要的技能之一。与传统软件调试不同，渲染调试需要理解 GPU 的工作方式、图形管线的每个阶段，以及如何使用专业工具分析渲染问题。

本文档提供了系统化的渲染调试方法论，涵盖从问题识别到根因分析的完整流程。无论是黑屏、闪烁、性能问题还是渲染错误，都能通过本文的方法论快速定位并解决。

### 为什么渲染调试很重要

渲染问题往往难以定位，因为：

1. **异步执行**：CPU 提交命令，GPU 异步执行，错误可能延迟暴露
2. **状态依赖**：渲染状态复杂，一个错误的状态设置可能影响后续所有渲染
3. **硬件差异**：不同 GPU、驱动、平台行为可能不一致
4. **缺乏直接反馈**：GPU 内部状态难以直接观察
5. **性能影响**：性能问题可能由多个因素共同导致

### 本文目标

完成本文学习后，你将能够：

- ✅ 建立系统化的渲染调试思维
- ✅ 快速分类和定位各种渲染问题
- ✅ 熟练使用各种调试工具和技术
- ✅ 理解常见渲染错误的根本原因
- ✅ 建立预防性调试的最佳实践

---

## 渲染调试的五个阶段

```
┌────────────────────────────────────────────────────────┐
│  阶段 1: 问题识别和分类                                  │
│  ├─ 识别问题类型（视觉/性能/崩溃）                       │
│  ├─ 收集初步信息（平台/配置/复现条件）                   │
│  └─ 确定调试优先级                                       │
└──────────────────────┬─────────────────────────────────┘
                       ↓
┌────────────────────────────────────────────────────────┐
│  阶段 2: 环境准备和信息采集                              │
│  ├─ 配置调试环境（工具/符号/日志）                       │
│  ├─ 启用调试层和验证层                                   │
│  └─ 建立最小复现案例                                     │
└──────────────────────┬─────────────────────────────────┘
                       ↓
┌────────────────────────────────────────────────────────┐
│  阶段 3: 数据采集和分析                                  │
│  ├─ 捕获帧（RenderDoc/Nsight/PIX）                      │
│  ├─ 分析渲染状态和资源                                   │
│  └─ 检查着色器和管线配置                                 │
└──────────────────────┬─────────────────────────────────┘
                       ↓
┌────────────────────────────────────────────────────────┐
│  阶段 4: 问题定位和验证                                  │
│  ├─ 二分法缩小范围                                       │
│  ├─ 隔离变量验证假设                                     │
│  └─ 找到问题的直接原因                                   │
└──────────────────────┬─────────────────────────────────┘
                       ↓
┌────────────────────────────────────────────────────────┐
│  阶段 5: 修复验证和预防                                  │
│  ├─ 实施修复方案                                         │
│  ├─ 多平台验证                                           │
│  └─ 建立预防机制（测试/断言/文档）                       │
└────────────────────────────────────────────────────────┘
```

---

## 阶段 1: 问题识别和分类

### 1.1 问题分类体系

渲染问题可以分为以下几大类：

#### A. 视觉问题（Visual Issues）

**黑屏/白屏**:
- 症状：屏幕完全黑色或白色，无任何内容
- 可能原因：
  - SwapChain 创建失败
  - 清屏颜色设置错误
  - 相机未正确配置
  - 渲染命令未提交

**闪烁（Flickering）**:
- 症状：画面快速闪烁或抖动
- 可能原因：
  - 双缓冲/多缓冲问题
  - Z-fighting（深度冲突）
  - 资源竞争（CPU/GPU 同时访问）
  - 垂直同步问题

**渲染错误（Artifacts）**:
- 症状：模型变形、纹理错误、光照异常
- 可能原因：
  - 顶点数据错误
  - 纹理采样问题
  - Shader 逻辑错误
  - 状态泄露

**颜色异常**:
- 症状：颜色过亮/过暗/色调错误
- 可能原因：
  - Gamma 校正错误
  - 颜色空间混淆（Linear vs sRGB）
  - 色调映射参数不当
  - Alpha 混合错误

#### B. 性能问题（Performance Issues）

**帧率低（Low FPS）**:
- 症状：FPS 低于期望值
- 可能原因：
  - Draw Call 过多
  - 过度绘制（Overdraw）
  - GPU 瓶颈（复杂 Shader、高分辨率）
  - CPU 瓶颈（提交开销、逻辑计算）

**卡顿（Stuttering）**:
- 症状：帧时间不稳定，偶尔出现长帧
- 可能原因：
  - 资源加载（同步加载）
  - GC/内存分配
  - 着色器编译
  - CPU-GPU 同步点

**内存占用高**:
- 症状：内存使用超出预期
- 可能原因：
  - 纹理未压缩或 Mipmap 过多
  - 资源泄漏
  - 重复加载资源
  - 缓冲区过大

#### C. 崩溃问题（Crashes）

**驱动崩溃（Driver Crash）**:
- 症状：GPU 重置、TDR、驱动报错
- 可能原因：
  - 无效的 GPU 命令
  - 资源访问越界
  - 驱动 Bug
  - 超时（Shader 死循环）

**应用崩溃（Application Crash）**:
- 症状：段错误、访问违例、断言失败
- 可能原因：
  - 空指针访问
  - 资源已释放
  - 线程竞争
  - API 误用

### 1.2 信息采集清单

问题发生时，立即采集以下信息：

```cpp
/**
 * 调试信息采集助手
 */
class DebugInfoCollector {
public:
    struct SystemInfo {
        std::string platform;        // "Android", "iOS", "Windows", etc.
        std::string osVersion;       // OS version
        std::string gpuModel;        // GPU model name
        std::string driverVersion;   // Graphics driver version
        std::string filamentVersion; // Filament version
    };

    struct RenderConfig {
        std::string backend;         // "OpenGL", "Vulkan", "Metal"
        uint32_t screenWidth;
        uint32_t screenHeight;
        bool vsyncEnabled;
        std::string buildType;       // "Debug", "Release"
    };

    struct ProblemDescription {
        std::string category;        // "Visual", "Performance", "Crash"
        std::string symptom;         // 具体症状描述
        std::string reproducibility; // "Always", "Sometimes", "Rare"
        std::vector<std::string> stepsToReproduce;
        std::string expectedBehavior;
        std::string actualBehavior;
    };

    /**
     * 收集完整的调试信息
     */
    static std::string collectAll() {
        std::stringstream ss;

        ss << "=== Debug Information Report ===" << std::endl;
        ss << "Timestamp: " << getCurrentTimestamp() << std::endl;
        ss << std::endl;

        ss << "--- System Information ---" << std::endl;
        auto sysInfo = collectSystemInfo();
        ss << "Platform: " << sysInfo.platform << std::endl;
        ss << "OS Version: " << sysInfo.osVersion << std::endl;
        ss << "GPU: " << sysInfo.gpuModel << std::endl;
        ss << "Driver: " << sysInfo.driverVersion << std::endl;
        ss << "Filament: " << sysInfo.filamentVersion << std::endl;
        ss << std::endl;

        ss << "--- Render Configuration ---" << std::endl;
        auto renderConfig = collectRenderConfig();
        ss << "Backend: " << renderConfig.backend << std::endl;
        ss << "Resolution: " << renderConfig.screenWidth << "x"
           << renderConfig.screenHeight << std::endl;
        ss << "VSync: " << (renderConfig.vsyncEnabled ? "On" : "Off") << std::endl;
        ss << "Build: " << renderConfig.buildType << std::endl;
        ss << std::endl;

        ss << "--- Recent Log Messages ---" << std::endl;
        ss << getRecentLogs(50) << std::endl;

        return ss.str();
    }

private:
    static SystemInfo collectSystemInfo();
    static RenderConfig collectRenderConfig();
    static std::string getCurrentTimestamp();
    static std::string getRecentLogs(size_t count);
};
```

### 1.3 优先级评估

根据影响程度确定调试优先级：

| 优先级 | 问题类型 | 影响 | 处理时间 |
|--------|---------|------|---------|
| **P0 - 阻断** | 崩溃、黑屏 | 功能完全不可用 | 立即处理 |
| **P1 - 严重** | 严重性能问题、明显渲染错误 | 严重影响用户体验 | 24小时内 |
| **P2 - 重要** | 中等性能问题、轻微视觉问题 | 影响部分用户或场景 | 1周内 |
| **P3 - 一般** | 优化机会、边缘情况 | 影响较小 | 计划处理 |

---

## 阶段 2: 环境准备和信息采集

### 2.1 调试环境配置

#### 编译配置

使用 Debug 构建进行调试：

```cmake
# CMakeLists.txt
set(CMAKE_BUILD_TYPE Debug)

# 启用调试符号
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# Filament 调试选项
add_definitions(
    -DFILAMENT_DEBUG_ENABLED=1
    -DFILAMENT_ENABLE_MATDBG=1  # 材质调试
)

# 启用地址消毒器（可选，用于检测内存错误）
if(ENABLE_ASAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
endif()
```

#### 日志配置

配置详细的日志输出：

```cpp
/**
 * 调试日志系统
 */
class DebugLogger {
public:
    enum class Level {
        VERBOSE,  // 详细信息
        DEBUG,    // 调试信息
        INFO,     // 一般信息
        WARNING,  // 警告
        ERROR,    // 错误
        FATAL     // 致命错误
    };

    /**
     * 配置日志级别
     */
    static void setLevel(Level level) {
        sCurrentLevel = level;
    }

    /**
     * 启用特定类别的日志
     */
    static void enableCategory(const std::string& category) {
        sEnabledCategories.insert(category);
    }

    /**
     * 记录日志
     */
    template<typename... Args>
    static void log(Level level, const std::string& category,
                   const std::string& format, Args... args) {
        if (level < sCurrentLevel) return;
        if (!sEnabledCategories.empty() &&
            sEnabledCategories.find(category) == sEnabledCategories.end()) {
            return;
        }

        char buffer[1024];
        snprintf(buffer, sizeof(buffer), format.c_str(), args...);

        std::cout << "[" << levelToString(level) << "]"
                  << "[" << category << "] "
                  << buffer << std::endl;

        // 同时写入文件
        if (sLogFile.is_open()) {
            sLogFile << getCurrentTimestamp() << " "
                     << "[" << levelToString(level) << "]"
                     << "[" << category << "] "
                     << buffer << std::endl;
            sLogFile.flush();
        }
    }

private:
    static Level sCurrentLevel;
    static std::set<std::string> sEnabledCategories;
    static std::ofstream sLogFile;

    static const char* levelToString(Level level);
    static std::string getCurrentTimestamp();
};

// 使用示例
void setupDebugLogging() {
    DebugLogger::setLevel(DebugLogger::Level::DEBUG);

    // 启用特定类别
    DebugLogger::enableCategory("Rendering");
    DebugLogger::enableCategory("Resources");
    DebugLogger::enableCategory("Performance");

    // 记录日志
    DebugLogger::log(DebugLogger::Level::DEBUG, "Rendering",
                    "Frame %d: Draw calls = %d", frameNumber, drawCalls);
}
```

### 2.2 验证层和调试层

#### Vulkan 验证层

```cpp
/**
 * 启用 Vulkan 验证层
 */
void setupVulkanValidation() {
    #ifdef DEBUG
    std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    // 检查验证层是否可用
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
        bool layerFound = false;
        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }
        if (!layerFound) {
            std::cerr << "Validation layer not available: " << layerName << std::endl;
        }
    }
    #endif
}
```

#### OpenGL 调试输出

```cpp
/**
 * 启用 OpenGL 调试回调
 */
void GLAPIENTRY openglDebugCallback(
    GLenum source, GLenum type, GLuint id, GLenum severity,
    GLsizei length, const GLchar* message, const void* userParam)
{
    // 忽略一些不重要的消息
    if (id == 131169 || id == 131185 || id == 131218 || id == 131204) return;

    std::cout << "OpenGL Debug Message (" << id << "): " << message << std::endl;

    std::cout << "Source: ";
    switch (source) {
        case GL_DEBUG_SOURCE_API:             std::cout << "API"; break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   std::cout << "Window System"; break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: std::cout << "Shader Compiler"; break;
        case GL_DEBUG_SOURCE_THIRD_PARTY:     std::cout << "Third Party"; break;
        case GL_DEBUG_SOURCE_APPLICATION:     std::cout << "Application"; break;
        case GL_DEBUG_SOURCE_OTHER:           std::cout << "Other"; break;
    }
    std::cout << std::endl;

    std::cout << "Type: ";
    switch (type) {
        case GL_DEBUG_TYPE_ERROR:               std::cout << "Error"; break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: std::cout << "Deprecated"; break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  std::cout << "Undefined Behavior"; break;
        case GL_DEBUG_TYPE_PORTABILITY:         std::cout << "Portability"; break;
        case GL_DEBUG_TYPE_PERFORMANCE:         std::cout << "Performance"; break;
        case GL_DEBUG_TYPE_MARKER:              std::cout << "Marker"; break;
        case GL_DEBUG_TYPE_PUSH_GROUP:          std::cout << "Push Group"; break;
        case GL_DEBUG_TYPE_POP_GROUP:           std::cout << "Pop Group"; break;
        case GL_DEBUG_TYPE_OTHER:               std::cout << "Other"; break;
    }
    std::cout << std::endl;

    std::cout << "Severity: ";
    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:         std::cout << "HIGH"; break;
        case GL_DEBUG_SEVERITY_MEDIUM:       std::cout << "MEDIUM"; break;
        case GL_DEBUG_SEVERITY_LOW:          std::cout << "LOW"; break;
        case GL_DEBUG_SEVERITY_NOTIFICATION: std::cout << "NOTIFICATION"; break;
    }
    std::cout << std::endl << std::endl;
}

void setupOpenGLDebug() {
    #ifdef DEBUG
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(openglDebugCallback, nullptr);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
    #endif
}
```

### 2.3 最小复现案例

建立最小复现案例（Minimal Reproducible Example, MRE）：

```cpp
/**
 * 最小复现案例模板
 */
class MinimalReproCase {
public:
    bool initialize() {
        // 1. 创建最小的 Filament 环境
        mEngine = Engine::create();
        mRenderer = mEngine->createRenderer();
        mScene = mEngine->createScene();
        mView = mEngine->createView();

        // 2. 设置最简单的相机
        auto& em = utils::EntityManager::get();
        mCamera = mEngine->createCamera(em.create());
        mCamera->setProjection(45.0, 1.0, 0.1, 100.0);
        mCamera->lookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});

        mView->setCamera(mCamera);
        mView->setScene(mScene);

        // 3. 创建最小的测试场景
        return setupMinimalScene();
    }

    bool setupMinimalScene() {
        // 创建一个简单的三角形或立方体
        // 用于复现问题的最小几何体
        return true;
    }

    void render() {
        if (mRenderer->beginFrame(mSwapChain)) {
            mRenderer->render(mView);
            mRenderer->endFrame();
        }
    }

private:
    Engine* mEngine = nullptr;
    Renderer* mRenderer = nullptr;
    Scene* mScene = nullptr;
    View* mView = nullptr;
    Camera* mCamera = nullptr;
    SwapChain* mSwapChain = nullptr;
};
```

---

## 阶段 3: 数据采集和分析

### 3.1 帧捕获

使用 RenderDoc 捕获帧（详见 [02-renderdoc-usage.md](./02-renderdoc-usage.md)）：

```cpp
/**
 * 触发 RenderDoc 帧捕获
 */
class RenderDocHelper {
public:
    static bool isAvailable() {
        #ifdef RENDERDOC_ENABLED
        return true;
        #else
        return false;
        #endif
    }

    /**
     * 开始帧捕获
     */
    static void beginCapture() {
        #ifdef RENDERDOC_ENABLED
        if (rdoc_api) {
            rdoc_api->StartFrameCapture(nullptr, nullptr);
        }
        #endif
    }

    /**
     * 结束帧捕获
     */
    static void endCapture() {
        #ifdef RENDERDOC_ENABLED
        if (rdoc_api) {
            rdoc_api->EndFrameCapture(nullptr, nullptr);
        }
        #endif
    }

    /**
     * 捕获当前帧
     */
    static void captureFrame() {
        beginCapture();
        // 渲染一帧
        endCapture();
    }

private:
    #ifdef RENDERDOC_ENABLED
    static RENDERDOC_API_1_1_2* rdoc_api;
    #endif
};

// 使用示例：在可疑的帧前后添加捕获
void renderFrame() {
    if (shouldCapture) {
        RenderDocHelper::beginCapture();
    }

    // 正常渲染
    renderer->beginFrame(swapChain);
    renderer->render(view);
    renderer->endFrame();

    if (shouldCapture) {
        RenderDocHelper::endCapture();
        shouldCapture = false;
    }
}
```

### 3.2 渲染状态检查

```cpp
/**
 * 渲染状态检查器
 */
class RenderStateChecker {
public:
    /**
     * 检查 View 配置
     */
    static void checkViewState(View* view) {
        std::cout << "=== View State ===" << std::endl;

        const auto& viewport = view->getViewport();
        std::cout << "Viewport: " << viewport.left << ", " << viewport.bottom
                  << ", " << viewport.width << ", " << viewport.height << std::endl;

        std::cout << "Clear flags: "
                  << (view->getClearTargetColor().has_value() ? "Color " : "")
                  << (view->getClearTargetDepth().has_value() ? "Depth " : "")
                  << (view->getClearTargetStencil().has_value() ? "Stencil" : "")
                  << std::endl;

        if (view->getClearTargetColor().has_value()) {
            auto color = view->getClearTargetColor().value();
            std::cout << "Clear color: (" << color.r << ", " << color.g
                      << ", " << color.b << ", " << color.a << ")" << std::endl;
        }
    }

    /**
     * 检查相机配置
     */
    static void checkCameraState(Camera* camera) {
        std::cout << "=== Camera State ===" << std::endl;

        // 获取投影矩阵
        auto projection = camera->getProjectionMatrix();
        std::cout << "Projection matrix:" << std::endl;
        printMatrix(projection);

        // 获取视图矩阵
        auto view = camera->getViewMatrix();
        std::cout << "View matrix:" << std::endl;
        printMatrix(view);

        // 位置
        auto position = camera->getPosition();
        std::cout << "Position: (" << position.x << ", " << position.y
                  << ", " << position.z << ")" << std::endl;
    }

    /**
     * 检查场景内容
     */
    static void checkSceneState(Engine* engine, Scene* scene) {
        std::cout << "=== Scene State ===" << std::endl;

        size_t entityCount = scene->getEntityCount();
        std::cout << "Total entities: " << entityCount << std::endl;

        auto& rcm = engine->getRenderableManager();
        size_t renderableCount = 0;

        // 统计可渲染实体
        for (utils::Entity entity : scene->getEntities()) {
            if (rcm.hasComponent(entity)) {
                renderableCount++;
            }
        }

        std::cout << "Renderable entities: " << renderableCount << std::endl;
    }

private:
    static void printMatrix(const math::mat4f& matrix) {
        for (int i = 0; i < 4; i++) {
            std::cout << "  ";
            for (int j = 0; j < 4; j++) {
                std::cout << std::fixed << std::setprecision(2)
                          << std::setw(8) << matrix[i][j] << " ";
            }
            std::cout << std::endl;
        }
    }
};
```

### 3.3 资源状态检查

```cpp
/**
 * 资源状态检查器
 */
class ResourceStateChecker {
public:
    /**
     * 检查纹理状态
     */
    static void checkTexture(Engine* engine, Texture* texture) {
        if (!texture) {
            std::cerr << "ERROR: Texture is null!" << std::endl;
            return;
        }

        std::cout << "=== Texture State ===" << std::endl;
        std::cout << "Width: " << texture->getWidth() << std::endl;
        std::cout << "Height: " << texture->getHeight() << std::endl;
        std::cout << "Depth: " << texture->getDepth() << std::endl;
        std::cout << "Levels: " << texture->getLevels() << std::endl;

        // 格式信息
        std::cout << "Format: " << textureFormatToString(texture->getFormat()) << std::endl;
        std::cout << "Usage: " << textureUsageToString(texture->getUsage()) << std::endl;
    }

    /**
     * 检查 VertexBuffer 状态
     */
    static void checkVertexBuffer(VertexBuffer* vb) {
        if (!vb) {
            std::cerr << "ERROR: VertexBuffer is null!" << std::endl;
            return;
        }

        std::cout << "=== VertexBuffer State ===" << std::endl;
        std::cout << "Vertex count: " << vb->getVertexCount() << std::endl;
        std::cout << "Buffer count: " << vb->getBufferCount() << std::endl;
    }

    /**
     * 检查 Material 状态
     */
    static void checkMaterial(Material* material) {
        if (!material) {
            std::cerr << "ERROR: Material is null!" << std::endl;
            return;
        }

        std::cout << "=== Material State ===" << std::endl;
        std::cout << "Name: " << material->getName() << std::endl;
        std::cout << "Shading model: " << shadingModelToString(material->getShadingModel()) << std::endl;
        std::cout << "Blend mode: " << blendModeToString(material->getBlendMode()) << std::endl;
    }

private:
    static const char* textureFormatToString(Texture::InternalFormat format);
    static const char* textureUsageToString(Texture::Usage usage);
    static const char* shadingModelToString(Material::Shading model);
    static const char* blendModeToString(Material::BlendMode mode);
};
```

---

## 阶段 4: 问题定位和验证

### 4.1 二分法调试

```cpp
/**
 * 二分法调试助手
 */
class BisectionDebugger {
public:
    /**
     * 逐步禁用渲染内容，定位问题范围
     */
    static void bisectRenderables(Scene* scene,
                                  std::function<bool()> testFunction) {
        // 获取所有实体
        auto entities = scene->getEntities();
        std::vector<utils::Entity> entityList(entities.begin(), entities.end());

        std::cout << "Starting bisection with " << entityList.size()
                  << " entities" << std::endl;

        // 二分法禁用实体
        size_t left = 0;
        size_t right = entityList.size();

        while (left < right) {
            size_t mid = (left + right) / 2;

            // 禁用前半部分
            for (size_t i = left; i < mid; i++) {
                scene->remove(entityList[i]);
            }

            // 测试问题是否还存在
            bool problemExists = testFunction();

            if (problemExists) {
                // 问题在后半部分
                left = mid;
            } else {
                // 问题在前半部分
                right = mid;
                // 恢复后半部分
                for (size_t i = mid; i < right; i++) {
                    scene->addEntity(entityList[i]);
                }
            }
        }

        std::cout << "Problem isolated to entity at index: " << left << std::endl;
    }

    /**
     * 逐步禁用 Draw Call
     */
    static void bisectDrawCalls(Engine* engine, Scene* scene,
                                std::function<bool()> testFunction) {
        auto& rcm = engine->getRenderableManager();
        std::vector<utils::Entity> renderables;

        // 收集所有可渲染实体
        for (auto entity : scene->getEntities()) {
            if (rcm.hasComponent(entity)) {
                renderables.push_back(entity);
            }
        }

        std::cout << "Bisecting " << renderables.size()
                  << " renderables" << std::endl;

        // 类似的二分逻辑...
    }
};
```

### 4.2 隔离变量

```cpp
/**
 * 变量隔离测试
 */
class IsolationTester {
public:
    /**
     * 测试是否是材质问题
     */
    static bool testMaterial(Engine* engine, Scene* scene) {
        // 用简单的 unlit 材质替换所有材质
        auto simpleMaterial = createSimpleUnlitMaterial(engine);

        auto& rcm = engine->getRenderableManager();
        for (auto entity : scene->getEntities()) {
            if (rcm.hasComponent(entity)) {
                auto instance = rcm.getInstance(entity);
                for (size_t i = 0; i < rcm.getPrimitiveCount(instance); i++) {
                    rcm.setMaterialInstanceAt(instance, i,
                                             simpleMaterial->getDefaultInstance());
                }
            }
        }

        // 渲染并检查问题是否消失
        return checkProblem();
    }

    /**
     * 测试是否是几何问题
     */
    static bool testGeometry(Engine* engine, Scene* scene) {
        // 用简单的立方体替换所有几何体
        auto simpleCube = createSimpleCube(engine);

        // 替换所有几何体...

        return checkProblem();
    }

    /**
     * 测试是否是光照问题
     */
    static bool testLighting(Scene* scene) {
        // 移除所有光源
        scene->removeAll();

        // 添加一个简单的方向光
        utils::Entity light = createSimpleDirectionalLight();
        scene->addEntity(light);

        return checkProblem();
    }

private:
    static Material* createSimpleUnlitMaterial(Engine* engine);
    static utils::Entity createSimpleCube(Engine* engine);
    static utils::Entity createSimpleDirectionalLight();
    static bool checkProblem();
};
```

### 4.3 断言和验证

```cpp
/**
 * 渲染断言助手
 */
class RenderAssert {
public:
    /**
     * 断言 View 配置有效
     */
    static void assertValidView(View* view) {
        FILAMENT_ASSERT(view != nullptr, "View is null");

        const auto& viewport = view->getViewport();
        FILAMENT_ASSERT(viewport.width > 0 && viewport.height > 0,
                       "Invalid viewport dimensions: %dx%d",
                       viewport.width, viewport.height);

        FILAMENT_ASSERT(view->getScene() != nullptr, "View has no scene");
        FILAMENT_ASSERT(view->getCamera() != nullptr, "View has no camera");
    }

    /**
     * 断言相机配置有效
     */
    static void assertValidCamera(Camera* camera) {
        FILAMENT_ASSERT(camera != nullptr, "Camera is null");

        // 检查投影矩阵是否有效
        auto proj = camera->getProjectionMatrix();
        FILAMENT_ASSERT(!isnan(proj[0][0]), "Projection matrix contains NaN");

        // 检查视图矩阵是否有效
        auto view = camera->getViewMatrix();
        FILAMENT_ASSERT(!isnan(view[0][0]), "View matrix contains NaN");
    }

    /**
     * 断言纹理有效
     */
    static void assertValidTexture(Texture* texture) {
        FILAMENT_ASSERT(texture != nullptr, "Texture is null");
        FILAMENT_ASSERT(texture->getWidth() > 0, "Texture width is 0");
        FILAMENT_ASSERT(texture->getHeight() > 0, "Texture height is 0");
    }

    /**
     * 断言材质有效
     */
    static void assertValidMaterial(Material* material) {
        FILAMENT_ASSERT(material != nullptr, "Material is null");
        // 检查材质是否已编译
        FILAMENT_ASSERT(material->getDefaultInstance() != nullptr,
                       "Material has no default instance");
    }
};
```

---

## 阶段 5: 修复验证和预防

### 5.1 修复验证清单

```cpp
/**
 * 修复验证助手
 */
class FixVerifier {
public:
    struct VerificationResult {
        bool passed;
        std::vector<std::string> failedTests;
        std::vector<std::string> warnings;
    };

    /**
     * 完整的修复验证流程
     */
    static VerificationResult verifyFix() {
        VerificationResult result;
        result.passed = true;

        // 1. 功能验证
        std::cout << "1. Testing functionality..." << std::endl;
        if (!testFunctionality()) {
            result.passed = false;
            result.failedTests.push_back("Functionality test failed");
        }

        // 2. 性能验证
        std::cout << "2. Testing performance..." << std::endl;
        if (!testPerformance()) {
            result.warnings.push_back("Performance regression detected");
        }

        // 3. 多平台验证
        std::cout << "3. Testing on multiple platforms..." << std::endl;
        if (!testMultiplePlatforms()) {
            result.passed = false;
            result.failedTests.push_back("Multi-platform test failed");
        }

        // 4. 回归测试
        std::cout << "4. Running regression tests..." << std::endl;
        if (!testRegression()) {
            result.passed = false;
            result.failedTests.push_back("Regression test failed");
        }

        return result;
    }

private:
    static bool testFunctionality();
    static bool testPerformance();
    static bool testMultiplePlatforms();
    static bool testRegression();
};
```

### 5.2 预防性措施

```cpp
/**
 * 预防性调试措施
 */
class PreventiveMeasures {
public:
    /**
     * 启用运行时检查
     */
    static void enableRuntimeChecks() {
        #ifdef DEBUG
        // 启用 Filament 的内部断言
        filament::debug::enableAssertions(true);

        // 启用资源追踪
        filament::debug::enableResourceTracking(true);

        // 启用 API 调用验证
        filament::debug::enableAPIValidation(true);
        #endif
    }

    /**
     * 添加性能监控
     */
    static void setupPerformanceMonitoring() {
        // 监控帧时间
        PerformanceMonitor::getInstance().addMetric("FrameTime");

        // 监控 Draw Call 数量
        PerformanceMonitor::getInstance().addMetric("DrawCalls");

        // 监控内存使用
        PerformanceMonitor::getInstance().addMetric("MemoryUsage");

        // 设置告警阈值
        PerformanceMonitor::getInstance().setThreshold("FrameTime", 16.6f); // 60 FPS
    }

    /**
     * 建立自动化测试
     */
    static void setupAutomatedTesting() {
        // 注册渲染测试
        TestRegistry::registerTest("BasicRendering", testBasicRendering);
        TestRegistry::registerTest("MaterialLoading", testMaterialLoading);
        TestRegistry::registerTest("AnimationPlayback", testAnimationPlayback);

        // 设置持续集成
        // CI 会在每次提交后自动运行这些测试
    }
};
```

---

## 常见调试场景和方法

### 场景 1: 黑屏问题

**症状**: 窗口显示纯黑色，无任何内容

**调试步骤**:

1. 检查 SwapChain 是否正确创建
2. 检查清屏颜色（可能被设置为黑色）
3. 检查相机位置和朝向
4. 检查场景是否有内容
5. 检查视锥剔除（相机可能看不到任何物体）

```cpp
void debugBlackScreen() {
    // 1. 修改清屏颜色为明显的颜色
    view->setClearColor({1.0f, 0.0f, 1.0f, 1.0f}); // 洋红色

    // 如果现在看到洋红色,说明渲染管线工作正常,问题在于场景内容

    // 2. 检查相机
    camera->lookAt({0, 0, 10}, {0, 0, 0}, {0, 1, 0});

    // 3. 添加一个简单的测试物体
    auto testEntity = createSimpleTriangle();
    scene->addEntity(testEntity);

    // 4. 检查场景统计
    std::cout << "Entities in scene: " << scene->getEntityCount() << std::endl;
}
```

### 场景 2: 闪烁问题

**症状**: 画面快速闪烁

**调试步骤**:

1. 检查双缓冲配置
2. 检查 Z-fighting（两个面距离极近）
3. 检查资源同步（CPU/GPU 竞争）
4. 检查垂直同步设置

```cpp
void debugFlickering() {
    // 1. 启用 VSync
    swapChain->setFrameRate(60.0f);

    // 2. 检查深度缓冲精度
    // 如果是 Z-fighting, 调整近裁剪面
    camera->setProjection(45.0, aspect, 1.0, 1000.0); // 近裁剪面从 0.1 改为 1.0

    // 3. 检查资源同步
    // 确保在修改资源前等待 GPU 完成
    engine->flushAndWait();

    // 4. 使用帧捕获工具查看两帧之间的差异
    RenderDocHelper::captureFrame();
}
```

### 场景 3: 性能问题

**症状**: FPS 低于预期

**调试步骤**:

1. 测量 CPU 和 GPU 时间
2. 统计 Draw Call 数量
3. 检查过度绘制（Overdraw）
4. 分析着色器复杂度

```cpp
void debugPerformance() {
    // 1. 测量帧时间
    auto frameStart = std::chrono::high_resolution_clock::now();

    renderer->beginFrame(swapChain);
    renderer->render(view);
    renderer->endFrame();

    auto frameEnd = std::chrono::high_resolution_clock::now();
    float frameTime = std::chrono::duration<float, std::milli>(
        frameEnd - frameStart).count();

    std::cout << "Frame time: " << frameTime << " ms ("
              << (1000.0f / frameTime) << " FPS)" << std::endl;

    // 2. 统计 Draw Call
    // 使用 RenderDoc 或 Nsight Graphics 查看

    // 3. 检查批处理机会
    analyzeDrawCallBatching();

    // 4. 检查着色器性能
    // 使用 Shader Profiler
}
```

---

## 调试工具对比

| 工具 | 平台 | 优势 | 适用场景 |
|------|------|------|---------|
| **RenderDoc** | Windows, Linux, Android | 免费,开源,易用 | 通用调试,帧分析 |
| **Nsight Graphics** | Windows, Linux | NVIDIA 深度分析 | NVIDIA GPU 优化 |
| **PIX** | Windows, Xbox | DirectX 深度集成 | Windows/Xbox 开发 |
| **Metal Debugger** | macOS, iOS | Metal 原生支持 | Apple 平台开发 |
| **Android GPU Inspector** | Android | 移动端特化 | Android 性能分析 |
| **Xcode Instruments** | macOS, iOS | 系统级分析 | iOS 性能分析 |

---

## 最佳实践

### 1. 建立调试习惯

- ✅ **早期验证**: 在开发早期就启用验证层和调试层
- ✅ **增量测试**: 每添加一个功能就测试一次
- ✅ **保留最小案例**: 遇到问题立即创建最小复现案例
- ✅ **记录问题**: 建立问题日志,记录症状、原因、解决方案

### 2. 代码防御性编程

```cpp
// 使用断言验证假设
FILAMENT_ASSERT(texture != nullptr, "Texture must not be null");
FILAMENT_ASSERT(width > 0 && height > 0, "Invalid dimensions");

// 添加范围检查
if (index >= buffer.size()) {
    std::cerr << "Index out of range: " << index << std::endl;
    return;
}

// 验证浮点数有效性
if (std::isnan(value) || std::isinf(value)) {
    std::cerr << "Invalid float value: " << value << std::endl;
    value = 0.0f;
}
```

### 3. 使用调试标记

```cpp
// OpenGL 调试标记
glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Shadow Pass");
// ... 阴影渲染代码 ...
glPopDebugGroup();

glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Main Pass");
// ... 主渲染代码 ...
glPopDebugGroup();

// 在 RenderDoc 中可以看到清晰的分组
```

### 4. 定期性能审计

```cpp
/**
 * 定期性能审计
 */
void performPerformanceAudit() {
    std::cout << "=== Performance Audit ===" << std::endl;

    // Draw Call 数量
    std::cout << "Draw Calls: " << getDrawCallCount() << std::endl;

    // 三角形数量
    std::cout << "Triangles: " << getTriangleCount() << std::endl;

    // 纹理内存
    std::cout << "Texture Memory: " << getTextureMemoryMB() << " MB" << std::endl;

    // 帧时间分布
    auto frameStats = getFrameTimeStats();
    std::cout << "Frame Time (avg/min/max): "
              << frameStats.avg << "/" << frameStats.min << "/"
              << frameStats.max << " ms" << std::endl;
}
```

---

## 常见问题

### Q1: 如何判断问题在 CPU 还是 GPU？

**A:** 使用帧时间分析：

```cpp
// 测量 CPU 提交时间
auto cpuStart = std::chrono::high_resolution_clock::now();
renderer->render(view);
auto cpuEnd = std::chrono::high_resolution_clock::now();
float cpuTime = std::chrono::duration<float, std::milli>(cpuEnd - cpuStart).count();

// GPU 时间需要使用 GPU 查询或分析工具
// 如果 CPU 时间 >> GPU 时间, 瓶颈在 CPU
// 如果 GPU 时间 >> CPU 时间, 瓶颈在 GPU
```

### Q2: 调试 Release 构建的问题怎么办？

**A:** 创建 RelWithDebInfo 构建：

```cmake
set(CMAKE_BUILD_TYPE RelWithDebInfo)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
```

保留调试符号但启用优化，性能接近 Release，同时可以调试。

### Q3: 如何调试只在特定设备上出现的问题？

**A:**
1. 收集设备详细信息（GPU 型号、驱动版本）
2. 使用远程调试（Android Studio Remote Debugging）
3. 添加大量日志输出
4. 尝试在相似设备上复现

### Q4: 如何调试间歇性问题？

**A:**
1. 添加大量日志和断言
2. 使用内存工具检查竞争条件（AddressSanitizer, ThreadSanitizer）
3. 压力测试（循环运行问题场景）
4. 使用确定性随机数（固定种子）

### Q5: 渲染正确但性能差怎么办？

**A:** 参考性能分析专题 [04-performance-profiling.md](./04-performance-profiling.md)

---

## 总结

渲染调试是一个系统化的过程：

1. **问题识别** → 正确分类问题类型
2. **环境准备** → 配置调试工具和日志
3. **数据采集** → 使用专业工具分析
4. **问题定位** → 二分法和隔离验证
5. **修复验证** → 多平台测试和回归检测

掌握这套方法论，你将能够快速诊断和解决各种渲染问题。记住：**好的调试习惯比调试技巧更重要**。

## 相关文档

- [02-renderdoc-usage.md](./02-renderdoc-usage.md) - RenderDoc 深度使用
- [03-platform-debuggers.md](./03-platform-debuggers.md) - 平台调试器
- [04-performance-profiling.md](./04-performance-profiling.md) - 性能分析
- [05-common-issues.md](./05-common-issues.md) - 常见问题排查
- [tools/07-debugging-tools.md](../../tools/07-debugging-tools.md) - 调试工具概览
