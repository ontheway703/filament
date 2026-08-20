# glTFIO 材质加载与处理

本文档详细说明 gltfio 库如何加载和处理 glTF 材质，以及与 Filament 材质系统的集成。

---

## 概述

**gltfio** 是 Filament 的 glTF 2.0 加载器，负责将 glTF 场景转换为 Filament 可渲染的对象。材质加载是其中的关键环节，需要将 glTF 的 PBR 材质定义转换为 Filament 材质实例。

### 核心挑战

1. **格式转换**: glTF 使用基于 JSON 的材质定义，Filament 使用编译后的二进制材质包（.filamat）
2. **特性映射**: glTF 支持多种扩展和特性，需要映射到 Filament 的材质模型
3. **性能优化**: 避免重复编译相同材质，需要高效的缓存机制
4. **约束处理**: Filament 有特定限制（如最多 2 组 UV），需要优雅降级

### 整体流程

```
glTF 文件
    ↓ [cgltf 解析]
cgltf_material
    ↓ [特征提取]
MaterialKey
    ↓ [MaterialProvider]
Material + MaterialInstance
    ↓ [参数设置]
最终可渲染材质
```

---

## MaterialProvider 架构

**MaterialProvider** 是 gltfio 材质加载的抽象层，定义了统一的材质创建接口。

### 接口定义

```cpp
// libs/gltfio/include/gltfio/MaterialProvider.h

class MaterialProvider {
public:
    // 创建材质实例
    virtual MaterialInstance* createMaterialInstance(
        MaterialKey* config,    // 材质特征配置
        UvMap* uvmap,           // UV 映射关系
        const char* label,      // 材质名称
        const char* extras      // 额外的 JSON 数据
    ) = 0;

    // 获取基础材质
    virtual Material* getMaterial(
        MaterialKey* config,
        UvMap* uvmap,
        const char* label
    ) = 0;

    // 获取所有缓存的材质
    virtual const Material* const* getMaterials() const noexcept = 0;
    virtual size_t getMaterialsCount() const noexcept = 0;

    // 销毁所有材质
    virtual void destroyMaterials() = 0;
};
```

### 两种实现

1. **JitShaderProvider**: 运行时生成材质（Just-In-Time）
2. **UbershaderProvider**: 使用预编译的 ubershader

---

## MaterialKey 结构

**MaterialKey** 是材质特征的完整描述，用于材质缓存和查找。

### 结构定义

```cpp
// libs/gltfio/include/gltfio/MaterialProvider.h

struct MaterialKey {
    // 基础特性
    bool doubleSided : 1;                    // 双面渲染
    bool unlit : 1;                          // 无光照（自发光）
    bool hasVertexColors : 1;                // 顶点颜色

    // 纹理特性
    bool hasBaseColorTexture : 1;            // 基础颜色纹理
    bool hasNormalTexture : 1;               // 法线贴图
    bool hasOcclusionTexture : 1;            // 环境光遮蔽
    bool hasEmissiveTexture : 1;             // 自发光纹理
    bool hasMetallicRoughnessTexture : 1;    // 金属粗糙度纹理

    // 高级特性
    bool hasClearCoat : 1;                   // 清漆层
    bool hasClearCoatTexture : 1;
    bool hasClearCoatRoughnessTexture : 1;
    bool hasClearCoatNormalTexture : 1;

    bool hasTransmission : 1;                // 透射（玻璃等）
    bool hasTransmissionTexture : 1;

    bool hasSheen : 1;                       // 光泽层（织物）
    bool hasSheenColorTexture : 1;
    bool hasSheenRoughnessTexture : 1;

    bool hasVolume : 1;                      // 体积散射
    bool hasVolumeThicknessTexture : 1;

    bool hasIOR : 1;                         // 折射率
    bool hasSpecular : 1;                    // 镜面反射控制
    bool hasSpecularTexture : 1;
    bool hasSpecularColorTexture : 1;

    // 兼容特性
    bool useSpecularGlossiness : 1;          // KHR_materials_pbrSpecularGlossiness
    bool hasSpecularGlossinessTexture : 1;

    // 其他
    bool hasTextureTransforms : 1;           // 纹理变换（旋转、缩放、偏移）
    bool enableDiagnostics : 1;              // 调试着色

    // 混合模式
    AlphaMode alphaMode : 4;                 // OPAQUE, MASK, BLEND

    // UV 坐标索引（每个纹理可以指定不同的 UV set）
    uint8_t baseColorUV;                     // 0-7
    uint8_t metallicRoughnessUV;
    uint8_t normalUV;
    uint8_t aoUV;
    uint8_t emissiveUV;
    uint8_t clearCoatUV;
    uint8_t clearCoatRoughnessUV;
    uint8_t clearCoatNormalUV;
    uint8_t sheenColorUV;
    uint8_t sheenRoughnessUV;
    uint8_t volumeThicknessUV;
    uint8_t transmissionUV;
    uint8_t specularTextureUV;
    uint8_t specularColorTextureUV;
    uint8_t specularGlossinessUV;
};

enum class AlphaMode : uint8_t {
    OPAQUE,  // 完全不透明
    MASK,    // 基于阈值的透明度裁剪
    BLEND    // Alpha 混合
};
```

### 特征提取流程

从 glTF 材质提取 MaterialKey：

