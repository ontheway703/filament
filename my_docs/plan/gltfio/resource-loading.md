# 资源加载流程

## 概述

gltfio 的资源加载分为两个独立的阶段：
1. **结构加载** (AssetLoader) - 解析 glTF 结构，创建 Filament 对象
2. **资源加载** (ResourceLoader) - 加载外部数据，上传到 GPU

这种分离允许灵活的资源获取策略和平台适配。

## AssetLoader - 结构加载

### 创建 AssetLoader

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:1753`

```cpp
AssetConfiguration config = {
    .engine = engine,
    .materials = materialProvider,
    .names = nameManager,       // 可选
    .entities = entityManager,  // 可选，默认使用全局 EntityManager
    .defaultNodeName = nullptr, // 可选
    .ext = nullptr             // 扩展配置（mikktspace）
};

AssetLoader* loader = AssetLoader::create(config);
```

**内部实现** (`libs/gltfio/src/AssetLoader.cpp:256-270`):
```cpp
FAssetLoader(AssetConfiguration const& config) :
    mEntityManager(config.entities ? *config.entities : EntityManager::get()),
    mRenderableManager(config.engine->getRenderableManager()),
    mNameManager(config.names),
    mTransformManager(config.engine->getTransformManager()),
    mMaterials(*config.materials),
    mEngine(*config.engine),
    mDefaultNodeName(config.defaultNodeName) {

    if (config.ext) {
        // 创建扩展加载器（仅桌面平台）
        mLoaderExtended = std::make_unique<AssetLoaderExtended>(...);
    }
}
```

### 加载 glTF 文件

**主入口**: `AssetLoader::createAsset(bytes, nbytes)`

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:361-418`

```cpp
FilamentAsset* createAsset(const uint8_t* bytes, uint32_t byteCount) {
    // 1. 拷贝源数据（因为 cgltf 需要持久指针）
    utils::FixedCapacityVector<uint8_t> glbdata(byteCount);
    std::copy_n(bytes, byteCount, glbdata.data());

    // 2. 解析 glTF
    cgltf_data* sourceAsset;
    cgltf_result result = cgltf_parse(&options, glbdata.data(), byteCount, &sourceAsset);
    if (result != cgltf_result_success) {
        return nullptr;
    }

    // 3. 创建根 Asset（第一次遍历）
    FFilamentAsset* fAsset = createRootAsset(sourceAsset);

    // 4. 创建 Instance（第二次遍历）
    createInstances(numInstances, fAsset);

    return fAsset;
}
```

### 第一次遍历：createRootAsset

**职责**: 创建共享的 GPU 资源（VertexBuffer、IndexBuffer）

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:474-553`

**流程**:

```cpp
FFilamentAsset* createRootAsset(const cgltf_data* srcAsset) {
    // 1. 创建 FFilamentAsset 对象
    FFilamentAsset* fAsset = new FFilamentAsset(...);

    // 2. 创建根实体
    fAsset->mRoot = mEntityManager.create();
    mTransformManager.create(fAsset->mRoot);

    // 3. 解析场景（Scene）信息
    for (size_t si = 0; si < srcAsset->scenes_count; ++si) {
        const cgltf_scene& scene = srcAsset->scenes[si];
        fAsset->mScenes.emplace_back(scene.name);

        // 记录每个根节点属于哪些场景
        for (size_t ni = 0; ni < scene.nodes_count; ++ni) {
            fAsset->mRootNodes[scene.nodes[ni]].set(si);
        }
    }

    // 4. 遍历所有节点，创建 Primitive（VertexBuffer/IndexBuffer）
    for (const auto& [node, sceneMask] : fAsset->mRootNodes) {
        recursePrimitives(node, fAsset);
    }

    // 5. 收集所有外部资源 URI
    for (cgltf_size i = 0; i < srcAsset->buffers_count; ++i) {
        addResourceUri(srcAsset->buffers[i].uri);
    }
    for (cgltf_size i = 0; i < srcAsset->images_count; ++i) {
        addResourceUri(srcAsset->images[i].uri);
    }

    return fAsset;
}
```

#### recursePrimitives - 递归创建 Primitive

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:555-568`

