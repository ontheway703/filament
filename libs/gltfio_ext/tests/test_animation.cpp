#include <gltfio/AssetLoaderExt.h>
#include <gltfio/AnimationAsset.h>
#include <gltfio/AnimationPack.h>
#include "../src/FAnimationAsset.h"
#include "../src/FAnimationPack.h"

#include <filament/Engine.h>
#include <iostream>

using namespace filament;
using namespace filament::gltfio;

int main() {
    std::cout << "=== AnimationAsset Test ===" << std::endl;

    // 创建Engine
    Engine* engine = Engine::create();

    // 手动创建一个简单的AnimationAsset用于测试
    FAnimationAsset* anim = new FAnimationAsset();
    anim->mName = "TestAnimation";
    anim->mDuration = 2.0f;

    // 创建一个简单的Sampler
    Sampler sampler1;
    sampler1.times[0.0f] = 0;
    sampler1.times[1.0f] = 1;
    sampler1.times[2.0f] = 2;
    sampler1.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f};  // 3 vec3 values
    sampler1.interpolation = Sampler::LINEAR;
    anim->mSamplers.push_back(sampler1);

    Sampler sampler2;
    sampler2.times[0.0f] = 0;
    sampler2.times[1.0f] = 1;
    sampler2.times[2.0f] = 2;
    sampler2.values = {0.0f, 0.0f, 0.0f, 1.0f,   // quat at t=0
                       0.707f, 0.0f, 0.0f, 0.707f,  // quat at t=1
                       1.0f, 0.0f, 0.0f, 0.0f};     // quat at t=2
    sampler2.interpolation = Sampler::LINEAR;
    anim->mSamplers.push_back(sampler2);

    // 创建Channels
    Channel ch1;
    ch1.sampler = &anim->mSamplers[0];
    ch1.targetBoneName = "bone_0";
    ch1.transformType = Channel::TRANSLATION;
    anim->mChannels.push_back(ch1);

    Channel ch2;
    ch2.sampler = &anim->mSamplers[1];
    ch2.targetBoneName = "bone_1";
    ch2.transformType = Channel::ROTATION;
    anim->mChannels.push_back(ch2);

    // 测试API
    std::cout << "\n--- Testing AnimationAsset API ---" << std::endl;
    std::cout << "Name: " << anim->getName() << std::endl;
    assert(std::string(anim->getName()) == "TestAnimation");

    std::cout << "Duration: " << anim->getDuration() << "s" << std::endl;
    assert(anim->getDuration() == 2.0f);

    std::cout << "Channel count: " << anim->getChannelCount() << std::endl;
    assert(anim->getChannelCount() == 2);

    // 测试各个channel
    for (size_t i = 0; i < anim->getChannelCount(); ++i) {
        const char* boneName = anim->getChannelTargetBone(i);
        AnimationAsset::ChannelType type = anim->getChannelType(i);

        std::cout << "  Channel " << i << ": target=" << boneName
                  << ", type=";

        switch (type) {
            case AnimationAsset::ChannelType::TRANSLATION:
                std::cout << "TRANSLATION";
                break;
            case AnimationAsset::ChannelType::ROTATION:
                std::cout << "ROTATION";
                break;
            case AnimationAsset::ChannelType::SCALE:
                std::cout << "SCALE";
                break;
            case AnimationAsset::ChannelType::WEIGHTS:
                std::cout << "WEIGHTS";
                break;
        }
        std::cout << std::endl;
    }

    assert(std::string(anim->getChannelTargetBone(0)) == "bone_0");
    assert(anim->getChannelType(0) == AnimationAsset::ChannelType::TRANSLATION);
    assert(std::string(anim->getChannelTargetBone(1)) == "bone_1");
    assert(anim->getChannelType(1) == AnimationAsset::ChannelType::ROTATION);

    // 测试AnimationPack
    std::cout << "\n--- Testing AnimationPack ---" << std::endl;

    FAnimationPack* pack = new FAnimationPack();

    // 创建第二个动画
    FAnimationAsset* anim2 = new FAnimationAsset();
    anim2->mName = "TestAnimation2";
    anim2->mDuration = 1.0f;

    Channel ch3;
    ch3.sampler = &sampler1;  // 重用sampler
    ch3.targetBoneName = "bone_2";
    ch3.transformType = Channel::SCALE;
    anim2->mChannels.push_back(ch3);

    // 添加到pack
    pack->mAnimations.push_back(anim);
    pack->mAnimations.push_back(anim2);
    pack->mAnimationNames.push_back(anim->getName());
    pack->mAnimationNames.push_back(anim2->getName());

    std::cout << "Animation count: " << pack->getAnimationCount() << std::endl;
    assert(pack->getAnimationCount() == 2);

    const char* const* names = pack->getAnimationNames();
    std::cout << "Animation names:" << std::endl;
    for (size_t i = 0; i < pack->getAnimationCount(); ++i) {
        std::cout << "  [" << i << "] " << names[i] << std::endl;
    }

    // 测试通过索引获取
    AnimationAsset* foundByIndex = pack->getAnimation(0);
    assert(foundByIndex != nullptr);
    std::cout << "Get by index [0]: " << foundByIndex->getName() << std::endl;

    // 测试通过名称查找
    AnimationAsset* foundByName = pack->findAnimation("TestAnimation2");
    assert(foundByName != nullptr);
    std::cout << "Find by name 'TestAnimation2': " << foundByName->getName() << std::endl;

    // 测试查找不存在的动画
    AnimationAsset* notFound = pack->findAnimation("NonExistent");
    assert(notFound == nullptr);
    std::cout << "Find non-existent animation: " << (notFound ? "FOUND (unexpected)" : "NULL (expected)") << std::endl;

    // 清理
    delete pack;  // pack会自动删除所有AnimationAsset
    Engine::destroy(&engine);

    std::cout << "\n=== AnimationAsset Test PASSED ===" << std::endl;
    return 0;
}