```cpp
// libs/gltfio/src/AssetLoader.cpp

MaterialKey FAssetLoader::getMaterialKey(
        const cgltf_data* srcAsset,
        const cgltf_material* inputMat,
        UvMap* uvmap,
        bool vertexColor,
        cgltf_texture_view* baseColorTexture,
        cgltf_texture_view* metallicRoughnessTexture) const {

    auto mrConfig = inputMat->pbr_metallic_roughness;
    auto sgConfig = inputMat->pbr_specular_glossiness;
    auto ccConfig = inputMat->clearcoat;
    auto trConfig = inputMat->transmission;
    auto shConfig = inputMat->sheen;
    auto vlConfig = inputMat->volume;
    auto spConfig = inputMat->specular;

    // 提取纹理引用
    *baseColorTexture = mrConfig.base_color_texture;
    *metallicRoughnessTexture = mrConfig.metallic_roughness_texture;

    // 检查是否有纹理变换
    bool hasTextureTransforms =
        mrConfig.base_color_texture.has_transform ||
        mrConfig.metallic_roughness_texture.has_transform ||
        inputMat->normal_texture.has_transform ||
        // ... 其他纹理
        ;

    // 构建 MaterialKey
    MaterialKey matkey {
        .doubleSided = !!inputMat->double_sided,
        .unlit = !!inputMat->unlit,
        .hasVertexColors = vertexColor,
        .hasBaseColorTexture = baseColorTexture->texture != nullptr,
        .hasNormalTexture = inputMat->normal_texture.texture != nullptr,
        .hasOcclusionTexture = inputMat->occlusion_texture.texture != nullptr,
        .hasEmissiveTexture = inputMat->emissive_texture.texture != nullptr,

        // UV 坐标索引
        .baseColorUV = (uint8_t) baseColorTexture->texcoord,
        .normalUV = (uint8_t) inputMat->normal_texture.texcoord,
        .aoUV = (uint8_t) inputMat->occlusion_texture.texcoord,
        .emissiveUV = (uint8_t) inputMat->emissive_texture.texcoord,

        // 高级特性
        .hasClearCoat = !!inputMat->has_clearcoat,
        .hasTransmission = !!inputMat->has_transmission,
        .hasSheen = !!inputMat->has_sheen,
        .hasIOR = !!inputMat->has_ior,
        .hasVolume = !!inputMat->has_volume,

        // 其他
        .hasTextureTransforms = hasTextureTransforms,
        .enableDiagnostics = mDiagnosticsEnabled,
    };

    // 处理 alpha 模式
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

    // 处理 KHR_materials_pbrSpecularGlossiness 扩展
    if (inputMat->has_pbr_specular_glossiness) {
        matkey.useSpecularGlossiness = true;
        if (sgConfig.diffuse_texture.texture) {
            *baseColorTexture = sgConfig.diffuse_texture;
            matkey.hasBaseColorTexture = true;
            matkey.baseColorUV = (uint8_t) baseColorTexture->texcoord;
        }
        // ...
    } else {
        matkey.hasMetallicRoughnessTexture =
            metallicRoughnessTexture->texture != nullptr;
        matkey.metallicRoughnessUV =
            (uint8_t) metallicRoughnessTexture->texcoord;
    }

    return matkey;
}
```

### UvMap 结构

**UvMap** 记录 glTF 的 UV 索引到 Filament 的 UV0/UV1 的映射关系：

```cpp
// libs/gltfio/include/gltfio/MaterialProvider.h

struct UvMap {
    uint8_t at(uint8_t sourceIndex) const {
        return sourceIndex < 8 ? map[sourceIndex] : 0;
    }

    uint8_t map[8] = {};  // glTF 最多支持 8 组 UV，映射到 Filament 的 UV0/UV1
};
```

---

## JitShaderProvider - 运行时材质生成

**JitShaderProvider** 在运行时动态生成材质，使用 **filamat** 编译器和 **MaterialBuilder** API。

### 创建 JitShaderProvider

```cpp
// 创建 JIT shader provider
MaterialProvider* createJitShaderProvider(Engine* engine,
        OptimizationLevel optimizationLevel = OptimizationLevel::PERFORMANCE) {
    // 内部实现
}

// 使用示例
auto engine = Engine::create();
auto materials = createJitShaderProvider(engine);

AssetConfiguration config {
    .engine = engine,
    .materials = materials,
};

auto loader = AssetLoader::create(config);
```

### 运行时材质生成流程

