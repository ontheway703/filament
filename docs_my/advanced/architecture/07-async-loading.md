# Filament 异步加载架构

## 📖 概述

异步加载是现代渲染引擎的核心特性之一。在大型 3D 应用中，资源加载往往耗时较长（模型、纹理、材质、动画等），如果在主线程同步加载，会导致应用卡顿、掉帧、用户体验极差。

**异步加载的核心目标**：
- **主线程不阻塞**：加载过程在后台线程进行，主线程持续响应用户输入和渲染
- **渐进式显示**：资源加载完成后逐步显示，而非全部完成后一次性出现
- **优先级调度**：根据重要性和距离调度加载顺序
- **内存管理**：控制并发加载数量，避免内存溢出

**适用场景**：
- 开放世界游戏（动态加载地形、建筑、角色）
- CAD/BIM 软件（大型模型分块加载）
- VR/AR 应用（低延迟要求，必须异步）
- Web 应用（网络加载延迟高）

本文档将深入讲解 Filament 异步加载的完整架构，包括线程模型、任务调度、资源生命周期、错误处理等核心技术。

---

## 1. 异步加载架构概览

### 1.1 架构组件

```
┌─────────────────────────────────────────────────────────────────┐
│                         Main Thread                              │
│  ┌─────────────┐    ┌──────────────┐    ┌──────────────────┐   │
│  │ Application │───▶│ AsyncLoader  │───▶│ ResourceManager  │   │
│  └─────────────┘    └──────────────┘    └──────────────────┘   │
│                            │                       │             │
└────────────────────────────┼───────────────────────┼─────────────┘
                             │                       │
                ┌────────────▼───────────────────────▼────────┐
                │          Task Queue (Thread-Safe)            │
                └──────────────────┬───────────────────────────┘
                                   │
        ┌──────────────────────────┼──────────────────────────┐
        │                          │                           │
┌───────▼────────┐        ┌────────▼───────┐        ┌─────────▼──────┐
│ Worker Thread  │        │ Worker Thread  │        │ Worker Thread  │
│   (Load I/O)   │        │   (Decompress) │        │   (Convert)    │
└───────┬────────┘        └────────┬───────┘        └─────────┬──────┘
        │                          │                           │
        └──────────────────────────┼───────────────────────────┘
                                   │
                        ┌──────────▼────────────┐
                        │  Completion Callback  │
                        │  (Back to Main Thread)│
                        └───────────────────────┘
```

### 1.2 关键概念

**LoadTask**：异步加载任务，包含资源路径、回调函数、优先级等信息。

**Worker Thread Pool**：工作线程池，执行实际的 I/O、解压缩、格式转换等耗时操作。

**Main Thread Callback**：加载完成后，在主线程回调，安全地创建 GPU 资源。

**Priority Queue**：优先级队列，根据重要性和距离调度任务执行顺序。

---

## 2. 线程模型

### 2.1 线程划分

Filament 异步加载涉及以下线程：

1. **Main Thread**：
   - 提交加载请求
   - 接收加载完成回调
   - 创建 GPU 资源（Texture、Mesh、Material）
   - 更新场景图

2. **Worker Threads（I/O）**：
   - 读取文件（磁盘/网络）
   - 解析文件格式（glTF、OBJ、PNG 等）

3. **Worker Threads（Processing）**：
   - 解压缩（Zlib、BC、ETC2）
   - 格式转换（RGBA→DXT、生成 Mipmaps）
   - 数据验证

4. **Filament Backend Thread**：
   - 执行 GPU 命令
   - 创建底层 GPU 对象

### 2.2 线程同步

