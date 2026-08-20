# Filament 插件系统设计

## 📖 概述

插件系统是大型应用程序实现模块化和可扩展性的关键机制。通过插件架构，可以在不修改核心代码的情况下动态扩展功能，支持第三方开发者贡献内容，实现按需加载以减少应用体积。

**核心价值**：
- **解耦核心与扩展**：核心引擎保持精简，功能通过插件提供
- **动态加载**：运行时加载/卸载插件，无需重新编译
- **第三方生态**：开发者可独立开发插件，扩展引擎能力
- **版本隔离**：插件独立升级，不影响主程序

**适用场景**：
- 渲染效果插件（自定义后处理、着色器）
- 资源加载器插件（支持新的模型格式、纹理格式）
- 物理引擎插件（不同的物理后端）
- 平台扩展插件（VR、AR 支持）

本文档将从零开始，详细讲解如何为 Filament 项目设计和实现一个健壮的插件系统。

---

## 1. 插件系统架构

### 1.1 基本概念

**插件（Plugin）**：独立的动态库（.so/.dylib/.dll），实现特定接口，提供特定功能。

**插件管理器（Plugin Manager）**：负责插件的发现、加载、初始化、卸载。

**插件接口（Plugin Interface）**：插件必须实现的抽象接口，定义核心引擎与插件的通信协议。

**插件元数据（Metadata）**：描述插件的信息（名称、版本、依赖、作者等）。

### 1.2 架构图

```
Application
    │
    ├─> PluginManager
    │       ├─> Plugin Discovery (查找插件)
    │       ├─> Plugin Loader (加载动态库)
    │       ├─> Plugin Registry (注册插件)
    │       └─> Plugin Lifecycle (初始化/卸载)
    │
    └─> Plugin Instances
            ├─> RenderEffectPlugin (渲染效果)
            ├─> AssetLoaderPlugin (资源加载)
            ├─> PhysicsPlugin (物理引擎)
            └─> CustomPlugin (用户自定义)
```

### 1.3 设计原则

1. **版本兼容性**：插件与主程序的 ABI 兼容性至关重要
2. **沙箱隔离**：插件崩溃不应导致主程序崩溃
3. **最小依赖**：插件接口应尽量简洁，减少对主程序内部的依赖
4. **生命周期管理**：清晰的加载、初始化、卸载流程
5. **错误处理**：插件加载失败不应影响主程序运行

---

## 2. 插件接口设计

### 2.1 核心接口

```cpp
// src/plugins/include/plugins/IPlugin.h
#pragma once

#include <cstdint>
#include <string>

namespace myproject::plugins {

// 插件版本
struct PluginVersion {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    
    bool isCompatibleWith(const PluginVersion& other) const {
        // 主版本号必须相同
        return major == other.major;
    }
};

// 插件元数据
struct PluginMetadata {
    const char* name;           // 插件名称
    const char* author;         // 作者
    const char* description;    // 描述
    PluginVersion version;      // 版本
    PluginVersion apiVersion;   // 所需的 API 版本
};

// 插件接口
class IPlugin {
public:
    virtual ~IPlugin() = default;
    
    // 获取元数据
    virtual const PluginMetadata& getMetadata() const = 0;
    
    // 生命周期
    virtual bool initialize(void* context) = 0;
    virtual void shutdown() = 0;
    
    // 更新（可选，每帧调用）
    virtual void update(float deltaTime) {}
};

}  // namespace myproject::plugins

// 导出函数（C ABI，确保跨编译器兼容）
extern "C" {
    // 创建插件实例
    typedef myproject::plugins::IPlugin* (*CreatePluginFunc)();
    
    // 销毁插件实例
    typedef void (*DestroyPluginFunc)(myproject::plugins::IPlugin*);
}

// 插件必须导出的宏
#define EXPORT_PLUGIN(PluginClass) \
    extern "C" { \
        __attribute__((visibility("default"))) \
        myproject::plugins::IPlugin* createPlugin() { \
            return new PluginClass(); \
        } \
        \
        __attribute__((visibility("default"))) \
        void destroyPlugin(myproject::plugins::IPlugin* plugin) { \
            delete plugin; \
        } \
    }

// Windows 导出
#ifdef _WIN32
    #define PLUGIN_EXPORT __declspec(dllexport)
#else
    #define PLUGIN_EXPORT __attribute__((visibility("default")))
#endif
```

### 2.2 特定功能接口

**渲染效果插件**：

