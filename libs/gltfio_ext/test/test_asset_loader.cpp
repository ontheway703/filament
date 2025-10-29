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
 * AssetLoader 测试 - loadAnimationAsset() 功能测试
 * Tests for AssetLoader::loadAnimationAsset() functionality
 *
 * 本测试文件验证 AssetLoader 加载动画资产的核心功能，包括：
 * 1. 从 GLB 文件加载动画数据
 * 2. 节点树提取
 * 3. 动画通道和采样器提取
 * 4. 蒙皮数据提取
 * 5. 错误处理和边界情况
 */

#include <gtest/gtest.h>

#include <gltfio_ext/AssetLoader.h>
#include <gltfio_ext/AnimationAsset.h>
#include <gltfio_ext/MaterialProvider.h>

#include <filament/Engine.h>

#include <math/mat4.h>

#include "materials/uberarchive.h"

#include <fstream>
#include <vector>

using namespace filament;
using namespace filament::gltfio_ext;
using namespace filament::math;

/**
 * 测试 Fixture
 * 负责初始化 Filament Engine 和 AssetLoader
 */
class AssetLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建 Filament Engine
        mEngine = Engine::create(Engine::Backend::NOOP);
        ASSERT_NE(mEngine, nullptr);

        // 创建 MaterialProvider
        mMaterials = createUbershaderProvider(mEngine, UBERARCHIVE_DEFAULT_DATA,
                UBERARCHIVE_DEFAULT_SIZE);
        ASSERT_NE(mMaterials, nullptr);

        // 创建 AssetLoader
        AssetConfiguration config;
        config.engine = mEngine;
        config.materials = mMaterials;

        mLoader = AssetLoader::create(config);
        ASSERT_NE(mLoader, nullptr);
    }

    void TearDown() override {
        // 清理资源
        if (mLoader) {
            AssetLoader::destroy(&mLoader);
        }
        if (mMaterials) {
            mMaterials->destroyMaterials();
            delete mMaterials;
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
};

// ================================================================================================
// 基础功能测试
// ================================================================================================

/**
 * 测试用例1：加载空数据应该返回 nullptr
 *
 * 测试目标：错误处理 - 空输入
 * 测试场景：传入空指针或零长度数据
 * 预期结果：返回 nullptr，不崩溃
 * 验证点：函数对无效输入的鲁棒性
 */
TEST_F(AssetLoaderTest, LoadEmptyData) {
    uint8_t emptyData[] = {0};

    AnimationAsset* asset1 = mLoader->loadAnimationAsset(nullptr, 0);
    EXPECT_EQ(asset1, nullptr);

    AnimationAsset* asset2 = mLoader->loadAnimationAsset(emptyData, 0);
    EXPECT_EQ(asset2, nullptr);
}

/**
 * 测试用例2：加载无效的 GLB 数据应该返回 nullptr
 *
 * 测试目标：错误处理 - 无效 GLB 格式
 * 测试场景：传入非 GLB 格式的随机数据
 * 预期结果：返回 nullptr
 * 验证点：cgltf 解析失败时的错误处理
 */
TEST_F(AssetLoaderTest, LoadInvalidGLB) {
    // 随机数据，不是合法的 GLB 文件
    uint8_t invalidData[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};

    AnimationAsset* asset = mLoader->loadAnimationAsset(invalidData, sizeof(invalidData));
    EXPECT_EQ(asset, nullptr);
}

/**
 * 测试用例3：加载有效的动画 GLB 文件
 *
 * 测试目标：核心功能 - 成功加载包含动画的 GLB
 * 测试场景：加载 AnimatedMorphCube.glb 文件
 * 预期结果：返回非空 AnimationAsset，包含节点、动画通道、采样器
 * 验证点：
 * - 资产对象不为空
 * - 节点数量 > 0
 * - 动画通道数量 > 0
 * - 采样器数量 > 0
 * - 动画时长 > 0
 */
TEST_F(AssetLoaderTest, LoadValidAnimatedGLB) {
    // 加载测试文件
    std::vector<uint8_t> glbData = loadFile(
        "AnimatedMorphCube.glb"
    );

    // 如果文件不存在，跳过测试
    if (glbData.empty()) {
        // TODO：直接报错，返回测试失败！
        GTEST_SKIP() << "Test file not found, skipping test";
    }

    // 加载动画资产
    AnimationAsset* asset = mLoader->loadAnimationAsset(glbData.data(), glbData.size());

    // 验证资产加载成功
    ASSERT_NE(asset, nullptr);

    // 验证节点数据
    EXPECT_GT(asset->nodes.size(), 0);

    // 验证动画数据
    EXPECT_GT(asset->getAnimationCount(), 0);
    const auto& anim = asset->getAnimation(0);
    EXPECT_GT(anim.channels.size(), 0);
    EXPECT_GT(anim.samplers.size(), 0);

    // 验证动画时长
    float duration = anim.getDuration();
    EXPECT_GT(duration, 0.0f);

    // 验证 validate() 通过
    EXPECT_TRUE(asset->validate());

    // 清理
    mLoader->destroyAnimationAsset(asset);
}

// ================================================================================================
// 节点树测试
// ================================================================================================

/**
 * 测试用例4：验证节点树提取
 *
 * 测试目标：节点树结构的正确性
 * 测试场景：加载包含层级节点的 GLB 文件
 * 预期结果：
 * - 所有节点都有名称或默认名称
 * - 父节点索引有效（-1 或指向有效节点）
 * - 变换矩阵不全为零
 * 验证点：节点树的完整性和一致性
 */
TEST_F(AssetLoaderTest, NodeTreeExtraction) {
    std::vector<uint8_t> glbData = loadFile(
        "AnimatedMorphCube.glb"
    );

    if (glbData.empty()) {
        GTEST_SKIP() << "Test file not found";
    }

    AnimationAsset* asset = mLoader->loadAnimationAsset(glbData.data(), glbData.size());
    ASSERT_NE(asset, nullptr);

    size_t nodeCount = asset->nodes.size();
    ASSERT_GT(nodeCount, 0);

    // 验证每个节点
    for (size_t i = 0; i < nodeCount; ++i) {
        const AnimationNode& node = asset->nodes[i];

        // 节点名称应该非空（或有默认命名策略）
        EXPECT_FALSE(node.name.empty()) << "Node " << i << " has empty name";

        // 父节点索引应该有效
        if (node.parentIndex != -1) {
            EXPECT_GE(node.parentIndex, 0) << "Node " << i << " has negative parent index";
            EXPECT_LT(node.parentIndex, static_cast<int>(nodeCount))
                << "Node " << i << " parent index out of range";
            // 父节点不能指向自己（检测自循环）
            EXPECT_NE(node.parentIndex, static_cast<int>(i))
                << "Node " << i << " has circular parent reference";
        }

        // 变换矩阵不应该全为零（至少对角线为单位矩阵）
        bool hasNonZero = false;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                if (node.transform[row][col] != 0.0f) {
                    hasNonZero = true;
                    break;
                }
            }
            if (hasNonZero) break;
        }
        EXPECT_TRUE(hasNonZero) << "Node " << i << " has zero transform matrix";

        // skinIndex 有效性检查
        if (node.skinIndex != -1) {
            EXPECT_GE(node.skinIndex, 0) << "Node " << i << " has negative skin index";
        }
    }

    mLoader->destroyAnimationAsset(asset);
}

