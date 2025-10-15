/**
 * @file FAnimationAsset.h
 * @brief 动画资产内部实现 - 存储关键帧动画数据
 *
 * 设计说明：
 * - FAnimationAsset是AnimationAsset的具体实现（F前缀表示Filament内部类型）
 * - 使用Sampler存储关键帧数据（时间轴+值）
 * - 使用Channel存储动画通道（目标骨骼+变换类型）
 * - 使用名称延迟绑定（Animation与Skeleton解耦）
 *
 * 核心数据结构：
 * 1. Sampler（采样器）：
 *    - times: map<时间, 索引>，支持快速关键帧查找
 *    - values: 关键帧值（TRANSLATION=3float, ROTATION=4float, SCALE=3float）
 *    - interpolation: 插值类型（LINEAR/STEP/CUBIC）
 *
 * 2. Channel（通道）：
 *    - targetBoneName: 目标骨骼名称（而非Entity，支持延迟绑定）
 *    - sampler: 关联的Sampler
 *    - transformType: 变换类型（TRANSLATION/ROTATION/SCALE）
 *
 * 设计理由：
 * - 为什么用名称而非Entity？
 *   * AnimationAsset加载时不知道SkeletonAsset
 *   * 播放时才绑定（StandaloneAnimator::playAnimation）
 *   * 好处：同一动画可用于不同skeleton
 */

#ifndef GLTFIO_EXT_FANIMATIONASSET_H
#define GLTFIO_EXT_FANIMATIONASSET_H

#include <gltfio/AnimationAsset.h>
#include <utils/CString.h>
#include <map>
#include <vector>
#include <string>

struct cgltf_animation;

namespace filament::gltfio {

/**
 * 采样器数据（关键帧时间+值）
 */
struct Sampler {
    std::map<float, size_t> times;  // 时间 -> 索引
    std::vector<float> values;       // 关键帧值
    enum { LINEAR, STEP, CUBIC } interpolation;
};

// 通道数据（使用名称延迟绑定）
struct Channel {
    const Sampler* sampler;
    std::string targetBoneName;  // ⭐ 使用名称而非Entity
    enum { TRANSLATION, ROTATION, SCALE, WEIGHTS } transformType;
};

struct FAnimationAsset : public AnimationAsset {
    utils::CString mName;
    float mDuration = 0.0f;

    // 动画数据（自主存储）
    std::vector<Sampler> mSamplers;
    std::vector<Channel> mChannels;

    // 从cgltf_animation加载
    bool loadFromGltfAnimation(const cgltf_animation* anim);

    ~FAnimationAsset() = default;
};

} // namespace filament::gltfio

#endif // GLTFIO_EXT_FANIMATIONASSET_H