```cpp
// src/plugins/include/plugins/IRenderEffectPlugin.h
#pragma once

#include <plugins/IPlugin.h>
#include <filament/View.h>
#include <filament/Engine.h>

namespace myproject::plugins {

class IRenderEffectPlugin : public IPlugin {
public:
    // 渲染效果配置
    virtual void configure(filament::View* view) = 0;
    
    // 渲染前处理
    virtual void preRender(filament::Engine& engine, filament::View& view) = 0;
    
    // 渲染后处理
    virtual void postRender(filament::Engine& engine, filament::View& view) = 0;
    
    // 获取所需的后处理材质
    virtual filament::Material* getMaterial(filament::Engine& engine) = 0;
};

}  // namespace myproject::plugins
```

**资源加载器插件**：

```cpp
// src/plugins/include/plugins/IAssetLoaderPlugin.h
#pragma once

#include <plugins/IPlugin.h>
#include <vector>
#include <string>

namespace myproject::plugins {

struct AssetData {
    void* data;
    size_t size;
    std::string mimeType;
};

class IAssetLoaderPlugin : public IPlugin {
public:
    // 支持的文件扩展名
    virtual std::vector<std::string> getSupportedExtensions() const = 0;
    
    // 加载资产
    virtual AssetData load(const std::string& path) = 0;
    
    // 卸载资产
    virtual void unload(AssetData& asset) = 0;
    
    // 异步加载
    virtual void loadAsync(const std::string& path, 
                          std::function<void(AssetData)> callback) = 0;
};

}  // namespace myproject::plugins
```

---

## 3. 插件管理器实现

### 3.1 插件加载器

```cpp
// src/plugins/include/plugins/PluginLoader.h
#pragma once

#include <string>
#include <memory>

#if defined(_WIN32)
    #include <windows.h>
    typedef HMODULE LibraryHandle;
#else
    #include <dlfcn.h>
    typedef void* LibraryHandle;
#endif

namespace myproject::plugins {

class PluginLoader {
public:
    // 加载动态库
    static LibraryHandle loadLibrary(const std::string& path) {
#if defined(_WIN32)
        return LoadLibraryA(path.c_str());
#else
        return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    }
    
    // 卸载动态库
    static void unloadLibrary(LibraryHandle handle) {
        if (!handle) return;
#if defined(_WIN32)
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
    }
    
    // 获取符号
    template<typename T>
    static T getSymbol(LibraryHandle handle, const char* name) {
#if defined(_WIN32)
        return reinterpret_cast<T>(GetProcAddress(handle, name));
#else
        return reinterpret_cast<T>(dlsym(handle, name));
#endif
    }
    
    // 获取错误信息
    static std::string getLastError() {
#if defined(_WIN32)
        DWORD error = GetLastError();
        char buffer[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, error, 0, 
                      buffer, sizeof(buffer), nullptr);
        return std::string(buffer);
#else
        const char* err = dlerror();
        return err ? std::string(err) : "Unknown error";
#endif
    }
};

}  // namespace myproject::plugins
```

### 3.2 插件管理器

