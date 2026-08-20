# Filament 内存管理优化

## 📖 概述

内存管理是性能优化的基石。Filament 应用通常需要处理海量的几何数据、纹理、材质等资源，这些资源分布在 CPU 内存和 GPU 内存中。有效的内存管理不仅能减少内存占用，还能显著提升运行时性能。

**核心挑战**：
- **内存容量限制**：移动设备仅有 2-8GB 内存，需精打细算
- **分配性能**：频繁的 new/delete 导致碎片化和性能下降
- **Cache 一致性**：数据布局影响 CPU Cache 命中率
- **GPU 内存传输**：CPU-GPU 数据传输是瓶颈

**优化目标**：
- 减少内存占用 30-50%
- 避免内存泄漏和野指针
- 提升内存分配性能 10-100 倍
- 优化数据布局，提升 Cache 命中率 20-40%
- 减少 GPU 内存传输

本文档将系统讲解 CPU 和 GPU 内存管理技术，包括对象池、内存分配器、智能指针、内存对齐、泄漏检测等实用技术。

---

## 1. CPU 内存管理

### 1.1 对象池（Object Pool）

**原理**：预分配一批对象，复用而非频繁创建/销毁，避免内存碎片和分配开销。

**完整实现**：

```cpp
// include/memory/ObjectPool.h
#pragma once

#include <vector>
#include <stack>
#include <memory>
#include <mutex>

namespace myproject::memory {

template<typename T>
class ObjectPool {
public:
    explicit ObjectPool(size_t initialSize = 100, size_t chunkSize = 50)
        : mChunkSize(chunkSize) {
        allocateChunk(initialSize);
    }
    
    ~ObjectPool() {
        // 清理所有分配的内存
        for (auto* chunk : mChunks) {
            ::operator delete(chunk);
        }
    }
    
    // 分配对象
    T* allocate() {
        std::lock_guard<std::mutex> lock(mMutex);
        
        if (mFreeList.empty()) {
            allocateChunk(mChunkSize);
        }
        
        T* obj = mFreeList.top();
        mFreeList.pop();
        
        // 调用构造函数
        new (obj) T();
        
        mAllocatedCount++;
        return obj;
    }
    
    // 释放对象
    void deallocate(T* obj) {
        std::lock_guard<std::mutex> lock(mMutex);
        
        if (!obj) return;
        
        // 调用析构函数
        obj->~T();
        
        mFreeList.push(obj);
        mAllocatedCount--;
    }
    
    // 统计信息
    struct Stats {
        size_t totalCapacity;
        size_t allocatedCount;
        size_t freeCount;
        size_t chunkCount;
    };
    
    Stats getStats() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return {
            mTotalCapacity,
            mAllocatedCount,
            mFreeList.size(),
            mChunks.size()
        };
    }
    
private:
    void allocateChunk(size_t count) {
        // 分配原始内存（不调用构造函数）
        void* memory = ::operator new(sizeof(T) * count);
        mChunks.push_back(memory);
        
        // 将所有对象加入 FreeList
        T* objects = static_cast<T*>(memory);
        for (size_t i = 0; i < count; ++i) {
            mFreeList.push(&objects[i]);
        }
        
        mTotalCapacity += count;
    }
    
    std::vector<void*> mChunks;
    std::stack<T*> mFreeList;
    size_t mChunkSize;
    size_t mTotalCapacity = 0;
    size_t mAllocatedCount = 0;
    mutable std::mutex mMutex;
};

}  // namespace myproject::memory
```

**使用示例**：

```cpp
#include <memory/ObjectPool.h>

struct Particle {
    math::float3 position;
    math::float3 velocity;
    float lifetime;
};

// 创建粒子对象池
ObjectPool<Particle> particlePool(1000);

// 分配粒子
Particle* p = particlePool.allocate();
p->position = {0, 0, 0};
p->velocity = {1, 1, 0};
p->lifetime = 2.0f;

// 释放粒子
particlePool.deallocate(p);

// 查看统计
auto stats = particlePool.getStats();
LOG_INFO("Pool: %zu/%zu allocated", stats.allocatedCount, stats.totalCapacity);
```

**性能对比**：