```cpp
// libs/gltfio/src/JitShaderProvider.cpp

class JitShaderProvider : public MaterialProvider {
public:
    MaterialInstance* createMaterialInstance(
            MaterialKey* config, UvMap* uvmap,
            const char* label, const char* extras) override {
        return getMaterial(config, uvmap, label)->createInstance(label);
    }

    Material* getMaterial(MaterialKey* config, UvMap* uvmap,
                          const char* label) override {
        // 应用 UV 约束（Filament 最多支持 2 组 UV）
        constrainMaterial(config, uvmap);

        // 计算材质哈希
        const uint32_t hash = hash_value(*config);

        // 查找缓存
        auto iter = mMaterials.find(hash);
        if (iter != mMaterials.end()) {
            return iter.value();  // 缓存命中
        }

        // 缓存未命中，生成新材质
        Material* material = createMaterial(*config, uvmap, label);
        mMaterials[hash] = material;
        return material;
    }

private:
    Material* createMaterial(const MaterialKey& config,
                             UvMap* uvmap, const char* label) {
        // 1. 创建 MaterialBuilder
        MaterialBuilder builder;
        builder.name(label);

        // 2. 根据 MaterialKey 配置材质属性
        if (config.unlit) {
            builder.shading(Shading::UNLIT);
        } else {
            builder.shading(Shading::LIT);
        }

        if (config.doubleSided) {
            builder.doubleSided(true);
        }

        switch (config.alphaMode) {
            case AlphaMode::OPAQUE:
                builder.blending(BlendingMode::OPAQUE);
                break;
            case AlphaMode::MASK:
                builder.blending(BlendingMode::MASKED);
                break;
            case AlphaMode::BLEND:
                builder.blending(BlendingMode::TRANSPARENT);
                break;
        }

        // 3. 声明参数
        builder.parameter("baseColorFactor", MaterialBuilder::Type::FLOAT4);
        builder.parameter("metallicFactor", MaterialBuilder::Type::FLOAT);
        builder.parameter("roughnessFactor", MaterialBuilder::Type::FLOAT);
        builder.parameter("emissiveFactor", MaterialBuilder::Type::FLOAT3);

        if (config.hasBaseColorTexture) {
            builder.parameter("baseColorMap", MaterialBuilder::SamplerType::SAMPLER_2D);
        }
        if (config.hasNormalTexture) {
            builder.parameter("normalMap", MaterialBuilder::SamplerType::SAMPLER_2D);
        }
        // ... 其他纹理参数

        // 4. 生成 shader 代码
        std::string shaderCode = generateShaderCode(config, uvmap);
        builder.material(shaderCode.c_str());

        // 5. 编译材质
        Package pkg = builder.build(mEngine->getJobSystem());
        return Material::Builder()
            .package(pkg.getData(), pkg.getSize())
            .build(*mEngine);
    }

    std::string generateShaderCode(const MaterialKey& config,
                                     const UvMap* uvmap) {
        std::string shader = "void material(inout MaterialInputs material) {\n";
        shader += "    prepareMaterial(material);\n";

        // 基础颜色
        if (config.hasBaseColorTexture) {
            shader += "    vec4 baseColor = texture(materialParams_baseColorMap, ";
            shader += "getUV" + std::to_string(uvmap->at(config.baseColorUV));
            shader += "());\n";
            shader += "    baseColor *= materialParams.baseColorFactor;\n";
        } else {
            shader += "    vec4 baseColor = materialParams.baseColorFactor;\n";
        }

        if (config.hasVertexColors) {
            shader += "    baseColor *= getColor();\n";
        }

        shader += "    material.baseColor = baseColor;\n";

        // 法线贴图
        if (config.hasNormalTexture && !config.unlit) {
            shader += "    vec3 normal = texture(materialParams_normalMap, ";
            shader += "getUV" + std::to_string(uvmap->at(config.normalUV));
            shader += "()).xyz * 2.0 - 1.0;\n";
            shader += "    material.normal = normalize(normal);\n";
        }

        // 金属度和粗糙度
        if (!config.unlit) {
            if (config.hasMetallicRoughnessTexture) {
                shader += "    vec4 mr = texture(materialParams_metallicRoughnessMap, ";
                shader += "getUV" + std::to_string(uvmap->at(config.metallicRoughnessUV));
                shader += "());\n";
                shader += "    material.metallic = mr.b * materialParams.metallicFactor;\n";
                shader += "    material.roughness = mr.g * materialParams.roughnessFactor;\n";
            } else {
                shader += "    material.metallic = materialParams.metallicFactor;\n";
                shader += "    material.roughness = materialParams.roughnessFactor;\n";
            }
        }

        // 自发光
        if (config.hasEmissiveTexture) {
            shader += "    vec3 emissive = texture(materialParams_emissiveMap, ";
            shader += "getUV" + std::to_string(uvmap->at(config.emissiveUV));
            shader += "()).rgb;\n";
            shader += "    material.emissive = vec4(emissive * materialParams.emissiveFactor, 0.0);\n";
        } else {
            shader += "    material.emissive = vec4(materialParams.emissiveFactor, 0.0);\n";
        }

        // 环境光遮蔽
        if (config.hasOcclusionTexture) {
            shader += "    material.ambientOcclusion = texture(materialParams_occlusionMap, ";
            shader += "getUV" + std::to_string(uvmap->at(config.aoUV));
            shader += "()).r;\n";
        }

        // 清漆层
        if (config.hasClearCoat) {
            shader += "    material.clearCoat = materialParams.clearCoatFactor;\n";
            if (config.hasClearCoatTexture) {
                shader += "    material.clearCoat *= texture(materialParams_clearCoatMap, ";
                shader += "getUV" + std::to_string(uvmap->at(config.clearCoatUV));
                shader += "()).r;\n";
            }
            // ...
        }

        shader += "}\n";
        return shader;
    }

    Engine* mEngine;
    tsl::robin_map<uint32_t, Material*> mMaterials;  // 材质缓存
    bool mOptimizeShaders;
};
```

### JitShaderProvider 优缺点

**优点**:
- ✅ 按需生成，只编译实际使用的材质变体
- ✅ Shader 代码精简，针对特定特性优化
- ✅ 支持所有材质特性，包括扩展
- ✅ 运行时性能最优

**缺点**:
- ❌ 首次加载时需要编译，可能较慢（秒级）
- ❌ 需要链接 filamat 库，增加应用大小
- ❌ 材质缓存占用内存

**适用场景**:
- 桌面应用（PC/Mac）
- 材质种类多样的场景
- 需要支持 glTF 扩展的项目
- 开发和调试阶段

---

## UbershaderProvider - 预编译材质

**UbershaderProvider** 使用预编译的 **ubershader**（超级着色器），包含所有可能的特性分支。

### 创建 UbershaderProvider

```cpp
// 创建 ubershader provider（从内嵌的材质包）
MaterialProvider* createUbershaderProvider(Engine* engine,
        const void* archive, size_t archiveByteCount);

// 使用示例
#include "gltf_ubershaders.h"  // 预编译的材质数据

auto engine = Engine::create();
auto materials = createUbershaderProvider(engine,
    GLTF_UBERSHADERS_DATA, GLTF_UBERSHADERS_SIZE);

AssetConfiguration config {
    .engine = engine,
    .materials = materials,
};

auto loader = AssetLoader::create(config);
```

### Ubershader 原理

Ubershader 是一个包含所有特性分支的"超级材质"：