```cpp
// src/plugins/include/plugins/PluginManager.h
#pragma once

#include <plugins/IPlugin.h>
#include <plugins/PluginLoader.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace myproject::plugins {

struct LoadedPlugin {
    LibraryHandle libraryHandle;
    IPlugin* instance;
    CreatePluginFunc createFunc;
    DestroyPluginFunc destroyFunc;
    std::string path;
    bool initialized;
};

class PluginManager {
public:
    static PluginManager& getInstance() {
        static PluginManager instance;
        return instance;
    }
    
    // 设置插件目录
    void setPluginDirectory(const std::string& dir) {
        std::lock_guard<std::mutex> lock(mMutex);
        mPluginDirectory = dir;
    }
    
    // 扫描并加载所有插件
    void discoverAndLoadPlugins() {
        std::lock_guard<std::mutex> lock(mMutex);
        
        // 查找插件文件
        auto pluginFiles = findPluginFiles(mPluginDirectory);
        
        for (const auto& file : pluginFiles) {
            loadPlugin(file);
        }
    }
    
    // 加载单个插件
    bool loadPlugin(const std::string& path) {
        LOG_INFO("Loading plugin: %s", path.c_str());
        
        // 加载动态库
        LibraryHandle handle = PluginLoader::loadLibrary(path);
        if (!handle) {
            LOG_ERROR("Failed to load library: %s", 
                     PluginLoader::getLastError().c_str());
            return false;
        }
        
        // 获取导出函数
        auto createFunc = PluginLoader::getSymbol<CreatePluginFunc>(
            handle, "createPlugin");
        auto destroyFunc = PluginLoader::getSymbol<DestroyPluginFunc>(
            handle, "destroyPlugin");
        
        if (!createFunc || !destroyFunc) {
            LOG_ERROR("Plugin missing required exports: %s", path.c_str());
            PluginLoader::unloadLibrary(handle);
            return false;
        }
        
        // 创建插件实例
        IPlugin* instance = createFunc();
        if (!instance) {
            LOG_ERROR("Failed to create plugin instance");
            PluginLoader::unloadLibrary(handle);
            return false;
        }
        
        // 检查版本兼容性
        const auto& meta = instance->getMetadata();
        PluginVersion currentAPI = {1, 0, 0};  // 当前 API 版本
        
        if (!meta.apiVersion.isCompatibleWith(currentAPI)) {
            LOG_ERROR("Plugin API version mismatch: %s (requires %d.%d.%d, have %d.%d.%d)",
                     meta.name, 
                     meta.apiVersion.major, meta.apiVersion.minor, meta.apiVersion.patch,
                     currentAPI.major, currentAPI.minor, currentAPI.patch);
            destroyFunc(instance);
            PluginLoader::unloadLibrary(handle);
            return false;
        }
        
        // 存储插件
        LoadedPlugin plugin;
        plugin.libraryHandle = handle;
        plugin.instance = instance;
        plugin.createFunc = createFunc;
        plugin.destroyFunc = destroyFunc;
        plugin.path = path;
        plugin.initialized = false;
        
        mPlugins[meta.name] = plugin;
        
        LOG_INFO("Loaded plugin: %s v%d.%d.%d by %s",
                meta.name, meta.version.major, meta.version.minor, meta.version.patch,
                meta.author);
        
        return true;
    }
    
    // 初始化所有插件
    void initializePlugins(void* context) {
        std::lock_guard<std::mutex> lock(mMutex);
        
        for (auto& [name, plugin] : mPlugins) {
            if (!plugin.initialized) {
                LOG_INFO("Initializing plugin: %s", name.c_str());
                
                if (plugin.instance->initialize(context)) {
                    plugin.initialized = true;
                } else {
                    LOG_ERROR("Failed to initialize plugin: %s", name.c_str());
                }
            }
        }
    }
    
    // 卸载插件
    void unloadPlugin(const std::string& name) {
        std::lock_guard<std::mutex> lock(mMutex);
        
        auto it = mPlugins.find(name);
        if (it == mPlugins.end()) {
            LOG_WARNING("Plugin not found: %s", name.c_str());
            return;
        }
        
        auto& plugin = it->second;
        
        // 关闭插件
        if (plugin.initialized) {
            plugin.instance->shutdown();
        }
        
        // 销毁实例
        plugin.destroyFunc(plugin.instance);
        
        // 卸载动态库
        PluginLoader::unloadLibrary(plugin.libraryHandle);
        
        mPlugins.erase(it);
        
        LOG_INFO("Unloaded plugin: %s", name.c_str());
    }
    
    // 卸载所有插件
    void unloadAllPlugins() {
        std::lock_guard<std::mutex> lock(mMutex);
        
        for (auto& [name, plugin] : mPlugins) {
            if (plugin.initialized) {
                plugin.instance->shutdown();
            }
            plugin.destroyFunc(plugin.instance);
            PluginLoader::unloadLibrary(plugin.libraryHandle);
        }
        
        mPlugins.clear();
    }
    
    // 更新所有插件
    void updatePlugins(float deltaTime) {
        std::lock_guard<std::mutex> lock(mMutex);
        
        for (auto& [name, plugin] : mPlugins) {
            if (plugin.initialized) {
                plugin.instance->update(deltaTime);
            }
        }
    }
    
    // 获取插件
    IPlugin* getPlugin(const std::string& name) {
        std::lock_guard<std::mutex> lock(mMutex);
        
        auto it = mPlugins.find(name);
        if (it != mPlugins.end() && it->second.initialized) {
            return it->second.instance;
        }
        return nullptr;
    }
    
    // 获取特定类型的插件
    template<typename T>
    T* getPluginAs(const std::string& name) {
        IPlugin* plugin = getPlugin(name);
        return dynamic_cast<T*>(plugin);
    }
    
    // 枚举所有插件
    std::vector<std::string> getPluginNames() const {
        std::lock_guard<std::mutex> lock(mMutex);
        
        std::vector<std::string> names;
        for (const auto& [name, _] : mPlugins) {
            names.push_back(name);
        }
        return names;
    }
    
private:
    PluginManager() = default;
    ~PluginManager() {
        unloadAllPlugins();
    }
    
    std::vector<std::string> findPluginFiles(const std::string& directory) {
        std::vector<std::string> files;
        
#if defined(_WIN32)
        WIN32_FIND_DATAA findData;
        std::string pattern = directory + "/*.dll";
        HANDLE hFind = FindFirstFileA(pattern.c_str(), &findData);
        
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                files.push_back(directory + "/" + findData.cFileName);
            } while (FindNextFileA(hFind, &findData));
            FindClose(hFind);
        }
        
#else
        DIR* dir = opendir(directory.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name = entry->d_name;
#if defined(__APPLE__)
                if (name.find(".dylib") != std::string::npos) {
#else
                if (name.find(".so") != std::string::npos) {
#endif
                    files.push_back(directory + "/" + name);
                }
            }
            closedir(dir);
        }
#endif
        
        return files;
    }
    
    mutable std::mutex mMutex;
    std::string mPluginDirectory = "plugins/";
    std::unordered_map<std::string, LoadedPlugin> mPlugins;
};

}  // namespace myproject::plugins
```

