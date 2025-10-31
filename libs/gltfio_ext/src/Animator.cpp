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

    // === 外部动画支持 (External animation support) ===
    AnimationAsset* mExternalAnimAsset = nullptr;           // 弱引用，不持有所有权
    std::unique_ptr<AnimationBinding> mExternalBinding;      // 骨骼名称映射
    std::vector<Animation> mExternalAnimations;              // 转换后的外部动画数据数组（支持多个动画）
    size_t mInternalAnimCount = 0;                          // 内部动画数量（用于索引区分）

    void addChannels(const FixedCapacityVector<Entity>& nodeMap, const cgltf_animation& srcAnim,
            Animation& dst);
    void applyAnimation(const Channel& channel, float t, size_t prevIndex, size_t nextIndex);
    void stashCrossFade();
    void applyCrossFade(float alpha);
    void resetBoneMatrices(FFilamentInstance* instance);
    void updateBoneMatrices(FFilamentInstance* instance);

    // === 外部动画方法 (External animation methods) ===
    bool loadExternalAnimation(AnimationAsset* animAsset, filament::Engine* engine);

    bool isExternalAnimationIndex(size_t index) const {
        return index >= mInternalAnimCount && !mExternalAnimations.empty();
    }

    bool hasExternalAnimations() const {
        return mExternalAnimAsset != nullptr && !mExternalAnimations.empty();
    }

    size_t getExternalAnimationCount() const {
        return hasExternalAnimations() ? mExternalAnimations.size() : 0;
    }
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

void Animator::applyCrossFade(size_t previousAnimIndex, float previousAnimTime, float alpha) {
    mImpl->stashCrossFade();
    applyAnimation(previousAnimIndex, previousAnimTime);
    mImpl->applyCrossFade(alpha);
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

size_t Animator::getAnimationCount() const {
    size_t count = mImpl->animations.size();
    count += mImpl->getExternalAnimationCount();  // 添加所有外部动画
    return count;
}

void Animator::applyAnimation(size_t animationIndex, float time) const {
    // 确定使用内部动画还是外部动画
    const Animation* anim = nullptr;

    if (mImpl->isExternalAnimationIndex(animationIndex)) {
        // 外部动画
        // 安全检查：确保外部动画资产仍然有效
        if (!mImpl->hasExternalAnimations()) {
            slog.e << "External animation has been unloaded" << io::endl;
            return;
        }

        size_t extIndex = animationIndex - mImpl->mInternalAnimCount;
        if (extIndex >= mImpl->mExternalAnimations.size()) {
            slog.e << "Invalid external animation index: " << animationIndex
                   << " (have " << mImpl->mExternalAnimations.size()
                   << " external animations)" << io::endl;
            return;
        }
        anim = &mImpl->mExternalAnimations[extIndex];
    } else {
        // 内部动画
        if (animationIndex >= mImpl->animations.size()) {
            slog.e << "Invalid animation index: " << animationIndex << io::endl;
            return;
        }
        anim = &mImpl->animations[animationIndex];
    }

    time = time == anim->duration ? time : fmod(time, anim->duration);
    TransformManager& transformManager = *mImpl->transformManager;
    transformManager.openLocalTransformTransaction();
    for (const auto& channel : anim->channels) {
        const Sampler* sampler = channel.sourceData;
        if (sampler->times.size() < 2) {
            continue;
        }

        const TimeValues& times = sampler->times;

        // Find the first keyframe after the given time, or the keyframe that matches it exactly.
        TimeValues::const_iterator iter = times.lower_bound(time);

        // Compute the interpolant (between 0 and 1) and determine the keyframe pair.
        float t = 0.0f;
        size_t nextIndex;
        size_t prevIndex;
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

        mImpl->applyAnimation(channel, t, prevIndex, nextIndex);
    }
    transformManager.commitLocalTransformTransaction();
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

float Animator::getAnimationDuration(size_t animationIndex) const {
    if (mImpl->isExternalAnimationIndex(animationIndex)) {
        size_t extIndex = animationIndex - mImpl->mInternalAnimCount;
        if (extIndex >= mImpl->mExternalAnimations.size()) {
            slog.e << "Invalid external animation index: " << animationIndex << io::endl;
            return 0.0f;
        }
        return mImpl->mExternalAnimations[extIndex].duration;
    }

    if (animationIndex >= mImpl->animations.size()) {
        slog.e << "Invalid internal animation index: " << animationIndex << io::endl;
        return 0.0f;
    }
    return mImpl->animations[animationIndex].duration;
}

const char* Animator::getAnimationName(size_t animationIndex) const {
    if (mImpl->isExternalAnimationIndex(animationIndex)) {
        size_t extIndex = animationIndex - mImpl->mInternalAnimCount;
        if (extIndex >= mImpl->mExternalAnimations.size()) {
            slog.e << "Invalid external animation index: " << animationIndex << io::endl;
            return "";
        }
        return mImpl->mExternalAnimations[extIndex].name.c_str();
    }

    if (animationIndex >= mImpl->animations.size()) {
        slog.e << "Invalid internal animation index: " << animationIndex << io::endl;
        return "";
    }
    return mImpl->animations[animationIndex].name.c_str();
}

// ========================================
// 外部动画公共 API 实现
// External Animation Public API Implementation
// ========================================

bool Animator::loadExternalAnimation(AnimationAsset* animAsset) {
    return mImpl->loadExternalAnimation(animAsset, mImpl->asset->mEngine);
}

void Animator::unloadExternalAnimation() {
    mImpl->mExternalAnimations.clear();
    mImpl->mExternalBinding.reset();
    mImpl->mExternalAnimAsset = nullptr;

    slog.i << "External animation unloaded" << io::endl;
}

bool Animator::hasExternalAnimation() const {
    return mImpl->hasExternalAnimations();
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
}

} // namespace filament::gltfio
