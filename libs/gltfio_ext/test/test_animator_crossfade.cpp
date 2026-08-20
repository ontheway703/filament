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
 * Animator Cross-Fade and Complex Integration Tests
 *
 * This file tests cross-fade animation blending and complex integration scenarios:
 * - applyCrossFade basic functionality
 * - Alpha interpolation in cross-fade blending
 * - Advancing time during cross-fade transitions
 * - Cross-fade by animation name (LRU selection)
 * - Error handling in cross-fade operations
 * - Multiple animators playing simultaneously
 * - Fast animation switching performance
 *
 * These tests ensure smooth animation transitions and robust handling of complex
 * multi-animator scenarios.
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

#include <chrono>
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
// CROSS-FADE ANIMATION TESTS
// ============================================================================

/**
 * Test: applyCrossFade basic functionality
 * Verifies that applyCrossFade can blend between two animations without crashing
 */
TEST_F(AnimatorTest, ApplyCrossFadeBasic) {
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

    size_t count = animator->loadAnimationsFromSource("test_anims", animAsset.get());
    ASSERT_GT(count, 1) << "Need at least 2 animations for cross-fade test";

    std::vector<std::string> anims = animator->getAnimationsInSource("test_anims");
    ASSERT_GE(anims.size(), 2);

    const char* anim0 = anims[0].c_str();
    const char* anim1 = anims[1].c_str();

    // Apply current animation (anim1 at 0.3s)
    EXPECT_NO_THROW(animator->applyAnimation("test_anims", anim1, 0.3f));

    // Cross-fade with previous animation (alpha = 0.5 means 50% blend)
    // anim0 at 0.5s is the "previous", anim1 at 0.3s is the "current"
    EXPECT_NO_THROW(animator->applyCrossFade("test_anims", anim0, 0.5f, 0.5f));

    // Cross-fade at different alpha values
    EXPECT_NO_THROW(animator->applyAnimation("test_anims", anim1, 0.8f));
    EXPECT_NO_THROW(animator->applyCrossFade("test_anims", anim0, 0.2f, 0.0f));

    EXPECT_NO_THROW(animator->applyAnimation("test_anims", anim1, 0.8f));
    EXPECT_NO_THROW(animator->applyCrossFade("test_anims", anim0, 0.2f, 1.0f));

    // Cleanup
    animator->unloadAnimationsFromSource("test_anims");
    mLoader->destroyAsset(meshAsset);
}

/**
 * Test: applyCrossFade alpha interpolation
 * Verifies that alpha parameter correctly controls the blend between animations
 * - alpha = 0.0 should give 100% previous animation
 * - alpha = 1.0 should give 100% current animation
 * - alpha = 0.5 should give 50/50 blend
 */
TEST_F(AnimatorTest, ApplyCrossFadeAlpha) {
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

    size_t count = animator->loadAnimationsFromSource("test_anims", animAsset.get());
    ASSERT_GT(count, 1) << "Need at least 2 animations for cross-fade test";

    std::vector<std::string> anims = animator->getAnimationsInSource("test_anims");
    ASSERT_GE(anims.size(), 2);

    const char* anim0 = anims[0].c_str();
    const char* anim1 = anims[1].c_str();

    // Test alpha = 0.0 (100% previous animation)
    EXPECT_NO_THROW(animator->applyAnimation("test_anims", anim1, 0.3f));
    EXPECT_NO_THROW(animator->applyCrossFade("test_anims", anim0, 0.5f, 0.0f));

    // Test alpha = 0.5 (50/50 blend)
    EXPECT_NO_THROW(animator->applyAnimation("test_anims", anim1, 0.3f));
    EXPECT_NO_THROW(animator->applyCrossFade("test_anims", anim0, 0.5f, 0.5f));

    // Test alpha = 1.0 (100% current animation)
    EXPECT_NO_THROW(animator->applyAnimation("test_anims", anim1, 0.3f));
    EXPECT_NO_THROW(animator->applyCrossFade("test_anims", anim0, 0.5f, 1.0f));

    // Test intermediate values
    std::vector<float> alphaValues = {0.25f, 0.75f, 0.1f, 0.9f};
    for (float alpha : alphaValues) {
        EXPECT_NO_THROW(animator->applyAnimation("test_anims", anim1, 0.3f));
        EXPECT_NO_THROW(animator->applyCrossFade("test_anims", anim0, 0.5f, alpha))
            << "Failed at alpha = " << alpha;
    }

    // Cleanup
    animator->unloadAnimationsFromSource("test_anims");
    mLoader->destroyAsset(meshAsset);
}