---

## 4. 插件开发示例

### 4.1 简单示例：Bloom 效果插件

**插件实现**：

```cpp
// plugins/bloom/BloomPlugin.h
#pragma once

#include <plugins/IRenderEffectPlugin.h>
#include <filament/Material.h>

class BloomPlugin : public myproject::plugins::IRenderEffectPlugin {
public:
    BloomPlugin();
    ~BloomPlugin() override;
    
    // IPlugin 接口
    const myproject::plugins::PluginMetadata& getMetadata() const override;
    bool initialize(void* context) override;
    void shutdown() override;
    
    // IRenderEffectPlugin 接口
    void configure(filament::View* view) override;
    void preRender(filament::Engine& engine, filament::View& view) override;
    void postRender(filament::Engine& engine, filament::View& view) override;
    filament::Material* getMaterial(filament::Engine& engine) override;
    
private:
    filament::Engine* mEngine;
    filament::Material* mBloomMaterial;
    
    static myproject::plugins::PluginMetadata sMetadata;
};

// 导出插件
EXPORT_PLUGIN(BloomPlugin)
```

**插件实现文件**：

```cpp
// plugins/bloom/BloomPlugin.cpp
#include "BloomPlugin.h"
#include <core/Logger.h>

myproject::plugins::PluginMetadata BloomPlugin::sMetadata = {
    .name = "BloomEffect",
    .author = "MyCompany",
    .description = "HDR Bloom post-processing effect",
    .version = {1, 0, 0},
    .apiVersion = {1, 0, 0}
};

BloomPlugin::BloomPlugin()
    : mEngine(nullptr)
    , mBloomMaterial(nullptr) {}

BloomPlugin::~BloomPlugin() {}

const myproject::plugins::PluginMetadata& BloomPlugin::getMetadata() const {
    return sMetadata;
}

bool BloomPlugin::initialize(void* context) {
    // context 是 Engine 指针
    mEngine = static_cast<filament::Engine*>(context);
    
    // 加载 Bloom 材质
    // 实际项目中应从文件加载
    // mBloomMaterial = Material::Builder()
    //     .package(bloomPackageData, bloomPackageSize)
    //     .build(*mEngine);
    
    LOG_INFO("BloomPlugin initialized");
    return true;
}

void BloomPlugin::shutdown() {
    if (mBloomMaterial) {
        mEngine->destroy(mBloomMaterial);
        mBloomMaterial = nullptr;
    }
    
    LOG_INFO("BloomPlugin shutdown");
}

void BloomPlugin::configure(filament::View* view) {
    // 配置 View 的 Bloom 设置
    view->setBloomOptions({
        .enabled = true,
        .levels = 5,
        .resolution = 256,
        .strength = 0.2f
    });
}

void BloomPlugin::preRender(filament::Engine& engine, filament::View& view) {
    // 渲染前处理（如果需要）
}

void BloomPlugin::postRender(filament::Engine& engine, filament::View& view) {
    // 渲染后处理（如果需要）
}

filament::Material* BloomPlugin::getMaterial(filament::Engine& engine) {
    return mBloomMaterial;
}
```

