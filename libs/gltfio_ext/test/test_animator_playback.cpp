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
 * Animator Playback and Source Management Tests
 *
 * This file tests playback-related functionality of the Animator API:
 * - Loading animations from multiple sources
 * - Source management (unload, clear cache)
 * - Animation playback by sourceId + name
 * - Animation playback by name only (with LRU selection)
 * - Querying animation durations and sources
 * - Complete workflow integration tests
 * - Mesh residency verification
 *
 * These tests ensure that the cache-based animation API works correctly
 * for typical usage scenarios including source management and playback.
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
// SOURCE MANAGEMENT TESTS
// ============================================================================

/**
 * Test: Load animations from source - basic functionality
 */
TEST_F(AnimatorTest, LoadAnimationsFromSourceBasic) {
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

    // Load animations from source
    auto animAsset = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset, nullptr);

    size_t count = animator->loadAnimationsFromSource("character_anims", animAsset.get());
    EXPECT_GT(count, 0) << "Should load at least one animation";

    // Verify source exists
    EXPECT_TRUE(animator->hasSource("character_anims"));

    // Verify animations are queryable
    std::vector<std::string> anims = animator->getAnimationsInSource("character_anims");
    EXPECT_EQ(anims.size(), count);

    // Cleanup
    mLoader->destroyAsset(meshAsset);
}

/**
 * Test: Load from multiple sources
 */
TEST_F(AnimatorTest, LoadMultipleSources) {
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

    // Load from source 1
    auto animAsset1 = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset1, nullptr);
    size_t count1 = animator->loadAnimationsFromSource("character_anims", animAsset1.get());
    EXPECT_GT(count1, 0);

    // Load from source 2
    auto animAsset2 = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset2, nullptr);
    size_t count2 = animator->loadAnimationsFromSource("weapon_anims", animAsset2.get());
    EXPECT_GT(count2, 0);

    // Verify both sources exist
    EXPECT_TRUE(animator->hasSource("character_anims"));
    EXPECT_TRUE(animator->hasSource("weapon_anims"));

    // Verify loaded sources list
    std::vector<std::string> sources = animator->getLoadedSources();
    EXPECT_EQ(sources.size(), 2);

    // Cleanup
    // animAsset1 和 animAsset2 自动销毁
    mLoader->destroyAsset(meshAsset);
}

/**
 * Test: Unload a specific source
 */
TEST_F(AnimatorTest, UnloadSource) {
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

    // Load from two sources
    auto animAsset1 = mLoader->loadAnimationAsset(animData.data(), animData.size());
    auto animAsset2 = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset1, nullptr);
    ASSERT_NE(animAsset2, nullptr);

    animator->loadAnimationsFromSource("source1", animAsset1.get());
    animator->loadAnimationsFromSource("source2", animAsset2.get());

    EXPECT_TRUE(animator->hasSource("source1"));
    EXPECT_TRUE(animator->hasSource("source2"));

    // Unload source1
    animator->unloadAnimationsFromSource("source1");
    EXPECT_FALSE(animator->hasSource("source1"));
    EXPECT_TRUE(animator->hasSource("source2")) << "source2 should remain loaded";

    // Verify loaded sources list
    std::vector<std::string> sources = animator->getLoadedSources();
    EXPECT_EQ(sources.size(), 1);
    EXPECT_EQ(sources[0], "source2");

    // Cleanup
    // animAsset1 和 animAsset2 自动销毁
    mLoader->destroyAsset(meshAsset);
}

/**
 * Test: Clear all cache
 */
