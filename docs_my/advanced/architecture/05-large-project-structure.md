# Filament 大型项目架构设计

## 📖 概述

本文档深入讲解如何使用 Filament 构建大型商业级渲染应用的项目架构。随着项目规模增长，良好的架构设计变得至关重要——它决定了代码的可维护性、可扩展性、团队协作效率以及长期演化能力。

**核心主题**：
- **模块化设计**：如何将大型项目拆分为可管理的模块
- **依赖管理**：控制模块间依赖，避免循环依赖和紧耦合
- **构建系统**：CMake 最佳实践和多平台支持
- **代码组织**：目录结构、命名规范、接口设计
- **资源管理**：大规模资源的加载、缓存、卸载策略
- **测试架构**：单元测试、集成测试、性能测试框架

本文档将结合实际案例（游戏引擎、CAD 软件、VR 应用等），展示如何在实战中应用这些架构原则。

---

## 1. 项目架构原则

### 1.1 SOLID 原则在渲染引擎中的应用

虽然 SOLID 是面向对象设计原则，但其核心思想同样适用于 C++ 渲染引擎：

**单一职责原则 (SRP)**  
每个模块/类只负责一项功能：

```cpp
// ❌ 不好：一个类做太多事情
class SceneManager {
    void loadModel(const char* path);
    void renderScene();
    void updatePhysics(float dt);
    void playAudio();
};

// ✅ 好：职责分离
class AssetLoader {
    FilamentAsset* loadModel(const char* path);
};

class SceneRenderer {
    void render(Engine& engine, Scene& scene, View& view);
};

class PhysicsWorld {
    void update(float dt);
};
```

**开闭原则 (OCP)**  
对扩展开放，对修改封闭。使用抽象接口：

```cpp
// 资源加载器接口
class IResourceLoader {
public:
    virtual ~IResourceLoader() = default;
    virtual void* load(const char* path) = 0;
    virtual void unload(void* resource) = 0;
};

// 纹理加载器实现
class TextureLoader : public IResourceLoader {
public:
    void* load(const char* path) override {
        return Texture::Builder()
            .width(width).height(height)
            .format(Texture::InternalFormat::RGBA8)
            .build(*mEngine);
    }
    
    void unload(void* resource) override {
        mEngine->destroy(static_cast<Texture*>(resource));
    }
    
private:
    Engine* mEngine;
};

// 模型加载器实现
class ModelLoader : public IResourceLoader {
public:
    void* load(const char* path) override {
        return AssetLoader::createFromFile(*mEngine, path);
    }
    
    void unload(void* resource) override {
        mAssetLoader->destroyAsset(static_cast<FilamentAsset*>(resource));
    }
    
private:
    Engine* mEngine;
    AssetLoader* mAssetLoader;
};

// 资源管理器可以处理任何类型的资源
class ResourceManager {
public:
    void registerLoader(const std::string& type, IResourceLoader* loader) {
        mLoaders[type] = loader;
    }
    
    void* load(const std::string& type, const char* path) {
        auto it = mLoaders.find(type);
        if (it != mLoaders.end()) {
            return it->second->load(path);
        }
        return nullptr;
    }
    
private:
    std::unordered_map<std::string, IResourceLoader*> mLoaders;
};
```

**依赖倒置原则 (DIP)**  
高层模块不应依赖低层模块，两者都应依赖抽象：

```cpp
// ❌ 不好：直接依赖具体实现
class Application {
    OpenGLRenderer mRenderer;  // 紧耦合到 OpenGL
    
    void init() {
        mRenderer.initialize();
    }
};

// ✅ 好：依赖抽象接口
class IBackend {
public:
    virtual ~IBackend() = default;
    virtual void initialize() = 0;
    virtual void render(const RenderData& data) = 0;
};

class VulkanBackend : public IBackend {
public:
    void initialize() override { /* Vulkan 初始化 */ }
    void render(const RenderData& data) override { /* Vulkan 渲染 */ }
};

class MetalBackend : public IBackend {
public:
    void initialize() override { /* Metal 初始化 */ }
    void render(const RenderData& data) override { /* Metal 渲染 */ }
};

class Application {
    std::unique_ptr<IBackend> mBackend;
    
    void init(BackendType type) {
        switch (type) {
            case BackendType::VULKAN:
                mBackend = std::make_unique<VulkanBackend>();
                break;
            case BackendType::METAL:
                mBackend = std::make_unique<MetalBackend>();
                break;
        }
        mBackend->initialize();
    }
};
```

### 1.2 模块化设计

将大型项目拆分为独立的模块，每个模块有清晰的边界和接口：

```
MyProject/
├── Core/              # 核心系统（日志、内存、线程）
├── Rendering/         # 渲染系统（Filament 封装）
├── Assets/            # 资源管理（加载、缓存、卸载）
├── Scene/             # 场景管理（场景图、ECS）
├── Animation/         # 动画系统（骨骼、变形）
├── Physics/           # 物理系统（碰撞、刚体）
├── Audio/             # 音频系统
├── Input/             # 输入系统
├── UI/                # UI 系统
├── Scripting/         # 脚本系统
└── Platform/          # 平台抽象层（Windows、macOS、Android）
```

每个模块应该：
1. 有独立的 CMakeLists.txt
2. 只通过公共接口与其他模块交互
3. 可以独立测试
4. 有明确的依赖关系（避免循环依赖）

### 1.3 依赖管理

**依赖图示例**：

```
Platform (无依赖)
    ↑
Core (依赖 Platform)
    ↑
Assets (依赖 Core)
    ↑
Rendering (依赖 Core, Assets)
    ↑
Scene (依赖 Core, Assets, Rendering)
    ↑
Animation (依赖 Core, Scene)
    ↑
Application (依赖所有模块)
```

**关键规则**：
- 低层模块（Core, Platform）不能依赖高层模块
- 同层模块之间通过接口通信，避免直接依赖
- 使用依赖注入（Dependency Injection）解耦

**依赖注入示例**：