**插件 CMakeLists.txt**：

```cmake
# plugins/bloom/CMakeLists.txt

project(BloomPlugin)

add_library(BloomPlugin SHARED
    BloomPlugin.cpp
    BloomPlugin.h
)

target_include_directories(BloomPlugin
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/plugins/include
        ${CMAKE_SOURCE_DIR}/src/core/include
)

target_link_libraries(BloomPlugin
    PRIVATE
        filament::filament
        core
)

# 设置输出目录
set_target_properties(BloomPlugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
)

# macOS 特定设置
if(APPLE)
    set_target_properties(BloomPlugin PROPERTIES
        SUFFIX ".dylib"
    )
endif()
```

### 4.2 复杂示例：FBX 资源加载器插件

```cpp
// plugins/fbx_loader/FBXLoaderPlugin.h
#pragma once

#include <plugins/IAssetLoaderPlugin.h>
#include <fbxsdk.h>

class FBXLoaderPlugin : public myproject::plugins::IAssetLoaderPlugin {
public:
    FBXLoaderPlugin();
    ~FBXLoaderPlugin() override;
    
    // IPlugin 接口
    const myproject::plugins::PluginMetadata& getMetadata() const override;
    bool initialize(void* context) override;
    void shutdown() override;
    
    // IAssetLoaderPlugin 接口
    std::vector<std::string> getSupportedExtensions() const override;
    myproject::plugins::AssetData load(const std::string& path) override;
    void unload(myproject::plugins::AssetData& asset) override;
    void loadAsync(const std::string& path, 
                   std::function<void(myproject::plugins::AssetData)> callback) override;
    
private:
    FbxManager* mFbxManager;
    FbxIOSettings* mIOSettings;
    
    static myproject::plugins::PluginMetadata sMetadata;
};

EXPORT_PLUGIN(FBXLoaderPlugin)
```

```cpp
// plugins/fbx_loader/FBXLoaderPlugin.cpp
#include "FBXLoaderPlugin.h"
#include <core/Logger.h>

myproject::plugins::PluginMetadata FBXLoaderPlugin::sMetadata = {
    .name = "FBXLoader",
    .author = "MyCompany",
    .description = "FBX file format loader using Autodesk FBX SDK",
    .version = {1, 0, 0},
    .apiVersion = {1, 0, 0}
};

FBXLoaderPlugin::FBXLoaderPlugin()
    : mFbxManager(nullptr)
    , mIOSettings(nullptr) {}

FBXLoaderPlugin::~FBXLoaderPlugin() {}

const myproject::plugins::PluginMetadata& FBXLoaderPlugin::getMetadata() const {
    return sMetadata;
}

bool FBXLoaderPlugin::initialize(void* context) {
    // 初始化 FBX SDK
    mFbxManager = FbxManager::Create();
    if (!mFbxManager) {
        LOG_ERROR("Failed to create FBX Manager");
        return false;
    }
    
    mIOSettings = FbxIOSettings::Create(mFbxManager, IOSROOT);
    mFbxManager->SetIOSettings(mIOSettings);
    
    LOG_INFO("FBXLoaderPlugin initialized");
    return true;
}

void FBXLoaderPlugin::shutdown() {
    if (mFbxManager) {
        mFbxManager->Destroy();
        mFbxManager = nullptr;
    }
    
    LOG_INFO("FBXLoaderPlugin shutdown");
}

std::vector<std::string> FBXLoaderPlugin::getSupportedExtensions() const {
    return {".fbx", ".FBX"};
}

myproject::plugins::AssetData FBXLoaderPlugin::load(const std::string& path) {
    LOG_INFO("Loading FBX: %s", path.c_str());
    
    FbxImporter* importer = FbxImporter::Create(mFbxManager, "");
    
    if (!importer->Initialize(path.c_str(), -1, mIOSettings)) {
        LOG_ERROR("Failed to initialize FBX importer: %s", 
                 importer->GetStatus().GetErrorString());
        importer->Destroy();
        return {nullptr, 0, ""};
    }
    
    FbxScene* scene = FbxScene::Create(mFbxManager, "ImportedScene");
    importer->Import(scene);
    importer->Destroy();
    
    // 转换 FBX 数据为引擎内部格式
    // ...（省略具体转换逻辑）
    
    myproject::plugins::AssetData result;
    result.data = scene;  // 实际应转换为引擎格式
    result.size = 0;
    result.mimeType = "model/fbx";
    
    return result;
}

void FBXLoaderPlugin::unload(myproject::plugins::AssetData& asset) {
    if (asset.data) {
        FbxScene* scene = static_cast<FbxScene*>(asset.data);
        scene->Destroy();
        asset.data = nullptr;
    }
}

void FBXLoaderPlugin::loadAsync(const std::string& path, 
                                std::function<void(myproject::plugins::AssetData)> callback) {
    // 启动异步加载线程
    std::thread([this, path, callback]() {
        auto asset = load(path);
        callback(asset);
    }).detach();
}
```

