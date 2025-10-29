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
 * 6. 多动画支持（getAnimationCount, getAnimation, getAnimationByName）
 *
 * AnimationAsset 是一个轻量级数据结构，用于表示"只包含动画数据"的资产，
 * 与传统的 FilamentAsset（包含 mesh + 动画）不同。
 *
 * 测试用例总数：20 个
 * 覆盖范围：
 * - 基础功能：空资产、节点查找、时长计算、多动画访问
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
    // 创建动画
    AnimationAsset::Animation anim;
    anim.name = "TestAnim";

    // 添加一个简单的动画通道
    AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f, 2.0f};  // 3个关键帧的时间点
    sampler.values = {0.0f, 0.0f, 0.0f,  // t=0: 位置 (0,0,0)
                      1.0f, 0.0f, 0.0f,  // t=1: 位置 (1,0,0)
                      2.0f, 0.0f, 0.0f}; // t=2: 位置 (2,0,0)
    sampler.interpolation = AnimationInterpolationType::LINEAR;  // 线性插值
    anim.samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;  // 目标是 spine 节点
    channel.samplerIndex = 0;     // 使用第一个采样器
    channel.path = AnimationPathType::TRANSLATION;  // 平移动画
    anim.channels.push_back(channel);

    asset->animations.push_back(std::move(anim));

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
    AnimationAsset::Animation anim;
    anim.name = "TestAnim";

    AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f};
    sampler.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    anim.samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 99;  // 无效索引：超出节点数组范围（只有3个节点）
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::TRANSLATION;
    anim.channels.push_back(channel);

    asset->animations.push_back(std::move(anim));

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
    AnimationAsset::Animation anim;
    anim.name = "TestAnim";

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 99;  // 无效索引：没有任何采样器
    channel.path = AnimationPathType::TRANSLATION;
    anim.channels.push_back(channel);

    asset->animations.push_back(std::move(anim));

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
    AnimationAsset::Animation anim;
    anim.name = "TestAnim";

    AnimationSampler sampler;
    sampler.times = {};  // 空数组：没有时间点
    sampler.values = {0.0f, 0.0f, 0.0f};
    anim.samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::TRANSLATION;
    anim.channels.push_back(channel);

    asset->animations.push_back(std::move(anim));

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
    AnimationAsset::Animation anim;
    anim.name = "TestAnim";

    AnimationSampler sampler;
    sampler.times = {0.0f, 2.0f, 1.0f};  // 未排序：第3个元素比第2个小
    sampler.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f};
    anim.samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::TRANSLATION;
    anim.channels.push_back(channel);

    asset->animations.push_back(std::move(anim));

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
    AnimationAsset::Animation anim;
    anim.name = "TestAnim";

    // 添加虚拟通道，使 validate() 执行完整的验证逻辑
    AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f};
    sampler.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    anim.samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::TRANSLATION;
    anim.channels.push_back(channel);

    asset->animations.push_back(std::move(anim));

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
    AnimationAsset::Animation anim;
    anim.name = "TestAnim";

    // 添加虚拟通道
    AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f};
    sampler.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    anim.samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::TRANSLATION;
    anim.channels.push_back(channel);

    asset->animations.push_back(std::move(anim));

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
    AnimationAsset::Animation anim;
    anim.name = "TestAnim";

    // 添加虚拟通道
    AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f};
    sampler.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    anim.samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::TRANSLATION;
    anim.channels.push_back(channel);

    asset->animations.push_back(std::move(anim));

    asset->joints = {0, 1, 99};  // 99 是无效索引（只有3个节点）
    asset->inverseBindMatrices = {mat4f(1.0f), mat4f(1.0f), mat4f(1.0f)};

    EXPECT_FALSE(asset->validate());  // 应该检测到无效索引
}

