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
 * 示例 1: external_animation.cpp
 *
 * 演示加载外部动画资产的基本工作流程：
 * 1. 加载仅含网格的 GLB (ecorche_mesh_only.glb, 82 MB)
 * 2. 加载仅含动画的 GLB (ecorche_animation_only.glb, 679 KB)
 * 3. 将外部动画绑定到网格的 Animator（使用缓存系统）
 * 4. 使用键盘(1/2/3 键)切换动画（通过名称访问）
 *
 * 这验证了完整的外部动画加载-绑定-播放流程。
 *
 * 【gltfio_ext 核心特性】基于缓存的动画系统：
 * - 支持将动画和网格分离存储，显著减少资源大小
 * - 允许在运行时动态加载和卸载动画源
 * - 通过缓存机制共享动画数据，提高内存效率
 * - 支持按名称访问动画，提供更灵活的动画控制
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
#include <vector>
#include <string>

using namespace filament;
using namespace filament::math;
using namespace utils;

static constexpr int WINDOW_WIDTH = 1024;
static constexpr int WINDOW_HEIGHT = 768;

// 资产路径（来自 PLAN.md）
static constexpr const char* MESH_GLB = "ecorche_mesh_only.glb";      // 仅含网格和骨骼的 GLB
static constexpr const char* ANIM_GLB = "ecorche_animation_only.glb";  // 仅含动画数据的 GLB

// 【新 API】缓存源 ID，用于标识动画源
static constexpr const char* ANIM_SOURCE_ID = "character_anims";

struct App {
    // SDL 和窗口
    SDL_Window* window = nullptr;

    // Filament 核心组件
    Engine* engine = nullptr;
    Renderer* renderer = nullptr;
    SwapChain* swapChain = nullptr;
    View* view = nullptr;
    Scene* scene = nullptr;
    Camera* camera = nullptr;
    Entity cameraEntity;

    // 资产加载器 (gltfio_ext)
    gltfio_ext::AssetLoader* assetLoader = nullptr;        // 资产加载器，用于加载网格和动画
    gltfio_ext::MaterialProvider* materialProvider = nullptr; // 材质提供器
    utils::NameComponentManager* nameManager = nullptr;    // 名称组件管理器
    gltfio_ext::FilamentAsset* meshAsset = nullptr;        // 网格资产（包含骨骼）
    gltfio_ext::ResourceLoader* resourceLoader = nullptr;  // 资源加载器（纹理、缓冲区等）
    gltfio_ext::TextureProvider* stbProvider = nullptr;    // STB 纹理提供器（PNG/JPEG）
    gltfio_ext::TextureProvider* ktx2Provider = nullptr;   // KTX2 纹理提供器
    gltfio_ext::Animator* animator = nullptr;              // 动画器，管理动画播放

    // 【gltfio_ext 独特特性】外部动画资产
    // AnimationAsset 是 gltfio_ext 特有的类型，表示独立的动画数据
    // 可以在运行时加载/卸载，不需要重新加载整个模型
    gltfio_ext::AnimationAsset* animAsset = nullptr;

    // 光照
    Entity keyLight, fillLight;

    // 【新 API】动画状态
    // 使用动画名称列表和索引，而不是总数计数
    std::vector<std::string> animationNames;  // 从源中加载的动画名称列表
    int currentAnimIndex = 0;                 // 当前播放的动画在列表中的索引
    float animTime = 0.0f;                    // 当前动画时间
    bool animPlaying = true;                  // 是否正在播放
};

