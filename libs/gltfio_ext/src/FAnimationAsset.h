/**
 * @file FAnimationAsset.h
 * @brief 动画资产内部实现 - 存储多个关键帧动画数据
 *
 * 设计说明：
 * - FAnimationAsset是AnimationAsset的具体实现（F前缀表示Filament内部类型）
 * - 支持加载glTF文件中的所有动画（一次性加载多个动画）
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
 * 3. SingleAnimation（单个动画）：
 *    - mName: 动画名称
 *    - mDuration: 动画时长
 *    - mSamplers: 该动画的所有采样器
 *    - mChannels: 该动画的所有通道
 *
 * 设计理由：
 * - 为什么用名称而非Entity？
 *   * AnimationAsset加载时不知道SkeletonAsset
 *   * 播放时才绑定（StandaloneAnimator::playAnimation）
 *   * 好处：同一动画可用于不同skeleton
 * - 为什么一次加载多个动画？
 *   * glTF文件本身支持多个动画
 *   * 一次加载所有动画可以统一管理资源
 *   * 加载单个动画只是N=1的特殊情况
 */

#ifndef GLTFIO_EXT_FANIMATIONASSET_H
#define GLTFIO_EXT_FANIMATIONASSET_H

#include <gltfio/AnimationAsset.h>
#include <utils/CString.h>
#include <map>
#include <vector>
#include <string>

struct cgltf_data;

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

/**
 * 单个动画数据（从glTF的cgltf_animation加载）
 */
struct SingleAnimation {
    utils::CString mName;
    float mDuration = 0.0f;
    std::vector<Sampler> mSamplers;
    std::vector<Channel> mChannels;
};

struct FAnimationAsset : public AnimationAsset {
    // 存储多个动画（从glTF文件一次性加载所有动画）
    std::vector<SingleAnimation> mAnimations;

    // 缓存动画名称指针（用于getAnimationNames()返回const char* const*）
    std::vector<const char*> mAnimationNames;

    // 从cgltf_data加载所有动画
    bool loadFromGltfData(const cgltf_data* data);

    ~FAnimationAsset() = default;
};

} // namespace filament::gltfio

#endif // GLTFIO_EXT_FANIMATIONASSET_H
