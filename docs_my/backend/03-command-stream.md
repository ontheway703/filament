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

### Filament 的解决方案：双线程架构

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

CommandStream 使用循环缓冲区存储命令和参数：

```cpp
class CircularBuffer {
public:
    // 分配空间
    void* allocate(size_t size, size_t alignment = 8);

    // 提交已写入的数据
    void* getBuffer() const;

    // 重置缓冲区
    void reset();

private:
    uint8_t* mBuffer;      // 缓冲区指针
    size_t mSize;          // 总大小
    size_t mHead;          // 写入位置
    size_t mTail;          // 读取位置
};
```

**内存布局**:
```
循环缓冲区 (例如 2MB)
┌────────────────────────────────────────────────────────────┐
│ Command1 │ Args1 │ Command2 │ Args2 │ ... │ CommandN │ ArgsN │
└────────────────────────────────────────────────────────────┘
 ↑                                                           ↑
mTail (读取位置)                                          mHead (写入位置)

当 mHead 追上 mTail 时：
- 选项1: 阻塞等待渲染线程消费
- 选项2: 扩展缓冲区（Filament 的做法）
```

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

## 线程同步机制

### 同步原语

```cpp
class CommandStream {
private:
    std::mutex mLock;                    // 互斥锁
    std::condition_variable mCondition;  // 条件变量
    std::atomic<bool> mHasCommands;      // 是否有命令
    std::atomic<bool> mExitRequested;    // 退出标志
};
```

### flush() - 提交命令

```cpp
void CommandStream::flush() {
    std::lock_guard<std::mutex> lock(mLock);

    // 标记有命令待执行
    mHasCommands.store(true, std::memory_order_release);

    // 通知渲染线程
    mCondition.notify_one();
}
```

### wait() - 等待命令

```cpp
void CommandStream::wait() {
    std::unique_lock<std::mutex> lock(mLock);

    // 等待直到有命令或收到退出请求
    mCondition.wait(lock, [this] {
        return mHasCommands.load(std::memory_order_acquire) ||
               mExitRequested.load(std::memory_order_acquire);
    });
}
```

### finish() - 等待完成

```cpp
void CommandStream::finish() {
    // 1. 提交当前命令
    flush();

    // 2. 等待渲染线程执行完成
    std::unique_lock<std::mutex> lock(mLock);
    mCondition.wait(lock, [this] {
        return !mHasCommands.load(std::memory_order_acquire);
    });
}
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

```cpp
void renderThreadLoop() {
    while (!exitRequested) {
        // 1. 等待命令
        commandStream.wait();

        if (exitRequested) break;

        // 2. 执行命令缓冲区中的所有命令
        commandStream.execute();

        // 具体执行：
        // OpenGLDriver::beginFrameImpl(...)
        // OpenGLDriver::updateVertexBufferImpl(...)
        // OpenGLDriver::updateTextureImpl(...)
        // OpenGLDriver::setRenderTargetImpl(...)
        // OpenGLDriver::drawImpl(...)
        // OpenGLDriver::endFrameImpl(...)

        // 3. 标记命令执行完成
        commandStream.notifyComplete();
    }
}
```

---

## 内存管理和优化

### 双缓冲机制

Filament 使用双缓冲避免主线程和渲染线程冲突：

```cpp
class CommandStreamDispatcher {
private:
    CircularBuffer mBuffers[2];  // 双缓冲
    int mWriteBuffer = 0;         // 当前写入的缓冲
    int mReadBuffer = 1;          // 当前读取的缓冲
};

void flush() {
    // 交换缓冲区
    std::swap(mWriteBuffer, mReadBuffer);

    // 通知渲染线程读取 mReadBuffer
    notifyRenderThread();
}
```

**优势**:
- 主线程写入缓冲A时，渲染线程读取缓冲B
- 无需等待，完全并行

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

CommandStream 通过**双线程架构**、**命令缓冲**、**异步执行**，实现了主线程和渲染线程的完美分离，显著提升了 CPU 利用率和渲染性能。这是 Filament Backend 高性能的关键设计之一。

**核心优势**:
- ✅ 主线程不阻塞，可立即准备下一帧
- ✅ CPU 多核并行，充分利用硬件
- ✅ 命令批处理，减少同步开销
- ✅ 跨平台统一，简化上层代码
