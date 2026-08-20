# 动画播放管线

本文档分析 Filament 中动画的播放管线，包括时间管理、更新循环和同步机制。

## 目录

1. [动画播放流程](#动画播放流程)
2. [时间管理系统](#时间管理系统)
3. [更新循环优化](#更新循环优化)
4. [批处理系统](#批处理系统)
5. [内存管理](#内存管理)
6. [性能监控](#性能监控)

## 动画播放流程

### 基本播放循环
```cpp
// 在 ViewerGui::applyAnimation 中的播放流程
void applyAnimation(double currentTime, FilamentInstance* instance) {
    Animator& animator = *instance->getAnimator();
    
    // 1. 时间管理
    const double elapsedSeconds = currentTime - mCurrentStartTime;
    
    // 2. 应用动画
    if (mCurrentAnimation >= 0) {
        animator.applyAnimation(mCurrentAnimation, elapsedSeconds);
    }
    
    // 3. 交叉淡化
    if (elapsedSeconds < mCrossFadeDuration && mPreviousAnimation >= 0) {
        const double previousSeconds = currentTime - mPreviousStartTime;
        const float lerpFactor = elapsedSeconds / mCrossFadeDuration;
        animator.applyCrossFade(mPreviousAnimation, previousSeconds, lerpFactor);
    }
    
    // 4. 更新骨骼矩阵
    if (!mShowingRestPose) {
        animator.updateBoneMatrices();
    } else {
        animator.resetBoneMatrices();
    }
}
```

### 渲染循环集成
```cpp
// 在主渲染循环中
app.animate = [&](Engine* engine, View* view, double now) {
    // 1. 更新动画
    app.viewer->applyAnimation(now);
    
    // 2. 更新场景
    app.viewer->populateScene();
    
    // 3. 渲染
    if (app.renderer->beginFrame(app.swapChain)) {
        app.renderer->render(view);
        app.renderer->endFrame();
    }
};
```

## 时间管理系统

### 时间状态管理
```cpp
class AnimationTimeState {
    double mCurrentStartTime = 0;    // 当前动画开始时间
    double mPreviousStartTime = 0;   // 前一个动画开始时间
    float mCrossFadeDuration = 0.3f; // 交叉淡化时长
    bool mResetAnimation = false;    // 重置标志
    
public:
    void resetAnimation(double currentTime) {
        mPreviousStartTime = mCurrentStartTime;
        mCurrentStartTime = currentTime;
        mResetAnimation = false;
    }
    
    double getElapsedTime(double currentTime) const {
        return currentTime - mCurrentStartTime;
    }
    
    float getCrossFadeAlpha(double currentTime) const {
        double elapsed = getElapsedTime(currentTime);
        return static_cast<float>(elapsed / mCrossFadeDuration);
    }
};
```

### 时间缩放控制
```cpp
class TimeController {
    float mTimeScale = 1.0f;
    float mAnimationSpeed = 1.0f;
    bool mPaused = false;
    
public:
    double getScaledDeltaTime(double deltaTime) const {
        if (mPaused) return 0.0;
        return deltaTime * mTimeScale * mAnimationSpeed;
    }
    
    void setGlobalTimeScale(float scale) { mTimeScale = scale; }
    void setAnimationSpeed(float speed) { mAnimationSpeed = speed; }
    void pause() { mPaused = true; }
    void resume() { mPaused = false; }
};
```

## 更新循环优化

### 事务批处理
```cpp
// 在 Animator::applyAnimation 中的事务优化
void applyAnimation(size_t animationIndex, float time) const {
    TransformManager& tm = *mImpl->transformManager;
    
    // 开启变换事务，批量更新
    tm.openLocalTransformTransaction();
    
    // 应用所有动画通道
    for (const auto& channel : anim.channels) {
        mImpl->applyAnimation(channel, t, prevIndex, nextIndex);
    }
    
    // 提交事务，一次性更新所有变换
    tm.commitLocalTransformTransaction();
}
```

### LOD 系统
```cpp
class AnimationLOD {
    enum Level { HIGH, MEDIUM, LOW, STATIC };
    
public:
    Level calculateLOD(const Entity& entity, const Camera& camera) {
        float distance = length(getWorldPosition(entity) - camera.getPosition());
        
        if (distance < 10.0f) return HIGH;
        if (distance < 50.0f) return MEDIUM;
        if (distance < 100.0f) return LOW;
        return STATIC;
    }
    
    void updateAnimationLOD(Animator& animator, Level lod) {
        switch (lod) {
            case HIGH:
                animator.setUpdateRate(60.0f);
                animator.setBoneCount(256);
                break;
            case MEDIUM:
                animator.setUpdateRate(30.0f);
                animator.setBoneCount(128);
                break;
            case LOW:
                animator.setUpdateRate(15.0f);
                animator.setBoneCount(64);
                break;
            case STATIC:
                animator.setUpdateRate(0.0f);
                break;
        }
    }
};
```

## 批处理系统

### 动画实例批处理
```cpp
class AnimationBatchProcessor {
    struct AnimationJob {
        Animator* animator;
        size_t animationIndex;
        float time;
        float priority;
    };
    
    std::vector<AnimationJob> mJobs;
    std::vector<std::thread> mWorkers;
    
public:
    void addJob(Animator* animator, size_t animIndex, float time, float priority = 1.0f) {
        mJobs.push_back({animator, animIndex, time, priority});
    }
    
    void processBatch() {
        // 按优先级排序
        std::sort(mJobs.begin(), mJobs.end(), 
                 [](const AnimationJob& a, const AnimationJob& b) {
                     return a.priority > b.priority;
                 });
        
        // 并行处理
        const size_t numThreads = std::thread::hardware_concurrency();
        const size_t jobsPerThread = mJobs.size() / numThreads;
        
        for (size_t i = 0; i < numThreads; ++i) {
            size_t start = i * jobsPerThread;
            size_t end = (i == numThreads - 1) ? mJobs.size() : start + jobsPerThread;
            
            mWorkers.emplace_back([this, start, end]() {
                for (size_t j = start; j < end; ++j) {
                    const auto& job = mJobs[j];
                    job.animator->applyAnimation(job.animationIndex, job.time);
                }
            });
        }
        
        // 等待所有线程完成
        for (auto& worker : mWorkers) {
            worker.join();
        }
        
        mWorkers.clear();
        mJobs.clear();
    }
};
```

### GPU 并行动画
```cpp
class GPUAnimationProcessor {
    GLuint mComputeProgram;
    GLuint mAnimationDataSSBO;
    GLuint mBoneMatricesSSBO;
    
public:
    void processAnimationsGPU(const std::vector<AnimationData>& animations) {
        // 上传动画数据
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, mAnimationDataSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 
                    animations.size() * sizeof(AnimationData),
                    animations.data(), GL_DYNAMIC_DRAW);
        
        // 分发计算着色器
        glUseProgram(mComputeProgram);
        glDispatchCompute((animations.size() + 63) / 64, 1, 1);
        
        // 等待完成
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        
        // 读取结果
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, mBoneMatricesSSBO);
        mat4* boneMatrices = (mat4*)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);
        
        // 处理结果...
        
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    }
};
```

## 内存管理

### 对象池管理
```cpp
template<typename T>
class AnimationObjectPool {
    struct Node {
        alignas(T) uint8_t data[sizeof(T)];
        Node* next;
    };
    
    Node* mFreeList = nullptr;
    std::vector<std::unique_ptr<Node[]>> mBlocks;
    
public:
    T* acquire() {
        if (!mFreeList) {
            allocateNewBlock();
        }
        
        Node* node = mFreeList;
        mFreeList = mFreeList->next;
        return reinterpret_cast<T*>(node->data);
    }
    
    void release(T* obj) {
        obj->~T();
        
        Node* node = reinterpret_cast<Node*>(obj);
        node->next = mFreeList;
        mFreeList = node;
    }
};

// 全局对象池
AnimationObjectPool<AnimatorImpl> gAnimatorPool;
AnimationObjectPool<TransformCache> gTransformPool;
```

### 内存缓存优化
```cpp
class CacheOptimizedAnimator {
    // SOA 布局优化缓存局部性
    std::vector<float3> mTranslations;
    std::vector<quatf> mRotations;
    std::vector<float3> mScales;
    std::vector<Entity> mTargetEntities;
    
    // 预取优化
    void prefetchAnimationData(size_t startIndex, size_t count) {
        const size_t cacheLineSize = 64;
        const size_t stride = sizeof(float3);
        
        for (size_t i = startIndex; i < startIndex + count; i += cacheLineSize / stride) {
            __builtin_prefetch(&mTranslations[i], 0, 3);
            __builtin_prefetch(&mRotations[i], 0, 3);
            __builtin_prefetch(&mScales[i], 0, 3);
        }
    }
};
```

## 性能监控

### 动画性能统计
```cpp
class AnimationProfiler {
    struct Stats {
        double totalTime = 0;
        size_t frameCount = 0;
        size_t animatorCount = 0;
        size_t boneUpdateCount = 0;
        double averageTime() const { return totalTime / frameCount; }
    };
    
    Stats mCurrentFrame;
    Stats mAccumulated;
    std::chrono::high_resolution_clock::time_point mFrameStart;
    
public:
    void beginFrame() {
        mFrameStart = std::chrono::high_resolution_clock::now();
        mCurrentFrame = Stats{};
    }
    
    void endFrame() {
        auto frameEnd = std::chrono::high_resolution_clock::now();
        mCurrentFrame.totalTime = std::chrono::duration<double>(frameEnd - mFrameStart).count();
        
        mAccumulated.totalTime += mCurrentFrame.totalTime;
        mAccumulated.frameCount++;
        mAccumulated.animatorCount += mCurrentFrame.animatorCount;
        mAccumulated.boneUpdateCount += mCurrentFrame.boneUpdateCount;
    }
    
    void reportStats() {
        printf("Animation Performance:\n");
        printf("  Average frame time: %.3f ms\n", mAccumulated.averageTime() * 1000);
        printf("  Animators per frame: %.1f\n", 
               (double)mAccumulated.animatorCount / mAccumulated.frameCount);
        printf("  Bone updates per frame: %.1f\n", 
               (double)mAccumulated.boneUpdateCount / mAccumulated.frameCount);
    }
};
```

### 瘴颈分析
```cpp
class BottleneckAnalyzer {
    enum class Bottleneck {
        CPU_BOUND,      // CPU 限制
        MEMORY_BOUND,   // 内存带宽限制
        CACHE_BOUND,    // 缓存限制
        GPU_BOUND       // GPU 限制
    };
    
public:
    Bottleneck analyzePerformance(const AnimationStats& stats) {
        if (stats.cacheHitRate < 0.8f) {
            return Bottleneck::CACHE_BOUND;
        }
        
        if (stats.memoryBandwidthUsage > 0.9f) {
            return Bottleneck::MEMORY_BOUND;
        }
        
        if (stats.cpuUsage > 0.8f) {
            return Bottleneck::CPU_BOUND;
        }
        
        return Bottleneck::GPU_BOUND;
    }
    
    void suggestOptimizations(Bottleneck bottleneck) {
        switch (bottleneck) {
            case Bottleneck::CACHE_BOUND:
                printf("优化建议: 改善数据局部性，使用 SOA 布局\n");
                break;
            case Bottleneck::MEMORY_BOUND:
                printf("优化建议: 减少内存带宽使用，压缩数据\n");
                break;
            case Bottleneck::CPU_BOUND:
                printf("优化建议: 使用多线程，游到 GPU 计算\n");
                break;
            case Bottleneck::GPU_BOUND:
                printf("优化建议: 优化着色器，减少复杂度\n");
                break;
        }
    }
};
```

这个播放管线设计为 Filament 提供了高效的动画系统，支持大量动画实例的实时播放。