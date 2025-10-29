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

#include <gltfio_ext/AnimationBinding.h>

#include <utils/Log.h>

using namespace utils;

namespace filament::gltfio_ext {

/**
 * 构造函数：初始化骨骼绑定（不立即构建映射）
 *
 * 设计原则：
 * - 轻量级构造，仅保存输入指针
 * - 延迟构建映射（通过 buildMapping() 显式触发）
 * - 允许在构造后检查参数有效性
 *
 * 注意事项：
 * - 所有输入参数必须在 AnimationBinding 生命周期内保持有效
 * - 如果 externalAnim 或 residentMesh 被销毁，行为未定义（悬空指针）
 */
AnimationBinding::AnimationBinding(
    const AnimationAsset* externalAnim,
    const FilamentAsset* residentMesh,
    filament::Engine* engine)
    : mExternalAnim(externalAnim)
    , mResidentMesh(residentMesh)
    , mEngine(engine) {
    // 注意：构造函数不调用 buildMapping()，由用户显式调用
    // 这样设计的原因：
    // 1. 允许用户在绑定前检查参数有效性
    // 2. 避免构造时抛出异常或记录错误日志
}

/**
 * 构建骨骼名称映射（核心方法）
 *
 * 映射流程：
 * 1. 清空之前的映射数据（允许重复调用）
 * 2. 从驻留网格提取所有命名实体（buildResidentBoneMap）
 * 3. 遍历动画节点，按名称匹配到网格实体（matchByBoneName）
 * 4. 计算匹配率并验证（getMatchRate）
 * 5. 记录未匹配的骨骼名称（用于调试）
 *
 * 返回值：
 * - true: 匹配率 >= 90%（成功）
 * - false: 匹配率 < 90% 或发生错误（失败）
 *
 * 常见失败原因：
 * - 驻留网格没有 NameComponentManager（导致 0 匹配率）
 * - 动画和网格来自不同的角色模型（骨骼名称完全不同）
 * - 骨骼命名不一致（如 "Spine" vs "spine"，大小写敏感）
 */
bool AnimationBinding::buildMapping() {
    // === 第 1 步：清空之前的映射数据 ===
    // 允许重复调用 buildMapping()，例如切换到不同的动画资产
    mNodeToEntityMap.clear();
    mNodeToInstanceMap.clear();
    mUnmatchedBones.clear();

    // === 第 2 步：参数有效性检查 ===
    if (!mExternalAnim || !mResidentMesh || !mEngine) {
        slog.e << "AnimationBinding: Invalid input parameters (null pointers)" << io::endl;
        return false;
    }

    if (mExternalAnim->nodes.empty()) {
        slog.e << "AnimationBinding: External animation has no nodes" << io::endl;
        return false;
    }

    // === 第 3 步：从驻留网格构建骨骼名称映射表 ===
    // residentBoneMap 格式：{ "Spine" → Entity(123), "Head" → Entity(456), ... }
    std::map<std::string, Entity> residentBoneMap;
    buildResidentBoneMap(residentBoneMap);

    if (residentBoneMap.empty()) {
        slog.e << "AnimationBinding: No named entities found in resident mesh. "
               << "Did you configure AssetConfiguration.names?" << io::endl;
        return false;
    }

    slog.i << "AnimationBinding: Resident mesh has " << residentBoneMap.size()
           << " named entities" << io::endl;

    // === 第 4 步：遍历动画节点，按名称匹配到网格实体 ===
    int matchedCount = 0;
    for (size_t i = 0; i < mExternalAnim->nodes.size(); i++) {
        const auto& node = mExternalAnim->nodes[i];

        // 尝试按名称匹配
        // 成功：mNodeToEntityMap 和 mNodeToInstanceMap 被更新
        // 失败：node.name 被添加到 mUnmatchedBones（用于调试）
        if (matchByBoneName(static_cast<int>(i), node.name, residentBoneMap)) {
            matchedCount++;
        } else {
            mUnmatchedBones.push_back(node.name);
        }
    }

    // === 第 5 步：计算匹配率并验证阈值 ===
    // 匹配率 = 匹配成功的骨骼数 / 总动画节点数
    float matchRate = static_cast<float>(matchedCount) / mExternalAnim->nodes.size();

    // 检查匹配率是否满足要求（>= 90%）
    if (matchRate < (1.0f - TOLERANCE_THRESHOLD)) {
        slog.e << "AnimationBinding: Bone matching rate too low: " << (matchRate * 100.0f) << "%"
               << " (" << matchedCount << "/" << mExternalAnim->nodes.size() << ")"
               << io::endl;
        slog.e << "AnimationBinding: Required: " << ((1.0f - TOLERANCE_THRESHOLD) * 100.0f) << "%" << io::endl;
        return false;  // 匹配率不足，绑定失败
    }

    // === 第 6 步：记录结果（成功） ===
    // 如果有未匹配的骨骼，记录警告日志（但不影响成功）
    if (!mUnmatchedBones.empty()) {
        slog.w << "AnimationBinding: " << mUnmatchedBones.size() << " bones not matched:" << io::endl;
        for (const auto& bone : mUnmatchedBones) {
            slog.w << "  - " << bone << io::endl;
        }
    }

    slog.i << "AnimationBinding: Bone mapping built successfully: " << matchedCount << "/"
           << mExternalAnim->nodes.size() << " matched (" << (matchRate * 100.0f) << "%)" << io::endl;

    return true;  // 绑定成功
}

/**
 * 从驻留网格构建骨骼名称到实体的映射表
 *
 * 流程详解：
 * 1. 获取驻留网格的所有实体（getEntities）
 * 2. 遍历每个实体，查询其名称（通过 FilamentAsset::getName）
 * 3. 如果实体有名称，添加到映射表（名称 → Entity）
 *
 * 示例输出（ecorche_full.glb）：
 * - 742 个实体，所有实体都有名称
 * - 映射表包含 742 个条目（包括骨骼、mesh、camera 等所有实体）
 * - 匹配时只会用到骨骼相关的实体（327 个）
 */
void AnimationBinding::buildResidentBoneMap(std::map<std::string, Entity>& outMap) {
    // 获取驻留网格的所有实体
    const Entity* entities = mResidentMesh->getEntities();
    size_t entityCount = mResidentMesh->getEntityCount();

    for (size_t i = 0; i < entityCount; i++) {
        Entity entity = entities[i];

        // 从 FilamentAsset 获取实体名称（通过 NameComponentManager）
        const char* name = mResidentMesh->getName(entity);
        if (name && name[0] != '\0') {
            outMap[std::string(name)] = entity;
        }
    }

    slog.i << "AnimationBinding: Resident mesh has " << outMap.size() << " named entities" << io::endl;
}

/**
 * 按骨骼名称匹配单个动画节点到网格实体
 *
 * 算法：
 * 1. 在 residentBoneMap 中查找 boneName（O(log n) 复杂度）
 * 2. 如果找到，获取对应的 Entity
 * 3. 通过 TransformManager::getInstance(entity) 获取变换实例
 * 4. 验证 instance.isValid()（确保实体有 TransformComponent）
 * 5. 将映射结果存入 mNodeToEntityMap 和 mNodeToInstanceMap
 *
 * 失败情况：
 * - boneName 在 residentBoneMap 中不存在（网格缺少该骨骼）
 * - Entity 没有 TransformManager 组件（理论上不应发生，但做防御性检查）
 *
 * 为何需要 TransformManager::Instance：
 * - 动画播放时，需要调用 transformManager->setTransform(instance, matrix)
 * - 直接存储 Instance 比每次通过 Entity 查询更高效
 *
 * @return true 匹配成功，false 匹配失败
 */
bool AnimationBinding::matchByBoneName(
    int nodeIndex,
    const std::string& boneName,
    const std::map<std::string, Entity>& residentBoneMap) {

    // 在映射表中查找骨骼名称
    auto it = residentBoneMap.find(boneName);
    if (it == residentBoneMap.end()) {
        // 骨骼在驻留网格中不存在（常见于动画有额外的辅助骨骼）
        return false;
    }

    Entity entity = it->second;

    // 存储 node → entity 映射（主要用于调试）
    mNodeToEntityMap[nodeIndex] = entity;

    // 获取 TransformManager::Instance（核心映射，用于动画播放）
    auto& tm = mEngine->getTransformManager();
    auto instance = tm.getInstance(entity);

    // 验证实例有效性（防御性检查）
    if (!instance.isValid()) {
        slog.w << "AnimationBinding: Entity '" << boneName << "' has no Transform component" << io::endl;
        // 从 entity map 中移除（无法使用的映射）
        mNodeToEntityMap.erase(nodeIndex);
        return false;
    }

    // 存储 node → transform instance 映射（这是最终使用的映射）
    mNodeToInstanceMap[nodeIndex] = instance;

    return true;  // 匹配成功
}

/**
 * 验证当前映射的有效性
 *
 * 检查项：
 * - 所有映射的实体是否仍然有效（Entity 未被销毁）
 * - 所有映射的实体是否仍有 TransformComponent（Component 未被移除）
 *
 * 使用场景：
 * - 在动画播放前验证绑定仍然有效
 * - 在长时间运行的应用中定期验证（防止资源被释放）
 *
 * @return true 所有映射仍然有效，false 存在无效映射
 */
bool AnimationBinding::validateMapping() const {
    if (mNodeToEntityMap.empty()) {
        return false;  // 空映射视为无效
    }

    auto& tm = mEngine->getTransformManager();

    // 遍历所有映射的实体，验证它们仍有 TransformComponent
    for (const auto& [nodeIndex, entity] : mNodeToEntityMap) {
        if (!tm.hasComponent(entity)) {
            slog.e << "AnimationBinding: Entity for node " << nodeIndex
                   << " no longer has a transform component" << io::endl;
            return false;
        }
    }

    return true;  // 所有映射仍然有效
}

/**
 * 计算骨骼匹配率
 *
 * 公式：match_rate = matched_bones_count / total_animation_nodes_count
 *
 * 返回值范围：[0.0, 1.0]
 * - 0.0: 没有任何骨骼匹配
 * - 1.0: 所有骨骼都匹配
 * - 0.9: 90% 的骨骼匹配（阈值要求）
 *
 * 示例：
 * - 327 个动画节点，327 个匹配 → 1.0 (100%)
 * - 100 个动画节点，90 个匹配 → 0.9 (90%)
 * - 100 个动画节点，85 个匹配 → 0.85 (85%，低于阈值)
 */
float AnimationBinding::getMatchRate() const {
    if (!mExternalAnim || mExternalAnim->nodes.empty()) {
        return 0.0f;  // 无动画数据，匹配率为 0
    }

    int matchedCount = static_cast<int>(mNodeToEntityMap.size());
    return static_cast<float>(matchedCount) / mExternalAnim->nodes.size();
}

} // namespace filament::gltfio_ext
