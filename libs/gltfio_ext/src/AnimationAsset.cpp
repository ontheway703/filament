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

// ==================================================================================================
// AnimationAsset::Animation implementation
// ==================================================================================================

// 获取动画时长：遍历所有 sampler 的时间数组，取最大值
float AnimationAsset::Animation::getDuration() const {
    float maxTime = 0.0f;

    // 遍历所有采样器
    for (const auto& sampler : samplers) {
        if (!sampler.times.empty()) {
            // times 数组已经排序（validate() 中保证），直接取最后一个元素
            float samplerMaxTime = sampler.times.back();
            maxTime = std::max(maxTime, samplerMaxTime);
        }
    }

    // 返回最长的采样器时间（即整个动画的时长）
    // 例如：sampler 0 时长 1.5s，sampler 1 时长 2.0s → 返回 2.0s
    return maxTime;
}

/**
 * 验证动画数据的完整性和一致性
 *
 * 验证策略分三部分：
 * 1. 验证 channels 的 samplerIndex 有效性
 * 2. 验证 samplers 的基本属性（非空、时间排序）
 * 3. 验证 values 数组大小与 path 类型的匹配性（最复杂）
 */
bool AnimationAsset::Animation::validate() const {
    // === 第 1 步：基本数据检查 ===
    if (channels.empty() && samplers.empty()) {
        slog.w << "Animation '" << name << "': No channels or samplers" << io::endl;
        return true; // 空动画在技术上是有效的（例如占位符动画）
    }

    // === 第 2 步：验证 channels 的 samplerIndex ===
    for (size_t i = 0; i < channels.size(); ++i) {
        const auto& channel = channels[i];

        // 检查 sampler 索引是否在有效范围内
        if (channel.samplerIndex < 0 ||
            channel.samplerIndex >= static_cast<int>(samplers.size())) {
            slog.e << "Animation '" << name << "': Channel " << i
                   << " has invalid sampler index " << channel.samplerIndex
                   << " (samplers.size=" << samplers.size() << ")"
                   << io::endl;
            return false;
        }
    }

    // === 第 3 步：验证 samplers 的基本属性 ===
    for (size_t i = 0; i < samplers.size(); ++i) {
        const auto& sampler = samplers[i];

        // 检查时间数组不为空
        if (sampler.times.empty()) {
            slog.e << "Animation '" << name << "': Sampler " << i << " has no time values" << io::endl;
            return false;
        }

        // 检查值数组不为空
        if (sampler.values.empty()) {
            slog.e << "Animation '" << name << "': Sampler " << i << " has no output values" << io::endl;
            return false;
        }

        // 检查时间数组单调递增（glTF 2.0 规范要求）
        // 这是插值算法的前提条件，否则无法正确查找关键帧
        for (size_t j = 1; j < sampler.times.size(); ++j) {
            if (sampler.times[j] < sampler.times[j - 1]) {
                slog.e << "Animation '" << name << "': Sampler " << i
                       << " has unsorted time values at index " << j
                       << " (" << sampler.times[j-1] << " → " << sampler.times[j] << ")"
                       << io::endl;
                return false;
            }
        }
    }

    // === 第 4 步：验证 values 数组大小与 path 类型的匹配性 ===
    // 这是最核心的验证：确保 sampler.values 的大小与动画类型和插值方式匹配
    for (size_t i = 0; i < channels.size(); ++i) {
        const auto& channel = channels[i];
        const auto& sampler = samplers[channel.samplerIndex];

        size_t numKeyframes = sampler.times.size();
        size_t numValues = sampler.values.size();

        // 根据 path 类型确定每个关键帧的值数量
        size_t valuesPerKeyframe;
        const char* pathTypeName;

        switch (channel.path) {
            case AnimationPathType::TRANSLATION:
                valuesPerKeyframe = 3;  // vec3: (x, y, z)
                pathTypeName = "TRANSLATION";
                break;
            case AnimationPathType::ROTATION:
                valuesPerKeyframe = 4;  // quaternion: (x, y, z, w)
                pathTypeName = "ROTATION";
                break;
            case AnimationPathType::SCALE:
                valuesPerKeyframe = 3;  // vec3: (x, y, z)
                pathTypeName = "SCALE";
                break;
            case AnimationPathType::WEIGHTS:
                // WEIGHTS 的数量是可变的（取决于 morph target 数量）
                // 当前暂不支持，跳过验证
                continue;
        }

        // CUBICSPLINE 插值需要 3 倍数据：in-tangent, value, out-tangent
        // 例如：TRANSLATION + CUBICSPLINE = 3 (vec3) × 3 (切线组) = 9 个 float/关键帧
        size_t interpolationMultiplier =
            (sampler.interpolation == AnimationInterpolationType::CUBICSPLINE) ? 3 : 1;

        // 期望的值数量 = 关键帧数 × 每帧值数量 × 插值乘数
        size_t expectedValues = numKeyframes * valuesPerKeyframe * interpolationMultiplier;

        // 如果实际值数量与期望不符，说明数据损坏或格式错误
        if (numValues != expectedValues) {
            slog.e << "Animation '" << name << "': Channel " << i
                   << " (path=" << pathTypeName << ", sampler=" << channel.samplerIndex
                   << ") has incorrect value count: expected " << expectedValues
                   << " (" << numKeyframes << " keyframes × " << valuesPerKeyframe
                   << " values/keyframe × " << interpolationMultiplier << " interpolation multiplier)"
                   << ", got " << numValues
                   << io::endl;
            return false;
        }
    }

    return true;  // 所有检查通过
}

