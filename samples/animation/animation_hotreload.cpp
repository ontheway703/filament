/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Sample 2: animation_hotreload.cpp
 *
 * 【验证目标】外部动画的运行时热加载/热卸载能力
 *
 * 演示场景：
 * 1. 启动时只加载网格（不含动画）
 * 2. 按 R 键在运行时动态加载外部动画
 * 3. 按 U 键在运行时动态卸载外部动画
 * 4. 按 T 键运行 100 次加载/卸载循环测试
 *
 * 【核心验证点】
 * - 验证多次加载/卸载循环不会产生内存泄漏
 * - 验证卸载后状态管理的正确性
 * - 验证卸载后动画索引的边界处理
 * - 验证运行时加载不会影响已有渲染流程
 *
 * 【gltfio_ext 关键特性】
 * - AnimationAsset 可以独立于 FilamentAsset 生命周期
 * - Animator 支持运行时绑定和解绑外部动画
 * - hasExternalAnimation() 用于查询当前绑定状态
 * - 卸载后需要检查 currentAnimation 索引是否仍然有效
 */

#include "common/AnimationUtils.h"

#include <SDL.h>

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/Renderer.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/SwapChain.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <backend/DriverEnums.h>

#include <gltfio_ext/AssetLoader.h>
#include <gltfio_ext/AnimationAsset.h>
#include <gltfio_ext/Animator.h>
#include <gltfio_ext/FilamentAsset.h>
#include <gltfio_ext/FilamentInstance.h>
#include <gltfio_ext/MaterialProvider.h>
#include <gltfio_ext/ResourceLoader.h>
#include <gltfio_ext/TextureProvider.h>

#include <math/mat4.h>
#include <math/vec3.h>
#include <utils/EntityManager.h>
#include <utils/NameComponentManager.h>

#include <iostream>
#include <chrono>
#include <thread>

using namespace filament;
using namespace filament::math;
using namespace utils;

static constexpr int WINDOW_WIDTH = 1024;
static constexpr int WINDOW_HEIGHT = 768;

// Asset paths from PLAN.md
static constexpr const char* MESH_GLB = "ecorche_mesh_only.glb";
static constexpr const char* ANIM_GLB = "ecorche_animation_only.glb";

struct App {
    // SDL and window
    SDL_Window* window = nullptr;

    // Filament core
    Engine* engine = nullptr;
    Renderer* renderer = nullptr;
    SwapChain* swapChain = nullptr;
    View* view = nullptr;
    Scene* scene = nullptr;
    Camera* camera = nullptr;
    Entity cameraEntity;

    // 【gltfio_ext 资产加载器】
    // 注意：必须全部使用 gltfio_ext 命名空间的类型，不能混用 gltfio 类型
    gltfio_ext::AssetLoader* assetLoader = nullptr;
    gltfio_ext::MaterialProvider* materialProvider = nullptr;
    utils::NameComponentManager* nameManager = nullptr;
    gltfio_ext::FilamentAsset* meshAsset = nullptr;           // 网格资产（包含骨骼，不含动画）
    gltfio_ext::ResourceLoader* resourceLoader = nullptr;
    gltfio_ext::TextureProvider* stbProvider = nullptr;
    gltfio_ext::TextureProvider* ktx2Provider = nullptr;
    gltfio_ext::Animator* animator = nullptr;                 // 动画播放器（从 meshAsset 获取）

    // 【gltfio_ext 核心特性】外部动画资产
    // animAsset 可以在运行时动态加载/卸载，独立于 meshAsset 生命周期
    gltfio_ext::AnimationAsset* animAsset = nullptr;

    // Lighting
    Entity keyLight, fillLight;

    // 【动画状态管理】
    size_t internalAnimCount = 0;    // 内部动画数量（来自网格 GLB，本示例为 0）
    size_t totalAnimCount = 0;       // 总动画数量（内部 + 外部）
    int currentAnimation = 0;        // 当前播放的动画索引
    float animTime = 0.0f;           // 当前动画时间（秒）
    bool animPlaying = true;         // 是否正在播放

    // 【运行时热加载状态】
    // cachedAnimBytes：缓存的动画文件数据，用于多次加载测试而无需重复读文件
    // loadUnloadCycleCount：加载/卸载循环次数计数器，用于内存泄漏测试
    std::vector<uint8_t> cachedAnimBytes;
    int loadUnloadCycleCount = 0;
};

