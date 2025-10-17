# Phase 2 - 动画系统增强

**优先级**: P1-2 (CrossFade) + P1-3 (CUBIC)
**预计工作量**: 3-5 个工作日
**依赖**: 无（可与 MeshInstance 并行开发）
**目标**: 提升动画质量，支持平滑过渡和高质量插值

---

## 1. 问题分析

### 1.1 P1-2: CrossFade 缺失

**问题表现**:
```cpp
// 当前实现（生硬切换）
animator->stopAnimation(idleAnimId);
animator->playAnimation(runAnimId, 0);  // ❌ 瞬间切换，不自然
```

**效果**:
- 从 idle 到 run 瞬间切换，角色会"跳变"
- 用户体验差（看起来像卡顿）

**期望效果**:
- 从 idle 到 run 平滑过渡（1 秒渐变）
- 两个动画的权重自动调整：
  - 0.0s: idle=100%, run=0%
  - 0.5s: idle=50%, run=50%
  - 1.0s: idle=0%, run=100%

**业界对比**:
- **Unity**: `Animator.CrossFade("Run", 0.5f)`
- **Unreal Engine**: `PlaySlotAnimationAsDynamicMontage(..., BlendTime=0.5f)`
- **gltfio_ext（当前）**: ❌ 不支持

### 1.2 P1-3: CUBIC 插值缺失

**问题表现**:

当前实现只支持 LINEAR 插值，对 CUBIC 类型的关键帧强制使用 LINEAR：

```cpp
// 当前实现
float3 interpolateTranslation(float t, const Keyframe& k0, const Keyframe& k1) {
    // ❌ 无论 interpolation 类型是什么，都用 LINEAR
    return lerp(k0.value, k1.value, t);
}
```

**glTF 支持的插值类型**:
- **STEP**: 阶跃（无插值）
- **LINEAR**: 线性插值 ✅ 已支持
- **CUBICSPLINE**: 三次样条插值 ❌ 未支持

**CUBIC 的优势**:
- 更平滑的曲线（特别是旋转动画）
- 支持缓入缓出（ease-in/ease-out）
- 高质量动画必备

**对比**:
```
LINEAR 插值:   A ----直线---- B
CUBIC 插值:    A ~~~曲线~~~ B  (更自然)
```

**实际影响**:
- 角色挥剑动画：LINEAR 看起来机械，CUBIC 更流畅
- 相机动画：LINEAR 会有"停顿感"，CUBIC 更电影化

---

## 2. 技术背景

### 2.1 CrossFade 原理

**核心思想**: 同时播放两个动画，动态调整权重

```
最终变换 = Transform(Anim1) × weight1 + Transform(Anim2) × weight2
其中: weight1 + weight2 = 1.0
```

**时间轴**:
```
t=0.0s    t=0.5s    t=1.0s
│         │         │
idle 100% ─ 50% ──── 0% ─── (停止)
run   0% ── 50% ──── 100%
```

**实现要点**:
1. 同时 `playAnimation()` 两个动画，设置不同权重
2. 每帧更新权重（线性过渡）
3. 过渡完成后停止旧动画

### 2.2 CUBIC 插值原理

**glTF 的 CUBICSPLINE 格式**:

每个关键帧存储 3 个值（而非 1 个）：
```
KeyframeData = [in-tangent, value, out-tangent]
```

**Hermite 样条插值公式**:
```cpp
// t ∈ [0, 1]
value(t) = (2t³ - 3t² + 1) × p0
         + (t³ - 2t² + t) × m0
         + (-2t³ + 3t²) × p1
         + (t³ - t²) × m1

其中:
  p0 = value0 (第一个关键帧的值)
  p1 = value1 (第二个关键帧的值)
  m0 = Δt × out-tangent0 (第一个关键帧的出切线)
  m1 = Δt × in-tangent1 (第二个关键帧的入切线)
  Δt = time1 - time0 (时间间隔)
```

**图示**:
```
        out-tangent0
       ↗
  p0 ●────────────● p1
                 ↙
            in-tangent1
```

---

## 3. P1-2: CrossFade 实现

