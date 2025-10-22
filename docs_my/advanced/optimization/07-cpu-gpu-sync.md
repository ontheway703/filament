# CPU-GPU 同步优化

## 📖 概述

CPU 和 GPU 的同步是常见的性能瓶颈。本文档介绍如何最小化同步开销。

**优化目标**:
- 减少 CPU-GPU 等待
- 提升并行度
- 降低延迟
- 提高吞吐量

---

## 1. 多缓冲

```cpp
// TripleBuffering.h
class TripleBuffer {
public:
    void* getCurrentBuffer() {
        return mBuffers[mCurrentIndex];
    }

    void swapBuffers() {
        mCurrentIndex = (mCurrentIndex + 1) % 3;
    }

private:
    void* mBuffers[3];
    int mCurrentIndex = 0;
};
```

---

## 2. 异步数据上传

```cpp
// 使用 Buffer Streaming
void uploadDataAsync(const void* data, size_t size) {
    // Map buffer
    void* mapped = mapBuffer();
    
    // Copy data
    memcpy(mapped, data, size);
    
    // Unmap (no sync)
    unmapBuffer();
}
```

---

## 3. Fence 同步

```cpp
// FenceSync.h
class FenceSync {
public:
    void insertFence() {
        mFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }

    bool isSignaled() {
        GLint status;
        glGetSynciv(mFence, GL_SYNC_STATUS, sizeof(GLint), nullptr, &status);
        return status == GL_SIGNALED;
    }

private:
    GLsync mFence;
};
```

---

## 4. 总结

CPU-GPU 同步优化关键点:
1. 使用多缓冲（双缓冲/三缓冲）
2. 避免 glReadPixels 等阻塞调用
3. 异步数据上传
4. 使用 Fence 和 Event
5. Pipeline 并行