```cpp
// 基准测试
void benchmarkAllocation() {
    const int N = 100000;
    
    // 方法 1：std::vector + new/delete
    auto start1 = std::chrono::high_resolution_clock::now();
    std::vector<Particle*> particles1;
    for (int i = 0; i < N; ++i) {
        particles1.push_back(new Particle());
    }
    for (auto* p : particles1) {
        delete p;
    }
    auto end1 = std::chrono::high_resolution_clock::now();
    double time1 = std::chrono::duration<double, std::milli>(end1 - start1).count();
    
    // 方法 2：ObjectPool
    auto start2 = std::chrono::high_resolution_clock::now();
    ObjectPool<Particle> pool(N);
    std::vector<Particle*> particles2;
    for (int i = 0; i < N; ++i) {
        particles2.push_back(pool.allocate());
    }
    for (auto* p : particles2) {
        pool.deallocate(p);
    }
    auto end2 = std::chrono::high_resolution_clock::now();
    double time2 = std::chrono::duration<double, std::milli>(end2 - start2).count();
    
    LOG_INFO("new/delete: %.2f ms", time1);
    LOG_INFO("ObjectPool: %.2f ms", time2);
    LOG_INFO("Speedup: %.2fx", time1 / time2);
}

// 典型结果：
// new/delete: 45.32 ms
// ObjectPool: 1.23 ms
// Speedup: 36.85x
```

### 1.2 自定义内存分配器

**线性分配器（Linear Allocator）**：适用于每帧分配临时数据。

```cpp
// include/memory/LinearAllocator.h
#pragma once

#include <cstddef>
#include <cstdint>

namespace myproject::memory {

class LinearAllocator {
public:
    explicit LinearAllocator(size_t capacity)
        : mCapacity(capacity)
        , mUsed(0) {
        mMemory = static_cast<uint8_t*>(::operator new(capacity));
    }
    
    ~LinearAllocator() {
        ::operator delete(mMemory);
    }
    
    // 分配内存（无法单独释放）
    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
        // 对齐当前指针
        uintptr_t current = reinterpret_cast<uintptr_t>(mMemory + mUsed);
        uintptr_t aligned = (current + alignment - 1) & ~(alignment - 1);
        size_t padding = aligned - current;
        
        if (mUsed + padding + size > mCapacity) {
            return nullptr;  // 容量不足
        }
        
        void* ptr = reinterpret_cast<void*>(aligned);
        mUsed += padding + size;
        
        return ptr;
    }
    
    // 释放所有内存（重置）
    void reset() {
        mUsed = 0;
    }
    
    size_t getUsed() const { return mUsed; }
    size_t getCapacity() const { return mCapacity; }
    
private:
    uint8_t* mMemory;
    size_t mCapacity;
    size_t mUsed;
};

}  // namespace myproject::memory
```

**使用场景**：

```cpp
// 每帧临时数据分配
LinearAllocator frameAllocator(10 * 1024 * 1024);  // 10MB

void renderFrame() {
    // 分配临时缓冲
    float* tempVertices = static_cast<float*>(
        frameAllocator.allocate(10000 * sizeof(float))
    );
    
    // 使用 tempVertices...
    
    // 帧结束时重置
    frameAllocator.reset();
}
```

### 1.3 SmallVector（Small Buffer Optimization）

**原理**：小数组存储在栈上，避免堆分配；大数组才使用堆。

