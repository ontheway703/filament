/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef GLTFIO_EXT_ANIMATOR_H
#define GLTFIO_EXT_ANIMATOR_H

#include <gltfio_ext/FilamentAsset.h>
#include <gltfio_ext/FilamentInstance.h>

namespace filament::gltfio_ext {

struct FFilamentAsset;
struct FFilamentInstance;
struct AnimatorImpl;
class AnimationAsset;

/**
 * \class Animator Animator.h gltfio/Animator.h
 * \brief Updates matrices according to glTF \c animation and \c skin definitions.
 *
 * Animator can be used for two things:
 * - Updating matrices in filament::TransformManager components according to glTF \c animation definitions.
 * - Updating bone matrices in filament::RenderableManager components according to glTF \c skin definitions.
 *
 * For a usage example, see the documentation for AssetLoader.
 */
class UTILS_PUBLIC Animator {
public:
    /**
     * Applies rotation, translation, and scale to entities that have been targeted by the given
     * animation definition. Uses filament::TransformManager.
     *
     * @param animationIndex Zero-based index for the \c animation of interest.
     * @param time Elapsed time of interest in seconds.
     */
    void applyAnimation(size_t animationIndex, float time) const;

    /**
     * Computes root-to-node transforms for all bone nodes, then passes
     * the results into filament::RenderableManager::setBones.
     * Uses filament::TransformManager and filament::RenderableManager.
     *
     * NOTE: this operation is independent of \c animation.
     */
    void updateBoneMatrices();

    /**
     * Applies a blended transform to the union of nodes affected by two animations.
     * Used for cross-fading from a previous skinning-based animation or rigid body animation.
     *
     * First, this stashes the current transform hierarchy into a transient memory buffer.
     *
     * Next, this applies previousAnimIndex / previousAnimTime to the actual asset by internally
     * calling applyAnimation().
     *
     * Finally, the stashed local transforms are lerped (via the scale / translation / rotation
     * components) with their live counterparts, and the results are pushed to the asset.
     *
     * To achieve a cross fade effect with skinned models, clients will typically call animator
     * methods in this order: (1) applyAnimation (2) applyCrossFade (3) updateBoneMatrices. The
     * animation that clients pass to applyAnimation is the "current" animation corresponding to
     * alpha=1, while the "previous" animation passed to applyCrossFade corresponds to alpha=0.
     */
    void applyCrossFade(size_t previousAnimIndex, float previousAnimTime, float alpha);

    /**
     * Pass the identity matrix into all bone nodes, useful for returning to the T pose.
     *
     * NOTE: this operation is independent of \c animation.
     */
    void resetBoneMatrices();

    /** Returns the number of \c animation definitions in the glTF asset. */
    size_t getAnimationCount() const;

    /** Returns the duration of the specified glTF \c animation in seconds. */
    float getAnimationDuration(size_t animationIndex) const;

    /**
     * Returns a weak reference to the string name of the specified \c animation, or an
     * empty string if none was specified.
     */
    const char* getAnimationName(size_t animationIndex) const;

    // ========================================
    // 外部动画支持 (External Animation Support)
    // ========================================

    /**
     * 加载外部动画资产
     * Loads an external animation asset
     *
     * Associates an external AnimationAsset with this Animator, enabling it to drive bone animations.
     * Internally creates an AnimationBinding for bone name mapping.
     *
     * 加载 AnimationAsset 中的所有动画。
     * 外部动画索引从内部动画数量开始，范围为 [N, N+M)，其中：
     * - N = 加载外部动画前的 getAnimationCount() 返回值（内部动画数量）
     * - M = animAsset->getAnimationCount()（外部动画数量）
     *
     * Loads ALL animations from the AnimationAsset.
     * External animation indices start after internal animations, ranging from [N, N+M), where:
     * - N = getAnimationCount() before loading external animations (internal animation count)
     * - M = animAsset->getAnimationCount() (external animation count)
     *
     * @param animAsset External animation asset (Animator does not take ownership; caller must ensure lifetime)
     * @return true if loaded successfully, false if failed (bone mapping failed or invalid data)
     *
     * Notes:
     * - Loads all animations from the AnimationAsset (not just the first one)
     * - Repeated calls will replace the previously loaded external animations
     * - animAsset must remain valid for the duration of Animator usage
     * - Example: If internal animation count is 3 and animAsset has 2 animations,
     *   the external animations will be at indices 3 and 4
     */
    bool loadExternalAnimation(AnimationAsset* animAsset);

    /**
     * 卸载外部动画
     * Unloads the external animation
     *
     * Releases resources related to the external animation (AnimationBinding, converted data).
     * Does not affect the animAsset itself (caller is responsible for destruction).
     */
    void unloadExternalAnimation();

    /**
     * 检查是否已加载外部动画
     * Checks if an external animation is loaded
     *
     * @return true if loaded, false if not loaded
     */
    bool hasExternalAnimation() const;

    // For internal use only.
    void addInstance(FFilamentInstance* instance);

private:

    /*! \cond PRIVATE */
    friend struct FFilamentAsset;
    friend struct FFilamentInstance;
    /*! \endcond */

    // If "instance" is null, then this is the primary animator.
    Animator(FFilamentAsset const* asset, FFilamentInstance* instance);
    ~Animator();

    Animator(const Animator& animator) = delete;
    Animator(Animator&& animator) = delete;
    Animator& operator=(const Animator&) = delete;

    AnimatorImpl* mImpl;
};

} // namespace filament::gltfio_ext

#endif // GLTFIO_EXT_ANIMATOR_H
