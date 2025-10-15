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

    // 创建Engine
    Engine* engine = Engine::create();
    auto& em = EntityManager::get();
    auto& tm = engine->getTransformManager();

    // 手动创建一个简单的骨骼用于测试
    FSkeletonAsset* skeleton = new FSkeletonAsset();
    skeleton->mEngine = engine;
    skeleton->mEntityManager = &em;
    skeleton->mTransformManager = &tm;

    // 创建测试骨骼层次: root -> child1 -> child2
    const size_t boneCount = 3;
    skeleton->mBoneEntities.reserve(boneCount);
    skeleton->mBoneNames.reserve(boneCount);
    skeleton->mInverseBindMatrices.reserve(boneCount);

    // 创建3个骨骼
    for (size_t i = 0; i < boneCount; ++i) {
        Entity entity = em.create();
        skeleton->mBoneEntities.push_back(entity);

        auto ti = tm.getInstance(entity);
        if (!ti) {
            tm.create(entity);
            ti = tm.getInstance(entity);
        }

        // 设置名称
        char nameBuf[32];
        snprintf(nameBuf, sizeof(nameBuf), "bone_%zu", i);
        skeleton->mBoneNames.emplace_back(nameBuf);
        skeleton->mBoneNameToIndex[nameBuf] = i;

        // 设置逆绑定矩阵（单位矩阵）
        skeleton->mInverseBindMatrices.push_back(math::mat4f());

        // 设置父子关系
        if (i > 0) {
            auto parentTi = tm.getInstance(skeleton->mBoneEntities[i - 1]);
            tm.setParent(ti, parentTi);
        }
    }

    skeleton->mRoot = skeleton->mBoneEntities[0];

    // 测试查询接口
    std::cout << "\n--- Testing API ---" << std::endl;
    std::cout << "Bone count: " << skeleton->getBoneCount() << std::endl;
    assert(skeleton->getBoneCount() == 3);

    for (size_t i = 0; i < skeleton->getBoneCount(); ++i) {
        const char* name = skeleton->getBoneName(i);
        std::cout << "  [" << i << "] " << (name ? name : "(unnamed)") << std::endl;
        assert(name != nullptr);
    }

    // 测试名称查找
    int index = skeleton->getBoneIndex("bone_1");
    std::cout << "\nName lookup test: 'bone_1' -> index " << index << std::endl;
    assert(index == 1);

    index = skeleton->getBoneIndex("nonexistent");
    std::cout << "Name lookup test: 'nonexistent' -> index " << index << std::endl;
    assert(index == -1);

    // 测试逆绑定矩阵
    const auto* ibm = skeleton->getInverseBindMatrices();
    std::cout << "\nInverse bind matrices: " << (ibm ? "OK" : "NULL") << std::endl;
    assert(ibm != nullptr);

    // 测试根节点
    Entity root = skeleton->getRoot();
    std::cout << "Root entity: " << root.getId() << std::endl;
    assert(root == skeleton->mBoneEntities[0]);

    // 测试骨骼索引验证
    uint16_t validIndices[] = {0, 1, 2};
    bool valid = skeleton->validateBoneIndices(validIndices, 3);
    std::cout << "\nValidate indices [0,1,2]: " << (valid ? "PASS" : "FAIL") << std::endl;
    assert(valid);

    uint16_t invalidIndices[] = {0, 1, 10};
    valid = skeleton->validateBoneIndices(invalidIndices, 3);
    std::cout << "Validate indices [0,1,10]: " << (valid ? "PASS" : "FAIL") << std::endl;
    assert(!valid);

    // 清理
    delete skeleton;
    Engine::destroy(&engine);

    std::cout << "\n=== SkeletonAsset Test PASSED ===" << std::endl;
    return 0;
}