### 3.1 API 设计

```cpp
// StandaloneAnimator.h

class StandaloneAnimator {
public:
    /**
     * 交叉淡入淡出到新动画
     * @param fromAnimId 当前动画 ID（从 playAnimation 返回）
     * @param toAnimId 目标动画 ID（从 playAnimation 返回）
     * @param duration 过渡时长（秒）
     *
     * 说明：
     * - 自动管理两个动画的权重
     * - 过渡完成后自动停止 fromAnim
     * - 如果 fromAnimId = -1，则从当前姿态淡入
     */
    void crossFade(int fromAnimId, int toAnimId, float duration);

    /**
     * 检查是否正在进行 CrossFade
     * @return true 如果正在过渡
     */
    bool isCrossFading() const;

    /**
     * 获取 CrossFade 进度
     * @return 进度 [0, 1]，如果没有进行 CrossFade 则返回 -1
     */
    float getCrossFadeProgress() const;

private:
    struct CrossFadeState {
        int fromAnimId;
        int toAnimId;
        float duration;
        float elapsed;
        bool active;
    };

    CrossFadeState mCrossFade;
};
```

### 3.2 实现步骤

**步骤 1: 添加 CrossFadeState**

**修改文件**: `libs/gltfio_ext/src/StandaloneAnimator.cpp`

```cpp
struct AnimatorImpl {
    // ... 现有字段 ...

    struct CrossFadeState {
        int fromAnimId = -1;
        int toAnimId = -1;
        float duration = 0.0f;
        float elapsed = 0.0f;
        bool active = false;
    };

    CrossFadeState mCrossFade;
};
```

**步骤 2: 实现 crossFade()**

```cpp
void StandaloneAnimator::crossFade(int fromAnimId, int toAnimId, float duration) {
    auto& impl = *mImpl;

    // 1. 查找动画
    auto* fromAnim = findAnimationState(fromAnimId);
    auto* toAnim = findAnimationState(toAnimId);

    ASSERT_PRECONDITION(toAnim, "Invalid toAnimId");

    // 2. 初始化 CrossFade 状态
    impl.mCrossFade.fromAnimId = fromAnimId;
    impl.mCrossFade.toAnimId = toAnimId;
    impl.mCrossFade.duration = duration;
    impl.mCrossFade.elapsed = 0.0f;
    impl.mCrossFade.active = true;

    // 3. 设置初始权重
    if (fromAnim) {
        fromAnim->weight = 1.0f;
    }
    toAnim->weight = 0.0f;

    // 4. 确保目标动画正在播放
    if (!toAnim->playing) {
        toAnim->playing = true;
        toAnim->time = 0.0f;
    }
}
```

**步骤 3: 修改 update() 更新权重**

```cpp
void AnimatorImpl::update(float deltaTime) {
    // 1. 更新 CrossFade
    if (mCrossFade.active) {
        mCrossFade.elapsed += deltaTime;
        float t = std::min(1.0f, mCrossFade.elapsed / mCrossFade.duration);

        // 计算权重
        float fromWeight = 1.0f - t;
        float toWeight = t;

        // 应用权重
        auto* fromAnim = findAnimationState(mCrossFade.fromAnimId);
        auto* toAnim = findAnimationState(mCrossFade.toAnimId);

        if (fromAnim) {
            fromAnim->weight = fromWeight;
        }
        if (toAnim) {
            toAnim->weight = toWeight;
        }

        // 过渡完成
        if (t >= 1.0f) {
            if (fromAnim) {
                fromAnim->playing = false;  // 停止旧动画
            }
            mCrossFade.active = false;
        }
    }

    // 2. 更新所有动画（现有逻辑）
    for (auto& state : mAnimationStates) {
        if (state.playing) {
            state.time += deltaTime * state.speed;
            // ... 循环/夹断处理 ...
        }
    }

    // 3. 应用动画到骨骼
    applyAnimations();
}
```

**工作量**: 1.5 天

### 3.3 使用示例

