# 扩展策略与方案

本文档介绍如何扩展 Filament 动画系统，包括架构扩展点、性能优化和未来发展方向。

## 目录

1. [架构扩展点](#架构扩展点)
2. [动画类型扩展](#动画类型扩展)
3. [性能优化方案](#性能优化方案)
4. [高级动画功能](#高级动画功能)
5. [工具链扩展](#工具链扩展)
6. [未来发展路线](#未来发展路线)

## 架构扩展点

### 1. 继承和扩展 Animator

```cpp
// 扩展 Animator 类添加新功能
class ExtendedAnimator : public Animator {
public:
    // 动画事件系统
    void addAnimationEvent(float time, std::function<void()> callback);
    
    // 动画混合树
    void setBlendTree(std::unique_ptr<BlendTree> tree);
    
    // IK 求解器
    void addIKConstraint(Entity effector, Entity target, IKType type);
    
    // 物理模拟集成
    void enablePhysicsBlend(bool enabled);
    
    // 动画压缩
    void enableCompressionCodec(CompressionType type);
};
```

### 2. 扩展插值类型

```cpp
// 在 Sampler 结构中添加新的插值类型
struct Sampler {
    TimeValues times;
    SourceValues values;
    enum InterpolationType {
        LINEAR,
        STEP, 
        CUBIC,
        // 新增的插值类型
        BEZIER,        // 贝塞尔曲线
        ELASTIC,       // 弹性插值
        BOUNCE,        // 弹跳插值
        CATMULL_ROM,   // Catmull-Rom 样条
        B_SPLINE       // B 样条
    } interpolation;
};

// 插值函数实现
float evaluateBezier(float p0, float p1, float p2, float p3, float t) {
    float u = 1 - t;
    return u*u*u*p0 + 3*u*u*t*p1 + 3*u*t*t*p2 + t*t*t*p3;
}

float evaluateElastic(float start, float end, float t) {
    if (t == 0) return start;
    if (t == 1) return end;
    
    float p = 0.3f;
    float s = p / 4;
    return start + (end - start) * (pow(2, -10*t) * sin((t-s)*(2*PI)/p) + 1);
}
```

### 3. 自定义动画提供者

```cpp
// 抽象动画提供者接口
class AnimationProvider {
public:
    virtual ~AnimationProvider() = default;
    
    // 获取动画数据
    virtual AnimationData getAnimationData(size_t index, float time) = 0;
    
    // 获取动画数量
    virtual size_t getAnimationCount() const = 0;
    
    // 获取动画信息
    virtual AnimationInfo getAnimationInfo(size_t index) const = 0;
};

// 程序化动画提供者
class ProceduralAnimationProvider : public AnimationProvider {
    std::vector<std::function<Transform(float)>> mAnimationFunctions;
    
public:
    void addSinusoidalAnimation(float frequency, float amplitude) {
        mAnimationFunctions.push_back([frequency, amplitude](float time) {
            return Transform{
                .translation = {0, amplitude * sin(frequency * time), 0},
                .rotation = quatf(),
                .scale = {1, 1, 1}
            };
        });
    }
    
    void addNoiseBasedAnimation(int seed, float scale) {
        mAnimationFunctions.push_back([seed, scale](float time) {
            // 使用 Perlin 噪声生成动画
            float3 offset = perlinNoise3D(time, seed) * scale;
            return Transform{
                .translation = offset,
                .rotation = quatf(),
                .scale = {1, 1, 1}
            };
        });
    }
};
```

## 动画类型扩展

### 1. 添加新的变换类型

```cpp
// 扩展 Channel::transformType 枚举
enum class ExtendedTransformType {
    TRANSLATION,
    ROTATION,
    SCALE,
    WEIGHTS,
    // 新增类型
    COLOR,           // 颜色动画
    VISIBILITY,      // 可见性动画
    MATERIAL_PARAM,  // 材质参数动画
    LIGHT_INTENSITY, // 光照强度动画
    CAMERA_FOV,      // 摄像机视角动画
    CUSTOM_FLOAT,    // 自定义浮点数动画
    CUSTOM_VECTOR    // 自定义向量动画
};

// 处理新动画类型
void ExtendedAnimatorImpl::applyAnimation(const Channel& channel, float t, 
                                         size_t prevIndex, size_t nextIndex) {
    switch (channel.transformType) {
        case ExtendedTransformType::COLOR: {
            float3 color = interpolateColor(channel, t, prevIndex, nextIndex);
            applyColorToMaterial(channel.targetEntity, color);
            break;
        }
        case ExtendedTransformType::VISIBILITY: {
            float visibility = interpolateFloat(channel, t, prevIndex, nextIndex);
            setEntityVisibility(channel.targetEntity, visibility > 0.5f);
            break;
        }
        case ExtendedTransformType::MATERIAL_PARAM: {
            float value = interpolateFloat(channel, t, prevIndex, nextIndex);
            setMaterialParameter(channel.targetEntity, channel.parameterName, value);
            break;
        }
        // ... 其他类型
    }
}
```

### 2. 高级骨骼功能

```cpp
// IK 求解器
class IKSolver {
public:
    enum Type {
        TWO_BONE,     // 两骨骼 IK（手腕、足踝）
        MULTI_BONE,   // 多骨骼 IK（脊柱）
        LOOK_AT       // 注视 IK（头部、眼部）
    };
    
    struct Constraint {
        Entity effector;    // 终端执行器
        Entity target;      // 目标位置
        float weight;       // 求解权重
        Type type;          // IK 类型
        int maxIterations;  // 最大迭代次数
    };
    
    void solveIK(const std::vector<Constraint>& constraints,
                 TransformManager& transformManager);
    
private:
    void solveTwoBoneIK(const Constraint& constraint, TransformManager& tm);
    void solveMultiBoneIK(const Constraint& constraint, TransformManager& tm);
    void solveLookAtIK(const Constraint& constraint, TransformManager& tm);
};

// 物理模拟集成
class PhysicsAnimationBlend {
    struct RigidBody {
        Entity entity;
        float3 velocity;
        float3 acceleration;
        float mass;
        float damping;
    };
    
    std::vector<RigidBody> mRigidBodies;
    
public:
    void addRigidBody(Entity entity, float mass, float damping) {
        mRigidBodies.push_back({entity, {0,0,0}, {0,0,0}, mass, damping});
    }
    
    void simulatePhysics(float deltaTime, TransformManager& tm) {
        for (auto& body : mRigidBodies) {
            // 简单的物理模拟
            body.velocity += body.acceleration * deltaTime;
            body.velocity *= (1.0f - body.damping * deltaTime);
            
            auto transform = tm.getInstance(body.entity);
            float3 position = extractTranslation(tm.getTransform(transform));
            position += body.velocity * deltaTime;
            
            tm.setTransform(transform, 
                           composeMatrix(position, extractRotation(tm.getTransform(transform)), 
                                       extractScale(tm.getTransform(transform))));
        }
    }
};
```

## 性能优化方案

### 1. 动画 LOD 系统

```cpp
class AnimationLODSystem {
    enum LODLevel {
        LOD_HIGH,      // 全精度动画
        LOD_MEDIUM,    // 中等精度
        LOD_LOW,       // 低精度
        LOD_IMPOSTOR   // 静态替身
    };
    
    struct LODSettings {
        float updateRate;      // 更新频率
        int maxBones;         // 最大骨骼数
        int keyFrameSkip;     // 关键帧跳过
        bool enableMorphing;  // 是否启用变形
    };
    
    std::array<LODSettings, 4> mLODSettings = {{
        {60.0f, 256, 1, true},   // HIGH
        {30.0f, 128, 2, true},   // MEDIUM  
        {15.0f, 64, 4, false},   // LOW
        {0.0f, 0, 0, false}      // IMPOSTOR
    }};
    
public:
    LODLevel calculateLOD(Entity entity, const Camera& camera) {
        float distance = getDistanceToCamera(entity, camera);
        float screenSize = calculateScreenSize(entity, camera);
        
        if (screenSize > 0.1f) return LOD_HIGH;
        if (screenSize > 0.05f) return LOD_MEDIUM;
        if (screenSize > 0.01f) return LOD_LOW;
        return LOD_IMPOSTOR;
    }
    
    void applyLOD(Animator& animator, LODLevel lod) {
        const auto& settings = mLODSettings[lod];
        
        animator.setUpdateRate(settings.updateRate);
        animator.setMaxBoneCount(settings.maxBones);
        animator.setKeyFrameSkip(settings.keyFrameSkip);
        animator.enableMorphing(settings.enableMorphing);
    }
};
```

### 2. 动画压缩系统

```cpp
class AnimationCompression {
public:
    // 时间轴压缩
    struct CompressedTimeline {
        float startTime;
        float duration;
        uint16_t frameCount;
        std::vector<uint8_t> quantizedTimes;  // 量化时间
    };
    
    // 数值压缩
    struct CompressedValues {
        float3 boundsMin, boundsMax;  // 数值范围
        std::vector<uint16_t> quantizedValues;
    };
    
    CompressedTimeline compressTimeline(const std::vector<float>& times) {
        CompressedTimeline result;
        result.startTime = times.front();
        result.duration = times.back() - times.front();
        result.frameCount = times.size();
        
        result.quantizedTimes.reserve(times.size());
        for (float time : times) {
            float normalized = (time - result.startTime) / result.duration;
            result.quantizedTimes.push_back(static_cast<uint8_t>(normalized * 255));
        }
        
        return result;
    }
    
    CompressedValues compressValues(const std::vector<float3>& values) {
        CompressedValues result;
        
        // 计算边界
        result.boundsMin = result.boundsMax = values[0];
        for (const auto& v : values) {
            result.boundsMin = min(result.boundsMin, v);
            result.boundsMax = max(result.boundsMax, v);
        }
        
        // 量化数值
        result.quantizedValues.reserve(values.size() * 3);
        for (const auto& v : values) {
            for (int i = 0; i < 3; ++i) {
                float normalized = (v[i] - result.boundsMin[i]) / 
                                 (result.boundsMax[i] - result.boundsMin[i]);
                result.quantizedValues.push_back(static_cast<uint16_t>(normalized * 65535));
            }
        }
        
        return result;
    }
};
```

## 高级动画功能

### 1. 动画状态机

```cpp
class AnimationStateMachine {
public:
    struct State {
        std::string name;
        size_t animationIndex;
        float speed = 1.0f;
        bool looping = true;
        float blendInTime = 0.3f;
        float blendOutTime = 0.3f;
    };
    
    struct Transition {
        size_t fromState;
        size_t toState;
        float duration;
        std::function<bool()> condition;
        AnimationCurve blendCurve;
    };
    
private:
    std::vector<State> mStates;
    std::vector<Transition> mTransitions;
    size_t mCurrentState = 0;
    size_t mTargetState = 0;
    float mTransitionTime = 0;
    bool mInTransition = false;
    
public:
    void addState(const State& state) {
        mStates.push_back(state);
    }
    
    void addTransition(const Transition& transition) {
        mTransitions.push_back(transition);
    }
    
    void update(float deltaTime, Animator& animator) {
        // 检查转换条件
        if (!mInTransition) {
            for (const auto& transition : mTransitions) {
                if (transition.fromState == mCurrentState && transition.condition()) {
                    startTransition(transition.toState, transition.duration);
                    break;
                }
            }
        }
        
        // 更新状态
        if (mInTransition) {
            updateTransition(deltaTime, animator);
        } else {
            updateState(deltaTime, animator);
        }
    }
    
private:
    void startTransition(size_t targetState, float duration) {
        mTargetState = targetState;
        mTransitionTime = 0;
        mInTransition = true;
    }
    
    void updateTransition(float deltaTime, Animator& animator) {
        mTransitionTime += deltaTime;
        
        // 计算混合权重
        float alpha = mTransitionTime / getCurrentTransition().duration;
        
        if (alpha >= 1.0f) {
            // 转换完成
            mCurrentState = mTargetState;
            mInTransition = false;
            alpha = 1.0f;
        }
        
        // 应用混合动画
        const State& fromState = mStates[mCurrentState];
        const State& toState = mStates[mTargetState];
        
        float time1 = getCurrentStateTime();
        float time2 = mTransitionTime;
        
        animator.applyAnimation(fromState.animationIndex, time1);
        animator.applyCrossFade(toState.animationIndex, time2, alpha);
    }
};
```

### 2. 动画混合树

```cpp
class BlendTree {
public:
    class Node {
    public:
        virtual ~Node() = default;
        virtual float evaluate(const AnimationContext& context) = 0;
    };
    
    class AnimationNode : public Node {
        size_t mAnimationIndex;
        float mWeight;
        
    public:
        float evaluate(const AnimationContext& context) override {
            return mWeight;
        }
    };
    
    class BlendNode : public Node {
        std::unique_ptr<Node> mLeft;
        std::unique_ptr<Node> mRight;
        float mBlendParameter;
        
    public:
        float evaluate(const AnimationContext& context) override {
            float leftWeight = mLeft->evaluate(context);
            float rightWeight = mRight->evaluate(context);
            return lerp(leftWeight, rightWeight, mBlendParameter);
        }
    };
    
    class BlendSpace2D : public Node {
        struct BlendPoint {
            float2 position;
            size_t animationIndex;
        };
        
        std::vector<BlendPoint> mBlendPoints;
        float2 mCurrentPosition;
        
    public:
        float evaluate(const AnimationContext& context) override {
            // 基于位置的多维混合
            return calculateBlendWeights(mCurrentPosition);
        }
    };
};
```

## 工具链扩展

### 1. 动画编辑器接口

```cpp
class AnimationEditor {
public:
    // 动画轨道编辑
    void addKeyframe(size_t track, float time, const Transform& value);
    void removeKeyframe(size_t track, float time);
    void moveKeyframe(size_t track, float oldTime, float newTime);
    
    // 曲线编辑
    void setCurveType(size_t track, CurveType type);
    void adjustTangents(size_t track, float time, float inTangent, float outTangent);
    
    // 动画导出
    void exportToGLTF(const std::string& filename);
    void exportToBinary(const std::string& filename);
    
    // 实时预览
    void setPreviewTime(float time);
    void play();
    void pause();
    void stop();
};
```

### 2. 性能分析工具

```cpp
class AnimationProfilerTool {
public:
    struct ProfileData {
        std::string animationName;
        float cpuTime;
        float gpuTime;
        size_t memoryUsage;
        float cacheHitRate;
    };
    
    void startProfiling();
    void stopProfiling();
    std::vector<ProfileData> getResults();
    
    // 生成报告
    void generateReport(const std::string& filename);
    
    // 实时监控
    void enableRealtimeMonitoring(bool enabled);
    ProfileData getCurrentFrameData();
};
```

## 未来发展路线

### 1. 机器学习集成

```cpp
// 神经网络驱动的动画
class NeuralAnimationSystem {
    struct MotionMatchingDB {
        std::vector<MotionClip> clips;
        std::vector<FeatureVector> features;
        KDTree spatialIndex;
    };
    
    MotionMatchingDB mDatabase;
    
public:
    // 动作匹配
    size_t findBestMatch(const FeatureVector& query);
    
    // 动作生成
    AnimationClip generateAnimation(const MotionGoal& goal);
    
    // 实时适应
    void adaptToCharacter(const CharacterRig& rig);
};
```

### 2. 实时光线追踪集成

```cpp
// RTX 加速的动画计算
class RTXAnimationAccelerator {
public:
    // GPU 驱动的蒙皮计算
    void setupRTXSkinning(const SkinnedMesh& mesh);
    
    // 实时阴影更新
    void updateRTXShadows(const AnimatedScene& scene);
    
    // 动态光照适应
    void adaptLightingToAnimation(const LightRig& lights);
};
```

### 3. 云端动画流

```cpp
// 云端动画流式传输
class CloudAnimationStreaming {
public:
    // 流式加载
    void startStreaming(const std::string& animationURL);
    
    // 自适应质量
    void setQualityBasedOnBandwidth(float bandwidth);
    
    // 预测缓存
    void enablePredictiveCaching(bool enabled);
    
    // 多用户同步
    void syncWithOtherUsers(const std::vector<UserID>& users);
};
```

这些扩展方案为 Filament 动画系统提供了广阔的发展空间，从基本的功能扩展到先进的 AI 和硬件加速技术。