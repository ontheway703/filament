# 命令流和多线程架构

本文档详细讲解 Filament CommandStream 的工作原理，包括命令录制、异步执行、线程同步机制，以及如何实现主线程和渲染线程的分离。

---

## 为什么需要 CommandStream？

### 传统单线程渲染的问题

在传统的渲染架构中，渲染操作通常在主线程执行：

```cpp
// ❌ 传统方式：主线程直接调用 OpenGL
void renderFrame() {
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);  // 主线程阻塞！
    glSwapBuffers();
}
```

**问题**：
1. **主线程阻塞**: GPU 操作可能很慢，主线程被迫等待
2. **CPU 利用率低**: 主线程等待 GPU 时无法做其他工作
3. **帧率受限**: 准备下一帧的数据必须等当前帧渲染完成

### Filament 的解决方案：三线程架构

Filament 实际使用 **3 个线程**（如果启用多线程）：

1. **主线程 (Application Thread)**: 用户代码运行的线程，负责录制命令
2. **渲染线程 (Render Thread)**: 执行图形 API 调用的线程
3. **服务线程 (Service Thread)**: 处理用户回调的线程（在 DriverBase 中创建）

```
┌─────────────────────────────────────────────────────────────────┐
│                     主线程 (Application Thread)                   │
│                                                                   │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  while (running) {                                        │   │
│  │      // 1. 处理用户输入                                    │   │
│  │      handleInput();                                       │   │
│  │                                                           │   │
│  │      // 2. 更新场景                                        │   │
│  │      updateScene(deltaTime);                              │   │
│  │                                                           │   │
│  │      // 3. 录制渲染命令（不阻塞！）                        │   │
│  │      driver.createTexture(...);    // 录制到命令流         │   │
│  │      driver.draw(...);              // 录制到命令流         │   │
│  │                                                           │   │
│  │      // 4. 提交命令到渲染线程                              │   │
│  │      driver.flush();                // 非阻塞              │   │
│  │                                                           │   │
│  │      // 5. 继续准备下一帧（不等待GPU）                     │   │
│  │  }                                                        │   │
│  └──────────────────────────────────────────────────────────┘   │
└───────────────────────────────┬───────────────────────────────┘
                                │
                          CommandStream
                     (线程安全的命令队列)
                                │
                                ↓
┌─────────────────────────────────────────────────────────────────┐
│                     渲染线程 (Render Thread)                      │
│                                                                   │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  while (running) {                                        │   │
│  │      // 1. 等待命令                                        │   │
│  │      commandStream.wait();                                │   │
│  │                                                           │   │
│  │      // 2. 执行命令（调用实际的 OpenGL/Vulkan/Metal）      │   │
│  │      commandStream.execute();                             │   │
│  │          ├─> glGenTextures()                              │   │
│  │          ├─> glDrawElements()                             │   │
│  │          └─> ...                                          │   │
│  │                                                           │   │
│  │      // 3. GPU 执行渲染                                    │   │
│  │  }                                                        │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

**优势**：
- ✅ **主线程不阻塞**: 可以立即准备下一帧
- ✅ **CPU 并行**: 主线程准备数据，渲染线程执行 GPU 命令
- ✅ **提高帧率**: 充分利用 CPU 多核能力

---

## CommandStream 架构

### 核心类设计

**文件位置**: `filament/backend/src/CommandStream.h`

```cpp
class CommandStream {
public:
    // 构造函数
    explicit CommandStream(Driver& driver, CircularBuffer& buffer);

    // 录制命令（主线程调用）
    template<typename... ARGS>
    void queueCommand(void (Driver::*method)(ARGS...), ARGS... args);

    // 执行命令（渲染线程调用）
    void execute(void* buffer);

    // 同步操作
    void flush();    // 提交命令
    void wait();     // 等待命令完成
    void finish();   // 等待所有命令完成

private:
    Driver& mDriver;                    // Driver 引用
    CircularBuffer& mCircularBuffer;    // 循环缓冲区
};
```

### 循环缓冲区 (CircularBuffer)

**文件位置**: `filament/backend/include/private/backend/CircularBuffer.h`

CommandStream 使用循环缓冲区存储命令和参数：

```cpp
class CircularBuffer {
public:
    explicit CircularBuffer(size_t bufferSize);