```cpp
// ❌ 不好：内部创建依赖
class SceneManager {
    ResourceManager mResourceMgr;  // 硬编码依赖
    
    SceneManager() : mResourceMgr() {}
};

// ✅ 好：外部注入依赖
class SceneManager {
    ResourceManager& mResourceMgr;
    
    SceneManager(ResourceManager& resMgr) 
        : mResourceMgr(resMgr) {}
};

// 使用时
int main() {
    ResourceManager resourceMgr;
    SceneManager sceneMgr(resourceMgr);  // 注入依赖
}
```

---

## 2. 目录结构设计

### 2.1 推荐的目录结构

```
MyFilamentProject/
│
├── cmake/                  # CMake 工具脚本
│   ├── FindFilament.cmake
│   ├── CompilerWarnings.cmake
│   └── Sanitizers.cmake
│
├── third_party/            # 第三方库
│   ├── filament/          # Filament SDK
│   ├── glm/               # GLM 数学库
│   ├── stb/               # STB 图像库
│   └── assimp/            # Assimp 模型加载
│
├── src/                    # 源代码
│   ├── core/              # 核心模块
│   │   ├── include/       # 公共头文件
│   │   │   └── core/
│   │   │       ├── Logger.h
│   │   │       ├── Memory.h
│   │   │       └── Types.h
│   │   ├── src/           # 实现文件
│   │   │   ├── Logger.cpp
│   │   │   └── Memory.cpp
│   │   └── CMakeLists.txt
│   │
│   ├── rendering/         # 渲染模块
│   │   ├── include/
│   │   │   └── rendering/
│   │   │       ├── RenderSystem.h
│   │   │       ├── MaterialManager.h
│   │   │       └── LightManager.h
│   │   ├── src/
│   │   │   ├── RenderSystem.cpp
│   │   │   └── MaterialManager.cpp
│   │   └── CMakeLists.txt
│   │
│   ├── assets/            # 资源模块
│   │   ├── include/
│   │   │   └── assets/
│   │   │       ├── AssetLoader.h
│   │   │       ├── TextureCache.h
│   │   │       └── ModelCache.h
│   │   ├── src/
│   │   └── CMakeLists.txt
│   │
│   ├── scene/             # 场景模块
│   │   ├── include/
│   │   │   └── scene/
│   │   │       ├── SceneGraph.h
│   │   │       ├── Entity.h
│   │   │       └── Component.h
│   │   ├── src/
│   │   └── CMakeLists.txt
│   │
│   └── app/               # 应用层
│       ├── main.cpp
│       └── CMakeLists.txt
│
├── tests/                  # 测试
│   ├── core/
│   │   └── LoggerTest.cpp
│   ├── rendering/
│   │   └── MaterialManagerTest.cpp
│   └── CMakeLists.txt
│
├── samples/                # 示例程序
│   ├── basic/
│   ├── advanced/
│   └── CMakeLists.txt
│
├── assets/                 # 资源文件
│   ├── models/
│   ├── textures/
│   ├── materials/
│   └── shaders/
│
├── docs/                   # 文档
│   ├── architecture.md
│   └── api_reference.md
│
├── scripts/                # 构建/工具脚本
│   ├── build.sh
│   ├── format.sh
│   └── generate_docs.sh
│
├── CMakeLists.txt          # 根 CMake 文件
├── README.md
└── .gitignore
```

### 2.2 头文件组织

**公共 API 头文件**：

```cpp
// src/rendering/include/rendering/RenderSystem.h
#pragma once

#include <core/Types.h>  // 跨模块依赖使用 <> 引用
#include <filament/Engine.h>
#include <filament/Renderer.h>

namespace myproject::rendering {

class RenderSystem {
public:
    explicit RenderSystem(filament::Engine* engine);
    ~RenderSystem();
    
    void render(float deltaTime);
    
    // 禁止拷贝
    RenderSystem(const RenderSystem&) = delete;
    RenderSystem& operator=(const RenderSystem&) = delete;
    
private:
    class Impl;  // Pimpl 模式隐藏实现细节
    std::unique_ptr<Impl> mImpl;
};

}  // namespace myproject::rendering
```

**实现文件**：

```cpp
// src/rendering/src/RenderSystem.cpp
#include <rendering/RenderSystem.h>
#include "RenderSystemImpl.h"  // 内部头文件使用 "" 引用

namespace myproject::rendering {

// Pimpl 实现
class RenderSystem::Impl {
public:
    Impl(filament::Engine* engine)
        : mEngine(engine)
        , mRenderer(engine->createRenderer())
        , mSwapChain(nullptr) {}
    
    ~Impl() {
        mEngine->destroy(mRenderer);
        mEngine->destroy(mSwapChain);
    }
    
    void render(float deltaTime) {
        if (mRenderer->beginFrame(mSwapChain)) {
            mRenderer->render(mView);
            mRenderer->endFrame();
        }
    }
    
private:
    filament::Engine* mEngine;
    filament::Renderer* mRenderer;
    filament::SwapChain* mSwapChain;
    filament::View* mView;
};

RenderSystem::RenderSystem(filament::Engine* engine)
    : mImpl(std::make_unique<Impl>(engine)) {}

RenderSystem::~RenderSystem() = default;

void RenderSystem::render(float deltaTime) {
    mImpl->render(deltaTime);
}

}  // namespace myproject::rendering
```

**Pimpl 模式的优点**：
1. **编译隔离**：修改实现不会导致客户端代码重新编译
2. **ABI 稳定**：动态库升级时保持二进制兼容性
3. **隐藏细节**：头文件中不暴露 Filament 内部类型
4. **减少依赖**：头文件中不需要 include Filament 所有头文件

### 2.3 命名规范

**文件命名**：
- 头文件：`ClassName.h` (PascalCase)
- 源文件：`ClassName.cpp`
- 测试文件：`ClassNameTest.cpp`

**代码命名**：
- 类名：`PascalCase` (例如 `RenderSystem`)
- 函数名：`camelCase` (例如 `renderFrame`)
- 成员变量：`mCamelCase` (例如 `mEngine`)
- 静态成员：`sCamelCase` (例如 `sInstance`)
- 常量：`kCamelCase` 或 `UPPER_CASE` (例如 `kMaxLights`)
- 命名空间：`lowercase` (例如 `myproject::rendering`)