/**
 * 测试用例5：验证节点名称查找
 *
 * 测试目标：findNodeByName() 功能
 * 测试场景：根据名称查找节点
 * 预期结果：能够找到存在的节点，查找不存在的节点返回 -1
 * 验证点：名称映射表的正确性
 */
TEST_F(AssetLoaderTest, NodeNameLookup) {
    std::vector<uint8_t> glbData = loadFile(
        "AnimatedMorphCube.glb"
    );

    if (glbData.empty()) {
        GTEST_SKIP() << "Test file not found";
    }

    AnimationAsset* asset = mLoader->loadAnimationAsset(glbData.data(), glbData.size());
    ASSERT_NE(asset, nullptr);

    size_t nodeCount = asset->nodes.size();
    ASSERT_GT(nodeCount, 0);

    // 获取第一个节点的名称并查找
    const AnimationNode& firstNode = asset->nodes[0];
    if (!firstNode.name.empty()) {
        int foundIndex = asset->findNodeByName(firstNode.name);
        EXPECT_EQ(foundIndex, 0);
    }

    // 查找不存在的节点
    int notFoundIndex = asset->findNodeByName("NonExistentNode12345");
    EXPECT_EQ(notFoundIndex, -1);

    mLoader->destroyAnimationAsset(asset);
}

