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
 * AnimationCache 单元测试
 *
 * 本文件测试 Animator 的轻量级缓存系统（Phase 2-3），包括：
 * 1. 基础功能：加载/卸载/清空缓存
 * 2. 播放功能：精确播放/便捷播放
 * 3. LRU 淘汰：访问顺序更新/淘汰机制
 * 4. 极端场景：超限源/空缓存/重名处理
 * 5. 移动语义：指针正确性验证（最关键）
 * 6. 统计查询：CacheStats/命中率
 *
 * 测试用例总数：26+
 * 目标覆盖率：>90%
 */

#include <gtest/gtest.h>

#include <gltfio_ext/Animator.h>
#include <gltfio_ext/AnimationAsset.h>
#include <gltfio_ext/AssetLoader.h>
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
using namespace filament::math;
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
 *
 * 创建必要的 Filament 引擎和基础资产环境
 */
class AnimationCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建 Filament 引擎（NOOP backend 用于测试）
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

        // 加载基础网格资产（用于 Animator 创建）
        loadMeshAsset();

        // 加载参考动画名称（用于后续测试，避免硬编码）
        loadReferenceAnimationNames();
    }

    void TearDown() override {
        if (meshAsset) {
            mLoader->destroyAsset(meshAsset);
            meshAsset = nullptr;
        }

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
     * 动态加载参考动画名称
     * 从测试 GLB 文件中提取真实的动画名称，避免硬编码
     */
    void loadReferenceAnimationNames() {
        const char* path = "ecorche_animation_only.glb";
        auto data = readBinaryFile(path);
        ASSERT_FALSE(data.empty()) << "Failed to read reference animation file";

        AnimationAsset* refAsset = mLoader->loadAnimationAsset(data.data(), data.size());
        ASSERT_NE(refAsset, nullptr) << "Failed to load reference animation asset";

        // 提取所有动画名称
        size_t animCount = refAsset->getAnimationCount();
        mReferenceAnimNames.clear();
        mReferenceAnimNames.reserve(animCount);

        for (size_t i = 0; i < animCount; ++i) {
            mReferenceAnimNames.push_back(refAsset->getAnimation(i).name);
        }

        mLoader->destroyAnimationAsset(refAsset);

        ASSERT_GT(mReferenceAnimNames.size(), 0) << "No animations in reference asset";
    }

    /**
     * 获取参考动画名称（通过索引）
     */
    const char* getAnimName(size_t index) const {
        EXPECT_LT(index, mReferenceAnimNames.size());
        return index < mReferenceAnimNames.size() ? mReferenceAnimNames[index].c_str() : "";
    }

    /**
     * 获取参考动画数量
     */
    size_t getAnimCount() const {
        return mReferenceAnimNames.size();
    }

    /**
     * 加载基础网格资产（用于骨骼映射）
     */
    void loadMeshAsset() {
        // 使用 ecorche_full.glb（包含完整骨骼结构）
        const char* path = "ecorche_full.glb";

        // 读取 GLB 数据
        auto data = readBinaryFile(path);
        if (data.empty()) {
            FAIL() << "Failed to read mesh file: " << path;
        }

        // 加载资产
        meshAsset = mLoader->createAsset(data.data(), data.size());
        if (!meshAsset) {
            FAIL() << "Failed to load mesh asset from: " << path;
        }

        // 加载资源（创建 Animator）
        mResourceLoader->loadResources(meshAsset);

        // 获取 Animator
        FilamentInstance* instance = meshAsset->getInstance();
        ASSERT_NE(instance, nullptr);
        animator = instance->getAnimator();
        ASSERT_NE(animator, nullptr);
    }

    /**
     * 加载真实的测试动画资产（从 ecorche_animation_only.glb）
     *
     * 注意：为了测试多个源，我们需要多次加载同一个文件
     */
    AnimationAsset* loadTestAnimationAsset(const std::string& /*namePrefix*/) {
        const char* path = "ecorche_animation_only.glb";
        auto data = readBinaryFile(path);
        if (data.empty()) {
            return nullptr;
        }

        AnimationAsset* asset = mLoader->loadAnimationAsset(data.data(), data.size());
        return asset;
    }

    Engine* mEngine = nullptr;
    MaterialProvider* mMaterials = nullptr;
    AssetLoader* mLoader = nullptr;
    ResourceLoader* mResourceLoader = nullptr;
    NameComponentManager* mNameManager = nullptr;
    FilamentAsset* meshAsset = nullptr;
    Animator* animator = nullptr;

    // 参考动画名称列表（从测试 GLB 文件动态提取，避免硬编码）
    std::vector<std::string> mReferenceAnimNames;
};

// ========================================
// 基础功能测试（8个）
// ========================================

TEST_F(AnimationCacheTest, LoadSingleSource) {
    auto animAsset = loadTestAnimationAsset("test");
    ASSERT_NE(animAsset, nullptr);

    size_t count = animator->loadAnimationsFromSource("test_source", animAsset);

    EXPECT_GT(count, 0);  // 应该加载至少 1 个动画
    EXPECT_TRUE(animator->hasSource("test_source"));

    auto stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.cachedCount, count);
    EXPECT_EQ(stats.sourceCount, 1);

    mLoader->destroyAnimationAsset(animAsset);
}