```glsl
// ubershader fragment shader (简化示例)

void material(inout MaterialInputs material) {
    prepareMaterial(material);

    vec4 baseColor = materialParams.baseColorFactor;

    #if defined(HAS_BASE_COLOR_TEXTURE)
        #if defined(USE_UV0)
            baseColor *= texture(materialParams_baseColorMap, getUV0());
        #elif defined(USE_UV1)
            baseColor *= texture(materialParams_baseColorMap, getUV1());
        #endif
    #endif

    #if defined(HAS_VERTEX_COLORS)
        baseColor *= getColor();
    #endif

    material.baseColor = baseColor;

    #if !defined(UNLIT)
        float metallic = materialParams.metallicFactor;
        float roughness = materialParams.roughnessFactor;

        #if defined(HAS_METALLIC_ROUGHNESS_TEXTURE)
            vec4 mr = texture(materialParams_metallicRoughnessMap, getUV0());
            metallic *= mr.b;
            roughness *= mr.g;
        #endif

        material.metallic = metallic;
        material.roughness = roughness;

        #if defined(HAS_NORMAL_TEXTURE)
            material.normal = texture(materialParams_normalMap, getUV0()).xyz * 2.0 - 1.0;
        #endif

        #if defined(HAS_CLEAR_COAT)
            material.clearCoat = materialParams.clearCoatFactor;
            #if defined(HAS_CLEAR_COAT_TEXTURE)
                material.clearCoat *= texture(materialParams_clearCoatMap, getUV0()).r;
            #endif
        #endif
    #endif

    // ... 更多特性
}
```

### 材质实例创建

```cpp
// libs/gltfio/src/UbershaderProvider.cpp

MaterialInstance* UbershaderProvider::createMaterialInstance(
        MaterialKey* config, UvMap* uvmap,
        const char* label, const char* extras) {

    // 应用 UV 约束
    constrainMaterial(config, uvmap);

    // 查找匹配的 ubershader
    Material* material = findUbershader(*config);
    if (!material) {
        // 找不到完全匹配的，选择最接近的
        material = findClosestUbershader(*config);
        logFeatureCompatibilityWarning(*config, material);
    }

    // 创建材质实例
    MaterialInstance* mi = material->createInstance(label);

    // 设置特性开关（通过材质参数控制分支）
    mi->setParameter("hasBaseColorTexture", config->hasBaseColorTexture);
    mi->setParameter("hasNormalTexture", config->hasNormalTexture);
    mi->setParameter("hasMetallicRoughnessTexture",
                     config->hasMetallicRoughnessTexture);
    // ... 其他开关

    return mi;
}
```

### Ubershader 构建

Ubershader 材质包可以通过 matc 预编译：

```bash
# 编译 ubershader
matc \
    -p all \
    -a all \
    -O performance \
    -o gltf_ubershader.filamat \
    gltf_ubershader.mat

# 转换为 C 数组（嵌入应用）
xxd -i gltf_ubershader.filamat > gltf_ubershaders.h
```

### UbershaderProvider 优缺点

**优点**:
- ✅ 加载极快，无需运行时编译
- ✅ 不依赖 filamat，应用体积更小
- ✅ 材质数量固定，内存占用可预测

**缺点**:
- ❌ Shader 代码冗长，包含所有分支
- ❌ 运行时性能略低（更多条件判断）
- ❌ 材质包文件较大（~几 MB）
- ❌ 需要 dummy 顶点属性（UV1, COLOR 等）

**适用场景**:
- 移动应用（Android/iOS）
- WebGL 应用
- 需要快速加载的场景
- 发布版本

---

## UV 映射约束处理

Filament 最多支持 **2 组 UV 坐标**（UV0 和 UV1），而 glTF 支持最多 **8 组**。gltfio 需要将多余的 UV 集映射到有限的 UV0/UV1。

### constrainMaterial 函数

```cpp
// libs/gltfio/src/MaterialProvider.cpp

void constrainMaterial(MaterialKey* key, UvMap* uvmap) {
    // 收集所有纹理使用的 UV 索引
    std::vector<uint8_t> uvIndices;

    if (key->hasBaseColorTexture) {
        uvIndices.push_back(key->baseColorUV);
    }
    if (key->hasMetallicRoughnessTexture) {
        uvIndices.push_back(key->metallicRoughnessUV);
    }
    if (key->hasNormalTexture) {
        uvIndices.push_back(key->normalUV);
    }
    if (key->hasOcclusionTexture) {
        uvIndices.push_back(key->aoUV);
    }
    if (key->hasEmissiveTexture) {
        uvIndices.push_back(key->emissiveUV);
    }
    // ... 其他纹理

    // 去重并排序
    std::sort(uvIndices.begin(), uvIndices.end());
    auto last = std::unique(uvIndices.begin(), uvIndices.end());
    uvIndices.erase(last, uvIndices.end());

    // 如果超过 2 组 UV，需要降级
    if (uvIndices.size() > 2) {
        slog.w << "Material uses " << uvIndices.size()
               << " UV sets, but Filament supports max 2. "
               << "Degrading material." << io::endl;

        // glTF 规范的降级顺序（保留最重要的纹理）:
        // 1. baseColor 和 metallicRoughness（核心 PBR）
        // 2. normal（视觉质量）
        // 3. occlusion（环境细节）
        // 4. emissive（发光效果）
        // 5. 其他扩展纹理

        // 只保留前 2 个 UV 索引
        std::set<uint8_t> supportedUVs(uvIndices.begin(), uvIndices.begin() + 2);

        // 移除不支持的纹理
        if (supportedUVs.find(key->aoUV) == supportedUVs.end() &&
            key->hasOcclusionTexture) {
            slog.w << "  Disabling occlusion texture (UV"
                   << (int)key->aoUV << ")" << io::endl;
            key->hasOcclusionTexture = false;
        }

        if (supportedUVs.find(key->emissiveUV) == supportedUVs.end() &&
            key->hasEmissiveTexture) {
            slog.w << "  Disabling emissive texture (UV"
                   << (int)key->emissiveUV << ")" << io::endl;
            key->hasEmissiveTexture = false;
        }

        // ... 检查其他纹理
    }

    // 构建 UvMap（glTF UV 索引 → Filament UV0/UV1）
    for (size_t i = 0; i < uvIndices.size() && i < 2; ++i) {
        uvmap->map[uvIndices[i]] = i;  // UV0 或 UV1
    }
}
```