```cpp
// 1. 播放初始动画
int idleAnimId = animator->playAnimation(idleAnim, 0, 1.0f, true);

// 2. 1 秒后切换到 run（0.5 秒过渡）
int runAnimId = animator->playAnimation(runAnim, 0, 0.0f, true);  // 初始权重 0
animator->crossFade(idleAnimId, runAnimId, 0.5f);

// 3. 更新循环
while (rendering) {
    animator->update(deltaTime);

    // 检查进度
    if (animator->isCrossFading()) {
        float progress = animator->getCrossFadeProgress();
        printf("CrossFade progress: %.2f\n", progress);
    }

    meshInstance->updateSkinning(animator);
}
```

---

## 4. P1-3: CUBIC 插值实现

### 4.1 数据结构扩展

**修改文件**: `libs/gltfio_ext/src/StandaloneAnimator.cpp`

**当前结构**:
```cpp
struct Keyframe {
    float time;
    float3 value;  // ❌ 只存储 value
};
```

**修改后**:
```cpp
struct Keyframe {
    float time;
    float3 value;
    float3 inTangent;   // CUBIC 插值需要
    float3 outTangent;  // CUBIC 插值需要
};

// 每个通道需要记录插值类型
struct Channel {
    // ... 现有字段 ...
    cgltf_interpolation_type interpolation;  // STEP, LINEAR, CUBICSPLINE
};
```

### 4.2 读取 CUBIC 数据

**修改文件**: `libs/gltfio_ext/src/AssetLoaderExt.cpp`

**位置**: 解析 AnimationAsset 时

```cpp
// 读取关键帧
cgltf_animation_sampler* sampler = channel->sampler;
cgltf_accessor* inputAccessor = sampler->input;   // 时间
cgltf_accessor* outputAccessor = sampler->output; // 值

cgltf_interpolation_type interpolation = sampler->interpolation;
channelData.interpolation = interpolation;

size_t keyframeCount = inputAccessor->count;
channelData.keyframes.resize(keyframeCount);

if (interpolation == cgltf_interpolation_type_cubic_spline) {
    // CUBIC: outputAccessor 包含 [in-tangent, value, out-tangent] × keyframeCount
    ASSERT_PRECONDITION(outputAccessor->count == keyframeCount * 3,
                        "CUBIC sampler output size mismatch");

    for (size_t i = 0; i < keyframeCount; i++) {
        // 读取时间
        cgltf_accessor_read_float(inputAccessor, i, &channelData.keyframes[i].time, 1);

        // 读取 in-tangent, value, out-tangent
        size_t baseIndex = i * 3;
        cgltf_accessor_read_float(outputAccessor, baseIndex + 0,
                                  &channelData.keyframes[i].inTangent.x, 3);
        cgltf_accessor_read_float(outputAccessor, baseIndex + 1,
                                  &channelData.keyframes[i].value.x, 3);
        cgltf_accessor_read_float(outputAccessor, baseIndex + 2,
                                  &channelData.keyframes[i].outTangent.x, 3);
    }
} else {
    // LINEAR 或 STEP
    for (size_t i = 0; i < keyframeCount; i++) {
        cgltf_accessor_read_float(inputAccessor, i, &channelData.keyframes[i].time, 1);
        cgltf_accessor_read_float(outputAccessor, i, &channelData.keyframes[i].value.x, 3);
        // inTangent 和 outTangent 保持 0
    }
}
```

**工作量**: 1 天

### 4.3 实现 CUBIC 插值函数

**新建辅助函数**: `libs/gltfio_ext/src/StandaloneAnimator.cpp`

