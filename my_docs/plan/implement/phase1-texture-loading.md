# Phase 1 - 纹理加载机制实现

**优先级**: P0-1（最高优先级）
**预计工作量**: 3-5 个工作日
**依赖**: 无
**目标**: 使 gltfio_ext 能够正确显示纹理，达到 gltfio 的基础可用性

---

## 1. 问题分析

### 1.1 当前状态

**问题表现**:
- 加载的模型完全黑色，无法显示纹理
- 材质创建正确，但纹理始终为 nullptr
- 用户无法手动加载和绑定纹理

**根本原因** (`libs/gltfio_ext/src/MeshAsset.cpp:465-491`):

```cpp
// 当前实现
MaterialKey matkey = getMaterialKey(prim->material, &uvmap, hasVertexColor);
MaterialInstance* mi = mMaterialProvider->createMaterialInstance(&matkey, &uvmap, name);

// ❌ 问题：MaterialKey 告诉 shader 需要纹理（hasBaseColorTexture = true）
//         但实际从未加载和绑定纹理
//         shader 期望纹理，但收到 nullptr → 黑色模型
```

**与 gltfio 的差异**:

| 功能 | gltfio | gltfio_ext（当前） |
|------|--------|-------------------|
| 纹理加载 | ✅ ResourceLoader 自动加载 | ❌ 完全缺失 |
| 纹理绑定 | ✅ ResourceLoader.loadResources() | ❌ 无 API |
| 用户控制 | ❌ 无法自定义加载策略 | - |

### 1.2 为何 gltfio_ext 不能直接使用 ResourceLoader

**API 不兼容**:
```cpp
// gltfio 的 ResourceLoader 需要 FilamentAsset
ResourceLoader loader;
loader.loadResources(FilamentAsset* asset);  // ❌ gltfio_ext 没有 FilamentAsset

// gltfio_ext 使用独立的 Asset 类型
MeshAsset* mesh = loader->loadMesh(...);  // ✅ 独立的 MeshAsset
```

**设计理念不同**:
- **gltfio**: 整体加载（AssetLoader + ResourceLoader 配合）
- **gltfio_ext**: 模块化加载（Skeleton/Mesh/Animation 分离）

因此需要为 gltfio_ext 设计独立的纹理加载机制。

---

## 2. 设计目标

### 2.1 功能目标

1. **基础可用性**: mesh.glb 的纹理可以正确显示
2. **用户可控**: 用户可以选择纹理加载策略（同步/异步、从文件/从内存）
3. **简洁 API**: 学习成本低，符合 Filament 风格
4. **兼容性**: 支持 embedded 和 external 纹理

### 2.2 非目标（留待后续）

- ❌ 自动纹理加载（需要集成 TextureProvider）
- ❌ KTX2 压缩纹理支持
- ❌ 纹理缓存和复用（P2 优化）

---

## 3. 技术方案

### 3.1 方案选择

**方案 A: 集成 ResourceLoader**
- 优点: 复用成熟代码
- 缺点: API 不兼容，需要大量适配工作

**方案 B: 提供纹理绑定 API（✅ 选择）**
- 优点: 简单直接，用户可控，灵活性高
- 缺点: 需要用户手动加载纹理

**选择理由**: 方案 B 更符合 gltfio_ext 的模块化设计理念，且实现成本低。

### 3.2 API 设计

#### 3.2.1 核心数据结构

```cpp
// MeshAsset.h

/**
 * 纹理信息结构
 * 描述一个 primitive 需要的纹理
 */
struct TextureInfo {
    size_t primitiveIndex;        // 所属 primitive 索引
    const char* slot;             // 纹理槽位名称（如 "baseColorMap"）
    const char* uri;              // 纹理 URI（如果是外部文件）
    const uint8_t* data;          // Embedded 数据指针（如果嵌入）
    size_t dataSize;              // Embedded 数据大小
    const char* mimeType;         // MIME 类型（"image/png", "image/jpeg"）

    // Sampler 参数
    SamplerWrapMode wrapS;
    SamplerWrapMode wrapT;
    SamplerMinFilter minFilter;
    SamplerMagFilter magFilter;
};

/**
 * Sampler 参数枚举（与 glTF 对齐）
 */
enum class SamplerWrapMode {
    CLAMP_TO_EDGE = 33071,
    MIRRORED_REPEAT = 33648,
    REPEAT = 10497
};

enum class SamplerMinFilter {
    NEAREST = 9728,
    LINEAR = 9729,
    NEAREST_MIPMAP_NEAREST = 9984,
    LINEAR_MIPMAP_NEAREST = 9985,
    NEAREST_MIPMAP_LINEAR = 9986,
    LINEAR_MIPMAP_LINEAR = 9987
};

enum class SamplerMagFilter {
    NEAREST = 9728,
    LINEAR = 9729
};
```

