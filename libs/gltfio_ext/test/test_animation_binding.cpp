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
 * AnimationBinding 测试 - 骨骼映射功能测试
 * Tests for AnimationBinding bone mapping functionality
 *
 * 本测试文件验证 AnimationBinding 的核心功能，包括：
 * 1. 骨骼名称映射
 * 2. TransformManager::Instance 获取
 * 3. 映射验证
 * 4. 匹配率计算
 * 5. 容忍度机制
 */

#include <gtest/gtest.h>

#include <gltfio_ext/AnimationBinding.h>
#include <gltfio_ext/AssetLoader.h>
#include <gltfio_ext/AnimationAsset.h>
#include <gltfio_ext/FilamentAsset.h>
#include <gltfio_ext/MaterialProvider.h>

#include <filament/Engine.h>
#include <filament/TransformManager.h>

#include <utils/NameComponentManager.h>
#include <utils/EntityManager.h>

#include "materials/uberarchive.h"

#include <fstream>
#include <vector>

using namespace filament;
using namespace filament::gltfio_ext;
using namespace utils;

/**
 * 测试 Fixture
 * 负责初始化 Filament Engine、AssetLoader 和测试资产
 */
class AnimationBindingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建 Filament Engine
        mEngine = Engine::create(Engine::Backend::NOOP);
        ASSERT_NE(mEngine, nullptr);

        // 创建 NameComponentManager (必须在创建 AssetLoader 之前)
        mNameManager = new NameComponentManager(EntityManager::get());
        ASSERT_NE(mNameManager, nullptr);

        // 创建 MaterialProvider
        mMaterials = createUbershaderProvider(mEngine, UBERARCHIVE_DEFAULT_DATA,
                UBERARCHIVE_DEFAULT_SIZE);
        ASSERT_NE(mMaterials, nullptr);

        // 创建 AssetLoader，并提供 NameComponentManager
        AssetConfiguration config;
        config.engine = mEngine;
        config.materials = mMaterials;
        config.names = mNameManager;  // 关键：提供 NameComponentManager

        mLoader = AssetLoader::create(config);
        ASSERT_NE(mLoader, nullptr);
    }

    void TearDown() override {
        // 清理资源（注意顺序：先销毁使用者，再销毁被使用者）
        if (mLoader) {
            AssetLoader::destroy(&mLoader);
        }
        if (mMaterials) {
            mMaterials->destroyMaterials();
            delete mMaterials;
        }
        if (mNameManager) {
            delete mNameManager;
            mNameManager = nullptr;
        }
        if (mEngine) {
            Engine::destroy(&mEngine);
        }
    }

    /**
     * 从文件加载数据到内存
     */
    std::vector<uint8_t> loadFile(const std::string& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return {};
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(size);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            return {};
        }

        return buffer;
    }

    Engine* mEngine = nullptr;
    MaterialProvider* mMaterials = nullptr;
    AssetLoader* mLoader = nullptr;
    NameComponentManager* mNameManager = nullptr;
};

// ==================================================================================================
// Test Cases
// ==================================================================================================

/**
 * Test 1: 验证骨骼完全匹配时映射成功
 */
TEST_F(AnimationBindingTest, BuildMappingSuccess) {
    // 加载测试资产
    auto meshData = loadFile("ecorche_full.glb");
    auto animData = loadFile("ecorche_animation_only.glb");

    if (meshData.empty() || animData.empty()) {
        FAIL() << "Test files 'ecorche_full.glb' or 'ecorche_animation_only.glb' not found.\n"
               << "Please ensure:\n"
               << "  1. Tests are run from: out/cmake-debug/libs/gltfio_ext/\n"
               << "  2. CMake build succeeded and copied test files\n"
               << "  3. Run: cd out/cmake-debug/libs/gltfio_ext && ./test_animation_binding";
    }

    // 创建资产
    FilamentAsset* meshAsset = mLoader->createAsset(meshData.data(), meshData.size());
    AnimationAsset* animAsset = mLoader->loadAnimationAsset(animData.data(), animData.size());

    if (!meshAsset || !animAsset) {
        if (meshAsset) mLoader->destroyAsset(meshAsset);
        if (animAsset) mLoader->destroyAnimationAsset(animAsset);
        FAIL() << "Failed to load assets from GLB files.\n"
               << "The GLB files may be corrupted or in an unsupported format.\n"
               << "Please verify the test files are valid glTF 2.0 files.";
    }

    // 检查是否有命名实体
    size_t namedEntityCount = 0;
    const Entity* entities = meshAsset->getEntities();
    size_t entityCount = meshAsset->getEntityCount();
    for (size_t i = 0; i < entityCount; i++) {
        if (meshAsset->getName(entities[i]) != nullptr) {
            namedEntityCount++;
        }
    }

    if (namedEntityCount == 0) {
        mLoader->destroyAnimationAsset(animAsset);
        mLoader->destroyAsset(meshAsset);
        FAIL() << "No named entities in mesh, cannot test bone mapping.\n"
               << "AssetConfiguration.names was not configured properly.\n"
               << "NameComponentManager is required for AnimationBinding to work.";
    }

    // 创建 AnimationBinding
    AnimationBinding binding(animAsset, meshAsset, mEngine);

    // 构建映射
    bool success = binding.buildMapping();

    // 验证结果
    EXPECT_TRUE(success) << "Bone mapping should succeed";
    EXPECT_GT(binding.getNodeToEntityMap().size(), 0) << "Should have mapped nodes";
    EXPECT_EQ(binding.getNodeToEntityMap().size(), binding.getNodeToInstanceMap().size())
        << "Entity map and Instance map should have same size";

    // 清理
    mLoader->destroyAnimationAsset(animAsset);
    mLoader->destroyAsset(meshAsset);
}

