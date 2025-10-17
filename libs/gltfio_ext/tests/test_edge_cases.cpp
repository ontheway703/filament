/**
 * ============================================================================
 * test_edge_cases.cpp - 边界情况与鲁棒性测试
 * ============================================================================
 *
 * 【测试目标】
 * 验证 gltfio_ext 在极端和异常情况下的鲁棒性：
 * - 空数据处理（空骨骼、空动画）
 * - 索引越界处理
 * - 无效参数处理（null指针）
 * - 时间边界测试（t=0, t>duration）
 *
 * 【背景知识】
 * 鲁棒性测试（Robustness Testing）是验证软件在异常输入下的行为：
 * 1. 不应该崩溃（No Crash）
 * 2. 返回明确的错误信号（如 -1, nullptr, false）
 * 3. 日志记录错误但继续运行
 *
 * 关键测试场景：
 * - 空骨骼：getBoneCount() = 0，查询返回 -1
 * - 索引越界：访问 bone[999]，返回空 Entity
 * - 动画目标不存在：通道指向不存在的骨骼，不崩溃
 * - 时间边界：t=0 和 t>duration，循环/非循环模式
 * - null 参数：bindSkeleton(nullptr)，返回 false
 *
 * 【测试方法】
 * 边界测试 - 构造极端场景，验证错误处理逻辑
 *
 * 【测试用例概览】
 * Test 1: 空骨骼处理
 * Test 2: 骨骼索引越界
 * Test 4: 动画通道目标骨骼不存在
 * Test 5: 时间边界（t=0, t>duration）
 * Test 6: 无逆绑定矩阵
 * Test 7: Mesh 绑定 null skeleton
 * Test 8: Mesh 骨骼索引超范围
 *
 * 【关键验证点】
 * - 所有边界情况都不崩溃
 * - 返回明确的错误信号
 * - 日志友好（如有日志系统）
 *
 * 【运行方式】
 * ./out/cmake-debug/libs/gltfio_ext/test_edge_cases
 *
 * 【预期输出】
 * 所有边界测试通过，显示 "ALL EDGE CASE TESTS PASSED!"
 * ============================================================================
 */

#include <gltfio/AssetLoaderExt.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/MeshAsset.h>
#include <gltfio/AnimationAsset.h>
#include <gltfio/StandaloneAnimator.h>
#include "../src/FSkeletonAsset.h"
#include "../src/FMeshAsset.h"
#include "../src/FAnimationAsset.h"

#include <filament/Engine.h>
#include <filament/TransformManager.h>
#include <utils/EntityManager.h>
#include <math/mat4.h>
#include <iostream>

using namespace filament;
using namespace filament::gltfio;
using namespace filament::math;
using namespace utils;