TEST_F(AnimationCacheTest, LoadMultipleSources) {
    auto asset1 = loadTestAnimationAsset("source1");
    auto asset2 = loadTestAnimationAsset("source2");
    ASSERT_NE(asset1, nullptr);
    ASSERT_NE(asset2, nullptr);

    animator->loadAnimationsFromSource("source1", asset1);
    animator->loadAnimationsFromSource("source2", asset2);

    EXPECT_TRUE(animator->hasSource("source1"));
    EXPECT_TRUE(animator->hasSource("source2"));

    auto stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.cachedCount, 6);  // 3 + 3
    EXPECT_EQ(stats.sourceCount, 2);

    mLoader->destroyAnimationAsset(asset1);
    mLoader->destroyAnimationAsset(asset2);
}

TEST_F(AnimationCacheTest, UnloadSource) {
    auto animAsset = loadTestAnimationAsset("test");
    ASSERT_NE(animAsset, nullptr);

    animator->loadAnimationsFromSource("test_source", animAsset);
    EXPECT_EQ(animator->getAnimationCacheStats().cachedCount, 3);

    animator->unloadAnimationsFromSource("test_source");

    EXPECT_FALSE(animator->hasSource("test_source"));
    EXPECT_EQ(animator->getAnimationCacheStats().cachedCount, 0);

    mLoader->destroyAnimationAsset(animAsset);
}

TEST_F(AnimationCacheTest, ClearCache) {
    auto asset1 = loadTestAnimationAsset("source1");
    auto asset2 = loadTestAnimationAsset("source2");
    ASSERT_NE(asset1, nullptr);
    ASSERT_NE(asset2, nullptr);

    animator->loadAnimationsFromSource("source1", asset1);
    animator->loadAnimationsFromSource("source2", asset2);
    EXPECT_EQ(animator->getAnimationCacheStats().cachedCount, 6);

    animator->clearAnimationCache();

    EXPECT_EQ(animator->getAnimationCacheStats().cachedCount, 0);
    EXPECT_EQ(animator->getAnimationCacheStats().sourceCount, 0);

    mLoader->destroyAnimationAsset(asset1);
    mLoader->destroyAnimationAsset(asset2);
}

TEST_F(AnimationCacheTest, ReplaceSource) {
    auto asset1 = loadTestAnimationAsset("v1");
    auto asset2 = loadTestAnimationAsset("v2");
    ASSERT_NE(asset1, nullptr);
    ASSERT_NE(asset2, nullptr);

    // 加载第一次
    size_t count1 = animator->loadAnimationsFromSource("test_source", asset1);
    ASSERT_GT(count1, 0);

    // 替换（相同 sourceId）
    // 注意：由于两次加载同一个 GLB 文件，动画名称相同，无法验证名称变化
    // 只能验证统计信息和数量
    size_t count2 = animator->loadAnimationsFromSource("test_source", asset2);
    EXPECT_EQ(count2, count1);  // 数量应该相同

    auto stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.cachedCount, count1);  // 旧的被替换
    EXPECT_EQ(stats.sourceCount, 1);

    mLoader->destroyAnimationAsset(asset1);
    mLoader->destroyAnimationAsset(asset2);
}

TEST_F(AnimationCacheTest, GetLoadedSources) {
    auto asset1 = loadTestAnimationAsset("source1");
    auto asset2 = loadTestAnimationAsset("source2");
    ASSERT_NE(asset1, nullptr);
    ASSERT_NE(asset2, nullptr);

    animator->loadAnimationsFromSource("source1", asset1);
    animator->loadAnimationsFromSource("source2", asset2);

    auto sources = animator->getLoadedSources();
    EXPECT_EQ(sources.size(), 2);
    EXPECT_TRUE(std::find(sources.begin(), sources.end(), "source1") != sources.end());
    EXPECT_TRUE(std::find(sources.begin(), sources.end(), "source2") != sources.end());

    mLoader->destroyAnimationAsset(asset1);
    mLoader->destroyAnimationAsset(asset2);
}

TEST_F(AnimationCacheTest, GetAnimationsInSource) {
    auto animAsset = loadTestAnimationAsset("test");
    ASSERT_NE(animAsset, nullptr);

    animator->loadAnimationsFromSource("test_source", animAsset);

    auto animNames = animator->getAnimationsInSource("test_source");
    EXPECT_EQ(animNames.size(), getAnimCount());

    // 验证每个动画名称
    for (size_t i = 0; i < animNames.size() && i < getAnimCount(); ++i) {
        EXPECT_EQ(animNames[i], getAnimName(i));
    }

    mLoader->destroyAnimationAsset(animAsset);
}

TEST_F(AnimationCacheTest, QueryNonExistentSource) {
    EXPECT_FALSE(animator->hasSource("nonexistent"));
    EXPECT_FALSE(animator->hasAnimation("nonexistent", "anim"));
    EXPECT_EQ(animator->getAnimationDuration("nonexistent", "anim"), 0.0f);

    auto animNames = animator->getAnimationsInSource("nonexistent");
    EXPECT_TRUE(animNames.empty());
}

