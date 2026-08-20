# Filament 多线程架构设计

## 📖 概述

本文档深入分析 Filament 的多线程架构，包括主线程、渲染线程（Backend Thread）、JobSystem 工作线程池，以及它们之间的协作机制。理解多线程设计是充分发挥现代多核 CPU 性能的关键。

**核心线程**：
- **主线程（Main Thread）**：运行应用逻辑、更新场景、记录渲染命令
- **渲染线程（Backend Thread）**：执行命令队列、调用图形 API、提交 GPU 任务
- **JobSystem 线程池**：并行处理 CPU 密集型任务（剔除、排序、资源加载等）

**关键机制**：
- **线程分离**：主线程和渲染线程异步执行，避免互相阻塞
- **无锁通信**：使用循环缓冲区和原子操作实现高效通信
- **任务窃取**：JobSystem 使用 Work-Stealing 调度算法平衡负载

---

## 1. 多线程架构总览

### 1.1 线程模型

```
┌──────────────────────────────────────────────────────────────────┐
│                         Main Thread                              │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ Application Logic                                          │  │
│  │  - Update game state                                       │  │
│  │  - Process input                                           │  │
│  │  - Animation                                               │  │
│  └────────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ Filament Frontend                                          │  │
│  │  - Update scene (transforms, lights, materials)            │  │
│  │  - Record rendering commands                               │  │
│  │  - Submit to CommandStream                                 │  │
│  └───────────────────────┬────────────────────────────────────┘  │
└────────────────────────────┼─────────────────────────────────────┘
                             │ CommandStream
                             │ (Circular Buffer)
                             ↓
┌──────────────────────────────────────────────────────────────────┐
│                      Backend Thread                              │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ Command Execution                                          │  │
│  │  - Dequeue commands from CircularBuffer                    │  │
│  │  - Execute Driver calls                                    │  │
│  │  - Manage GPU resources                                    │  │
│  └────────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ Graphics API Calls                                         │  │
│  │  - Vulkan/Metal/OpenGL commands                            │  │
│  │  - Submit to GPU queue                                     │  │
│  └────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────┐
│                  JobSystem Thread Pool                           │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐         │
│  │ Worker 0 │  │ Worker 1 │  │ Worker 2 │  │ Worker 3 │   ...   │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘         │
│       │             │              │             │                │
│  [Parallel Tasks Execution]                                      │
│  - View frustum culling                                          │
│  - Shadow map culling                                            │
│  - Renderables sorting                                           │
│  - Resource loading/decoding                                     │
│  - Animation evaluation                                          │
│  - Physics simulation (if integrated)                            │
└──────────────────────────────────────────────────────────────────┘
```

### 1.2 线程职责划分

| 线程类型 | 数量 | 主要职责 | 优先级 |
|---------|------|---------|--------|
| **Main Thread** | 1 | 应用逻辑、场景更新、命令记录 | Normal |
| **Backend Thread** | 1 | 命令执行、GPU 资源管理、API 调用 | Display (High) |
| **JobSystem Workers** | N (CPU核心数-2) | 并行计算任务 | Background/Normal |

---

## 2. 主线程设计

### 2.1 主线程主循环

```cpp
/**
 * 典型的主线程渲染循环
 */
class Application {
    Engine* mEngine;
    Renderer* mRenderer;
    View* mView;
    Scene* mScene;

public:
    void mainLoop() {
        while (!shouldQuit()) {
            // 1. 处理输入事件 (通常由窗口系统驱动)
            processInput();

            // 2. 更新应用逻辑 (游戏状态、动画等)
            float deltaTime = calculateDeltaTime();
            updateGameLogic(deltaTime);

            // 3. 更新 Filament 场景 (在主线程)
            updateScene(deltaTime);

            // 4. 开始帧渲染
            if (mRenderer->beginFrame(mSwapChain)) {
                // 5. 记录渲染命令到 CommandStream
                mRenderer->render(mView);

                // 6. 结束帧并提交到渲染线程
                mRenderer->endFrame();
            }

            // 注意: beginFrame/endFrame 只是记录命令，
            // 实际GPU执行在Backend线程中异步进行
        }
    }

private:
    /**
     * 更新场景数据 (主线程)
     */
    void updateScene(float dt) {
        // 更新变换 (通过 TransformManager)
        auto& tcm = mEngine->getTransformManager();
        for (auto entity : mEntities) {
            auto instance = tcm.getInstance(entity);
            math::mat4f transform = calculateTransform(entity, dt);
            tcm.setTransform(instance, transform);
        }

        // 更新光源
        auto& lcm = mEngine->getLightManager();
        auto lightInstance = lcm.getInstance(mLightEntity);
        lcm.setIntensity(lightInstance, calculateIntensity(dt));

        // 更新材质参数
        for (auto mi : mMaterialInstances) {
            mi->setParameter("time", mTime);
            mi->setParameter("color", calculateColor(dt));
        }

        mTime += dt;
    }
};
```

### 2.2 线程安全的资源创建