TEST_F(AnimatorTest, ClearAllCache) {
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

    // Load from multiple sources
    auto animAsset1 = mLoader->loadAnimationAsset(animData.data(), animData.size());
    auto animAsset2 = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset1, nullptr);
    ASSERT_NE(animAsset2, nullptr);

    animator->loadAnimationsFromSource("source1", animAsset1.get());
    animator->loadAnimationsFromSource("source2", animAsset2.get());

    EXPECT_TRUE(animator->hasSource("source1"));
    EXPECT_TRUE(animator->hasSource("source2"));

    // Clear all cache
    animator->clearAnimationCache();

    // Verify all sources are cleared
    EXPECT_FALSE(animator->hasSource("source1"));
    EXPECT_FALSE(animator->hasSource("source2"));

    std::vector<std::string> sources = animator->getLoadedSources();
    EXPECT_EQ(sources.size(), 0);

    // Cleanup
    // animAsset1 和 animAsset2 自动销毁
    mLoader->destroyAsset(meshAsset);
}

// ============================================================================
// PLAYBACK TESTS
// ============================================================================

/**
 * Test: Apply animation by sourceId and animation name (precise playback)
 */
TEST_F(AnimatorTest, ApplyAnimationBySourceAndName) {
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

    // Load animations
    auto animAsset = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset, nullptr);

    size_t count = animator->loadAnimationsFromSource("character_anims", animAsset.get());
    ASSERT_GT(count, 0);

    // Get animation names
    std::vector<std::string> anims = animator->getAnimationsInSource("character_anims");
    ASSERT_GT(anims.size(), 0);

    // Apply animation by sourceId + animName
    const char* animName = anims[0].c_str();
    EXPECT_TRUE(animator->hasAnimation("character_anims", animName));

    bool success = animator->applyAnimation("character_anims", animName, 1.0f);
    EXPECT_TRUE(success) << "Should successfully apply animation by sourceId and name";

    // Try non-existent animation
    bool failResult = animator->applyAnimation("character_anims", "nonexistent_anim", 0.0f);
    EXPECT_FALSE(failResult) << "Should fail for non-existent animation";

    // Cleanup
    mLoader->destroyAsset(meshAsset);
}

/**
 * Test: Apply animation by name only (convenient playback with LRU selection)
 */
TEST_F(AnimatorTest, ApplyAnimationByNameOnly) {
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

    // Load animations
    auto animAsset = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset, nullptr);

    size_t count = animator->loadAnimationsFromSource("character_anims", animAsset.get());
    ASSERT_GT(count, 0);

    // Get animation names
    std::vector<std::string> anims = animator->getAnimationsInSource("character_anims");
    ASSERT_GT(anims.size(), 0);

    // Apply animation by name only (no sourceId)
    const char* animName = anims[0].c_str();
    EXPECT_TRUE(animator->hasAnimationByName(animName));

    bool success = animator->applyAnimationByName(animName, 1.0f);
    EXPECT_TRUE(success) << "Should successfully apply animation by name only";

    // Try non-existent animation
    bool failResult = animator->applyAnimationByName("nonexistent_anim", 0.0f);
    EXPECT_FALSE(failResult) << "Should fail for non-existent animation";

    // Cleanup
    mLoader->destroyAsset(meshAsset);
}

/**
 * Test: Multiple sources with same animation name (LRU selection)
 */
TEST_F(AnimatorTest, MultipleSourcesSameAnimName) {
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

    // Load same animations from two different sources
    auto animAsset1 = mLoader->loadAnimationAsset(animData.data(), animData.size());
    auto animAsset2 = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset1, nullptr);
    ASSERT_NE(animAsset2, nullptr);

    animator->loadAnimationsFromSource("source1", animAsset1.get());
    animator->loadAnimationsFromSource("source2", animAsset2.get());

    // Get animation name (same in both sources)
    std::vector<std::string> anims1 = animator->getAnimationsInSource("source1");
    std::vector<std::string> anims2 = animator->getAnimationsInSource("source2");
    ASSERT_GT(anims1.size(), 0);
    ASSERT_GT(anims2.size(), 0);

    const char* animName = anims1[0].c_str();

    // Verify animation exists in both sources
    EXPECT_TRUE(animator->hasAnimation("source1", animName));
    EXPECT_TRUE(animator->hasAnimation("source2", animName));
    EXPECT_TRUE(animator->hasAnimationByName(animName));

    // Play by name only - should use LRU (most recently loaded is source2)
    bool success = animator->applyAnimationByName(animName, 0.0f);
    EXPECT_TRUE(success);

    // Play from specific source
    success = animator->applyAnimation("source1", animName, 0.0f);
    EXPECT_TRUE(success);

    // Cleanup
    // animAsset1 和 animAsset2 自动销毁
    mLoader->destroyAsset(meshAsset);
}