static bool initSDL(App& app) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return false;
    }

    app.window = SDL_CreateWindow(
        "Animation Hotreload Sample",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE
    );

    if (!app.window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        return false;
    }

    return true;
}

static bool initFilament(App& app) {
    Engine::Config config = {};
    config.commandBufferSizeMB = 32;
    config.minCommandBufferSizeMB = 8;

    app.engine = Engine::create(filament::backend::Backend::DEFAULT, nullptr, nullptr, &config);
    if (!app.engine) {
        std::cerr << "Failed to create Filament engine" << std::endl;
        return false;
    }

    // Create swap chain with native window
    void* nativeView = animation_utils::getNativeWindow(app.window);
    if (!nativeView) {
        std::cerr << "Failed to get native window" << std::endl;
        return false;
    }

#if defined(__APPLE__)
    void* metalLayer = animation_utils::setUpMetalLayer(nativeView);
    if (!metalLayer) {
        std::cerr << "Failed to set up Metal layer" << std::endl;
        return false;
    }
    app.swapChain = app.engine->createSwapChain(metalLayer, SwapChain::CONFIG_HAS_STENCIL_BUFFER);
#else
    app.swapChain = app.engine->createSwapChain(nativeView, SwapChain::CONFIG_HAS_STENCIL_BUFFER);
#endif

    if (!app.swapChain) {
        std::cerr << "Failed to create swap chain" << std::endl;
        return false;
    }

    // Create core components
    app.renderer = app.engine->createRenderer();
    app.scene = app.engine->createScene();
    app.view = app.engine->createView();
    app.view->setScene(app.scene);

    // Set viewport using actual drawable size for HiDPI support
    int vpw = 0, vph = 0;
    SDL_GL_GetDrawableSize(app.window, &vpw, &vph);
    app.view->setViewport({0, 0, (uint32_t)vpw, (uint32_t)vph});

    // Setup camera
    app.cameraEntity = EntityManager::get().create();
    app.camera = app.engine->createCamera(app.cameraEntity);
    app.view->setCamera(app.camera);

    const double aspect = (double)vpw / vph;
    app.camera->setProjection(45.0, aspect, 0.1, 100.0);
    app.camera->lookAt({4.0f, 1.0f, 4.0f}, {0, 0, 0}, {0, 1, 0});

    // Create material providers (gltfio_ext)
    app.materialProvider = gltfio_ext::createJitShaderProvider(app.engine);
    app.nameManager = new utils::NameComponentManager(EntityManager::get());
    app.stbProvider = gltfio_ext::createStbProvider(app.engine);
    app.ktx2Provider = gltfio_ext::createKtx2Provider(app.engine);

    // Setup basic lighting
    app.keyLight = EntityManager::get().create();
    LightManager::Builder(LightManager::Type::DIRECTIONAL)
        .color({1.0f, 0.98f, 0.95f})
        .intensity(120000.0f)
        .direction({0.3f, -0.8f, -0.5f})
        .castShadows(false)
        .build(*app.engine, app.keyLight);
    app.scene->addEntity(app.keyLight);

    app.fillLight = EntityManager::get().create();
    LightManager::Builder(LightManager::Type::DIRECTIONAL)
        .color({0.9f, 0.95f, 1.0f})
        .intensity(80000.0f)
        .direction({-0.7f, -0.3f, 0.6f})
        .castShadows(false)
        .build(*app.engine, app.fillLight);
    app.scene->addEntity(app.fillLight);

    return true;
}

