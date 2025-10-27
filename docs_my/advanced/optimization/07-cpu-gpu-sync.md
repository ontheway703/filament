# CPU-GPU 同步优化

## 📖 概述

CPU 和 GPU 的同步是渲染管线中常见的性能瓶颈。不当的同步会导致：
- **Pipeline Stall**：CPU 或 GPU 互相等待
- **帧率下降**：同步开销占用帧时间
- **延迟增加**：输入到显示的延迟变大

本文档深入讲解 CPU-GPU 同步机制、常见问题及优化方案。

**优化目标**:
- 减少 CPU-GPU 等待时间 50-80%
- 提升并行度，CPU 和 GPU 同时工作
- 降低帧延迟（Input Lag）
- 提高吞吐量，充分利用硬件

**核心概念**:
- **Fence**：CPU 等待 GPU 完成某个命令
- **Semaphore**：GPU 内部同步（Queue 间）
- **Event**：GPU 命令流中的标记点
- **Buffering**：使用多个缓冲区避免等待

---

## 1. CPU-GPU 并行架构

### 1.1 Pipeline 模型

```
Frame N-2:      CPU: Sim  |         |         |
                GPU:      | Render  |         |

Frame N-1:      CPU:      | Sim     |         |
                GPU:      |         | Render  |

Frame N:        CPU:      |         | Sim     |
                GPU:      |         |         | Render

Time:           ----0ms----16ms----32ms----48ms--->
```

**理想状态**：CPU 和 GPU 并行工作，无等待。

**问题场景**：

```
Bad Sync:
Frame N:        CPU: Sim  |  WAIT   | Upload  |
                GPU:      | Render  |         | Idle

CPU 等待 GPU 渲染完成才上传数据 → 浪费时间！
```

### 1.2 同步点识别

**常见同步点**：

| 操作 | 同步类型 | 延迟 | 可优化？ |
|------|---------|------|----------|
| `glReadPixels` | CPU 等待 GPU | 高（数 ms） | ✅ 异步读取 |
| `glMapBuffer` (读) | CPU 等待 GPU | 高 | ✅ 多缓冲 |
| `glFinish` | CPU 等待 GPU 完成所有命令 | 极高 | ✅ 避免使用 |
| `glFlush` | 强制提交命令 | 低 | ✅ 适当使用 |
| `vkQueueSubmit` (Fence) | CPU 等待 GPU | 中 | ✅ 异步查询 |
| SwapBuffers (VSync) | CPU 等待显示器 | 16.7ms (60Hz) | ⚠️ 必要 |

---

## 2. 多缓冲机制

### 2.1 双缓冲 (Double Buffering)

**原理**：使用两个缓冲区，CPU 写入一个，GPU 读取另一个。

```cpp
// DoubleBuffer.h
template<typename T>
class DoubleBuffer {
public:
    DoubleBuffer() {
        mBuffers[0] = new T();
        mBuffers[1] = new T();
        mWriteIndex = 0;
    }

    ~DoubleBuffer() {
        delete mBuffers[0];
        delete mBuffers[1];
    }

    // CPU 写入
    T* getWriteBuffer() {
        return mBuffers[mWriteIndex];
    }

    // GPU 读取
    T* getReadBuffer() const {
        return mBuffers[1 - mWriteIndex];
    }

    // 交换缓冲区
    void swap() {
        mWriteIndex = 1 - mWriteIndex;
    }

private:
    T* mBuffers[2];
    int mWriteIndex;
};

// 使用示例
DoubleBuffer<UniformData> uniformBuffer;

void onFrame() {
    // CPU 更新数据
    UniformData* data = uniformBuffer.getWriteBuffer();
    data->modelMatrix = getModelMatrix();
    data->viewProjection = getViewProjection();

    // GPU 使用上一帧数据
    renderer->setUniforms(uniformBuffer.getReadBuffer());

    // 交换
    uniformBuffer.swap();
}
```

**时间线**：

