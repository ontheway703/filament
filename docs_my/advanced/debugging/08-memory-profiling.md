# 内存性能分析

## 📖 概述

内存是图形应用的关键资源,不当的内存使用会导致性能下降、崩溃甚至系统不稳定。本文档介绍如何分析和优化 Filament 应用的内存使用,包括 CPU 内存和 GPU 内存。

**本文涵盖**:
- 内存使用分析
- 内存泄漏检测
- GPU 内存管理
- 内存优化策略
- 移动端内存限制

**目标**:
- 识别内存泄漏
- 优化内存占用
- 避免内存碎片化
- 合理管理 GPU 资源

---

## 1. 内存分析工具

### 1.1 内存跟踪器

```cpp
// MemoryTracker.h
#pragma once

#include <atomic>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>

/**
 * 内存跟踪器
 *
 * 跟踪 CPU 和 GPU 内存分配
 */
class MemoryTracker {
public:
    /**
     * 内存类型
     */
    enum class MemoryType {
        CPU,            // CPU 内存
        GPU_BUFFER,     // GPU 缓冲区
        GPU_TEXTURE,    // GPU 纹理
        GPU_RENDERTARGET  // GPU 渲染目标
    };

    /**
     * 内存分配记录
     */
    struct AllocationRecord {
        void* address;
        size_t size;
        MemoryType type;
        std::string tag;
        std::string stackTrace;  // 调用栈
        double timestamp;        // 分配时间
    };

    /**
     * 内存统计
     */
    struct MemoryStats {
        size_t cpuMemory = 0;
        size_t gpuBufferMemory = 0;
        size_t gpuTextureMemory = 0;
        size_t gpuRenderTargetMemory = 0;

        size_t totalAllocations = 0;
        size_t totalDeallocations = 0;

        size_t getTotalGPUMemory() const {
            return gpuBufferMemory + gpuTextureMemory + gpuRenderTargetMemory;
        }

        size_t getTotalMemory() const {
            return cpuMemory + getTotalGPUMemory();
        }
    };

public:
    /**
     * 获取单例
     */
    static MemoryTracker& getInstance();

    /**
     * 记录分配
     */
    void recordAllocation(void* address, size_t size, MemoryType type, const char* tag);

    /**
     * 记录释放
     */
    void recordDeallocation(void* address, MemoryType type);

    /**
     * 获取统计
     */
    MemoryStats getStats() const;

    /**
     * 生成报告
     */
    std::string generateReport() const;

    /**
     * 检测内存泄漏
     */
    std::vector<AllocationRecord> detectLeaks() const;

    /**
     * 导出分配记录
     */
    void exportAllocations(const std::string& filename) const;

    /**
     * 重置统计
     */
    void reset();

private:
    MemoryTracker() = default;
    ~MemoryTracker() = default;
    MemoryTracker(const MemoryTracker&) = delete;
    MemoryTracker& operator=(const MemoryTracker&) = delete;

    mutable std::mutex mMutex;
    std::unordered_map<void*, AllocationRecord> mAllocations;

    std::atomic<size_t> mTotalCPUMemory{0};
    std::atomic<size_t> mTotalGPUBufferMemory{0};
    std::atomic<size_t> mTotalGPUTextureMemory{0};
    std::atomic<size_t> mTotalGPURenderTargetMemory{0};

    std::atomic<size_t> mTotalAllocations{0};
    std::atomic<size_t> mTotalDeallocations{0};
};
```