```cpp
void recursePrimitives(const cgltf_node* node, FFilamentAsset* fAsset) {
    if (node->mesh) {
        createPrimitives(node, name, fAsset);
        fAsset->mRenderableCount++;
    }

    // 递归子节点
    for (cgltf_size i = 0; i < node->children_count; ++i) {
        recursePrimitives(node->children[i], fAsset);
    }
}
```

**createPrimitives 的详细实现见 [mesh-processing.md](./mesh-processing.md)**

### 第二次遍历：createInstances

**职责**: 创建实体层次结构和渲染组件

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:570-587`

```cpp
void createInstances(size_t numInstances, FFilamentAsset* fAsset) {
    for (size_t index = 0; index < numInstances; ++index) {
        createInstance(fAsset);
    }

    // 排序实体，让可渲染的在前面
    const auto& rm = mEngine.getRenderableManager();
    std::partition(fAsset->mEntities.begin(), fAsset->mEntities.end(),
        [&rm](Entity a) {
            return rm.hasComponent(a);
        }
    );
}
```

#### createInstance - 创建单个实例

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:421-472`

```cpp
FilamentInstance* createInstance(FFilamentAsset* fAsset) {
    // 1. 创建实例的根实体
    Entity instanceRoot = mEntityManager.create();
    mTransformManager.create(instanceRoot, rootTransform);

    // 2. 创建 FFilamentInstance 对象
    FFilamentInstance* instance = new FFilamentInstance(instanceRoot, fAsset);

    // 3. 递归创建所有实体
    for (const auto& pair : fAsset->mRootNodes) {
        recurseEntities(pair.first, pair.second, instanceRoot, fAsset, instance);
    }

    // 4. 导入骨骼蒙皮
    importSkins(instance, srcAsset);

    // 5. 创建动画器（可能延迟到资源加载完成）
    instance->createAnimator();

    return instance;
}
```

#### recurseEntities - 递归创建实体层次

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:589-659`

```cpp
void recurseEntities(const cgltf_node* node, SceneMask scenes, Entity parent,
        FFilamentAsset* fAsset, FFilamentInstance* instance) {

    // 1. 创建 Entity
    const Entity entity = mEntityManager.create();

    // 2. 创建 Transform 组件
    if (node->has_matrix) {
        // 直接使用矩阵
        memcpy(&localTransform[0][0], &node->matrix[0], 16 * sizeof(float));
    } else {
        // 使用 TRS (Translation, Rotation, Scale)
        mTrsTransformManager.create(entity, *translation, *rotation, *scale);
        localTransform = mTrsTransformManager.getTransform(...);
    }
    mTransformManager.create(entity, parentTransform, localTransform);

    // 3. 记录到 Instance
    instance->mEntities.push_back(entity);
    instance->mNodeMap[node - srcAsset->nodes] = entity;

    // 4. 设置名称
    if (name && mNameManager) {
        mNameManager->addComponent(entity);
        mNameManager->setName(mNameManager->getInstance(entity), name);
    }

    // 5. 创建 Renderable 组件（如果有 mesh）
    if (node->mesh) {
        createRenderable(node, entity, name, fAsset);
    }

    // 6. 创建 Light 组件
    if (node->light) {
        createLight(node->light, entity, fAsset);
    }

    // 7. 创建 Camera 组件
    if (node->camera) {
        createCamera(node->camera, entity, fAsset);
    }

    // 8. 递归子节点
    for (cgltf_size i = 0; i < node->children_count; ++i) {
        recurseEntities(node->children[i], scenes, entity, fAsset, instance);
    }
}
```

**createRenderable 的详细实现见 [mesh-processing.md](./mesh-processing.md#创建-renderable)**

## ResourceLoader - 资源加载

### 创建 ResourceLoader

**代码位置**: `libs/gltfio/src/ResourceLoader.cpp:324`

```cpp
ResourceConfiguration config = {
    .engine = engine,
    .gltfPath = "path/to/model.gltf",  // 基础路径，用于解析相对 URI
    .normalizeSkinningWeights = true   // 是否规范化蒙皮权重
};

