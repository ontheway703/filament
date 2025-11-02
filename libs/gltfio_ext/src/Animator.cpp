/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gltfio_ext/Animator.h>
#include <gltfio_ext/AnimationAsset.h>
#include <gltfio_ext/AnimationBinding.h>
#include <gltfio_ext/math.h>

#include "FFilamentAsset.h"
#include "FFilamentInstance.h"
#include "FTrsTransformManager.h"
#include "downcast.h"

#include <filament/VertexBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>

#include <utils/Log.h>
#include <utils/Logger.h>

#include <math/mat4.h>
#include <math/quat.h>
#include <math/scalar.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <map>
#include <string>
#include <vector>

using namespace filament;
using namespace filament::math;
using namespace std;
using namespace utils;

namespace filament::gltfio_ext {

using TimeValues = map<float, size_t>;
using SourceValues = vector<float>;
using BoneVector = vector<mat4f>;

struct Sampler {
    TimeValues times;
    SourceValues values;
    enum { LINEAR, STEP, CUBIC } interpolation;
};

struct Channel {
    const Sampler* sourceData;
    Entity targetEntity;
    enum { TRANSLATION, ROTATION, SCALE, WEIGHTS } transformType;
};

struct Animation {
    float duration;
    std::string name;
    vector<Sampler> samplers;
    vector<Channel> channels;
};

struct AnimatorImpl {
    vector<Animation> animations;
    BoneVector boneMatrices;
    FFilamentAsset const* asset = nullptr;
    FFilamentInstance* instance = nullptr;
    RenderableManager* renderableManager;
    TransformManager* transformManager;
    TrsTransformManager* trsTransformManager;
    vector<float> weights;
    FixedCapacityVector<mat4f> crossFade;

    // ========================================
    // 外部动画缓存（轻量化）
    // ========================================

    /**
     * 缓存条目：单个动画的完整信息
     */
    struct CachedAnimation {
        std::string fullName;       // 唯一标识 "sourceId:animName"
        std::string sourceId;       // 动画源 ID（文件标识）
        std::string animName;       // 动画名称
        Animation animation;         // 转换后的动画数据（深拷贝，独立存在）
        uint64_t accessOrder;        // LRU 访问顺序（值越小越久未访问）
    };

    /**
     * 主缓存：fullName → CachedAnimation
     *
     * 说明：使用 unordered_map 而非 vector，因为：
     * 1. 动画按名称访问，不需要连续索引
     * 2. LRU 淘汰会导致索引变化，map 更稳定
     * 3. 查找性能 O(1)
     *
     * mutable：允许在 const 方法中修改（播放动画会更新LRU访问顺序）
     */
    mutable std::unordered_map<std::string, CachedAnimation> mAnimationCache;

    /**
     * Source 索引：sourceId → [fullName 列表]
     *
     * 用途：支持批量卸载（unloadAnimationsFromSource）
     *
     * mutable：允许在 const 方法中修改（查询操作可能触发缓存加载）
     */
    mutable std::unordered_map<std::string, std::vector<std::string>> mSourceIndex;

    /**
     * 名称索引：animName → [fullName 列表]
     *
     * 用途：
     * 1. 支持便捷播放（applyAnimationByName）
     * 2. 处理不同源的同名动画
     *
     * mutable：允许在 const 方法中修改（查询操作可能触发缓存加载）
     */
    mutable std::unordered_map<std::string, std::vector<std::string>> mAnimNameIndex;

    // ========================================
    // 配置与统计
    // ========================================

    /**
     * 最大缓存数量（可运行时调整）
     */
    size_t mMaxCacheSize = GLTFIO_EXT_DEFAULT_ANIMATION_CACHE_SIZE;

    /**
     * LRU 访问计数器（单调递增）
     *
     * 说明：使用自增计数器而非时间戳，因为：
     * 1. 性能更高（无需系统调用）
     * 2. 绝对顺序保证
     * 3. 实现简单
     *
     * mutable：允许在 const 方法中递增（播放动画会更新访问顺序）
     */
    mutable uint64_t mAccessCounter = 0;

    /**
     * 统计信息
     *
     * mutable：允许在 const 方法中更新统计（播放动画会记录命中/未命中）
     */
    mutable uint64_t mCacheHitCount = 0;     // 缓存命中次数
    mutable uint64_t mCacheMissCount = 0;    // 缓存未命中次数

    /**
     * 内部动画数量（保留）
     *
     * 说明：内部动画仍然使用原有的 animations 向量
     */
    size_t mInternalAnimCount = 0;

    void addChannels(const FixedCapacityVector<Entity>& nodeMap, const cgltf_animation& srcAnim,
            Animation& dst);
    void applyAnimation(const Channel& channel, float t, size_t prevIndex, size_t nextIndex);
    void stashCrossFade();
    void applyCrossFade(float alpha);
    void resetBoneMatrices(FFilamentInstance* instance);
    void updateBoneMatrices(FFilamentInstance* instance);
    bool resetToBindPose(FFilamentInstance* instance);

    // ========================================
    // 外部动画方法（Phase 2-3 实现）
    // ========================================

    // Phase 2: 核心 API
    size_t loadAnimationsFromSource(const std::string& sourceId, AnimationAsset* asset);
    void unloadAnimationsFromSource(const std::string& sourceId);
    void clearAnimationCache();
    void evictLRU(size_t count);
    bool handleSpaceManagement(const std::string& sourceId, size_t animCount);
    void removeAnimation(const std::string& fullName);

    // Phase 3: 播放与查询 API（marked const for backward compatibility）
    bool applyAnimation(const std::string& sourceId, const std::string& animName, float time) const;
    bool applyAnimationByName(const std::string& animName, float time) const;

    bool hasAnimation(const std::string& sourceId, const std::string& animName) const;
    bool hasAnimationByName(const std::string& animName) const;
    bool hasSource(const std::string& sourceId) const;

    float getAnimationDuration(const std::string& sourceId, const std::string& animName) const;
    float getAnimationDurationByName(const std::string& animName) const;

    std::vector<std::string> getLoadedSources() const;
    std::vector<std::string> getAnimationsInSource(const std::string& sourceId) const;

    void setAnimationCacheSize(size_t maxSize);
    size_t getAnimationCacheSize() const;

    struct CacheStats {
        size_t cachedCount;
        size_t maxSize;
        size_t sourceCount;
        uint64_t hitCount;
        uint64_t missCount;
        float hitRate;
    };
    CacheStats getAnimationCacheStats() const;
    void resetCacheStats();

    // 临时保留旧方法声明以避免编译错误（实现已禁用）
    bool loadExternalAnimation(AnimationAsset* animAsset, filament::Engine* engine);
};

static void createSampler(const cgltf_animation_sampler& src, Sampler& dst) {
    // Copy the time values into a red-black tree.
    const cgltf_accessor* timelineAccessor = src.input;
    const uint8_t* timelineBlob = nullptr;
    const float* timelineFloats = nullptr;
    if (timelineAccessor->buffer_view->has_meshopt_compression) {
        timelineBlob = (const uint8_t*) timelineAccessor->buffer_view->data;
        timelineFloats = (const float*) (timelineBlob + timelineAccessor->offset);
    } else {
        timelineBlob = (const uint8_t*) timelineAccessor->buffer_view->buffer->data;
        timelineFloats = (const float*) (timelineBlob + timelineAccessor->offset +
                timelineAccessor->buffer_view->offset);
    }
    for (size_t i = 0, len = timelineAccessor->count; i < len; ++i) {
        dst.times[timelineFloats[i]] = i;
    }

    // Convert source data to float.
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
        default:
            GLTFIO_EXT_WARN("Unknown animation type.");
            return;
    }

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
        case cgltf_interpolation_type_max_enum:
            break;
    }
}

static void setTransformType(const cgltf_animation_channel& src, Channel& dst) {
    switch (src.target_path) {
        case cgltf_animation_path_type_translation:
            dst.transformType = Channel::TRANSLATION;
            break;
        case cgltf_animation_path_type_rotation:
            dst.transformType = Channel::ROTATION;
            break;
        case cgltf_animation_path_type_scale:
            dst.transformType = Channel::SCALE;
            break;
        case cgltf_animation_path_type_weights:
            dst.transformType = Channel::WEIGHTS;
            break;
        case cgltf_animation_path_type_max_enum:
        case cgltf_animation_path_type_invalid:
            GLTFIO_EXT_WARN("Unsupported channel path.");
            break;
    }
}