    // 分配空间（主线程写入）
    void* allocate(size_t s) noexcept {
        // 断言：不能分配超过总大小
        assert_invariant(getUsed() + s <= size());
        char* const cur = static_cast<char*>(mHead);
        mHead = cur + s;  // 写指针向前移动
        return cur;
    }

    // 获取当前已写入的范围，并重置写指针
    Range getBuffer() noexcept;

    // 判断是否为空
    bool empty() const noexcept { return mTail == mHead; }

    // 获取已使用的大小
    size_t getUsed() const noexcept {
        return intptr_t(mHead) - intptr_t(mTail);
    }

private:
    void* mData;    // 缓冲区起始地址（常量）
    size_t mSize;   // 缓冲区总大小（常量）

    void* mTail;    // 读取位置（渲染线程读）
    void* mHead;    // 写入位置（主线程写）
};
```

**内存布局**:
```
环形缓冲区（推荐 3 * requiredSize）
┌────────────────────────────────────────────────────────────┐
│    已消费区域    │   待执行命令   │    可用空间            │
│   (已释放)       │               │                        │
└────────────────────────────────────────────────────────────┘
                  ↑               ↑                          ↑
                mTail           mHead                    mData+mSize
              (读指针)         (写指针)

当 mFreeSpace < requiredSize 时：
- 主线程阻塞等待渲染线程消费（通过 CommandBufferQueue）
- 渲染线程执行完命令后调用 releaseBuffer() 归还空间
- 唤醒主线程继续写入
```

**推荐大小**: `bufferSize = 3 * requiredSize`

- **1x requiredSize**: 主线程当前正在写入
- **1x requiredSize**: 渲染线程正在执行
- **1x requiredSize**: 额外缓冲，避免阻塞

---

## 命令录制机制

### queueCommand 实现

```cpp
template<typename... ARGS>
void CommandStream::queueCommand(
    void (Driver::*method)(ARGS...),
    ARGS... args
) {
    // 1. 计算需要的空间
    constexpr size_t commandSize = sizeof(Command);
    constexpr size_t argsSize = (sizeof(ARGS) + ...);
    size_t totalSize = commandSize + argsSize;

    // 2. 分配缓冲区空间
    void* buffer = mCircularBuffer.allocate(totalSize);

    // 3. 写入命令头
    Command* cmd = new(buffer) Command();
    cmd->methodPtr = reinterpret_cast<void*>(method);

    // 4. 写入参数（参数拷贝）
    uint8_t* argsBuffer = reinterpret_cast<uint8_t*>(cmd + 1);
    size_t offset = 0;

    // 展开参数包，依次拷贝
    ((new(argsBuffer + offset) std::decay_t<ARGS>(std::forward<ARGS>(args)),
      offset += sizeof(std::decay_t<ARGS>)), ...);
}
```

### 命令结构

```cpp
struct Command {
    void* methodPtr;     // 方法指针
    uint32_t size;       // 参数总大小
};
```

### 实际示例

```cpp
// 主线程调用
Handle<HwTexture> texHandle = driver.createTexture(
    SamplerType::SAMPLER_2D,
    1,                      // levels
    TextureFormat::RGBA8,
    1,                      // samples
    512, 512, 1            // width, height, depth
);

// 实际发生的事情：
commandStream.queueCommand(
    &Driver::createTextureImpl,  // 方法指针
    SamplerType::SAMPLER_2D,     // 参数1
    1,                            // 参数2
    TextureFormat::RGBA8,         // 参数3
    1,                            // 参数4
    512, 512, 1                  // 参数5,6,7
);