ResourceLoader resourceLoader(config);
```

**内部实现** (`libs/gltfio/src/ResourceLoader.cpp:81-85`):
```cpp
struct ResourceLoader::Impl {
    Engine* const mEngine;
    bool mNormalizeSkinningWeights;
    std::string mGltfPath;

    UriDataCacheHandle mUriDataCache;  // URI 数据缓存
    TextureProviderList mTextureProviders;  // 纹理提供者
    BufferTextureCache mBufferTextureCache;  // 缓冲区纹理缓存
    FilepathTextureCache mFilepathTextureCache;  // 文件路径纹理缓存
};
```

### 添加纹理提供者

**代码位置**: `libs/gltfio/src/ResourceLoader.cpp:491-493`

```cpp
// 创建纹理提供者（例如 StbProvider）
TextureProvider* stbProvider = createStbProvider(engine);

// 注册 MIME 类型
resourceLoader.addTextureProvider("image/png", stbProvider);
resourceLoader.addTextureProvider("image/jpeg", stbProvider);
```

### 添加资源数据 (无文件系统平台)

**代码位置**: `libs/gltfio/src/ResourceLoader.cpp:364-384`

```cpp
void addResourceData(const char* uri, BufferDescriptor&& buffer) {
    // 替换已存在的数据
    auto iter = mUriDataCache->find(uri);
    if (iter != mUriDataCache->end()) {
        mUriDataCache->erase(iter);
    }

    // 存储到缓存
    mUriDataCache->emplace(uri, std::move(buffer));

    // 如果正在异步加载且是纹理，立即处理
    if (isTexture(uri) && mAsyncAsset && mRemainingTextureDownloads > 0) {
        createTextures(mAsyncAsset, true);
    }
}
```

### 同步加载资源

**代码位置**: `libs/gltfio/src/ResourceLoader.cpp:395-478`

```cpp
bool loadResources(FilamentAsset* asset) {
    // 1. 加载 cgltf 缓冲区数据
    utility::loadCgltfBuffers(gltf, mGltfPath.c_str(), mUriDataCache);

    // 2. 解压 Draco 网格
    for (auto& [prim, vertexBuffer]: primitives) {
        if (prim->has_draco_mesh_compression) {
            utility::decodeDracoMeshes(gltf, prim, dracoCache);
        }
    }

    // 3. 解压 meshopt 压缩
    utility::decodeMeshoptCompression((cgltf_data*) gltf);

    // 4. 上传顶点和索引缓冲区
    uploadBuffers(asset, *mEngine, mUriDataCache);

    // 5. 计算切线空间
    computeTangents(asset);

    // 6. 创建骨骼数据
    createSkins(gltf, mNormalizeSkinningWeights, asset->mSkins);

    // 7. 创建纹理
    createTextures(asset, async=false);

    // 8. 等待纹理解码完成（同步模式）
    for (const auto& iter: mTextureProviders) {
        iter.second->waitForCompletion();
        iter.second->updateQueue();
    }

    // 9. 提交依赖图
    asset->mDependencyGraph.commitEdges();

    // 10. 创建动画器
    for (FFilamentInstance* instance : asset->mInstances) {
        instance->createAnimator();
    }

    return true;
}
```

### 异步加载资源

**API**:
```cpp
bool asyncBeginLoad(FilamentAsset* asset);
void asyncUpdateLoad();  // 在主线程调用
float asyncGetLoadProgress() const;
void asyncCancelLoad();
```

**代码位置**:
- `asyncBeginLoad`: `libs/gltfio/src/ResourceLoader.cpp:480-483`
- `asyncUpdateLoad`: `libs/gltfio/src/ResourceLoader.cpp:513-523`
- `asyncGetLoadProgress`: `libs/gltfio/src/ResourceLoader.cpp:495-511`

**异步加载流程**:

```cpp
// 1. 开始异步加载
resourceLoader.asyncBeginLoad(asset);