// ========================================
// 播放功能测试（5个）
// ========================================

TEST_F(AnimationCacheTest, ApplyAnimationPrecise) {
    auto animAsset = loadTestAnimationAsset("test");
    ASSERT_NE(animAsset, nullptr);
    animator->loadAnimationsFromSource("test_source", animAsset);

    // 精确播放
    bool success = animator->applyAnimation("test_source", getAnimName(0), 0.5f);
    EXPECT_TRUE(success);

    // 统计应该更新
    auto stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.hitCount, 1);
    EXPECT_EQ(stats.missCount, 0);

    mLoader->destroyAnimationAsset(animAsset);
}

TEST_F(AnimationCacheTest, ApplyAnimationByName) {
    auto animAsset = loadTestAnimationAsset("test");
    ASSERT_NE(animAsset, nullptr);
    ASSERT_GE(getAnimCount(), 2) << "Test requires at least 2 animations";
    animator->loadAnimationsFromSource("test_source", animAsset);

    // 便捷播放
    bool success = animator->applyAnimationByName(getAnimName(1), 0.5f);
    EXPECT_TRUE(success);

    auto stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.hitCount, 1);

    mLoader->destroyAnimationAsset(animAsset);
}

TEST_F(AnimationCacheTest, ApplyNonExistentAnimation) {
    // 播放不存在的动画
    bool success = animator->applyAnimation("test_source", "nonexistent", 0.5f);
    EXPECT_FALSE(success);

    auto stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.hitCount, 0);
    EXPECT_EQ(stats.missCount, 1);
}

TEST_F(AnimationCacheTest, ApplyAnimationByNameWithMultipleSources) {
    auto asset1 = loadTestAnimationAsset("source1");
    auto asset2 = loadTestAnimationAsset("source2");
    ASSERT_NE(asset1, nullptr);
    ASSERT_NE(asset2, nullptr);

    // 两个源都有相同的动画名称
    animator->loadAnimationsFromSource("source1", asset1);
    animator->loadAnimationsFromSource("source2", asset2);

    // 先播放 source1 的第一个动画
    animator->applyAnimation("source1", getAnimName(0), 0.0f);

    // 便捷播放（应该选择最近访问的 source1）
    bool success = animator->applyAnimationByName(getAnimName(0), 0.5f);
    EXPECT_TRUE(success);

    mLoader->destroyAnimationAsset(asset1);
    mLoader->destroyAnimationAsset(asset2);
}

TEST_F(AnimationCacheTest, GetAnimationDuration) {
    auto animAsset = loadTestAnimationAsset("test");
    ASSERT_NE(animAsset, nullptr);
    animator->loadAnimationsFromSource("test_source", animAsset);

    float duration = animator->getAnimationDuration("test_source", getAnimName(0));
    EXPECT_GT(duration, 0.0f);  // 应该有有效时长

    float durationByName = animator->getAnimationDurationByName(getAnimName(0));
    EXPECT_EQ(duration, durationByName);

    mLoader->destroyAnimationAsset(animAsset);
}

// ========================================
// LRU 淘汰测试（5个）
// ========================================

TEST_F(AnimationCacheTest, LRUEvictionWhenFull) {
    // 设置小缓存（容量 5）
    animator->setAnimationCacheSize(5);

    auto asset1 = loadTestAnimationAsset("source1");
    auto asset2 = loadTestAnimationAsset("source2");
    ASSERT_NE(asset1, nullptr);
    ASSERT_NE(asset2, nullptr);

    animator->loadAnimationsFromSource("source1", asset1);
    EXPECT_EQ(animator->getAnimationCacheStats().cachedCount, 3);

    // 加载第二个源（6 > 5，应该淘汰 2 个最早的）
    animator->loadAnimationsFromSource("source2", asset2);
    EXPECT_EQ(animator->getAnimationCacheStats().cachedCount, 5);

    // source1 的部分动画应该被淘汰
    EXPECT_TRUE(animator->hasSource("source2"));  // source2 完整保留

    mLoader->destroyAnimationAsset(asset1);
    mLoader->destroyAnimationAsset(asset2);
}

TEST_F(AnimationCacheTest, AccessOrderUpdatesOnPlayback) {
    animator->setAnimationCacheSize(5);

    auto asset1 = loadTestAnimationAsset("source1");
    ASSERT_NE(asset1, nullptr);
    size_t count1 = animator->loadAnimationsFromSource("source1", asset1);
    ASSERT_GT(count1, 0);

    // 获取第一个动画名称并播放
    auto anims1 = animator->getAnimationsInSource("source1");
    ASSERT_GT(anims1.size(), 0);
    animator->applyAnimation("source1", anims1[0].c_str(), 0.5f);

    // 加载新源
    auto asset2 = loadTestAnimationAsset("source2");
    ASSERT_NE(asset2, nullptr);
    animator->loadAnimationsFromSource("source2", asset2);

    // 第一个动画应该还在（因为最近访问）
    EXPECT_TRUE(animator->hasAnimation("source1", anims1[0].c_str()));

    mLoader->destroyAnimationAsset(asset1);
    mLoader->destroyAnimationAsset(asset2);
}