### UV 映射示例

```
glTF 材质使用的 UV:
  - baseColor:  UV0
  - normal:     UV0
  - metallic:   UV1
  - emissive:   UV2  ← 超过 2 组！

降级后:
  - baseColor:  UV0  →  Filament UV0
  - normal:     UV0  →  Filament UV0
  - metallic:   UV1  →  Filament UV1
  - emissive:   [禁用]

UvMap 映射:
  uvmap.map[0] = 0  (glTF UV0 → Filament UV0)
  uvmap.map[1] = 1  (glTF UV1 → Filament UV1)
  uvmap.map[2] = 0  (未使用)
```

---

## 完整材质加载流程

### AssetLoader 集成

```cpp
// libs/gltfio/src/AssetLoader.cpp

MaterialInstance* FAssetLoader::createMaterialInstance(
        const cgltf_material* inputMat,
        UvMap* uvmap,
        bool vertexColor,
        FFilamentAsset* fAsset) {

    const cgltf_data* srcAsset = fAsset->mSourceAsset->hierarchy;

    // 1. 检查缓存（避免重复创建）
    MaterialInstanceCache::Entry* cacheEntry =
        mMaterialInstanceCache.getEntry(&inputMat, vertexColor);
    if (cacheEntry->instance) {
        *uvmap = cacheEntry->uvmap;
        return cacheEntry->instance;  // 缓存命中
    }

    // 2. 提取材质特征
    cgltf_texture_view baseColorTexture;
    cgltf_texture_view metallicRoughnessTexture;
    MaterialKey matkey = getMaterialKey(srcAsset, inputMat, uvmap, vertexColor,
                                        &baseColorTexture, &metallicRoughnessTexture);

    // 3. 获取 extras（额外的 JSON 数据）
    CString extras;
    const cgltf_size extrasSize =
        inputMat->extras.end_offset - inputMat->extras.start_offset;
    if (extrasSize > 0) {
        extras = CString(srcAsset->json + inputMat->extras.start_offset, extrasSize);
    }

    // 4. 通过 MaterialProvider 创建材质实例
    // 这会应用 UV 约束，修改 matkey 和 uvmap
    const char* label = inputMat->name ? inputMat->name : "material";
    MaterialInstance* mi = mMaterials.createMaterialInstance(
        &matkey, uvmap, label, extras.c_str());

    if (!mi) {
        slog.e << "No material with the specified requirements exists." << io::endl;
        return nullptr;
    }

    // 5. 设置材质参数
    auto mrConfig = inputMat->pbr_metallic_roughness;
    auto sgConfig = inputMat->pbr_specular_glossiness;
    auto ccConfig = inputMat->clearcoat;
    auto trConfig = inputMat->transmission;
    auto shConfig = inputMat->sheen;

    // Alpha cutoff（用于 MASK 模式）
    if (mi->getMaterial()->getBlendingMode() == BlendingMode::MASKED) {
        mi->setMaskThreshold(inputMat->alpha_cutoff);
    }

    // 自发光
    const float* emissive = &inputMat->emissive_factor[0];
    float3 emissiveFactor(emissive[0], emissive[1], emissive[2]);
    if (inputMat->has_emissive_strength) {
        emissiveFactor *= inputMat->emissive_strength.emissive_strength;
    }
    mi->setParameter("emissiveFactor", emissiveFactor);

    // 基础颜色、金属度、粗糙度
    const float* c = mrConfig.base_color_factor;
    mi->setParameter("baseColorFactor", float4(c[0], c[1], c[2], c[3]));
    mi->setParameter("metallicFactor", mrConfig.metallic_factor);
    mi->setParameter("roughnessFactor", mrConfig.roughness_factor);

    // 处理 KHR_materials_pbrSpecularGlossiness
    if (matkey.useSpecularGlossiness) {
        const float* df = sgConfig.diffuse_factor;
        const float* sf = sgConfig.specular_factor;
        mi->setParameter("baseColorFactor", float4(df[0], df[1], df[2], df[3]));
        mi->setParameter("specularFactor", float3(sf[0], sf[1], sf[2]));
        mi->setParameter("glossinessFactor", sgConfig.glossiness_factor);
    }

    // 6. 绑定纹理（延迟加载）
    const TextureProvider::TextureFlags sRGB = TextureProvider::TextureFlags::sRGB;
    const TextureProvider::TextureFlags LINEAR = TextureProvider::TextureFlags::NONE;

    if (matkey.hasBaseColorTexture) {
        fAsset->addTextureBinding(mi, "baseColorMap",
                                  baseColorTexture.texture, sRGB);

        // 纹理变换
        if (matkey.hasTextureTransforms) {
            const cgltf_texture_transform& uvt = baseColorTexture.transform;
            auto uvmat = matrixFromUvTransform(uvt.offset, uvt.rotation, uvt.scale);
            mi->setParameter("baseColorUvMatrix", uvmat);
        }
    }

    if (matkey.hasMetallicRoughnessTexture) {
        fAsset->addTextureBinding(mi, "metallicRoughnessMap",
                                  metallicRoughnessTexture.texture, LINEAR);
    }

    if (matkey.hasNormalTexture) {
        fAsset->addTextureBinding(mi, "normalMap",
                                  inputMat->normal_texture.texture, LINEAR);
        mi->setParameter("normalScale", inputMat->normal_texture.scale);
    }

    if (matkey.hasOcclusionTexture) {
        fAsset->addTextureBinding(mi, "occlusionMap",
                                  inputMat->occlusion_texture.texture, LINEAR);
        mi->setParameter("aoStrength", inputMat->occlusion_texture.scale);
    }

    if (matkey.hasEmissiveTexture) {
        fAsset->addTextureBinding(mi, "emissiveMap",
                                  inputMat->emissive_texture.texture, sRGB);
    }

    // 清漆层
    if (matkey.hasClearCoat) {
        mi->setParameter("clearCoatFactor", ccConfig.clearcoat_factor);
        mi->setParameter("clearCoatRoughnessFactor", ccConfig.clearcoat_roughness_factor);
        if (matkey.hasClearCoatTexture) {
            fAsset->addTextureBinding(mi, "clearCoatMap",
                                      ccConfig.clearcoat_texture.texture, LINEAR);
        }
        // ...
    }

    // 透射
    if (matkey.hasTransmission) {
        mi->setParameter("transmissionFactor", trConfig.transmission_factor);
        // ...
    }

    // 7. 缓存材质实例
    cacheEntry->instance = mi;
    cacheEntry->uvmap = *uvmap;

    return mi;
}
```