---

## 5. 插件使用示例

### 5.1 应用程序集成

```cpp
// src/app/main.cpp
#include <plugins/PluginManager.h>
#include <plugins/IRenderEffectPlugin.h>
#include <core/Logger.h>

int main() {
    // 创建引擎
    auto engine = filament::Engine::create();
    
    // 设置插件目录并加载插件
    auto& pluginMgr = myproject::plugins::PluginManager::getInstance();
    pluginMgr.setPluginDirectory("./plugins");
    pluginMgr.discoverAndLoadPlugins();
    
    // 初始化插件（传递 Engine 作为上下文）
    pluginMgr.initializePlugins(engine);
    
    // 使用 Bloom 插件
    auto* bloomPlugin = pluginMgr.getPluginAs<myproject::plugins::IRenderEffectPlugin>("BloomEffect");
    if (bloomPlugin) {
        LOG_INFO("Applying Bloom effect");
        bloomPlugin->configure(view);
    }
    
    // 主循环
    while (!shouldClose) {
        float deltaTime = calculateDeltaTime();
        
        // 更新插件
        pluginMgr.updatePlugins(deltaTime);
        
        // 渲染前处理
        if (bloomPlugin) {
            bloomPlugin->preRender(*engine, *view);
        }
        
        // 渲染
        renderer->render(view);
        
        // 渲染后处理
        if (bloomPlugin) {
            bloomPlugin->postRender(*engine, *view);
        }
    }
    
    // 清理
    pluginMgr.unloadAllPlugins();
    filament::Engine::destroy(&engine);
    
    return 0;
}
```

### 5.2 动态加载/卸载

```cpp
// 运行时加载插件
pluginMgr.loadPlugin("./plugins/CustomEffect.so");
pluginMgr.initializePlugins(engine);

// 使用插件
auto* customPlugin = pluginMgr.getPlugin("CustomEffect");
if (customPlugin) {
    // ...
}

// 卸载插件
pluginMgr.unloadPlugin("CustomEffect");
```

---

## 6. 高级特性

### 6.1 插件依赖管理

```cpp
// 插件元数据扩展
struct PluginMetadata {
    const char* name;
    const char* author;
    const char* description;
    PluginVersion version;
    PluginVersion apiVersion;
    
    // 新增：依赖项
    struct Dependency {
        const char* name;
        PluginVersion minVersion;
    };
    const Dependency* dependencies;
    size_t dependencyCount;
};

// 插件管理器依赖解析
void PluginManager::resolveDependencies() {
    // 拓扑排序，确保依赖的插件先初始化
    std::vector<std::string> initOrder;
    std::unordered_set<std::string> visited;
    
    for (const auto& [name, plugin] : mPlugins) {
        resolveDependenciesRecursive(name, visited, initOrder);
    }
    
    // 按顺序初始化
    for (const auto& name : initOrder) {
        auto& plugin = mPlugins[name];
        plugin.instance->initialize(mContext);
    }
}

void PluginManager::resolveDependenciesRecursive(
    const std::string& name,
    std::unordered_set<std::string>& visited,
    std::vector<std::string>& initOrder) {
    
    if (visited.count(name)) return;
    visited.insert(name);
    
    auto& plugin = mPlugins[name];
    const auto& meta = plugin.instance->getMetadata();
    
    // 先初始化依赖
    for (size_t i = 0; i < meta.dependencyCount; ++i) {
        const auto& dep = meta.dependencies[i];
        
        // 检查依赖是否存在
        if (mPlugins.find(dep.name) == mPlugins.end()) {
            LOG_ERROR("Plugin %s requires %s, but it's not loaded", 
                     name.c_str(), dep.name);
            continue;
        }
        
        // 检查版本
        auto& depPlugin = mPlugins[dep.name];
        const auto& depMeta = depPlugin.instance->getMetadata();
        
        if (!depMeta.version.isCompatibleWith(dep.minVersion)) {
            LOG_ERROR("Plugin %s requires %s v%d.%d.%d or later, but v%d.%d.%d is loaded",
                     name.c_str(), dep.name,
                     dep.minVersion.major, dep.minVersion.minor, dep.minVersion.patch,
                     depMeta.version.major, depMeta.version.minor, depMeta.version.patch);
        }
        
        resolveDependenciesRecursive(dep.name, visited, initOrder);
    }
    
    initOrder.push_back(name);
}
```