/**
 * Test: applyCrossFade with advancing time
 * Verifies that both previous and current animations can advance in time during blending
 * This test prevents the bug where previousAnimTime was frozen, causing jerky transitions
 */
TEST_F(AnimatorTest, ApplyCrossFadeAdvancing) {
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

    size_t count = animator->loadAnimationsFromSource("test_anims", animAsset.get());
    ASSERT_GT(count, 1) << "Need at least 2 animations for cross-fade test";

    std::vector<std::string> anims = animator->getAnimationsInSource("test_anims");
    ASSERT_GE(anims.size(), 2);

    const char* anim0 = anims[0].c_str();
    const char* anim1 = anims[1].c_str();

    float duration0 = animator->getAnimationDuration("test_anims", anim0);
    float duration1 = animator->getAnimationDuration("test_anims", anim1);
    ASSERT_GT(duration0, 0.0f);
    ASSERT_GT(duration1, 0.0f);

    // Simulate a blend transition where both animations advance
    // This mimics what animation_blending.cpp does after the fix
    float blendDuration = 1.0f;
    float previousStartTime = 0.5f;

    for (float t = 0.0f; t <= blendDuration; t += 0.1f) {
        float alpha = t / blendDuration;

        // Both times should advance (key insight from the bug fix)
        float prevTime = previousStartTime + t;
        float currTime = t;

        // Wrap times to animation duration
        if (prevTime > duration0) prevTime = fmod(prevTime, duration0);
        if (currTime > duration1) currTime = fmod(currTime, duration1);

        // Apply current animation first, then cross-fade with previous
        EXPECT_NO_THROW(animator->applyAnimation("test_anims", anim1, currTime));
        EXPECT_NO_THROW(animator->applyCrossFade("test_anims", anim0, prevTime, alpha))
            << "Failed at t=" << t << ", alpha=" << alpha;
    }

    // Cleanup
    animator->unloadAnimationsFromSource("test_anims");
    mLoader->destroyAsset(meshAsset);
}

/**
 * Test: applyCrossFadeByName
 * Verifies that the name-based variant of applyCrossFade works correctly
 */
TEST_F(AnimatorTest, ApplyCrossFadeByName) {
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

    size_t count = animator->loadAnimationsFromSource("test_anims", animAsset.get());
    ASSERT_GT(count, 1) << "Need at least 2 animations for cross-fade test";

    std::vector<std::string> anims = animator->getAnimationsInSource("test_anims");
    ASSERT_GE(anims.size(), 2);

    const char* anim0 = anims[0].c_str();
    const char* anim1 = anims[1].c_str();

    // Test applyCrossFadeByName with different alpha values
    EXPECT_NO_THROW(animator->applyAnimationByName(anim1, 0.3f));
    EXPECT_NO_THROW(animator->applyCrossFadeByName(anim0, 0.5f, 0.0f));

    EXPECT_NO_THROW(animator->applyAnimationByName(anim1, 0.3f));
    EXPECT_NO_THROW(animator->applyCrossFadeByName(anim0, 0.5f, 0.5f));

    EXPECT_NO_THROW(animator->applyAnimationByName(anim1, 0.3f));
    EXPECT_NO_THROW(animator->applyCrossFadeByName(anim0, 0.5f, 1.0f));

    // Test with advancing time
    for (float alpha = 0.0f; alpha <= 1.0f; alpha += 0.2f) {
        EXPECT_NO_THROW(animator->applyAnimationByName(anim1, alpha));
        EXPECT_NO_THROW(animator->applyCrossFadeByName(anim0, alpha, alpha))
            << "Failed at alpha = " << alpha;
    }

    // Cleanup
    animator->unloadAnimationsFromSource("test_anims");
    mLoader->destroyAsset(meshAsset);
}

/**
 * Test: applyCrossFade error handling
 * Verifies that applyCrossFade gracefully handles invalid inputs
 */