#### 3.2.2 MeshAsset API 扩展

```cpp
// MeshAsset.h

class MeshAsset {
public:
    // ... 现有 API ...

    /**
     * 获取所有需要的纹理信息
     * @return 纹理信息列表
     *
     * 使用时机：调用 uploadResources() 之前
     */
    std::vector<TextureInfo> getRequiredTextures() const;

    /**
     * 绑定纹理到指定 primitive 的指定槽位
     * @param primitiveIndex primitive 索引
     * @param slot 槽位名称（"baseColorMap", "normalMap", "metallicRoughnessMap", "occlusionMap", "emissiveMap"）
     * @param texture Filament Texture 对象
     * @param sampler Filament TextureSampler 对象
     *
     * 使用时机：在 uploadResources() 之前调用
     */
    void bindTexture(size_t primitiveIndex, const char* slot,
                     filament::Texture* texture,
                     const filament::TextureSampler& sampler);

    /**
     * 辅助函数：从 TextureInfo 创建 Filament TextureSampler
     * @param info 纹理信息
     * @return Filament TextureSampler
     */
    static filament::TextureSampler createSampler(const TextureInfo& info);
};
```

---

## 4. 实现步骤

### 4.1 步骤 1: 扩展 MeshAsset 数据结构

**修改文件**: `libs/gltfio_ext/include/gltfio/MeshAsset.h`

```cpp
class MeshAsset {
private:
    // 新增：纹理信息缓存
    struct TextureSlot {
        const char* slotName;
        cgltf_texture* gltfTexture;
        cgltf_texture_view textureView;
    };

    struct PrimitiveTextureInfo {
        size_t primitiveIndex;
        std::vector<TextureSlot> slots;
    };

    std::vector<PrimitiveTextureInfo> mTextureInfos;
};
```

**工作量**: 0.5 天

### 4.2 步骤 2: 实现 getRequiredTextures()

**修改文件**: `libs/gltfio_ext/src/MeshAsset.cpp`

**实现逻辑**:
```cpp
std::vector<TextureInfo> MeshAsset::getRequiredTextures() const {
    std::vector<TextureInfo> result;

    // 遍历所有 primitives
    for (const auto& primTexInfo : mTextureInfos) {
        for (const auto& slot : primTexInfo.slots) {
            TextureInfo info = {};
            info.primitiveIndex = primTexInfo.primitiveIndex;
            info.slot = slot.slotName;

            cgltf_texture* tex = slot.gltfTexture;
            cgltf_image* img = tex->image;

            // 1. 提取 URI 或 embedded 数据
            if (img->uri) {
                info.uri = img->uri;
                info.data = nullptr;
                info.dataSize = 0;
            } else {
                info.uri = nullptr;
                info.data = (const uint8_t*)img->buffer_view->buffer->data
                          + img->buffer_view->offset;
                info.dataSize = img->buffer_view->size;
            }

            // 2. MIME 类型
            info.mimeType = img->mime_type ? img->mime_type : "image/png";

            // 3. Sampler 参数
            cgltf_sampler* sampler = tex->sampler;
            if (sampler) {
                info.wrapS = (SamplerWrapMode)sampler->wrap_s;
                info.wrapT = (SamplerWrapMode)sampler->wrap_t;
                info.minFilter = (SamplerMinFilter)sampler->min_filter;
                info.magFilter = (SamplerMagFilter)sampler->mag_filter;
            } else {
                // 默认值
                info.wrapS = SamplerWrapMode::REPEAT;
                info.wrapT = SamplerWrapMode::REPEAT;
                info.minFilter = SamplerMinFilter::LINEAR;
                info.magFilter = SamplerMagFilter::LINEAR;
            }

            result.push_back(info);
        }
    }

    return result;
}
```

