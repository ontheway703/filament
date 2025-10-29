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

//! \file AnimationBinding.h
//! \brief 外部动画与驻留网格的骨骼名称映射
//!
//! 设计背景：
//! -----------
//! 当动画数据（AnimationAsset）和网格数据（FilamentAsset）分别加载时，
//! 需要建立两者之间的关联关系。AnimationBinding 通过骨骼名称匹配实现这一映射。
//!
//! 核心问题：
//! - AnimationAsset 存储的是节点索引（0, 1, 2, ...）
//! - FilamentAsset 存储的是 Filament Entity（实体句柄）
//! - 需要建立"动画节点索引 → 网格实体 → TransformManager::Instance"的映射链
//!
//! 映射流程：
//! 1. 从 FilamentAsset 提取所有命名实体（通过 NameComponentManager）
//! 2. 构建"骨骼名称 → Entity"的映射表（驻留网格侧）
//! 3. 遍历 AnimationAsset 的节点，按名称查找对应的 Entity
//! 4. 将 Entity 转换为 TransformManager::Instance（用于应用变换）
//! 5. 存储"动画节点索引 → TransformManager::Instance"的最终映射
//!
//! 匹配策略：
//! ---------
//! - **大小写敏感**：骨骼名称必须完全匹配（如 "Spine" != "spine"）
//! - **容差机制**：允许最多 10% 的骨骼不匹配（match rate >= 90%）
//! - **空名称处理**：无名称的节点会被跳过（无法参与匹配）
//!
//! 使用场景：
//! ---------
//! - **动画复用**：一套角色网格 + 多套外部动画（idle.glb, walk.glb, run.glb）
//! - **动画库**：预先加载大量动画，运行时动态切换
//! - **动画混合**：Phase 2 将支持多个 AnimationBinding 同时驱动一个网格

#ifndef GLTFIO_EXT_ANIMATIONBINDING_H
#define GLTFIO_EXT_ANIMATIONBINDING_H

#include <gltfio_ext/AnimationAsset.h>
#include <gltfio_ext/FilamentAsset.h>

#include <filament/Engine.h>
#include <filament/TransformManager.h>

#include <utils/Entity.h>

#include <map>
#include <vector>
#include <string>

namespace filament::gltfio_ext {

/**
 * \class AnimationBinding AnimationBinding.h gltfio_ext/AnimationBinding.h
 * \brief 通过骨骼名称将外部动画节点映射到驻留网格实体
 *
 * AnimationBinding 建立动画数据（AnimationAsset）和可渲染网格（FilamentAsset）
 * 之间的映射关系，通过骨骼名称匹配实现外部动画驱动驻留网格的骨架。
 *
 * 核心功能：
 * - **骨骼名称匹配**（大小写敏感）
 * - **容差机制**（允许 10% 不匹配，match rate >= 90%）
 * - **NodeIndex → Entity 映射**（动画节点 → 网格实体）
 * - **NodeIndex → TransformManager::Instance 映射**（用于应用变换）
 * - **映射完整性验证**（检测无效映射和匹配率）
 *
 * 设计要点：
 * ----------
 * 1. **依赖 NameComponentManager**：
 *    - 驻留网格必须有命名实体（通过 AssetConfiguration.names 配置）
 *    - 没有 NameComponentManager 会导致 0 匹配率
 *
 * 2. **映射存储**：
 *    - mBoneMapping: 动画节点索引 → TransformManager::Instance（核心映射）
 *    - mResidentBoneMap: 骨骼名称 → Entity（驻留网格的查找表）
 *
 * 3. **匹配率计算**：
 *    - match_rate = matched_bones / total_animation_nodes
 *    - 例如：327 个动画节点，327 个匹配 → 100%
 *    - 例如：100 个动画节点，85 个匹配 → 85%（低于 90%，会警告）
 *
 * 4. **空 Instance 处理**：
 *    - TransformManager::Instance() 表示无效实例（未匹配或 Entity 无 transform）
 *    - 动画播放时应跳过无效实例
 *
 * 真实案例（ecorche_full.glb）：
 * ------------------------------
 * - 驻留网格：742 个命名实体，327 个是骨骼节点
 * - 外部动画：327 个动画节点（Pull/Push/Squat 三个动画共享同一骨架）
 * - 匹配结果：327/327 匹配（100%）
 * - 映射表大小：327 个 TransformManager::Instance
 *
 * 使用示例：
 * \code
 * // 1. 加载网格和动画
 * FilamentAsset* meshAsset = assetLoader->createAsset(meshData, meshDataSize);
 * AnimationAsset* animAsset = assetLoader->loadAnimationAsset(animData, animDataSize);
 *
 * // 2. 创建绑定
 * AnimationBinding binding(animAsset, meshAsset, engine);
 *
 * // 3. 检查匹配率
 * float matchRate = binding.getMatchRate();
 * if (matchRate < 0.9f) {
 *     utils::slog.w << "Warning: Only " << (matchRate * 100) << "% bones matched!" << utils::io::endl;
 * }
 *
 * // 4. 获取映射表
 * const auto& boneMapping = binding.getBoneMapping();
 *
 * // 5. 使用映射表应用动画（Phase 2 - Animator）
 * for (size_t i = 0; i < animAsset->nodes.size(); ++i) {
 *     TransformManager::Instance instance = boneMapping[i];
 *     if (instance.isValid()) {
 *         // 从 animation channel 读取变换数据，应用到 instance
 *         transformManager->setTransform(instance, transform);
 *     }
 * }
 * \endcode
 *
 * 常见问题：
 * ---------
 * Q: 为什么匹配率是 0%？
 * A: 检查 AssetLoader 是否配置了 NameComponentManager（config.names）
 *
 * Q: 为什么有些骨骼不匹配？
 * A: 可能原因：(1) 骨骼名称不一致  (2) 网格 GLB 缺少某些骨骼  (3) 动画 GLB 有额外骨骼
 *
 * Q: 可以绑定多个动画到同一个网格吗？
 * A: 可以，创建多个 AnimationBinding 对象，它们共享同一个 FilamentAsset
 */
class UTILS_PUBLIC AnimationBinding {
public:
    /**
     * Creates an AnimationBinding.
     *
     * @param externalAnim External animation asset (must remain valid during binding lifetime)
     * @param residentMesh Resident mesh asset (must remain valid during binding lifetime)
     * @param engine Filament engine instance
     */
    AnimationBinding(
        const AnimationAsset* externalAnim,
        const FilamentAsset* residentMesh,
        filament::Engine* engine
    );

