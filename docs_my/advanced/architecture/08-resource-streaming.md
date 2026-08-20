# Filament 资源流式加载架构

## 📖 概述

资源流式加载（Resource Streaming）是大型 3D 应用的核心技术之一,它解决了一个根本性矛盾：**有限的内存 vs 海量的资源数据**。通过动态加载和卸载资源，应用可以渲染远超物理内存容量的巨大场景。

**流式加载与异步加载的区别**：
- **异步加载**：关注"不阻塞主线程"，资源一次性加载完成
- **流式加载**：关注"内存管理"，资源按需动态加载/卸载，支持无限大的数据集

**核心挑战**：
- **预测性**：提前加载即将需要的资源，避免"弹出"（Pop-in）
- **内存预算**：严格控制内存占用，在限制内最大化质量
- **加载速度**：I/O 速度有限，需要优化数据格式和加载策略
- **LOD 管理**：根据距离和重要性选择合适的细节级别

**适用场景**：
- 开放世界游戏（《塞尔达：旷野之息》、《侠盗猎车手》）
- 虚拟地球应用（Google Earth、Cesium）
- 大型 CAD/BIM 模型（建筑、工厂、城市）
- VR 体验（高分辨率、低延迟要求）

本文档将详细讲解如何为 Filament 设计和实现一个高性能的资源流式加载系统。

---

## 1. 流式加载架构概览

### 1.1 架构组件

```
┌─────────────────────────────────────────────────────────────────┐
│                      Streaming Manager                           │
│  ┌──────────────┐  ┌───────────────┐  ┌─────────────────────┐  │
│  │ Visibility   │  │ Memory Budget │  │ Priority Scheduler  │  │
│  │ Determination│  │  Controller   │  │                     │  │
│  └──────┬───────┘  └───────┬───────┘  └──────────┬──────────┘  │
│         │                  │                      │              │
│  ┌──────▼──────────────────▼──────────────────────▼──────────┐  │
│  │               Resource State Manager                       │  │
│  │  (Unloaded → Loading → Loaded → Evicting → Unloaded)      │  │
│  └──────┬──────────────────┬──────────────────────┬──────────┘  │
│         │                  │                      │              │
└─────────┼──────────────────┼──────────────────────┼──────────────┘
          │                  │                      │
    ┌─────▼─────┐      ┌─────▼──────┐      ┌──────▼───────┐
    │ Load Queue│      │ Active Set │      │ Evict Queue  │
    └─────┬─────┘      └─────┬──────┘      └──────┬───────┘
          │                  │                     │
    ┌─────▼──────────────────▼─────────────────────▼───────┐
    │              I/O Subsystem (Async)                    │
    │  - Disk/Network Reader                                │
    │  - Decompressor                                       │
    │  - GPU Uploader                                       │
    └───────────────────────────────────────────────────────┘
```

### 1.2 关键数据结构

```cpp
// src/streaming/include/streaming/StreamingManager.h
#pragma once

#include <unordered_map>
#include <set>
#include <memory>
#include <atomic>

namespace myproject::streaming {

// 资源状态
enum class ResourceState {
    Unloaded,    // 未加载
    Requested,   // 已请求加载
    Loading,     // 加载中
    Loaded,      // 已加载
    Evicting     // 卸载中
};

// 资源 LOD 级别
enum class LODLevel {
    LOD0 = 0,    // 最高精度
    LOD1 = 1,
    LOD2 = 2,
    LOD3 = 3,    // 最低精度
    COUNT
};

// 资源描述符
struct ResourceDescriptor {
    std::string path;
    size_t estimatedSize;      // 预估内存占用
    LODLevel maxLOD;           // 最大 LOD 级别
    float boundingSphereRadius;
    filament::math::float3 boundingCenter;
};

// 流式资源
class StreamingResource {
public:
    StreamingResource(const ResourceDescriptor& desc)
        : mDescriptor(desc)
        , mState(ResourceState::Unloaded)
        , mCurrentLOD(LODLevel::LOD3)
        , mRefCount(0)
        , mLastAccessTime(0)
        , mPriority(0.0f) {}
    
    // 状态管理
    ResourceState getState() const { return mState; }
    void setState(ResourceState state) { mState = state; }
    
    // LOD 管理
    LODLevel getCurrentLOD() const { return mCurrentLOD; }
    void setCurrentLOD(LODLevel lod) { mCurrentLOD = lod; }
    
    // 引用计数
    void addRef() { mRefCount++; }
    void release() { mRefCount--; }
    int getRefCount() const { return mRefCount; }
    
    // 访问时间（LRU）
    void touch(uint64_t time) { mLastAccessTime = time; }
    uint64_t getLastAccessTime() const { return mLastAccessTime; }
    
    // 优先级
    void setPriority(float priority) { mPriority = priority; }
    float getPriority() const { return mPriority; }
    
    // 资源数据
    const ResourceDescriptor& getDescriptor() const { return mDescriptor; }
    void* getData(LODLevel lod) const { return mLODData[static_cast<int>(lod)]; }
    void setData(LODLevel lod, void* data) { mLODData[static_cast<int>(lod)] = data; }
    
private:
    ResourceDescriptor mDescriptor;
    ResourceState mState;
    LODLevel mCurrentLOD;
    int mRefCount;
    uint64_t mLastAccessTime;
    float mPriority;
    
    void* mLODData[static_cast<int>(LODLevel::COUNT)] = {nullptr};
};

}  // namespace myproject::streaming
```