**示例**：

```cpp
namespace myproject::rendering {

class MaterialManager {
public:
    static constexpr size_t kMaxMaterials = 1024;
    
    MaterialManager();
    ~MaterialManager();
    
    Material* createMaterial(const char* name);
    void destroyMaterial(Material* material);
    
private:
    static MaterialManager* sInstance;
    
    Engine* mEngine;
    std::vector<Material*> mMaterials;
    std::unordered_map<std::string, size_t> mMaterialIndices;
};

}  // namespace myproject::rendering
```

---

## 3. CMake 构建系统

### 3.1 根 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.19)
project(MyFilamentProject VERSION 1.0.0 LANGUAGES CXX)

# C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 项目配置选项
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_SAMPLES "Build sample applications" ON)
option(ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(ENABLE_TSAN "Enable Thread Sanitizer" OFF)

# 编译器警告
include(cmake/CompilerWarnings.cmake)
set_project_warnings()

# Sanitizers
if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
endif()

if(ENABLE_TSAN)
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
endif()

# 查找 Filament
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(Filament REQUIRED)

# 第三方库
add_subdirectory(third_party)

# 源代码模块
add_subdirectory(src/core)
add_subdirectory(src/rendering)
add_subdirectory(src/assets)
add_subdirectory(src/scene)
add_subdirectory(src/app)

# 测试
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# 示例
if(BUILD_SAMPLES)
    add_subdirectory(samples)
endif()
```

### 3.2 模块 CMakeLists.txt

**Core 模块**：

```cmake
# src/core/CMakeLists.txt

# 收集源文件
file(GLOB_RECURSE CORE_SOURCES 
    "src/*.cpp"
    "src/*.c"
)

file(GLOB_RECURSE CORE_HEADERS
    "include/core/*.h"
)

# 创建静态库
add_library(core STATIC
    ${CORE_SOURCES}
    ${CORE_HEADERS}
)

# 包含目录
target_include_directories(core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 链接依赖
target_link_libraries(core
    PUBLIC
        # 公共依赖（会传播给依赖 core 的目标）
    PRIVATE
        # 私有依赖（不会传播）
)

# 编译选项
target_compile_options(core
    PRIVATE
        # 编译警告等
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# 设置属性
set_target_properties(core PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON  # 支持动态库
)

# 安装规则
install(TARGETS core
    EXPORT CoreTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
)
```

**Rendering 模块**：

```cmake
# src/rendering/CMakeLists.txt

file(GLOB_RECURSE RENDERING_SOURCES "src/*.cpp")
file(GLOB_RECURSE RENDERING_HEADERS "include/rendering/*.h")

add_library(rendering STATIC
    ${RENDERING_SOURCES}
    ${RENDERING_HEADERS}
)

target_include_directories(rendering
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(rendering
    PUBLIC
        core                    # 依赖 core 模块
        filament::filament
        filament::filabridge
        filament::utils
    PRIVATE
        # 私有依赖
)

set_target_properties(rendering PROPERTIES
    CXX_STANDARD 17
    POSITION_INDEPENDENT_CODE ON
)
```

### 3.3 FindFilament.cmake

```cmake
# cmake/FindFilament.cmake

# 查找 Filament 安装路径
find_path(FILAMENT_INCLUDE_DIR
    NAMES filament/Engine.h
    PATHS
        ${FILAMENT_ROOT}/include
        /usr/local/include
        /usr/include
    PATH_SUFFIXES filament
)

# 查找 Filament 库
find_library(FILAMENT_LIBRARY
    NAMES filament
    PATHS
        ${FILAMENT_ROOT}/lib
        /usr/local/lib
        /usr/lib
)

find_library(FILABRIDGE_LIBRARY
    NAMES filabridge
    PATHS
        ${FILAMENT_ROOT}/lib
        /usr/local/lib
        /usr/lib
)

find_library(UTILS_LIBRARY
    NAMES utils
    PATHS
        ${FILAMENT_ROOT}/lib
        /usr/local/lib
        /usr/lib
)

# 检查是否找到
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Filament
    REQUIRED_VARS
        FILAMENT_INCLUDE_DIR
        FILAMENT_LIBRARY
        FILABRIDGE_LIBRARY
        UTILS_LIBRARY
)

# 创建 imported targets
if(Filament_FOUND AND NOT TARGET filament::filament)
    add_library(filament::filament UNKNOWN IMPORTED)
    set_target_properties(filament::filament PROPERTIES
        IMPORTED_LOCATION "${FILAMENT_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FILAMENT_INCLUDE_DIR}"
    )
    
    add_library(filament::filabridge UNKNOWN IMPORTED)
    set_target_properties(filament::filabridge PROPERTIES
        IMPORTED_LOCATION "${FILABRIDGE_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FILAMENT_INCLUDE_DIR}"
    )
    
    add_library(filament::utils UNKNOWN IMPORTED)
    set_target_properties(filament::utils PROPERTIES
        IMPORTED_LOCATION "${UTILS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FILAMENT_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(
    FILAMENT_INCLUDE_DIR
    FILAMENT_LIBRARY
    FILABRIDGE_LIBRARY
    UTILS_LIBRARY
)
```

### 3.4 跨平台构建

**平台检测**：

```cmake
# 检测平台
if(WIN32)
    set(PLATFORM_NAME "Windows")
    add_definitions(-DPLATFORM_WINDOWS)
elseif(APPLE)
    if(IOS)
        set(PLATFORM_NAME "iOS")
        add_definitions(-DPLATFORM_IOS)
    else()
        set(PLATFORM_NAME "macOS")
        add_definitions(-DPLATFORM_MACOS)
    endif()
elseif(ANDROID)
    set(PLATFORM_NAME "Android")
    add_definitions(-DPLATFORM_ANDROID)
elseif(UNIX)
    set(PLATFORM_NAME "Linux")
    add_definitions(-DPLATFORM_LINUX)
endif()

message(STATUS "Building for platform: ${PLATFORM_NAME}")
```

**平台特定代码**：

```cpp
// src/core/src/Platform.cpp
#include <core/Platform.h>

#if defined(PLATFORM_WINDOWS)
    #include <windows.h>
#elif defined(PLATFORM_MACOS)
    #include <mach-o/dyld.h>
#elif defined(PLATFORM_LINUX)
    #include <unistd.h>
#endif

namespace myproject::core {

std::string Platform::getExecutablePath() {
#if defined(PLATFORM_WINDOWS)
    char buffer[MAX_PATH];
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return std::string(buffer);
    
#elif defined(PLATFORM_MACOS)
    char buffer[1024];
    uint32_t size = sizeof(buffer);
    _NSGetExecutablePath(buffer, &size);
    return std::string(buffer);
    
#elif defined(PLATFORM_LINUX)
    char buffer[1024];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        return std::string(buffer);
    }
#endif
    return "";
}

}  // namespace myproject::core
```

---

## 4. 资源管理架构

### 4.1 资源生命周期

大型项目中的资源管理是核心挑战之一，需要处理：
- **异步加载**：避免阻塞主线程
- **引用计数**：自动管理生命周期
- **缓存策略**：LRU、预加载、卸载
- **内存预算**：限制资源内存占用

**资源状态机**：

```
Unloaded → Loading → Loaded → Unloading → Unloaded
              ↓          ↓
            Failed    InUse
```

### 4.2 资源管理器实现

```cpp
// src/assets/include/assets/ResourceManager.h
#pragma once

#include <core/Types.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <functional>

namespace myproject::assets {

enum class ResourceState {
    Unloaded,
    Loading,
    Loaded,
    Failed,
    Unloading
};

// 资源基类
class Resource {
public:
    Resource(const std::string& path)
        : mPath(path)
        , mRefCount(0)
        , mState(ResourceState::Unloaded) {}
    
    virtual ~Resource() = default;
    
    const std::string& getPath() const { return mPath; }
    ResourceState getState() const { return mState.load(); }
    
    void addRef() { mRefCount.fetch_add(1); }
    void release() {
        if (mRefCount.fetch_sub(1) == 1) {
            // 引用计数归零，标记为可卸载
            mState.store(ResourceState::Unloading);
        }
    }
    
    int32_t getRefCount() const { return mRefCount.load(); }
    
protected:
    std::string mPath;
    std::atomic<int32_t> mRefCount;
    std::atomic<ResourceState> mState;
};

// 智能指针封装
template<typename T>
class ResourcePtr {
public:
    ResourcePtr() : mResource(nullptr) {}
    
    explicit ResourcePtr(T* res) : mResource(res) {
        if (mResource) mResource->addRef();
    }
    
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
    
    T* get() const { return mResource; }
    T* operator->() const { return mResource; }
    T& operator*() const { return *mResource; }
    
    explicit operator bool() const { return mResource != nullptr; }
    
private:
    T* mResource;
};

// 纹理资源
class TextureResource : public Resource {
public:
    TextureResource(const std::string& path) : Resource(path) {}
    
    filament::Texture* getTexture() const { return mTexture; }
    void setTexture(filament::Texture* tex) { mTexture = tex; }
    
private:
    filament::Texture* mTexture = nullptr;
};

// 模型资源
class ModelResource : public Resource {
public:
    ModelResource(const std::string& path) : Resource(path) {}
    
    FilamentAsset* getAsset() const { return mAsset; }
    void setAsset(FilamentAsset* asset) { mAsset = asset; }
    
private:
    FilamentAsset* mAsset = nullptr;
};

// 资源管理器
class ResourceManager {
public:
    ResourceManager(filament::Engine* engine);
    ~ResourceManager();
    
    // 同步加载（阻塞）
    ResourcePtr<TextureResource> loadTexture(const std::string& path);
    ResourcePtr<ModelResource> loadModel(const std::string& path);
    
    // 异步加载（非阻塞）
    using LoadCallback = std::function<void(Resource*)>;
    void loadTextureAsync(const std::string& path, LoadCallback callback);
    void loadModelAsync(const std::string& path, LoadCallback callback);
    
    // 预加载
    void preloadTexture(const std::string& path);
    void preloadModel(const std::string& path);
    
    // 卸载未使用的资源
    void unloadUnusedResources();
    
    // 内存管理
    size_t getMemoryUsage() const;
    void setMemoryBudget(size_t bytes);
    
private:
    filament::Engine* mEngine;
    
    std::mutex mMutex;
    std::unordered_map<std::string, TextureResource*> mTextures;
    std::unordered_map<std::string, ModelResource*> mModels;
    
    std::atomic<size_t> mMemoryUsage{0};
    std::atomic<size_t> mMemoryBudget{512 * 1024 * 1024};  // 512MB 默认
    
    // 加载队列
    struct LoadTask {
        std::string path;
        LoadCallback callback;
    };
    std::vector<LoadTask> mLoadQueue;
};

}  // namespace myproject::assets
```

**实现**：

```cpp
// src/assets/src/ResourceManager.cpp
#include <assets/ResourceManager.h>
#include <core/Logger.h>

namespace myproject::assets {

ResourceManager::ResourceManager(filament::Engine* engine)
    : mEngine(engine) {}

ResourceManager::~ResourceManager() {
    // 卸载所有资源
    for (auto& [path, tex] : mTextures) {
        if (tex->getTexture()) {
            mEngine->destroy(tex->getTexture());
        }
        delete tex;
    }
    mTextures.clear();
    
    for (auto& [path, model] : mModels) {
        // 销毁模型...
        delete model;
    }
    mModels.clear();
}

ResourcePtr<TextureResource> ResourceManager::loadTexture(const std::string& path) {
    std::lock_guard<std::mutex> lock(mMutex);
    
    // 检查缓存
    auto it = mTextures.find(path);
    if (it != mTextures.end()) {
        LOG_INFO("Texture already loaded: %s", path.c_str());
        return ResourcePtr<TextureResource>(it->second);
    }
    
    // 创建新资源
    TextureResource* resource = new TextureResource(path);
    resource->mState.store(ResourceState::Loading);
    
    // 实际加载逻辑（简化示例）
    int width, height, channels;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    
    if (!data) {
        LOG_ERROR("Failed to load texture: %s", path.c_str());
        resource->mState.store(ResourceState::Failed);
        delete resource;
        return ResourcePtr<TextureResource>(nullptr);
    }
    
    // 创建 Filament Texture
    filament::Texture* texture = filament::Texture::Builder()
        .width(static_cast<uint32_t>(width))
        .height(static_cast<uint32_t>(height))
        .levels(1)
        .format(filament::Texture::InternalFormat::RGBA8)
        .build(*mEngine);
    
    filament::Texture::PixelBufferDescriptor buffer(
        data, 
        size_t(width * height * 4),
        filament::Texture::Format::RGBA,
        filament::Texture::Type::UBYTE,
        [](void* buffer, size_t, void*) {
            stbi_image_free(buffer);
        }
    );
    
    texture->setImage(*mEngine, 0, std::move(buffer));
    
    resource->setTexture(texture);
    resource->mState.store(ResourceState::Loaded);
    
    // 更新内存使用
    size_t texSize = width * height * 4;
    mMemoryUsage.fetch_add(texSize);
    
    // 添加到缓存
    mTextures[path] = resource;
    
    LOG_INFO("Loaded texture: %s (%dx%d)", path.c_str(), width, height);
    
    return ResourcePtr<TextureResource>(resource);
}

void ResourceManager::unloadUnusedResources() {
    std::lock_guard<std::mutex> lock(mMutex);
    
    // 遍历所有纹理，卸载引用计数为0的
    for (auto it = mTextures.begin(); it != mTextures.end(); ) {
        if (it->second->getRefCount() == 0) {
            LOG_INFO("Unloading texture: %s", it->first.c_str());
            
            if (it->second->getTexture()) {
                mEngine->destroy(it->second->getTexture());
            }
            
            delete it->second;
            it = mTextures.erase(it);
        } else {
            ++it;
        }
    }
    
    // 同样处理模型...
}

size_t ResourceManager::getMemoryUsage() const {
    return mMemoryUsage.load();
}

void ResourceManager::setMemoryBudget(size_t bytes) {
    mMemoryBudget.store(bytes);
}

}  // namespace myproject::assets
```

**使用示例**：

```cpp
#include <assets/ResourceManager.h>

ResourceManager resMgr(engine);

// 加载纹理
auto texPtr = resMgr.loadTexture("assets/textures/wood.png");
if (texPtr) {
    material->setParameter("baseColorMap", texPtr->getTexture(), sampler);
}

// texPtr 离开作用域时自动减少引用计数
// 引用计数归零后，unloadUnusedResources() 会卸载它
```

### 4.3 LRU 缓存策略

当内存超过预算时，使用 LRU (Least Recently Used) 策略卸载最少使用的资源：

```cpp
class LRUCache {
public:
    void touch(const std::string& path) {
        auto it = mAccessList.find(path);
        if (it != mAccessList.end()) {
            mAccessList.erase(it);
        }
        mAccessList.push_front(path);
    }
    
    std::string getLRU() const {
        if (!mAccessList.empty()) {
            return mAccessList.back();
        }
        return "";
    }
    
    void remove(const std::string& path) {
        auto it = std::find(mAccessList.begin(), mAccessList.end(), path);
        if (it != mAccessList.end()) {
            mAccessList.erase(it);
        }
    }
    
private:
    std::list<std::string> mAccessList;  // 最近使用在前
};
```

---

## 5. 测试架构

### 5.1 单元测试

使用 Google Test 框架：

```cmake
# tests/CMakeLists.txt

find_package(GTest REQUIRED)

# Core 模块测试
add_executable(CoreTests
    core/LoggerTest.cpp
    core/MemoryTest.cpp
)

target_link_libraries(CoreTests
    PRIVATE
        core
        GTest::gtest
        GTest::gtest_main
)

add_test(NAME CoreTests COMMAND CoreTests)

# Rendering 模块测试
add_executable(RenderingTests
    rendering/MaterialManagerTest.cpp
    rendering/RenderSystemTest.cpp
)

target_link_libraries(RenderingTests
    PRIVATE
        rendering
        GTest::gtest
        GTest::gtest_main
)

add_test(NAME RenderingTests COMMAND RenderingTests)
```

**测试示例**：

```cpp
// tests/assets/ResourceManagerTest.cpp
#include <assets/ResourceManager.h>
#include <gtest/gtest.h>

class ResourceManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        mEngine = filament::Engine::create();
        mResMgr = std::make_unique<ResourceManager>(mEngine);
    }
    
    void TearDown() override {
        mResMgr.reset();
        filament::Engine::destroy(&mEngine);
    }
    
    filament::Engine* mEngine;
    std::unique_ptr<ResourceManager> mResMgr;
};

