# 加载流程三个阶段

本文档详细说明 gltfio 从加载文件到渲染使用的三个主要阶段。

---

## 概述

gltfio 的加载流程分为三个清晰的阶段:

```
Phase 1: 解析 glTF 文件          (AssetLoader::createAsset)
    ↓
Phase 2: 加载资源到 GPU          (ResourceLoader::loadResources)
    ↓
Phase 3: 渲染循环使用             (Animator + Renderer)
```

每个阶段都有明确的输入输出和职责边界。

---

## Phase 1: 解析 glTF 文件

**入口**: `AssetLoader::createAsset(bytes, numBytes)`

**目标**:
- 解析 glTF/GLB 文件结构
- 创建 Filament Entity 和组件
- **不上传任何数据到 GPU** (只创建空对象)

### 1.1 解析文件

```cpp
// 调用 cgltf 库解析
cgltf_options options = {};
cgltf_data* srcAsset = nullptr;
cgltf_result result = cgltf_parse(&options, bytes, numBytes, &srcAsset);
```

**输出**: `cgltf_data` 树状结构
- scenes: 场景列表
- nodes: 节点层级
- meshes: 网格定义
- materials: 材质定义
- animations: 动画数据
- skins: 蒙皮数据

### 1.2 创建根资产

```cpp
FFilamentAsset* asset = new FFilamentAsset(engine, ...);

// 创建根 Entity
asset->mRoot = entityManager.create();
transformManager.create(asset->mRoot);

// 构建场景根节点映射
for (size_t i = 0; i < srcAsset->scenes_count; i++) {
    cgltf_scene* scene = &srcAsset->scenes[i];
    for (size_t j = 0; j < scene->nodes_count; j++) {
        cgltf_node* node = scene->nodes[j];
        asset->mRootNodes[node] = SceneMask(1 << i);
    }
}
```

### 1.3 第一次遍历: 创建 Primitives

**目的**: 创建 VertexBuffer 和 IndexBuffer (空的,等待数据)

```cpp
void FAssetLoader::recursePrimitives(FFilamentAsset* asset) {
    // 遍历所有节点
    for (cgltf_node* node : allNodes) {
        if (node->mesh) {
            createPrimitives(asset, node->mesh);
        }
    }
}

void FAssetLoader::createPrimitives(FFilamentAsset* asset, cgltf_mesh* mesh) {
    // 为每个 primitive 创建 VB/IB
    for (size_t i = 0; i < mesh->primitives_count; i++) {
        cgltf_primitive* prim = &mesh->primitives[i];

        // 创建空 VertexBuffer
        VertexBuffer* vb = VertexBuffer::Builder()
            .vertexCount(vertexCount)
            .bufferCount(bufferCount)
            .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3)
            .attribute(VertexAttribute::NORMAL, 1, VertexBuffer::AttributeType::FLOAT3)
            // ...
            .build(*engine);

        // 创建空 IndexBuffer
        IndexBuffer* ib = IndexBuffer::Builder()
            .indexCount(indexCount)
            .bufferType(IndexBuffer::IndexType::UINT)
            .build(*engine);

        // 记录 BufferSlot 信息 (稍后填充数据)
        asset->mResourceInfo.slots.push_back({
            .vertices = vb,
            .indices = ib,
            .accessor = prim->indices,
            // ...
        });

        // 保存到 MeshCache
        asset->mMeshCache[meshIndex][primIndex] = {vb, ib, aabb, ...};
    }
}
```

**关键点**:
- ✅ VertexBuffer/IndexBuffer 已创建
- ❌ 但还没有上传数据 (空对象)
- ✅ BufferSlot 记录了待填充的位置

### 1.4 第二次遍历: 创建 Entities

**目的**: 为每个 glTF 节点创建 Entity 和组件

