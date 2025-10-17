# 动画理论与技术

本文档深入探讨计算机动画的理论基础和技术实现，为理解 Filament 动画系统提供理论支撑。

## 目录

1. [动画基础概念](#动画基础概念)
2. [关键帧动画](#关键帧动画)
3. [插值理论](#插值理论)
4. [骨骼动画原理](#骨骼动画原理)
5. [顶点变形技术](#顶点变形技术)
6. [动画混合与状态机](#动画混合与状态机)
7. [时间与速度控制](#时间与速度控制)

## 动画基础概念

### 什么是计算机动画？

计算机动画是通过在时间序列上改变对象的属性（位置、旋转、缩放、颜色等）来创建运动幻觉的技术。

### 动画的时间模型

#### 1. 线性时间
- 时间以恒定速度流逝
- 最简单的时间模型
- 适用于大多数基础动画

#### 2. 非线性时间
- 使用缓动函数（Easing Functions）
- 创造更自然的运动感
- 包括加速、减速、弹性等效果

```cpp
// 常见的缓动函数
float easeInQuad(float t) { return t * t; }
float easeOutQuad(float t) { return 1 - (1-t) * (1-t); }
float easeInOutQuad(float t) {
    return t < 0.5f ? 2*t*t : 1 - 2*(1-t)*(1-t);
}
```

### 动画属性类型

#### 1. 标量属性
- 透明度、权重、强度等
- 使用简单的数值插值

#### 2. 向量属性
- 位置、缩放、颜色等
- 分量独立插值

#### 3. 旋转属性
- 需要特殊处理避免万向锁
- 通常使用四元数表示

## 关键帧动画

### 关键帧概念

关键帧动画是动画师在重要时间点设置关键状态，计算机自动计算中间帧的技术。

```
时间:  0s    1s    2s    3s    4s
状态:  A  →  B  →  C  →  D  →  E
       |     |     |     |     |
      关键帧  ←── 插值帧 ──→  关键帧
```

### 关键帧数据结构

```cpp
template<typename T>
struct Keyframe {
    float time;        // 时间戳
    T value;          // 关键帧值
    T inTangent;      // 入切线（用于三次样条）
    T outTangent;     // 出切线
    InterpolationType interpolation;  // 插值类型
};

template<typename T>
class AnimationCurve {
    std::vector<Keyframe<T>> keyframes;

public:
    T evaluate(float time) const;
    void addKeyframe(float time, const T& value);
    void optimize();  // 移除冗余关键帧
};
```

### 关键帧优化

#### 1. 冗余关键帧移除
如果三个连续关键帧在直线上，可以移除中间帧：
```cpp
bool isRedundant(const Keyframe& prev, const Keyframe& current, const Keyframe& next) {
    float t = (current.time - prev.time) / (next.time - prev.time);
    auto interpolated = lerp(prev.value, next.value, t);
    return length(interpolated - current.value) < epsilon;
}
```

#### 2. 时间量化
将时间戳量化到固定精度，减少存储空间：
```cpp
uint16_t quantizeTime(float time, float duration) {
    return static_cast<uint16_t>((time / duration) * 65535);
}
```

## 插值理论

### 插值方法分类

#### 1. 线性插值 (Linear Interpolation)
最简单的插值方法：
```cpp
T lerp(const T& a, const T& b, float t) {
    return a + t * (b - a);
}
```

优点：
- 计算简单，性能高
- 内存占用小
- 预测性强

缺点：
- 在关键帧处有不连续的导数
- 运动不够自然

#### 2. 阶梯插值 (Step Interpolation)
保持前一个关键帧的值：
```cpp
T step(const T& a, const T& b, float t) {
    return a;  // 直到 t = 1 才切换到 b
}
```

适用场景：
- 离散状态切换
- 材质属性切换
- 可见性控制

#### 3. 三次样条插值 (Cubic Spline)
使用三次贝塞尔曲线：
```cpp
T cubicSpline(const T& v0, const T& t0, const T& v1, const T& t1, float t) {
    float t2 = t * t;
    float t3 = t2 * t;

    // 三次 Hermite 多项式基函数
    float h00 = 2*t3 - 3*t2 + 1;   // v0 的权重
    float h10 = t3 - 2*t2 + t;     // t0 的权重
    float h01 = -2*t3 + 3*t2;      // v1 的权重
    float h11 = t3 - t2;           // t1 的权重

    return h00 * v0 + h10 * t0 + h01 * v1 + h11 * t1;
}
```

优点：
- C1 连续性（切线连续）
- 更自然的运动曲线
- 支持切线控制

缺点：
- 计算复杂度高
- 需要额外的切线数据

### 四元数插值

#### SLERP (Spherical Linear Interpolation)
用于旋转的球面线性插值：
```cpp
quatf slerp(const quatf& q1, const quatf& q2, float t) {
    float dot = q1.x*q2.x + q1.y*q2.y + q1.z*q2.z + q1.w*q2.w;

    // 选择较短路径
    quatf q2_corrected = (dot < 0) ? -q2 : q2;
    dot = abs(dot);

    if (dot > 0.9995f) {
        // 接近平行时使用线性插值
        return normalize(lerp(q1, q2_corrected, t));
    }

    float theta = acos(dot);
    float sinTheta = sin(theta);
    float w1 = sin((1-t) * theta) / sinTheta;
    float w2 = sin(t * theta) / sinTheta;

    return w1 * q1 + w2 * q2_corrected;
}
```

#### SQUAD (Spherical Quadrangle)
四元数的三次插值：
```cpp
quatf squad(const quatf& q1, const quatf& a, const quatf& b, const quatf& q2, float t) {
    quatf slerp1 = slerp(q1, q2, t);
    quatf slerp2 = slerp(a, b, t);
    return slerp(slerp1, slerp2, 2*t*(1-t));
}
```

## 骨骼动画原理

### 骨骼层次结构

骨骼系统是一个树形结构，每个骨骼都有父子关系：

```
Root
├── Spine
│   ├── Chest
│   │   ├── LeftShoulder
│   │   │   ├── LeftUpperArm
│   │   │   └── LeftForearm
│   │   │       └── LeftHand
│   │   └── RightShoulder
│   │       ├── RightUpperArm
│   │       └── RightForearm
│   │           └── RightHand
│   └── Neck
│       └── Head
```

### 正向运动学 (Forward Kinematics)

从根骨骼开始，依次计算每个子骨骼的世界变换：

```cpp
void updateBoneTransforms(Bone& bone, const mat4f& parentTransform) {
    // 计算当前骨骼的世界变换
    bone.worldTransform = parentTransform * bone.localTransform;

    // 递归更新子骨骼
    for (auto& child : bone.children) {
        updateBoneTransforms(child, bone.worldTransform);
    }
}
```

### 蒙皮 (Skinning)

将顶点绑定到多个骨骼，通过权重混合：

```cpp
// 顶点蒙皮公式
vec3 skinnedPosition = vec3(0);
vec3 skinnedNormal = vec3(0);

for (int i = 0; i < 4; ++i) {  // 通常最多 4 个骨骼影响
    int boneIndex = int(boneIndices[i]);
    float weight = boneWeights[i];

    if (weight > 0) {
        mat4 boneMatrix = bones[boneIndex] * inverseBindMatrices[boneIndex];
        skinnedPosition += weight * (boneMatrix * vec4(position, 1.0)).xyz;
        skinnedNormal += weight * (mat3(boneMatrix) * normal);
    }
}
```

### 绑定姿势 (Bind Pose)

绑定姿势是骨骼系统的初始状态，定义了：
- 每个骨骼的初始位置和方向
- 逆绑定矩阵（Inverse Bind Matrix）
- 顶点到骨骼的权重分配

```cpp
// 计算逆绑定矩阵
mat4f computeInverseBindMatrix(const Bone& bone) {
    return inverse(bone.bindPoseWorldTransform);
}
```

## 顶点变形技术

### Morph Targets (形状关键帧)

通过混合不同的顶点位置来实现变形：

```cpp
// 顶点变形公式
vec3 morphedPosition = basePosition;
vec3 morphedNormal = baseNormal;

for (int i = 0; i < numMorphTargets; ++i) {
    float weight = morphWeights[i];
    morphedPosition += weight * morphTargetPositions[i];
    morphedNormal += weight * morphTargetNormals[i];
}
```

### Morph Target 优化

#### 1. 稀疏存储
只存储与基础网格不同的顶点：
```cpp
struct SparseMorphTarget {
    std::vector<uint32_t> vertexIndices;  // 变形的顶点索引
    std::vector<vec3> deltaPositions;     // 位置差值
    std::vector<vec3> deltaNormals;       // 法线差值
};
```

#### 2. 压缩存储
使用 16 位或 8 位整数存储差值：
```cpp
struct CompressedMorphTarget {
    vec3 boundsMin, boundsMax;  // 用于反量化
    std::vector<uint16_t> quantizedDeltas;
};

vec3 dequantize(uint16_t quantized, vec3 boundsMin, vec3 boundsMax) {
    float t = quantized / 65535.0f;
    return mix(boundsMin, boundsMax, t);
}
```

## 动画混合与状态机

### 动画混合类型

#### 1. 线性混合 (Linear Blending)
最简单的混合方式：
```cpp
Transform blend(const Transform& a, const Transform& b, float weight) {
    Transform result;
    result.translation = lerp(a.translation, b.translation, weight);
    result.rotation = slerp(a.rotation, b.rotation, weight);
    result.scale = lerp(a.scale, b.scale, weight);
    return result;
}
```

#### 2. 遮罩混合 (Masked Blending)
只混合指定的骨骼：
```cpp
void blendWithMask(Skeleton& target, const Skeleton& source,
                   const BoneMask& mask, float weight) {
    for (int i = 0; i < target.bones.size(); ++i) {
        if (mask.isEnabled(i)) {
            target.bones[i] = blend(target.bones[i], source.bones[i], weight);
        }
    }
}
```

#### 3. 加法混合 (Additive Blending)
在基础动画上叠加其他动画：
```cpp
Transform additiveBlend(const Transform& base, const Transform& additive,
                       const Transform& reference, float weight) {
    // 计算相对于参考姿势的差值
    Transform delta;
    delta.translation = additive.translation - reference.translation;
    delta.rotation = additive.rotation * inverse(reference.rotation);
    delta.scale = additive.scale / reference.scale;

    // 应用到基础变换
    Transform result = base;
    result.translation += weight * delta.translation;
    result.rotation = slerp(quatf(), delta.rotation, weight) * result.rotation;
    result.scale *= lerp(vec3(1), delta.scale, weight);

    return result;
}
```

### 动画状态机

#### 状态定义
```cpp
struct AnimationState {
    int animationIndex;
    float speed = 1.0f;
    bool looping = true;
    float fadeInTime = 0.3f;
    float fadeOutTime = 0.3f;
};

struct Transition {
    int fromState, toState;
    float duration;
    std::function<bool()> condition;  // 转换条件
};
```

#### 状态机更新
```cpp
class AnimationStateMachine {
    std::vector<AnimationState> states;
    std::vector<Transition> transitions;
    int currentState = 0;
    float stateTime = 0;

public:
    void update(float deltaTime) {
        stateTime += deltaTime;

        // 检查转换条件
        for (const auto& transition : transitions) {
            if (transition.fromState == currentState &&
                transition.condition()) {
                startTransition(transition.toState, transition.duration);
                break;
            }
        }

        // 更新当前状态
        updateCurrentState(deltaTime);
    }
};
```

## 时间与速度控制

### 时间缩放

```cpp
class AnimationTimeController {
    float globalTimeScale = 1.0f;
    float animationSpeed = 1.0f;

public:
    float getScaledDeltaTime(float deltaTime) {
        return deltaTime * globalTimeScale * animationSpeed;
    }

    void setGlobalTimeScale(float scale) { globalTimeScale = scale; }
    void setAnimationSpeed(float speed) { animationSpeed = speed; }
};
```

### 时间扭曲

使用曲线控制时间流逝：
```cpp
class TimeWarpCurve {
    std::vector<vec2> controlPoints;  // (input_time, output_time) 对

public:
    float evaluate(float inputTime) {
        // 在控制点之间插值
        for (size_t i = 1; i < controlPoints.size(); ++i) {
            if (inputTime <= controlPoints[i].x) {
                float t = (inputTime - controlPoints[i-1].x) /
                         (controlPoints[i].x - controlPoints[i-1].x);
                return lerp(controlPoints[i-1].y, controlPoints[i].y, t);
            }
        }
        return inputTime;  // 超出范围时返回原始时间
    }
};
```

### 同步播放

多个动画同步播放的技术：
```cpp
class SynchronizedAnimationGroup {
    struct SyncedAnimation {
        int animationIndex;
        float normalizedTime;  // [0, 1] 范围
        float originalDuration;
    };

    std::vector<SyncedAnimation> animations;
    float groupTime = 0;

public:
    void update(float deltaTime) {
        groupTime += deltaTime;

        for (auto& anim : animations) {
            // 将组时间映射到各个动画的归一化时间
            anim.normalizedTime = fmod(groupTime / anim.originalDuration, 1.0f);
        }
    }
};
```

## 性能优化技巧

### 1. 层次细节 (LOD)

根据距离或重要性调整动画质量：
```cpp
enum class AnimationLOD {
    High,    // 全精度动画
    Medium,  // 减少骨骼数量
    Low,     // 只更新主要骨骼
    Static   // 静态姿势
};

void updateAnimationLOD(const Entity& entity, const Camera& camera) {
    float distance = length(getPosition(entity) - camera.getPosition());

    if (distance < 10.0f) {
        setAnimationLOD(entity, AnimationLOD::High);
    } else if (distance < 50.0f) {
        setAnimationLOD(entity, AnimationLOD::Medium);
    } else if (distance < 100.0f) {
        setAnimationLOD(entity, AnimationLOD::Low);
    } else {
        setAnimationLOD(entity, AnimationLOD::Static);
    }
}
```

### 2. 时间量化

减少时间精度以提高缓存效率：
```cpp
float quantizeTime(float time, float precision = 1.0f/60.0f) {
    return floor(time / precision) * precision;
}
```

### 3. 早期退出

当动画权重很小时跳过计算：
```cpp
if (animationWeight < 0.001f) {
    return;  // 跳过这个动画的计算
}
```

## 总结

动画理论涵盖了广泛的数学和算法概念：

1. **插值理论**: 线性、三次样条、球面线性插值
2. **骨骼动画**: 正向运动学、蒙皮、绑定姿势
3. **顶点变形**: Morph targets、压缩存储
4. **动画混合**: 线性、遮罩、加法混合
5. **状态管理**: 状态机、转换条件
6. **时间控制**: 缩放、扭曲、同步
7. **性能优化**: LOD、量化、早期退出

这些理论为实现高效、灵活的动画系统提供了坚实的基础，也是理解 Filament 动画实现的关键。