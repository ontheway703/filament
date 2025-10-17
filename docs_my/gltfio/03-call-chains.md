# 关键调用链详解

本文档详细展示 gltfio 三个关键流程的完整调用链。

---

## 1. 文件加载到实例创建的调用链

### 用户代码

```cpp
AssetLoader* loader = AssetLoader::create({engine, materials});
FilamentAsset* asset = loader->createAsset(glbBytes, glbSize);
```

### 完整调用栈

```
AssetLoader::createAsset(bytes, numBytes)
│
├─> FAssetLoader::createAsset(bytes, numBytes)
│   │
│   └─> FAssetLoader::createInstancedAsset(bytes, numBytes, instances, 1)
│       │
│       ├─> [1] 解析 glTF 文件
│       │   └─> cgltf_parse(&options, bytes, numBytes, &srcAsset)
│       │       └─> 返回 cgltf_data*
│       │
│       ├─> [2] 创建根资产
│       │   └─> FAssetLoader::createRootAsset(srcAsset)
│       │       │
│       │       ├─> new FFilamentAsset(engine, ...)
│       │       │
│       │       ├─> mEntityManager.create()  // 创建 mRoot
│       │       │   └─> asset->mRoot = entity
│       │       │
│       │       ├─> mTransformManager.create(asset->mRoot)
│       │       │
│       │       ├─> 构建场景根节点映射
│       │       │   └─> for each scene.nodes:
│       │       │       └─> asset->mRootNodes[node] = SceneMask(1 << sceneIndex)
│       │       │
│       │       ├─> [第一次遍历] 创建 Primitives
│       │       │   └─> FAssetLoader::recursePrimitives(asset)
│       │       │       │
│       │       │       └─> for each node in hierarchy:
│       │       │           │
│       │       │           └─> if (node->mesh):
│       │       │               └─> FAssetLoader::createPrimitives(asset, mesh)
│       │       │                   │
│       │       │                   └─> for each primitive in mesh:
│       │       │                       └─> FAssetLoader::createPrimitive(...)
│       │       │                           │
│       │       │                           ├─> VertexBuffer::Builder()
│       │       │                           │   .vertexCount(...)
│       │       │                           │   .attribute(POSITION, ...)
│       │       │                           │   .attribute(NORMAL, ...)
│       │       │                           │   .build(*engine)
│       │       │                           │   └─> vb (空,无数据)
│       │       │                           │
│       │       │                           ├─> IndexBuffer::Builder()
│       │       │                           │   .indexCount(...)
│       │       │                           │   .build(*engine)
│       │       │                           │   └─> ib (空,无数据)
│       │       │                           │
│       │       │                           ├─> 记录 BufferSlot
│       │       │                           │   └─> asset->mResourceInfo.slots.push_back({
│       │       │                           │       .vertices = vb,
│       │       │                           │       .indices = ib,
│       │       │                           │       .accessor = primitive->indices,
│       │       │                           │       ...
│       │       │                           │   })
│       │       │                           │
│       │       │                           └─> 保存到 MeshCache
│       │       │                               └─> asset->mMeshCache[meshIdx][primIdx] = {
│       │       │                                   vb, ib, aabb, uvmap, ...
│       │       │                               }
│       │       │
│       │       └─> 计算包围盒
│       │           └─> asset->mBoundingBox = computeAABB(...)
│       │
│       └─> [3] 创建实例
│           └─> FAssetLoader::createInstances(asset, numInstances)
│               │
│               └─> for (i = 0; i < numInstances; i++):
│                   └─> FAssetLoader::createInstance(asset)
│                       │
│                       ├─> 创建实例根节点
│                       │   ├─> Entity root = mEntityManager.create()
│                       │   ├─> mTransformManager.create(root)
│                       │   └─> instance = new FFilamentInstance(root, asset)
│                       │
│                       ├─> [第二次遍历] 创建实体
│                       │   └─> FAssetLoader::recurseEntities(asset, instance, root, rootNodes)
│                       │       │
│                       │       └─> for each node in hierarchy:
│                       │           │
│                       │           ├─> mEntityManager.create()
│                       │           │   └─> entity
│                       │           │
│                       │           ├─> instance->mEntities.push_back(entity)
│                       │           │
│                       │           ├─> instance->mNodeMap[nodeIndex] = entity
│                       │           │
│                       │           ├─> mTransformManager.create(entity)
│                       │           │   └─> setParent(parent)
│                       │           │   └─> setTransform(localMatrix)
│                       │           │
│                       │           ├─> if (node->mesh):
│                       │           │   └─> createRenderable(asset, instance, entity, node)
│                       │           │       │
│                       │           │       ├─> 获取 VB/IB
│                       │           │       │   └─> Primitive& prim = asset->mMeshCache[meshIdx][primIdx]
│                       │           │       │
│                       │           │       ├─> 创建材质实例
│                       │           │       │   └─> mi = mMaterialInstanceCache.getOrCreateInstance(...)
│                       │           │       │       └─> instance->mMaterialInstances.push_back(mi)
│                       │           │       │
│                       │           │       └─> RenderableManager::Builder(primitiveCount)
│                       │           │           .geometry(i, TRIANGLES, vb, ib)
│                       │           │           .material(i, mi)
│                       │           │           .boundingBox(aabb)
│                       │           │           .skinning(jointCount)  // if has skin
│                       │           │           .morphing(targetCount) // if has morphs
│                       │           │           .build(*engine, entity)
│                       │           │
│                       │           ├─> if (node->light):
│                       │           │   └─> createLight(asset, entity, node->light)
│                       │           │       └─> LightManager::Builder(type)
│                       │           │           .color(...)
│                       │           │           .intensity(...)
│                       │           │           .build(*engine, entity)
│                       │           │
│                       │           ├─> if (node->camera):
│                       │           │   └─> createCamera(asset, entity, node->camera)
│                       │           │       └─> engine->createCamera(entity)
│                       │           │           └─> camera->setProjection(...)
│                       │           │
│                       │           ├─> if (node->name):
│                       │           │   └─> mNameManager->addComponent(entity)
│                       │           │       └─> mNameManager->setName(entity, node->name)
│                       │           │
│                       │           └─> 递归子节点
│                       │               └─> for each child:
│                       │                   └─> recurseEntities(asset, instance, entity, child)
│                       │
│                       ├─> 导入蒙皮
│                       │   └─> FAssetLoader::importSkins(instance)
│                       │       └─> for each skin:
│                       │           ├─> instance->mSkins[i].joints.resize(jointCount)
│                       │           └─> for each joint:
│                       │               └─> instance->mSkins[i].joints[j] =
│                       │                   instance->mNodeMap[jointNodeIndex]
│                       │
│                       ├─> 尝试创建 Animator
│                       │   └─> instance->createAnimator()
│                       │       └─> if (!mOwner->mResourcesLoaded):
│                       │           └─> return nullptr  ❌ Phase 1 失败
│                       │
│                       └─> 刷新材质缓存
│                           └─> mMaterialInstanceCache.flush(instance)
│                               └─> 保存到 instance->mMaterialInstances
│
└─> 返回 asset
```