```cpp
// src/async/include/async/AsyncLoader.h
#pragma once

#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <atomic>
#include <functional>

namespace myproject::async {

// 加载任务
struct LoadTask {
    std::string path;
    std::function<void(void*)> callback;  // 主线程回调
    void* userData;
    int priority;  // 越小越优先
    
    bool operator<(const LoadTask& other) const {
        return priority > other.priority;  // 最小堆
    }
};

// 异步加载器
class AsyncLoader {
public:
    AsyncLoader(size_t numThreads = 4);
    ~AsyncLoader();
    
    // 提交加载任务
    void load(const std::string& path, 
              std::function<void(void*)> callback,
              void* userData = nullptr,
              int priority = 0);
    
    // 处理完成的任务（主线程调用）
    void processCompletedTasks();
    
    // 暂停/恢复加载
    void pause();
    void resume();
    
    // 取消所有任务
    void cancelAll();
    
private:
    // 工作线程函数
    void workerThread();
    
    // 线程池
    std::vector<std::thread> mWorkers;
    
    // 任务队列（待处理）
    std::priority_queue<LoadTask> mTaskQueue;
    std::mutex mTaskQueueMutex;
    std::condition_variable mTaskQueueCV;
    
    // 完成队列（待回调）
    struct CompletedTask {
        std::function<void(void*)> callback;
        void* data;
        void* userData;
    };
    std::queue<CompletedTask> mCompletedQueue;
    std::mutex mCompletedQueueMutex;
    
    // 控制标志
    std::atomic<bool> mRunning{true};
    std::atomic<bool> mPaused{false};
};

}  // namespace myproject::async
```

**实现**：

```cpp
// src/async/src/AsyncLoader.cpp
#include <async/AsyncLoader.h>
#include <core/Logger.h>

namespace myproject::async {

AsyncLoader::AsyncLoader(size_t numThreads) {
    // 启动工作线程
    for (size_t i = 0; i < numThreads; ++i) {
        mWorkers.emplace_back(&AsyncLoader::workerThread, this);
    }
    
    LOG_INFO("AsyncLoader started with %zu threads", numThreads);
}

AsyncLoader::~AsyncLoader() {
    // 停止所有线程
    mRunning.store(false);
    mTaskQueueCV.notify_all();
    
    for (auto& worker : mWorkers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    LOG_INFO("AsyncLoader stopped");
}

void AsyncLoader::load(const std::string& path,
                       std::function<void(void*)> callback,
                       void* userData,
                       int priority) {
    std::lock_guard<std::mutex> lock(mTaskQueueMutex);
    
    LoadTask task;
    task.path = path;
    task.callback = callback;
    task.userData = userData;
    task.priority = priority;
    
    mTaskQueue.push(task);
    mTaskQueueCV.notify_one();  // 唤醒一个工作线程
    
    LOG_DEBUG("Queued load task: %s (priority: %d)", path.c_str(), priority);
}

void AsyncLoader::workerThread() {
    while (mRunning.load()) {
        LoadTask task;
        
        // 从队列取任务
        {
            std::unique_lock<std::mutex> lock(mTaskQueueMutex);
            
            // 等待任务或停止信号
            mTaskQueueCV.wait(lock, [this] {
                return !mTaskQueue.empty() || !mRunning.load();
            });
            
            if (!mRunning.load()) break;
            
            // 检查是否暂停
            if (mPaused.load()) {
                continue;
            }
            
            task = mTaskQueue.top();
            mTaskQueue.pop();
        }
        
        LOG_DEBUG("Worker processing: %s", task.path.c_str());
        
        // 执行加载（耗时操作）
        void* data = performLoad(task.path);
        
        // 加载完成，放入完成队列
        {
            std::lock_guard<std::mutex> lock(mCompletedQueueMutex);
            mCompletedQueue.push({task.callback, data, task.userData});
        }
        
        LOG_DEBUG("Worker completed: %s", task.path.c_str());
    }
}

void AsyncLoader::processCompletedTasks() {
    std::lock_guard<std::mutex> lock(mCompletedQueueMutex);
    
    // 处理所有完成的任务（在主线程）
    while (!mCompletedQueue.empty()) {
        auto& completed = mCompletedQueue.front();
        
        if (completed.callback) {
            completed.callback(completed.data);
        }
        
        mCompletedQueue.pop();
    }
}

void AsyncLoader::pause() {
    mPaused.store(true);
    LOG_INFO("AsyncLoader paused");
}

void AsyncLoader::resume() {
    mPaused.store(false);
    mTaskQueueCV.notify_all();
    LOG_INFO("AsyncLoader resumed");
}

void AsyncLoader::cancelAll() {
    std::lock_guard<std::mutex> lock(mTaskQueueMutex);
    
    // 清空任务队列
    while (!mTaskQueue.empty()) {
        mTaskQueue.pop();
    }
    
    LOG_INFO("All pending tasks cancelled");
}

// 实际加载函数（示例）
void* AsyncLoader::performLoad(const std::string& path) {
    // 这里执行实际的 I/O 操作
    // 例如：读取文件、解析格式、解压缩等
    
    // 示例：加载纹理
    if (path.find(".png") != std::string::npos) {
        int width, height, channels;
        unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
        
        if (!data) {
            LOG_ERROR("Failed to load texture: %s", path.c_str());
            return nullptr;
        }
        
        // 封装数据
        struct TextureData {
            unsigned char* pixels;
            int width;
            int height;
        };
        
        TextureData* texData = new TextureData{data, width, height};
        return texData;
    }
    
    return nullptr;
}

}  // namespace myproject::async
```

