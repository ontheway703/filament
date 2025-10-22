# 着色器调试

## 📖 概述

着色器调试是图形开发中最具挑战性的任务之一,因为着色器在 GPU 上并行执行,传统的断点调试方法不适用。本文档介绍各种着色器调试技术,帮助你快速定位和解决着色器问题。

**本文涵盖**:
- 着色器错误诊断
- 可视化调试技术
- 着色器调试器使用
- 常见着色器问题
- Filament Material 调试

**目标**:
- 快速定位着色器错误
- 理解着色器行为
- 优化着色器性能
- 掌握各种调试技巧

---

## 1. 着色器错误诊断

### 1.1 编译错误处理

```cpp
// ShaderErrorHandler.h
#pragma once

#include <string>
#include <vector>

/**
 * 着色器错误处理器
 */
class ShaderErrorHandler {
public:
    /**
     * 错误信息
     */
    struct ShaderError {
        int line;
        int column;
        std::string message;
        std::string type;  // "error" or "warning"
        std::string snippet;  // 错误代码片段
    };

    /**
     * 解析着色器编译错误
     */
    static std::vector<ShaderError> parseCompileErrors(const std::string& log);

    /**
     * 格式化错误报告
     */
    static std::string formatErrorReport(const std::vector<ShaderError>& errors, const std::string& source);

    /**
     * 高亮显示错误行
     */
    static std::string highlightErrorLine(const std::string& source, int lineNumber);

private:
    static std::vector<std::string> splitLines(const std::string& text);
    static std::string extractSnippet(const std::string& source, int line, int contextLines = 3);
};
```

```cpp
// ShaderErrorHandler.cpp
#include "ShaderErrorHandler.h"
#include <sstream>
#include <regex>
#include <algorithm>

std::vector<ShaderErrorHandler::ShaderError> ShaderErrorHandler::parseCompileErrors(const std::string& log) {
    std::vector<ShaderError> errors;

    // 解析常见的着色器错误格式
    // OpenGL: ERROR: 0:42: 'variable' : undeclared identifier
    // Metal: <program source>:42:10: error: use of undeclared identifier 'variable'
    // Vulkan: ERROR: 0:42: 'variable' : undeclared identifier

    std::regex errorPattern(R"(ERROR:\s*(\d+):(\d+):\s*(.+))");
    std::regex metalPattern(R"(:(\d+):(\d+):\s*(error|warning):\s*(.+))");

    std::istringstream stream(log);
    std::string line;

    while (std::getline(stream, line)) {
        std::smatch match;

        if (std::regex_search(line, match, errorPattern)) {
            ShaderError error;
            error.line = std::stoi(match[1].str());
            error.column = std::stoi(match[2].str());
            error.message = match[3].str();
            error.type = "error";
            errors.push_back(error);
        } else if (std::regex_search(line, match, metalPattern)) {
            ShaderError error;
            error.line = std::stoi(match[1].str());
            error.column = std::stoi(match[2].str());
            error.type = match[3].str();
            error.message = match[4].str();
            errors.push_back(error);
        }
    }

    return errors;
}

std::string ShaderErrorHandler::formatErrorReport(const std::vector<ShaderError>& errors, const std::string& source) {
    std::ostringstream report;

    report << "=== Shader Compilation Errors ===\n\n";
    report << "Total Errors: " << errors.size() << "\n\n";

    for (size_t i = 0; i < errors.size(); ++i) {
        const auto& error = errors[i];

        report << "[" << (i + 1) << "] " << error.type << " at line " << error.line;
        if (error.column > 0) {
            report << ":" << error.column;
        }
        report << "\n";
        report << "    " << error.message << "\n\n";

        // 显示代码片段
        std::string snippet = extractSnippet(source, error.line, 3);
        report << snippet << "\n";
    }

    return report.str();
}

std::string ShaderErrorHandler::highlightErrorLine(const std::string& source, int lineNumber) {
    auto lines = splitLines(source);

    if (lineNumber < 1 || lineNumber > static_cast<int>(lines.size())) {
        return "";
    }

    std::ostringstream result;
    int start = std::max(1, lineNumber - 3);
    int end = std::min(static_cast<int>(lines.size()), lineNumber + 3);

    for (int i = start; i <= end; ++i) {
        if (i == lineNumber) {
            result << ">>> " << i << ": " << lines[i - 1] << "\n";
        } else {
            result << "    " << i << ": " << lines[i - 1] << "\n";
        }
    }

    return result.str();
}

std::vector<std::string> ShaderErrorHandler::splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        lines.push_back(line);
    }

    return lines;
}

std::string ShaderErrorHandler::extractSnippet(const std::string& source, int line, int contextLines) {
    auto lines = splitLines(source);

    if (line < 1 || line > static_cast<int>(lines.size())) {
        return "";
    }

    std::ostringstream snippet;
    int start = std::max(1, line - contextLines);
    int end = std::min(static_cast<int>(lines.size()), line + contextLines);

    for (int i = start; i <= end; ++i) {
        if (i == line) {
            snippet << ">>> ";
        } else {
            snippet << "    ";
        }
        snippet << std::setw(4) << i << ": " << lines[i - 1] << "\n";
    }

    return snippet.str();
}
```