TEST_F(ResourceManagerTest, LoadTexture) {
    auto texPtr = mResMgr->loadTexture("test_data/test.png");
    
    ASSERT_NE(texPtr.get(), nullptr);
    EXPECT_EQ(texPtr->getState(), ResourceState::Loaded);
    EXPECT_NE(texPtr->getTexture(), nullptr);
}

TEST_F(ResourceManagerTest, TextureRefCounting) {
    auto texPtr1 = mResMgr->loadTexture("test_data/test.png");
    EXPECT_EQ(texPtr1->getRefCount(), 1);
    
    {
        auto texPtr2 = texPtr1;  // 拷贝
        EXPECT_EQ(texPtr1->getRefCount(), 2);
    }
    
    EXPECT_EQ(texPtr1->getRefCount(), 1);
}

TEST_F(ResourceManagerTest, UnloadUnused) {
    {
        auto texPtr = mResMgr->loadTexture("test_data/test.png");
        EXPECT_NE(texPtr.get(), nullptr);
    }
    
    // texPtr 离开作用域，引用计数归零
    mResMgr->unloadUnusedResources();
    
    // 再次加载应重新加载
    auto texPtr = mResMgr->loadTexture("test_data/test.png");
    EXPECT_NE(texPtr.get(), nullptr);
}
```

### 5.2 集成测试

测试多个模块协作：

```cpp
// tests/integration/RenderingIntegrationTest.cpp
#include <rendering/RenderSystem.h>
#include <assets/ResourceManager.h>
#include <scene/SceneManager.h>
#include <gtest/gtest.h>