### 6.2 插件热重载

```cpp
class PluginManager {
public:
    // 监控插件文件变化
    void enableHotReload(bool enable) {
        mHotReloadEnabled = enable;
        
        if (enable && !mFileWatcher) {
            mFileWatcher = std::make_unique<FileWatcher>(mPluginDirectory);
            mFileWatcher->setCallback([this](const std::string& path) {
                onPluginFileChanged(path);
            });
        }
    }
    
private:
    void onPluginFileChanged(const std::string& path) {
        LOG_INFO("Plugin file changed: %s", path.c_str());
        
        // 查找对应的插件
        for (auto& [name, plugin] : mPlugins) {
            if (plugin.path == path) {
                LOG_INFO("Reloading plugin: %s", name.c_str());
                
                // 保存状态
                // auto state = plugin.instance->saveState();
                
                // 卸载旧插件
                plugin.instance->shutdown();
                plugin.destroyFunc(plugin.instance);
                PluginLoader::unloadLibrary(plugin.libraryHandle);
                
                // 加载新插件
                loadPlugin(path);
                
                // 恢复状态
                // plugin.instance->restoreState(state);
                
                break;
            }
        }
    }
    
    bool mHotReloadEnabled = false;
    std::unique_ptr<FileWatcher> mFileWatcher;
};
```

### 6.3 插件沙箱

为了防止插件崩溃导致主程序崩溃，可以使用进程隔离：

```cpp
// 在独立进程中运行插件
class SandboxedPlugin {
public:
    SandboxedPlugin(const std::string& pluginPath) {
        // 启动插件进程
        mProcess = launchPluginProcess(pluginPath);
        
        // 建立 IPC 通信（共享内存/管道/Socket）
        mIPC = createIPCChannel(mProcess);
    }
    
    ~SandboxedPlugin() {
        terminateProcess(mProcess);
    }
    
    // 通过 IPC 调用插件方法
    void callMethod(const std::string& method, const std::string& args) {
        mIPC->sendMessage({method, args});
        auto response = mIPC->receiveMessage();
        // ...
    }
    
private:
    ProcessHandle mProcess;
    std::unique_ptr<IPCChannel> mIPC;
};
```

### 6.4 插件配置

```cpp
// 插件配置文件
// plugins/BloomPlugin.json
{
  "enabled": true,
  "parameters": {
    "strength": 0.2,
    "threshold": 1.0,
    "levels": 5
  }
}

// 插件接口扩展
class IPlugin {
public:
    virtual void configure(const nlohmann::json& config) {}
};

// 加载配置
void PluginManager::loadPluginConfigs() {
    for (auto& [name, plugin] : mPlugins) {
        std::string configPath = mPluginDirectory + "/" + name + ".json";
        
        std::ifstream file(configPath);
        if (file.is_open()) {
            nlohmann::json config;
            file >> config;
            plugin.instance->configure(config);
        }
    }
}
```

---

## 7. 最佳实践

### 7.1 版本兼容性

1. **语义化版本**：遵循 SemVer（主版本.次版本.修订版）
2. **API 稳定性**：主版本号相同时保证 ABI 兼容
3. **向后兼容**：新版本支持旧插件（在合理范围内）

### 7.2 错误处理

```cpp
bool IPlugin::initialize(void* context) {
    try {
        // 初始化逻辑
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Plugin initialization failed: %s", e.what());
        return false;
    }
}
```

### 7.3 性能考虑

1. **懒加载**：按需加载插件，而非启动时全部加载
2. **缓存**：缓存插件查找结果，避免重复遍历
3. **异步加载**：在后台线程加载插件，避免阻塞主线程

### 7.4 安全性

1. **签名验证**：验证插件的数字签名
2. **权限控制**：限制插件可访问的 API
3. **沙箱隔离**：高风险插件在独立进程运行

---

## 8. 调试和测试

### 8.1 插件调试

