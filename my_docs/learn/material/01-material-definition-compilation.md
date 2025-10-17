# Material Definition and Compilation

## Overview

Filament's material system transforms high-level material definitions (.mat files) into optimized GPU shaders through a sophisticated compilation pipeline. This document covers the complete flow from material definition to GPU execution.

## Material Definition Format (.mat)

### Basic Structure

Materials are defined using a custom domain-specific language:

```glsl
material {
    name : LitOpaque,
    shadingModel : lit,
    blendingMode : opaque,
    culling : back,

    parameters : [
        {
            type : float3,
            name : baseColor
        },
        {
            type : float,
            name : roughness
        },
        {
            type : sampler2d,
            name : albedoMap
        }
    ],

    specularAntiAliasing : true,
    shadowSampling : PCF_LOW
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        material.baseColor.rgb = materialParams.baseColor;
        material.roughness = materialParams.roughness;

        // Sample texture if available
        if (materialParams_albedoMap != 0u) {
            material.baseColor.rgb *= texture(materialParams_albedoMap, getUV0()).rgb;
        }
    }
}
```

### Parameter Types

Filament supports comprehensive parameter types:

```cpp
// Scalar types
float, int, bool
float2, float3, float4
int2, int3, int4
mat3, mat4

// Texture samplers
sampler2d, samplerCube, sampler2dArray
samplerExternal  // Android camera/video
subpassInput     // Mobile tile-based rendering
```

### Shading Models

Core shading models with different lighting calculations:

```glsl
// Physically-based lighting
shadingModel : lit

// No lighting calculations
shadingModel : unlit

// Subsurface scattering for organic materials
shadingModel : subsurface

// Specialized fabric/cloth rendering
shadingModel : cloth

// Specular/glossiness workflow
shadingModel : specularGlossiness
```

## Compilation Pipeline

### 1. Material Compiler (matc)

The `matc` tool processes .mat files through multiple stages:

```cpp
// tools/matc/src/matc/MaterialCompiler.cpp
class MaterialCompiler {
    bool run(const matp::Config& config) {
        // Parse .mat file syntax
        auto materialDefinition = mParser.parse(source);

        // Build material package
        filamat::MaterialBuilder builder;
        builder.name(materialDefinition.name)
               .material(materialDefinition.fragmentCode)
               .shading(materialDefinition.shadingModel);

        // Generate for target platforms
        return compileForTargets(builder, config);
    }
};
```

### 2. MaterialBuilder API

Core compilation interface from `libs/filamat/include/filamat/MaterialBuilder.h`:

```cpp
class MaterialBuilder : public MaterialBuilderBase {
public:
    enum class Platform { DESKTOP, MOBILE, ALL };
    enum class TargetApi { OPENGL, VULKAN, METAL, WEBGPU, ALL };
    enum class Optimization { NONE, PREPROCESSOR, SIZE, PERFORMANCE };

    MaterialBuilder& name(const char* name);
    MaterialBuilder& material(const char* code);
    MaterialBuilder& shading(Shading shading);
    MaterialBuilder& blending(BlendingMode blending);
    MaterialBuilder& platform(Platform platform);
    MaterialBuilder& targetApi(TargetApi targetApi);
    MaterialBuilder& optimization(Optimization optimization);

    Package build(utils::JobSystem& jobSystem);
};
```

### 3. Shader Generation Process

Material compilation generates GLSL variants for different scenarios:

```cpp
// Vertex domain variants
enum class VertexDomain {
    OBJECT,     // Standard mesh rendering
    WORLD,      // World-space effects
    VIEW,       // Screen-space effects
    DEVICE      // Post-processing
};

// User-configurable variants
enum class UserVariantFilterBit : uint8_t {
    DIRECTIONAL_LIGHTING = 0x01,  // Directional lights
    DYNAMIC_LIGHTING     = 0x02,  // Point/spot lights
    SHADOW_RECEIVER      = 0x04,  // Shadow mapping
    SKINNING_OR_MORPHING = 0x08,  // Animation
    FOG                  = 0x10,  // Atmospheric effects
    VSM                  = 0x20,  // Variance shadow maps
    STE                  = 0x40,  // Stereoscopic rendering
    ALL                  = 0xFF
};
```

### 4. Cross-Platform Compilation

GLSL generation with platform-specific optimizations:

```cpp
struct CodeGenParams {
    ShaderModel shaderModel;      // GLSL_4_1, ESSL_3_0, etc.
    TargetApi targetApi;          // OPENGL, VULKAN, METAL
    TargetLanguage targetLanguage; // GLSL, SPIRV
    FeatureLevel featureLevel;    // FEATURE_LEVEL_1/2/3
};

// Generate shaders for each platform combination
std::vector<CodeGenParams> mCodeGenPermutations = {
    { GLSL_4_1,  OPENGL, GLSL,  FEATURE_LEVEL_3 },  // Desktop OpenGL
    { ESSL_3_0,  OPENGL, GLSL,  FEATURE_LEVEL_1 },  // Mobile OpenGL ES
    { GLSL_4_5,  VULKAN, SPIRV, FEATURE_LEVEL_3 },  // Desktop Vulkan
    { ESSL_3_1,  VULKAN, SPIRV, FEATURE_LEVEL_2 },  // Mobile Vulkan
    { MSL_2_3,   METAL,  SPIRV, FEATURE_LEVEL_3 }   // Apple Metal
};
```

