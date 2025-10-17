# Shader 代码生成详解

本文档详细说明 Filament 如何生成完整的 shader 代码。

---

## 概述

Shader 代码生成是材质编译的核心环节，负责将用户的材质定义转换成完整可执行的 GLSL 代码。

```
MaterialInfo + 用户代码
    ↓
CodeGenerator::createFragmentProgram()
    ↓
完整的 GLSL Fragment Shader
```

---

## CodeGenerator 架构

**位置**: `/libs/filamat/src/shaders/CodeGenerator.h`

### 核心组件

```cpp
class CodeGenerator {
public:
    // 生成片段着色器
    static std::string createFragmentProgram(
        ShaderModel shaderModel,
        TargetApi targetApi,
        TargetLanguage targetLanguage,
        MaterialInfo const& material,
        Variant variant,
        Interpolation interpolation
    );

    // 生成顶点着色器
    static std::string createVertexProgram(
        ShaderModel shaderModel,
        TargetApi targetApi,
        TargetLanguage targetLanguage,
        MaterialInfo const& material,
        Variant variant
    );

private:
    // 生成预处理器定义
    static void generateDefines(
        std::ostream& out,
        const MaterialInfo& material,
        const Variant& variant
    );

    // 生成 Uniform 块
    static void generateUniforms(
        std::ostream& out,
        const MaterialInfo& material
    );

    // 生成 Sampler 声明
    static void generateSamplers(
        std::ostream& out,
        const MaterialInfo& material
    );

    // 生成 Varying 变量
    static void generateVaryings(
        std::ostream& out,
        const MaterialInfo& material,
        const Variant& variant
    );
};
```

---

## Fragment Shader 生成流程

### 1. 版本和扩展声明

```cpp
void generateVersionAndExtensions(
    std::ostream& fs,
    ShaderModel shaderModel,
    TargetLanguage targetLanguage)
{
    // 版本声明
    switch (shaderModel) {
        case ShaderModel::GL_ES_30:
            fs << "#version 300 es\n";
            break;
        case ShaderModel::GL_CORE_41:
            fs << "#version 410 core\n";
            break;
        case ShaderModel::GL_CORE_45:
            fs << "#version 450 core\n";
            break;
    }

    // 扩展
    if (targetLanguage == TargetLanguage::SPIRV) {
        fs << "#extension GL_GOOGLE_include_directive : enable\n";
        fs << "#extension GL_GOOGLE_cpp_style_line_directive : enable\n";
    }

    fs << "\n";
}
```

### 2. 预处理器定义

```cpp
void generateDefines(
    std::ostream& fs,
    const MaterialInfo& material,
    const Variant& variant)
{
    // 材质类型定义
    fs << "#define MATERIAL_DOMAIN_" << domainToString(material.domain) << "\n";
    fs << "#define MATERIAL_SHADING_" << shadingToString(material.shading) << "\n";

    // 变体定义
    if (variant.isDirectionalLightingEnabled()) {
        fs << "#define HAS_DIRECTIONAL_LIGHTING\n";
    }

    if (variant.isDynamicLightingEnabled()) {
        fs << "#define HAS_DYNAMIC_LIGHTING\n";
    }

    if (variant.isShadowReceiverEnabled()) {
        fs << "#define HAS_SHADOWING\n";
    }

    if (variant.isSkinningEnabled()) {
        fs << "#define HAS_SKINNING_OR_MORPHING\n";
    }

    // 顶点属性定义
    AttributeBitset attributes = material.requiredAttributes;
    if (attributes.test(VertexAttribute::UV0)) {
        fs << "#define HAS_ATTRIBUTE_UV0\n";
    }
    if (attributes.test(VertexAttribute::UV1)) {
        fs << "#define HAS_ATTRIBUTE_UV1\n";
    }
    if (attributes.test(VertexAttribute::COLOR)) {
        fs << "#define HAS_ATTRIBUTE_COLOR\n";
    }
    if (attributes.test(VertexAttribute::TANGENTS)) {
        fs << "#define HAS_ATTRIBUTE_TANGENTS\n";
    }

    fs << "\n";
}
```

### 3. Uniform Interface Block 生成

