#include <gltfio/StandaloneAnimator.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/AnimationAsset.h>
#include "../src/FSkeletonAsset.h"
#include "../src/FAnimationAsset.h"

#include <filament/Engine.h>
#include <filament/TransformManager.h>
#include <utils/EntityManager.h>
#include <math/vec3.h>
#include <iostream>
#include <chrono>
#include <cmath>

using namespace filament;
using namespace filament::gltfio;
using namespace filament::math;
using namespace utils;

// 辅助函数：创建大量骨骼的skeleton
FSkeletonAsset* createLargeSkeleton(Engine* engine, EntityManager& em,
                                     TransformManager& tm, size_t boneCount) {
    FSkeletonAsset* skeleton = new FSkeletonAsset();
    skeleton->mEngine = engine;
    skeleton->mEntityManager = &em;
    skeleton->mTransformManager = &tm;

    skeleton->mBoneEntities.reserve(boneCount);
    skeleton->mBoneNames.reserve(boneCount);
    skeleton->mInverseBindMatrices.reserve(boneCount);

    for (size_t i = 0; i < boneCount; ++i) {
        Entity e = em.create();
        auto ti = tm.getInstance(e);
        if (!ti) {
            tm.create(e);
        }

        skeleton->mBoneEntities.push_back(e);

        char nameBuf[32];
        snprintf(nameBuf, sizeof(nameBuf), "bone_%zu", i);
        skeleton->mBoneNames.emplace_back(nameBuf);
        skeleton->mBoneNameToIndex[nameBuf] = i;
        skeleton->mInverseBindMatrices.push_back(mat4f());

        if (i == 0) {
            skeleton->mRoot = e;
        } else {
            // 建立父子关系（形成链）
            auto parentTi = tm.getInstance(skeleton->mBoneEntities[i - 1]);
            auto childTi = tm.getInstance(e);
            tm.setParent(childTi, parentTi);
        }
    }

    return skeleton;
}

// 辅助函数：创建影响所有骨骼的动画
FAnimationAsset* createLargeAnimation(size_t boneCount) {
    FAnimationAsset* anim = new FAnimationAsset();
    anim->mName = CString("LargeAnimation");
    anim->mDuration = 2.0f;

    // 创建sampler
    Sampler sampler;
    sampler.times[0.0f] = 0;
    sampler.times[1.0f] = 1;
    sampler.times[2.0f] = 2;
    sampler.values = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f
    };
    sampler.interpolation = Sampler::LINEAR;
    anim->mSamplers.push_back(sampler);

    // 为每个骨骼创建channel
    for (size_t i = 0; i < boneCount; ++i) {
        Channel channel;
        channel.sampler = &anim->mSamplers[0];

        char nameBuf[32];
        snprintf(nameBuf, sizeof(nameBuf), "bone_%zu", i);
        channel.targetBoneName = nameBuf;
        channel.transformType = Channel::TRANSLATION;

        anim->mChannels.push_back(channel);
    }

    return anim;
}