```cpp
/**
 * Filament 的资源创建 API 是线程安全的
 * 内部使用锁保护共享数据结构
 */
class FEngine : public Engine {
    // 保护资源列表的互斥锁
    mutable Mutex mResourceListMutex;

    ResourceList<FTexture> mTextures;
    ResourceList<FMaterial> mMaterials;

public:
    /**
     * 创建纹理 (线程安全)
     */
    Texture* createTexture(Texture::Builder const& builder) {
        // 1. 分配前端对象 (无锁，使用线程安全的分配器)
        FTexture* texture = mHeapArena.make<FTexture>(*this, builder);

        // 2. 分配后端句柄 (无锁，原子操作)
        backend::Handle<backend::HwTexture> handle =
            mResourceAllocator.allocHandle();
        texture->mHandle = handle;

        // 3. 记录创建命令到 CommandStream (无锁)
        mDriverApi.createTexture(handle, /* ... */);

        // 4. 添加到资源列表 (需要加锁)
        {
            std::lock_guard<Mutex> lock(mResourceListMutex);
            mTextures.insert(texture);
        }

        return texture;
    }

    /**
     * 多线程环境下的使用示例
     */
    static void backgroundTextureLoading(Engine* engine) {
        std::thread([engine]() {
            // 在后台线程中创建纹理 (安全)
            Texture* tex = Texture::Builder()
                .width(1024)
                .height(1024)
                .format(Texture::InternalFormat::RGBA8)
                .build(*engine);

            // 加载图像数据
            void* data = loadImageFromDisk("texture.png");

            // 上传数据到 GPU (也是线程安全的)
            Texture::PixelBufferDescriptor buffer(data, 1024*1024*4,
                Texture::Format::RGBA, Texture::Type::UBYTE,
                [](void* buf, size_t, void*) { free(buf); });

            tex->setImage(*engine, 0, std::move(buffer));

            // 注意: 命令会被记录到 CommandStream，
            // 在渲染线程中异步执行
        }).detach();
    }
};
```

### 2.3 主线程性能优化

```cpp
/**
 * 避免主线程阻塞的最佳实践
 */
class OptimizedApp {
public:
    /**
     * ❌ 不好的做法: 等待 GPU 完成
     */
    void badPractice() {
        mRenderer->beginFrame(mSwapChain);
        mRenderer->render(mView);
        mRenderer->endFrame();

        // 等待渲染完成 - 会阻塞主线程!
        Fence* fence = mEngine->createFence();
        fence->wait(Fence::Mode::FLUSH, FENCE_WAIT_FOR_EVER);
        mEngine->destroy(fence);

        // 主线程被阻塞，CPU 空闲，性能损失!
    }

    /**
     * ✅ 好的做法: 异步执行，不等待
     */
    void goodPractice() {
        mRenderer->beginFrame(mSwapChain);
        mRenderer->render(mView);
        mRenderer->endFrame();

        // 不等待! 立即返回继续下一帧
        // GPU 和 CPU 并行工作，充分利用硬件

        // 如果需要读取渲染结果，使用异步回调
        readPixelsAsync([](void* data, size_t size) {
            // 在回调中处理结果
            processScreenshot(data, size);
        });
    }

    /**
     * 异步像素读取
     */
    void readPixelsAsync(std::function<void(void*, size_t)> callback) {
        size_t bufferSize = mWidth * mHeight * 4;
        void* buffer = malloc(bufferSize);

        Renderer::PixelBufferDescriptor pbd(buffer, bufferSize,
            Renderer::PixelDataFormat::RGBA,
            Renderer::PixelDataType::UBYTE,
            [callback](void* buf, size_t size, void*) {
                callback(buf, size);
                free(buf);
            });

        mRenderer->readPixels(mRenderTarget, 0, 0, mWidth, mHeight,
            std::move(pbd));

        // 立即返回，回调在数据ready时异步执行
    }
};
```

---

## 3. 渲染线程（Backend Thread）

### 3.1 渲染线程启动和配置

```cpp
/**
 * 渲染线程的创建和配置
 * 位置: filament/backend/src/Platform.cpp
 */
class PlatformThreadPoolExecutor {
    std::thread mBackendThread;
    std::atomic<bool> mExitRequested{false};
    CommandBufferQueue mCommandQueue;

public:
    /**
     * 启动渲染线程
     */
    void start(Driver* driver) {
        mBackendThread = std::thread([this, driver]() {
            // 1. 设置线程名称 (便于调试)
            utils::ThreadUtils::setThreadName("FilamentBackend");

            // 2. 设置线程优先级为 DISPLAY (高优先级)
            // 确保渲染不被其他任务打断，保持稳定帧率
            utils::ThreadUtils::setThreadPriority(
                utils::ThreadPriority::DISPLAY
            );

            // 3. 设置线程亲和性 (可选，固定到特定 CPU 核心)
            #ifdef __ANDROID__
            // Android 上，固定到大核 (Big core)
            utils::ThreadUtils::setThreadAffinity(
                utils::ThreadAffinity::BIG_CORES
            );
            #endif

            // 4. 进入渲染循环
            renderLoop(driver);
        });
    }

private:
    /**
     * 渲染线程主循环
     */
    void renderLoop(Driver* driver) {
        while (!mExitRequested.load(std::memory_order_relaxed)) {
            // 1. 等待命令缓冲区 (阻塞，直到有命令或超时)
            CommandStream* commands = mCommandQueue.waitForCommands(
                std::chrono::milliseconds(100)  // 100ms超时
            );

            if (!commands) {
                // 超时，检查退出标志
                continue;
            }

            // 2. 执行所有命令
            Systrace::beginSection("Driver::execute");
            driver->execute(commands);
            Systrace::endSection();

            // 3. 归还缓冲区给主线程重用
            mCommandQueue.releaseBuffer(commands);

            // 4. 周期性垃圾回收 (释放延迟销毁的资源)
            driver->collectGarbage();
        }

        // 5. 清理并退出
        driver->terminate();
    }
};
```

