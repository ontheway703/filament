/**
 * ============================================================================
 * test_skeleton.cpp - SkeletonAsset 基础功能测试
 * ============================================================================
 *
 * 【测试目标】
 * 验证 SkeletonAsset 的核心功能：
 * - 骨骼层次结构的构建和管理
 * - 骨骼名称到索引的映射查询
 * - Entity 实体的创建和访问
 * - 逆绑定矩阵(Inverse Bind Matrices)的存储
 *
 * 【背景知识】
 * Skeleton（骨骼）是 3D 角色动画的基础，由多个骨骼(Bone)组成层次树结构。
 * 每个骨骼对应一个 Filament Entity，通过 TransformManager 管理其变换。
 *
 * 关键概念：
 * - Bone Hierarchy（骨骼层次）: 父子关系构成的树形结构，如 root -> spine -> arm
 * - Inverse Bind Matrix（逆绑定矩阵）: 将顶点从模型空间转到骨骼局部空间的矩阵，
 *   用于蒙皮计算。公式：finalMatrix = boneMatrix * inverseBindMatrix
 * - Entity: Filament 的 ECS（Entity-Component-System）架构中的实体 ID
 *
 * 【测试方法】
 * 单元测试 - 手动构造最小化的骨骼数据（3个骨骼的简单链），验证 API 正确性
 *
 * 【关键验证点】
 * 1. 骨骼数量统计 - getBoneCount()
 * 2. 名称查询 - getBoneIndex(name) 返回正确索引
 * 3. Entity 访问 - getBoneEntity(index) 返回有效实体
 * 4. 逆绑定矩阵存储 - getInverseBindMatrices() 返回非空数组
 * 5. 边界情况 - 不存在的名称返回 -1
 *
 * 【与其他测试的关系】
 * - 前置：无（最基础的测试）
 * - 后续：test_animator.cpp 需要 SkeletonAsset 来播放动画
 *         test_mesh.cpp 需要 SkeletonAsset 来绑定蒙皮网格
 *
 * 【运行方式】
 * ./out/cmake-debug/libs/gltfio_ext/test_skeleton
 *
 * 【预期输出】
 * 所有断言通过，显示 "=== SkeletonAsset Test PASSED ==="
 * ============================================================================
 */

#include <gltfio/AssetLoaderExt.h>
#include <gltfio/SkeletonAsset.h>
#include "../src/FSkeletonAsset.h"

#include <filament/Engine.h>
#include <filament/TransformManager.h>
#include <utils/EntityManager.h>
#include <iostream>

using namespace filament;
using namespace filament::gltfio;
using namespace utils;