```cpp
namespace {

/**
 * Hermite 样条插值（用于 CUBIC 插值）
 * @param t 插值参数 [0, 1]
 * @param p0 第一个关键帧的值
 * @param m0 第一个关键帧的出切线（已乘以 Δt）
 * @param p1 第二个关键帧的值
 * @param m1 第二个关键帧的入切线（已乘以 Δt）
 * @return 插值结果
 */
float3 cubicHermiteSpline(float t, const float3& p0, const float3& m0,
                         const float3& p1, const float3& m1) {
    float t2 = t * t;
    float t3 = t2 * t;

    // Hermite 基函数
    float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    float h10 = t3 - 2.0f * t2 + t;
    float h01 = -2.0f * t3 + 3.0f * t2;
    float h11 = t3 - t2;

    return h00 * p0 + h10 * m0 + h01 * p1 + h11 * m1;
}

/**
 * Quaternion 的 CUBIC 插值（特殊处理）
 */
quatf cubicHermiteSplineQuat(float t, const quatf& q0, const quatf& outTangent0,
                             const quatf& q1, const quatf& inTangent1, float dt) {
    // 注意：Quaternion 的 CUBIC 插值较复杂，需要在对数空间进行
    // 简化实现：退化为 slerp（更安全）
    // 完整实现需要参考：https://theory.org/software/qfa/writeup/node12.html

    return slerp(q0, q1, t);  // 简化版
}

} // anonymous namespace
```

### 4.4 修改 applyAnimation() 使用 CUBIC 插值

**修改文件**: `libs/gltfio_ext/src/StandaloneAnimator.cpp`

```cpp
void AnimatorImpl::applyAnimation(const AnimationState& state) {
    // ... 现有代码 ...

    // 对每个通道应用动画
    for (const auto& channel : state.animation->channels) {
        // 1. 找到当前时间对应的两个关键帧
        size_t k0, k1;
        float t;
        findKeyframes(channel.keyframes, state.time, k0, k1, t);

        const Keyframe& keyframe0 = channel.keyframes[k0];
        const Keyframe& keyframe1 = channel.keyframes[k1];

        // 2. 根据插值类型进行插值
        float3 interpolatedValue;

        switch (channel.interpolation) {
            case cgltf_interpolation_type_step:
                interpolatedValue = keyframe0.value;
                break;

            case cgltf_interpolation_type_linear:
                interpolatedValue = lerp(keyframe0.value, keyframe1.value, t);
                break;

            case cgltf_interpolation_type_cubic_spline: {
                // CUBIC 插值
                float dt = keyframe1.time - keyframe0.time;
                float3 m0 = keyframe0.outTangent * dt;
                float3 m1 = keyframe1.inTangent * dt;

                interpolatedValue = cubicHermiteSpline(t,
                    keyframe0.value, m0,
                    keyframe1.value, m1);
                break;
            }
        }

        // 3. 应用到目标节点（现有逻辑）
        applyChannelValue(channel, interpolatedValue, state.weight);
    }
}
```

**工作量**: 1.5 天

### 4.5 处理 Rotation 的 CUBIC 插值

**注意**: Rotation 使用 quaternion，需要特殊处理

```cpp
// 对于 Rotation 通道
if (channel.targetPath == cgltf_animation_path_type_rotation) {
    quatf interpolatedQuat;

    switch (channel.interpolation) {
        case cgltf_interpolation_type_step:
            interpolatedQuat = keyframe0.quatValue;
            break;

        case cgltf_interpolation_type_linear:
            interpolatedQuat = slerp(keyframe0.quatValue, keyframe1.quatValue, t);
            break;

        case cgltf_interpolation_type_cubic_spline: {
            // 简化实现：退化为 slerp
            interpolatedQuat = slerp(keyframe0.quatValue, keyframe1.quatValue, t);

            // 完整实现（可选）：
            // interpolatedQuat = cubicHermiteSplineQuat(t, keyframe0.quatValue,
            //     keyframe0.outTangentQuat, keyframe1.quatValue,
            //     keyframe1.inTangentQuat, dt);
            break;
        }
    }

    // 应用权重混合
    // ...
}
```

---

## 5. 测试计划

### 5.1 CrossFade 测试

**单元测试** (`test_crossfade.cpp`):

