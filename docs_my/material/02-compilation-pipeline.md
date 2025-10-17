# 材质编译流程

本文档详细说明 Filament 材质从定义到可用的完整编译流程。

---

## 概述

材质编译流程分为四个清晰的阶段：

```
Phase 1: 解析 .mat 文件              (Parser)
    ↓
Phase 2: GLSL 代码生成               (CodeGenerator)
    ↓
Phase 3: SPIRV 编译与优化            (glslang + spirv-tools)
    ↓
Phase 4: 打包成 .filamat 二进制      (ChunkContainer)
```

每个阶段都有明确的输入输出和职责边界。

---

## Phase 1: 解析 .mat 文件

**入口**: `MaterialBuilder::material(code)` / `matc` 工具

**目标**:
- 解析 material 块配置
- 解析 fragment/vertex 代码
- 构建 MaterialInfo 数据结构
- **不生成任何代码** (只收集信息)

### 1.1 .mat 文件结构

```
material {
    name : MaterialName,
    shadingModel : lit,
    parameters : [...],
    requires : [...],
    // ... 其他配置
}

fragment {
    void material(inout MaterialInputs material) {
        // fragment shader 代码
    }
}

vertex {
    void materialVertex(inout MaterialVertexInputs material) {
        // vertex shader 代码（可选）
    }
}
```

### 1.2 解析 material 块

```cpp
// MaterialBuilder 内部解析过程
void MaterialBuilder::prepareToBuild(MaterialInfo& info) {
    // 1. 收集材质基本信息
    info.shading = mShading;
    info.blendingMode = mBlendingMode;
    info.isLit = (mShading != Shading::UNLIT);

    // 2. 构建 Uniform Interface Block (UIB)
    info.uib = buildUniformInterfaceBlock();

    // 3. 构建 Sampler Interface Block (SIB)
    info.sib = buildSamplerInterfaceBlock();

    // 4. 收集顶点属性要求
    info.requiredAttributes = mRequiredAttributes;

    // 5. 收集其他配置
    info.hasDoubleSidedCapability = mDoubleSidedCapability;
    info.flipUV = mFlipUV;
    // ... 更多配置
}
```

### 1.3 构建参数接口块

**Uniform Interface Block (UIB)** - 存储 uniform 参数

```cpp
BufferInterfaceBlock buildUniformInterfaceBlock() {
    BufferInterfaceBlock uib("MaterialParams");

    // 添加用户定义的参数
    for (const Parameter& param : mParameters) {
        if (param.isUniform()) {
            uib.add(param.name, param.size, param.uniformType, param.precision);
        }
    }

    // 添加系统生成的参数
    if (mSpecularAntiAliasing) {
        uib.add("_specularAAScreenSpaceVariance", 1, UniformType::FLOAT);
        uib.add("_specularAAThreshold", 1, UniformType::FLOAT);
    }

    if (mBlendingMode == BlendingMode::MASKED) {
        uib.add("_maskThreshold", 1, UniformType::FLOAT);
    }

    return uib;
}
```

**Sampler Interface Block (SIB)** - 存储纹理采样器

```cpp
SamplerInterfaceBlock buildSamplerInterfaceBlock() {
    SamplerInterfaceBlock sib("MaterialParams");

    // 添加用户定义的采样器
    for (const Parameter& param : mParameters) {
        if (param.isSampler()) {
            sib.add(param.name, param.samplerType, param.format,
                   param.precision, param.multisample);
        }
    }

    return sib;
}
```

### 1.4 语义分析

```cpp
bool MaterialBuilder::runSemanticAnalysis(
    MaterialInfo* info,
    CodeGenParams const& params)
{
    // 1. 使用 glslang 进行语法检查
    TShader* shader = new TShader(EShLangFragment);

    // 2. 生成临时代码（用于分析）
    std::string code = peek(ShaderStage::FRAGMENT, params, mProperties);

    // 3. 编译检查
    const char* shaderCString = code.c_str();
    shader->setStrings(&shaderCString, 1);

    if (!shader->parse(...)) {
        // 报告错误
        std::cerr << shader->getInfoLog() << std::endl;
        return false;
    }

    // 4. 提取使用的属性
    TProgram* program = new TProgram();
    program->addShader(shader);

    if (!program->link(EShMsgDefault)) {
        return false;
    }

    // 5. 清理
    delete program;
    delete shader;

    return true;
}
```

### 1.5 Phase 1 输出

