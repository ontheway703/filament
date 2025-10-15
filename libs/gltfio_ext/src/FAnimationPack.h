/**
 * @file FAnimationPack.h
 * @brief 动画包内部实现 - 存储多个动画的容器
 *
 * 设计说明：
 * - FAnimationPack是AnimationPack的具体实现（F前缀表示Filament内部类型）
 * - 简单容器，拥有AnimationAsset的所有权
 * - 提供按索引/名称访问动画
 *
 * 核心数据结构：
 * - mAnimations: 动画资产指针数组（拥有所有权，析构时销毁）
 * - mAnimationNames: 动画名称指针数组（指向mAnimations[i]->getName()）
 *
 * 设计理由：
 * - 为什么mAnimationNames是const char*数组而非string？
 *   * 避免字符串复制（直接指向AnimationAsset内部的名称）
 *   * getAnimationNames()返回const char* const*（零拷贝）
 */

#ifndef GLTFIO_EXT_FANIMATIONPACK_H
#define GLTFIO_EXT_FANIMATIONPACK_H

#include <gltfio/AnimationPack.h>
#include <gltfio/AnimationAsset.h>
#include <vector>
#include <string>

namespace filament::gltfio {

/**
 * 动画包内部实现
 */
struct FAnimationPack : public AnimationPack {
    std::vector<AnimationAsset*> mAnimations;
    std::vector<const char*> mAnimationNames;  // 指向mAnimations中的名称

    ~FAnimationPack();
};

} // namespace filament::gltfio

#endif // GLTFIO_EXT_FANIMATIONPACK_H