// 2. 渲染循环
while (!done) {
    // 更新加载状态（解码完成的纹理会被应用）
    resourceLoader.asyncUpdateLoad();

    // 逐步添加准备好的实体到场景
    while (Entity e = asset->popRenderable()) {
        scene->addEntity(e);
    }

    // 检查进度
    float progress = resourceLoader.asyncGetLoadProgress();

    // 渲染
    if (renderer->beginFrame(swapChain)) {
        renderer->render(view);
        renderer->endFrame();
    }
}
```

**asyncUpdateLoad 实现** (`libs/gltfio/src/ResourceLoader.cpp:513-523`):
```cpp
void asyncUpdateLoad() {
    if (!mAsyncAsset) return;

    for (const auto& iter : mTextureProviders) {
        // 更新解码队列
        iter.second->updateQueue();

        // 弹出已解码的纹理
        while (Texture* texture = iter.second->popTexture()) {
            // 标记为就绪，触发依赖的 Entity 可以添加到场景
            mAsyncAsset->mDependencyGraph.markAsReady(texture);
        }
    }
}
```

## 缓冲区数据加载

### loadCgltfBuffers

**位置**: `libs/gltfio/src/Utility.cpp` (未在提供的代码中，但被引用)

**功能**:
1. 遍历 `cgltf_data->buffers`
2. 对每个 buffer:
   - 如果是 GLB 嵌入数据，直接使用
   - 如果是 data URI，解码 base64
   - 如果是外部 URI，从 UriDataCache 获取或从文件系统加载
3. 将数据指针存储到 `buffer->data`

### uploadBuffers

**代码位置**: `libs/gltfio/src/ResourceLoader.cpp:230-320`

**功能**: 将 CPU 缓冲区数据上传到 GPU

```cpp
void uploadBuffers(FFilamentAsset* asset, Engine& engine, UriDataCacheHandle uriDataCache) {
    auto& slots = asset->mResourceInfo.mBufferSlots;

    for (auto const& slot: slots) {
        const cgltf_accessor* accessor = slot.accessor;
        const uint8_t* data = /* 计算数据指针 */;
        const uint32_t size = utility::computeBindingSize(accessor);

        if (slot.vertexBuffer) {
            // 检查是否需要类型转换
            if (utility::requiresConversion(accessor)) {
                // 转换为 float
                float* floatsData = (float*) malloc(floatsByteCount);
                cgltf_accessor_unpack_floats(accessor, floatsData, floatsCount);

                // 创建 BufferObject 并上传
                BufferObject* bo = BufferObject::Builder().size(floatsByteCount).build(engine);
                bo->setBuffer(engine, BufferDescriptor(floatsData, floatsByteCount, FREE_CALLBACK));
                slot.vertexBuffer->setBufferObjectAt(engine, slot.bufferIndex, bo);
            } else {
                // 直接上传原始数据
                BufferObject* bo = BufferObject::Builder().size(size).build(engine);
                bo->setBuffer(engine, BufferDescriptor(data, size, uploadCallback, uploadUserdata));
                slot.vertexBuffer->setBufferObjectAt(engine, slot.bufferIndex, bo);
            }
        }
        else if (slot.indexBuffer) {
            // 处理 8 位索引（需要转换为 16 位）
            if (accessor->component_type == cgltf_component_type_r_8u) {
                uint16_t* data16 = (uint16_t*) malloc(size * 2);
                utility::convertBytesToShorts(data16, data, size);
                slot.indexBuffer->setBuffer(engine, BufferDescriptor(data16, size16, FREE_CALLBACK));
            } else {
                slot.indexBuffer->setBuffer(engine, BufferDescriptor(data, size, uploadCallback));
            }
        }
        else if (slot.morphTargetBuffer) {
            // 上传变形目标数据
            slot.morphTargetBuffer->setPositionsAt(engine, slot.bufferIndex,
                (const float3*) data, slot.morphTargetCount, slot.morphTargetOffset);
        }
    }
}
```

**关键点**:
- **uploadCallback**: 在数据上传完成后释放 CPU 内存
- **UploadEvent**: 保持对 SourceAsset 的引用，防止过早释放
- **类型转换**: 某些格式（如归一化整数）需要转换为浮点数

## 纹理加载

### createTextures

**代码位置**: `libs/gltfio/src/ResourceLoader.cpp:634-672`

```cpp
void createTextures(FFilamentAsset* asset, bool async) {
    mRemainingTextureDownloads = 0;

    for (size_t textureIndex = 0; textureIndex < asset->mTextures.size(); ++textureIndex) {
        FFilamentAsset::TextureInfo& info = asset->mTextures[textureIndex];

        // 获取或创建纹理
        auto [texture, cacheResult] = getOrCreateTexture(asset, textureIndex, info.flags);

        if (texture == nullptr) {
            if (cacheResult == CacheResult::NOT_READY) {
                // 资源还未下载
                mRemainingTextureDownloads++;
            }
            continue;
        }

        // 初始化 TextureInfo
        if (info.texture == nullptr) {
            info.texture = texture;
            info.isOwner = (cacheResult == CacheResult::MISS);
        }

        // 绑定到 MaterialInstance
        for (const TextureSlot& slot : info.bindings) {
            asset->applyTextureBinding(textureIndex, slot);
        }
    }

    // 同步模式：等待所有纹理解码完成
    if (!async) {
        for (const auto& iter : mTextureProviders) {
            iter.second->waitForCompletion();
            iter.second->updateQueue();
        }
    }
}
```

### getOrCreateTexture

**代码位置**: `libs/gltfio/src/ResourceLoader.cpp:525-625`

**功能**: 从不同来源获取纹理数据并创建 Texture 对象

```cpp
std::pair<Texture*, CacheResult> getOrCreateTexture(
        FFilamentAsset* asset, size_t textureIndex, TextureProvider::TextureFlags flags) {

    const cgltf_texture& srcTexture = asset->mSourceAsset->hierarchy->textures[textureIndex];
    const cgltf_image* image = srcTexture.image;
    const cgltf_buffer_view* bv = image->buffer_view;
    const char* uri = image->uri;

    // 确定 MIME 类型
    std::string mime = image->mime_type ? image->mime_type : "";
    if (mime.empty()) {
        const std::string extension = Path(uri).getExtension();
        mime = extension == "jpg" ? "image/jpeg" : "image/" + extension;
    }

    // 获取对应的 TextureProvider
    auto foundProvider = mTextureProviders.find(mime);
    if (foundProvider == mTextureProviders.end()) {
        slog.e << "Missing texture provider for " << mime << io::endl;
        return {};
    }
    TextureProvider* provider = foundProvider->second;

    // 1. 尝试从 BufferView 获取
    if (bv && bv->buffer->data) {
        const uint8_t* sourceData = (const uint8_t*) bv->buffer->data + bv->offset;

        // 检查缓存
        if (auto iter = mBufferTextureCache.find(sourceData); iter != mBufferTextureCache.end()) {
            return {iter->second, CacheResult::FOUND};
        }

        // 推送到解码队列
        if (Texture* texture = provider->pushTexture(sourceData, bv->size, mime.c_str(), flags)) {
            mBufferTextureCache[sourceData] = texture;
            return {texture, CacheResult::MISS};
        }
    }

    // 2. 尝试解析 data URI
    else if (const uint8_t* dataUriContent = parseDataUri(uri, &mime, &dataUriSize)) {
        if (auto iter = mBufferTextureCache.find(uri); iter != mBufferTextureCache.end()) {
            free((void*)dataUriContent);
            return {iter->second, CacheResult::FOUND};
        }

        Texture* texture = provider->pushTexture(dataUriContent, dataUriSize, mime.c_str(), flags);
        free((void*)dataUriContent);
        if (texture) {
            mBufferTextureCache[uri] = texture;
            return {texture, CacheResult::MISS};
        }
    }

    // 3. 尝试从 UriDataCache 获取
    else if (auto iter = mUriDataCache->find(uri); iter != mUriDataCache->end()) {
        const uint8_t* sourceData = (const uint8_t*) iter->second.buffer;

        if (auto iter = mBufferTextureCache.find(sourceData); iter != mBufferTextureCache.end()) {
            return {iter->second, CacheResult::FOUND};
        }

        if (Texture* texture = provider->pushTexture(sourceData, iter->second.size, mime.c_str(), flags)) {
            mBufferTextureCache[sourceData] = texture;
            return {texture, CacheResult::MISS};
        }
    }

    // 4. 尝试从文件系统加载 (如果支持)
    else if constexpr (GLTFIO_USE_FILESYSTEM) {
        if (auto iter = mFilepathTextureCache.find(uri); iter != mFilepathTextureCache.end()) {
            return {iter->second, CacheResult::FOUND};
        }

        Path fullpath = Path(mGltfPath).getParent() + uri;
        if (fullpath.exists()) {
            std::vector<uint8_t> buffer = /* 读取文件 */;
            if (Texture* texture = provider->pushTexture(buffer.data(), buffer.size(), mime.c_str(), flags)) {
                mFilepathTextureCache[uri] = texture;
                return {texture, CacheResult::MISS};
            }
        }
    }

    // 5. 资源尚未准备好（异步加载中）
    else {
        return {nullptr, CacheResult::NOT_READY};
    }

    return {};
}
```

**纹理缓存策略**:
- **BufferTextureCache**: 按内存地址缓存（BufferView、data URI、UriDataCache）
- **FilepathTextureCache**: 按文件路径缓存（仅文件系统）
- 避免重复创建相同的纹理对象

### applyTextureBinding

**代码位置**: `libs/gltfio/src/FilamentAsset.cpp` (未在提供的代码中)

**功能**: 将 Texture 绑定到 MaterialInstance

```cpp
void applyTextureBinding(size_t textureIndex, const TextureSlot& tb, bool addDependency) {
    TextureInfo& info = mTextures[textureIndex];

    // 设置材质参数
    tb.materialInstance->setParameter(tb.materialParameter,
        info.texture, textureSampler);

    // 添加到依赖图（用于渐进式加载）
    if (addDependency) {
        mDependencyGraph.addEdge(tb.materialInstance, info.texture);
    }
}
```

## 切线空间计算

**代码位置**: `libs/gltfio/src/ResourceLoader.cpp:674-763`

**功能**: 为没有切线数据的模型生成切线空间

```cpp
void computeTangents(FFilamentAsset* asset) {
    const cgltf_accessor* kGenerateTangents = &asset->mGenerateTangents;
    const cgltf_accessor* kGenerateNormals = &asset->mGenerateNormals;

    // 1. 收集需要生成切线的 VertexBuffer
    tsl::robin_map<VertexBuffer*, uint8_t> baseTangents;
    for (auto const& slot: slots) {
        if (slot.accessor == kGenerateTangents || slot.accessor == kGenerateNormals) {
            baseTangents[slot.vertexBuffer] = slot.bufferIndex;
        }
    }

    // 2. 为每个三角形网格创建 Job
    std::vector<TangentsJob::Params> jobParams;
    for (auto const& [prim, vb] : primitives) {
        if (prim->type == cgltf_primitive_type_triangles) {
            auto iter = baseTangents.find(vb);
            if (iter != baseTangents.end()) {
                jobParams.emplace_back(Params {{ prim }, {vb, nullptr, 0, iter->second }});
            }
        }
    }

    // 3. 为变形目标创建 Job
    for (size_t i = 0; i < asset->mSourceAsset->hierarchy->meshes_count; ++i) {
        const cgltf_mesh& mesh = asset->mSourceAsset->hierarchy->meshes[i];
        // ... 创建变形目标的切线 Job
    }

    // 4. 并行执行所有 Job
    JobSystem* js = &mEngine->getJobSystem();
    JobSystem::Job* parent = js->createJob();
    for (Params& params : jobParams) {
        js->run(jobs::createJob(*js, parent, [pptr] { TangentsJob::run(pptr); }));
    }
    js->runAndWait(parent);

    // 5. 上传结果到 GPU
    for (Params& params : jobParams) {
        if (params.context.vb) {
            BufferObject* bo = BufferObject::Builder()
                .size(params.out.vertexCount * sizeof(short4)).build(*mEngine);
            bo->setBuffer(*mEngine, BufferDescriptor(params.out.results,
                bo->getByteCount(), FREE_CALLBACK));
            params.context.vb->setBufferObjectAt(*mEngine, params.context.slot, bo);
        } else {
            params.context.tb->setTangentsAt(*mEngine, params.in.morphTargetIndex,
                params.out.results, params.out.vertexCount, params.context.offset);
        }
    }
}
```

**切线存储格式**: short4 四元数（MikkTSpace）

## 骨骼数据创建

**代码位置**: `libs/gltfio/src/ResourceLoader.cpp:190-228`

```cpp
void createSkins(cgltf_data const* gltf, bool normalize,
        utils::FixedCapacityVector<FFilamentAsset::Skin>& skins) {

    // 1. 可选：规范化蒙皮权重（确保每个顶点的权重和为 1）
    if (normalize) {
        normalizeSkinningWeights(gltf);
    }

    // 2. 为每个 skin 创建数据
    skins.reserve(gltf->skins_count);
    for (cgltf_size i = 0; i < gltf->skins_count; ++i) {
        const cgltf_skin& srcSkin = gltf->skins[i];

        // 拷贝 inverse bind matrices
        const cgltf_accessor* srcMatrices = srcSkin.inverse_bind_matrices;
        FixedCapacityVector<mat4f> inverseBindMatrices(srcSkin.joints_count);

        if (srcMatrices) {
            uint8_t* srcBuffer = /* 获取数据指针 */;
            memcpy((uint8_t*) inverseBindMatrices.data(), (const void*) srcBuffer,
                srcSkin.joints_count * sizeof(mat4f));
        }

        FFilamentAsset::Skin skin{
            .name = CString(srcSkin.name),
            .inverseBindMatrices = std::move(inverseBindMatrices),
        };
        skins.emplace_back(std::move(skin));
    }
}
```

**normalizeSkinningWeights** (`libs/gltfio/src/ResourceLoader.cpp:159-188`):
```cpp
void normalizeSkinningWeights(cgltf_data const* gltf) {
    for (cgltf_size mindex = 0; mindex < gltf->meshes_count; ++mindex) {
        for (cgltf_size pindex = 0; pindex < mesh.primitives_count; ++pindex) {
            for (cgltf_size aindex = 0; aindex < prim.attributes_count; ++aindex) {
                const auto& attr = prim.attributes[aindex];
                if (attr.type == cgltf_attribute_type_weights) {
                    cgltf_accessor* data = attr.data;

                    // 遍历所有权重向量
                    for (cgltf_size i = 0; i < data->count; ++i) {
                        float4* weights = /* 获取权重指针 */;
                        const float sum = weights->x + weights->y + weights->z + weights->w;
                        *weights /= sum;  // 归一化
                    }
                }
            }
        }
    }
}
```

## URI 处理

### parseDataUri

**代码位置**: `libs/gltfio/src/ResourceLoader.cpp:132-157`

**功能**: 解析 data URI (base64 编码)

```cpp
uint8_t const* parseDataUri(const char* uri, std::string* mimeType, size_t* psize) {
    // 检查是否是 data URI
    if (strncmp(uri, "data:", 5) != 0) {
        return nullptr;
    }

    // 查找 base64 编码部分
    const char* comma = strchr(uri, ',');
    if (comma && comma - uri >= 7 && strncmp(comma - 7, ";base64", 7) == 0) {
        const char* base64 = comma + 1;
        const size_t base64Size = strlen(base64);
        size_t size = /* 计算解码后大小 */;

        // 使用 cgltf 的 base64 解码器
        void* data = 0;
        cgltf_options options = {};
        cgltf_result result = cgltf_load_buffer_base64(&options, size, base64, &data);

        if (result == cgltf_result_success) {
            *mimeType = std::string(uri + 5, comma - 7);
            *psize = size;
            return (const uint8_t*) data;
        }
    }

    return nullptr;
}
```

**示例**: `data:image/png;base64,iVBORw0KGgo...`

## 资源生命周期

### SourceAsset 引用计数

**代码位置**: `libs/gltfio/src/FFilamentAsset.h:286-299`

```cpp
struct SourceAsset {
    ~SourceAsset() { cgltf_free(hierarchy); }
    cgltf_data* hierarchy;
    DracoCache dracoCache;
    utils::FixedCapacityVector<uint8_t> glbData;
};