---

## 3. 资源异步加载实现

### 3.1 纹理异步加载

```cpp
// src/rendering/include/rendering/AsyncTextureLoader.h
#pragma once

#include <async/AsyncLoader.h>
#include <filament/Engine.h>
#include <filament/Texture.h>
#include <functional>

namespace myproject::rendering {

class AsyncTextureLoader {
public:
    AsyncTextureLoader(filament::Engine* engine, async::AsyncLoader* asyncLoader);
    
    // 异步加载纹理
    void loadTexture(const std::string& path,
                     std::function<void(filament::Texture*)> callback,
                     int priority = 0);
    
    // 主线程更新
    void update();
    
private:
    filament::Engine* mEngine;
    async::AsyncLoader* mAsyncLoader;
    
    struct TextureLoadData {
        unsigned char* pixels;
        int width;
        int height;
        std::function<void(filament::Texture*)> callback;
    };
};

}  // namespace myproject::rendering
```

**实现**：

```cpp
// src/rendering/src/AsyncTextureLoader.cpp
#include <rendering/AsyncTextureLoader.h>
#include <core/Logger.h>

namespace myproject::rendering {

AsyncTextureLoader::AsyncTextureLoader(filament::Engine* engine, 
                                       async::AsyncLoader* asyncLoader)
    : mEngine(engine)
    , mAsyncLoader(asyncLoader) {}

void AsyncTextureLoader::loadTexture(const std::string& path,
                                     std::function<void(filament::Texture*)> callback,
                                     int priority) {
    LOG_INFO("Async loading texture: %s", path.c_str());
    
    // 提交异步任务
    mAsyncLoader->load(path, [this, callback](void* data) {
        // 这个回调在主线程执行
        if (!data) {
            LOG_ERROR("Texture load failed");
            if (callback) callback(nullptr);
            return;
        }
        
        // 解包数据
        auto* texData = static_cast<TextureLoadData*>(data);
        
        // 创建 Filament Texture（主线程）
        filament::Texture* texture = filament::Texture::Builder()
            .width(static_cast<uint32_t>(texData->width))
            .height(static_cast<uint32_t>(texData->height))
            .levels(1)
            .format(filament::Texture::InternalFormat::RGBA8)
            .build(*mEngine);
        
        // 上传数据
        filament::Texture::PixelBufferDescriptor buffer(
            texData->pixels,
            size_t(texData->width * texData->height * 4),
            filament::Texture::Format::RGBA,
            filament::Texture::Type::UBYTE,
            [](void* buffer, size_t, void*) {
                stbi_image_free(buffer);  // 释放 stb_image 分配的内存
            }
        );
        
        texture->setImage(*mEngine, 0, std::move(buffer));
        
        LOG_INFO("Texture created: %dx%d", texData->width, texData->height);
        
        // 用户回调
        if (callback) {
            callback(texture);
        }
        
        delete texData;
        
    }, nullptr, priority);
}

void AsyncTextureLoader::update() {
    // 处理完成的任务
    mAsyncLoader->processCompletedTasks();
}

}  // namespace myproject::rendering
```

**使用示例**：

```cpp
AsyncTextureLoader textureLoader(engine, &asyncLoader);

// 提交异步加载请求
textureLoader.loadTexture("assets/textures/wood.png", [](filament::Texture* tex) {
    if (tex) {
        LOG_INFO("Texture loaded successfully!");
        // 使用纹理
        material->setParameter("baseColorMap", tex, sampler);
    }
}, 10);  // 优先级 10

// 主循环中
while (running) {
    textureLoader.update();  // 处理完成的加载任务
    
    renderer->render(view);
}
```

### 3.2 模型异步加载

