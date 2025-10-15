/**
 * @file AnimationPack.h
 * @brief 动画包 - 多个动画的集合
 *
 * AnimationPack用于管理一个glTF文件中的所有动画：
 * - 统一加载：一次性加载文件中的所有动画
 * - 统一管理：提供按索引或名称查找动画
 * - 统一销毁：销毁AnimationPack时自动销毁所有动画
 *
 * 设计理由：
 * 角色通常有多个动画（idle/walk/run/attack等），AnimationPack简化管理：
 * - 避免多次解析同一个glTF文件
 * - 方便按名称切换动画（如animator->playAnimation(pack->findAnimation("run"))）
 */

#ifndef GLTFIO_ANIMATIONPACK_H
#define GLTFIO_ANIMATIONPACK_H

#include <utils/compiler.h>
#include <stddef.h>

namespace filament::gltfio {

class AnimationAsset;

/**
 * @class AnimationPack
 * @brief 动画包 - 多个AnimationAsset的容器
 *
 * 典型用法：
 * @code
 *   AnimationPack* pack = loader->loadAnimationPack(data, size);
 *   for (size_t i = 0; i < pack->getAnimationCount(); ++i) {
 *       std::cout << "动画 " << i << ": " << pack->getAnimationNames()[i] << std::endl;
 *   }
 *   AnimationAsset* walk = pack->findAnimation("walk");
 *   animator->playAnimation(walk, 1.0f, true);
 * @endcode
 */
class UTILS_PUBLIC AnimationPack {
public:
    /**
     * 获取动画数量
     * @return 动画数量（glTF文件中的animations_count）
     */
    size_t getAnimationCount() const noexcept;

    /**
     * 按索引获取动画
     * @param index 动画索引（0 ~ getAnimationCount()-1）
     * @return 动画资产，无效索引返回nullptr
     */
    AnimationAsset* getAnimation(size_t index) noexcept;

    /**
     * 按名称查找动画
     * @param name 动画名称（glTF animation.name）
     * @return 动画资产，未找到返回nullptr
     */
    AnimationAsset* findAnimation(const char* name) noexcept;

    /**
     * 获取所有动画名称
     * @return 动画名称数组（长度为getAnimationCount()）
     */
    const char* const* getAnimationNames() const noexcept;

protected:
    AnimationPack() noexcept = default;
    ~AnimationPack() = default;

public:
    AnimationPack(AnimationPack const&) = delete;
    AnimationPack(AnimationPack&&) = delete;
    AnimationPack& operator=(AnimationPack const&) = delete;
    AnimationPack& operator=(AnimationPack&&) = delete;
};

} // namespace filament::gltfio

#endif // GLTFIO_ANIMATIONPACK_H
