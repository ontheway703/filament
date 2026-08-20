# 07 - Scene Manager (场景和资源管理系统)

## 概述

本示例展示如何在 Filament 中构建一个完整的场景和资源管理系统。我们将实现实体管理、场景图层级、资源加载/卸载、内存管理、以及批量渲染优化等功能。

### 功能特性

- **实体管理** - Entity/Component 架构管理场景对象
- **场景图层级** - 父子关系和变换传播
- **资源加载** - 异步加载模型、纹理、材质
- **资源池** - 共享资源减少内存占用
- **内存管理** - 自动资源生命周期管理
- **批量渲染** - 实例化和合批优化
- **可见性剔除** - 视锥剔除和遮挡剔除
- **LOD 系统** - 多细节层次管理

### 适用场景

- 3D 游戏引擎
- 大型场景渲染
- 建筑可视化
- VR/AR 应用
- 模型查看器

### 技术要点

- 理解 ECS (Entity Component System) 架构
- 掌握 TransformManager 和 RenderableManager
- 实现资源引用计数和自动释放
- 优化大量对象的渲染性能
- 实现异步资源加载

---

## 完整实现 (C++)

### 1. 场景节点和场景图

**文件：`samples/scene_manager/SceneNode.h`**

```cpp
/*
 * 场景节点 - 场景图的基本单元
 *
 * 场景图是一个树形结构,每个节点包含：
 * - 局部变换（相对于父节点）
 * - 全局变换（世界空间）
 * - 子节点列表
 * - 可选的渲染组件
 */

#pragma once

#include <filament/Engine.h>
#include <utils/Entity.h>
#include <math/mat4.h>
#include <math/vec3.h>
#include <math/quat.h>

#include <string>
#include <vector>
#include <memory>

using namespace filament;
using namespace filament::math;
using namespace utils;

class SceneNode : public std::enable_shared_from_this<SceneNode> {
public:
    SceneNode(const std::string& name = "Node")
        : mName(name)
        , mParent(nullptr)
        , mLocalTransform(mat4())
        , mWorldTransform(mat4())
        , mEntity(EntityManager::get().create())
        , mDirty(true)
        , mVisible(true)
    {}

    virtual ~SceneNode() {
        // 清理所有子节点
        removeAllChildren();

        // 销毁实体
        if (mEntity) {
            EntityManager::get().destroy(mEntity);
        }
    }

    // 名称管理
    const std::string& getName() const { return mName; }
    void setName(const std::string& name) { mName = name; }

    // 实体访问
    Entity getEntity() const { return mEntity; }

    // 层级管理
    void addChild(std::shared_ptr<SceneNode> child) {
        if (!child || child.get() == this) return;

        // 从旧父节点移除
        if (child->mParent) {
            child->mParent->removeChild(child);
        }

        // 添加到子节点列表
        mChildren.push_back(child);
        child->mParent = this;
        child->markDirty();
    }

    void removeChild(std::shared_ptr<SceneNode> child) {
        auto it = std::find(mChildren.begin(), mChildren.end(), child);
        if (it != mChildren.end()) {
            (*it)->mParent = nullptr;
            mChildren.erase(it);
        }
    }

    void removeAllChildren() {
        for (auto& child : mChildren) {
            child->mParent = nullptr;
        }
        mChildren.clear();
    }

    SceneNode* getParent() const { return mParent; }
    const std::vector<std::shared_ptr<SceneNode>>& getChildren() const { return mChildren; }

    // 局部变换
    void setLocalTransform(const mat4& transform) {
        mLocalTransform = transform;
        markDirty();
    }

    const mat4& getLocalTransform() const { return mLocalTransform; }

    void setLocalPosition(const float3& position) {
        mLocalTransform[3].xyz = position;
        markDirty();
    }

    float3 getLocalPosition() const {
        return mLocalTransform[3].xyz;
    }

    void setLocalRotation(const quat& rotation) {
        mat4 rotMatrix = mat4::rotation(rotation);
        // 保持平移和缩放,只更新旋转
        float3 pos = mLocalTransform[3].xyz;
        float3 scale = getLocalScale();

        mLocalTransform = rotMatrix;
        mLocalTransform[0].xyz *= scale.x;
        mLocalTransform[1].xyz *= scale.y;
        mLocalTransform[2].xyz *= scale.z;
        mLocalTransform[3].xyz = pos;

        markDirty();
    }

    void setLocalScale(const float3& scale) {
        // 提取旋转矩阵并应用新缩放
        mat4 rotMatrix = mLocalTransform;
        rotMatrix[0].xyz = normalize(rotMatrix[0].xyz) * scale.x;
        rotMatrix[1].xyz = normalize(rotMatrix[1].xyz) * scale.y;
        rotMatrix[2].xyz = normalize(rotMatrix[2].xyz) * scale.z;

        mLocalTransform = rotMatrix;
        markDirty();
    }

    float3 getLocalScale() const {
        return float3(
            length(mLocalTransform[0].xyz),
            length(mLocalTransform[1].xyz),
            length(mLocalTransform[2].xyz)
        );
    }

    // 世界变换
    const mat4& getWorldTransform() {
        updateWorldTransform();
        return mWorldTransform;
    }

    float3 getWorldPosition() {
        updateWorldTransform();
        return mWorldTransform[3].xyz;
    }

    // 可见性
    void setVisible(bool visible) {
        if (mVisible != visible) {
            mVisible = visible;
            // 递归更新子节点
            for (auto& child : mChildren) {
                child->setVisible(visible);
            }
        }
    }

    bool isVisible() const { return mVisible; }

    // 遍历树
    void traverse(std::function<void(SceneNode*)> callback) {
        callback(this);
        for (auto& child : mChildren) {
            child->traverse(callback);
        }
    }

    // 查找节点
    std::shared_ptr<SceneNode> findByName(const std::string& name) {
        if (mName == name) {
            return shared_from_this();
        }

        for (auto& child : mChildren) {
            auto found = child->findByName(name);
            if (found) return found;
        }

        return nullptr;
    }

protected:
    void markDirty() {
        if (!mDirty) {
            mDirty = true;
            // 递归标记所有子节点为脏
            for (auto& child : mChildren) {
                child->markDirty();
            }
        }
    }

    void updateWorldTransform() {
        if (mDirty) {
            if (mParent) {
                mWorldTransform = mParent->getWorldTransform() * mLocalTransform;
            } else {
                mWorldTransform = mLocalTransform;
            }
            mDirty = false;
        }
    }

protected:
    std::string mName;
    SceneNode* mParent;
    std::vector<std::shared_ptr<SceneNode>> mChildren;

    mat4 mLocalTransform;
    mat4 mWorldTransform;
    Entity mEntity;

    bool mDirty;
    bool mVisible;
};
```

