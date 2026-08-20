# Material Properties and Shading Models

## Overview

Filament implements a comprehensive physically-based rendering (PBR) material system with support for advanced material properties and multiple shading models. This document explores the material property system, shading model implementations, and advanced rendering effects.

## Core PBR Properties

### Base Material Properties

Filament follows the metallic-roughness PBR workflow:

```glsl
struct MaterialInputs {
    // Base PBR properties
    float4 baseColor;        // Albedo (RGB) + opacity (A)
    float  roughness;        // Surface microsurface roughness [0,1]
    float  metallic;         // Metallic/dielectric blend [0,1]
    float  reflectance;      // Dielectric reflectance [0,1], default 0.04

    // Surface normal
    float3 normal;           // World-space normal vector

    // Emission
    float4 emissive;         // Self-emitted light (RGB) + exposure (A)

    // Ambient occlusion
    float  ambientOcclusion; // AO factor [0,1]
};
```

### Property Relationships

Understanding PBR property interactions:

```glsl
// Metallic workflow relationships
float3 diffuseColor = baseColor.rgb * (1.0 - metallic);
float3 f0 = mix(vec3(0.16 * reflectance * reflectance), baseColor.rgb, metallic);

// Roughness mapping (perceptual to linear)
float alpha = roughness * roughness;

// Energy conservation
float3 kS = fresnelSchlick(cosTheta, f0);  // Specular contribution
float3 kD = (1.0 - kS) * (1.0 - metallic); // Diffuse contribution
```

## Advanced Material Properties

### Clear Coat

Automotive paint and plastic materials:

```glsl
// Clear coat properties
float  clearCoat;            // Clear coat strength [0,1]
float  clearCoatRoughness;   // Clear coat surface roughness [0,1]
float3 clearCoatNormal;      // Clear coat normal (optional)

// Implementation in material function
void material(inout MaterialInputs material) {
    material.clearCoat = materialParams.clearCoat;
    material.clearCoatRoughness = materialParams.clearCoatRoughness;

    // Optional separate normal map for clear coat
    if (materialParams_clearCoatNormalMap != 0u) {
        material.clearCoatNormal = texture(materialParams_clearCoatNormalMap, getUV0()).xyz * 2.0 - 1.0;
    }
}
```

### Sheen

Fabric and velvet-like materials:

```glsl
// Sheen properties for fabric materials
float3 sheenColor;       // Sheen tint color
float  sheenRoughness;   // Sheen roughness [0,1]

// Typical fabric material setup
void material(inout MaterialInputs material) {
    material.baseColor = float4(0.1, 0.1, 0.2, 1.0);  // Dark base
    material.roughness = 0.9;                          // Very rough
    material.metallic = 0.0;                           // Non-metallic

    // Sheen for fabric look
    material.sheenColor = float3(0.8, 0.8, 1.0);      // Blue-white sheen
    material.sheenRoughness = 0.3;
}
```

### Anisotropy

Brushed metal and hair-like materials:

```glsl
// Anisotropic reflection
float anisotropy;        // Anisotropy strength [-1,1]
float3 anisotropyDirection; // Anisotropy direction (tangent space)

// Brushed metal example
void material(inout MaterialInputs material) {
    material.baseColor = float4(0.9, 0.9, 0.9, 1.0);
    material.roughness = 0.1;
    material.metallic = 1.0;

    // Anisotropic reflection along brush direction
    material.anisotropy = 0.8;
    // Direction derived from tangent space or texture
    material.anisotropyDirection = normalize(getWorldTangent());
}
```

### Subsurface Scattering

Organic materials with light penetration:

```glsl
// Subsurface properties
float3 subsurfaceColor;  // Subsurface scattering color
float  subsurfacePower;  // Scattering falloff [0.1, 20+]
float  thickness;        // Material thickness [0,1]

// Skin material example
void material(inout MaterialInputs material) {
    material.baseColor = float4(0.8, 0.6, 0.5, 1.0);
    material.roughness = 0.4;
    material.metallic = 0.0;

    // Subsurface scattering for skin
    material.subsurfaceColor = float3(0.9, 0.3, 0.2);  // Red undertone
    material.subsurfacePower = 12.234;                  // Typical skin value
    material.thickness = 0.5;
}
```

## Shading Models

### Lit Shading Model

Standard physically-based lighting:

```glsl
material {
    shadingModel : lit,
    // ... other properties
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        // Full PBR lighting calculation will be applied
        material.baseColor = materialParams.baseColor;
        material.roughness = materialParams.roughness;
        material.metallic = materialParams.metallic;
    }
}
```

Lit shading implements:
- **Diffuse BRDF**: Lambertian or Disney diffuse
- **Specular BRDF**: GGX/Trowbridge-Reitz distribution
- **Fresnel**: Schlick approximation
- **Geometric attenuation**: Smith G function
- **Multi-scattering compensation**: Energy-conserving BRDF

### Unlit Shading Model

No lighting calculations, perfect for UI and effects:

```glsl
material {
    shadingModel : unlit,
    vertexDomain : device  // Often used for post-processing
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        // Only baseColor and emissive are used
        material.baseColor = texture(materialParams_colorMap, getUV0());
        material.emissive = float4(0.0); // No additional emission
    }
}
```

### Subsurface Shading Model

Specialized lighting for translucent materials:

