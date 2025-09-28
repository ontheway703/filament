# glTF 加载机制详解

本文档深入分析 Filament 中 glTF 2.0 资产的加载机制，从文件格式解析到资源创建的完整流程。

## 目录

1. [glTF 格式概述](#gltf-格式概述)
2. [cgltf 解析库](#cgltf-解析库)
3. [AssetLoader 架构](#assetloader-架构)
4. [ResourceLoader 系统](#resourceloader-系统)
5. [资产实例化](#资产实例化)
6. [动画数据解析](#动画数据解析)
7. [内存管理和优化](#内存管理和优化)

## glTF 格式概述

### glTF 2.0 规范要点

glTF (Graphics Language Transmission Format) 是 Khronos Group 制定的 3D 场景传输格式，被称为"3D 界的 JPEG"。

#### 格式变体
1. **glTF (.gltf)**: JSON + 外部二进制文件和图像
2. **GLB (.glb)**: 自包含的二进制格式
3. **glTF + 嵌入**: JSON 中嵌入 base64 编码的二进制数据

#### 核心概念

```json
{
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [
    {
      "mesh": 0,
      "skin": 0,
      "translation": [0, 0, 0],
      "rotation": [0, 0, 0, 1],
      "scale": [1, 1, 1]
    }
  ],
  "meshes": [
    {
      "primitives": [
        {
          "attributes": {"POSITION": 0, "NORMAL": 1},
          "indices": 2,
          "material": 0
        }
      ]
    }
  ],
  "animations": [
    {
      "channels": [
        {
          "sampler": 0,
          "target": {"node": 0, "path": "translation"}
        }
      ],
      "samplers": [
        {
          "input": 3,   // 时间轴访问器
          "output": 4,  // 值访问器
          "interpolation": "LINEAR"
        }
      ]
    }
  ]
}
```

### 数据访问模式

glTF 使用访问器 (Accessor) 来描述二进制数据的布局：

```
Accessor → BufferView → Buffer → 实际数据

Accessor: {
  "bufferView": 0,
  "componentType": 5126,  // GL_FLOAT
  "count": 1024,          // 元素数量
  "type": "VEC3",         // 每个元素的类型
  "min": [-1.0, -1.0, -1.0],
  "max": [1.0, 1.0, 1.0]
}
```

## cgltf 解析库

### cgltf 库特点

Filament 使用 cgltf 库来解析 glTF 文件，该库的特点：
- **C 语言实现**: 轻量级，无外部依赖
- **零拷贝解析**: 直接在内存中操作数据
- **扩展支持**: 支持 glTF 官方扩展
- **验证功能**: 内置格式验证

### 主要数据结构

在 Filament 中，cgltf 的关键数据结构：

```cpp
// cgltf 库的主要结构体
struct cgltf_data {
    cgltf_scene* scenes;
    cgltf_size scenes_count;

    cgltf_node* nodes;
    cgltf_size nodes_count;

    cgltf_mesh* meshes;
    cgltf_size meshes_count;

    cgltf_animation* animations;
    cgltf_size animations_count;

    cgltf_skin* skins;
    cgltf_size skins_count;

    cgltf_buffer* buffers;
    cgltf_size buffers_count;

    cgltf_accessor* accessors;
    cgltf_size accessors_count;
};

struct cgltf_node {
    char* name;
    cgltf_node* parent;
    cgltf_node** children;
    cgltf_size children_count;

    cgltf_mesh* mesh;
    cgltf_skin* skin;
    cgltf_light* light;
    cgltf_camera* camera;

    cgltf_float translation[3];
    cgltf_float rotation[4];
    cgltf_float scale[3];
    cgltf_float matrix[16];
    cgltf_bool has_translation;
    cgltf_bool has_rotation;
    cgltf_bool has_scale;
    cgltf_bool has_matrix;
};
```

### 解析流程

```cpp
// libs/gltfio/src/AssetLoader.cpp 中的解析过程
cgltf_result cgltf_parse_file(const cgltf_options* options,
                             const char* path,
                             cgltf_data** out_data) {
    // 1. 读取文件
    FILE* file = fopen(path, "rb");

    // 2. 检测格式（JSON 或 GLB）
    uint32_t magic;
    fread(&magic, sizeof(magic), 1, file);

    if (magic == 0x46546C67) {  // "glTF" magic
        return parse_glb(file, options, out_data);
    } else {
        return parse_json(file, options, out_data);
    }
}

// GLB 格式解析
cgltf_result parse_glb(FILE* file, const cgltf_options* options,
                      cgltf_data** out_data) {
    // GLB 头部结构
    struct {
        uint32_t magic;      // 0x46546C67
        uint32_t version;    // 版本号
        uint32_t length;     // 总长度
    } header;

    // JSON chunk
    struct {
        uint32_t chunkLength;
        uint32_t chunkType;  // 0x4E4F534A ("JSON")
    } jsonChunk;

    // BIN chunk (可选)
    struct {
        uint32_t chunkLength;
        uint32_t chunkType;  // 0x004E4942 ("BIN\0")
    } binChunk;

    // 解析 JSON 部分
    char* jsonData = malloc(jsonChunk.chunkLength + 1);
    fread(jsonData, jsonChunk.chunkLength, 1, file);
    jsonData[jsonChunk.chunkLength] = '\0';

    // 解析二进制部分
    void* binData = malloc(binChunk.chunkLength);
    fread(binData, binChunk.chunkLength, 1, file);

    return cgltf_parse_json(options, jsonData, jsonChunk.chunkLength, out_data);
}
```

## AssetLoader 架构

### AssetLoader 类设计

AssetLoader 是 gltfio 库的核心类，负责将 glTF 数据转换为 Filament 资源：

```cpp
// libs/gltfio/include/gltfio/AssetLoader.h
class AssetLoader {
public:
    // 创建和销毁
    static AssetLoader* create(const AssetConfiguration& config);
    static void destroy(AssetLoader** loader);

    // 资产创建
    FilamentAsset* createAsset(const uint8_t* bytes, uint32_t nbytes);
    FilamentAsset* createInstancedAsset(const uint8_t* bytes, uint32_t numBytes,
                                       FilamentInstance** instances, size_t numInstances);

    // 实例管理
    FilamentInstance* createInstance(FilamentAsset* asset);

    // 资源清理
    void destroyAsset(const FilamentAsset* asset);
};

// 配置结构
struct AssetConfiguration {
    Engine* engine;                           // Filament 引擎
    MaterialProvider* materials;              // 材质提供者
    NameComponentManager* names = nullptr;    // 名称管理器
    EntityManager* entities = nullptr;        // 实体管理器
    char* defaultNodeName = nullptr;          // 默认节点名
};
```

### 内部实现 (FAssetLoader)

```cpp
// libs/gltfio/src/AssetLoader.cpp
class FAssetLoader : public AssetLoader {
    Engine* mEngine;
    MaterialProvider* mMaterialProvider;
    NameComponentManager* mNameManager;
    EntityManager* mEntityManager;
    NodeManager mNodeManager;

    // 缓存
    std::vector<const Material*> mMaterials;
    tsl::robin_map<const cgltf_material*, MaterialInstance*> mMaterialInstances;

public:
    FFilamentAsset* createAsset(const uint8_t* bytes, uint32_t byteCount) override;
    void destroyAsset(const FFilamentAsset* asset) override;

private:
    // 核心创建函数
    FFilamentAsset* createAssetFromCgltf(cgltf_data* data,
                                        FilamentInstance** instances,
                                        size_t numInstances);

    // 组件创建
    void createRenderables(const cgltf_data* data, FFilamentAsset* asset);
    void createMaterials(const cgltf_data* data);
    void createTextures(const cgltf_data* data, FFilamentAsset* asset);
    void createSkins(const cgltf_data* data, FFilamentAsset* asset);
    void createAnimations(const cgltf_data* data, FFilamentAsset* asset);
};
```

### 资产创建流程

```cpp
FFilamentAsset* FAssetLoader::createAssetFromCgltf(cgltf_data* data,
                                                  FilamentInstance** instances,
                                                  size_t numInstances) {
    SYSTRACE_CALL();

    // 1. 创建 FilamentAsset 对象
    FFilamentAsset* asset = new FFilamentAsset(mEngine, mNameManager,
                                              mEntityManager, &mNodeManager);
    asset->mSourceAsset = data;

    // 2. 验证数据
    if (!validateAsset(data)) {
        delete asset;
        return nullptr;
    }

    // 3. 创建材质
    createMaterials(data);

    // 4. 创建实例
    for (size_t i = 0; i < numInstances; ++i) {
        instances[i] = new FFilamentInstance(asset);
        asset->mInstances.push_back(instances[i]);
    }

    // 5. 构建节点层次结构
    buildNodeHierarchy(data, asset);

    // 6. 创建几何体和可渲染组件
    createRenderables(data, asset);

    // 7. 创建骨骼和蒙皮
    createSkins(data, asset);

    // 8. 创建动画数据
    createAnimations(data, asset);

    // 9. 创建光源和摄像机
    createLights(data, asset);
    createCameras(data, asset);

    // 10. 计算边界框
    updateBoundingBox(asset);

    return asset;
}
```

### 几何体创建详解

```cpp
void FAssetLoader::createRenderables(const cgltf_data* data, FFilamentAsset* asset) {
    // 为每个网格创建 Filament 几何体
    for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex) {
        const cgltf_mesh& srcMesh = data->meshes[meshIndex];

        // 预处理：计算所需的缓冲区大小
        size_t totalVertices = 0;
        size_t totalIndices = 0;
        size_t totalMorphTargets = 0;

        for (cgltf_size primIndex = 0; primIndex < srcMesh.primitives_count; ++primIndex) {
            const cgltf_primitive& srcPrim = srcMesh.primitives[primIndex];

            // 计算顶点数量
            if (srcPrim.attributes_count > 0) {
                const cgltf_accessor* accessor = srcPrim.attributes[0].data;
                totalVertices += accessor->count;
            }

            // 计算索引数量
            if (srcPrim.indices) {
                totalIndices += srcPrim.indices->count;
            }

            // 计算变形目标数量
            totalMorphTargets += srcPrim.targets_count;
        }

        // 创建顶点缓冲区
        VertexBuffer* vertexBuffer = createVertexBuffer(srcMesh, totalVertices);
        asset->mVertexBuffers.push_back(vertexBuffer);

        // 创建索引缓冲区
        if (totalIndices > 0) {
            IndexBuffer* indexBuffer = createIndexBuffer(srcMesh, totalIndices);
            asset->mIndexBuffers.push_back(indexBuffer);
        }

        // 创建变形目标缓冲区
        if (totalMorphTargets > 0) {
            MorphTargetBuffer* morphBuffer = createMorphTargetBuffer(srcMesh, totalMorphTargets);
            asset->mMorphTargetBuffers.push_back(morphBuffer);
        }

        // 为每个图元创建可渲染组件
        createRenderableForMesh(data, asset, meshIndex);
    }
}

VertexBuffer* FAssetLoader::createVertexBuffer(const cgltf_mesh& mesh, size_t vertexCount) {
    // 分析所需的顶点属性
    std::set<VertexAttribute> requiredAttributes;

    for (cgltf_size primIndex = 0; primIndex < mesh.primitives_count; ++primIndex) {
        const cgltf_primitive& prim = mesh.primitives[primIndex];

        for (cgltf_size attrIndex = 0; attrIndex < prim.attributes_count; ++attrIndex) {
            const cgltf_attribute& attr = prim.attributes[attrIndex];

            if (attr.type == cgltf_attribute_type_position) {
                requiredAttributes.insert(VertexAttribute::POSITION);
            } else if (attr.type == cgltf_attribute_type_normal) {
                requiredAttributes.insert(VertexAttribute::NORMAL);
            } else if (attr.type == cgltf_attribute_type_texcoord_0) {
                requiredAttributes.insert(VertexAttribute::UV0);
            } else if (attr.type == cgltf_attribute_type_texcoord_1) {
                requiredAttributes.insert(VertexAttribute::UV1);
            } else if (attr.type == cgltf_attribute_type_joints_0) {
                requiredAttributes.insert(VertexAttribute::BONE_INDICES);
            } else if (attr.type == cgltf_attribute_type_weights_0) {
                requiredAttributes.insert(VertexAttribute::BONE_WEIGHTS);
            }
        }
    }

    // 构建顶点缓冲区
    VertexBuffer::Builder builder;
    builder.vertexCount(vertexCount).bufferCount(1);

    for (const auto& attr : requiredAttributes) {
        switch (attr) {
            case VertexAttribute::POSITION:
                builder.attribute(attr, 0, VertexBuffer::AttributeType::FLOAT3, 0, 12);
                break;
            case VertexAttribute::NORMAL:
                builder.attribute(attr, 0, VertexBuffer::AttributeType::FLOAT3, 12, 12);
                break;
            case VertexAttribute::UV0:
                builder.attribute(attr, 0, VertexBuffer::AttributeType::FLOAT2, 24, 8);
                break;
            case VertexAttribute::BONE_INDICES:
                builder.attribute(attr, 0, VertexBuffer::AttributeType::USHORT4, 32, 8);
                break;
            case VertexAttribute::BONE_WEIGHTS:
                builder.attribute(attr, 0, VertexBuffer::AttributeType::FLOAT4, 40, 16);
                break;
        }
    }

    return builder.build(*mEngine);
}
```

## ResourceLoader 系统

### ResourceLoader 职责

ResourceLoader 负责加载 glTF 引用的外部资源（纹理、缓冲区数据）：

```cpp
// libs/gltfio/include/gltfio/ResourceLoader.h
class ResourceLoader {
public:
    // 构造配置
    struct Configuration {
        Engine* engine;
        std::string gltfPath;      // glTF 文件路径（用于解析相对路径）
        bool normalizeSkinningWeights = true;
        std::function<void(double)> progressCallback;
    };

    ResourceLoader(const Configuration& config);

    // 纹理提供者
    void addTextureProvider(const char* mimeType, TextureProvider* provider);

    // 资源加载
    void loadResources(FilamentAsset* asset);
    void asyncBeginLoad(FilamentAsset* asset);

    // 进度查询
    float asyncGetLoadProgress() const;
    void asyncUpdateLoad();
    bool asyncIsLoadComplete() const;
};
```

### 异步加载实现

```cpp
// libs/gltfio/src/ResourceLoader.cpp
class FResourceLoader : public ResourceLoader {
    struct AsyncContext {
        FilamentAsset* asset;
        std::vector<std::future<void>> pendingTextures;
        std::vector<std::future<void>> pendingBuffers;
        std::atomic<size_t> completedTasks{0};
        size_t totalTasks = 0;
        bool finished = false;
    };

    std::unique_ptr<AsyncContext> mAsyncContext;
    Engine* mEngine;
    std::string mGltfPath;
    std::unordered_map<std::string, TextureProvider*> mTextureProviders;

public:
    void asyncBeginLoad(FilamentAsset* asset) override {
        mAsyncContext = std::make_unique<AsyncContext>();
        mAsyncContext->asset = asset;

        // 收集所有需要加载的资源
        collectResources(asset);

        // 启动异步加载任务
        startTextureLoading();
        startBufferLoading();
    }

private:
    void startTextureLoading() {
        const FFilamentAsset* fasset = downcast(mAsyncContext->asset);
        const cgltf_data* data = fasset->mSourceAsset->hierarchy;

        for (cgltf_size i = 0; i < data->textures_count; ++i) {
            const cgltf_texture& srcTexture = data->textures[i];

            if (srcTexture.image && srcTexture.image->uri) {
                std::string uri = resolveUri(srcTexture.image->uri);

                // 创建异步加载任务
                auto future = std::async(std::launch::async, [this, i, uri]() {
                    loadTextureAsync(i, uri);
                });

                mAsyncContext->pendingTextures.push_back(std::move(future));
                ++mAsyncContext->totalTasks;
            }
        }
    }

    void loadTextureAsync(size_t textureIndex, const std::string& uri) {
        // 确定 MIME 类型
        std::string mimeType = getMimeTypeFromUri(uri);

        auto provider = mTextureProviders.find(mimeType);
        if (provider == mTextureProviders.end()) {
            LOGE("No texture provider for MIME type: %s", mimeType.c_str());
            return;
        }

        // 读取文件数据
        std::vector<uint8_t> data = readFile(uri);
        if (data.empty()) {
            LOGE("Failed to read texture file: %s", uri.c_str());
            return;
        }

        // 解码纹理
        Texture* texture = provider->second->load(mEngine, data.data(), data.size(), uri);

        if (texture) {
            // 线程安全地设置纹理
            std::lock_guard<std::mutex> lock(mTextureMutex);
            mLoadedTextures[textureIndex] = texture;
        }

        ++mAsyncContext->completedTasks;
    }

    float asyncGetLoadProgress() const override {
        if (!mAsyncContext || mAsyncContext->totalTasks == 0) {
            return 1.0f;
        }

        return static_cast<float>(mAsyncContext->completedTasks) / mAsyncContext->totalTasks;
    }
};
```

### 纹理提供者系统

```cpp
// libs/gltfio/include/gltfio/TextureProvider.h
class TextureProvider {
public:
    virtual ~TextureProvider() = default;

    // 从内存数据创建纹理
    virtual Texture* load(Engine* engine, const uint8_t* data, size_t size,
                         const std::string& uri) = 0;

    // 支持的 MIME 类型
    virtual std::vector<std::string> getSupportedMimeTypes() const = 0;
};

// STB 实现
class StbProvider : public TextureProvider {
public:
    Texture* load(Engine* engine, const uint8_t* data, size_t size,
                 const std::string& uri) override {
        int width, height, channels;

        // 使用 stb_image 解码
        unsigned char* pixels = stbi_load_from_memory(
            data, static_cast<int>(size), &width, &height, &channels, 0);

        if (!pixels) {
            LOGE("Failed to decode image: %s", uri.c_str());
            return nullptr;
        }

        // 确定纹理格式
        Texture::InternalFormat format;
        switch (channels) {
            case 1: format = Texture::InternalFormat::R8; break;
            case 2: format = Texture::InternalFormat::RG8; break;
            case 3: format = Texture::InternalFormat::RGB8; break;
            case 4: format = Texture::InternalFormat::RGBA8; break;
            default:
                stbi_image_free(pixels);
                return nullptr;
        }

        // 创建 Filament 纹理
        Texture* texture = Texture::Builder()
            .width(width)
            .height(height)
            .levels(1)
            .format(format)
            .build(*engine);

        // 上传像素数据
        Texture::PixelBufferDescriptor buffer(
            pixels, size_t(width * height * channels),
            Texture::Format::RGB, Texture::Type::UBYTE,
            [](void* buffer, size_t, void*) {
                stbi_image_free(buffer);
            });

        texture->setImage(*engine, 0, std::move(buffer));
        return texture;
    }

    std::vector<std::string> getSupportedMimeTypes() const override {
        return {"image/jpeg", "image/png", "image/bmp", "image/gif"};
    }
};
```

## 资产实例化

### FilamentInstance 设计

FilamentInstance 允许共享几何和材质，但拥有独立的变换和动画状态：

```cpp
// libs/gltfio/include/gltfio/FilamentInstance.h
class FilamentInstance {
public:
    // 实体管理
    const Entity* getEntities() const noexcept;
    size_t getEntityCount() const noexcept;
    Entity getRoot() const noexcept;

    // 动画控制
    Animator* getAnimator() noexcept;

    // 材质变体
    void applyMaterialVariant(size_t variantIndex);
    size_t getMaterialVariantCount() const noexcept;

    // 可见性控制
    void setVisible(bool visible);
    bool isVisible() const noexcept;
};

// 内部实现
struct FFilamentInstance {
    FFilamentAsset* mAsset;                          // 父资产
    Entity mRoot;                                    // 根实体
    FixedCapacityVector<Entity> mNodeMap;            // 节点映射
    FixedCapacityVector<InstanceInfo> mSkins;        // 蒙皮实例
    std::unique_ptr<Animator> mAnimator;             // 动画器
    size_t mCurrentMaterialVariant = 0;              // 当前材质变体
    bool mVisible = true;                            // 可见性
};
```

### 实例创建过程

```cpp
FFilamentInstance* FAssetLoader::createInstance(FFilamentAsset* asset) {
    if (!asset->mSourceAsset) {
        LOGE("Cannot create instance after releaseSourceData()");
        return nullptr;
    }

    // 1. 创建实例对象
    FFilamentInstance* instance = new FFilamentInstance();
    instance->mAsset = asset;

    const cgltf_data* data = asset->mSourceAsset->hierarchy;

    // 2. 创建根实体
    instance->mRoot = mEntityManager->create();
    auto rootTransform = mEngine->getTransformManager().getInstance(instance->mRoot);
    mEngine->getTransformManager().setTransform(rootTransform, mat4f());

    // 3. 克隆节点层次结构
    instance->mNodeMap.resize(data->nodes_count);

    for (cgltf_size nodeIndex = 0; nodeIndex < data->nodes_count; ++nodeIndex) {
        const cgltf_node& srcNode = data->nodes[nodeIndex];

        // 创建新实体
        Entity entity = mEntityManager->create();
        instance->mNodeMap[nodeIndex] = entity;

        // 设置变换
        auto transform = mEngine->getTransformManager().getInstance(entity);
        mat4f localMatrix = computeLocalMatrix(srcNode);
        mEngine->getTransformManager().setTransform(transform, localMatrix);

        // 设置父子关系
        if (srcNode.parent) {
            size_t parentIndex = srcNode.parent - data->nodes;
            Entity parentEntity = instance->mNodeMap[parentIndex];
            auto parentTransform = mEngine->getTransformManager().getInstance(parentEntity);
            mEngine->getTransformManager().setParent(transform, parentTransform);
        } else {
            // 根节点的父亲是实例根
            mEngine->getTransformManager().setParent(transform, rootTransform);
        }

        // 复制可渲染组件（如果有）
        if (srcNode.mesh) {
            cloneRenderableComponent(asset, entity, srcNode.mesh);
        }
    }

    // 4. 创建蒙皮实例
    createSkinInstances(asset, instance);

    // 5. 创建动画器
    instance->mAnimator = std::make_unique<Animator>(asset, instance);

    // 6. 添加到资产的实例列表
    asset->mInstances.push_back(instance);

    return instance;
}
```

### 蒙皮实例化

```cpp
void FAssetLoader::createSkinInstances(FFilamentAsset* asset, FFilamentInstance* instance) {
    const cgltf_data* data = asset->mSourceAsset->hierarchy;
    instance->mSkins.resize(data->skins_count);

    for (cgltf_size skinIndex = 0; skinIndex < data->skins_count; ++skinIndex) {
        const cgltf_skin& srcSkin = data->skins[skinIndex];
        InstanceInfo& instanceSkin = instance->mSkins[skinIndex];

        // 映射关节实体
        instanceSkin.joints.resize(srcSkin.joints_count);
        for (cgltf_size jointIndex = 0; jointIndex < srcSkin.joints_count; ++jointIndex) {
            size_t nodeIndex = srcSkin.joints[jointIndex] - data->nodes;
            instanceSkin.joints[jointIndex] = instance->mNodeMap[nodeIndex];
        }

        // 查找受影响的可渲染实体
        instanceSkin.targets.clear();
        for (cgltf_size nodeIndex = 0; nodeIndex < data->nodes_count; ++nodeIndex) {
            const cgltf_node& node = data->nodes[nodeIndex];

            if (node.skin == &srcSkin && node.mesh) {
                Entity entity = instance->mNodeMap[nodeIndex];
                instanceSkin.targets.push_back(entity);
            }
        }
    }
}
```

## 动画数据解析

### 动画解析流程

```cpp
void FAssetLoader::createAnimations(const cgltf_data* data, FFilamentAsset* asset) {
    // 动画数据将在 Animator 构造时解析
    // 这里只进行基本验证

    for (cgltf_size animIndex = 0; animIndex < data->animations_count; ++animIndex) {
        const cgltf_animation& anim = data->animations[animIndex];

        // 验证动画数据完整性
        if (!validateAnimation(anim)) {
            LOGW("Animation %zu has invalid data, skipping", animIndex);
            continue;
        }

        // 检查采样器和通道的数量限制
        if (anim.samplers_count > MAX_SAMPLERS_PER_ANIMATION) {
            LOGW("Animation %zu has too many samplers (%zu > %d)",
                 animIndex, anim.samplers_count, MAX_SAMPLERS_PER_ANIMATION);
        }

        if (anim.channels_count > MAX_CHANNELS_PER_ANIMATION) {
            LOGW("Animation %zu has too many channels (%zu > %d)",
                 animIndex, anim.channels_count, MAX_CHANNELS_PER_ANIMATION);
        }
    }

    // 标记动画数据可用
    asset->mHasAnimations = (data->animations_count > 0);
}

bool validateAnimation(const cgltf_animation& anim) {
    // 检查采样器
    for (cgltf_size samplerIndex = 0; samplerIndex < anim.samplers_count; ++samplerIndex) {
        const cgltf_animation_sampler& sampler = anim.samplers[samplerIndex];

        if (!sampler.input || !sampler.output) {
            return false;
        }

        // 验证输入访问器（时间轴）
        if (sampler.input->type != cgltf_type_scalar ||
            sampler.input->component_type != cgltf_component_type_r_32f) {
            return false;
        }

        // 验证输出访问器（值）
        const cgltf_accessor* output = sampler.output;
        if (output->component_type != cgltf_component_type_r_32f) {
            return false;
        }

        // 根据插值类型验证数据大小
        size_t expectedCount = sampler.input->count;
        if (sampler.interpolation == cgltf_interpolation_type_cubic_spline) {
            expectedCount *= 3;  // in-tangent, value, out-tangent
        }

        size_t actualCount = output->count;
        switch (output->type) {
            case cgltf_type_scalar: /* no change */ break;
            case cgltf_type_vec3: actualCount /= 3; break;
            case cgltf_type_vec4: actualCount /= 4; break;
            default: return false;
        }

        if (actualCount != expectedCount) {
            return false;
        }
    }

    // 检查通道
    for (cgltf_size channelIndex = 0; channelIndex < anim.channels_count; ++channelIndex) {
        const cgltf_animation_channel& channel = anim.channels[channelIndex];

        if (!channel.sampler || !channel.target_node) {
            return false;
        }

        // 验证采样器索引
        size_t samplerIndex = channel.sampler - anim.samplers;
        if (samplerIndex >= anim.samplers_count) {
            return false;
        }

        // 验证目标路径
        switch (channel.target_path) {
            case cgltf_animation_path_type_translation:
            case cgltf_animation_path_type_rotation:
            case cgltf_animation_path_type_scale:
            case cgltf_animation_path_type_weights:
                break;  // 有效路径
            default:
                return false;
        }
    }

    return true;
}
```

### 时间轴优化

```cpp
// 在 Animator.cpp 中的时间轴处理
void createSampler(const cgltf_animation_sampler& src, Sampler& dst) {
    const cgltf_accessor* timelineAccessor = src.input;

    // 读取时间数据
    const float* timelineFloats = getAccessorFloatData(timelineAccessor);

    // 构建时间到索引的映射（使用红黑树优化查找）
    dst.times.clear();
    for (size_t i = 0, len = timelineAccessor->count; i < len; ++i) {
        float time = timelineFloats[i];
        dst.times[time] = i;
    }

    // 转换值数据为浮点数
    const cgltf_accessor* valuesAccessor = src.output;
    dst.values.resize(getAccessorElementCount(valuesAccessor));

    cgltf_accessor_unpack_floats(src.output, &dst.values[0], dst.values.size());

    // 设置插值类型
    switch (src.interpolation) {
        case cgltf_interpolation_type_linear:
            dst.interpolation = Sampler::LINEAR;
            break;
        case cgltf_interpolation_type_step:
            dst.interpolation = Sampler::STEP;
            break;
        case cgltf_interpolation_type_cubic_spline:
            dst.interpolation = Sampler::CUBIC;
            break;
    }
}
```

## 内存管理和优化

### 内存使用优化

#### 1. 零拷贝访问器数据
```cpp
// 直接使用 glTF 缓冲区中的数据，避免拷贝
const float* getAccessorFloatData(const cgltf_accessor* accessor) {
    const cgltf_buffer_view* bufferView = accessor->buffer_view;
    const cgltf_buffer* buffer = bufferView->buffer;

    const uint8_t* basePtr = static_cast<const uint8_t*>(buffer->data);
    return reinterpret_cast<const float*>(
        basePtr + bufferView->offset + accessor->offset);
}
```

#### 2. 延迟数据解包
```cpp
class LazyAccessor {
    const cgltf_accessor* mAccessor;
    mutable std::vector<float> mUnpackedData;
    mutable bool mUnpacked = false;

public:
    const float* getData() const {
        if (!mUnpacked) {
            mUnpackedData.resize(getElementCount());
            cgltf_accessor_unpack_floats(mAccessor, mUnpackedData.data(), mUnpackedData.size());
            mUnpacked = true;
        }
        return mUnpackedData.data();
    }
};
```

#### 3. 资源去重
```cpp
class ResourceDeduplicator {
    struct BufferHash {
        size_t operator()(const std::vector<uint8_t>& buffer) const {
            return std::hash<std::string_view>{}(
                std::string_view(reinterpret_cast<const char*>(buffer.data()), buffer.size()));
        }
    };

    std::unordered_map<std::vector<uint8_t>, VertexBuffer*, BufferHash> mVertexBufferCache;
    std::unordered_map<std::vector<uint8_t>, IndexBuffer*, BufferHash> mIndexBufferCache;

public:
    VertexBuffer* getOrCreateVertexBuffer(const std::vector<uint8_t>& data) {
        auto it = mVertexBufferCache.find(data);
        if (it != mVertexBufferCache.end()) {
            return it->second;  // 返回缓存的缓冲区
        }

        // 创建新缓冲区
        VertexBuffer* buffer = createVertexBufferFromData(data);
        mVertexBufferCache[data] = buffer;
        return buffer;
    }
};
```

### 流式加载

```cpp
class StreamingAssetLoader {
    struct StreamingContext {
        FilamentAsset* asset;
        std::atomic<bool> geometryLoaded{false};
        std::atomic<bool> texturesLoaded{false};
        std::atomic<bool> animationsLoaded{false};
    };

public:
    void loadAssetStreaming(const std::string& path,
                           std::function<void(FilamentAsset*)> callback) {
        auto context = std::make_shared<StreamingContext>();

        // 阶段 1: 加载基础几何体（高优先级）
        std::async(std::launch::async, [this, path, context, callback]() {
            context->asset = loadGeometryOnly(path);
            context->geometryLoaded = true;

            // 立即回调，让用户可以开始渲染基础模型
            callback(context->asset);

            // 阶段 2: 加载纹理（中优先级）
            loadTexturesAsync(context->asset, [context]() {
                context->texturesLoaded = true;
            });

            // 阶段 3: 加载动画（低优先级）
            loadAnimationsAsync(context->asset, [context]() {
                context->animationsLoaded = true;
            });
        });
    }
};
```

## 总结

glTF 加载机制的关键特点：

### 分层设计
1. **cgltf 解析**: 底层格式解析和验证
2. **AssetLoader**: 中层资源转换和创建
3. **ResourceLoader**: 高层异步资源加载
4. **实例化系统**: 支持高效的资源共享

### 性能优化
1. **零拷贝**: 直接使用 glTF 缓冲区数据
2. **异步加载**: 并行处理纹理和缓冲区
3. **资源去重**: 共享相同的几何和纹理
4. **流式加载**: 分阶段加载不同优先级的数据

### 扩展性
1. **纹理提供者**: 支持多种图像格式
2. **材质系统**: 可插拔的材质生成
3. **实例化**: 高效的多实例渲染
4. **动画系统**: 独立的动画处理

这个设计使 Filament 能够高效地处理复杂的 glTF 场景，同时保持良好的内存使用和加载性能。