```cpp
void FAssetLoader::createInstances(FFilamentAsset* asset, size_t numInstances) {
    for (size_t i = 0; i < numInstances; i++) {
        FFilamentInstance* instance = createInstance(asset);
        asset->mInstances.push_back(instance);
    }
}

FFilamentInstance* FAssetLoader::createInstance(FFilamentAsset* asset) {
    // 1. 创建实例根节点
    Entity root = entityManager.create();
    transformManager.create(root);

    FFilamentInstance* instance = new FFilamentInstance(root, asset);

    // 2. 递归创建所有节点
    recurseEntities(asset, instance, root, asset->mRootNodes);

    // 3. 导入蒙皮信息
    importSkins(instance);

    // 4. 尝试创建 Animator (此时会失败,返回 nullptr)
    instance->createAnimator();  // mResourcesLoaded=false → 返回 nullptr

    return instance;
}
```

#### 递归创建节点

```cpp
void FAssetLoader::recurseEntities(
    FFilamentAsset* asset,
    FFilamentInstance* instance,
    Entity parent,
    cgltf_node* node)
{
    // 创建 Entity
    Entity entity = entityManager.create();
    instance->mEntities.push_back(entity);
    instance->mNodeMap[nodeIndex] = entity;

    // 创建 Transform 组件
    TransformManager::Instance tmi = transformManager.create(entity);
    transformManager.setParent(tmi, parent);
    transformManager.setTransform(tmi, localMatrix);

    // 如果有 mesh,创建 Renderable 组件
    if (node->mesh) {
        createRenderable(asset, instance, entity, node);
    }

    // 如果有 light,创建 Light 组件
    if (node->light) {
        createLight(asset, entity, node->light);
    }

    // 如果有 camera,创建 Camera 组件
    if (node->camera) {
        createCamera(asset, entity, node->camera);
    }

    // 添加 Name 组件
    if (node->name) {
        nameManager->addComponent(entity);
        nameManager->setName(entity, node->name);
    }

    // 递归子节点
    for (size_t i = 0; i < node->children_count; i++) {
        recurseEntities(asset, instance, entity, node->children[i]);
    }
}
```

#### 创建 Renderable

```cpp
void FAssetLoader::createRenderable(
    FFilamentAsset* asset,
    FFilamentInstance* instance,
    Entity entity,
    cgltf_node* node)
{
    cgltf_mesh* mesh = node->mesh;

    RenderableManager::Builder builder(mesh->primitives_count);

    for (size_t i = 0; i < mesh->primitives_count; i++) {
        cgltf_primitive* prim = &mesh->primitives[i];

        // 从 MeshCache 获取 VB/IB
        Primitive& primData = asset->mMeshCache[meshIndex][i];

        // 创建材质实例
        MaterialInstance* mi = createMaterialInstance(prim->material);
        instance->mMaterialInstances.push_back(mi);

        // 配置 Renderable
        builder.geometry(i,
            RenderableManager::PrimitiveType::TRIANGLES,
            primData.vertices,
            primData.indices);
        builder.material(i, mi);
        builder.boundingBox(primData.aabb);

        // 如果有蒙皮
        if (node->skin) {
            builder.skinning(node->skin->joints_count);
        }

        // 如果有变形目标
        if (prim->targets_count > 0) {
            builder.morphing(prim->targets_count);
        }
    }

    // 构建 Renderable 组件
    builder.build(*engine, entity);
}
```

### 1.5 Phase 1 输出

**已创建**:
- ✅ FFilamentAsset 对象
- ✅ VertexBuffer/IndexBuffer (空,无数据)
- ✅ Entity 层级结构
- ✅ TransformManager 组件
- ✅ RenderableManager 组件
- ✅ LightManager 组件
- ✅ CameraManager 组件
- ✅ MaterialInstance 对象

**未创建**:
- ❌ Texture (需要解码图像)
- ❌ Animator (mResourcesLoaded=false)
- ❌ 蒙皮矩阵 (需要骨骼数据)

