# 对象创建时机和生命周期

本文档详细说明 gltfio 中各种对象的创建时机、创建位置、拥有者和生命周期。

---

## 对象创建时机总表

| 对象类型 | 创建阶段 | 创建时机 | 创建位置 | 创建者 | 拥有者 | 销毁者 |
|---------|---------|---------|---------|-------|-------|-------|
| **FFilamentAsset** | Phase 1 | `createAsset()` 调用时 | `FAssetLoader::createRootAsset()` | AssetLoader | 用户 | `destroyAsset()` |
| **Entity (根)** | Phase 1 | Asset 创建时 | `createRootAsset()` | EntityManager | Asset | Asset 析构 |
| **Entity (节点)** | Phase 1 | Instance 创建时 | `recurseEntities()` | EntityManager | Asset | Asset 析构 |
| **VertexBuffer** | Phase 1 | 第一次遍历时 | `createPrimitive()` | Engine | Asset | Asset 析构 |
| **IndexBuffer** | Phase 1 | 第一次遍历时 | `createPrimitive()` | Engine | Asset | Asset 析构 |
| **BufferObject** | Phase 2 | 上传数据时 | `uploadBuffers()` | Engine | Asset | Asset 析构 |
| **MorphTargetBuffer** | Phase 1 | 有变形目标时 | `createRenderable()` | Engine | Asset | Asset 析构 |
| **TransformManager 组件** | Phase 1 | Entity 创建时 | `recurseEntities()` | TransformManager | Entity | Entity 销毁 |
| **RenderableManager 组件** | Phase 1 | 有网格时 | `createRenderable()` | RenderableManager | Entity | Entity 销毁 |
| **LightManager 组件** | Phase 1 | 有灯光时 | `createLight()` | LightManager | Entity | Entity 销毁 |
| **CameraManager 组件** | Phase 1 | 有相机时 | `createCamera()` | Engine | Entity | Entity 销毁 |
| **MaterialInstance** | Phase 1 | Renderable 创建时 | `createRenderable()` | MaterialProvider | Instance | Instance 析构 |
| **Texture** | Phase 2 | 资源加载时 | `createTextures()` | TextureProvider | Asset | Asset 析构 |
| **FilamentInstance** | Phase 1 | `createInstance()` 时 | `FAssetLoader::createInstance()` | AssetLoader | Asset | Asset 析构 |
| **Animator** | Phase 2 | 资源加载完成后 | `instance->createAnimator()` | Animator 类 | Instance | Instance 析构 |
| **Skin 数据 (Asset)** | Phase 2 | 资源加载时 | `createSkins()` | ResourceLoader | Asset | Asset 析构 |
| **Skin 数据 (Instance)** | Phase 1 | Instance 创建时 | `importSkins()` | AssetLoader | Instance | Instance 析构 |

---

## 详细创建流程

### 1. FFilamentAsset

**创建时机**: `AssetLoader::createAsset()` 调用时

**创建位置**:
```cpp
FFilamentAsset* FAssetLoader::createRootAsset(cgltf_data* srcAsset) {
    FFilamentAsset* asset = new FFilamentAsset(
        mEngine,
        mNameManager,
        mEntityManager,
        mNodeManager,
        mTrsTransformManager,
        srcAsset,
        useExtendedAlgo
    );
    return asset;
}
```

**生命周期**:
- 创建: `AssetLoader::createAsset()`
- 持有: 用户代码 (`FilamentAsset* asset`)
- 销毁: `AssetLoader::destroyAsset(asset)`

**职责**:
- 持有所有 GPU 资源 (VB/IB/Texture)
- 持有所有 Instance
- 持有 cgltf_data (可选)

---

### 2. Entity (根节点)

**创建时机**: Asset 创建时立即创建

**创建位置**:
```cpp
FFilamentAsset* FAssetLoader::createRootAsset(cgltf_data* srcAsset) {
    // ...

    // 创建根 Entity
    asset->mRoot = mEntityManager.create();

    // 添加 Transform 组件
    mTransformManager.create(asset->mRoot);

    return asset;
}
```