/**
 * 测试用例11：计算动画时长
 *
 * 测试目标：验证 Animation::getDuration() 能正确计算动画的总时长
 * 测试场景：
 *   - 添加两个采样器，时长分别为 2.0秒 和 3.5秒
 * 预期结果：getDuration() 返回 3.5（最大时长）
 * 验证点：动画时长应该是所有采样器中最大的时间值
 */
TEST_F(AnimationAssetTest, GetDuration) {
    AnimationAsset::Animation anim;
    anim.name = "TestAnim";

    // 添加第一个采样器（时长 2.0秒）
    AnimationSampler sampler1;
    sampler1.times = {0.0f, 1.0f, 2.0f};
    sampler1.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f};
    anim.samplers.push_back(sampler1);

    // 添加第二个采样器（时长 3.5秒，更长）
    AnimationSampler sampler2;
    sampler2.times = {0.0f, 0.5f, 3.5f};
    sampler2.values = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    anim.samplers.push_back(sampler2);

    // 时长应该是所有采样器的最大时间值
    EXPECT_FLOAT_EQ(anim.getDuration(), 3.5f);
}

/**
 * 测试用例12：空资产的时长
 *
 * 测试目标：验证没有动画数据的动画时长为 0
 * 测试场景：创建空的 Animation
 * 预期结果：getDuration() 返回 0.0
 * 验证点：边界情况处理
 */
TEST_F(AnimationAssetTest, GetDurationEmptyAsset) {
    AnimationAsset::Animation emptyAnim;
    emptyAnim.name = "Empty";
    EXPECT_FLOAT_EQ(emptyAnim.getDuration(), 0.0f);
}

/**
 * 测试用例13：多动画访问
 *
 * 测试目标：验证 getAnimationCount(), getAnimation(), getAnimationByName() 功能
 * 测试场景：
 *   - 添加两个动画："idle" 和 "walk"
 *   - 测试按索引访问和按名称访问
 * 预期结果：
 *   - getAnimationCount() 返回 2
 *   - 能够按索引和名称正确访问动画
 * 验证点：多动画支持的基础功能
 */
TEST_F(AnimationAssetTest, MultipleAnimations) {
    // 添加第一个动画
    AnimationAsset::Animation idle;
    idle.name = "idle";
    AnimationSampler sampler1;
    sampler1.times = {0.0f, 1.0f};
    sampler1.values = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    idle.samplers.push_back(sampler1);
    asset->animations.push_back(std::move(idle));

    // 添加第二个动画
    AnimationAsset::Animation walk;
    walk.name = "walk";
    AnimationSampler sampler2;
    sampler2.times = {0.0f, 0.5f, 1.0f};
    sampler2.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    walk.samplers.push_back(sampler2);
    asset->animations.push_back(std::move(walk));

    // 测试动画数量
    EXPECT_EQ(asset->getAnimationCount(), 2);

    // 测试按索引访问
    EXPECT_EQ(asset->getAnimation(0).name, "idle");
    EXPECT_EQ(asset->getAnimation(1).name, "walk");

    // 测试按名称访问
    const auto* idleAnim = asset->getAnimationByName("idle");
    ASSERT_NE(idleAnim, nullptr);
    EXPECT_EQ(idleAnim->name, "idle");

    const auto* walkAnim = asset->getAnimationByName("walk");
    ASSERT_NE(walkAnim, nullptr);
    EXPECT_EQ(walkAnim->name, "walk");

    // 测试不存在的动画
    const auto* nonexistent = asset->getAnimationByName("run");
    EXPECT_EQ(nonexistent, nullptr);
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

/**
 * 测试用例15：旋转动画通道（四元数数据）
 *
 * 测试目标：验证旋转类型的动画通道能正确验证
 * 测试场景：
 *   - 创建一个旋转动画采样器
 *   - 每个关键帧包含 4 个值（四元数：x, y, z, w）
 *   - t=0: 单位四元数 (0,0,0,1) - 无旋转
 *   - t=1: 绕Y轴旋转90度的四元数 (0,0.707,0,0.707)
 * 预期结果：validate() 返回 true
 * 验证点：四元数旋转数据的正确性
 *
 * 背景知识：
 * - 旋转动画使用四元数表示，每个关键帧需要 4 个 float
 * - 四元数格式：(x, y, z, w)，其中 w 是实部
 * - 单位四元数 (0,0,0,1) 表示无旋转
 */
TEST_F(AnimationAssetTest, RotationChannel) {
    AnimationAsset::Animation anim;
    anim.name = "TestAnim";

    // 测试旋转通道，使用四元数数据（每个关键帧4个值）
    AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f};
    sampler.values = {
        0.0f, 0.0f, 0.0f, 1.0f,       // t=0: 单位四元数（无旋转）
        0.0f, 0.707f, 0.0f, 0.707f    // t=1: 绕Y轴旋转90度
    };
    sampler.interpolation = AnimationInterpolationType::LINEAR;  // 线性插值（slerp）
    anim.samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::ROTATION;  // 旋转动画
    anim.channels.push_back(channel);

    asset->animations.push_back(std::move(anim));

    EXPECT_TRUE(asset->validate());
}