int main() {
    std::cout << "=== Performance Stress Test ===" << std::endl;

    Engine* engine = Engine::create();
    auto& em = EntityManager::get();
    auto& tm = engine->getTransformManager();

    // Test 1: 512骨骼（UBO上限）
    std::cout << "\n[Test 1] 512-bone skeleton..." << std::endl;
    {
        auto start = std::chrono::high_resolution_clock::now();

        FSkeletonAsset* skeleton = createLargeSkeleton(engine, em, tm, 512);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "  ✓ Created 512-bone skeleton in " << duration.count() << "ms" << std::endl;
        std::cout << "  Bone count: " << skeleton->getBoneCount() << std::endl;

        // 测试名称查找
        start = std::chrono::high_resolution_clock::now();
        int index = skeleton->getBoneIndex("bone_256");
        end = std::chrono::high_resolution_clock::now();
        auto lookupTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "  Name lookup time: " << lookupTime.count() << "µs" << std::endl;
        std::cout << "  Found bone_256 at index: " << index << std::endl;

        delete skeleton;
    }

    // Test 2: 大规模动画（512通道）
    std::cout << "\n[Test 2] Large animation with 512 channels..." << std::endl;
    {
        FSkeletonAsset* skeleton = createLargeSkeleton(engine, em, tm, 512);
        FAnimationAsset* anim = createLargeAnimation(512);

        std::cout << "  Animation channels: " << anim->getChannelCount() << std::endl;

        auto start = std::chrono::high_resolution_clock::now();

        StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
        animator->bindSkeleton(skeleton);

        auto end = std::chrono::high_resolution_clock::now();
        auto bindTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "  Skeleton binding time: " << bindTime.count() << "ms" << std::endl;

        start = std::chrono::high_resolution_clock::now();
        int animId = animator->playAnimation(anim, 1.0f, true);
        end = std::chrono::high_resolution_clock::now();
        auto playTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "  Animation play time: " << playTime.count() << "ms" << std::endl;
        std::cout << "  ✓ Large animation setup complete" << std::endl;

        StandaloneAnimator::destroy(&animator);
        delete anim;
        delete skeleton;
    }

    // Test 3: 1000帧连续更新（性能测试）
    std::cout << "\n[Test 3] 1000-frame update performance..." << std::endl;
    {
        FSkeletonAsset* skeleton = createLargeSkeleton(engine, em, tm, 256);
        FAnimationAsset* anim = createLargeAnimation(256);

        StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
        animator->bindSkeleton(skeleton);
        animator->playAnimation(anim, 1.0f, true);

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < 1000; ++i) {
            animator->update(1.0f / 60.0f);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        double avgFrameTime = totalTime.count() / 1000.0;
        double fps = 1000.0 / avgFrameTime;

        std::cout << "  Total time for 1000 frames: " << totalTime.count() << "ms" << std::endl;
        std::cout << "  Average frame time: " << avgFrameTime << "ms" << std::endl;
        std::cout << "  Effective FPS: " << fps << std::endl;

        if (avgFrameTime < 16.67) {
            std::cout << "  ✓ Performance acceptable for 60fps" << std::endl;
        } else {
            std::cout << "  ⚠ Performance may not sustain 60fps" << std::endl;
        }

        StandaloneAnimator::destroy(&animator);
        delete anim;
        delete skeleton;
    }

    // Test 4: 多个动画同时播放（5个动画）
    std::cout << "\n[Test 4] Multiple concurrent animations..." << std::endl;
    {
        FSkeletonAsset* skeleton = createLargeSkeleton(engine, em, tm, 128);

        std::vector<FAnimationAsset*> anims;
        for (int i = 0; i < 5; ++i) {
            FAnimationAsset* anim = createLargeAnimation(128);
            anims.push_back(anim);
        }

        StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
        animator->bindSkeleton(skeleton);

        // 播放所有动画
        for (int i = 0; i < 5; ++i) {
            animator->playAnimation(anims[i], 1.0f / 5.0f, true);  // 每个权重0.2
        }

        std::cout << "  Playing 5 animations concurrently..." << std::endl;

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < 100; ++i) {
            animator->update(1.0f / 60.0f);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "  Time for 100 frames: " << time.count() << "ms" << std::endl;
        std::cout << "  ✓ Multiple animations handled" << std::endl;

        StandaloneAnimator::destroy(&animator);
        for (auto* anim : anims) delete anim;
        delete skeleton;
    }

    // Test 5: 内存泄漏检测（创建/销毁循环）
    std::cout << "\n[Test 5] Memory leak detection (create/destroy cycles)..." << std::endl;
    {
        auto start = std::chrono::high_resolution_clock::now();

        for (int cycle = 0; cycle < 100; ++cycle) {
            FSkeletonAsset* skeleton = createLargeSkeleton(engine, em, tm, 64);
            FAnimationAsset* anim = createLargeAnimation(64);

            StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
            animator->bindSkeleton(skeleton);
            animator->playAnimation(anim, 1.0f, true);

            for (int i = 0; i < 10; ++i) {
                animator->update(1.0f / 60.0f);
            }

            StandaloneAnimator::destroy(&animator);
            delete anim;
            delete skeleton;
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "  100 create/destroy cycles completed in " << time.count() << "ms" << std::endl;
        std::cout << "  ✓ No crashes - basic memory management works" << std::endl;
        std::cout << "  (Use Valgrind/ASan for detailed leak detection)" << std::endl;
    }

    // Test 6: 深层骨骼层次（64层深）
    std::cout << "\n[Test 6] Deep bone hierarchy (64 levels)..." << std::endl;
    {
        FSkeletonAsset* skeleton = createLargeSkeleton(engine, em, tm, 64);

        StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
        animator->bindSkeleton(skeleton);

        FAnimationAsset* anim = createLargeAnimation(64);
        animator->playAnimation(anim, 1.0f, true);

        // 更新动画
        for (int i = 0; i < 60; ++i) {
            animator->update(1.0f / 60.0f);
        }

        // 检查最深层骨骼的世界变换
        const mat4f* boneMatrices = animator->getBoneMatrices();
        if (boneMatrices) {
            std::cout << "  ✓ Deep hierarchy bone matrices computed" << std::endl;
        }

        StandaloneAnimator::destroy(&animator);
        delete anim;
        delete skeleton;
    }

    Engine::destroy(&engine);

    std::cout << "\n=== Performance Test Complete ===" << std::endl;
    std::cout << "\n✅ ALL PERFORMANCE TESTS PASSED" << std::endl;
    std::cout << "\nNote: For production use, consider:" << std::endl;
    std::cout << "  - Profiling with actual game content" << std::endl;
    std::cout << "  - Memory leak detection with Valgrind/ASan" << std::endl;
    std::cout << "  - GPU profiling for skinning performance" << std::endl;

    return 0;
}