### 完整使用示例

```cpp
#include <filament/Engine.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/MaterialProvider.h>
#include <gltfio/ResourceLoader.h>

using namespace filament;
using namespace filament::gltfio;

void loadGltfModel(const char* path) {
    // 1. 创建引擎
    Engine* engine = Engine::create();

    // 2. 选择材质提供器
    // 方式 A: JIT 编译（桌面平台推荐）
    MaterialProvider* materials = createJitShaderProvider(engine);

    // 方式 B: Ubershader（移动平台推荐）
    // #include "gltf_ubershaders.h"
    // MaterialProvider* materials = createUbershaderProvider(
    //     engine, GLTF_UBERSHADERS_DATA, GLTF_UBERSHADERS_SIZE);

    // 3. 创建资源加载器
    AssetConfiguration config {
        .engine = engine,
        .materials = materials,
    };
    AssetLoader* loader = AssetLoader::create(config);

    // 4. 加载 glTF 文件
    std::vector<uint8_t> content = readFile(path);
    FilamentAsset* asset = loader->createAsset(content.data(), content.size());
    if (!asset) {
        slog.e << "Failed to load asset" << io::endl;
        return;
    }

    // 5. 加载资源（纹理、缓冲）
    TextureProvider* textureProvider = createStbProvider(engine);
    ResourceConfiguration resourceConfig {
        .engine = engine,
        .gltfPath = path,
        .normalizeSkinningWeights = true,
    };
    ResourceLoader* resourceLoader = new ResourceLoader(resourceConfig);
    resourceLoader->addTextureProvider("image/png", textureProvider);
    resourceLoader->addTextureProvider("image/jpeg", textureProvider);

    if (!resourceLoader->asyncBeginLoad(asset)) {
        slog.e << "Failed to load resources" << io::endl;
        return;
    }

    // 等待资源加载完成
    while (!resourceLoader->asyncUpdateLoad()) {
        // 渲染循环...
    }

    // 6. 释放 glTF 解析数据（保留 Filament 对象）
    asset->releaseSourceData();

    // 7. 添加到场景
    Scene* scene = engine->createScene();
    scene->addEntities(asset->getEntities(), asset->getEntityCount());

    // 8. 渲染...

    // 9. 清理
    scene->removeEntities(asset->getEntities(), asset->getEntityCount());
    loader->destroyAsset(asset);
    materials->destroyMaterials();
    delete materials;
    delete textureProvider;
    delete resourceLoader;
    AssetLoader::destroy(&loader);
    Engine::destroy(&engine);
}
```

---

## 核心技术栈

### 1. cgltf

**cgltf** 是单头文件的 glTF 2.0 解析库，用于读取 glTF JSON 和 GLB 文件。

```cpp
#include <cgltf.h>

// 解析 glTF
cgltf_options options = {};
cgltf_data* data = nullptr;
cgltf_result result = cgltf_parse_file(&options, path, &data);

if (result == cgltf_result_success) {
    // 加载二进制数据（buffers）
    cgltf_load_buffers(&options, data, path);

    // 访问材质
    for (size_t i = 0; i < data->materials_count; ++i) {
        cgltf_material* mat = &data->materials[i];

        // 读取 PBR 参数
        cgltf_pbr_metallic_roughness* pbr = &mat->pbr_metallic_roughness;
        float* baseColor = pbr->base_color_factor;  // RGBA
        float metallic = pbr->metallic_factor;
        float roughness = pbr->roughness_factor;

        // 检查纹理
        if (pbr->base_color_texture.texture) {
            cgltf_texture* tex = pbr->base_color_texture.texture;
            // ...
        }
    }

    // 释放
    cgltf_free(data);
}
```

**特点**:
- 单头文件，易于集成
- 零依赖
- 支持 glTF 2.0 核心规范和扩展
- 解析速度快

### 2. filamat

**filamat** 是 Filament 的材质编译库，用于运行时生成材质。

```cpp
#include <filamat/MaterialBuilder.h>

using namespace filamat;

Package package = MaterialBuilder()
    .name("my_material")
    .shading(MaterialBuilder::Shading::LIT)
    .blending(MaterialBuilder::BlendingMode::OPAQUE)
    .parameter("baseColorFactor", MaterialBuilder::Type::FLOAT4)
    .parameter("baseColorMap", MaterialBuilder::SamplerType::SAMPLER_2D)
    .material(R"(
        void material(inout MaterialInputs material) {
            prepareMaterial(material);
            vec4 baseColor = texture(materialParams_baseColorMap, getUV0());
            baseColor *= materialParams.baseColorFactor;
            material.baseColor = baseColor;
        }
    )")
    .build(engine->getJobSystem());

// 创建材质
Material* material = Material::Builder()
    .package(package.getData(), package.getSize())
    .build(*engine);
```

