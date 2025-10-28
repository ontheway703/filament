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
 * AnimationAsset 数据结构单元测试
 *
 * 本文件测试 AnimationAsset 类的核心功能，包括：
 * 1. 数据验证逻辑（validate）
 * 2. 节点查找功能（findNodeByName）
 * 3. 动画时长计算（getDuration）
 * 4. 节点层级结构验证
 * 5. 各种插值类型和动画路径支持
 *
 * AnimationAsset 是一个轻量级数据结构，用于表示"只包含动画数据"的资产，
 * 与传统的 FilamentAsset（包含 mesh + 动画）不同。
 *
 * 测试用例总数：16 个
 * 覆盖范围：
 * - 基础功能：空资产、节点查找、时长计算、名称管理
 * - 数据验证：通道索引、采样器索引、时间序列、层级循环
 * - 蒙皮数据：逆绑定矩阵、关节索引
 * - 动画类型：平移、旋转、三次样条插值
 */

#include <gtest/gtest.h>

#include <gltfio_ext/AnimationAsset.h>

using namespace filament::gltfio_ext;
using namespace filament::math;

/**
 * 测试 Fixture
 *
 * 在每个测试用例执行前，SetUp() 会创建一个基础的骨骼层级结构：
 *
 * 骨骼层级：root (索引0) -> spine (索引1) -> head (索引2)
 *
 * 这个简单的三节点骨骼用于测试各种动画数据验证逻辑。
 * 测试用例可以在这个基础上添加动画通道、采样器等数据。
 */
class AnimationAssetTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建一个简单的测试资产，包含基础骨骼结构
        // 层级结构：root -> spine -> head
        asset = std::make_unique<AnimationAsset>();

        // 创建根节点
        AnimationNode root;
        root.name = "root";
        root.parentIndex = -1;  // -1 表示根节点，无父节点
        root.transform = mat4f(1.0f);  // 单位矩阵
        asset->nodes.push_back(root);

        // 创建脊柱节点
        AnimationNode spine;
        spine.name = "spine";
        spine.parentIndex = 0;  // 父节点是 root
        spine.transform = mat4f(1.0f);
        asset->nodes.push_back(spine);

        // 创建头部节点
        AnimationNode head;
        head.name = "head";
        head.parentIndex = 1;  // 父节点是 spine
        head.transform = mat4f(1.0f);
        asset->nodes.push_back(head);

        // 构建骨骼名称到索引的映射表（用于快速查找）
        asset->buildBoneNameMap();
    }

    std::unique_ptr<AnimationAsset> asset;  // 测试用的动画资产实例
};

/**
 * 测试用例1：空资产验证
 *
 * 测试目标：验证一个空的 AnimationAsset 应该被认为是有效的
 * 测试场景：创建一个默认构造的 AnimationAsset，不添加任何节点、通道或采样器
 * 预期结果：validate() 返回 true
 * 验证点：空资产是一个合法的边界情况，不应报错
 */
TEST_F(AnimationAssetTest, EmptyAssetIsValid) {
    AnimationAsset emptyAsset;
    EXPECT_TRUE(emptyAsset.validate());
}

/**
 * 测试用例2：按名称查找节点
 *
 * 测试目标：验证 findNodeByName() 能正确通过节点名称查找节点索引
 * 测试场景：
 *   - 查找存在的节点名称："root"、"spine"、"head"
 *   - 查找不存在的节点名称："nonexistent"
 * 预期结果：
 *   - 存在的节点返回正确的索引（0, 1, 2）
 *   - 不存在的节点返回 -1
 * 验证点：节点名称映射表构建正确，查找逻辑准确
 */
TEST_F(AnimationAssetTest, FindNodeByName) {
    EXPECT_EQ(asset->findNodeByName("root"), 0);
    EXPECT_EQ(asset->findNodeByName("spine"), 1);
    EXPECT_EQ(asset->findNodeByName("head"), 2);
    EXPECT_EQ(asset->findNodeByName("nonexistent"), -1);  // 不存在的节点
}