---

## 2. 资源加载的调用链

### 用户代码

```cpp
ResourceLoader loader({engine, ".", true});
loader.addTextureProvider("image/png", pngProvider);
loader.loadResources(asset);
```

### 完整调用栈

```
ResourceLoader::loadResources(asset)
│
└─> ResourceLoader::Impl::loadResources(FFilamentAsset* asset, false)
    │
    ├─> [1] 标记资源加载开始
    │   ├─> asset->mResourcesLoaded = true
    │   └─> asset->mDependencyGraph.disableProgressiveReveal()
    │
    ├─> [2] 加载 buffer 数据
    │   └─> utility::loadCgltfBuffers(gltf, gltfPath, uriDataCache)
    │       │
    │       └─> for each buffer:
    │           │
    │           ├─> if (buffer->uri):
    │           │   │
    │           │   ├─> if (starts_with("data:")):
    │           │   │   └─> decodeBase64(uri)  // 嵌入 base64
    │           │   │
    │           │   ├─> else if (uriDataCache->contains(uri)):
    │           │   │   └─> data = uriDataCache->get(uri)  // 缓存
    │           │   │
    │           │   └─> else:
    │           │       └─> data = readFile(gltfPath + uri)  // 外部文件
    │           │           └─> buffer->data = data
    │           │
    │           └─> else:
    │               └─> // GLB 嵌入,已在 cgltf_parse 时加载
    │
    ├─> [3] 解压缩网格
    │   ├─> utility::decodeDracoMeshes(gltf, dracoCache)
    │   │   └─> for each primitive with KHR_draco_mesh_compression:
    │   │       └─> draco::Decoder::Decode(...)
    │   │           └─> 解压到临时 buffer
    │   │
    │   └─> utility::decodeMeshoptCompression(gltf)
    │       └─> for each buffer_view with EXT_meshopt_compression:
    │           └─> meshopt_decodeVertexBuffer(...)
    │
    ├─> [4] 上传 buffer 数据到 GPU
    │   └─> Impl::uploadBuffers(asset, engine, uriDataCache)
    │       │
    │       └─> for each BufferSlot in asset->mResourceInfo.slots:
    │           │
    │           ├─> if (slot.vertices):
    │           │   │
    │           │   ├─> 获取数据
    │           │   │   └─> const uint8_t* data = getAccessorData(slot.accessor)
    │           │   │
    │           │   ├─> 创建 BufferObject
    │           │   │   └─> bo = BufferObject::Builder()
    │           │   │       .size(dataSize)
    │           │   │       .build(*engine)
    │           │   │
    │           │   ├─> 上传数据
    │           │   │   └─> bo->setBuffer(*engine, BufferDescriptor(
    │           │   │       data, size,
    │           │   │       [](void* buf, size_t sz, void* user) {
    │           │   │           // 回调释放 CPU 内存
    │           │   │           free(buf);
    │           │   │       }
    │           │   │   ))
    │           │   │
    │           │   └─> 设置到 VertexBuffer
    │           │       └─> slot.vertices->setBufferObjectAt(
    │           │           *engine, slot.bufferIndex, bo)
    │           │
    │           └─> if (slot.indices):
    │               └─> (类似上传索引数据)
    │                   └─> slot.indices->setBuffer(*engine, BufferDescriptor(...))
    │
    ├─> [5] 计算切线
    │   └─> Impl::computeTangents(asset, engine)
    │       │
    │       ├─> JobSystem* js = engine->getJobSystem()
    │       │
    │       ├─> for each primitive needing tangents:
    │       │   │
    │       │   └─> Job* job = jobs::create(js, nullptr, [vb, prim]() {
    │       │       │
    │       │       ├─> 获取位置/法线/UV 数据
    │       │       │   └─> const float3* positions = ...
    │       │       │   └─> const float3* normals = ...
    │       │       │   └─> const float2* uvs = ...
    │       │       │
    │       │       ├─> 计算切线四元数
    │       │       │   └─> quatf* tangents = geometry::SurfaceOrientation::Builder()
    │       │       │       .vertexCount(vertexCount)
    │       │       │       .normals(normals)
    │       │       │       .uvs(uvs)
    │       │       │       .positions(positions)
    │       │       │       .triangles(triangles, triangleCount)
    │       │       │       .build()
    │       │       │       ->getQuaternions()
    │       │       │
    │       │       └─> 上传到 GPU
    │       │           └─> vb->setBufferObjectAt(*engine, TANGENT_SLOT,
    │       │               BufferObject::Builder()
    │       │                   .size(tangentSize)
    │       │                   .build(*engine))
    │       │   })
    │       │   └─> js->run(job)
    │       │
    │       └─> js->waitAndRelease(parentJob)
    │
    ├─> [6] 创建蒙皮数据
    │   └─> Impl::createSkins(asset)
    │       │
    │       └─> for each skin in gltf:
    │           │
    │           ├─> 读取逆绑定矩阵
    │           │   └─> const cgltf_accessor* ibmAccessor = skin->inverse_bind_matrices
    │           │   └─> const mat4f* ibmData = (mat4f*)getAccessorData(ibmAccessor)
    │           │
    │           ├─> 复制到 Asset
    │           │   └─> asset->mSkins[i].name = skin->name
    │           │   └─> asset->mSkins[i].inverseBindMatrices.resize(jointCount)
    │           │   └─> memcpy(asset->mSkins[i].inverseBindMatrices.data(),
    │           │               ibmData, sizeof(mat4f) * jointCount)
    │           │
    │           └─> 可选: 规范化蒙皮权重
    │               └─> if (config.normalizeSkinningWeights):
    │                   └─> normalizeSkinningWeights(asset, skin)
    │
    ├─> [7] 创建纹理
    │   └─> Impl::createTextures(asset, async=false)
    │       │
    │       └─> for each texture in gltf:
    │           │
    │           └─> getOrCreateTexture(asset, texture)
    │               │
    │               ├─> 查找缓存
    │               │   ├─> if (mBufferTextureCache.contains(key)):
    │               │   │   └─> return cached Texture*
    │               │   └─> if (mFilepathTextureCache.contains(key)):
    │               │       └─> return cached Texture*
    │               │
    │               ├─> 获取图像数据
    │               │   └─> cgltf_image* image = texture->image
    │               │   │
    │               │   ├─> if (image->buffer_view):
    │               │   │   └─> imageData = getBufferViewData(image->buffer_view)
    │               │   │   └─> imageSize = image->buffer_view->size
    │               │   │
    │               │   └─> else if (image->uri):
    │               │       └─> imageData = uriDataCache->get(image->uri)
    │               │       └─> imageSize = uriDataCache->getSize(image->uri)
    │               │
    │               ├─> 获取 TextureProvider
    │               │   └─> const char* mimeType = image->mime_type
    │               │   └─> provider = mTextureProviders[mimeType]
    │               │   └─> assert(provider != nullptr)
    │               │
    │               ├─> 计算纹理标志
    │               │   └─> flags = computeTextureFlags(texture)
    │               │       ├─> if (used as base color): SRGB
    │               │       ├─> if (has sampler.wrapS == REPEAT): ...
    │               │       └─> ...
    │               │
    │               ├─> 调用 TextureProvider 解码
    │               │   └─> provider->pushTexture(
    │               │       imageData, imageSize, mimeType, flags,
    │               │       [asset, texIndex, slot](Texture* texture) {
    │               │           // 解码完成回调
    │               │           asset->applyTextureBinding(texIndex, slot);
    │               │           asset->mDependencyGraph.commitEdges();
    │               │       }
    │               │   )
    │               │   │
    │               │   └─> [异步执行] TextureProvider::Impl
    │               │       │
    │               │       ├─> JobSystem::run([imageData, callback]() {
    │               │       │   │
    │               │       │   ├─> 解码图像
    │               │       │   │   └─> stbi_load_from_memory(imageData, ...)
    │               │       │   │       └─> rgba pixels
    │               │       │   │
    │               │       │   ├─> 创建 Texture
    │               │       │   │   └─> texture = Texture::Builder()
    │               │       │   │       .width(width)
    │               │       │   │       .height(height)
    │               │       │   │       .format(TextureFormat::RGBA8)
    │               │       │   │       .sampler(Texture::Sampler::SAMPLER_2D)
    │               │       │   │       .build(*engine)
    │               │       │   │
    │               │       │   ├─> 上传纹理数据
    │               │       │   │   └─> texture->setImage(*engine, 0,
    │               │       │   │       Texture::PixelBufferDescriptor(
    │               │       │   │           pixels, size, format, type,
    │               │       │   │           [](void* buf, ...) { free(buf); }
    │               │       │   │       ))
    │               │       │   │
    │               │       │   └─> 调用回调
    │               │       │       └─> callback(texture)
    │               │       │           └─> asset->applyTextureBinding(...)
    │               │       │               └─> mi->setParameter(paramName, texture, sampler)
    │               │       │
    │               │       └─> })
    │               │
    │               └─> if (async):
    │                   ├─> return NOT_READY
    │                   └─> asset->mDependencyGraph.addEdge(entity, textureIndex)
    │                   └─> else:
    │                       └─> 等待解码完成,返回 Texture*
    │
    ├─> [8] 提交依赖图
    │   └─> asset->mDependencyGraph.commitEdges()
    │
    └─> [9] 创建 Animator
        └─> for (FFilamentInstance* instance : asset->mInstances):
            └─> instance->createAnimator()
                │
                └─> FFilamentInstance::createAnimator()
                    │
                    ├─> if (!mOwner->mResourcesLoaded):
                    │   └─> return nullptr  // 不会执行
                    │
                    ├─> if (mAnimator):
                    │   └─> return mAnimator  // 已存在
                    │
                    ├─> mAnimator = new AnimatorImpl(mOwner, this)
                    │
                    └─> 添加动画通道
                        └─> for each animation in gltf:
                            └─> mAnimator->addAnimation(animation)
                                │
                                └─> AnimatorImpl::addChannels(cgltf_animation* anim)
                                    │
                                    ├─> mAnimations.push_back({
                                    │   .name = anim->name,
                                    │   .duration = computeDuration(anim),
                                    │   .channels = {}
                                    │   })
                                    │
                                    └─> for each channel in anim->channels:
                                        │
                                        ├─> 解析 Sampler
                                        │   └─> sampler = channel->sampler
                                        │   └─> times = getAccessorData(sampler->input)
                                        │   └─> values = getAccessorData(sampler->output)
                                        │
                                        └─> mAnimations[i].channels.push_back({
                                            .nodeIndex = getNodeIndex(channel->target_node),
                                            .transformType = channel->target_path,  // T/R/S/W
                                            .interpolation = sampler->interpolation,
                                            .times = times,
                                            .values = values
                                            })
```