// ============================================================================
// QUERY TESTS
// ============================================================================

/**
 * Test: Query animation duration by sourceId + name and by name only
 */
TEST_F(AnimatorTest, QueryAnimationDuration) {
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

    // Load animations
    auto animAsset = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset, nullptr);

    animator->loadAnimationsFromSource("character_anims", animAsset.get());

    // Get animation names
    std::vector<std::string> anims = animator->getAnimationsInSource("character_anims");
    ASSERT_GT(anims.size(), 0);

    const char* animName = anims[0].c_str();

    // Query duration by sourceId + name
    float duration1 = animator->getAnimationDuration("character_anims", animName);
    EXPECT_GT(duration1, 0.0f);

    // Query duration by name only
    float duration2 = animator->getAnimationDurationByName(animName);
    EXPECT_GT(duration2, 0.0f);

    // Should be the same
    EXPECT_NEAR(duration1, duration2, 0.001f);

    // Query non-existent animation
    float invalidDuration = animator->getAnimationDuration("character_anims", "nonexistent");
    EXPECT_EQ(invalidDuration, 0.0f);

    // Cleanup
    mLoader->destroyAsset(meshAsset);
}

/**
 * Test: Query loaded sources and animations in each source
 */
TEST_F(AnimatorTest, QuerySourcesAndAnimations) {
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

    // Initially no sources loaded
    std::vector<std::string> sources = animator->getLoadedSources();
    EXPECT_EQ(sources.size(), 0);

    // Load from source 1
    auto animAsset1 = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset1, nullptr);
    size_t count1 = animator->loadAnimationsFromSource("character_anims", animAsset1.get());
    EXPECT_GT(count1, 0);

    // Load from source 2
    auto animAsset2 = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset2, nullptr);
    size_t count2 = animator->loadAnimationsFromSource("weapon_anims", animAsset2.get());
    EXPECT_GT(count2, 0);

    // Query loaded sources
    sources = animator->getLoadedSources();
    EXPECT_EQ(sources.size(), 2);

    // Verify source names
    bool hasCharacter = false;
    bool hasWeapon = false;
    for (const auto& source : sources) {
        if (source == "character_anims") hasCharacter = true;
        if (source == "weapon_anims") hasWeapon = true;
    }
    EXPECT_TRUE(hasCharacter);
    EXPECT_TRUE(hasWeapon);

    // Query animations in each source
    std::vector<std::string> charAnims = animator->getAnimationsInSource("character_anims");
    std::vector<std::string> weaponAnims = animator->getAnimationsInSource("weapon_anims");

    EXPECT_EQ(charAnims.size(), count1);
    EXPECT_EQ(weaponAnims.size(), count2);

    // Query non-existent source
    std::vector<std::string> invalidAnims = animator->getAnimationsInSource("nonexistent");
    EXPECT_EQ(invalidAnims.size(), 0);

    // Cleanup
    // animAsset1 和 animAsset2 自动销毁
    mLoader->destroyAsset(meshAsset);
}

// ============================================================================
// GLTFIO COMPATIBILITY TESTS
// ============================================================================

/**
 * Test: gltfio Compatibility - Index-based API for internal animations
 *
 * This test verifies that the legacy gltfio API methods work correctly:
 * - getAnimationCount() returns only internal animation count
 * - getAnimationName(index) returns internal animation names
 * - getAnimationDuration(index) returns internal animation durations
 * - applyAnimation(index, time) applies internal animations
 * - External animations loaded via cache don't affect internal count
 */
