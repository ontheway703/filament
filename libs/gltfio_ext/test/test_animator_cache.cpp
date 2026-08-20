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
 * Animator Cache Management Tests
 *
 * This file tests cache management aspects of the Animator API:
 * - LRU eviction when cache reaches size limit
 * - Cache size management and dynamic resizing
 * - Cache statistics tracking (hits, misses, cached count)
 *
 * These tests ensure proper cache behavior for animation data management.
 */

#include <gtest/gtest.h>

#include <gltfio_ext/Animator.h>
#include <gltfio_ext/AssetLoader.h>
#include <gltfio_ext/AnimationAsset.h>
#include <gltfio_ext/FilamentAsset.h>
#include <gltfio_ext/MaterialProvider.h>
#include <gltfio_ext/ResourceLoader.h>

#include <filament/Engine.h>
#include <filament/TransformManager.h>

#include <utils/NameComponentManager.h>
#include <utils/EntityManager.h>

#include "materials/uberarchive.h"

#include <fstream>
#include <iostream>
#include <vector>

using namespace filament;
using namespace filament::gltfio_ext;
using namespace utils;

/**
 * Read binary file
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
 * Test Fixture
 */
class AnimatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create Filament Engine
        mEngine = Engine::create(Engine::Backend::NOOP);
        ASSERT_NE(mEngine, nullptr);

        // Create NameComponentManager
        mNameManager = new NameComponentManager(EntityManager::get());
        ASSERT_NE(mNameManager, nullptr);

        // Create MaterialProvider
        mMaterials = createUbershaderProvider(mEngine, UBERARCHIVE_DEFAULT_DATA,
                UBERARCHIVE_DEFAULT_SIZE);
        ASSERT_NE(mMaterials, nullptr);

        // Create AssetLoader
        AssetConfiguration config;
        config.engine = mEngine;
        config.materials = mMaterials;
        config.names = mNameManager;

        mLoader = AssetLoader::create(config);
        ASSERT_NE(mLoader, nullptr);

        // Create ResourceLoader
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
     * Helper method: Load GLB file and initialize resources
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

        // Load resources (this sets mResourcesLoaded = true and creates Animator)
        mResourceLoader->loadResources(asset);

        return asset;
    }

    Engine* mEngine = nullptr;
    MaterialProvider* mMaterials = nullptr;
    AssetLoader* mLoader = nullptr;
    ResourceLoader* mResourceLoader = nullptr;
    NameComponentManager* mNameManager = nullptr;
};

// ============================================================================
// CACHE MANAGEMENT TESTS
// ============================================================================

/**
 * Test: Cache LRU eviction when cache is full
 */
TEST_F(AnimatorTest, CacheLRUEviction) {
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

    // First load animations with large cache size
    animator->setAnimationCacheSize(100);

    auto animAsset = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset, nullptr);

    size_t count = animator->loadAnimationsFromSource("character_anims", animAsset.get());
    ASSERT_GT(count, 2) << "Need at least 3 animations for LRU eviction test";

    // Verify all animations loaded
    auto stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.cachedCount, count);

    // Get animation names
    std::vector<std::string> anims = animator->getAnimationsInSource("character_anims");
    ASSERT_GE(anims.size(), 3);

    // Update access order by playing animations
    animator->applyAnimationByName(anims[0].c_str(), 0.0f);  // Oldest
    animator->applyAnimationByName(anims[1].c_str(), 0.0f);  // Middle
    animator->applyAnimationByName(anims[2].c_str(), 0.0f);  // Newest

    // Now reduce cache size to 2 - should trigger LRU eviction
    animator->setAnimationCacheSize(2);

    // Cache should now have only 2 animations (anims[1] and anims[2])
    stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.cachedCount, 2);

    // anims[0] should have been evicted
    // anims[1] and anims[2] should still be in cache
    // Play them to verify they still work
    EXPECT_TRUE(animator->applyAnimationByName(anims[1].c_str(), 0.0f));
    EXPECT_TRUE(animator->applyAnimationByName(anims[2].c_str(), 0.0f));

    // Cleanup
    mLoader->destroyAsset(meshAsset);
}

/**
 * Test: Cache size management
 */
TEST_F(AnimatorTest, CacheSizeManagement) {
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

    // Set cache size to 5
    animator->setAnimationCacheSize(5);

    // Load animations
    auto animAsset = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset, nullptr);

    animator->loadAnimationsFromSource("character_anims", animAsset.get());

    // Get cache stats
    auto stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.maxSize, 5);

    // Change cache size to 10
    animator->setAnimationCacheSize(10);
    stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.maxSize, 10);

    // Cleanup
    mLoader->destroyAsset(meshAsset);
}

/**
 * Test: Cache statistics
 */
TEST_F(AnimatorTest, CacheStatistics) {
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

    // Set cache size
    animator->setAnimationCacheSize(10);

    // Initial stats - cache should be empty
    auto stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.cachedCount, 0);
    EXPECT_EQ(stats.maxSize, 10);
    EXPECT_EQ(stats.hitCount, 0);
    EXPECT_EQ(stats.missCount, 0);

    // Load animations
    auto animAsset = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset, nullptr);

    size_t loadedCount = animator->loadAnimationsFromSource("character_anims", animAsset.get());
    ASSERT_GT(loadedCount, 0);

    // After loading, all animations should be in cache (eager loading)
    stats = animator->getAnimationCacheStats();
    EXPECT_EQ(stats.cachedCount, loadedCount);
    EXPECT_EQ(stats.sourceCount, 1);

    // Get animation names
    std::vector<std::string> anims = animator->getAnimationsInSource("character_anims");
    ASSERT_EQ(anims.size(), loadedCount);

    // Play first animation - already in cache (eager loading)
    animator->applyAnimationByName(anims[0].c_str(), 0.0f);
    stats = animator->getAnimationCacheStats();
    EXPECT_GT(stats.hitCount, 0);  // Hit because already loaded

    // Play same animation again - another cache hit
    size_t prevHits = stats.hitCount;
    animator->applyAnimationByName(anims[0].c_str(), 0.5f);
    stats = animator->getAnimationCacheStats();
    EXPECT_GT(stats.hitCount, prevHits);

    // Cleanup
    mLoader->destroyAsset(meshAsset);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