### 1.2 运行时错误检测

```cpp
// ShaderValidator.h
#pragma once

#include <filament/Engine.h>
#include <filament/Material.h>
#include <string>

using namespace filament;

/**
 * 着色器验证器
 *
 * 在运行时验证着色器状态
 */
class ShaderValidator {
public:
    /**
     * 验证材质
     */
    static bool validateMaterial(Material* material);

    /**
     * 验证材质实例
     */
    static bool validateMaterialInstance(MaterialInstance* instance);

    /**
     * 检查常见错误
     */
    struct ValidationResult {
        bool valid;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };

    static ValidationResult checkCommonIssues(MaterialInstance* instance);

private:
    static bool checkTextureBindings(MaterialInstance* instance);
    static bool checkUniformValues(MaterialInstance* instance);
    static bool checkSamplerState(MaterialInstance* instance);
};
```

```cpp
// ShaderValidator.cpp
#include "ShaderValidator.h"

bool ShaderValidator::validateMaterial(Material* material) {
    if (material == nullptr) {
        utils::slog.e << "Material is null" << utils::io::endl;
        return false;
    }

    // 检查材质是否有效
    if (!material->isValid()) {
        utils::slog.e << "Material is not valid" << utils::io::endl;
        return false;
    }

    // 检查材质参数
    const auto& parameters = material->getParameters();
    for (const auto& param : parameters) {
        utils::slog.d << "Parameter: " << param.name.c_str() << utils::io::endl;
    }

    return true;
}

bool ShaderValidator::validateMaterialInstance(MaterialInstance* instance) {
    if (instance == nullptr) {
        utils::slog.e << "MaterialInstance is null" << utils::io::endl;
        return false;
    }

    bool valid = true;

    // 检查纹理绑定
    if (!checkTextureBindings(instance)) {
        valid = false;
    }

    // 检查 Uniform 值
    if (!checkUniformValues(instance)) {
        valid = false;
    }

    return valid;
}

ShaderValidator::ValidationResult ShaderValidator::checkCommonIssues(MaterialInstance* instance) {
    ValidationResult result;
    result.valid = true;

    if (instance == nullptr) {
        result.valid = false;
        result.errors.push_back("MaterialInstance is null");
        return result;
    }

    // 常见问题 1: 未设置的纹理
    // 注: Filament API 可能不直接暴露这些信息,需要在应用层跟踪

    // 常见问题 2: NaN 或 Inf 值
    result.warnings.push_back("Remember to check for NaN/Inf values in uniforms");

    // 常见问题 3: 错误的颜色空间
    result.warnings.push_back("Ensure textures use correct color space (sRGB vs Linear)");

    return result;
}

bool ShaderValidator::checkTextureBindings(MaterialInstance* instance) {
    // 检查所有纹理采样器是否绑定了有效纹理
    // 注: 这需要在应用层维护纹理绑定信息

    return true;
}

bool ShaderValidator::checkUniformValues(MaterialInstance* instance) {
    // 检查 Uniform 值是否合理
    // 例如: 法线不应该全为零

    return true;
}

bool ShaderValidator::checkSamplerState(MaterialInstance* instance) {
    // 检查采样器状态是否正确配置

    return true;
}
```

