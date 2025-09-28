# Filament 动画系统实现详解

本文档深入分析 Filament 动画系统的具体实现，基于前面章节分析的源代码，详细解释 Animator 类的工作原理。

## 目录

1. [Animator 类架构](#animator-类架构)
2. [动画数据结构](#动画数据结构)
3. [动画解析过程](#动画解析过程)
4. [动画播放机制](#动画播放机制)
5. [骨骼动画实现](#骨骼动画实现)
6. [变形动画实现](#变形动画实现)
7. [动画混合系统](#动画混合系统)
8. [TRS 变换管理](#trs-变换管理)

## Animator 类架构

### 核心类层次结构

```cpp
// libs/gltfio/include/gltfio/Animator.h
class Animator {
public:
    // 动画播放控制
    void applyAnimation(size_t animationIndex, float time) const;
    void applyCrossFade(size_t previousAnimIndex, float previousAnimTime, float alpha);

    // 骨骼控制
    void updateBoneMatrices();
    void resetBoneMatrices();

    // 查询接口
    size_t getAnimationCount() const;
    float getAnimationDuration(size_t animationIndex) const;
    const char* getAnimationName(size_t animationIndex) const;

private:
    AnimatorImpl* mImpl;  // PIMPL 模式隐藏实现细节
};

// libs/gltfio/src/Animator.cpp - 实现类
struct AnimatorImpl {
    vector<Animation> animations;           // 动画数据
    BoneVector boneMatrices;               // 骨骼矩阵缓存
    FFilamentAsset const* asset;           // 关联的资产
    FFilamentInstance* instance;           // 关联的实例（可为null）

    // Filament 管理器引用
    RenderableManager* renderableManager;
    TransformManager* transformManager;
    TrsTransformManager* trsTransformManager;

    // 混合相关
    vector<float> weights;                 // 变形权重缓存
    FixedCapacityVector<mat4f> crossFade;  // 交叉淡化缓存

    // 核心功能函数
    void addChannels(const FixedCapacityVector<Entity>& nodeMap,
                    const cgltf_animation& srcAnim, Animation& dst);
    void applyAnimation(const Channel& channel, float t,
                       size_t prevIndex, size_t nextIndex);
    void stashCrossFade();
    void applyCrossFade(float alpha);
    void resetBoneMatrices(FFilamentInstance* instance);
    void updateBoneMatrices(FFilamentInstance* instance);
};
```

### PIMPL 设计模式

使用 PIMPL (Pointer to Implementation) 模式的优势：
1. **隐藏实现细节**: 头文件不暴露内部数据结构
2. **编译时间优化**: 减少头文件依赖
3. **ABI 稳定性**: 实现变更不影响二进制接口
4. **内存局部性**: 实现数据集中存储

```cpp
// 构造函数中创建实现对象
Animator::Animator(FFilamentAsset const* asset, FFilamentInstance* instance) {
    assert(asset->mResourcesLoaded && asset->mSourceAsset);
    mImpl = new AnimatorImpl();
    mImpl->asset = asset;
    mImpl->instance = instance;

    // 获取 Filament 管理器的引用
    mImpl->renderableManager = &asset->mEngine->getRenderableManager();
    mImpl->transformManager = &asset->mEngine->getTransformManager();
    mImpl->trsTransformManager = asset->getTrsTransformManager();

    // 解析动画数据
    parseAnimations();
}
```

## 动画数据结构

### 核心数据结构

基于分析的源码 (`libs/gltfio/src/Animator.cpp:48-88`)：

```cpp
// 采样器：存储时间轴和对应的值
struct Sampler {
    TimeValues times;        // map<float, size_t> 时间到索引的映射
    SourceValues values;     // vector<float> 动画值数组
    enum { LINEAR, STEP, CUBIC } interpolation;  // 插值类型
};

// 动画通道：连接采样器到目标实体
struct Channel {
    const Sampler* sourceData;    // 指向采样器的指针
    Entity targetEntity;          // 目标实体
    enum { TRANSLATION, ROTATION, SCALE, WEIGHTS } transformType;  // 变换类型
};

// 动画：包含多个通道和采样器
struct Animation {
    float duration;               // 动画持续时间
    std::string name;            // 动画名称
    vector<Sampler> samplers;    // 采样器数组
    vector<Channel> channels;    // 通道数组
};

// 类型别名
using TimeValues = map<float, size_t>;   // 使用红黑树优化时间查找
using SourceValues = vector<float>;      // 线性存储动画值
using BoneVector = vector<mat4f>;        // 骨骼矩阵数组
```

### 内存布局优化

#### 1. 采样器优化
```cpp
// 时间轴使用 map 结构，O(log n) 查找复杂度
void createSampler(const cgltf_animation_sampler& src, Sampler& dst) {
    const cgltf_accessor* timelineAccessor = src.input;

    // 构建时间索引映射 - 使用红黑树
    for (size_t i = 0, len = timelineAccessor->count; i < len; ++i) {
        dst.times[timelineFloats[i]] = i;  // O(log n) 插入
    }

    // 值数据线性存储，方便缓存访问
    dst.values.resize(valuesAccessor->count * componentsPerElement);
    cgltf_accessor_unpack_floats(src.output, &dst.values[0], dst.values.size());
}
```

#### 2. 通道数据结构
```cpp
// 通道使用指针引用采样器，避免数据拷贝
void AnimatorImpl::addChannels(const FixedCapacityVector<Entity>& nodeMap,
                              const cgltf_animation& srcAnim, Animation& dst) {
    for (cgltf_size j = 0, nchans = srcAnim.channels_count; j < nchans; ++j) {
        const cgltf_animation_channel& srcChannel = srcChannels[j];

        Channel dstChannel;
        // 指针指向同一动画中的采样器，避免数据重复
        dstChannel.sourceData = samplers + (srcChannel.sampler - srcSamplers);
        dstChannel.targetEntity = nodeMap[srcChannel.target_node - nodes];
        setTransformType(srcChannel, dstChannel);

        dst.channels.push_back(dstChannel);
    }
}
```

## 动画解析过程

### 解析流程概述

基于源码 (`libs/gltfio/src/Animator.cpp:188-238`)：

```cpp
Animator::Animator(FFilamentAsset const* asset, FFilamentInstance* instance) {
    // 1. 验证动画数据完整性
    for (cgltf_size i = 0, len = srcAsset->animations_count; i < len; ++i) {
        const cgltf_animation& anim = srcAnims[i];
        if (!validateAnimation(anim)) {
            GLTFIO_WARN("Disabling animation due to validation failure.");
            return;
        }
    }

    // 2. 为每个 glTF 动画创建内部表示
    mImpl->animations.resize(srcAsset->animations_count);

    for (cgltf_size i = 0, len = srcAsset->animations_count; i < len; ++i) {
        const cgltf_animation& srcAnim = srcAnims[i];
        Animation& dstAnim = mImpl->animations[i];

        dstAnim.duration = 0;
        if (srcAnim.name) {
            dstAnim.name = srcAnim.name;
        }

        // 3. 解析采样器
        parseSamplers(srcAnim, dstAnim);

        // 4. 解析通道并映射到实体
        parseChannels(srcAnim, dstAnim, instance);
    }
}

void parseSamplers(const cgltf_animation& srcAnim, Animation& dstAnim) {
    dstAnim.samplers.resize(srcAnim.samplers_count);

    for (cgltf_size j = 0, nsamps = srcAnim.samplers_count; j < nsamps; ++j) {
        const cgltf_animation_sampler& srcSampler = srcSamplers[j];
        Sampler& dstSampler = dstAnim.samplers[j];

        // 创建采样器并计算动画持续时间
        createSampler(srcSampler, dstSampler);

        if (dstSampler.times.size() > 1) {
            float maxtime = (--dstSampler.times.end())->first;
            dstAnim.duration = std::max(dstAnim.duration, maxtime);
        }
    }
}
```

### 数据验证机制

```cpp
// 基于源码中的 validateAnimation 函数
static bool validateAnimation(const cgltf_animation& anim) {
    for (cgltf_size j = 0; j < anim.channels_count; ++j) {
        const cgltf_animation_channel& channel = anim.channels[j];
        const cgltf_animation_sampler* sampler = channel.sampler;

        // 检查通道的完整性
        if (!channel.target_node || !channel.sampler) {
            return false;
        }

        // 验证变形目标的特殊情况
        if (channel.target_path == cgltf_animation_path_type_weights) {
            if (!channel.target_node->mesh ||
                !channel.target_node->mesh->primitives_count) {
                return false;
            }

            // 计算预期的组件数量
            cgltf_size components = channel.target_node->mesh->primitives[0].targets_count;
            cgltf_size values = (sampler->interpolation == cgltf_interpolation_type_cubic_spline) ? 3 : 1;

            // 验证数据尺寸匹配
            if (sampler->input->count * components * values != sampler->output->count) {
                return false;
            }
        }
    }
    return true;
}
```

## 动画播放机制

### 时间计算和关键帧查找

基于源码 (`libs/gltfio/src/Animator.cpp:265-311`)：

```cpp
void Animator::applyAnimation(size_t animationIndex, float time) const {
    const Animation& anim = mImpl->animations[animationIndex];

    // 1. 时间循环处理
    time = (time == anim.duration) ? time : fmod(time, anim.duration);

    // 2. 开启变换事务（批量更新优化）
    TransformManager& transformManager = *mImpl->transformManager;
    transformManager.openLocalTransformTransaction();

    // 3. 处理每个动画通道
    for (const auto& channel : anim.channels) {
        const Sampler* sampler = channel.sourceData;

        if (sampler->times.size() < 2) {
            continue;  // 无效的采样器
        }

        // 4. 关键帧查找 - 使用二分查找
        const TimeValues& times = sampler->times;
        TimeValues::const_iterator iter = times.lower_bound(time);

        // 5. 计算插值参数
        float t = 0.0f;
        size_t nextIndex, prevIndex;

        if (iter == times.end()) {
            // 超出时间范围，使用最后一个关键帧
            nextIndex = times.size() - 1;
            prevIndex = nextIndex;
        } else if (iter == times.begin()) {
            // 在第一个关键帧之前，使用第一个关键帧
            nextIndex = 0;
            prevIndex = 0;
        } else {
            // 在两个关键帧之间，计算插值因子
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

        // 6. 应用插值类型
        if (sampler->interpolation == Sampler::STEP) {
            t = 0.0f;  // 阶梯插值，保持前一个值
        }

        // 7. 应用动画到目标实体
        mImpl->applyAnimation(channel, t, prevIndex, nextIndex);
    }

    // 8. 提交变换事务
    transformManager.commitLocalTransformTransaction();
}
```

### 插值计算实现

```cpp
// 基于源码 libs/gltfio/src/Animator.cpp:437-530
void AnimatorImpl::applyAnimation(const Channel& channel, float t,
                                 size_t prevIndex, size_t nextIndex) {
    const Sampler* sampler = channel.sourceData;

    // 获取目标实体的变换组件
    TrsTransformManager::Instance trsNode = trsTransformManager->getInstance(channel.targetEntity);
    TransformManager::Instance node = transformManager->getInstance(channel.targetEntity);

    switch (channel.transformType) {
        case Channel::SCALE: {
            float3 scale;
            const float3* srcVec3 = (const float3*) sampler->values.data();

            if (sampler->interpolation == Sampler::CUBIC) {
                // 三次样条插值
                float3 vert0 = srcVec3[prevIndex * 3 + 1];  // 前一个值
                float3 tang0 = srcVec3[prevIndex * 3 + 2];  // 前一个出切线
                float3 tang1 = srcVec3[nextIndex * 3];      // 下一个入切线
                float3 vert1 = srcVec3[nextIndex * 3 + 1];  // 下一个值

                scale = cubicSpline(vert0, tang0, vert1, tang1, t);
            } else {
                // 线性插值
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
                // 球面线性插值 (SLERP)
                rotation = slerp(srcQuat[prevIndex], srcQuat[nextIndex], t);
            }

            trsTransformManager->setRotation(trsNode, rotation);
            break;
        }

        case Channel::WEIGHTS: {
            // 变形目标权重处理（后面详述）
            handleMorphWeights(channel, t, prevIndex, nextIndex);
            return;
        }
    }

    // 更新最终变换矩阵
    transformManager->setTransform(node, trsTransformManager->getTransform(trsNode));
}
```

## 骨骼动画实现

### 骨骼矩阵更新

基于源码 (`libs/gltfio/src/Animator.cpp:548-600`)：

```cpp
void AnimatorImpl::updateBoneMatrices(FFilamentInstance* instance) {
    assert_invariant(instance->mSkins.size() == asset->mSkins.size());

    size_t skinIndex = 0;
    for (const auto& skin : instance->mSkins) {
        const auto& assetSkin = asset->mSkins[skinIndex++];
        size_t njoints = skin.joints.size();

        // 调整骨骼矩阵缓存大小
        boneMatrices.resize(njoints);

        // 为每个受蒙皮影响的可渲染实体更新骨骼矩阵
        for (Entity entity : skin.targets) {
            auto renderable = renderableManager->getInstance(entity);
            if (!renderable) {
                continue;
            }

            // 计算逆变换矩阵（从可渲染实体空间到世界空间的逆变换）
            mat4 inverseGlobalTransform;
            auto xformable = transformManager->getInstance(entity);
            if (xformable) {
                inverseGlobalTransform = inverse(
                    transformManager->getWorldTransformAccurate(xformable));
            }

            // 为每个关节计算最终的骨骼矩阵
            for (size_t boneIndex = 0; boneIndex < njoints; ++boneIndex) {
                Entity jointEntity = skin.joints[boneIndex];
                auto jointInstance = transformManager->getInstance(jointEntity);

                if (jointInstance) {
                    // 获取关节的世界变换
                    mat4 globalJointTransform =
                        transformManager->getWorldTransformAccurate(jointInstance);

                    // 计算最终骨骼矩阵：
                    // 1. 应用逆绑定矩阵（从模型空间到关节空间）
                    // 2. 应用关节的当前世界变换
                    // 3. 应用逆全局变换（回到可渲染实体的局部空间）
                    const mat4f& inverseBindMatrix = assetSkin.inverseBindMatrices[boneIndex];
                    boneMatrices[boneIndex] = mat4f{
                        inverseGlobalTransform * globalJointTransform
                    } * inverseBindMatrix;
                } else {
                    boneMatrices[boneIndex] = mat4f();  // 单位矩阵
                }
            }

            // 将骨骼矩阵设置到 RenderableManager
            renderableManager->setBones(renderable, boneMatrices.data(), boneMatrices.size());
        }
    }
}
```

### 骨骼矩阵计算详解

```cpp
// 骨骼变换的数学公式
mat4 finalBoneMatrix = inverseGlobalTransform * globalJointTransform * inverseBindMatrix;

/*
详细解释：
1. inverseBindMatrix: 将顶点从模型空间变换到关节的绑定空间
   - 这是在绑定姿势时计算的，表示关节在绑定时的逆变换

2. globalJointTransform: 关节的当前世界变换
   - 包含了动画后关节的最新位置、旋转和缩放

3. inverseGlobalTransform: 从世界空间回到可渲染实体的局部空间
   - 因为顶点着色器中需要在局部空间进行蒙皮计算

顶点蒙皮在 GPU 中的计算：
skinnedPosition = sum(weight[i] * boneMatrix[i] * originalPosition)
*/
```

### 重置骨骼矩阵

```cpp
void AnimatorImpl::resetBoneMatrices(FFilamentInstance* instance) {
    for (const auto& skin : instance->mSkins) {
        size_t njoints = skin.joints.size();
        boneMatrices.resize(njoints);

        // 将所有骨骼矩阵重置为单位矩阵
        for (const auto& entity : skin.targets) {
            auto renderable = renderableManager->getInstance(entity);
            if (renderable) {
                for (size_t boneIndex = 0; boneIndex < njoints; ++boneIndex) {
                    boneMatrices[boneIndex] = mat4f();  // 单位矩阵
                }
                renderableManager->setBones(renderable, boneMatrices.data(), boneMatrices.size());
            }
        }
    }
}
```

## 变形动画实现

### 变形权重处理

基于源码 (`libs/gltfio/src/Animator.cpp:494-530`)：

```cpp
void handleMorphWeights(const Channel& channel, float t, size_t prevIndex, size_t nextIndex) {
    const Sampler* sampler = channel.sourceData;
    const TimeValues& times = sampler->times;
    const float* const samplerValues = sampler->values.data();

    // 计算每个关键帧的值数量
    assert(sampler->values.size() % times.size() == 0);
    const int valuesPerKeyframe = sampler->values.size() / times.size();

    if (sampler->interpolation == Sampler::CUBIC) {
        // 三次样条插值，数据布局：[入切线, 值, 出切线]
        assert(valuesPerKeyframe % 3 == 0);
        const int numMorphTargets = valuesPerKeyframe / 3;

        const float* const inTangents = samplerValues;
        const float* const splineVerts = samplerValues + numMorphTargets;
        const float* const outTangents = samplerValues + numMorphTargets * 2;

        weights.resize(numMorphTargets);
        for (int comp = 0; comp < numMorphTargets; ++comp) {
            float vert0 = splineVerts[comp + prevIndex * valuesPerKeyframe];
            float tang0 = outTangents[comp + prevIndex * valuesPerKeyframe];
            float tang1 = inTangents[comp + nextIndex * valuesPerKeyframe];
            float vert1 = splineVerts[comp + nextIndex * valuesPerKeyframe];

            weights[comp] = cubicSpline(vert0, tang0, vert1, tang1, t);
        }
    } else {
        // 线性插值
        weights.resize(valuesPerKeyframe);
        for (int comp = 0; comp < valuesPerKeyframe; ++comp) {
            float previous = samplerValues[comp + prevIndex * valuesPerKeyframe];
            float current = samplerValues[comp + nextIndex * valuesPerKeyframe];
            weights[comp] = (1 - t) * previous + t * current;
        }
    }

    // 将权重应用到 RenderableManager
    auto ci = renderableManager->getInstance(channel.targetEntity);
    renderableManager->setMorphWeights(ci, weights.data(), weights.size());
}
```

### 变形目标的 GPU 实现

```glsl
// 在顶点着色器中的变形计算
attribute vec3 position;
attribute vec3 normal;

// Morph target 数据（通过 MorphTargetBuffer 提供）
uniform float morphWeights[MAX_MORPH_TARGETS];

void main() {
    vec3 morphedPosition = position;
    vec3 morphedNormal = normal;

    // 应用所有激活的 morph targets
    for (int i = 0; i < numActiveMorphTargets; ++i) {
        float weight = morphWeights[i];
        if (weight != 0.0) {
            morphedPosition += weight * getMorphTargetPosition(i);
            morphedNormal += weight * getMorphTargetNormal(i);
        }
    }

    // 继续进行骨骼变换和其他处理
    gl_Position = projectionMatrix * modelViewMatrix * vec4(morphedPosition, 1.0);
}
```

## 动画混合系统

### 交叉淡化实现

基于源码 (`libs/gltfio/src/Animator.cpp:347-401`)：

```cpp
void Animator::applyCrossFade(size_t previousAnimIndex, float previousAnimTime, float alpha) {
    // 1. 保存当前变换状态
    mImpl->stashCrossFade();

    // 2. 应用前一个动画到当前时间
    applyAnimation(previousAnimIndex, previousAnimTime);

    // 3. 与保存的状态进行混合
    mImpl->applyCrossFade(alpha);
}

void AnimatorImpl::stashCrossFade() {
    using Instance = TransformManager::Instance;
    auto& tm = *this->transformManager;
    auto& stash = this->crossFade;

    // 递归计算需要保存的节点数量
    auto recursiveCount = [&tm](Instance node, size_t count, auto& fn) -> size_t {
        ++count;
        for (auto iter = tm.getChildrenBegin(node); iter != tm.getChildrenEnd(node); ++iter) {
            count = fn(*iter, count, fn);
        }
        return count;
    };

    // 递归保存变换矩阵
    auto recursiveStash = [&tm, &stash](Instance node, size_t index, auto& fn) -> size_t {
        stash[index++] = tm.getTransform(node);
        for (auto iter = tm.getChildrenBegin(node); iter != tm.getChildrenEnd(node); ++iter) {
            index = fn(*iter, index, fn);
        }
        return index;
    };

    // 从根节点开始保存
    const Entity rootEntity = instance ? instance->getRoot() : asset->mRoot;
    const Instance root = tm.getInstance(rootEntity);
    const size_t count = recursiveCount(root, 0, recursiveCount);

    crossFade.reserve(count);
    crossFade.resize(count);
    recursiveStash(root, 0, recursiveStash);
}

void AnimatorImpl::applyCrossFade(float alpha) {
    using Instance = TransformManager::Instance;
    auto& tm = *this->transformManager;
    auto& stash = this->crossFade;

    // 递归应用混合
    auto recursiveFn = [&tm, &stash, alpha](Instance node, size_t index, auto& fn) -> size_t {
        float3 scale0, scale1;
        quatf rotation0, rotation1;
        float3 translation0, translation1;

        // 分解保存的变换（alpha=1 时的状态）
        decomposeMatrix(stash[index++], &translation1, &rotation1, &scale1);

        // 分解当前变换（alpha=0 时的状态）
        decomposeMatrix(tm.getTransform(node), &translation0, &rotation0, &scale0);

        // 分别对每个组件进行插值
        const float3 scale = mix(scale0, scale1, alpha);
        const quatf rotation = slerp(rotation0, rotation1, alpha);
        const float3 translation = mix(translation0, translation1, alpha);

        // 重新组合并设置变换
        tm.setTransform(node, composeMatrix(translation, rotation, scale));

        // 递归处理子节点
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

### 混合数学原理

```cpp
// TRS 组件的混合公式
Transform blend(const Transform& a, const Transform& b, float alpha) {
    Transform result;

    // 平移：线性插值
    result.translation = (1 - alpha) * a.translation + alpha * b.translation;

    // 旋转：球面线性插值（避免万向锁）
    result.rotation = slerp(a.rotation, b.rotation, alpha);

    // 缩放：线性插值
    result.scale = (1 - alpha) * a.scale + alpha * b.scale;

    return result;
}

// 四元数球面线性插值
quatf slerp(const quatf& q1, const quatf& q2, float t) {
    float dot = q1.x*q2.x + q1.y*q2.y + q1.z*q2.z + q1.w*q2.w;

    // 选择较短路径
    quatf q2_corrected = (dot < 0) ? -q2 : q2;
    dot = abs(dot);

    if (dot > 0.9995f) {
        // 接近平行，使用线性插值避免数值不稳定
        return normalize((1-t) * q1 + t * q2_corrected);
    }

    float theta = acos(dot);
    float sinTheta = sin(theta);
    float w1 = sin((1-t) * theta) / sinTheta;
    float w2 = sin(t * theta) / sinTheta;

    return w1 * q1 + w2 * q2_corrected;
}
```

## TRS 变换管理

### TrsTransformManager 设计

```cpp
// TRS (Translation, Rotation, Scale) 分离存储
class TrsTransformManager {
    std::vector<float3> mTranslations;  // 平移向量
    std::vector<quatf> mRotations;      // 旋转四元数
    std::vector<float3> mScales;        // 缩放向量

public:
    // 分别设置 TRS 组件
    void setTranslation(Instance ci, const float3& translation);
    void setRotation(Instance ci, const quatf& rotation);
    void setScale(Instance ci, const float3& scale);

    // 组合成变换矩阵
    mat4f getTransform(Instance ci) const;
};

mat4f TrsTransformManager::getTransform(Instance ci) const {
    const float3& t = mTranslations[ci.getId()];
    const quatf& r = mRotations[ci.getId()];
    const float3& s = mScales[ci.getId()];

    // 组合 TRS 矩阵：M = T * R * S
    return composeMatrix(t, r, s);
}

mat4f composeMatrix(const float3& translation, const quatf& rotation, const float3& scale) {
    // 从四元数构建旋转矩阵
    mat3f rotMatrix = mat3f(rotation);

    // 构建完整的 4x4 变换矩阵
    return mat4f(
        scale.x * rotMatrix[0][0], scale.x * rotMatrix[0][1], scale.x * rotMatrix[0][2], translation.x,
        scale.y * rotMatrix[1][0], scale.y * rotMatrix[1][1], scale.y * rotMatrix[1][2], translation.y,
        scale.z * rotMatrix[2][0], scale.z * rotMatrix[2][1], scale.z * rotMatrix[2][2], translation.z,
        0.0f,                      0.0f,                      0.0f,                      1.0f
    );
}
```

### 批量变换更新

```cpp
// 事务系统优化批量更新
void TransformManager::openLocalTransformTransaction() {
    mInTransaction = true;
    mDirtyTransforms.clear();
}

void TransformManager::commitLocalTransformTransaction() {
    if (mInTransaction) {
        // 批量更新所有脏标记的变换
        updateWorldTransforms(mDirtyTransforms);
        mInTransaction = false;
    }
}

void TransformManager::setTransform(Instance ci, const mat4f& localTransform) {
    mLocalTransforms[ci.getId()] = localTransform;

    if (mInTransaction) {
        mDirtyTransforms.insert(ci);
    } else {
        // 立即更新世界变换
        updateWorldTransform(ci);
    }
}
```

## 总结

Filament 动画系统实现的关键特点：

### 架构设计
1. **PIMPL 模式**: 隐藏实现细节，保持 ABI 稳定
2. **数据驱动**: 基于 glTF 标准的动画数据格式
3. **组件分离**: TRS 组件独立存储和处理
4. **批量优化**: 事务系统减少重复计算

### 性能优化
1. **时间查找**: 红黑树 O(log n) 时间复杂度
2. **内存布局**: 线性存储动画值，提高缓存效率
3. **批量更新**: 变换事务减少中间计算
4. **懒惰求值**: 只在需要时计算世界变换

### 功能完整性
1. **多种插值**: 线性、阶梯、三次样条
2. **骨骼动画**: 完整的蒙皮系统
3. **变形动画**: 支持 morph targets
4. **动画混合**: 交叉淡化和状态转换

### 扩展能力
1. **实例化支持**: 独立的动画状态
2. **多后端兼容**: 抽象的渲染接口
3. **组件化设计**: 易于添加新功能
4. **标准兼容**: 完全支持 glTF 2.0 规范

这个实现为 Filament 提供了强大而高效的动画能力，同时保持了良好的可维护性和扩展性。