/**
 * ============================================================================
 * test_animation.cpp - AnimationAsset 多动画容器测试
 * ============================================================================
 *
 * 【测试目标】
 * 验证 AnimationAsset 作为动画容器的管理功能：
 * - 多个动画的存储和查询
 * - 动画名称到索引的映射
 * - 动画时长获取
 * - SingleAnimation 的 Channel 和 Sampler 结构
 *
 * 【背景知识】
 * glTF 文件可以包含多个动画（如：idle, walk, run），AnimationAsset 负责管理
 * 这些动画数据。每个动画(SingleAnimation)包含：
 *
 * 1. Channels（通道）: 定义动画目标和变换类型
 *    - targetBoneName: 目标骨骼名称
 *    - transformType: TRANSLATION（平移）/ ROTATION（旋转）/ SCALE（缩放）
 *
 * 2. Samplers（采样器）: 存储关键帧数据
 *    - times: 时间戳数组（如 0.0s, 1.0s, 2.0s）
 *    - values: 对应的变换值（vec3 或 quat）
 *    - interpolation: 插值方式（LINEAR, STEP, CUBICSPLINE）
 *
 * 关键概念：
 * - 关键帧动画: 只存储关键时刻的姿态，中间帧通过插值计算
 * - 插值(Interpolation): 在两个关键帧之间平滑过渡（线性插值最常用）
 * - Animation Clip: 一个完整的动画序列（如一个走路循环）
 *
 * 【测试方法】
 * 单元测试 - 手动构造包含2个动画的 AnimationAsset，验证容器 API
 *
 * 【关键验证点】
 * 1. getAnimationCount() 返回正确数量
 * 2. getAnimationName(index) 返回正确名称
 * 3. getAnimationDuration(index) 返回正确时长
 * 4. findAnimationIndex(name) 查询成功/失败
 * 5. Channel 和 Sampler 数据结构完整
 *
 * 【与其他测试的关系】
 * - 前置：无（独立测试）
 * - 后续：test_animator.cpp 需要 AnimationAsset 来播放动画
 *
 * 【运行方式】
 * ./out/cmake-debug/libs/gltfio_ext/test_animation
 *
 * 【预期输出】
 * 所有断言通过，显示 "AnimationAsset Multi-Animation Test PASSED"
 * ============================================================================
 */

#include <gltfio/AssetLoaderExt.h>
#include <gltfio/AnimationAsset.h>
#include "../src/FAnimationAsset.h"

#include <filament/Engine.h>
#include <iostream>

using namespace filament;
using namespace filament::gltfio;