---

## 2. 可视化调试技术

### 2.1 调试着色器

```glsl
// DebugVisualization.mat
material {
    name : DebugVisualization,
    shadingModel : unlit,

    parameters : [
        {
            type : int,
            name : debugMode
        }
    ],

    requires : [
        uv0,
        color,
        tangents
    ]
}

fragment {
    void material(inout MaterialInputs material) {
        // 准备材质输入
        prepareMaterial(material);

        // 获取插值数据
        vec3 worldPosition = getWorldPosition();
        vec3 worldNormal = getWorldNormalVector();
        vec3 worldTangent = getWorldTangentFrame()[0];
        vec3 worldBitangent = getWorldTangentFrame()[1];
        vec2 uv = getUV0();
        vec4 vertexColor = getColor();

        // 调试模式选择
        int mode = materialParams.debugMode;

        vec3 debugColor = vec3(0.0);

        if (mode == 0) {
            // 显示法线
            debugColor = worldNormal * 0.5 + 0.5;

        } else if (mode == 1) {
            // 显示切线
            debugColor = worldTangent * 0.5 + 0.5;

        } else if (mode == 2) {
            // 显示副切线
            debugColor = worldBitangent * 0.5 + 0.5;

        } else if (mode == 3) {
            // 显示 UV 坐标
            debugColor = vec3(uv, 0.0);

        } else if (mode == 4) {
            // 显示顶点色
            debugColor = vertexColor.rgb;

        } else if (mode == 5) {
            // 显示世界坐标
            debugColor = fract(worldPosition);

        } else if (mode == 6) {
            // 显示深度
            float depth = gl_FragCoord.z;
            debugColor = vec3(depth);

        } else if (mode == 7) {
            // 显示导数 (检测不连续性)
            vec3 dx = dFdx(worldPosition);
            vec3 dy = dFdy(worldPosition);
            float derivative = length(dx) + length(dy);
            debugColor = vec3(derivative * 10.0);

        } else if (mode == 8) {
            // 棋盘格 (检测 UV 平铺)
            float checker = mod(floor(uv.x * 10.0) + floor(uv.y * 10.0), 2.0);
            debugColor = vec3(checker);

        } else if (mode == 9) {
            // Mipmap 级别可视化
            vec2 dx = dFdx(uv * 1024.0);
            vec2 dy = dFdy(uv * 1024.0);
            float mipLevel = 0.5 * log2(max(dot(dx, dx), dot(dy, dy)));
            debugColor = vec3(mipLevel / 10.0);
        }

        material.baseColor = vec4(debugColor, 1.0);
    }
}
```

### 2.2 使用调试着色器