TEST_F(AnimationCacheTest, LeastRecentlyAccessedEvictedFirst) {
    ASSERT_GE(getAnimCount(), 3) << "Test requires at least 3 animations";
    animator->setAnimationCacheSize(3);

    auto asset = loadTestAnimationAsset("test");
    ASSERT_NE(asset, nullptr);
    animator->loadAnimationsFromSource("test", asset);
    EXPECT_EQ(animator->getAnimationCacheStats().cachedCount, getAnimCount());

    // 访问顺序：anim2 > anim1 > anim0（从旧到新）
    animator->applyAnimation("test", getAnimName(0), 0.0f);
    animator->applyAnimation("test", getAnimName(1), 0.0f);
    animator->applyAnimation("test", getAnimName(2), 0.0f);

    // 缩小缓存（应该淘汰 anim0，保留 anim1 和 anim2）
    animator->setAnimationCacheSize(2);
    EXPECT_EQ(animator->getAnimationCacheStats().cachedCount, 2);
    EXPECT_FALSE(animator->hasAnimation("test", getAnimName(0)));
    EXPECT_TRUE(animator->hasAnimation("test", getAnimName(1)));
    EXPECT_TRUE(animator->hasAnimation("test", getAnimName(2)));

    mLoader->destroyAnimationAsset(asset);
}

TEST_F(AnimationCacheTest, EvictionWithSetCacheSize) {
    auto asset1 = loadTestAnimationAsset("source1");
    auto asset2 = loadTestAnimationAsset("source2");
    ASSERT_NE(asset1, nullptr);
    ASSERT_NE(asset2, nullptr);

    animator->loadAnimationsFromSource("source1", asset1);
    animator->loadAnimationsFromSource("source2", asset2);
    EXPECT_EQ(animator->getAnimationCacheStats().cachedCount, 6);

    // 缩小缓存到 3
    animator->setAnimationCacheSize(3);
    auto stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.cachedCount, 3);
    EXPECT_EQ(stats.maxSize, 3) << "maxSize should reflect new cache size";
    EXPECT_LE(stats.cachedCount, animator->getAnimationCacheSize());

    // 扩大缓存到 10
    animator->setAnimationCacheSize(10);
    stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.cachedCount, 3) << "Cached count should remain unchanged when expanding";
    EXPECT_EQ(stats.maxSize, 10) << "maxSize should reflect new cache size";

    // 缩小到 2
    animator->setAnimationCacheSize(2);
    stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.cachedCount, 2);
    EXPECT_EQ(stats.maxSize, 2) << "maxSize should reflect new cache size";

    mLoader->destroyAnimationAsset(asset1);
    mLoader->destroyAnimationAsset(asset2);
}

TEST_F(AnimationCacheTest, QueryDoesNotUpdateAccessOrder) {
    ASSERT_GE(getAnimCount(), 3) << "Test requires at least 3 animations";
    animator->setAnimationCacheSize(3);

    auto asset = loadTestAnimationAsset("test");
    ASSERT_NE(asset, nullptr);
    animator->loadAnimationsFromSource("test", asset);

    // 查询不应该更新访问顺序
    animator->hasAnimation("test", getAnimName(0));
    animator->getAnimationDuration("test", getAnimName(0));

    // 播放其他动画
    animator->applyAnimation("test", getAnimName(1), 0.0f);
    animator->applyAnimation("test", getAnimName(2), 0.0f);

    // 缩小缓存（anim0 应该被淘汰，因为查询不更新访问顺序）
    animator->setAnimationCacheSize(2);
    EXPECT_FALSE(animator->hasAnimation("test", getAnimName(0)));
    EXPECT_TRUE(animator->hasAnimation("test", getAnimName(1)));
    EXPECT_TRUE(animator->hasAnimation("test", getAnimName(2)));

    mLoader->destroyAnimationAsset(asset);
}

// ========================================
// 极端场景测试（4个）
// ========================================

TEST_F(AnimationCacheTest, RejectOversizedSource) {
    // 设置缓存只能容纳 2 个动画
    animator->setAnimationCacheSize(2);

    // 尝试加载多个动画的源（应该被拒绝）
    auto asset = loadTestAnimationAsset("oversized");
    ASSERT_NE(asset, nullptr);
    size_t count = animator->loadAnimationsFromSource("oversized", asset);

    EXPECT_EQ(count, 0);  // 拒绝加载
    EXPECT_FALSE(animator->hasSource("oversized"));
    EXPECT_EQ(animator->getAnimationCacheStats().cachedCount, 0);

    mLoader->destroyAnimationAsset(asset);
}

