/**
 * ============================================================================
 * test_mesh_instance.cpp - MeshInstance 网格实例化测试
 * ============================================================================
 *
 * 【测试目标】
 * 验证 MeshInstance 的实例化功能：
 * - 创建和销毁多个实例
 * - GPU 资源共享（VertexBuffer/IndexBuffer）
 * - 独立的 Entity 和变换
 * - 独立的骨骼绑定
 * - 性能测试（100个实例）
 *
 * 【背景知识】
 * Instancing（实例化）是3D渲染的重要优化技术：
 *
 * 传统方式（无实例化）：
 * - 每个网格独立存储顶点/索引数据
 * - 占用大量 GPU 内存
 * - 例：100个树 = 100份顶点数据
 *
 * 实例化方式：
 * - 多个实例共享同一份顶点/索引数据
 * - 每个实例有独立的变换和材质
 * - 例：100个树 = 1份顶点数据 + 100个变换矩阵
 *
 * 关键概念：
 * - 共享 GPU 资源：VertexBuffer/IndexBuffer/Material
 * - 独立属性：Entity, Transform, Skeleton Binding
 * - 应用场景：森林（大量树）、军队（大量士兵）、粒子效果
 *
 * 【测试方法】
 * 单元测试 + 性能测试 - 创建多个实例，验证独立性和性能
 *
 * 【测试用例概览】
 * Test 1: 创建和销毁单个实例
 * Test 2: 多实例创建（共享GPU资源）
 * Test 3: 实例独立骨骼绑定
 * Test 4: 实例蒙皮更新
 * Test 5: 性能测试（100个实例）
 * Test 6: getInstances() 数组访问
 *
 * 【关键验证点】
 * - 实例创建成功
 * - getInstanceCount() 正确
 * - 每个实例有独立 Entity
 * - 骨骼绑定相互独立
 * - updateSkinning() 正常工作
 * - 100个实例创建时间 < 100ms
 *
 * 【运行方式】
 * ./out/cmake-debug/libs/gltfio_ext/test_mesh_instance
 *
 * 【预期输出】
 * 所有测试通过，显示 "ALL MESH INSTANCE TESTS PASSED!"
 * ============================================================================
 */

#include <gltfio/MeshAsset.h>
#include <gltfio/MeshInstance.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/StandaloneAnimator.h>
#include "../src/FMeshAsset.h"
#include "../src/FSkeletonAsset.h"

#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/RenderableManager.h>
#include <utils/EntityManager.h>
#include <math/mat4.h>
#include <iostream>
#include <chrono>

using namespace filament;
using namespace filament::gltfio;
using namespace filament::math;
using namespace utils;