// 缓冲区写入：
// [Command头] [SamplerType] [levels] [format] [samples] [width] [height] [depth]
```

---

## 命令执行机制

### execute() 实现

```cpp
void CommandStream::execute(void* buffer) {
    uint8_t* current = reinterpret_cast<uint8_t*>(buffer);
    uint8_t* end = current + mCircularBuffer.getUsedSize();

    // 遍历所有命令
    while (current < end) {
        // 1. 读取命令头
        Command* cmd = reinterpret_cast<Command*>(current);
        current += sizeof(Command);

        // 2. 读取参数
        void* args = current;
        current += cmd->size;

        // 3. 调用 Driver 方法
        dispatchCommand(cmd->methodPtr, args);
    }

    // 4. 重置缓冲区
    mCircularBuffer.reset();
}
```

### 命令分发 (Dispatcher)

Filament 使用模板元编程实现类型安全的命令分发：

```cpp
template<typename... ARGS>
class Dispatcher {
public:
    static void dispatch(
        Driver* driver,
        void (Driver::*method)(ARGS...),
        void* argsBuffer
    ) {
        // 从缓冲区解包参数
        auto args = unpackArgs<ARGS...>(argsBuffer);

        // 调用 Driver 方法
        std::apply([driver, method](auto&&... args) {
            (driver->*method)(std::forward<decltype(args)>(args)...);
        }, args);
    }

private:
    template<typename... T>
    static std::tuple<T...> unpackArgs(void* buffer) {
        uint8_t* current = reinterpret_cast<uint8_t*>(buffer);
        return std::make_tuple(readArg<T>(current)...);
    }

    template<typename T>
    static T readArg(uint8_t*& buffer) {
        T* ptr = reinterpret_cast<T*>(buffer);
        buffer += sizeof(T);
        return *ptr;
    }
};
```

### 实际分发流程

```cpp
// 1. 命令缓冲区中存储的内容
// [methodPtr = &Driver::createTextureImpl]
// [args = {SAMPLER_2D, 1, RGBA8, 1, 512, 512, 1}]

// 2. execute() 读取命令
Command* cmd = readCommand();
void* args = readArgs();

// 3. 类型安全的分发
Dispatcher<SamplerType, uint8_t, TextureFormat, uint8_t,
           uint32_t, uint32_t, uint32_t>::dispatch(
    driver,
    cmd->methodPtr,
    args
);

// 4. 调用实际的 Driver 实现
// 对于 OpenGL:
Handle<HwTexture> OpenGLDriver::createTextureImpl(
    SamplerType::SAMPLER_2D,
    1, TextureFormat::RGBA8, 1, 512, 512, 1
) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 512, 512);
    return makeHandle(texture);
}
```

---

## CommandBufferQueue: 生产者-消费者模型

**文件位置**: `filament/backend/include/private/backend/CommandBufferQueue.h`

CommandBufferQueue 封装了 CircularBuffer，实现了主线程和渲染线程之间的同步机制。

### 核心数据结构

```cpp
class CommandBufferQueue {
public:
    struct Range {
        void* begin;  // 命令缓冲区起始地址
        void* end;    // 命令缓冲区结束地址
    };

private:
    const size_t mRequiredSize;              // 保证的可用空间
    CircularBuffer mCircularBuffer;           // 唯一的环形缓冲区

    mutable utils::Mutex mLock;               // 互斥锁
    mutable utils::Condition mCondition;      // 条件变量
    mutable std::vector<Range> mCommandBuffersToExecute;  // 待执行队列
    size_t mFreeSpace;                        // 当前可用空间
    uint32_t mExitRequested;                  // 退出标志
    bool mPaused;                             // 暂停标志
};
```

### 主线程：生产者（flush）

**文件位置**: `filament/backend/src/CommandBufferQueue.cpp:82-143`

```cpp
void CommandBufferQueue::flush() {
    CircularBuffer& circularBuffer = mCircularBuffer;
    if (circularBuffer.empty()) {
        return;  // 没有命令，直接返回
    }

    // 1. 添加终止命令
    new(circularBuffer.allocate(sizeof(NoopCommand))) NoopCommand(nullptr);

    // 2. 获取当前写入的范围 [begin, end)
    auto const [begin, end] = circularBuffer.getBuffer();

    // 3. 计算使用的空间
    size_t const used = std::distance(
        static_cast<char const*>(begin),
        static_cast<char const*>(end));

    std::unique_lock lock(mLock);

    // 4. 检查是否溢出
    FILAMENT_CHECK_POSTCONDITION(used <= mFreeSpace) <<
        "Backend CommandStream overflow!";

    // 5. 更新可用空间
    mFreeSpace -= used;

    // 6. 加入待执行队列
    mCommandBuffersToExecute.push_back({ begin, end });

    // 7. 通知渲染线程
    mCondition.notify_one();

    // 8. 如果剩余空间不足，阻塞等待
    if (UTILS_UNLIKELY(mFreeSpace < mRequiredSize)) {
        mCondition.wait(lock, [this]() -> bool {
            return mFreeSpace >= mRequiredSize;
        });
    }
}
```

### 渲染线程：消费者（waitForCommands + releaseBuffer）

**文件位置**: `filament/backend/src/CommandBufferQueue.cpp:145-162`

```cpp
// 1. 等待命令
std::vector<Range> CommandBufferQueue::waitForCommands() const {
    std::unique_lock lock(mLock);

    // 等待直到有命令可执行
    while ((mCommandBuffersToExecute.empty() || mPaused) && !mExitRequested) {
        mCondition.wait(lock);
    }

    // 移动队列中的所有命令（避免拷贝）
    return std::move(mCommandBuffersToExecute);
}