**工作量**: 1 天

### 4.3 步骤 3: 实现 bindTexture()

**修改文件**: `libs/gltfio_ext/src/MeshAsset.cpp`

**实现逻辑**:
```cpp
void MeshAsset::bindTexture(size_t primitiveIndex, const char* slot,
                           Texture* texture, const TextureSampler& sampler) {
    ASSERT_PRECONDITION(primitiveIndex < mPrimitives.size(),
                        "Invalid primitive index");

    // 获取 MaterialInstance
    MaterialInstance* mi = mPrimitives[primitiveIndex].materialInstance;
    ASSERT_PRECONDITION(mi, "MaterialInstance is null");

    // 绑定纹理到对应槽位
    mi->setParameter(slot, texture, sampler);
}
```

**工作量**: 0.5 天

### 4.4 步骤 4: 实现 createSampler() 辅助函数

**修改文件**: `libs/gltfio_ext/src/MeshAsset.cpp`

**实现逻辑**:
```cpp
TextureSampler MeshAsset::createSampler(const TextureInfo& info) {
    TextureSampler sampler;

    // Wrap 模式
    switch (info.wrapS) {
        case SamplerWrapMode::CLAMP_TO_EDGE:
            sampler.setWrapModeS(TextureSampler::WrapMode::CLAMP_TO_EDGE);
            break;
        case SamplerWrapMode::MIRRORED_REPEAT:
            sampler.setWrapModeS(TextureSampler::WrapMode::MIRRORED_REPEAT);
            break;
        case SamplerWrapMode::REPEAT:
            sampler.setWrapModeS(TextureSampler::WrapMode::REPEAT);
            break;
    }

    // 类似处理 wrapT, minFilter, magFilter...

    return sampler;
}
```

**工作量**: 0.5 天

### 4.5 步骤 5: 修改 loadMesh() 收集纹理信息

**修改文件**: `libs/gltfio_ext/src/AssetLoaderExt.cpp`

**实现逻辑**:
```cpp
MeshAsset* AssetLoaderExtImpl::loadMesh(const uint8_t* bytes, uint32_t nbytes) {
    // ... 现有解析逻辑 ...

    // 新增：收集每个 primitive 的纹理信息
    for (size_t i = 0; i < mesh->primitives_count; i++) {
        cgltf_primitive* prim = &mesh->primitives[i];
        cgltf_material* mat = prim->material;

        if (!mat) continue;

        MeshAsset::PrimitiveTextureInfo primTexInfo;
        primTexInfo.primitiveIndex = i;

        // Base Color
        if (mat->pbr_metallic_roughness.base_color_texture.texture) {
            primTexInfo.slots.push_back({
                "baseColorMap",
                mat->pbr_metallic_roughness.base_color_texture.texture,
                mat->pbr_metallic_roughness.base_color_texture
            });
        }

        // Normal
        if (mat->normal_texture.texture) {
            primTexInfo.slots.push_back({
                "normalMap",
                mat->normal_texture.texture,
                mat->normal_texture
            });
        }

        // Metallic Roughness
        if (mat->pbr_metallic_roughness.metallic_roughness_texture.texture) {
            primTexInfo.slots.push_back({
                "metallicRoughnessMap",
                mat->pbr_metallic_roughness.metallic_roughness_texture.texture,
                mat->pbr_metallic_roughness.metallic_roughness_texture
            });
        }

        // Occlusion
        if (mat->occlusion_texture.texture) {
            primTexInfo.slots.push_back({
                "occlusionMap",
                mat->occlusion_texture.texture,
                mat->occlusion_texture
            });
        }

        // Emissive
        if (mat->emissive_texture.texture) {
            primTexInfo.slots.push_back({
                "emissiveMap",
                mat->emissive_texture.texture,
                mat->emissive_texture
            });
        }

        if (!primTexInfo.slots.empty()) {
            meshAsset->mTextureInfos.push_back(primTexInfo);
        }
    }

    return meshAsset;
}
```