### 2. 资源管理器

**文件：`samples/scene_manager/ResourceManager.h`**

```cpp
/*
 * 资源管理器 - 管理模型、纹理、材质等资源
 *
 * 功能：
 * - 资源引用计数
 * - 资源缓存和共享
 * - 异步加载
 * - 自动释放
 */

#pragma once

#include <filament/Engine.h>
#include <filament/Material.h>
#include <filament/Texture.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/ResourceLoader.h>

#include <string>
#include <map>
#include <memory>
#include <functional>
#include <future>

using namespace filament;

// 资源句柄 - 使用引用计数自动管理资源生命周期
template<typename T>
class ResourceHandle {
public:
    ResourceHandle() : mResource(nullptr), mRefCount(nullptr) {}

    ResourceHandle(T* resource, std::function<void(T*)> deleter)
        : mResource(resource)
        , mRefCount(new size_t(1))
        , mDeleter(deleter)
    {}

    ResourceHandle(const ResourceHandle& other)
        : mResource(other.mResource)
        , mRefCount(other.mRefCount)
        , mDeleter(other.mDeleter)
    {
        if (mRefCount) {
            (*mRefCount)++;
        }
    }

    ResourceHandle& operator=(const ResourceHandle& other) {
        if (this != &other) {
            release();
            mResource = other.mResource;
            mRefCount = other.mRefCount;
            mDeleter = other.mDeleter;
            if (mRefCount) {
                (*mRefCount)++;
            }
        }
        return *this;
    }

    ~ResourceHandle() {
        release();
    }

    T* get() const { return mResource; }
    T* operator->() const { return mResource; }
    operator bool() const { return mResource != nullptr; }

    size_t getRefCount() const {
        return mRefCount ? *mRefCount : 0;
    }

private:
    void release() {
        if (mRefCount && --(*mRefCount) == 0) {
            if (mDeleter && mResource) {
                mDeleter(mResource);
            }
            delete mRefCount;
        }
        mResource = nullptr;
        mRefCount = nullptr;
    }

    T* mResource;
    size_t* mRefCount;
    std::function<void(T*)> mDeleter;
};

// 资源管理器
class ResourceManager {
public:
    ResourceManager(Engine* engine)
        : mEngine(engine)
        , mAssetLoader(gltfio::AssetLoader::create({engine}))
        , mResourceLoader(new gltfio::ResourceLoader({
            .engine = engine,
            .normalizeSkinningWeights = true
        }))
    {}

    ~ResourceManager() {
        // 清理所有资源
        clearAll();

        if (mResourceLoader) {
            delete mResourceLoader;
        }

        if (mAssetLoader) {
            gltfio::AssetLoader::destroy(&mAssetLoader);
        }
    }

    // 加载 glTF 模型（同步）
    ResourceHandle<gltfio::FilamentAsset> loadModel(const std::string& path) {
        // 检查缓存
        auto it = mModelCache.find(path);
        if (it != mModelCache.end()) {
            return it->second;
        }

        // 读取文件
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open model: " << path << std::endl;
            return ResourceHandle<gltfio::FilamentAsset>();
        }

        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(size);
        file.read(reinterpret_cast<char*>(buffer.data()), size);
        file.close();

        // 创建资产
        auto* asset = mAssetLoader->createAsset(buffer.data(), buffer.size());
        if (!asset) {
            std::cerr << "Failed to create asset: " << path << std::endl;
            return ResourceHandle<gltfio::FilamentAsset>();
        }

        // 加载资源
        mResourceLoader->asyncBeginLoad(asset);
        while (mResourceLoader->asyncGetLoadProgress() < 1.0f) {
            mResourceLoader->asyncUpdateLoad();
        }

        // 创建句柄
        auto deleter = [this](gltfio::FilamentAsset* a) {
            if (a) {
                mAssetLoader->destroyAsset(a);
            }
        };

        ResourceHandle<gltfio::FilamentAsset> handle(asset, deleter);
        mModelCache[path] = handle;

        std::cout << "Loaded model: " << path << " (ref count: "
                  << handle.getRefCount() << ")" << std::endl;

        return handle;
    }

    // 异步加载模型
    std::future<ResourceHandle<gltfio::FilamentAsset>>
    loadModelAsync(const std::string& path) {
        return std::async(std::launch::async, [this, path]() {
            return loadModel(path);
        });
    }

    // 加载材质（从编译的 .filamat 文件）
    ResourceHandle<Material> loadMaterial(const std::string& path) {
        auto it = mMaterialCache.find(path);
        if (it != mMaterialCache.end()) {
            return it->second;
        }

        // 读取材质文件
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open material: " << path << std::endl;
            return ResourceHandle<Material>();
        }

        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(size);
        file.read(reinterpret_cast<char*>(buffer.data()), size);
        file.close();

        // 创建材质
        auto* material = Material::Builder()
            .package(buffer.data(), buffer.size())
            .build(*mEngine);

        if (!material) {
            std::cerr << "Failed to create material: " << path << std::endl;
            return ResourceHandle<Material>();
        }

        auto deleter = [this](Material* m) {
            if (m) {
                mEngine->destroy(m);
            }
        };

        ResourceHandle<Material> handle(material, deleter);
        mMaterialCache[path] = handle;

        return handle;
    }

    // 加载纹理
    ResourceHandle<Texture> loadTexture(const std::string& path) {
        auto it = mTextureCache.find(path);
        if (it != mTextureCache.end()) {
            return it->second;
        }

        // 这里简化处理,实际应该使用图像加载库
        // 例如 stb_image, libpng 等

        std::cout << "Loading texture: " << path << std::endl;

        // 创建简单的占位纹理
        auto* texture = Texture::Builder()
            .width(256)
            .height(256)
            .levels(1)
            .format(Texture::InternalFormat::RGBA8)
            .build(*mEngine);

        auto deleter = [this](Texture* t) {
            if (t) {
                mEngine->destroy(t);
            }
        };

        ResourceHandle<Texture> handle(texture, deleter);
        mTextureCache[path] = handle;

        return handle;
    }

    // 获取缓存统计
    struct CacheStats {
        size_t modelCount;
        size_t materialCount;
        size_t textureCount;
    };

    CacheStats getCacheStats() const {
        return {
            mModelCache.size(),
            mMaterialCache.size(),
            mTextureCache.size()
        };
    }

    // 清理未使用的资源
    void cleanupUnused() {
        // 清理只有一个引用（缓存本身）的资源
        auto cleanCache = [](auto& cache) {
            for (auto it = cache.begin(); it != cache.end();) {
                if (it->second.getRefCount() <= 1) {
                    std::cout << "Cleaning up: " << it->first << std::endl;
                    it = cache.erase(it);
                } else {
                    ++it;
                }
            }
        };

        cleanCache(mModelCache);
        cleanCache(mMaterialCache);
        cleanCache(mTextureCache);
    }

    // 清理所有资源
    void clearAll() {
        mModelCache.clear();
        mMaterialCache.clear();
        mTextureCache.clear();
    }

private:
    Engine* mEngine;
    gltfio::AssetLoader* mAssetLoader;
    gltfio::ResourceLoader* mResourceLoader;

    std::map<std::string, ResourceHandle<gltfio::FilamentAsset>> mModelCache;
    std::map<std::string, ResourceHandle<Material>> mMaterialCache;
    std::map<std::string, ResourceHandle<Texture>> mTextureCache;
};
```