```cpp
void generateUniforms(
    std::ostream& fs,
    const MaterialInfo& material)
{
    const BufferInterfaceBlock& uib = material.uib;

    if (uib.isEmpty()) {
        return;
    }

    fs << "layout(std140) uniform " << uib.getName() << " {\n";

    for (const auto& field : uib.getFieldInfoList()) {
        fs << "    ";

        // 精度修饰符
        if (field.precision != Precision::DEFAULT) {
            fs << precisionToString(field.precision) << " ";
        }

        // 类型
        fs << uniformTypeToString(field.type) << " ";

        // 名称
        fs << field.name;

        // 数组
        if (field.size > 1) {
            fs << "[" << field.size << "]";
        }

        fs << ";\n";
    }

    fs << "} materialParams;\n\n";
}

// 类型转换函数
std::string uniformTypeToString(UniformType type) {
    switch (type) {
        case UniformType::BOOL:   return "bool";
        case UniformType::BOOL2:  return "bvec2";
        case UniformType::BOOL3:  return "bvec3";
        case UniformType::BOOL4:  return "bvec4";
        case UniformType::FLOAT:  return "float";
        case UniformType::FLOAT2: return "vec2";
        case UniformType::FLOAT3: return "vec3";
        case UniformType::FLOAT4: return "vec4";
        case UniformType::INT:    return "int";
        case UniformType::INT2:   return "ivec2";
        case UniformType::INT3:   return "ivec3";
        case UniformType::INT4:   return "ivec4";
        case UniformType::UINT:   return "uint";
        case UniformType::UINT2:  return "uvec2";
        case UniformType::UINT3:  return "uvec3";
        case UniformType::UINT4:  return "uvec4";
        case UniformType::MAT3:   return "mat3";
        case UniformType::MAT4:   return "mat4";
    }
}
```

**生成示例**:

```glsl
layout(std140) uniform MaterialParams {
    highp vec3 baseColor;
    highp float roughness;
    highp float metallic;
    highp float reflectance;
} materialParams;
```

### 4. Sampler 声明生成

```cpp
void generateSamplers(
    std::ostream& fs,
    const MaterialInfo& material)
{
    const SamplerInterfaceBlock& sib = material.sib;

    for (const auto& sampler : sib.getSamplerInfoList()) {
        fs << "uniform ";

        // 精度
        if (sampler.precision != Precision::DEFAULT) {
            fs << precisionToString(sampler.precision) << " ";
        }

        // 采样器类型
        fs << samplerTypeToString(sampler.type) << " ";

        // 名称（添加前缀避免冲突）
        fs << "materialParams_" << sampler.name << ";\n";
    }

    fs << "\n";
}

std::string samplerTypeToString(SamplerType type) {
    switch (type) {
        case SamplerType::SAMPLER_2D:         return "sampler2D";
        case SamplerType::SAMPLER_2D_ARRAY:   return "sampler2DArray";
        case SamplerType::SAMPLER_CUBEMAP:    return "samplerCube";
        case SamplerType::SAMPLER_EXTERNAL:   return "samplerExternalOES";
        case SamplerType::SAMPLER_3D:         return "sampler3D";
        case SamplerType::SAMPLER_CUBEMAP_ARRAY: return "samplerCubeArray";
    }
}
```

**生成示例**:

```glsl
uniform sampler2D materialParams_albedoMap;
uniform sampler2D materialParams_normalMap;
uniform sampler2D materialParams_roughnessMap;
```

### 5. Varying 变量生成

```cpp
void generateVaryings(
    std::ostream& fs,
    const MaterialInfo& material,
    const Variant& variant)
{
    // 位置（屏幕空间）
    fs << "in highp vec4 vertex_position;\n";

    // UV 坐标
    if (material.requiredAttributes.test(VertexAttribute::UV0)) {
        fs << "in highp vec2 vertex_uv0;\n";
    }
    if (material.requiredAttributes.test(VertexAttribute::UV1)) {
        fs << "in highp vec2 vertex_uv1;\n";
    }

    // 顶点颜色
    if (material.requiredAttributes.test(VertexAttribute::COLOR)) {
        fs << "in lowp vec4 vertex_color;\n";
    }

    // 法线、切线、副切线
    if (material.isLit) {
        fs << "in mediump vec3 vertex_worldNormal;\n";

        if (material.requiredAttributes.test(VertexAttribute::TANGENTS)) {
            fs << "in mediump vec3 vertex_worldTangent;\n";
            fs << "in mediump vec3 vertex_worldBitangent;\n";
        }
    }

    // 世界空间位置
    if (material.isLit || material.reflectionMode != ReflectionMode::DEFAULT) {
        fs << "in highp vec3 vertex_worldPosition;\n";
    }

    fs << "\n";
}
```

### 6. 辅助函数注入