**工作量**: 1 天

### 4.6 步骤 6: 编写单元测试

**新建文件**: `libs/gltfio_ext/tests/test_texture_loading.cpp`

**测试用例**:
```cpp
TEST(TextureLoading, GetRequiredTextures) {
    // 1. 加载带纹理的 mesh.glb
    MeshAsset* mesh = loader->loadMesh(meshData, meshSize);

    // 2. 获取纹理列表
    auto textures = mesh->getRequiredTextures();

    // 3. 验证
    EXPECT_GT(textures.size(), 0);
    EXPECT_STREQ(textures[0].slot, "baseColorMap");
}

TEST(TextureLoading, BindTextureWorkflow) {
    // 1. 加载 mesh
    MeshAsset* mesh = loader->loadMesh(meshData, meshSize);

    // 2. 加载纹理
    auto textures = mesh->getRequiredTextures();
    for (const auto& texInfo : textures) {
        Texture* tex = loadTextureHelper(texInfo);
        TextureSampler sampler = MeshAsset::createSampler(texInfo);
        mesh->bindTexture(texInfo.primitiveIndex, texInfo.slot, tex, sampler);
    }

    // 3. 上传资源
    mesh->uploadResources();

    // 4. 验证纹理已绑定（通过 MaterialInstance）
    // ...
}
```

**工作量**: 1 天

---

## 5. 用户使用示例

### 5.1 基础使用（从文件加载纹理）

```cpp
#include <gltfio/AssetLoaderExt.h>
#include <stb_image.h>  // 用户选择的图像加载库

using namespace filament;
using namespace gltfio;

// 1. 加载 mesh
AssetLoaderExt* loader = AssetLoaderExt::create(engine, matProvider);
MeshAsset* mesh = loader->loadMesh(meshData, meshSize);

// 2. 获取需要的纹理列表
auto textures = mesh->getRequiredTextures();

// 3. 加载每个纹理
for (const auto& texInfo : textures) {
    Texture* texture = nullptr;

    if (texInfo.uri) {
        // 外部文件：从 URI 加载
        std::string path = std::string("assets/") + texInfo.uri;
        int w, h, channels;
        uint8_t* data = stbi_load(path.c_str(), &w, &h, &channels, 4);

        texture = Texture::Builder()
            .width(w)
            .height(h)
            .levels(1)
            .format(Texture::InternalFormat::RGBA8)
            .build(*engine);

        Texture::PixelBufferDescriptor buffer(data, w * h * 4,
            Texture::Format::RGBA, Texture::Type::UBYTE,
            [](void* buf, size_t, void*) { stbi_image_free(buf); });

        texture->setImage(*engine, 0, std::move(buffer));
    } else {
        // Embedded 数据：直接解码
        int w, h, channels;
        uint8_t* data = stbi_load_from_memory(texInfo.data, texInfo.dataSize,
                                              &w, &h, &channels, 4);

        texture = Texture::Builder()
            .width(w)
            .height(h)
            .levels(1)
            .format(Texture::InternalFormat::RGBA8)
            .build(*engine);

        Texture::PixelBufferDescriptor buffer(data, w * h * 4,
            Texture::Format::RGBA, Texture::Type::UBYTE,
            [](void* buf, size_t, void*) { stbi_image_free(buf); });

        texture->setImage(*engine, 0, std::move(buffer));
    }

    // 4. 绑定纹理
    TextureSampler sampler = MeshAsset::createSampler(texInfo);
    mesh->bindTexture(texInfo.primitiveIndex, texInfo.slot, texture, sampler);
}

// 5. 上传资源
mesh->uploadResources();

// 6. 添加到场景
scene->addEntity(mesh->getRenderableEntity());
```

### 5.2 高级使用（异步加载）