**已创建**:
- ✅ MaterialInfo 数据结构
- ✅ UniformInterfaceBlock (参数定义)
- ✅ SamplerInterfaceBlock (纹理定义)
- ✅ 用户 shader 代码字符串

**未创建**:
- ❌ GLSL 完整代码
- ❌ SPIRV 二进制
- ❌ 材质包

---

## Phase 2: GLSL 代码生成

**入口**: `MaterialBuilder::generateShaders()`

**目标**:
- 生成完整的 GLSL 代码
- 为每个变体 (Variant) 生成代码
- 注入内置函数和辅助代码

### 2.1 变体系统 (Variants)

Filament 根据材质使用场景生成多个变体：

```cpp
// 变体维度
struct Variant {
    bool directionalLighting : 1;  // 是否有方向光
    bool dynamicLighting : 1;       // 是否有动态光
    bool shadowReceiver : 1;        // 是否接收阴影
    bool skinning : 1;              // 是否有骨骼动画
    bool fog : 1;                   // 是否有雾效
    bool vsm : 1;                   // 是否使用 VSM 阴影
    bool ssr : 1;                   // 是否使用屏幕空间反射
    // ... 更多维度
};
```

**变体数量**: 可能有数百个变体，根据材质配置和过滤器决定

**变体过滤**: 通过 `variantFilter()` 减少不需要的变体

```cpp
builder.variantFilter(UserVariantFilterBit::DIRECTIONAL_LIGHTING |
                     UserVariantFilterBit::DYNAMIC_LIGHTING);
```

### 2.2 代码生成器 (CodeGenerator)

```cpp
std::string CodeGenerator::createFragmentProgram(
    ShaderModel shaderModel,
    TargetApi targetApi,
    TargetLanguage targetLanguage,
    MaterialInfo const& material,
    Variant variant,
    Interpolation interpolation)
{
    std::stringstream fs;

    // 1. 生成版本声明
    fs << "#version " << getShaderVersion(shaderModel) << "\n";

    // 2. 生成预处理器定义
    fs << "#define FILAMENT_OPENGL_SEMANTICS\n";
    if (variant.directionalLighting) {
        fs << "#define HAS_DIRECTIONAL_LIGHTING\n";
    }
    // ... 更多定义

    // 3. 包含公共头文件
    fs << "#include \"common_defines.glsl\"\n";
    fs << "#include \"common_types.glsl\"\n";

    // 4. 生成 Uniform 块
    fs << "layout(std140) uniform MaterialParams {\n";
    for (auto const& field : material.uib.getFieldInfoList()) {
        fs << "    " << getTypeName(field.type) << " " << field.name;
        if (field.size > 1) {
            fs << "[" << field.size << "]";
        }
        fs << ";\n";
    }
    fs << "} materialParams;\n\n";

    // 5. 生成 Sampler 声明
    for (auto const& sampler : material.sib.getSamplerInfoList()) {
        fs << "uniform " << getSamplerTypeName(sampler.type)
           << " materialParams_" << sampler.name << ";\n";
    }

    // 6. 生成 varying 变量
    fs << generateVaryings(material, variant);

    // 7. 包含材质辅助函数
    fs << "#include \"shading_model_" << getShadingModelName(material.shading)
       << ".glsl\"\n";
    fs << "#include \"shading_lit.glsl\"\n";

    // 8. 生成 getMaterialInputs() 函数
    fs << "void getMaterialInputs(out MaterialInputs material) {\n";
    fs << "    material = MaterialInputs();\n";  // 初始化
    fs << "    " << material.fragmentCode << "\n";  // 用户代码
    fs << "}\n\n";

    // 9. 生成主函数
    fs << "void main() {\n";
    fs << "    MaterialInputs inputs;\n";
    fs << "    getMaterialInputs(inputs);\n";
    fs << "    \n";
    fs << "    // 计算光照\n";
    fs << "    vec4 color = evaluateMaterial(inputs);\n";
    fs << "    \n";
    fs << "    // 输出颜色\n";
    fs << "    fragColor = color;\n";
    fs << "}\n";

    return fs.str();
}
```

### 2.3 内置函数注入

Filament 提供大量内置函数供材质使用：

**顶点属性访问**:
```glsl
vec2 getUV0()              // 获取 UV 坐标
vec2 getUV1()
vec4 getColor()            // 获取顶点颜色
vec3 getPosition()         // 获取顶点位置
vec3 getWorldPosition()    // 获取世界空间位置
vec3 getWorldNormal()      // 获取世界空间法线
vec3 getWorldTangent()
vec3 getWorldBitangent()
```