```cpp
// include/memory/SmallVector.h
#pragma once

#include <cstddef>
#include <utility>
#include <algorithm>

namespace myproject::memory {

template<typename T, size_t N>
class SmallVector {
public:
    SmallVector() : mSize(0), mCapacity(N), mData(mInlineStorage) {}
    
    ~SmallVector() {
        clear();
        if (mData != mInlineStorage) {
            ::operator delete(mData);
        }
    }
    
    void push_back(const T& value) {
        if (mSize >= mCapacity) {
            grow();
        }
        new (&mData[mSize++]) T(value);
    }
    
    void push_back(T&& value) {
        if (mSize >= mCapacity) {
            grow();
        }
        new (&mData[mSize++]) T(std::move(value));
    }
    
    T& operator[](size_t index) { return mData[index]; }
    const T& operator[](size_t index) const { return mData[index]; }
    
    size_t size() const { return mSize; }
    size_t capacity() const { return mCapacity; }
    
    void clear() {
        for (size_t i = 0; i < mSize; ++i) {
            mData[i].~T();
        }
        mSize = 0;
    }
    
private:
    void grow() {
        size_t newCapacity = mCapacity * 2;
        T* newData = static_cast<T*>(::operator new(sizeof(T) * newCapacity));
        
        // 移动现有元素
        for (size_t i = 0; i < mSize; ++i) {
            new (&newData[i]) T(std::move(mData[i]));
            mData[i].~T();
        }
        
        if (mData != mInlineStorage) {
            ::operator delete(mData);
        }
        
        mData = newData;
        mCapacity = newCapacity;
    }
    
    size_t mSize;
    size_t mCapacity;
    T* mData;
    alignas(T) uint8_t mInlineStorage[sizeof(T) * N];
};

}  // namespace myproject::memory
```

**性能优势**：

```cpp
// 小数组无堆分配
SmallVector<int, 8> vec;  // 内联存储 8 个 int
for (int i = 0; i < 8; ++i) {
    vec.push_back(i);  // 全部在栈上，极快
}

// 超过 8 个时自动扩展到堆
vec.push_back(9);  // 触发堆分配
```

---

## 2. GPU 内存管理

### 2.1 Buffer Pool（缓冲池）

GPU Buffer 创建开销大，使用 Buffer Pool 复用：

```cpp
// include/gpu/BufferPool.h
#pragma once

#include <filament/Engine.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <unordered_map>
#include <queue>

namespace myproject::gpu {

class BufferPool {
public:
    explicit BufferPool(filament::Engine* engine)
        : mEngine(engine) {}
    
    ~BufferPool() {
        // 销毁所有 Buffer
        for (auto& [size, buffers] : mVertexBuffers) {
            for (auto* buffer : buffers) {
                mEngine->destroy(buffer);
            }
        }
        for (auto& [size, buffers] : mIndexBuffers) {
            for (auto* buffer : buffers) {
                mEngine->destroy(buffer);
            }
        }
    }
    
    // 分配 Vertex Buffer
    filament::VertexBuffer* allocateVertexBuffer(size_t vertexCount, size_t vertexSize) {
        size_t size = vertexCount * vertexSize;
        
        auto& pool = mVertexBuffers[size];
        if (!pool.empty()) {
            auto* buffer = pool.front();
            pool.pop();
            return buffer;
        }
        
        // 创建新 Buffer
        auto* buffer = filament::VertexBuffer::Builder()
            .vertexCount(vertexCount)
            .bufferCount(1)
            .build(*mEngine);
        
        return buffer;
    }
    
    // 释放 Vertex Buffer
    void deallocateVertexBuffer(filament::VertexBuffer* buffer, size_t size) {
        mVertexBuffers[size].push(buffer);
    }
    
    // 清理未使用的 Buffer
    void trim() {
        // 保留最近使用的，销毁旧的
        // 实现省略...
    }
    
private:
    filament::Engine* mEngine;
    std::unordered_map<size_t, std::queue<filament::VertexBuffer*>> mVertexBuffers;
    std::unordered_map<size_t, std::queue<filament::IndexBuffer*>> mIndexBuffers;
};

}  // namespace myproject::gpu
```

### 2.2 Texture Atlas（纹理图集）

**原理**：将多个小纹理合并到一张大纹理，减少纹理切换和内存占用。