```cpp
// 用户可以实现自己的异步纹理加载器
class MyTextureLoader {
public:
    void loadAsync(const TextureInfo& info, std::function<void(Texture*)> callback) {
        std::thread([=]() {
            Texture* tex = loadTextureSync(info);
            callback(tex);
        }).detach();
    }
};

// 使用
MyTextureLoader texLoader;
std::atomic<int> loadedCount = 0;
auto textures = mesh->getRequiredTextures();

for (const auto& texInfo : textures) {
    texLoader.loadAsync(texInfo, [&](Texture* tex) {
        TextureSampler sampler = MeshAsset::createSampler(texInfo);
        mesh->bindTexture(texInfo.primitiveIndex, texInfo.slot, tex, sampler);

        if (++loadedCount == textures.size()) {
            // 所有纹理加载完成
            mesh->uploadResources();
        }
    });
}
```

---

## 6. 测试计划

### 6.1 单元测试

| 测试用例 | 测试内容 | 验收标准 |
|---------|---------|---------|
| `GetRequiredTextures_ExternalUri` | 外部 URI 纹理 | URI 正确，data 为 null |
| `GetRequiredTextures_Embedded` | 嵌入式纹理 | URI 为 null，data 非空 |
| `GetRequiredTextures_MultiplePrimitives` | 多 primitive | 返回所有纹理 |
| `BindTexture_Valid` | 正常绑定 | MaterialInstance 参数已设置 |
| `BindTexture_InvalidIndex` | 无效索引 | 断言失败 |
| `CreateSampler_AllWrapModes` | 所有 Wrap 模式 | 正确转换 |

### 6.2 集成测试

**测试场景 1: 简单模型**
- 文件: `Khronos glTF Sample Models/BoxTextured`
- 验证: Base Color 纹理正确显示

**测试场景 2: 多纹理模型**
- 文件: 自定义模型（Base Color + Normal + Metallic Roughness）
- 验证: 所有纹理正确显示

**测试场景 3: Embedded 纹理**
- 文件: GLB 格式（嵌入式纹理）
- 验证: Embedded 数据正确解码

**测试场景 4: 分离加载场景**
- 文件: skeleton.glb + mesh.glb（mesh 有纹理）
- 验证: 换装后纹理正确显示

### 6.3 性能测试

**指标**:
- 纹理绑定耗时 < 1ms（per texture）
- 内存泄漏检测（Valgrind）

---

## 7. 验收标准

### 7.1 功能验收

- ✅ `getRequiredTextures()` 返回正确的纹理列表
- ✅ `bindTexture()` 可以成功绑定纹理
- ✅ `createSampler()` 正确转换 glTF sampler 参数
- ✅ BoxTextured 示例可以正确显示纹理
- ✅ 支持 embedded 和 external 纹理
- ✅ 支持多 primitive 场景

### 7.2 质量验收

- ✅ 所有单元测试通过
- ✅ 所有集成测试通过
- ✅ 无内存泄漏
- ✅ API 文档完善（Doxygen 注释）

### 7.3 用户体验验收

- ✅ 示例代码清晰易懂
- ✅ 错误信息友好（如：绑定纹理时 primitiveIndex 越界）

---

## 8. 风险和缓解

### 8.1 风险 1: cgltf 纹理信息提取错误

**影响**: 纹理无法正确显示
**概率**: 低
**缓解**: 参考 gltfio 原生实现，单元测试覆盖

### 8.2 风险 2: TextureSampler 参数转换错误

**影响**: 纹理采样错误（拉伸/平铺问题）
**概率**: 低
**缓解**: 对比 glTF 规范和 Filament 文档

### 8.3 风险 3: 用户学习成本高

**影响**: API 使用复杂，用户体验差
**概率**: 中
**缓解**: 提供完整示例代码和文档

---

## 9. 后续优化（P2）

1. **自动纹理加载**: 集成 TextureProvider，提供 `mesh->loadTexturesAsync()` API
2. **纹理缓存**: 同一纹理在多个 primitive 中复用
3. **KTX2 支持**: 支持压缩纹理格式
4. **Mipmap 生成**: 自动生成 mipmap

---

## 10. 里程碑

| 里程碑 | 完成标准 | 预计时间 |
|--------|---------|---------|
| **M1: API 设计完成** | 头文件修改完成 | 第 1 天 |
| **M2: 核心实现完成** | getRequiredTextures() + bindTexture() 完成 | 第 3 天 |
| **M3: 测试完成** | 单元测试 + 集成测试通过 | 第 5 天 |

---

**下一步**: 开始实现（从步骤 1 开始）