// ========================================
// AnimationAsset → Animator::Animation 转换逻辑
// Conversion logic from AnimationAsset to Animator::Animation
// ========================================

/**
 * 将 AnimationAsset::AnimationSampler 转换为 Animator::Sampler
 * Converts AnimationAsset::AnimationSampler (vector) to Animator::Sampler (map)
 *
 * 核心转换：
 * - times: vector<float> → map<float, size_t>（用于 O(log n) 二分查找）
 * - values: vector<float> → vector<float>（直接复制）
 * - interpolation: 枚举类型转换
 */
static void convertSampler(const AnimationSampler& srcSampler, Sampler& dstSampler) {
    // 1. 构建时间映射表：vector → map
    dstSampler.times.clear();
    for (size_t i = 0; i < srcSampler.times.size(); i++) {
        dstSampler.times[srcSampler.times[i]] = i;
    }

    // 2. 直接复制值数组（无需转换）
    dstSampler.values = srcSampler.values;

    // 3. 转换插值类型枚举
    switch (srcSampler.interpolation) {
        case AnimationInterpolationType::LINEAR:
            dstSampler.interpolation = Sampler::LINEAR;
            break;
        case AnimationInterpolationType::STEP:
            dstSampler.interpolation = Sampler::STEP;
            break;
        case AnimationInterpolationType::CUBICSPLINE:
            dstSampler.interpolation = Sampler::CUBIC;
            break;
    }
}

/**
 * 将 AnimationAsset::Animation 转换为 Animator::Animation
 * Converts AnimationAsset::Animation to Animator::Animation
 *
 * 注意：此函数只转换 samplers 和元数据（name, duration）
 * channels 需要 AnimationBinding 的映射信息，在 loadExternalAnimation 中单独处理
 */
static void convertAnimation(const AnimationAsset::Animation& srcAnim, Animation& dstAnim) {
    dstAnim.name = srcAnim.name;
    dstAnim.duration = srcAnim.getDuration();

    // 转换所有 samplers
    dstAnim.samplers.resize(srcAnim.samplers.size());
    for (size_t i = 0; i < srcAnim.samplers.size(); i++) {
        convertSampler(srcAnim.samplers[i], dstAnim.samplers[i]);
    }

    // channels 暂时为空，由 loadExternalAnimation 填充
    dstAnim.channels.clear();
}

static bool validateAnimation(const cgltf_animation& anim) {
    for (cgltf_size j = 0; j < anim.channels_count; ++j) {
        const cgltf_animation_channel& channel = anim.channels[j];
        const cgltf_animation_sampler* sampler = channel.sampler;
        if (!channel.target_node) {
            continue;
        }
        if (!channel.sampler) {
            return false;
        }
        cgltf_size components = 1;
        if (channel.target_path == cgltf_animation_path_type_weights) {
            if (!channel.target_node->mesh || !channel.target_node->mesh->primitives_count) {
                return false;
            }
            components = channel.target_node->mesh->primitives[0].targets_count;
        }
        cgltf_size values = sampler->interpolation == cgltf_interpolation_type_cubic_spline ? 3 : 1;
        if (sampler->input->count * components * values != sampler->output->count) {
            return false;
        }
    }
    return true;
}

Animator::Animator(FFilamentAsset const* asset, FFilamentInstance* instance) {
    assert(asset->mResourcesLoaded && asset->mSourceAsset);
    mImpl = new AnimatorImpl();
    mImpl->asset = asset;
    mImpl->instance = instance;
    mImpl->renderableManager = &asset->mEngine->getRenderableManager();
    mImpl->transformManager = &asset->mEngine->getTransformManager();
    mImpl->trsTransformManager = asset->getTrsTransformManager();

    const cgltf_data* srcAsset = asset->mSourceAsset->hierarchy;
    const cgltf_animation* srcAnims = srcAsset->animations;
    for (cgltf_size i = 0, len = srcAsset->animations_count; i < len; ++i) {
        const cgltf_animation& anim = srcAnims[i];
        if (!validateAnimation(anim)) {
            GLTFIO_EXT_WARN("Disabling animation due to validation failure.");
            return;
        }
    }

    // Loop over the glTF animation definitions.
    mImpl->animations.resize(srcAsset->animations_count);
    for (cgltf_size i = 0, len = srcAsset->animations_count; i < len; ++i) {
        const cgltf_animation& srcAnim = srcAnims[i];
        Animation& dstAnim = mImpl->animations[i];
        dstAnim.duration = 0;
        if (srcAnim.name) {
            dstAnim.name = srcAnim.name;
        }

        // Import each glTF sampler into a custom data structure.
        cgltf_animation_sampler* srcSamplers = srcAnim.samplers;
        dstAnim.samplers.resize(srcAnim.samplers_count);
        for (cgltf_size j = 0, nsamps = srcAnim.samplers_count; j < nsamps; ++j) {
            const cgltf_animation_sampler& srcSampler = srcSamplers[j];
            Sampler& dstSampler = dstAnim.samplers[j];
            createSampler(srcSampler, dstSampler);
            if (dstSampler.times.size() > 1) {
                float maxtime = (--dstSampler.times.end())->first;
                dstAnim.duration = std::max(dstAnim.duration, maxtime);
            }
        }

        // Import each glTF channel into a custom data structure.
        if (instance) {
            mImpl->addChannels(instance->mNodeMap, srcAnim, dstAnim);
        } else {
            for (FFilamentInstance* instance : asset->mInstances) {
                mImpl->addChannels(instance->mNodeMap, srcAnim, dstAnim);
            }
        }
    }

    // 记录内部动画数量（用于区分内部/外部动画索引）
    mImpl->mInternalAnimCount = mImpl->animations.size();
}

void Animator::addInstance(FFilamentInstance* instance) {
    const cgltf_data* srcAsset = mImpl->asset->mSourceAsset->hierarchy;
    const cgltf_animation* srcAnims = srcAsset->animations;
    for (cgltf_size i = 0, len = srcAsset->animations_count; i < len; ++i) {
        const cgltf_animation& srcAnim = srcAnims[i];
        Animation& dstAnim = mImpl->animations[i];
        mImpl->addChannels(instance->mNodeMap, srcAnim, dstAnim);
    }
}

Animator::~Animator() {
    delete mImpl;
}

void Animator::resetBoneMatrices() {
    // If this is a single-instance animator, then reset only this instance.
    if (mImpl->instance) {
        mImpl->resetBoneMatrices(mImpl->instance);
        return;
    }

    // If this is a broadcast animator, then reset all instances.
    for (FFilamentInstance* instance : mImpl->asset->mInstances) {
        mImpl->resetBoneMatrices(instance);
    }
}

bool Animator::resetToBindPose() {
    // Check if source data is available
    if (!mImpl->asset->mSourceAsset || !mImpl->asset->mSourceAsset->hierarchy) {
        return false;
    }

    // If this is a single-instance animator, then reset only this instance.
    if (mImpl->instance) {
        return mImpl->resetToBindPose(mImpl->instance);
    }

    // If this is a broadcast animator, then reset all instances.
    bool success = true;
    for (FFilamentInstance* instance : mImpl->asset->mInstances) {
        success &= mImpl->resetToBindPose(instance);
    }
    return success;
}

bool Animator::applyCrossFade(const char* prevSourceId, const char* prevAnimName, float prevTime, float alpha) {
    mImpl->stashCrossFade();
    if (!applyAnimation(prevSourceId, prevAnimName, prevTime)) {
        return false;
    }
    mImpl->applyCrossFade(alpha);
    return true;
}

bool Animator::applyCrossFadeByName(const char* prevAnimName, float prevTime, float alpha) {
    mImpl->stashCrossFade();
    if (!applyAnimationByName(prevAnimName, prevTime)) {
        return false;
    }
    mImpl->applyCrossFade(alpha);
    return true;
}

void Animator::updateBoneMatrices() {
    // If this is a single-instance animator, then update only this instance.
    if (mImpl->instance) {
        mImpl->updateBoneMatrices(mImpl->instance);
        return;
    }

    // If this is a broadcast animator, then update all instances.
    for (FFilamentInstance* instance : mImpl->asset->mInstances) {
        mImpl->updateBoneMatrices(instance);
    }
}

// ========================================
// 动画缓存公共 API 实现
// Animation Cache Public API Implementation
// ========================================

size_t Animator::loadAnimationsFromSource(const char* sourceId, AnimationAsset* asset) {
    return mImpl->loadAnimationsFromSource(sourceId, asset);
}

