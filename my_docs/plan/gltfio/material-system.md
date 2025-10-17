# 材质系统

## 概述

gltfio 的材质系统负责将 glTF 材质定义转换为 Filament 的 Material 和 MaterialInstance。它支持 PBR metallic-roughness 工作流，以及各种 glTF 扩展（clearcoat, transmission, sheen 等）。

## 核心组件

### MaterialProvider

**代码位置**: `libs/gltfio/include/gltfio/MaterialProvider.h:141`

**接口**:
```cpp
class MaterialProvider {
public:
    virtual MaterialInstance* createMaterialInstance(
        MaterialKey* config,
        UvMap* uvmap,
        const char* label = "material",
        const char* extras = nullptr
    ) = 0;

    virtual Material* getMaterial(
        MaterialKey* config,
        UvMap* uvmap,
        const char* label = "material"
    );

    virtual const Material* const* getMaterials() const noexcept = 0;
    virtual size_t getMaterialsCount() const noexcept = 0;
    virtual void destroyMaterials() = 0;
    virtual bool needsDummyData(VertexAttribute attrib) const noexcept = 0;
};
```

### MaterialKey

**代码位置**: `libs/gltfio/include/gltfio/MaterialProvider.h:48-105`

**结构**:
```cpp
struct alignas(4) MaterialKey {
    // -- 32 bit boundary --
    bool doubleSided : 1;
    bool unlit : 1;
    bool hasVertexColors : 1;
    bool hasBaseColorTexture : 1;
    bool hasNormalTexture : 1;
    bool hasOcclusionTexture : 1;
    bool hasEmissiveTexture : 1;
    bool useSpecularGlossiness : 1;
    AlphaMode alphaMode : 4;
    bool enableDiagnostics : 4;
    union {
        struct {
            bool hasMetallicRoughnessTexture : 1;
            uint8_t metallicRoughnessUV : 7;
        };
        struct {
            bool hasSpecularGlossinessTexture : 1;
            uint8_t specularGlossinessUV : 7;
        };
    };
    uint8_t baseColorUV;

    // -- 32 bit boundary --
    bool hasClearCoatTexture : 1;
    uint8_t clearCoatUV : 7;
    bool hasClearCoatRoughnessTexture : 1;
    uint8_t clearCoatRoughnessUV : 7;
    bool hasClearCoatNormalTexture : 1;
    uint8_t clearCoatNormalUV : 7;
    bool hasClearCoat : 1;
    bool hasTransmission : 1;
    bool hasTextureTransforms : 6;

    // -- 32 bit boundary --
    uint8_t emissiveUV;
    uint8_t aoUV;
    uint8_t normalUV;
    bool hasTransmissionTexture : 1;
    uint8_t transmissionUV : 7;

    // -- 32 bit boundary --
    bool hasSheenColorTexture : 1;
    uint8_t sheenColorUV : 7;
    bool hasSheenRoughnessTexture : 1;
    uint8_t sheenRoughnessUV : 7;
    bool hasVolumeThicknessTexture : 1;
    uint8_t volumeThicknessUV : 7;
    bool hasSheen : 1;
    bool hasIOR : 1;
    bool hasVolume : 1;
    bool hasSpecular : 1;
    bool hasSpecularTexture : 1;
    bool hasSpecularColorTexture : 1;
    bool padding : 2;

    // -- 32 bit boundary --
    uint8_t specularTextureUV;
    uint8_t specularColorTextureUV;
    uint16_t padding2;
};

static_assert(sizeof(MaterialKey) == 20, "MaterialKey has unexpected size.");
```

**用途**:
- 作为材质缓存的键
- 描述材质的所有特性
- 确保填充位显式，以便哈希

### UvMap

**代码位置**: `libs/gltfio/include/gltfio/MaterialProvider.h:113-123`

```cpp
enum UvSet : uint8_t { UNUSED, UV0, UV1 };
constexpr int UvMapSize = 8;
using UvMap = std::array<UvSet, UvMapSize>;

inline uint8_t getNumUvSets(const UvMap& uvmap) {
    return std::max({
        uvmap[0], uvmap[1], uvmap[2], uvmap[3],
        uvmap[4], uvmap[5], uvmap[6], uvmap[7],
    });
}
```

## JitShaderProvider vs UbershaderProvider