TEST(RenderingIntegration, FullPipeline) {
    // 创建引擎
    auto engine = filament::Engine::create();
    
    // 创建各个模块
    ResourceManager resMgr(engine);
    RenderSystem renderSys(engine);
    SceneManager sceneMgr(engine);
    
    // 加载资源
    auto model = resMgr.loadModel("test_data/cube.glb");
    ASSERT_NE(model.get(), nullptr);
    
    // 添加到场景
    sceneMgr.addModel(model);
    
    // 渲染一帧
    renderSys.render(0.016f);
    
    // 清理
    filament::Engine::destroy(&engine);
}
```

### 5.3 性能测试

使用 Google Benchmark：

```cpp
// tests/benchmark/RenderingBenchmark.cpp
#include <benchmark/benchmark.h>
#include <rendering/RenderSystem.h>

static void BM_RenderFrame(benchmark::State& state) {
    auto engine = filament::Engine::create();
    RenderSystem renderSys(engine);
    
    for (auto _ : state) {
        renderSys.render(0.016f);
    }
    
    filament::Engine::destroy(&engine);
}

BENCHMARK(BM_RenderFrame);

static void BM_CreateMaterial(benchmark::State& state) {
    auto engine = filament::Engine::create();
    MaterialManager matMgr(engine);
    
    for (auto _ : state) {
        auto mat = matMgr.createMaterial("test_material");
        matMgr.destroyMaterial(mat);
    }
    
    filament::Engine::destroy(&engine);
}