```cpp
// src/rendering/include/rendering/AsyncModelLoader.h
#pragma once

#include <async/AsyncLoader.h>
#include <filament/Engine.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <functional>

namespace myproject::rendering {

class AsyncModelLoader {
public:
    AsyncModelLoader(filament::Engine* engine, 
                     gltfio::AssetLoader* assetLoader,
                     async::AsyncLoader* asyncLoader);
    
    ~AsyncModelLoader();
    
    // 异步加载模型
    void loadModel(const std::string& path,
                   std::function<void(gltfio::FilamentAsset*)> callback,
                   int priority = 0);
    
    // 主线程更新
    void update();
    
private:
    filament::Engine* mEngine;
    gltfio::AssetLoader* mAssetLoader;
    async::AsyncLoader* mAsyncLoader;
    
    struct ModelLoadData {
        uint8_t* gltfData;
        size_t gltfSize;
        std::function<void(gltfio::FilamentAsset*)> callback;
    };
};

}  // namespace myproject::rendering
```

**实现**：

```cpp
// src/rendering/src/AsyncModelLoader.cpp
#include <rendering/AsyncModelLoader.h>
#include <core/Logger.h>
#include <fstream>

namespace myproject::rendering {

AsyncModelLoader::AsyncModelLoader(filament::Engine* engine,
                                   gltfio::AssetLoader* assetLoader,
                                   async::AsyncLoader* asyncLoader)
    : mEngine(engine)
    , mAssetLoader(assetLoader)
    , mAsyncLoader(asyncLoader) {}

AsyncModelLoader::~AsyncModelLoader() {}

void AsyncModelLoader::loadModel(const std::string& path,
                                 std::function<void(gltfio::FilamentAsset*)> callback,
                                 int priority) {
    LOG_INFO("Async loading model: %s", path.c_str());
    
    // 提交异步任务
    mAsyncLoader->load(path, [this, callback](void* data) {
        // 主线程回调
        if (!data) {
            LOG_ERROR("Model load failed");
            if (callback) callback(nullptr);
            return;
        }
        
        auto* modelData = static_cast<ModelLoadData*>(data);
        
        // 创建 FilamentAsset（主线程）
        gltfio::FilamentAsset* asset = mAssetLoader->createAsset(
            modelData->gltfData, 
            modelData->gltfSize
        );
        
        if (!asset) {
            LOG_ERROR("Failed to create FilamentAsset");
            delete[] modelData->gltfData;
            delete modelData;
            if (callback) callback(nullptr);
            return;
        }
        
        LOG_INFO("Model created: %zu entities", asset->getEntityCount());
        
        // 用户回调
        if (callback) {
            callback(asset);
        }
        
        // 清理
        delete[] modelData->gltfData;
        delete modelData;
        
    }, nullptr, priority);
}

void AsyncModelLoader::update() {
    mAsyncLoader->processCompletedTasks();
}

// Worker 线程执行的加载函数
void* AsyncLoader::performLoad(const std::string& path) {
    // glTF 文件
    if (path.find(".glb") != std::string::npos || 
        path.find(".gltf") != std::string::npos) {
        
        // 读取文件
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open file: %s", path.c_str());
            return nullptr;
        }
        
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        
        uint8_t* buffer = new uint8_t[fileSize];
        file.read(reinterpret_cast<char*>(buffer), fileSize);
        file.close();
        
        // 封装数据
        AsyncModelLoader::ModelLoadData* modelData = 
            new AsyncModelLoader::ModelLoadData{
                buffer, 
                fileSize, 
                nullptr  // callback 将在 loadModel 中设置
            };
        
        return modelData;
    }
    
    return nullptr;
}

}  // namespace myproject::rendering
```

---

## 4. 优先级调度

### 4.1 优先级策略

根据不同场景设置加载优先级：

```cpp
enum class LoadPriority {
    Critical = 0,     // 立即需要（当前视野内）
    High = 10,        // 高优先级（即将进入视野）
    Normal = 50,      // 正常优先级
    Low = 100,        // 低优先级（远处对象）
    Background = 200  // 后台预加载
};

// 根据距离计算优先级
int calculatePriority(const vec3& objectPos, const vec3& cameraPos) {
    float distance = length(objectPos - cameraPos);
    
    if (distance < 10.0f) {
        return static_cast<int>(LoadPriority::Critical);
    } else if (distance < 50.0f) {
        return static_cast<int>(LoadPriority::High);
    } else if (distance < 200.0f) {
        return static_cast<int>(LoadPriority::Normal);
    } else {
        return static_cast<int>(LoadPriority::Low);
    }
}

// 使用
textureLoader.loadTexture("wood.png", callback, calculatePriority(objPos, camPos));
```

