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

//! \file AnimationAsset.h
//! \brief 轻量级动画资产，支持动画与网格解耦
//!
//! 设计背景：
//! -----------
//! 在传统的 glTF 工作流中，动画数据嵌入在包含完整网格几何体的 GLB 文件中，
//! 这导致无法灵活复用动画。AnimationAsset 实现了动画与网格的完全解耦：
//!
//! - **分离式存储**：网格 GLB 和动画 GLB 可以独立存储和加载
//! - **动画复用**：一套角色网格可以应用多套外部动画（idle、walk、run 等）
//! - **轻量级加载**：只加载骨骼层级和关键帧数据，不加载 mesh 几何体、材质、纹理
//! - **多动画支持**：单个 GLB 文件可包含多个动画片段，一次性全部加载
//!
//! glTF 2.0 规范对应关系：
//! ----------------------
//! - AnimationNode      ↔ glTF Node（仅保留 name、parent、transform、skin）
//! - AnimationChannel   ↔ glTF Animation Channel（sampler + target）
//! - AnimationSampler   ↔ glTF Animation Sampler（input + output + interpolation）
//! - AnimationPathType  ↔ glTF Animation Channel Target Path（translation/rotation/scale）
//!
//! 典型使用流程：
//! -------------
//! 1. 使用 AssetLoader::createAsset() 加载网格 GLB（包含完整的蒙皮网格）
//! 2. 使用 AssetLoader::loadAnimationAsset() 加载动画 GLB（可能包含多个动画）
//! 3. 使用 AnimationBinding 通过骨骼名称将动画绑定到网格实体
//! 4. 使用 Animator 播放和混合动画（Phase 2 待实现）

#ifndef GLTFIO_EXT_ANIMATIONASSET_H
#define GLTFIO_EXT_ANIMATIONASSET_H

#include <string>
#include <vector>
#include <unordered_map>

#include <math/mat4.h>
#include <math/quat.h>
#include <math/vec3.h>

#include <utils/compiler.h>

namespace filament::gltfio_ext {

/**
 * \struct AnimationNode AnimationAsset.h gltfio_ext/AnimationAsset.h
 * \brief 轻量级节点结构，仅包含骨骼动画所需的最小信息
 *
 * 设计要点：
 * - **轻量级**：不包含 mesh、camera、light 等完整 glTF Node 的属性，大幅减少内存占用
 * - **专注骨骼**：仅保留动画播放必需的数据（名称、层级、变换、蒙皮索引）
 * - **对比**：完整的 glTF Node 可能有 20+ 字段，AnimationNode 只有 4 个字段
 *
 * 与 glTF 2.0 规范的对应：
 * - name       ↔ node.name（用于跨文件的骨骼名称匹配）
 * - parentIndex↔ node 数组的索引关系（glTF 用 children 数组，这里反向存储 parent）
 * - transform  ↔ node.matrix 或 TRS（translation/rotation/scale）的组合
 * - skinIndex  ↔ node.skin（指向 skin 数组，-1 表示非蒙皮节点）
 */
struct UTILS_PUBLIC AnimationNode {
    //! 骨骼名称（用于跨文件的骨骼映射，如 "Spine.001"）
    std::string name;

    //! 父节点索引（-1 表示根节点，如 Armature 根骨骼）
    //! 取值范围：[-1, nodes.size()-1]
    int parentIndex;

    //! 局部变换矩阵（相对于父节点的变换，列主序）
    //! 用于初始姿态，运行时会被动画关键帧覆盖
    filament::math::mat4f transform;

    //! 蒙皮索引（-1 表示此节点不参与蒙皮，如相机或灯光节点）
    //! 取值范围：[-1, skins.size()-1]
    int skinIndex;

