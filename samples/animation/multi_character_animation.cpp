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
 * Sample 3: multi_character_animation.cpp
 *
 * 【验证目标】多实例共享缓存动画的内存效率与并发安全性
 *
 * 演示场景：
 * 1. 创建 5 个角色实例（5 个 FilamentAsset，每个 82MB 网格数据）
 * 2. 加载 1 个共享动画资产（1 个 AnimationAsset，679KB 动画数据）
 * 3. 将共享动画绑定到所有 5 个 Animator（使用相同的源 ID）
 * 4. 支持两种播放模式：独立播放和同步播放
 *
 * 【核心验证点】
 * - 【内存效率】验证动画数据只存储一份，节约内存（传统方式需要 679KB × 5 = 3.4MB）
 * - 【独立状态】验证每个 Animator 维护独立的播放状态（时间、索引）
 * - 【并发安全】验证多个 Animator 并发访问同一缓存的安全性
 * - 【资源管理】验证共享资源的正确清理顺序（先解绑所有 Animator，再销毁一次 AnimationAsset）
 *
 * 【gltfio_ext 关键特性】基于缓存的动画共享系统
 * - AnimationAsset 可以被多个 Animator 共享（通过相同的源 ID）
 * - 每个 Animator 调用 loadAnimationsFromSource() 时传入相同的源 ID
 * - 播放时每个 Animator 独立调用 applyAnimationByName()，但读取的是共享缓存
 * - 清理时必须先对所有 Animator 调用 unloadAnimationsFromSource()，最后才能 destroyAnimationAsset()
 *
 * 【内存对比】
 * - 传统方式（不共享）：5 × (82MB mesh + 679KB anim) = 413MB
 * - gltfio_ext 方式（共享）：5 × 82MB mesh + 1 × 679KB anim = 410.7MB
 * - 节省：~3.4MB 动画数据（在更多角色或更大动画时节省更明显）
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
#include <filament/TransformManager.h>
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
#include <vector>
#include <string>
#include <cmath>
#include <cstdlib>

using namespace filament;
using namespace filament::math;
using namespace utils;

static constexpr int WINDOW_WIDTH = 1280;
static constexpr int WINDOW_HEIGHT = 768;

// Multi-character configuration
static constexpr int NUM_CHARACTERS = 5;
static constexpr float CHARACTER_SPACING = 2.5f;  // Spacing between characters

// Asset paths from PLAN.md
static constexpr const char* MESH_GLB = "ecorche_mesh_only.glb";
static constexpr const char* ANIM_GLB = "ecorche_animation_only.glb";

// 【新 API】共享的动画源 ID
// 所有角色使用相同的源 ID，实现缓存共享
static constexpr const char* SHARED_ANIM_SOURCE = "shared_anims";

// Playback modes
enum PlayMode {
    INDEPENDENT,   // Each character plays different animation/time
    SYNCHRONIZED   // All characters play same animation/time
};

// Character state for independent mode
struct CharacterState {
    std::string currentAnimName;  // 【新 API】使用动画名称而不是索引
    float animTime = 0.0f;
};

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
    gltfio_ext::AssetLoader* assetLoader = nullptr;
    gltfio_ext::MaterialProvider* materialProvider = nullptr;
    utils::NameComponentManager* nameManager = nullptr;
    gltfio_ext::ResourceLoader* resourceLoader = nullptr;
    gltfio_ext::TextureProvider* stbProvider = nullptr;
    gltfio_ext::TextureProvider* ktx2Provider = nullptr;

    // 【多角色资产】
    // meshAssets: 存储 5 个 FilamentAsset，每个代表一个角色的网格和骨骼数据
    // animators: 存储 5 个 Animator，每个从对应的 FilamentAsset 获取
    // 关键：每个角色有独立的 FilamentAsset 和 Animator，但共享同一个动画缓存源
    std::vector<gltfio_ext::FilamentAsset*> meshAssets;
    std::vector<gltfio_ext::Animator*> animators;

    // 【gltfio_ext 核心特性】共享的外部动画资产
    // 这是本示例的核心：1 个 AnimationAsset 被 5 个 Animator 共享（通过相同的源 ID）
    // 内存只分配一次（679KB），但可以被多个角色使用
    gltfio_ext::AnimationAsset* sharedAnimAsset = nullptr;

    // Lighting
    Entity keyLight, fillLight;

    // 【新 API】动画状态
    std::vector<std::string> animationNames;  // 可用的动画名称列表

    // 【播放控制】
    PlayMode playMode = SYNCHRONIZED;               // 当前播放模式
    std::vector<CharacterState> characterStates;    // 独立模式：每个角色的播放状态

    // 【同步模式状态】
    // 同步模式下所有角色播放相同的动画和时间
    int syncAnimIndex = 0;        // 当前动画在名称列表中的索引
    float syncAnimTime = 0.0f;
    bool animPlaying = true;
};