### 4.2 动态调整优先级

```cpp
class AsyncLoader {
public:
    // 更新任务优先级
    void updatePriority(const std::string& path, int newPriority) {
        std::lock_guard<std::mutex> lock(mTaskQueueMutex);
        
        // 查找任务
        auto temp = std::priority_queue<LoadTask>();
        bool found = false;
        
        while (!mTaskQueue.empty()) {
            LoadTask task = mTaskQueue.top();
            mTaskQueue.pop();
            
            if (task.path == path) {
                task.priority = newPriority;
                found = true;
            }
            
            temp.push(task);
        }
        
        if (found) {
            mTaskQueue = std::move(temp);
            LOG_INFO("Updated priority for %s to %d", path.c_str(), newPriority);
        }
    }
};
```

---

## 5. 流式加载

### 5.1 分块加载

对于大型资源，分块加载避免长时间阻塞：

```cpp
class StreamingLoader {
public:
    // 加载大型纹理（分块）
    void loadLargeTexture(const std::string& path, 
                          std::function<void(Texture*)> callback) {
        // 步骤1：异步加载纹理头信息
        asyncLoader->load(path + ".header", [this, path, callback](void* data) {
            auto* header = static_cast<TextureHeader*>(data);
            
            // 创建纹理对象（仅分配 GPU 内存）
            Texture* tex = Texture::Builder()
                .width(header->width)
                .height(header->height)
                .levels(header->mipLevels)
                .format(header->format)
                .build(*mEngine);
            
            // 步骤2：异步加载各级 Mipmap
            for (int level = 0; level < header->mipLevels; ++level) {
                std::string mipPath = path + ".mip" + std::to_string(level);
                
                asyncLoader->load(mipPath, [this, tex, level](void* mipData) {
                    // 上传单个 Mip 级别
                    auto* pixels = static_cast<uint8_t*>(mipData);
                    
                    Texture::PixelBufferDescriptor buffer(pixels, ...);
                    tex->setImage(*mEngine, level, std::move(buffer));
                    
                }, nullptr, level);  // Mip 0 优先级最高
            }
            
            // 立即回调（纹理尚未完全加载）
            if (callback) callback(tex);
            
            delete header;
        });
    }
};
```

### 5.2 LOD 流式加载

```cpp
class LODStreamingLoader {
public:
    void loadModelWithLOD(const std::string& basePath,
                          std::function<void(FilamentAsset*, int lod)> callback) {
        // 先加载低精度 LOD（小文件，快速）
        asyncLoader->load(basePath + "_lod2.glb", [this, basePath, callback](void* data) {
            auto* asset = createAsset(data);
            callback(asset, 2);  // LOD 2 加载完成
            
            // 继续加载中精度 LOD
            asyncLoader->load(basePath + "_lod1.glb", [this, basePath, callback](void* data) {
                auto* asset = createAsset(data);
                callback(asset, 1);  // LOD 1 加载完成
                
                // 最后加载高精度 LOD
                asyncLoader->load(basePath + "_lod0.glb", [callback](void* data) {
                    auto* asset = createAsset(data);
                    callback(asset, 0);  // LOD 0 加载完成
                }, nullptr, LoadPriority::Low);
                
            }, nullptr, LoadPriority::Normal);
            
        }, nullptr, LoadPriority::Critical);
    }
};

// 使用
lodLoader.loadModelWithLOD("assets/models/tree", [](FilamentAsset* asset, int lod) {
    LOG_INFO("LOD %d loaded", lod);
    
    // 替换场景中的模型
    if (lod == 2) {
        // 立即显示低精度模型
        scene->addEntities(asset->getEntities(), asset->getEntityCount());
    } else {
        // 替换为更高精度的模型
        scene->remove(oldAsset->getEntities(), oldAsset->getEntityCount());
        scene->addEntities(asset->getEntities(), asset->getEntityCount());
    }
});
```

---

## 6. 内存管理

### 6.1 限制并发加载

