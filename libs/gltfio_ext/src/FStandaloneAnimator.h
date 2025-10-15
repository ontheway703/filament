/**
 * @file FStandaloneAnimator.h
 * @brief 独立动画播放器内部实现 - 动画混合和运行时状态管理
 *
 * 设计说明：
 * - AnimatorImpl是StandaloneAnimator的pimpl实现
 * - 使用BoundChannel缓存骨骼索引（避免每帧查找）
 * - 使用AnimationState存储每个动画的播放状态
 * - 支持多动画混合（多个AnimationState同时应用）
 *
 * 核心数据结构：
 * 1. BoundChannel（绑定后的通道）：
 *    - Channel使用名称（加载时）→ BoundChannel使用索引（运行时）
 *    - boneIndex: 缓存的骨骼索引（避免每帧getBoneIndex查找）
 *    - sampler: 关键帧数据的指针
 *
 * 2. AnimationState（动画播放状态）：
 *    - time: 当前播放时间
 *    - weight: 动画权重（用于混合，0.0~1.0）
 *    - loop: 是否循环播放
 *    - boundChannels: 绑定后的通道列表（每个通道对应一个骨骼的一种变换）
 *
 * 3. AnimatorImpl（动画器实现）：
 *    - animations: 当前播放的所有动画状态
 *    - boneMatrices: 计算出的骨骼矩阵（用于蒙皮）
 *    - applyAnimation(): 应用单个动画到骨骼（TRS分解/组合）
 *    - computeBoneMatrices(): 计算最终骨骼矩阵
 *
 * 设计理由：
 * - 为什么需要BoundChannel？
 *   * 名称查找很慢（每帧遍历字符串比较）
 *   * 绑定时一次性查找，缓存索引
 *   * 运行时直接使用索引（数组访问，O(1)）
 */

#ifndef GLTFIO_EXT_FSTANDALONEANIMATOR_H
#define GLTFIO_EXT_FSTANDALONEANIMATOR_H

#include <gltfio/StandaloneAnimator.h>
#include <filament/TransformManager.h>
#include <math/mat4.h>
#include <vector>

namespace filament {
    class Engine;
}

namespace filament::gltfio {

class SkeletonAsset;
class AnimationAsset;
struct Sampler;
struct Channel;

/**
 * 绑定后的通道（运行时使用索引，避免每帧名称查找）
 */
struct BoundChannel {
    const Sampler* sampler;
    size_t boneIndex;  // ⭐ 缓存索引，避免每帧查找
    enum { TRANSLATION, ROTATION, SCALE, WEIGHTS } transformType;
};

// 动画播放状态
struct AnimationState {
    AnimationAsset* animation;
    float time = 0.0f;
    float weight = 1.0f;
    bool loop = true;
    bool playing = true;
    int id;

    // 绑定后的通道列表
    std::vector<BoundChannel> boundChannels;
};

struct AnimatorImpl {
    filament::Engine* engine = nullptr;
    filament::TransformManager* transformManager = nullptr;

    // 绑定的骨骼
    SkeletonAsset* skeleton = nullptr;

    // 播放中的动画
    std::vector<AnimationState> animations;
    int nextAnimationId = 1;

    // 骨骼变换缓存
    std::vector<filament::math::mat4f> boneMatrices;

    // 更新逻辑
    void applyAnimation(const AnimationState& state);
    void computeBoneMatrices();
};

} // namespace filament::gltfio

#endif // GLTFIO_EXT_FSTANDALONEANIMATOR_H