---

## 2. 可见性判断

### 2.1 视锥剔除

```cpp
class VisibilityDeterminer {
public:
    VisibilityDeterminer(filament::Camera* camera)
        : mCamera(camera) {}
    
    // 判断资源是否在视锥内
    bool isVisible(const StreamingResource& resource) const {
        const auto& desc = resource.getDescriptor();
        
        // 获取相机视锥
        auto frustum = mCamera->getFrustum();
        
        // 包围球测试
        filament::math::float4 sphere(
            desc.boundingCenter.x,
            desc.boundingCenter.y,
            desc.boundingCenter.z,
            desc.boundingSphereRadius
        );
        
        return frustum.intersects(sphere);
    }
    
    // 计算到相机的距离
    float getDistance(const StreamingResource& resource) const {
        auto camPos = mCamera->getPosition();
        auto resPos = resource.getDescriptor().boundingCenter;
        
        return filament::math::distance(camPos, resPos);
    }
    
    // 计算屏幕空间大小（用于 LOD 选择）
    float getScreenSpaceSize(const StreamingResource& resource) const {
        float distance = getDistance(resource);
        float radius = resource.getDescriptor().boundingSphereRadius;
        
        // 投影到屏幕
        auto projection = mCamera->getProjectionMatrix();
        float fov = mCamera->getFieldOfView(filament::Camera::Fov::VERTICAL);
        
        // 简化计算：屏幕高度占比
        float screenHeight = 1080.0f;  // 假设分辨率
        float angularSize = 2.0f * std::atan(radius / distance);
        float screenSize = (angularSize / fov) * screenHeight;
        
        return screenSize;
    }
    
private:
    filament::Camera* mCamera;
};
```

### 2.2 遮挡剔除

```cpp
class OcclusionCuller {
public:
    // 使用 GPU 遮挡查询
    bool isOccluded(const StreamingResource& resource) {
        // 1. 渲染包围盒到遮挡缓冲
        // 2. 执行遮挡查询
        // 3. 读取结果
        
        // 简化示例（实际需要 GPU 查询）
        return false;
    }
    
    // 使用 HZB（Hierarchical Z-Buffer）
    bool isOccludedHZB(const StreamingResource& resource) {
        // 1. 生成层级深度缓冲（Mipmap）
        // 2. 查询最大深度值
        // 3. 比较资源最近深度
        
        return false;
    }
};
```

---

## 3. LOD 选择策略

### 3.1 基于距离的 LOD

```cpp
class LODSelector {
public:
    LODLevel selectLOD(const StreamingResource& resource, 
                       const VisibilityDeterminer& visDet) const {
        float distance = visDet.getDistance(resource);
        
        // 距离阈值
        if (distance < mLODDistances[0]) return LODLevel::LOD0;
        if (distance < mLODDistances[1]) return LODLevel::LOD1;
        if (distance < mLODDistances[2]) return LODLevel::LOD2;
        return LODLevel::LOD3;
    }
    
    void setLODDistances(const std::array<float, 3>& distances) {
        mLODDistances = distances;
    }
    
private:
    std::array<float, 3> mLODDistances = {50.0f, 150.0f, 500.0f};
};
```

### 3.2 基于屏幕空间的 LOD

```cpp
class ScreenSpaceLODSelector {
public:
    LODLevel selectLOD(const StreamingResource& resource,
                       const VisibilityDeterminer& visDet) const {
        float screenSize = visDet.getScreenSpaceSize(resource);
        
        // 屏幕像素阈值
        if (screenSize > 200.0f) return LODLevel::LOD0;  // 大于 200 像素
        if (screenSize > 50.0f)  return LODLevel::LOD1;
        if (screenSize > 10.0f)  return LODLevel::LOD2;
        return LODLevel::LOD3;
    }
};
```

### 3.3 混合策略