```cpp
class AsyncLoader {
public:
    void setMaxConcurrentLoads(size_t maxLoads) {
        mMaxConcurrentLoads = maxLoads;
    }
    
private:
    void workerThread() {
        while (mRunning.load()) {
            LoadTask task;
            
            {
                std::unique_lock<std::mutex> lock(mTaskQueueMutex);
                
                // 等待任务，且并发数未超限
                mTaskQueueCV.wait(lock, [this] {
                    return (!mTaskQueue.empty() && 
                            mActiveLoads.load() < mMaxConcurrentLoads) || 
                           !mRunning.load();
                });
                
                if (!mRunning.load()) break;
                
                task = mTaskQueue.top();
                mTaskQueue.pop();
                
                mActiveLoads.fetch_add(1);
            }
            
            // 执行加载
            void* data = performLoad(task.path);
            
            mActiveLoads.fetch_sub(1);
            mTaskQueueCV.notify_one();  // 唤醒其他等待的线程
            
            // 放入完成队列...
        }
    }
    
    std::atomic<size_t> mActiveLoads{0};
    size_t mMaxConcurrentLoads = 4;
};
```

### 6.2 内存预算

```cpp
class AsyncLoader {
public:
    void setMemoryBudget(size_t bytes) {
        mMemoryBudget = bytes;
    }
    
    bool canLoad(size_t estimatedSize) {
        return (mCurrentMemoryUsage.load() + estimatedSize) < mMemoryBudget;
    }
    
private:
    void* performLoad(const std::string& path) {
        size_t estimatedSize = estimateFileSize(path);
        
        // 等待内存可用
        while (!canLoad(estimatedSize)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // 执行加载
        void* data = actualLoad(path);
        
        mCurrentMemoryUsage.fetch_add(estimatedSize);
        
        return data;
    }
    
    void onLoadComplete(void* data, size_t size) {
        mCurrentMemoryUsage.fetch_sub(size);
    }
    
    std::atomic<size_t> mCurrentMemoryUsage{0};
    size_t mMemoryBudget = 512 * 1024 * 1024;  // 512MB
};
```

---

## 7. 错误处理

### 7.1 超时机制

```cpp
class AsyncLoader {
public:
    void setTimeout(std::chrono::seconds timeout) {
        mTimeout = timeout;
    }
    
private:
    void workerThread() {
        while (mRunning.load()) {
            LoadTask task;
            
            {
                std::unique_lock<std::mutex> lock(mTaskQueueMutex);
                
                if (mTaskQueueCV.wait_for(lock, mTimeout, [this] {
                    return !mTaskQueue.empty() || !mRunning.load();
                })) {
                    // 有任务或停止信号
                    if (!mRunning.load()) break;
                    
                    task = mTaskQueue.top();
                    mTaskQueue.pop();
                } else {
                    // 超时
                    continue;
                }
            }
            
            // 加载任务，带超时检测
            auto start = std::chrono::steady_clock::now();
            void* data = performLoad(task.path);
            auto elapsed = std::chrono::steady_clock::now() - start;
            
            if (elapsed > mTimeout) {
                LOG_WARNING("Load timeout: %s (%.2fs)", 
                           task.path.c_str(),
                           std::chrono::duration<double>(elapsed).count());
                
                // 通知失败
                mCompletedQueue.push({task.callback, nullptr, task.userData});
            } else {
                mCompletedQueue.push({task.callback, data, task.userData});
            }
        }
    }
    
    std::chrono::seconds mTimeout{30};
};
```

### 7.2 重试机制

```cpp
struct LoadTask {
    std::string path;
    std::function<void(void*)> callback;
    void* userData;
    int priority;
    int retryCount = 0;
    int maxRetries = 3;
};

void AsyncLoader::workerThread() {
    while (mRunning.load()) {
        LoadTask task = getNextTask();
        
        void* data = performLoad(task.path);
        
        if (!data && task.retryCount < task.maxRetries) {
            // 加载失败，重试
            task.retryCount++;
            LOG_WARNING("Load failed, retry %d/%d: %s",
                       task.retryCount, task.maxRetries, task.path.c_str());
            
            // 降低优先级后重新入队
            task.priority += 10;
            mTaskQueue.push(task);
        } else {
            // 成功或达到最大重试次数
            mCompletedQueue.push({task.callback, data, task.userData});
        }
    }
}
```

---

## 8. 占位符和渐进式显示

### 8.1 占位符纹理