### 3.2 命令队列管理

```cpp
/**
 * 命令缓冲区队列 (主线程生产，渲染线程消费)
 * 位置: filament/backend/include/private/backend/CommandBufferQueue.h
 */
class CommandBufferQueue {
    static constexpr size_t BUFFER_COUNT = 3;  // 三缓冲

    // 缓冲区池
    CircularBuffer mBuffers[BUFFER_COUNT];

    // 当前写入缓冲区索引 (主线程使用)
    std::atomic<uint32_t> mWriteIndex{0};

    // 待执行队列 (渲染线程使用)
    std::mutex mQueueMutex;
    std::condition_variable mQueueCondition;
    std::queue<CircularBuffer*> mPendingBuffers;

public:
    /**
     * 获取可写缓冲区 (主线程调用)
     */
    CircularBuffer* getWriteBuffer() {
        uint32_t index = mWriteIndex.load(std::memory_order_relaxed);
        return &mBuffers[index % BUFFER_COUNT];
    }

    /**
     * 提交缓冲区到队列 (主线程调用)
     */
    void flush() {
        // 1. 获取当前缓冲区
        uint32_t index = mWriteIndex.load(std::memory_order_relaxed);
        CircularBuffer* buffer = &mBuffers[index % BUFFER_COUNT];

        // 2. 切换到下一个缓冲区
        mWriteIndex.store(index + 1, std::memory_order_release);

        // 3. 将缓冲区加入待执行队列
        {
            std::lock_guard<std::mutex> lock(mQueueMutex);
            mPendingBuffers.push(buffer);
        }

        // 4. 唤醒渲染线程
        mQueueCondition.notify_one();
    }

    /**
     * 等待并获取待执行缓冲区 (渲染线程调用)
     */
    CircularBuffer* waitForCommands(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mQueueMutex);

        // 等待队列非空或超时
        bool hasData = mQueueCondition.wait_for(lock, timeout,
            [this] { return !mPendingBuffers.empty(); }
        );

        if (!hasData) {
            return nullptr;  // 超时
        }

        // 取出队首缓冲区
        CircularBuffer* buffer = mPendingBuffers.front();
        mPendingBuffers.pop();

        return buffer;
    }

    /**
     * 释放已执行的缓冲区 (渲染线程调用)
     */
    void releaseBuffer(CircularBuffer* buffer) {
        // 重置缓冲区供主线程重用
        buffer->reset();
    }
};
```

### 3.3 渲染线程同步点

```cpp
/**
 * 渲染线程的关键同步点
 */
class Driver {
public:
    /**
     * 帧开始 - 等待上一帧GPU完成
     */
    void beginFrame(uint64_t frameId) {
        // 1. 等待N帧前的栅栏 (控制延迟)
        constexpr size_t MAX_FRAMES_IN_FLIGHT = 2;
        if (frameId >= MAX_FRAMES_IN_FLIGHT) {
            uint64_t waitFrameId = frameId - MAX_FRAMES_IN_FLIGHT;
            Fence* fence = mFrameFences[waitFrameId % MAX_FRAMES_IN_FLIGHT];

            if (fence) {
                // 等待GPU完成旧帧 (可能阻塞)
                fence->wait(TIMEOUT_INFINITE);
                destroyFence(fence);
            }
        }

        // 2. 开始新帧的命令记录
        mCurrentFrameId = frameId;
        beginCommandBuffer();
    }

    /**
     * 帧结束 - 提交到GPU并插入栅栏
     */
    void endFrame(uint64_t frameId) {
        // 1. 结束命令记录
        endCommandBuffer();

        // 2. 提交到GPU队列
        submitToGPU();

        // 3. 插入栅栏标记此帧
        Fence* fence = createFence();
        mFrameFences[frameId % MAX_FRAMES_IN_FLIGHT] = fence;

        // 4. 呈现到屏幕
        present();

        // 注意: present() 后立即返回，不等待vsync
        // 这样渲染线程可以继续处理下一帧的命令
    }

private:
    Fence* mFrameFences[2];  // 双缓冲栅栏
    uint64_t mCurrentFrameId = 0;
};
```

---

## 4. JobSystem 线程池

### 4.1 JobSystem 架构

