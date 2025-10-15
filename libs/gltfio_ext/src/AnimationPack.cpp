/**
 * @file AnimationPack.cpp
 * @brief 动画包实现 - 管理多个动画的容器
 *
 * ========== 核心设计 ==========
 *
 * AnimationPack是AnimationAsset的容器，提供：
 * 1. 按索引访问：getAnimation(index)
 * 2. 按名称查找：findAnimation(name)
 * 3. 获取所有名称：getAnimationNames()
 * 4. 统一销毁：析构函数自动销毁所有AnimationAsset
 *
 * ========== 设计理由 ==========
 *
 * Q: 为什么需要AnimationPack？
 * A: 角色通常有多个动画（idle/walk/run/attack等）：
 *    - 不用AnimationPack：需要手动管理多个AnimationAsset指针
 *    - 用AnimationPack：一次加载，按需获取
 *
 * Q: 为什么拥有AnimationAsset的所有权？
 * A: 简化资源管理：
 *    - 用户只需destroyAnimationPack(pack)
 *    - 无需逐个销毁AnimationAsset
 *
 * 典型用法：
 * @code
 *   AnimationPack* pack = loader->loadAnimationPack(data, size);
 *   AnimationAsset* idle = pack->findAnimation("idle");
 *   AnimationAsset* walk = pack->findAnimation("walk");
 *   animator->playAnimation(idle, 1.0f, true);
 *   // 切换动画
 *   animator->stopAnimation(idleId);
 *   animator->playAnimation(walk, 1.0f, true);
 * @endcode
 */

#include <gltfio/AnimationPack.h>
#include "FAnimationPack.h"
#include "FAnimationAsset.h"
#include <cstring>

namespace filament::gltfio {

FAnimationPack::~FAnimationPack() {
    // AnimationPack拥有所有AnimationAsset的所有权
    for (auto* anim : mAnimations) {
        delete static_cast<FAnimationAsset*>(anim);
    }
}

size_t AnimationPack::getAnimationCount() const noexcept {
    auto* self = static_cast<const FAnimationPack*>(this);
    return self->mAnimations.size();
}

AnimationAsset* AnimationPack::getAnimation(size_t index) noexcept {
    auto* self = static_cast<FAnimationPack*>(this);
    if (index >= self->mAnimations.size()) {
        return nullptr;
    }
    return self->mAnimations[index];
}

AnimationAsset* AnimationPack::findAnimation(const char* name) noexcept {
    auto* self = static_cast<FAnimationPack*>(this);
    if (!name) {
        return nullptr;
    }

    for (auto* anim : self->mAnimations) {
        if (strcmp(anim->getName(), name) == 0) {
            return anim;
        }
    }

    return nullptr;
}

const char* const* AnimationPack::getAnimationNames() const noexcept {
    auto* self = static_cast<const FAnimationPack*>(this);
    return self->mAnimationNames.empty() ? nullptr : self->mAnimationNames.data();
}

} // namespace filament::gltfio
