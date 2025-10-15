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