```cpp
class AsyncTextureLoader {
public:
    AsyncTextureLoader(Engine* engine, AsyncLoader* asyncLoader)
        : mEngine(engine)
        , mAsyncLoader(asyncLoader) {
        // 创建占位符纹理（1x1 灰色）
        uint8_t gray[] = {128, 128, 128, 255};
        mPlaceholderTexture = Texture::Builder()
            .width(1).height(1)
            .format(Texture::InternalFormat::RGBA8)
            .build(*mEngine);
        
        Texture::PixelBufferDescriptor buffer(gray, 4, ...);
        mPlaceholderTexture->setImage(*mEngine, 0, std::move(buffer));
    }
    
    Texture* loadTexture(const std::string& path,
                         std::function<void(Texture*)> callback) {
        // 立即返回占位符
        Texture* placeholder = mPlaceholderTexture;
        
        // 异步加载实际纹理
        mAsyncLoader->load(path, [callback, placeholder](void* data) {
            Texture* actualTexture = createTexture(data);
            
            // 回调时传递实际纹理
            if (callback) {
                callback(actualTexture);
            }
        });
        
        return placeholder;
    }
    
private:
    Engine* mEngine;
    AsyncLoader* mAsyncLoader;
    Texture* mPlaceholderTexture;
};

// 使用
Texture* tex = textureLoader.loadTexture("wood.png", [material](Texture* actualTex) {
    // 替换占位符
    material->setParameter("baseColorMap", actualTex, sampler);
});

// 先使用占位符
material->setParameter("baseColorMap", tex, sampler);
```

### 8.2 渐进式纹理加载

```cpp
void loadProgressiveTexture(const std::string& path,
                            std::function<void(Texture*, int level)> callback) {
    // 先加载低分辨率版本（thumbnail）
    asyncLoader->load(path + "_thumb.png", [this, path, callback](void* data) {
        Texture* tex = createTexture(data, 64, 64);  // 64x64
        callback(tex, 2);  // Level 2（低分辨率）
        
        // 继续加载中分辨率
        asyncLoader->load(path + "_medium.png", [this, path, tex, callback](void* data) {
            updateTexture(tex, data, 256, 256);  // 256x256
            callback(tex, 1);  // Level 1（中分辨率）
            
            // 最后加载高分辨率
            asyncLoader->load(path, [tex, callback](void* data) {
                updateTexture(tex, data, 1024, 1024);  // 1024x1024
                callback(tex, 0);  // Level 0（高分辨率）
            });
        });
    }, nullptr, LoadPriority::Critical);
}
```

---

## 9. 实战案例

### 9.1 开放世界游戏

```cpp
class OpenWorldLoader {
public:
    void update(const vec3& playerPos, const vec3& playerDir) {
        // 1. 卸载远处的资源
        unloadDistantResources(playerPos);
        
        // 2. 根据玩家位置和朝向预测需要加载的区域
        std::vector<GridCell> cellsToLoad = predictCells(playerPos, playerDir);
        
        // 3. 按距离排序，近的优先加载
        std::sort(cellsToLoad.begin(), cellsToLoad.end(), 
                  [&playerPos](const GridCell& a, const GridCell& b) {
            return distance(a.center, playerPos) < distance(b.center, playerPos);
        });
        
        // 4. 提交加载任务
        for (const auto& cell : cellsToLoad) {
            if (!isLoaded(cell) && !isLoading(cell)) {
                loadCell(cell, playerPos);
            }
        }
    }
    
private:
    void loadCell(const GridCell& cell, const vec3& playerPos) {
        int priority = calculatePriority(cell.center, playerPos);
        
        // 加载地形
        asyncLoader->load(cell.terrainPath, [this, cell](void* data) {
            auto* terrain = createTerrain(data);
            mLoadedCells[cell.id].terrain = terrain;
        }, nullptr, priority);
        
        // 加载建筑
        asyncLoader->load(cell.buildingsPath, [this, cell](void* data) {
            auto* buildings = createBuildings(data);
            mLoadedCells[cell.id].buildings = buildings;
        }, nullptr, priority + 5);
        
        // 加载植被（低优先级）
        asyncLoader->load(cell.vegetationPath, [this, cell](void* data) {
            auto* vegetation = createVegetation(data);
            mLoadedCells[cell.id].vegetation = vegetation;
        }, nullptr, priority + 10);
        
        mLoadingCells.insert(cell.id);
    }
    
    void unloadDistantResources(const vec3& playerPos) {
        const float unloadDistance = 500.0f;
        
        for (auto it = mLoadedCells.begin(); it != mLoadedCells.end(); ) {
            float dist = distance(it->second.center, playerPos);
            
            if (dist > unloadDistance) {
                // 卸载资源
                destroyTerrain(it->second.terrain);
                destroyBuildings(it->second.buildings);
                destroyVegetation(it->second.vegetation);
                
                it = mLoadedCells.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    AsyncLoader* asyncLoader;
    std::unordered_map<int, LoadedCell> mLoadedCells;
    std::unordered_set<int> mLoadingCells;
};
```