---

## 3. 动画播放的调用链

### 用户代码

```cpp
Animator* animator = instance->getAnimator();
animator->applyAnimation(0, time);
animator->updateBoneMatrices();
```

### applyAnimation 调用栈

```
Animator::applyAnimation(animIndex, time)
│
└─> AnimatorImpl::applyAnimation(animIndex, time)
    │
    ├─> Animation& anim = mAnimations[animIndex]
    │
    ├─> 打开变换事务
    │   └─> trsTransformManager->openLocalTransformTransaction()
    │
    ├─> 遍历所有通道
    │   └─> for (Channel& channel : anim.channels):
    │       │
    │       ├─> 获取 Entity
    │       │   └─> Entity entity = mInstance->mNodeMap[channel.nodeIndex]
    │       │
    │       ├─> 查找关键帧
    │       │   └─> size_t frame0 = findKeyframe(channel.times, time)
    │       │   └─> size_t frame1 = frame0 + 1
    │       │   └─> if (frame1 >= channel.times.size()): frame1 = frame0
    │       │
    │       ├─> 计算插值因子
    │       │   └─> float t = (time - channel.times[frame0]) /
    │       │                 (channel.times[frame1] - channel.times[frame0])
    │       │
    │       └─> 根据通道类型应用变换
    │           │
    │           ├─> if (channel.transformType == TRANSLATION):
    │           │   │
    │           │   ├─> float3 v0 = *(float3*)&channel.values[frame0 * 3]
    │           │   ├─> float3 v1 = *(float3*)&channel.values[frame1 * 3]
    │           │   │
    │           │   ├─> 插值
    │           │   │   └─> float3 value = mix(v0, v1, t)  // 线性插值
    │           │   │
    │           │   └─> 设置平移
    │           │       └─> trsTransformManager->setTranslation(
    │           │           trsTransformManager->getInstance(entity),
    │           │           value
    │           │       )
    │           │
    │           ├─> if (channel.transformType == ROTATION):
    │           │   │
    │           │   ├─> quatf q0 = *(quatf*)&channel.values[frame0 * 4]
    │           │   ├─> quatf q1 = *(quatf*)&channel.values[frame1 * 4]
    │           │   │
    │           │   ├─> 插值
    │           │   │   └─> quatf value = slerp(q0, q1, t)  // 球面线性插值
    │           │   │
    │           │   └─> 设置旋转
    │           │       └─> trsTransformManager->setRotation(
    │           │           trsTransformManager->getInstance(entity),
    │           │           value
    │           │       )
    │           │
    │           ├─> if (channel.transformType == SCALE):
    │           │   │
    │           │   ├─> float3 s0 = *(float3*)&channel.values[frame0 * 3]
    │           │   ├─> float3 s1 = *(float3*)&channel.values[frame1 * 3]
    │           │   │
    │           │   ├─> 插值
    │           │   │   └─> float3 value = mix(s0, s1, t)
    │           │   │
    │           │   └─> 设置缩放
    │           │       └─> trsTransformManager->setScale(
    │           │           trsTransformManager->getInstance(entity),
    │           │           value
    │           │       )
    │           │
    │           └─> if (channel.transformType == WEIGHTS):
    │               │
    │               ├─> 计算权重数量
    │               │   └─> size_t weightCount = channel.values.size() / channel.times.size()
    │               │
    │               ├─> 插值权重
    │               │   └─> float* weights = new float[weightCount]
    │               │   └─> for (i = 0; i < weightCount; i++):
    │               │       └─> weights[i] = mix(
    │               │           channel.values[frame0 * weightCount + i],
    │               │           channel.values[frame1 * weightCount + i],
    │               │           t
    │               │       )
    │               │
    │               └─> 设置变形权重
    │                   └─> RenderableManager::Instance rmi =
    │                       renderableManager->getInstance(entity)
    │                   └─> renderableManager->setMorphWeights(rmi, weights, weightCount)
    │                   └─> delete[] weights
    │
    ├─> 提交变换事务
    │   └─> trsTransformManager->commitLocalTransformTransaction()
    │
    └─> 同步到 TransformManager
        └─> trsTransformManager->updateTransformMatrices(transformManager)
```