```cpp
class HybridLODSelector {
public:
    LODLevel selectLOD(const StreamingResource& resource,
                       const VisibilityDeterminer& visDet,
                       float memoryPressure) const {
        // 基础 LOD（距离/屏幕大小）
        LODLevel baseLOD = mScreenSpaceSelector.selectLOD(resource, visDet);
        
        // 内存压力调整
        if (memoryPressure > 0.9f) {
            // 内存紧张，降低 LOD
            baseLOD = std::min(LODLevel::LOD3, 
                              static_cast<LODLevel>(static_cast<int>(baseLOD) + 1));
        } else if (memoryPressure < 0.5f) {
            // 内存充裕，提升 LOD
            baseLOD = std::max(LODLevel::LOD0,
                              static_cast<LODLevel>(static_cast<int>(baseLOD) - 1));
        }
        
        // 重要性调整（主角、重要物体优先高 LOD）
        float importance = resource.getImportance();
        if (importance > 0.8f) {
            baseLOD = std::max(LODLevel::LOD0,
                              static_cast<LODLevel>(static_cast<int>(baseLOD) - 1));
        }
        
        return baseLOD;
    }
    
private:
    ScreenSpaceLODSelector mScreenSpaceSelector;
};
```

---

## 4. 内存预算管理

### 4.1 内存预算控制器

```cpp
// src/streaming/include/streaming/MemoryBudget.h
#pragma once

#include <atomic>
#include <vector>

namespace myproject::streaming {

class MemoryBudgetController {
public:
    MemoryBudgetController(size_t totalBudget)
        : mTotalBudget(totalBudget)
        , mCurrentUsage(0) {}
    
    // 设置总预算
    void setTotalBudget(size_t bytes) {
        mTotalBudget = bytes;
    }
    
    size_t getTotalBudget() const {
        return mTotalBudget;
    }
    
    // 当前使用量
    size_t getCurrentUsage() const {
        return mCurrentUsage.load();
    }
    
    // 可用空间
    size_t getAvailableSpace() const {
        size_t current = mCurrentUsage.load();
        return current < mTotalBudget ? mTotalBudget - current : 0;
    }
    
    // 内存压力（0.0 - 1.0）
    float getMemoryPressure() const {
        return static_cast<float>(mCurrentUsage.load()) / mTotalBudget;
    }
    
    // 分配内存
    bool allocate(size_t bytes) {
        size_t current = mCurrentUsage.load();
        
        // 检查是否超预算
        if (current + bytes > mTotalBudget) {
            return false;
        }
        
        // 原子增加
        mCurrentUsage.fetch_add(bytes);
        return true;
    }
    
    // 释放内存
    void deallocate(size_t bytes) {
        mCurrentUsage.fetch_sub(bytes);
    }
    
    // 分级预算
    struct BudgetTier {
        std::string name;
        size_t budget;
        size_t usage;
    };
    
    void addTier(const std::string& name, size_t budget) {
        mTiers.push_back({name, budget, 0});
    }
    
    bool allocateTier(const std::string& name, size_t bytes) {
        for (auto& tier : mTiers) {
            if (tier.name == name) {
                if (tier.usage + bytes <= tier.budget) {
                    tier.usage += bytes;
                    return allocate(bytes);
                }
                return false;
            }
        }
        return false;
    }
    
    void deallocateTier(const std::string& name, size_t bytes) {
        for (auto& tier : mTiers) {
            if (tier.name == name) {
                tier.usage -= bytes;
                deallocate(bytes);
                break;
            }
        }
    }
    
private:
    size_t mTotalBudget;
    std::atomic<size_t> mCurrentUsage;
    std::vector<BudgetTier> mTiers;
};

}  // namespace myproject::streaming
```

**使用示例**：

```cpp
MemoryBudgetController memBudget(1024 * 1024 * 1024);  // 1GB

// 分级预算
memBudget.addTier("Textures", 512 * 1024 * 1024);   // 512MB
memBudget.addTier("Meshes",   256 * 1024 * 1024);   // 256MB
memBudget.addTier("Audio",    128 * 1024 * 1024);   // 128MB
memBudget.addTier("Other",    128 * 1024 * 1024);   // 128MB

// 加载纹理
if (memBudget.allocateTier("Textures", textureSize)) {
    loadTexture(...);
} else {
    LOG_WARNING("Texture budget exceeded, evicting...");
    evictLRUTexture();
}
```

### 4.2 LRU 驱逐策略

```cpp
class LRUEvictionPolicy {
public:
    // 选择要驱逐的资源
    StreamingResource* selectVictim(
        const std::vector<StreamingResource*>& candidates) {
        
        StreamingResource* victim = nullptr;
        uint64_t oldestTime = UINT64_MAX;
        
        for (auto* resource : candidates) {
            // 跳过正在使用的资源
            if (resource->getRefCount() > 0) {
                continue;
            }
            
            // 查找最久未使用的
            if (resource->getLastAccessTime() < oldestTime) {
                oldestTime = resource->getLastAccessTime();
                victim = resource;
            }
        }
        
        return victim;
    }
};
```