// 2. 释放缓冲区
void CommandBufferQueue::releaseBuffer(Range const& buffer) {
    // 计算释放的大小
    size_t const used = std::distance(
        static_cast<char const*>(buffer.begin),
        static_cast<char const*>(buffer.end));

    std::lock_guard lock(mLock);

    // 归还空间
    mFreeSpace += used;

    // 唤醒可能在等待的主线程
    mCondition.notify_one();
}
```

### 完整流程图

```
主线程（生产者）:
  ├─> allocate() 分配空间
  ├─> 写入命令
  ├─> flush()
  │    ├─> 获取 [begin, end) 范围
  │    ├─> mFreeSpace -= used
  │    ├─> mCommandBuffersToExecute.push_back({begin, end})
  │    ├─> notify_one()  唤醒渲染线程
  │    └─> 如果 mFreeSpace < requiredSize，wait() 阻塞
  │
  │ (等待空间可用...)
  │
  ↑ notify_one() ← releaseBuffer()

渲染线程（消费者）:
  ├─> waitForCommands()
  │    ├─> 等待 mCommandBuffersToExecute 非空
  │    └─> 返回所有待执行的命令
  │
  ├─> for (auto& range : buffers)
  │    └─> driver.execute(range.begin)  // 执行命令
  │
  └─> releaseBuffer(range)
       ├─> mFreeSpace += used
       └─> notify_one()  唤醒主线程
```

---

## 完整的命令流程

### 主线程视角

```cpp
// 第 N 帧
void renderFrameN() {
    // 1. 更新场景数据
    scene->update(deltaTime);

    // 2. 录制渲染命令（写入 CommandStream 缓冲区）
    driver.beginFrame(timestamp, frameN);

    // 创建/更新资源
    driver.updateVertexBuffer(vbh, bufferData);  // 命令1
    driver.updateTexture(texh, imageData);       // 命令2

    // 渲染
    driver.setRenderTarget(rth);                 // 命令3
    driver.draw(pipelineState, primitive);       // 命令4

    driver.endFrame(frameN);                      // 命令5

    // 3. 提交命令到渲染线程（非阻塞）
    driver.flush();

    // 4. 主线程立即返回，可以开始准备第 N+1 帧
    // （此时第 N 帧的命令正在渲染线程执行）
}
```

### 渲染线程视角

**文件位置**: `filament/src/details/Engine.cpp:757-843` (`FEngine::loop`) 和 `Engine.cpp:1398-1415` (`FEngine::execute`)

```cpp
// 渲染线程入口
int FEngine::loop() {
    // 1. 创建 Platform 和 Driver
    mPlatform = PlatformFactory::create(&mBackend);
    mDriver = mPlatform->createDriver(mSharedGLContext, ...);

    // 2. 设置线程名称和优先级
    JobSystem::setThreadName("FEngine::loop");
    JobSystem::setThreadPriority(JobSystem::Priority::DISPLAY);

    // 3. 通知主线程 Driver 已就绪
    mDriverBarrier.latch();

    // 4. 渲染循环
    while (true) {
        if (!execute()) {
            break;  // 收到退出请求
        }
    }

    // 5. 清理
    getDriverApi().terminate();
    return 0;
}