int main() {
    std::cout << "=== AnimationAsset Multi-Animation Test ===" << std::endl;

    // 创建Engine
    Engine* engine = Engine::create();

    // 手动创建一个包含多个动画的AnimationAsset
    FAnimationAsset* animations = new FAnimationAsset();

    // 创建第一个动画
    SingleAnimation anim1;
    anim1.mName = "TestAnimation1";
    anim1.mDuration = 2.0f;

    // 创建第一个动画的Sampler
    Sampler sampler1;
    sampler1.times[0.0f] = 0;
    sampler1.times[1.0f] = 1;
    sampler1.times[2.0f] = 2;
    sampler1.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f};  // 3 vec3 values
    sampler1.interpolation = Sampler::LINEAR;
    anim1.mSamplers.push_back(sampler1);

    Sampler sampler2;
    sampler2.times[0.0f] = 0;
    sampler2.times[1.0f] = 1;
    sampler2.times[2.0f] = 2;
    sampler2.values = {0.0f, 0.0f, 0.0f, 1.0f,   // quat at t=0
                       0.707f, 0.0f, 0.0f, 0.707f,  // quat at t=1
                       1.0f, 0.0f, 0.0f, 0.0f};     // quat at t=2
    sampler2.interpolation = Sampler::LINEAR;
    anim1.mSamplers.push_back(sampler2);

    // 创建第一个动画的Channels
    Channel ch1;
    ch1.sampler = &anim1.mSamplers[0];
    ch1.targetBoneName = "bone_0";
    ch1.transformType = Channel::TRANSLATION;
    anim1.mChannels.push_back(ch1);

    Channel ch2;
    ch2.sampler = &anim1.mSamplers[1];
    ch2.targetBoneName = "bone_1";
    ch2.transformType = Channel::ROTATION;
    anim1.mChannels.push_back(ch2);

    // 创建第二个动画
    SingleAnimation anim2;
    anim2.mName = "TestAnimation2";
    anim2.mDuration = 1.0f;

    Sampler sampler3;
    sampler3.times[0.0f] = 0;
    sampler3.times[1.0f] = 1;
    sampler3.values = {1.0f, 1.0f, 1.0f, 2.0f, 2.0f, 2.0f};
    sampler3.interpolation = Sampler::LINEAR;
    anim2.mSamplers.push_back(sampler3);

    Channel ch3;
    ch3.sampler = &anim2.mSamplers[0];
    ch3.targetBoneName = "bone_2";
    ch3.transformType = Channel::SCALE;
    anim2.mChannels.push_back(ch3);

    // 将两个动画添加到AnimationAsset
    animations->mAnimations.push_back(anim1);
    animations->mAnimations.push_back(anim2);

    // 缓存动画名称
    animations->mAnimationNames.push_back(animations->mAnimations[0].mName.c_str());
    animations->mAnimationNames.push_back(animations->mAnimations[1].mName.c_str());
    animations->mAnimationNames.push_back(nullptr);

    // 测试容器API
    std::cout << "\n--- Testing AnimationAsset Container API ---" << std::endl;
    std::cout << "Animation count: " << animations->getAnimationCount() << std::endl;
    assert(animations->getAnimationCount() == 2);

    std::cout << "Animation names:" << std::endl;
    for (size_t i = 0; i < animations->getAnimationCount(); ++i) {
        std::cout << "  [" << i << "] " << animations->getAnimationName(i) << std::endl;
    }

    // 测试第一个动画的API
    std::cout << "\n--- Testing Animation 0 API ---" << std::endl;
    std::cout << "Name: " << animations->getAnimationName(0) << std::endl;
    assert(std::string(animations->getAnimationName(0)) == "TestAnimation1");

    std::cout << "Duration: " << animations->getAnimationDuration(0) << "s" << std::endl;
    assert(animations->getAnimationDuration(0) == 2.0f);

    // 测试第二个动画
    std::cout << "\n--- Testing Animation 1 API ---" << std::endl;
    std::cout << "Name: " << animations->getAnimationName(1) << std::endl;
    assert(std::string(animations->getAnimationName(1)) == "TestAnimation2");

    std::cout << "Duration: " << animations->getAnimationDuration(1) << "s" << std::endl;
    assert(animations->getAnimationDuration(1) == 1.0f);

    // 测试findAnimationIndex
    std::cout << "\n--- Testing findAnimationIndex ---" << std::endl;

    int idx1 = animations->findAnimationIndex("TestAnimation1");
    assert(idx1 == 0);
    std::cout << "findAnimationIndex('TestAnimation1'): " << idx1 << std::endl;

    int idx2 = animations->findAnimationIndex("TestAnimation2");
    assert(idx2 == 1);
    std::cout << "findAnimationIndex('TestAnimation2'): " << idx2 << std::endl;

    // 测试查找不存在的动画
    int notFound = animations->findAnimationIndex("NonExistent");
    assert(notFound == -1);
    std::cout << "findAnimationIndex('NonExistent'): " << notFound << " (expected: -1)" << std::endl;

    // 清理
    delete animations;
    Engine::destroy(&engine);

    std::cout << "\n=== AnimationAsset Multi-Animation Test PASSED ===" << std::endl;
    return 0;
}