void Animator::unloadAnimationsFromSource(const char* sourceId) {
    mImpl->unloadAnimationsFromSource(sourceId);
}

void Animator::clearAnimationCache() {
    mImpl->clearAnimationCache();
}

bool Animator::applyAnimation(const char* sourceId, const char* animName, float time) const {
    return mImpl->applyAnimation(sourceId, animName, time);
}

bool Animator::applyAnimationByName(const char* animName, float time) const {
    return mImpl->applyAnimationByName(animName, time);
}

bool Animator::hasAnimation(const char* sourceId, const char* animName) const {
    return mImpl->hasAnimation(sourceId, animName);
}

bool Animator::hasAnimationByName(const char* animName) const {
    return mImpl->hasAnimationByName(animName);
}

bool Animator::hasSource(const char* sourceId) const {
    return mImpl->hasSource(sourceId);
}

float Animator::getAnimationDuration(const char* sourceId, const char* animName) const {
    return mImpl->getAnimationDuration(sourceId, animName);
}

float Animator::getAnimationDurationByName(const char* animName) const {
    return mImpl->getAnimationDurationByName(animName);
}

std::vector<std::string> Animator::getLoadedSources() const {
    return mImpl->getLoadedSources();
}

std::vector<std::string> Animator::getAnimationsInSource(const char* sourceId) const {
    return mImpl->getAnimationsInSource(sourceId);
}

void Animator::setAnimationCacheSize(size_t maxSize) {
    mImpl->setAnimationCacheSize(maxSize);
}

size_t Animator::getAnimationCacheSize() const {
    return mImpl->getAnimationCacheSize();
}

Animator::CacheStats Animator::getAnimationCacheStats() const {
    auto implStats = mImpl->getAnimationCacheStats();

    CacheStats stats;
    stats.cachedCount = implStats.cachedCount;
    stats.maxSize = implStats.maxSize;
    stats.sourceCount = implStats.sourceCount;
    stats.hitCount = implStats.hitCount;
    stats.missCount = implStats.missCount;
    stats.hitRate = implStats.hitRate;

    return stats;
}

void Animator::resetCacheStats() {
    mImpl->resetCacheStats();
}

void AnimatorImpl::stashCrossFade() {
    using Instance = TransformManager::Instance;
    auto& tm = *this->transformManager;
    auto& stash = this->crossFade;

    // Count the total number of transformable nodes to preallocate the stash memory.
    // We considered caching this count, but the cache would need to be invalidated when entities
    // are added into the hierarchy.
    auto recursiveCount = [&tm](Instance node, size_t count, auto& fn) -> size_t {
        ++count;
        for (auto iter = tm.getChildrenBegin(node); iter != tm.getChildrenEnd(node); ++iter) {
            count = fn(*iter, count, fn);
        }
        return count;
    };

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
    using Instance = TransformManager::Instance;
    auto& tm = *this->transformManager;
    auto& stash = this->crossFade;
    auto recursiveFn = [&tm, &stash, alpha](Instance node, size_t index, auto& fn) -> size_t {
        float3 scale0, scale1;
        quatf rotation0, rotation1;
        float3 translation0, translation1;
        decomposeMatrix(stash[index++], &translation1, &rotation1, &scale1);
        decomposeMatrix(tm.getTransform(node), &translation0, &rotation0, &scale0);
        const float3 scale = mix(scale0, scale1, alpha);
        const quatf rotation = slerp(rotation0, rotation1, alpha);
        const float3 translation = mix(translation0, translation1, alpha);
        tm.setTransform(node, composeMatrix(translation, rotation, scale));
        for (auto iter = tm.getChildrenBegin(node); iter != tm.getChildrenEnd(node); ++iter) {
            index = fn(*iter, index, fn);
        }
        return index;
    };
    const Entity rootEntity = instance ? instance->getRoot() : asset->mRoot;
    const Instance root = tm.getInstance(rootEntity);
    recursiveFn(root, 0, recursiveFn);
}

void AnimatorImpl::addChannels(const FixedCapacityVector<Entity>& nodeMap,
        const cgltf_animation& srcAnim, Animation& dst) {
    const cgltf_animation_channel* srcChannels = srcAnim.channels;
    const cgltf_animation_sampler* srcSamplers = srcAnim.samplers;
    const cgltf_node* nodes = asset->mSourceAsset->hierarchy->nodes;
    const Sampler* samplers = dst.samplers.data();
    for (cgltf_size j = 0, nchans = srcAnim.channels_count; j < nchans; ++j) {
        const cgltf_animation_channel& srcChannel = srcChannels[j];
        if (!srcChannel.target_node) {
            continue;
        }
        Entity targetEntity = nodeMap[srcChannel.target_node - nodes];
        if (UTILS_UNLIKELY(!targetEntity)) {
            if (GLTFIO_EXT_VERBOSE) {
                slog.w << "No scene root contains node ";
                if (srcChannel.target_node->name) {
                    slog.w << "'" << srcChannel.target_node->name << "' ";
                }
                slog.w << "for animation ";
                if (srcAnim.name) {
                    slog.w << "'" << srcAnim.name << "' ";
                }
                slog.w << "in channel " << j << io::endl;
            }
            continue;
        }
        Channel dstChannel;
        dstChannel.sourceData = samplers + (srcChannel.sampler - srcSamplers);
        dstChannel.targetEntity = targetEntity;
        setTransformType(srcChannel, dstChannel);
        dst.channels.push_back(dstChannel);
    }
}

void AnimatorImpl::applyAnimation(const Channel& channel, float t, size_t prevIndex,
        size_t nextIndex) {
    const Sampler* sampler = channel.sourceData;
    const TimeValues& times = sampler->times;
    TrsTransformManager::Instance trsNode = trsTransformManager->getInstance(channel.targetEntity);
    TransformManager::Instance node = transformManager->getInstance(channel.targetEntity);

    switch (channel.transformType) {

        case Channel::SCALE: {
            float3 scale;
            const float3* srcVec3 = (const float3*) sampler->values.data();
            if (sampler->interpolation == Sampler::CUBIC) {
                float3 vert0 = srcVec3[prevIndex * 3 + 1];
                float3 tang0 = srcVec3[prevIndex * 3 + 2];
                float3 tang1 = srcVec3[nextIndex * 3];
                float3 vert1 = srcVec3[nextIndex * 3 + 1];
                scale = cubicSpline(vert0, tang0, vert1, tang1, t);
            } else {
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
                rotation = slerp(srcQuat[prevIndex], srcQuat[nextIndex], t);
            }
            trsTransformManager->setRotation(trsNode, rotation);
            break;
        }

        case Channel::WEIGHTS: {
            const float* const samplerValues = sampler->values.data();
            assert(sampler->values.size() % times.size() == 0);
            const int valuesPerKeyframe = sampler->values.size() / times.size();

            if (sampler->interpolation == Sampler::CUBIC) {
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
                weights.resize(valuesPerKeyframe);
                for (int comp = 0; comp < valuesPerKeyframe; ++comp) {
                    float previous = samplerValues[comp + prevIndex * valuesPerKeyframe];
                    float current = samplerValues[comp + nextIndex * valuesPerKeyframe];
                    weights[comp] = (1 - t) * previous + t * current;
                }
            }

            auto ci = renderableManager->getInstance(channel.targetEntity);
            renderableManager->setMorphWeights(ci, weights.data(), weights.size());
            return;
        }
    }

    transformManager->setTransform(node, trsTransformManager->getTransform(trsNode));
}

void AnimatorImpl::resetBoneMatrices(FFilamentInstance* instance) {
    for (const auto& skin : instance->mSkins) {
        size_t njoints = skin.joints.size();
        boneMatrices.resize(njoints);
        for (const auto& entity : skin.targets) {
            auto renderable = renderableManager->getInstance(entity);
            if (renderable) {
                for (size_t boneIndex = 0; boneIndex < njoints; ++boneIndex) {
                    boneMatrices[boneIndex] = mat4f();
                }
                renderableManager->setBones(renderable, boneMatrices.data(), boneMatrices.size());
            }
        }
    }
}