// 执行一批命令
bool FEngine::execute() {
    // 1. 等待命令（阻塞直到有命令或收到退出请求）
    auto const buffers = mCommandBufferQueue.waitForCommands();
    if (UTILS_UNLIKELY(buffers.empty())) {
        return false;  // 退出
    }

    // 2. 执行所有命令缓冲
    auto& driver = getDriverApi();
    for (auto& item : buffers) {
        if (UTILS_LIKELY(item.begin)) {
            driver.execute(item.begin);  // 遍历命令并执行
            mCommandBufferQueue.releaseBuffer(item);  // 释放空间
        }
    }

    return true;
}
```

---

## 内存管理和优化

### 环形缓冲区的实现原理

Filament 使用**单个环形缓冲区**来存储命令，而不是双缓冲。通过巧妙的内存映射技术实现高效的环形访问。

#### "硬环形缓冲"技术（mmap 两次映射）

**文件位置**: `filament/backend/src/CircularBuffer.cpp:74-100`

在支持 mmap 的系统上（Linux, macOS, Android, iOS），Filament 将同一块物理内存映射到连续的两个虚拟地址空间：

```cpp
// 1. 创建共享内存
int fd = ashmem_create_region("CircularBuffer", size);

// 2. 映射到虚拟地址空间两次
void* vaddr = mmap(reserve_vaddr, size, ...);           // [0, 2MB)
void* vaddr_shadow = mmap(vaddr + size, size, ...);     // [2MB, 4MB)
                                                         // 指向同一块物理内存！
```

**内存布局**：

```
虚拟地址空间（4MB）:
┌──────────────────────┬──────────────────────┐
│    vaddr (0~2MB)     │  vaddr_shadow (2~4MB)│
│   第一次映射         │    第二次映射         │
└──────────────────────┴──────────────────────┘
         ↓                      ↓
         └──────────┬───────────┘
                    ↓
物理内存（2MB）:
┌─────────────────────────────────┐
│    实际的物理页面                │
└─────────────────────────────────┘
```

**优势**：写指针可以越界而无需手动回绕！

```cpp
void* allocate(size_t s) {
    char* cur = mHead;
    mHead = cur + s;  // 即使超过 mData+size，进入 vaddr_shadow 区域
                       // 也没问题，因为指向同一物理内存！
    return cur;
}
```

#### Windows/Emscripten 的软环形缓冲

在不支持 mmap 的系统上（Windows, WebAssembly），使用两个连续的内存块模拟：

```cpp
// 分配两倍大小的内存
mData = malloc(size * 2);
// 在 getBuffer() 中手动处理回绕
```

### 命令批处理

```cpp
// ❌ 低效：每个命令立即 flush
for (int i = 0; i < 1000; i++) {
    driver.draw(state, primitive[i]);
    driver.flush();  // 1000次上下文切换！
}

// ✅ 高效：批量录制，一次 flush
for (int i = 0; i < 1000; i++) {
    driver.draw(state, primitive[i]);  // 录制到缓冲区
}
driver.flush();  // 一次提交
```

### 参数生命周期管理

某些参数（如 BufferDescriptor）包含大块数据，需要特殊处理：

```cpp
// BufferDescriptor 包含数据指针和回调
struct BufferDescriptor {
    void* buffer;
    size_t size;
    Callback callback;  // 释放回调
};

// 录制命令时
void CommandStream::queueCommand(
    &Driver::updateVertexBuffer,
    vbh,
    BufferDescriptor(data, size, [](void* buf, size_t, void*) {
        free(buf);  // 执行后释放
    })
) {
    // BufferDescriptor 被移动到命令缓冲区
    // 执行后自动调用 callback 释放内存
}
```

---

## 错误处理和调试

### 命令调试

在 Debug 模式下，Filament 可以记录每个命令：

```cpp
#if FILAMENT_DEBUG_COMMANDS
void CommandStream::queueCommand(...) {
    const char* methodName = getMethodName(method);
    DLOG(INFO) << "Queue: " << methodName;

    // 录制命令...
}