### 3. 场景管理器主类

**文件：`samples/scene_manager/SceneManager.h`**

```cpp
/*
 * 场景管理器 - 管理整个 3D 场景
 *
 * 功能：
 * - 场景图管理
 * - 实体添加/删除
 * - 批量渲染
 * - 可见性剔除
 */

#pragma once

#include "SceneNode.h"
#include "ResourceManager.h"

#include <filament/Scene.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>

#include <memory>
#include <vector>

class SceneManager {
public:
    SceneManager(Engine* engine, Scene* scene)
        : mEngine(engine)
        , mScene(scene)
        , mResourceManager(std::make_unique<ResourceManager>(engine))
        , mRootNode(std::make_shared<SceneNode>("Root"))
    {}

    ~SceneManager() {
        clear();
    }

    // 场景图访问
    std::shared_ptr<SceneNode> getRootNode() const { return mRootNode; }

    // 创建节点
    std::shared_ptr<SceneNode> createNode(const std::string& name = "Node") {
        return std::make_shared<SceneNode>(name);
    }

    // 添加模型到场景
    std::shared_ptr<SceneNode> addModel(const std::string& path,
                                       const std::string& nodeName = "") {
        // 加载模型
        auto modelHandle = mResourceManager->loadModel(path);
        if (!modelHandle) {
            return nullptr;
        }

        auto* asset = modelHandle.get();

        // 创建场景节点
        std::string name = nodeName.empty() ? path : nodeName;
        auto node = std::make_shared<SceneNode>(name);

        // 添加实体到场景
        mScene->addEntities(asset->getEntities(), asset->getEntityCount());

        // 保存资源句柄
        mLoadedModels[node.get()] = modelHandle;

        // 添加到根节点
        mRootNode->addChild(node);

        std::cout << "Added model to scene: " << name << std::endl;
        return node;
    }

    // 移除节点
    void removeNode(std::shared_ptr<SceneNode> node) {
        if (!node) return;

        // 从场景中移除实体
        auto it = mLoadedModels.find(node.get());
        if (it != mLoadedModels.end()) {
            auto* asset = it->second.get();
            mScene->removeEntities(asset->getEntities(), asset->getEntityCount());
            mLoadedModels.erase(it);
        }

        // 从场景图中移除
        if (node->getParent()) {
            node->getParent()->removeChild(node);
        }
    }

    // 更新场景（每帧调用）
    void update(float deltaTime) {
        // 更新所有节点的世界变换
        updateTransforms();

        // 更新 Filament TransformManager
        syncTransformsToFilament();
    }

    // 查找节点
    std::shared_ptr<SceneNode> findNode(const std::string& name) {
        return mRootNode->findByName(name);
    }

    // 遍历场景
    void traverseScene(std::function<void(SceneNode*)> callback) {
        mRootNode->traverse(callback);
    }

    // 获取资源管理器
    ResourceManager* getResourceManager() { return mResourceManager.get(); }

    // 清空场景
    void clear() {
        // 移除所有模型
        for (auto& pair : mLoadedModels) {
            auto* asset = pair.second.get();
            mScene->removeEntities(asset->getEntities(), asset->getEntityCount());
        }
        mLoadedModels.clear();

        // 清空场景图
        mRootNode->removeAllChildren();

        // 清理资源
        mResourceManager->clearAll();
    }

    // 统计信息
    struct SceneStats {
        size_t nodeCount;
        size_t entityCount;
        ResourceManager::CacheStats resourceStats;
    };

    SceneStats getStats() const {
        SceneStats stats;
        stats.nodeCount = countNodes(mRootNode.get());
        stats.entityCount = mLoadedModels.size();
        stats.resourceStats = mResourceManager->getCacheStats();
        return stats;
    }

private:
    void updateTransforms() {
        // 递归更新所有节点的世界变换
        mRootNode->getWorldTransform();  // 触发递归更新
    }

    void syncTransformsToFilament() {
        auto& tcm = mEngine->getTransformManager();

        // 遍历所有节点,同步变换到 Filament
        traverseScene([&](SceneNode* node) {
            auto entity = node->getEntity();
            auto instance = tcm.getInstance(entity);

            if (instance.isValid()) {
                tcm.setTransform(instance, node->getWorldTransform());
            } else {
                // 如果实体还没有 Transform 组件,创建一个
                tcm.create(entity);
                auto newInstance = tcm.getInstance(entity);
                tcm.setTransform(newInstance, node->getWorldTransform());
            }
        });
    }

    static size_t countNodes(const SceneNode* node) {
        size_t count = 1;
        for (const auto& child : node->getChildren()) {
            count += countNodes(child.get());
        }
        return count;
    }

private:
    Engine* mEngine;
    Scene* mScene;
    std::unique_ptr<ResourceManager> mResourceManager;
    std::shared_ptr<SceneNode> mRootNode;

    // 节点到模型的映射
    std::map<SceneNode*, ResourceHandle<gltfio::FilamentAsset>> mLoadedModels;
};
```