```cpp
// include/gpu/TextureAtlas.h
#pragma once

#include <filament/Engine.h>
#include <filament/Texture.h>
#include <vector>
#include <unordered_map>

namespace myproject::gpu {

class TextureAtlas {
public:
    struct Rect {
        uint32_t x, y, width, height;
    };
    
    explicit TextureAtlas(filament::Engine* engine, uint32_t width, uint32_t height)
        : mEngine(engine)
        , mWidth(width)
        , mHeight(height) {
        
        mTexture = filament::Texture::Builder()
            .width(width)
            .height(height)
            .format(filament::Texture::InternalFormat::RGBA8)
            .build(*mEngine);
    }
    
    ~TextureAtlas() {
        mEngine->destroy(mTexture);
    }
    
    // 添加纹理到图集
    Rect addTexture(const void* pixels, uint32_t width, uint32_t height) {
        // 简化实现：线性排列
        Rect rect = findFreeSpace(width, height);
        
        if (rect.width == 0) {
            LOG_ERROR("TextureAtlas full!");
            return {0, 0, 0, 0};
        }
        
        // 上传子区域
        filament::Texture::PixelBufferDescriptor buffer(
            pixels,
            width * height * 4,
            filament::Texture::Format::RGBA,
            filament::Texture::Type::UBYTE
        );
        
        mTexture->setImage(*mEngine, 0, std::move(buffer),
                          rect.x, rect.y, width, height);
        
        return rect;
    }
    
    filament::Texture* getTexture() const { return mTexture; }
    
    // 计算 UV 坐标
    math::float4 getUVRect(const Rect& rect) const {
        return {
            static_cast<float>(rect.x) / mWidth,
            static_cast<float>(rect.y) / mHeight,
            static_cast<float>(rect.width) / mWidth,
            static_cast<float>(rect.height) / mHeight
        };
    }
    
private:
    Rect findFreeSpace(uint32_t width, uint32_t height) {
        // 简化实现：Shelf Bin Packing
        // 实际应使用 Guillotine 或 MaxRects 算法
        
        if (mCurrentX + width > mWidth) {
            // 换行
            mCurrentX = 0;
            mCurrentY += mCurrentRowHeight;
            mCurrentRowHeight = 0;
        }
        
        if (mCurrentY + height > mHeight) {
            // 空间不足
            return {0, 0, 0, 0};
        }
        
        Rect rect = {mCurrentX, mCurrentY, width, height};
        mCurrentX += width;
        mCurrentRowHeight = std::max(mCurrentRowHeight, height);
        
        return rect;
    }
    
    filament::Engine* mEngine;
    filament::Texture* mTexture;
    uint32_t mWidth;
    uint32_t mHeight;
    
    uint32_t mCurrentX = 0;
    uint32_t mCurrentY = 0;
    uint32_t mCurrentRowHeight = 0;
};

}  // namespace myproject::gpu
```

**使用示例**：

```cpp
TextureAtlas atlas(engine, 2048, 2048);

// 添加多个小纹理
auto rect1 = atlas.addTexture(icon1Data, 64, 64);
auto rect2 = atlas.addTexture(icon2Data, 64, 64);
auto rect3 = atlas.addTexture(sprite1Data, 128, 128);

// 在 Shader 中使用
material->setParameter("atlasTexture", atlas.getTexture(), sampler);

auto uv1 = atlas.getUVRect(rect1);
// 传递 uv1 到 Shader...
```

**优势**：
- **减少纹理切换**：1000 个小图标 → 1 次绑定
- **节省内存**：减少 Padding 和 Mipmap 开销
- **提升性能**：DrawCall 减少 50-90%

### 2.3 GPU 内存流式加载

**分级 LOD 纹理**：

```cpp
class StreamingTexture {
public:
    enum class LOD {
        LOW = 0,     // 64x64
        MEDIUM = 1,  // 256x256
        HIGH = 2     // 1024x1024
    };
    
    StreamingTexture(Engine* engine, const std::string& basePath)
        : mEngine(engine)
        , mBasePath(basePath)
        , mCurrentLOD(LOD::LOW) {
        
        // 先加载低精度版本
        loadLOD(LOD::LOW);
    }
    
    void upgradeLOD() {
        if (mCurrentLOD == LOD::HIGH) return;
        
        LOD nextLOD = static_cast<LOD>(static_cast<int>(mCurrentLOD) + 1);
        
        // 异步加载更高精度版本
        asyncLoader->load(getLODPath(nextLOD), [this, nextLOD](void* data) {
            // 替换纹理
            mEngine->destroy(mTexture);
            mTexture = createTexture(data, getLODSize(nextLOD));
            mCurrentLOD = nextLOD;
        });
    }
    
private:
    std::string getLODPath(LOD lod) const {
        const char* suffixes[] = {"_low", "_med", "_high"};
        return mBasePath + suffixes[static_cast<int>(lod)] + ".png";
    }
    
    uint32_t getLODSize(LOD lod) const {
        const uint32_t sizes[] = {64, 256, 1024};
        return sizes[static_cast<int>(lod)];
    }
    
    Engine* mEngine;
    std::string mBasePath;
    Texture* mTexture;
    LOD mCurrentLOD;
};
```