**材质辅助函数**:
```glsl
void prepareMaterial(inout MaterialInputs material)  // 必须调用
vec4 texture(sampler2D s, vec2 uv)                   // 纹理采样
vec3 sRGBToLinear(vec3 color)                        // 颜色空间转换
vec3 linearToSRGB(vec3 color)
```

**光照计算**:
```glsl
vec4 evaluateMaterial(MaterialInputs inputs)         // 计算最终颜色
vec3 computeDiffuseColor(vec3 baseColor, float metallic)
vec3 computeF0(vec3 baseColor, float metallic, float reflectance)
```

### 2.4 Shader Model 差异处理

Filament 支持多个 Shader Model:

```cpp
enum class ShaderModel {
    GL_ES_30 = 1,   // OpenGL ES 3.0 (mobile)
    GL_CORE_41 = 2, // OpenGL 4.1 (desktop)
    GL_CORE_45 = 3, // OpenGL 4.5
};

std::string getShaderVersion(ShaderModel model) {
    switch (model) {
        case ShaderModel::GL_ES_30:  return "300 es";
        case ShaderModel::GL_CORE_41: return "410 core";
        case ShaderModel::GL_CORE_45: return "450 core";
    }
}
```

### 2.5 Phase 2 输出

**已生成**:
- ✅ 顶点着色器 GLSL 代码（所有变体）
- ✅ 片段着色器 GLSL 代码（所有变体）
- ✅ 完整的 shader 代码，包含所有依赖

**代码特点**:
- ✅ 包含版本声明和预处理器定义
- ✅ 注入了内置函数
- ✅ 针对不同 ShaderModel 的代码差异
- ✅ 嵌入了用户的 material() 函数

---

## Phase 3: SPIRV 编译与优化

**入口**: `MaterialBuilder::generateShaders()` 内部

**目标**:
- 将 GLSL 编译成 SPIRV
- 优化 SPIRV
- 转译到目标语言（MSL, GLSL, etc）

### 3.1 GLSL → SPIRV 编译

使用 **glslang** 编译器：

```cpp
#include <glslang/Public/ShaderLang.h>

std::vector<uint32_t> compileGLSLToSPIRV(
    const std::string& glsl,
    EShLanguage stage)
{
    // 1. 初始化 glslang
    glslang::InitializeProcess();

    // 2. 创建 shader
    glslang::TShader shader(stage);
    const char* shaderCString = glsl.c_str();
    shader.setStrings(&shaderCString, 1);

    // 3. 设置环境
    shader.setEnvInput(glslang::EShSourceGlsl, stage,
                      glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan,
                       glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv,
                       glslang::EShTargetSpv_1_3);

    // 4. 解析和编译
    EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

    if (!shader.parse(GetDefaultResources(), 100, false, messages)) {
        std::cerr << "GLSL compilation failed:\n"
                 << shader.getInfoLog() << std::endl;
        return {};
    }

    // 5. 链接
    glslang::TProgram program;
    program.addShader(&shader);

    if (!program.link(messages)) {
        std::cerr << "Program linking failed:\n"
                 << program.getInfoLog() << std::endl;
        return {};
    }

    // 6. 生成 SPIRV
    std::vector<uint32_t> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);

    // 7. 清理
    glslang::FinalizeProcess();

    return spirv;
}
```

### 3.2 SPIRV 优化

使用 **spirv-tools** 优化：

```cpp
#include <spirv-tools/optimizer.hpp>

std::vector<uint32_t> optimizeSPIRV(
    const std::vector<uint32_t>& spirv,
    Optimization level)
{
    spvtools::Optimizer optimizer(SPV_ENV_VULKAN_1_1);

    // 根据优化级别注册优化 passes
    switch (level) {
        case Optimization::PERFORMANCE:
            optimizer.RegisterPerformancePasses();
            break;
        case Optimization::SIZE:
            optimizer.RegisterSizePasses();
            break;
        case Optimization::NONE:
            return spirv;  // 不优化
    }

    // 运行优化
    std::vector<uint32_t> optimized;
    spvtools::OptimizerOptions options;

    if (!optimizer.Run(spirv.data(), spirv.size(), &optimized, options)) {
        std::cerr << "SPIRV optimization failed" << std::endl;
        return spirv;  // 返回未优化版本
    }

    return optimized;
}
```