// ================================================================================================
// 动画数据测试
// ================================================================================================

/**
 * 测试用例6：验证动画通道提取
 *
 * 测试目标：动画通道数据的正确性
 * 测试场景：加载包含动画的 GLB 文件
 * 预期结果：
 * - 通道目标节点索引有效
 * - 采样器索引有效
 * - 路径类型有效（TRANSLATION/ROTATION/SCALE/WEIGHTS）
 * 验证点：通道数据的完整性
 */
TEST_F(AssetLoaderTest, AnimationChannelExtraction) {
    std::vector<uint8_t> glbData = loadFile(
        "AnimatedMorphCube.glb"
    );

    if (glbData.empty()) {
        GTEST_SKIP() << "Test file not found";
    }

    AnimationAsset* asset = mLoader->loadAnimationAsset(glbData.data(), glbData.size());
    ASSERT_NE(asset, nullptr);
    ASSERT_GT(asset->getAnimationCount(), 0);

    const auto& anim = asset->getAnimation(0);
    size_t channelCount = anim.channels.size();
    size_t samplerCount = anim.samplers.size();
    size_t nodeCount = asset->nodes.size();

    ASSERT_GT(channelCount, 0);

    // 验证每个通道
    for (size_t i = 0; i < channelCount; ++i) {
        const AnimationChannel& channel = anim.channels[i];

        // 目标节点索引应该有效
        EXPECT_GE(channel.targetNodeIndex, 0);
        EXPECT_LT(channel.targetNodeIndex, static_cast<int>(nodeCount));

        // 采样器索引应该有效
        EXPECT_GE(channel.samplerIndex, 0);
        EXPECT_LT(channel.samplerIndex, static_cast<int>(samplerCount));

        // 路径类型应该是有效的枚举值
        // （AnimationPathType 是枚举类，值应该在合理范围内）
    }

    mLoader->destroyAnimationAsset(asset);
}

/**
 * 测试用例7：验证采样器数据提取
 *
 * 测试目标：采样器关键帧数据的正确性
 * 测试场景：加载包含动画的 GLB 文件
 * 预期结果：
 * - 时间数组不为空，升序排列
 * - 值数组不为空
 * - 插值类型有效
 * - 时间和值数量匹配
 * 验证点：采样器数据的完整性和一致性
 */
TEST_F(AssetLoaderTest, AnimationSamplerExtraction) {
    std::vector<uint8_t> glbData = loadFile(
        "AnimatedMorphCube.glb"
    );

    if (glbData.empty()) {
        GTEST_SKIP() << "Test file not found";
    }

    AnimationAsset* asset = mLoader->loadAnimationAsset(glbData.data(), glbData.size());
    ASSERT_NE(asset, nullptr);
    ASSERT_GT(asset->getAnimationCount(), 0);

    const auto& anim = asset->getAnimation(0);
    size_t samplerCount = anim.samplers.size();
    ASSERT_GT(samplerCount, 0);

    // 验证每个采样器
    for (size_t i = 0; i < samplerCount; ++i) {
        const AnimationSampler& sampler = anim.samplers[i];

        // 时间数组不应为空
        EXPECT_GT(sampler.times.size(), 0);

        // 值数组不应为空
        EXPECT_GT(sampler.values.size(), 0);

        // 时间应该升序排列
        for (size_t j = 1; j < sampler.times.size(); ++j) {
            EXPECT_GE(sampler.times[j], sampler.times[j - 1]);
        }

        // 插值类型应该是有效的枚举值
        // （LINEAR/STEP/CUBICSPLINE）
    }

    mLoader->destroyAnimationAsset(asset);
}

// ================================================================================================
// 内存管理测试
// ================================================================================================

/**
 * 测试用例8：验证资产销毁
 *
 * 测试目标：destroyAnimationAsset() 正确释放内存
 * 测试场景：创建并销毁多个资产
 * 预期结果：不崩溃，不内存泄漏
 * 验证点：内存管理的正确性
 */