BENCHMARK(BM_CreateMaterial);

BENCHMARK_MAIN();
```

---

## 6. 场景图架构

### 6.1 场景图设计

场景图用于组织场景中的对象层次结构：

```cpp
// src/scene/include/scene/SceneNode.h
#pragma once

#include <core/Types.h>
#include <filament/TransformManager.h>
#include <vector>
#include <string>

namespace myproject::scene {

class SceneNode {
public:
    SceneNode(const std::string& name, Entity entity)
        : mName(name)
        , mEntity(entity)
        , mParent(nullptr)
        , mLocalTransform(filament::math::mat4f(1.0f))
        , mWorldTransform(filament::math::mat4f(1.0f))
        , mDirty(true) {}
    
    ~SceneNode() {
        // 递归删除子节点
        for (auto* child : mChildren) {
            delete child;
        }
    }
    
    // 层次结构
    void addChild(SceneNode* child) {
        child->mParent = this;
        mChildren.push_back(child);
        child->markDirty();
    }
    
    void removeChild(SceneNode* child) {
        auto it = std::find(mChildren.begin(), mChildren.end(), child);
        if (it != mChildren.end()) {
            (*it)->mParent = nullptr;
            mChildren.erase(it);
        }
    }
    
    SceneNode* getParent() const { return mParent; }
    const std::vector<SceneNode*>& getChildren() const { return mChildren; }
    
    // 变换
    void setLocalTransform(const filament::math::mat4f& transform) {
        mLocalTransform = transform;
        markDirty();
    }
    
    const filament::math::mat4f& getLocalTransform() const {
        return mLocalTransform;
    }
    
    const filament::math::mat4f& getWorldTransform() {
        if (mDirty) {
            updateWorldTransform();
        }
        return mWorldTransform;
    }
    
    // Entity
    Entity getEntity() const { return mEntity; }
    
    // 名称
    const std::string& getName() const { return mName; }
    
private:
    void markDirty() {
        if (!mDirty) {
            mDirty = true;
            // 递归标记子节点
            for (auto* child : mChildren) {
                child->markDirty();
            }
        }
    }
    
    void updateWorldTransform() {
        if (mParent) {
            mWorldTransform = mParent->getWorldTransform() * mLocalTransform;
        } else {
            mWorldTransform = mLocalTransform;
        }
        mDirty = false;
    }
    
    std::string mName;
    Entity mEntity;
    SceneNode* mParent;
    std::vector<SceneNode*> mChildren;
    
    filament::math::mat4f mLocalTransform;
    filament::math::mat4f mWorldTransform;
    bool mDirty;
};

}  // namespace myproject::scene
```

**使用示例**：

```cpp
// 创建场景图
SceneNode* root = new SceneNode("Root", rootEntity);

SceneNode* car = new SceneNode("Car", carEntity);
root->addChild(car);

SceneNode* wheel1 = new SceneNode("Wheel1", wheel1Entity);
SceneNode* wheel2 = new SceneNode("Wheel2", wheel2Entity);
car->addChild(wheel1);
car->addChild(wheel2);

// 移动车辆，轮子会自动跟随
car->setLocalTransform(filament::math::mat4f::translation({10, 0, 0}));

// 获取轮子的世界坐标
auto worldTrans = wheel1->getWorldTransform();
```

### 6.2 ECS 集成

将场景图与 Filament 的 ECS 集成：

```cpp
class SceneManager {
public:
    SceneManager(filament::Engine* engine)
        : mEngine(engine)
        , mScene(mEngine->createScene())
        , mRootNode(new SceneNode("Root", EntityManager::get().create())) {
        mScene->addEntity(mRootNode->getEntity());
    }
    
    ~SceneManager() {
        delete mRootNode;
        mEngine->destroy(mScene);
    }
    
    SceneNode* getRootNode() { return mRootNode; }
    
    // 更新所有变换到 TransformManager
    void updateTransforms() {
        auto& tcm = mEngine->getTransformManager();
        updateNodeTransforms(mRootNode, tcm);
    }
    
private:
    void updateNodeTransforms(SceneNode* node, filament::TransformManager& tcm) {
        auto ti = tcm.getInstance(node->getEntity());
        if (ti) {
            tcm.setTransform(ti, node->getWorldTransform());
        }
        
        for (auto* child : node->getChildren()) {
            updateNodeTransforms(child, tcm);
        }
    }
    
