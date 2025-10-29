/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * Animator 外部动画测试
 * Tests for Animator external animation support
 *
 * 本测试文件验证 Animator 的外部动画加载功能，包括：
 * 1. 外部动画加载
 * 2. 外部动画卸载
 * 3. 动画计数
 * 4. 外部动画播放
 * 5. 内部/外部动画切换
 */

#include <gtest/gtest.h>

#include <gltfio_ext/Animator.h>
#include <gltfio_ext/AssetLoader.h>
#include <gltfio_ext/AnimationAsset.h>
#include <gltfio_ext/FilamentAsset.h>
#include <gltfio_ext/MaterialProvider.h>
#include <gltfio_ext/ResourceLoader.h>

#include <filament/Engine.h>

#include <utils/NameComponentManager.h>
#include <utils/EntityManager.h>

#include "materials/uberarchive.h"

#include <fstream>
#include <vector>

using namespace filament;
using namespace filament::gltfio_ext;
using namespace utils;

/**
 * 读取二进制文件
 */
static std::vector<uint8_t> readBinaryFile(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}

/**
 * 测试 Fixture
 */
class AnimatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建 Filament Engine
        mEngine = Engine::create(Engine::Backend::NOOP);
        ASSERT_NE(mEngine, nullptr);

        // 创建 NameComponentManager
        mNameManager = new NameComponentManager(EntityManager::get());
        ASSERT_NE(mNameManager, nullptr);

        // 创建 MaterialProvider
        mMaterials = createUbershaderProvider(mEngine, UBERARCHIVE_DEFAULT_DATA,
                UBERARCHIVE_DEFAULT_SIZE);
        ASSERT_NE(mMaterials, nullptr);

        // 创建 AssetLoader
        AssetConfiguration config;
        config.engine = mEngine;
        config.materials = mMaterials;
        config.names = mNameManager;

        mLoader = AssetLoader::create(config);
        ASSERT_NE(mLoader, nullptr);

        // 创建 ResourceLoader
        ResourceConfiguration resourceConfig;
        resourceConfig.engine = mEngine;
        resourceConfig.gltfPath = nullptr;
        resourceConfig.normalizeSkinningWeights = false;

        mResourceLoader = new ResourceLoader(resourceConfig);
        ASSERT_NE(mResourceLoader, nullptr);
    }

    void TearDown() override {
        delete mResourceLoader;
        AssetLoader::destroy(&mLoader);
        if (mMaterials) {
            mMaterials->destroyMaterials();
            delete mMaterials;
        }
        delete mNameManager;
        Engine::destroy(&mEngine);
    }

    /**
     * 辅助方法：加载 GLB 文件并初始化资源
     */
    FilamentAsset* loadAssetWithResources(const char* path) {
        auto meshData = readBinaryFile(path);
        if (meshData.empty()) {
            return nullptr;
        }

        FilamentAsset* asset = mLoader->createAsset(meshData.data(), meshData.size());
        if (asset == nullptr) {
            return nullptr;
        }

        // 加载资源（这会设置 mResourcesLoaded = true 并创建 Animator）
        mResourceLoader->loadResources(asset);

        return asset;
    }

    Engine* mEngine = nullptr;
    MaterialProvider* mMaterials = nullptr;
    AssetLoader* mLoader = nullptr;
    ResourceLoader* mResourceLoader = nullptr;
    NameComponentManager* mNameManager = nullptr;
};

/**
 * 测试：加载外部动画（Invalid Asset）
 */
TEST_F(AnimatorTest, LoadExternalAnimationInvalidAsset) {
    // 加载一个简单的 mesh asset
    FilamentAsset* meshAsset = loadAssetWithResources("AnimatedMorphCube.glb");
    ASSERT_NE(meshAsset, nullptr);

    FilamentInstance* instance = meshAsset->getInstance();
    ASSERT_NE(instance, nullptr);

    auto* animator = instance->getAnimator();
    ASSERT_NE(animator, nullptr);

    // 测试传入 nullptr
    bool success = animator->loadExternalAnimation(nullptr);
    EXPECT_FALSE(success);
    EXPECT_FALSE(animator->hasExternalAnimation());

    mLoader->destroyAsset(meshAsset);
}

/**
 * 测试：检查未加载外部动画的状态
 */