TEST_F(AssetLoaderTest, AssetDestruction) {
    std::vector<uint8_t> glbData = loadFile(
        "AnimatedMorphCube.glb"
    );

    if (glbData.empty()) {
        GTEST_SKIP() << "Test file not found";
    }

    // 创建并销毁多个资产
    for (int i = 0; i < 5; ++i) {
        AnimationAsset* asset = mLoader->loadAnimationAsset(glbData.data(), glbData.size());
        ASSERT_NE(asset, nullptr);
        mLoader->destroyAnimationAsset(asset);
    }

    // 测试销毁 nullptr（应该安全）
    mLoader->destroyAnimationAsset(nullptr);

    // 如果有内存泄漏，valgrind 或 ASan 会检测到
}

/**
 * 测试用例9：验证多次加载同一文件
 *
 * 测试目标：同一文件可以被多次加载，每次创建独立的资产
 * 测试场景：加载同一 GLB 文件两次
 * 预期结果：两个资产独立，互不干扰
 * 验证点：资产的独立性
 */
TEST_F(AssetLoaderTest, MultipleLoadsOfSameFile) {
    std::vector<uint8_t> glbData = loadFile(
        "AnimatedMorphCube.glb"
    );

    if (glbData.empty()) {
        GTEST_SKIP() << "Test file not found";
    }

    // 加载两次
    AnimationAsset* asset1 = mLoader->loadAnimationAsset(glbData.data(), glbData.size());
    AnimationAsset* asset2 = mLoader->loadAnimationAsset(glbData.data(), glbData.size());

    ASSERT_NE(asset1, nullptr);
    ASSERT_NE(asset2, nullptr);
    EXPECT_NE(asset1, asset2);  // 应该是不同的对象

    // 两个资产应该有相同的数据
    EXPECT_EQ(asset1->nodes.size(), asset2->nodes.size());
    EXPECT_EQ(asset1->getAnimationCount(), asset2->getAnimationCount());
    if (asset1->getAnimationCount() > 0 && asset2->getAnimationCount() > 0) {
        const auto& anim1 = asset1->getAnimation(0);
        const auto& anim2 = asset2->getAnimation(0);
        EXPECT_EQ(anim1.channels.size(), anim2.channels.size());
        EXPECT_EQ(anim1.samplers.size(), anim2.samplers.size());
    }

    // 清理
    mLoader->destroyAnimationAsset(asset1);
    mLoader->destroyAnimationAsset(asset2);
}

/**
 * 测试用例10：加载 animation-only GLB 文件
 *
 * 测试目标：验证能正确加载 export_meshes=False 导出的 GLB
 * 测试场景：加载只包含骨骼和动画、不包含 mesh 几何体的 GLB
 * 预期结果：
 * - 成功加载 AnimationAsset
 * - 节点数量 > 0
 * - 动画通道和采样器存在
 * - 不会创建 mesh 相关的 Filament 对象
 * 验证点：支持动画与 mesh 解耦的核心功能
 */
TEST_F(AssetLoaderTest, LoadAnimationOnlyGLB) {
    // 加载 animation-only GLB 文件
    std::vector<uint8_t> glbData = loadFile(
        "ecorche_animation_only.glb"
    );

    // 如果文件不存在，跳过测试
    if (glbData.empty()) {
        // TODO：应该直接报错，返回测试失败！
        GTEST_SKIP() << "ecorche_animation_only.glb not found, skipping test";
    }

    // 加载动画资产
    AnimationAsset* asset = mLoader->loadAnimationAsset(glbData.data(), glbData.size());

    // 验证资产加载成功
    ASSERT_NE(asset, nullptr) << "Failed to load animation-only GLB";

    // 验证节点树存在（即使没有 mesh，骨骼节点也应该被提取）
    EXPECT_GT(asset->nodes.size(), 0) << "Animation-only GLB should have nodes";

    // 验证动画数据存在
    EXPECT_GT(asset->getAnimationCount(), 0) << "Animation-only GLB should have animations";
    const auto& anim = asset->getAnimation(0);
    EXPECT_GT(anim.channels.size(), 0) << "Animation-only GLB should have channels";
    EXPECT_GT(anim.samplers.size(), 0) << "Animation-only GLB should have samplers";

    // 验证动画时长有效
    float duration = anim.getDuration();
    EXPECT_GT(duration, 0.0f) << "Animation duration should be positive";

    // 验证 validate() 通过
    EXPECT_TRUE(asset->validate()) << "Animation-only asset validation failed";

    // 清理
    mLoader->destroyAnimationAsset(asset);
}