---

## 3. 内存对齐和 Cache 优化

### 3.1 数据结构对齐

**问题**：未对齐的数据导致 Cache Line 跨越，降低性能。

```cpp
// ❌ 不好：未对齐
struct Particle {
    float x, y, z;        // 12 bytes
    float vx, vy, vz;     // 12 bytes
    float lifetime;       // 4 bytes
    bool active;          // 1 byte
    // Total: 29 bytes (跨越多个 Cache Line)
};

// ✅ 好：对齐到 32 bytes (Cache Line 的一半)
struct alignas(32) Particle {
    float x, y, z;        // 12 bytes
    float vx, vy, vz;     // 12 bytes
    float lifetime;       // 4 bytes
    bool active;          // 1 byte
    uint8_t padding[3];   // 3 bytes padding
    // Total: 32 bytes (恰好半个 Cache Line)
};
```

### 3.2 SoA vs AoS

**AoS（Array of Structures）**：

```cpp
struct Particle {
    math::float3 position;
    math::float3 velocity;
    float lifetime;
};

std::vector<Particle> particles(10000);

// 更新所有粒子位置
for (auto& p : particles) {
    p.position += p.velocity * dt;  // 跨越 velocity 字段，Cache 不友好
}
```

**SoA（Structure of Arrays）**：

```cpp
struct ParticleSystem {
    std::vector<math::float3> positions;
    std::vector<math::float3> velocities;
    std::vector<float> lifetimes;
};

ParticleSystem particles;
particles.positions.resize(10000);
particles.velocities.resize(10000);
particles.lifetimes.resize(10000);

// 更新所有粒子位置
for (size_t i = 0; i < particles.positions.size(); ++i) {
    particles.positions[i] += particles.velocities[i] * dt;  // 连续内存访问，Cache 友好
}
```

**性能对比**：

```cpp
// 基准测试
const int N = 1000000;

// AoS
auto start1 = now();
for (auto& p : aosParticles) {
    p.position += p.velocity * dt;
}
auto time1 = now() - start1;

// SoA
auto start2 = now();
for (size_t i = 0; i < N; ++i) {
    soaParticles.positions[i] += soaParticles.velocities[i] * dt;
}
auto time2 = now() - start2;

LOG_INFO("AoS: %.2f ms", time1);
LOG_INFO("SoA: %.2f ms", time2);
LOG_INFO("Speedup: %.2fx", time1 / time2);

// 典型结果：
// AoS: 12.34 ms
// SoA: 3.21 ms
// Speedup: 3.84x
```

---

## 4. 智能指针和 RAII

### 4.1 Filament 资源 RAII 封装

```cpp
// include/memory/ResourceHandle.h
#pragma once

#include <filament/Engine.h>
#include <filament/Texture.h>
#include <filament/VertexBuffer.h>

namespace myproject::memory {

template<typename T>
class ResourceHandle {
public:
    ResourceHandle(filament::Engine* engine, T* resource)
        : mEngine(engine), mResource(resource) {}
    
    ~ResourceHandle() {
        if (mResource) {
            mEngine->destroy(mResource);
        }
    }
    
    // 禁止拷贝
    ResourceHandle(const ResourceHandle&) = delete;
    ResourceHandle& operator=(const ResourceHandle&) = delete;
    
    // 允许移动
    ResourceHandle(ResourceHandle&& other) noexcept
        : mEngine(other.mEngine), mResource(other.mResource) {
        other.mResource = nullptr;
    }
    
    ResourceHandle& operator=(ResourceHandle&& other) noexcept {
        if (this != &other) {
            if (mResource) {
                mEngine->destroy(mResource);
            }
            mEngine = other.mEngine;
            mResource = other.mResource;
            other.mResource = nullptr;
        }
        return *this;
    }
    
    T* get() const { return mResource; }
    T* operator->() const { return mResource; }
    
private:
    filament::Engine* mEngine;
    T* mResource;
};

// 便利别名
using TextureHandle = ResourceHandle<filament::Texture>;
using VertexBufferHandle = ResourceHandle<filament::VertexBuffer>;

}  // namespace myproject::memory
```