/**
 * Test 2: 验证 TransformManager::Instance 有效性
 */
TEST_F(AnimationBindingTest, TransformInstanceValid) {
    // 加载测试资产
    auto meshData = loadFile("ecorche_full.glb");
    auto animData = loadFile("ecorche_animation_only.glb");

    if (meshData.empty() || animData.empty()) {
        FAIL() << "Test files 'ecorche_full.glb' or 'ecorche_animation_only.glb' not found.\n"
               << "Please ensure:\n"
               << "  1. Tests are run from: out/cmake-debug/libs/gltfio_ext/\n"
               << "  2. CMake build succeeded and copied test files\n"
               << "  3. Run: cd out/cmake-debug/libs/gltfio_ext && ./test_animation_binding";
    }

    // 创建资产
    FilamentAsset* meshAsset = mLoader->createAsset(meshData.data(), meshData.size());
    AnimationAsset* animAsset = mLoader->loadAnimationAsset(animData.data(), animData.size());

    if (!meshAsset || !animAsset) {
        if (meshAsset) mLoader->destroyAsset(meshAsset);
        if (animAsset) mLoader->destroyAnimationAsset(animAsset);
        FAIL() << "Failed to load assets from GLB files.\n"
               << "The GLB files may be corrupted or in an unsupported format.\n"
               << "Please verify the test files are valid glTF 2.0 files.";
    }

    // 创建 AnimationBinding
    AnimationBinding binding(animAsset, meshAsset, mEngine);
    bool success = binding.buildMapping();

    ASSERT_TRUE(success);

    // 验证所有 TransformManager::Instance 有效
    auto& entityMap = binding.getNodeToEntityMap();
    auto& tm = mEngine->getTransformManager();

    EXPECT_GT(entityMap.size(), 0) << "Should have transform instances";

    for (const auto& [nodeIndex, entity] : entityMap) {
        EXPECT_TRUE(tm.hasComponent(entity))
            << "Entity for node " << nodeIndex << " should have transform component";
    }

    // 验证映射有效性
    EXPECT_TRUE(binding.validateMapping()) << "Mapping validation should pass";

    // 清理
    mLoader->destroyAnimationAsset(animAsset);
    mLoader->destroyAsset(meshAsset);
}

/**
 * Test 3: 验证匹配率计算
 */
TEST_F(AnimationBindingTest, GetMatchRate) {
    // 加载测试资产
    auto meshData = loadFile("ecorche_full.glb");
    auto animData = loadFile("ecorche_animation_only.glb");

    ASSERT_FALSE(meshData.empty());
    ASSERT_FALSE(animData.empty());

    // 创建资产
    FilamentAsset* meshAsset = mLoader->createAsset(meshData.data(), meshData.size());
    AnimationAsset* animAsset = mLoader->loadAnimationAsset(animData.data(), animData.size());

    ASSERT_NE(meshAsset, nullptr);
    ASSERT_NE(animAsset, nullptr);

    // 创建 AnimationBinding
    AnimationBinding binding(animAsset, meshAsset, mEngine);

    // 映射前匹配率应为 0
    EXPECT_FLOAT_EQ(binding.getMatchRate(), 0.0f);

    // 构建映射
    bool success = binding.buildMapping();
    ASSERT_TRUE(success);

    // 映射后匹配率应大于阈值 (90%)
    float matchRate = binding.getMatchRate();
    EXPECT_GE(matchRate, 0.9f) << "Match rate should be >= 90%";
    EXPECT_LE(matchRate, 1.0f) << "Match rate should be <= 100%";

    // 清理
    mLoader->destroyAnimationAsset(animAsset);
    mLoader->destroyAsset(meshAsset);
}