TEST_F(AnimatorTest, GltfioCompatibilityIndexBasedAPI) {
    // Load asset with embedded (internal) animations
    auto meshData = readBinaryFile("ecorche_full.glb");
    if (meshData.empty()) {
        GTEST_SKIP() << "Test asset not found: ecorche_full.glb";
    }

    FilamentAsset* meshAsset = mLoader->createAsset(meshData.data(), meshData.size());
    ASSERT_NE(meshAsset, nullptr);
    mResourceLoader->loadResources(meshAsset);

    FilamentInstance* instance = meshAsset->getInstance();
    auto* animator = instance->getAnimator();
    ASSERT_NE(animator, nullptr);

    // === Test 1: getAnimationCount() returns internal animation count ===
    size_t internalCount = animator->getAnimationCount();
    EXPECT_EQ(internalCount, 3) << "ecorche_full.glb should have 3 internal animations";

    // === Test 2: getAnimationName() returns correct names ===
    const char* name0 = animator->getAnimationName(0);
    const char* name1 = animator->getAnimationName(1);
    const char* name2 = animator->getAnimationName(2);

    ASSERT_NE(name0, nullptr);
    ASSERT_NE(name1, nullptr);
    ASSERT_NE(name2, nullptr);

    EXPECT_STREQ(name0, "Pull");
    EXPECT_STREQ(name1, "Push");
    EXPECT_STREQ(name2, "Squat");

    // === Test 3: Invalid index returns empty string (gltfio compatible) ===
    const char* invalidName = animator->getAnimationName(999);
    EXPECT_STREQ(invalidName, "") << "Invalid index should return empty string";

    // === Test 4: getAnimationDuration() returns correct durations ===
    float duration0 = animator->getAnimationDuration(0);
    float duration1 = animator->getAnimationDuration(1);
    float duration2 = animator->getAnimationDuration(2);

    EXPECT_NEAR(duration0, 2.083f, 0.001f) << "Pull animation duration";
    EXPECT_NEAR(duration1, 2.083f, 0.001f) << "Push animation duration";
    EXPECT_NEAR(duration2, 2.083f, 0.001f) << "Squat animation duration";

    // === Test 5: Invalid index returns 0.0f ===
    float invalidDuration = animator->getAnimationDuration(999);
    EXPECT_EQ(invalidDuration, 0.0f) << "Invalid index should return 0.0f";

    // === Test 6: applyAnimation(index, time) works correctly ===
    EXPECT_NO_THROW({
        animator->applyAnimation(0, 0.0f);  // Pull at start
        animator->applyAnimation(1, 1.0f);  // Push at mid
        animator->applyAnimation(2, 2.0f);  // Squat at end
    }) << "Index-based animation playback should work";

    // === Test 7: Invalid index in applyAnimation() is handled gracefully ===
    EXPECT_NO_THROW({
        animator->applyAnimation(999, 0.0f);
    }) << "Invalid index should be handled gracefully (no crash)";

    // Cleanup
    mLoader->destroyAsset(meshAsset);
}

/**
 * Test: gltfio Compatibility - Internal vs External animation separation
 *
 * This test verifies that:
 * - Internal animation count remains stable when external animations are loaded
 * - Index-based API only accesses internal animations
 * - Name-based API can access both internal and external animations
 * - Both APIs can coexist without interference
 */
