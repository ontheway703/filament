# 骨骼动画系统

## 概述

gltfio 的骨骼动画系统负责处理 glTF 的动画和蒙皮数据。它包括：
1. **骨骼蒙皮** (Skinning) - 基于骨骼的网格变形
2. **关键帧动画** (Animation) - TRS (Translation, Rotation, Scale) 动画
3. **变形目标动画** (Morph Target Animation) - 权重动画

## 核心组件

### Animator

**代码位置**: `libs/gltfio/src/Animator.cpp:70-87`

```cpp
struct AnimatorImpl {
    vector<Animation> animations;           // 所有动画数据
    BoneVector boneMatrices;                // 骨骼矩阵缓存
    FFilamentAsset const* asset = nullptr;
    FFilamentInstance* instance = nullptr;
    RenderableManager* renderableManager;
    TransformManager* transformManager;
    TrsTransformManager* trsTransformManager;
    vector<float> weights;                  // 变形权重缓存
    FixedCapacityVector<mat4f> crossFade;   // CrossFade 缓存
};
```

### Animation

**代码位置**: `libs/gltfio/src/Animator.cpp:63-68`

```cpp
struct Animation {
    float duration;                 // 动画时长（秒）
    std::string name;               // 动画名称
    vector<Sampler> samplers;       // 采样器（时间-值数据）
    vector<Channel> channels;       // 通道（目标实体+变换类型）
};
```

### Sampler

**代码位置**: `libs/gltfio/src/Animator.cpp:51-55`

```cpp
struct Sampler {
    TimeValues times;               // 时间 → 索引的映射（std::map<float, size_t>）
    SourceValues values;            // 值数据（std::vector<float>）
    enum { LINEAR, STEP, CUBIC } interpolation;  // 插值类型
};
```

### Channel

**代码位置**: `libs/gltfio/src/Animator.cpp:57-61`

```cpp
struct Channel {
    const Sampler* sourceData;      // 指向 Sampler
    Entity targetEntity;            // 目标实体
    enum { TRANSLATION, ROTATION, SCALE, WEIGHTS } transformType;  // 变换类型
};
```

### Skin (FFilamentAsset)

**代码位置**: `libs/gltfio/src/FFilamentAsset.h:249-252`

```cpp
struct Skin {
    utils::CString name;
    utils::FixedCapacityVector<math::mat4f> inverseBindMatrices;  // 逆绑定矩阵
};
```

**存储位置**: `FFilamentAsset::mSkins` (共享，所有实例共用)

### Skin (FFilamentInstance)

**代码位置**: `libs/gltfio/src/FFilamentInstance.h:85-92`

```cpp
struct Skin {
    // 关节实体列表
    utils::FixedCapacityVector<utils::Entity> joints;

    // 被此 skin 影响的实体集合
    tsl::robin_set<utils::Entity, utils::Entity::Hasher> targets;
};
```

**存储位置**: `FFilamentInstance::mSkins` (每个实例独立)

## 骨骼蒙皮

### 数据流程

```
glTF Skin 定义
  ├─ joints: [node0, node1, node2, ...]  // 骨骼节点
  ├─ inverseBindMatrices: accessor       // 逆绑定矩阵
  └─ (implicitly) targets: nodes with skin reference

1. [AssetLoader] importSkins()
   └─ 为每个 skin 创建 FFilamentInstance::Skin
      ├─ joints = [Entity对应的关节]
      └─ targets = {被skin影响的Entity集合}

2. [ResourceLoader] createSkins()
   └─ 为每个 skin 创建 FFilamentAsset::Skin
      └─ inverseBindMatrices = 从accessor拷贝矩阵

3. [Runtime] updateBoneMatrices()
   └─ 计算最终的骨骼矩阵
      └─ RenderableManager::setBones()
```