/**
 * Test 4: 验证未匹配骨骼列表
 */
TEST_F(AnimationBindingTest, GetUnmatchedBones) {
    // 加载测试资产
    auto meshData = loadFile("ecorche_full.glb");
    auto animData = loadFile("ecorche_animation_only.glb");

    ASSERT_FALSE(meshData.empty());
    ASSERT_FALSE(animData.empty());

    // 创建资产
    FilamentAsset* meshAsset = mLoader->createAsset(meshData.data(), meshData.size());
    AnimationAsset* animAsset = mLoader->loadAnimationAsset(animData.data(), animData.size());

    ASSERT_NE(meshAsset, nullptr);
    ASSERT_NE(animAsset, nullptr);

    // 创建 AnimationBinding
    AnimationBinding binding(animAsset, meshAsset, mEngine);
    bool success = binding.buildMapping();

    ASSERT_TRUE(success);

    // 获取未匹配骨骼列表
    const auto& unmatchedBones = binding.getUnmatchedBones();

    // 如果匹配率为 100%，则未匹配列表应为空
    if (binding.getMatchRate() >= 1.0f) {
        EXPECT_EQ(unmatchedBones.size(), 0) << "Should have no unmatched bones";
    } else {
        // 否则应该有一些未匹配的骨骼
        EXPECT_GT(unmatchedBones.size(), 0) << "Should have some unmatched bones";

        // 验证未匹配骨骼数量与匹配率一致
        size_t totalNodes = animAsset->nodes.size();
        size_t matchedNodes = binding.getNodeToEntityMap().size();
        size_t expectedUnmatched = totalNodes - matchedNodes;

        EXPECT_EQ(unmatchedBones.size(), expectedUnmatched)
            << "Unmatched bones count should match calculation";
    }

    // 清理
    mLoader->destroyAnimationAsset(animAsset);
    mLoader->destroyAsset(meshAsset);
}

/**
 * Test 5: 验证空输入处理
 */
TEST_F(AnimationBindingTest, HandleNullInputs) {
    // 测试空指针输入
    AnimationBinding binding1(nullptr, nullptr, mEngine);
    EXPECT_FALSE(binding1.buildMapping()) << "Should fail with null animation asset";

    AnimationBinding binding2(nullptr, nullptr, nullptr);
    EXPECT_FALSE(binding2.buildMapping()) << "Should fail with null engine";
}

/**
 * Test 6: 验证映射一致性
 */
TEST_F(AnimationBindingTest, MappingConsistency) {
    // 加载测试资产
    auto meshData = loadFile("ecorche_full.glb");
    auto animData = loadFile("ecorche_animation_only.glb");

    ASSERT_FALSE(meshData.empty());
    ASSERT_FALSE(animData.empty());

    // 创建资产
    FilamentAsset* meshAsset = mLoader->createAsset(meshData.data(), meshData.size());
    AnimationAsset* animAsset = mLoader->loadAnimationAsset(animData.data(), animData.size());

    ASSERT_NE(meshAsset, nullptr);
    ASSERT_NE(animAsset, nullptr);

    // 创建 AnimationBinding 并构建映射
    AnimationBinding binding(animAsset, meshAsset, mEngine);
    bool success = binding.buildMapping();

    ASSERT_TRUE(success);

    // 验证 entity map 和 instance map 的一致性
    const auto& entityMap = binding.getNodeToEntityMap();
    const auto& instanceMap = binding.getNodeToInstanceMap();

    EXPECT_EQ(entityMap.size(), instanceMap.size())
        << "Entity map and instance map should have same size";

    // 验证每个节点索引在两个 map 中都存在
    auto& tm = mEngine->getTransformManager();
    for (const auto& [nodeIndex, entity] : entityMap) {
        EXPECT_NE(instanceMap.find(nodeIndex), instanceMap.end())
            << "Node " << nodeIndex << " should exist in instance map";

        // 验证 instance 对应的 entity 一致
        auto instance = instanceMap.at(nodeIndex);
        auto entityFromInstance = tm.getInstance(entity);
        EXPECT_EQ(instance, entityFromInstance)
            << "Instance should match entity's transform instance";
    }

    // 清理
    mLoader->destroyAnimationAsset(animAsset);
    mLoader->destroyAsset(meshAsset);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