### 4. 主应用示例

**文件：`samples/scene_manager_demo.cpp`**

```cpp
/*
 * 场景管理器示例应用
 *
 * 展示如何使用场景管理器构建复杂场景
 */

#include "SceneNode.h"
#include "ResourceManager.h"
#include "SceneManager.h"

#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/Renderer.h>
#include <filament/SwapChain.h>
#include <filament/LightManager.h>

#include <filamentapp/FilamentApp.h>
#include <filamentapp/Config.h>

#include <iostream>
#include <memory>

using namespace filament;
using namespace filament::math;

class SceneManagerApp {
public:
    SceneManagerApp() = default;
    ~SceneManagerApp() { cleanup(); }

    bool initialize(Engine* engine, View* view, Scene* scene) {
        mEngine = engine;
        mView = view;
        mScene = scene;

        // 创建场景管理器
        mSceneManager = std::make_unique<SceneManager>(engine, scene);

        // 设置相机
        setupCamera();

        // 设置光照
        setupLighting();

        // 构建示例场景
        buildDemoScene();

        std::cout << "Scene Manager App initialized" << std::endl;
        return true;
    }

    void update(float deltaTime) {
        // 更新场景
        mSceneManager->update(deltaTime);

        // 动画示例：旋转某些对象
        static float angle = 0.0f;
        angle += deltaTime;

        auto rotatingNode = mSceneManager->findNode("RotatingObject");
        if (rotatingNode) {
            quat rotation = quat::fromAxisAngle(float3{0, 1, 0}, angle);
            rotatingNode->setLocalRotation(rotation);
        }

        // 定期打印统计信息
        static float statsTimer = 0.0f;
        statsTimer += deltaTime;
        if (statsTimer > 5.0f) {
            printStats();
            statsTimer = 0.0f;
        }
    }

    void cleanup() {
        mSceneManager.reset();
        std::cout << "Scene Manager App cleaned up" << std::endl;
    }

private:
    void setupCamera() {
        Camera& camera = mView->getCamera();
        camera.setProjection(45.0, 16.0f/9.0f, 0.1, 100.0);

        float3 eye(0, 5, 10);
        float3 center(0, 0, 0);
        float3 up(0, 1, 0);

        camera.lookAt(eye, center, up);
    }

    void setupLighting() {
        // 太阳光
        Entity sun = EntityManager::get().create();
        LightManager::Builder(LightManager::Type::SUN)
            .color(Color::toLinear<ACCURATE>({1.0f, 1.0f, 1.0f}))
            .intensity(100000.0f)
            .direction({0.6f, -1.0f, -0.8f})
            .castShadows(true)
            .build(*mEngine, sun);

        mScene->addEntity(sun);

        // 环境光
        Entity ambient = EntityManager::get().create();
        LightManager::Builder(LightManager::Type::DIRECTIONAL)
            .color(Color::toLinear<ACCURATE>({0.5f, 0.5f, 0.6f}))
            .intensity(20000.0f)
            .direction({0, -1, 0})
            .build(*mEngine, ambient);

        mScene->addEntity(ambient);
    }

    void buildDemoScene() {
        // 创建场景层级结构
        // Root
        //  ├─ Ground
        //  ├─ Building1
        //  │   ├─ Door
        //  │   └─ Window
        //  └─ Character
        //      ├─ Body
        //      ├─ Head
        //      └─ Weapon

        // 地面
        auto ground = mSceneManager->addModel("assets/models/ground.glb", "Ground");
        if (ground) {
            ground->setLocalPosition(float3{0, 0, 0});
            ground->setLocalScale(float3{10, 1, 10});
        }

        // 建筑
        auto building = mSceneManager->createNode("Building1");
        building->setLocalPosition(float3{5, 0, 0});
        mSceneManager->getRootNode()->addChild(building);

        // 建筑的门（作为建筑的子节点）
        auto door = mSceneManager->addModel("assets/models/door.glb", "Door");
        if (door) {
            door->setLocalPosition(float3{0, 0, 2});  // 相对于建筑的位置
            building->addChild(door);
        }

        // 角色
        auto character = mSceneManager->addModel(
            "assets/models/character.glb", "Character");
        if (character) {
            character->setLocalPosition(float3{-3, 0, 0});
        }

        // 旋转对象示例
        auto rotatingObj = mSceneManager->addModel(
            "assets/models/cube.glb", "RotatingObject");
        if (rotatingObj) {
            rotatingObj->setLocalPosition(float3{0, 2, -5});
        }

        // 创建对象组
        auto objectGroup = mSceneManager->createNode("ObjectGroup");
        objectGroup->setLocalPosition(float3{-5, 0, -5});
        mSceneManager->getRootNode()->addChild(objectGroup);

        // 在组中添加多个对象
        for (int i = 0; i < 5; ++i) {
            auto obj = mSceneManager->addModel(
                "assets/models/cube.glb",
                "Cube_" + std::to_string(i));
            if (obj) {
                obj->setLocalPosition(float3{i * 2.0f, 0, 0});
                objectGroup->addChild(obj);
            }
        }

        std::cout << "Demo scene built successfully" << std::endl;
        printStats();
    }

    void printStats() {
        auto stats = mSceneManager->getStats();
        std::cout << "\n=== Scene Statistics ===" << std::endl;
        std::cout << "Nodes: " << stats.nodeCount << std::endl;
        std::cout << "Loaded Models: " << stats.entityCount << std::endl;
        std::cout << "Cached Models: " << stats.resourceStats.modelCount << std::endl;
        std::cout << "Cached Materials: " << stats.resourceStats.materialCount << std::endl;
        std::cout << "Cached Textures: " << stats.resourceStats.textureCount << std::endl;
        std::cout << "======================\n" << std::endl;
    }

private:
    Engine* mEngine = nullptr;
    View* mView = nullptr;
    Scene* mScene = nullptr;
    std::unique_ptr<SceneManager> mSceneManager;
};

// 全局应用实例
static SceneManagerApp* g_app = nullptr;

void setup(Engine* engine, View* view, Scene* scene) {
    g_app = new SceneManagerApp();
    if (!g_app->initialize(engine, view, scene)) {
        std::cerr << "Failed to initialize app" << std::endl;
        delete g_app;
        g_app = nullptr;
    }
}

void animate(Engine* engine, View* view, double now) {
    static double lastTime = 0.0;
    double deltaTime = now - lastTime;
    lastTime = now;

    if (g_app) {
        g_app->update(deltaTime);
    }
}

void cleanup(Engine* engine, View* view, Scene* scene) {
    if (g_app) {
        g_app->cleanup();
        delete g_app;
        g_app = nullptr;
    }
}

int main(int argc, char** argv) {
    Config config;
    config.title = "Filament Scene Manager Demo";
    config.backend = Engine::Backend::DEFAULT;

    FilamentApp::get().run(config, setup, cleanup, nullptr, animate);

    return 0;
}
```