**优化 Passes 包括**:
- Dead Code Elimination (删除死代码)
- Constant Folding (常量折叠)
- Loop Unrolling (循环展开)
- Inline Functions (函数内联)
- Strength Reduction (强度削减)

### 3.3 SPIRV → 目标语言转译

使用 **spirv-cross** 转译：

```cpp
#include <spirv_cross/spirv_cross.hpp>
#include <spirv_cross/spirv_msl.hpp>   // Metal
#include <spirv_cross/spirv_glsl.hpp>  // GLSL

// SPIRV → MSL (Metal Shading Language)
std::string spirvToMSL(const std::vector<uint32_t>& spirv) {
    spirv_cross::CompilerMSL msl(spirv);

    // 配置 MSL 选项
    spirv_cross::CompilerMSL::Options options;
    options.platform = spirv_cross::CompilerMSL::Options::iOS;
    options.msl_version = spirv_cross::CompilerMSL::Options::make_msl_version(2, 1);
    msl.set_msl_options(options);

    // 编译
    std::string source = msl.compile();
    return source;
}

// SPIRV → GLSL (重新生成 OpenGL GLSL)
std::string spirvToGLSL(const std::vector<uint32_t>& spirv,
                        ShaderModel model) {
    spirv_cross::CompilerGLSL glsl(spirv);

    // 配置 GLSL 选项
    spirv_cross::CompilerGLSL::Options options;
    options.version = (model == ShaderModel::GL_ES_30) ? 300 : 410;
    options.es = (model == ShaderModel::GL_ES_30);
    glsl.set_common_options(options);

    // 编译
    std::string source = glsl.compile();
    return source;
}
```

### 3.4 多后端处理

```cpp
struct ShaderBlob {
    std::vector<uint32_t> spirv;    // Vulkan 使用
    std::string msl;                // Metal 使用
    std::string glsl;               // OpenGL 使用
};

ShaderBlob compileForAllBackends(
    const std::string& glslSource,
    EShLanguage stage,
    TargetApi targetApi)
{
    ShaderBlob blob;

    // 1. 编译到 SPIRV
    blob.spirv = compileGLSLToSPIRV(glslSource, stage);

    // 2. 优化 SPIRV
    blob.spirv = optimizeSPIRV(blob.spirv, Optimization::PERFORMANCE);

    // 3. 转译到各个后端
    if (targetApi & TargetApi::METAL) {
        blob.msl = spirvToMSL(blob.spirv);
    }

    if (targetApi & TargetApi::OPENGL) {
        blob.glsl = spirvToGLSL(blob.spirv, ShaderModel::GL_CORE_41);
    }

    // Vulkan 直接使用 SPIRV

    return blob;
}
```

### 3.5 Phase 3 输出

**已编译**:
- ✅ SPIRV 二进制（Vulkan）
- ✅ MSL 源代码（Metal）
- ✅ GLSL 源代码（OpenGL）
- ✅ 所有代码都经过优化

**编译结果**:
- ✅ 每个变体都有对应的编译结果
- ✅ 每个后端都有对应的格式
- ✅ 已验证语法正确性

---

## Phase 4: 打包成 .filamat 二进制

**入口**: `MaterialBuilder::build()` 最后阶段

**目标**:
- 将所有编译结果打包
- 添加材质元数据
- 生成二进制 Package

### 4.1 材质包格式 (EIFF)

Filament 使用自定义的 **EIFF** (Embedded Interchangeable File Format) 格式：

```
+------------------------+
|  Magic Number (8 bytes)|  "FILAMAT\0"
+------------------------+
|  Version (4 bytes)     |
+------------------------+
|  Chunk Count (4 bytes) |
+------------------------+
|  Chunk 1               |
|    - Type (4 bytes)    |
|    - Size (4 bytes)    |
|    - Data (N bytes)    |
+------------------------+
|  Chunk 2               |
+------------------------+
|  ...                   |
+------------------------+
```

### 4.2 Chunk 类型

```cpp
enum class ChunkType : uint32_t {
    MaterialName            = 0x00,  // 材质名称
    MaterialVersion         = 0x01,  // 材质版本
    MaterialDomain          = 0x02,  // Surface/PostProcess/Compute
    MaterialShading         = 0x03,  // Lit/Unlit/Subsurface/Cloth

    MaterialUib             = 0x10,  // Uniform Interface Block
    MaterialSib             = 0x11,  // Sampler Interface Block
    MaterialConstants       = 0x12,  // 常量参数

    MaterialGlsl            = 0x20,  // OpenGL GLSL 代码
    MaterialSpirv           = 0x21,  // Vulkan SPIRV 代码
    MaterialMetal           = 0x22,  // Metal 代码

    DictionaryText          = 0x30,  // 文本字典（共享字符串）
    DictionarySpirv         = 0x31,  // SPIRV 字典（共享代码）

    // ... 更多类型
};
```

