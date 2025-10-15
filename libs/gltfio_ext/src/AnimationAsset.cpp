/**
 * @file AnimationAsset.cpp
 * @brief 动画资产实现 - 从glTF animation加载关键帧数据
 *
 * ========== 核心设计 ==========
 *
 * 动画加载流程（loadFromGltfAnimation）：
 * 1. 加载Samplers（关键帧数据）：
 *    - 时间轴：cgltf_animation_sampler.input → map<time, index>
 *    - 值数据：cgltf_animation_sampler.output → vector<float>
 *    - 插值类型：LINEAR/STEP/CUBIC
 *
 * 2. 加载Channels（动画通道）：
 *    - 目标骨骼：cgltf_animation_channel.target_node.name
 *    - 变换类型：TRANSLATION/ROTATION/SCALE/WEIGHTS
 *    - 关联Sampler：指向上一步创建的Sampler
 *
 * 3. 计算Duration：
 *    - 遍历所有Samplers，找到最大时间值
 *
 * ========== 设计理由 ==========
 *
 * Q: 为什么用map存储时间轴？
 * A: 支持快速关键帧查找：
 *    - lower_bound(time)找到≥time的第一个关键帧
 *    - 常见用法：查找当前时间前后的两个关键帧进行插值
 *
 * Q: 为什么存储骨骼名称而非索引？
 * A: 动画与骨骼解耦：
 *    - AnimationAsset加载时还不知道SkeletonAsset
 *    - 播放时才绑定：StandaloneAnimator::playAnimation()建立名称→索引映射
 *    - 好处：同一动画可用于不同skeleton（如果骨骼名称匹配）
 *
 * Q: 为什么用vector<float>存储值数据？
 * A: 灵活性：
 *    - TRANSLATION: 3个float（xyz）
 *    - ROTATION: 4个float（quaternion xyzw）
 *    - SCALE: 3个float（xyz）
 *    - 使用时reinterpret_cast<float3*>或<quatf*>
 */

#include <gltfio/AnimationAsset.h>
#include "FAnimationAsset.h"
#include "GltfioExtInternal.h"

#include <cgltf.h>
#include <algorithm>

namespace filament::gltfio {

// 辅助函数：创建Sampler
static void createSampler(const cgltf_animation_sampler& src, Sampler& dst) {
    // 1. 复制时间轴到map
    const cgltf_accessor* timelineAccessor = src.input;
    const uint8_t* timelineBlob = nullptr;
    const float* timelineFloats = nullptr;

    if (timelineAccessor->buffer_view->has_meshopt_compression) {
        timelineBlob = (const uint8_t*)timelineAccessor->buffer_view->data;
        timelineFloats = (const float*)(timelineBlob + timelineAccessor->offset);
    } else {
        timelineBlob = (const uint8_t*)timelineAccessor->buffer_view->buffer->data;
        timelineFloats = (const float*)(timelineBlob + timelineAccessor->offset +
                                        timelineAccessor->buffer_view->offset);
    }

    for (size_t i = 0; i < timelineAccessor->count; ++i) {
        dst.times[timelineFloats[i]] = i;
    }

    // 2. 解包值数据
    const cgltf_accessor* valuesAccessor = src.output;
    size_t componentCount = 0;

    switch (valuesAccessor->type) {
        case cgltf_type_scalar:
            componentCount = 1;
            break;
        case cgltf_type_vec3:
            componentCount = 3;
            break;
        case cgltf_type_vec4:
            componentCount = 4;
            break;
        default:
            GLTFIO_EXT_WARN("Unknown animation value type");
            return;
    }

    dst.values.resize(valuesAccessor->count * componentCount);
    cgltf_accessor_unpack_floats(src.output, dst.values.data(), dst.values.size());

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
        default:
            dst.interpolation = Sampler::LINEAR;
            break;
    }
}

bool FAnimationAsset::loadFromGltfAnimation(const cgltf_animation* anim) {
    if (!anim) {
        return false;
    }

    // 1. 存储名称
    mName = utils::CString(anim->name ? anim->name : "");
    mDuration = 0.0f;

    // 2. 加载Samplers
    mSamplers.resize(anim->samplers_count);
    for (size_t i = 0; i < anim->samplers_count; ++i) {
        const cgltf_animation_sampler& srcSampler = anim->samplers[i];
        Sampler& dstSampler = mSamplers[i];
        createSampler(srcSampler, dstSampler);

        // 更新duration
        if (!dstSampler.times.empty()) {
            float maxTime = dstSampler.times.rbegin()->first;
            mDuration = std::max(mDuration, maxTime);
        }
    }

    // 3. 加载Channels（使用骨骼名称）
    for (size_t i = 0; i < anim->channels_count; ++i) {
        const cgltf_animation_channel& srcChannel = anim->channels[i];

        // 跳过无效channel
        if (!srcChannel.target_node || !srcChannel.sampler) {
            continue;
        }

        Channel dstChannel;
        dstChannel.sampler = &mSamplers[srcChannel.sampler - anim->samplers];

        // ⭐ 关键：存储骨骼名称而非Entity
        if (srcChannel.target_node->name) {
            dstChannel.targetBoneName = srcChannel.target_node->name;
        } else {
            // 跳过匿名节点
            GLTFIO_EXT_WARN("Skipping animation channel with unnamed target node");
            continue;
        }

        // 设置变换类型
        switch (srcChannel.target_path) {
            case cgltf_animation_path_type_translation:
                dstChannel.transformType = Channel::TRANSLATION;
                break;
            case cgltf_animation_path_type_rotation:
                dstChannel.transformType = Channel::ROTATION;
                break;
            case cgltf_animation_path_type_scale:
                dstChannel.transformType = Channel::SCALE;
                break;
            case cgltf_animation_path_type_weights:
                dstChannel.transformType = Channel::WEIGHTS;
                break;
            default:
                GLTFIO_EXT_WARN("Unsupported animation path type");
                continue;
        }

        mChannels.push_back(dstChannel);
    }

    if (mChannels.empty()) {
        GLTFIO_EXT_WARN("Animation has no valid channels");
        return false;
    }

    return true;
}

// 公开API实现
const char* AnimationAsset::getName() const noexcept {
    auto* self = static_cast<const FAnimationAsset*>(this);
    return self->mName.c_str();
}

float AnimationAsset::getDuration() const noexcept {
    auto* self = static_cast<const FAnimationAsset*>(this);
    return self->mDuration;
}

size_t AnimationAsset::getChannelCount() const noexcept {
    auto* self = static_cast<const FAnimationAsset*>(this);
    return self->mChannels.size();
}

const char* AnimationAsset::getChannelTargetBone(size_t channelIndex) const noexcept {
    auto* self = static_cast<const FAnimationAsset*>(this);
    if (channelIndex >= self->mChannels.size()) {
        return nullptr;
    }
    return self->mChannels[channelIndex].targetBoneName.c_str();
}

AnimationAsset::ChannelType AnimationAsset::getChannelType(size_t channelIndex) const noexcept {
    auto* self = static_cast<const FAnimationAsset*>(this);
    if (channelIndex >= self->mChannels.size()) {
        return ChannelType::TRANSLATION;
    }

    switch (self->mChannels[channelIndex].transformType) {
        case Channel::TRANSLATION: return ChannelType::TRANSLATION;
        case Channel::ROTATION: return ChannelType::ROTATION;
        case Channel::SCALE: return ChannelType::SCALE;
        case Channel::WEIGHTS: return ChannelType::WEIGHTS;
        default: return ChannelType::TRANSLATION;
    }
}

} // namespace filament::gltfio