    AnimationNode()
        : parentIndex(-1), transform(filament::math::mat4f(1.0f)), skinIndex(-1) {}
};

/**
 * \enum AnimationPathType
 * \brief 动画通道目标属性类型
 *
 * 对应 glTF 2.0 规范的 animation.channel.target.path 字段。
 * 每个 AnimationChannel 通过此枚举指定要驱动节点的哪个属性。
 *
 * 数据格式说明：
 * - TRANSLATION: 3 个 float (x, y, z)，单位为场景单位（通常是米）
 * - ROTATION:    4 个 float (x, y, z, w) 表示四元数，必须归一化
 * - SCALE:       3 个 float (x, y, z)，通常为正数，1.0 表示原始大小
 * - WEIGHTS:     N 个 float，N 为 morph target 数量（当前不支持）
 */
enum class AnimationPathType : uint8_t {
    TRANSLATION,  //!< 平移动画（位置），vec3
    ROTATION,     //!< 旋转动画（朝向），quaternion (x,y,z,w)
    SCALE,        //!< 缩放动画（大小），vec3
    WEIGHTS       //!< Morph target 权重动画（当前暂不支持）
};

/**
 * \enum AnimationInterpolationType
 * \brief 关键帧插值方式
 *
 * 对应 glTF 2.0 规范的 animation.sampler.interpolation 字段。
 * 不同插值方式对数据量和平滑度有不同要求。
 *
 * 数学原理：
 * - **LINEAR**:       v(t) = lerp(v[k], v[k+1], (t - t[k]) / (t[k+1] - t[k]))
 *                     最常用，数据量最小，适合大多数动画
 *
 * - **STEP**:         v(t) = v[k]  (对于 t ∈ [t[k], t[k+1]))
 *                     阶跃插值，无过渡，用于离散状态切换（如开关、可见性）
 *
 * - **CUBICSPLINE**:  v(t) = Hermite 三次样条插值
 *                     最平滑，但数据量是 LINEAR 的 3 倍（每个关键帧需要：in-tangent, value, out-tangent）
 *                     适合高质量动画（如面部表情）
 *
 * 数据布局差异（以 TRANSLATION 为例）：
 * - LINEAR:      keyframe 数据为 [x, y, z, x, y, z, ...]  (3N 个 float)
 * - STEP:        keyframe 数据为 [x, y, z, x, y, z, ...]  (3N 个 float，但不插值)
 * - CUBICSPLINE: keyframe 数据为 [in_x, in_y, in_z, x, y, z, out_x, out_y, out_z, ...]  (9N 个 float)
 */
enum class AnimationInterpolationType : uint8_t {
    LINEAR,       //!< 线性插值（默认，适合大多数情况）
    STEP,         //!< 阶跃插值（无过渡，用于离散状态）
    CUBICSPLINE   //!< 三次样条插值（最平滑，数据量 ×3）
};

/**
 * \struct AnimationSampler AnimationAsset.h gltfio_ext/AnimationAsset.h
 * \brief 存储动画通道的关键帧数据
 *
 * 对应 glTF 2.0 规范的 animation.sampler，是动画数据的核心容器。
 * 一个 sampler 可以被多个 channel 共享（例如左右手使用相同的旋转动画）。
 *
 * 数据结构说明：
 * - times 和 values 数组必须对应（times.size() × valuesPerKeyframe = values.size()）
 * - times 必须单调递增（已在 validate() 中检查）
 * - values 的解释取决于关联的 channel.path 类型
 *
 * 示例（ROTATION，3 个关键帧，LINEAR 插值）：
 * \code
 * times  = [0.0, 1.0, 2.0]  // 3 个时间点
 * values = [0,0,0,1,  0.707,0,0,0.707,  1,0,0,0]  // 12 个 float = 3 × 4（quaternion）
 * \endcode
 *
 * 示例（TRANSLATION，2 个关键帧，CUBICSPLINE 插值）：
 * \code
 * times  = [0.0, 1.0]  // 2 个时间点
 * values = [in_x, in_y, in_z, x, y, z, out_x, out_y, out_z,  // keyframe 0
 *           in_x, in_y, in_z, x, y, z, out_x, out_y, out_z]  // keyframe 1
 *        = 18 个 float = 2 × 3（vec3）× 3（in/value/out）
 * \endcode
 */
struct UTILS_PUBLIC AnimationSampler {
    //! 关键帧时间点（单位：秒，必须单调递增）
    //! 例如：[0.0, 0.5, 1.0, 1.5] 表示 4 个关键帧
    std::vector<float> times;