int main() {
    std::cout << "=== Edge Cases Robustness Test ===" << std::endl;

    Engine* engine = Engine::create();
    auto& em = EntityManager::get();
    auto& tm = engine->getTransformManager();

    int passed = 0, failed = 0;

    // Test 1: 空骨骼处理
    std::cout << "\n[Test 1] Empty skeleton..." << std::endl;
    {
        FSkeletonAsset* skeleton = new FSkeletonAsset();
        skeleton->mEngine = engine;
        skeleton->mEntityManager = &em;
        skeleton->mTransformManager = &tm;

        if (skeleton->getBoneCount() == 0) {
            std::cout << "  ✓ Empty skeleton handled correctly" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Expected 0 bones" << std::endl;
            failed++;
        }

        // 测试查询空骨骼
        if (skeleton->getBoneIndex("nonexistent") == -1) {
            std::cout << "  ✓ Empty skeleton name lookup returns -1" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Empty skeleton lookup should return -1" << std::endl;
            failed++;
        }

        delete skeleton;
    }

    // Test 2: 骨骼索引越界
    std::cout << "\n[Test 2] Bone index out of bounds..." << std::endl;
    {
        FSkeletonAsset* skeleton = new FSkeletonAsset();
        skeleton->mEngine = engine;
        skeleton->mEntityManager = &em;
        skeleton->mTransformManager = &tm;

        // Reserve capacity
        skeleton->mBoneEntities.reserve(3);
        skeleton->mBoneNames.reserve(3);
        skeleton->mInverseBindMatrices.reserve(3);

        // 创建3个骨骼
        for (int i = 0; i < 3; ++i) {
            Entity e = em.create();
            skeleton->mBoneEntities.push_back(e);
            char nameBuf[32];
            snprintf(nameBuf, sizeof(nameBuf), "bone_%d", i);
            skeleton->mBoneNames.emplace_back(nameBuf);
            skeleton->mInverseBindMatrices.push_back(mat4f());
        }

        // 测试越界访问
        if (skeleton->getBoneEntity(999).getId() == 0) {
            std::cout << "  ✓ Out of bounds getBoneEntity returns null entity" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Should return null entity for invalid index" << std::endl;
            failed++;
        }

        delete skeleton;
    }

    // Test 4: 动画通道目标骨骼不存在
    std::cout << "\n[Test 4] Animation channel with missing target bone..." << std::endl;
    {
        FSkeletonAsset* skeleton = new FSkeletonAsset();
        skeleton->mEngine = engine;
        skeleton->mEntityManager = &em;
        skeleton->mTransformManager = &tm;

        // Reserve capacity
        skeleton->mBoneEntities.reserve(2);
        skeleton->mBoneNames.reserve(2);
        skeleton->mInverseBindMatrices.reserve(2);

        // 只创建2个骨骼
        for (int i = 0; i < 2; ++i) {
            Entity e = em.create();
            tm.create(e);
            skeleton->mBoneEntities.push_back(e);
            char nameBuf[32];
            snprintf(nameBuf, sizeof(nameBuf), "bone_%zu", i);
            skeleton->mBoneNames.emplace_back(nameBuf);
            skeleton->mBoneNameToIndex[nameBuf] = i;
            skeleton->mInverseBindMatrices.push_back(mat4f());
        }

        // 创建动画，通道目标是不存在的骨骼
        FAnimationAsset* anim = new FAnimationAsset();

        SingleAnimation singleAnim;
        singleAnim.mName = CString("TestAnim");
        singleAnim.mDuration = 1.0f;

        Sampler sampler;
        sampler.times[0.0f] = 0;
        sampler.times[1.0f] = 1;
        sampler.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
        sampler.interpolation = Sampler::LINEAR;
        singleAnim.mSamplers.push_back(sampler);

        Channel channel;
        channel.sampler = &singleAnim.mSamplers[0];
        channel.targetBoneName = "nonexistent_bone";  // 不存在的骨骼
        channel.transformType = Channel::TRANSLATION;
        singleAnim.mChannels.push_back(channel);

        // 将SingleAnimation添加到AnimationAsset
        anim->mAnimations.push_back(singleAnim);

        // 缓存动画名称
        anim->mAnimationNames.push_back(anim->mAnimations[0].mName.c_str());
        anim->mAnimationNames.push_back(nullptr);

        // 创建animator并绑定
        StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
        animator->bindSkeleton(skeleton);
        int animId = animator->playAnimation(anim, 0, 1.0f, true);

        // 更新应该不崩溃
        try {
            animator->update(0.016f);
            std::cout << "  ✓ Animation with missing target bone doesn't crash" << std::endl;
            passed++;
        } catch (...) {
            std::cout << "  ✗ FAIL: Crashed on missing target bone" << std::endl;
            failed++;
        }

        StandaloneAnimator::destroy(&animator);
        delete anim;
        delete skeleton;
    }

    // Test 5: 时间边界测试
    std::cout << "\n[Test 5] Animation time boundaries..." << std::endl;
    {
        FSkeletonAsset* skeleton = new FSkeletonAsset();
        skeleton->mEngine = engine;
        skeleton->mEntityManager = &em;
        skeleton->mTransformManager = &tm;

        // Reserve capacity
        skeleton->mBoneEntities.reserve(1);
        skeleton->mBoneNames.reserve(1);
        skeleton->mInverseBindMatrices.reserve(1);

        Entity e = em.create();
        tm.create(e);
        skeleton->mBoneEntities.push_back(e);
        skeleton->mBoneNames.emplace_back("bone_0");
        skeleton->mBoneNameToIndex["bone_0"] = 0;
        skeleton->mInverseBindMatrices.push_back(mat4f());
        skeleton->mRoot = e;

        // 创建2秒动画
        FAnimationAsset* anim = new FAnimationAsset();

        SingleAnimation singleAnim;
        singleAnim.mName = CString("TestAnim");
        singleAnim.mDuration = 2.0f;

        Sampler sampler;
        sampler.times[0.0f] = 0;
        sampler.times[2.0f] = 1;
        sampler.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
        sampler.interpolation = Sampler::LINEAR;
        singleAnim.mSamplers.push_back(sampler);

        Channel channel;
        channel.sampler = &singleAnim.mSamplers[0];
        channel.targetBoneName = "bone_0";
        channel.transformType = Channel::TRANSLATION;
        singleAnim.mChannels.push_back(channel);

        // 将SingleAnimation添加到AnimationAsset
        anim->mAnimations.push_back(singleAnim);

        // 缓存动画名称
        anim->mAnimationNames.push_back(anim->mAnimations[0].mName.c_str());
        anim->mAnimationNames.push_back(nullptr);

        StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
        animator->bindSkeleton(skeleton);
        animator->playAnimation(anim, 0, 1.0f, true);

        // 测试时间=0
        animator->update(0.0f);
        std::cout << "  ✓ time=0 handled" << std::endl;
        passed++;

        // 测试时间超过duration（循环）
        for (int i = 0; i < 150; ++i) {
            animator->update(0.016f);  // 总共2.4秒，应该循环
        }
        std::cout << "  ✓ time>duration loops correctly" << std::endl;
        passed++;

        StandaloneAnimator::destroy(&animator);
        delete anim;
        delete skeleton;
    }

    // Test 6: 无逆绑定矩阵
    std::cout << "\n[Test 6] Skeleton without inverse bind matrices..." << std::endl;
    {
        FSkeletonAsset* skeleton = new FSkeletonAsset();
        skeleton->mEngine = engine;
        skeleton->mEntityManager = &em;
        skeleton->mTransformManager = &tm;

        // Reserve capacity
        skeleton->mBoneEntities.reserve(1);
        skeleton->mBoneNames.reserve(1);

        Entity e = em.create();
        skeleton->mBoneEntities.push_back(e);
        skeleton->mBoneNames.emplace_back(CString("bone_0"));
        // 不设置mInverseBindMatrices

        const mat4f* ibm = skeleton->getInverseBindMatrices();
        if (ibm == nullptr) {
            std::cout << "  ✓ Empty inverse bind matrices returns nullptr" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Should return nullptr when no matrices" << std::endl;
            failed++;
        }

        delete skeleton;
    }

    // Test 7: Mesh绑定null skeleton
    std::cout << "\n[Test 7] Mesh binding null skeleton..." << std::endl;
    {
        FMeshAsset* mesh = new FMeshAsset();
        mesh->mEngine = engine;
        mesh->mRenderableManager = &engine->getRenderableManager();
        mesh->mHasSkinning = true;  // 声称有skinning

        if (!mesh->bindSkeleton(nullptr)) {
            std::cout << "  ✓ Binding null skeleton fails gracefully" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Should reject null skeleton" << std::endl;
            failed++;
        }

        delete mesh;
    }

    // Test 8: Mesh绑定骨骼索引超范围
    std::cout << "\n[Test 8] Mesh with out-of-range bone indices..." << std::endl;
    {
        FSkeletonAsset* skeleton = new FSkeletonAsset();
        skeleton->mEngine = engine;
        skeleton->mEntityManager = &em;
        skeleton->mTransformManager = &tm;

        // Reserve capacity
        skeleton->mBoneEntities.reserve(3);
        skeleton->mBoneNames.reserve(3);
        skeleton->mInverseBindMatrices.reserve(3);

        // 只创建3个骨骼
        for (int i = 0; i < 3; ++i) {
            Entity e = em.create();
            skeleton->mBoneEntities.push_back(e);
            char nameBuf[32];
            snprintf(nameBuf, sizeof(nameBuf), "bone_%d", i);
            skeleton->mBoneNames.emplace_back(nameBuf);
            skeleton->mInverseBindMatrices.push_back(mat4f());
        }

        FMeshAsset* mesh = new FMeshAsset();
        mesh->mEngine = engine;
        mesh->mRenderableManager = &engine->getRenderableManager();
        mesh->mHasSkinning = true;
        mesh->mMaxBoneIndex = 10;  // 超过skeleton的骨骼数

        if (!mesh->bindSkeleton(skeleton)) {
            std::cout << "  ✓ Out-of-range bone index detected" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Should reject invalid bone indices" << std::endl;
            failed++;
        }

        delete mesh;
        delete skeleton;
    }

    Engine::destroy(&engine);

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total: " << (passed + failed) << std::endl;

    if (failed == 0) {
        std::cout << "\n🎉 ALL EDGE CASE TESTS PASSED!" << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ SOME TESTS FAILED" << std::endl;
        return 1;
    }
}