```
Frame N-1:  CPU: Write[0] | Read[1]
            GPU: Read[1]  |

Frame N:    CPU: Write[1] | Read[0]
            GPU: Read[0]  |

无等待！
```

### 2.2 三缓冲 (Triple Buffering)

**优势**：进一步减少等待，适用于 VSync 环境。

```cpp
// TripleBuffer.h
template<typename T>
class TripleBuffer {
public:
    TripleBuffer() {
        for (int i = 0; i < 3; ++i) {
            mBuffers[i] = new T();
        }
        mWriteIndex = 0;
        mReadIndex = 1;
        mSwapIndex = 2;
    }

    ~TripleBuffer() {
        for (int i = 0; i < 3; ++i) {
            delete mBuffers[i];
        }
    }

    // CPU 写入
    T* getWriteBuffer() {
        return mBuffers[mWriteIndex];
    }

    // GPU 读取
    T* getReadBuffer() const {
        return mBuffers[mReadIndex];
    }

    // 交换缓冲区
    void swap() {
        // CPU 完成写入，准备下一帧
        int newWrite = mSwapIndex;
        mSwapIndex = mWriteIndex;
        mWriteIndex = newWrite;
    }

    // Present 后调用
    void present() {
        // GPU 开始读取新的缓冲区
        int newRead = mSwapIndex;
        mSwapIndex = mReadIndex;
        mReadIndex = newRead;
    }

private:
    T* mBuffers[3];
    int mWriteIndex;   // CPU 正在写入
    int mReadIndex;    // GPU 正在读取
    int mSwapIndex;    // 准备交换
};
```

**对比**：

```
Double Buffering (60 FPS + VSync):
  Frame N:   CPU: 10ms work + 6.7ms WAIT  = 16.7ms
  Frame N+1: CPU: 10ms work + 6.7ms WAIT  = 16.7ms

Triple Buffering (60 FPS + VSync):
  Frame N:   CPU: 10ms work (no wait)
  Frame N+1: CPU: 10ms work (no wait)

Triple Buffering 允许 CPU 提前工作，减少延迟！
```

### 2.3 Filament 中的多缓冲

```cpp
#include <filament/Engine.h>
#include <filament/Renderer.h>

using namespace filament;

// Filament 默认使用 Triple Buffering
Engine* engine = Engine::create(Engine::Backend::VULKAN);

// 配置缓冲模式
Engine::Config config;
config.commandBufferSizeMB = 2;  // 每个缓冲区大小
engine = Engine::create(Engine::Backend::VULKAN, nullptr, &config);

// Uniform Buffer 多缓冲
struct PerFrameUniforms {
    math::mat4f viewProjection;
    math::float3 cameraPosition;
    float time;
};

class UniformBufferPool {
public:
    UniformBufferPool(Engine& engine, size_t bufferCount = 3) 
        : mEngine(engine), mBufferCount(bufferCount) {
        for (size_t i = 0; i < bufferCount; ++i) {
            auto buffer = UniformBuffer::Builder()
                .size(sizeof(PerFrameUniforms))
                .build(mEngine);
            mBuffers.push_back(buffer);
        }
    }

    UniformBuffer* getCurrentBuffer() {
        return mBuffers[mCurrentIndex];
    }

    void advance() {
        mCurrentIndex = (mCurrentIndex + 1) % mBufferCount;
    }

private:
    Engine& mEngine;
    std::vector<UniformBuffer*> mBuffers;
    size_t mBufferCount;
    size_t mCurrentIndex = 0;
};

// 使用
UniformBufferPool uniformPool(*engine, 3);

void onFrame() {
    auto* uniforms = uniformPool.getCurrentBuffer();

    // 更新 Uniform
    PerFrameUniforms data;
    data.viewProjection = getViewProjection();
    data.cameraPosition = getCameraPosition();
    data.time = getTime();

    uniforms->setBuffer(engine, &data, sizeof(data));

    // 渲染
    renderer->render(view);

    // 切换到下一个缓冲区
    uniformPool.advance();
}
```