### 4.3 优先级驱逐策略

```cpp
class PriorityEvictionPolicy {
public:
    StreamingResource* selectVictim(
        const std::vector<StreamingResource*>& candidates) {
        
        StreamingResource* victim = nullptr;
        float lowestPriority = FLT_MAX;
        
        for (auto* resource : candidates) {
            if (resource->getRefCount() > 0) continue;
            
            // 综合考虑优先级和访问时间
            float score = resource->getPriority() * 0.7f +
                         (getCurrentTime() - resource->getLastAccessTime()) * 0.3f;
            
            if (score < lowestPriority) {
                lowestPriority = score;
                victim = resource;
            }
        }
        
        return victim;
    }
};
```

---

## 5. 优先级调度

### 5.1 优先级计算

```cpp
class PriorityCalculator {
public:
    float calculatePriority(const StreamingResource& resource,
                           const VisibilityDeterminer& visDet) const {
        float priority = 0.0f;
        
        // 1. 可见性权重（最重要）
        if (visDet.isVisible(resource)) {
            priority += 100.0f;
        }
        
        // 2. 距离权重（越近越高）
        float distance = visDet.getDistance(resource);
        float distanceScore = 1000.0f / (distance + 1.0f);
        priority += distanceScore;
        
        // 3. 屏幕空间大小权重
        float screenSize = visDet.getScreenSpaceSize(resource);
        priority += screenSize * 0.5f;
        
        // 4. 重要性权重（手动标记）
        priority += resource.getImportance() * 50.0f;
        
        // 5. 预测权重（即将进入视野）
        if (isPredictedVisible(resource)) {
            priority += 30.0f;
        }
        
        return priority;
    }
    
private:
    bool isPredictedVisible(const StreamingResource& resource) const {
        // 根据玩家移动方向和速度预测
        // 实现省略...
        return false;
    }
};
```

### 5.2 优先级队列调度

```cpp
class PriorityScheduler {
public:
    // 更新所有资源的优先级
    void updatePriorities(std::vector<StreamingResource*>& resources,
                         const VisibilityDeterminer& visDet) {
        mLoadQueue.clear();
        
        for (auto* resource : resources) {
            if (resource->getState() == ResourceState::Unloaded) {
                float priority = mPriorityCalc.calculatePriority(*resource, visDet);
                resource->setPriority(priority);
                
                mLoadQueue.push({resource, priority});
            }
        }
    }
    
    // 获取下一个要加载的资源
    StreamingResource* getNextToLoad() {
        if (mLoadQueue.empty()) return nullptr;
        
        auto top = mLoadQueue.top();
        mLoadQueue.pop();
        return top.resource;
    }
    
private:
    struct PriorityEntry {
        StreamingResource* resource;
        float priority;
        
        bool operator<(const PriorityEntry& other) const {
            return priority < other.priority;  // 最大堆
        }
    };
    
    std::priority_queue<PriorityEntry> mLoadQueue;
    PriorityCalculator mPriorityCalc;
};
```

---

## 6. 流式加载管理器

### 6.1 核心管理器实现

```cpp
// src/streaming/include/streaming/StreamingManager.h
#pragma once

#include <streaming/MemoryBudget.h>
#include <async/AsyncLoader.h>
#include <vector>
#include <unordered_map>

namespace myproject::streaming {

class StreamingManager {
public:
    StreamingManager(filament::Engine* engine,
                     async::AsyncLoader* asyncLoader,
                     size_t memoryBudget);
    
    ~StreamingManager();
    
    // 注册资源
    void registerResource(const ResourceDescriptor& desc);
    
    // 更新（每帧调用）
    void update(filament::Camera* camera, float deltaTime);
    
    // 请求资源
    StreamingResource* requestResource(const std::string& path);
    
    // 释放资源引用
    void releaseResource(StreamingResource* resource);
    
    // 统计信息
    struct Stats {
        size_t totalResources;
        size_t loadedResources;
        size_t loadingResources;
        size_t memoryUsage;
        size_t memoryBudget;
        float memoryPressure;
    };
    Stats getStats() const;
    
private:
    // 可见性判断
    void updateVisibility(filament::Camera* camera);
    
    // LOD 选择
    void updateLOD();
    
    // 优先级调度
    void scheduleTasks();
    
    // 执行加载
    void processLoadQueue();
    
    // 执行驱逐
    void processEviction();
    
    // 加载资源
    void loadResource(StreamingResource* resource, LODLevel lod);
    
    // 卸载资源
    void unloadResource(StreamingResource* resource, LODLevel lod);
    
    filament::Engine* mEngine;
    async::AsyncLoader* mAsyncLoader;
    
    MemoryBudgetController mMemoryBudget;
    VisibilityDeterminer mVisibilityDet;
    HybridLODSelector mLODSelector;
    PriorityScheduler mScheduler;
    LRUEvictionPolicy mEvictionPolicy;
    
    std::unordered_map<std::string, std::unique_ptr<StreamingResource>> mResources;
    std::vector<StreamingResource*> mVisibleResources;
    
    uint64_t mCurrentFrame = 0;
};

}  // namespace myproject::streaming
```