### JitShaderProvider

**创建**:
```cpp
MaterialProvider* createJitShaderProvider(
    Engine* engine,
    bool optimizeShaders = false,
    utils::FixedCapacityVector<char const*> const& variantFilters = {}
);
```

**特性**:
- 运行时生成 shader（使用 filamat）
- 每个 MaterialKey 生成一个定制的 shader
- shader 代码简洁高效
- 首次加载较慢（编译 shader）
- 需要链接 filamat 库

**实现位置**: `libs/gltfio/src/JitShaderProvider.cpp`

**优点**:
- Shader 性能最优
- 没有不需要的分支

**缺点**:
- 需要 filamat (增加应用大小)
- 首次加载慢（编译时间）

### UbershaderProvider

**创建**:
```cpp
MaterialProvider* createUbershaderProvider(
    Engine* engine,
    const void* archive,
    size_t archiveByteCount
);
```

**特性**:
- 使用预编译的 ubershader
- 通过 #define 和条件分支适配不同材质
- shader 代码复杂，包含所有可能的特性
- 加载快速（无需编译）
- 不需要 filamat

**实现位置**: `libs/gltfio/src/UbershaderProvider.cpp`

**优点**:
- 加载速度快
- 不需要 filamat
- 应用体积小

**缺点**:
- Shader 性能较低（冗余分支）
- 需要 dummy 数据（见 needsDummyData）

### needsDummyData

**UbershaderProvider 实现**:
```cpp
bool needsDummyData(VertexAttribute attrib) const noexcept {
    // Ubershader 总是需要 UV0, UV1, COLOR
    return attrib == VertexAttribute::UV0 ||
           attrib == VertexAttribute::UV1 ||
           attrib == VertexAttribute::COLOR;
}
```

**JitShaderProvider 实现**:
```cpp
bool needsDummyData(VertexAttribute attrib) const noexcept {
    // JIT 生成的 shader 只包含需要的属性
    return false;
}
```

## MaterialKey 生成

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:1337-1439`

```cpp
MaterialKey getMaterialKey(const cgltf_data* srcAsset,
        const cgltf_material* inputMat, UvMap* uvmap, bool vertexColor,
        cgltf_texture_view* baseColorTexture,
        cgltf_texture_view* metallicRoughnessTexture) const {

    auto mrConfig = inputMat->pbr_metallic_roughness;
    auto sgConfig = inputMat->pbr_specular_glossiness;
    auto ccConfig = inputMat->clearcoat;
    auto trConfig = inputMat->transmission;
    auto shConfig = inputMat->sheen;
    auto vlConfig = inputMat->volume;
    auto spConfig = inputMat->specular;

    *baseColorTexture = mrConfig.base_color_texture;
    *metallicRoughnessTexture = mrConfig.metallic_roughness_texture;

    // 检测纹理变换
    bool hasTextureTransforms =
        sgConfig.diffuse_texture.has_transform ||
        sgConfig.specular_glossiness_texture.has_transform ||
        mrConfig.base_color_texture.has_transform ||
        mrConfig.metallic_roughness_texture.has_transform ||
        inputMat->normal_texture.has_transform ||
        inputMat->occlusion_texture.has_transform ||
        inputMat->emissive_texture.has_transform ||
        ccConfig.clearcoat_texture.has_transform ||
        ccConfig.clearcoat_roughness_texture.has_transform ||
        ccConfig.clearcoat_normal_texture.has_transform ||
        shConfig.sheen_color_texture.has_transform ||
        shConfig.sheen_roughness_texture.has_transform ||
        trConfig.transmission_texture.has_transform ||
        spConfig.specular_color_texture.has_transform ||
        spConfig.specular_texture.has_transform;

    MaterialKey matkey {
        .doubleSided = !!inputMat->double_sided,
        .unlit = !!inputMat->unlit,
        .hasVertexColors = vertexColor,
        .hasBaseColorTexture = baseColorTexture->texture != nullptr,
        .hasNormalTexture = inputMat->normal_texture.texture != nullptr,
        .hasOcclusionTexture = inputMat->occlusion_texture.texture != nullptr,
        .hasEmissiveTexture = inputMat->emissive_texture.texture != nullptr,
        .enableDiagnostics = mDiagnosticsEnabled,
        .baseColorUV = (uint8_t) baseColorTexture->texcoord,
        // ... 更多字段
    };

    // 处理 Specular-Glossiness 扩展
    if (inputMat->has_pbr_specular_glossiness) {
        matkey.useSpecularGlossiness = true;
        if (sgConfig.diffuse_texture.texture) {
            *baseColorTexture = sgConfig.diffuse_texture;
            matkey.hasBaseColorTexture = true;
            matkey.baseColorUV = (uint8_t) baseColorTexture->texcoord;
        }
        if (sgConfig.specular_glossiness_texture.texture) {
            *metallicRoughnessTexture = sgConfig.specular_glossiness_texture;
            matkey.hasSpecularGlossinessTexture = true;
            matkey.specularGlossinessUV = (uint8_t) metallicRoughnessTexture->texcoord;
        }
    } else {
        matkey.hasMetallicRoughnessTexture = metallicRoughnessTexture->texture != nullptr;
        matkey.metallicRoughnessUV = (uint8_t) metallicRoughnessTexture->texcoord;
    }

    // Alpha 模式
    switch (inputMat->alpha_mode) {
        case cgltf_alpha_mode_opaque:
            matkey.alphaMode = AlphaMode::OPAQUE;
            break;
        case cgltf_alpha_mode_mask:
            matkey.alphaMode = AlphaMode::MASK;
            break;
        case cgltf_alpha_mode_blend:
            matkey.alphaMode = AlphaMode::BLEND;
            break;
    }

    return matkey;
}
```

### constrainMaterial

**代码位置**: `libs/gltfio/src/MaterialProvider.cpp` (未提供，但被引用)

**功能**: 调整 MaterialKey 以满足 Filament 的限制

**约束**:
1. 只支持 2 个 UV 集（UV0, UV1）
2. 如果某个纹理的 UV 集不可用，禁用该纹理
3. 更新 UvMap

**示例**:
```cpp
// 输入: baseColor 使用 UV2, metallic 使用 UV3
// 输出: baseColor 映射到 UV0, metallic 映射到 UV1
// UvMap: [UNUSED, UNUSED, UV0, UV1, UNUSED, UNUSED, UNUSED, UNUSED]
```

## MaterialInstance 创建

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:1456-1719`