```cpp
/**
 * JobSystem - Work-Stealing 任务调度系统
 * 位置: libs/utils/include/utils/JobSystem.h
 */
class JobSystem {
public:
    static constexpr size_t MAX_JOB_COUNT = 16384;  // 最大任务数

    /**
     * Job 结构体 (64字节，缓存行对齐)
     */
    class alignas(64) Job {
        // 任务数据存储 (48字节，可存储 std::function)
        void* storage[6];

        // 执行函数指针
        JobFunc function;

        // 父任务索引 (用于依赖管理)
        uint16_t parent;

        // 执行线程ID
        ThreadId id;

        // 引用计数
        std::atomic<uint8_t> refCount{1};

        // 子任务计数
        std::atomic<uint32_t> runningJobCount{1};
    };

private:
    // 每个线程的工作队列 (Work-Stealing Deque)
    struct alignas(64) ThreadState {
        WorkStealingDequeue<uint16_t> workQueue;
        std::thread thread;
        JobSystem* js;
        uint8_t threadId;
        uint32_t jobSeed;  // 随机种子，用于窃取
    };

    // 全局任务池
    Job mJobPool[MAX_JOB_COUNT];
    std::atomic<uint16_t> mJobPoolIndex{0};

    // 工作线程
    std::vector<ThreadState> mThreadStates;

    // 等待条件
    std::mutex mWaitMutex;
    std::condition_variable mWaitCondition;

public:
    /**
     * 创建 JobSystem
     * threadCount: 工作线程数量，0表示自动检测(CPU核心数-2)
     */
    explicit JobSystem(size_t threadCount = 0) {
        if (threadCount == 0) {
            // 自动检测: 保留1个核心给主线程，1个给渲染线程
            threadCount = std::max(1u,
                std::thread::hardware_concurrency() - 2);
        }

        mThreadStates.resize(threadCount);

        // 启动工作线程
        for (size_t i = 0; i < threadCount; ++i) {
            ThreadState& state = mThreadStates[i];
            state.js = this;
            state.threadId = (uint8_t)i;
            state.jobSeed = (uint32_t)i;

            state.thread = std::thread([&state]() {
                utils::ThreadUtils::setThreadName(
                    "JobSystem::Worker" + std::to_string(state.threadId)
                );
                state.js->workerLoop(state);
            });
        }
    }

    /**
     * 创建任务
     */
    Job* createJob(Job* parent, JobFunc func) {
        // 1. 从任务池分配
        uint16_t index = mJobPoolIndex.fetch_add(1, std::memory_order_relaxed);
        index &= (MAX_JOB_COUNT - 1);  // 循环使用

        Job* job = &mJobPool[index];

        // 2. 初始化任务
        job->function = func;
        job->parent = parent ? parent - mJobPool : 0xFFFF;
        job->refCount.store(1, std::memory_order_relaxed);
        job->runningJobCount.store(1, std::memory_order_relaxed);

        // 3. 增加父任务的子任务计数
        if (parent) {
            parent->runningJobCount.fetch_add(1, std::memory_order_relaxed);
        }

        return job;
    }

    /**
     * 运行任务
     */
    void run(Job* job) {
        ThreadId threadId = getThreadId();

        if (threadId < mThreadStates.size()) {
            // 在工作线程中，加入本地队列
            ThreadState& state = mThreadStates[threadId];
            state.workQueue.push(job - mJobPool);
        } else {
            // 在主线程中，随机选择一个工作队列
            uint32_t index = rand() % mThreadStates.size();
            mThreadStates[index].workQueue.push(job - mJobPool);
        }

        // 唤醒一个工作线程
        mWaitCondition.notify_one();
    }

    /**
     * 等待任务完成
     */
    void wait(Job* job) {
        // 在等待期间，执行其他任务 (避免死锁)
        while (job->runningJobCount.load(std::memory_order_acquire) > 0) {
            Job* nextJob = steal();
            if (nextJob) {
                execute(nextJob);
            } else {
                // 没有任务可窃取，短暂休眠
                std::this_thread::yield();
            }
        }
    }

private:
    /**
     * 工作线程主循环
     */
    void workerLoop(ThreadState& state) {
        while (!mExitRequested) {
            // 1. 从本地队列取任务
            uint16_t jobIndex;
            if (state.workQueue.pop(jobIndex)) {
                Job* job = &mJobPool[jobIndex];
                execute(job);
                continue;
            }

            // 2. 本地队列为空，尝试窃取其他线程的任务
            Job* stolenJob = steal(state);
            if (stolenJob) {
                execute(stolenJob);
                continue;
            }

            // 3. 没有任务，等待唤醒
            std::unique_lock<std::mutex> lock(mWaitMutex);
            mWaitCondition.wait_for(lock, std::chrono::milliseconds(10));
        }
    }

    /**
     * 窃取任务 (Work-Stealing 算法)
     */
    Job* steal(ThreadState& myState) {
        // 随机选择受害者线程 (避免争用)
        uint32_t victimIndex = myState.jobSeed % mThreadStates.size();
        myState.jobSeed = (myState.jobSeed + 1) * 0x9E3779B9;  // 哈希函数

        // 尝试从受害者队列底部窃取
        ThreadState& victim = mThreadStates[victimIndex];
        uint16_t jobIndex;

        if (victim.workQueue.steal(jobIndex)) {
            return &mJobPool[jobIndex];
        }

        return nullptr;
    }

    /**
     * 执行任务
     */
    void execute(Job* job) {
        // 1. 调用任务函数
        if (job->function) {
            job->function(job->storage, *this, job);
        }

        // 2. 减少任务的运行计数
        uint32_t count = job->runningJobCount.fetch_sub(1,
            std::memory_order_release);

        if (count == 1) {
            // 任务及其所有子任务已完成
            finish(job);
        }
    }

    /**
     * 完成任务
     */
    void finish(Job* job) {
        // 减少父任务的子任务计数
        uint16_t parentIndex = job->parent;
        if (parentIndex != 0xFFFF) {
            Job* parent = &mJobPool[parentIndex];
            uint32_t count = parent->runningJobCount.fetch_sub(1,
                std::memory_order_release);

            if (count == 1) {
                // 父任务也完成了
                finish(parent);
            }
        }

        // 唤醒可能在等待此任务的线程
        mWaitCondition.notify_all();
    }
};
```

### 4.2 JobSystem 使用示例

