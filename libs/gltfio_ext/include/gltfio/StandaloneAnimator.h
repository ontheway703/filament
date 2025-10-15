/**
 * @file StandaloneAnimator.h
 * @brief 独立动画器 - 不依赖场景图的骨骼动画播放器
 *
 * StandaloneAnimator提供独立的骨骼动画播放功能：
 * - 独立于Filament场景图：不依赖FilamentAsset/FilamentInstance
 * - 支持动画混合：多个动画可以同时播放并混合
 * - Runtime管理：支持动态加载/切换/混合动画
 * - 蒙皮矩阵输出：计算骨骼变换矩阵供MeshAsset使用
 *
 * 设计理由 - 为什么不用Filament原生Animator：
 * Filament原生gltfio::Animator绑定在FilamentAsset上，存在限制：
 * - 必须加载整个glTF场景（不支持分离加载）
 * - 动画数据绑定在Asset中（不支持runtime动态加载动画）
 * - 无法独立使用（必须有完整的FilamentAsset）
 *
 * StandaloneAnimator解耦设计：
 * - 只需要SkeletonAsset（不需要完整场景）
 * - 动画可以runtime加载/卸载（AnimationAsset独立）
 * - 支持多动画混合（如idle 50% + walk 50%）
 * - 适合游戏运行时动画管理
 *
 * 技术细节 - TRS分解/组合修复：
 * glTF动画可能同时包含T/R/S通道，StandaloneAnimator正确处理：
 * 1. 从当前骨骼Transform分解出TRS
 * 2. 应用各通道的动画数据（独立修改T/R/S）
 * 3. 重新组合成Transform矩阵
 * 这避免了只有单通道时其他分量被覆盖的bug。
 */

#ifndef GLTFIO_STANDALONEANIMATOR_H
#define GLTFIO_STANDALONEANIMATOR_H

#include <utils/compiler.h>
#include <math/mat4.h>
#include <stddef.h>

namespace filament {
    class Engine;
}

namespace filament::gltfio {

class SkeletonAsset;
class AnimationAsset;
struct AnimatorImpl;

/**
 * @class StandaloneAnimator
 * @brief 独立动画播放器 - 骨骼动画的runtime管理器
 *
 * 核心功能：
 * - 绑定骨骼：bindSkeleton()绑定到SkeletonAsset
 * - 播放动画：playAnimation()播放AnimationAsset
 * - 动画混合：多个动画可以同时播放，权重控制混合比例
 * - 蒙皮计算：每帧update()后，getBoneMatrices()获取骨骼矩阵
 *
 * 典型用法：
 * @code
 *   StandaloneAnimator* animator = StandaloneAnimator::create(engine);
 *   animator->bindSkeleton(skeleton);
 *
 *   // 播放单个动画
 *   int walkId = animator->playAnimation(walkAnim, 1.0f, true);
 *
 *   // 动画混合示例（idle 50% + walk 50%）
 *   int idleId = animator->playAnimation(idleAnim, 0.5f, true);
 *   int walkId = animator->playAnimation(walkAnim, 0.5f, true);
 *
 *   // 每帧更新
 *   animator->update(deltaTime);
 *   mesh->updateSkinning(animator);  // 使用骨骼矩阵更新蒙皮
 * @endcode
 */
class UTILS_PUBLIC StandaloneAnimator {
public:
    /**
     * 创建独立动画器
     * @param engine Filament引擎实例
     * @return 动画器实例
     */
    static StandaloneAnimator* create(filament::Engine& engine);

    /**
     * 销毁动画器
     * @param animator 动画器指针的指针，销毁后置为nullptr
     */
    static void destroy(StandaloneAnimator** animator);

    /**
     * 绑定骨骼层级
     * @param skeleton 骨骼资产
     * @note 绑定后才能播放动画
     */
    void bindSkeleton(SkeletonAsset* skeleton) noexcept;

    /**
     * 播放动画
     * @param animation 动画资产
     * @param weight 动画权重（0.0 ~ 1.0），用于多动画混合
     * @param loop 是否循环播放
     * @return 动画ID（用于stopAnimation/setAnimationWeight）
     * @note 权重1.0表示完全应用，0.5表示50%混合
     */
    int playAnimation(AnimationAsset* animation, float weight = 1.0f, bool loop = true) noexcept;

    /**
     * 停止动画
     * @param animationId 动画ID（playAnimation返回值）
     */
    void stopAnimation(int animationId) noexcept;

    /**
     * 设置动画权重（用于动态调整混合比例）
     * @param animationId 动画ID
     * @param weight 新权重（0.0 ~ 1.0）
     */
    void setAnimationWeight(int animationId, float weight) noexcept;

    /**
     * 更新动画（每帧调用）
     * @param deltaTime 时间增量（秒）
     * @note 更新所有播放中的动画，计算骨骼变换，混合多动画
     */
    void update(float deltaTime) noexcept;

    /**
     * 获取骨骼变换矩阵数组（用于蒙皮计算）
     * @return 骨骼矩阵数组（长度为getBoneCount()）
     * @note 矩阵格式：worldTransform * inverseBindMatrix
     */
    const filament::math::mat4f* getBoneMatrices() const noexcept;

    /**
     * 获取骨骼数量
     * @return 骨骼数量（等于绑定的skeleton->getBoneCount()）
     */
    size_t getBoneCount() const noexcept;

    /**
     * 重置到绑定姿态（停止所有动画，恢复初始姿态）
     */
    void resetToBindPose() noexcept;

private:
    StandaloneAnimator(filament::Engine& engine);
    ~StandaloneAnimator();

    StandaloneAnimator(const StandaloneAnimator&) = delete;
    StandaloneAnimator(StandaloneAnimator&&) = delete;
    StandaloneAnimator& operator=(const StandaloneAnimator&) = delete;

    AnimatorImpl* mImpl;
};

} // namespace filament::gltfio

#endif // GLTFIO_STANDALONEANIMATOR_H
