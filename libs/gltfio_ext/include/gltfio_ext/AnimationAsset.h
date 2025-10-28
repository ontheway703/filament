/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef GLTFIO_EXT_ANIMATIONASSET_H
#define GLTFIO_EXT_ANIMATIONASSET_H

#include <string>
#include <vector>
#include <unordered_map>

#include <math/mat4.h>
#include <math/quat.h>
#include <math/vec3.h>

#include <utils/compiler.h>

namespace filament::gltfio_ext {

/**
 * \struct AnimationNode AnimationAsset.h gltfio_ext/AnimationAsset.h
 * \brief Lightweight node structure containing only skeleton-related information.
 *
 * Unlike full glTF nodes, AnimationNode only stores the minimum data required
 * for animation playback: name, hierarchy, and transform.
 */
struct UTILS_PUBLIC AnimationNode {
    //! Bone name (used for skeleton mapping)
    std::string name;

    //! Parent node index (-1 for root nodes)
    int parentIndex;

    //! Local transform matrix
    filament::math::mat4f transform;

    //! Skin index (-1 if not a skinned node)
    int skinIndex;

    AnimationNode()
        : parentIndex(-1), transform(filament::math::mat4f(1.0f)), skinIndex(-1) {}
};

/**
 * \enum AnimationPathType
 * \brief Animation channel target property types.
 */
enum class AnimationPathType : uint8_t {
    TRANSLATION,  //!< Animates node translation (vec3)
    ROTATION,     //!< Animates node rotation (quaternion)
    SCALE,        //!< Animates node scale (vec3)
    WEIGHTS       //!< Animates morph target weights (not currently supported)
};

/**
 * \enum AnimationInterpolationType
 * \brief Keyframe interpolation methods.
 */
enum class AnimationInterpolationType : uint8_t {
    LINEAR,       //!< Linear interpolation between keyframes
    STEP,         //!< Step (constant) interpolation
    CUBICSPLINE   //!< Cubic spline interpolation
};

/**
 * \struct AnimationSampler AnimationAsset.h gltfio_ext/AnimationAsset.h
 * \brief Stores keyframe data for animation channels.
 *
 * Contains time values and corresponding output values, along with the
 * interpolation method to use between keyframes.
 */
struct UTILS_PUBLIC AnimationSampler {
    //! Keyframe times in seconds
    std::vector<float> times;

    //! Keyframe values (interpretation depends on path type)
    //! - TRANSLATION: 3 floats per keyframe (x, y, z)
    //! - ROTATION: 4 floats per keyframe (quaternion: x, y, z, w)
    //! - SCALE: 3 floats per keyframe (x, y, z)
    std::vector<float> values;

    //! Interpolation method
    AnimationInterpolationType interpolation;

    AnimationSampler() : interpolation(AnimationInterpolationType::LINEAR) {}
};

/**
 * \struct AnimationChannel AnimationAsset.h gltfio_ext/AnimationAsset.h
 * \brief Connects a sampler to a target node property.
 *
 * Defines which node property is animated and which sampler provides the data.
 */
struct UTILS_PUBLIC AnimationChannel {
    //! Index of the target node in AnimationAsset::nodes
    int targetNodeIndex;

    //! Index of the sampler in AnimationAsset::samplers
    int samplerIndex;

    //! Which property of the node is animated
    AnimationPathType path;

    AnimationChannel() : targetNodeIndex(-1), samplerIndex(-1), path(AnimationPathType::TRANSLATION) {}
};

/**
 * \class AnimationAsset AnimationAsset.h gltfio_ext/AnimationAsset.h
 * \brief Lightweight animation asset containing only animation data (no meshes).
 *
 * AnimationAsset represents the minimal data required to play back skeletal animations:
 * - Node hierarchy (skeleton structure)
 * - Animation channels (which bones are animated)
 * - Samplers (keyframe data)
 *
 * This class is designed to support the workflow where animations are stored separately
 * from mesh geometry (e.g., Blender's "export_meshes=False" option).
 *
 * Typical usage:
 * \code
 * // Load animation-only GLB file
 * AnimationAsset* animAsset = assetLoader->loadAnimationAsset(data, size);
 *
 * // Bind to existing mesh skeleton
 * AnimationBinding binding;
 * binding.buildMapping(animAsset, meshAsset);
 *
 * // Load into animator
 * animator->loadExternalAnimation(animAsset);
 * animator->applyAnimation(externalIndex, time);
 * \endcode
 */
class UTILS_PUBLIC AnimationAsset {
public:
    AnimationAsset() = default;
    ~AnimationAsset() = default;

    // Non-copyable
    AnimationAsset(const AnimationAsset&) = delete;
    AnimationAsset& operator=(const AnimationAsset&) = delete;

    // Movable
    AnimationAsset(AnimationAsset&&) = default;
    AnimationAsset& operator=(AnimationAsset&&) = default;

    /**
     * Validates the asset data for consistency.
     *
     * Checks:
     * - All channel target nodes exist
     * - All sampler indices are valid
     * - Sampler data is consistent (times/values size match)
     * - Node hierarchy is acyclic
     *
     * @return true if asset is valid, false otherwise
     */
    bool validate() const;

    /**
     * Finds a node by name.
     *
     * @param name The node name to search for
     * @return Node index, or -1 if not found
     */
    int findNodeByName(const std::string& name) const;

    /**
     * Builds an internal name-to-index mapping for fast lookups.
     * Called automatically after loading.
     */
    void buildBoneNameMap();

    /**
     * Gets the animation duration in seconds.
     *
     * @return Maximum time value across all samplers
     */
    float getDuration() const;

    /**
     * Gets the animation name.
     */
    const std::string& getName() const { return mName; }

    /**
     * Sets the animation name.
     */
    void setName(const std::string& name) { mName = name; }

    // === Core Data ===

    //! Node hierarchy (skeleton structure)
    std::vector<AnimationNode> nodes;

    //! Animation channels (connects samplers to nodes)
    std::vector<AnimationChannel> channels;

    //! Keyframe samplers
    std::vector<AnimationSampler> samplers;

    //! Inverse bind matrices (one per node, empty if no skinning data)
    //! These are the matrices that transform from mesh space to bone space
    std::vector<filament::math::mat4f> inverseBindMatrices;

    //! Joint indices (node indices that are part of the skin)
    std::vector<int> joints;

private:
    //! Animation name
    std::string mName;

    //! Bone name to node index mapping (for fast lookups)
    std::unordered_map<std::string, int> mBoneNameMap;
};

} // namespace filament::gltfio_ext

#endif // GLTFIO_EXT_ANIMATIONASSET_H