```cpp
// ShaderDebugger.h
#pragma once

#include <filament/Engine.h>
#include <filament/Material.h>
#include <filament/Scene.h>

using namespace filament;

/**
 * 着色器调试助手
 */
class ShaderDebugger {
public:
    /**
     * 调试模式
     */
    enum class DebugMode {
        NORMAL = 0,
        TANGENT = 1,
        BITANGENT = 2,
        UV = 3,
        VERTEX_COLOR = 4,
        WORLD_POSITION = 5,
        DEPTH = 6,
        DERIVATIVES = 7,
        CHECKER = 8,
        MIPMAP_LEVEL = 9
    };

public:
    ShaderDebugger(Engine* engine, Scene* scene);
    ~ShaderDebugger();

    /**
     * 设置调试模式
     */
    void setDebugMode(DebugMode mode);

    /**
     * 应用调试材质到场景中的所有物体
     */
    void applyDebugMaterial();

    /**
     * 恢复原始材质
     */
    void restoreOriginalMaterials();

    /**
     * 获取调试模式名称
     */
    static const char* getDebugModeName(DebugMode mode);

private:
    Engine* mEngine;
    Scene* mScene;
    Material* mDebugMaterial;
    MaterialInstance* mDebugInstance;

    // 保存原始材质
    struct OriginalMaterial {
        utils::Entity entity;
        MaterialInstance* instance;
    };
    std::vector<OriginalMaterial> mOriginalMaterials;
};
```

```cpp
// ShaderDebugger.cpp
#include "ShaderDebugger.h"
#include <filament/RenderableManager.h>

ShaderDebugger::ShaderDebugger(Engine* engine, Scene* scene)
    : mEngine(engine), mScene(scene) {

    // 加载调试材质
    // 注: 实际应用中需要从 filamat 文件加载
    // mDebugMaterial = Material::Builder()
    //     .package(debugMaterialData, debugMaterialSize)
    //     .build(*mEngine);

    // mDebugInstance = mDebugMaterial->createInstance();
}

ShaderDebugger::~ShaderDebugger() {
    restoreOriginalMaterials();

    if (mDebugInstance) {
        mEngine->destroy(mDebugInstance);
    }
    if (mDebugMaterial) {
        mEngine->destroy(mDebugMaterial);
    }
}

void ShaderDebugger::setDebugMode(DebugMode mode) {
    if (mDebugInstance) {
        mDebugInstance->setParameter("debugMode", static_cast<int>(mode));

        utils::slog.i << "Debug Mode: " << getDebugModeName(mode) << utils::io::endl;
    }
}

void ShaderDebugger::applyDebugMaterial() {
    // 保存原始材质并应用调试材质

    auto& rcm = mEngine->getRenderableManager();
    auto& em = utils::EntityManager::get();

    // 遍历场景中的所有实体
    size_t entityCount = mScene->getRenderableCount();

    for (size_t i = 0; i < entityCount; ++i) {
        // 获取实体 (注: Filament API 可能不直接提供这个方法)
        // 实际应用中需要在应用层维护实体列表

        // auto instance = rcm.getInstance(entity);
        // if (instance) {
        //     // 保存原始材质
        //     size_t primitiveCount = rcm.getPrimitiveCount(instance);
        //     for (size_t j = 0; j < primitiveCount; ++j) {
        //         auto* originalMat = rcm.getMaterialInstanceAt(instance, j);
        //         mOriginalMaterials.push_back({entity, originalMat});

        //         // 应用调试材质
        //         rcm.setMaterialInstanceAt(instance, j, mDebugInstance);
        //     }
        // }
    }

    utils::slog.i << "Debug material applied to " << mOriginalMaterials.size() << " primitives" << utils::io::endl;
}

void ShaderDebugger::restoreOriginalMaterials() {
    auto& rcm = mEngine->getRenderableManager();

    for (const auto& original : mOriginalMaterials) {
        auto instance = rcm.getInstance(original.entity);
        if (instance) {
            // 恢复原始材质
            // rcm.setMaterialInstanceAt(instance, 0, original.instance);
        }
    }

    mOriginalMaterials.clear();

    utils::slog.i << "Original materials restored" << utils::io::endl;
}

const char* ShaderDebugger::getDebugModeName(DebugMode mode) {
    switch (mode) {
        case DebugMode::NORMAL: return "Normal";
        case DebugMode::TANGENT: return "Tangent";
        case DebugMode::BITANGENT: return "Bitangent";
        case DebugMode::UV: return "UV Coordinates";
        case DebugMode::VERTEX_COLOR: return "Vertex Color";
        case DebugMode::WORLD_POSITION: return "World Position";
        case DebugMode::DEPTH: return "Depth";
        case DebugMode::DERIVATIVES: return "Derivatives";
        case DebugMode::CHECKER: return "Checker Pattern";
        case DebugMode::MIPMAP_LEVEL: return "Mipmap Level";
        default: return "Unknown";
    }
}
```