```cpp
MaterialInstance* createMaterialInstance(const cgltf_material* inputMat,
        UvMap* uvmap, bool vertexColor, FFilamentAsset* fAsset) {

    // 1. 检查缓存
    MaterialInstanceCache::Entry* const cacheEntry =
        mMaterialInstanceCache.getEntry(&inputMat, vertexColor);
    if (cacheEntry->instance) {
        *uvmap = cacheEntry->uvmap;
        return cacheEntry->instance;
    }

    // 2. 生成 MaterialKey
    cgltf_texture_view baseColorTexture;
    cgltf_texture_view metallicRoughnessTexture;
    MaterialKey matkey = getMaterialKey(srcAsset, inputMat, uvmap, vertexColor,
        &baseColorTexture, &metallicRoughnessTexture);

    // 3. 提取 extras
    CString extras;
    const cgltf_size extras_size = inputMat->extras.end_offset - inputMat->extras.start_offset;
    if (extras_size > 0) {
        extras = CString(srcAsset->json + inputMat->extras.start_offset, extras_size);
    }

    // 4. 创建 MaterialInstance（同时修改 MaterialKey 以满足约束）
    MaterialInstance* mi = mMaterials.createMaterialInstance(&matkey, uvmap,
        inputMat->name, extras.c_str());

    // 5. 设置 Alpha 模式参数
    if (mi->getMaterial()->getBlendingMode() == filament::BlendingMode::MASKED) {
        mi->setParameter("maskThreshold", inputMat->alpha_cutoff);
    }

    // 6. 设置基础参数
    const float* emissive = &inputMat->emissive_factor[0];
    float3 emissiveFactor(emissive[0], emissive[1], emissive[2]);
    if (inputMat->has_emissive_strength) {
        emissiveFactor *= inputMat->emissive_strength.emissive_strength;
    }
    mi->setParameter("emissiveFactor", emissiveFactor);

    const float* c = mrConfig.base_color_factor;
    mi->setParameter("baseColorFactor", float4(c[0], c[1], c[2], c[3]));
    mi->setParameter("metallicFactor", mrConfig.metallic_factor);
    mi->setParameter("roughnessFactor", mrConfig.roughness_factor);

    // 7. Specular-Glossiness 参数（如果启用）
    if (matkey.useSpecularGlossiness) {
        const float* df = sgConfig.diffuse_factor;
        const float* sf = sgConfig.specular_factor;
        mi->setParameter("baseColorFactor", float4(df[0], df[1], df[2], df[3]));
        mi->setParameter("specularFactor", float3(sf[0], sf[1], sf[2]));
        mi->setParameter("glossinessFactor", sgConfig.glossiness_factor);
    }

    // 8. 绑定纹理
    const TextureProvider::TextureFlags sRGB = TextureProvider::TextureFlags::sRGB;
    const TextureProvider::TextureFlags LINEAR = TextureProvider::TextureFlags::NONE;

    if (matkey.hasBaseColorTexture) {
        fAsset->addTextureBinding(mi, "baseColorMap", baseColorTexture.texture, sRGB);
        if (matkey.hasTextureTransforms) {
            const cgltf_texture_transform& uvt = baseColorTexture.transform;
            auto uvmat = matrixFromUvTransform(uvt.offset, uvt.rotation, uvt.scale);
            mi->setParameter("baseColorUvMatrix", uvmat);
        }
    }

    if (matkey.hasMetallicRoughnessTexture) {
        TextureProvider::TextureFlags srgb = inputMat->has_pbr_specular_glossiness ? sRGB : LINEAR;
        fAsset->addTextureBinding(mi, "metallicRoughnessMap",
            metallicRoughnessTexture.texture, srgb);
        if (matkey.hasTextureTransforms) {
            const cgltf_texture_transform& uvt = metallicRoughnessTexture.transform;
            auto uvmat = matrixFromUvTransform(uvt.offset, uvt.rotation, uvt.scale);
            mi->setParameter("metallicRoughnessUvMatrix", uvmat);
        }
    }

    if (matkey.hasNormalTexture) {
        fAsset->addTextureBinding(mi, "normalMap", inputMat->normal_texture.texture, LINEAR);
        if (matkey.hasTextureTransforms) {
            const cgltf_texture_transform& uvt = inputMat->normal_texture.transform;
            auto uvmat = matrixFromUvTransform(uvt.offset, uvt.rotation, uvt.scale);
            mi->setParameter("normalUvMatrix", uvmat);
        }
        mi->setParameter("normalScale", inputMat->normal_texture.scale);
    }

    // ... 更多纹理（occlusion, emissive, clearcoat 等）

    // 9. ClearCoat 扩展
    if (matkey.hasClearCoat) {
        mi->setParameter("clearCoatFactor", ccConfig.clearcoat_factor);
        mi->setParameter("clearCoatRoughnessFactor", ccConfig.clearcoat_roughness_factor);
        // ... clearcoat 纹理
    }

    // 10. Sheen 扩展
    if (matkey.hasSheen) {
        const float* s = shConfig.sheen_color_factor;
        mi->setParameter("sheenColorFactor", float3{s[0], s[1], s[2]});
        mi->setParameter("sheenRoughnessFactor", shConfig.sheen_roughness_factor);
        // ... sheen 纹理
    }

    // 11. Transmission 扩展
    if (matkey.hasTransmission) {
        mi->setParameter("transmissionFactor", trConfig.transmission_factor);
        // ... transmission 纹理
    }

    // 12. Volume 扩展
    if (matkey.hasVolume) {
        mi->setParameter("volumeThicknessFactor", vlConfig.thickness_factor);

        float attenuationDistance = vlConfig.attenuation_distance;
        const float* attenuationColor = vlConfig.attenuation_color;
        LinearColor absorption = Color::absorptionAtDistance(
            *reinterpret_cast<const LinearColor*>(attenuationColor), attenuationDistance);
        mi->setParameter("volumeAbsorption", RgbType::LINEAR, absorption);
        // ... volume 纹理
    }

    // 13. IOR 扩展
    if (matkey.hasIOR) {
        if (mi->getMaterial()->hasParameter("ior")) {
            mi->setParameter("ior", inputMat->ior.ior);
        }
        if (mi->getMaterial()->hasParameter("reflectance")) {
            float ior = inputMat->ior.ior;
            float f0 = (ior - 1.0f) / (ior + 1.0f);
            f0 *= f0;
            float reflectance = std::sqrt(f0 / 0.16f);
            mi->setParameter("reflectance", reflectance);
        }
    }

    // 14. Specular 扩展
    if (matkey.hasSpecular) {
        const float* s = spConfig.specular_color_factor;
        mi->setParameter("specularColorFactor", float3{s[0], s[1], s[2]});
        mi->setParameter("specularStrength", spConfig.specular_factor);
        // ... specular 纹理
    }

    // 15. 缓存并返回
    *cacheEntry = { mi, *uvmap };
    return mi;
}
```