```cpp
// 在加载插件时启用调试信息
#ifdef DEBUG
    #define PLUGIN_DEBUG_ENABLED 1
#endif

void PluginManager::loadPlugin(const std::string& path) {
#ifdef PLUGIN_DEBUG_ENABLED
    LOG_INFO("Loading plugin from: %s", path.c_str());
    LOG_INFO("Library handle: %p", handle);
    LOG_INFO("createPlugin address: %p", createFunc);
#endif
    // ...
}
```

### 8.2 插件单元测试

```cpp
// tests/plugins/PluginTest.cpp
#include <gtest/gtest.h>
#include <plugins/PluginManager.h>

TEST(PluginTest, LoadPlugin) {
    auto& mgr = PluginManager::getInstance();
    
    bool loaded = mgr.loadPlugin("./test_plugins/TestPlugin.so");
    ASSERT_TRUE(loaded);
    
    auto* plugin = mgr.getPlugin("TestPlugin");
    ASSERT_NE(plugin, nullptr);
    
    mgr.unloadPlugin("TestPlugin");
}

TEST(PluginTest, VersionCompatibility) {
    // 测试版本兼容性检查
    PluginVersion v1{1, 0, 0};
    PluginVersion v2{1, 1, 0};
    PluginVersion v3{2, 0, 0};
    
    EXPECT_TRUE(v1.isCompatibleWith(v2));
    EXPECT_FALSE(v1.isCompatibleWith(v3));
}
```

---

## 9. 示例项目结构

```
MyFilamentProject/
├── src/
│   ├── plugins/
│   │   ├── include/
│   │   │   └── plugins/
│   │   │       ├── IPlugin.h
│   │   │       ├── IRenderEffectPlugin.h
│   │   │       ├── IAssetLoaderPlugin.h
│   │   │       ├── PluginManager.h
│   │   │       └── PluginLoader.h
│   │   └── src/
│   │       ├── PluginManager.cpp
│   │       └── PluginLoader.cpp
│   └── app/
│       └── main.cpp
│
├── plugins/
│   ├── bloom/
│   │   ├── BloomPlugin.h
│   │   ├── BloomPlugin.cpp
│   │   └── CMakeLists.txt
│   ├── fbx_loader/
│   │   ├── FBXLoaderPlugin.h
│   │   ├── FBXLoaderPlugin.cpp
│   │   └── CMakeLists.txt
│   └── CMakeLists.txt
│
└── CMakeLists.txt
```

**根 CMakeLists.txt**：

```cmake
cmake_minimum_required(VERSION 3.19)
project(MyFilamentProject)

# 构建插件系统
add_subdirectory(src/plugins)

# 构建应用程序
add_subdirectory(src/app)

# 构建插件
add_subdirectory(plugins)
```

---

## 10. 常见问题

### Q1: 插件在 Windows 上加载失败？

**A**: 检查依赖 DLL 是否在 PATH 中，或与插件放在同一目录。使用 Dependency Walker 工具分析缺失的依赖。

### Q2: 插件热重载导致崩溃？

**A**: 卸载插件前确保所有引用都已释放。考虑使用双缓冲策略：加载新版本后再卸载旧版本。

### Q3: 如何调试插件初始化失败？

**A**: 在插件的 `initialize` 方法中添加详细日志，使用调试器附加到主进程，设置断点在插件加载处。

### Q4: 插件之间如何通信？

**A**: 通过主程序中转（事件系统），或使用共享服务接口。避免插件直接依赖其他插件。

---

## 11. 相关文档

- [05-large-project-structure.md](./05-large-project-structure.md) - 大型项目架构
- [07-async-loading.md](./07-async-loading.md) - 异步加载
- [02-multithreading-design.md](./02-multithreading-design.md) - 多线程设计

---

## 12. 总结

插件系统为 Filament 应用提供了强大的扩展能力：

1. **模块化**：核心引擎与扩展功能解耦
2. **动态性**：运行时加载/卸载插件
3. **可扩展性**：第三方开发者可贡献插件
4. **灵活性**：按需启用功能，减少应用体积

通过本文档介绍的架构设计、接口定义、管理器实现和最佳实践，您可以为自己的 Filament 项目构建一个健壮、高效、易用的插件系统。

**关键要点**：
- 使用 C ABI 确保跨编译器兼容性
- 严格的版本管理和依赖解析
- 完善的错误处理和日志记录
- 考虑性能和安全性
- 提供清晰的插件开发文档

插件系统的成功取决于良好的设计和完善的生态。投资于插件架构，将为项目带来长期的可维护性和扩展性优势。
