/**
 * @file AnimationAsset.cpp
 * @brief 动画资产实现 - 从glTF data加载所有动画
 *
 * ========== 核心设计 ==========
 *
 * 动画加载流程（loadFromGltfData）：
 * 1. 遍历cgltf_data中的所有animations（而非只加载第一个）
 * 2. 对每个animation：
 *    - 加载Samplers（关键帧数据）
 *    - 加载Channels（动画通道）
 *    - 计算Duration（最大时间值）
 *    - 存储到mAnimations vector
 * 3. 缓存所有动画名称指针到mAnimationNames
 *
 * ========== 设计理由 ==========
 *
 * Q: 为什么loadFromGltfData加载所有动画？
 * A: glTF文件本身支持多个动画：
 *    - 一次性加载避免重复解析glTF数据
 *    - 统一管理所有动画资源
 *    - "多个"是"单个"的超集（N=1时等同于单个动画）
 *
 * Q: 为什么用vector<SingleAnimation>而非vector<AnimationAsset*>？
 * A: 避免间接指针开销：
 *    - SingleAnimation是值类型，内存连续
 *    - 访问mAnimations[i]直接，无需解引用
 *    - 生命周期管理简单（自动析构）
 */

#include <gltfio/AnimationAsset.h>
#include "FAnimationAsset.h"
#include "GltfioExtInternal.h"

#include <cgltf.h>
#include <algorithm>
#include <cstring>

namespace filament::gltfio {

// 辅助函数：创建Sampler（从AnimationPack.cpp迁移）
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

// 辅助函数：从cgltf_animation加载单个动画
static bool loadSingleAnimation(const cgltf_animation* anim, SingleAnimation& out) {
    if (!anim) {
        return false;
    }

    // 1. 存储名称
    out.mName = utils::CString(anim->name ? anim->name : "");
    out.mDuration = 0.0f;

    // 2. 加载Samplers
    out.mSamplers.resize(anim->samplers_count);
    for (size_t i = 0; i < anim->samplers_count; ++i) {
        const cgltf_animation_sampler& srcSampler = anim->samplers[i];
        Sampler& dstSampler = out.mSamplers[i];
        createSampler(srcSampler, dstSampler);

        // 更新duration
        if (!dstSampler.times.empty()) {
            float maxTime = dstSampler.times.rbegin()->first;
            out.mDuration = std::max(out.mDuration, maxTime);
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
        dstChannel.sampler = &out.mSamplers[srcChannel.sampler - anim->samplers];

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

        out.mChannels.push_back(dstChannel);
    }

    if (out.mChannels.empty()) {
        GLTFIO_EXT_WARN("Animation has no valid channels");
        return false;
    }

    return true;
}

// 从cgltf_data加载所有动画
bool FAnimationAsset::loadFromGltfData(const cgltf_data* data) {
    if (!data || data->animations_count == 0) {
        GLTFIO_EXT_WARN("No animations in glTF data");
        return false;
    }

    // 加载所有动画
    mAnimations.reserve(data->animations_count);
    for (size_t i = 0; i < data->animations_count; ++i) {
        SingleAnimation anim;
        if (loadSingleAnimation(&data->animations[i], anim)) {
            mAnimations.push_back(std::move(anim));
        }
    }

    if (mAnimations.empty()) {
        GLTFIO_EXT_WARN("Failed to load any animations");
        return false;
    }

    // 缓存动画名称指针
    mAnimationNames.reserve(mAnimations.size() + 1);
    for (const auto& anim : mAnimations) {
        mAnimationNames.push_back(anim.mName.c_str());
    }
    mAnimationNames.push_back(nullptr);  // 以nullptr结尾

    return true;
}

// ========== 公开API实现 ==========

// 容器API
size_t AnimationAsset::getAnimationCount() const noexcept {
    auto* self = static_cast<const FAnimationAsset*>(this);
    return self->mAnimations.size();
}

// 单个动画查询API（按索引访问）
const char* AnimationAsset::getAnimationName(size_t animIndex) const noexcept {
    auto* self = static_cast<const FAnimationAsset*>(this);
    if (animIndex >= self->mAnimations.size()) {
        return nullptr;
    }
    return self->mAnimations[animIndex].mName.c_str();
}

float AnimationAsset::getAnimationDuration(size_t animIndex) const noexcept {
    auto* self = static_cast<const FAnimationAsset*>(this);
    if (animIndex >= self->mAnimations.size()) {
        return 0.0f;
    }
    return self->mAnimations[animIndex].mDuration;
}

// 查找API
int AnimationAsset::findAnimationIndex(const char* name) const noexcept {
    auto* self = static_cast<const FAnimationAsset*>(this);
    if (!name) {
        return -1;
    }

    for (size_t i = 0; i < self->mAnimations.size(); ++i) {
        if (strcmp(self->mAnimations[i].mName.c_str(), name) == 0) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

} // namespace filament::gltfio