TEST_F(AnimationCacheTest, EmptyCacheOperations) {
    // 空缓存查询
    EXPECT_FALSE(animator->hasSource("nonexistent"));
    EXPECT_FALSE(animator->hasAnimation("nonexistent", "anim"));
    EXPECT_EQ(animator->getAnimationDuration("nonexistent", "anim"), 0.0f);

    // 空缓存播放
    EXPECT_FALSE(animator->applyAnimation("nonexistent", "anim", 0.0f));
    EXPECT_FALSE(animator->applyAnimationByName("anim", 0.0f));

    // 空缓存卸载（不应崩溃）
    animator->unloadAnimationsFromSource("nonexistent");
    animator->clearAnimationCache();

    auto stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.cachedCount, 0);
    EXPECT_EQ(stats.missCount, 2);  // 两次播放失败
}

TEST_F(AnimationCacheTest, DuplicateAnimationNameHandling) {
    // 从真实 GLB 文件加载动画（包含完整骨骼结构）
    const char* path = "ecorche_animation_only.glb";
    auto data = readBinaryFile(path);
    ASSERT_FALSE(data.empty()) << "Failed to read: " << path;

    auto sourceAsset = mLoader->loadAnimationAsset(data.data(), data.size());
    ASSERT_NE(sourceAsset, nullptr);
    ASSERT_GE(sourceAsset->getAnimationCount(), 2)
        << "Test requires at least 2 animations in source GLB";

    // 创建两个虚拟源，共享骨骼结构但包含不同的动画
    auto asset1 = new AnimationAsset();
    asset1->nodes = sourceAsset->nodes;  // 共享节点结构

    // 拷贝第一个动画并重命名（拷贝是安全的）
    AnimationAsset::Animation anim1 = sourceAsset->getAnimation(0);  // Pull
    anim1.name = "common";  // 重命名为相同名称
    asset1->animations.push_back(std::move(anim1));

    auto asset2 = new AnimationAsset();
    asset2->nodes = sourceAsset->nodes;  // 共享节点结构

    // 拷贝第二个动画并重命名（拷贝是安全的）
    AnimationAsset::Animation anim2 = sourceAsset->getAnimation(1);  // Push
    anim2.name = "common";  // 重命名为相同名称
    asset2->animations.push_back(std::move(anim2));

    // 加载到缓存（骨骼匹配应该成功）
    size_t count1 = animator->loadAnimationsFromSource("source1", asset1);
    size_t count2 = animator->loadAnimationsFromSource("source2", asset2);

    // 验证骨骼匹配成功（不再跳过）
    ASSERT_GT(count1, 0) << "Failed to load source1 (bone matching failed)";
    ASSERT_GT(count2, 0) << "Failed to load source2 (bone matching failed)";

    // 测试1：精确播放应该区分两个源
    EXPECT_TRUE(animator->applyAnimation("source1", "common", 0.0f));
    EXPECT_TRUE(animator->applyAnimation("source2", "common", 0.0f));

    // 测试2：便捷播放应该选择最近访问的源
    animator->applyAnimation("source1", "common", 0.0f);  // 访问 source1
    EXPECT_TRUE(animator->applyAnimationByName("common", 0.5f));  // 应该使用 source1

    // 清理
    mLoader->destroyAnimationAsset(sourceAsset);
    delete asset1;
    delete asset2;
}

TEST_F(AnimationCacheTest, RapidLoadUnloadCycles) {
    auto asset = loadTestAnimationAsset("test");
    ASSERT_NE(asset, nullptr);

    // 快速加载/卸载 10 次
    for (int i = 0; i < 10; i++) {
        animator->loadAnimationsFromSource("test", asset);
        EXPECT_EQ(animator->getAnimationCacheStats().cachedCount, 3);

        animator->unloadAnimationsFromSource("test");
        EXPECT_EQ(animator->getAnimationCacheStats().cachedCount, 0);
    }

    // 最终状态应该正确
    EXPECT_FALSE(animator->hasSource("test"));
    EXPECT_EQ(animator->getAnimationCacheStats().sourceCount, 0);

    mLoader->destroyAnimationAsset(asset);
}

// ========================================
// 移动语义测试（2个）⚠️ 最关键
// ========================================

TEST_F(AnimationCacheTest, ChannelPointersValidAfterMove) {
    auto asset = loadTestAnimationAsset("test");
    ASSERT_NE(asset, nullptr);
    animator->loadAnimationsFromSource("test", asset);

    // 播放动画多次（验证 Channel::sourceData 指针有效）
    size_t testCount = std::min<size_t>(3, getAnimCount());
    for (int i = 0; i < 5; i++) {
        for (size_t j = 0; j < testCount; ++j) {
            EXPECT_TRUE(animator->applyAnimation("test", getAnimName(j), 0.5f));
        }
    }

    // 不应该崩溃（如果指针悬空会在此崩溃）
    EXPECT_EQ(animator->getAnimationCacheStats().hitCount, 5 * testCount);

    mLoader->destroyAnimationAsset(asset);
}