---

## 3. Fence 同步

### 3.1 Fence 基础

**Fence** 是 CPU 等待 GPU 完成某个命令的同步原语。

```cpp
#include <filament/Fence.h>

using namespace filament;

// 创建 Fence
Fence* fence = Fence::create(*engine);

// 插入 Fence 到命令流
renderer->render(view);
fence->insert(*engine);

// CPU 等待 GPU
auto status = fence->wait(Fence::Mode::FLUSH, 1000000000);  // 1秒超时
if (status == Fence::FenceStatus::TIMEOUT_EXPIRED) {
    // 超时处理
} else if (status == Fence::FenceStatus::CONDITION_SATISFIED) {
    // GPU 完成
}

// 销毁
engine->destroy(fence);
```

### 3.2 非阻塞查询

**问题**：`fence->wait()` 会阻塞 CPU。

**方案**：异步查询，CPU 继续工作。

```cpp
class AsyncFenceWaiter {
public:
    AsyncFenceWaiter(Engine& engine) : mEngine(engine) {}

    // 提交工作并创建 Fence
    void submitWork(std::function<void()> work) {
        Work item;
        item.fence = Fence::create(mEngine);
        item.callback = work;

        // 执行渲染命令
        // ...

        // 插入 Fence
        item.fence->insert(mEngine);

        mPendingWork.push_back(std::move(item));
    }

    // 每帧检查
    void update() {
        auto it = mPendingWork.begin();
        while (it != mPendingWork.end()) {
            // 非阻塞检查
            auto status = it->fence->wait(Fence::Mode::DONT_FLUSH, 0);

            if (status == Fence::FenceStatus::CONDITION_SATISFIED) {
                // GPU 完成，执行回调
                it->callback();

                // 清理
                mEngine.destroy(it->fence);
                it = mPendingWork.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    struct Work {
        Fence* fence;
        std::function<void()> callback;
    };

    Engine& mEngine;
    std::vector<Work> mPendingWork;
};

// 使用
AsyncFenceWaiter waiter(*engine);

void onFrame() {
    // 提交渲染工作
    renderer->render(view);

    waiter.submitWork([]() {
        printf("Frame completed on GPU!\n");
        // 读取结果、上传新数据等
    });

    // 检查之前的工作
    waiter.update();
}
```

### 3.3 Fence Pool

**优化**：复用 Fence 对象，减少创建销毁开销。

```cpp
class FencePool {
public:
    FencePool(Engine& engine, size_t poolSize = 8) 
        : mEngine(engine) {
        for (size_t i = 0; i < poolSize; ++i) {
            Fence* fence = Fence::create(mEngine);
            mAvailable.push(fence);
        }
    }

    ~FencePool() {
        while (!mAvailable.empty()) {
            mEngine.destroy(mAvailable.front());
            mAvailable.pop();
        }
        for (auto* fence : mInUse) {
            mEngine.destroy(fence);
        }
    }

    Fence* acquire() {
        Fence* fence = nullptr;

        if (!mAvailable.empty()) {
            fence = mAvailable.front();
            mAvailable.pop();
        } else {
            // Pool 空了，创建新的
            fence = Fence::create(mEngine);
        }

        mInUse.insert(fence);
        return fence;
    }

    void release(Fence* fence) {
        mInUse.erase(fence);
        mAvailable.push(fence);
    }

private:
    Engine& mEngine;
    std::queue<Fence*> mAvailable;
    std::unordered_set<Fence*> mInUse;
};

// 使用
FencePool fencePool(*engine, 8);

void onFrame() {
    Fence* fence = fencePool.acquire();

    renderer->render(view);
    fence->insert(*engine);

    // 异步检查
    // ...

    // 完成后归还
    fencePool.release(fence);
}
```

---

## 4. 异步数据上传

### 4.1 Staging Buffer

**问题**：直接上传大数据会阻塞。

**方案**：使用 Staging Buffer（CPU 可见）+ Transfer Queue（异步传输）。

