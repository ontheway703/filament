/**
 * @file AnimationAsset.h
 * @brief 动画资产 - 骨骼动画数据容器
 *
 * AnimationAsset存储一段骨骼动画的关键帧数据：
 * - 时间轴：每个关键帧的时间戳
 * - 关键帧：TRS（Translation/Rotation/Scale）或权重数据
 * - 通道：每个通道控制一个骨骼的一种变换类型
 *
 * 设计理由：
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
 * @brief 骨骼动画数据 - 存储关键帧动画
 *
 * 核心概念：
 * - Channel（通道）：控制一个骨骼的一种变换（如"Spine"的ROTATION）
 * - ChannelType：变换类型（平移/旋转/缩放/权重）
 * - 时间轴：关键帧时间（秒），从0到getDuration()
 *
 * 典型用法：
 * @code
 *   AnimationAsset* anim = loader->loadAnimation(data, size);
 *   std::cout << "动画：" << anim->getName()
 *             << " 时长：" << anim->getDuration() << "秒"
 *             << " 通道数：" << anim->getChannelCount() << std::endl;
 *   animator->playAnimation(anim, 1.0f, true);  // 播放，权重1.0，循环
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

    /**
     * 获取动画名称
     * @return 动画名称（来自glTF animation.name）
     */
    const char* getName() const noexcept;

    /**
     * 获取动画时长（秒）
     * @return 时长（最后一个关键帧的时间）
     */
    float getDuration() const noexcept;

    /**
     * 获取通道数量
     * @return 通道数（一个骨骼可能有多个通道：T+R+S）
     */
    size_t getChannelCount() const noexcept;

    /**
     * 获取通道的目标骨骼名称
     * @param channelIndex 通道索引
     * @return 骨骼名称（glTF node名称）
     */
    const char* getChannelTargetBone(size_t channelIndex) const noexcept;

    /**
     * 获取通道的变换类型
     * @param channelIndex 通道索引
     * @return 变换类型（TRANSLATION/ROTATION/SCALE）
     */
    ChannelType getChannelType(size_t channelIndex) const noexcept;

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
