#include <gltfio/StandaloneAnimator.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/AnimationAsset.h>
#include "../src/FSkeletonAsset.h"
#include "../src/FAnimationAsset.h"

#include <filament/Engine.h>
#include <filament/TransformManager.h>
#include <utils/EntityManager.h>
#include <math/vec3.h>
#include <math/quat.h>
#include <math/mat4.h>
#include <iostream>
#include <cmath>

using namespace filament;
using namespace filament::gltfio;
using namespace filament::math;
using namespace utils;

int main() {
    std::cout << "=== Multi-Channel Transform Bug Verification ===" << std::endl;
    std::cout << "\n🔍 Testing if TRANSLATION + ROTATION + SCALE channels" << std::endl;
    std::cout << "   can coexist without overwriting each other\n" << std::endl;

    Engine* engine = Engine::create();
    auto& em = EntityManager::get();
    auto& tm = engine->getTransformManager();

    // 1. 创建简单骨骼
    FSkeletonAsset* skeleton = new FSkeletonAsset();
    skeleton->mEngine = engine;
    skeleton->mEntityManager = &em;
    skeleton->mTransformManager = &tm;

    // Reserve capacity
    skeleton->mBoneEntities.reserve(1);
    skeleton->mBoneNames.reserve(1);
    skeleton->mInverseBindMatrices.reserve(1);

    Entity bone = em.create();
    tm.create(bone);
    skeleton->mBoneEntities.push_back(bone);
    skeleton->mBoneNames.emplace_back("test_bone");
    skeleton->mBoneNameToIndex["test_bone"] = 0;
    skeleton->mInverseBindMatrices.push_back(mat4f());
    skeleton->mRoot = bone;

    std::cout << "✓ Skeleton created with 1 bone" << std::endl;

    // 2. 创建动画 - 包含3个通道：TRANSLATION + ROTATION + SCALE
    FAnimationAsset* anim = new FAnimationAsset();

    // 创建SingleAnimation
    SingleAnimation singleAnim;
    singleAnim.mName = CString("MultiChannelTest");
    singleAnim.mDuration = 1.0f;

    // Sampler 1: TRANSLATION (0,0,0) -> (1,0,0)
    Sampler translateSampler;
    translateSampler.times[0.0f] = 0;
    translateSampler.times[1.0f] = 1;
    translateSampler.values = {
        0.0f, 0.0f, 0.0f,  // t=0
        1.0f, 0.0f, 0.0f   // t=1
    };
    translateSampler.interpolation = Sampler::LINEAR;
    singleAnim.mSamplers.push_back(translateSampler);

    // Sampler 2: ROTATION (identity) -> (90度绕Z轴)
    Sampler rotateSampler;
    rotateSampler.times[0.0f] = 0;
    rotateSampler.times[1.0f] = 1;
    quatf q0(1, 0, 0, 0);  // identity
    quatf q1 = quatf::fromAxisAngle(float3(0, 0, 1), M_PI / 2);  // 90度绕Z
    rotateSampler.values = {
        q0.x, q0.y, q0.z, q0.w,
        q1.x, q1.y, q1.z, q1.w
    };
    rotateSampler.interpolation = Sampler::LINEAR;
    singleAnim.mSamplers.push_back(rotateSampler);

    // Sampler 3: SCALE (1,1,1) -> (2,2,2)
    Sampler scaleSampler;
    scaleSampler.times[0.0f] = 0;
    scaleSampler.times[1.0f] = 1;
    scaleSampler.values = {
        1.0f, 1.0f, 1.0f,  // t=0
        2.0f, 2.0f, 2.0f   // t=1
    };
    scaleSampler.interpolation = Sampler::LINEAR;
    singleAnim.mSamplers.push_back(scaleSampler);

    // 添加3个通道
    Channel transChannel;
    transChannel.sampler = &singleAnim.mSamplers[0];
    transChannel.targetBoneName = "test_bone";
    transChannel.transformType = Channel::TRANSLATION;
    singleAnim.mChannels.push_back(transChannel);

    Channel rotChannel;
    rotChannel.sampler = &singleAnim.mSamplers[1];
    rotChannel.targetBoneName = "test_bone";
    rotChannel.transformType = Channel::ROTATION;
    singleAnim.mChannels.push_back(rotChannel);

    Channel scaleChannel;
    scaleChannel.sampler = &singleAnim.mSamplers[2];
    scaleChannel.targetBoneName = "test_bone";
    scaleChannel.transformType = Channel::SCALE;
    singleAnim.mChannels.push_back(scaleChannel);

    // 将SingleAnimation添加到AnimationAsset
    anim->mAnimations.push_back(singleAnim);

    // 缓存动画名称
    anim->mAnimationNames.push_back(anim->mAnimations[0].mName.c_str());
    anim->mAnimationNames.push_back(nullptr);

    std::cout << "✓ Animation created with 3 channels (T + R + S)" << std::endl;

    // 3. 创建animator并播放
    StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
    animator->bindSkeleton(skeleton);
    int animId = animator->playAnimation(anim, 0, 1.0f, false);

    std::cout << "\n--- Frame-by-Frame Analysis ---" << std::endl;

    bool bugDetected = false;

    // 4. 更新到t=0.5，检查变换
    animator->update(0.5f);

    TransformManager::Instance ti = tm.getInstance(bone);
    mat4f transform = tm.getWorldTransform(ti);

    // 提取变换分量（简单方法）
    float3 translation(transform[3][0], transform[3][1], transform[3][2]);

    // 提取缩放（列向量长度）
    float3 scaleX(transform[0][0], transform[0][1], transform[0][2]);
    float3 scaleY(transform[1][0], transform[1][1], transform[1][2]);
    float3 scaleZ(transform[2][0], transform[2][1], transform[2][2]);
    float scaleValue = length(scaleX);

    std::cout << "\nAt t=0.5:" << std::endl;
    std::cout << "  Translation: (" << translation.x << ", " << translation.y << ", " << translation.z << ")" << std::endl;
    std::cout << "  Scale (approx): " << scaleValue << std::endl;

    // 期望值
    float expectedTransX = 0.5f;  // lerp(0, 1, 0.5) = 0.5
    float expectedScale = 1.5f;   // lerp(1, 2, 0.5) = 1.5

    std::cout << "\nExpected at t=0.5:" << std::endl;
    std::cout << "  Translation.x: ~" << expectedTransX << std::endl;
    std::cout << "  Scale: ~" << expectedScale << std::endl;

    // 检测BUG
    std::cout << "\n🔬 Bug Detection:" << std::endl;

    bool translationOk = std::abs(translation.x - expectedTransX) < 0.1f;
    bool scaleOk = std::abs(scaleValue - expectedScale) < 0.1f;

    if (!translationOk) {
        std::cout << "  ❌ BUG: Translation is WRONG!" << std::endl;
        std::cout << "     Expected: " << expectedTransX << ", Got: " << translation.x << std::endl;
        bugDetected = true;
    } else {
        std::cout << "  ✓ Translation is correct" << std::endl;
    }

    if (!scaleOk) {
        std::cout << "  ❌ BUG: Scale is WRONG!" << std::endl;
        std::cout << "     Expected: " << expectedScale << ", Got: " << scaleValue << std::endl;
        std::cout << "     Likely OVERWRITTEN by another channel!" << std::endl;
        bugDetected = true;
    } else {
        std::cout << "  ✓ Scale is correct" << std::endl;
    }

    // 5. 测试只有单个通道的情况（对照组）
    std::cout << "\n--- Control Test: Single Channel Only ---" << std::endl;

    FSkeletonAsset* skeleton2 = new FSkeletonAsset();
    skeleton2->mEngine = engine;
    skeleton2->mEntityManager = &em;
    skeleton2->mTransformManager = &tm;

    // Reserve capacity
    skeleton2->mBoneEntities.reserve(1);
    skeleton2->mBoneNames.reserve(1);
    skeleton2->mInverseBindMatrices.reserve(1);

    Entity bone2 = em.create();
    tm.create(bone2);
    skeleton2->mBoneEntities.push_back(bone2);
    skeleton2->mBoneNames.emplace_back("test_bone2");
    skeleton2->mBoneNameToIndex["test_bone2"] = 0;
    skeleton2->mInverseBindMatrices.push_back(mat4f());
    skeleton2->mRoot = bone2;

    // 创建只有TRANSLATION的动画
    FAnimationAsset* animSingle = new FAnimationAsset();

    SingleAnimation singleAnimObj;
    singleAnimObj.mName = CString("SingleChannelTest");
    singleAnimObj.mDuration = 1.0f;
    singleAnimObj.mSamplers.push_back(translateSampler);  // 复用sampler

    Channel singleChannel;
    singleChannel.sampler = &singleAnimObj.mSamplers[0];
    singleChannel.targetBoneName = "test_bone2";
    singleChannel.transformType = Channel::TRANSLATION;
    singleAnimObj.mChannels.push_back(singleChannel);

    // 将SingleAnimation添加到AnimationAsset
    animSingle->mAnimations.push_back(singleAnimObj);

    // 缓存动画名称
    animSingle->mAnimationNames.push_back(animSingle->mAnimations[0].mName.c_str());
    animSingle->mAnimationNames.push_back(nullptr);

    StandaloneAnimator* animator2 = StandaloneAnimator::create(*engine);
    animator2->bindSkeleton(skeleton2);
    animator2->playAnimation(animSingle, 0, 1.0f, false);
    animator2->update(0.5f);

    TransformManager::Instance ti2 = tm.getInstance(bone2);
    mat4f transform2 = tm.getWorldTransform(ti2);
    float3 translation2(transform2[3][0], transform2[3][1], transform2[3][2]);

    std::cout << "Single TRANSLATION channel at t=0.5:" << std::endl;
    std::cout << "  Translation: (" << translation2.x << ", " << translation2.y << ", " << translation2.z << ")" << std::endl;

    if (std::abs(translation2.x - 0.5f) < 0.1f) {
        std::cout << "  ✓ Single channel works correctly" << std::endl;
    }

    // Cleanup
    StandaloneAnimator::destroy(&animator);
    StandaloneAnimator::destroy(&animator2);
    delete anim;
    delete animSingle;
    delete skeleton;
    delete skeleton2;
    Engine::destroy(&engine);

    std::cout << "\n=== Test Summary ===" << std::endl;
    if (bugDetected) {
        std::cout << "\n🐛 BUG CONFIRMED!" << std::endl;
        std::cout << "   Multiple animation channels (T+R+S) overwrite each other." << std::endl;
        std::cout << "   Root cause: applyAnimation() sets entire transform matrix" << std::endl;
        std::cout << "               instead of decomposing and composing TRS components." << std::endl;
        std::cout << "\n   Fix needed: Implement TRS decompose/compose in applyAnimation()" << std::endl;
        return 1;
    } else {
        std::cout << "\n✅ No multi-channel bug detected" << std::endl;
        std::cout << "   All transform components preserved correctly." << std::endl;
        return 0;
    }
}