int main() {
    std::cout << "=== SkeletonAsset Test ===" << std::endl;

    // ========================================================================
    // 步骤 1: 创建 Filament 渲染引擎环境
    // ========================================================================
    // Engine: Filament 的核心渲染引擎实例
    // EntityManager: 管理所有实体的全局单例
    // TransformManager: 管理所有实体的变换（位置、旋转、缩放）
    Engine* engine = Engine::create();
    auto& em = EntityManager::get();
    auto& tm = engine->getTransformManager();

    // ========================================================================
    // 步骤 2: 创建骨骼资源（FSkeletonAsset 是内部实现类）
    // ========================================================================
    // 注意：正常使用中，SkeletonAsset 由 AssetLoaderExt 从 glTF 文件加载。
    //       这里为了测试，我们手动构造一个最小化的骨骼。
    FSkeletonAsset* skeleton = new FSkeletonAsset();
    skeleton->mEngine = engine;
    skeleton->mEntityManager = &em;
    skeleton->mTransformManager = &tm;

    // ========================================================================
    // 步骤 3: 构造测试用的骨骼层次结构
    // ========================================================================
    // 创建一个简单的骨骼链：bone_0 -> bone_1 -> bone_2
    // 这模拟了一个最简单的骨骼层次，比如 root -> upperArm -> forearm
    const size_t boneCount = 3;
    skeleton->mBoneEntities.reserve(boneCount);       // Entity ID 数组
    skeleton->mBoneNames.reserve(boneCount);          // 骨骼名称数组
    skeleton->mInverseBindMatrices.reserve(boneCount);// 逆绑定矩阵数组

    // 创建3个骨骼，为每个骨骼分配 Entity 和设置属性
    for (size_t i = 0; i < boneCount; ++i) {
        // 创建 Entity（Filament 的实体 ID）
        Entity entity = em.create();
        skeleton->mBoneEntities.push_back(entity);

        // 为 Entity 创建 Transform 组件（如果不存在）
        // Transform 用于存储骨骼的位置、旋转、缩放
        auto ti = tm.getInstance(entity);
        if (!ti) {
            tm.create(entity);
            ti = tm.getInstance(entity);
        }

        // 设置骨骼名称（用于动画通道查找）
        // glTF 动画通过骨骼名称来指定目标骨骼
        char nameBuf[32];
        snprintf(nameBuf, sizeof(nameBuf), "bone_%zu", i);
        skeleton->mBoneNames.emplace_back(nameBuf);
        skeleton->mBoneNameToIndex[nameBuf] = i;  // 名称到索引的快速查找映射

        // 设置逆绑定矩阵（这里使用单位矩阵简化测试）
        // 实际应用中，这个矩阵由建模软件（如 Blender）导出
        skeleton->mInverseBindMatrices.push_back(math::mat4f());

        // 设置父子关系，构建骨骼层次树
        // bone_1 的父亲是 bone_0，bone_2 的父亲是 bone_1
        if (i > 0) {
            auto parentTi = tm.getInstance(skeleton->mBoneEntities[i - 1]);
            tm.setParent(ti, parentTi);
        }
    }

    // 设置根骨骼（层次树的顶端）
    skeleton->mRoot = skeleton->mBoneEntities[0];

    // ========================================================================
    // 步骤 4: 测试 SkeletonAsset 的公开 API
    // ========================================================================
    std::cout << "\n--- Testing API ---" << std::endl;

    // 【测试点 1】getBoneCount() - 验证骨骼数量统计
    // 应该返回我们创建的3个骨骼
    std::cout << "Bone count: " << skeleton->getBoneCount() << std::endl;
    assert(skeleton->getBoneCount() == 3);

    // 【测试点 2】getBoneIndex() - 验证名称到索引的查询
    // 测试场景 A：查询存在的骨骼名称
    int index = skeleton->getBoneIndex("bone_1");
    std::cout << "Name lookup test: 'bone_1' -> index " << index << std::endl;
    assert(index == 1);  // 期望返回索引 1

    // 测试场景 B：查询不存在的骨骼名称（边界情况）
    index = skeleton->getBoneIndex("nonexistent");
    std::cout << "Name lookup test: 'nonexistent' -> index " << index << std::endl;
    assert(index == -1);  // 期望返回 -1 表示未找到

    // 【测试点 3】getBoneEntity() - 验证 Entity 访问接口
    // 动画播放和蒙皮计算需要通过 Entity 访问骨骼的变换矩阵
    Entity entity0 = skeleton->getBoneEntity(0);
    Entity entity1 = skeleton->getBoneEntity(1);
    std::cout << "\ngetBoneEntity test:" << std::endl;
    std::cout << "  Entity[0]: " << entity0.getId() << std::endl;
    std::cout << "  Entity[1]: " << entity1.getId() << std::endl;
    assert(entity0);  // 验证 Entity 有效（非空）
    assert(entity1);

    // 【测试点 4】getInverseBindMatrices() - 验证逆绑定矩阵存储
    // 这些矩阵在 GPU 蒙皮计算中必需
    const auto* ibm = skeleton->getInverseBindMatrices();
    std::cout << "\nInverse bind matrices: " << (ibm ? "OK" : "NULL") << std::endl;
    assert(ibm != nullptr);

    // ========================================================================
    // 步骤 5: 清理资源
    // ========================================================================
    // 重要：避免内存泄漏，按顺序清理资源
    delete skeleton;
    Engine::destroy(&engine);

    std::cout << "\n=== SkeletonAsset Test PASSED ===" << std::endl;
    return 0;
}