    //! 关键帧输出值（解释取决于 channel.path 类型和插值方式）
    //! - TRANSLATION (LINEAR):      3 floats/keyframe (x, y, z)
    //! - ROTATION (LINEAR):         4 floats/keyframe (quaternion: x, y, z, w)
    //! - SCALE (LINEAR):            3 floats/keyframe (x, y, z)
    //! - TRANSLATION (CUBICSPLINE): 9 floats/keyframe (in-tangent + value + out-tangent)
    std::vector<float> values;

    //! 插值方式（LINEAR/STEP/CUBICSPLINE）
    //! 默认 LINEAR，可以在运行时切换以实现不同的播放效果
    AnimationInterpolationType interpolation;

    AnimationSampler() : interpolation(AnimationInterpolationType::LINEAR) {}
};

/**
 * \struct AnimationChannel AnimationAsset.h gltfio_ext/AnimationAsset.h
 * \brief 连接采样器到目标节点属性（动画通道）
 *
 * 对应 glTF 2.0 规范的 animation.channel，充当"线路"角色：
 * 将一个 sampler 的关键帧数据路由到指定节点的特定属性。
 *
 * 关系图解：
 * \code
 *   AnimationChannel                  AnimationSampler              AnimationNode
 *   ┌────────────────┐                ┌─────────────┐              ┌─────────────┐
 *   │ samplerIndex=2 │───────────────▶│ times[...]  │              │ name="Arm"  │
 *   │ targetIndex=5  │───────────┐    │ values[...] │              │ parent=...  │
 *   │ path=ROTATION  │           │    └─────────────┘              └─────────────┘
 *   └────────────────┘           │                                        ▲
 *                                └────────────────────────────────────────┘
 *                                    "将 sampler 2 的数据应用到 node 5 的 rotation"
 * \endcode
 *
 * 典型示例：
 * - Channel 0: sampler=0, target=5, path=TRANSLATION → "节点 5 的位置由 sampler 0 驱动"
 * - Channel 1: sampler=0, target=6, path=TRANSLATION → "节点 6 的位置也由 sampler 0 驱动"（复用）
 * - Channel 2: sampler=1, target=5, path=ROTATION    → "节点 5 的旋转由 sampler 1 驱动"
 *
 * 注意事项：
 * - 同一节点可以有多个 channel（如同时驱动平移、旋转、缩放）
 * - 多个 channel 可以共享同一个 sampler（数据复用）
 * - 不允许同一节点的同一属性被多个 channel 驱动（glTF 规范禁止）
 */
struct UTILS_PUBLIC AnimationChannel {
    //! 目标节点索引（指向 AnimationAsset::nodes 数组）
    //! 取值范围：[0, nodes.size()-1]
    int targetNodeIndex;

    //! 采样器索引（指向 Animation::samplers 数组）
    //! 取值范围：[0, samplers.size()-1]
    int samplerIndex;

    //! 要驱动的节点属性（TRANSLATION/ROTATION/SCALE/WEIGHTS）
    AnimationPathType path;