/**
 * 测试用例3：验证有效的动画资产
 *
 * 测试目标：验证一个完整、正确配置的动画资产能够通过验证
 * 测试场景：
 *   - 创建一个采样器，包含3个关键帧（t=0, 1, 2秒）
 *   - 采样器数据是平移动画（3个float per keyframe）
 *   - 使用线性插值
 *   - 创建一个动画通道，目标节点是 spine，使用上述采样器
 * 预期结果：validate() 返回 true
 * 验证点：完整的动画数据结构能够正常工作
 */
TEST_F(AnimationAssetTest, ValidateValidAsset) {
    // 添加一个简单的动画通道
    AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f, 2.0f};  // 3个关键帧的时间点
    sampler.values = {0.0f, 0.0f, 0.0f,  // t=0: 位置 (0,0,0)
                      1.0f, 0.0f, 0.0f,  // t=1: 位置 (1,0,0)
                      2.0f, 0.0f, 0.0f}; // t=2: 位置 (2,0,0)
    sampler.interpolation = AnimationInterpolationType::LINEAR;  // 线性插值
    asset->samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;  // 目标是 spine 节点
    channel.samplerIndex = 0;     // 使用第一个采样器
    channel.path = AnimationPathType::TRANSLATION;  // 平移动画
    asset->channels.push_back(channel);

    EXPECT_TRUE(asset->validate());
}

/**
 * 测试用例4：检测无效的目标节点索引
 *
 * 测试目标：验证 validate() 能检测出指向不存在节点的动画通道
 * 测试场景：
 *   - 创建一个有效的采样器
 *   - 创建一个通道，其 targetNodeIndex 设为 99（超出节点数组范围）
 * 预期结果：validate() 返回 false
 * 验证点：数据验证逻辑能够捕获索引越界错误
 */
TEST_F(AnimationAssetTest, ValidateInvalidTargetNode) {
    AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f};
    sampler.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    asset->samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 99;  // 无效索引：超出节点数组范围（只有3个节点）
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::TRANSLATION;
    asset->channels.push_back(channel);

    EXPECT_FALSE(asset->validate());  // 应该验证失败
}

/**
 * 测试用例5：检测无效的采样器索引
 *
 * 测试目标：验证 validate() 能检测出引用不存在采样器的动画通道
 * 测试场景：
 *   - 创建一个通道，其 samplerIndex 设为 99（但采样器数组是空的）
 * 预期结果：validate() 返回 false
 * 验证点：数据验证逻辑能够捕获采样器索引错误
 */
TEST_F(AnimationAssetTest, ValidateInvalidSamplerIndex) {
    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 99;  // 无效索引：没有任何采样器
    channel.path = AnimationPathType::TRANSLATION;
    asset->channels.push_back(channel);

    EXPECT_FALSE(asset->validate());  // 应该验证失败
}

/**
 * 测试用例6：检测空的采样器时间数组
 *
 * 测试目标：验证 validate() 能检测出没有关键帧时间的采样器
 * 测试场景：
 *   - 创建一个采样器，times 数组为空
 *   - values 数组有数据（不匹配）
 * 预期结果：validate() 返回 false
 * 验证点：采样器必须至少有一个关键帧时间点
 */
TEST_F(AnimationAssetTest, ValidateEmptySamplerTimes) {
    AnimationSampler sampler;
    sampler.times = {};  // 空数组：没有时间点
    sampler.values = {0.0f, 0.0f, 0.0f};
    asset->samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::TRANSLATION;
    asset->channels.push_back(channel);

    EXPECT_FALSE(asset->validate());  // 应该验证失败
}

/**
 * 测试用例7：检测未排序的时间数组
 *
 * 测试目标：验证 validate() 能检测出时间值未按升序排列的采样器
 * 测试场景：
 *   - 创建一个采样器，times 为 {0.0, 2.0, 1.0}（乱序）
 * 预期结果：validate() 返回 false
 * 验证点：关键帧时间必须按时间顺序排列，才能正确插值
 */