    filament::Engine* mEngine;
    filament::Scene* mScene;
    SceneNode* mRootNode;
};
```

---

## 7. 多平台支持

### 7.1 平台抽象层

```cpp
// src/platform/include/platform/IPlatform.h
#pragma once

#include <string>

namespace myproject::platform {

class IPlatform {
public:
    virtual ~IPlatform() = default;
    
    virtual std::string getExecutablePath() const = 0;
    virtual std::string getDocumentsPath() const = 0;
    virtual std::string getTempPath() const = 0;
    
    virtual void* getNativeWindowHandle() const = 0;
    virtual void getWindowSize(uint32_t& width, uint32_t& height) const = 0;
    
    virtual bool isHighDPI() const = 0;
    virtual float getDPIScale() const = 0;
};

// 工厂函数
IPlatform* createPlatform();

}  // namespace myproject::platform
```

**Windows 实现**：

```cpp
// src/platform/src/WindowsPlatform.cpp
#include <platform/IPlatform.h>

#if defined(PLATFORM_WINDOWS)

#include <windows.h>
#include <shlobj.h>

namespace myproject::platform {

class WindowsPlatform : public IPlatform {
public:
    std::string getExecutablePath() const override {
        char buffer[MAX_PATH];
        GetModuleFileNameA(nullptr, buffer, MAX_PATH);
        return std::string(buffer);
    }
    
    std::string getDocumentsPath() const override {
        char buffer[MAX_PATH];
        SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, buffer);
        return std::string(buffer);
    }
    
    std::string getTempPath() const override {
        char buffer[MAX_PATH];
        GetTempPathA(MAX_PATH, buffer);
        return std::string(buffer);
    }
    
    void* getNativeWindowHandle() const override {
        return mHwnd;
    }
    
    void getWindowSize(uint32_t& width, uint32_t& height) const override {
        RECT rect;
        GetClientRect(mHwnd, &rect);
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
    }
    
    bool isHighDPI() const override {
        return getDPIScale() > 1.0f;
    }
    
    float getDPIScale() const override {
        HDC hdc = GetDC(nullptr);
        int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
        return dpi / 96.0f;
    }
    
private:
    HWND mHwnd = nullptr;
};

IPlatform* createPlatform() {
    return new WindowsPlatform();
}

}  // namespace myproject::platform

#endif  // PLATFORM_WINDOWS
```

**macOS 实现**：

```cpp
// src/platform/src/MacOSPlatform.mm
#include <platform/IPlatform.h>

#if defined(PLATFORM_MACOS)

#import <Cocoa/Cocoa.h>
#include <mach-o/dyld.h>

namespace myproject::platform {

class MacOSPlatform : public IPlatform {
public:
    std::string getExecutablePath() const override {
        char buffer[1024];
        uint32_t size = sizeof(buffer);
        _NSGetExecutablePath(buffer, &size);
        return std::string(buffer);
    }
    
    std::string getDocumentsPath() const override {
        NSArray* paths = NSSearchPathForDirectoriesInDomains(
            NSDocumentDirectory, NSUserDomainMask, YES);
        NSString* documentsDirectory = [paths firstObject];
        return std::string([documentsDirectory UTF8String]);
    }
    
    std::string getTempPath() const override {
        NSString* tempDir = NSTemporaryDirectory();
        return std::string([tempDir UTF8String]);
    }
    
    void* getNativeWindowHandle() const override {
        return (__bridge void*)mNSView;
    }
    
    void getWindowSize(uint32_t& width, uint32_t& height) const override {
        NSRect frame = [mNSView frame];
        width = static_cast<uint32_t>(frame.size.width);
        height = static_cast<uint32_t>(frame.size.height);
    }
    
    bool isHighDPI() const override {
        return [[NSScreen mainScreen] backingScaleFactor] > 1.0;
    }
    
    float getDPIScale() const override {
        return static_cast<float>([[NSScreen mainScreen] backingScaleFactor]);
    }
    
private:
    NSView* mNSView = nullptr;
};

IPlatform* createPlatform() {
    return new MacOSPlatform();
}

}  // namespace myproject::platform

#endif  // PLATFORM_MACOS
```

### 7.2 条件编译

```cpp
// 跨平台窗口创建示例
#include <platform/IPlatform.h>

void createNativeSwapChain(filament::Engine* engine) {
    auto platform = createPlatform();
    void* nativeWindow = platform->getNativeWindowHandle();
    
    uint32_t width, height;
    platform->getWindowSize(width, height);
    
#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX)
    mSwapChain = engine->createSwapChain(nativeWindow);
    
#elif defined(PLATFORM_MACOS)
    mSwapChain = engine->createSwapChain(nativeWindow, 
        filament::SwapChain::CONFIG_TRANSPARENT);
    
#elif defined(PLATFORM_IOS)
    mSwapChain = engine->createSwapChain(nativeWindow, 
        filament::SwapChain::CONFIG_HAS_STENCIL_BUFFER);
#endif
    
    delete platform;
}
```

---

## 8. 性能监控

### 8.1 Profiler 集成

```cpp
// src/core/include/core/Profiler.h
#pragma once

#include <chrono>
#include <string>
#include <unordered_map>

namespace myproject::core {

class Profiler {
public:
    static Profiler& getInstance() {
        static Profiler instance;
        return instance;
    }
    
    void beginSection(const char* name) {
        mSections[name].start = std::chrono::high_resolution_clock::now();
    }
    
    void endSection(const char* name) {
        auto end = std::chrono::high_resolution_clock::now();
        auto& section = mSections[name];
        section.duration = std::chrono::duration<double, std::milli>(
            end - section.start).count();
    }
    
    double getSectionTime(const char* name) const {
        auto it = mSections.find(name);
        if (it != mSections.end()) {
            return it->second.duration;
        }
        return 0.0;
    }
    