**生命周期**:
- 创建: Asset 创建时
- 持有: `asset->mRoot`
- 销毁: Asset 析构时

**特点**:
- 唯一用途: 作为所有实例的父节点
- 无 Renderable/Light/Camera 组件
- 只有 Transform 组件

---

### 3. Entity (场景节点)

**创建时机**: 每个 Instance 创建时,递归创建所有节点

**创建位置**:
```cpp
void FAssetLoader::recurseEntities(
    FFilamentAsset* asset,
    FFilamentInstance* instance,
    Entity parent,
    cgltf_node* node)
{
    // 创建 Entity
    Entity entity = mEntityManager.create();

    // 添加到实例
    instance->mEntities.push_back(entity);
    instance->mNodeMap[nodeIndex] = entity;

    // 创建 Transform 组件
    TransformManager::Instance tmi = mTransformManager.create(entity);
    mTransformManager.setParent(tmi, parent);

    // 根据节点类型添加组件
    if (node->mesh) createRenderable(...);
    if (node->light) createLight(...);
    if (node->camera) createCamera(...);

    // 递归子节点
    for (child : node->children) {
        recurseEntities(asset, instance, entity, child);
    }
}
```

**生命周期**:
- 创建: Instance 创建时
- 持有: `instance->mEntities[]`
- 销毁: Asset 析构时 (通过 Instance)

**组件**:
- 必有: TransformManager 组件
- 可选: RenderableManager / LightManager / CameraManager / Name 组件

---

### 4. VertexBuffer

**创建时机**: 第一次遍历,每个 primitive 创建一次

**创建位置**:
```cpp
void FAssetLoader::createPrimitive(FFilamentAsset* asset, cgltf_primitive* prim) {
    // 计算顶点数和属性
    size_t vertexCount = prim->attributes[0].data->count;

    // 构建 VertexBuffer
    VertexBuffer::Builder builder;
    builder.vertexCount(vertexCount)
           .bufferCount(bufferCount);

    // 添加属性
    builder.attribute(VertexAttribute::POSITION, 0,
                     VertexBuffer::AttributeType::FLOAT3, 0, stride);
    builder.attribute(VertexAttribute::NORMAL, 1,
                     VertexBuffer::AttributeType::FLOAT3, 0, stride);
    // ...

    VertexBuffer* vb = builder.build(*mEngine);

    // 保存到 Asset
    asset->mVertexBuffers.push_back(vb);
    asset->mMeshCache[meshIndex][primIndex].vertices = vb;
}
```

**生命周期**:
- 创建: Phase 1 第一次遍历
- 数据上传: Phase 2 `uploadBuffers()`
- 持有: `asset->mVertexBuffers[]`
- 共享: 多个实例复用同一 VB
- 销毁: Asset 析构时

**状态**:
- Phase 1: 空对象,无数据
- Phase 2: 已上传数据,可用

---

### 5. IndexBuffer

**创建时机**: 第一次遍历,每个有索引的 primitive 创建

**创建位置**:
```cpp
void FAssetLoader::createPrimitive(FFilamentAsset* asset, cgltf_primitive* prim) {
    if (prim->indices) {
        size_t indexCount = prim->indices->count;
        IndexBuffer::IndexType indexType = computeIndexType(prim->indices);

        IndexBuffer* ib = IndexBuffer::Builder()
            .indexCount(indexCount)
            .bufferType(indexType)
            .build(*mEngine);

        asset->mIndexBuffers.push_back(ib);
        asset->mMeshCache[meshIndex][primIndex].indices = ib;
    }
}
```

**生命周期**:
- 创建: Phase 1 第一次遍历
- 数据上传: Phase 2 `uploadBuffers()`
- 持有: `asset->mIndexBuffers[]`
- 共享: 多个实例复用
- 销毁: Asset 析构时

---

### 6. BufferObject

**创建时机**: Phase 2 上传数据时