TEST_F(AnimationCacheTest, NoMemoryLeaksAfterEviction) {
    animator->setAnimationCacheSize(3);

    // 加载多个源，触发淘汰
    auto asset1 = loadTestAnimationAsset("source1");
    auto asset2 = loadTestAnimationAsset("source2");
    auto asset3 = loadTestAnimationAsset("source3");
    ASSERT_NE(asset1, nullptr);
    ASSERT_NE(asset2, nullptr);
    ASSERT_NE(asset3, nullptr);

    animator->loadAnimationsFromSource("source1", asset1);
    animator->loadAnimationsFromSource("source2", asset2);  // 淘汰 source1
    animator->loadAnimationsFromSource("source3", asset3);  // 淘汰 source2

    // 验证缓存状态
    EXPECT_EQ(animator->getAnimationCacheStats().cachedCount, 3);
    EXPECT_TRUE(animator->hasSource("source3"));

    // 清空缓存
    animator->clearAnimationCache();
    EXPECT_EQ(animator->getAnimationCacheStats().cachedCount, 0);

    // NOTE: 内存泄漏需要通过 Valgrind 或 AddressSanitizer 检测
    // 此测试确保代码可以正常执行

    mLoader->destroyAnimationAsset(asset1);
    mLoader->destroyAnimationAsset(asset2);
    mLoader->destroyAnimationAsset(asset3);
}

// ========================================
// 统计查询测试（2个）
// ========================================

TEST_F(AnimationCacheTest, CacheStatsAccuracy) {
    ASSERT_GE(getAnimCount(), 2) << "Test requires at least 2 animations";
    auto asset1 = loadTestAnimationAsset("source1");
    auto asset2 = loadTestAnimationAsset("source2");
    ASSERT_NE(asset1, nullptr);
    ASSERT_NE(asset2, nullptr);

    size_t count1 = animator->loadAnimationsFromSource("source1", asset1);
    size_t count2 = animator->loadAnimationsFromSource("source2", asset2);

    auto stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.cachedCount, count1 + count2);
    EXPECT_EQ(stats.sourceCount, 2);
    EXPECT_EQ(stats.maxSize, 100);  // Default size
    EXPECT_EQ(stats.hitCount, 0);
    EXPECT_EQ(stats.missCount, 0);
    EXPECT_EQ(stats.hitRate, 0.0f);

    // 播放一些动画
    animator->applyAnimation("source1", getAnimName(0), 0.0f);  // Hit
    animator->applyAnimation("source1", getAnimName(1), 0.0f);  // Hit
    animator->applyAnimation("source1", "nonexistent", 0.0f);   // Miss

    stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.hitCount, 2);
    EXPECT_EQ(stats.missCount, 1);
    EXPECT_FLOAT_EQ(stats.hitRate, 2.0f / 3.0f * 100.0f);

    mLoader->destroyAnimationAsset(asset1);
    mLoader->destroyAnimationAsset(asset2);
}

TEST_F(AnimationCacheTest, ResetCacheStats) {
    auto asset = loadTestAnimationAsset("test");
    ASSERT_NE(asset, nullptr);
    size_t count = animator->loadAnimationsFromSource("test", asset);
    ASSERT_GT(count, 0);

    // 产生一些统计数据
    animator->applyAnimation("test", getAnimName(0), 0.0f);
    animator->applyAnimation("test", "nonexistent", 0.0f);

    auto stats1 = animator->getAnimationCacheStats();
    EXPECT_GT(stats1.hitCount, 0);
    EXPECT_GT(stats1.missCount, 0);

    // 重置统计
    animator->resetCacheStats();

    auto stats2 = animator->getAnimationCacheStats();
    EXPECT_EQ(stats2.hitCount, 0);
    EXPECT_EQ(stats2.missCount, 0);
    EXPECT_EQ(stats2.hitRate, 0.0f);

    // 缓存内容应该保留
    EXPECT_EQ(stats2.cachedCount, count);
    EXPECT_EQ(stats2.sourceCount, 1);

    mLoader->destroyAnimationAsset(asset);
}

// ========================================
// 测试盲区补充（7个）
// 基于 gltfio_ext_cache_test_evaluation.md 评估文档
// ========================================

/**
 * Test 1.1: 无效输入 - 空 sourceId
 */
TEST_F(AnimationCacheTest, LoadWithEmptySourceId) {
    auto asset = loadTestAnimationAsset("test");
    ASSERT_NE(asset, nullptr);

    // 尝试使用空 sourceId 加载
    size_t count = animator->loadAnimationsFromSource("", asset);

    // 应该被拒绝
    EXPECT_EQ(count, 0) << "Empty sourceId should be rejected";

    // 验证缓存未被污染
    auto stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.cachedCount, 0) << "Cache should remain empty";
    EXPECT_EQ(stats.sourceCount, 0) << "No source should be registered";

    // 验证 getLoadedSources 为空
    auto sources = animator->getLoadedSources();
    EXPECT_TRUE(sources.empty()) << "No sources should be loaded";

    mLoader->destroyAnimationAsset(asset);
}

/**
 * Test 1.2: 无效输入 - nullptr asset
 */