**实现**：

```cpp
// src/streaming/src/StreamingManager.cpp
#include <streaming/StreamingManager.h>
#include <core/Logger.h>

namespace myproject::streaming {

StreamingManager::StreamingManager(filament::Engine* engine,
                                   async::AsyncLoader* asyncLoader,
                                   size_t memoryBudget)
    : mEngine(engine)
    , mAsyncLoader(asyncLoader)
    , mMemoryBudget(memoryBudget)
    , mVisibilityDet(nullptr) {}

StreamingManager::~StreamingManager() {
    // 卸载所有资源
    for (auto& [path, resource] : mResources) {
        for (int i = 0; i < static_cast<int>(LODLevel::COUNT); ++i) {
            unloadResource(resource.get(), static_cast<LODLevel>(i));
        }
    }
}

void StreamingManager::registerResource(const ResourceDescriptor& desc) {
    auto resource = std::make_unique<StreamingResource>(desc);
    mResources[desc.path] = std::move(resource);
    
    LOG_INFO("Registered streaming resource: %s (size: %zu bytes)",
            desc.path.c_str(), desc.estimatedSize);
}

void StreamingManager::update(filament::Camera* camera, float deltaTime) {
    mCurrentFrame++;
    
    // 1. 更新可见性
    mVisibilityDet = VisibilityDeterminer(camera);
    updateVisibility(camera);
    
    // 2. 更新 LOD
    updateLOD();
    
    // 3. 调度任务
    scheduleTasks();
    
    // 4. 处理加载队列
    processLoadQueue();
    
    // 5. 处理驱逐（如果内存超预算）
    if (mMemoryBudget.getMemoryPressure() > 0.9f) {
        processEviction();
    }
}

void StreamingManager::updateVisibility(filament::Camera* camera) {
    mVisibleResources.clear();
    
    for (auto& [path, resource] : mResources) {
        if (mVisibilityDet.isVisible(*resource)) {
            mVisibleResources.push_back(resource.get());
            resource->touch(mCurrentFrame);
        }
    }
    
    LOG_DEBUG("Visible resources: %zu / %zu", 
             mVisibleResources.size(), mResources.size());
}

void StreamingManager::updateLOD() {
    for (auto* resource : mVisibleResources) {
        LODLevel desiredLOD = mLODSelector.selectLOD(
            *resource, 
            mVisibilityDet,
            mMemoryBudget.getMemoryPressure()
        );
        
        LODLevel currentLOD = resource->getCurrentLOD();
        
        // 如果需要切换 LOD
        if (desiredLOD != currentLOD) {
            // 请求加载新 LOD
            if (resource->getState() == ResourceState::Unloaded ||
                resource->getData(desiredLOD) == nullptr) {
                resource->setState(ResourceState::Requested);
            }
        }
    }
}

void StreamingManager::scheduleTasks() {
    std::vector<StreamingResource*> requestedResources;
    
    for (auto& [path, resource] : mResources) {
        if (resource->getState() == ResourceState::Requested) {
            requestedResources.push_back(resource.get());
        }
    }
    
    mScheduler.updatePriorities(requestedResources, mVisibilityDet);
}

void StreamingManager::processLoadQueue() {
    // 每帧最多处理 N 个加载请求
    const int maxLoadsPerFrame = 4;
    
    for (int i = 0; i < maxLoadsPerFrame; ++i) {
        auto* resource = mScheduler.getNextToLoad();
        if (!resource) break;
        
        LODLevel targetLOD = mLODSelector.selectLOD(
            *resource, mVisibilityDet, mMemoryBudget.getMemoryPressure());
        
        loadResource(resource, targetLOD);
    }
}

void StreamingManager::processEviction() {
    // 收集可驱逐的资源
    std::vector<StreamingResource*> candidates;
    
    for (auto& [path, resource] : mResources) {
        if (resource->getState() == ResourceState::Loaded &&
            resource->getRefCount() == 0) {
            candidates.push_back(resource.get());
        }
    }
    
    // 选择受害者
    auto* victim = mEvictionPolicy.selectVictim(candidates);
    
    if (victim) {
        LOG_INFO("Evicting resource: %s", victim->getDescriptor().path.c_str());
        
        // 卸载当前 LOD
        unloadResource(victim, victim->getCurrentLOD());
        
        victim->setState(ResourceState::Unloaded);
    }
}

void StreamingManager::loadResource(StreamingResource* resource, LODLevel lod) {
    const auto& desc = resource->getDescriptor();
    
    // 构造 LOD 文件路径
    std::string lodPath = desc.path + "_lod" + std::to_string(static_cast<int>(lod));
    
    LOG_INFO("Loading resource: %s (LOD %d)", lodPath.c_str(), static_cast<int>(lod));
    
    resource->setState(ResourceState::Loading);
    
    // 提交异步加载
    mAsyncLoader->load(lodPath, [this, resource, lod](void* data) {
        // 主线程回调
        if (data) {
            resource->setData(lod, data);
            resource->setCurrentLOD(lod);
            resource->setState(ResourceState::Loaded);
            
            // 更新内存使用
            size_t size = resource->getDescriptor().estimatedSize;
            mMemoryBudget.allocate(size);
            
            LOG_INFO("Resource loaded: LOD %d", static_cast<int>(lod));
        } else {
            LOG_ERROR("Failed to load resource");
            resource->setState(ResourceState::Unloaded);
        }
    }, nullptr, static_cast<int>(resource->getPriority()));
}

void StreamingManager::unloadResource(StreamingResource* resource, LODLevel lod) {
    void* data = resource->getData(lod);
    if (!data) return;
    
    // 释放数据（具体实现取决于资源类型）
    // delete data;
    
    resource->setData(lod, nullptr);
    
    // 更新内存使用
    size_t size = resource->getDescriptor().estimatedSize;
    mMemoryBudget.deallocate(size);
    
    LOG_INFO("Resource unloaded: LOD %d", static_cast<int>(lod));
}

StreamingResource* StreamingManager::requestResource(const std::string& path) {
    auto it = mResources.find(path);
    if (it != mResources.end()) {
        it->second->addRef();
        return it->second.get();
    }
    return nullptr;
}

void StreamingManager::releaseResource(StreamingResource* resource) {
    if (resource) {
        resource->release();
    }
}

StreamingManager::Stats StreamingManager::getStats() const {
    Stats stats{};
    stats.totalResources = mResources.size();
    stats.memoryBudget = mMemoryBudget.getTotalBudget();
    stats.memoryUsage = mMemoryBudget.getCurrentUsage();
    stats.memoryPressure = mMemoryBudget.getMemoryPressure();
    
    for (const auto& [path, resource] : mResources) {
        if (resource->getState() == ResourceState::Loaded) {
            stats.loadedResources++;
        } else if (resource->getState() == ResourceState::Loading) {
            stats.loadingResources++;
        }
    }
    
    return stats;
}

}  // namespace myproject::streaming
```