```cpp
/**
 * 实际场景中的 JobSystem 应用
 */
class RenderableManager {
    JobSystem& mJobSystem;

public:
    /**
     * 并行视锥剔除
     */
    void cullRenderables(
        Frustum const& frustum,
        utils::Slice<Entity> const& entities,
        std::vector<Entity>& outVisible
    ) {
        const size_t entityCount = entities.size();
        const size_t threadCount = mJobSystem.getThreadCount();
        const size_t jobCount = std::min(threadCount * 4, entityCount);

        // 每个job的分区大小
        const size_t jobSize = (entityCount + jobCount - 1) / jobCount;

        // 每个线程的局部结果
        struct LocalResult {
            std::vector<Entity> visible;
        };
        std::vector<LocalResult> localResults(jobCount);

        // 创建父任务
        JobSystem::Job* parent = mJobSystem.createJob();

        // 创建并行剔除任务
        for (size_t i = 0; i < jobCount; ++i) {
            size_t start = i * jobSize;
            size_t end = std::min(start + jobSize, entityCount);

            if (start >= end) break;

            // 创建子任务
            auto* job = jobs::createJob(mJobSystem, parent,
                [this, &frustum, &entities, &localResults, i, start, end]() {
                    auto& result = localResults[i].visible;

                    // 剔除此分区的对象
                    for (size_t j = start; j < end; ++j) {
                        Entity e = entities[j];
                        Box aabb = getAABB(e);

                        if (frustum.intersects(aabb)) {
                            result.push_back(e);
                        }
                    }
                }
            );

            // 提交任务
            mJobSystem.run(job);
        }

        // 运行父任务
        mJobSystem.run(parent);

        // 等待所有子任务完成
        mJobSystem.wait(parent);

        // 合并结果
        for (auto const& local : localResults) {
            outVisible.insert(outVisible.end(),
                local.visible.begin(), local.visible.end());
        }
    }

    /**
     * parallel_for 辅助函数
     */
    template<typename Func>
    void parallelFor(size_t count, Func&& func) {
        const size_t jobCount = std::min(
            mJobSystem.getThreadCount() * 4, count
        );
        const size_t jobSize = (count + jobCount - 1) / jobCount;

        JobSystem::Job* parent = mJobSystem.createJob();

        for (size_t i = 0; i < jobCount; ++i) {
            size_t start = i * jobSize;
            size_t end = std::min(start + jobSize, count);

            auto* job = jobs::createJob(mJobSystem, parent,
                [&func, start, end]() {
                    for (size_t j = start; j < end; ++j) {
                        func(j);
                    }
                }
            );

            mJobSystem.run(job);
        }

        mJobSystem.run(parent);
        mJobSystem.wait(parent);
    }
};

/**
 * 使用示例
 */
void updateTransforms(TransformManager& tcm, std::vector<Entity> const& entities) {
    JobSystem& js = Engine::getJobSystem();

    // 并行更新所有变换矩阵
    jobs::parallel_for(js, entities.data(), entities.size(),
        [&tcm](Entity const* data, size_t count) {
            for (size_t i = 0; i < count; ++i) {
                auto instance = tcm.getInstance(data[i]);
                math::mat4f matrix = tcm.getTransform(instance);

                // 计算世界矩阵
                math::mat4f worldMatrix = computeWorldMatrix(matrix);
                tcm.setWorldTransform(instance, worldMatrix);
            }
        }
    );
}
```

---

## 5. 线程间通信

### 5.1 无锁数据结构

```cpp
/**
 * Work-Stealing Deque (Chase-Lev 算法)
 * 位置: libs/utils/include/utils/WorkStealingDequeue.h
 */
template<typename T, size_t SIZE>
class WorkStealingDequeue {
    static_assert((SIZE & (SIZE - 1)) == 0, "SIZE must be power of 2");

    std::atomic<int64_t> mTop{0};     // 队列顶部 (push/pop)
    std::atomic<int64_t> mBottom{0};  // 队列底部 (steal)
    std::array<T, SIZE> mItems;

public:
    /**
     * 推入任务 (仅所有者线程调用)
     */
    void push(T item) {
        int64_t bottom = mBottom.load(std::memory_order_relaxed);
        mItems[bottom & (SIZE - 1)] = item;

        // 确保写入item在更新bottom之前完成
        std::atomic_thread_fence(std::memory_order_release);

        mBottom.store(bottom + 1, std::memory_order_relaxed);
    }

    /**
     * 弹出任务 (仅所有者线程调用)
     */
    bool pop(T& out) {
        int64_t bottom = mBottom.load(std::memory_order_relaxed) - 1;
        mBottom.store(bottom, std::memory_order_relaxed);

        std::atomic_thread_fence(std::memory_order_seq_cst);

        int64_t top = mTop.load(std::memory_order_relaxed);

        if (top <= bottom) {
            // 队列非空
            out = mItems[bottom & (SIZE - 1)];

            if (top == bottom) {
                // 最后一个元素，需要CAS确保
                if (!mTop.compare_exchange_strong(top, top + 1,
                        std::memory_order_seq_cst,
                        std::memory_order_relaxed)) {
                    // CAS失败，被窃取了
                    mBottom.store(bottom + 1, std::memory_order_relaxed);
                    return false;
                }

                mBottom.store(bottom + 1, std::memory_order_relaxed);
            }

            return true;
        } else {
            // 队列为空
            mBottom.store(bottom + 1, std::memory_order_relaxed);
            return false;
        }
    }

    /**
     * 窃取任务 (其他线程调用)
     */
    bool steal(T& out) {
        int64_t top = mTop.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t bottom = mBottom.load(std::memory_order_acquire);

        if (top < bottom) {
            // 队列非空，窃取顶部元素
            out = mItems[top & (SIZE - 1)];

            // CAS更新top
            if (mTop.compare_exchange_strong(top, top + 1,
                    std::memory_order_seq_cst,
                    std::memory_order_relaxed)) {
                return true;  // 窃取成功
            }
        }

        return false;  // 窃取失败
    }
};
```