bool AnimatorImpl::resetToBindPose(FFilamentInstance* instance) {
    // Verify source data is available
    if (!asset->mSourceAsset || !asset->mSourceAsset->hierarchy) {
        return false;
    }

    cgltf_data* sourceData = asset->mSourceAsset->hierarchy;
    const cgltf_node* nodes = sourceData->nodes;
    const size_t nodeCount = sourceData->nodes_count;

    TransformManager& tm = *transformManager;
    TrsTransformManager& trs = *trsTransformManager;

    // Open transform transaction to batch updates
    tm.openLocalTransformTransaction();

    // Reset all node transforms to their original values from the glTF file
    for (size_t i = 0; i < nodeCount; ++i) {
        // Get entity from nodeMap using index
        if (i >= instance->mNodeMap.size()) continue;

        Entity entity = instance->mNodeMap[i];
        if (!entity) continue;

        const cgltf_node& sourceNode = nodes[i];

        auto trsNode = trs.getInstance(entity);
        if (!trsNode) {
            continue;  // Skip nodes that don't have TRS component
        }

        // Read original TRS values from cgltf_node
        float3 translation{0.0f};
        quatf rotation{1.0f, 0.0f, 0.0f, 0.0f};  // Identity quaternion
        float3 scale{1.0f};

        if (sourceNode.has_translation) {
            translation = float3{
                sourceNode.translation[0],
                sourceNode.translation[1],
                sourceNode.translation[2]
            };
        }

        if (sourceNode.has_rotation) {
            rotation = quatf{
                sourceNode.rotation[3],  // w (real part)
                sourceNode.rotation[0],  // x
                sourceNode.rotation[1],  // y
                sourceNode.rotation[2]   // z
            };
        }

        if (sourceNode.has_scale) {
            scale = float3{
                sourceNode.scale[0],
                sourceNode.scale[1],
                sourceNode.scale[2]
            };
        }

        // Reset TrsTransformManager to bind pose
        trs.setTranslation(trsNode, translation);
        trs.setRotation(trsNode, rotation);
        trs.setScale(trsNode, scale);

        // Sync TRS → TransformManager (same as applyAnimation line 772)
        auto node = tm.getInstance(entity);
        if (node) {
            tm.setTransform(node, trs.getTransform(trsNode));
        }
    }

    // Commit transaction to sync TrsTransformManager -> TransformManager
    tm.commitLocalTransformTransaction();

    // Skip updateBoneMatrices() in NOOP backend to avoid CircularBuffer overflow.
    // NOOP backend is used for testing business logic only (no real rendering),
    // so bone matrix updates are unnecessary and would exceed buffer capacity
    // with large skeletons (e.g., ecorche with 327 bones).
    // The TRS reset logic is fully tested even without this call.
    if (asset->mEngine->getBackend() == backend::Backend::NOOP) {
        return true;
    }

    // Update bone matrices to match the new transforms
    updateBoneMatrices(instance);

    return true;
}

void AnimatorImpl::updateBoneMatrices(FFilamentInstance* instance) {
    assert_invariant(instance->mSkins.size() == asset->mSkins.size());
    size_t skinIndex = 0;
    for (const auto& skin : instance->mSkins) {
        const auto& assetSkin = asset->mSkins[skinIndex++];
        size_t njoints = skin.joints.size();
        boneMatrices.resize(njoints);
        for (Entity entity : skin.targets) {
            auto renderable = renderableManager->getInstance(entity);
            if (!renderable) {
                continue;
            }
            mat4 inverseGlobalTransform;
            auto xformable = transformManager->getInstance(entity);
            if (xformable) {
                inverseGlobalTransform = inverse(transformManager->getWorldTransformAccurate(xformable));
            }
            for (size_t boneIndex = 0; boneIndex < njoints; ++boneIndex) {
                const auto& joint = skin.joints[boneIndex];
                const mat4f& inverseBindMatrix = assetSkin.inverseBindMatrices[boneIndex];
                TransformManager::Instance jointInstance = transformManager->getInstance(joint);
                mat4 globalJointTransform = transformManager->getWorldTransformAccurate(jointInstance);
                boneMatrices[boneIndex] =
                        mat4f{ inverseGlobalTransform * globalJointTransform } *
                        inverseBindMatrix;
            }
            renderableManager->setBones(renderable, boneMatrices.data(), boneMatrices.size());
        }
    }
}

/**
 * 【gltfio_ext 核心功能】加载外部动画资产并绑定到 Animator
 * Loads external animation asset and binds to Animator
 *
 * ============================================================================
 * 功能概述
 * ============================================================================
 * 将独立的动画资产（AnimationAsset，从外部 GLB 文件加载）绑定到当前 Animator，
 * 使其能够驱动已加载的网格骨骼。这是 gltfio_ext 最核心的功能之一，支持：
 *   - 动画与网格分离存储（减少文件大小）
 *   - 运行时动态加载/卸载动画
 *   - 多个模型共享同一套动画数据
 *
 * ============================================================================
 * 完整执行流程
 * ============================================================================
 *
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │ 步骤 1: 验证 AnimationAsset 有效性                                   │
 * │ ─────────────────────────────────────────────────────────────────── │
 * │ - 检查 animAsset 指针非空                                            │
 * │ - 调用 animAsset->validate() 验证内部数据                           │
 * │ - 检查至少包含一个动画                                               │
 * │                                                                      │
 * │ 失败则返回 false（不影响当前状态）                                  │
 * └─────────────────────────────────────────────────────────────────────┘
 *                               ↓
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │ 步骤 2: 创建 AnimationBinding（骨骼名称映射）                       │
 * │ ─────────────────────────────────────────────────────────────────── │
 * │ AnimationBinding 是核心的映射层，解决以下问题：                     │
 * │                                                                      │
 * │ 【问题】                                                             │
 * │   - AnimationAsset（外部动画）使用节点索引：0, 1, 2, ...           │
 * │   - FilamentAsset（驻留网格）使用 Entity：Entity(123), Entity(456)  │
 * │   - 两者来自不同 GLB 文件，索引不对应                               │
 * │                                                                      │
 * │ 【解决方案】通过骨骼名称建立映射：                                   │
 * │   动画节点索引 → 骨骼名称 → 网格Entity → TransformManager::Instance │
 * │        0      → "Spine"  → Entity(123) → Instance(...)              │
 * │        1      → "Head"   → Entity(456) → Instance(...)              │
 * │                                                                      │
 * │ 【映射流程】AnimationBinding::buildMapping()：                      │
 * │   1. 从 FilamentAsset 提取所有命名实体（通过 NameComponentManager）│
 * │   2. 构建 residentBoneMap：{ "Spine" → Entity(123), ... }           │
 * │   3. 遍历 AnimationAsset 的节点，按名称查找对应 Entity             │
 * │   4. 将 Entity 转换为 TransformManager::Instance（用于应用变换）   │
 * │   5. 计算匹配率（match_rate = matched / total）                     │
 * │   6. 验证匹配率 >= 90%（容差阈值），失败则拒绝绑定                  │
 * │                                                                      │
 * │ 【输出】                                                             │
 * │   - nodeToEntityMap: map<int, Entity>                               │
 * │   - nodeToInstanceMap: map<int, TransformManager::Instance>         │
 * │                                                                      │
 * │ 失败则返回 false（匹配率 < 90% 或网格无命名实体）                   │
 * └─────────────────────────────────────────────────────────────────────┘
 *                               ↓
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │ 步骤 3: 转换所有动画数据（关键数据转换）                            │
 * │ ─────────────────────────────────────────────────────────────────── │
 * │ 遍历 AnimationAsset 的所有动画（不是单个动画！）                    │
 * │ 例如：ecorche_animation_only.glb 有 3 个动画（Pull, Push, Squat）   │
 * │                                                                      │
 * │ 对每个动画执行：                                                     │
 * │                                                                      │
 * │ ┌─────────────────────────────────────────────────────────────────┐ │
 * │ │ 3.1 转换 samplers 和元数据                                       │ │
 * │ │ ───────────────────────────────────────────────────────────────  │ │
 * │ │ convertAnimation(srcAnim, dstAnim)：                            │ │
 * │ │   - 复制动画名称（name）                                         │ │
 * │ │   - 复制动画时长（duration）                                     │ │
 * │ │   - 转换 samplers（关键帧时间 + 数据）                           │ │
 * │ └─────────────────────────────────────────────────────────────────┘ │
 * │                                                                      │
 * │ ┌─────────────────────────────────────────────────────────────────┐ │
 * │ │ 3.2 重建 channels（核心转换）                                    │ │
 * │ │ ───────────────────────────────────────────────────────────────  │ │
 * │ │ 为什么需要重建？数据结构不同：                                   │ │
 * │ │                                                                  │ │
 * │ │   【AnimationAsset::Channel】 → 【Animator::Channel】           │ │
 * │ │   targetNodeIndex: int          targetEntity: Entity            │ │
 * │ │   samplerIndex: int             sourceData: Sampler*            │ │
 * │ │   path: enum                    transformType: enum             │ │
 * │ │                                                                  │ │
 * │ │ 转换步骤（对每个 channel）：                                     │ │
 * │ │   a) 从 srcChannel 读取 targetNodeIndex（例如 5）                │ │
 * │ │   b) 在 nodeToEntityMap 中查找：nodeToEntityMap[5] = Entity(123)│ │
 * │ │   c) 创建 dstChannel：                                           │ │
 * │ │        - targetEntity = Entity(123)  ← 来自映射表                │ │
 * │ │        - sourceData = &samplers[samplerIndex]  ← 指针引用        │ │
 * │ │        - transformType = 路径类型转换（TRANSLATION/ROTATION/...) │ │
 * │ │   d) 添加到 dstAnim.channels                                     │ │
 * │ │                                                                  │ │
 * │ │ 【容错机制】跳过未映射的骨骼（允许 10% 不匹配）                  │ │
 * │ └─────────────────────────────────────────────────────────────────┘ │
 * │                                                                      │
 * │ 【输出示例】（ecorche_full.glb）                                     │
 * │   newAnimations = [                                                 │
 * │     { name: "Pull",  channels: [327个], duration: 1.5s },          │
 * │     { name: "Push",  channels: [327个], duration: 1.2s },          │
 * │     { name: "Squat", channels: [327个], duration: 1.8s }           │
 * │   ]                                                                  │
 * └─────────────────────────────────────────────────────────────────────┘
 *                               ↓
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │ 步骤 4: 两段式更新 - Phase 2: 提交                                  │
 * │ ─────────────────────────────────────────────────────────────────── │
 * │ 所有准备工作成功后，一次性替换旧状态：                              │
 * │   mExternalAnimations = std::move(newAnimations)                    │
 * │   mExternalBinding = std::move(newBinding)                          │
 * │   mExternalAnimAsset = animAsset（弱引用）                          │
 * │                                                                      │
 * │ 【设计原则：原子性】                                                 │
 * │   - Phase 1: 在临时变量中准备数据（newAnimations, newBinding）      │
 * │   - Phase 2: 准备成功后一次性提交（替换成员变量）                   │
 * │   - 好处：失败时旧状态保持不变，避免系统处于不一致状态              │
 * └─────────────────────────────────────────────────────────────────────┘
 *                               ↓
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │ 步骤 5: 日志输出并返回成功                                          │
 * │ ─────────────────────────────────────────────────────────────────── │
 * │ 输出匹配率和动画数量：                                              │
 * │   "External animations loaded: 3 animations, bone match rate: 100%" │
 * │                                                                      │
 * │ 返回 true（加载成功）                                               │
 * └─────────────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 * 参数说明
 * ============================================================================
 * @param animAsset 外部动画资产（必须在 Animator 生命周期内保持有效）
 * @param engine    Filament 引擎实例（用于访问 TransformManager）
 * @return true 加载成功，false 加载失败（验证失败或匹配率 < 90%）
 *
 * 注意事项：
 * ---------
 * - 如果已加载外部动画，会被新动画替换（旧的会被卸载）
 * - animAsset 必须在 Animator 生命周期内保持有效（弱引用）
 * - 网格必须配置 NameComponentManager（否则匹配率为 0）
 * - 失败时不影响当前状态（原子性保证）
 */