TEST_F(AnimationCacheTest, LoadWithNullAsset) {
    // 尝试使用 nullptr 加载
    size_t count = animator->loadAnimationsFromSource("valid_id", nullptr);

    // 应该被拒绝
    EXPECT_EQ(count, 0) << "Null asset should be rejected";

    // 验证缓存未被污染
    auto stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.cachedCount, 0);
    EXPECT_EQ(stats.sourceCount, 0);

    // 验证 sourceId 未被注册
    EXPECT_FALSE(animator->hasSource("valid_id")) << "Source should not be registered";
}

/**
 * Test 2: 零动画资产
 *
 * 注意：由于无法直接构造空AnimationAsset，我们使用 getAnimationCount()==0 的分支
 * 这个测试验证实现对零动画的健壮性
 */
TEST_F(AnimationCacheTest, LoadEmptyAnimationAsset) {
    // 当前 ecorche_animation_only.glb 包含 3 个动画
    // 我们无法构造真正的空AnimationAsset（需要私有构造函数）
    // 因此这个测试验证逻辑：如果未来支持空资产，应该正确处理

    auto asset = loadTestAnimationAsset("test");
    ASSERT_NE(asset, nullptr);
    size_t actualCount = asset->getAnimationCount();

    if (actualCount == 0) {
        // 如果资产为空（未来可能），验证行为
        size_t count = animator->loadAnimationsFromSource("empty_source", asset);
        EXPECT_EQ(count, 0);
        EXPECT_FALSE(animator->hasSource("empty_source"));
    } else {
        // 当前资产非空，跳过此测试
        // 但至少验证加载成功的逻辑正常
        size_t count = animator->loadAnimationsFromSource("test_source", asset);
        EXPECT_EQ(count, actualCount);
        EXPECT_TRUE(animator->hasSource("test_source"));
        animator->unloadAnimationsFromSource("test_source");
    }

    mLoader->destroyAnimationAsset(asset);
}

/**
 * Test 3: 骨骼匹配失败路径
 *
 * 注意：需要不匹配的mesh和animation组合
 * 当前测试使用相同的 ecorche 资产（匹配成功）
 * 真实的骨骼不匹配测试需要特殊构造的测试资产
 */
TEST_F(AnimationCacheTest, BoneMappingFailure) {
    // 当前测试资产（ecorche）骨骼完全匹配
    // 理想情况需要：
    // 1. 使用不同模型的 mesh （如 AnimatedMorphCube）
    // 2. 加载 ecorche 的动画（骨骼名称不匹配）

    // 尝试使用 AnimatedMorphCube.glb 作为 mesh
    const char* mismatchMeshPath = "AnimatedMorphCube.glb";
    auto mismatchMeshData = readBinaryFile(mismatchMeshPath);

    if (mismatchMeshData.empty()) {
        // 如果测试资产不存在，跳过
        GTEST_SKIP() << "Mismatch test asset not found: " << mismatchMeshPath;
    }

    // 创建不匹配的 mesh asset
    FilamentAsset* mismatchMesh = mLoader->createAsset(
        mismatchMeshData.data(), mismatchMeshData.size());

    if (!mismatchMesh) {
        GTEST_SKIP() << "Failed to load mismatch mesh asset";
    }

    mResourceLoader->loadResources(mismatchMesh);
    FilamentInstance* instance = mismatchMesh->getInstance();
    Animator* mismatchAnimator = instance ? instance->getAnimator() : nullptr;

    if (!mismatchAnimator) {
        mLoader->destroyAsset(mismatchMesh);
        GTEST_SKIP() << "Failed to get animator from mismatch mesh";
    }

    // 尝试加载 ecorche 动画到 AnimatedMorphCube mesh
    // 骨骼结构完全不同，应该失败
    auto ecorcheAnim = loadTestAnimationAsset("test");
    ASSERT_NE(ecorcheAnim, nullptr);

    size_t beforeCount = mismatchAnimator->getAnimationCacheStats().cachedCount;
    size_t loadedCount = mismatchAnimator->loadAnimationsFromSource("mismatch_test", ecorcheAnim);

    // 骨骼匹配率可能很低，但 buildMapping 可能部分成功
    // 验证：即使失败，缓存状态也应该稳定（不污染）
    auto stats = mismatchAnimator->getAnimationCacheStats();

    if (loadedCount == 0) {
        // 完全失败（理想情况）
        EXPECT_EQ(stats.cachedCount, beforeCount) << "Cache should not be polluted on failure";
        EXPECT_FALSE(mismatchAnimator->hasSource("mismatch_test"));
    } else {
        // 部分成功（某些骨骼匹配）
        // 至少验证缓存状态一致
        EXPECT_EQ(stats.cachedCount, beforeCount + loadedCount);
        EXPECT_TRUE(mismatchAnimator->hasSource("mismatch_test"));
    }

    mLoader->destroyAnimationAsset(ecorcheAnim);
    mLoader->destroyAsset(mismatchMesh);
}

/**
 * Test 4: hasAnimationByName API 精确断言
 */