```cpp
// Vulkan 示例
class AsyncUploader {
public:
    AsyncUploader(VkDevice device, VkPhysicalDevice physicalDevice) 
        : mDevice(device) {
        // 创建 Staging Buffer (CPU 可见)
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = 256 * 1024 * 1024;  // 256 MB
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(mDevice, &bufferInfo, nullptr, &mStagingBuffer);

        // 分配 CPU 可见内存
        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(mDevice, mStagingBuffer, &memReq);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        vkAllocateMemory(mDevice, &allocInfo, nullptr, &mStagingMemory);
        vkBindBufferMemory(mDevice, mStagingBuffer, mStagingMemory, 0);
    }

    void uploadTexture(VkImage dstImage, const void* data, size_t size) {
        // Map staging buffer
        void* mapped;
        vkMapMemory(mDevice, mStagingMemory, 0, size, 0, &mapped);
        memcpy(mapped, data, size);
        vkUnmapMemory(mDevice, mStagingMemory);

        // Record copy command
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(mTransferCmdBuffer, &beginInfo);

        // Transition image layout
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.image = dstImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(mTransferCmdBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Copy buffer to image
        VkBufferImageCopy region = {};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(mTransferCmdBuffer, mStagingBuffer, dstImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        vkEndCommandBuffer(mTransferCmdBuffer);

        // Submit to transfer queue (async)
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &mTransferCmdBuffer;

        vkQueueSubmit(mTransferQueue, 1, &submitInfo, VK_NULL_HANDLE);

        // NO WAIT! CPU continues working
    }

private:
    VkDevice mDevice;
    VkBuffer mStagingBuffer;
    VkDeviceMemory mStagingMemory;
    VkCommandBuffer mTransferCmdBuffer;
    VkQueue mTransferQueue;

    uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, 
                            VkMemoryPropertyFlags properties);
};
```

### 4.2 Filament 异步上传

```cpp
#include <filament/Texture.h>
#include <filament/Stream.h>

using namespace filament;

// 方法 1：使用 Stream（推荐）
void uploadTextureAsync(Engine& engine, Texture* texture, const void* data, size_t size) {
    // 创建 Stream
    Stream* stream = Stream::Builder()
        .width(width)
        .height(height)
        .build(engine);

    // 异步上传
    stream->setDimensions(width, height);
    stream->readPixels(0, 0, width, height,
        Texture::PixelBufferDescriptor(
            data, size,
            Texture::Format::RGBA, Texture::Type::UBYTE,
            [](void* buffer, size_t size, void* user) {
                // 上传完成回调
                printf("Upload complete!\n");
            }
        )
    );

    // 设置到纹理
    texture->setExternalStream(engine, stream);

    // NO WAIT! CPU continues
}

// 方法 2：BufferObject（适用于大量数据）
class BufferUploadQueue {
public:
    BufferUploadQueue(Engine& engine) : mEngine(engine) {}

    void uploadBuffer(VertexBuffer* vb, const void* data, size_t size) {
        // 创建 BufferObject
        BufferObject* bo = BufferObject::Builder()
            .size(size)
            .build(mEngine);

        // 设置数据（异步）
        bo->setBuffer(mEngine, 
            BufferObject::BufferDescriptor(data, size, [](void* buffer, size_t size, void* user) {
                // 上传完成
            })
        );

        // 绑定到 VertexBuffer
        vb->setBufferObjectAt(mEngine, 0, bo);

        mPendingBuffers.push_back(bo);
    }

    void cleanup() {
        for (auto* bo : mPendingBuffers) {
            mEngine.destroy(bo);
        }
        mPendingBuffers.clear();
    }

private:
    Engine& mEngine;
    std::vector<BufferObject*> mPendingBuffers;
};
```

---

## 5. Pipeline Stall 分析

### 5.1 常见 Stall 场景

**Scenario 1: ReadPixels Stall**