/**
 * 测试用例16：三次样条插值（CUBICSPLINE）
 *
 * 测试目标：验证三次样条插值类型的采样器能正确验证
 * 测试场景：
 *   - 使用 CUBICSPLINE 插值类型
 *   - 每个关键帧需要 3 组数据：入切线、值、出切线
 *   - 对于平移动画（vec3），每组3个float，共 3×3=9 个值/关键帧
 *   - 2个关键帧，共 18 个值
 * 预期结果：validate() 返回 true
 * 验证点：三次样条数据格式的正确性
 *
 * 背景知识：
 * - CUBICSPLINE 提供平滑的曲线插值，比 LINEAR 更自然
 * - 数据格式：[in-tangent, value, out-tangent] 对每个关键帧
 * - 切线用于控制曲线的形状和速度
 */
TEST_F(AnimationAssetTest, CubicSplineInterpolation) {
    AnimationAsset::Animation anim;
    anim.name = "TestAnim";

    // CUBICSPLINE 需要 3倍 的数据量（入切线、值、出切线）
    AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f};
    sampler.values = {
        // t=0 的数据：入切线、值、出切线（各3个float）
        0.0f, 0.0f, 0.0f,  // 入切线
        0.0f, 0.0f, 0.0f,  // 实际值（起始位置）
        0.0f, 0.0f, 0.0f,  // 出切线

        // t=1 的数据：入切线、值、出切线
        0.0f, 0.0f, 0.0f,  // 入切线
        1.0f, 0.0f, 0.0f,  // 实际值（结束位置）
        0.0f, 0.0f, 0.0f   // 出切线
    };
    sampler.interpolation = AnimationInterpolationType::CUBICSPLINE;  // 三次样条插值
    anim.samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::TRANSLATION;  // 平移动画
    anim.channels.push_back(channel);

    asset->animations.push_back(std::move(anim));

    EXPECT_TRUE(asset->validate());
}

/**
 * 测试用例17：检测平移动画数据不足
 *
 * 测试目标：验证 validate() 能检测出平移动画采样器的值数量不足
 * 测试场景：
 *   - 创建一个平移动画采样器，有2个关键帧
 *   - 平移动画需要每个关键帧3个值（x,y,z），共需要 2*3=6 个值
 *   - 实际只提供4个值（数量不足）
 * 预期结果：validate() 返回 false
 * 验证点：采样器数据长度必须与路径类型匹配
 */
TEST_F(AnimationAssetTest, ValidateTranslationInsufficientValues) {
    AnimationAsset::Animation anim;
    anim.name = "TestAnim";

    AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f};  // 2个关键帧
    // 平移需要 2 * 3 = 6 个值，但只提供了4个
    sampler.values = {0.0f, 0.0f, 0.0f, 1.0f};
    sampler.interpolation = AnimationInterpolationType::LINEAR;
    anim.samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::TRANSLATION;
    anim.channels.push_back(channel);

    asset->animations.push_back(std::move(anim));

    EXPECT_FALSE(asset->validate());  // 应该检测到数据不足
}