void CommandStream::execute() {
    const char* methodName = getMethodName(cmd->methodPtr);
    DLOG(INFO) << "Execute: " << methodName;

    // 执行命令...
}
#endif
```

**输出示例**:
```
Queue: createTexture
Queue: updateVertexBuffer
Queue: draw
Execute: createTexture
Execute: updateVertexBuffer
Execute: draw
```

### 同步点调试

```cpp
void Driver::finish() {
    auto start = std::chrono::high_resolution_clock::now();

    commandStream.finish();  // 等待完成

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    if (duration.count() > 16) {
        DLOG(WARNING) << "Finish took " << duration.count() << "ms (>16ms!)";
    }
}
```

---

## 性能考虑

### 命令录制开销

```cpp
// 录制一个 draw 命令的开销：
// 1. 函数调用: ~5ns
// 2. 参数拷贝: ~20ns (假设 64 字节参数)
// 3. 缓冲区分配: ~10ns (无锁分配)
// 总计: ~35ns

// 相比直接 OpenGL 调用 (可能 1-10μs)，开销可忽略
```

### 内存占用

```cpp
// 典型的命令缓冲区大小
constexpr size_t COMMAND_BUFFER_SIZE = 2 * 1024 * 1024;  // 2MB

// 一帧的命令数量估算
// 假设平均每个命令 100 字节
// 2MB / 100B = ~20,000 条命令
// 足够一帧的渲染（通常 < 5000 draw calls）
```

### 同步开销

```cpp
// flush() 的开销：
// 1. 获取锁: ~100ns
// 2. 原子操作: ~10ns
// 3. 条件变量通知: ~1μs
// 总计: ~1-2μs

// 一帧调用一次，开销完全可接受
```

---

## 服务线程和用户回调机制

### 为什么需要服务线程？

当 GPU 完成数据上传（如纹理、顶点缓冲）后，需要释放 CPU 端的内存。但这个释放操作**不能在渲染线程执行**，因为：

1. **用户回调可能很慢**，会阻塞渲染线程
2. **用户回调可能调用 Filament API**，导致死锁
3. **用户回调可能需要在特定线程执行**（如 Android 主线程）

因此，Filament 创建了**服务线程**专门处理用户回调。

### 服务线程的创建

**文件位置**: `filament/backend/src/Driver.cpp:50-76`

```cpp
DriverBase::DriverBase() noexcept {
    if constexpr (UTILS_HAS_THREADING) {
        // This thread services user callbacks
        mServiceThread = std::thread([this]() {
            do {
                // 等待回调任务
                std::unique_lock<std::mutex> lock(mServiceThreadLock);
                while (serviceThreadCallbackQueue.empty() && !mExitRequested) {
                    serviceThreadCondition.wait(lock);
                }
                if (mExitRequested) break;

                // 移动回调队列到本地
                auto callbacks(std::move(serviceThreadCallbackQueue));
                lock.unlock();

                // 执行所有回调（不持有锁）
                for (auto[handler, callback, user]: callbacks) {
                    handler->post(user, callback);
                }
            } while (true);
        });
    }
}
```

### BufferDescriptor 的回调机制

**文件位置**: `filament/backend/include/backend/BufferDescriptor.h`

BufferDescriptor 用于向 GPU 传递数据，包含一个回调函数用于释放内存：

```cpp
class BufferDescriptor {
public:
    using Callback = void(*)(void* buffer, size_t size, void* user);

    BufferDescriptor(void const* buffer, size_t size,
                     Callback callback = nullptr, void* user = nullptr)
        : buffer(const_cast<void*>(buffer)), size(size),
          mCallback(callback), mUser(user) {}

    ~BufferDescriptor() noexcept {
        // 析构时调用回调
        if (mCallback) {
            mCallback(buffer, size, mUser);
        }
    }

    void* buffer = nullptr;
    size_t size = 0;

private:
    Callback mCallback = nullptr;
    void* mUser = nullptr;
    CallbackHandler* mHandler = nullptr;  // 可选的自定义处理器
};
```

### 使用示例：异步纹理上传

```cpp
// 1. 从磁盘加载图片到 CPU 内存
unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);

// 2. 创建 BufferDescriptor，指定释放回调
Texture::PixelBufferDescriptor buffer(
    data,                               // CPU 数据
    size_t(w * h * 4),                 // 大小
    Texture::Format::RGBA,
    Texture::Type::UBYTE,
    (Texture::PixelBufferDescriptor::Callback) &stbi_image_free  // 回调
);

// 3. 上传纹理（录制命令）
texture->setImage(*engine, 0, std::move(buffer));