static bool loadMesh(App& app) {
    std::cout << "\n=== Loading mesh asset ===" << std::endl;

    // Resolve and read mesh GLB
    std::string meshPath = animation_utils::getAssetPath(MESH_GLB);
    std::vector<uint8_t> meshBytes = animation_utils::readFile(meshPath);
    if (meshBytes.empty()) {
        std::cerr << "Failed to read mesh GLB: " << meshPath << std::endl;
        return false;
    }

    // Create mesh AssetLoader
    gltfio_ext::AssetConfiguration meshConfig;
    meshConfig.engine = app.engine;
    meshConfig.materials = app.materialProvider;
    meshConfig.names = app.nameManager;

    app.assetLoader = gltfio_ext::AssetLoader::create(meshConfig);
    if (!app.assetLoader) {
        std::cerr << "Failed to create AssetLoader" << std::endl;
        return false;
    }

    // Create mesh asset
    app.meshAsset = app.assetLoader->createAsset(meshBytes.data(), meshBytes.size());
    if (!app.meshAsset) {
        std::cerr << "Failed to create mesh asset" << std::endl;
        return false;
    }

    // Load resources (textures, buffers, etc.)
    gltfio_ext::ResourceConfiguration config = {};
    config.engine = app.engine;
    config.gltfPath = meshPath.c_str();
    config.normalizeSkinningWeights = true;

    app.resourceLoader = new gltfio_ext::ResourceLoader(config);
    app.resourceLoader->addTextureProvider("image/png", app.stbProvider);
    app.resourceLoader->addTextureProvider("image/jpeg", app.stbProvider);
    app.resourceLoader->addTextureProvider("image/ktx2", app.ktx2Provider);

    if (!app.resourceLoader->loadResources(app.meshAsset)) {
        if (!app.resourceLoader->asyncBeginLoad(app.meshAsset)) {
            std::cerr << "Failed to load mesh resources" << std::endl;
            return false;
        }

        // Wait for async loading to complete
        while (app.resourceLoader->asyncGetLoadProgress() < 1.0f) {
            if (!UTILS_HAS_THREADING) {
                app.engine->execute();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    app.meshAsset->releaseSourceData();

    // Add mesh entities to scene
    app.scene->addEntities(app.meshAsset->getEntities(), app.meshAsset->getEntityCount());

    // Get Animator immediately after loading mesh (difference from Sample 1)
    if (auto* instance = app.meshAsset->getInstance()) {
        app.animator = instance->getAnimator();
    }

    if (!app.animator) {
        std::cerr << "Failed to get Animator from mesh instance" << std::endl;
        return false;
    }

    // Get internal animation count
    app.internalAnimCount = app.animator->getAnimationCount();
    app.totalAnimCount = app.internalAnimCount;

    std::cout << "Mesh loaded successfully" << std::endl;
    std::cout << "Internal animations in mesh: " << app.internalAnimCount << std::endl;

    return true;
}

// Runtime load external animation (R key handler)
// 【验证核心功能】运行时动态加载外部动画（R 键处理器）
//
// 【验证点】
// 1. 验证 AnimationAsset 可以在渲染循环运行时动态加载
// 2. 验证 hasExternalAnimation() 正确查询绑定状态
// 3. 验证加载后 totalAnimCount 正确更新
// 4. 验证多次加载不会产生内存泄漏
//
// 【gltfio_ext 关键 API 使用流程】
// 步骤 1: hasExternalAnimation() - 检查是否已绑定外部动画，避免重复加载
// 步骤 2: loadAnimationAsset() - 从内存缓冲区创建 AnimationAsset 对象
// 步骤 3: loadExternalAnimation() - 将 AnimationAsset 绑定到 Animator
// 步骤 4: getAnimationCount() - 获取更新后的总动画数量
static bool runtimeLoadAnimation(App& app) {
    std::cout << "\n[R] Loading external animation..." << std::endl;

    // 【gltfio_ext API】hasExternalAnimation() - 查询当前是否已绑定外部动画
    // 避免重复加载，防止资源泄漏
    if (app.animator->hasExternalAnimation()) {
        std::cout << "[ERROR] External animation already loaded!" << std::endl;
        return false;
    }

    // 【gltfio_ext API】loadAnimationAsset() - 从内存数据创建 AnimationAsset
    // 参数：动画 GLB 文件的内存缓冲区指针和大小
    // 返回：AnimationAsset* 对象，失败返回 nullptr
    app.animAsset = app.assetLoader->loadAnimationAsset(
        app.cachedAnimBytes.data(),
        app.cachedAnimBytes.size()
    );

    if (!app.animAsset) {
        std::cerr << "[ERROR] Failed to load AnimationAsset" << std::endl;
        return false;
    }

    // 【gltfio_ext 核心 API】loadExternalAnimation() - 将外部动画绑定到 Animator
    // 这是 gltfio_ext 的核心特性：运行时绑定外部动画数据
    // 绑定后，Animator 的动画数量会增加（内部动画 + 外部动画）
    if (!app.animator->loadExternalAnimation(app.animAsset)) {
        app.assetLoader->destroyAnimationAsset(app.animAsset);
        app.animAsset = nullptr;
        std::cerr << "[ERROR] Failed to bind external animation to Animator" << std::endl;
        return false;
    }

    // 【状态更新】加载后需要更新动画总数
    app.totalAnimCount = app.animator->getAnimationCount();
    app.loadUnloadCycleCount++;

    std::cout << "[SUCCESS] External animation loaded!" << std::endl;
    std::cout << "  Total animations: " << app.totalAnimCount << std::endl;
    std::cout << "  External animations: " << (app.totalAnimCount - app.internalAnimCount) << std::endl;
    std::cout << "  Load/unload cycle count: " << app.loadUnloadCycleCount << std::endl;

    return true;
}

// 【验证核心功能】运行时动态卸载外部动画（U 键处理器）
//
// 【验证点】
// 1. 验证 AnimationAsset 可以在渲染循环运行时动态卸载
// 2. 验证卸载后 totalAnimCount 正确更新
// 3. 验证卸载后动画索引边界处理的正确性（关键！）
// 4. 验证卸载不会导致 Animator 崩溃或状态异常
//
// 【gltfio_ext 关键 API 使用流程】
// 步骤 1: hasExternalAnimation() - 检查是否有外部动画可卸载
// 步骤 2: unloadExternalAnimation() - 从 Animator 解绑外部动画
// 步骤 3: destroyAnimationAsset() - 销毁 AnimationAsset 对象
// 步骤 4: getAnimationCount() - 获取更新后的动画数量
// 步骤 5: 【关键】检查 currentAnimation 索引，防止越界访问
static bool runtimeUnloadAnimation(App& app) {
    std::cout << "\n[U] Unloading external animation..." << std::endl;

    // 【gltfio_ext API】hasExternalAnimation() - 检查是否有外部动画
    if (!app.animator->hasExternalAnimation()) {
        std::cout << "[ERROR] No external animation to unload!" << std::endl;
        return false;
    }

    // 【gltfio_ext 核心 API】unloadExternalAnimation() - 解绑外部动画
    // 这会将外部动画从 Animator 中移除，但不会销毁 AnimationAsset 对象
    // Animator 的动画数量会减少（移除外部动画）
    app.animator->unloadExternalAnimation();

    // 【gltfio_ext API】destroyAnimationAsset() - 销毁 AnimationAsset 对象
    // 注意：必须先 unload 再 destroy，顺序不能错
    if (app.animAsset) {
        app.assetLoader->destroyAnimationAsset(app.animAsset);
        app.animAsset = nullptr;
    }

    // 【状态更新】卸载后需要更新动画总数
    app.totalAnimCount = app.animator->getAnimationCount();

    // 【关键逻辑】卸载后动画索引边界检查
    // 场景：如果当前播放的是外部动画（索引 >= internalAnimCount），
    // 卸载后该索引会越界，必须重置为有效值
    // 示例：内部动画 0 个，外部动画 3 个，当前播放索引 2
    //      卸载后：总数变为 0，索引 2 越界 -> 重置为 0
    if (app.currentAnimation >= (int)app.totalAnimCount) {
        app.currentAnimation = 0;
        app.animTime = 0.0f;
        std::cout << "[INFO] Current animation index reset to 0" << std::endl;
    }

    std::cout << "[SUCCESS] External animation unloaded!" << std::endl;
    std::cout << "  Total animations: " << app.totalAnimCount << std::endl;
    std::cout << "  Current animation: " << app.currentAnimation << std::endl;

    return true;
}

// 【验证核心功能】内存泄漏测试（T 键处理器）
//
// 【验证目标】
// 通过 100 次连续的加载/卸载循环，验证 gltfio_ext 的外部动画管理不会产生内存泄漏
//
// 【验证方法】
// 1. 循环 100 次：load -> bind -> use -> unbind -> destroy
// 2. 每次循环使用临时 AnimationAsset 对象
// 3. 每个循环必须完整清理，不留任何引用
// 4. 循环结束后，内存使用应恢复到测试前水平
//
// 【gltfio_ext API 压力测试】
// 测试 API 序列的健壮性：
// - loadAnimationAsset() 能否正确管理内存分配
// - loadExternalAnimation() 能否正确建立引用关系
// - unloadExternalAnimation() 能否正确清除引用
// - destroyAnimationAsset() 能否正确释放内存
//
// 【如何验证无泄漏】
// macOS: 使用 Instruments 的 Leaks 工具监控内存
// Linux: 使用 Valgrind 检测内存泄漏
// 预期结果：循环结束后内存使用量应回到测试前水平
static void runLeakTest(App& app) {
    const int CYCLES = 100;
    std::cout << "\n=== Starting Memory Leak Test ===" << std::endl;
    std::cout << "Cycles: " << CYCLES << std::endl;
    std::cout << "This will load/unload animation " << CYCLES << " times." << std::endl;

    auto startTime = std::chrono::steady_clock::now();

    for (int i = 0; i < CYCLES; i++) {
        // 【步骤 1】加载 AnimationAsset（分配内存）
        auto* tempAsset = app.assetLoader->loadAnimationAsset(
            app.cachedAnimBytes.data(),
            app.cachedAnimBytes.size()
        );

        if (!tempAsset) {
            std::cerr << "Cycle " << i << ": Load failed!" << std::endl;
            break;
        }

        // 【步骤 2】绑定到 Animator（建立引用关系）
        if (!app.animator->loadExternalAnimation(tempAsset)) {
            app.assetLoader->destroyAnimationAsset(tempAsset);
            std::cerr << "Cycle " << i << ": Bind failed!" << std::endl;
            break;
        }

        // 【步骤 3】模拟使用动画（验证绑定后功能正常）
        if (app.animator->getAnimationCount() > app.internalAnimCount) {
            app.animator->applyAnimation(app.internalAnimCount, 0.5f);
            app.animator->updateBoneMatrices();
        }

        // 【步骤 4】解绑（清除引用关系）
        app.animator->unloadExternalAnimation();

        // 【步骤 5】销毁 AnimationAsset（释放内存）
        // 关键：必须在 unload 之后才能 destroy
        app.assetLoader->destroyAnimationAsset(tempAsset);

        // Progress report every 10 cycles
        if ((i + 1) % 10 == 0) {
            std::cout << "  Progress: " << (i + 1) << "/" << CYCLES << std::endl;
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    std::cout << "\n=== Test Complete ===" << std::endl;
    std::cout << "Total time: " << duration.count() << " ms" << std::endl;
    std::cout << "Average per cycle: " << (duration.count() / (double)CYCLES) << " ms" << std::endl;
    std::cout << "\nUse Instruments (macOS) or Valgrind (Linux) to verify no memory leaks." << std::endl;
}

static void updateAnimation(App& app, double deltaTime) {
    if (!app.animator) return;

    if (app.totalAnimCount == 0) return;

    if (app.currentAnimation < 0 || app.currentAnimation >= (int)app.totalAnimCount) {
        app.currentAnimation = 0;
    }

    if (app.animPlaying) {
        const float duration = app.animator->getAnimationDuration((size_t)app.currentAnimation);
        if (duration > 0.0f) {
            app.animTime += (float)deltaTime;
            // Loop animation
            if (app.animTime > duration) {
                app.animTime = fmod(app.animTime, duration);
            }
        }
    }

    app.animator->applyAnimation((size_t)app.currentAnimation, app.animTime);
    app.animator->updateBoneMatrices();
}

static void cleanup(App& app) {
    if (app.engine) {
        // Unload external animation if still loaded
        if (app.animator && app.animator->hasExternalAnimation()) {
            std::cout << "\nCleaning up: Unloading external animation..." << std::endl;
            app.animator->unloadExternalAnimation();
        }

        // Destroy external animation asset
        if (app.assetLoader && app.animAsset) {
            app.assetLoader->destroyAnimationAsset(app.animAsset);
        }

        // Destroy mesh asset
        if (app.assetLoader && app.meshAsset) {
            app.scene->removeEntities(app.meshAsset->getEntities(), app.meshAsset->getEntityCount());
            app.assetLoader->destroyAsset(app.meshAsset);
            gltfio_ext::AssetLoader::destroy(&app.assetLoader);
        }

        // Cleanup resources
        delete app.resourceLoader;
        delete app.nameManager;

        if (app.materialProvider) {
            app.materialProvider->destroyMaterials();
            delete app.materialProvider;
        }
        if (app.stbProvider) delete app.stbProvider;
        if (app.ktx2Provider) delete app.ktx2Provider;

        // Destroy Filament components
        app.engine->destroy(app.keyLight);
        app.engine->destroy(app.fillLight);
        app.engine->destroy(app.view);
        app.engine->destroy(app.scene);
        app.engine->destroy(app.renderer);
        app.engine->destroy(app.swapChain);
        app.engine->destroyCameraComponent(app.cameraEntity);
        EntityManager::get().destroy(app.cameraEntity);

        Engine::destroy(&app.engine);
    }

    if (app.window) {
        SDL_DestroyWindow(app.window);
    }

    SDL_Quit();
}

int main(int argc, char* argv[]) {
    App app;

    std::cout << "=== Animation Hotreload Sample ===" << std::endl;
    std::cout << "This sample demonstrates runtime dynamic loading/unloading" << std::endl;
    std::cout << "of external animation assets using the gltfio_ext library.\n" << std::endl;

    // Initialize SDL and Filament
    if (!initSDL(app)) {
        return 1;
    }

    if (!initFilament(app)) {
        cleanup(app);
        return 1;
    }

    // Load mesh asset (mesh + skeleton, no animations)
    if (!loadMesh(app)) {
        cleanup(app);
        return 1;
    }

    // Cache animation file data for repeated loading (difference from Sample 1)
    std::string animPath = animation_utils::getAssetPath(ANIM_GLB);
    app.cachedAnimBytes = animation_utils::readFile(animPath);
    if (app.cachedAnimBytes.empty()) {
        std::cerr << "Failed to read and cache animation file: " << animPath << std::endl;
        cleanup(app);
        return 1;
    }
    std::cout << "Animation file cached (" << app.cachedAnimBytes.size() << " bytes)" << std::endl;

    // Initial state: No external animation loaded
    std::cout << "\nInitial state: " << app.totalAnimCount << " animation(s)" << std::endl;
    std::cout << "hasExternalAnimation(): " << (app.animator->hasExternalAnimation() ? "true" : "false") << std::endl;

    // Print controls
    std::cout << "\n=== Controls ===" << std::endl;
    std::cout << "  R: Load external animation (Runtime Load)" << std::endl;
    std::cout << "  U: Unload external animation (Runtime Unload)" << std::endl;
    std::cout << "  1/2/3: Switch animation (when available)" << std::endl;
    std::cout << "  T: Run memory leak test (100 cycles)" << std::endl;
    std::cout << "  Space: Play/Pause animation" << std::endl;
    std::cout << "  ESC/Q: Quit" << std::endl;
    std::cout << "\nPress keys to interact...\n" << std::endl;

    // Main loop
    bool running = true;
    SDL_Event event;
    Uint64 lastTime = SDL_GetPerformanceCounter();
    const Uint64 frequency = SDL_GetPerformanceFrequency();

    while (running) {
        // Process events
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }

            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
                int width, height;
                SDL_GL_GetDrawableSize(app.window, &width, &height);
                app.view->setViewport({0, 0, (uint32_t)width, (uint32_t)height});
                app.camera->setProjection(45.0, (double)width / height, 0.1, 100.0);
            }

            if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                    case SDLK_q:
                        running = false;
                        break;

                    case SDLK_SPACE:
                        app.animPlaying = !app.animPlaying;
                        std::cout << (app.animPlaying ? "Playing" : "Paused") << std::endl;
                        break;

                    // Runtime Load (R key)
                    case SDLK_r:
                        runtimeLoadAnimation(app);
                        break;

                    // Runtime Unload (U key)
                    case SDLK_u:
                        runtimeUnloadAnimation(app);
                        break;

                    // Memory Leak Test (T key)
                    case SDLK_t:
                        runLeakTest(app);
                        break;

                    case SDLK_1:
                    case SDLK_KP_1:
                        if (app.totalAnimCount > 0) {
                            app.currentAnimation = 0;
                            app.animTime = 0.0f;
                            std::cout << "Switched to animation 1" << std::endl;
                        }
                        break;

                    case SDLK_2:
                    case SDLK_KP_2:
                        if (app.totalAnimCount > 1) {
                            app.currentAnimation = 1;
                            app.animTime = 0.0f;
                            std::cout << "Switched to animation 2" << std::endl;
                        }
                        break;

                    case SDLK_3:
                    case SDLK_KP_3:
                        if (app.totalAnimCount > 2) {
                            app.currentAnimation = 2;
                            app.animTime = 0.0f;
                            std::cout << "Switched to animation 3" << std::endl;
                        }
                        break;

                    default:
                        break;
                }
            }
        }

        // Update animation
        Uint64 currentTime = SDL_GetPerformanceCounter();
        double deltaTime = (currentTime - lastTime) / (double)frequency;
        lastTime = currentTime;

        updateAnimation(app, deltaTime);

        // Render
        if (app.renderer->beginFrame(app.swapChain)) {
            app.renderer->render(app.view);
            app.renderer->endFrame();
        }

        if (!UTILS_HAS_THREADING) {
            app.engine->execute();
        }
    }

    // Cleanup
    cleanup(app);

    std::cout << "\nSample completed successfully." << std::endl;
    std::cout << "Total load/unload cycles: " << app.loadUnloadCycleCount << std::endl;
    return 0;
}