### 5.2 原子操作优化

```cpp
/**
 * 使用原子操作避免锁
 */
class ResourceAllocator {
    static constexpr uint32_t MAX_HANDLES = 1 << 24;  // 16M handles

    // 句柄分配器 (无锁)
    std::atomic<uint32_t> mHandleCounter{1};

public:
    /**
     * 分配句柄 (线程安全，无锁)
     */
    template<typename T>
    Handle<T> allocHandle() {
        uint32_t id = mHandleCounter.fetch_add(1, std::memory_order_relaxed);

        // 循环使用ID (避免溢出)
        id = (id & (MAX_HANDLES - 1)) | (id & ~(MAX_HANDLES - 1));

        return Handle<T>(id);
    }
};

/**
 * 引用计数优化 (避免虚假共享)
 */
class alignas(64) RefCounted {  // 缓存行对齐
    std::atomic<uint32_t> mRefCount{1};

    // 填充到64字节，避免与其他对象共享缓存行
    char mPadding[64 - sizeof(std::atomic<uint32_t>)];

public:
    void addRef() {
        mRefCount.fetch_add(1, std::memory_order_relaxed);
    }

    bool release() {
        // 使用 release 语义确保之前的修改可见
        if (mRefCount.fetch_sub(1, std::memory_order_release) == 1) {
            // 使用 acquire 语义确保看到最终状态
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;  // 可以删除
        }
        return false;
    }
};
```

---

## 6. 同步机制

### 6.1 Fence 同步

```cpp
/**
 * GPU Fence - CPU等待GPU完成
 */
class VulkanFence {
    VkFence mFence;
    VkDevice mDevice;
    bool mSignaled = false;

public:
    /**
     * 等待 Fence
     */
    FenceStatus wait(uint64_t timeout) {
        if (mSignaled) {
            return FenceStatus::CONDITION_SATISFIED;
        }

        VkResult result = vkWaitForFences(
            mDevice, 1, &mFence, VK_TRUE, timeout
        );

        if (result == VK_SUCCESS) {
            mSignaled = true;
            return FenceStatus::CONDITION_SATISFIED;
        } else if (result == VK_TIMEOUT) {
            return FenceStatus::TIMEOUT_EXPIRED;
        }

        return FenceStatus::ERROR;
    }
};

/**
 * 软件 Fence - 线程间同步
 */
class SoftwareFence {
    std::mutex mMutex;
    std::condition_variable mCondition;
    std::atomic<bool> mSignaled{false};

public:
    void signal() {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mSignaled.store(true, std::memory_order_release);
        }
        mCondition.notify_all();
    }

    void wait() {
        std::unique_lock<std::mutex> lock(mMutex);
        mCondition.wait(lock, [this] {
            return mSignaled.load(std::memory_order_acquire);
        });
    }

    bool waitFor(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mMutex);
        return mCondition.wait_for(lock, timeout, [this] {
            return mSignaled.load(std::memory_order_acquire);
        });
    }
};
```

### 6.2 双缓冲同步

```cpp
/**
 * 双缓冲模式 - 减少锁争用
 */
template<typename T>
class DoubleBuffer {
    T mBuffers[2];
    std::atomic<uint32_t> mWriteIndex{0};
    std::atomic<uint32_t> mReadIndex{1};

public:
    /**
     * 获取写缓冲 (生产者)
     */
    T& getWriteBuffer() {
        return mBuffers[mWriteIndex.load(std::memory_order_relaxed)];
    }

    /**
     * 交换缓冲区
     */
    void swap() {
        uint32_t write = mWriteIndex.load(std::memory_order_relaxed);
        uint32_t read = mReadIndex.load(std::memory_order_relaxed);

        // 原子交换
        mWriteIndex.store(read, std::memory_order_release);
        mReadIndex.store(write, std::memory_order_release);
    }

    /**
     * 获取读缓冲 (消费者)
     */
    T const& getReadBuffer() const {
        return mBuffers[mReadIndex.load(std::memory_order_acquire)];
    }
};

/**
 * 使用示例: 动画数据双缓冲
 */
class AnimationSystem {
    struct AnimationData {
        std::vector<math::mat4f> jointMatrices;
        std::vector<math::quat> rotations;
    };

    DoubleBuffer<AnimationData> mAnimData;

public:
    /**
     * 后台线程更新动画
     */
    void updateAnimation(float deltaTime) {
        // 写入后台缓冲区
        auto& writeBuffer = mAnimData.getWriteBuffer();

        for (auto& joint : writeBuffer.jointMatrices) {
            joint = calculateJointMatrix(deltaTime);
        }

        // 交换缓冲区
        mAnimData.swap();
    }

    /**
     * 渲染线程读取动画数据
     */
    void uploadToGPU() {
        // 读取前台缓冲区 (无锁)
        auto const& readBuffer = mAnimData.getReadBuffer();

        // 上传到GPU
        updateSkinningBuffer(readBuffer.jointMatrices);
    }
};
```

---

## 7. 性能优化

### 7.1 缓存行优化