---

## 7. 虚拟纹理（Virtual Texturing）

### 7.1 虚拟纹理概念

虚拟纹理允许使用远超 GPU 内存容量的超大纹理（如 16K、32K），通过按需加载可见部分（Tiles）实现。

**核心思想**：
- 将大纹理切分为小块（Tiles，如 128x128）
- GPU 只存储当前需要的 Tiles
- 根据摄像机视角动态加载/卸载 Tiles

### 7.2 虚拟纹理实现

```cpp
class VirtualTexture {
public:
    VirtualTexture(int width, int height, int tileSize = 128)
        : mWidth(width)
        , mHeight(height)
        , mTileSize(tileSize)
        , mTilesX((width + tileSize - 1) / tileSize)
        , mTilesY((height + tileSize - 1) / tileSize) {
        
        // 创建物理纹理缓存（如 2048x2048）
        mPhysicalTexture = Texture::Builder()
            .width(2048)
            .height(2048)
            .format(Texture::InternalFormat::RGBA8)
            .build(*mEngine);
        
        // 初始化 Tile 映射表
        int totalTiles = mTilesX * mTilesY;
        mTiles.resize(totalTiles);
    }
    
    // 更新可见 Tiles
    void update(const std::vector<int2>& visibleTiles) {
        for (const auto& tileCoord : visibleTiles) {
            int tileIndex = tileCoord.y * mTilesX + tileCoord.x;
            
            if (!mTiles[tileIndex].loaded) {
                loadTile(tileIndex, tileCoord);
            }
            
            mTiles[tileIndex].lastAccessFrame = mCurrentFrame;
        }
        
        // 驱逐旧 Tiles
        evictOldTiles();
        
        mCurrentFrame++;
    }
    
private:
    struct Tile {
        bool loaded = false;
        int physicalX = -1;
        int physicalY = -1;
        uint64_t lastAccessFrame = 0;
    };
    
    void loadTile(int tileIndex, const int2& tileCoord) {
        // 查找空闲的物理 Tile 槽位
        int2 physicalSlot = findFreeSlot();
        
        // 异步加载 Tile 数据
        std::string tilePath = mBasePath + 
            "/tile_" + std::to_string(tileCoord.x) + 
            "_" + std::to_string(tileCoord.y) + ".png";
        
        mAsyncLoader->load(tilePath, [this, tileIndex, physicalSlot](void* data) {
            auto* pixels = static_cast<uint8_t*>(data);
            
            // 上传到物理纹理的对应位置
            Texture::PixelBufferDescriptor buffer(pixels, mTileSize * mTileSize * 4, ...);
            mPhysicalTexture->setImage(*mEngine, 0, std::move(buffer),
                                      physicalSlot.x * mTileSize,
                                      physicalSlot.y * mTileSize,
                                      mTileSize, mTileSize);
            
            // 更新映射
            mTiles[tileIndex].loaded = true;
            mTiles[tileIndex].physicalX = physicalSlot.x;
            mTiles[tileIndex].physicalY = physicalSlot.y;
        });
    }
    
    int2 findFreeSlot() {
        // 查找最久未使用的槽位
        // 实现省略...
        return {0, 0};
    }
    
    void evictOldTiles() {
        // 驱逐超过 N 帧未访问的 Tiles
        const uint64_t maxAge = 300;  // 300 帧 (~5 秒 @ 60fps)
        
        for (auto& tile : mTiles) {
            if (tile.loaded && (mCurrentFrame - tile.lastAccessFrame) > maxAge) {
                tile.loaded = false;
                // 物理槽位可被复用
            }
        }
    }
    
    int mWidth, mHeight, mTileSize;
    int mTilesX, mTilesY;
    Texture* mPhysicalTexture;
    std::vector<Tile> mTiles;
    uint64_t mCurrentFrame = 0;
    std::string mBasePath;
    AsyncLoader* mAsyncLoader;
    Engine* mEngine;
};
```