    /**
     * Destructor.
     */
    ~AnimationBinding() = default;

    /**
     * Builds the bone name mapping between external animation and resident mesh.
     *
     * This method:
     * 1. Extracts all named entities from the resident mesh
     * 2. Matches each animation node by name to a mesh entity
     * 3. Retrieves TransformManager::Instance for each match
     * 4. Calculates match rate and validates against tolerance threshold
     *
     * @return true if mapping succeeds (match rate >= 90%), false otherwise
     */
    bool buildMapping();

    /**
     * Gets the NodeIndex → Entity mapping.
     *
     * @return Map from animation node indices to mesh entities
     */
    const std::map<int, utils::Entity>& getNodeToEntityMap() const {
        return mNodeToEntityMap;
    }

    /**
     * Gets the NodeIndex → TransformManager::Instance mapping.
     *
     * @return Map from animation node indices to transform instances
     */
    const std::map<int, TransformManager::Instance>& getNodeToInstanceMap() const {
        return mNodeToInstanceMap;
    }

    /**
     * Gets the list of unmatched bone names.
     *
     * @return Vector of bone names that could not be matched
     */
    const std::vector<std::string>& getUnmatchedBones() const {
        return mUnmatchedBones;
    }

    /**
     * Validates the current mapping.
     *
     * Checks that all mapped TransformManager::Instance objects are still alive.
     *
     * @return true if all instances are valid, false otherwise
     */
    bool validateMapping() const;

    /**
     * Calculates the bone match rate.
     *
     * @return Match rate as a float in [0.0, 1.0], where 1.0 means all bones matched
     */
    float getMatchRate() const;

private:
    // === 输入数据（弱引用，不拥有所有权） ===

    //! 外部动画资产（动画节点 + 关键帧数据）
    //! 必须在 AnimationBinding 生命周期内保持有效
    const AnimationAsset* mExternalAnim;

    //! 驻留网格资产（可渲染的网格实体）
    //! 必须在 AnimationBinding 生命周期内保持有效
    const FilamentAsset* mResidentMesh;

    //! Filament 引擎实例（用于访问 TransformManager 和 NameComponentManager）
    filament::Engine* mEngine;

    // === 映射结果（构建后存储） ===