**使用示例**：

```cpp
#include <memory/ResourceHandle.h>

void createTexture(Engine* engine) {
    // 自动管理生命周期
    TextureHandle texture(engine, Texture::Builder()
        .width(1024).height(1024)
        .format(Texture::InternalFormat::RGBA8)
        .build(*engine)
    );
    
    // 使用 texture...
    
    // 离开作用域时自动销毁，无需手动 destroy
}
```

### 4.2 引用计数资源

```cpp
// include/memory/RefCountedResource.h
#pragma once

#include <atomic>

namespace myproject::memory {

template<typename T>
class RefCountedResource {
public:
    explicit RefCountedResource(T* resource)
        : mResource(resource), mRefCount(1) {}
    
    ~RefCountedResource() {
        delete mResource;
    }
    
    void addRef() {
        mRefCount.fetch_add(1, std::memory_order_relaxed);
    }
    
    void release() {
        if (mRefCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }
    
    T* get() const { return mResource; }
    int32_t getRefCount() const { return mRefCount.load(); }
    
private:
    T* mResource;
    std::atomic<int32_t> mRefCount;
};

// 智能指针封装
template<typename T>
class ResourcePtr {
public:
    ResourcePtr() : mResource(nullptr) {}
    
    explicit ResourcePtr(T* resource)
        : mResource(new RefCountedResource<T>(resource)) {}
    
    ResourcePtr(const ResourcePtr& other) : mResource(other.mResource) {
        if (mResource) mResource->addRef();
    }
    
    ResourcePtr(ResourcePtr&& other) noexcept : mResource(other.mResource) {
        other.mResource = nullptr;
    }
    
    ~ResourcePtr() {
        if (mResource) mResource->release();
    }
    
    ResourcePtr& operator=(const ResourcePtr& other) {
        if (this != &other) {
            if (mResource) mResource->release();
            mResource = other.mResource;
            if (mResource) mResource->addRef();
        }
        return *this;
    }
    
    T* get() const { return mResource ? mResource->get() : nullptr; }
    T* operator->() const { return get(); }
    
private:
    RefCountedResource<T>* mResource;
};

}  // namespace myproject::memory
```

---

## 5. 内存泄漏检测

### 5.1 Valgrind（Linux/macOS）

```bash
# 编译时开启调试信息
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# 运行 Valgrind
valgrind --leak-check=full --show-leak-kinds=all ./myapp

# 输出示例：
# ==12345== 1,024 bytes in 1 blocks are definitely lost in loss record 42 of 100
# ==12345==    at 0x4C2E0EF: operator new(unsigned long) (vg_replace_malloc.c:334)
# ==12345==    by 0x400A2C: main (main.cpp:25)
```

### 5.2 AddressSanitizer（ASAN）

```cmake
# CMakeLists.txt
if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
```

```bash
# 编译
cmake -DENABLE_ASAN=ON ..
make

# 运行
./myapp

# 输出示例：
# =================================================================
# ==12345==ERROR: LeakSanitizer: detected memory leaks
#
# Direct leak of 1024 byte(s) in 1 object(s) allocated from:
#     #0 0x7f1234 in operator new(unsigned long)
#     #1 0x400a2c in main main.cpp:25
```

### 5.3 自定义内存追踪