```cpp
// ❌ 不好：立即读取像素（强制等待）
renderer->render(view);
uint8_t pixels[width * height * 4];
renderer->readPixels(0, 0, width, height, pixels);
// CPU 阻塞直到 GPU 渲染完成！

// ✅ 好：异步读取
class AsyncPixelReader {
public:
    void requestReadPixels(Renderer* renderer, int x, int y, int w, int h) {
        Request req;
        req.fence = Fence::create(*engine);
        req.x = x; req.y = y; req.w = w; req.h = h;
        req.data = new uint8_t[w * h * 4];

        renderer->readPixels(x, y, w, h, req.data);
        req.fence->insert(*engine);

        mPending.push_back(std::move(req));
    }

    void update() {
        auto it = mPending.begin();
        while (it != mPending.end()) {
            if (it->fence->wait(Fence::Mode::DONT_FLUSH, 0) == 
                Fence::FenceStatus::CONDITION_SATISFIED) {
                // 数据可用
                processPixels(it->data, it->w * it->h * 4);

                delete[] it->data;
                engine->destroy(it->fence);
                it = mPending.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    struct Request {
        Fence* fence;
        int x, y, w, h;
        uint8_t* data;
    };
    std::vector<Request> mPending;
};
```

**Scenario 2: Buffer Map Stall**

```cpp
// ❌ 不好：Map GPU 正在使用的缓冲区
vkMapMemory(device, bufferMemory, 0, size, 0, &mapped);
// 如果 GPU 正在读取，CPU 阻塞！

// ✅ 好：使用多个缓冲区
class MappedBufferRing {
public:
    MappedBufferRing(VkDevice device, size_t bufferSize, int count = 3) {
        for (int i = 0; i < count; ++i) {
            VkBuffer buffer;
            VkDeviceMemory memory;
            createBuffer(device, bufferSize, &buffer, &memory);
            
            void* mapped;
            vkMapMemory(device, memory, 0, bufferSize, 0, &mapped);

            mBuffers.push_back({buffer, memory, mapped});
        }
    }

    void* getCurrentMapped() {
        return mBuffers[mCurrentIndex].mapped;
    }

    VkBuffer getCurrentBuffer() {
        return mBuffers[mCurrentIndex].buffer;
    }

    void advance() {
        mCurrentIndex = (mCurrentIndex + 1) % mBuffers.size();
    }

private:
    struct BufferData {
        VkBuffer buffer;
        VkDeviceMemory memory;
        void* mapped;
    };

    std::vector<BufferData> mBuffers;
    int mCurrentIndex = 0;
};
```

### 5.2 使用 GPU Profiler 定位 Stall

```cpp
// RenderDoc / Nsight Graphics 标记
void renderFrameWithMarkers(Renderer* renderer, View* view) {
    // 插入调试标记
    renderer->beginDebugMarker("Shadow Pass");
    renderShadows(view);
    renderer->endDebugMarker();

    renderer->beginDebugMarker("Main Pass");
    renderer->render(view);
    renderer->endDebugMarker();

    renderer->beginDebugMarker("Post Processing");
    renderPostFX();
    renderer->endDebugMarker();

    // 在 RenderDoc 中查看：
    // - 每个 Pass 的 GPU 时间
    // - CPU-GPU 同步点（红色标记）
    // - Pipeline Stall（黄色标记）
}
```

---

## 6. Async Compute

### 6.1 Compute Queue 并行

**原理**：在 Graphics Queue 渲染时，并行运行 Compute Shader。