---

## 8. 实战案例：开放世界

### 8.1 地形流式加载

```cpp
class TerrainStreaming {
public:
    void update(const vec3& playerPos) {
        // 1. 确定当前所在的 Chunk
        int2 currentChunk = worldPosToChunk(playerPos);
        
        // 2. 计算加载半径内的 Chunks
        const int loadRadius = 5;  // 5x5 grid
        std::vector<int2> chunksToLoad;
        
        for (int y = -loadRadius; y <= loadRadius; ++y) {
            for (int x = -loadRadius; x <= loadRadius; ++x) {
                int2 chunkCoord = currentChunk + int2{x, y};
                
                if (!isChunkLoaded(chunkCoord)) {
                    chunksToLoad.push_back(chunkCoord);
                }
            }
        }
        
        // 3. 按距离排序
        std::sort(chunksToLoad.begin(), chunksToLoad.end(),
                 [&playerPos](const int2& a, const int2& b) {
            vec3 posA = chunkToWorldPos(a);
            vec3 posB = chunkToWorldPos(b);
            return distance(posA, playerPos) < distance(posB, playerPos);
        });
        
        // 4. 加载 Chunks
        for (const auto& chunkCoord : chunksToLoad) {
            loadChunk(chunkCoord);
        }
        
        // 5. 卸载远处的 Chunks
        unloadDistantChunks(currentChunk, loadRadius + 2);
    }
    
private:
    void loadChunk(const int2& chunkCoord) {
        std::string chunkPath = "terrain/chunk_" + 
            std::to_string(chunkCoord.x) + "_" + 
            std::to_string(chunkCoord.y) + ".bin";
        
        mStreamingMgr->registerResource({
            .path = chunkPath,
            .estimatedSize = 4 * 1024 * 1024,  // 4MB per chunk
            .maxLOD = LODLevel::LOD2,
            .boundingSphereRadius = 50.0f,
            .boundingCenter = chunkToWorldPos(chunkCoord)
        });
        
        mStreamingMgr->requestResource(chunkPath);
    }
    
    void unloadDistantChunks(const int2& centerChunk, int radius) {
        for (auto it = mLoadedChunks.begin(); it != mLoadedChunks.end(); ) {
            int2 chunkCoord = it->first;
            int dist = std::max(std::abs(chunkCoord.x - centerChunk.x),
                               std::abs(chunkCoord.y - centerChunk.y));
            
            if (dist > radius) {
                mStreamingMgr->releaseResource(it->second);
                it = mLoadedChunks.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    StreamingManager* mStreamingMgr;
    std::unordered_map<int2, StreamingResource*> mLoadedChunks;
};
```