    AnimationChannel() : targetNodeIndex(-1), samplerIndex(-1), path(AnimationPathType::TRANSLATION) {}
};

/**
 * \class AnimationAsset AnimationAsset.h gltfio_ext/AnimationAsset.h
 * \brief 轻量级动画资产，支持多个动画片段（不包含 mesh 几何体）
 *
 * AnimationAsset 代表播放骨骼动画所需的最小数据集：
 * - **节点层级**（骨骼结构，所有动画共享）
 * - **多个动画片段**（每个动画有独立的 channels 和 samplers）
 *
 * 核心设计思想：
 * ---------------
 * 1. **骨骼共享**：所有动画使用同一套节点层级（nodes 数组），避免重复存储骨架结构
 * 2. **动画独立**：每个 Animation 有独立的 channels/samplers，可以有不同的时长和复杂度
 * 3. **轻量级**：不包含 mesh 几何体、材质、纹理，内存占用仅为完整 GLB 的 1/10 ~ 1/50
 *
 * 数据结构示例（ecorche_full.glb）：
 * \code
 * AnimationAsset {
 *   nodes = [327 个节点]              // 骨骼层级（所有动画共享）
 *   animations = [
 *     Animation { name="Pull",  channels=327, samplers=327, duration=1.5s },
 *     Animation { name="Push",  channels=327, samplers=327, duration=1.2s },
 *     Animation { name="Squat", channels=327, samplers=327, duration=2.0s }
 *   ]
 *   inverseBindMatrices = [327 个 mat4]  // 蒙皮数据（可选）
 * }
 * \endcode
 *
 * 使用场景：
 * ---------
 * 支持 Blender 的 "export_meshes=False" 工作流，即动画与网格分离存储。
 * 一个角色可以有：
 * - 1 个完整 GLB（网格 + 蒙皮 + 初始骨架）
 * - N 个动画 GLB（只有骨骼 + 动画，每个可包含多个动画片段）
 *
 * Typical usage:
 * \code
 * // 加载动画 GLB（可能包含多个动画片段）
 * AnimationAsset* animAsset = assetLoader->loadAnimationAsset(data, size);
 *
 * // 遍历所有动画
 * for (size_t i = 0; i < animAsset->getAnimationCount(); ++i) {
 *     const auto& anim = animAsset->getAnimation(i);
 *     utils::slog.i << "Animation: " << anim.name
 *                   << ", duration: " << anim.getDuration() << "s"
 *                   << utils::io::endl;
 * }
 *
 * // 按名称查找动画
 * const auto* walkAnim = animAsset->getAnimationByName("walk");
 * if (walkAnim) {
 *     // 播放 walk 动画...
 * }
 * \endcode
 */
class UTILS_PUBLIC AnimationAsset {
public:
    /**
     * \struct Animation
     * \brief 单个动画片段（动画剪辑）
     *
     * 每个 Animation 拥有独立的 channels 和 samplers，但共享父 AnimationAsset 的
     * nodes 节点层级。这种设计使得多个动画可以复用同一套骨架结构。
     *
     * 设计原理：
     * - channels 和 samplers 的数量可能不同（一个 sampler 可被多个 channel 共享）
     * - 时长由最长的 sampler 决定（getDuration() 计算）
     * - 名称用于按名称查找（如 "idle"、"walk"、"run"）
     */
    struct UTILS_PUBLIC Animation {
        //! 动画名称（例如："idle"、"walk"、"run"）
        //! 如果 glTF 文件未指定名称，会自动命名为 "Animation0"、"Animation1" 等
        std::string name;

        //! 动画通道（连接 samplers 到目标节点）
        //! 数量通常等于被驱动的节点数 × 属性数（如 327 个关节 × 1 个属性 = 327 个 channels）
        std::vector<AnimationChannel> channels;

        //! 关键帧采样器
        //! 数量可能小于 channels（多个 channel 可共享同一个 sampler）
        std::vector<AnimationSampler> samplers;

        /**
         * 获取动画时长（秒）
         * @return 所有 samplers 中最大的时间值（遍历所有 sampler.times 取最大值）
         */
        float getDuration() const;

        /**
         * 验证此动画的一致性
         * 检查项：
         * - channels 的 samplerIndex 是否有效
         * - samplers 的 times 数组是否单调递增
         * - samplers 的 values 数组大小是否与 path 类型匹配
         * @return true 动画数据有效，false 存在错误
         */
        bool validate() const;
    };

    AnimationAsset() = default;
    ~AnimationAsset() = default;

    // Non-copyable
    AnimationAsset(const AnimationAsset&) = delete;
    AnimationAsset& operator=(const AnimationAsset&) = delete;

    // Movable
    AnimationAsset(AnimationAsset&&) = default;
    AnimationAsset& operator=(AnimationAsset&&) = default;

    /**
     * Validates the asset data for consistency.
     * Checks all animations and node hierarchy.
     * @return true if asset is valid, false otherwise
     */
    bool validate() const;

    /**
     * Finds a node by name.
     * @param name The node name to search for
     * @return Node index, or -1 if not found
     */
    int findNodeByName(const std::string& name) const;