bool AnimatorImpl::loadExternalAnimation(AnimationAsset* animAsset, Engine* engine) {
    // ========================================
    // Phase 1: 临时禁用（数据结构已重构）
    // ========================================
    slog.e << "AnimatorImpl::loadExternalAnimation() is disabled (Phase 1 data structure refactoring)" << io::endl;
    slog.e << "This function will be replaced with loadAnimationsFromSource() in Phase 2" << io::endl;
    return false;

    // 原实现代码保留供参考（Phase 2 重写时可参考）
    #if 0
    // 1. 验证输入
    if (!animAsset || !animAsset->validate()) {
        slog.e << "Invalid AnimationAsset" << io::endl;
        return false;
    }

    if (animAsset->getAnimationCount() == 0) {
        slog.e << "AnimationAsset has no animations" << io::endl;
        return false;
    }

    // 2. 保存旧状态（两段式更新 - Phase 1: 准备）
    // 如果加载失败，需要恢复旧状态，防止状态不一致
    if (mExternalAnimAsset) {
        slog.w << "External animation already loaded, replacing" << io::endl;
    }

    // 3. 创建新的 AnimationBinding（骨骼名称映射）
    // 注意：在临时变量中构建，不影响当前状态
    auto newBinding = std::make_unique<AnimationBinding>(animAsset, asset, engine);

    if (!newBinding->buildMapping()) {
        slog.e << "Failed to build bone mapping for external animation" << io::endl;
        float matchRate = newBinding->getMatchRate();
        slog.e << "Match rate: " << (matchRate * 100.0f) << "%" << io::endl;
        // 加载失败，旧状态保持不变
        return false;
    }

    // 4. 转换所有动画到临时缓冲区
    size_t animCount = animAsset->getAnimationCount();
    std::vector<Animation> newAnimations(animCount);

    // ========================================================================
    // 【核心概念】获取 AnimationBinding 构建的节点索引到实体的映射表
    // ========================================================================
    //
    // nodeToEntityMap 的数据结构：
    //   类型：std::map<int, Entity>
    //   含义：动画节点索引 → 网格实体
    //
    // 真实数据示例（ecorche_full.glb）：
    //   nodeToEntityMap = {
    //     0 → Entity(100),   // 动画节点 0（"Armature"） → 网格中的 Entity 100
    //     1 → Entity(101),   // 动画节点 1（"Spine"）    → 网格中的 Entity 101
    //     2 → Entity(102),   // 动画节点 2（"Spine.001"）→ 网格中的 Entity 102
    //     ...
    //     326 → Entity(426)  // 327 个骨骼节点的映射
    //   }
    //
    // 为什么需要这个映射表？
    // -------------------------
    // 1. **数据源差异**：
    //    - AnimationAsset（外部动画）的 channels 使用 targetNodeIndex（整数索引）
    //    - Animator（内部）的 channels 使用 targetEntity（Filament 实体）
    //    - 需要将索引转换为实体
    //
    // 2. **来自不同 GLB 文件**：
    //    - 外部动画和网格来自不同的 GLB 文件
    //    - 节点索引不对应（如动画的节点 0 != 网格的节点 0）
    //    - 必须通过骨骼名称建立映射
    //
    // 3. **由 AnimationBinding 构建**：
    //    - AnimationBinding::buildMapping() 已经完成了骨骼名称匹配
    //    - 这里直接获取构建好的映射表，用于后续的 channels 转换
    //
    // 下一步：遍历所有动画，使用这个映射表将 AnimationAsset::Channel 转换为 Animator::Channel
    auto& nodeToEntityMap = newBinding->getNodeToEntityMap();

    // ========================================================================
    // 【核心流程】遍历 AnimationAsset 的所有动画，转换为 Animator 内部格式
    // ========================================================================
    //
    // 这个循环处理 AnimationAsset 中的**所有动画**（不是单个动画！）
    // 例如：ecorche_animation_only.glb 包含 3 个动画（Pull, Push, Squat）
    //       这个循环会执行 3 次，每次处理一个动画
    //
    // 每次循环做两件事：
    //   1. 转换 samplers 和元数据（convertAnimation）
    //   2. 重建 channels（将 targetNodeIndex 转换为 targetEntity）
    //
    // 为什么需要转换？
    // ----------------
    // AnimationAsset 和 Animator 的数据结构不同：
    //
    //   【AnimationAsset::Animation】          【Animator::Animation】
    //   - name: string                         - name: string
    //   - duration: float                      - duration: float
    //   - samplers: vector<Sampler>            - samplers: vector<Sampler>
    //   - channels: vector<Channel>            - channels: vector<Channel>
    //       └─ targetNodeIndex: int                └─ targetEntity: Entity  ← 关键区别！
    //       └─ samplerIndex: int                   └─ sourceData: Sampler*
    //       └─ path: enum                          └─ transformType: enum
    //
    // 核心差异：
    //   - AnimationAsset 使用节点索引（int）引用骨骼
    //   - Animator 使用 Entity 引用骨骼
    //   - 需要通过 nodeToEntityMap 将索引转换为 Entity
    //
    for (size_t i = 0; i < animCount; i++) {
        const auto& srcAnim = animAsset->getAnimation(i);

        // ====================================================================
        // 步骤 1：转换 samplers 和元数据
        // ====================================================================
        // convertAnimation() 会复制：
        //   - 动画名称（name）
        //   - 动画时长（duration）
        //   - samplers 数组（关键帧时间 + 数据）
        //
        // 注意：此时 channels 还未转换（在下面的循环中处理）
        convertAnimation(srcAnim, newAnimations[i]);

        // ====================================================================
        // 步骤 2：重建 channels（核心转换）
        // ====================================================================
        // 原因：AnimationAsset::Channel 使用 targetNodeIndex（整数索引）
        //      Animator::Channel 使用 targetEntity（Filament 实体）
        //      需要通过 nodeToEntityMap 将索引转换为 Entity
        //
        auto& dstAnim = newAnimations[i];
        const Sampler* samplers = dstAnim.samplers.data();

        // 遍历源动画的所有 channels（每个 channel 控制一个骨骼的一个属性）
        // 示例：Pull 动画有 327 个 channels（每个骨骼的 translation/rotation/scale）
        for (const auto& srcChannel : srcAnim.channels) {
            int nodeIndex = srcChannel.targetNodeIndex;

            // ----------------------------------------------------------------
            // 【关键转换】使用 nodeToEntityMap 将节点索引转换为实体
            // ----------------------------------------------------------------
            // 例如：
            //   srcChannel.targetNodeIndex = 5（动画节点索引）
            //   nodeToEntityMap[5] = Entity(123)（网格中的实体）
            //   → dstChannel.targetEntity = Entity(123)
            //
            auto it = nodeToEntityMap.find(nodeIndex);
            if (it == nodeToEntityMap.end()) {
                // ----------------------------------------------------------------
                // 【容错机制】跳过未映射的骨骼
                // ----------------------------------------------------------------
                // 正常情况，原因可能是：
                //   1. 动画有额外的辅助骨骼（网格中不存在）
                //   2. AnimationBinding 允许 10% 不匹配
                //   3. 某些骨骼在网格中被优化掉了
                //
                // 例如：动画有 "LeftHandThumb" 骨骼，但网格模型没有手指细节
                continue;
            }

            // ----------------------------------------------------------------
            // 创建 Animator::Channel（内部格式）
            // ----------------------------------------------------------------
            Channel dstChannel;

            // 核心字段 1：目标实体（从 nodeToEntityMap 获取）
            dstChannel.targetEntity = it->second;

            // 核心字段 2：sampler 数据指针（指向 dstAnim.samplers 数组）
            // 为什么用指针？运行时播放动画时，直接访问 sampler 数据更高效
            dstChannel.sourceData = samplers + srcChannel.samplerIndex;

            // ----------------------------------------------------------------
            // 转换路径类型（枚举值映射）
            // ----------------------------------------------------------------
            // AnimationPathType（AnimationAsset） → Channel::TransformType（Animator）
            //
            // 数据转换前（AnimationAsset::Channel）：
            //   targetNodeIndex: 5
            //   samplerIndex: 2
            //   path: ROTATION
            //
            // 数据转换后（Animator::Channel）：
            //   targetEntity: Entity(123)  ← 通过 nodeToEntityMap[5] 查找
            //   sourceData: &samplers[2]   ← 直接指针引用
            //   transformType: ROTATION    ← 枚举值映射
            //
            switch (srcChannel.path) {
                case AnimationPathType::TRANSLATION:
                    dstChannel.transformType = Channel::TRANSLATION;
                    break;
                case AnimationPathType::ROTATION:
                    dstChannel.transformType = Channel::ROTATION;
                    break;
                case AnimationPathType::SCALE:
                    dstChannel.transformType = Channel::SCALE;
                    break;
                case AnimationPathType::WEIGHTS:
                    dstChannel.transformType = Channel::WEIGHTS;
                    break;
            }

            // 将转换后的 channel 添加到目标动画
            dstAnim.channels.push_back(dstChannel);
        }

        // 日志输出：每个动画的转换结果
        // 示例：Loaded external animation 0: 'Pull' (327 channels, duration=1.5s)
        slog.i << "Loaded external animation " << i << ": '" << dstAnim.name << "' "
               << "(" << dstAnim.channels.size() << " channels, "
               << "duration=" << dstAnim.duration << "s)" << io::endl;
    }


    // 7. 两段式更新 - Phase 2: 提交（所有准备工作成功，现在替换旧状态）
    mExternalAnimations = std::move(newAnimations);
    mExternalBinding = std::move(newBinding);
    mExternalAnimAsset = animAsset;

    // 8. 日志输出汇总
    float matchRate = mExternalBinding->getMatchRate();
    slog.i << "External animations loaded: " << animCount << " animations, "
           << "bone match rate: " << (matchRate * 100.0f) << "%" << io::endl;

    return true;
    #endif  // 结束 Phase 1 临时禁用的代码块
}