```cpp
/**
 * 避免伪共享 (False Sharing)
 */
struct alignas(64) CacheLineAligned {
    std::atomic<uint64_t> value;
    char padding[64 - sizeof(std::atomic<uint64_t>)];
};

/**
 * 良好的数据布局
 */
class OptimizedThreadData {
    // ❌ 不好: 原子变量可能在同一缓存行
    struct Bad {
        std::atomic<int> counter1;  // 线程1写
        std::atomic<int> counter2;  // 线程2写
        // 导致缓存行乒乓 (Cache Line Ping-Pong)
    };

    // ✅ 好: 每个原子变量独占缓存行
    struct alignas(64) Good {
        std::atomic<int> counter1;
        char padding1[64 - sizeof(std::atomic<int>)];
    };

    struct alignas(64) Good2 {
        std::atomic<int> counter2;
        char padding2[64 - sizeof(std::atomic<int>)];
    };
};
```

### 7.2 线程局部存储

```cpp
/**
 * Thread-Local Storage (TLS) 优化
 */
class PerThreadAllocator {
    // 每个线程的独立分配器
    static thread_local LinearAllocator tls_allocator;

public:
    /**
     * 线程局部分配 (无锁)
     */
    static void* allocate(size_t size) {
        return tls_allocator.allocate(size);
    }

    /**
     * 每帧重置
     */
    static void reset() {
        tls_allocator.reset();
    }
};

/**
 * 使用示例
 */
void parallelProcessing() {
    JobSystem& js = getJobSystem();

    jobs::parallel_for(js, 0, 10000, [](size_t i) {
        // 每个线程使用独立的分配器，无竞争
        void* buffer = PerThreadAllocator::allocate(1024);

        // 处理数据...

        // 帧结束时统一释放，无需逐个free
    });
}
```

### 7.3 批处理减少同步

```cpp
/**
 * 批量操作减少线程切换
 */
class BatchedCommandQueue {
    static constexpr size_t BATCH_SIZE = 256;

    struct Batch {
        std::array<Command, BATCH_SIZE> commands;
        size_t count = 0;
    };

    thread_local static Batch tls_batch;

public:
    /**
     * 添加命令 (线程局部批处理)
     */
    void enqueue(Command cmd) {
        tls_batch.commands[tls_batch.count++] = cmd;

        if (tls_batch.count >= BATCH_SIZE) {
            flush();
        }
    }

    /**
     * 批量提交
     */
    void flush() {
        if (tls_batch.count == 0) return;

        // 一次性提交整个批次
        globalQueue.enqueueBatch(
            tls_batch.commands.data(),
            tls_batch.count
        );

        tls_batch.count = 0;
    }
};
```

---

## 8. 调试和诊断

### 8.1 线程死锁检测

```cpp
/**
 * 死锁检测工具
 */
class DeadlockDetector {
    struct LockInfo {
        std::thread::id thread;
        void* lockAddress;
        const char* location;
    };

    static thread_local std::vector<LockInfo> tls_heldLocks;
    static std::mutex s_globalMutex;
    static std::map<std::thread::id, std::vector<LockInfo>> s_allLocks;

public:
    /**
     * 记录锁获取
     */
    static void recordLock(void* lock, const char* location) {
        #ifdef DEBUG
        LockInfo info{
            std::this_thread::get_id(),
            lock,
            location
        };

        tls_heldLocks.push_back(info);

        {
            std::lock_guard<std::mutex> guard(s_globalMutex);
            s_allLocks[info.thread] = tls_heldLocks;
        }

        // 检测死锁
        detectDeadlock();
        #endif
    }

    /**
     * 记录锁释放
     */
    static void recordUnlock(void* lock) {
        #ifdef DEBUG
        auto it = std::find_if(tls_heldLocks.begin(), tls_heldLocks.end(),
            [lock](auto const& info) { return info.lockAddress == lock; }
        );

        if (it != tls_heldLocks.end()) {
            tls_heldLocks.erase(it);
        }
        #endif
    }

private:
    static void detectDeadlock() {
        // 简化的死锁检测逻辑
        // 实际应使用图算法检测循环依赖
    }
};

/**
 * RAII 锁包装器
 */
template<typename Mutex>
class TrackedLock {
    Mutex& mMutex;
    const char* mLocation;

public:
    TrackedLock(Mutex& mutex, const char* location)
        : mMutex(mutex), mLocation(location)
    {
        DeadlockDetector::recordLock(&mMutex, mLocation);
        mMutex.lock();
    }

    ~TrackedLock() {
        mMutex.unlock();
        DeadlockDetector::recordUnlock(&mMutex);
    }
};

#define TRACKED_LOCK(mutex) TrackedLock lock(mutex, __FILE__ ":" STRINGIFY(__LINE__))
```

### 8.2 线程性能分析