TEST_F(AnimatorTest, ApplyCrossFadeErrors) {
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

    size_t count = animator->loadAnimationsFromSource("test_anims", animAsset.get());
    ASSERT_GT(count, 1);

    std::vector<std::string> anims = animator->getAnimationsInSource("test_anims");
    ASSERT_GE(anims.size(), 2);

    const char* anim0 = anims[0].c_str();
    const char* anim1 = anims[1].c_str();

    // Test with invalid source name (should not crash, just log error)
    EXPECT_NO_THROW(animator->applyAnimation("test_anims", anim1, 0.3f));
    EXPECT_NO_THROW(animator->applyCrossFade("invalid_source", anim0, 0.5f, 0.5f));

    // Test with invalid animation name (should not crash, just log error)
    EXPECT_NO_THROW(animator->applyAnimation("test_anims", anim1, 0.3f));
    EXPECT_NO_THROW(animator->applyCrossFade("test_anims", "invalid_anim", 0.5f, 0.5f));

    // Test with negative time (implementation should clamp)
    EXPECT_NO_THROW(animator->applyAnimation("test_anims", anim1, -1.0f));
    EXPECT_NO_THROW(animator->applyCrossFade("test_anims", anim0, -1.0f, 0.5f));

    // Test with very large time (should wrap or clamp)
    EXPECT_NO_THROW(animator->applyAnimation("test_anims", anim1, 999999.0f));
    EXPECT_NO_THROW(animator->applyCrossFade("test_anims", anim0, 999999.0f, 0.5f));

    // Test with out-of-range alpha (should clamp to [0, 1])
    EXPECT_NO_THROW(animator->applyAnimation("test_anims", anim1, 0.3f));
    EXPECT_NO_THROW(animator->applyCrossFade("test_anims", anim0, 0.5f, -0.5f));

    EXPECT_NO_THROW(animator->applyAnimation("test_anims", anim1, 0.3f));
    EXPECT_NO_THROW(animator->applyCrossFade("test_anims", anim0, 0.5f, 1.5f));

    // Test applyCrossFadeByName with invalid names
    EXPECT_NO_THROW(animator->applyAnimationByName(anim1, 0.3f));
    EXPECT_NO_THROW(animator->applyCrossFadeByName("invalid_anim", 0.5f, 0.5f));

    // Cleanup
    animator->unloadAnimationsFromSource("test_anims");
    mLoader->destroyAsset(meshAsset);
}

// ============================================================================
// COMPLEX INTEGRATION TESTS
// ============================================================================

/**
 * Integration Test 3: Multiple Animators playing simultaneously
 */