```cpp
void generateHelperFunctions(
    std::ostream& fs,
    const MaterialInfo& material)
{
    // 包含公共辅助函数
    fs << "#include \"common_getters.glsl\"\n";  // getUV0(), getColor() 等

    // Shading Model 特定函数
    if (material.shading == Shading::LIT) {
        fs << "#include \"shading_model_standard.glsl\"\n";
        fs << "#include \"shading_lit.glsl\"\n";
    }
    else if (material.shading == Shading::SUBSURFACE) {
        fs << "#include \"shading_model_subsurface.glsl\"\n";
    }
    else if (material.shading == Shading::CLOTH) {
        fs << "#include \"shading_model_cloth.glsl\"\n";
    }
    else if (material.shading == Shading::UNLIT) {
        fs << "#include \"shading_unlit.glsl\"\n";
    }

    // BRDF
    if (material.isLit) {
        fs << "#include \"brdf.glsl\"\n";
        fs << "#include \"ambient_occlusion.glsl\"\n";
    }

    fs << "\n";
}
```

**内置函数示例** (`common_getters.glsl`):

```glsl
vec2 getUV0() {
    #if defined(HAS_ATTRIBUTE_UV0)
        return vertex_uv0;
    #else
        return vec2(0.0);
    #endif
}

vec2 getUV1() {
    #if defined(HAS_ATTRIBUTE_UV1)
        return vertex_uv1;
    #else
        return vec2(0.0);
    #endif
}

vec4 getColor() {
    #if defined(HAS_ATTRIBUTE_COLOR)
        return vertex_color;
    #else
        return vec4(1.0);
    #endif
}

vec3 getWorldPosition() {
    return vertex_worldPosition;
}

vec3 getWorldNormal() {
    return normalize(vertex_worldNormal);
}

vec3 getWorldTangent() {
    #if defined(HAS_ATTRIBUTE_TANGENTS)
        return normalize(vertex_worldTangent);
    #else
        return vec3(0.0, 0.0, 1.0);
    #endif
}
```

### 7. MaterialInputs 结构体

```cpp
void generateMaterialInputs(
    std::ostream& fs,
    const MaterialInfo& material)
{
    fs << "struct MaterialInputs {\n";
    fs << "    vec4 baseColor;\n";

    if (material.isLit) {
        fs << "    float roughness;\n";
        fs << "    float metallic;\n";
        fs << "    float reflectance;\n";
        fs << "    float ambientOcclusion;\n";
        fs << "    vec3 normal;\n";
        fs << "    vec3 emissive;\n";

        if (material.shading == Shading::CLOTH) {
            fs << "    vec3 sheenColor;\n";
            fs << "    float sheenRoughness;\n";
            fs << "    vec3 subsurfaceColor;\n";
        }

        if (material.hasClearCoat) {
            fs << "    float clearCoat;\n";
            fs << "    float clearCoatRoughness;\n";
            fs << "    vec3 clearCoatNormal;\n";
        }

        if (material.hasAnisotropy) {
            fs << "    float anisotropy;\n";
            fs << "    vec3 anisotropyDirection;\n";
        }
    }

    fs << "};\n\n";

    // 初始化函数
    fs << "MaterialInputs MaterialInputs_ctor() {\n";
    fs << "    MaterialInputs inputs;\n";
    fs << "    inputs.baseColor = vec4(1.0);\n";

    if (material.isLit) {
        fs << "    inputs.roughness = 1.0;\n";
        fs << "    inputs.metallic = 0.0;\n";
        fs << "    inputs.reflectance = 0.5;\n";
        fs << "    inputs.ambientOcclusion = 1.0;\n";
        fs << "    inputs.normal = vec3(0.0, 0.0, 1.0);\n";
        fs << "    inputs.emissive = vec3(0.0);\n";
    }

    fs << "    return inputs;\n";
    fs << "}\n\n";
}
```

### 8. 用户材质函数包装

```cpp
void generateMaterialFunction(
    std::ostream& fs,
    const MaterialInfo& material,
    const std::string& userCode)
{
    // 声明用户的材质函数
    fs << "// User material function\n";
    fs << userCode << "\n\n";

    // 包装函数
    fs << "void getMaterialInputs(out MaterialInputs material) {\n";
    fs << "    // 初始化\n";
    fs << "    material = MaterialInputs_ctor();\n";
    fs << "    \n";
    fs << "    // 调用用户材质函数\n";
    fs << "    material(material);\n";
    fs << "}\n\n";
}
```

### 9. 主函数生成