```cpp
// Vulkan Async Compute
class AsyncComputeExample {
public:
    void renderFrame() {
        // === Graphics Queue ===
        VkCommandBuffer graphicsCmd = beginGraphicsCommands();

        // 主渲染
        vkCmdBeginRenderPass(graphicsCmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(graphicsCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        vkCmdDraw(graphicsCmd, vertexCount, 1, 0, 0);
        vkCmdEndRenderPass(graphicsCmd);

        vkEndCommandBuffer(graphicsCmd);

        // === Compute Queue (Parallel) ===
        VkCommandBuffer computeCmd = beginComputeCommands();

        // 粒子更新
        vkCmdBindPipeline(computeCmd, VK_PIPELINE_BIND_POINT_COMPUTE, particlePipeline);
        vkCmdDispatch(computeCmd, particleCount / 256, 1, 1);

        vkEndCommandBuffer(computeCmd);

        // === Submit ===
        // Compute Queue 先提交（开始计算）
        VkSubmitInfo computeSubmit = {};
        computeSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        computeSubmit.commandBufferCount = 1;
        computeSubmit.pCommandBuffers = &computeCmd;
        computeSubmit.signalSemaphoreCount = 1;
        computeSubmit.pSignalSemaphores = &computeFinishedSemaphore;

        vkQueueSubmit(computeQueue, 1, &computeSubmit, VK_NULL_HANDLE);

        // Graphics Queue 等待 Compute（在 Vertex Input 阶段）
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        VkSubmitInfo graphicsSubmit = {};
        graphicsSubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        graphicsSubmit.commandBufferCount = 1;
        graphicsSubmit.pCommandBuffers = &graphicsCmd;
        graphicsSubmit.waitSemaphoreCount = 1;
        graphicsSubmit.pWaitSemaphores = &computeFinishedSemaphore;
        graphicsSubmit.pWaitDstStageMask = &waitStage;

        vkQueueSubmit(graphicsQueue, 1, &graphicsSubmit, renderFinishedFence);
    }

private:
    VkQueue graphicsQueue;
    VkQueue computeQueue;
    VkSemaphore computeFinishedSemaphore;
    VkFence renderFinishedFence;
};
```

**性能对比**：

```
Sequential:
  Graphics: 12 ms
  Compute:   4 ms
  Total:    16 ms

Async Compute (Overlap):
  Graphics: 12 ms
  Compute:   4 ms (overlap 4ms with graphics)
  Total:    12 ms (25% faster!)
```

### 6.2 Filament Async Compute（未来支持）

```cpp
// 概念示例（Filament 计划支持）
#include <filament/ComputePass.h>

ComputePass* particleUpdate = ComputePass::Builder()
    .computeShader("particle_update.comp")
    .build(*engine);

void onFrame() {
    // 提交到 Compute Queue（异步）
    engine->executeComputeAsync(particleUpdate);

    // 主渲染（并行）
    renderer->render(view);

    // 引擎内部处理同步
}
```

---

## 7. 延迟优化

### 7.1 Input Lag 分析

**Input Lag 来源**：

```
User Input → CPU Processing → GPU Rendering → Display
   ↓             ↓                ↓              ↓
   0ms          +5ms            +16ms          +32ms (60Hz)

Total Lag: 32-48ms (2-3 frames)
```

**优化方案**：

```cpp
// 1. 减少缓冲区数量（延迟 vs 吞吐量权衡）
// Triple Buffering: 2-3 frames lag
// Double Buffering: 1-2 frames lag

// 2. Low Latency Mode
class LowLatencyRenderer {
public:
    void enableLowLatency(bool enable) {
        if (enable) {
            // 使用 Double Buffering
            engine->setFrameSkipCount(0);

            // 禁用 VSync（可选，增加撕裂）
            swapChain->setVSync(false);

            // 减少预渲染帧数
            // Vulkan:
            VkPresentInfoKHR presentInfo = {};
            // ...
            // DX12:
            // swapChain->SetMaximumFrameLatency(1);
        }
    }

    void renderLowLatency() {
        // 尽可能晚地读取输入
        readInput();

        // 立即提交
        renderer->render(view);
        renderer->endFrame();

        // 立即 Present
        swapChain->present();
    }
};
```

### 7.2 测量延迟