// 4. 主线程继续执行，无需等待
//    GPU 上传完成后，服务线程会调用 stbi_image_free(data)
```

### 回调执行流程

```
时间线 →

主线程:
  ├─> 分配内存: data = malloc(1MB)
  ├─> 创建 BufferDescriptor(data, callback=free)
  ├─> texture->setImage(buffer)
  │      ↓
  │   【命令录制到 CommandStream】
  │      ↓
  └─> 主线程继续...

渲染线程:
  │   【执行命令】
  ├─> OpenGLDriver::updateTextureImpl(...)
  ├─> glTexImage2D(..., data)  // GPU 开始上传
  │      ↓
  │   【GPU 上传完成】
  │      ↓
  ├─> BufferDescriptor 析构
  │      ↓
  └─> scheduleCallback(handler, callback, data)
         ↓
      【加入 mServiceThreadCallbackQueue】
         ↓

服务线程:
  ├─> wait() 被唤醒
  ├─> handler->post(data, callback)
  │      ↓
  │   【如果没有 handler，回调在主线程的 purge() 执行】
  │      ↓
  └─> callback(data)  // 执行 free(data)

✅ CPU 内存被释放！
```

### CallbackHandler: 自定义回调调度

**文件位置**: `filament/backend/include/backend/CallbackHandler.h`

用户可以实现自定义的 CallbackHandler，将回调调度到任意线程：

```cpp
class CallbackHandler {
public:
    using Callback = void(*)(void* user);

    // 调度回调到指定线程
    virtual void post(void* user, Callback callback) = 0;
};

// 示例：Android 主线程处理器
class AndroidLooperHandler : public CallbackHandler {
    void post(void* user, Callback callback) override {
        // 发送到 Android Looper
        androidLooper->postMessage([=]() {
            callback(user);
        });
    }
};
```

### 默认行为：主线程延迟执行

如果没有提供 CallbackHandler，回调会在主线程的下一次 `purge()` 调用时执行：

**文件位置**: `filament/backend/src/Driver.cpp:111-130`

```cpp
void DriverBase::scheduleCallback(CallbackHandler* handler,
                                   void* user,
                                   Callback callback) {
    if (handler && UTILS_HAS_THREADING) {
        // 有 handler：交给服务线程
        std::lock_guard lock(mServiceThreadLock);
        mServiceThreadCallbackQueue.emplace_back(handler, callback, user);
        mServiceThreadCondition.notify_one();
    } else {
        // 没有 handler：延迟到主线程 purge()
        std::lock_guard lock(mPurgeLock);
        mCallbacks.emplace_back(user, callback);
    }
}

void DriverBase::purge() noexcept {
    // 在主线程调用（通常每帧一次）
    decltype(mCallbacks) callbacks;
    std::unique_lock lock(mPurgeLock);
    std::swap(callbacks, mCallbacks);
    lock.unlock();

    // 执行所有待处理的回调
    for (auto& item : callbacks) {
        item.second(item.first);
    }
}
```

---

## 高级特性

### 延迟资源销毁

```cpp
void Driver::destroyTexture(Handle<HwTexture> th) {
    // 不立即销毁，延迟 N 帧
    commandStream.queueCommand(&Driver::destroyTextureImpl, th);

    // 资源销毁队列
    mDestroyQueue.push({th, currentFrameId + FRAME_DELAY});
}

void Driver::executeFrame() {
    // 每帧清理过期资源
    while (!mDestroyQueue.empty() &&
           mDestroyQueue.front().frameId <= currentFrameId) {
        auto [handle, frameId] = mDestroyQueue.front();
        mDestroyQueue.pop();

        // 现在可以安全销毁
        freeResource(handle);
    }
}
```

### 异步纹理上传

```cpp
void Driver::updateTexture(Handle<HwTexture> th, PixelBufferDescriptor&& data) {
    // 数据异步上传
    commandStream.queueCommand(
        &Driver::updateTextureImpl,
        th,
        std::move(data)  // 移动语义，避免拷贝
    );

    // 渲染线程执行时才真正上传到 GPU
}
```

---

## 与其他系统的集成

### 与 Vulkan 命令缓冲的关系

```
Filament CommandStream (CPU 端)
    │
    ├─> 录制 Driver 命令
    ├─> 录制 Driver 命令
    └─> flush()
         │
         ↓
    渲染线程执行
         │
         ↓
    VulkanDriver::drawImpl()
         │
         ├─> vkCmdBindPipeline()    ┐
         ├─> vkCmdBindVertexBuffers() ├─> Vulkan 命令缓冲 (GPU 端)
         ├─> vkCmdDrawIndexed()      ┘
         └─> vkQueueSubmit()