    //! 动画节点索引 → 网格实体
    //! 示例：{ 0 → Entity(123), 1 → Entity(456), ... }
    //! 用途：快速查找动画节点对应的网格实体（很少直接使用，主要用于调试）
    std::map<int, utils::Entity> mNodeToEntityMap;

    //! 动画节点索引 → TransformManager::Instance（核心映射）
    //! 示例：{ 0 → Instance(...), 1 → Instance(...), 2 → Instance() [invalid], ... }
    //! 用途：动画播放时，直接通过节点索引获取 TransformManager::Instance 以应用变换
    //!
    //! 数组布局：
    //! - 索引对应 AnimationAsset::nodes 数组的索引
    //! - Instance() 表示未匹配或无效实例（需要跳过）
    //! - Instance(...) 表示有效实例（可以应用变换）
    std::map<int, TransformManager::Instance> mNodeToInstanceMap;

    //! 未匹配的骨骼名称列表
    //! 用途：调试和日志输出，帮助定位匹配失败的原因
    //! 示例：["LeftHandThumb", "RightHandThumb"] 表示这两个骨骼在网格中不存在
    std::vector<std::string> mUnmatchedBones;

    // === 容差阈值 ===

    //! 容差阈值：10% 不匹配是可接受的
    //! 即要求至少 90% 的骨骼名称匹配成功
    //!
    //! 设计原因：
    //! - 允许轻微的骨骼差异（如动画有额外的辅助骨骼，网格中没有）
    //! - 防止完全不兼容的动画和网格绑定（如人形动画绑定到四足动物）
    //!
    //! 示例：
    //! - 100 个动画节点，90 个匹配 → 90% ≥ 90%，通过验证
    //! - 100 个动画节点，85 个匹配 → 85% < 90%，验证失败
    static constexpr float TOLERANCE_THRESHOLD = 0.1f;

    // === 内部方法 ===

    /**
     * 从驻留网格构建骨骼名称到实体的映射表
     *
     * 流程：
     * 1. 遍历 FilamentAsset 的所有实体（getEntities()）
     * 2. 通过 NameComponentManager::getInstance(entity) 获取实体名称
     * 3. 将 (名称 → Entity) 存入映射表
     *
     * 依赖：
     * - 需要 NameComponentManager（通过 AssetConfiguration.names 配置）
     * - 如果没有 NameComponentManager，映射表为空，导致 0 匹配率
     *
     * @param outMap 输出映射表（BoneName → Entity）
     *
     * 示例输出：
     * \code
     * outMap = {
     *   "Armature"   → Entity(100),
     *   "Spine"      → Entity(101),
     *   "Spine.001"  → Entity(102),
     *   "Head"       → Entity(103),
     *   ...
     * }
     * \endcode
     */
    void buildResidentBoneMap(std::map<std::string, utils::Entity>& outMap);

    /**
     * 按骨骼名称匹配单个动画节点到网格实体
     *
     * 算法：
     * 1. 在 residentBoneMap 中查找 boneName
     * 2. 如果找到，获取对应的 Entity
     * 3. 通过 TransformManager::getInstance(entity) 获取变换实例
     * 4. 将映射结果存入 mNodeToEntityMap 和 mNodeToInstanceMap
     *
     * 失败情况：
     * - boneName 在 residentBoneMap 中不存在（网格缺少该骨骼）
     * - Entity 没有 TransformManager 组件（理论上不应发生）
     *
     * @param nodeIndex 动画节点索引（AnimationAsset::nodes 的索引）
     * @param boneName 骨骼名称（AnimationNode::name）
     * @param residentBoneMap 预构建的驻留网格骨骼映射表
     * @return true 匹配成功，false 匹配失败
     *
     * 使用示例（在 buildMapping() 中调用）：
     * \code
     * for (size_t i = 0; i < externalAnim->nodes.size(); ++i) {
     *     const auto& node = externalAnim->nodes[i];
     *     if (!node.name.empty()) {
     *         if (matchByBoneName(i, node.name, residentBoneMap)) {
     *             matchedCount++;
     *         } else {
     *             mUnmatchedBones.push_back(node.name);
     *         }
     *     }
     * }
     * \endcode
     */
    bool matchByBoneName(
        int nodeIndex,
        const std::string& boneName,
        const std::map<std::string, utils::Entity>& residentBoneMap
    );
};

} // namespace filament::gltfio_ext

#endif // GLTFIO_EXT_ANIMATIONBINDING_H