### 4.3 打包过程

```cpp
Package MaterialBuilder::build(utils::JobSystem& jobSystem) {
    ChunkContainer container;
    MaterialInfo info;

    // 1. 准备材质信息
    prepareToBuild(info);

    // 2. 生成所有 shader 变体
    std::vector<Variant> variants = generateVariants(info);

    // 3. 写入公共块
    writeCommonChunks(container, info);

    // 4. 生成和写入 shader 代码
    if (!generateShaders(jobSystem, variants, container, info)) {
        return Package::invalidPackage();
    }

    // 5. 序列化为二进制
    BlobDictionary blobDictionary;
    Flattener flattener(blobDictionary);

    for (const auto& chunk : container.getChunks()) {
        flattener.writeChunk(chunk);
    }

    // 6. 创建 Package
    size_t packageSize = flattener.getSize();
    uint8_t* packageData = new uint8_t[packageSize];
    memcpy(packageData, flattener.getData(), packageSize);

    return Package(packageData, packageSize);
}
```

### 4.4 写入公共块

```cpp
void MaterialBuilder::writeCommonChunks(
    ChunkContainer& container,
    MaterialInfo& info) const
{
    // 材质名称
    container.addChild<MaterialNameChunk>(mMaterialName);

    // 材质配置
    container.addChild<MaterialDomainChunk>(mMaterialDomain);
    container.addChild<MaterialShadingChunk>(mShading);
    container.addChild<MaterialBlendingModeChunk>(mBlendingMode);
    container.addChild<MaterialCullingModeChunk>(mCullingMode);

    // Uniform 接口块
    container.addChild<MaterialUibChunk>(info.uib);

    // Sampler 接口块
    container.addChild<MaterialSibChunk>(info.sib);

    // 常量参数
    if (!mConstants.empty()) {
        container.addChild<MaterialConstantsChunk>(mConstants);
    }

    // 顶点属性要求
    container.addChild<MaterialRequiredAttributesChunk>(info.requiredAttributes);

    // 更多元数据...
}
```

### 4.5 写入 Shader 代码块

```cpp
bool MaterialBuilder::generateShaders(
    utils::JobSystem& jobSystem,
    const std::vector<Variant>& variants,
    ChunkContainer& container,
    const MaterialInfo& info) const
{
    // 为每个后端生成代码
    for (const auto& params : mCodeGenPermutations) {
        // 为每个变体生成
        for (const Variant& variant : variants) {
            // 生成顶点着色器
            std::string vs = CodeGenerator::createVertexProgram(
                params.shaderModel, params.targetApi,
                params.targetLanguage, info, variant
            );

            // 生成片段着色器
            std::string fs = CodeGenerator::createFragmentProgram(
                params.shaderModel, params.targetApi,
                params.targetLanguage, info, variant
            );

            // 编译到 SPIRV
            std::vector<uint32_t> vsSpirvBlob = compileGLSLToSPIRV(vs, EShLangVertex);
            std::vector<uint32_t> fsSpirvBlob = compileGLSLToSPIRV(fs, EShLangFragment);

            // 根据目标 API 存储相应格式
            if (params.targetApi == TargetApi::VULKAN) {
                container.addChild<SpirvChunk>(variant.key,
                                              ShaderStage::VERTEX,
                                              vsSpirvBlob);
                container.addChild<SpirvChunk>(variant.key,
                                              ShaderStage::FRAGMENT,
                                              fsSpirvBlob);
            }
            else if (params.targetApi == TargetApi::METAL) {
                std::string vsMsl = spirvToMSL(vsSpirvBlob);
                std::string fsMsl = spirvToMSL(fsSpirvBlob);

                container.addChild<MetalChunk>(variant.key,
                                              ShaderStage::VERTEX,
                                              vsMsl);
                container.addChild<MetalChunk>(variant.key,
                                              ShaderStage::FRAGMENT,
                                              fsMsl);
            }
            else if (params.targetApi == TargetApi::OPENGL) {
                container.addChild<GlslChunk>(variant.key,
                                             ShaderStage::VERTEX,
                                             vs);
                container.addChild<GlslChunk>(variant.key,
                                             ShaderStage::FRAGMENT,
                                             fs);
            }
        }
    }

    return true;
}
```