TEST_F(AnimatorTest, HasExternalAnimationInitiallyFalse) {
    FilamentAsset* meshAsset = loadAssetWithResources("AnimatedMorphCube.glb");
    ASSERT_NE(meshAsset, nullptr);

    FilamentInstance* instance = meshAsset->getInstance();
    ASSERT_NE(instance, nullptr);

    auto* animator = instance->getAnimator();
    ASSERT_NE(animator, nullptr);

    // 初始状态应该是 false
    EXPECT_FALSE(animator->hasExternalAnimation());

    mLoader->destroyAsset(meshAsset);
}

/**
 * 测试：动画计数（无外部动画）
 */
TEST_F(AnimatorTest, GetAnimationCountWithoutExternal) {
    FilamentAsset* meshAsset = loadAssetWithResources("AnimatedMorphCube.glb");
    ASSERT_NE(meshAsset, nullptr);

    FilamentInstance* instance = meshAsset->getInstance();
    ASSERT_NE(instance, nullptr);

    auto* animator = instance->getAnimator();
    ASSERT_NE(animator, nullptr);

    size_t count = animator->getAnimationCount();
    EXPECT_GT(count, 0);  // AnimatedMorphCube 有内部动画

    mLoader->destroyAsset(meshAsset);
}

/**
 * 测试：卸载未加载的外部动画（不应崩溃）
 */
TEST_F(AnimatorTest, UnloadExternalAnimationWhenNoneLoaded) {
    FilamentAsset* meshAsset = loadAssetWithResources("AnimatedMorphCube.glb");
    ASSERT_NE(meshAsset, nullptr);

    FilamentInstance* instance = meshAsset->getInstance();
    ASSERT_NE(instance, nullptr);

    auto* animator = instance->getAnimator();
    ASSERT_NE(animator, nullptr);

    // 卸载不存在的外部动画不应崩溃
    EXPECT_NO_THROW(animator->unloadExternalAnimation());
    EXPECT_FALSE(animator->hasExternalAnimation());

    mLoader->destroyAsset(meshAsset);
}

/**
 * 测试：播放内部动画（确保未破坏原有功能）
 */
TEST_F(AnimatorTest, ApplyInternalAnimation) {
    FilamentAsset* meshAsset = loadAssetWithResources("AnimatedMorphCube.glb");
    ASSERT_NE(meshAsset, nullptr);

    FilamentInstance* instance = meshAsset->getInstance();
    ASSERT_NE(instance, nullptr);

    auto* animator = instance->getAnimator();
    ASSERT_NE(animator, nullptr);

    size_t animCount = animator->getAnimationCount();
    if (animCount > 0) {
        // 应该能正常播放内部动画
        EXPECT_NO_THROW({
            animator->applyAnimation(0, 0.0f);
            animator->applyAnimation(0, 0.5f);
            animator->updateBoneMatrices();
        });
    }

    mLoader->destroyAsset(meshAsset);
}

/**
 * 测试：播放无效索引的动画（应该输出错误但不崩溃）
 */
TEST_F(AnimatorTest, ApplyAnimationInvalidIndex) {
    FilamentAsset* meshAsset = loadAssetWithResources("AnimatedMorphCube.glb");
    ASSERT_NE(meshAsset, nullptr);

    FilamentInstance* instance = meshAsset->getInstance();
    ASSERT_NE(instance, nullptr);

    auto* animator = instance->getAnimator();
    ASSERT_NE(animator, nullptr);

    size_t animCount = animator->getAnimationCount();

    // 尝试播放超出范围的索引（不应崩溃）
    EXPECT_NO_THROW({
        animator->applyAnimation(animCount + 100, 0.0f);
    });

    mLoader->destroyAsset(meshAsset);
}

/**
 * 测试：获取内部动画的元数据
 */
TEST_F(AnimatorTest, GetInternalAnimationMetadata) {
    FilamentAsset* meshAsset = loadAssetWithResources("AnimatedMorphCube.glb");
    ASSERT_NE(meshAsset, nullptr);

    FilamentInstance* instance = meshAsset->getInstance();
    ASSERT_NE(instance, nullptr);

    auto* animator = instance->getAnimator();
    ASSERT_NE(animator, nullptr);

    size_t animCount = animator->getAnimationCount();
    if (animCount > 0) {
        // 获取第一个动画的元数据
        float duration = animator->getAnimationDuration(0);
        const char* name = animator->getAnimationName(0);

        EXPECT_GT(duration, 0.0f);
        EXPECT_NE(name, nullptr);
    }

    mLoader->destroyAsset(meshAsset);
}

/**
 * 测试：外部动画元数据边界检查（Duration）
 */