```cpp
// MemoryTracker.cpp
#include "MemoryTracker.h"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>

MemoryTracker& MemoryTracker::getInstance() {
    static MemoryTracker instance;
    return instance;
}

void MemoryTracker::recordAllocation(void* address, size_t size, MemoryType type, const char* tag) {
    if (address == nullptr) return;

    std::lock_guard<std::mutex> lock(mMutex);

    AllocationRecord record;
    record.address = address;
    record.size = size;
    record.type = type;
    record.tag = tag ? tag : "Unknown";

    // 记录时间戳
    auto now = std::chrono::system_clock::now();
    record.timestamp = std::chrono::duration<double>(now.time_since_epoch()).count();

    // TODO: 捕获调用栈 (可以使用 libunwind 或平台特定 API)
    record.stackTrace = "Stack trace not implemented";

    mAllocations[address] = record;

    // 更新统计
    switch (type) {
        case MemoryType::CPU:
            mTotalCPUMemory += size;
            break;
        case MemoryType::GPU_BUFFER:
            mTotalGPUBufferMemory += size;
            break;
        case MemoryType::GPU_TEXTURE:
            mTotalGPUTextureMemory += size;
            break;
        case MemoryType::GPU_RENDERTARGET:
            mTotalGPURenderTargetMemory += size;
            break;
    }

    mTotalAllocations++;
}

void MemoryTracker::recordDeallocation(void* address, MemoryType type) {
    if (address == nullptr) return;

    std::lock_guard<std::mutex> lock(mMutex);

    auto it = mAllocations.find(address);
    if (it != mAllocations.end()) {
        size_t size = it->second.size;

        // 更新统计
        switch (type) {
            case MemoryType::CPU:
                mTotalCPUMemory -= size;
                break;
            case MemoryType::GPU_BUFFER:
                mTotalGPUBufferMemory -= size;
                break;
            case MemoryType::GPU_TEXTURE:
                mTotalGPUTextureMemory -= size;
                break;
            case MemoryType::GPU_RENDERTARGET:
                mTotalGPURenderTargetMemory -= size;
                break;
        }

        mAllocations.erase(it);
        mTotalDeallocations++;
    }
}

MemoryTracker::MemoryStats MemoryTracker::getStats() const {
    MemoryStats stats;
    stats.cpuMemory = mTotalCPUMemory.load();
    stats.gpuBufferMemory = mTotalGPUBufferMemory.load();
    stats.gpuTextureMemory = mTotalGPUTextureMemory.load();
    stats.gpuRenderTargetMemory = mTotalGPURenderTargetMemory.load();
    stats.totalAllocations = mTotalAllocations.load();
    stats.totalDeallocations = mTotalDeallocations.load();
    return stats;
}

std::string MemoryTracker::generateReport() const {
    std::ostringstream report;
    auto stats = getStats();

    report << "=== Memory Report ===\n\n";

    report << "CPU Memory:\n";
    report << "  Total: " << (stats.cpuMemory / 1024.0 / 1024.0) << " MB\n\n";

    report << "GPU Memory:\n";
    report << "  Buffers: " << (stats.gpuBufferMemory / 1024.0 / 1024.0) << " MB\n";
    report << "  Textures: " << (stats.gpuTextureMemory / 1024.0 / 1024.0) << " MB\n";
    report << "  Render Targets: " << (stats.gpuRenderTargetMemory / 1024.0 / 1024.0) << " MB\n";
    report << "  Total GPU: " << (stats.getTotalGPUMemory() / 1024.0 / 1024.0) << " MB\n\n";

    report << "Total Memory: " << (stats.getTotalMemory() / 1024.0 / 1024.0) << " MB\n\n";

    report << "Allocations:\n";
    report << "  Total: " << stats.totalAllocations << "\n";
    report << "  Active: " << (stats.totalAllocations - stats.totalDeallocations) << "\n";
    report << "  Deallocated: " << stats.totalDeallocations << "\n";

    return report.str();
}

std::vector<MemoryTracker::AllocationRecord> MemoryTracker::detectLeaks() const {
    std::lock_guard<std::mutex> lock(mMutex);

    std::vector<AllocationRecord> leaks;

    // 所有未释放的分配都是潜在的泄漏
    for (const auto& [address, record] : mAllocations) {
        leaks.push_back(record);
    }

    // 按大小排序
    std::sort(leaks.begin(), leaks.end(), [](const AllocationRecord& a, const AllocationRecord& b) {
        return a.size > b.size;
    });

    return leaks;
}

void MemoryTracker::exportAllocations(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(mMutex);

    std::ofstream file(filename);
    if (!file.is_open()) return;

    file << "Address,Size,Type,Tag,Timestamp\n";

    for (const auto& [address, record] : mAllocations) {
        const char* typeStr = "Unknown";
        switch (record.type) {
            case MemoryType::CPU: typeStr = "CPU"; break;
            case MemoryType::GPU_BUFFER: typeStr = "GPU_Buffer"; break;
            case MemoryType::GPU_TEXTURE: typeStr = "GPU_Texture"; break;
            case MemoryType::GPU_RENDERTARGET: typeStr = "GPU_RenderTarget"; break;
        }

        file << address << ","
             << record.size << ","
             << typeStr << ","
             << record.tag << ","
             << std::fixed << std::setprecision(3) << record.timestamp << "\n";
    }

    file.close();
}

void MemoryTracker::reset() {
    std::lock_guard<std::mutex> lock(mMutex);
    mAllocations.clear();
    mTotalCPUMemory = 0;
    mTotalGPUBufferMemory = 0;
    mTotalGPUTextureMemory = 0;
    mTotalGPURenderTargetMemory = 0;
    mTotalAllocations = 0;
    mTotalDeallocations = 0;
}
```