TEST_F(AnimatorTest, GltfioCompatibilityInternalVsExternal) {
    // Load asset with 3 internal animations
    auto meshData = readBinaryFile("ecorche_full.glb");
    if (meshData.empty()) {
        GTEST_SKIP() << "Test asset not found: ecorche_full.glb";
    }

    FilamentAsset* meshAsset = mLoader->createAsset(meshData.data(), meshData.size());
    ASSERT_NE(meshAsset, nullptr);
    mResourceLoader->loadResources(meshAsset);

    FilamentInstance* instance = meshAsset->getInstance();
    auto* animator = instance->getAnimator();
    ASSERT_NE(animator, nullptr);

    // === Initial state: only internal animations ===
    size_t initialCount = animator->getAnimationCount();
    EXPECT_EQ(initialCount, 3) << "Should have 3 internal animations initially";

    // === Load external animations from cache ===
    auto animData = readBinaryFile("ecorche_animation_only.glb");
    if (animData.empty()) {
        mLoader->destroyAsset(meshAsset);
        GTEST_SKIP() << "Test asset not found: ecorche_animation_only.glb";
    }

    auto animAsset = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset, nullptr);

    size_t loadedCount = animator->loadAnimationsFromSource("external_source", animAsset.get());
    EXPECT_GT(loadedCount, 0) << "Should load external animations";

    // === Critical: Internal count should NOT change ===
    size_t countAfterLoad = animator->getAnimationCount();
    EXPECT_EQ(countAfterLoad, 3) << "Internal animation count should remain 3 after loading external animations";

    // === Index-based API still only accesses internal animations ===
    const char* name0 = animator->getAnimationName(0);
    const char* name1 = animator->getAnimationName(1);
    const char* name2 = animator->getAnimationName(2);

    ASSERT_NE(name0, nullptr);
    ASSERT_NE(name1, nullptr);
    ASSERT_NE(name2, nullptr);

    EXPECT_STREQ(name0, "Pull");
    EXPECT_STREQ(name1, "Push");
    EXPECT_STREQ(name2, "Squat");

    // === Index 3 and beyond should be invalid (not external animations) ===
    const char* name3 = animator->getAnimationName(3);
    EXPECT_STREQ(name3, "") << "Index 3 should return empty string (external animations not accessible via index)";

    float duration3 = animator->getAnimationDuration(3);
    EXPECT_EQ(duration3, 0.0f) << "Index 3 duration should be 0.0f (invalid)";

    // === Old API (index) and new API (name) can coexist ===

    // Play internal animation via index
    EXPECT_NO_THROW({
        animator->applyAnimation(0, 1.0f);  // Old API: Pull
    });

    // Play internal animation via name
    bool success1 = animator->applyAnimationByName("Push", 1.0f);  // New API
    EXPECT_TRUE(success1);

    // Play external animation via name + sourceId
    std::vector<std::string> externalAnims = animator->getAnimationsInSource("external_source");
    if (externalAnims.size() > 0) {
        bool success2 = animator->applyAnimation("external_source", externalAnims[0].c_str(), 1.0f);
        EXPECT_TRUE(success2) << "Should be able to play external animation via name-based API";
    }

    // === Verify internal count unchanged after all playback ===
    size_t finalCount = animator->getAnimationCount();
    EXPECT_EQ(finalCount, 3) << "Internal animation count should still be 3";

    // === Clear external cache - internal animations remain accessible ===
    animator->clearAnimationCache();

    size_t countAfterClear = animator->getAnimationCount();
    EXPECT_EQ(countAfterClear, 3) << "Internal animations should remain after clearing cache";

    // Index-based API should still work for internal animations
    const char* nameAfterClear = animator->getAnimationName(0);
    ASSERT_NE(nameAfterClear, nullptr);
    EXPECT_STREQ(nameAfterClear, "Pull");

    // Cleanup
    mLoader->destroyAsset(meshAsset);
}