TEST_F(AnimationAssetTest, ValidateUnsortedTimes) {
    AnimationSampler sampler;
    sampler.times = {0.0f, 2.0f, 1.0f};  // 未排序：第3个元素比第2个小
    sampler.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f};
    asset->samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::TRANSLATION;
    asset->channels.push_back(channel);

    EXPECT_FALSE(asset->validate());  // 应该验证失败
}

/**
 * 测试用例8：检测节点层级循环
 *
 * 测试目标：验证 validate() 能检测出节点层级中的循环引用
 * 测试场景：
 *   - 添加一个虚拟通道（确保 validate 不会因为空数据而跳过层级检查）
 *   - 修改 root 节点的父索引指向 head，形成循环：root -> spine -> head -> root
 * 预期结果：validate() 返回 false
 * 验证点：节点层级必须是无环的树结构，循环会导致无限递归
 *
 * 正常层级：root(-1) -> spine(0) -> head(1)
 * 错误层级：root(2) -> spine(0) -> head(1) -> root  [循环！]
 */
TEST_F(AnimationAssetTest, ValidateCyclicHierarchy) {
    // 添加虚拟通道，使 validate() 执行完整的验证逻辑
    AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f};
    sampler.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    asset->samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::TRANSLATION;
    asset->channels.push_back(channel);

    // 创建循环：将 root 的父节点设为 head（索引2），形成环形引用
    asset->nodes[0].parentIndex = 2;

    EXPECT_FALSE(asset->validate());  // 应该检测到循环并验证失败
}

/**
 * 测试用例9：验证逆绑定矩阵（Inverse Bind Matrices）
 *
 * 测试目标：验证逆绑定矩阵的数量必须与关节（joints）数量匹配
 * 测试场景：
 *   1. 添加3个关节和对应的3个逆绑定矩阵 -> 应该通过验证
 *   2. 再添加一个逆绑定矩阵（数量不匹配）-> 应该验证失败
 * 预期结果：
 *   - 匹配时：validate() 返回 true
 *   - 不匹配时：validate() 返回 false
 * 验证点：骨骼蒙皮数据的完整性检查
 *
 * 背景知识：逆绑定矩阵用于将顶点从网格空间转换到骨骼空间，
 * 每个关节必须对应一个逆绑定矩阵。
 */
TEST_F(AnimationAssetTest, ValidateInverseBindMatrices) {
    // 添加虚拟通道
    AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f};
    sampler.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    asset->samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::TRANSLATION;
    asset->channels.push_back(channel);

    // 添加3个关节（对应3个节点）
    asset->joints = {0, 1, 2};

    // 添加对应数量的逆绑定矩阵（3个）
    asset->inverseBindMatrices = {
        mat4f(1.0f),  // 单位矩阵
        mat4f(1.0f),
        mat4f(1.0f)
    };

    EXPECT_TRUE(asset->validate());  // 数量匹配，应该通过

    // 添加不匹配的矩阵（现在有4个矩阵，但只有3个关节）
    asset->inverseBindMatrices.push_back(mat4f(1.0f));
    EXPECT_FALSE(asset->validate());  // 数量不匹配，应该失败
}

/**
 * 测试用例10：检测无效的关节索引
 *
 * 测试目标：验证 validate() 能检测出指向不存在节点的关节索引
 * 测试场景：
 *   - 关节数组包含无效索引 99（超出节点范围）
 * 预期结果：validate() 返回 false
 * 验证点：所有关节索引必须指向有效的节点
 */
TEST_F(AnimationAssetTest, ValidateInvalidJointIndex) {
    // 添加虚拟通道
    AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f};
    sampler.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    asset->samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::TRANSLATION;
    asset->channels.push_back(channel);

    asset->joints = {0, 1, 99};  // 99 是无效索引（只有3个节点）
    asset->inverseBindMatrices = {mat4f(1.0f), mat4f(1.0f), mat4f(1.0f)};

    EXPECT_FALSE(asset->validate());  // 应该检测到无效索引
}