```glsl
material {
    shadingModel : subsurface,
    blendingMode : transparent,  // Often requires transparency
    twoSidedLighting : true      // Light from both sides
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        material.baseColor = materialParams.baseColor;
        material.roughness = materialParams.roughness;

        // Subsurface properties
        material.subsurfaceColor = materialParams.subsurfaceColor;
        material.subsurfacePower = materialParams.subsurfacePower;
        material.thickness = materialParams.thickness;
    }
}
```

### Cloth Shading Model

Optimized for fabric materials:

```glsl
material {
    shadingModel : cloth,
    // Cloth materials often use these settings
    specularAntiAliasing : true,
    clearCoatIor : 1.5
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        material.baseColor = materialParams.baseColor;
        material.roughness = materialParams.roughness;

        // Sheen is especially important for cloth
        material.sheenColor = materialParams.sheenColor;
        material.sheenRoughness = materialParams.sheenRoughness;

        // Subsurface scattering for fabric fibers
        material.subsurfaceColor = materialParams.subsurfaceColor;
    }
}
```

## Transparency and Blending

### Blending Modes

Filament supports multiple transparency modes:

```glsl
material {
    blendingMode : transparent,    // Alpha blending
    // OR
    blendingMode : fade,          // Dithered transparency
    // OR
    blendingMode : masked,        // Alpha testing
    maskThreshold : 0.5           // Alpha test threshold
}
```

### Transparency Mode Implementation

```glsl
enum class TransparencyMode : uint8_t {
    DEFAULT,                      // Blend normally
    TWO_PASSES_ONE_SIDE,         // Two-pass rendering
    TWO_PASSES_TWO_SIDES         // Separate front/back passes
};

// Two-pass transparent material
material {
    blendingMode : transparent,
    transparencyMode : twoPassesOneSide,
    depthWrite : false,          // Disable depth writing
    depthFunc : lessEqual        // Depth testing mode
}
```

### Alpha Coverage

Anti-aliased alpha testing for foliage and textures:

```glsl
material {
    blendingMode : masked,
    alphaToCoverage : true,      // Hardware alpha-to-coverage
    maskThreshold : 0.5,
    multisampleAntiAliasing : true
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        float4 albedo = texture(materialParams_albedoMap, getUV0());
        material.baseColor = albedo;

        // Alpha testing handled automatically
    }
}
```

## Advanced Rendering Effects

### Refraction

Real-time refraction through transparent materials:

```glsl
material {
    blendingMode : transparent,
    refractionMode : cubemap,     // OR screenSpace
    refractionType : solid,       // OR thin
    ior : 1.5                     // Index of refraction
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        material.baseColor = float4(0.9, 0.95, 1.0, 0.1);  // Tinted glass
        material.roughness = 0.0;                           // Smooth surface
        material.transmission = 1.0;                        // Full transmission
    }
}
```

### Reflection

Environment mapping and screen-space reflections:

```glsl
material {
    reflectionMode : screenSpace,  // Real-time SSR
    // OR
    reflectionMode : cubemap      // Environment mapping
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        material.baseColor = float4(0.1, 0.1, 0.1, 1.0);
        material.roughness = 0.05;    // Mirror-like
        material.metallic = 1.0;      // Metallic surface
        material.reflectance = 1.0;   // Maximum reflectance
    }
}
```

### Normal Mapping

Surface detail through normal maps:

```glsl
fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        // Sample normal map
        float3 normalSample = texture(materialParams_normalMap, getUV0()).xyz;

        // Transform from [0,1] to [-1,1]
        float3 normal = normalSample * 2.0 - 1.0;

        // Transform to world space (handled automatically by Filament)
        material.normal = normal;
    }
}
```

## Performance Optimization

### Material Property Costs

Understanding relative performance costs:

```
Performance Cost (relative):
├── baseColor       : 1x (minimal)
├── roughness       : 1x (minimal)
├── metallic        : 1x (minimal)
├── normal          : 2x (texture lookup + transform)
├── emissive        : 1x (minimal if zero)
├── clearCoat       : 3x (additional BRDF evaluation)
├── sheen           : 2x (fabric BRDF)
├── anisotropy      : 2x (anisotropic BRDF)
├── subsurface      : 4x (additional lighting passes)
└── refraction      : 5x (screen-space or cubemap lookup)
```

### Optimization Strategies

```glsl
// Use constants for static properties
material {
    constants : [
        { name : HAS_NORMAL_MAP, value : true },
        { name : USE_CLEAR_COAT, value : false }
    ]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        material.baseColor = materialParams.baseColor;

        // Conditional compilation eliminates unused branches
        #if HAS_NORMAL_MAP
            material.normal = texture(materialParams_normalMap, getUV0()).xyz * 2.0 - 1.0;
        #endif

        #if USE_CLEAR_COAT
            material.clearCoat = materialParams.clearCoat;
        #endif
    }
}
```

### LOD and Quality Scaling

```glsl
// Quality-based feature selection
#if QUALITY_HIGH
    material.sheenColor = calculateSheenColor();
    material.clearCoat = calculateClearCoat();
#elif QUALITY_MEDIUM
    material.sheenColor = materialParams.sheenColor;
#endif
// Low quality: skip expensive effects entirely
```

This material property system enables Filament to render a wide variety of materials efficiently while maintaining physical realism and artistic flexibility.