/**
 * 测试用例18：检测旋转动画数据数量不正确
 *
 * 测试目标：验证 validate() 能检测出旋转动画采样器的值数量不正确
 * 测试场景：
 *   - 创建一个旋转动画采样器，有2个关键帧
 *   - 旋转动画需要每个关键帧4个值（四元数：x,y,z,w），共需要 2*4=8 个值
 *   - 实际只提供6个值（不是4的倍数）
 * 预期结果：validate() 返回 false
 * 验证点：旋转动画必须提供四元数格式的数据，值数量必须是4的倍数
 */
TEST_F(AnimationAssetTest, ValidateRotationInvalidValueCount) {
    AnimationAsset::Animation anim;
    anim.name = "TestAnim";

    AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f};  // 2个关键帧
    // 旋转需要 2 * 4 = 8 个值，但只提供了6个（不是4的倍数）
    sampler.values = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    sampler.interpolation = AnimationInterpolationType::LINEAR;
    anim.samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::ROTATION;
    anim.channels.push_back(channel);

    asset->animations.push_back(std::move(anim));

    EXPECT_FALSE(asset->validate());  // 应该检测到数据数量错误
}

/**
 * 测试用例19：检测三次样条插值数据不足
 *
 * 测试目标：验证 validate() 能检测出 CUBICSPLINE 插值的数据不足
 * 测试场景：
 *   - 使用 CUBICSPLINE 插值，有2个关键帧
 *   - 平移动画 + CUBICSPLINE 需要：2关键帧 * 3值/关键帧 * 3倍(切线) = 18个值
 *   - 实际只提供6个值（只够 LINEAR 插值）
 * 预期结果：validate() 返回 false
 * 验证点：CUBICSPLINE 的数据量必须是 LINEAR 的3倍
 */
TEST_F(AnimationAssetTest, ValidateCubicSplineInsufficientValues) {
    AnimationAsset::Animation anim;
    anim.name = "TestAnim";

    AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f};  // 2个关键帧
    // CUBICSPLINE 需要 2 * 3 * 3 = 18 个值，但只提供了 2 * 3 = 6 个（LINEAR的量）
    sampler.values = {
        0.0f, 0.0f, 0.0f,  // t=0
        1.0f, 0.0f, 0.0f   // t=1
    };
    sampler.interpolation = AnimationInterpolationType::CUBICSPLINE;  // 需要3倍数据
    anim.samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::TRANSLATION;
    anim.channels.push_back(channel);

    asset->animations.push_back(std::move(anim));

    EXPECT_FALSE(asset->validate());  // 应该检测到数据不足
}

/**
 * 测试用例20：不使用骨骼名称映射表的节点查找
 *
 * 测试目标：验证 findNodeByName() 在没有预构建映射表时也能正确工作
 * 测试场景：
 *   - 创建一个新的 AnimationAsset，不调用 buildBoneNameMap()
 *   - 直接使用 findNodeByName() 查找节点
 * 预期结果：
 *   - 能够通过线性搜索找到存在的节点
 *   - 不存在的节点返回 -1
 * 验证点：findNodeByName() 的回退逻辑（线性搜索）能正常工作
 *
 * 背景知识：
 * - findNodeByName() 优先使用哈希表（O(1)查找）
 * - 如果哈希表为空，回退到线性搜索（O(n)查找）
 * - 这个测试确保回退路径也被正确实现
 */