## 纹理绑定

### addTextureBinding

**代码位置**: `libs/gltfio/src/FilamentAsset.cpp` (未提供，但被引用)

```cpp
void addTextureBinding(MaterialInstance* materialInstance, const char* parameterName,
        const cgltf_texture* srcTexture, TextureProvider::TextureFlags flags) {

    // 获取 cgltf_texture 的索引
    size_t textureIndex = srcTexture - mSourceAsset->hierarchy->textures;

    // 获取 TextureInfo
    TextureInfo& info = mTextures[textureIndex];

    // 记录绑定（延迟到纹理创建后）
    info.bindings.push_back({
        .materialInstance = materialInstance,
        .materialParameter = parameterName
    });

    // 记录纹理标志
    info.flags = flags;
}
```

### applyTextureBinding

**代码位置**: `libs/gltfio/src/FilamentAsset.cpp` (未提供，但被引用)

```cpp
void applyTextureBinding(size_t textureIndex, const TextureSlot& tb, bool addDependency) {
    TextureInfo& info = mTextures[textureIndex];

    // 创建纹理采样器
    TextureSampler sampler(
        TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
        TextureSampler::MagFilter::LINEAR,
        TextureSampler::WrapMode::REPEAT
    );

    // 设置材质参数
    tb.materialInstance->setParameter(tb.materialParameter, info.texture, sampler);

    // 添加到依赖图（用于渐进式加载）
    if (addDependency) {
        mDependencyGraph.addEdge(tb.materialInstance, info.texture);
    }
}
```