### 4.6 Phase 4 输出

**已打包**:
- ✅ 材质元数据（名称、配置等）
- ✅ 参数定义（UIB/SIB）
- ✅ 所有变体的 shader 代码
- ✅ 所有后端的代码格式

**Package 结构**:
```
Package
├── MaterialName: "MyMaterial"
├── MaterialDomain: SURFACE
├── MaterialShading: LIT
├── MaterialUib: {baseColor, roughness, ...}
├── MaterialSib: {albedoMap, normalMap, ...}
├── GlslChunk (variant 0x00, vertex)
├── GlslChunk (variant 0x00, fragment)
├── GlslChunk (variant 0x01, vertex)
├── GlslChunk (variant 0x01, fragment)
├── SpirvChunk (variant 0x00, vertex)
├── SpirvChunk (variant 0x00, fragment)
├── MetalChunk (variant 0x00, vertex)
├── MetalChunk (variant 0x00, fragment)
└── ...
```

---

## 完整流程示例

### 源代码 (.mat)

```glsl
material {
    name : MyPBRMaterial,
    shadingModel : lit,
    parameters : [
        { type : float3, name : baseColor },
        { type : float, name : roughness },
        { type : sampler2d, name : albedoMap }
    ],
    requires : [uv0]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        vec4 albedo = texture(materialParams_albedoMap, getUV0());
        material.baseColor = albedo * vec4(materialParams.baseColor, 1.0);
        material.roughness = materialParams.roughness;
    }
}
```

### 编译命令

```bash
matc -p mobile -a opengl -o material.filamat material.mat
```

### 生成的 GLSL (部分)

```glsl
#version 300 es
precision highp float;

// Uniforms
layout(std140) uniform MaterialParams {
    vec3 baseColor;
    float roughness;
} materialParams;

// Samplers
uniform sampler2D materialParams_albedoMap;

// Varyings
in vec2 vertex_uv0;

// Outputs
out vec4 fragColor;

// 内置函数
vec2 getUV0() { return vertex_uv0; }

// 用户材质函数
void getMaterialInputs(out MaterialInputs material) {
    material = MaterialInputs();

    // 用户代码
    prepareMaterial(material);
    vec4 albedo = texture(materialParams_albedoMap, getUV0());
    material.baseColor = albedo * vec4(materialParams.baseColor, 1.0);
    material.roughness = materialParams.roughness;
}

// 主函数
void main() {
    MaterialInputs inputs;
    getMaterialInputs(inputs);

    vec4 color = evaluateMaterial(inputs);
    fragColor = color;
}
```

### 最终输出 (material.filamat)

```
二进制文件，包含:
- 材质名称: "MyPBRMaterial"
- Shading Model: LIT
- 参数: baseColor, roughness, albedoMap
- OpenGL GLSL 代码 (所有变体)
- Vulkan SPIRV 代码 (所有变体)
- Metal MSL 代码 (所有变体)
```

---

## 编译流程总结

| 阶段 | 输入 | 输出 | 工具 |
|-----|------|-----|------|
| **Phase 1** | .mat 文件 | MaterialInfo | MaterialBuilder |
| **Phase 2** | MaterialInfo + 用户代码 | GLSL 代码 | CodeGenerator |
| **Phase 3** | GLSL 代码 | SPIRV + MSL | glslang + spirv-cross |
| **Phase 4** | 所有编译结果 | Package (.filamat) | ChunkContainer |

---

## 常见问题

### Q1: 为什么需要生成这么多变体？

**A**: 性能优化
- 只编译实际使用的代码路径
- 避免运行时分支判断
- 减少 shader 指令数

### Q2: 编译需要多长时间？

**A**: 取决于多种因素
- 材质复杂度: 10ms - 10s
- 变体数量: 1 - 500+
- 优化级别: NONE 最快, PERFORMANCE 最慢
- 并行编译: 使用 JobSystem 可显著加速

### Q3: 如何减少编译时间？

**A**:
1. 使用 `variantFilter()` 过滤不需要的变体
2. 使用 `optimization(NONE)` 在开发时跳过优化
3. 只编译需要的后端 (`targetApi()`)
4. 使用材质缓存，避免重复编译

### Q4: .filamat 文件可以跨平台使用吗？

**A**: 可以
- 一个 .filamat 包含所有后端代码
- 运行时根据当前后端选择相应代码
- 但文件会比较大（包含多份 shader）