**未上传**:
- ❌ 顶点数据 (在 cgltf_data 中,还未上传)
- ❌ 索引数据 (在 cgltf_data 中,还未上传)
- ❌ 纹理数据 (还未解码)

---

## Phase 2: 加载资源到 GPU

**入口**: `ResourceLoader::loadResources(asset)`

**目标**:
- 加载外部资源 (buffer, 纹理文件)
- 上传数据到 GPU
- 创建 Texture 对象
- 创建 Animator 对象

### 2.1 加载 Buffer 数据

```cpp
// 从文件或 URI 缓存加载所有 buffer
utility::loadCgltfBuffers(srcAsset, gltfPath, uriDataCache);

// 此时 cgltf_buffer->data 已填充
for (size_t i = 0; i < srcAsset->buffers_count; i++) {
    cgltf_buffer* buf = &srcAsset->buffers[i];
    assert(buf->data != nullptr);  // 已加载
}
```

### 2.2 解压缩网格

```cpp
// Draco 压缩
utility::decodeDracoMeshes(srcAsset, asset->mDracoCache);

// Meshopt 压缩
utility::decodeMeshoptCompression(srcAsset);
```

### 2.3 上传 Buffer 数据到 GPU

```cpp
void uploadBuffers(FFilamentAsset* asset) {
    // 遍历所有 BufferSlot
    for (BufferSlot& slot : asset->mResourceInfo.slots) {
        if (slot.vertices) {
            // 获取数据指针
            const cgltf_accessor* accessor = slot.accessor;
            const uint8_t* data = getAccessorData(accessor);

            // 创建 BufferObject
            BufferObject* bo = BufferObject::Builder()
                .size(dataSize)
                .build(*engine);

            // 上传数据
            bo->setBuffer(*engine, BufferDescriptor(
                data, dataSize,
                [](void* buffer, size_t size, void* user) {
                    // 回调释放 CPU 内存
                }
            ));

            // 设置到 VertexBuffer
            slot.vertices->setBufferObjectAt(*engine, slot.bufferIndex, bo);
        }

        if (slot.indices) {
            // 类似上传索引数据
            const uint8_t* indexData = getAccessorData(slot.accessor);
            slot.indices->setBuffer(*engine, BufferDescriptor(indexData, ...));
        }
    }
}
```

**关键点**:
- ✅ 数据已上传到 GPU
- ✅ CPU 内存通过回调异步释放
- ✅ VertexBuffer/IndexBuffer 现在有数据了

### 2.4 计算切线数据

```cpp
void computeTangents(FFilamentAsset* asset) {
    JobSystem* js = engine->getJobSystem();

    // 并行计算切线四元数
    for (auto& [vb, prim] : primitivesNeedingTangents) {
        Job* job = jobs::create(js, nullptr, [vb, prim]() {
            // 计算切线四元数
            quatf* tangents = computeTangentQuaternions(
                positions, normals, uvs, triangles);

            // 上传到 GPU
            vb->setBufferObjectAt(*engine, TANGENT_SLOT,
                BufferObject::Builder()
                    .size(tangentSize)
                    .build(*engine)
            );
        });
        js->run(job);
    }

    // 等待所有任务完成
    js->waitAndRelease(parent);
}
```

### 2.5 创建蒙皮数据

```cpp
void createSkins(FFilamentAsset* asset) {
    for (size_t i = 0; i < srcAsset->skins_count; i++) {
        cgltf_skin* srcSkin = &srcAsset->skins[i];

        // 读取逆绑定矩阵
        const cgltf_accessor* ibmAccessor = srcSkin->inverse_bind_matrices;
        const mat4f* ibmData = (const mat4f*)getAccessorData(ibmAccessor);

        // 复制到 Asset
        Skin& skin = asset->mSkins[i];
        skin.name = srcSkin->name ? srcSkin->name : "";
        skin.inverseBindMatrices.resize(srcSkin->joints_count);
        memcpy(skin.inverseBindMatrices.data(), ibmData, ...);
    }
}
```