---

## 10. 性能优化

### 10.1 批量上传

```cpp
class BatchUploader {
public:
    void addTexture(Texture* tex, void* data, size_t size) {
        mPendingUploads.push_back({tex, data, size});
        mPendingDataSize += size;
        
        // 达到批量大小，立即上传
        if (mPendingDataSize >= mBatchSize) {
            flush();
        }
    }
    
    void flush() {
        if (mPendingUploads.empty()) return;
        
        // 批量上传到 GPU
        for (auto& upload : mPendingUploads) {
            Texture::PixelBufferDescriptor buffer(upload.data, upload.size, ...);
            upload.texture->setImage(*mEngine, 0, std::move(buffer));
        }
        
        mPendingUploads.clear();
        mPendingDataSize = 0;
    }
    
private:
    struct PendingUpload {
        Texture* texture;
        void* data;
        size_t size;
    };
    
    std::vector<PendingUpload> mPendingUploads;
    size_t mPendingDataSize = 0;
    size_t mBatchSize = 16 * 1024 * 1024;  // 16MB
};
```

### 10.2 预加载

```cpp
class Preloader {
public:
    void preloadLevel(const std::string& levelName) {
        // 读取关卡配置
        auto config = loadLevelConfig(levelName);
        
        // 后台预加载所有资源
        for (const auto& texturePath : config.textures) {
            asyncLoader->load(texturePath, nullptr, nullptr, LoadPriority::Background);
        }
        
        for (const auto& modelPath : config.models) {
            asyncLoader->load(modelPath, nullptr, nullptr, LoadPriority::Background);
        }
        
        LOG_INFO("Preloading level: %s (%zu textures, %zu models)",
                levelName.c_str(), config.textures.size(), config.models.size());
    }
};
```

---

## 11. 调试工具

### 11.1 加载状态可视化

```cpp
class LoadingDebugger {
public:
    void drawDebugInfo() {
        // 显示正在加载的任务
        for (const auto& task : asyncLoader->getPendingTasks()) {
            drawText("Loading: %s (priority: %d)", task.path.c_str(), task.priority);
        }
        
        // 显示加载统计
        drawText("Active loads: %d", asyncLoader->getActiveLoads());
        drawText("Queued tasks: %d", asyncLoader->getQueuedTasks());
        drawText("Memory usage: %.2f MB", asyncLoader->getMemoryUsage() / 1024.0f / 1024.0f);
    }
};
```

---

## 12. 常见问题

### Q1: 异步加载后纹理显示为黑色？

**A**: 确保在主线程创建 Texture 并上传数据。Worker 线程只负责 I/O 和解码，GPU 对象创建必须在主线程或 Backend 线程。

### Q2: 加载大量小文件时性能差？

**A**: 考虑打包资源到单个文件（.pak），减少文件系统调用开销。

### Q3: 如何取消正在加载的任务？

**A**: 为每个任务分配唯一 ID，Worker 线程执行前检查任务是否被取消。

---

## 13. 相关文档

- [02-multithreading-design.md](./02-multithreading-design.md) - 多线程设计
- [08-resource-streaming.md](./08-resource-streaming.md) - 资源流式加载
- [05-large-project-structure.md](./05-large-project-structure.md) - 大型项目架构

---

## 14. 总结

异步加载是现代渲染引擎的必备特性：

1. **非阻塞**：主线程持续响应，用户体验流畅
2. **优先级调度**：重要资源优先加载
3. **渐进式显示**：先显示低精度，逐步提升
4. **内存可控**：限制并发和内存占用
5. **容错性**：超时、重试、错误回调

通过本文档的架构设计和实现细节，您可以为 Filament 项目构建一个健壮、高效的异步加载系统，支撑大规模、复杂场景的流畅运行。

**关键技术点**：
- 线程池和任务队列
- 优先级堆调度
- 主线程回调机制
- 占位符和 LOD 流式加载
- 内存预算和限流

异步加载不仅是技术实现，更是用户体验的保障。合理的加载策略能显著提升应用的响应性和流畅度。