```cpp
void generateMainFunction(
    std::ostream& fs,
    const MaterialInfo& material,
    const Variant& variant)
{
    fs << "void main() {\n";

    // 1. 获取材质输入
    fs << "    MaterialInputs inputs;\n";
    fs << "    getMaterialInputs(inputs);\n";
    fs << "    \n";

    if (material.isLit) {
        // 2. 准备着色数据
        fs << "    // Prepare shading data\n";
        fs << "    ShadingData shadingData;\n";
        fs << "    shadingData.position = vertex_worldPosition;\n";
        fs << "    shadingData.normal = getWorldNormal();\n";
        fs << "    shadingData.view = normalize(frameUniforms.cameraPosition - vertex_worldPosition);\n";
        fs << "    \n";

        // 3. 应用法线贴图
        fs << "    // Apply normal mapping\n";
        fs << "    if (length(inputs.normal) > 0.1) {\n";
        fs << "        mat3 tbn = mat3(getWorldTangent(), getWorldBitangent(), shadingData.normal);\n";
        fs << "        shadingData.normal = normalize(tbn * inputs.normal);\n";
        fs << "    }\n";
        fs << "    \n";

        // 4. 计算光照
        fs << "    // Evaluate lighting\n";
        fs << "    vec4 color = vec4(0.0);\n";

        if (variant.isDirectionalLightingEnabled()) {
            fs << "    color += evaluateDirectionalLights(inputs, shadingData);\n";
        }

        if (variant.isDynamicLightingEnabled()) {
            fs << "    color += evaluateDynamicLights(inputs, shadingData);\n";
        }

        // 5. 添加环境光和自发光
        fs << "    color.rgb += evaluateIBL(inputs, shadingData);\n";
        fs << "    color.rgb += inputs.emissive;\n";
        fs << "    color.a = inputs.baseColor.a;\n";
    }
    else {
        // Unlit 材质
        fs << "    vec4 color = inputs.baseColor;\n";
    }

    // 6. 输出颜色
    fs << "    \n";
    fs << "    fragColor = color;\n";

    fs << "}\n";
}
```

---

## 完整生成示例

### 输入材质定义

```glsl
material {
    name : SimpleTextured,
    shadingModel : lit,
    parameters : [
        { type : sampler2d, name : albedoMap },
        { type : float, name : roughness }
    ],
    requires : [uv0]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        material.baseColor = texture(materialParams_albedoMap, getUV0());
        material.roughness = materialParams.roughness;
    }
}
```

### 生成的完整 Fragment Shader

```glsl
#version 300 es
precision highp float;

// Defines
#define MATERIAL_DOMAIN_SURFACE
#define MATERIAL_SHADING_LIT
#define HAS_ATTRIBUTE_UV0
#define HAS_DIRECTIONAL_LIGHTING

// Uniforms
layout(std140) uniform MaterialParams {
    highp float roughness;
} materialParams;

// Samplers
uniform sampler2D materialParams_albedoMap;

// Varyings
in highp vec4 vertex_position;
in highp vec2 vertex_uv0;
in mediump vec3 vertex_worldNormal;
in mediump vec3 vertex_worldTangent;
in mediump vec3 vertex_worldBitangent;
in highp vec3 vertex_worldPosition;

// Outputs
out vec4 fragColor;

// Helper functions
vec2 getUV0() { return vertex_uv0; }
vec3 getWorldPosition() { return vertex_worldPosition; }
vec3 getWorldNormal() { return normalize(vertex_worldNormal); }
vec3 getWorldTangent() { return normalize(vertex_worldTangent); }
vec3 getWorldBitangent() { return normalize(vertex_worldBitangent); }

// MaterialInputs structure
struct MaterialInputs {
    vec4 baseColor;
    float roughness;
    float metallic;
    float reflectance;
    float ambientOcclusion;
    vec3 normal;
    vec3 emissive;
};

MaterialInputs MaterialInputs_ctor() {
    MaterialInputs inputs;
    inputs.baseColor = vec4(1.0);
    inputs.roughness = 1.0;
    inputs.metallic = 0.0;
    inputs.reflectance = 0.5;
    inputs.ambientOcclusion = 1.0;
    inputs.normal = vec3(0.0, 0.0, 1.0);
    inputs.emissive = vec3(0.0);
    return inputs;
}

// User material function
void material(inout MaterialInputs material) {
    prepareMaterial(material);
    material.baseColor = texture(materialParams_albedoMap, getUV0());
    material.roughness = materialParams.roughness;
}

void getMaterialInputs(out MaterialInputs material) {
    material = MaterialInputs_ctor();
    material(material);
}

// Include shading functions
#include \"shading_model_standard.glsl\"
#include \"shading_lit.glsl\"
#include \"brdf.glsl\"

// Main function
void main() {
    MaterialInputs inputs;
    getMaterialInputs(inputs);

    ShadingData shadingData;
    shadingData.position = vertex_worldPosition;
    shadingData.normal = getWorldNormal();
    shadingData.view = normalize(frameUniforms.cameraPosition - vertex_worldPosition);

    if (length(inputs.normal) > 0.1) {
        mat3 tbn = mat3(getWorldTangent(), getWorldBitangent(), shadingData.normal);
        shadingData.normal = normalize(tbn * inputs.normal);
    }

    vec4 color = vec4(0.0);
    color += evaluateDirectionalLights(inputs, shadingData);
    color.rgb += evaluateIBL(inputs, shadingData);
    color.rgb += inputs.emissive;
    color.a = inputs.baseColor.a;

    fragColor = color;
}
```