// ==================================================================================================
// AnimationAsset implementation
// ==================================================================================================

/**
 * 验证整个 AnimationAsset 的完整性
 *
 * 验证范围：
 * 1. 所有 Animation 对象的内部一致性（调用 Animation::validate()）
 * 2. Animation channels 引用的 targetNodeIndex 在 nodes 数组范围内
 * 3. 节点层级无循环（使用 DFS 遍历检测）
 * 4. 蒙皮数据的一致性（inverseBindMatrices 和 joints 数量匹配）
 */
bool AnimationAsset::validate() const {
    // === 第 1 步：基本数据检查 ===
    if (animations.empty()) {
        slog.w << "AnimationAsset: No animations" << io::endl;
        return true; // 空资产在技术上是有效的
    }

    // === 第 2 步：验证每个动画的内部一致性 ===
    for (size_t i = 0; i < animations.size(); ++i) {
        const auto& anim = animations[i];

        // 调用 Animation::validate() 检查 channels/samplers
        if (!anim.validate()) {
            slog.e << "AnimationAsset: Animation " << i << " ('" << anim.name << "') validation failed"
                   << io::endl;
            return false;
        }

        // 验证 channel 的 targetNodeIndex 是否指向有效节点
        // （因为 Animation 只能验证 samplerIndex，无法验证 targetNodeIndex）
        for (size_t j = 0; j < anim.channels.size(); ++j) {
            const auto& channel = anim.channels[j];

            if (channel.targetNodeIndex < 0 ||
                channel.targetNodeIndex >= static_cast<int>(nodes.size())) {
                slog.e << "AnimationAsset: Animation " << i << " ('" << anim.name
                       << "'): Channel " << j
                       << " has invalid target node index " << channel.targetNodeIndex
                       << " (nodes.size=" << nodes.size() << ")"
                       << io::endl;
                return false;
            }
        }
    }

    // === 第 3 步：检测节点层级中的循环 ===
    // 算法：对每个节点，沿着 parentIndex 向上遍历，直到到达根节点（parentIndex == -1）
    // 如果在遍历过程中访问到已访问过的节点，说明存在循环
    //
    // 示例：正常层级  A → B → C → -1  （无循环）
    //       循环层级  A → B → C → A   （有循环，无法到达根节点）
    std::unordered_set<int> visited;
    for (size_t i = 0; i < nodes.size(); ++i) {
        visited.clear();  // 每个节点独立检查
        int current = static_cast<int>(i);

        // 沿着 parent 链向上遍历
        while (current != -1) {
            // 检测循环：如果当前节点已访问，说明形成了环
            if (visited.count(current)) {
                slog.e << "AnimationAsset: Cycle detected in node hierarchy at node " << i
                       << " ('" << nodes[i].name << "')" << io::endl;
                return false;
            }
            visited.insert(current);
            current = nodes[current].parentIndex;

            // 检查 parentIndex 是否在有效范围内
            if (current >= static_cast<int>(nodes.size())) {
                slog.e << "AnimationAsset: Node " << i << " ('" << nodes[i].name << "')"
                       << " has invalid parent index " << current
                       << " (nodes.size=" << nodes.size() << ")"
                       << io::endl;
                return false;
            }
        }
        // 循环结束，current == -1，说明到达了根节点（无循环）
    }

    // === 第 4 步：验证蒙皮数据的一致性 ===
    // inverseBindMatrices 和 joints 必须成对出现，且数量一致
    if (!inverseBindMatrices.empty() && !joints.empty()) {
        if (inverseBindMatrices.size() != joints.size()) {
            slog.e << "AnimationAsset: Inverse bind matrices count ("
                   << inverseBindMatrices.size()
                   << ") doesn't match joints count ("
                   << joints.size() << ")"
                   << io::endl;
            return false;
        }

        // 检查所有 joints 索引是否指向有效节点
        for (size_t i = 0; i < joints.size(); ++i) {
            if (joints[i] < 0 || joints[i] >= static_cast<int>(nodes.size())) {
                slog.e << "AnimationAsset: Invalid joint index " << joints[i]
                       << " at position " << i
                       << " (nodes.size=" << nodes.size() << ")"
                       << io::endl;
                return false;
            }
        }
    }

    return true;  // 所有检查通过
}