**创建位置**:
```cpp
void uploadBuffers(FFilamentAsset* asset, Engine* engine) {
    for (BufferSlot& slot : asset->mResourceInfo.slots) {
        if (slot.vertices) {
            const uint8_t* data = getAccessorData(slot.accessor);
            size_t dataSize = slot.accessor->count * getAccessorStride(slot.accessor);

            BufferObject* bo = BufferObject::Builder()
                .size(dataSize)
                .build(*engine);

            bo->setBuffer(*engine, BufferDescriptor(
                data, dataSize,
                [](void* buffer, size_t size, void* user) {
                    // 释放 CPU 内存
                }
            ));

            slot.vertices->setBufferObjectAt(*engine, slot.bufferIndex, bo);

            // BufferObject 由 VertexBuffer 持有
            asset->mBufferObjects.push_back(bo);
        }
    }
}
```

**生命周期**:
- 创建: Phase 2 `uploadBuffers()`
- 持有: `asset->mBufferObjects[]` (实际由 VB/IB 引用)
- 销毁: Asset 析构时

**特点**:
- CPU 数据通过回调异步释放
- GPU 数据在 BufferObject 销毁时释放

---

### 7. MaterialInstance

**创建时机**: Renderable 创建时,每个 primitive 一个

**创建位置**:
```cpp
void FAssetLoader::createRenderable(
    FFilamentAsset* asset,
    FFilamentInstance* instance,
    Entity entity,
    cgltf_node* node)
{
    RenderableManager::Builder builder(primitiveCount);

    for (size_t i = 0; i < primitiveCount; i++) {
        cgltf_primitive* prim = &node->mesh->primitives[i];

        // 创建材质实例
        MaterialInstance* mi = mMaterialInstanceCache.getOrCreateInstance(
            prim->material,
            computeUvMap(prim),
            /* ... */
        );

        instance->mMaterialInstances.push_back(mi);
        builder.material(i, mi);
    }

    builder.build(*mEngine, entity);
}
```

**生命周期**:
- 创建: Phase 1 Renderable 创建时
- 纹理绑定: Phase 2 `createTextures()`
- 持有: `instance->mMaterialInstances[]`
- 独立: 每个 Instance 有独立的 MaterialInstance
- 销毁: Instance 析构时

**特点**:
- 同一材质可能创建多个 MaterialInstance (不同 UV 映射)
- MaterialInstanceCache 复用相同配置的实例

---

### 8. Texture

**创建时机**: Phase 2 资源加载时,异步创建

**创建位置**:
```cpp
void createTextures(FFilamentAsset* asset, bool async) {
    for (size_t i = 0; i < gltf->textures_count; i++) {
        cgltf_texture* srcTexture = &gltf->textures[i];

        TextureProvider* provider = getTextureProvider(srcTexture);

        provider->pushTexture(imageData, imageSize, mimeType, flags,
            [asset, i, slot](Texture* texture) {
                // 异步回调
                asset->mTextures[i].texture = texture;
                asset->applyTextureBinding(i, slot);
            }
        );
    }
}

// TextureProvider 内部 (异步)
JobSystem::run([imageData, callback]() {
    // 解码图像
    uint8_t* pixels = stbi_load_from_memory(imageData, ...);

    // 创建 Texture
    Texture* texture = Texture::Builder()
        .width(width)
        .height(height)
        .format(TextureFormat::RGBA8)
        .build(*engine);

    // 上传数据
    texture->setImage(*engine, 0, PixelBufferDescriptor(pixels, ...));

    // 回调
    callback(texture);
});
```

**生命周期**:
- 创建: Phase 2 `createTextures()` (异步)
- 持有: `asset->mTextures[].texture`
- 共享: 同一纹理被多个 MaterialInstance 引用
- 销毁: Asset 析构时

**缓存**:
- BufferTextureCache: buffer_view 相同的纹理
- FilepathTextureCache: URI 相同的纹理

---

### 9. FilamentInstance

**创建时机**: `createInstance()` 或 `createInstancedAsset()` 时