**时机**:
- `addTextureBinding()`: AssetLoader 创建 MaterialInstance 时
- `applyTextureBinding()`: ResourceLoader 创建 Texture 后

## 纹理变换

### matrixFromUvTransform

**代码位置**: `libs/gltfio/src/Utility.cpp` (未提供，但被引用)

```cpp
mat3f matrixFromUvTransform(float2 offset, float rotation, float2 scale) {
    // glTF 定义的 UV 变换顺序：scale -> rotate -> translate
    mat3f T = mat3f::translation(offset);
    mat3f R = mat3f::rotation(rotation);
    mat3f S = mat3f::scaling(scale);

    return T * R * S;
}
```

**glTF 纹理变换扩展** (KHR_texture_transform):
```json
{
  "texture": {
    "index": 0,
    "texCoord": 0,
    "extensions": {
      "KHR_texture_transform": {
        "offset": [0.5, 0.5],
        "rotation": 1.5708,
        "scale": [2.0, 2.0]
      }
    }
  }
}
```

**Shader 使用**:
```glsl
vec2 uv = uvMatrix * vec3(vUV0, 1.0);
vec4 color = texture(baseColorMap, uv);
```

## MaterialInstanceCache

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:183-253`

```cpp
class MaterialInstanceCache {
public:
    struct Entry {
        MaterialInstance* instance;
        UvMap uvmap;
    };

    MaterialInstanceCache(const cgltf_data* hierarchy) :
        mHierarchy(hierarchy),
        mMaterialInstances(hierarchy->materials_count, Entry{}),
        mMaterialInstancesWithVertexColor(hierarchy->materials_count, Entry{}) {}

    Entry* getEntry(const cgltf_material** mat, bool vertexColor) {
        if (*mat) {
            EntryVector& entries = vertexColor ?
                mMaterialInstancesWithVertexColor : mMaterialInstances;
            const cgltf_material* basePointer = mHierarchy->materials;
            return &entries[*mat - basePointer];
        }

        // 默认材质
        *mat = &kDefaultMat;
        return vertexColor ?
            &mDefaultMaterialInstanceWithVertexColor :
            &mDefaultMaterialInstance;
    }