```cpp
TEST(CrossFade, WeightInterpolation) {
    StandaloneAnimator* animator = StandaloneAnimator::create(skeleton);

    int anim1 = animator->playAnimation(idleAnim, 0, 1.0f, true);
    int anim2 = animator->playAnimation(runAnim, 0, 0.0f, true);

    animator->crossFade(anim1, anim2, 1.0f);

    // t=0.0s
    EXPECT_TRUE(animator->isCrossFading());
    EXPECT_FLOAT_EQ(animator->getCrossFadeProgress(), 0.0f);

    // t=0.5s
    animator->update(0.5f);
    EXPECT_FLOAT_EQ(animator->getCrossFadeProgress(), 0.5f);
    // 验证权重
    EXPECT_FLOAT_EQ(getAnimationWeight(anim1), 0.5f);
    EXPECT_FLOAT_EQ(getAnimationWeight(anim2), 0.5f);

    // t=1.0s
    animator->update(0.5f);
    EXPECT_FALSE(animator->isCrossFading());
    EXPECT_FLOAT_EQ(getAnimationWeight(anim1), 0.0f);
    EXPECT_FLOAT_EQ(getAnimationWeight(anim2), 1.0f);
    EXPECT_FALSE(isAnimationPlaying(anim1));  // 旧动画已停止
}
```

**视觉测试**:
- 加载角色 + idle 动画 + run 动画
- 调用 `crossFade(idle, run, 0.5f)`
- 观察过渡是否平滑（无跳变）

### 5.2 CUBIC 插值测试

**单元测试** (`test_cubic_interpolation.cpp`):

```cpp
TEST(CubicInterpolation, HermiteSpline) {
    // 测试 Hermite 样条函数
    float3 p0 = {0, 0, 0};
    float3 m0 = {1, 0, 0};
    float3 p1 = {2, 0, 0};
    float3 m1 = {1, 0, 0};

    // t=0 应该等于 p0
    float3 v0 = cubicHermiteSpline(0.0f, p0, m0, p1, m1);
    EXPECT_FLOAT_EQ(v0.x, 0.0f);

    // t=1 应该等于 p1
    float3 v1 = cubicHermiteSpline(1.0f, p0, m0, p1, m1);
    EXPECT_FLOAT_EQ(v1.x, 2.0f);

    // t=0.5 应该在 p0 和 p1 之间（但不一定是中点）
    float3 v05 = cubicHermiteSpline(0.5f, p0, m0, p1, m1);
    EXPECT_GT(v05.x, 0.0f);
    EXPECT_LT(v05.x, 2.0f);
}

TEST(CubicInterpolation, LoadCubicAnimation) {
    // 加载包含 CUBIC 插值的动画
    AnimationAsset* anim = loader->loadAnimation(cubicAnimData, size);

    // 验证插值类型
    EXPECT_EQ(anim->getChannel(0).interpolation, cgltf_interpolation_type_cubic_spline);

    // 验证关键帧数据
    const Keyframe& kf = anim->getChannel(0).keyframes[0];
    EXPECT_NE(kf.inTangent, float3{0, 0, 0});  // 切线不为 0
}
```

**对比测试**:
- 使用 glTF Viewer 查看 CUBIC 动画
- 使用 gltfio_ext 渲染同一动画
- 对比曲线是否一致（可以导出关键帧数据对比）

---

## 6. 验收标准

### 6.1 CrossFade 验收

- ✅ `crossFade()` API 可用
- ✅ 权重正确插值（0 → 1）
- ✅ 过渡完成后旧动画自动停止
- ✅ 视觉效果平滑（无跳变）
- ✅ idle → run → idle 多次切换无问题

### 6.2 CUBIC 验收

- ✅ 正确读取 CUBIC 格式数据（in-tangent + value + out-tangent）
- ✅ Hermite 样条插值计算正确
- ✅ Translation 通道的 CUBIC 插值生效
- ✅ Rotation 通道的 CUBIC 插值生效（或退化为 slerp）
- ✅ 与 glTF Viewer 渲染效果一致

### 6.3 性能验收

- ✅ CrossFade 无性能回退（每帧 < 0.1ms 额外开销）
- ✅ CUBIC 插值耗时可接受（相比 LINEAR 慢 < 2x）

---

## 7. 使用示例汇总

### 7.1 CrossFade 示例