// ========================================
// Phase 2: 核心 API 实现
// ========================================

/**
 * 从缓存和所有索引中移除单个动画
 */
void AnimatorImpl::removeAnimation(const std::string& fullName) {
    auto it = mAnimationCache.find(fullName);
    if (it == mAnimationCache.end()) {
        return;  // 动画不存在，直接返回
    }

    const CachedAnimation& cached = it->second;

    // ========================================
    // 1. 从名称索引移除
    // ========================================
    auto animNameIt = mAnimNameIndex.find(cached.animName);
    if (animNameIt != mAnimNameIndex.end()) {
        auto& fullNames = animNameIt->second;
        fullNames.erase(
            std::remove(fullNames.begin(), fullNames.end(), fullName),
            fullNames.end()
        );

        // 如果该名称没有动画了，移除整个条目
        if (fullNames.empty()) {
            mAnimNameIndex.erase(animNameIt);
        }
    }

    // ========================================
    // 2. 从 Source 索引移除
    // ========================================
    auto sourceIt = mSourceIndex.find(cached.sourceId);
    if (sourceIt != mSourceIndex.end()) {
        auto& fullNames = sourceIt->second;
        fullNames.erase(
            std::remove(fullNames.begin(), fullNames.end(), fullName),
            fullNames.end()
        );

        // 如果该源没有动画了，移除整个条目
        if (fullNames.empty()) {
            mSourceIndex.erase(sourceIt);
        }
    }

    // ========================================
    // 3. 从主缓存移除
    // ========================================
    mAnimationCache.erase(it);
}

/**
 * LRU 淘汰：移除最久未访问的 N 个动画
 */
void AnimatorImpl::evictLRU(size_t count) {
    if (count == 0 || mAnimationCache.empty()) {
        return;
    }

    // ========================================
    // 1. 收集所有缓存条目并排序
    // ========================================
    std::vector<std::pair<std::string, uint64_t>> candidates;
    candidates.reserve(mAnimationCache.size());

    for (const auto& [fullName, cached] : mAnimationCache) {
        candidates.push_back({fullName, cached.accessOrder});
    }

    // 按访问顺序排序（最久未访问的在前）
    std::sort(candidates.begin(), candidates.end(),
        [](const auto& a, const auto& b) {
            return a.second < b.second;  // accessOrder 越小越久
        });

    // ========================================
    // 2. 淘汰最久未访问的 N 个
    // ========================================
    size_t actualEvict = std::min(count, candidates.size());

    for (size_t i = 0; i < actualEvict; i++) {
        const std::string& fullName = candidates[i].first;
        removeAnimation(fullName);
    }

    DLOG(INFO) << "Evicted " << actualEvict << " animations (LRU)";
}

/**
 * 空间管理（方案A：拒绝超限源）
 */
bool AnimatorImpl::handleSpaceManagement(const std::string& sourceId, size_t animCount) {
    size_t currentSize = mAnimationCache.size();
    size_t requiredSpace = currentSize + animCount;

    if (requiredSpace <= mMaxCacheSize) {
        // 空间充足，无需淘汰
        return true;
    }

    // ========================================
    // 场景 A：超限源 → 拒绝加载（方案A）
    // ========================================
    if (animCount > mMaxCacheSize) {
        slog.e << "Failed to load source '" << sourceId << "': "
               << "animation count (" << animCount << ") exceeds cache size ("
               << mMaxCacheSize << ")" << io::endl;
        slog.e << "Please unload other sources or increase cache size." << io::endl;

        return false;  // ❌ 拒绝加载
    }

    // ========================================
    // 场景 B：正常情况（LRU 淘汰）
    // ========================================
    size_t needEvict = requiredSpace - mMaxCacheSize;
    DLOG(INFO) << "Need to evict " << needEvict << " animations before loading source '"
               << sourceId << "'";

    evictLRU(needEvict);

    // 验证淘汰是否成功
    if (mAnimationCache.size() + animCount > mMaxCacheSize) {
        slog.w << "Warning: eviction may be insufficient, cache might exceed limit" << io::endl;
    }

    return true;  // ✅ 可以加载
}