### 1.2 Filament 资源跟踪

```cpp
// FilamentResourceTracker.h
#pragma once

#include <filament/Engine.h>
#include <filament/Texture.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <string>
#include <unordered_map>

using namespace filament;

/**
 * Filament 资源跟踪器
 *
 * 跟踪 Filament 创建的所有资源
 */
class FilamentResourceTracker {
public:
    struct ResourceInfo {
        std::string type;
        std::string name;
        size_t estimatedSize;
        double creationTime;
    };

public:
    static FilamentResourceTracker& getInstance();

    /**
     * 跟踪纹理
     */
    void trackTexture(Texture* texture, const char* name);
    void untrackTexture(Texture* texture);

    /**
     * 跟踪顶点缓冲区
     */
    void trackVertexBuffer(VertexBuffer* vb, const char* name, size_t size);
    void untrackVertexBuffer(VertexBuffer* vb);

    /**
     * 跟踪索引缓冲区
     */
    void trackIndexBuffer(IndexBuffer* ib, const char* name, size_t size);
    void untrackIndexBuffer(IndexBuffer* ib);

    /**
     * 获取资源列表
     */
    std::vector<ResourceInfo> getResources() const;

    /**
     * 生成报告
     */
    std::string generateReport() const;

    /**
     * 计算纹理大小
     */
    static size_t calculateTextureSize(Texture* texture);

private:
    FilamentResourceTracker() = default;
    ~FilamentResourceTracker() = default;

    std::unordered_map<void*, ResourceInfo> mResources;
    mutable std::mutex mMutex;
};
```