---

## 3. 平台调试器使用

### 3.1 RenderDoc 着色器调试

```cpp
/**
 * 使用 RenderDoc 调试着色器
 *
 * 步骤:
 * 1. 捕获帧
 * 2. 选择 Draw Call
 * 3. 查看 Vertex Shader 输入/输出
 * 4. 查看 Fragment Shader 输入/输出
 * 5. 单步调试着色器 (如果支持)
 */
class RenderDocShaderDebugging {
public:
    static void setupForDebugging() {
        // 在关键 Draw Call 前插入标记
        RenderDocIntegration::setMarker("DEBUG_SHADER");

        // 渲染单个对象
        renderDebugObject();
    }

    static void renderDebugObject() {
        // 渲染一个简单的对象用于调试
        // 例如: 单个三角形或四边形

        // 在 RenderDoc 中:
        // 1. 查找 "DEBUG_SHADER" 标记
        // 2. 选择该 Draw Call
        // 3. 切换到 "Pipeline State" 查看着色器
        // 4. 点击 "Edit" 可以修改着色器并重新编译
        // 5. 查看 "Mesh Output" 查看顶点着色器输出
    }
};
```

### 3.2 Nsight 着色器性能分析

```cpp
/**
 * 使用 Nsight Graphics 分析着色器性能
 *
 * 步骤:
 * 1. 捕获帧
 * 2. 选择 Draw Call
 * 3. 点击 "Shader Profiler"
 * 4. 查看每条指令的执行时间
 * 5. 识别性能热点
 */
class NsightShaderProfiling {
public:
    static void profileShader() {
        // 在 Nsight 中:
        // 1. 查看 "Shader Profiler" 标签
        // 2. 分析:
        //    - Instruction Throughput
        //    - Register Usage
        //    - Memory Access Pattern
        //    - Warp Occupancy

        // 优化建议:
        // - 减少分支 (if/else)
        // - 减少纹理采样
        // - 优化数学运算
        // - 减少寄存器使用
    }
};
```

---

## 4. 常见着色器问题

### 4.1 颜色空间问题

```glsl
// 问题: 纹理颜色不正确

// 错误示例:
vec3 color = texture(albedoMap, uv).rgb;
// 如果 albedoMap 是 sRGB 纹理,这会导致颜色过亮

// 正确做法:
// 在 Filament Material 中指定纹理是 sRGB
parameter sampler2D albedoMap;

// Filament 会自动处理 sRGB 到 Linear 的转换

// 或者手动转换:
vec3 sRGBToLinear(vec3 srgb) {
    return pow(srgb, vec3(2.2));
}

vec3 linearToSRGB(vec3 linear) {
    return pow(linear, vec3(1.0 / 2.2));
}
```

### 4.2 法线计算问题

```glsl
// 问题: 法线不正确导致光照异常

// 错误示例:
vec3 normal = texture(normalMap, uv).rgb;
// 忘记从 [0,1] 映射到 [-1,1]

// 正确做法:
vec3 normal = texture(normalMap, uv).rgb * 2.0 - 1.0;

// 归一化
normal = normalize(normal);

// 转换到世界空间
mat3 TBN = mat3(tangent, bitangent, normal);
vec3 worldNormal = TBN * normal;
```

### 4.3 精度问题