TEST_F(AnimationAssetTest, FindNodeByNameWithoutBoneMap) {
    // 创建新资产，不构建骨骼名称映射表
    AnimationAsset assetWithoutMap;

    // 添加节点
    AnimationNode node1;
    node1.name = "testNode1";
    node1.parentIndex = -1;
    assetWithoutMap.nodes.push_back(node1);

    AnimationNode node2;
    node2.name = "testNode2";
    node2.parentIndex = 0;
    assetWithoutMap.nodes.push_back(node2);

    // 注意：这里故意不调用 buildBoneNameMap()，强制使用线性搜索

    // 测试线性搜索路径
    EXPECT_EQ(assetWithoutMap.findNodeByName("testNode1"), 0);
    EXPECT_EQ(assetWithoutMap.findNodeByName("testNode2"), 1);
    EXPECT_EQ(assetWithoutMap.findNodeByName("nonexistent"), -1);
}

/**
 * 测试用例21：WEIGHTS 通道（Morph Target 动画）
 *
 * 测试目标：验证 WEIGHTS 类型的动画通道能正确验证
 * 测试场景：
 *   - 创建一个 WEIGHTS 动画采样器
 *   - 每个关键帧包含 N 个 weight 值（N = morph target 数量）
 *   - 例如：5个morph targets，2个关键帧 = 10个值
 * 预期结果：validate() 返回 true
 * 验证点：可变长度的 weights 数据能正确处理
 *
 * 注意：当前 AnimationAsset::Animation::validate() 在处理 WEIGHTS 时会跳过值数量验证（line 138-139），
 * 因为 morph target 数量是可变的。本测试用于验证基本的加载和结构正确性。
 */
TEST_F(AnimationAssetTest, WeightsChannel) {
    AnimationAsset::Animation anim;
    anim.name = "MorphAnim";

    // 假设有 5 个 morph targets
    const size_t numMorphTargets = 5;
    const size_t numKeyframes = 2;

    AnimationSampler sampler;
    sampler.times = {0.0f, 1.0f};  // 2 个关键帧
    // WEIGHTS: 每帧 5 个值，共 2 * 5 = 10 个值
    sampler.values = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,  // t=0: 5个weights
        1.0f, 0.5f, 0.3f, 0.0f, 0.0f   // t=1: 5个weights
    };
    sampler.interpolation = AnimationInterpolationType::LINEAR;
    anim.samplers.push_back(sampler);

    AnimationChannel channel;
    channel.targetNodeIndex = 1;
    channel.samplerIndex = 0;
    channel.path = AnimationPathType::WEIGHTS;  // WEIGHTS 路径
    anim.channels.push_back(channel);

    asset->animations.push_back(std::move(anim));

    // 验证基本结构
    ASSERT_EQ(asset->getAnimationCount(), 1);
    const auto& loadedAnim = asset->getAnimation(0);
    EXPECT_EQ(loadedAnim.name, "MorphAnim");
    EXPECT_EQ(loadedAnim.channels.size(), 1);
    EXPECT_EQ(loadedAnim.samplers.size(), 1);

    // 验证通道配置
    const auto& loadedChannel = loadedAnim.channels[0];
    EXPECT_EQ(loadedChannel.path, AnimationPathType::WEIGHTS);
    EXPECT_EQ(loadedChannel.samplerIndex, 0);

    // 验证采样器数据
    const auto& loadedSampler = loadedAnim.samplers[0];
    EXPECT_EQ(loadedSampler.times.size(), numKeyframes);
    EXPECT_EQ(loadedSampler.values.size(), numKeyframes * numMorphTargets);
    EXPECT_EQ(loadedSampler.interpolation, AnimationInterpolationType::LINEAR);

    // 验证值的正确性
    EXPECT_FLOAT_EQ(loadedSampler.values[0], 0.0f);  // t=0, weight[0]
    EXPECT_FLOAT_EQ(loadedSampler.values[4], 0.0f);  // t=0, weight[4]
    EXPECT_FLOAT_EQ(loadedSampler.values[5], 1.0f);  // t=1, weight[0]
    EXPECT_FLOAT_EQ(loadedSampler.values[6], 0.5f);  // t=1, weight[1]

    // validate() 应该通过（WEIGHTS 路径会被跳过详细验证）
    EXPECT_TRUE(asset->validate());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