```cpp
// FilamentResourceTracker.cpp
#include "FilamentResourceTracker.h"
#include <chrono>
#include <sstream>
#include <algorithm>

FilamentResourceTracker& FilamentResourceTracker::getInstance() {
    static FilamentResourceTracker instance;
    return instance;
}

void FilamentResourceTracker::trackTexture(Texture* texture, const char* name) {
    std::lock_guard<std::mutex> lock(mMutex);

    ResourceInfo info;
    info.type = "Texture";
    info.name = name ? name : "Unnamed";
    info.estimatedSize = calculateTextureSize(texture);
    info.creationTime = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    mResources[texture] = info;

    // 同时记录到 MemoryTracker
    MemoryTracker::getInstance().recordAllocation(
        texture,
        info.estimatedSize,
        MemoryTracker::MemoryType::GPU_TEXTURE,
        info.name.c_str()
    );
}

void FilamentResourceTracker::untrackTexture(Texture* texture) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto it = mResources.find(texture);
    if (it != mResources.end()) {
        MemoryTracker::getInstance().recordDeallocation(
            texture,
            MemoryTracker::MemoryType::GPU_TEXTURE
        );
        mResources.erase(it);
    }
}

void FilamentResourceTracker::trackVertexBuffer(VertexBuffer* vb, const char* name, size_t size) {
    std::lock_guard<std::mutex> lock(mMutex);

    ResourceInfo info;
    info.type = "VertexBuffer";
    info.name = name ? name : "Unnamed";
    info.estimatedSize = size;
    info.creationTime = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    mResources[vb] = info;

    MemoryTracker::getInstance().recordAllocation(
        vb, size, MemoryTracker::MemoryType::GPU_BUFFER, info.name.c_str()
    );
}

void FilamentResourceTracker::untrackVertexBuffer(VertexBuffer* vb) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto it = mResources.find(vb);
    if (it != mResources.end()) {
        MemoryTracker::getInstance().recordDeallocation(
            vb, MemoryTracker::MemoryType::GPU_BUFFER
        );
        mResources.erase(it);
    }
}

void FilamentResourceTracker::trackIndexBuffer(IndexBuffer* ib, const char* name, size_t size) {
    std::lock_guard<std::mutex> lock(mMutex);

    ResourceInfo info;
    info.type = "IndexBuffer";
    info.name = name ? name : "Unnamed";
    info.estimatedSize = size;
    info.creationTime = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    mResources[ib] = info;

    MemoryTracker::getInstance().recordAllocation(
        ib, size, MemoryTracker::MemoryType::GPU_BUFFER, info.name.c_str()
    );
}

void FilamentResourceTracker::untrackIndexBuffer(IndexBuffer* ib) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto it = mResources.find(ib);
    if (it != mResources.end()) {
        MemoryTracker::getInstance().recordDeallocation(
            ib, MemoryTracker::MemoryType::GPU_BUFFER
        );
        mResources.erase(it);
    }
}

std::vector<FilamentResourceTracker::ResourceInfo> FilamentResourceTracker::getResources() const {
    std::lock_guard<std::mutex> lock(mMutex);

    std::vector<ResourceInfo> resources;
    resources.reserve(mResources.size());

    for (const auto& [ptr, info] : mResources) {
        resources.push_back(info);
    }

    // 按大小排序
    std::sort(resources.begin(), resources.end(), [](const ResourceInfo& a, const ResourceInfo& b) {
        return a.estimatedSize > b.estimatedSize;
    });

    return resources;
}

std::string FilamentResourceTracker::generateReport() const {
    std::lock_guard<std::mutex> lock(mMutex);

    std::ostringstream report;
    report << "=== Filament Resource Report ===\n\n";

    // 按类型统计
    std::unordered_map<std::string, size_t> typeCounts;
    std::unordered_map<std::string, size_t> typeSizes;

    for (const auto& [ptr, info] : mResources) {
        typeCounts[info.type]++;
        typeSizes[info.type] += info.estimatedSize;
    }

    report << "Resource Summary:\n";
    for (const auto& [type, count] : typeCounts) {
        size_t size = typeSizes[type];
        report << "  " << type << ": " << count << " (" << (size / 1024.0 / 1024.0) << " MB)\n";
    }

    report << "\nTop 10 Largest Resources:\n";
    auto resources = getResources();
    size_t count = std::min<size_t>(10, resources.size());

    for (size_t i = 0; i < count; ++i) {
        const auto& info = resources[i];
        report << "  " << (i + 1) << ". " << info.type << " \"" << info.name << "\" - "
               << (info.estimatedSize / 1024.0 / 1024.0) << " MB\n";
    }

    return report.str();
}

size_t FilamentResourceTracker::calculateTextureSize(Texture* texture) {
    // 估算纹理大小
    uint32_t width = texture->getWidth();
    uint32_t height = texture->getHeight();
    uint32_t levels = texture->getLevels();

    // 简化计算,假设 RGBA8 格式
    size_t baseSize = width * height * 4;

    // 包含 mipmap
    size_t totalSize = 0;
    for (uint32_t i = 0; i < levels; ++i) {
        totalSize += baseSize / (1 << (i * 2));
    }

    return totalSize;
}
```

---

## 2. 内存泄漏检测

### 2.1 RAII 资源管理