**创建位置**:
```cpp
FFilamentInstance* FAssetLoader::createInstance(FFilamentAsset* asset) {
    // 创建实例根节点
    Entity root = mEntityManager.create();
    mTransformManager.create(root);

    // 创建实例对象
    FFilamentInstance* instance = new FFilamentInstance(root, asset);

    // 递归创建所有节点
    recurseEntities(asset, instance, root, asset->mRootNodes);

    // 导入蒙皮
    importSkins(instance);

    // 尝试创建 Animator (Phase 1 时失败)
    instance->createAnimator();

    // 添加到 Asset
    asset->mInstances.push_back(instance);

    return instance;
}
```

**生命周期**:
- 创建: `createInstance()`
- 持有: `asset->mInstances[]`
- 销毁: Asset 析构时 (或 `destroyInstance()`)

**独立状态**:
- 独立的 Entity 列表
- 独立的 MaterialInstance 列表
- 独立的 Animator
- 独立的 Skin joints

**共享资源**:
- 共享 VertexBuffer / IndexBuffer
- 共享 Texture
- 共享逆绑定矩阵

---

### 10. Animator

**创建时机**: Phase 2 资源加载完成后

**创建位置**:
```cpp
// Phase 2
void ResourceLoader::loadResources(FFilamentAsset* asset) {
    // ... 加载资源 ...

    asset->mResourcesLoaded = true;

    // 为所有实例创建 Animator
    for (FFilamentInstance* instance : asset->mInstances) {
        instance->createAnimator();
    }
}

// Instance
Animator* FFilamentInstance::createAnimator() {
    if (!mOwner->mResourcesLoaded) {
        return nullptr;  // Phase 1 时失败
    }

    if (mAnimator) {
        return mAnimator;  // 已创建
    }

    mAnimator = new AnimatorImpl(mOwner, this);

    // 添加动画通道
    for (cgltf_animation* anim : mOwner->mSourceAsset->animations) {
        mAnimator->addAnimation(anim);
    }

    return mAnimator;
}
```

**生命周期**:
- 创建: Phase 2 `instance->createAnimator()`
- 持有: `instance->mAnimator`
- 独立: 每个 Instance 独立
- 销毁: Instance 析构时

**依赖**:
- 需要 `mResourcesLoaded = true`
- 需要 cgltf_data 中的动画数据

---

### 11. Skin 数据

#### Asset Skin (共享)

**创建时机**: Phase 2 资源加载时

**创建位置**:
```cpp
void createSkins(FFilamentAsset* asset) {
    for (size_t i = 0; i < gltf->skins_count; i++) {
        cgltf_skin* srcSkin = &gltf->skins[i];

        // 读取逆绑定矩阵
        const cgltf_accessor* ibmAccessor = srcSkin->inverse_bind_matrices;
        const mat4f* ibmData = (mat4f*)getAccessorData(ibmAccessor);

        // 复制到 Asset
        FFilamentAsset::Skin& skin = asset->mSkins[i];
        skin.name = srcSkin->name ? srcSkin->name : "";
        skin.inverseBindMatrices.resize(srcSkin->joints_count);
        memcpy(skin.inverseBindMatrices.data(), ibmData,
               sizeof(mat4f) * srcSkin->joints_count);
    }
}
```

**生命周期**:
- 创建: Phase 2 `createSkins()`
- 持有: `asset->mSkins[]`
- 共享: 所有 Instance 共享逆绑定矩阵
- 销毁: Asset 析构时

#### Instance Skin (独立)

**创建时机**: Phase 1 Instance 创建时

**创建位置**:
```cpp
void FAssetLoader::importSkins(FFilamentInstance* instance) {
    cgltf_data* gltf = instance->mOwner->mSourceAsset->hierarchy;

    for (size_t i = 0; i < gltf->skins_count; i++) {
        cgltf_skin* srcSkin = &gltf->skins[i];

        FFilamentInstance::Skin& skin = instance->mSkins[i];

        // 填充关节 Entity
        skin.joints.resize(srcSkin->joints_count);
        for (size_t j = 0; j < srcSkin->joints_count; j++) {
            size_t nodeIndex = getNodeIndex(srcSkin->joints[j]);
            skin.joints[j] = instance->mNodeMap[nodeIndex];
        }

        // 初始化蒙皮目标
        skin.targets.clear();
    }
}
```

