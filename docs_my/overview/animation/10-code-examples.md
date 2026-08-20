# 代码示例与最佳实践

本文档提供 Filament 动画系统的实际使用示例、常见问题解决方案和调试技巧。

## 目录

1. [基本使用示例](#基本使用示例)
2. [高级动画控制](#高级动画控制)
3. [性能优化实践](#性能优化实践)
4. [常见问题解决](#常见问题解决)
5. [调试技巧](#调试技巧)
6. [最佳实践指南](#最佳实践指南)

## 基本使用示例

### 1. 加载和播放 glTF 动画

```cpp
#include <filament/Engine.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/Animator.h>

class SimpleAnimationExample {
    Engine* mEngine;
    AssetLoader* mAssetLoader;
    ResourceLoader* mResourceLoader;
    FilamentAsset* mAsset;
    Scene* mScene;
    
public:
    void setup() {
        // 1. 创建引擎和加载器
        mEngine = Engine::create();
        
        auto materials = createJitShaderProvider(mEngine);
        mAssetLoader = AssetLoader::create({mEngine, materials});
        
        mResourceLoader = new ResourceLoader({
            .engine = mEngine,
            .gltfPath = "assets/models/",
            .normalizeSkinningWeights = true
        });
        
        // 2. 加载 glTF 文件
        std::vector<uint8_t> buffer = readFile("animated_character.glb");
        mAsset = mAssetLoader->createAsset(buffer.data(), buffer.size());
        
        if (!mAsset) {
            throw std::runtime_error("无法加载 glTF 文件");
        }
        
        // 3. 加载资源（纹理、缓冲区）
        mResourceLoader->loadResources(mAsset);
        
        // 4. 释放源数据
        mAsset->releaseSourceData();
        
        // 5. 添加到场景
        mScene = mEngine->createScene();
        mScene->addEntities(mAsset->getEntities(), mAsset->getEntityCount());
    }
    
    void animate(double currentTime) {
        auto instance = mAsset->getInstance();
        if (!instance) return;
        
        auto animator = instance->getAnimator();
        if (!animator || animator->getAnimationCount() == 0) return;
        
        // 播放第一个动画
        float animationTime = fmod(currentTime, animator->getAnimationDuration(0));
        
        animator->applyAnimation(0, animationTime);
        animator->updateBoneMatrices();
    }
    
    void cleanup() {
        mScene->removeEntities(mAsset->getEntities(), mAsset->getEntityCount());
        mAssetLoader->destroyAsset(mAsset);
        delete mResourceLoader;
        AssetLoader::destroy(&mAssetLoader);
        Engine::destroy(&mEngine);
    }
};
```

### 2. 多动画播放控制

```cpp
class MultiAnimationController {
    struct AnimationState {
        size_t animationIndex;
        float currentTime;
        float speed;
        bool looping;
        bool paused;
    };
    
    Animator* mAnimator;
    std::vector<AnimationState> mAnimations;
    int mCurrentAnimation = -1;
    
public:
    MultiAnimationController(Animator* animator) : mAnimator(animator) {
        // 初始化所有动画状态
        size_t animCount = animator->getAnimationCount();
        mAnimations.resize(animCount);
        
        for (size_t i = 0; i < animCount; ++i) {
            mAnimations[i] = {
                .animationIndex = i,
                .currentTime = 0.0f,
                .speed = 1.0f,
                .looping = true,
                .paused = false
            };
        }
    }
    
    void playAnimation(size_t index, bool restart = false) {
        if (index >= mAnimations.size()) return;
        
        mCurrentAnimation = static_cast<int>(index);
        
        if (restart) {
            mAnimations[index].currentTime = 0.0f;
        }
        
        mAnimations[index].paused = false;
    }
    
    void pauseAnimation() {
        if (mCurrentAnimation >= 0) {
            mAnimations[mCurrentAnimation].paused = true;
        }
    }
    
    void setAnimationSpeed(size_t index, float speed) {
        if (index < mAnimations.size()) {
            mAnimations[index].speed = speed;
        }
    }
    
    void update(float deltaTime) {
        if (mCurrentAnimation < 0 || mCurrentAnimation >= mAnimations.size()) {
            return;
        }
        
        auto& anim = mAnimations[mCurrentAnimation];
        if (anim.paused) return;
        
        // 更新动画时间
        anim.currentTime += deltaTime * anim.speed;
        
        float duration = mAnimator->getAnimationDuration(anim.animationIndex);
        
        if (anim.looping) {
            anim.currentTime = fmod(anim.currentTime, duration);
        } else if (anim.currentTime > duration) {
            anim.currentTime = duration;
            anim.paused = true;  // 动画结束
        }
        
        // 应用动画
        mAnimator->applyAnimation(anim.animationIndex, anim.currentTime);
        mAnimator->updateBoneMatrices();
    }
    
    // 获取动画信息
    std::vector<std::string> getAnimationNames() const {
        std::vector<std::string> names;
        for (size_t i = 0; i < mAnimations.size(); ++i) {
            const char* name = mAnimator->getAnimationName(i);
            names.emplace_back(name ? name : ("Animation_" + std::to_string(i)));
        }
        return names;
    }
    
    float getAnimationProgress() const {
        if (mCurrentAnimation < 0) return 0.0f;
        
        const auto& anim = mAnimations[mCurrentAnimation];
        float duration = mAnimator->getAnimationDuration(anim.animationIndex);
        return duration > 0 ? anim.currentTime / duration : 0.0f;
    }
};
```

### 3. 动画混合系统

```cpp
class AnimationBlender {
    struct BlendEntry {
        size_t animationIndex;
        float weight;
        float time;
    };
    
    Animator* mAnimator;
    std::vector<BlendEntry> mBlendList;
    float mTotalWeight = 0.0f;
    
public:
    AnimationBlender(Animator* animator) : mAnimator(animator) {}
    
    void clearBlend() {
        mBlendList.clear();
        mTotalWeight = 0.0f;
    }
    
    void addAnimation(size_t index, float weight, float time) {
        mBlendList.push_back({index, weight, time});
        mTotalWeight += weight;
    }
    
    void applyBlend() {
        if (mBlendList.empty() || mTotalWeight <= 0.0f) return;
        
        // 归一化权重
        for (auto& entry : mBlendList) {
            entry.weight /= mTotalWeight;
        }
        
        if (mBlendList.size() == 1) {
            // 单个动画，直接应用
            const auto& entry = mBlendList[0];
            mAnimator->applyAnimation(entry.animationIndex, entry.time);
        } else {
            // 多个动画混合
            blendMultipleAnimations();
        }
        
        mAnimator->updateBoneMatrices();
    }
    
private:
    void blendMultipleAnimations() {
        // 这里实现复杂的多动画混合逻辑
        // 简化版本：使用第一个动画作为基础，然后与其他动画混合
        
        if (mBlendList.size() >= 2) {
            const auto& base = mBlendList[0];
            const auto& blend = mBlendList[1];
            
            // 应用基础动画
            mAnimator->applyAnimation(base.animationIndex, base.time);
            
            // 与第二个动画混合
            float alpha = blend.weight / (base.weight + blend.weight);
            mAnimator->applyCrossFade(blend.animationIndex, blend.time, alpha);
        }
    }
};

// 使用示例
void demonstrateBlending(Animator* animator) {
    AnimationBlender blender(animator);
    
    // 混合走路和跑步动画
    float walkWeight = 0.7f;
    float runWeight = 0.3f;
    float currentTime = getCurrentTime();
    
    blender.clearBlend();
    blender.addAnimation(WALK_ANIMATION, walkWeight, currentTime);
    blender.addAnimation(RUN_ANIMATION, runWeight, currentTime);
    blender.applyBlend();
}
```

## 高级动画控制

### 1. 动画事件系统

```cpp
class AnimationEventSystem {
public:
    struct AnimationEvent {
        float time;                              // 事件触发时间
        std::string name;                        // 事件名称
        std::function<void()> callback;          // 回调函数
        bool triggered = false;                  // 是否已触发
    };
    
private:
    std::vector<AnimationEvent> mEvents;
    float mLastTime = 0.0f;
    
public:
    void addEvent(float time, const std::string& name, std::function<void()> callback) {
        mEvents.push_back({time, name, callback, false});
        
        // 按时间排序
        std::sort(mEvents.begin(), mEvents.end(), 
                 [](const AnimationEvent& a, const AnimationEvent& b) {
                     return a.time < b.time;
                 });
    }
    
    void update(float currentTime, float animationDuration) {
        // 处理循环动画的时间跳跃
        if (currentTime < mLastTime) {
            resetEvents();
        }
        
        // 检查和触发事件
        for (auto& event : mEvents) {
            if (!event.triggered && currentTime >= event.time) {
                event.callback();
                event.triggered = true;
                
                printf("动画事件触发: %s 于时间 %.2f\n", 
                       event.name.c_str(), event.time);
            }
        }
        
        mLastTime = currentTime;
    }
    
    void resetEvents() {
        for (auto& event : mEvents) {
            event.triggered = false;
        }
    }
};

// 使用示例
void setupCharacterAnimationEvents(AnimationEventSystem& eventSystem) {
    // 足步声事件
    eventSystem.addEvent(0.2f, "footstep_left", []() {
        playSound("footstep.wav");
    });
    
    eventSystem.addEvent(0.7f, "footstep_right", []() {
        playSound("footstep.wav");
    });
    
    // 攻击事件
    eventSystem.addEvent(0.5f, "sword_hit", []() {
        applyDamage(SWORD_DAMAGE);
        spawnParticleEffect("sword_slash");
    });
}
```

### 2. 适应性动画系统

```cpp
class AdaptiveAnimationSystem {
    struct CharacterState {
        float speed;          // 移动速度
        float direction;      // 移动方向
        float slope;          // 地面斜度
        bool inAir;           // 是否在空中
        float energy;         // 能量值
    };
    
    CharacterState mState;
    Animator* mAnimator;
    AnimationBlender mBlender;
    
public:
    AdaptiveAnimationSystem(Animator* animator) 
        : mAnimator(animator), mBlender(animator) {}
    
    void updateState(const CharacterState& newState) {
        mState = newState;
    }
    
    void update(float deltaTime) {
        mBlender.clearBlend();
        
        // 根据状态选择合适的动画混合
        if (mState.inAir) {
            handleAirborneAnimation();
        } else {
            handleGroundMovement();
        }
        
        mBlender.applyBlend();
    }
    
private:
    void handleGroundMovement() {
        float currentTime = getCurrentTime();
        
        if (mState.speed < 0.1f) {
            // 静止状态
            float idleWeight = 1.0f;
            
            // 根据能量调整静止动画
            if (mState.energy < 0.3f) {
                mBlender.addAnimation(IDLE_TIRED, idleWeight, currentTime);
            } else {
                mBlender.addAnimation(IDLE_NORMAL, idleWeight, currentTime);
            }
        } else {
            // 移动状态
            float walkSpeed = 2.0f;
            float runSpeed = 5.0f;
            
            if (mState.speed <= walkSpeed) {
                // 纯走路
                mBlender.addAnimation(WALK, 1.0f, currentTime * mState.speed / walkSpeed);
            } else if (mState.speed >= runSpeed) {
                // 纯跑步
                mBlender.addAnimation(RUN, 1.0f, currentTime * mState.speed / runSpeed);
            } else {
                // 走路和跑步混合
                float blend = (mState.speed - walkSpeed) / (runSpeed - walkSpeed);
                mBlender.addAnimation(WALK, 1.0f - blend, currentTime);
                mBlender.addAnimation(RUN, blend, currentTime);
            }
            
            // 根据地面斜度调整
            if (abs(mState.slope) > 0.1f) {
                float slopeInfluence = std::min(abs(mState.slope), 0.5f) / 0.5f;
                
                if (mState.slope > 0) {
                    // 上坡
                    mBlender.addAnimation(WALK_UPHILL, slopeInfluence, currentTime);
                } else {
                    // 下坡
                    mBlender.addAnimation(WALK_DOWNHILL, slopeInfluence, currentTime);
                }
            }
        }
    }
    
    void handleAirborneAnimation() {
        float currentTime = getCurrentTime();
        
        if (mState.speed > 0.1f) {
            // 跑跳
            mBlender.addAnimation(JUMP_RUNNING, 1.0f, currentTime);
        } else {
            // 站立跳跃
            mBlender.addAnimation(JUMP_STANDING, 1.0f, currentTime);
        }
    }
};
```

## 性能优化实践

### 1. 动画 LOD 管理

```cpp
class AnimationLODManager {
    enum LODLevel { HIGH, MEDIUM, LOW, DISABLED };
    
    struct LODSettings {
        float updateRate;      // 更新频率 (Hz)
        int maxBones;         // 最大骨骼数
        bool enableMorphing;  // 是否启用变形
        int keyFrameSkip;     // 跳过的关键帧数
    };
    
    std::array<LODSettings, 4> mLODSettings = {{
        {60.0f, 256, true, 1},   // HIGH
        {30.0f, 128, true, 2},   // MEDIUM
        {15.0f, 64, false, 4},   // LOW
        {0.0f, 0, false, 0}      // DISABLED
    }};
    
    struct ManagedAnimator {
        Animator* animator;
        Entity entity;
        LODLevel currentLOD;
        float lastUpdateTime;
        float timeAccumulator;
    };
    
    std::vector<ManagedAnimator> mAnimators;
    Camera* mCamera;
    
public:
    void addAnimator(Animator* animator, Entity entity) {
        mAnimators.push_back({
            .animator = animator,
            .entity = entity,
            .currentLOD = HIGH,
            .lastUpdateTime = 0.0f,
            .timeAccumulator = 0.0f
        });
    }
    
    void setCamera(Camera* camera) {
        mCamera = camera;
    }
    
    void update(float currentTime, TransformManager& tm) {
        for (auto& managed : mAnimators) {
            // 计算 LOD 级别
            LODLevel newLOD = calculateLOD(managed.entity, tm);
            
            if (newLOD != managed.currentLOD) {
                managed.currentLOD = newLOD;
                applyLODSettings(managed, newLOD);
            }
            
            // 按 LOD 设置更新动画
            updateAnimatorWithLOD(managed, currentTime);
        }
    }
    
private:
    LODLevel calculateLOD(Entity entity, TransformManager& tm) {
        if (!mCamera) return HIGH;
        
        auto instance = tm.getInstance(entity);
        if (!instance) return DISABLED;
        
        float3 entityPos = extractTranslation(tm.getWorldTransform(instance));
        float3 cameraPos = mCamera->getPosition();
        float distance = length(entityPos - cameraPos);
        
        // 根据距离和屏幕大小确定 LOD
        float screenSize = calculateScreenSize(entity, distance);
        
        if (screenSize > 0.1f) return HIGH;
        if (screenSize > 0.05f) return MEDIUM;
        if (screenSize > 0.01f) return LOW;
        return DISABLED;
    }
    
    void updateAnimatorWithLOD(ManagedAnimator& managed, float currentTime) {
        const auto& settings = mLODSettings[managed.currentLOD];
        
        if (settings.updateRate <= 0) {
            return;  // 禁用状态，不更新
        }
        
        float updateInterval = 1.0f / settings.updateRate;
        managed.timeAccumulator += currentTime - managed.lastUpdateTime;
        managed.lastUpdateTime = currentTime;
        
        if (managed.timeAccumulator >= updateInterval) {
            managed.timeAccumulator = 0.0f;
            
            // 更新动画（这里需要具体的动画管理逻辑）
            updateAnimatorAnimation(managed.animator, currentTime, settings);
        }
    }
};
```

### 2. 内存池优化

```cpp
class AnimationMemoryPool {
    template<typename T>
    class TypedPool {
        struct Node {
            alignas(T) char data[sizeof(T)];
            Node* next;
        };
        
        Node* mFreeList = nullptr;
        std::vector<std::unique_ptr<Node[]>> mBlocks;
        size_t mBlockSize;
        
    public:
        TypedPool(size_t blockSize = 1024) : mBlockSize(blockSize) {
            allocateNewBlock();
        }
        
        T* acquire() {
            if (!mFreeList) {
                allocateNewBlock();
            }
            
            Node* node = mFreeList;
            mFreeList = mFreeList->next;
            return reinterpret_cast<T*>(node->data);
        }
        
        void release(T* obj) {
            if (!obj) return;
            
            obj->~T();
            
            Node* node = reinterpret_cast<Node*>(obj);
            node->next = mFreeList;
            mFreeList = node;
        }
        
    private:
        void allocateNewBlock() {
            auto block = std::make_unique<Node[]>(mBlockSize);
            
            for (size_t i = 0; i < mBlockSize - 1; ++i) {
                block[i].next = &block[i + 1];
            }
            block[mBlockSize - 1].next = mFreeList;
            mFreeList = &block[0];
            
            mBlocks.push_back(std::move(block));
        }
    };
    
    TypedPool<mat4f> mMatrixPool;
    TypedPool<Animation> mAnimationPool;
    TypedPool<Channel> mChannelPool;
    
public:
    // 矩阵池管理
    mat4f* acquireMatrix() { return mMatrixPool.acquire(); }
    void releaseMatrix(mat4f* matrix) { mMatrixPool.release(matrix); }
    
    // 动画池管理
    Animation* acquireAnimation() { return mAnimationPool.acquire(); }
    void releaseAnimation(Animation* anim) { mAnimationPool.release(anim); }
    
    // 通道池管理
    Channel* acquireChannel() { return mChannelPool.acquire(); }
    void releaseChannel(Channel* channel) { mChannelPool.release(channel); }
};

// 全局内存池单例
AnimationMemoryPool& getGlobalAnimationPool() {
    static AnimationMemoryPool pool;
    return pool;
}
```

## 常见问题解决

### 1. 动画不播放

```cpp
class AnimationDiagnostics {
public:
    static void diagnoseAnimation(FilamentAsset* asset) {
        printf("动画诊断报告:\n");
        printf("================\n");
        
        // 检查资源加载状态
        if (!asset) {
            printf("错误: 资产为空\n");
            return;
        }
        
        auto instance = asset->getInstance();
        if (!instance) {
            printf("错误: 无法获取资产实例\n");
            return;
        }
        
        auto animator = instance->getAnimator();
        if (!animator) {
            printf("错误: 无法获取动画器\n");
            return;
        }
        
        // 检查动画数量
        size_t animCount = animator->getAnimationCount();
        printf("动画数量: %zu\n", animCount);
        
        if (animCount == 0) {
            printf("警告: 没有找到动画数据\n");
            return;
        }
        
        // 检查每个动画
        for (size_t i = 0; i < animCount; ++i) {
            const char* name = animator->getAnimationName(i);
            float duration = animator->getAnimationDuration(i);
            
            printf("动画 %zu: %s, 时长: %.2f秒\n", 
                   i, name ? name : "未命名", duration);
            
            if (duration <= 0.0f) {
                printf("  警告: 动画时长为零\n");
            }
        }
        
        // 检查实体数量
        size_t entityCount = asset->getEntityCount();
        printf("实体数量: %zu\n", entityCount);
        
        // 检查可渲染实体
        size_t renderableCount = asset->getRenderableEntityCount();
        printf("可渲染实体数量: %zu\n", renderableCount);
        
        printf("诊断完成\n");
    }
    
    static void checkTransformHierarchy(FilamentAsset* asset, Engine* engine) {
        printf("检查变换层次结构:\n");
        
        auto& tm = engine->getTransformManager();
        const Entity* entities = asset->getEntities();
        size_t entityCount = asset->getEntityCount();
        
        for (size_t i = 0; i < entityCount; ++i) {
            Entity entity = entities[i];
            auto instance = tm.getInstance(entity);
            
            if (instance) {
                mat4f transform = tm.getTransform(instance);
                auto parent = tm.getParent(instance);
                
                printf("实体 %zu: %s父节点\n", 
                       i, parent ? "有" : "无");
                
                // 检查变换矩阵的有效性
                if (isMatrixValid(transform)) {
                    printf("  变换矩阵: 有效\n");
                } else {
                    printf("  警告: 变换矩阵包含非法值\n");
                }
            }
        }
    }
    
private:
    static bool isMatrixValid(const mat4f& matrix) {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                float value = matrix[i][j];
                if (std::isnan(value) || std::isinf(value)) {
                    return false;
                }
            }
        }
        return true;
    }
};
```

### 2. 性能问题诊断

```cpp
class PerformanceDiagnostics {
    struct FrameStats {
        double animationTime = 0;
        double boneUpdateTime = 0;
        size_t animatorCount = 0;
        size_t boneCount = 0;
    };
    
    FrameStats mCurrentFrame;
    std::vector<FrameStats> mFrameHistory;
    size_t mMaxHistorySize = 60;  // 1秒的历史数据
    
public:
    void beginFrame() {
        mCurrentFrame = FrameStats{};
    }
    
    void recordAnimationTime(double time) {
        mCurrentFrame.animationTime += time;
    }
    
    void recordBoneUpdate(double time, size_t boneCount) {
        mCurrentFrame.boneUpdateTime += time;
        mCurrentFrame.boneCount += boneCount;
    }
    
    void incrementAnimatorCount() {
        mCurrentFrame.animatorCount++;
    }
    
    void endFrame() {
        mFrameHistory.push_back(mCurrentFrame);
        
        if (mFrameHistory.size() > mMaxHistorySize) {
            mFrameHistory.erase(mFrameHistory.begin());
        }
    }
    
    void generateReport() {
        if (mFrameHistory.empty()) return;
        
        double avgAnimTime = 0;
        double avgBoneTime = 0;
        double avgAnimatorCount = 0;
        double avgBoneCount = 0;
        
        for (const auto& frame : mFrameHistory) {
            avgAnimTime += frame.animationTime;
            avgBoneTime += frame.boneUpdateTime;
            avgAnimatorCount += frame.animatorCount;
            avgBoneCount += frame.boneCount;
        }
        
        size_t frameCount = mFrameHistory.size();
        avgAnimTime /= frameCount;
        avgBoneTime /= frameCount;
        avgAnimatorCount /= frameCount;
        avgBoneCount /= frameCount;
        
        printf("动画性能报告 (过去 %zu 帧):\n", frameCount);
        printf("================\n");
        printf("平均动画时间: %.3f ms\n", avgAnimTime * 1000);
        printf("平均骨骼更新时间: %.3f ms\n", avgBoneTime * 1000);
        printf("平均动画器数量: %.1f\n", avgAnimatorCount);
        printf("平均骨骼数量: %.1f\n", avgBoneCount);
        
        // 性能建议
        if (avgAnimTime > 0.005) {  // > 5ms
            printf("建议: 动画时间过长，考虑使用 LOD 或减少动画数\n");
        }
        
        if (avgBoneCount > 1000) {
            printf("建议: 骨骼数量过多，考虑优化模型或使用 LOD\n");
        }
    }
};
```

## 调试技巧

### 1. 可视化调试

```cpp
class AnimationDebugRenderer {
    Engine* mEngine;
    Scene* mScene;
    std::vector<Entity> mDebugEntities;
    
public:
    AnimationDebugRenderer(Engine* engine, Scene* scene) 
        : mEngine(engine), mScene(scene) {}
    
    void renderSkeletonHierarchy(FilamentAsset* asset) {
        clearDebugEntities();
        
        auto& tm = mEngine->getTransformManager();
        const Entity* entities = asset->getEntities();
        size_t entityCount = asset->getEntityCount();
        
        // 为每个骨骼创建可视化球体
        for (size_t i = 0; i < entityCount; ++i) {
            Entity entity = entities[i];
            auto instance = tm.getInstance(entity);
            
            if (instance) {
                Entity debugEntity = createDebugSphere(tm.getWorldTransform(instance));
                mDebugEntities.push_back(debugEntity);
                mScene->addEntity(debugEntity);
                
                // 绘制骨骼连接
                auto parent = tm.getParent(instance);
                if (parent) {
                    Entity lineEntity = createDebugLine(
                        tm.getWorldTransform(instance),
                        tm.getWorldTransform(parent)
                    );
                    mDebugEntities.push_back(lineEntity);
                    mScene->addEntity(lineEntity);
                }
            }
        }
    }
    
    void renderBoundingBoxes(FilamentAsset* asset) {
        auto& rm = mEngine->getRenderableManager();
        const Entity* entities = asset->getRenderableEntities();
        size_t entityCount = asset->getRenderableEntityCount();
        
        for (size_t i = 0; i < entityCount; ++i) {
            auto instance = rm.getInstance(entities[i]);
            if (instance) {
                Box aabb = rm.getAxisAlignedBoundingBox(instance);
                Entity debugEntity = createDebugWireframeBox(aabb);
                mDebugEntities.push_back(debugEntity);
                mScene->addEntity(debugEntity);
            }
        }
    }
    
    void clearDebugEntities() {
        for (Entity entity : mDebugEntities) {
            mScene->remove(entity);
            mEngine->getEntityManager().destroy(entity);
        }
        mDebugEntities.clear();
    }
    
private:
    Entity createDebugSphere(const mat4f& transform) {
        // 实现球体创建逻辑
        // ...
        return Entity{};
    }
    
    Entity createDebugLine(const mat4f& start, const mat4f& end) {
        // 实现线条创建逻辑
        // ...
        return Entity{};
    }
    
    Entity createDebugWireframeBox(const Box& aabb) {
        // 实现线框盒创建逻辑
        // ...
        return Entity{};
    }
};
```

### 2. 日志系统

```cpp
class AnimationLogger {
    enum LogLevel { DEBUG, INFO, WARNING, ERROR };
    
    LogLevel mCurrentLevel = INFO;
    std::ofstream mLogFile;
    
public:
    AnimationLogger(const std::string& filename) {
        mLogFile.open(filename, std::ios::app);
    }
    
    void setLogLevel(LogLevel level) {
        mCurrentLevel = level;
    }
    
    void logAnimationStart(const std::string& animationName, float duration) {
        if (mCurrentLevel <= INFO) {
            log(INFO, "Animation started: %s (%.2fs)", animationName.c_str(), duration);
        }
    }
    
    void logAnimationEnd(const std::string& animationName) {
        if (mCurrentLevel <= INFO) {
            log(INFO, "Animation ended: %s", animationName.c_str());
        }
    }
    
    void logPerformanceWarning(const std::string& message) {
        log(WARNING, "Performance: %s", message.c_str());
    }
    
    void logError(const std::string& message) {
        log(ERROR, "Error: %s", message.c_str());
    }
    
private:
    void log(LogLevel level, const char* format, ...) {
        if (level < mCurrentLevel) return;
        
        const char* levelStr[] = {"DEBUG", "INFO", "WARNING", "ERROR"};
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        char timeStr[100];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&time_t));
        
        va_list args;
        va_start(args, format);
        
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, args);
        
        va_end(args);
        
        std::string logLine = std::string(timeStr) + " [" + levelStr[level] + "] " + buffer;
        
        printf("%s\n", logLine.c_str());
        
        if (mLogFile.is_open()) {
            mLogFile << logLine << std::endl;
            mLogFile.flush();
        }
    }
};
```

## 最佳实践指南

### 1. 资源管理

```cpp
class AnimationResourceManager {
    std::unordered_map<std::string, std::shared_ptr<FilamentAsset>> mAssetCache;
    std::unordered_map<std::string, std::shared_ptr<AnimationClip>> mAnimationCache;
    
public:
    // 资产缓存管理
    std::shared_ptr<FilamentAsset> loadAsset(const std::string& path, AssetLoader* loader) {
        auto it = mAssetCache.find(path);
        if (it != mAssetCache.end()) {
            return it->second;
        }
        
        auto buffer = readFile(path);
        auto asset = loader->createAsset(buffer.data(), buffer.size());
        
        if (asset) {
            auto sharedAsset = std::shared_ptr<FilamentAsset>(asset, 
                [loader](FilamentAsset* a) { loader->destroyAsset(a); });
            mAssetCache[path] = sharedAsset;
            return sharedAsset;
        }
        
        return nullptr;
    }
    
    // 预加载资产
    void preloadAssets(const std::vector<std::string>& paths, AssetLoader* loader) {
        for (const auto& path : paths) {
            loadAsset(path, loader);
        }
    }
    
    // 清理未使用的资产
    void cleanup() {
        auto it = mAssetCache.begin();
        while (it != mAssetCache.end()) {
            if (it->second.use_count() == 1) {
                it = mAssetCache.erase(it);
            } else {
                ++it;
            }
        }
    }
};
```

### 2. 错误处理

```cpp
class AnimationErrorHandler {
public:
    enum ErrorType {
        ASSET_LOAD_FAILED,
        ANIMATION_NOT_FOUND,
        BONE_COUNT_EXCEEDED,
        INVALID_ANIMATION_TIME,
        RESOURCE_EXHAUSTED
    };
    
    static void handleError(ErrorType type, const std::string& details = "") {
        switch (type) {
            case ASSET_LOAD_FAILED:
                LOGE("资产加载失败: %s", details.c_str());
                // 加载默认资产或显示错误信息
                break;
                
            case ANIMATION_NOT_FOUND:
                LOGW("未找到动画: %s", details.c_str());
                // 使用默认动画或静止姿态
                break;
                
            case BONE_COUNT_EXCEEDED:
                LOGW("骨骼数量超出限制: %s", details.c_str());
                // 启用 LOD 或减少骨骼数量
                break;
                
            case INVALID_ANIMATION_TIME:
                LOGD("无效的动画时间: %s", details.c_str());
                // 修正时间参数
                break;
                
            case RESOURCE_EXHAUSTED:
                LOGE("资源耗尽: %s", details.c_str());
                // 清理资源或降低质量
                break;
        }
    }
    
    // 安全的动画播放函数
    static bool safeApplyAnimation(Animator* animator, size_t animIndex, float time) {
        if (!animator) {
            handleError(ANIMATION_NOT_FOUND, "Animator is null");
            return false;
        }
        
        if (animIndex >= animator->getAnimationCount()) {
            handleError(ANIMATION_NOT_FOUND, 
                       "Animation index " + std::to_string(animIndex) + " out of range");
            return false;
        }
        
        if (time < 0.0f || std::isnan(time) || std::isinf(time)) {
            handleError(INVALID_ANIMATION_TIME, 
                       "Invalid time: " + std::to_string(time));
            time = 0.0f;
        }
        
        try {
            animator->applyAnimation(animIndex, time);
            animator->updateBoneMatrices();
            return true;
        } catch (const std::exception& e) {
            handleError(RESOURCE_EXHAUSTED, e.what());
            return false;
        }
    }
};
```

### 3. 配置管理

```cpp
class AnimationConfig {
public:
    struct Settings {
        // 性能设置
        int maxBoneCount = 256;
        int maxMorphTargets = 8;
        float lodDistanceMultiplier = 1.0f;
        bool enableGPUSkinning = true;
        
        // 质量设置
        bool enableHighQualityInterpolation = true;
        bool enableMotionBlur = false;
        int animationUpdateRate = 60;
        
        // 调试设置
        bool enableDebugVisualization = false;
        bool enablePerformanceLogging = false;
        LogLevel logLevel = LogLevel::INFO;
    };
    
private:
    Settings mSettings;
    
public:
    void loadFromFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            LOGW("无法加载配置文件: %s", filename.c_str());
            return;
        }
        
        // 这里实现 JSON 或其他格式的配置文件解析
        // 简化示例：
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("maxBoneCount=") == 0) {
                mSettings.maxBoneCount = std::stoi(line.substr(13));
            } else if (line.find("enableGPUSkinning=") == 0) {
                mSettings.enableGPUSkinning = (line.substr(18) == "true");
            }
            // ... 其他配置项
        }
    }
    
    void saveToFile(const std::string& filename) const {
        std::ofstream file(filename);
        if (!file.is_open()) {
            LOGE("无法保存配置文件: %s", filename.c_str());
            return;
        }
        
        file << "maxBoneCount=" << mSettings.maxBoneCount << std::endl;
        file << "enableGPUSkinning=" << (mSettings.enableGPUSkinning ? "true" : "false") << std::endl;
        // ... 其他配置项
    }
    
    const Settings& getSettings() const { return mSettings; }
    Settings& getSettings() { return mSettings; }
};
```

这些代码示例和最佳实践为使用 Filament 动画系统提供了全面的指导，帮助开发者触及常见问题并实现高质量的动画效果。