```cpp
/**
 * 线程性能追踪
 */
class ThreadProfiler {
    struct ThreadStats {
        std::thread::id threadId;
        std::string threadName;
        uint64_t totalTime;      // 总运行时间
        uint64_t idleTime;       // 空闲时间
        uint64_t taskCount;      // 执行任务数
    };

    static thread_local ThreadStats tls_stats;
    static std::vector<ThreadStats> s_allStats;

public:
    /**
     * 任务执行计时
     */
    class ScopedTimer {
        auto mStart = std::chrono::high_resolution_clock::now();

    public:
        ~ScopedTimer() {
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                end - mStart
            ).count();

            tls_stats.totalTime += duration;
            tls_stats.taskCount++;
        }
    };

    /**
     * 打印统计信息
     */
    static void printStats() {
        std::cout << "Thread Profiling Results:\n";
        std::cout << "┌────────────────┬──────────┬──────────┬──────────┐\n";
        std::cout << "│ Thread Name    │ Tasks    │ Time(ms) │ Util(%)  │\n";
        std::cout << "├────────────────┼──────────┼──────────┼──────────┤\n";

        for (auto const& stats : s_allStats) {
            float utilization = (stats.totalTime - stats.idleTime) * 100.0f
                              / stats.totalTime;

            std::cout << "│ " << std::setw(14) << stats.threadName
                      << " │ " << std::setw(8) << stats.taskCount
                      << " │ " << std::setw(8) << stats.totalTime / 1000
                      << " │ " << std::setw(7) << std::fixed << std::setprecision(2)
                      << utilization << "% │\n";
        }

        std::cout << "└────────────────┴──────────┴──────────┴──────────┘\n";
    }
};

/**
 * 使用示例
 */
void workerTask() {
    ThreadProfiler::ScopedTimer timer;

    // 执行任务...
}
```

---

## 9. 最佳实践

### 9.1 多线程设计原则

1. **最小化共享状态**: 优先使用线程局部数据
2. **无锁优先**: 使用原子操作代替锁
3. **批处理**: 减少线程间通信频率
4. **缓存友好**: 注意数据布局和内存访问模式
5. **避免过度同步**: 只在必要时同步

```cpp
/**
 * ✅ 好的设计
 */
class GoodDesign {
    // 每个线程独立的数据 (无共享)
    thread_local static std::vector<Entity> tls_culledEntities;

public:
    void cullInParallel(Scene* scene, Frustum const& frustum) {
        // 并行剔除，每个线程写入自己的局部向量
        jobs::parallel_for(js, scene->getEntities(), [&](Entity e) {
            if (frustum.intersects(getAABB(e))) {
                tls_culledEntities.push_back(e);  // 无锁
            }
        });

        // 主线程合并结果 (仅同步一次)
        mergeResults();
    }
};

/**
 * ❌ 不好的设计
 */
class BadDesign {
    std::vector<Entity> mCulledEntities;  // 共享状态
    std::mutex mMutex;                    // 需要锁保护

public:
    void cullInParallel(Scene* scene, Frustum const& frustum) {
        jobs::parallel_for(js, scene->getEntities(), [&](Entity e) {
            if (frustum.intersects(getAABB(e))) {
                // 每次push都需要加锁 - 性能灾难!
                std::lock_guard<std::mutex> lock(mMutex);
                mCulledEntities.push_back(e);
            }
        });
    }
};
```

### 9.2 线程安全检查清单

- [ ] 识别所有共享数据
- [ ] 为共享数据添加适当的同步机制
- [ ] 避免嵌套锁 (防止死锁)
- [ ] 考虑缓存行对齐 (避免伪共享)
- [ ] 使用 Thread Sanitizer 检测数据竞争
- [ ] 压力测试多线程场景

---

## 10. 常见问题

### Q1: 如何在多线程中安全地使用 Filament API？

**A**:
- 资源创建 API (createTexture, createMaterial 等) 是线程安全的
- 同一个 Renderer 只能在一个线程中使用
- 避免在多个线程中同时修改同一个 Entity 的组件

### Q2: JobSystem 的线程数如何设置？

**A**:
```cpp
// 默认: CPU核心数 - 2 (保留主线程和渲染线程)
JobSystem js(0);

// 手动设置
JobSystem js(4);  // 4个工作线程

// 移动设备: 更保守
#ifdef __ANDROID__
JobSystem js(std::max(1u, std::thread::hardware_concurrency() / 2));
#endif
```

### Q3: 如何调试多线程问题？

**A**:
1. 使用 Thread Sanitizer: `clang++ -fsanitize=thread`
2. 启用死锁检测工具
3. 使用断言验证线程ID
4. 记录详细的线程日志

### Q4: 渲染线程卡顿怎么办？

**A**:
1. 检查命令缓冲区大小是否足够
2. 使用 Systrace/Profiler 定位瓶颈
3. 减少单帧的 draw call 数量
4. 优化资源创建时机 (避免在渲染时创建)

---

## 11. 相关文档

- [01-data-flow-analysis.md](./01-data-flow-analysis.md) - 数据流架构
- [03-pipeline-architecture.md](./03-pipeline-architecture.md) - 渲染管线
- [../optimization/07-cpu-gpu-sync.md](../optimization/07-cpu-gpu-sync.md) - CPU-GPU 同步优化
- [../debugging/04-performance-profiling.md](../debugging/04-performance-profiling.md) - 性能分析

---

## 12. 总结

Filament 的多线程架构体现了现代渲染引擎的最佳实践：

1. **线程分离**: 主线程和渲染线程异步执行，CPU和GPU并行
2. **无锁设计**: 使用原子操作和循环缓冲区避免锁争用
3. **任务并行**: JobSystem 实现 Work-Stealing，充分利用多核 CPU
4. **缓存优化**: 注意数据布局，减少伪共享
5. **性能优先**: 最小化同步点，批处理优化通信

理解多线程设计是优化 Filament 应用性能的关键，它决定了：
- 如何充分利用硬件资源
- 如何避免性能瓶颈
- 如何设计线程安全的应用架构
- 如何调试并发问题

通过合理使用多线程，您可以构建高性能、响应快速的渲染应用。