### 2.6 创建纹理

```cpp
void createTextures(FFilamentAsset* asset, bool async) {
    for (size_t i = 0; i < srcAsset->textures_count; i++) {
        cgltf_texture* srcTexture = &srcAsset->textures[i];

        // 查找或创建 Texture
        Texture* texture = getOrCreateTexture(asset, srcTexture);

        if (texture == NOT_READY) {
            // 异步加载中,注册依赖
            asset->mDependencyGraph.addEdge(...);
        } else {
            // Texture 已准备好,绑定到材质
            asset->applyTextureBinding(i, textureSlot);
        }
    }
}

Texture* getOrCreateTexture(FFilamentAsset* asset, cgltf_texture* srcTexture) {
    // 从缓存查找
    if (cachedTexture) return cachedTexture;

    // 获取图像数据
    cgltf_image* image = srcTexture->image;
    const uint8_t* imageData = nullptr;
    size_t imageSize = 0;

    if (image->buffer_view) {
        // 嵌入数据
        imageData = getBufferViewData(image->buffer_view);
        imageSize = image->buffer_view->size;
    } else if (image->uri) {
        // 外部文件
        imageData = uriDataCache->get(image->uri);
        imageSize = uriDataCache->getSize(image->uri);
    }

    // 获取 TextureProvider (按 MIME 类型)
    const char* mimeType = image->mime_type;
    TextureProvider* provider = mTextureProviders[mimeType];

    // 调用 TextureProvider 解码
    TextureProvider::FlagBits flags = computeTextureFlags(srcTexture);
    provider->pushTexture(imageData, imageSize, mimeType, flags,
        [](Texture* texture) {
            // 解码完成回调
            asset->applyTextureBinding(texIndex, textureSlot);
            asset->mDependencyGraph.commitEdges();
        }
    );

    return NOT_READY;  // 异步解码中
}
```

**纹理加载流程**:
1. 从 buffer_view 或 URI 获取图像数据
2. 根据 MIME 类型选择 TextureProvider
3. 异步解码图像 (JobSystem)
4. 创建 Filament::Texture
5. 回调绑定到 MaterialInstance

### 2.7 创建 Animator

```cpp
// 标记资源已加载
asset->mResourcesLoaded = true;

// 为所有实例创建 Animator
for (FFilamentInstance* instance : asset->mInstances) {
    instance->createAnimator();
}

// FFilamentInstance::createAnimator()
void FFilamentInstance::createAnimator() {
    if (!mOwner->mResourcesLoaded) {
        return nullptr;  // Phase 1 时返回 nullptr
    }

    if (mAnimator) {
        return mAnimator;  // 已创建
    }

    // 创建 Animator
    mAnimator = new AnimatorImpl(mOwner, this);

    // 添加动画通道
    for (size_t i = 0; i < srcAsset->animations_count; i++) {
        mAnimator->addAnimation(srcAsset->animations[i]);
    }

    return mAnimator;
}
```

### 2.8 Phase 2 输出

**已创建**:
- ✅ Texture 对象
- ✅ Animator 对象
- ✅ 蒙皮逆绑定矩阵

**已上传**:
- ✅ 顶点数据到 GPU
- ✅ 索引数据到 GPU
- ✅ 切线数据到 GPU
- ✅ 纹理数据到 GPU

**资源状态**:
- ✅ mResourcesLoaded = true
- ✅ 所有材质已绑定纹理
- ✅ Animator 可用

---

## Phase 3: 渲染循环使用

**入口**: 用户渲染循环

**目标**:
- 应用动画
- 计算蒙皮矩阵
- 渲染场景

### 3.1 应用动画

```cpp
// 每帧更新
float time = currentTime - startTime;

Animator* animator = instance->getAnimator();
animator->applyAnimation(0, time);  // 播放第 0 个动画
```

**内部流程**:

```cpp
void Animator::applyAnimation(size_t animIndex, float time) {
    Animation& anim = mAnimations[animIndex];

    // 打开变换事务 (批量更新)
    trsTransformManager->openLocalTransformTransaction();

    // 遍历所有动画通道
    for (Channel& channel : anim.channels) {
        Entity entity = mInstance->mNodeMap[channel.nodeIndex];

        // 查找关键帧
        size_t frame0 = findKeyframe(channel.times, time);
        size_t frame1 = frame0 + 1;

        // 计算插值因子
        float t = (time - channel.times[frame0]) /
                  (channel.times[frame1] - channel.times[frame0]);

        // 根据通道类型插值
        switch (channel.transformType) {
            case TransformType::TRANSLATION: {
                float3 v0 = channel.values[frame0];
                float3 v1 = channel.values[frame1];
                float3 value = mix(v0, v1, t);
                trsTransformManager->setTranslation(entity, value);
                break;
            }
            case TransformType::ROTATION: {
                quatf q0 = channel.values[frame0];
                quatf q1 = channel.values[frame1];
                quatf value = slerp(q0, q1, t);  // 球面线性插值
                trsTransformManager->setRotation(entity, value);
                break;
            }
            case TransformType::SCALE: {
                float3 s0 = channel.values[frame0];
                float3 s1 = channel.values[frame1];
                float3 value = mix(s0, s1, t);
                trsTransformManager->setScale(entity, value);
                break;
            }
            case TransformType::WEIGHTS: {
                // 变形目标权重
                float* weights = interpolateMorphWeights(channel, t);
                renderableManager->setMorphWeights(entity, weights, count);
                break;
            }
        }
    }

    // 提交变换事务
    trsTransformManager->commitLocalTransformTransaction();

    // 同步到 TransformManager
    trsTransformManager->updateTransformMatrices(transformManager);
}
```

**关键点**:
- ✅ 批量更新 Transform (开/提交事务)
- ✅ 使用正确的插值方法 (线性/球面/三次)
- ✅ 支持多种变换类型 (T/R/S/Weights)

### 3.2 更新蒙皮矩阵

```cpp
// 每帧调用
animator->updateBoneMatrices();
```

**内部流程**:

```cpp
void Animator::updateBoneMatrices() {
    // 遍历所有蒙皮
    for (size_t skinIndex = 0; skinIndex < mInstance->mSkins.size(); skinIndex++) {
        Skin& skin = mInstance->mSkins[skinIndex];

        // 遍历受影响的网格
        for (Entity meshEntity : skin.targets) {
            // 获取网格的逆世界矩阵
            mat4f meshInverseGlobal = inverse(
                transformManager->getWorldTransform(meshEntity)
            );

            // 计算骨骼矩阵
            mat4f* boneMatrices = new mat4f[skin.joints.size()];

            for (size_t i = 0; i < skin.joints.size(); i++) {
                Entity joint = skin.joints[i];

                // 骨骼世界矩阵
                mat4f jointGlobal = transformManager->getWorldTransform(joint);

                // 逆绑定矩阵
                mat4f inverseBindMatrix = mAsset->mSkins[skinIndex]
                    .inverseBindMatrices[i];

                // 计算最终骨骼矩阵
                boneMatrices[i] = meshInverseGlobal * jointGlobal * inverseBindMatrix;
            }

            // 设置到 RenderableManager
            renderableManager->setBones(
                renderableManager->getInstance(meshEntity),
                boneMatrices,
                skin.joints.size()
            );

            delete[] boneMatrices;
        }
    }
}
```

**蒙皮矩阵计算公式**:
```
BoneMatrix[i] = MeshInverseGlobal × JointGlobal × InverseBindMatrix[i]
```

**作用**:
- 将顶点从网格空间变换到骨骼空间
- GPU 使用这些矩阵进行顶点蒙皮

### 3.3 渲染

```cpp
// 渲染循环
if (renderer->beginFrame(swapChain)) {
    renderer->render(view);
    renderer->endFrame();
}
```