---

## Android 实现 (Kotlin)

**文件：`android/app/src/main/java/com/example/SceneManagerActivity.kt`**

```kotlin
package com.example.filament.samples

import android.app.Activity
import android.os.Bundle
import android.view.Choreographer
import android.view.SurfaceView
import android.widget.Button
import android.widget.TextView
import com.google.android.filament.*
import com.google.android.filament.gltfio.*
import com.google.android.filament.utils.*
import java.nio.ByteBuffer

/**
 * 场景管理器示例 - Android 实现
 *
 * 展示如何在 Android 上使用场景管理系统
 */
class SceneManagerActivity : Activity() {

    // Filament 组件
    private lateinit var surfaceView: SurfaceView
    private lateinit var choreographer: Choreographer
    private lateinit var modelViewer: ModelViewer

    // UI 组件
    private lateinit var addObjectButton: Button
    private lateinit var removeObjectButton: Button
    private lateinit var statsTextView: TextView

    // 资源管理
    private var assetLoader: AssetLoader? = null
    private var resourceLoader: ResourceLoader? = null

    // 场景对象
    private val sceneObjects = mutableListOf<SceneObject>()
    private var nextObjectId = 0

    data class SceneObject(
        val id: Int,
        val name: String,
        val asset: FilamentAsset,
        var position: FloatArray,
        var rotation: Float = 0f
    )

    companion object {
        init {
            Utils.init()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_scene_manager)

        surfaceView = findViewById(R.id.surfaceView)
        addObjectButton = findViewById(R.id.addObjectButton)
        removeObjectButton = findViewById(R.id.removeObjectButton)
        statsTextView = findViewById(R.id.statsTextView)

        choreographer = Choreographer.getInstance()
        modelViewer = ModelViewer(surfaceView)

        // 初始化
        setupCamera()
        setupLighting()
        initializeResourceLoaders()

        // UI 事件
        addObjectButton.setOnClickListener { addRandomObject() }
        removeObjectButton.setOnClickListener { removeLastObject() }

        // 启动渲染循环
        choreographer.postFrameCallback(frameCallback)
    }

    private fun setupCamera() {
        modelViewer.view.camera.apply {
            setProjection(45.0,
                         surfaceView.width.toDouble() / surfaceView.height,
                         0.1, 100.0, Camera.Fov.VERTICAL)
            lookAt(
                0.0, 5.0, 10.0,  // eye
                0.0, 0.0, 0.0,   // center
                0.0, 1.0, 0.0    // up
            )
        }
    }

    private fun setupLighting() {
        // 添加太阳光
        val sun = EntityManager.get().create()
        LightManager.Builder(LightManager.Type.SUN)
            .color(1.0f, 1.0f, 1.0f)
            .intensity(100000.0f)
            .direction(0.6f, -1.0f, -0.8f)
            .castShadows(true)
            .build(modelViewer.engine, sun)

        modelViewer.scene.addEntity(sun)

        // IBL
        modelViewer.scene.indirectLight = IndirectLight.Builder()
            .intensity(30000.0f)
            .build(modelViewer.engine)
    }

    private fun initializeResourceLoaders() {
        assetLoader = AssetLoader(
            modelViewer.engine,
            MaterialProvider(modelViewer.engine),
            EntityManager.get()
        )

        resourceLoader = ResourceLoader(
            modelViewer.engine,
            normalizeSkinningWeights = true
        )
    }

    private fun addRandomObject() {
        // 加载模型（这里简化为加载同一个模型）
        val buffer = readAsset("models/cube.glb")
        val asset = assetLoader?.createAsset(buffer) ?: return

        // 加载资源
        resourceLoader?.loadResources(asset)

        // 添加到场景
        modelViewer.scene.addEntities(asset.entities)

        // 随机位置
        val x = (Math.random() * 10 - 5).toFloat()
        val z = (Math.random() * 10 - 5).toFloat()
        val position = floatArrayOf(x, 0f, z)

        // 创建场景对象
        val sceneObj = SceneObject(
            id = nextObjectId++,
            name = "Object_${sceneObj.id}",
            asset = asset,
            position = position
        )

        sceneObjects.add(sceneObj)

        // 更新变换
        updateObjectTransform(sceneObj)

        updateStats()
    }

    private fun removeLastObject() {
        if (sceneObjects.isEmpty()) return

        val obj = sceneObjects.removeLast()

        // 从场景移除
        modelViewer.scene.removeEntities(obj.asset.entities)

        // 销毁资产
        assetLoader?.destroyAsset(obj.asset)

        updateStats()
    }

    private fun updateObjectTransform(obj: SceneObject) {
        val tcm = modelViewer.engine.transformManager

        // 为每个实体更新变换
        obj.asset.entities.forEach { entity ->
            val instance = tcm.getInstance(entity)
            if (instance.isValid) {
                // 创建变换矩阵
                val transform = FloatArray(16)
                android.opengl.Matrix.setIdentityM(transform, 0)

                // 应用旋转
                android.opengl.Matrix.rotateM(transform, 0,
                                             obj.rotation, 0f, 1f, 0f)

                // 应用位置
                android.opengl.Matrix.translateM(transform, 0,
                                                obj.position[0],
                                                obj.position[1],
                                                obj.position[2])

                tcm.setTransform(instance, transform)
            }
        }
    }

    private fun updateStats() {
        val stats = """
            Objects: ${sceneObjects.size}
            Entities: ${sceneObjects.sumOf { it.asset.entityCount }}
            Loader Progress: ${resourceLoader?.asyncGetLoadProgress() ?: 0f}
        """.trimIndent()

        runOnUiThread {
            statsTextView.text = stats
        }
    }

    private val frameCallback = object : Choreographer.FrameCallback {
        private var lastFrameTime = 0L

        override fun doFrame(frameTimeNanos: Long) {
            val deltaTime = if (lastFrameTime != 0L) {
                (frameTimeNanos - lastFrameTime) / 1_000_000_000.0f
            } else {
                0f
            }
            lastFrameTime = frameTimeNanos

            // 更新场景
            updateScene(deltaTime)

            // 渲染
            modelViewer.render(frameTimeNanos)

            // 下一帧
            choreographer.postFrameCallback(this)
        }
    }

    private fun updateScene(deltaTime: Float) {
        // 旋转所有对象
        sceneObjects.forEach { obj ->
            obj.rotation += deltaTime * 30f  // 30 度/秒
            updateObjectTransform(obj)
        }
    }

    private fun readAsset(assetName: String): ByteBuffer {
        val input = assets.open(assetName)
        val bytes = ByteArray(input.available())
        input.read(bytes)
        input.close()
        return ByteBuffer.wrap(bytes)
    }

    override fun onResume() {
        super.onResume()
        choreographer.postFrameCallback(frameCallback)
    }

    override fun onPause() {
        super.onPause()
        choreographer.removeFrameCallback(frameCallback)
    }

    override fun onDestroy() {
        super.onDestroy()

        choreographer.removeFrameCallback(frameCallback)

        // 清理资源
        sceneObjects.forEach { obj ->
            assetLoader?.destroyAsset(obj.asset)
        }
        sceneObjects.clear()

        assetLoader?.destroy()
        resourceLoader?.destroy()

        modelViewer.destroy()
    }
}
```