---

## UibGenerator 和 SibGenerator

### UibGenerator - Uniform 块生成器

**位置**: `/libs/filamat/src/shaders/UibGenerator.h`

```cpp
class UibGenerator {
public:
    static BufferInterfaceBlock generate(
        const ParameterList& parameters,
        const MaterialBuilder& builder);

private:
    static void addParameter(
        BufferInterfaceBlock& uib,
        const Parameter& param);

    static void addBuiltInParameters(
        BufferInterfaceBlock& uib,
        const MaterialBuilder& builder);
};
```

**实现**:

```cpp
BufferInterfaceBlock UibGenerator::generate(
    const ParameterList& parameters,
    const MaterialBuilder& builder)
{
    BufferInterfaceBlock uib("MaterialParams");

    // 添加用户参数
    for (const Parameter& param : parameters) {
        if (param.isUniform()) {
            addParameter(uib, param);
        }
    }

    // 添加内置参数
    addBuiltInParameters(uib, builder);

    return uib;
}

void UibGenerator::addParameter(
    BufferInterfaceBlock& uib,
    const Parameter& param)
{
    uib.add(param.name, param.size, param.uniformType, param.precision);
}

void UibGenerator::addBuiltInParameters(
    BufferInterfaceBlock& uib,
    const MaterialBuilder& builder)
{
    // Specular Anti-Aliasing
    if (builder.hasSpecularAntiAliasing()) {
        uib.add("_specularAAScreenSpaceVariance", 1,
               UniformType::FLOAT, Precision::DEFAULT);
        uib.add("_specularAAThreshold", 1,
               UniformType::FLOAT, Precision::DEFAULT);
    }

    // Mask Threshold
    if (builder.getBlendingMode() == BlendingMode::MASKED) {
        uib.add("_maskThreshold", 1,
               UniformType::FLOAT, Precision::DEFAULT);
    }

    // ... 更多内置参数
}
```

### SibGenerator - Sampler 块生成器

**位置**: `/libs/filamat/src/shaders/SibGenerator.h`

```cpp
class SibGenerator {
public:
    static SamplerInterfaceBlock generate(
        const ParameterList& parameters);

private:
    static void addSampler(
        SamplerInterfaceBlock& sib,
        const Parameter& param);
};

SamplerInterfaceBlock SibGenerator::generate(
    const ParameterList& parameters)
{
    SamplerInterfaceBlock sib("MaterialParams");

    for (const Parameter& param : parameters) {
        if (param.isSampler()) {
            addSampler(sib, param);
        }
    }

    return sib;
}

void SibGenerator::addSampler(
    SamplerInterfaceBlock& sib,
    const Parameter& param)
{
    sib.add(param.name,
           param.samplerType,
           param.format,
           param.precision,
           param.multisample);
}
```

---

## 关键设计原则

1. **代码模块化**: 通过 `#include` 包含共享代码
2. **条件编译**: 使用预处理器定义控制代码生成
3. **类型安全**: 强类型的 MaterialInputs 结构
4. **性能优化**: 根据变体生成最小必要代码
5. **可维护性**: 清晰的代码结构和注释

---

## 总结

Shader 代码生成是一个复杂但有序的过程：

1. 收集材质信息 (MaterialInfo)
2. 生成 GLSL 基础结构（版本、定义、uniform）
3. 注入辅助函数和库代码
4. 包装用户材质代码
5. 生成主函数完成光照计算

通过这种方式，Filament 能够从简洁的材质定义生成完整、优化的 shader 代码。