**GPU 端蒙皮**:

```glsl
// 顶点着色器
attribute vec4 boneIndices;
attribute vec4 boneWeights;
uniform mat4 boneMatrices[MAX_BONES];

void main() {
    // 蒙皮变换
    mat4 skinMatrix =
        boneWeights.x * boneMatrices[int(boneIndices.x)] +
        boneWeights.y * boneMatrices[int(boneIndices.y)] +
        boneWeights.z * boneMatrices[int(boneIndices.z)] +
        boneWeights.w * boneMatrices[int(boneIndices.w)];

    vec4 skinnedPosition = skinMatrix * vec4(position, 1.0);
    vec3 skinnedNormal = mat3(skinMatrix) * normal;

    gl_Position = viewProjection * model * skinnedPosition;
}
```

### 3.4 Phase 3 流程

```
每帧:
1. animator->applyAnimation(index, time)
   └─> 更新 TransformManager (T/R/S)

2. animator->updateBoneMatrices()
   └─> 计算骨骼矩阵,设置到 RenderableManager

3. renderer->render(view)
   └─> GPU 使用骨骼矩阵进行顶点蒙皮
```

---

## 三个阶段对比

| 阶段 | 入口 | 主要工作 | GPU 上传 | 输出 |
|-----|------|---------|---------|------|
| **Phase 1** | createAsset | 解析文件,创建实体 | ❌ 否 | Asset + Instance |
| **Phase 2** | loadResources | 加载资源,上传 GPU | ✅ 是 | Texture + Animator |
| **Phase 3** | 渲染循环 | 应用动画,渲染 | ❌ 否 | 渲染帧 |

---

## 关键时间点

| 时间点 | 事件 | 状态变化 |
|-------|-----|---------|
| `createAsset()` 开始 | 解析 glTF 文件 | cgltf_data 创建 |
| `createPrimitives()` | 创建 VB/IB | 空 Buffer 对象 |
| `recurseEntities()` | 创建 Entity | TransformManager + RenderableManager |
| `createInstance()` 结束 | 第一次尝试创建 Animator | 返回 nullptr |
| `loadResources()` 开始 | 加载 buffer 数据 | cgltf_buffer->data 填充 |
| `uploadBuffers()` | 上传到 GPU | VB/IB 有数据 |
| `createTextures()` | 创建纹理 | Texture 对象创建 |
| `mResourcesLoaded=true` | 标记资源已加载 | - |
| `createAnimator()` | 创建 Animator | Animator 可用 |
| `loadResources()` 结束 | - | 资源完全就绪 |
| 渲染循环 | 应用动画 | Transform 更新 |
| 渲染循环 | 更新骨骼 | 骨骼矩阵计算 |
| 渲染循环 | 渲染 | GPU 渲染 |

---

## 常见问题

### Q1: 为什么要两次遍历?

**A**: 职责分离
- 第一次: 创建共享资源 (VB/IB) - 多个实例复用
- 第二次: 创建实例数据 (Entity) - 每个实例独立

### Q2: 为什么 Animator 延迟创建?

**A**: 依赖资源数据
- Animator 需要访问动画数据 (在 cgltf_data 中)
- Phase 1 后可能调用 `releaseSourceData()` 释放
- 必须在资源加载完成后创建

### Q3: 纹理可以异步加载吗?

**A**: 可以
- `asyncBeginLoad()` 开始异步加载
- `asyncUpdateLoad()` 轮询进度
- DependencyGraph 追踪纹理加载状态
- 支持渐进式显示 (纹理逐步就绪)

### Q4: 如何优化加载性能?

**A**:
1. 使用 Draco/Meshopt 压缩减少传输
2. 使用 KTX2 纹理格式 (GPU 压缩)
3. 异步加载纹理
4. 并行计算切线 (JobSystem)
5. 缓存材质实例 (跨资产复用)
