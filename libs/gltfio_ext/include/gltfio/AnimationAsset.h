/**
 * @file AnimationAsset.h
 * @brief 动画资产 - 骨骼动画数据容器（支持多个动画）
 *
 * AnimationAsset存储glTF文件中的所有动画关键帧数据：
 * - 时间轴：每个关键帧的时间戳
 * - 关键帧：TRS（Translation/Rotation/Scale）或权重数据
 * - 通道：每个通道控制一个骨骼的一种变换类型
 *
 * 设计理由：
 * - glTF文件本身支持多个动画（idle/walk/run等）
 * - 一次性加载所有动画可以统一管理资源
 * - 加载单个动画只是N=1的特殊情况
 *
 * 动画独立存储，支持：
 * - 动态加载/卸载动画（节省内存）
 * - 同一动画应用到不同skeleton（如果骨骼名称匹配）
 * - 动画混合（由StandaloneAnimator实现）
 */

#ifndef GLTFIO_ANIMATIONASSET_H
#define GLTFIO_ANIMATIONASSET_H

#include <utils/compiler.h>
#include <stddef.h>

namespace filament::gltfio {

/**
 * @class AnimationAsset
 * @brief 骨骼动画数据容器 - 存储多个关键帧动画
 *
 * 核心概念：
 * - 多动画容器：一个AnimationAsset可包含多个动画（从glTF文件加载）
 * - Channel（通道）：控制一个骨骼的一种变换（如"Spine"的ROTATION）
 * - ChannelType：变换类型（平移/旋转/缩放/权重）
 * - 时间轴：关键帧时间（秒），从0到getDuration()
 *
 * 典型用法：
 * @code
 *   // 加载glTF文件中的所有动画
 *   AnimationAsset* animations = loader->loadAnimation(data, size);
 *   std::cout << "加载了 " << animations->getAnimationCount() << " 个动画" << std::endl;
 *
 *   // 按索引访问
 *   std::cout << "动画0：" << animations->getAnimationName(0)
 *             << " 时长：" << animations->getAnimationDuration(0) << "秒" << std::endl;
 *
 *   // 按名称查找
 *   int idleIdx = animations->findAnimationIndex("idle");
 *   if (idleIdx >= 0) {
 *       animator->playAnimation(animations, idleIdx, 1.0f, true);  // 播放idle动画
 *   }
 * @endcode
 */
class UTILS_PUBLIC AnimationAsset {
public:
    /**
     * 通道类型 - 骨骼的变换类型
     */
    enum class ChannelType {
        TRANSLATION,  ///< 平移（float3）
        ROTATION,     ///< 旋转（quaternion）
        SCALE,        ///< 缩放（float3）
        WEIGHTS       ///< Morph权重（用于blend shapes，暂不支持）
    };

    // ========== 容器API ==========

    /**
     * 获取动画数量
     * @return 动画数量（从glTF文件加载的动画总数）
     */
    size_t getAnimationCount() const noexcept;

    // ========== 单个动画查询API（按索引访问）==========

    /**
     * 获取动画名称
     * @param animIndex 动画索引
     * @return 动画名称（来自glTF animation.name）
     */
    const char* getAnimationName(size_t animIndex) const noexcept;

    /**
     * 获取动画时长（秒）
     * @param animIndex 动画索引
     * @return 时长（最后一个关键帧的时间）
     */
    float getAnimationDuration(size_t animIndex) const noexcept;

    // ========== 查找API ==========

    /**
     * 按名称查找动画索引
     * @param name 动画名称
     * @return 动画索引，如果未找到则返回-1
     */
    int findAnimationIndex(const char* name) const noexcept;

protected:
    AnimationAsset() noexcept = default;
    ~AnimationAsset() = default;

public:
    AnimationAsset(AnimationAsset const&) = delete;
    AnimationAsset(AnimationAsset&&) = delete;
    AnimationAsset& operator=(AnimationAsset const&) = delete;
    AnimationAsset& operator=(AnimationAsset&&) = delete;
};

} // namespace filament::gltfio

#endif // GLTFIO_ANIMATIONASSET_H
