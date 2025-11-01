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

#include <string>
#include <vector>

/**
 * 默认动画缓存大小
 *
 * 可在编译时通过 -DGLTFIO_EXT_DEFAULT_ANIMATION_CACHE_SIZE=200 覆盖
 *
 * 内存估算：
 * - 50 个动画：~5 MB
 * - 100 个动画：~10 MB
 * - 150 个动画：~15 MB
 */
#ifndef GLTFIO_EXT_DEFAULT_ANIMATION_CACHE_SIZE
#define GLTFIO_EXT_DEFAULT_ANIMATION_CACHE_SIZE 100
#endif

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
    // 动画缓存 API (Animation Cache API)
    // ========================================

    /**
     * 从动画源加载所有动画
     * Load all animations from an animation source
     *
     * @param sourceId Animation source identifier (e.g. "chest_basic")
     * @param asset Animation asset (can be released after conversion)
     * @return Number of animations successfully loaded
     */
    size_t loadAnimationsFromSource(const char* sourceId, AnimationAsset* asset);

    /**
     * 卸载整个源的所有动画
     * Unload all animations from a source
     *
     * @param sourceId Animation source identifier
     */
    void unloadAnimationsFromSource(const char* sourceId);

    /**
     * 清空所有外部动画缓存
     * Clear all external animation cache
     */
    void clearAnimationCache();

    /**
     * 精确播放：使用 sourceId 和 animName 播放动画
     * Precise playback: play animation using sourceId and animName
     *
     * @param sourceId Animation source identifier
     * @param animName Animation name
     * @param time Time in seconds
     * @return true if successful, false if animation not found
     *
     * Note: Marked as const for backward compatibility with applyAnimation(size_t, float).
     *       Internally updates LRU cache statistics (using mutable members).
     */
    bool applyAnimation(const char* sourceId, const char* animName, float time) const;

    /**
     * 便捷播放：仅使用 animName 播放动画
     * Convenient playback: play animation using only animName
     *
     * If multiple sources contain the same animation name, plays the most recently accessed one.
     *
     * @param animName Animation name
     * @param time Time in seconds
     * @return true if successful, false if animation not found
     *
     * Note: Marked as const for backward compatibility with applyAnimation(size_t, float).
     *       Internally updates LRU cache statistics (using mutable members).
     */
    bool applyAnimationByName(const char* animName, float time) const;

    /**
     * 检查动画是否存在
     * Check if animation exists
     */
    bool hasAnimation(const char* sourceId, const char* animName) const;
    bool hasAnimationByName(const char* animName) const;
    bool hasSource(const char* sourceId) const;

    /**
     * 获取动画时长
     * Get animation duration
     */
    float getAnimationDuration(const char* sourceId, const char* animName) const;
    float getAnimationDurationByName(const char* animName) const;

    /**
     * 获取已加载的源列表
     * Get list of loaded sources
     */
    std::vector<std::string> getLoadedSources() const;

    /**
     * 获取指定源中的动画列表
     * Get list of animations in a source
     */
    std::vector<std::string> getAnimationsInSource(const char* sourceId) const;

    /**
     * 设置最大缓存数量
     * Set maximum cache size
     *
     * If new size is smaller than current cache size, immediately evicts LRU animations.
     */
    void setAnimationCacheSize(size_t maxSize);

    /**
     * 获取最大缓存数量
     * Get maximum cache size
     */
    size_t getAnimationCacheSize() const;

    /**
     * 缓存统计信息
     * Cache statistics
     */
    struct CacheStats {
        size_t cachedCount;      // Current cache count
        size_t maxSize;          // Maximum capacity
        size_t sourceCount;      // Number of loaded sources
        uint64_t hitCount;       // Cache hit count
        uint64_t missCount;      // Cache miss count
        float hitRate;           // Hit rate (percentage)
    };

    /**
     * 获取缓存统计信息
     * Get cache statistics
     */
    CacheStats getAnimationCacheStats() const;

    /**
     * 重置缓存统计信息
     * Reset cache statistics
     */
    void resetCacheStats();

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