```cpp
// ResourceManager.h
#pragma once

#include <filament/Engine.h>
#include <memory>

using namespace filament;

/**
 * RAII 资源管理器
 *
 * 自动管理 Filament 资源生命周期
 */
template<typename T>
class ResourceHandle {
public:
    ResourceHandle(Engine* engine, T* resource)
        : mEngine(engine), mResource(resource) {
        if (mResource) {
            trackResource();
        }
    }

    ~ResourceHandle() {
        if (mResource && mEngine) {
            untrackResource();
            destroyResource();
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
            if (mResource && mEngine) {
                destroyResource();
            }
            mEngine = other.mEngine;
            mResource = other.mResource;
            other.mResource = nullptr;
        }
        return *this;
    }

    T* get() { return mResource; }
    const T* get() const { return mResource; }

    T* operator->() { return mResource; }
    const T* operator->() const { return mResource; }

private:
    void trackResource();
    void untrackResource();
    void destroyResource();

    Engine* mEngine;
    T* mResource;
};

// 特化实现
using TextureHandle = ResourceHandle<Texture>;
using VertexBufferHandle = ResourceHandle<VertexBuffer>;
using IndexBufferHandle = ResourceHandle<IndexBuffer>;
using MaterialInstanceHandle = ResourceHandle<MaterialInstance>;
```

```cpp
// ResourceManager.cpp
#include "ResourceManager.h"
#include "FilamentResourceTracker.h"

// Texture 特化
template<>
void ResourceHandle<Texture>::trackResource() {
    FilamentResourceTracker::getInstance().trackTexture(mResource, "AutoManaged");
}

template<>
void ResourceHandle<Texture>::untrackResource() {
    FilamentResourceTracker::getInstance().untrackTexture(mResource);
}

template<>
void ResourceHandle<Texture>::destroyResource() {
    mEngine->destroy(mResource);
}

// VertexBuffer 特化
template<>
void ResourceHandle<VertexBuffer>::trackResource() {
    // 需要知道大小才能准确跟踪
    FilamentResourceTracker::getInstance().trackVertexBuffer(mResource, "AutoManaged", 0);
}

template<>
void ResourceHandle<VertexBuffer>::untrackResource() {
    FilamentResourceTracker::getInstance().untrackVertexBuffer(mResource);
}

template<>
void ResourceHandle<VertexBuffer>::destroyResource() {
    mEngine->destroy(mResource);
}

// 使用示例
void exampleUsage(Engine* engine) {
    // 自动管理纹理
    {
        auto texture = TextureHandle(engine,
            Texture::Builder()
                .width(1024)
                .height(1024)
                .format(Texture::InternalFormat::RGBA8)
                .build(*engine)
        );

        // 使用纹理
        // ...

    } // texture 在作用域结束时自动销毁
}
```

### 2.2 泄漏检测工具集成

```cpp
// LeakDetector.h
#pragma once

#include "MemoryTracker.h"
#include <vector>
#include <string>

/**
 * 内存泄漏检测器
 */
class LeakDetector {
public:
    /**
     * 开始泄漏检测会话
     */
    static void beginSession();

    /**
     * 结束泄漏检测会话并报告
     */
    static std::string endSession();

    /**
     * 检查并报告当前泄漏
     */
    static std::string checkLeaks();

    /**
     * 设置泄漏阈值 (bytes)
     */
    static void setLeakThreshold(size_t threshold);

private:
    static size_t sInitialAllocations;
    static size_t sLeakThreshold;
};
```