**特点**:
- 运行时材质编译
- 支持所有 Filament 材质特性
- 可选的优化级别
- 跨平台（生成所有后端的 shader）

### 3. MaterialBuilder

**MaterialBuilder** 是 filamat 的 API，用于声明式地构建材质。

```cpp
MaterialBuilder builder;

// 基础配置
builder.name("pbr_material");
builder.shading(Shading::LIT);
builder.blending(BlendingMode::OPAQUE);
builder.doubleSided(true);

// 参数声明
builder.parameter("baseColorFactor", Type::FLOAT4);
builder.parameter("metallicFactor", Type::FLOAT);
builder.parameter("roughnessFactor", Type::FLOAT);
builder.parameter("baseColorMap", SamplerType::SAMPLER_2D);
builder.parameter("normalMap", SamplerType::SAMPLER_2D);

// Shader 代码
builder.material("void material(...) { ... }");

// 编译
Package pkg = builder.build(jobSystem);
```

### 4. robin_map (tsl::robin_map)

**robin_map** 是基于 robin hood hashing 的高性能哈希表，用于材质缓存。

```cpp
#include <tsl/robin_map.h>

// JitShaderProvider 中的使用
tsl::robin_map<uint32_t, Material*> mMaterials;

// 插入
uint32_t hash = hash_value(materialKey);
mMaterials[hash] = material;

// 查找
auto iter = mMaterials.find(hash);
if (iter != mMaterials.end()) {
    Material* material = iter.value();
    // 缓存命中
}
```

**特点**:
- 比 std::unordered_map 更快
- 更好的缓存局部性
- 内存占用更小

### 5. MaterialInstanceCache

**MaterialInstanceCache** 缓存材质实例，避免重复创建。

```cpp
// libs/gltfio/src/AssetLoader.cpp

class MaterialInstanceCache {
public:
    struct Entry {
        MaterialInstance* instance = nullptr;
        UvMap uvmap;
    };

    Entry* getEntry(const cgltf_material** key, bool vertexColor) {
        uint64_t hash = reinterpret_cast<uintptr_t>(*key);
        if (vertexColor) {
            hash ^= 1;  // 顶点颜色作为额外的缓存维度
        }
        return &mCache[hash];
    }

private:
    std::unordered_map<uint64_t, Entry> mCache;
};
```

**缓存策略**:
- Key: `cgltf_material*` 指针 + 顶点颜色标志
- Value: `MaterialInstance*` + `UvMap`
- 同一个 glTF 材质在多个 primitive 间共享实例

---

## 性能优化策略

### 1. 材质缓存

**问题**: 相同特征的材质被重复创建，浪费时间和内存。

**解决**: 使用 MaterialKey 哈希缓存。

```cpp
// JitShaderProvider 缓存
uint32_t hash = hash_value(materialKey);
auto iter = mMaterials.find(hash);
if (iter != mMaterials.end()) {
    return iter.value();  // 缓存命中，直接返回
}

// 缓存未命中，创建新材质
Material* material = createMaterial(materialKey);
mMaterials[hash] = material;
return material;
```

**效果**:
- 典型场景中，材质创建次数减少 80-90%
- 内存占用显著降低

### 2. 材质实例复用

**问题**: 同一个 glTF 材质在多个 mesh primitive 中使用，每次都创建新实例。

**解决**: MaterialInstanceCache。

```cpp
// 第一次使用材质
MaterialInstance* mi = createMaterialInstance(gltfMat, ...);
cache[gltfMat] = mi;

// 后续使用
MaterialInstance* mi = cache[gltfMat];  // 复用
```

**注意**:
- 如果不同 primitive 使用不同的 UV 集或顶点颜色，需要创建不同的实例
- 缓存 Key 需要包含这些差异

### 3. UV 映射优化

**问题**: 不必要的 UV 集降级警告和性能损失。

**建议**:
- 在 3D 建模软件中限制 UV 集数量（≤2）
- 合并使用相同 UV 的纹理
- 优先使用 UV0

**示例**:

```
优化前:
  baseColor:  UV0
  normal:     UV0
  metallic:   UV1
  occlusion:  UV2  ← 降级！

优化后:
  baseColor:  UV0
  normal:     UV0
  metallic:   UV1
  occlusion:  UV1  ← 合并到 UV1
```

### 4. 纹理延迟加载

**问题**: 所有纹理同时加载，阻塞主线程。

**解决**: gltfio 使用延迟绑定机制。

```cpp
// AssetLoader::createMaterialInstance 中
// 不立即加载纹理，只记录绑定信息
fAsset->addTextureBinding(mi, "baseColorMap", texture, flags);

// ResourceLoader 异步加载纹理
resourceLoader->asyncBeginLoad(asset);

// 纹理加载完成后，回调设置参数
asset->applyTextureBinding(textureIndex, textureSlot);
```

**效果**:
- glTF 解析和材质创建不阻塞
- 纹理在后台加载
- 渐进式显示（依赖图管理）

### 5. 选择合适的 MaterialProvider

**JitShaderProvider**:
- 适用于桌面平台
- 材质种类多
- 首次加载可以等待

**UbershaderProvider**:
- 适用于移动平台和 WebGL
- 材质种类少或标准
- 需要快速启动

**混合策略**:
```cpp
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    auto materials = createUbershaderProvider(engine, ...);
#else
    auto materials = createJitShaderProvider(engine);
#endif
```