**生命周期**:
- 创建: Phase 1 `importSkins()`
- 持有: `instance->mSkins[]`
- 独立: 每个 Instance 独立的关节列表
- 销毁: Instance 析构时

---

## 创建顺序时间线

```
时间轴: createAsset() → loadResources() → 渲染循环
        │                │                │
        Phase 1          Phase 2          Phase 3

Phase 1 (解析和实体创建):
├─ t1: FFilamentAsset 创建
├─ t2: Entity (根) 创建
├─ t3: [第一次遍历] VertexBuffer 创建 (空)
├─ t4: [第一次遍历] IndexBuffer 创建 (空)
├─ t5: [第二次遍历] FFilamentInstance 创建
├─ t6: [第二次遍历] Entity (节点) 创建
├─ t7: [第二次遍历] TransformManager 组件创建
├─ t8: [第二次遍历] RenderableManager 组件创建
├─ t9: [第二次遍历] MaterialInstance 创建
├─ t10: [第二次遍历] Instance Skin 创建
└─ t11: Animator 创建失败 (mResourcesLoaded=false)

Phase 2 (资源加载):
├─ t12: 加载 buffer 数据到 cgltf_data
├─ t13: BufferObject 创建
├─ t14: 数据上传到 VertexBuffer/IndexBuffer
├─ t15: MorphTargetBuffer 创建 (如果有)
├─ t16: 切线计算 (并行)
├─ t17: Asset Skin 创建 (逆绑定矩阵)
├─ t18: Texture 异步创建开始
├─ t19: mResourcesLoaded = true
├─ t20: Animator 创建成功
└─ t21: Texture 异步创建完成,绑定到 MaterialInstance

Phase 3 (渲染使用):
├─ applyAnimation(): 更新 Transform
└─ updateBoneMatrices(): 计算骨骼矩阵
```

---

## 拥有关系图

```
FFilamentAsset (root)
├─ mRoot (Entity)
├─ mVertexBuffers[]
├─ mIndexBuffers[]
├─ mBufferObjects[]
├─ mTextures[]
├─ mSkins[] (逆绑定矩阵)
└─ mInstances[]
    └─ FFilamentInstance
        ├─ mRoot (Entity, instance root)
        ├─ mEntities[] (所有节点)
        │   ├─ TransformManager 组件
        │   ├─ RenderableManager 组件 (可选)
        │   ├─ LightManager 组件 (可选)
        │   └─ CameraManager 组件 (可选)
        ├─ mMaterialInstances[]
        ├─ mSkins[] (关节 Entity 列表)
        └─ mAnimator
            └─ 动画通道数据
```

---

## 常见问题

### Q1: 为什么 VertexBuffer 在 Phase 1 创建但 Phase 2 才有数据?

**A**: 两次遍历的职责分离
- Phase 1: 创建对象结构 (Entity, 组件, Buffer)
- Phase 2: 填充数据 (加载外部资源, 上传 GPU)

这样可以:
- Phase 1 后立即显示场景结构 (虽然没纹理)
- Phase 2 异步加载,不阻塞主线程
- 支持渐进式加载

### Q2: MaterialInstance 为什么在每个 Instance 中独立?

**A**: 支持独立的材质参数
- 不同实例可能需要不同的颜色/参数
- 材质变体 (Material Variant) 需要切换
- 避免实例间相互影响

### Q3: Animator 为什么延迟到 Phase 2 创建?

**A**: 依赖资源数据
- Animator 需要读取 cgltf_data 中的动画数据
- 如果 Phase 1 后调用 `releaseSourceData()`,数据将被释放
- 必须在资源加载完成,确认不会释放源数据后创建

### Q4: Texture 创建是否会阻塞?

**A**: 不会 (异步)
- TextureProvider 在 JobSystem 中异步解码
- 主线程继续执行,不阻塞
- 解码完成后通过回调绑定到 MaterialInstance
- DependencyGraph 追踪纹理加载状态,支持渐进式显示