    void flush(utils::FixedCapacityVector<MaterialInstance*>* dest) {
        // 收集所有非空的 MaterialInstance
        for (const Entry& entry : mMaterialInstances) {
            if (entry.instance) {
                dest->push_back(entry.instance);
            }
        }
        for (const Entry& entry : mMaterialInstancesWithVertexColor) {
            if (entry.instance) {
                dest->push_back(entry.instance);
            }
        }
        // ... 默认材质
    }

private:
    const cgltf_data* mHierarchy;
    EntryVector mMaterialInstances;
    EntryVector mMaterialInstancesWithVertexColor;
    Entry mDefaultMaterialInstance;
    Entry mDefaultMaterialInstanceWithVertexColor;
};
```

**关键点**:
- 按 `cgltf_material*` 指针索引
- 区分有/无顶点颜色（生成不同的 MaterialInstance）
- 生命周期：单次加载过程（createInstance 调用）

## 默认材质

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:76-91`

```cpp
static constexpr cgltf_material kDefaultMat = {
    .name = (char*) "Default GLTF material",
    .has_pbr_metallic_roughness = true,
    .has_pbr_specular_glossiness = false,
    .has_clearcoat = false,
    .has_transmission = false,
    .has_volume = false,
    .has_ior = false,
    .has_specular = false,
    .has_sheen = false,
    .pbr_metallic_roughness = {
        .base_color_factor = {1.0, 1.0, 1.0, 1.0},
        .metallic_factor = 1.0,
        .roughness_factor = 1.0,
    },
};
```

**使用场景**: glTF primitive 没有指定材质时

## 材质变体 (Material Variants)

### 数据结构

**代码位置**: `libs/gltfio/src/FFilamentInstance.h:45-54`

```cpp
struct VariantMapping {
    utils::Entity renderable;
    size_t primitiveIndex;
    MaterialInstance* material;
};

struct Variant {
    utils::CString name;
    std::vector<VariantMapping> mappings;
};
```