// 初始化 SDL 窗口系统
static bool initSDL(App& app) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return false;
    }

    app.window = SDL_CreateWindow(
        "External Animation Sample",
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

// 初始化 Filament 渲染引擎和相关组件
static bool initFilament(App& app) {
    // 配置引擎参数
    Engine::Config config = {};
    config.commandBufferSizeMB = 32;       // 命令缓冲区大小
    config.minCommandBufferSizeMB = 8;     // 最小命令缓冲区大小

    app.engine = Engine::create(filament::backend::Backend::DEFAULT, nullptr, nullptr, &config);
    if (!app.engine) {
        std::cerr << "Failed to create Filament engine" << std::endl;
        return false;
    }

    // 创建交换链（用于渲染到窗口）
    void* nativeView = animation_utils::getNativeWindow(app.window);
    if (!nativeView) {
        std::cerr << "Failed to get native window" << std::endl;
        return false;
    }

#if defined(__APPLE__)
    // macOS/iOS 使用 Metal 渲染后端
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

    // 创建核心渲染组件
    app.renderer = app.engine->createRenderer();
    app.scene = app.engine->createScene();
    app.view = app.engine->createView();
    app.view->setScene(app.scene);

    // 设置视口（支持高DPI显示）
    int vpw = 0, vph = 0;
    SDL_GL_GetDrawableSize(app.window, &vpw, &vph);
    app.view->setViewport({0, 0, (uint32_t)vpw, (uint32_t)vph});

    // 设置相机
    app.cameraEntity = EntityManager::get().create();
    app.camera = app.engine->createCamera(app.cameraEntity);
    app.view->setCamera(app.camera);

    const double aspect = (double)vpw / vph;
    app.camera->setProjection(45.0, aspect, 0.1, 100.0);  // 45度视野角
    app.camera->lookAt({4.0f, 1.0f, 4.0f}, {0, 0, 0}, {0, 1, 0});

    // 创建材质和纹理提供器 (gltfio_ext)
    app.materialProvider = gltfio_ext::createJitShaderProvider(app.engine);  // JIT 着色器编译器
    app.nameManager = new utils::NameComponentManager(EntityManager::get());  // 实体名称管理
    app.stbProvider = gltfio_ext::createStbProvider(app.engine);       // STB 图像解码器（PNG/JPEG）
    app.ktx2Provider = gltfio_ext::createKtx2Provider(app.engine);     // KTX2 纹理格式支持

    // 设置基础光照（主光源和补光）
    app.keyLight = EntityManager::get().create();
    LightManager::Builder(LightManager::Type::DIRECTIONAL)
        .color({1.0f, 0.98f, 0.95f})       // 主光源：暖白色
        .intensity(120000.0f)
        .direction({0.3f, -0.8f, -0.5f})
        .castShadows(false)
        .build(*app.engine, app.keyLight);
    app.scene->addEntity(app.keyLight);

    app.fillLight = EntityManager::get().create();
    LightManager::Builder(LightManager::Type::DIRECTIONAL)
        .color({0.9f, 0.95f, 1.0f})        // 补光：冷白色
        .intensity(80000.0f)
        .direction({-0.7f, -0.3f, 0.6f})
        .castShadows(false)
        .build(*app.engine, app.fillLight);
    app.scene->addEntity(app.fillLight);

    return true;
}

// 加载网格资产（仅包含网格和骨骼，不含动画）
static bool loadMesh(App& app) {
    std::cout << "\n=== Loading mesh asset ===" << std::endl;

    // 解析并读取网格 GLB 文件
    std::string meshPath = animation_utils::getAssetPath(MESH_GLB);
    std::vector<uint8_t> meshBytes = animation_utils::readFile(meshPath);
    if (meshBytes.empty()) {
        std::cerr << "Failed to read mesh GLB: " << meshPath << std::endl;
        return false;
    }

    // 创建网格资产加载器
    // AssetConfiguration 用于配置如何加载 glTF/GLB 资产
    gltfio_ext::AssetConfiguration meshConfig;
    meshConfig.engine = app.engine;
    meshConfig.materials = app.materialProvider;  // 材质提供器
    meshConfig.names = app.nameManager;           // 名称组件管理器

    app.assetLoader = gltfio_ext::AssetLoader::create(meshConfig);
    if (!app.assetLoader) {
        std::cerr << "Failed to create AssetLoader" << std::endl;
        return false;
    }

    // 创建网格资产（解析 GLB 二进制数据）
    app.meshAsset = app.assetLoader->createAsset(meshBytes.data(), meshBytes.size());
    if (!app.meshAsset) {
        std::cerr << "Failed to create mesh asset" << std::endl;
        return false;
    }

    // 加载资源（纹理、缓冲区等）
    gltfio_ext::ResourceConfiguration config = {};
    config.engine = app.engine;
    config.gltfPath = meshPath.c_str();
    config.normalizeSkinningWeights = true;  // 规范化蒙皮权重（重要：确保权重总和为1）

    app.resourceLoader = new gltfio_ext::ResourceLoader(config);
    // 注册纹理提供器（支持不同的图像格式）
    app.resourceLoader->addTextureProvider("image/png", app.stbProvider);
    app.resourceLoader->addTextureProvider("image/jpeg", app.stbProvider);
    app.resourceLoader->addTextureProvider("image/ktx2", app.ktx2Provider);

    // 同步或异步加载资源
    if (!app.resourceLoader->loadResources(app.meshAsset)) {
        if (!app.resourceLoader->asyncBeginLoad(app.meshAsset)) {
            std::cerr << "Failed to load mesh resources" << std::endl;
            return false;
        }

        // 等待异步加载完成
        while (app.resourceLoader->asyncGetLoadProgress() < 1.0f) {
            if (!UTILS_HAS_THREADING) {
                app.engine->execute();  // 在无线程环境下手动执行引擎任务
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    // 释放源数据（减少内存占用）
    app.meshAsset->releaseSourceData();

    // 将网格实体添加到场景中
    app.scene->addEntities(app.meshAsset->getEntities(), app.meshAsset->getEntityCount());

    std::cout << "Mesh loaded successfully" << std::endl;
    std::cout << "Note: Animator will be retrieved after loading external animation" << std::endl;

    return true;
}

// 【gltfio_ext 核心特性】加载外部动画资产（使用新的缓存 API）
// 这是 gltfio_ext 最重要的功能之一：支持将动画数据与网格分离
static bool loadExternalAnimation(App& app) {
    std::cout << "\n=== Loading external animation ===" << std::endl;

    // 解析并读取动画 GLB 文件
    std::string animPath = animation_utils::getAssetPath(ANIM_GLB);
    std::vector<uint8_t> animBytes = animation_utils::readFile(animPath);
    if (animBytes.empty()) {
        std::cerr << "Failed to read animation GLB: " << animPath << std::endl;
        return false;
    }

    // 【gltfio_ext 独特 API】使用 AssetLoader::loadAnimationAsset() 加载外部动画
    // 这与标准的 createAsset() 不同，专门用于加载仅含动画数据的 GLB
    app.animAsset = app.assetLoader->loadAnimationAsset(animBytes.data(), animBytes.size());
    if (!app.animAsset) {
        std::cerr << "Failed to load external animation asset" << std::endl;
        return false;
    }

    // 从网格实例获取 gltfio_ext Animator（动画器）
    // FilamentInstance 是每个资产的实例化表示
    if (auto* instance = app.meshAsset->getInstance()) {
        app.animator = instance->getAnimator();
    }

    if (!app.animator) {
        std::cerr << "Failed to get gltfio_ext Animator from mesh instance" << std::endl;
        return false;
    }

    // 获取网格内部的动画数量（现在只返回内部动画）
    size_t internalAnimCount = app.animator->getAnimationCount();
    std::cout << "Internal animations in mesh: " << internalAnimCount << std::endl;

    // 【新 API】loadAnimationsFromSource() - 将外部动画加载到缓存中
    // 参数：sourceId - 唯一标识此动画源的字符串
    //       asset - AnimationAsset 指针
    // 返回：加载的动画数量
    size_t loadedCount = app.animator->loadAnimationsFromSource(ANIM_SOURCE_ID, app.animAsset);
    if (loadedCount == 0) {
        std::cerr << "Failed to load animations from source" << std::endl;
        return false;
    }

    std::cout << "External animation loaded successfully" << std::endl;
    std::cout << "Animations loaded from source: " << loadedCount << std::endl;

    // 【新 API】getAnimationsInSource() - 获取源中的动画名称列表
    // 返回一个 vector<string>，包含该源中所有动画的名称
    app.animationNames = app.animator->getAnimationsInSource(ANIM_SOURCE_ID);

    std::cout << "Available animations:" << std::endl;
    for (size_t i = 0; i < app.animationNames.size(); i++) {
        std::cout << "  " << (i + 1) << ". " << app.animationNames[i] << std::endl;
    }

    // 验证外部动画是否成功添加
    if (app.animationNames.empty()) {
        std::cerr << "Warning: No animations found in external asset" << std::endl;
    }

    return true;
}

// 更新动画（每帧调用）
static void updateAnimation(App& app, double deltaTime) {
    if (!app.animator) return;

    if (app.animationNames.empty()) return;

    // 确保动画索引有效
    if (app.currentAnimIndex < 0 || app.currentAnimIndex >= (int)app.animationNames.size()) {
        app.currentAnimIndex = 0;
    }

    // 更新动画时间（如果正在播放）
    if (app.animPlaying) {
        // 【新 API】getAnimationDurationByName() - 通过名称获取动画时长
        const std::string& animName = app.animationNames[app.currentAnimIndex];
        const float duration = app.animator->getAnimationDurationByName(animName.c_str());

        if (duration > 0.0f) {
            app.animTime += (float)deltaTime;
            // 循环播放动画
            if (app.animTime > duration) {
                app.animTime = fmod(app.animTime, duration);
            }
        }
    }

    // 【新 API】applyAnimationByName() - 通过名称应用动画
    // 这会使用 LRU 缓存机制，自动管理内存
    const std::string& animName = app.animationNames[app.currentAnimIndex];
    app.animator->applyAnimationByName(animName.c_str(), app.animTime);
    app.animator->updateBoneMatrices();
}

// 清理资源（按照正确的顺序销毁所有对象）
static void cleanup(App& app) {
    if (app.engine) {
        // 【新 API】unloadAnimationsFromSource() - 卸载指定源的所有动画
        // 必须在销毁动画资产之前先从 Animator 中卸载
        if (app.animator && !app.animationNames.empty()) {
            std::cout << "\nUnloading external animation source..." << std::endl;
            app.animator->unloadAnimationsFromSource(ANIM_SOURCE_ID);
        }

        // 【gltfio_ext 独特 API】销毁外部动画资产
        // 使用专门的 destroyAnimationAsset() 而不是 destroyAsset()
        if (app.assetLoader && app.animAsset) {
            app.assetLoader->destroyAnimationAsset(app.animAsset);
        }

        // 销毁网格资产
        if (app.assetLoader && app.meshAsset) {
            app.scene->removeEntities(app.meshAsset->getEntities(), app.meshAsset->getEntityCount());
            app.assetLoader->destroyAsset(app.meshAsset);
            gltfio_ext::AssetLoader::destroy(&app.assetLoader);
        }

        // 清理资源加载器和管理器
        delete app.resourceLoader;
        delete app.nameManager;

        // 清理材质和纹理提供器
        if (app.materialProvider) {
            app.materialProvider->destroyMaterials();
            delete app.materialProvider;
        }
        if (app.stbProvider) delete app.stbProvider;
        if (app.ktx2Provider) delete app.ktx2Provider;

        // 销毁 Filament 核心组件
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

    std::cout << "=== External Animation Sample ===" << std::endl;
    std::cout << "This sample demonstrates loading external animation assets" << std::endl;
    std::cout << "using the gltfio_ext cache-based animation API.\n" << std::endl;

    // 初始化 SDL 和 Filament
    if (!initSDL(app)) {
        return 1;
    }

    if (!initFilament(app)) {
        cleanup(app);
        return 1;
    }

    // 【示例工作流程步骤 1】加载网格资产（包含网格和骨骼，不含动画）
    if (!loadMesh(app)) {
        cleanup(app);
        return 1;
    }

    // 【示例工作流程步骤 2】加载外部动画资产并绑定到网格（使用缓存系统）
    // 这是 gltfio_ext 的核心特性：动画与网格分离，通过缓存机制共享
    if (!loadExternalAnimation(app)) {
        cleanup(app);
        return 1;
    }

    // 打印控制说明
    std::cout << "\n=== Controls ===" << std::endl;
    std::cout << "  1/2/3: Switch animation" << std::endl;
    std::cout << "  Space: Play/Pause animation" << std::endl;
    std::cout << "  ESC/Q: Quit" << std::endl;
    std::cout << "\nPress keys to interact...\n" << std::endl;

    // 主循环
    bool running = true;
    SDL_Event event;
    Uint64 lastTime = SDL_GetPerformanceCounter();
    const Uint64 frequency = SDL_GetPerformanceFrequency();

    while (running) {
        // 处理 SDL 事件
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }

            // 处理窗口大小变化
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
                int width, height;
                SDL_GL_GetDrawableSize(app.window, &width, &height);
                app.view->setViewport({0, 0, (uint32_t)width, (uint32_t)height});
                app.camera->setProjection(45.0, (double)width / height, 0.1, 100.0);
            }

            // 处理键盘输入
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

                    // 【示例工作流程步骤 3】切换动画（使用名称索引）
                    // 外部动画加载后，可以通过名称访问
                    case SDLK_1:
                    case SDLK_KP_1:
                        if (app.animationNames.size() > 0) {
                            app.currentAnimIndex = 0;
                            app.animTime = 0.0f;
                            std::cout << "Switched to animation 1: " << app.animationNames[0] << std::endl;
                        }
                        break;

                    case SDLK_2:
                    case SDLK_KP_2:
                        if (app.animationNames.size() > 1) {
                            app.currentAnimIndex = 1;
                            app.animTime = 0.0f;
                            std::cout << "Switched to animation 2: " << app.animationNames[1] << std::endl;
                        }
                        break;

                    case SDLK_3:
                    case SDLK_KP_3:
                        if (app.animationNames.size() > 2) {
                            app.currentAnimIndex = 2;
                            app.animTime = 0.0f;
                            std::cout << "Switched to animation 3: " << app.animationNames[2] << std::endl;
                        }
                        break;

                    default:
                        break;
                }
            }
        }

        // 【示例工作流程步骤 4】更新动画
        // 计算帧间隔时间
        Uint64 currentTime = SDL_GetPerformanceCounter();
        double deltaTime = (currentTime - lastTime) / (double)frequency;
        lastTime = currentTime;

        updateAnimation(app, deltaTime);

        // 渲染场景
        if (app.renderer->beginFrame(app.swapChain)) {
            app.renderer->render(app.view);
            app.renderer->endFrame();
        }

        // 在无线程环境下手动执行引擎任务
        if (!UTILS_HAS_THREADING) {
            app.engine->execute();
        }
    }

    // 清理所有资源
    cleanup(app);

    std::cout << "\nSample completed successfully." << std::endl;
    return 0;
}