### updateBoneMatrices 调用栈

```
Animator::updateBoneMatrices()
│
└─> AnimatorImpl::updateBoneMatrices(FFilamentInstance* instance)
    │
    └─> for each skin in instance->mSkins:
        │
        ├─> Skin& skin = instance->mSkins[skinIndex]
        │
        ├─> 获取 Asset 的蒙皮数据
        │   └─> FFilamentAsset::Skin& assetSkin = mAsset->mSkins[skinIndex]
        │   └─> const mat4f* inverseBindMatrices = assetSkin.inverseBindMatrices.data()
        │
        └─> 遍历受影响的网格
            └─> for (Entity meshEntity : skin.targets):
                │
                ├─> 获取网格的世界矩阵
                │   └─> TransformManager::Instance tmi =
                │       transformManager->getInstance(meshEntity)
                │   └─> mat4f meshWorldMatrix =
                │       transformManager->getWorldTransform(tmi)
                │   └─> mat4f meshInverseWorld = inverse(meshWorldMatrix)
                │
                ├─> 分配骨骼矩阵数组
                │   └─> size_t boneCount = skin.joints.size()
                │   └─> mat4f* boneMatrices = new mat4f[boneCount]
                │
                ├─> 计算每个骨骼矩阵
                │   └─> for (size_t i = 0; i < boneCount; i++):
                │       │
                │       ├─> Entity joint = skin.joints[i]
                │       │
                │       ├─> 获取关节世界矩阵
                │       │   └─> TransformManager::Instance jointTmi =
                │       │       transformManager->getInstance(joint)
                │       │   └─> mat4f jointWorldMatrix =
                │       │       transformManager->getWorldTransform(jointTmi)
                │       │
                │       ├─> 获取逆绑定矩阵
                │       │   └─> mat4f inverseBindMatrix = inverseBindMatrices[i]
                │       │
                │       └─> 计算最终骨骼矩阵
                │           └─> boneMatrices[i] =
                │               meshInverseWorld *
                │               jointWorldMatrix *
                │               inverseBindMatrix
                │
                ├─> 设置到 RenderableManager
                │   └─> RenderableManager::Instance rmi =
                │       renderableManager->getInstance(meshEntity)
                │   └─> renderableManager->setBones(rmi, boneMatrices, boneCount)
                │       │
                │       └─> [内部] 复制到 GPU uniform buffer
                │           └─> driver->updateUniformBuffer(
                │               boneBufferHandle,
                │               boneMatrices,
                │               sizeof(mat4f) * boneCount
                │           )
                │
                └─> 释放临时数组
                    └─> delete[] boneMatrices
```

---

## 调用链关键点总结

### Phase 1: createAsset

1. **cgltf_parse**: 解析 glTF → cgltf_data
2. **第一次遍历**: 创建 VB/IB (空)
3. **第二次遍历**: 创建 Entity + 组件
4. **createAnimator**: 返回 nullptr (资源未加载)

### Phase 2: loadResources

1. **loadCgltfBuffers**: 加载外部 buffer 数据
2. **uploadBuffers**: 上传到 GPU
3. **computeTangents**: 并行计算切线
4. **createTextures**: 异步解码纹理
5. **createAnimator**: 创建 Animator (资源已加载)

### Phase 3: 渲染循环

1. **applyAnimation**: 插值并更新 Transform
2. **updateBoneMatrices**: 计算蒙皮矩阵
3. **render**: GPU 使用骨骼矩阵渲染