---

## 9. 性能优化

### 9.1 数据压缩

```cpp
// 使用 LZ4 压缩资源数据
class CompressedResource {
public:
    void save(const std::string& path, const void* data, size_t size) {
        size_t compressedSize = LZ4_compressBound(size);
        char* compressed = new char[compressedSize];
        
        int actualSize = LZ4_compress_default(
            static_cast<const char*>(data),
            compressed,
            size,
            compressedSize
        );
        
        // 写入文件
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<char*>(&size), sizeof(size));  // 原始大小
        file.write(compressed, actualSize);
        
        delete[] compressed;
    }
    
    void* load(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        
        size_t originalSize;
        file.read(reinterpret_cast<char*>(&originalSize), sizeof(originalSize));
        
        file.seekg(0, std::ios::end);
        size_t compressedSize = file.tellg() - sizeof(originalSize);
        file.seekg(sizeof(originalSize));
        
        char* compressed = new char[compressedSize];
        file.read(compressed, compressedSize);
        
        char* decompressed = new char[originalSize];
        LZ4_decompress_safe(compressed, decompressed, compressedSize, originalSize);
        
        delete[] compressed;
        return decompressed;
    }
};
```

### 9.2 预取（Prefetching）

```cpp
class Prefetcher {
public:
    void predict(const vec3& playerPos, const vec3& playerVelocity) {
        // 预测 1 秒后的位置
        vec3 futurePos = playerPos + playerVelocity * 1.0f;
        
        // 查找该位置附近的资源
        std::vector<StreamingResource*> nearbyResources = 
            findResourcesNear(futurePos, 100.0f);
        
        // 预加载
        for (auto* resource : nearbyResources) {
            if (resource->getState() == ResourceState::Unloaded) {
                mStreamingMgr->requestResource(resource->getDescriptor().path);
            }
        }
    }
};
```

---

## 10. 调试工具

### 10.1 可视化工具

```cpp
class StreamingDebugger {
public:
    void drawDebugInfo() {
        auto stats = mStreamingMgr->getStats();
        
        // 显示统计
        drawText("Streaming Stats:");
        drawText("  Total Resources: %zu", stats.totalResources);
        drawText("  Loaded: %zu", stats.loadedResources);
        drawText("  Loading: %zu", stats.loadingResources);
        drawText("  Memory: %.2f / %.2f MB (%.1f%%)",
                stats.memoryUsage / 1024.0f / 1024.0f,
                stats.memoryBudget / 1024.0f / 1024.0f,
                stats.memoryPressure * 100.0f);
        
        // 可视化加载状态
        for (auto* resource : mVisibleResources) {
            vec3 pos = resource->getDescriptor().boundingCenter;
            
            Color color;
            switch (resource->getState()) {
                case ResourceState::Loaded:    color = Color::Green; break;
                case ResourceState::Loading:   color = Color::Yellow; break;
                case ResourceState::Unloaded:  color = Color::Red; break;
                default:                       color = Color::Gray; break;
            }
            
            drawSphere(pos, 1.0f, color);
        }
    }
};
```

---

## 11. 常见问题

### Q1: 如何避免"弹出"（Pop-in）现象？

**A**: 使用渐进式加载（LOD 0→1→2）+ 预测加载 + 淡入效果。

### Q2: 流式加载导致卡顿？

**A**: 限制每帧加载任务数量、使用异步加载、优化数据格式（压缩、预处理）。

### Q3: 如何处理网络加载？

**A**: 实现断点续传、缓存到本地、压缩传输数据。

---

## 12. 相关文档

- [07-async-loading.md](./07-async-loading.md) - 异步加载
- [04-ecs-best-practices.md](./04-ecs-best-practices.md) - ECS 最佳实践
- [../optimization/04-memory-management.md](../optimization/04-memory-management.md) - 内存管理

---

## 13. 总结

资源流式加载是大型 3D 应用的基石技术：

1. **可见性驱动**：只加载可见和即将可见的资源
2. **LOD 分级**：根据距离和屏幕大小动态调整细节
3. **内存预算**：严格控制内存占用，按优先级驱逐
4. **优先级调度**：重要资源优先加载
5. **预测加载**：根据玩家行为提前加载

通过本文档的架构设计，您可以为 Filament 项目构建一个高效的流式加载系统，支持无限大的虚拟世界。

**关键技术**：
- 视锥剔除和遮挡剔除
- 混合 LOD 选择策略
- LRU/Priority 驱逐策略
- 虚拟纹理
- 数据压缩和预取

流式加载不仅是技术挑战，更是艺术：平衡内存、性能、视觉质量，需要不断调优和迭代。