```cpp
// LeakDetector.cpp
#include "LeakDetector.h"
#include <sstream>
#include <iomanip>

size_t LeakDetector::sInitialAllocations = 0;
size_t LeakDetector::sLeakThreshold = 1024 * 1024;  // 1 MB

void LeakDetector::beginSession() {
    auto& tracker = MemoryTracker::getInstance();
    auto stats = tracker.getStats();
    sInitialAllocations = stats.totalAllocations;

    utils::slog.i << "Leak Detection Session Started" << utils::io::endl;
    utils::slog.i << "Initial allocations: " << sInitialAllocations << utils::io::endl;
}

std::string LeakDetector::endSession() {
    auto leaks = MemoryTracker::getInstance().detectLeaks();

    std::ostringstream report;
    report << "=== Leak Detection Report ===\n\n";

    if (leaks.empty()) {
        report << "No leaks detected!\n";
        return report.str();
    }

    // 统计泄漏
    size_t totalLeakedBytes = 0;
    std::unordered_map<std::string, size_t> leaksByTag;

    for (const auto& leak : leaks) {
        totalLeakedBytes += leak.size;
        leaksByTag[leak.tag] += leak.size;
    }

    report << "Total Leaked Memory: " << (totalLeakedBytes / 1024.0 / 1024.0) << " MB\n";
    report << "Total Leaked Allocations: " << leaks.size() << "\n\n";

    report << "Leaks by Tag:\n";
    for (const auto& [tag, size] : leaksByTag) {
        report << "  " << tag << ": " << (size / 1024.0 / 1024.0) << " MB\n";
    }

    report << "\nTop 10 Largest Leaks:\n";
    size_t count = std::min<size_t>(10, leaks.size());
    for (size_t i = 0; i < count; ++i) {
        const auto& leak = leaks[i];
        report << "  " << (i + 1) << ". " << leak.tag << " - "
               << (leak.size / 1024.0) << " KB at " << leak.address << "\n";
    }

    return report.str();
}

std::string LeakDetector::checkLeaks() {
    auto leaks = MemoryTracker::getInstance().detectLeaks();

    size_t totalSize = 0;
    for (const auto& leak : leaks) {
        totalSize += leak.size;
    }

    std::ostringstream result;
    if (totalSize > sLeakThreshold) {
        result << "WARNING: Potential memory leak detected! "
               << (totalSize / 1024.0 / 1024.0) << " MB leaked";
    } else {
        result << "Memory usage OK (" << (totalSize / 1024.0 / 1024.0) << " MB)";
    }

    return result.str();
}

void LeakDetector::setLeakThreshold(size_t threshold) {
    sLeakThreshold = threshold;
}
```

---

## 3. GPU 内存优化

### 3.1 纹理内存管理

```cpp
// TextureMemoryManager.h
#pragma once

#include <filament/Engine.h>
#include <filament/Texture.h>
#include <unordered_map>
#include <memory>

using namespace filament;

/**
 * 纹理内存管理器
 *
 * 优化纹理内存使用
 */
class TextureMemoryManager {
public:
    struct TextureConfig {
        bool useCompression = true;
        bool generateMipmaps = true;
        int maxSize = 2048;
        Texture::InternalFormat preferredFormat;
    };

public:
    explicit TextureMemoryManager(Engine* engine);
    ~TextureMemoryManager();

    /**
     * 加载纹理(自动优化)
     */
    Texture* loadTexture(const std::string& path, const TextureConfig& config);

    /**
     * 卸载未使用的纹理
     */
    void unloadUnusedTextures();

    /**
     * 获取内存使用情况
     */
    size_t getTotalMemoryUsage() const;

    /**
     * 设置内存预算
     */
    void setMemoryBudget(size_t bytes);

private:
    struct TextureEntry {
        Texture* texture;
        size_t size;
        int refCount;
        double lastAccessTime;
    };

    Engine* mEngine;
    std::unordered_map<std::string, TextureEntry> mTextures;
    size_t mMemoryBudget = 512 * 1024 * 1024;  // 512 MB
    size_t mCurrentMemoryUsage = 0;

    Texture* loadCompressedTexture(const std::string& path, const TextureConfig& config);
    void evictLRU();
};
```

### 3.2 缓冲区池化