```cpp
// include/memory/MemoryTracker.h
#pragma once

#include <unordered_map>
#include <mutex>
#include <cstddef>

namespace myproject::memory {

class MemoryTracker {
public:
    static MemoryTracker& getInstance() {
        static MemoryTracker instance;
        return instance;
    }
    
    void trackAllocation(void* ptr, size_t size, const char* file, int line) {
        std::lock_guard<std::mutex> lock(mMutex);
        mAllocations[ptr] = {size, file, line};
        mTotalAllocated += size;
    }
    
    void trackDeallocation(void* ptr) {
        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mAllocations.find(ptr);
        if (it != mAllocations.end()) {
            mTotalAllocated -= it->second.size;
            mAllocations.erase(it);
        }
    }
    
    void report() {
        std::lock_guard<std::mutex> lock(mMutex);
        
        LOG_INFO("=== Memory Leak Report ===");
        LOG_INFO("Total leaked: %zu bytes (%zu allocations)", 
                 mTotalAllocated, mAllocations.size());
        
        for (const auto& [ptr, info] : mAllocations) {
            LOG_INFO("  %p: %zu bytes at %s:%d", 
                     ptr, info.size, info.file, info.line);
        }
    }
    
private:
    struct AllocationInfo {
        size_t size;
        const char* file;
        int line;
    };
    
    std::unordered_map<void*, AllocationInfo> mAllocations;
    size_t mTotalAllocated = 0;
    std::mutex mMutex;
};

}  // namespace myproject::memory

// 重载全局 new/delete
#ifdef ENABLE_MEMORY_TRACKING
    void* operator new(size_t size, const char* file, int line) {
        void* ptr = ::operator new(size);
        MemoryTracker::getInstance().trackAllocation(ptr, size, file, line);
        return ptr;
    }
    
    void operator delete(void* ptr) noexcept {
        MemoryTracker::getInstance().trackDeallocation(ptr);
        ::operator delete(ptr);
    }
    
    #define new new(__FILE__, __LINE__)
#endif
```

---

## 6. 内存预算系统

```cpp
// include/memory/MemoryBudget.h
#pragma once

#include <atomic>
#include <string>
#include <unordered_map>

namespace myproject::memory {

class MemoryBudget {
public:
    void setBudget(const std::string& category, size_t bytes) {
        mBudgets[category] = bytes;
    }
    
    bool allocate(const std::string& category, size_t bytes) {
        auto& usage = mUsage[category];
        size_t current = usage.load();
        size_t budget = mBudgets[category];
        
        if (current + bytes > budget) {
            LOG_WARNING("Memory budget exceeded for %s: %zu / %zu",
                       category.c_str(), current + bytes, budget);
            return false;
        }
        
        usage.fetch_add(bytes);
        return true;
    }
    
    void deallocate(const std::string& category, size_t bytes) {
        mUsage[category].fetch_sub(bytes);
    }
    
    void report() const {
        LOG_INFO("=== Memory Budget Report ===");
        for (const auto& [category, budget] : mBudgets) {
            size_t usage = mUsage.at(category).load();
            float percent = (float)usage / budget * 100.0f;
            LOG_INFO("  %s: %zu / %zu bytes (%.1f%%)",
                    category.c_str(), usage, budget, percent);
        }
    }
    
private:
    std::unordered_map<std::string, size_t> mBudgets;
    std::unordered_map<std::string, std::atomic<size_t>> mUsage;
};

}  // namespace myproject::memory
```

**使用示例**：

```cpp
MemoryBudget budget;
budget.setBudget("Textures", 512 * 1024 * 1024);  // 512MB
budget.setBudget("Meshes", 256 * 1024 * 1024);    // 256MB
budget.setBudget("Audio", 128 * 1024 * 1024);     // 128MB

// 加载纹理
if (budget.allocate("Textures", textureSize)) {
    loadTexture(...);
} else {
    // 预算超限，驱逐 LRU 纹理
    evictOldestTexture();
}

// 定期报告
budget.report();
```

---

## 7. 实战案例：大型场景内存优化

### 7.1 问题描述

- **场景**：建筑可视化，10万+ 三角形，1000+ 纹理
- **问题**：内存占用 3GB，移动设备崩溃
- **目标**：降低到 1GB 以下

### 7.2 优化方案

**1. 纹理压缩**：

```cpp
// Before: RGBA8 (4 bytes/pixel)
Texture* texture = Texture::Builder()
    .width(2048).height(2048)
    .format(Texture::InternalFormat::RGBA8)  // 16MB
    .build(*engine);

// After: ASTC 4x4 (1 byte/pixel)
Texture* texture = Texture::Builder()
    .width(2048).height(2048)
    .format(Texture::InternalFormat::RGBA_ASTC_4x4)  // 4MB
    .build(*engine);

// 节省: 16MB - 4MB = 12MB per texture
// 1000 textures: 12GB saved!
```