---

## 最佳实践

### 1. glTF 资源准备

**UV 集优化**:
```
✅ 推荐: 最多使用 2 组 UV
✅ 推荐: 相同用途的纹理共享 UV（如 baseColor + normal 都用 UV0）
❌ 避免: 每个纹理使用不同的 UV
```

**纹理共享**:
```
✅ 推荐: 多个材质共享纹理
✅ 推荐: 使用纹理图集（texture atlas）
❌ 避免: 大量小纹理
```

**材质复用**:
```
✅ 推荐: 相似物体使用相同材质
✅ 推荐: 通过材质实例参数调整外观
❌ 避免: 轻微差异就创建新材质
```

### 2. MaterialProvider 选择

**开发阶段**:
```cpp
// 使用 JIT，方便调试和迭代
auto materials = createJitShaderProvider(engine);
```

**发布阶段（移动平台）**:
```cpp
// 使用 Ubershader，快速加载
#include "gltf_ubershaders.h"
auto materials = createUbershaderProvider(engine,
    GLTF_UBERSHADERS_DATA, GLTF_UBERSHADERS_SIZE);
```

### 3. 异步加载模式

```cpp
// 主线程：解析和创建材质（快速）
FilamentAsset* asset = loader->createAsset(data, size);

// 后台：加载纹理和缓冲（慢速）
resourceLoader->asyncBeginLoad(asset);

// 主线程：渲染循环
while (rendering) {
    // 定期检查加载进度
    float progress = resourceLoader->asyncGetLoadProgress();

    // 更新（上传 GPU 数据）
    resourceLoader->asyncUpdateLoad();

    // 渲染
    renderer->render(view);
}
```

### 4. 错误处理

```cpp
FilamentAsset* asset = loader->createAsset(data, size);
if (!asset) {
    slog.e << "Failed to parse glTF" << io::endl;
    return;
}

// 检查材质创建失败
for (size_t i = 0; i < asset->getMaterialInstanceCount(); ++i) {
    MaterialInstance* mi = asset->getMaterialInstances()[i];
    if (!mi) {
        slog.e << "Material " << i << " creation failed" << io::endl;
    }
}

// 检查资源加载
if (!resourceLoader->asyncBeginLoad(asset)) {
    slog.e << "Resource loading failed" << io::endl;
}
```

### 5. 内存管理

```cpp
// 加载完成后释放解析数据（保留 Filament 对象）
asset->releaseSourceData();

// 销毁顺序
scene->removeEntities(asset->getEntities(), asset->getEntityCount());
loader->destroyAsset(asset);      // 销毁资源和实例
materials->destroyMaterials();    // 销毁所有材质
delete materials;                 // 销毁 provider
AssetLoader::destroy(&loader);
```

---

## 调试技巧

### 1. 启用诊断着色

```cpp
AssetLoader* loader = AssetLoader::create(config);
loader->enableDiagnostics(true);  // 启用调试着色

// 所有新加载的材质会显示调试信息:
// - 绿色: 法线方向
// - 红色: UV0 坐标
// - 蓝色: UV1 坐标
// - 等等
```

### 2. 检查材质特征

```cpp
// 在 JitShaderProvider::createMaterial 中添加日志
slog.i << "Creating material: " << label << io::endl;
slog.i << "  doubleSided: " << config.doubleSided << io::endl;
slog.i << "  hasBaseColorTexture: " << config.hasBaseColorTexture << io::endl;
slog.i << "  hasNormalTexture: " << config.hasNormalTexture << io::endl;
slog.i << "  alphaMode: " << (int)config.alphaMode << io::endl;
// ...
```

### 3. UV 映射检查

```cpp
// 在 constrainMaterial 后检查 uvmap
slog.i << "UvMap for material: " << io::endl;
for (int i = 0; i < 8; ++i) {
    if (uvmap->map[i] != 0 || i == 0) {
        slog.i << "  glTF UV" << i << " → Filament UV"
               << (int)uvmap->map[i] << io::endl;
    }
}
```

### 4. 生成的 Shader 代码

```cpp
// JitShaderProvider 中
std::string shaderCode = generateShaderCode(config, uvmap);
slog.i << "Generated shader:\n" << shaderCode << io::endl;

// 也可以使用 matc 的 -E 选项打印生成的 shader
```

### 5. 材质缓存统计

```cpp
// 在 JitShaderProvider 析构函数中
~JitShaderProvider() {
    slog.i << "Material cache statistics:" << io::endl;
    slog.i << "  Total materials created: " << mMaterials.size() << io::endl;
    slog.i << "  Cache hit rate: " << (mCacheHits * 100.0 / mTotalQueries)
           << "%" << io::endl;
}
```

---

## 总结

gltfio 的材质加载系统通过以下机制实现高效的 glTF 到 Filament 的转换：

### 核心组件

1. **MaterialProvider**: 抽象材质创建接口
2. **MaterialKey**: 材质特征哈希和缓存
3. **JitShaderProvider**: 运行时生成（桌面平台）
4. **UbershaderProvider**: 预编译材质（移动平台）
5. **UV 映射约束**: 处理 Filament 的 2 UV 限制

### 技术栈

- **cgltf**: glTF 2.0 解析
- **filamat**: 运行时材质编译
- **MaterialBuilder**: 材质构建 API
- **robin_map**: 高性能缓存

### 优化策略

- ✅ 材质缓存（减少重复创建）
- ✅ 材质实例复用（共享材质）
- ✅ UV 映射优化（避免降级）
- ✅ 纹理延迟加载（异步加载）
- ✅ 平台适配（选择合适的 provider）

通过理解这些机制，你可以：
- 高效加载 glTF 资源
- 针对不同平台优化性能
- 处理复杂的材质特性
- 调试材质加载问题