**布局文件：`res/layout/activity_scene_manager.xml`**

```xml
<?xml version="1.0" encoding="utf-8"?>
<LinearLayout xmlns:android="http://schemas.android.com/apk/res/android"
    android:layout_width="match_parent"
    android:layout_height="match_parent"
    android:orientation="vertical">

    <SurfaceView
        android:id="@+id/surfaceView"
        android:layout_width="match_parent"
        android:layout_height="0dp"
        android:layout_weight="1" />

    <LinearLayout
        android:layout_width="match_parent"
        android:layout_height="wrap_content"
        android:orientation="vertical"
        android:padding="16dp"
        android:background="#f0f0f0">

        <TextView
            android:id="@+id/statsTextView"
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:text="Stats"
            android:textSize="12sp"
            android:fontFamily="monospace"
            android:layout_marginBottom="8dp" />

        <LinearLayout
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:orientation="horizontal">

            <Button
                android:id="@+id/addObjectButton"
                android:layout_width="0dp"
                android:layout_height="wrap_content"
                android:layout_weight="1"
                android:text="Add Object"
                android:layout_marginEnd="4dp" />

            <Button
                android:id="@+id/removeObjectButton"
                android:layout_width="0dp"
                android:layout_height="wrap_content"
                android:layout_weight="1"
                android:text="Remove Object"
                android:layout_marginStart="4dp" />
        </LinearLayout>
    </LinearLayout>
</LinearLayout>
```

---

## 性能优化

### 1. 批量渲染和实例化

```cpp
/*
 * 实例化渲染 - 高效渲染大量相同对象
 */

class InstancedRenderer {
public:
    InstancedRenderer(Engine* engine, Scene* scene)
        : mEngine(engine), mScene(scene) {}

    // 添加实例
    void addInstance(const mat4& transform) {
        mTransforms.push_back(transform);
        mDirty = true;
    }

    // 构建实例化数据
    void build(gltfio::FilamentAsset* baseAsset) {
        if (!mDirty) return;

        // 获取基础网格的渲染实体
        auto& rcm = mEngine->getRenderableManager();
        auto& tcm = mEngine->getTransformManager();

        // 为每个实例创建实体
        mInstances.resize(mTransforms.size());

        for (size_t i = 0; i < mTransforms.size(); ++i) {
            if (!mInstances[i]) {
                mInstances[i] = EntityManager::get().create();
            }

            Entity entity = mInstances[i];

            // 复制渲染组件（共享几何体和材质）
            // 注意：实际实现需要更复杂的逻辑

            // 设置变换
            auto tcmInstance = tcm.getInstance(entity);
            if (!tcmInstance.isValid()) {
                tcm.create(entity);
                tcmInstance = tcm.getInstance(entity);
            }

            tcm.setTransform(tcmInstance, mTransforms[i]);

            // 添加到场景
            mScene->addEntity(entity);
        }

        mDirty = false;
    }

    void clear() {
        for (auto entity : mInstances) {
            mScene->removeEntity(entity);
            EntityManager::get().destroy(entity);
        }
        mInstances.clear();
        mTransforms.clear();
    }

private:
    Engine* mEngine;
    Scene* mScene;
    std::vector<mat4> mTransforms;
    std::vector<Entity> mInstances;
    bool mDirty = false;
};

// 使用示例：渲染森林
void createForest(SceneManager* sceneMgr) {
    InstancedRenderer treeRenderer(engine, scene);

    // 添加 1000 棵树
    for (int i = 0; i < 1000; ++i) {
        float x = (rand() % 1000 - 500) / 10.0f;
        float z = (rand() % 1000 - 500) / 10.0f;
        float rotation = (rand() % 360) * M_PI / 180.0f;

        mat4 transform = mat4::translation(float3{x, 0, z}) *
                        mat4::rotation(rotation, float3{0, 1, 0});

        treeRenderer.addInstance(transform);
    }

    auto treeModel = sceneMgr->getResourceManager()->loadModel("tree.glb");
    treeRenderer.build(treeModel.get());
}
```