**2. Texture Atlas**：

```cpp
// Before: 1000 个 64x64 纹理
// 每个纹理: 64x64x4 = 16KB
// 1000 个: 16MB

// After: 1 个 2048x2048 Atlas
// 2048x2048x4 = 16MB
// 但可容纳 (2048/64)^2 = 1024 个小纹理

// 节省: Mipmap 开销、Padding 等
```

**3. LOD 系统**：

```cpp
struct LODLevel {
    float distance;
    float triangleReduction;
};

LODLevel lods[] = {
    {0,   1.0f},   // 原始精度
    {50,  0.5f},   // 50% 三角形
    {100, 0.25f},  // 25% 三角形
    {200, 0.1f}    // 10% 三角形
};

// 根据距离选择 LOD
float distance = length(cameraPos - objectPos);
for (const auto& lod : lods) {
    if (distance < lod.distance) {
        renderMesh(mesh, lod.triangleReduction);
        break;
    }
}
```

**4. 流式加载**：

```cpp
// 只加载可见区域
StreamingManager streamMgr;
streamMgr.setMemoryBudget(1024 * 1024 * 1024);  // 1GB

void update(Camera* camera) {
    // 卸载远处的资源
    streamMgr.unloadDistant(camera->getPosition(), 500.0f);
    
    // 加载即将可见的资源
    streamMgr.loadNearby(camera->getPosition(), 100.0f);
}
```

### 7.3 优化结果

| 项目 | Before | After | 节省 |
|------|--------|-------|------|
| 纹理 | 2000 MB | 500 MB | 75% |
| 网格 | 800 MB | 400 MB | 50% |
| 其他 | 200 MB | 100 MB | 50% |
| **总计** | **3000 MB** | **1000 MB** | **67%** |

---

## 8. 最佳实践总结

1. **使用对象池**：避免频繁分配（粒子、临时对象）
2. **自定义分配器**：临时数据用 Linear Allocator
3. **SmallVector**：小数组避免堆分配
4. **Buffer Pool**：复用 GPU Buffer
5. **Texture Atlas**：合并小纹理
6. **SoA 布局**：提升 Cache 命中率
7. **智能指针**：RAII 管理资源生命周期
8. **内存追踪**：定期检测泄漏
9. **内存预算**：严格控制各类资源占用
10. **流式加载**：按需加载，及时卸载

---

## 9. 常见问题

### Q1: 对象池 vs std::pmr::memory_resource？

**A**: 
- **对象池**：简单、高效，适用于同类型对象
- **pmr**：C++17 标准，支持多态分配器，更灵活但复杂

### Q2: 如何检测 GPU 内存泄漏？

**A**: 
- **RenderDoc**: Capture Frame，查看 Resource 列表
- **Driver Profiler**: NVIDIA Nsight、AMD RGP
- **手动追踪**: 记录所有 create/destroy 调用

### Q3: SoA 是否总是更快？

**A**: 不一定
- **优势**: 顺序访问单一字段时
- **劣势**: 需要同时访问多个字段时（增加 Cache Miss）

---

## 10. 相关文档

- [debugging/08-memory-profiling.md](../debugging/08-memory-profiling.md)
- [optimization/01-texture-optimization.md](./01-texture-optimization.md)
- [optimization/05-mobile-optimization.md](./05-mobile-optimization.md)

---

## 11. 总结

内存管理是性能优化的基础，通过本文档介绍的技术：

1. **CPU 内存优化**：对象池、自定义分配器、SmallVector
2. **GPU 内存优化**：Buffer Pool、Texture Atlas、流式加载
3. **数据布局优化**：内存对齐、SoA vs AoS
4. **生命周期管理**：智能指针、RAII、引用计数
5. **泄漏检测**：Valgrind、ASAN、自定义追踪
6. **内存预算**：严格控制资源占用

通过合理应用这些技术，可实现：
- **内存占用减少 30-70%**
- **分配性能提升 10-100 倍**
- **Cache 命中率提升 20-40%**
- **避免内存泄漏和崩溃**

内存管理不仅是技术，更是一种纪律——在项目早期就建立良好的内存管理习惯，将受益终身。