    /**
     * Builds an internal name-to-index mapping for fast lookups.
     * Called automatically after loading.
     */
    void buildBoneNameMap();

    /**
     * Gets the number of animation clips in this asset.
     * @return Animation count
     */
    size_t getAnimationCount() const { return animations.size(); }

    /**
     * Gets an animation by index.
     * @param index Animation index (0 to getAnimationCount()-1)
     * @return Reference to the animation
     */
    const Animation& getAnimation(size_t index) const;

    /**
     * Finds an animation by name.
     * @param name Animation name to search for
     * @return Pointer to animation, or nullptr if not found
     */
    const Animation* getAnimationByName(const std::string& name) const;

    // === Core Data ===

    //! 节点层级（骨骼结构，所有动画共享）
    //!
    //! 这是整个 AnimationAsset 的"骨架"，所有动画片段都基于这套节点结构。
    //! 节点之间通过 parentIndex 形成树状层级关系。
    //!
    //! 示例：Humanoid 骨架可能有 50+ 节点（Root → Spine → Head/LeftArm/RightArm/...）
    std::vector<AnimationNode> nodes;

    //! 多个动画片段（每个 GLB 文件可包含多个动画）
    //!
    //! 设计要点：
    //! - 所有 Animation 共享 nodes 数组（骨骼结构相同）
    //! - 每个 Animation 有独立的 channels/samplers（关键帧数据不同）
    //! - 支持按索引访问（getAnimation）或按名称查找（getAnimationByName）
    //!
    //! 典型值：
    //! - 简单角色：1-3 个动画（idle, walk, run）
    //! - 复杂角色：10+ 个动画（各种动作和表情）
    std::vector<Animation> animations;

    //! 逆绑定矩阵（inverse bind matrices，每个关节一个矩阵）
    //!
    //! 用途：将顶点从 mesh space 变换到 bone space。
    //! 公式：boneSpaceVertex = inverseBindMatrix * meshSpaceVertex
    //!
    //! 数据来源：
    //! - 如果 GLB 包含 skin 数据，会从 skin.inverseBindMatrices 提取
    //! - 如果是 animation-only GLB（export_meshes=False），此数组为空
    //!
    //! 数组大小：
    //! - 如果非空，大小必须等于 joints.size()（validate() 中检查）
    //! - 每个矩阵是 4×4 的列主序 mat4f
    std::vector<filament::math::mat4f> inverseBindMatrices;

    //! 关节索引（参与蒙皮的节点索引）
    //!
    //! 指向 nodes 数组中参与蒙皮的节点。
    //! 例如：joints = [5, 7, 9] 表示 nodes[5]、nodes[7]、nodes[9] 是关节骨骼。
    //!
    //! 关系：
    //! - joints[i] 对应 inverseBindMatrices[i]
    //! - 所有 joints[i] 必须在 [0, nodes.size()-1] 范围内（validate() 中检查）
    //!
    //! 注意：
    //! - animation-only GLB 中此数组可能为空（无蒙皮数据）
    //! - 完整 GLB 中通常不为空（包含蒙皮信息）
    std::vector<int> joints;

private:
    //! 骨骼名称到节点索引的映射（加速查找）
    //!
    //! 用途：
    //! - findNodeByName() 优先使用此哈希表查找，O(1) 时间复杂度
    //! - 如果哈希表为空，降级到线性搜索（O(n)）
    //!
    //! 构建时机：
    //! - AssetLoader::loadAnimationAsset() 加载完成后调用 buildBoneNameMap()
    //! - 手动调用 buildBoneNameMap() 也可以触发构建
    //!
    //! 数据示例：
    //! \code
    //! mBoneNameMap = {
    //!   "Armature"   → 0,
    //!   "Spine"      → 1,
    //!   "Spine.001"  → 2,
    //!   "Head"       → 3,
    //!   ...
    //! }
    //! \endcode
    std::unordered_map<std::string, int> mBoneNameMap;
};

} // namespace filament::gltfio_ext

#endif // GLTFIO_EXT_ANIMATIONASSET_H