```cpp
class LatencyMeasurement {
public:
    void measureLatency() {
        // 1. 记录输入时间
        auto inputTime = std::chrono::high_resolution_clock::now();
        processInput();

        // 2. 渲染（包含特殊标记）
        renderer->render(view);
        drawLatencyMarker();  // 屏幕角落绘制白点

        // 3. 使用高速相机拍摄屏幕
        // 4. 计算从按键到白点出现的时间
        //    → 这就是真实 Input Lag

        // 软件估算（不精确）
        auto renderTime = std::chrono::high_resolution_clock::now();
        auto lag = std::chrono::duration<double, std::milli>(renderTime - inputTime).count();
        printf("Software latency estimate: %.2f ms\n", lag);
    }

private:
    void drawLatencyMarker() {
        // 绘制一个小白点到屏幕角落
        // 用于高速相机测量
    }
};
```

---

## 8. 实战案例：优化场景加载

### 8.1 问题描述

- **场景**：加载大型场景（100+ 模型，2GB 数据）
- **问题**：加载时卡顿 3-5 秒，帧率归零
- **原因**：同步上传数据，阻塞渲染

### 8.2 优化前

```cpp
void loadScene(const std::vector<Model>& models) {
    for (const auto& model : models) {
        // 创建 Vertex Buffer（同步）
        VertexBuffer* vb = VertexBuffer::Builder()
            .vertexCount(model.vertexCount)
            .bufferCount(1)
            .attribute(VertexAttribute::POSITION, 0, 
                       VertexBuffer::AttributeType::FLOAT3)
            .build(*engine);

        // 上传数据（阻塞！）
        vb->setBufferAt(*engine, 0,
            VertexBuffer::BufferDescriptor(
                model.vertexData, model.vertexDataSize
            )
        );

        // 创建纹理（同步）
        Texture* texture = Texture::Builder()
            .width(model.textureWidth)
            .height(model.textureHeight)
            .levels(1)
            .format(Texture::InternalFormat::RGBA8)
            .build(*engine);

        // 上传纹理（阻塞！）
        texture->setImage(*engine, 0,
            Texture::PixelBufferDescriptor(
                model.textureData, model.textureDataSize,
                Texture::Format::RGBA, Texture::Type::UBYTE
            )
        );

        mRenderables.push_back({vb, texture});
    }

    // 总时间：3-5 秒，帧率 = 0
}
```

### 8.3 优化后

```cpp
class AsyncSceneLoader {
public:
    AsyncSceneLoader(Engine& engine) : mEngine(engine) {}

    void loadSceneAsync(const std::vector<Model>& models) {
        // 每帧加载一部分
        mModelsToLoad = models;
        mCurrentModel = 0;
        mLoading = true;
    }

    void update() {
        if (!mLoading) return;

        auto frameStart = std::chrono::high_resolution_clock::now();
        const double MAX_FRAME_TIME_MS = 8.0;  // 保持 60 FPS

        while (mCurrentModel < mModelsToLoad.size()) {
            const auto& model = mModelsToLoad[mCurrentModel];

            // 创建 Vertex Buffer
            VertexBuffer* vb = VertexBuffer::Builder()
                .vertexCount(model.vertexCount)
                .bufferCount(1)
                .attribute(VertexAttribute::POSITION, 0, 
                           VertexBuffer::AttributeType::FLOAT3)
                .build(mEngine);

            // 异步上传
            vb->setBufferAt(mEngine, 0,
                VertexBuffer::BufferDescriptor(
                    model.vertexData, model.vertexDataSize,
                    [](void* buffer, size_t size, void* user) {
                        // 上传完成回调
                    }
                )
            );

            // 创建纹理（异步）
            Texture* texture = Texture::Builder()
                .width(model.textureWidth)
                .height(model.textureHeight)
                .levels(1)
                .format(Texture::InternalFormat::RGBA8)
                .build(mEngine);

            texture->setImage(mEngine, 0,
                Texture::PixelBufferDescriptor(
                    model.textureData, model.textureDataSize,
                    Texture::Format::RGBA, Texture::Type::UBYTE,
                    [](void* buffer, size_t size, void* user) {
                        // 上传完成回调
                    }
                )
            );

            mRenderables.push_back({vb, texture});
            mCurrentModel++;

            // 检查时间，避免超过预算
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration<double, std::milli>(now - frameStart).count();
            if (elapsed > MAX_FRAME_TIME_MS) {
                // 下一帧继续
                break;
            }
        }

        if (mCurrentModel >= mModelsToLoad.size()) {
            mLoading = false;
            printf("Scene loading complete!\n");
        }
    }

    bool isLoading() const { return mLoading; }
    float getProgress() const {
        return (float)mCurrentModel / (float)mModelsToLoad.size();
    }

private:
    Engine& mEngine;
    std::vector<Model> mModelsToLoad;
    size_t mCurrentModel = 0;
    bool mLoading = false;
    std::vector<Renderable> mRenderables;
};

// 使用
AsyncSceneLoader loader(*engine);
loader.loadSceneAsync(models);

void onFrame() {
    // 继续渲染已有内容
    renderer->render(view);

    // 增量加载
    loader.update();

    // 显示加载进度
    if (loader.isLoading()) {
        drawLoadingBar(loader.getProgress());
    }
}
```