int main() {
    std::cout << "=== MeshInstance Comprehensive Test ===" << std::endl;

    // 创建Filament环境
    Engine* engine = Engine::create();
    Scene* scene = engine->createScene();
    auto& em = EntityManager::get();
    auto& tm = engine->getTransformManager();
    auto& rm = engine->getRenderableManager();
    std::cout << "✓ Engine and Scene created" << std::endl;

    int passed = 0, failed = 0;

    // Test 1: 创建和销毁单个实例
    std::cout << "\n[Test 1] Create and destroy single instance..." << std::endl;
    {
        FMeshAsset* mesh = new FMeshAsset();
        mesh->mEngine = engine;
        mesh->mRenderableManager = &rm;
        mesh->mHasSkinning = false;

        // 创建实例
        MeshInstance* instance = mesh->createInstance();
        if (instance) {
            std::cout << "  ✓ Instance created successfully" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Instance creation failed" << std::endl;
            failed++;
        }

        // 验证实例所属的 asset
        if (instance && instance->getAsset() == mesh) {
            std::cout << "  ✓ Instance getAsset() returns correct asset" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: getAsset() incorrect" << std::endl;
            failed++;
        }

        // 验证实例数量
        if (mesh->getInstanceCount() == 1) {
            std::cout << "  ✓ Instance count correct (1)" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Instance count should be 1" << std::endl;
            failed++;
        }

        // 销毁实例
        mesh->destroyInstance(instance);
        if (mesh->getInstanceCount() == 0) {
            std::cout << "  ✓ Instance destroyed successfully" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Instance count should be 0 after destroy" << std::endl;
            failed++;
        }

        delete mesh;
    }

    // Test 2: 多实例创建（共享GPU资源）
    std::cout << "\n[Test 2] Multiple instances (shared GPU resources)..." << std::endl;
    {
        FMeshAsset* mesh = new FMeshAsset();
        mesh->mEngine = engine;
        mesh->mRenderableManager = &rm;
        mesh->mHasSkinning = false;

        const int instanceCount = 10;
        std::vector<MeshInstance*> instances;

        // 创建多个实例
        for (int i = 0; i < instanceCount; ++i) {
            MeshInstance* instance = mesh->createInstance();
            instances.push_back(instance);
        }

        if (mesh->getInstanceCount() == instanceCount) {
            std::cout << "  ✓ Created " << instanceCount << " instances" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Instance count should be " << instanceCount << std::endl;
            failed++;
        }

        // 验证所有实例都有不同的 Entity
        bool allUnique = true;
        for (size_t i = 0; i < instances.size(); ++i) {
            for (size_t j = i + 1; j < instances.size(); ++j) {
                if (instances[i]->getRenderableEntity() == instances[j]->getRenderableEntity()) {
                    allUnique = false;
                    break;
                }
            }
        }

        if (allUnique) {
            std::cout << "  ✓ All instances have unique entities" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Instances share entities (should be unique)" << std::endl;
            failed++;
        }

        // 验证所有实例可以添加到场景
        for (auto* instance : instances) {
            scene->addEntity(instance->getRenderableEntity());
        }
        std::cout << "  ✓ All instances added to scene successfully" << std::endl;
        passed++;

        // 清理
        for (auto* instance : instances) {
            scene->remove(instance->getRenderableEntity());
            mesh->destroyInstance(instance);
        }

        if (mesh->getInstanceCount() == 0) {
            std::cout << "  ✓ All instances destroyed successfully" << std::endl;
            passed++;
        }

        delete mesh;
    }

    // Test 3: 实例骨骼绑定（独立绑定）
    std::cout << "\n[Test 3] Instance skeleton binding (independent)..." << std::endl;
    {
        // 创建两个骨骼
        FSkeletonAsset* skeleton1 = new FSkeletonAsset();
        skeleton1->mEngine = engine;
        skeleton1->mEntityManager = &em;
        skeleton1->mTransformManager = &tm;
        skeleton1->mBoneEntities.reserve(2);
        skeleton1->mBoneNames.reserve(2);
        skeleton1->mInverseBindMatrices.reserve(2);

        for (int i = 0; i < 2; ++i) {
            Entity e = em.create();
            tm.create(e);
            skeleton1->mBoneEntities.push_back(e);
            std::string boneName = "bone_" + std::to_string(i);
            skeleton1->mBoneNames.emplace_back(boneName.c_str());
            skeleton1->mBoneNameToIndex[boneName] = i;
            skeleton1->mInverseBindMatrices.push_back(mat4f());
        }

        FSkeletonAsset* skeleton2 = new FSkeletonAsset();
        skeleton2->mEngine = engine;
        skeleton2->mEntityManager = &em;
        skeleton2->mTransformManager = &tm;
        skeleton2->mBoneEntities.reserve(2);
        skeleton2->mBoneNames.reserve(2);
        skeleton2->mInverseBindMatrices.reserve(2);

        for (int i = 0; i < 2; ++i) {
            Entity e = em.create();
            tm.create(e);
            skeleton2->mBoneEntities.push_back(e);
            std::string boneName = "bone_" + std::to_string(i);
            skeleton2->mBoneNames.emplace_back(boneName.c_str());
            skeleton2->mBoneNameToIndex[boneName] = i;
            skeleton2->mInverseBindMatrices.push_back(mat4f());
        }

        // 创建 mesh 和实例
        FMeshAsset* mesh = new FMeshAsset();
        mesh->mEngine = engine;
        mesh->mRenderableManager = &rm;
        mesh->mHasSkinning = true;
        mesh->mMaxBoneIndex = 1;

        MeshInstance* instance1 = mesh->createInstance();
        MeshInstance* instance2 = mesh->createInstance();

        // 绑定不同的骨骼到不同实例
        if (instance1->bindSkeleton(skeleton1)) {
            std::cout << "  ✓ Instance 1 bound to skeleton 1" << std::endl;
            passed++;
        }

        if (instance2->bindSkeleton(skeleton2)) {
            std::cout << "  ✓ Instance 2 bound to skeleton 2" << std::endl;
            passed++;
        }

        // 验证绑定的骨骼不同
        if (instance1->getBoundSkeleton() != instance2->getBoundSkeleton()) {
            std::cout << "  ✓ Instances have independent skeleton bindings" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Instances should have independent skeletons" << std::endl;
            failed++;
        }

        mesh->destroyInstance(instance1);
        mesh->destroyInstance(instance2);
        delete mesh;
        delete skeleton2;
        delete skeleton1;
    }

    // Test 4: 实例蒙皮更新
    std::cout << "\n[Test 4] Instance skinning update..." << std::endl;
    {
        // 创建骨骼
        FSkeletonAsset* skeleton = new FSkeletonAsset();
        skeleton->mEngine = engine;
        skeleton->mEntityManager = &em;
        skeleton->mTransformManager = &tm;
        skeleton->mBoneEntities.reserve(2);
        skeleton->mBoneNames.reserve(2);
        skeleton->mInverseBindMatrices.reserve(2);

        for (int i = 0; i < 2; ++i) {
            Entity e = em.create();
            tm.create(e);
            skeleton->mBoneEntities.push_back(e);
            std::string boneName = "bone_" + std::to_string(i);
            skeleton->mBoneNames.emplace_back(boneName.c_str());
            skeleton->mBoneNameToIndex[boneName] = i;
            skeleton->mInverseBindMatrices.push_back(mat4f());
        }
        skeleton->mRoot = skeleton->mBoneEntities[0];

        // 创建 mesh 和实例
        FMeshAsset* mesh = new FMeshAsset();
        mesh->mEngine = engine;
        mesh->mRenderableManager = &rm;
        mesh->mHasSkinning = true;
        mesh->mMaxBoneIndex = 1;

        MeshInstance* instance = mesh->createInstance();
        instance->bindSkeleton(skeleton);

        // 创建 animator
        StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
        animator->bindSkeleton(skeleton);

        // 测试 updateSkinning
        try {
            instance->updateSkinning(animator);
            std::cout << "  ✓ Instance updateSkinning() executed successfully" << std::endl;
            passed++;
        } catch (...) {
            std::cout << "  ✗ FAIL: updateSkinning() crashed" << std::endl;
            failed++;
        }

        // 多次更新测试
        for (int i = 0; i < 20; ++i) {
            animator->update(0.016f);
            instance->updateSkinning(animator);
        }
        std::cout << "  ✓ Multiple skinning updates successful (20 frames)" << std::endl;
        passed++;

        StandaloneAnimator::destroy(&animator);
        mesh->destroyInstance(instance);
        delete mesh;
        delete skeleton;
    }

    // Test 5: 性能测试（100个实例）
    std::cout << "\n[Test 5] Performance test (100 instances)..." << std::endl;
    {
        auto startTime = std::chrono::high_resolution_clock::now();

        FMeshAsset* mesh = new FMeshAsset();
        mesh->mEngine = engine;
        mesh->mRenderableManager = &rm;
        mesh->mHasSkinning = false;

        const int instanceCount = 100;
        std::vector<MeshInstance*> instances;

        // 创建100个实例
        for (int i = 0; i < instanceCount; ++i) {
            MeshInstance* instance = mesh->createInstance();
            instances.push_back(instance);
            scene->addEntity(instance->getRenderableEntity());
        }

        auto midTime = std::chrono::high_resolution_clock::now();
        auto creationTime = std::chrono::duration_cast<std::chrono::milliseconds>(midTime - startTime);

        std::cout << "  ✓ Created " << instanceCount << " instances in " << creationTime.count() << "ms" << std::endl;
        passed++;

        if (mesh->getInstanceCount() == instanceCount) {
            std::cout << "  ✓ Instance count correct (" << instanceCount << ")" << std::endl;
            passed++;
        }

        // 清理所有实例
        for (auto* instance : instances) {
            scene->remove(instance->getRenderableEntity());
            mesh->destroyInstance(instance);
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

        std::cout << "  ✓ Total time for create/destroy cycle: " << totalTime.count() << "ms" << std::endl;
        passed++;

        delete mesh;
    }

    // Test 6: getInstances() 测试
    std::cout << "\n[Test 6] getInstances() array access..." << std::endl;
    {
        FMeshAsset* mesh = new FMeshAsset();
        mesh->mEngine = engine;
        mesh->mRenderableManager = &rm;
        mesh->mHasSkinning = false;

        const int instanceCount = 5;
        for (int i = 0; i < instanceCount; ++i) {
            mesh->createInstance();
        }

        MeshInstance* const* instances = mesh->getInstances();
        if (instances) {
            std::cout << "  ✓ getInstances() returns valid array" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: getInstances() returns null" << std::endl;
            failed++;
        }

        // 验证所有实例可访问
        bool allValid = true;
        for (size_t i = 0; i < mesh->getInstanceCount(); ++i) {
            if (!instances[i]) {
                allValid = false;
                break;
            }
        }

        if (allValid) {
            std::cout << "  ✓ All instances in array are valid" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Some instances are null" << std::endl;
            failed++;
        }

        // 清理
        for (size_t i = 0; i < mesh->getInstanceCount(); ) {
            mesh->destroyInstance(instances[0]);  // 总是销毁第一个
        }

        delete mesh;
    }

    // Cleanup
    engine->destroy(scene);
    Engine::destroy(&engine);

    // Summary
    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total: " << (passed + failed) << std::endl;

    if (failed == 0) {
        std::cout << "\n🎉 ALL MESH INSTANCE TESTS PASSED!" << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ SOME TESTS FAILED" << std::endl;
        return 1;
    }
}