## Shader Template System

### Material Function Interface

Every material must implement the material function:

```glsl
void material(inout MaterialInputs material) {
    prepareMaterial(material);  // Initialize defaults

    // Set material properties
    material.baseColor     = float4(1.0);
    material.roughness     = 1.0;
    material.metallic      = 0.0;
    material.reflectance   = 0.04;
    material.emissive      = float4(0.0);
    material.normal        = float3(0.0, 0.0, 1.0);

    // Advanced properties
    material.clearCoat           = 0.0;
    material.clearCoatRoughness  = 0.0;
    material.sheenColor          = float3(0.0);
    material.sheenRoughness      = 0.0;
    material.anisotropy          = 0.0;
    material.subsurfaceColor     = float3(0.0);
    material.subsurfacePower     = 12.234;
    material.thickness           = 0.5;
}
```

### Built-in Functions and Variables

Material shaders have access to rich built-in functionality:

```glsl
// Geometry information
float3 getWorldPosition();
float3 getWorldNormal();
float3 getWorldTangent();
float2 getUV0(), getUV1();  // Texture coordinates
float4 getColor();          // Vertex colors

// Lighting context
float3 getWorldCameraPosition();
float3 getWorldViewVector();

// Material parameters (auto-generated)
materialParams.baseColor     // User-defined parameters
materialParams.roughness
materialParams_albedoMap     // Texture sampler handle

// Utility functions
float3 inverseTransformDirection(float3 dir);
float2 uvToRenderTargetUV(float2 uv);
float luminance(float3 color);
```

## Package Format (filaflat)

### Binary Serialization

Compiled materials are serialized into efficient binary packages:

```cpp
// libs/filaflat/include/filaflat/MaterialChunk.h
class MaterialChunk {
    enum Type : uint32_t {
        MaterialGlsl        = 0x01,  // GLSL shader code
        MaterialSpirv       = 0x02,  // SPIRV bytecode
        MaterialMsl         = 0x03,  // Metal shading language
        MaterialSamplers    = 0x04,  // Sampler definitions
        MaterialUib         = 0x05,  // Uniform interface block
        MaterialConstants   = 0x06,  // Specialization constants
        MaterialPostProcess = 0x07   // Post-processing info
    };

    struct Header {
        uint32_t type;
        uint32_t size;
        uint32_t offset;
    };
};
```

### Chunk-Based Structure

Material packages use a chunk-based format for efficient loading:

```
┌─────────────────┐
│   File Header   │  Magic number, version, chunk count
├─────────────────┤
│  Chunk Index    │  Offset table for random access
├─────────────────┤
│ Material Info   │  Name, shading model, properties
├─────────────────┤
│   GLSL Chunk    │  GLSL shader variants
├─────────────────┤
│  SPIRV Chunk    │  SPIRV bytecode variants
├─────────────────┤
│ Sampler Chunk   │  Texture sampler definitions
├─────────────────┤
│    UB Chunk     │  Uniform buffer layout
└─────────────────┘
```

## Optimization Strategies

### 1. Variant Pruning

Eliminate unnecessary shader combinations:

```cpp
// Only compile variants actually used by the application
UserVariantFilterMask usedVariants =
    UserVariantFilterBit::DIRECTIONAL_LIGHTING |
    UserVariantFilterBit::DYNAMIC_LIGHTING     |
    UserVariantFilterBit::SHADOW_RECEIVER;

material->compile(CompilerPriorityQueue::HIGH, usedVariants);
```

### 2. Constant Specialization

Optimize shaders with compile-time constants:

```cpp
MaterialBuilder builder;
builder.constant("MATERIAL_HAS_NORMAL_MAP", true)
       .constant("MAX_LIGHT_COUNT", 8)
       .constant("USE_SHADOW_MAPPING", true);
```

### 3. Shader Minification

Remove dead code and optimize for size:

```cpp
builder.optimization(MaterialBuilder::Optimization::SIZE);
```

### 4. Asynchronous Compilation

Compile materials on background threads:

```cpp
material->compile(
    CompilerPriorityQueue::LOW,
    UserVariantFilterBit::ALL,
    callbackHandler,
    [](Material* material) {
        // Compilation complete callback
        LOG("Material %s compiled", material->getName());
    }
);
```

## Performance Considerations

### Material Creation Cost

- **Compilation**: Expensive, do at load time or asynchronously
- **Instance Creation**: Cheap, can be done at runtime
- **Parameter Updates**: Medium cost, batch when possible

### Memory Usage

```cpp
// Efficient material usage patterns
Material* baseMaterial = Material::Builder()
    .package(materialData, size)
    .build(*engine);

// Create instances for different parameter sets
MaterialInstance* instance1 = baseMaterial->createInstance("red_variant");
MaterialInstance* instance2 = baseMaterial->createInstance("blue_variant");

instance1->setParameter("baseColor", float3(1, 0, 0));
instance2->setParameter("baseColor", float3(0, 0, 1));
```

### GPU Resource Management

- **Uniform Buffers**: Automatically managed per MaterialInstance
- **Texture Binding**: Efficient descriptor set updates
- **Shader Switching**: Minimized through material sorting

This compilation system enables Filament to deliver high-performance rendering across multiple platforms while maintaining a clean, artist-friendly material authoring workflow.