### 8.4 优化结果

| 指标 | Before | After | 改善 |
|------|--------|-------|------|
| 加载时间 | 3-5 秒 | 分散到 60 帧 | - |
| 加载期间最低帧率 | 0 FPS | 55-60 FPS | ∞ |
| 用户体验 | 卡死 | 流畅 + 进度条 | 显著 |

---

## 9. 最佳实践总结

1. **使用多缓冲**：Triple Buffering（吞吐量）或 Double Buffering（低延迟）
2. **避免阻塞调用**：`glReadPixels`、`glMapBuffer`（读）、`glFinish`
3. **异步上传**：使用 Staging Buffer、Stream、BufferDescriptor callback
4. **Fence 优化**：非阻塞查询、Fence Pool、异步回调
5. **Pipeline 并行**：Async Compute、Transfer Queue
6. **增量加载**：分帧上传数据，保持帧率
7. **性能分析**：使用 RenderDoc / Nsight 定位 Stall
8. **延迟优化**：减少缓冲区数量、Late Input Sampling

---

## 10. 常见问题

### Q1: Triple Buffering 会增加延迟吗？

**A**: 是的。Triple Buffering 增加 1 帧延迟，但提升吞吐量。适合：
- 单机游戏（延迟不敏感）
- 高刷新率显示器（延迟绝对值小）

Double Buffering 适合：
- 竞技游戏（低延迟优先）
- VR（延迟敏感）

### Q2: 如何判断是 CPU 还是 GPU bound？

**A**:
```cpp
// 方法：降低分辨率
renderer->setViewport(view, {0, 0, 1920/2, 1080/2});  // 降低 50%

// 帧率显著提升 → GPU bound
// 帧率不变 → CPU bound
```

### Q3: Fence 和 Semaphore 的区别？

**A**:
- **Fence**：CPU 等待 GPU（跨 CPU-GPU）
- **Semaphore**：GPU 等待 GPU（Queue 间同步，如 Compute → Graphics）

---

## 11. 相关文档

- [optimization/03-drawcall-reduction.md](./03-drawcall-reduction.md)
- [optimization/06-shader-optimization.md](./06-shader-optimization.md)
- [architecture/03-pipeline-architecture.md](../architecture/03-pipeline-architecture.md)

---

## 12. 总结

CPU-GPU 同步优化关键技术：

1. **多缓冲机制**：Triple/Double Buffering
2. **异步操作**：数据上传、Fence 查询
3. **避免 Pipeline Stall**：非阻塞调用、Staging Buffer
4. **并行计算**：Async Compute、Transfer Queue
5. **延迟优化**：减少缓冲区、Late Input Sampling

通过这些技术，可实现：
- **同步等待减少 50-80%**
- **吞吐量提升 20-50%**
- **Input Lag 降低 1-2 帧**
- **加载时保持 60 FPS**

CPU-GPU 同步优化需要平衡延迟、吞吐量和复杂度，根据应用场景选择合适方案。