```glsl
// 问题: 移动设备上出现精度伪影

// 错误示例 (使用 lowp):
lowp float depth = texture(depthMap, uv).r;
lowp vec3 worldPos = reconstructWorldPosition(depth);
// lowp 在某些设备上只有 8-10 位精度

// 正确做法:
// 使用 mediump 或 highp
mediump float depth = texture(depthMap, uv).r;
highp vec3 worldPos = reconstructWorldPosition(depth);

// 在 Filament Material 中指定精度:
precision mediump float;
precision highp int;
```

### 4.4 NaN 和 Inf 问题

```glsl
// 问题: 出现黑色或白色像素

// 常见原因:
// 1. 除以零
vec3 direction = normalize(lightPos - worldPos);
float distance = length(lightPos - worldPos);
float attenuation = 1.0 / (distance * distance);
// 如果 distance = 0, attenuation = Inf

// 解决方案:
float distance = max(length(lightPos - worldPos), 0.001);
float attenuation = 1.0 / (distance * distance);

// 2. sqrt 负数
float value = sqrt(someValue);
// 如果 someValue < 0, 结果是 NaN

// 解决方案:
float value = sqrt(max(someValue, 0.0));

// 3. log 负数或零
float logValue = log(someValue);

// 解决方案:
float logValue = log(max(someValue, 0.0001));

// 调试技巧: 可视化 NaN/Inf
vec3 color = someComputation();
if (any(isnan(color)) || any(isinf(color))) {
    color = vec3(1.0, 0.0, 1.0);  // 洋红色表示错误
}
```

---

## 5. Filament Material 调试

### 5.1 Material 参数检查

```cpp
// MaterialDebugger.h
#pragma once

#include <filament/Material.h>
#include <filament/MaterialInstance.h>

using namespace filament;

/**
 * Material 调试器
 */
class MaterialDebugger {
public:
    /**
     * 打印材质信息
     */
    static void printMaterialInfo(Material* material);

    /**
     * 打印材质实例参数
     */
    static void printInstanceParameters(MaterialInstance* instance);

    /**
     * 验证材质定义
     */
    static bool validateMaterialDefinition(const char* matFilePath);
};
```

```cpp
// MaterialDebugger.cpp
#include "MaterialDebugger.h"

void MaterialDebugger::printMaterialInfo(Material* material) {
    if (!material) {
        utils::slog.e << "Material is null" << utils::io::endl;
        return;
    }

    utils::slog.i << "=== Material Info ===" << utils::io::endl;
    utils::slog.i << "Name: " << material->getName().c_str() << utils::io::endl;
    utils::slog.i << "Shading Model: " << static_cast<int>(material->getShadingModel()) << utils::io::endl;

    // 打印所有参数
    const auto& params = material->getParameters();
    utils::slog.i << "Parameters (" << params.size() << "):" << utils::io::endl;

    for (const auto& param : params) {
        utils::slog.i << "  - " << param.name.c_str()
                      << " (type: " << static_cast<int>(param.type) << ")"
                      << utils::io::endl;
    }
}

void MaterialDebugger::printInstanceParameters(MaterialInstance* instance) {
    if (!instance) {
        utils::slog.e << "MaterialInstance is null" << utils::io::endl;
        return;
    }

    utils::slog.i << "=== Material Instance Parameters ===" << utils::io::endl;

    // 打印已设置的参数
    // 注: Filament API 可能不直接提供遍历实例参数的方法
    // 需要在应用层跟踪设置的参数

    utils::slog.i << "Use getMaterial()->getParameters() to see available parameters" << utils::io::endl;
}

bool MaterialDebugger::validateMaterialDefinition(const char* matFilePath) {
    // 验证 .mat 文件
    // 1. 语法检查
    // 2. 参数类型检查
    // 3. 着色器代码检查

    utils::slog.i << "Validating material: " << matFilePath << utils::io::endl;

    // 实际验证需要使用 matc 编译器
    // 这里只是示例

    return true;
}
```

---