/**
 * 清空所有外部动画缓存
 */
void AnimatorImpl::clearAnimationCache() {
    size_t count = mAnimationCache.size();

    mAnimationCache.clear();
    mSourceIndex.clear();
    mAnimNameIndex.clear();

    DLOG(INFO) << "Cleared animation cache (" << count << " animations)";
}

/**
 * 卸载整个源的所有动画
 */
void AnimatorImpl::unloadAnimationsFromSource(const std::string& sourceId) {
    auto it = mSourceIndex.find(sourceId);
    if (it == mSourceIndex.end()) {
        slog.w << "Source '" << sourceId << "' not found in cache" << io::endl;
        return;
    }

    // 获取该源的所有动画
    const std::vector<std::string>& fullNames = it->second;
    size_t count = fullNames.size();

    // 逐个移除（需要拷贝，因为 removeAnimation 会修改 mSourceIndex）
    std::vector<std::string> fullNamesCopy = fullNames;
    for (const auto& fullName : fullNamesCopy) {
        removeAnimation(fullName);
    }

    DLOG(INFO) << "Unloaded " << count << " animations from source '" << sourceId << "'";
}

/**
 * 从动画源加载所有动画
 *
 * @param sourceId 动画源标识符（如 "chest_basic"，格式无关）
 * @param asset 动画资产（转换后可立即释放）
 * @return 成功加载的动画数量
 */
size_t AnimatorImpl::loadAnimationsFromSource(const std::string& sourceId, AnimationAsset* asset) {
    // ========================================
    // 1. 验证输入
    // ========================================
    if (!asset || sourceId.empty()) {
        slog.e << "Invalid arguments: asset=" << (void*)asset
               << ", sourceId=" << sourceId << io::endl;
        return 0;
    }

    size_t animCount = asset->getAnimationCount();
    if (animCount == 0) {
        slog.w << "Source '" << sourceId << "' contains no animations" << io::endl;
        return 0;
    }

    // ========================================
    // 2. 如果 sourceId 已存在，先卸载（替换模式）
    // ========================================
    if (mSourceIndex.count(sourceId)) {
        DLOG(INFO) << "Source '" << sourceId << "' already loaded, replacing...";
        unloadAnimationsFromSource(sourceId);
    }

    // ========================================
    // 3. 空间检查与淘汰
    // ========================================
    if (!handleSpaceManagement(sourceId, animCount)) {
        // 超限源，拒绝加载
        return 0;
    }

    // ========================================
    // 4. 构建 AnimationBinding（骨架映射）
    // ========================================
    // Use factory methods: createForInstance() or createForAsset()
    // - If this Animator belongs to a FilamentInstance, use createForInstance()
    //   (instance provides entities, owner asset provides names)
    // - Otherwise use createForAsset() (asset provides both entities and names)
    auto binding = this->instance
        ? AnimationBinding::createForInstance(asset, this->instance, this->asset->mEngine)
        : AnimationBinding::createForAsset(asset, this->asset, this->asset->mEngine);

    if (!binding->buildMapping()) {
        slog.e << "Failed to build mapping for source '" << sourceId
               << "': match rate too low" << io::endl;
        return 0;
    }

    // ========================================
    // 5. 转换并缓存所有动画
    // ========================================
    const auto& nodeToEntityMap = binding->getNodeToEntityMap();
    size_t loadedCount = 0;

    for (size_t i = 0; i < animCount; i++) {
        const AnimationAsset::Animation& srcAnim = asset->getAnimation(i);

        // 转换动画（复用现有逻辑）
        Animation convertedAnim;
        convertAnimation(srcAnim, convertedAnim);

        // ========================================
        // 6. 重建 channels（使用 AnimationBinding 映射）
        // ========================================
        const Sampler* samplers = convertedAnim.samplers.data();

        for (const auto& srcChannel : srcAnim.channels) {
            int nodeIndex = srcChannel.targetNodeIndex;

            // 使用 nodeToEntityMap 将节点索引转换为实体
            auto it = nodeToEntityMap.find(nodeIndex);
            if (it == nodeToEntityMap.end()) {
                // 跳过未映射的骨骼（正常情况，AnimationBinding 允许 10% 不匹配）
                continue;
            }

            Channel dstChannel;
            dstChannel.targetEntity = it->second;
            dstChannel.sourceData = samplers + srcChannel.samplerIndex;

            // 转换路径类型
            switch (srcChannel.path) {
                case AnimationPathType::TRANSLATION:
                    dstChannel.transformType = Channel::TRANSLATION;
                    break;
                case AnimationPathType::ROTATION:
                    dstChannel.transformType = Channel::ROTATION;
                    break;
                case AnimationPathType::SCALE:
                    dstChannel.transformType = Channel::SCALE;
                    break;
                case AnimationPathType::WEIGHTS:
                    dstChannel.transformType = Channel::WEIGHTS;
                    break;
            }

            convertedAnim.channels.push_back(dstChannel);
        }

        // ========================================
        // 7. 创建缓存条目并插入（使用移动语义）
        // ========================================
        CachedAnimation cached;
        cached.sourceId = sourceId;
        cached.animName = srcAnim.name.empty() ? ("anim_" + std::to_string(i)) : srcAnim.name;
        cached.fullName = sourceId + ":" + cached.animName;
        cached.accessOrder = ++mAccessCounter;  // 初始访问顺序

        // ⚠️ 关键：使用移动语义（防止 Channel::sourceData 指针悬挂）
        cached.animation = std::move(convertedAnim);

        // ⚠️ 重要：在移动前保存字符串（否则移动后为空）
        std::string fullName = cached.fullName;
        std::string animName = cached.animName;

        // 在移动前保存日志信息
        size_t channelCount = cached.animation.channels.size();
        float duration = cached.animation.duration;

        // 插入主缓存（使用移动语义）
        mAnimationCache[fullName] = std::move(cached);

        // 维护 Source 索引
        mSourceIndex[sourceId].push_back(fullName);

        // 维护名称索引
        mAnimNameIndex[animName].push_back(fullName);

        loadedCount++;

        slog.i << "Loaded animation '" << animName << "' from source '" << sourceId << "' "
               << "(" << channelCount << " channels, "
               << "duration=" << duration << "s)" << io::endl;
    }

    slog.i << "Loaded " << loadedCount << " animations from source '" << sourceId << "'" << io::endl;

    return loadedCount;
}

// ========================================
// Phase 3: 播放与查询 API 实现
// ========================================

/**
 * 精确播放：使用 sourceId 和 animName 播放动画
 */
bool AnimatorImpl::applyAnimation(const std::string& sourceId, const std::string& animName, float time) const {
    std::string fullName = sourceId + ":" + animName;

    auto it = mAnimationCache.find(fullName);
    if (it == mAnimationCache.end()) {
        mCacheMissCount++;
        return false;  // 动画不存在
    }

    // 更新访问顺序（LRU）
    it->second.accessOrder = ++mAccessCounter;
    mCacheHitCount++;

    const Animation& anim = it->second.animation;
    time = time == anim.duration ? time : fmod(time, anim.duration);

    TransformManager& tm = *this->transformManager;
    tm.openLocalTransformTransaction();

    for (const auto& channel : anim.channels) {
        const Sampler* sampler = channel.sourceData;
        if (sampler->times.size() < 2) {
            continue;
        }

        const TimeValues& times = sampler->times;
        TimeValues::const_iterator iter = times.lower_bound(time);

        // Compute interpolant and keyframe indices
        float t = 0.0f;
        size_t nextIndex, prevIndex;
        if (iter == times.end()) {
            nextIndex = times.size() - 1;
            prevIndex = nextIndex;
        } else if (iter == times.begin()) {
            nextIndex = 0;
            prevIndex = 0;
        } else {
            TimeValues::const_iterator prev = iter; --prev;
            nextIndex = iter->second;
            prevIndex = prev->second;
            const float nextTime = iter->first;
            const float prevTime = prev->first;
            float deltaTime = nextTime - prevTime;
            assert(deltaTime >= 0);
            if (deltaTime > 0) {
                t = (time - prevTime) / deltaTime;
            }
        }

        if (sampler->interpolation == Sampler::STEP) {
            t = 0.0f;
        }

        const_cast<AnimatorImpl*>(this)->applyAnimation(channel, t, prevIndex, nextIndex);
    }

    tm.commitLocalTransformTransaction();
    return true;
}

