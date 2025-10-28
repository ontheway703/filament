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

#include <gltfio_ext/AnimationAsset.h>

#include <utils/Log.h>

#include <algorithm>
#include <unordered_set>

using namespace filament::math;
using namespace utils;

namespace filament::gltfio_ext {

bool AnimationAsset::validate() const {
    // Check if we have any data
    if (channels.empty() && samplers.empty()) {
        slog.w << "AnimationAsset: No channels or samplers" << io::endl;
        return true; // Empty asset is technically valid
    }

    // Validate channels
    for (size_t i = 0; i < channels.size(); ++i) {
        const auto& channel = channels[i];

        // Check target node exists
        if (channel.targetNodeIndex < 0 ||
            channel.targetNodeIndex >= static_cast<int>(nodes.size())) {
            slog.e << "AnimationAsset: Channel " << i
                   << " has invalid target node index " << channel.targetNodeIndex
                   << io::endl;
            return false;
        }

        // Check sampler exists
        if (channel.samplerIndex < 0 ||
            channel.samplerIndex >= static_cast<int>(samplers.size())) {
            slog.e << "AnimationAsset: Channel " << i
                   << " has invalid sampler index " << channel.samplerIndex
                   << io::endl;
            return false;
        }
    }

    // Validate samplers
    for (size_t i = 0; i < samplers.size(); ++i) {
        const auto& sampler = samplers[i];

        // Check that times and values are not empty
        if (sampler.times.empty()) {
            slog.e << "AnimationAsset: Sampler " << i << " has no time values" << io::endl;
            return false;
        }

        if (sampler.values.empty()) {
            slog.e << "AnimationAsset: Sampler " << i << " has no output values" << io::endl;
            return false;
        }

        // Check that times are sorted
        for (size_t j = 1; j < sampler.times.size(); ++j) {
            if (sampler.times[j] < sampler.times[j - 1]) {
                slog.e << "AnimationAsset: Sampler " << i
                       << " has unsorted time values at index " << j
                       << io::endl;
                return false;
            }
        }

        // Validate values array size based on path type
        // We need to check this per-channel, but we can do basic validation here
        size_t numKeyframes = sampler.times.size();
        size_t numValues = sampler.values.size();

        // For LINEAR and STEP interpolation:
        // - TRANSLATION/SCALE: 3 values per keyframe
        // - ROTATION: 4 values per keyframe
        // For CUBICSPLINE: 3x the values (in-tangent, value, out-tangent)

        size_t expectedMultiplier = (sampler.interpolation == AnimationInterpolationType::CUBICSPLINE) ? 3 : 1;

        // We can't fully validate without knowing the path type, but we can check basic constraints
        if (numValues < numKeyframes * expectedMultiplier) {
            slog.e << "AnimationAsset: Sampler " << i
                   << " has insufficient values (" << numValues
                   << ") for " << numKeyframes << " keyframes"
                   << io::endl;
            return false;
        }
    }

    // Validate node hierarchy (check for cycles)
    std::unordered_set<int> visited;
    for (size_t i = 0; i < nodes.size(); ++i) {
        visited.clear();
        int current = static_cast<int>(i);

        while (current != -1) {
            if (visited.count(current)) {
                slog.e << "AnimationAsset: Cycle detected in node hierarchy at node " << i
                       << io::endl;
                return false;
            }
            visited.insert(current);
            current = nodes[current].parentIndex;

            if (current >= static_cast<int>(nodes.size())) {
                slog.e << "AnimationAsset: Node " << i
                       << " has invalid parent index " << current
                       << io::endl;
                return false;
            }
        }
    }

    // Validate inverse bind matrices if present
    if (!inverseBindMatrices.empty() && !joints.empty()) {
        if (inverseBindMatrices.size() != joints.size()) {
            slog.e << "AnimationAsset: Inverse bind matrices count ("
                   << inverseBindMatrices.size()
                   << ") doesn't match joints count ("
                   << joints.size() << ")"
                   << io::endl;
            return false;
        }

        // Check that all joint indices are valid
        for (size_t i = 0; i < joints.size(); ++i) {
            if (joints[i] < 0 || joints[i] >= static_cast<int>(nodes.size())) {
                slog.e << "AnimationAsset: Invalid joint index " << joints[i]
                       << " at position " << i
                       << io::endl;
                return false;
            }
        }
    }

    return true;
}

int AnimationAsset::findNodeByName(const std::string& name) const {
    // Use cached map if available
    if (!mBoneNameMap.empty()) {
        auto it = mBoneNameMap.find(name);
        return (it != mBoneNameMap.end()) ? it->second : -1;
    }

    // Fallback to linear search
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].name == name) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

void AnimationAsset::buildBoneNameMap() {
    mBoneNameMap.clear();
    mBoneNameMap.reserve(nodes.size());

    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        if (!node.name.empty()) {
            mBoneNameMap[node.name] = static_cast<int>(i);
        }
    }
}

float AnimationAsset::getDuration() const {
    float maxTime = 0.0f;

    for (const auto& sampler : samplers) {
        if (!sampler.times.empty()) {
            float samplerMaxTime = sampler.times.back();
            maxTime = std::max(maxTime, samplerMaxTime);
        }
    }

    return maxTime;
}

} // namespace filament::gltfio_ext