### importSkins

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:1721-1743`

```cpp
void importSkins(FFilamentInstance* instance, const cgltf_data* gltf) {
    instance->mSkins.reserve(gltf->skins_count);
    instance->mSkins.resize(gltf->skins_count);

    const auto& nodeMap = instance->mNodeMap;

    // 1. 找到所有使用 skin 的 node，记录为 targets
    for (cgltf_size i = 0; i < gltf->nodes_count; ++i) {
        const cgltf_node& node = gltf->nodes[i];
        Entity entity = nodeMap[i];

        if (node.skin && entity) {
            int skinIndex = node.skin - &gltf->skins[0];
            instance->mSkins[skinIndex].targets.insert(entity);
        }
    }

    // 2. 填充关节实体列表
    for (cgltf_size i = 0; i < gltf->skins_count; ++i) {
        FFilamentInstance::Skin& dstSkin = instance->mSkins[i];
        const cgltf_skin& srcSkin = gltf->skins[i];

        dstSkin.joints = FixedCapacityVector<Entity>(srcSkin.joints_count);
        for (cgltf_size j = 0; j < srcSkin.joints_count; ++j) {
            dstSkin.joints[j] = nodeMap[srcSkin.joints[j] - gltf->nodes];
        }
    }
}
```

### createSkins

见 [resource-loading.md#骨骼数据创建](./resource-loading.md#骨骼数据创建)

### updateBoneMatrices

**代码位置**: `libs/gltfio/src/Animator.cpp:548-577`

```cpp
void AnimatorImpl::updateBoneMatrices(FFilamentInstance* instance) {
    assert_invariant(instance->mSkins.size() == asset->mSkins.size());

    size_t skinIndex = 0;
    for (const auto& skin : instance->mSkins) {
        const auto& assetSkin = asset->mSkins[skinIndex++];
        size_t njoints = skin.joints.size();

        boneMatrices.resize(njoints);

        // 遍历所有被此 skin 影响的实体
        for (Entity entity : skin.targets) {
            auto renderable = renderableManager->getInstance(entity);
            if (!renderable) continue;

            // 获取目标实体的逆世界变换
            mat4 inverseGlobalTransform;
            auto xformable = transformManager->getInstance(entity);
            if (xformable) {
                inverseGlobalTransform = inverse(
                    transformManager->getWorldTransformAccurate(xformable)
                );
            }

            // 为每个关节计算骨骼矩阵
            for (size_t boneIndex = 0; boneIndex < njoints; ++boneIndex) {
                const auto& joint = skin.joints[boneIndex];
                const mat4f& inverseBindMatrix = assetSkin.inverseBindMatrices[boneIndex];

                // 获取关节的世界变换
                TransformManager::Instance jointInstance = transformManager->getInstance(joint);
                mat4 globalJointTransform = transformManager->getWorldTransformAccurate(jointInstance);

                // 计算最终的骨骼矩阵
                boneMatrices[boneIndex] =
                    mat4f{ inverseGlobalTransform * globalJointTransform } *
                    inverseBindMatrix;
            }

            // 设置到 RenderableManager
            renderableManager->setBones(renderable, boneMatrices.data(), boneMatrices.size());
        }
    }
}
```

**骨骼矩阵计算公式**:

```
BoneMatrix[i] = InverseGlobalTransform(mesh) × GlobalTransform(joint[i]) × InverseBindMatrix[i]
```

其中:
- `InverseGlobalTransform(mesh)`: 蒙皮网格的逆世界变换
- `GlobalTransform(joint[i])`: 关节 i 的世界变换
- `InverseBindMatrix[i]`: 关节 i 的逆绑定矩阵（来自 glTF）

**作用**:
1. 将顶点从 mesh 空间变换到关节空间
2. 应用关节的动画变换
3. 变换回 mesh 空间

## 关键帧动画

### 创建 Animator

**代码位置**: `libs/gltfio/src/Animator.cpp:187-238`

```cpp
Animator::Animator(FFilamentAsset const* asset, FFilamentInstance* instance) {
    mImpl = new AnimatorImpl();
    mImpl->asset = asset;
    mImpl->instance = instance;
    mImpl->renderableManager = &asset->mEngine->getRenderableManager();
    mImpl->transformManager = &asset->mEngine->getTransformManager();
    mImpl->trsTransformManager = asset->getTrsTransformManager();

    const cgltf_data* srcAsset = asset->mSourceAsset->hierarchy;
    const cgltf_animation* srcAnims = srcAsset->animations;

    // 验证动画数据
    for (cgltf_size i = 0; i < srcAsset->animations_count; ++i) {
        if (!validateAnimation(srcAnims[i])) {
            GLTFIO_WARN("Disabling animation due to validation failure.");
            return;
        }
    }

    // 导入每个动画
    mImpl->animations.resize(srcAsset->animations_count);
    for (cgltf_size i = 0; i < srcAsset->animations_count; ++i) {
        const cgltf_animation& srcAnim = srcAnims[i];
        Animation& dstAnim = mImpl->animations[i];

        dstAnim.duration = 0;
        if (srcAnim.name) {
            dstAnim.name = srcAnim.name;
        }

        // 导入采样器
        dstAnim.samplers.resize(srcAnim.samplers_count);
        for (cgltf_size j = 0; j < srcAnim.samplers_count; ++j) {
            createSampler(srcAnims[i].samplers[j], dstAnim.samplers[j]);

            // 更新动画时长
            if (dstAnim.samplers[j].times.size() > 1) {
                float maxtime = (--dstAnim.samplers[j].times.end())->first;
                dstAnim.duration = std::max(dstAnim.duration, maxtime);
            }
        }

        // 导入通道
        if (instance) {
            mImpl->addChannels(instance->mNodeMap, srcAnim, dstAnim);
        } else {
            // 广播模式：为所有实例添加通道
            for (FFilamentInstance* inst : asset->mInstances) {
                mImpl->addChannels(inst->mNodeMap, srcAnim, dstAnim);
            }
        }
    }
}
```

### createSampler

**代码位置**: `libs/gltfio/src/Animator.cpp:89-139`

```cpp
void createSampler(const cgltf_animation_sampler& src, Sampler& dst) {
    // 1. 拷贝时间数据到 red-black tree (std::map)
    const cgltf_accessor* timelineAccessor = src.input;
    const float* timelineFloats = /* 获取数据指针 */;

    for (size_t i = 0; i < timelineAccessor->count; ++i) {
        dst.times[timelineFloats[i]] = i;  // 时间 → 索引
    }

    // 2. 解包值数据为浮点数
    const cgltf_accessor* valuesAccessor = src.output;
    switch (valuesAccessor->type) {
        case cgltf_type_scalar:
            dst.values.resize(valuesAccessor->count);
            cgltf_accessor_unpack_floats(src.output, &dst.values[0], valuesAccessor->count);
            break;
        case cgltf_type_vec3:
            dst.values.resize(valuesAccessor->count * 3);
            cgltf_accessor_unpack_floats(src.output, &dst.values[0], valuesAccessor->count * 3);
            break;
        case cgltf_type_vec4:
            dst.values.resize(valuesAccessor->count * 4);
            cgltf_accessor_unpack_floats(src.output, &dst.values[0], valuesAccessor->count * 4);
            break;
    }

    // 3. 设置插值类型
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

### addChannels

**代码位置**: `libs/gltfio/src/Animator.cpp:403-435`

```cpp
void AnimatorImpl::addChannels(const FixedCapacityVector<Entity>& nodeMap,
        const cgltf_animation& srcAnim, Animation& dst) {

    const cgltf_animation_channel* srcChannels = srcAnim.channels;
    const cgltf_animation_sampler* srcSamplers = srcAnim.samplers;
    const cgltf_node* nodes = asset->mSourceAsset->hierarchy->nodes;
    const Sampler* samplers = dst.samplers.data();

    for (cgltf_size j = 0; j < srcAnim.channels_count; ++j) {
        const cgltf_animation_channel& srcChannel = srcChannels[j];

        if (!srcChannel.target_node) {
            continue;
        }

        // 获取目标实体
        Entity targetEntity = nodeMap[srcChannel.target_node - nodes];
        if (!targetEntity) {
            // 节点不在任何场景中，跳过
            continue;
        }

        // 创建通道
        Channel dstChannel;
        dstChannel.sourceData = samplers + (srcChannel.sampler - srcSamplers);
        dstChannel.targetEntity = targetEntity;
        setTransformType(srcChannel, dstChannel);

        dst.channels.push_back(dstChannel);
    }
}
```

### applyAnimation

**代码位置**: `libs/gltfio/src/Animator.cpp:264-310`

```cpp
void Animator::applyAnimation(size_t animationIndex, float time) const {
    const Animation& anim = mImpl->animations[animationIndex];

    // 循环时间
    time = time == anim.duration ? time : fmod(time, anim.duration);

    TransformManager& transformManager = *mImpl->transformManager;
    transformManager.openLocalTransformTransaction();

    for (const auto& channel : anim.channels) {
        const Sampler* sampler = channel.sourceData;
        if (sampler->times.size() < 2) {
            continue;
        }

        const TimeValues& times = sampler->times;

        // 1. 查找当前时间对应的关键帧区间
        TimeValues::const_iterator iter = times.lower_bound(time);

        // 2. 计算插值参数 t
        float t = 0.0f;
        size_t nextIndex, prevIndex;

        if (iter == times.end()) {
            // 超过最后一帧
            nextIndex = times.size() - 1;
            prevIndex = nextIndex;
        } else if (iter == times.begin()) {
            // 在第一帧之前
            nextIndex = 0;
            prevIndex = 0;
        } else {
            // 在两帧之间
            TimeValues::const_iterator prev = iter; --prev;
            nextIndex = iter->second;
            prevIndex = prev->second;

            const float nextTime = iter->first;
            const float prevTime = prev->first;
            float deltaTime = nextTime - prevTime;

            if (deltaTime > 0) {
                t = (time - prevTime) / deltaTime;
            }
        }

        // 3. STEP 插值不需要 t
        if (sampler->interpolation == Sampler::STEP) {
            t = 0.0f;
        }

        // 4. 应用动画
        mImpl->applyAnimation(channel, t, prevIndex, nextIndex);
    }

    transformManager.commitLocalTransformTransaction();
}
```

### applyAnimation (单个通道)

**代码位置**: `libs/gltfio/src/Animator.cpp:437-530`

```cpp
void AnimatorImpl::applyAnimation(const Channel& channel, float t,
        size_t prevIndex, size_t nextIndex) {

    const Sampler* sampler = channel.sourceData;
    TrsTransformManager::Instance trsNode = trsTransformManager->getInstance(channel.targetEntity);
    TransformManager::Instance node = transformManager->getInstance(channel.targetEntity);

    switch (channel.transformType) {

        case Channel::SCALE: {
            float3 scale;
            const float3* srcVec3 = (const float3*) sampler->values.data();

            if (sampler->interpolation == Sampler::CUBIC) {
                // Cubic spline: [inTangent, vertex, outTangent] per keyframe
                float3 vert0 = srcVec3[prevIndex * 3 + 1];
                float3 tang0 = srcVec3[prevIndex * 3 + 2];
                float3 tang1 = srcVec3[nextIndex * 3];
                float3 vert1 = srcVec3[nextIndex * 3 + 1];
                scale = cubicSpline(vert0, tang0, vert1, tang1, t);
            } else {
                // Linear interpolation
                scale = ((1 - t) * srcVec3[prevIndex]) + (t * srcVec3[nextIndex]);
            }

            trsTransformManager->setScale(trsNode, scale);
            break;
        }

        case Channel::TRANSLATION: {
            float3 translation;
            const float3* srcVec3 = (const float3*) sampler->values.data();

            if (sampler->interpolation == Sampler::CUBIC) {
                float3 vert0 = srcVec3[prevIndex * 3 + 1];
                float3 tang0 = srcVec3[prevIndex * 3 + 2];
                float3 tang1 = srcVec3[nextIndex * 3];
                float3 vert1 = srcVec3[nextIndex * 3 + 1];
                translation = cubicSpline(vert0, tang0, vert1, tang1, t);
            } else {
                translation = ((1 - t) * srcVec3[prevIndex]) + (t * srcVec3[nextIndex]);
            }

            trsTransformManager->setTranslation(trsNode, translation);
            break;
        }

        case Channel::ROTATION: {
            quatf rotation;
            const quatf* srcQuat = (const quatf*) sampler->values.data();

            if (sampler->interpolation == Sampler::CUBIC) {
                quatf vert0 = srcQuat[prevIndex * 3 + 1];
                quatf tang0 = srcQuat[prevIndex * 3 + 2];
                quatf tang1 = srcQuat[nextIndex * 3];
                quatf vert1 = srcQuat[nextIndex * 3 + 1];
                rotation = normalize(cubicSpline(vert0, tang0, vert1, tang1, t));
            } else {
                // Spherical linear interpolation
                rotation = slerp(srcQuat[prevIndex], srcQuat[nextIndex], t);
            }

            trsTransformManager->setRotation(trsNode, rotation);
            break;
        }

        case Channel::WEIGHTS: {
            // 变形目标权重动画
            const float* const samplerValues = sampler->values.data();
            const int valuesPerKeyframe = sampler->values.size() / sampler->times.size();

            if (sampler->interpolation == Sampler::CUBIC) {
                const int numMorphTargets = valuesPerKeyframe / 3;
                weights.resize(numMorphTargets);

                for (int comp = 0; comp < numMorphTargets; ++comp) {
                    float vert0 = splineVerts[comp + prevIndex * valuesPerKeyframe];
                    float tang0 = outTangents[comp + prevIndex * valuesPerKeyframe];
                    float tang1 = inTangents[comp + nextIndex * valuesPerKeyframe];
                    float vert1 = splineVerts[comp + nextIndex * valuesPerKeyframe];
                    weights[comp] = cubicSpline(vert0, tang0, vert1, tang1, t);
                }
            } else {
                weights.resize(valuesPerKeyframe);
                for (int comp = 0; comp < valuesPerKeyframe; ++comp) {
                    float previous = samplerValues[comp + prevIndex * valuesPerKeyframe];
                    float current = samplerValues[comp + nextIndex * valuesPerKeyframe];
                    weights[comp] = (1 - t) * previous + t * current;
                }
            }

            // 设置到 RenderableManager
            auto ci = renderableManager->getInstance(channel.targetEntity);
            renderableManager->setMorphWeights(ci, weights.data(), weights.size());
            return;  // WEIGHTS 不更新 TransformManager
        }
    }

    // 更新 TransformManager（从 TrsTransformManager 获取组合矩阵）
    transformManager->setTransform(node, trsTransformManager->getTransform(trsNode));
}
```

## TrsTransformManager

**目的**: 分别存储 Translation, Rotation, Scale，方便动画系统修改

**代码位置**: `libs/gltfio/include/gltfio/TrsTransformManager.h`

```cpp
class TrsTransformManager {
public:
    void create(Entity entity, float3 translation, quatf rotation, float3 scale);
    void setTranslation(Instance instance, float3 translation);
    void setRotation(Instance instance, quatf rotation);
    void setScale(Instance instance, float3 scale);
    mat4f getTransform(Instance instance) const;
};
```

**工作流程**:
1. 创建实体时，同时创建 TransformManager 和 TrsTransformManager 组件
2. 动画修改 TrsTransformManager (setTranslation/setRotation/setScale)
3. 从 TrsTransformManager 计算组合矩阵
4. 更新 TransformManager

## 动画混合 (CrossFade)

**代码位置**: `libs/gltfio/src/Animator.cpp:346-401`

```cpp
void Animator::applyCrossFade(size_t previousAnimIndex, float previousAnimTime, float alpha) {
    // 1. 保存当前变换状态
    mImpl->stashCrossFade();

    // 2. 应用前一个动画
    applyAnimation(previousAnimIndex, previousAnimTime);

    // 3. 混合两个状态
    mImpl->applyCrossFade(alpha);
}

void AnimatorImpl::stashCrossFade() {
    // 递归遍历整个变换树，保存所有节点的局部变换
    auto recursiveStash = [&tm, &stash](Instance node, size_t index, auto& fn) -> size_t {
        stash[index++] = tm.getTransform(node);
        for (auto iter = tm.getChildrenBegin(node); iter != tm.getChildrenEnd(node); ++iter) {
            index = fn(*iter, index, fn);
        }
        return index;
    };

    const Entity rootEntity = instance ? instance->getRoot() : asset->mRoot;
    const Instance root = tm.getInstance(rootEntity);
    const size_t count = recursiveCount(root, 0, recursiveCount);

    crossFade.reserve(count);
    crossFade.resize(count);
    recursiveStash(root, 0, recursiveStash);
}

void AnimatorImpl::applyCrossFade(float alpha) {
    // 递归遍历，对每个节点进行 TRS 插值
    auto recursiveFn = [&tm, &stash, alpha](Instance node, size_t index, auto& fn) -> size_t {
        float3 scale0, scale1;
        quatf rotation0, rotation1;
        float3 translation0, translation1;

        // 分解矩阵
        decomposeMatrix(stash[index++], &translation1, &rotation1, &scale1);
        decomposeMatrix(tm.getTransform(node), &translation0, &rotation0, &scale0);

        // 插值
        const float3 scale = mix(scale0, scale1, alpha);
        const quatf rotation = slerp(rotation0, rotation1, alpha);
        const float3 translation = mix(translation0, translation1, alpha);

        // 组合并设置
        tm.setTransform(node, composeMatrix(translation, rotation, scale));

        // 递归子节点
        for (auto iter = tm.getChildrenBegin(node); iter != tm.getChildrenEnd(node); ++iter) {
            index = fn(*iter, index, fn);
        }
        return index;
    };

    const Entity rootEntity = instance ? instance->getRoot() : asset->mRoot;
    const Instance root = tm.getInstance(rootEntity);
    recursiveFn(root, 0, recursiveFn);
}
```

**使用场景**: 从一个动画平滑过渡到另一个动画

```cpp
// alpha = 0: 完全是 previousAnim
// alpha = 1: 完全是 currentAnim
animator->applyAnimation(currentAnimIndex, currentTime);
animator->applyCrossFade(previousAnimIndex, previousTime, alpha);
animator->updateBoneMatrices();
```

## 变形目标 (Morph Targets)

### 数据流程

```
1. [AssetLoader] createRenderable()
   └─ 创建 MorphTargetBuffer
      └─ RenderableManager::Builder::morphing(morphTargetBuffer)

2. [ResourceLoader] uploadBuffers()
   └─ 上传变形目标数据
      └─ MorphTargetBuffer::setPositionsAt(targetIndex, positions, count, offset)

3. [Runtime] 设置默认权重
   └─ RenderableManager::setMorphWeights(weights, count)

4. [Animation] 动画权重
   └─ Animator::applyAnimation()
      └─ RenderableManager::setMorphWeights(animatedWeights, count)
```

### 默认权重

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:895-906`

```cpp
if (numMorphTargets > 0) {
    RenderableManager::Instance renderable = mRenderableManager.getInstance(entity);
    const auto size = std::min(MAX_MORPH_TARGETS, numMorphTargets);
    FixedCapacityVector<float> weights(size, 0.0f);

    // Mesh 的默认权重
    for (cgltf_size i = 0; i < std::min(size, mesh->weights_count); ++i) {
        weights[i] = mesh->weights[i];
    }

    // Node 的权重覆盖 Mesh 权重
    for (cgltf_size i = 0; i < std::min(size, node->weights_count); ++i) {
        weights[i] = node->weights[i];
    }

    mRenderableManager.setMorphWeights(renderable, weights.data(), size);
}
```

## 性能优化

### 减少变换更新

```cpp
// 打开事务，批量修改
transformManager.openLocalTransformTransaction();

for (const auto& channel : anim.channels) {
    // 修改局部变换
}

// 提交事务，一次性更新世界变换
transformManager.commitLocalTransformTransaction();
```

### 骨骼矩阵缓存

```cpp
// AnimatorImpl::boneMatrices 被重复使用
boneMatrices.resize(njoints);  // 避免频繁分配
```

### 广播 vs 单实例 Animator

**广播 Animator** (instance = nullptr):
- 控制所有实例
- 节省内存（一个 AnimatorImpl）
- 所有实例动画同步

**单实例 Animator**:
- 每个实例独立控制
- 可以播放不同的动画
- 更灵活但占用更多内存

## 使用示例

### 基本动画循环

```cpp
auto animator = asset->getInstance()->getAnimator();
float time = 0.0f;
float animationDuration = animator->getAnimationDuration(0);

while (running) {
    time += deltaTime;
    if (time > animationDuration) {
        time = 0.0f;
    }

    animator->applyAnimation(0, time);
    animator->updateBoneMatrices();

    // 渲染...
}
```

### 动画混合

```cpp
float blendTime = 0.5f;  // 0.5 秒混合时间
float blendAlpha = 0.0f;
float currentTime = 0.0f;
float previousTime = /* 上一个动画的时间 */;

while (blendAlpha < 1.0f) {
    blendAlpha += deltaTime / blendTime;
    blendAlpha = std::min(blendAlpha, 1.0f);

    animator->applyAnimation(currentAnimIndex, currentTime);
    animator->applyCrossFade(previousAnimIndex, previousTime, blendAlpha);
    animator->updateBoneMatrices();

    currentTime += deltaTime;
    // 渲染...
}
```

## 限制

1. **MAX_MORPH_TARGETS**: 通常为 4 或 8
2. **动画插值**: 只支持 LINEAR, STEP, CUBIC
3. **骨骼数量**: 受 GPU 限制（通常最多 256 个关节）

## 下一步阅读

- [Mesh 处理](./mesh-processing.md) - MorphTargetBuffer 创建
- [资源加载](./resource-loading.md) - 动画数据加载
- [架构关键点](./architecture.md) - TrsTransformManager 设计