## 6. CMakeLists.txt 配置

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.19)
project(FilamentShaderDebug)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件
set(SOURCES
    src/ShaderErrorHandler.cpp
    src/ShaderValidator.cpp
    src/ShaderDebugger.cpp
    src/MaterialDebugger.cpp
)

# 创建库
add_library(shader_debug STATIC ${SOURCES})

target_link_libraries(shader_debug PUBLIC
    filament
    utils
)

target_include_directories(shader_debug PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 编译调试材质
find_program(MATC matc)

if(MATC)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/debug_material.filamat
        COMMAND ${MATC}
            -o ${CMAKE_BINARY_DIR}/debug_material.filamat
            ${CMAKE_CURRENT_SOURCE_DIR}/materials/DebugVisualization.mat
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/materials/DebugVisualization.mat
        COMMENT "Compiling debug material"
    )

    add_custom_target(debug_materials ALL
        DEPENDS ${CMAKE_BINARY_DIR}/debug_material.filamat
    )
endif()
```

---

## 7. 常见问题

### Q1: 着色器编译成功但渲染结果不正确,如何调试?

**A**: 分步可视化:

```glsl
// 逐步可视化中间结果

// 步骤 1: 输出固定颜色
material.baseColor = vec4(1.0, 0.0, 0.0, 1.0);  // 红色

// 步骤 2: 输出 UV
material.baseColor = vec4(uv, 0.0, 1.0);

// 步骤 3: 输出法线
material.baseColor = vec4(normal * 0.5 + 0.5, 1.0);

// 步骤 4: 输出光照项
vec3 diffuse = max(dot(normal, lightDir), 0.0) * lightColor;
material.baseColor = vec4(diffuse, 1.0);

// 步骤 5: 组合最终结果
```

### Q2: 如何调试性能问题?

**A**: 使用 GPU 分析器:

```
1. 简化着色器
   - 注释掉复杂计算
   - 测量性能差异
   - 逐步添加回去

2. 使用 Nsight/RenderDoc
   - 查看每个 Draw Call 的 GPU 时间
   - 使用 Shader Profiler 分析具体指令

3. 减少纹理采样
   - 纹理采样是昂贵的操作
   - 尽量重用采样结果

4. 简化数学运算
   - 使用内置函数(mix, step, smoothstep)
   - 避免 pow, exp, log (如果可能)
```

### Q3: 移动设备和桌面渲染结果不一致?

**A**: 常见原因:

```glsl
// 1. 精度差异
// 移动设备使用 mediump, 桌面使用 highp

// 解决: 显式指定精度
precision highp float;

// 2. 扩展支持
// 某些扩展在移动设备上不可用

// 解决: 使用 #ifdef 检查
#ifdef GL_OES_standard_derivatives
    float dx = dFdx(value);
#endif

// 3. 着色器变体
// Filament 为不同平台生成不同的着色器

// 解决: 使用 matc 的 -p 参数指定平台
// matc -p mobile -o material.filamat material.mat
```

---

## 8. 相关文档

- [debugging/02-renderdoc-usage.md](./02-renderdoc-usage.md) - RenderDoc 使用
- [debugging/03-platform-debuggers.md](./03-platform-debuggers.md) - 平台调试工具
- [shaders/01-material-lang-reference.md](../shaders/01-material-lang-reference.md) - Material 语言参考
- [shaders/05-shader-debugging.md](../shaders/05-shader-debugging.md) - 着色器调试技术

---

## 9. 总结

着色器调试关键技术:

1. **错误处理**: 完善的编译错误解析和报告
2. **可视化**: 使用调试材质可视化中间结果
3. **工具使用**: 熟练使用 RenderDoc, Nsight 等工具
4. **分步验证**: 逐步验证着色器计算过程
5. **平台差异**: 注意移动端和桌面端的差异

通过系统化的着色器调试方法,你可以快速定位和解决着色器问题,提升开发效率。