    void printStats() const {
        for (const auto& [name, section] : mSections) {
            printf("%s: %.3f ms\n", name.c_str(), section.duration);
        }
    }
    
private:
    struct Section {
        std::chrono::high_resolution_clock::time_point start;
        double duration = 0.0;
    };
    
    std::unordered_map<std::string, Section> mSections;
};

// RAII 封装
class ProfileScope {
public:
    ProfileScope(const char* name) : mName(name) {
        Profiler::getInstance().beginSection(mName);
    }
    
    ~ProfileScope() {
        Profiler::getInstance().endSection(mName);
    }
    
private:
    const char* mName;
};

#define PROFILE_SCOPE(name) ProfileScope _profScope##__LINE__(name)
#define PROFILE_FUNCTION() PROFILE_SCOPE(__FUNCTION__)

}  // namespace myproject::core
```

**使用**：

```cpp
void RenderSystem::render(float deltaTime) {
    PROFILE_FUNCTION();
    
    {
        PROFILE_SCOPE("Culling");
        performCulling();
    }
    
    {
        PROFILE_SCOPE("Shadow Pass");
        renderShadows();
    }
    
    {
        PROFILE_SCOPE("Main Pass");
        renderMain();
    }
}

// 打印统计
Profiler::getInstance().printStats();
// 输出:
// RenderSystem::render: 16.7 ms
// Culling: 2.3 ms
// Shadow Pass: 5.1 ms
// Main Pass: 9.3 ms
```

---

## 9. 日志系统

### 9.1 日志接口

```cpp
// src/core/include/core/Logger.h
#pragma once

#include <cstdio>
#include <cstdarg>

namespace myproject::core {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }
    
    void setLevel(LogLevel level) { mLevel = level; }
    
    void log(LogLevel level, const char* file, int line, 
             const char* fmt, ...) {
        if (level < mLevel) return;
        
        const char* levelStr[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
        printf("[%s] %s:%d - ", levelStr[static_cast<int>(level)], file, line);
        
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
        
        printf("\n");
        fflush(stdout);
    }
    
private:
    LogLevel mLevel = LogLevel::Info;
};

#define LOG_DEBUG(fmt, ...) \
    Logger::getInstance().log(LogLevel::Debug, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
    Logger::getInstance().log(LogLevel::Info, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_WARNING(fmt, ...) \
    Logger::getInstance().log(LogLevel::Warning, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    Logger::getInstance().log(LogLevel::Error, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_FATAL(fmt, ...) \
    Logger::getInstance().log(LogLevel::Fatal, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

}  // namespace myproject::core
```

---

## 10. 配置管理

### 10.1 配置文件

使用 JSON 或 YAML 管理配置：

```json
// config.json
{
  "rendering": {
    "backend": "vulkan",
    "msaa": 4,
    "bloom": true,
    "shadowMapSize": 2048
  },
  "resources": {
    "memoryBudget": 536870912,
    "preloadModels": [
      "assets/models/player.glb",
      "assets/models/environment.glb"
    ]
  },
  "window": {
    "width": 1920,
    "height": 1080,
    "fullscreen": false,
    "vsync": true
  }
}
```

**配置加载器**：

```cpp
#include <nlohmann/json.hpp>
#include <fstream>

class Config {
public:
    static Config& getInstance() {
        static Config instance;
        return instance;
    }
    
    void load(const std::string& path) {
        std::ifstream file(path);
        file >> mJson;
    }
    
    template<typename T>
    T get(const std::string& key, const T& defaultValue = T{}) const {
        try {
            return mJson.at(key).get<T>();
        } catch (...) {
            return defaultValue;
        }
    }
    
private:
    nlohmann::json mJson;
};

// 使用
Config::getInstance().load("config.json");
int msaa = Config::getInstance().get<int>("rendering.msaa", 1);
```

---

## 11. 最佳实践总结

1. **模块化**：清晰的模块边界，避免循环依赖
2. **依赖注入**：解耦模块，方便测试和替换
3. **接口隔离**：公共接口稳定，实现细节隐藏（Pimpl）
4. **资源管理**：引用计数、LRU 缓存、内存预算
5. **跨平台**：平台抽象层，条件编译最小化
6. **测试**：单元测试、集成测试、性能测试
7. **日志和监控**：完善的日志系统和性能监控
8. **配置管理**：外部配置文件，支持运行时修改

---

## 12. 常见问题

### Q1: 如何避免循环依赖？

**A**: 使用依赖倒置原则，让高层模块和低层模块都依赖抽象接口。如果必须通信，使用事件系统或观察者模式。

### Q2: 如何管理大量的第三方库？

**A**: 使用 CMake 的 FetchContent 或 Git Submodules，将第三方库统一放在 third_party/ 目录。

### Q3: 如何支持动态库和静态库？

**A**: CMake 中使用 BUILD_SHARED_LIBS 选项：
```cmake
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
add_library(mylib ${SOURCES})  # 根据 BUILD_SHARED_LIBS 自动选择
```

### Q4: 如何处理资源的热重载？

**A**: 使用文件监控（FileWatcher），检测资源文件变化时触发重新加载。需要确保加载过程是线程安全的。

---

## 13. 相关文档

- [01-data-flow-analysis.md](./01-data-flow-analysis.md) - 数据流架构
- [04-ecs-best-practices.md](./04-ecs-best-practices.md) - ECS 最佳实践
- [07-async-loading.md](./07-async-loading.md) - 异步加载
- [08-resource-streaming.md](./08-resource-streaming.md) - 资源流式加载

---

## 14. 总结

大型 Filament 项目的架构设计需要综合考虑：

1. **模块化设计**：清晰的模块划分和依赖关系
2. **构建系统**：CMake 最佳实践，支持多平台
3. **资源管理**：高效的加载、缓存、卸载策略
4. **代码组织**：统一的目录结构和命名规范
5. **测试架构**：完善的测试覆盖和性能监控
6. **跨平台支持**：平台抽象层，最小化平台差异
7. **可维护性**：良好的文档、日志、配置管理

通过遵循这些架构原则和最佳实践，可以构建出高质量、可扩展、易维护的商业级 Filament 渲染应用。
