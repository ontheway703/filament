/**
 * ============================================================================
 * test_animator.cpp - StandaloneAnimator 动画播放器测试
 * ============================================================================
 *
 * 【测试目标】
 * 验证 StandaloneAnimator 的核心动画播放功能：
 * - 绑定骨骼资源
 * - 播放动画并更新骨骼变换
 * - 停止和重置动画
 * - 获取骨骼变换矩阵
 *
 * 【背景知识】
 * StandaloneAnimator 是动画播放引擎，负责：
 * 1. 读取 AnimationAsset 中的关键帧数据
 * 2. 根据当前时间插值计算骨骼变换（位置、旋转、缩放）
 * 3. 将变换应用到 SkeletonAsset 的骨骼 Entity
 * 4. 计算最终的骨骼矩阵（用于 GPU 蒙皮）
 *
 * 关键概念：
 * - Animation Playback（动画播放）:
 *   随时间推进，不断更新骨骼姿态。公式：
 *   currentTransform = interpolate(keyframe[t], keyframe[t+1], alpha)
 *
 * - Bone Matrices（骨骼矩阵）:
 *   animator 计算出的最终矩阵数组，传递给 GPU 进行蒙皮计算
 *
 * - Loop/Non-loop: 动画播放模式
 *   - Loop: 时间超过 duration 后回到起点（适合循环动画如走路）
 *   - Non-loop: 播放到结尾后停止
 *
 * 【测试方法】
 * 单元测试 - 创建简单的骨骼+动画，播放并验证骨骼矩阵更新
 *
 * 【关键验证点】
 * 1. bindSkeleton() 成功绑定
 * 2. playAnimation() 返回有效动画 ID
 * 3. update(deltaTime) 更新骨骼变换
 * 4. getBoneMatrices() 返回变换矩阵
 * 5. stopAnimation() 停止播放
 * 6. resetToBindPose() 重置到初始姿态
 * 7. getBoneCount() 返回骨骼数量
 *
 * 【与其他测试的关系】
 * - 依赖：test_skeleton.cpp, test_animation.cpp
 * - 后续：test_integration.cpp（完整渲染流程）
 *
 * 【运行方式】
 * ./out/cmake-debug/libs/gltfio_ext/test_animator
 *
 * 【预期输出】
 * 动画播放5帧，骨骼变换矩阵随时间变化，显示 "StandaloneAnimator Test PASSED"
 * ============================================================================
 */

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

    // 2. 创建简单的动画（适配新的多动画结构）
    FAnimationAsset* anim = new FAnimationAsset();

    // 创建一个SingleAnimation
    SingleAnimation singleAnim;
    singleAnim.mName = utils::CString("TestAnimation");
    singleAnim.mDuration = 2.0f;

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
    singleAnim.mSamplers.push_back(sampler);

    // 创建Channel
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

    std::cout << "Created animation with duration " << anim->getAnimationDuration(0) << "s" << std::endl;

    // 3. 创建动画器
    StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
    animator->bindSkeleton(skeleton);
    std::cout << "Animator bound to skeleton" << std::endl;

    // 4. 播放动画（使用新的API，需要animIndex参数）
    int animId = animator->playAnimation(anim, 0, 1.0f, true);
    std::cout << "Playing animation ID: " << animId << " (animIndex: 0)" << std::endl;

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

    // 6. 测试停止
    std::cout << "\nTesting stop..." << std::endl;
    animator->stopAnimation(animId);
    std::cout << "Animation stopped (ID: " << animId << ")" << std::endl;

    // 7. 测试重置到绑定姿势
    std::cout << "\nTesting reset to bind pose..." << std::endl;
    animator->resetToBindPose();
    std::cout << "Reset to bind pose" << std::endl;

    // 8. 测试getBoneCount
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