TEST_F(AnimatorTest, GetExternalAnimationDurationBoundaryCheck) {
    FilamentAsset* meshAsset = loadAssetWithResources("AnimatedMorphCube.glb");
    ASSERT_NE(meshAsset, nullptr);

    FilamentInstance* instance = meshAsset->getInstance();
    ASSERT_NE(instance, nullptr);

    auto* animator = instance->getAnimator();
    ASSERT_NE(animator, nullptr);

    size_t internalCount = animator->getAnimationCount();

    // 测试访问不存在的外部动画索引（应返回 0.0f，不崩溃）
    size_t invalidExternalIndex = internalCount + 100;
    float duration = animator->getAnimationDuration(invalidExternalIndex);
    EXPECT_EQ(duration, 0.0f) << "Invalid external animation index should return 0.0f";

    mLoader->destroyAsset(meshAsset);
}

/**
 * 测试：外部动画元数据边界检查（Name）
 */
TEST_F(AnimatorTest, GetExternalAnimationNameBoundaryCheck) {
    FilamentAsset* meshAsset = loadAssetWithResources("AnimatedMorphCube.glb");
    ASSERT_NE(meshAsset, nullptr);

    FilamentInstance* instance = meshAsset->getInstance();
    ASSERT_NE(instance, nullptr);

    auto* animator = instance->getAnimator();
    ASSERT_NE(animator, nullptr);

    size_t internalCount = animator->getAnimationCount();

    // 测试访问不存在的外部动画索引（应返回空字符串，不崩溃）
    size_t invalidExternalIndex = internalCount + 100;
    const char* name = animator->getAnimationName(invalidExternalIndex);
    EXPECT_STREQ(name, "") << "Invalid external animation index should return empty string";

    mLoader->destroyAsset(meshAsset);
}

/**
 * 测试：内部动画索引边界检查（Duration）
 */
TEST_F(AnimatorTest, GetInternalAnimationDurationBoundaryCheck) {
    FilamentAsset* meshAsset = loadAssetWithResources("AnimatedMorphCube.glb");
    ASSERT_NE(meshAsset, nullptr);

    FilamentInstance* instance = meshAsset->getInstance();
    ASSERT_NE(instance, nullptr);

    auto* animator = instance->getAnimator();
    ASSERT_NE(animator, nullptr);

    size_t animCount = animator->getAnimationCount();

    // 测试访问越界的内部动画索引（如果只有 1 个动画，访问索引 1 应该失败）
    if (animCount > 0) {
        size_t invalidIndex = animCount + 10;  // 明确超出范围
        float duration = animator->getAnimationDuration(invalidIndex);
        EXPECT_EQ(duration, 0.0f) << "Invalid internal animation index should return 0.0f";
    }

    mLoader->destroyAsset(meshAsset);
}

/**
 * 测试：内部动画索引边界检查（Name）
 */
TEST_F(AnimatorTest, GetInternalAnimationNameBoundaryCheck) {
    FilamentAsset* meshAsset = loadAssetWithResources("AnimatedMorphCube.glb");
    ASSERT_NE(meshAsset, nullptr);

    FilamentInstance* instance = meshAsset->getInstance();
    ASSERT_NE(instance, nullptr);

    auto* animator = instance->getAnimator();
    ASSERT_NE(animator, nullptr);

    size_t animCount = animator->getAnimationCount();

    // 测试访问越界的内部动画索引
    if (animCount > 0) {
        size_t invalidIndex = animCount + 10;  // 明确超出范围
        const char* name = animator->getAnimationName(invalidIndex);
        EXPECT_STREQ(name, "") << "Invalid internal animation index should return empty string";
    }

    mLoader->destroyAsset(meshAsset);
}

/**
 * 测试：成功加载外部动画（集成测试）
 */