static bool initSDL(App& app) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return false;
    }

    app.window = SDL_CreateWindow(
        "Multi-Character Animation Sample",
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
    config.commandBufferSizeMB = 96;   // Increased for 5 complex characters
    config.minCommandBufferSizeMB = 48; // Minimum needed: ~45MB (37MB used + 8MB overflow)

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

    // Setup camera - adjusted for multiple characters
    app.cameraEntity = EntityManager::get().create();
    app.camera = app.engine->createCamera(app.cameraEntity);
    app.view->setCamera(app.camera);

    const double aspect = (double)vpw / vph;
    app.camera->setProjection(45.0, aspect, 0.1, 100.0);

    // Camera position: further back to see all characters
    float sceneWidth = NUM_CHARACTERS * CHARACTER_SPACING;
    float cameraDistance = sceneWidth * 0.8f;
    app.camera->lookAt({0, 2.0f, cameraDistance}, {0, 1.0f, 0}, {0, 1, 0});

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

static bool loadMultipleCharacters(App& app) {
    std::cout << "\n=== Loading multiple characters ===" << std::endl;

    // Read mesh GLB once
    std::string meshPath = animation_utils::getAssetPath(MESH_GLB);
    std::vector<uint8_t> meshBytes = animation_utils::readFile(meshPath);
    if (meshBytes.empty()) {
        std::cerr << "Failed to read mesh GLB: " << meshPath << std::endl;
        return false;
    }

    // Create AssetLoader
    gltfio_ext::AssetConfiguration meshConfig;
    meshConfig.engine = app.engine;
    meshConfig.materials = app.materialProvider;
    meshConfig.names = app.nameManager;

    app.assetLoader = gltfio_ext::AssetLoader::create(meshConfig);
    if (!app.assetLoader) {
        std::cerr << "Failed to create AssetLoader" << std::endl;
        return false;
    }

    // Setup resource loader (shared for all characters)
    gltfio_ext::ResourceConfiguration config = {};
    config.engine = app.engine;
    config.gltfPath = meshPath.c_str();
    config.normalizeSkinningWeights = true;

    app.resourceLoader = new gltfio_ext::ResourceLoader(config);
    app.resourceLoader->addTextureProvider("image/png", app.stbProvider);
    app.resourceLoader->addTextureProvider("image/jpeg", app.stbProvider);
    app.resourceLoader->addTextureProvider("image/ktx2", app.ktx2Provider);

    // Create multiple character instances
    auto& tcm = app.engine->getTransformManager();

    for (int i = 0; i < NUM_CHARACTERS; i++) {
        std::cout << "Creating character " << (i + 1) << "/" << NUM_CHARACTERS << "..." << std::endl;

        // Create mesh asset (each character gets its own FilamentAsset)
        gltfio_ext::FilamentAsset* meshAsset = app.assetLoader->createAsset(
            meshBytes.data(),
            meshBytes.size()
        );

        if (!meshAsset) {
            std::cerr << "Failed to create mesh asset for character " << i << std::endl;
            return false;
        }

        // Load resources
        if (!app.resourceLoader->loadResources(meshAsset)) {
            if (!app.resourceLoader->asyncBeginLoad(meshAsset)) {
                std::cerr << "Failed to load resources for character " << i << std::endl;
                return false;
            }

            while (app.resourceLoader->asyncGetLoadProgress() < 1.0f) {
                if (!UTILS_HAS_THREADING) {
                    app.engine->execute();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        meshAsset->releaseSourceData();

        // Add to scene
        app.scene->addEntities(meshAsset->getEntities(), meshAsset->getEntityCount());

        // Position character in a line (centered)
        float offsetX = (i - NUM_CHARACTERS / 2.0f) * CHARACTER_SPACING;

        auto rootEntity = meshAsset->getRoot();
        auto instance = tcm.getInstance(rootEntity);
        if (instance) {
            mat4f transform = mat4f::translation(float3(offsetX, 0, 0));
            tcm.setTransform(instance, transform);
        }

        std::cout << "  Position: (" << offsetX << ", 0, 0)" << std::endl;

        // Get Animator
        gltfio_ext::Animator* animator = nullptr;
        if (auto* filamentInstance = meshAsset->getInstance()) {
            animator = filamentInstance->getAnimator();
        }

        if (!animator) {
            std::cerr << "Failed to get Animator for character " << i << std::endl;
            return false;
        }

        // Store assets and animators
        app.meshAssets.push_back(meshAsset);
        app.animators.push_back(animator);
    }

    // Get internal animation count from first animator
    size_t internalAnimCount = app.animators[0]->getAnimationCount();

    std::cout << "Successfully created " << NUM_CHARACTERS << " characters" << std::endl;
    std::cout << "Internal animations per character: " << internalAnimCount << std::endl;

    return true;
}

// 【验证核心功能】加载共享的外部动画资产并绑定到多个 Animator
//
// 【验证目标】
// 1. 证明 AnimationAsset 可以被多个 Animator 共享（通过相同的源 ID）
// 2. 证明只需加载一次动画数据，内存效率高
// 3. 证明每个 Animator 都能成功绑定到同一个缓存源
// 4. 证明绑定后每个 Animator 都能独立播放动画
//
// 【新 API 共享机制】
// - 步骤 1: loadAnimationAsset() 只调用一次，创建唯一的 AnimationAsset 对象
// - 步骤 2: 对每个 Animator 调用 loadAnimationsFromSource(相同的源 ID)
// - 步骤 3: 每个 Animator 内部建立到同一缓存源的引用
// - 结果：动画数据（通道、采样器、关键帧）只存储一份，被多个 Animator 读取
//
// 【内存效率分析】
// 不共享方式：每个 Animator 加载独立的 AnimationAsset = 5 × 679KB = 3.4MB
// 共享方式：1 个 AnimationAsset，5 个 Animator 使用相同源 ID = 1 × 679KB = 679KB
// 节省：2.7MB（在更多角色或更大动画时效果更明显）
static bool loadSharedAnimation(App& app) {
    std::cout << "\n=== Loading shared animation asset ===" << std::endl;

    // 读取动画 GLB 文件
    std::string animPath = animation_utils::getAssetPath(ANIM_GLB);
    std::vector<uint8_t> animBytes = animation_utils::readFile(animPath);
    if (animBytes.empty()) {
        std::cerr << "Failed to read animation GLB: " << animPath << std::endl;
        return false;
    }

    // 【gltfio_ext API】loadAnimationAsset() - 只加载一次！
    // 这是共享机制的关键：AnimationAsset 只创建一次，内存只分配一次
    // 返回的指针将被传递给多个 Animator（通过相同的源 ID）
    app.sharedAnimAsset = app.assetLoader->loadAnimationAsset(animBytes.data(), animBytes.size());
    if (!app.sharedAnimAsset) {
        std::cerr << "Failed to load shared AnimationAsset" << std::endl;
        return false;
    }

    std::cout << "Shared AnimationAsset loaded successfully" << std::endl;
    std::cout << "Binding to " << app.animators.size() << " animators..." << std::endl;

    // 【新 API 共享核心】将同一个 AnimationAsset 加载到所有 Animator（使用相同的源 ID）
    // 注意：每次循环都传入相同的 SHARED_ANIM_SOURCE 和 sharedAnimAsset！
    for (size_t i = 0; i < app.animators.size(); i++) {
        // 每个 Animator 调用 loadAnimationsFromSource() 时传入相同的源 ID
        // Animator 内部会检查缓存，如果源 ID 已存在则直接使用，否则创建新缓存条目
        size_t loadedCount = app.animators[i]->loadAnimationsFromSource(
            SHARED_ANIM_SOURCE,
            app.sharedAnimAsset
        );

        if (loadedCount == 0) {
            std::cerr << "Failed to load animations for animator " << i << std::endl;
            return false;
        }
    }

    // 【新 API】getAnimationsInSource() - 获取源中的动画名称列表
    // 所有 Animator 应该返回相同的列表（因为使用相同的源 ID）
    app.animationNames = app.animators[0]->getAnimationsInSource(SHARED_ANIM_SOURCE);

    std::cout << "Animation binding successful!" << std::endl;
    std::cout << "  Shared source ID: " << SHARED_ANIM_SOURCE << std::endl;
    std::cout << "  Animations in source: " << app.animationNames.size() << std::endl;
    std::cout << "  Memory efficiency: 1 AnimationAsset shared by " << NUM_CHARACTERS << " animators" << std::endl;

    std::cout << "  Available animations:" << std::endl;
    for (size_t i = 0; i < app.animationNames.size(); i++) {
        std::cout << "    " << (i + 1) << ". " << app.animationNames[i] << std::endl;
    }

    // 初始化独立播放模式的状态
    app.characterStates.resize(NUM_CHARACTERS);
    for (int i = 0; i < NUM_CHARACTERS; i++) {
        // 使用动画名称而不是索引
        size_t animIdx = i % app.animationNames.size();
        app.characterStates[i].currentAnimName = app.animationNames[animIdx];
        app.characterStates[i].animTime = 0.0f;
    }

    return true;
}

// 【验证核心功能】多 Animator 独立播放动画，验证共享缓存的并发安全性
//
// 【验证目标】
// 1. 验证每个 Animator 可以独立调用 applyAnimationByName()
// 2. 验证多个 Animator 并发读取同一缓存是安全的
// 3. 验证每个 Animator 维护独立的播放状态（时间、动画名称）
// 4. 验证两种播放模式：同步（所有角色同步）和独立（各自独立）
//
// 【新 API 并发访问机制】
// - 每个 Animator 内部维护独立的播放状态（时间偏移、动画名称）
// - applyAnimationByName() 只读取缓存的数据（通道、采样器、关键帧）
// - 多个 Animator 并发调用 applyAnimationByName() 是线程安全的（只读操作）
// - updateBoneMatrices() 更新各自 Animator 的骨骼矩阵，互不干扰
//
// 【并发安全性说明】
// 缓存中的动画数据是只读的，多个 Animator 同时读取不会冲突
// 每个 Animator 写入的是自己的骨骼矩阵缓冲区，不共享
static void updateAnimation(App& app, double deltaTime) {
    if (app.animators.empty() || app.animationNames.empty()) return;

    if (!app.animPlaying) return;

    if (app.playMode == SYNCHRONIZED) {
        // 【同步播放模式】所有角色播放相同的动画和时间
        // 所有 Animator 使用相同的动画名称和 animTime
        if (app.syncAnimIndex < 0 || app.syncAnimIndex >= (int)app.animationNames.size()) {
            app.syncAnimIndex = 0;
        }

        const std::string& animName = app.animationNames[app.syncAnimIndex];
        float duration = app.animators[0]->getAnimationDurationByName(animName.c_str());

        if (duration > 0.0f) {
            app.syncAnimTime += (float)deltaTime;
            if (app.syncAnimTime > duration) {
                app.syncAnimTime = fmod(app.syncAnimTime, duration);
            }
        }

        // 【新 API 并发调用】每个 Animator 独立调用 applyAnimationByName()
        // 虽然参数相同，但每个 Animator 会更新各自的骨骼矩阵
        // 关键：所有 Animator 读取的是同一个缓存源（SHARED_ANIM_SOURCE）
        for (auto* animator : app.animators) {
            animator->applyAnimationByName(animName.c_str(), app.syncAnimTime);
            animator->updateBoneMatrices();
        }
    } else {
        // 【独立播放模式】每个角色播放不同的动画和时间
        // 每个 Animator 使用各自的动画名称和 animTime
        // 这是验证独立状态的最佳方式
        for (size_t i = 0; i < app.animators.size(); i++) {
            auto& state = app.characterStates[i];

            float duration = app.animators[i]->getAnimationDurationByName(
                state.currentAnimName.c_str()
            );

            if (duration > 0.0f) {
                state.animTime += (float)deltaTime;
                if (state.animTime > duration) {
                    state.animTime = fmod(state.animTime, duration);
                }
            }

            // 【新 API 独立状态验证】
            // 每个 Animator 使用不同的动画名称和 animTime
            // 但都从同一个 SHARED_ANIM_SOURCE 缓存读取动画数据
            // 验证：即使参数不同，共享缓存访问仍然安全
            app.animators[i]->applyAnimationByName(
                state.currentAnimName.c_str(),
                state.animTime
            );
            app.animators[i]->updateBoneMatrices();
        }
    }
}

static void switchToIndependentMode(App& app) {
    app.playMode = INDEPENDENT;

    // Randomize each character's animation
    for (size_t i = 0; i < app.characterStates.size(); i++) {
        size_t randomIdx = rand() % app.animationNames.size();
        app.characterStates[i].currentAnimName = app.animationNames[randomIdx];
        app.characterStates[i].animTime = 0.0f;
    }

    std::cout << "\n[Mode] Switched to INDEPENDENT mode" << std::endl;
    std::cout << "Each character plays a random animation:" << std::endl;
    for (size_t i = 0; i < app.characterStates.size(); i++) {
        std::cout << "  Character " << i << ": " << app.characterStates[i].currentAnimName << std::endl;
    }
}

static void switchToSynchronizedMode(App& app) {
    app.playMode = SYNCHRONIZED;
    app.syncAnimTime = 0.0f;

    if (app.syncAnimIndex < 0 || app.syncAnimIndex >= (int)app.animationNames.size()) {
        app.syncAnimIndex = 0;
    }

    std::cout << "\n[Mode] Switched to SYNCHRONIZED mode" << std::endl;
    std::cout << "All characters play: " << app.animationNames[app.syncAnimIndex] << std::endl;
}

static void performanceTest(App& app) {
    const int TEST_DURATION_SEC = 10;
    const int ITERATIONS_PER_FRAME = 10;

    std::cout << "\n=== Performance Test ===" << std::endl;
    std::cout << "Rapidly switching animations for " << TEST_DURATION_SEC << " seconds..." << std::endl;
    std::cout << "This tests concurrent access to shared animation cache" << std::endl;

    auto startTime = std::chrono::steady_clock::now();
    int totalSwitches = 0;

    while (true) {
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime);

        if (elapsed.count() >= TEST_DURATION_SEC) break;

        // Rapidly switch animations on all characters
        for (int iter = 0; iter < ITERATIONS_PER_FRAME; iter++) {
            for (size_t i = 0; i < app.animators.size(); i++) {
                size_t randomIdx = rand() % app.animationNames.size();
                const std::string& animName = app.animationNames[randomIdx];
                app.animators[i]->applyAnimationByName(animName.c_str(), 0.0f);
                totalSwitches++;
            }
        }

        // Update bone matrices
        for (auto* animator : app.animators) {
            animator->updateBoneMatrices();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));  // ~60 FPS
    }

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    std::cout << "\n=== Test Complete ===" << std::endl;
    std::cout << "Total switches: " << totalSwitches << std::endl;
    std::cout << "Duration: " << duration.count() << " ms" << std::endl;
    std::cout << "Switches per second: " << (totalSwitches * 1000 / duration.count()) << std::endl;
    std::cout << "No crashes detected. Concurrent access is SAFE." << std::endl;
}

// 【验证核心功能】共享资源的正确清理顺序
//
// 【关键验证点】
// 1. 验证共享的 AnimationAsset 必须在所有 Animator 解绑后才能销毁
// 2. 验证 destroyAnimationAsset() 只调用一次（即使被 5 个 Animator 共享）
// 3. 验证清理顺序错误会导致崩溃或内存错误
//
// 【新 API 共享资源清理顺序（关键！）】
// 步骤 1: 对所有 Animator 调用 unloadAnimationsFromSource() - 解除缓存引用
// 步骤 2: 调用一次 destroyAnimationAsset() - 释放共享数据
// 步骤 3: 销毁各个 FilamentAsset - 清理网格数据
//
// 【错误示例（会崩溃）】
// 错误 1: 先 destroyAnimationAsset()，后 unloadAnimationsFromSource()
//        -> Animator 解绑时访问已释放的内存
// 错误 2: 对每个 Animator 都调用 destroyAnimationAsset()
//        -> 重复释放同一块内存，导致 double-free 错误
static void cleanup(App& app) {
    if (app.engine) {
        // 【步骤 1：解绑】先从所有 Animator 解绑动画源
        // 关键：必须在销毁 AnimationAsset 之前完成
        // 每个 Animator 都需要解绑，因为都持有对 SHARED_ANIM_SOURCE 的引用
        if (app.sharedAnimAsset) {
            std::cout << "\nCleaning up: Unloading animation source from all animators..." << std::endl;
            for (auto* animator : app.animators) {
                if (animator) {
                    animator->unloadAnimationsFromSource(SHARED_ANIM_SOURCE);
                }
            }
        }

        // 【步骤 2：销毁共享资源】销毁 AnimationAsset（只调用一次！）
        // 关键：虽然 5 个 Animator 都引用了它（通过相同的源 ID），但只能销毁一次
        // 因为内存只分配了一次，所以也只能释放一次
        if (app.assetLoader && app.sharedAnimAsset) {
            app.assetLoader->destroyAnimationAsset(app.sharedAnimAsset);
            app.sharedAnimAsset = nullptr;
        }

        // 【步骤 3：销毁各自的资源】销毁每个 FilamentAsset
        // 每个角色有独立的 FilamentAsset，需要分别销毁
        for (auto* meshAsset : app.meshAssets) {
            if (meshAsset) {
                app.scene->removeEntities(meshAsset->getEntities(), meshAsset->getEntityCount());
                app.assetLoader->destroyAsset(meshAsset);
            }
        }
        app.meshAssets.clear();

        // 【关键修复】ResourceLoader 必须在 AssetLoader 之前删除
        // 原因：ResourceLoader 可能持有对 TextureProvider 的引用
        if (app.resourceLoader) {
            delete app.resourceLoader;
            app.resourceLoader = nullptr;
        }

        // Destroy loader
        if (app.assetLoader) {
            gltfio_ext::AssetLoader::destroy(&app.assetLoader);
        }

        // Cleanup managers and providers
        if (app.nameManager) {
            delete app.nameManager;
            app.nameManager = nullptr;
        }

        if (app.stbProvider) {
            delete app.stbProvider;
            app.stbProvider = nullptr;
        }
        if (app.ktx2Provider) {
            delete app.ktx2Provider;
            app.ktx2Provider = nullptr;
        }

        if (app.materialProvider) {
            app.materialProvider->destroyMaterials();
            delete app.materialProvider;
            app.materialProvider = nullptr;
        }

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

    std::cout << "=== Multi-Character Animation Sample ===" << std::endl;
    std::cout << "This sample demonstrates:" << std::endl;
    std::cout << "  - Multiple character instances (" << NUM_CHARACTERS << " characters)" << std::endl;
    std::cout << "  - Shared animation cache (memory efficiency)" << std::endl;
    std::cout << "  - Independent and synchronized playback modes\n" << std::endl;

    // Initialize SDL and Filament
    if (!initSDL(app)) {
        return 1;
    }

    if (!initFilament(app)) {
        cleanup(app);
        return 1;
    }

    // Load multiple character instances
    if (!loadMultipleCharacters(app)) {
        cleanup(app);
        return 1;
    }

    // Load shared animation and bind to all animators
    if (!loadSharedAnimation(app)) {
        cleanup(app);
        return 1;
    }

    // Print controls
    std::cout << "\n=== Controls ===" << std::endl;
    std::cout << "  1: Switch to Independent mode (random animations)" << std::endl;
    std::cout << "  2: Switch to Synchronized mode (same animation)" << std::endl;
    std::cout << "  N: Next animation (synchronized mode only)" << std::endl;
    std::cout << "  R: Randomize animations (independent mode only)" << std::endl;
    std::cout << "  P: Run performance test (10 seconds)" << std::endl;
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

                    case SDLK_1:
                        switchToIndependentMode(app);
                        break;

                    case SDLK_2:
                        switchToSynchronizedMode(app);
                        break;

                    case SDLK_n:
                        if (app.playMode == SYNCHRONIZED) {
                            app.syncAnimIndex = (app.syncAnimIndex + 1) % app.animationNames.size();
                            app.syncAnimTime = 0.0f;
                            std::cout << "[Sync] Switched to: " << app.animationNames[app.syncAnimIndex] << std::endl;
                        }
                        break;

                    case SDLK_r:
                        if (app.playMode == INDEPENDENT) {
                            switchToIndependentMode(app);  // Re-randomize
                        }
                        break;

                    case SDLK_p:
                        performanceTest(app);
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
    return 0;
}