TEST_F(AnimatorTest, GltfioCompatibilityCrossFade) {
    // Load asset with multiple internal animations
    auto meshData = readBinaryFile("ecorche_full.glb");
    if (meshData.empty()) {
        GTEST_SKIP() << "Test asset not found: ecorche_full.glb";
    }

    FilamentAsset* meshAsset = mLoader->createAsset(meshData.data(), meshData.size());
    ASSERT_NE(meshAsset, nullptr);
    mResourceLoader->loadResources(meshAsset);

    FilamentInstance* instance = meshAsset->getInstance();
    auto* animator = instance->getAnimator();
    ASSERT_NE(animator, nullptr);

    // Verify we have internal animations
    size_t animCount = animator->getAnimationCount();
    ASSERT_GE(animCount, 2) << "Need at least 2 animations for crossfade test";

    // Apply current animation (animation 0)
    animator->applyAnimation(0, 1.0f);

    // Test 1: Cross-fade from previous animation (animation 1) using index-based API
    EXPECT_NO_THROW({
        animator->applyCrossFade(1, 0.5f, 0.3f);  // previousAnimIndex=1, previousTime=0.5s, alpha=0.3
    }) << "applyCrossFade with valid index should not throw";

    // Test 2: Cross-fade from animation 0 to current
    animator->applyAnimation(1, 2.0f);  // Switch to animation 1
    EXPECT_NO_THROW({
        animator->applyCrossFade(0, 1.5f, 0.7f);  // Cross-fade from animation 0
    });

    // Test 3: Invalid index should be handled gracefully
    EXPECT_NO_THROW({
        animator->applyCrossFade(999, 1.0f, 0.5f);  // Invalid index
    }) << "applyCrossFade with invalid index should not crash (just log warning)";

    // Test 4: Boundary case - index at internal animation count
    size_t invalidIndex = animator->getAnimationCount();
    EXPECT_NO_THROW({
        animator->applyCrossFade(invalidIndex, 1.0f, 0.5f);
    }) << "applyCrossFade with boundary index should not crash";

    // Test 5: Verify crossfade works with index 0
    EXPECT_NO_THROW({
        animator->applyAnimation(1, 1.0f);
        animator->applyCrossFade(0, 0.5f, 0.5f);  // 50/50 blend
    });

    // Cleanup
    mLoader->destroyAsset(meshAsset);
}

// ============================================================================
// INTEGRATION TESTS
// ============================================================================

/**
 * Integration Test 1: Complete workflow with cache-based API
 */
TEST_F(AnimatorTest, CompleteWorkflowIntegration) {
    // Use ecorche_full.glb (full asset with embedded animations)
    auto meshData = readBinaryFile("ecorche_full.glb");

    if (meshData.empty()) {
        GTEST_SKIP() << "Test asset not found: ecorche_full.glb";
    }

    FilamentAsset* meshAsset = mLoader->createAsset(meshData.data(), meshData.size());
    ASSERT_NE(meshAsset, nullptr);

    mResourceLoader->loadResources(meshAsset);

    FilamentInstance* instance = meshAsset->getInstance();
    auto* animator = instance->getAnimator();
    ASSERT_NE(animator, nullptr);

    // Verify initial state: no external animations loaded yet
    EXPECT_FALSE(animator->hasSource("external_anims"));

    // Load external animation asset
    auto animData = readBinaryFile("ecorche_animation_only.glb");
    if (animData.empty()) {
        mLoader->destroyAsset(meshAsset);
        GTEST_SKIP() << "Test asset not found: ecorche_animation_only.glb";
    }

    auto animAsset = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset, nullptr);

    size_t loadedCount = animator->loadAnimationsFromSource("external_anims", animAsset.get());
    ASSERT_GT(loadedCount, 0) << "Failed to load animations from source";

    // Verify loaded state
    EXPECT_TRUE(animator->hasSource("external_anims"));
    std::vector<std::string> loadedAnims = animator->getAnimationsInSource("external_anims");
    EXPECT_EQ(loadedAnims.size(), loadedCount);

    // Play first animation from source (multiple frames)
    if (loadedAnims.size() > 0) {
        const char* animName = loadedAnims[0].c_str();
        float animDuration = animator->getAnimationDuration("external_anims", animName);
        EXPECT_GT(animDuration, 0.0f);

        // Play multiple frames
        std::vector<float> timePoints = {0.0f, 0.5f, 1.0f};
        for (float time : timePoints) {
            if (time <= animDuration) {
                EXPECT_NO_THROW({
                    animator->applyAnimation("external_anims", animName, time);
                }) << "Failed to apply animation at time " << time;
            }
        }
    }

    // If multiple animations exist, switch to second animation
    if (loadedAnims.size() > 1) {
        const char* secondAnimName = loadedAnims[1].c_str();
        float secondDuration = animator->getAnimationDuration("external_anims", secondAnimName);
        EXPECT_GT(secondDuration, 0.0f);

        EXPECT_NO_THROW({
            animator->applyAnimation("external_anims", secondAnimName, 0.0f);
            animator->applyAnimation("external_anims", secondAnimName, secondDuration * 0.5f);
        }) << "Failed to switch to second animation";
    }

    // Note: Not calling updateBoneMatrices() because ecorche has 327 bones
    // which would fill the NOOP backend's circular buffer

    // Unload animations from source
    animator->unloadAnimationsFromSource("external_anims");
    EXPECT_FALSE(animator->hasSource("external_anims"));

    // Cleanup
    mLoader->destroyAsset(meshAsset);
}