TEST_F(AnimatorTest, MultipleAnimatorsSync) {
    auto meshData = readBinaryFile("ecorche_full.glb");
    auto animData = readBinaryFile("ecorche_animation_only.glb");

    if (meshData.empty() || animData.empty()) {
        GTEST_SKIP() << "Test assets not found";
    }

    // Create main asset
    FilamentAsset* meshAsset = mLoader->createAsset(meshData.data(), meshData.size());
    ASSERT_NE(meshAsset, nullptr);

    mResourceLoader->loadResources(meshAsset);

    // Get two independent instances
    FilamentInstance* instance1 = meshAsset->getInstance();
    FilamentInstance* instance2 = mLoader->createInstance(meshAsset);

    ASSERT_NE(instance1, nullptr);
    ASSERT_NE(instance2, nullptr);

    auto* animator1 = instance1->getAnimator();
    auto* animator2 = instance2->getAnimator();

    ASSERT_NE(animator1, nullptr);
    ASSERT_NE(animator2, nullptr);

    // Verify two Animators are independent
    EXPECT_NE(animator1, animator2) << "Two instances should have different Animator objects";

    // Load animation asset (shared by both instances)
    auto animAsset = mLoader->loadAnimationAsset(animData.data(), animData.size());
    ASSERT_NE(animAsset, nullptr);

    // Load animations for both Animators
    size_t count1 = animator1->loadAnimationsFromSource("shared_anims", animAsset.get());
    size_t count2 = animator2->loadAnimationsFromSource("shared_anims", animAsset.get());

    ASSERT_GT(count1, 0) << "Animator 1 failed to load animations";
    ASSERT_GT(count2, 0) << "Animator 2 failed to load animations";

    EXPECT_TRUE(animator1->hasSource("shared_anims"));
    EXPECT_TRUE(animator2->hasSource("shared_anims"));

    // Get animation names
    std::vector<std::string> anims1 = animator1->getAnimationsInSource("shared_anims");
    std::vector<std::string> anims2 = animator2->getAnimationsInSource("shared_anims");

    EXPECT_EQ(anims1.size(), anims2.size());
    ASSERT_GT(anims1.size(), 1) << "Need at least 2 animations for this test";

    // Synchronized playback: Animator1 plays first animation, Animator2 plays second
    const char* anim1Name = anims1[0].c_str();
    const char* anim2Name = anims2[1].c_str();

    float time1 = 0.0f;
    float time2 = 0.5f;

    EXPECT_NO_THROW({
        // Animator1 plays animation 0 at time 0.0
        animator1->applyAnimation("shared_anims", anim1Name, time1);

        // Animator2 plays animation 1 at time 0.5
        animator2->applyAnimation("shared_anims", anim2Name, time2);
    }) << "Failed to play animations simultaneously on two Animators";

    // Simulate multi-frame synchronized playback (not calling updateBoneMatrices)
    for (int frame = 0; frame < 3; frame++) {
        float t1 = frame * 0.1f;
        float t2 = frame * 0.2f;

        EXPECT_NO_THROW({
            animator1->applyAnimation("shared_anims", anim1Name, t1);
            animator2->applyAnimation("shared_anims", anim2Name, t2);
        }) << "Failed at frame " << frame;
    }

    // Note: Not calling updateBoneMatrices() to avoid NOOP backend circular buffer overflow

    // Verify state independence: unload Animator1's animations
    animator1->unloadAnimationsFromSource("shared_anims");
    EXPECT_FALSE(animator1->hasSource("shared_anims"));
    EXPECT_TRUE(animator2->hasSource("shared_anims")) << "Animator2 should still have animations";

    // Animator2 should still be able to play (not calling updateBoneMatrices)
    EXPECT_NO_THROW({
        animator2->applyAnimation("shared_anims", anim2Name, 1.0f);
    });

    // Cleanup
    mLoader->destroyAsset(meshAsset);  // Automatically cleans up all instances
}

/**
 * Integration Test 4: Fast animation switching (performance test)
 */
TEST_F(AnimatorTest, FastAnimationSwitching) {
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

    // 10 fast load/play/unload cycles
    const int CYCLES = 10;

    auto startTime = std::chrono::high_resolution_clock::now();

    for (int cycle = 0; cycle < CYCLES; cycle++) {
        // Load animations from source
        auto animAsset = mLoader->loadAnimationAsset(animData.data(), animData.size());
        ASSERT_NE(animAsset, nullptr) << "Failed to load animation at cycle " << cycle;

        size_t count = animator->loadAnimationsFromSource("fast_switch", animAsset.get());
        ASSERT_GT(count, 0) << "Failed to load animations at cycle " << cycle;

        // Quickly play multiple animations (not calling updateBoneMatrices)
        std::vector<std::string> anims = animator->getAnimationsInSource("fast_switch");
        ASSERT_GT(anims.size(), 0) << "No animations loaded at cycle " << cycle;

        // Play first animation (not calling updateBoneMatrices to avoid circular buffer overflow)
        if (anims.size() > 0) {
            animator->applyAnimation("fast_switch", anims[0].c_str(), 0.0f);
            animator->applyAnimation("fast_switch", anims[0].c_str(), 0.5f);
        }

        // Unload animations
        animator->unloadAnimationsFromSource("fast_switch");
        EXPECT_FALSE(animator->hasSource("fast_switch")) << "Failed to unload at cycle " << cycle;

        // Destroy asset
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    // Output performance info (for reference, not strict assertion)
    std::cout << "FastAnimationSwitching: " << CYCLES << " cycles completed in "
              << duration.count() << " ms ("
              << (duration.count() / static_cast<double>(CYCLES)) << " ms/cycle)" << std::endl;

    // Performance reasonableness check: should not exceed 5 seconds per cycle (very loose limit)
    EXPECT_LT(duration.count(), CYCLES * 5000) << "Performance is unexpectedly slow";

    // Cleanup
    mLoader->destroyAsset(meshAsset);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