using SourceHandle = std::shared_ptr<SourceAsset>;
```

**生命周期管理**:
1. FilamentAsset 持有 `SourceHandle mSourceAsset`
2. 异步上传回调持有 `SourceHandle`（通过 UploadEvent）
3. 所有引用释放后，自动调用 `cgltf_free()`

### releaseSourceData

**代码位置**: `libs/gltfio/src/FilamentAsset.cpp`

```cpp
void releaseSourceData() noexcept {
    mSourceAsset.reset();  // 释放 shared_ptr
    mResourceUris.clear();
    // ... 其他清理
}
```

**影响**:
- 无法再创建新的 Instance
- 无法再查询 glTF 元数据
- 减少内存占用

## 错误处理

### 验证和错误报告

**常见错误**:
1. glTF 解析失败
2. 缺少必需的顶点属性
3. 纹理提供者未注册
4. 资源 URI 无法解析
5. 数据格式不支持

**错误处理模式**:
```cpp
// 1. 解析错误
if (result != cgltf_result_success) {
    slog.e << "Unable to parse glTF file." << io::endl;
    return nullptr;
}

// 2. 资源错误
if (!texture) {
    slog.e << "Unable to create texture " << name << ": "
           << provider->getPushMessage() << io::endl;
    return {};
}

// 3. 警告
slog.w << "Missing UV0 data in " << name << io::endl;
```

## 性能优化建议

### 1. 预加载策略

```cpp
// 异步加载，逐步显示
resourceLoader.asyncBeginLoad(asset);