/**
 * 测试用例11：计算动画时长
 *
 * 测试目标：验证 getDuration() 能正确计算动画的总时长
 * 测试场景：
 *   - 添加两个采样器，时长分别为 2.0秒 和 3.5秒
 * 预期结果：getDuration() 返回 3.5（最大时长）
 * 验证点：动画时长应该是所有采样器中最大的时间值
 */
TEST_F(AnimationAssetTest, GetDuration) {
    // 添加第一个采样器（时长 2.0秒）
    AnimationSampler sampler1;
    sampler1.times = {0.0f, 1.0f, 2.0f};
    sampler1.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f};
    asset->samplers.push_back(sampler1);

    // 添加第二个采样器（时长 3.5秒，更长）
    AnimationSampler sampler2;
    sampler2.times = {0.0f, 0.5f, 3.5f};
    sampler2.values = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    asset->samplers.push_back(sampler2);

    // 时长应该是所有采样器的最大时间值
    EXPECT_FLOAT_EQ(asset->getDuration(), 3.5f);
}

/**
 * 测试用例12：空资产的时长
 *
 * 测试目标：验证没有动画数据的资产时长为 0
 * 测试场景：创建空的 AnimationAsset
 * 预期结果：getDuration() 返回 0.0
 * 验证点：边界情况处理
 */
TEST_F(AnimationAssetTest, GetDurationEmptyAsset) {
    AnimationAsset emptyAsset;
    EXPECT_FLOAT_EQ(emptyAsset.getDuration(), 0.0f);
}

/**
 * 测试用例13：动画名称的 Getter/Setter
 *
 * 测试目标：验证动画名称的设置和获取功能
 * 测试场景：设置名称为 "TestAnimation"，然后读取
 * 预期结果：getName() 返回设置的名称
 * 验证点：基础属性访问
 */
TEST_F(AnimationAssetTest, NameGetterSetter) {
    asset->setName("TestAnimation");
    EXPECT_EQ(asset->getName(), "TestAnimation");
}

/**
 * 测试用例14：节点结构验证
 *
 * 测试目标：验证节点层级结构和名称的正确性
 * 测试场景：检查 SetUp() 中创建的三节点骨骼结构
 * 预期结果：
 *   - 层级关系：root(parent=-1) -> spine(parent=0) -> head(parent=1)
 *   - 节点名称正确
 * 验证点：测试 fixture 的初始化正确性
 */
TEST_F(AnimationAssetTest, NodeStructure) {
    // 测试节点层级关系
    EXPECT_EQ(asset->nodes[0].parentIndex, -1);  // root 无父节点
    EXPECT_EQ(asset->nodes[1].parentIndex, 0);   // spine 的父节点是 root
    EXPECT_EQ(asset->nodes[2].parentIndex, 1);   // head 的父节点是 spine

    // 测试节点名称
    EXPECT_EQ(asset->nodes[0].name, "root");
    EXPECT_EQ(asset->nodes[1].name, "spine");
    EXPECT_EQ(asset->nodes[2].name, "head");
}

TEST_F(AnimationAssetTest, RotationChannel) {
    // Test rotation channel with quaternion data (4 values per keyframe)
    AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f};
    sampler.values = {0.0f, 0.0f, 0.0f, 1.0f,  // t=0: identity quaternion
                      0.0f, 0.707f, 0.0f, 0.707f}; // t=1: 90° rotation around Y
    sampler.interpolation = AnimationInterpolationType::LINEAR;
    asset->samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::ROTATION;
    asset->channels.push_back(channel);

    EXPECT_TRUE(asset->validate());
}

TEST_F(AnimationAssetTest, CubicSplineInterpolation) {
    // CUBICSPLINE requires 3x the values (in-tangent, value, out-tangent)
    AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f};
    sampler.values = {
        // t=0: in-tangent, value, out-tangent
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f,
        // t=1: in-tangent, value, out-tangent
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f
    };
    sampler.interpolation = AnimationInterpolationType::CUBICSPLINE;
    asset->samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::TRANSLATION;
    asset->channels.push_back(channel);

    EXPECT_TRUE(asset->validate());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
