#include <gltfio/StandaloneAnimator.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/AnimationAsset.h>
#include "../src/FSkeletonAsset.h"
#include "../src/FAnimationAsset.h"

#include <filament/Engine.h>
#include <filament/TransformManager.h>
#include <utils/EntityManager.h>
#include <math/vec3.h>
#include <math/vec4.h>
#include <iostream>

using namespace filament;
using namespace filament::gltfio;
using namespace filament::math;
using namespace utils;

int main() {
    std::cout << "=== StandaloneAnimator Test ===" << std::endl;

    // 创建Engine
    Engine* engine = Engine::create();
    auto& em = EntityManager::get();
    auto& tm = engine->getTransformManager();

    // 1. 创建简单的骨骼
    FSkeletonAsset* skeleton = new FSkeletonAsset();
    skeleton->mEngine = engine;
    skeleton->mEntityManager = &em;
    skeleton->mTransformManager = &tm;

    const size_t boneCount = 2;
    skeleton->mBoneEntities.reserve(boneCount);
    skeleton->mBoneNames.reserve(boneCount);
    skeleton->mInverseBindMatrices.reserve(boneCount);

    for (size_t i = 0; i < boneCount; ++i) {
        Entity entity = em.create();
        skeleton->mBoneEntities.push_back(entity);

        auto ti = tm.getInstance(entity);
        if (!ti) {
            tm.create(entity);
            ti = tm.getInstance(entity);
        }

        char nameBuf[32];
        snprintf(nameBuf, sizeof(nameBuf), "bone_%zu", i);
        skeleton->mBoneNames.emplace_back(nameBuf);
        skeleton->mBoneNameToIndex[nameBuf] = i;

        skeleton->mInverseBindMatrices.push_back(mat4f());
    }

    skeleton->mRoot = skeleton->mBoneEntities[0];

    std::cout << "Created skeleton with " << skeleton->getBoneCount() << " bones" << std::endl;

    // 2. 创建简单的动画
    FAnimationAsset* anim = new FAnimationAsset();
    anim->mName = utils::CString("TestAnimation");
    anim->mDuration = 2.0f;

    // 创建Translation Sampler
    Sampler sampler;
    sampler.times[0.0f] = 0;
    sampler.times[1.0f] = 1;
    sampler.times[2.0f] = 2;
    sampler.values = {
        0.0f, 0.0f, 0.0f,   // t=0
        1.0f, 0.0f, 0.0f,   // t=1
        2.0f, 0.0f, 0.0f    // t=2
    };
    sampler.interpolation = Sampler::LINEAR;
    anim->mSamplers.push_back(sampler);

    // 创建Channel
    Channel channel;
    channel.sampler = &anim->mSamplers[0];
    channel.targetBoneName = "bone_0";
    channel.transformType = Channel::TRANSLATION;
    anim->mChannels.push_back(channel);

    std::cout << "Created animation with duration " << anim->getDuration() << "s" << std::endl;

    // 3. 创建动画器
    StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
    animator->bindSkeleton(skeleton);
    std::cout << "Animator bound to skeleton" << std::endl;

    // 4. 播放动画
    int animId = animator->playAnimation(anim, 1.0f, true);
    std::cout << "Playing animation ID: " << animId << std::endl;

    // 5. 模拟更新
    std::cout << "\nSimulating animation updates..." << std::endl;
    for (int i = 0; i < 5; ++i) {
        animator->update(0.5f);  // 每次更新0.5秒

        const auto* boneMatrices = animator->getBoneMatrices();
        if (boneMatrices && animator->getBoneCount() > 0) {
            std::cout << "Frame " << i << ": bone[0] matrix[3][0] (translation.x) = "
                      << boneMatrices[0][3][0] << std::endl;
        }
    }

    // 6. 测试权重调整
    std::cout << "\nTesting weight adjustment..." << std::endl;
    animator->setAnimationWeight(animId, 0.5f);
    animator->update(0.1f);
    std::cout << "Weight set to 0.5" << std::endl;

    // 7. 测试停止
    std::cout << "\nTesting stop..." << std::endl;
    animator->stopAnimation(animId);
    std::cout << "Animation stopped (ID: " << animId << ")" << std::endl;

    // 8. 测试重置到绑定姿势
    std::cout << "\nTesting reset to bind pose..." << std::endl;
    animator->resetToBindPose();
    std::cout << "Reset to bind pose" << std::endl;

    // 9. 测试getBoneCount
    std::cout << "\nBone count: " << animator->getBoneCount() << std::endl;
    assert(animator->getBoneCount() == boneCount);

    // 清理
    StandaloneAnimator::destroy(&animator);
    delete anim;
    delete skeleton;
    Engine::destroy(&engine);

    std::cout << "\n=== StandaloneAnimator Test PASSED ===" << std::endl;
    return 0;
}