```cpp
// BufferPool.h
#pragma once

#include <filament/Engine.h>
#include <filament/VertexBuffer.h>
#include <vector>
#include <queue>

using namespace filament;

/**
 * 缓冲区池
 *
 * 重用缓冲区以减少分配开销
 */
class BufferPool {
public:
    explicit BufferPool(Engine* engine);
    ~BufferPool();

    /**
     * 分配顶点缓冲区
     */
    VertexBuffer* allocateVertexBuffer(size_t vertexCount, const VertexBuffer::AttributeData& attributes);

    /**
     * 回收顶点缓冲区
     */
    void releaseVertexBuffer(VertexBuffer* buffer);

    /**
     * 清理池
     */
    void cleanup();

    /**
     * 获取统计信息
     */
    struct Stats {
        size_t totalBuffers;
        size_t activeBuffers;
        size_t pooledBuffers;
        size_t totalMemory;
    };

    Stats getStats() const;

private:
    struct BufferEntry {
        VertexBuffer* buffer;
        size_t size;
        bool inUse;
    };

    Engine* mEngine;
    std::vector<BufferEntry> mBuffers;
    std::queue<VertexBuffer*> mFreeBuffers;

    VertexBuffer* createNewBuffer(size_t vertexCount, const VertexBuffer::AttributeData& attributes);
};
```

---

## 4. CMakeLists.txt 配置

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.19)
project(FilamentMemoryTools)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件
set(SOURCES
    src/MemoryTracker.cpp
    src/FilamentResourceTracker.cpp
    src/ResourceManager.cpp
    src/LeakDetector.cpp
    src/TextureMemoryManager.cpp
    src/BufferPool.cpp
)

# 创建库
add_library(memory_tools STATIC ${SOURCES})

target_link_libraries(memory_tools PUBLIC
    filament
    utils
)

target_include_directories(memory_tools PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 可选: 集成第三方内存分析工具
option(USE_VALGRIND "Enable Valgrind support" OFF)
option(USE_ASAN "Enable AddressSanitizer" OFF)

if(USE_ASAN)
    target_compile_options(memory_tools PUBLIC -fsanitize=address)
    target_link_options(memory_tools PUBLIC -fsanitize=address)
endif()
```

---

## 5. 常见问题

### Q1: 如何检测内存泄漏?

**A**: 多种方法:

```cpp
// 方法 1: 使用 LeakDetector
LeakDetector::beginSession();

// 运行应用
runApplication();

// 检查泄漏
std::string report = LeakDetector::endSession();
utils::slog.i << report << utils::io::endl;

// 方法 2: 使用 AddressSanitizer (编译时)
// g++ -fsanitize=address -g app.cpp

// 方法 3: 使用 Valgrind (Linux)
// valgrind --leak-check=full ./app

// 方法 4: 使用 Instruments (macOS/iOS)
// Xcode > Product > Profile > Leaks
```

### Q2: 如何优化纹理内存?

**A**: 关键策略:

```cpp
// 1. 使用纹理压缩
Texture::InternalFormat format = Texture::InternalFormat::ETC2_EAC_RGBA8;

// 2. 限制纹理大小
int maxSize = 2048;  // 移动设备

// 3. 生成 Mipmap
.levels(mipLevels)

// 4. 按需加载
loadTextureOnDemand(name);

// 5. LRU 缓存
evictLeastRecentlyUsed();
```

### Q3: GPU 内存占用过高怎么办?

**A**: 排查步骤:

```
1. 使用 FilamentResourceTracker 查看所有资源
2. 识别最大的纹理和缓冲区
3. 优化策略:
   - 使用纹理压缩(ETC2, ASTC)
   - 降低纹理分辨率
   - 使用纹理图集(Texture Atlas)
   - 实现流式加载
   - 及时释放不用的资源
```

---

## 6. 相关文档

- [debugging/04-performance-profiling.md](./04-performance-profiling.md) - 性能分析
- [optimization/01-texture-optimization.md](../optimization/01-texture-optimization.md) - 纹理优化
- [optimization/04-memory-management.md](../optimization/04-memory-management.md) - 内存管理

---

## 7. 总结

内存性能分析要点:

1. **追踪**: 使用 MemoryTracker 追踪所有内存分配
2. **检测**: 使用 LeakDetector 检测内存泄漏
3. **优化**: 使用压缩、池化、LRU 等技术
4. **监控**: 持续监控内存使用情况
5. **工具**: 善用平台工具(Valgrind, ASAN, Instruments)

通过系统化的内存分析和优化,你可以确保应用稳定高效运行。