```cpp
// 场景：角色从 idle 切换到 run

// 1. 创建动画器
StandaloneAnimator* animator = StandaloneAnimator::create(skeleton);

// 2. 播放初始动画
int idleAnimId = animator->playAnimation(idleAnim, 0, 1.0f, true);

// 3. 用户按下 W 键（前进）
if (input.isKeyPressed('W')) {
    int runAnimId = animator->playAnimation(runAnim, 0, 0.0f, true);
    animator->crossFade(idleAnimId, runAnimId, 0.3f);  // 0.3 秒过渡
}

// 4. 用户松开 W 键（停止）
if (input.isKeyReleased('W')) {
    int idleAnimId = animator->playAnimation(idleAnim, 0, 0.0f, true);
    int runAnimId = getCurrentAnimationId();  // 假设有这个函数
    animator->crossFade(runAnimId, idleAnimId, 0.3f);
}

// 5. 更新循环
while (rendering) {
    animator->update(deltaTime);
    meshInstance->updateSkinning(animator);
}
```

### 7.2 CUBIC 动画示例

```cpp
// 加载包含 CUBIC 插值的动画（如：相机动画）
AnimationAsset* cameraAnim = loader->loadAnimation(cameraAnimData, size);

// 播放（自动使用 CUBIC 插值）
animator->playAnimation(cameraAnim, 0, 1.0f, false);

// 更新循环
while (rendering) {
    animator->update(deltaTime);

    // 获取相机变换（应用 CUBIC 插值结果）
    const mat4f* transforms = animator->getNodeTransforms();
    // ...
}
```

---

## 8. 风险和缓解

### 8.1 风险 1: CrossFade 权重混合错误

**影响**: 动画过渡不自然（抖动、跳变）
**概率**: 中
**缓解**:
- 单元测试验证权重计算
- 视觉测试多个场景（idle→run, run→idle, idle→attack）

### 8.2 风险 2: CUBIC 插值计算错误

**影响**: 动画曲线不正确
**概率**: 中
**缓解**:
- 参考 glTF 规范和 Hermite 样条标准公式
- 单元测试边界情况（t=0, t=1）
- 对比测试（与 glTF Viewer 对比）

### 8.3 风险 3: Quaternion CUBIC 插值复杂

**影响**: Rotation 的 CUBIC 插值不正确
**概率**: 高
**缓解**:
- 简化实现：退化为 slerp（先保证正确性）
- 完整实现作为 P2 优化项

---

## 9. 实现优先级

### 9.1 推荐顺序

1. **P1-2: CrossFade**（优先）
   - 用户体验提升明显
   - 实现相对简单
   - 工作量: 1.5 天

2. **P1-3: CUBIC 插值（Translation + Scale）**
   - 实现 Hermite 样条
   - 支持 Translation 和 Scale 通道
   - 工作量: 1.5 天

3. **P1-3: CUBIC 插值（Rotation）**（可选）
   - 实现 Quaternion 的 CUBIC 插值
   - 或退化为 slerp
   - 工作量: 1 天

### 9.2 简化版（最小可用）

如果时间紧张，可以先做：
- ✅ CrossFade（必须）
- ✅ CUBIC Translation（必须）
- ❌ CUBIC Rotation（退化为 slerp）

---

## 10. 里程碑

| 里程碑 | 完成标准 | 预计时间 |
|--------|---------|---------|
| **M1: CrossFade 完成** | crossFade() API 可用，测试通过 | 第 2 天 |
| **M2: CUBIC Translation 完成** | Hermite 样条实现，Translation 通道生效 | 第 4 天 |
| **M3: CUBIC Rotation 完成** | Rotation 通道支持 CUBIC（或 slerp） | 第 5 天 |

---

## 11. 参考资料

1. **glTF 动画规范**: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#animations
2. **Hermite 样条**: https://en.wikipedia.org/wiki/Cubic_Hermite_spline
3. **Quaternion CUBIC 插值**: https://theory.org/software/qfa/writeup/node12.html
4. **Unity CrossFade 文档**: https://docs.unity3d.com/ScriptReference/Animator.CrossFade.html

---

**下一步**: 从 CrossFade 开始实现（优先级更高，实现更简单）