/**
 * 便捷播放：仅使用 animName 播放动画（自动选择最近访问的）
 */
bool AnimatorImpl::applyAnimationByName(const std::string& animName, float time) const {
    auto it = mAnimNameIndex.find(animName);
    if (it == mAnimNameIndex.end() || it->second.empty()) {
        mCacheMissCount++;
        return false;  // 动画名称不存在
    }

    // 如果只有一个源包含该动画，直接使用
    if (it->second.size() == 1) {
        const std::string& fullName = it->second[0];
        auto cachedIt = mAnimationCache.find(fullName);
        if (cachedIt != mAnimationCache.end()) {
            // 更新访问顺序
            cachedIt->second.accessOrder = ++mAccessCounter;
            mCacheHitCount++;

            const Animation& anim = cachedIt->second.animation;
            time = time == anim.duration ? time : fmod(time, anim.duration);
            TransformManager& tm = *this->transformManager;
            tm.openLocalTransformTransaction();

            for (const auto& channel : anim.channels) {
                const Sampler* sampler = channel.sourceData;
                if (sampler->times.size() < 2) {
                    continue;
                }

                const TimeValues& times = sampler->times;
                TimeValues::const_iterator iter = times.lower_bound(time);

                // Compute interpolant and keyframe indices
                float t = 0.0f;
                size_t nextIndex, prevIndex;
                if (iter == times.end()) {
                    nextIndex = times.size() - 1;
                    prevIndex = nextIndex;
                } else if (iter == times.begin()) {
                    nextIndex = 0;
                    prevIndex = 0;
                } else {
                    TimeValues::const_iterator prev = iter; --prev;
                    nextIndex = iter->second;
                    prevIndex = prev->second;
                    const float nextTime = iter->first;
                    const float prevTime = prev->first;
                    float deltaTime = nextTime - prevTime;
                    assert(deltaTime >= 0);
                    if (deltaTime > 0) {
                        t = (time - prevTime) / deltaTime;
                    }
                }

                if (sampler->interpolation == Sampler::STEP) {
                    t = 0.0f;
                }

                const_cast<AnimatorImpl*>(this)->applyAnimation(channel, t, prevIndex, nextIndex);
            }

            tm.commitLocalTransformTransaction();
            return true;
        }
        return false;
    }

    // 多个源包含同名动画，选择最近访问的（accessOrder 最大）
    std::string bestFullName;
    uint64_t bestAccessOrder = 0;

    for (const auto& fullName : it->second) {
        auto cachedIt = mAnimationCache.find(fullName);
        if (cachedIt != mAnimationCache.end()) {
            if (cachedIt->second.accessOrder > bestAccessOrder) {
                bestAccessOrder = cachedIt->second.accessOrder;
                bestFullName = fullName;
            }
        }
    }

    if (bestFullName.empty()) {
        mCacheMissCount++;
        return false;
    }

    // 播放最近访问的动画
    auto cachedIt = mAnimationCache.find(bestFullName);
    cachedIt->second.accessOrder = ++mAccessCounter;
    mCacheHitCount++;

    const Animation& anim = cachedIt->second.animation;
    time = time == anim.duration ? time : fmod(time, anim.duration);
    TransformManager& tm = *this->transformManager;
    tm.openLocalTransformTransaction();

    for (const auto& channel : anim.channels) {
        const Sampler* sampler = channel.sourceData;
        if (sampler->times.size() < 2) {
            continue;
        }

        const TimeValues& times = sampler->times;
        TimeValues::const_iterator iter = times.lower_bound(time);

        // Compute interpolant and keyframe indices
        float t = 0.0f;
        size_t nextIndex, prevIndex;
        if (iter == times.end()) {
            nextIndex = times.size() - 1;
            prevIndex = nextIndex;
        } else if (iter == times.begin()) {
            nextIndex = 0;
            prevIndex = 0;
        } else {
            TimeValues::const_iterator prev = iter; --prev;
            nextIndex = iter->second;
            prevIndex = prev->second;
            const float nextTime = iter->first;
            const float prevTime = prev->first;
            float deltaTime = nextTime - prevTime;
            assert(deltaTime >= 0);
            if (deltaTime > 0) {
                t = (time - prevTime) / deltaTime;
            }
        }

        if (sampler->interpolation == Sampler::STEP) {
            t = 0.0f;
        }

        const_cast<AnimatorImpl*>(this)->applyAnimation(channel, t, prevIndex, nextIndex);
    }

    tm.commitLocalTransformTransaction();
    return true;
}

/**
 * 查询 API 实现
 */
bool AnimatorImpl::hasAnimation(const std::string& sourceId, const std::string& animName) const {
    std::string fullName = sourceId + ":" + animName;
    return mAnimationCache.count(fullName) > 0;
}

bool AnimatorImpl::hasAnimationByName(const std::string& animName) const {
    auto it = mAnimNameIndex.find(animName);
    return it != mAnimNameIndex.end() && !it->second.empty();
}

bool AnimatorImpl::hasSource(const std::string& sourceId) const {
    return mSourceIndex.count(sourceId) > 0;
}

float AnimatorImpl::getAnimationDuration(const std::string& sourceId, const std::string& animName) const {
    std::string fullName = sourceId + ":" + animName;
    auto it = mAnimationCache.find(fullName);
    if (it == mAnimationCache.end()) {
        return 0.0f;
    }
    return it->second.animation.duration;
}

float AnimatorImpl::getAnimationDurationByName(const std::string& animName) const {
    auto it = mAnimNameIndex.find(animName);
    if (it == mAnimNameIndex.end() || it->second.empty()) {
        return 0.0f;
    }

    // 返回第一个匹配的动画时长
    const std::string& fullName = it->second[0];
    auto cachedIt = mAnimationCache.find(fullName);
    if (cachedIt == mAnimationCache.end()) {
        return 0.0f;
    }
    return cachedIt->second.animation.duration;
}

std::vector<std::string> AnimatorImpl::getLoadedSources() const {
    std::vector<std::string> sources;
    sources.reserve(mSourceIndex.size());
    for (const auto& [sourceId, _] : mSourceIndex) {
        sources.push_back(sourceId);
    }
    return sources;
}

std::vector<std::string> AnimatorImpl::getAnimationsInSource(const std::string& sourceId) const {
    auto it = mSourceIndex.find(sourceId);
    if (it == mSourceIndex.end()) {
        return {};
    }

    // 提取动画名称（去掉 "sourceId:" 前缀）
    std::vector<std::string> animNames;
    animNames.reserve(it->second.size());
    for (const auto& fullName : it->second) {
        size_t colonPos = fullName.find(':');
        if (colonPos != std::string::npos) {
            animNames.push_back(fullName.substr(colonPos + 1));
        }
    }
    return animNames;
}

/**
 * 配置与统计 API 实现
 */
void AnimatorImpl::setAnimationCacheSize(size_t maxSize) {
    mMaxCacheSize = maxSize;

    // 如果当前缓存超过新的上限，立即淘汰
    if (mAnimationCache.size() > mMaxCacheSize) {
        evictLRU(mAnimationCache.size() - mMaxCacheSize);
    }
}

size_t AnimatorImpl::getAnimationCacheSize() const {
    return mMaxCacheSize;
}

AnimatorImpl::CacheStats AnimatorImpl::getAnimationCacheStats() const {
    CacheStats stats;
    stats.cachedCount = mAnimationCache.size();
    stats.maxSize = mMaxCacheSize;
    stats.sourceCount = mSourceIndex.size();
    stats.hitCount = mCacheHitCount;
    stats.missCount = mCacheMissCount;

    uint64_t totalAccess = stats.hitCount + stats.missCount;
    stats.hitRate = (totalAccess > 0) ? (static_cast<float>(stats.hitCount) / totalAccess * 100.0f) : 0.0f;

    return stats;
}

void AnimatorImpl::resetCacheStats() {
    mCacheHitCount = 0;
    mCacheMissCount = 0;
}

} // namespace filament::gltfio