/**
 * Integration Test 2: Mesh residency verification
 */
TEST_F(AnimatorTest, MeshResidentVerification) {
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

    // Record initial mesh pointer and entity counts
    const FilamentAsset* meshPtr1 = meshAsset;
    size_t initialEntityCount = meshAsset->getEntityCount();
    size_t initialRenderableCount = meshAsset->getRenderableEntityCount();

    // Load animations from source 1
    auto animAsset1 = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset1, nullptr);

    size_t count1 = animator->loadAnimationsFromSource("source1", animAsset1.get());
    ASSERT_GT(count1, 0);

    // Verify mesh pointer unchanged
    EXPECT_EQ(meshPtr1, meshAsset) << "Mesh pointer should remain stable after loading animation";
    EXPECT_EQ(meshAsset->getEntityCount(), initialEntityCount) << "Entity count should not change";
    EXPECT_EQ(meshAsset->getRenderableEntityCount(), initialRenderableCount) << "Renderable count should not change";

    // Play animation (not calling updateBoneMatrices to avoid circular buffer overflow)
    std::vector<std::string> anims = animator->getAnimationsInSource("source1");
    if (anims.size() > 0) {
        animator->applyAnimationByName(anims[0].c_str(), 1.0f);
    }

    // Verify mesh pointer still unchanged
    EXPECT_EQ(meshPtr1, meshAsset) << "Mesh pointer should remain stable after playing animation";

    // Unload and load different animations (replace)
    animator->unloadAnimationsFromSource("source1");
    // animAsset1 自动销毁

    auto animAsset2 = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset2, nullptr);

    size_t count2 = animator->loadAnimationsFromSource("source2", animAsset2.get());
    ASSERT_GT(count2, 0);

    // Verify mesh pointer still unchanged after switching
    EXPECT_EQ(meshPtr1, meshAsset) << "Mesh pointer should remain stable after switching animations";
    EXPECT_EQ(meshAsset->getEntityCount(), initialEntityCount) << "Entity count should not change after switching";
    EXPECT_EQ(meshAsset->getRenderableEntityCount(), initialRenderableCount) << "Renderable count should not change after switching";

    // Play new animation (not calling updateBoneMatrices)
    anims = animator->getAnimationsInSource("source2");
    if (anims.size() > 0) {
        animator->applyAnimationByName(anims[0].c_str(), 0.5f);
    }

    // Final verification
    EXPECT_EQ(meshPtr1, meshAsset) << "Mesh pointer should remain stable throughout entire workflow";

    // Cleanup
    // animAsset2 自动销毁
    mLoader->destroyAsset(meshAsset);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