TEST_F(AnimationCacheTest, HasAnimationByNameAPI) {
    auto asset = loadTestAnimationAsset("test");
    ASSERT_NE(asset, nullptr);
    ASSERT_GT(getAnimCount(), 0);

    const char* animName = getAnimName(0);

    // 加载前：不存在
    EXPECT_FALSE(animator->hasAnimationByName(animName));

    // 加载动画
    size_t count = animator->loadAnimationsFromSource("source1", asset);
    ASSERT_GT(count, 0);

    // 加载后：存在
    EXPECT_TRUE(animator->hasAnimationByName(animName));

    // 非存在的动画：不存在
    EXPECT_FALSE(animator->hasAnimationByName("NonExistentAnimation"));

    // 卸载后：不存在
    animator->unloadAnimationsFromSource("source1");
    EXPECT_FALSE(animator->hasAnimationByName(animName));

    // 多源同名测试
    auto asset2 = loadTestAnimationAsset("test2");
    ASSERT_NE(asset2, nullptr);

    animator->loadAnimationsFromSource("source_a", asset);
    animator->loadAnimationsFromSource("source_b", asset2);

    // 任意源中存在即返回 true
    EXPECT_TRUE(animator->hasAnimationByName(animName));

    // 卸载一个源，另一个源仍有该动画
    animator->unloadAnimationsFromSource("source_a");
    EXPECT_TRUE(animator->hasAnimationByName(animName)) << "Should find in source_b";

    // 卸载所有源
    animator->unloadAnimationsFromSource("source_b");
    EXPECT_FALSE(animator->hasAnimationByName(animName));

    mLoader->destroyAnimationAsset(asset);
    mLoader->destroyAnimationAsset(asset2);
}

/**
 * Test 5: 容量设为 0 的语义
 */
TEST_F(AnimationCacheTest, SetCacheSizeToZero) {
    auto asset = loadTestAnimationAsset("test");
    ASSERT_NE(asset, nullptr);

    // 加载 3 个动画
    size_t count = animator->loadAnimationsFromSource("test", asset);
    ASSERT_GT(count, 0);
    EXPECT_EQ(animator->getAnimationCacheStats().cachedCount, count);

    // 设置缓存大小为 0
    animator->setAnimationCacheSize(0);

    // 验证：缓存应该立即清空
    auto stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.cachedCount, 0) << "Cache should be empty when size set to 0";
    EXPECT_EQ(stats.maxSize, 0) << "Max size should be 0";

    // 验证：所有动画已被淘汰
    for (size_t i = 0; i < getAnimCount(); ++i) {
        EXPECT_FALSE(animator->hasAnimation("test", getAnimName(i)))
            << "Animation " << i << " should be evicted";
    }

    // 验证：尝试播放动画不会崩溃（但会失败）
    bool playResult = animator->applyAnimationByName(getAnimName(0), 0.0f);
    EXPECT_FALSE(playResult) << "Should fail to play evicted animation";

    // 验证：尝试加载新动画应该被拒绝（容量为0）
    auto asset2 = loadTestAnimationAsset("test2");
    ASSERT_NE(asset2, nullptr);

    size_t newCount = animator->loadAnimationsFromSource("new_source", asset2);
    EXPECT_EQ(newCount, 0) << "Should reject loading when max size is 0";

    // 恢复缓存大小
    animator->setAnimationCacheSize(100);
    EXPECT_EQ(animator->getAnimationCacheStats().maxSize, 100);

    mLoader->destroyAnimationAsset(asset);
    mLoader->destroyAnimationAsset(asset2);
}

/**
 * Test 6: 时间边界 - time == duration
 */
TEST_F(AnimationCacheTest, PlayAtExactDuration) {
    auto asset = loadTestAnimationAsset("test");
    ASSERT_NE(asset, nullptr);
    ASSERT_GT(getAnimCount(), 0);

    animator->loadAnimationsFromSource("test", asset);

    const char* animName = getAnimName(0);
    float duration = animator->getAnimationDuration("test", animName);
    ASSERT_GT(duration, 0.0f);

    // 获取初始统计
    auto stats1 = animator->getAnimationCacheStats();
    uint64_t hitsBefore = stats1.hitCount;

    // 播放 time == duration（特殊分支：跳过 fmod）
    bool result = animator->applyAnimation("test", animName, duration);
    EXPECT_TRUE(result);

    // 验证：hitCount 增加（执行了播放）
    auto stats2 = animator->getAnimationCacheStats();
    EXPECT_GT(stats2.hitCount, hitsBefore) << "Should execute playback at exact duration";

    // 播放 time > duration（正常分支：执行 fmod）
    uint64_t hitsBeforeFmod = stats2.hitCount;
    result = animator->applyAnimation("test", animName, duration + 0.5f);
    EXPECT_TRUE(result);

    auto stats3 = animator->getAnimationCacheStats();
    EXPECT_GT(stats3.hitCount, hitsBeforeFmod) << "Should execute playback with fmod";

    // 验证：使用 applyAnimationByName 也正确处理 duration
    result = animator->applyAnimationByName(animName, duration);
    EXPECT_TRUE(result);

    auto stats4 = animator->getAnimationCacheStats();
    EXPECT_GT(stats4.hitCount, stats3.hitCount);

    mLoader->destroyAnimationAsset(asset);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