TEST_F(AnimatorTest, LoadExternalAnimationSuccess) {
    // 加载分离的 mesh 和 animation 文件
    auto meshData = readBinaryFile("ecorche_full.glb");
    auto animData = readBinaryFile("ecorche_animation_only.glb");

    if (meshData.empty() || animData.empty()) {
        GTEST_SKIP() << "Test assets not found (ecorche_full.glb or ecorche_animation_only.glb)";
    }

    // 创建资产
    FilamentAsset* meshAsset = mLoader->createAsset(meshData.data(), meshData.size());
    ASSERT_NE(meshAsset, nullptr);

    // 加载资源（创建 Animator）
    mResourceLoader->loadResources(meshAsset);

    FilamentInstance* instance = meshAsset->getInstance();
    ASSERT_NE(instance, nullptr);

    auto* animator = instance->getAnimator();
    ASSERT_NE(animator, nullptr);

    // 记录初始动画数量
    size_t initialAnimCount = animator->getAnimationCount();

    // 加载外部动画
    AnimationAsset* animAsset = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset, nullptr);

    bool success = animator->loadExternalAnimation(animAsset);
    EXPECT_TRUE(success) << "Failed to load external animation";

    // 验证状态
    EXPECT_TRUE(animator->hasExternalAnimation());

    // 验证动画数量增加
    size_t totalAnimCount = animator->getAnimationCount();
    size_t externalAnimCount = animAsset->getAnimationCount();
    EXPECT_EQ(totalAnimCount, initialAnimCount + externalAnimCount)
        << "Total animation count should be internal + external";

    // 验证可以获取外部动画的元数据
    for (size_t i = 0; i < externalAnimCount; i++) {
        size_t externalIndex = initialAnimCount + i;
        float duration = animator->getAnimationDuration(externalIndex);
        const char* name = animator->getAnimationName(externalIndex);

        EXPECT_GT(duration, 0.0f) << "External animation " << i << " should have valid duration";
        EXPECT_NE(name, nullptr) << "External animation " << i << " should have a name";
    }

    // 清理
    mLoader->destroyAnimationAsset(animAsset);
    mLoader->destroyAsset(meshAsset);
}

/**
 * 测试：替换外部动画（验证两段式更新）
 */
TEST_F(AnimatorTest, ReplaceExternalAnimation) {
    // 加载资产
    auto meshData = readBinaryFile("ecorche_full.glb");
    auto animData = readBinaryFile("ecorche_animation_only.glb");

    if (meshData.empty() || animData.empty()) {
        GTEST_SKIP() << "Test assets not found";
    }

    FilamentAsset* meshAsset = mLoader->createAsset(meshData.data(), meshData.size());
    ASSERT_NE(meshAsset, nullptr);

    mResourceLoader->loadResources(meshAsset);

    FilamentInstance* instance = meshAsset->getInstance();
    auto* animator = instance->getAnimator();
    ASSERT_NE(animator, nullptr);

    size_t initialAnimCount = animator->getAnimationCount();

    // 第一次加载外部动画
    AnimationAsset* animAsset1 = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset1, nullptr);

    bool success1 = animator->loadExternalAnimation(animAsset1);
    EXPECT_TRUE(success1);
    EXPECT_TRUE(animator->hasExternalAnimation());

    size_t countAfterFirst = animator->getAnimationCount();
    EXPECT_GT(countAfterFirst, initialAnimCount);

    // 第二次加载（替换）
    AnimationAsset* animAsset2 = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset2, nullptr);

    bool success2 = animator->loadExternalAnimation(animAsset2);
    EXPECT_TRUE(success2) << "Should successfully replace external animation";
    EXPECT_TRUE(animator->hasExternalAnimation());

    // 验证动画数量保持一致（因为加载的是同一个文件）
    size_t countAfterSecond = animator->getAnimationCount();
    EXPECT_EQ(countAfterSecond, countAfterFirst) << "Animation count should remain the same after replacement";

    // 清理
    mLoader->destroyAnimationAsset(animAsset1);
    mLoader->destroyAnimationAsset(animAsset2);
    mLoader->destroyAsset(meshAsset);
}

/**
 * 测试：加载失败后状态保持一致（验证两段式更新的回滚）
 */
TEST_F(AnimatorTest, LoadExternalAnimationFailureKeepsOldState) {
    auto meshData = readBinaryFile("AnimatedMorphCube.glb");
    ASSERT_FALSE(meshData.empty());

    FilamentAsset* meshAsset = mLoader->createAsset(meshData.data(), meshData.size());
    ASSERT_NE(meshAsset, nullptr);

    mResourceLoader->loadResources(meshAsset);

    FilamentInstance* instance = meshAsset->getInstance();
    auto* animator = instance->getAnimator();
    ASSERT_NE(animator, nullptr);

    // 初始状态：没有外部动画
    EXPECT_FALSE(animator->hasExternalAnimation());
    size_t initialCount = animator->getAnimationCount();

    // 尝试加载 nullptr（应该失败）
    bool success = animator->loadExternalAnimation(nullptr);
    EXPECT_FALSE(success);

    // 验证状态保持不变
    EXPECT_FALSE(animator->hasExternalAnimation()) << "Should still have no external animation after failure";
    EXPECT_EQ(animator->getAnimationCount(), initialCount) << "Animation count should not change after failure";

    mLoader->destroyAsset(meshAsset);
}

// 注意：上述集成测试验证了完整的外部动画加载流程，
// 包括成功加载、替换现有动画、以及失败后的状态回滚。

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