### 2. 视锥剔除

```cpp
/*
 * 视锥剔除 - 只渲染相机可见的对象
 */

#include <filament/Frustum.h>
#include <filament/Box.h>

class FrustumCuller {
public:
    static bool isVisible(const Frustum& frustum, const Box& aabb,
                         const mat4& transform) {
        // 将 AABB 变换到世界空间
        Box worldAABB = aabb.transform(transform);

        // 测试与视锥体的相交
        return frustum.intersects(worldAABB);
    }

    // 剔除场景节点
    static void cullScene(SceneNode* root, const Frustum& frustum,
                         std::vector<SceneNode*>& visible) {
        // 简化实现：假设每个节点都有边界盒
        // 实际应用中需要计算或缓存 AABB

        root->traverse([&](SceneNode* node) {
            // 获取节点的 AABB (需要实现)
            // Box aabb = node->getAABB();

            // if (isVisible(frustum, aabb, node->getWorldTransform())) {
            //     visible.push_back(node);
            // }

            // 简化版本：直接添加所有节点
            visible.push_back(node);
        });
    }
};

// 使用示例
void renderSceneWithCulling(Camera& camera, SceneManager* sceneMgr) {
    // 获取相机视锥体
    Frustum frustum = camera.getFrustum();

    // 剔除不可见对象
    std::vector<SceneNode*> visibleNodes;
    FrustumCuller::cullScene(sceneMgr->getRootNode().get(),
                            frustum, visibleNodes);

    std::cout << "Visible nodes: " << visibleNodes.size() << std::endl;
}
```

### 3. LOD (Level of Detail) 系统

```cpp
/*
 * LOD 系统 - 根据距离使用不同细节的模型
 */

class LODSystem {
public:
    struct LODLevel {
        float distance;                    // 切换距离
        ResourceHandle<gltfio::FilamentAsset> model;
    };

    struct LODObject {
        std::string name;
        float3 position;
        std::vector<LODLevel> levels;
        size_t currentLevel = 0;
    };

    void addObject(const std::string& name, const float3& pos) {
        LODObject obj;
        obj.name = name;
        obj.position = pos;
        mObjects.push_back(obj);
    }

    void addLODLevel(size_t objectIndex, float distance,
                    ResourceHandle<gltfio::FilamentAsset> model) {
        if (objectIndex < mObjects.size()) {
            mObjects[objectIndex].levels.push_back({distance, model});
        }
    }

    void update(const float3& cameraPos, Scene* scene) {
        for (auto& obj : mObjects) {
            float dist = length(obj.position - cameraPos);

            // 找到合适的 LOD 级别
            size_t newLevel = 0;
            for (size_t i = 0; i < obj.levels.size(); ++i) {
                if (dist > obj.levels[i].distance) {
                    newLevel = i;
                } else {
                    break;
                }
            }

            // 如果 LOD 级别改变,切换模型
            if (newLevel != obj.currentLevel) {
                switchLOD(obj, newLevel, scene);
            }
        }
    }

private:
    void switchLOD(LODObject& obj, size_t newLevel, Scene* scene) {
        // 移除当前级别的模型
        if (obj.currentLevel < obj.levels.size()) {
            auto* oldAsset = obj.levels[obj.currentLevel].model.get();
            scene->removeEntities(oldAsset->getEntities(),
                                oldAsset->getEntityCount());
        }

        // 添加新级别的模型
        auto* newAsset = obj.levels[newLevel].model.get();
        scene->addEntities(newAsset->getEntities(),
                          newAsset->getEntityCount());

        obj.currentLevel = newLevel;

        std::cout << "Switched " << obj.name << " to LOD level "
                  << newLevel << std::endl;
    }

    std::vector<LODObject> mObjects;
};

// 使用示例
void setupLODScene(ResourceManager* resMgr, LODSystem& lodSys) {
    lodSys.addObject("Tree_01", float3{10, 0, 0});

    // 添加 3 个 LOD 级别
    auto lod0 = resMgr->loadModel("tree_high.glb");   // 高质量
    auto lod1 = resMgr->loadModel("tree_medium.glb"); // 中等质量
    auto lod2 = resMgr->loadModel("tree_low.glb");    // 低质量

    lodSys.addLODLevel(0, 0.0f, lod0);    // 0-20m
    lodSys.addLODLevel(0, 20.0f, lod1);   // 20-50m
    lodSys.addLODLevel(0, 50.0f, lod2);   // 50m+
}
```

---

## 常见问题

### Q1: 如何正确管理资源生命周期?

**A:** 使用引用计数智能指针:

```cpp
// 使用 ResourceHandle 自动管理
ResourceHandle<Material> material = resourceMgr->loadMaterial("pbr.filamat");

// 当所有 ResourceHandle 销毁时,资源自动释放
{
    auto mat1 = material;  // ref count = 2
    auto mat2 = material;  // ref count = 3
}  // mat1 和 mat2 销毁, ref count = 1

// material 销毁时, ref count = 0, 资源被释放
```

### Q2: 场景图变换更新性能差?

**A:** 使用脏标记优化:

```cpp
// 只在变换改变时才更新
void SceneNode::setLocalPosition(const float3& pos) {
    if (mLocalTransform[3].xyz != pos) {
        mLocalTransform[3].xyz = pos;
        markDirty();  // 只标记,不立即计算
    }
}

// 批量更新
void SceneManager::update(float dt) {
    // 一次性更新所有脏节点
    updateTransforms();
}
```