```

两层命令缓冲：
1. **Filament CommandStream**: 跨 API 抽象层
2. **Vulkan CommandBuffer**: Vulkan 特定的 GPU 命令

---

## 相关文档

- **[01-architecture-overview.md](01-architecture-overview.md)**: Backend 架构总览
- **[02-driver-abstraction.md](02-driver-abstraction.md)**: Driver 接口
- **[04-resource-handles.md](04-resource-handles.md)**: 资源句柄系统

**上层调用**:
- `../engine/07-render-loop.md`: 渲染循环中的命令流

**底层依赖**:
- `../graphics/09-gpu-optimization.md`: GPU 优化技术

---

## 总结

Filament 的命令流系统通过精心设计的**三线程架构**、**环形缓冲区**、**生产者-消费者模型**、**服务线程机制**，实现了主线程和渲染线程的完美分离，显著提升了 CPU 利用率和渲染性能。

### 核心架构

**1. 三线程模型**
- **主线程**: 录制渲染命令到 CircularBuffer，通过 CommandBufferQueue::flush() 提交
- **渲染线程**: 循环调用 waitForCommands() → execute() → releaseBuffer()
- **服务线程**: 处理用户回调（BufferDescriptor 释放等），解耦渲染线程

**2. 单个环形缓冲区（不是双缓冲！）**
- 使用 mmap 将同一物理内存映射到两个连续虚拟地址（"硬环形缓冲"）
- 写指针可以越界进入 shadow 区域，无需手动回绕
- 推荐大小：`3 * requiredSize`（当前写入 + 正在执行 + 缓冲）

**3. CommandBufferQueue 同步机制**
- 生产者（主线程）：flush() 提交命令，如果空间不足则阻塞等待
- 消费者（渲染线程）：waitForCommands() 获取命令，releaseBuffer() 归还空间
- 通过 mFreeSpace 追踪可用空间，使用条件变量同步

**4. BufferDescriptor 回调机制**
- 用户数据（如纹理）通过 BufferDescriptor 传递给 GPU
- 析构时触发回调，由服务线程调度执行
- 支持自定义 CallbackHandler，可将回调路由到任意线程

### 关键优势

- ✅ **主线程不阻塞**: 录制命令后立即返回，可以准备下一帧
- ✅ **CPU 多核并行**: 主线程、渲染线程、服务线程同时工作
- ✅ **内存高效**: 单个环形缓冲 + mmap 技巧，避免内存拷贝
- ✅ **命令批处理**: 一次 flush 提交多个命令，减少同步开销
- ✅ **用户回调安全**: 服务线程隔离，不阻塞渲染，支持自定义调度
- ✅ **跨平台统一**: 同一套 API 抽象 OpenGL/Vulkan/Metal

### 性能数据

- 命令录制开销：~35ns/命令（函数调用 5ns + 参数拷贝 20ns + 分配 10ns）
- flush() 同步开销：~1-2μs（锁 100ns + 原子操作 10ns + 条件变量 1μs）
- 典型缓冲区大小：2MB，可容纳 ~20,000 条命令
- 对比直接 OpenGL 调用（1-10μs），录制开销可忽略不计

### 与其他引擎的对比

| 特性 | Filament | Unity (Scriptable Render Pipeline) | Unreal Engine |
|------|----------|-----------------------------------|---------------|
| 命令缓冲 | 单个环形缓冲 + mmap | 多个命令缓冲链表 | Command List Pool |
| 线程模型 | 3线程（主+渲染+服务） | 多线程 Job System | Task Graph |
| 内存管理 | 环形缓冲自动回绕 | 手动池管理 | 线性分配器 |
| 用户回调 | 专用服务线程 | 主线程延迟执行 | 回调队列 |

这是 Filament Backend 高性能的关键设计之一！