while (progress < 1.0f) {
    resourceLoader.asyncUpdateLoad();
    while (Entity e = asset->popRenderable()) {
        scene->addEntity(e);
    }
    // 渲染...
    progress = resourceLoader.asyncGetLoadProgress();
}
```

### 2. 资源复用

```cpp
// 创建多个实例共享资源
FilamentAsset* asset = loader->createAsset(bytes, size);
FilamentInstance* instances[10];
for (int i = 0; i < 10; i++) {
    instances[i] = loader->createInstance(asset);
}

// 只需加载一次资源
resourceLoader.loadResources(asset);

// 添加所有实例到场景
for (auto* instance : instances) {
    scene->addEntities(instance->getEntities(), instance->getEntityCount());
}
```

### 3. 释放源数据

```cpp
// 加载完成后释放
resourceLoader.loadResources(asset);
asset->releaseSourceData();  // 节省内存
```

### 4. 纹理压缩

```cpp
// 使用 KTX2 或 Basis Universal
TextureProvider* ktx2Provider = createKtx2Provider(engine);
resourceLoader.addTextureProvider("image/ktx2", ktx2Provider);
```

## 下一步阅读

- [Mesh 处理](./mesh-processing.md) - VertexBuffer 和 IndexBuffer 创建细节
- [材质系统](./material-system.md) - MaterialInstance 和纹理绑定
- [GPU 交互](./gpu-interaction.md) - BufferObject 和纹理上传机制