### 创建材质变体

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:909-929`

```cpp
void createMaterialVariants(const cgltf_mesh* mesh, Entity entity,
        FFilamentAsset* fAsset, FFilamentInstance* instance) {

    UvMap uvmap {};
    for (cgltf_size prim = 0; prim < mesh->primitives_count; ++prim) {
        const cgltf_primitive& srcPrim = mesh->primitives[prim];

        // 遍历所有材质映射（KHR_materials_variants 扩展）
        for (size_t i = 0; i < srcPrim.mappings_count; i++) {
            const size_t variantIndex = srcPrim.mappings[i].variant;
            const cgltf_material* material = srcPrim.mappings[i].material;

            bool hasVertexColor = primitiveHasVertexColor(srcPrim);
            MaterialInstance* mi = createMaterialInstance(material, &uvmap, hasVertexColor, fAsset);

            fAsset->mDependencyGraph.addEdge(entity, mi);

            // 记录变体映射
            instance->mVariants[variantIndex].mappings.push_back({entity, prim, mi});
        }
    }
}
```

### 应用材质变体

**代码位置**: `libs/gltfio/src/FilamentInstance.cpp` (未提供)

```cpp
void applyMaterialVariant(size_t variantIndex) noexcept {
    const Variant& variant = mVariants[variantIndex];

    for (const VariantMapping& mapping : variant.mappings) {
        auto renderable = mOwner->mEngine->getRenderableManager().getInstance(mapping.renderable);
        mOwner->mEngine->getRenderableManager().setMaterialInstanceAt(
            renderable, mapping.primitiveIndex, mapping.material);
    }
}
```

## 支持的 glTF 扩展

| 扩展 | 描述 | MaterialKey 字段 |
|-----|------|----------------|
| KHR_materials_pbrSpecularGlossiness | 高光-光泽度工作流 | `useSpecularGlossiness` |
| KHR_materials_clearcoat | 清漆层 | `hasClearCoat` |
| KHR_materials_transmission | 透射 | `hasTransmission` |
| KHR_materials_sheen | 织物光泽 | `hasSheen` |
| KHR_materials_volume | 体积散射 | `hasVolume` |
| KHR_materials_ior | 折射率 | `hasIOR` |
| KHR_materials_specular | 镜面反射 | `hasSpecular` |
| KHR_materials_unlit | 无光照 | `unlit` |
| KHR_materials_variants | 材质变体 | (通过 Variant) |
| KHR_texture_transform | 纹理变换 | `hasTextureTransforms` |

## 材质参数绑定

### 标准 PBR 参数

| glTF 参数 | Filament 参数 | 类型 |
|----------|--------------|-----|
| baseColorFactor | baseColorFactor | float4 |
| metallicFactor | metallicFactor | float |
| roughnessFactor | roughnessFactor | float |
| emissiveFactor | emissiveFactor | float3 |
| normalTexture.scale | normalScale | float |
| occlusionTexture.strength | aoStrength | float |
| alphaCutoff | maskThreshold | float |

### 纹理参数

| glTF 纹理 | Filament 参数 | sRGB |
|----------|--------------|------|
| baseColorTexture | baseColorMap | ✓ |
| metallicRoughnessTexture | metallicRoughnessMap | ✗ |
| normalTexture | normalMap | ✗ |
| occlusionTexture | occlusionMap | ✗ |
| emissiveTexture | emissiveMap | ✓ |

### 扩展参数

**ClearCoat**:
- `clearCoatFactor` (float)
- `clearCoatRoughnessFactor` (float)
- `clearCoatMap` (texture)
- `clearCoatRoughnessMap` (texture)
- `clearCoatNormalMap` (texture)

**Transmission**:
- `transmissionFactor` (float)
- `transmissionMap` (texture)

**Sheen**:
- `sheenColorFactor` (float3)
- `sheenRoughnessFactor` (float)
- `sheenColorMap` (texture, sRGB)
- `sheenRoughnessMap` (texture)

**Volume**:
- `volumeThicknessFactor` (float)
- `volumeAbsorption` (LinearColor)
- `volumeThicknessMap` (texture)

## 性能和内存优化

### Material 缓存

**策略**:
- MaterialProvider 缓存 Material（按 MaterialKey）
- 多个 MaterialInstance 可以共享同一个 Material
- 需要手动调用 `destroyMaterials()`

**好处**:
- 节省编译时间（JIT）
- 节省内存（shader 代码共享）

### MaterialInstance 不共享

**原因**:
- 每个 MaterialInstance 可能有不同的参数值
- 每个 MaterialInstance 可能绑定不同的 Texture

**示例**:
```
Material "PBR_baseColor_metallic"
  ├─ MaterialInstance 1 (baseColorFactor = red, metallicFactor = 0.8)
  ├─ MaterialInstance 2 (baseColorFactor = blue, metallicFactor = 0.5)
  └─ MaterialInstance 3 (baseColorFactor = green, metallicFactor = 1.0)
```

### UvMap 约束

**策略**: MaterialProvider 自动调整 MaterialKey，禁用无法映射的纹理

**示例**:
```
// 原始材质需要 4 个 UV 集
baseColorTexture:      UV0  ✓
normalTexture:         UV1  ✓
occlusionTexture:      UV2  ✗ (超出限制)
emissiveTexture:       UV3  ✗ (超出限制)

// 调整后
baseColorTexture:      UV0  ✓
normalTexture:         UV1  ✓
occlusionTexture:      disabled
emissiveTexture:       disabled
```

## 诊断模式

**启用**:
```cpp
assetLoader->enableDiagnostics(true);
```

**效果**:
- MaterialKey 的 `enableDiagnostics` 字段设为 true
- MaterialProvider 生成带有诊断着色的 shader
- 可以可视化法线、UV、切线等

## 常见问题

### 1. 材质看起来不对

**检查**:
- UV 映射是否正确
- 纹理的 sRGB 标志是否正确
- 是否缺少必要的纹理

### 2. Ubershader 性能差

**解决**:
- 切换到 JitShaderProvider
- 使用材质变体减少材质种类

### 3. 纹理缺失

**检查**:
- TextureProvider 是否注册了正确的 MIME 类型
- URI 数据是否已提供（`addResourceData`）
- 文件路径是否正确

## 下一步阅读

- [资源加载](./resource-loading.md) - 纹理加载和解码
- [GPU 交互](./gpu-interaction.md) - TextureProvider 实现细节
- [Mesh 处理](./mesh-processing.md) - UV 映射和顶点属性