### Q3: 大量对象导致内存占用高?

**A:** 使用实例化和资源共享:

```cpp
// 错误：每个对象加载独立的模型
for (int i = 0; i < 1000; ++i) {
    auto model = resourceMgr->loadModel("cube.glb");  // 加载 1000 次!
}

// 正确：共享同一个模型
auto sharedModel = resourceMgr->loadModel("cube.glb");  // 只加载一次
for (int i = 0; i < 1000; ++i) {
    auto instance = sharedModel.get()->createInstance();  // 创建实例
}
```

### Q4: 如何实现场景序列化?

**A:** 实现场景保存和加载:

```cpp
#include <nlohmann/json.hpp>  // 使用 JSON 库

class SceneSerializer {
public:
    static nlohmann::json saveScene(const SceneNode* root) {
        nlohmann::json j;
        j["name"] = root->getName();

        // 保存变换
        const auto& transform = root->getLocalTransform();
        j["transform"] = {
            transform[0][0], transform[0][1], /* ... */
        };

        // 保存子节点
        j["children"] = nlohmann::json::array();
        for (const auto& child : root->getChildren()) {
            j["children"].push_back(saveScene(child.get()));
        }

        return j;
    }

    static std::shared_ptr<SceneNode> loadScene(
        const nlohmann::json& j,
        SceneManager* sceneMgr)
    {
        auto node = sceneMgr->createNode(j["name"]);

        // 加载变换
        mat4 transform;
        auto t = j["transform"];
        // 解析矩阵...

        node->setLocalTransform(transform);

        // 加载子节点
        for (const auto& childJson : j["children"]) {
            auto child = loadScene(childJson, sceneMgr);
            node->addChild(child);
        }

        return node;
    }
};
```

### Q5: 如何调试场景图层级?

**A:** 实现场景图可视化:

```cpp
void printSceneHierarchy(const SceneNode* node, int depth = 0) {
    std::string indent(depth * 2, ' ');

    std::cout << indent << "├─ " << node->getName();

    auto pos = node->getLocalPosition();
    std::cout << " [" << pos.x << ", " << pos.y << ", " << pos.z << "]";

    std::cout << " (children: " << node->getChildren().size() << ")"
              << std::endl;

    for (const auto& child : node->getChildren()) {
        printSceneHierarchy(child.get(), depth + 1);
    }
}

// 使用
printSceneHierarchy(sceneManager->getRootNode().get());

// 输出:
// ├─ Root [0, 0, 0] (children: 3)
//   ├─ Ground [0, 0, 0] (children: 0)
//   ├─ Building [5, 0, 0] (children: 2)
//     ├─ Door [0, 0, 2] (children: 0)
//     ├─ Window [0, 2, 0] (children: 0)
//   ├─ Character [-3, 0, 0] (children: 1)
//     ├─ Weapon [0.5, 1, 0] (children: 0)
```

### Q6: 异步加载如何避免阻塞?

**A:** 使用后台线程和进度回调:

```cpp
class AsyncLoadManager {
public:
    using ProgressCallback = std::function<void(float)>;
    using CompleteCallback = std::function<void(ResourceHandle<gltfio::FilamentAsset>)>;

    void loadModelAsync(const std::string& path,
                       ProgressCallback onProgress,
                       CompleteCallback onComplete)
    {
        std::thread([=]() {
            // 后台加载
            auto handle = mResourceMgr->loadModel(path);

            // 模拟进度更新
            for (int i = 0; i <= 100; i += 10) {
                onProgress(i / 100.0f);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            // 完成回调（需要在主线程执行）
            mMainThreadQueue.push([=]() {
                onComplete(handle);
            });
        }).detach();
    }

    void processMainThreadQueue() {
        while (!mMainThreadQueue.empty()) {
            auto task = mMainThreadQueue.front();
            mMainThreadQueue.pop();
            task();
        }
    }

private:
    ResourceManager* mResourceMgr;
    std::queue<std::function<void()>> mMainThreadQueue;
};
```

---

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.19)
project(scene_manager)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FILAMENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Filament root")

find_package(filament REQUIRED CONFIG PATHS ${FILAMENT_DIR}/out/cmake-release)

add_executable(scene_manager_demo
    samples/scene_manager/SceneNode.h
    samples/scene_manager/ResourceManager.h
    samples/scene_manager/SceneManager.h
    samples/scene_manager_demo.cpp
)

target_link_libraries(scene_manager_demo PRIVATE
    filament
    gltfio_core
    gltfio
    utils
    filamentapp
)

target_include_directories(scene_manager_demo PRIVATE
    ${FILAMENT_DIR}/filament/include
    ${FILAMENT_DIR}/libs/gltfio/include
    ${FILAMENT_DIR}/libs/utils/include
    ${FILAMENT_DIR}/libs/math/include
    ${FILAMENT_DIR}/libs/filamentapp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/samples
)

file(COPY assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

install(TARGETS scene_manager_demo DESTINATION bin)
```

---

## 相关文档

- [06-animation-player.md](./06-animation-player.md) - 动画播放器
- [04-gltf-viewer.md](./04-gltf-viewer.md) - glTF 查看器
- [08-ui-rendering.md](./08-ui-rendering.md) - UI 渲染系统
- [../platforms/android.md](../platforms/android.md) - Android 平台集成

---

## 总结

本示例展示了完整的场景管理系统实现,包括:

1. **场景图** - 树形层级结构管理对象
2. **资源管理** - 引用计数和缓存机制
3. **变换管理** - 局部/世界空间转换
4. **性能优化** - 实例化、剔除、LOD
5. **跨平台** - C++ 和 Android 实现

关键收获:
- 理解 ECS 架构在场景管理中的应用
- 掌握资源生命周期管理
- 学习大规模场景优化技术
- 实现可扩展的场景系统

下一步学习 [08-ui-rendering.md](./08-ui-rendering.md) 了解 UI 渲染。