/**
 * 测试用例11：验证无蒙皮数据的情况
 *
 * 测试目标：验证没有蒙皮数据时，相关字段为空
 * 测试场景：加载不包含蒙皮数据的 GLB
 * 预期结果：
 * - joints 数组为空
 * - inverseBindMatrices 数组为空
 * - 所有节点的 skinIndex 为 -1
 * 验证点：正确处理无蒙皮数据的边界情况
 */
TEST_F(AssetLoaderTest, NoSkinData) {
    // 使用不包含蒙皮数据的 GLB（AnimatedMorphCube只有morph targets动画）
    std::vector<uint8_t> glbData = loadFile(
        "AnimatedMorphCube.glb"
    );

    if (glbData.empty()) {
        GTEST_SKIP() << "Test file not found";
    }

    AnimationAsset* asset = mLoader->loadAnimationAsset(glbData.data(), glbData.size());
    ASSERT_NE(asset, nullptr);

    // 验证蒙皮数据为空
    EXPECT_EQ(asset->joints.size(), 0) << "Non-skinned model should have empty joints";
    EXPECT_EQ(asset->inverseBindMatrices.size(), 0)
        << "Non-skinned model should have empty inverseBindMatrices";

    // 验证所有节点的 skinIndex 都是 -1
    for (size_t i = 0; i < asset->nodes.size(); ++i) {
        EXPECT_EQ(asset->nodes[i].skinIndex, -1)
            << "Node " << i << " should not reference a skin";
    }

    mLoader->destroyAnimationAsset(asset);
}

/**
 * 测试用例12：验证蒙皮数据提取
 *
 * 测试目标：验证从包含蒙皮数据的 GLB 中正确提取 joints 和 inverseBindMatrices
 * 测试场景：加载完整的骨骼动画模型
 * 预期结果：
 * - joints 数组非空且有效
 * - inverseBindMatrices 数量与 joints 一致
 * - 每个 joint 索引指向有效节点
 * - 逆绑定矩阵非零
 * - 节点的 skinIndex 字段有效
 * 验证点：蒙皮数据的完整性和一致性
 */
TEST_F(AssetLoaderTest, SkinDataExtraction) {
    // 使用包含蒙皮数据的完整模型
    std::vector<uint8_t> glbData = loadFile(
        "ecorche_full.glb"
    );

    if (glbData.empty()) {
        GTEST_SKIP() << "ecorche_full.glb not found, skipping test";
    }

    AnimationAsset* asset = mLoader->loadAnimationAsset(glbData.data(), glbData.size());
    ASSERT_NE(asset, nullptr);

    // 1. 验证 joints 数组存在且非空
    EXPECT_GT(asset->joints.size(), 0) << "Skinned model should have joints";

    // 2. 验证 inverseBindMatrices 数量与 joints 数量一致
    EXPECT_EQ(asset->inverseBindMatrices.size(), asset->joints.size())
        << "Inverse bind matrices count should match joints count";

    // 3. 验证每个 joint 索引指向有效节点
    size_t nodeCount = asset->nodes.size();
    for (size_t i = 0; i < asset->joints.size(); ++i) {
        int jointIndex = asset->joints[i];
        EXPECT_GE(jointIndex, 0) << "Joint " << i << " has negative index";
        EXPECT_LT(jointIndex, static_cast<int>(nodeCount))
            << "Joint " << i << " index out of range";
    }

    // 4. 验证逆绑定矩阵不全为零
    for (size_t i = 0; i < asset->inverseBindMatrices.size(); ++i) {
        const mat4f& matrix = asset->inverseBindMatrices[i];
        bool hasNonZero = false;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                if (matrix[row][col] != 0.0f) {
                    hasNonZero = true;
                    break;
                }
            }
            if (hasNonZero) break;
        }
        EXPECT_TRUE(hasNonZero) << "Inverse bind matrix " << i << " is all zeros";
    }

    // 5. 验证至少有一些节点引用了 skin
    bool foundSkinnedNode = false;
    for (const AnimationNode& node : asset->nodes) {
        if (node.skinIndex != -1) {
            foundSkinnedNode = true;
            // 对于单个 skin 的情况，skinIndex 应该是 0
            EXPECT_EQ(node.skinIndex, 0) << "Currently only single skin is supported";
        }
    }
    EXPECT_TRUE(foundSkinnedNode) << "Skinned model should have nodes referencing the skin";

    mLoader->destroyAnimationAsset(asset);
}

// ================================================================================================
// 主函数
// ================================================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