/**
 * 按名称查找节点索引（支持哈希表加速）
 *
 * 查找策略：
 * 1. 优先使用 mBoneNameMap 哈希表（O(1) 复杂度）
 * 2. 如果哈希表为空，降级到线性搜索（O(n) 复杂度）
 *
 * 设计原因：
 * - 哈希表需要手动构建（buildBoneNameMap），加载时默认不构建以节省启动时间
 * - 如果应用不需要按名称查找（如直接按索引访问），可以不构建哈希表
 * - 线性搜索作为后备方案，保证功能始终可用
 */
int AnimationAsset::findNodeByName(const std::string& name) const {
    // 优先使用哈希表（如果已构建）
    if (!mBoneNameMap.empty()) {
        auto it = mBoneNameMap.find(name);
        return (it != mBoneNameMap.end()) ? it->second : -1;
    }

    // 降级到线性搜索（遍历所有节点）
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].name == name) {
            return static_cast<int>(i);
        }
    }

    return -1;  // 未找到
}

/**
 * 构建骨骼名称到节点索引的哈希表
 *
 * 调用时机：
 * - AssetLoader::loadAnimationAsset() 加载完成后自动调用
 * - 如果需要按名称频繁查找，建议加载后立即调用
 *
 * 注意事项：
 * - 只添加有名称的节点（空名称节点会被跳过）
 * - 如果有重名节点，后者会覆盖前者（但这违反 glTF 规范，应在验证阶段报错）
 */
void AnimationAsset::buildBoneNameMap() {
    mBoneNameMap.clear();
    mBoneNameMap.reserve(nodes.size());  // 预分配，避免多次rehash

    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        if (!node.name.empty()) {  // 跳过无名称节点
            mBoneNameMap[node.name] = static_cast<int>(i);
        }
    }
}

/**
 * 按索引获取动画（带越界保护）
 *
 * @param index 动画索引（0 到 getAnimationCount()-1）
 * @return 动画引用
 *
 * 错误处理：
 * - 如果索引越界，记录错误日志并返回空动画或第一个动画（防止崩溃）
 * - 正确的使用方式应该先调用 getAnimationCount() 检查范围
 */
const AnimationAsset::Animation& AnimationAsset::getAnimation(size_t index) const {
    if (index >= animations.size()) {
        slog.e << "AnimationAsset::getAnimation: Index " << index
               << " out of range (animations.size=" << animations.size() << ")"
               << io::endl;
        // 返回第一个动画作为降级方案（不应发生在正确的使用中）
        static const Animation emptyAnimation;
        return animations.empty() ? emptyAnimation : animations[0];
    }
    return animations[index];
}

/**
 * 按名称查找动画（线性搜索）
 *
 * @param name 动画名称（例如 "idle"、"walk"、"run"）
 * @return 动画指针（如果找到）或 nullptr（如果未找到）
 *
 * 使用示例：
 * \code
 * const auto* walkAnim = animAsset->getAnimationByName("walk");
 * if (walkAnim) {
 *     float duration = walkAnim->getDuration();
 *     // 播放 walk 动画...
 * } else {
 *     // 动画不存在，使用默认动画或报错
 * }
 * \endcode
 *
 * 性能说明：
 * - 时间复杂度 O(n)，n 为动画数量
 * - 通常动画数量不多（< 20），线性搜索性能可接受
 * - 如果需要高性能查找，可以在外部维护 name → index 映射
 */
const AnimationAsset::Animation* AnimationAsset::getAnimationByName(const std::string& name) const {
    for (const auto& anim : animations) {
        if (anim.name == name) {
            return &anim;  // 找到，返回指针
        }
    }
    return nullptr;  // 未找到
}

} // namespace filament::gltfio_ext
