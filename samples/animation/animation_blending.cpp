/*
 * Copyright (C) 2025 The Android Open Source Project
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
 * Animation Blending Sample (animation_blending.cpp)
 *
 * Demonstrates the applyCrossFade() API for smooth animation transitions:
 * 1. Load character mesh + animation source
 * 2. Space key: Trigger idle→walk transition (2-second blend)
 * 3. 'S' key: Manual mode (arrow keys control alpha 0.0-1.0)
 * 4. 'A' key: Auto mode (automatic blending animation)
 *
 * UI displays:
 * - Current alpha value
 * - Two animation names being blended
 * - Blend mode (auto/manual)
 * - Instructions
 *
 * This demonstrates the core cross-fade functionality essential for game animation systems.
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

// Asset paths
static constexpr const char* MESH_GLB = "ecorche_mesh_only.glb";
static constexpr const char* ANIM_GLB = "ecorche_animation_only.glb";
static constexpr const char* ANIM_SOURCE_ID = "character_anims";

// Animation blend modes (renamed to avoid conflict with filament::BlendMode)
enum class AnimBlendMode {
    IDLE,           // Not blending, playing single animation
    AUTO_BLEND,     // Automatically blend from anim A to anim B
    MANUAL_BLEND    // Manual alpha control via keyboard
};

struct App {
    // SDL and window
    SDL_Window* window = nullptr;

    // Filament core components
    Engine* engine = nullptr;
    Renderer* renderer = nullptr;
    SwapChain* swapChain = nullptr;
    View* view = nullptr;
    Scene* scene = nullptr;
    Camera* camera = nullptr;
    Entity cameraEntity;

    // gltfio_ext components
    gltfio_ext::AssetLoader* assetLoader = nullptr;
    gltfio_ext::MaterialProvider* materialProvider = nullptr;
    gltfio_ext::ResourceLoader* resourceLoader = nullptr;
    gltfio_ext::TextureProvider* stbProvider = nullptr;
    gltfio_ext::TextureProvider* ktx2Provider = nullptr;
    NameComponentManager* nameManager = nullptr;

    // Loaded assets
    gltfio_ext::FilamentAsset* meshAsset = nullptr;
    std::unique_ptr<gltfio_ext::AnimationAsset> animAsset;
    gltfio_ext::Animator* animator = nullptr;

    // Animation state
    std::vector<std::string> animNames;  // Available animations
    size_t currentAnimIdx = 0;           // Current animation index
    size_t previousAnimIdx = 0;          // Previous animation index
    float animationTime = 0.0f;          // Current animation playback time
    float previousAnimTime = 0.0f;       // Previous animation playback time (for blending)

    // Blending state
    AnimBlendMode blendMode = AnimBlendMode::IDLE;
    float blendAlpha = 0.0f;             // 0.0 = previous anim, 1.0 = current anim
    float blendDuration = 2.0f;          // Auto blend duration in seconds
    float blendStartTime = 0.0f;         // Time when blend started

    // Timing
    std::chrono::high_resolution_clock::time_point lastFrameTime;
    float deltaTime = 0.0f;

    // Control flags
    bool running = true;
    bool paused = false;
};

// Forward declarations
bool initFilament(App& app);
bool loadAssets(App& app);
void setupScene(App& app);
void setupCamera(App& app);
void processInput(App& app);
void update(App& app);
void render(App& app);
void printInstructions();
void printStatus(const App& app);
void cleanup(App& app);

int main(int argc, char* argv[]) {
    App app;

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    // Create window
    app.window = SDL_CreateWindow(
        "Animation Blending Sample",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (!app.window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    // Initialize Filament
    if (!initFilament(app)) {
        cleanup(app);
        return -1;
    }

    // Load assets
    if (!loadAssets(app)) {
        cleanup(app);
        return -1;
    }

    setupScene(app);
    setupCamera(app);
    printInstructions();

    app.lastFrameTime = std::chrono::high_resolution_clock::now();

    // Main loop
    while (app.running) {
        processInput(app);
        update(app);
        render(app);
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
    }

    cleanup(app);
    return 0;
}

bool initFilament(App& app) {
    app.engine = Engine::create(backend::Backend::DEFAULT);
    if (!app.engine) {
        std::cerr << "Failed to create Filament engine" << std::endl;
        return false;
    }

    app.renderer = app.engine->createRenderer();
    app.scene = app.engine->createScene();
    app.view = app.engine->createView();

    void* nativeWindow = animation_utils::getNativeWindow(app.window);

    #ifdef __APPLE__
    void* metalLayer = animation_utils::setUpMetalLayer(nativeWindow);
    if (!metalLayer) {
        std::cerr << "Failed to set up Metal layer" << std::endl;
        return false;
    }
    app.swapChain = app.engine->createSwapChain(metalLayer);
    #else
    app.swapChain = app.engine->createSwapChain(nativeWindow);
    #endif

    // Setup view
    app.view->setScene(app.scene);
    app.view->setViewport(Viewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT));

    // Create camera
    app.cameraEntity = EntityManager::get().create();
    app.camera = app.engine->createCamera(app.cameraEntity);
    app.view->setCamera(app.camera);

    // Create gltfio_ext components
    app.materialProvider = gltfio_ext::createJitShaderProvider(app.engine);
    app.nameManager = new NameComponentManager(EntityManager::get());
    app.stbProvider = gltfio_ext::createStbProvider(app.engine);
    app.ktx2Provider = gltfio_ext::createKtx2Provider(app.engine);

    // Create AssetLoader with configuration
    gltfio_ext::AssetConfiguration meshConfig;
    meshConfig.engine = app.engine;
    meshConfig.materials = app.materialProvider;
    meshConfig.names = app.nameManager;
    app.assetLoader = gltfio_ext::AssetLoader::create(meshConfig);

    return true;
}

bool loadAssets(App& app) {
    // Load mesh asset
    std::string meshPath = animation_utils::getAssetPath(MESH_GLB);
    if (meshPath.empty()) {
        std::cerr << "Mesh asset not found: " << MESH_GLB << std::endl;
        return false;
    }

    std::vector<uint8_t> meshData = animation_utils::readFile(meshPath);
    if (meshData.empty()) {
        std::cerr << "Failed to read mesh file" << std::endl;
        return false;
    }

    // Configure and create ResourceLoader
    gltfio_ext::ResourceConfiguration config = {};
    config.engine = app.engine;
    config.gltfPath = meshPath.c_str();
    config.normalizeSkinningWeights = true;
    app.resourceLoader = new gltfio_ext::ResourceLoader(config);
    app.resourceLoader->addTextureProvider("image/png", app.stbProvider);
    app.resourceLoader->addTextureProvider("image/jpeg", app.stbProvider);
    app.resourceLoader->addTextureProvider("image/ktx2", app.ktx2Provider);

    app.meshAsset = app.assetLoader->createAsset(meshData.data(), meshData.size());
    if (!app.meshAsset) {
        std::cerr << "Failed to create mesh asset" << std::endl;
        return false;
    }

    // Load resources
    app.resourceLoader->loadResources(app.meshAsset);

    // Wait for resources to load
    while (app.resourceLoader->asyncGetLoadProgress() < 1.0f) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Load animation asset
    std::string animPath = animation_utils::getAssetPath(ANIM_GLB);
    if (animPath.empty()) {
        std::cerr << "Animation asset not found: " << ANIM_GLB << std::endl;
        return false;
    }

    std::vector<uint8_t> animData = animation_utils::readFile(animPath);
    if (animData.empty()) {
        std::cerr << "Failed to read animation file" << std::endl;
        return false;
    }

    app.animAsset = app.assetLoader->loadAnimationAsset(animData.data(), animData.size());
    if (!app.animAsset) {
        std::cerr << "Failed to create animation asset" << std::endl;
        return false;
    }

    // Get animator and load animations
    auto* instance = app.meshAsset->getInstance();
    if (!instance) {
        std::cerr << "Failed to get FilamentInstance" << std::endl;
        return false;
    }

    app.animator = instance->getAnimator();
    if (!app.animator) {
        std::cerr << "Failed to get Animator" << std::endl;
        return false;
    }

    // Load animations into cache
    size_t loadedCount = app.animator->loadAnimationsFromSource(ANIM_SOURCE_ID, app.animAsset.get());
    if (loadedCount == 0) {
        std::cerr << "Failed to load animations from source" << std::endl;
        return false;
    }

    // Get animation names
    app.animNames = app.animator->getAnimationsInSource(ANIM_SOURCE_ID);
    if (app.animNames.size() < 2) {
        std::cerr << "Need at least 2 animations for blending, got " << app.animNames.size() << std::endl;
        return false;
    }

    std::cout << "Loaded " << loadedCount << " animations:" << std::endl;
    for (size_t i = 0; i < app.animNames.size(); ++i) {
        float duration = app.animator->getAnimationDurationByName(app.animNames[i].c_str());
        std::cout << "  [" << i << "] " << app.animNames[i] << " (" << duration << "s)" << std::endl;
    }

    return true;
}

void setupScene(App& app) {
    // Add directional light
    Entity sunEntity = EntityManager::get().create();
    LightManager::Builder(LightManager::Type::SUN)
        .color(Color::toLinear<ACCURATE>(sRGBColor(0.98f, 0.92f, 0.89f)))
        .intensity(110000)
        .direction({0.6, -1, -0.8})
        .sunAngularRadius(1.9f)
        .castShadows(true)
        .build(*app.engine, sunEntity);
    app.scene->addEntity(sunEntity);

    // Add mesh to scene
    app.scene->addEntities(
        app.meshAsset->getEntities(),
        app.meshAsset->getEntityCount()
    );
}

void setupCamera(App& app) {
    float3 eye = {0, 1.6f, 3.0f};
    float3 center = {0, 1.0f, 0};
    float3 up = {0, 1, 0};

    mat4f lookAt = mat4f::lookAt(eye, center, up);
    app.camera->setModelMatrix(lookAt);

    float aspect = (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT;
    app.camera->setProjection(45.0, aspect, 0.1, 100.0, Camera::Fov::VERTICAL);
}

void processInput(App& app) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            app.running = false;
        } else if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                case SDLK_q:
                    app.running = false;
                    break;

                case SDLK_SPACE:
                    // Trigger auto blend to next animation
                    if (app.blendMode != AnimBlendMode::AUTO_BLEND) {
                        app.previousAnimIdx = app.currentAnimIdx;
                        app.previousAnimTime = app.animationTime;  // Save previous animation time
                        app.currentAnimIdx = (app.currentAnimIdx + 1) % app.animNames.size();
                        app.blendMode = AnimBlendMode::AUTO_BLEND;
                        app.blendAlpha = 0.0f;
                        app.blendStartTime = app.animationTime;
                        app.animationTime = 0.0f;  // Reset current animation time
                        std::cout << "\n[AUTO BLEND] " << app.animNames[app.previousAnimIdx]
                                  << " → " << app.animNames[app.currentAnimIdx] << std::endl;
                    }
                    break;

                case SDLK_a:
                    // Switch to auto mode
                    if (app.blendMode != AnimBlendMode::IDLE) {
                        app.blendMode = AnimBlendMode::IDLE;
                        std::cout << "\n[AUTO MODE] Continuous playback" << std::endl;
                        printStatus(app);
                    }
                    break;

                case SDLK_s:
                    // Switch to manual blend mode
                    if (app.blendMode != AnimBlendMode::MANUAL_BLEND) {
                        app.blendMode = AnimBlendMode::MANUAL_BLEND;
                        app.previousAnimIdx = app.currentAnimIdx;
                        app.previousAnimTime = app.animationTime;  // Save previous animation time
                        app.currentAnimIdx = (app.currentAnimIdx + 1) % app.animNames.size();
                        app.blendAlpha = 0.5f;  // Start at middle
                        app.animationTime = 0.0f;  // Reset current animation time
                        app.paused = true;
                        std::cout << "\n[MANUAL MODE] Use arrow keys to control blend" << std::endl;
                        std::cout << "Blending: " << app.animNames[app.previousAnimIdx]
                                  << " ↔ " << app.animNames[app.currentAnimIdx] << std::endl;
                        printStatus(app);
                    }
                    break;

                case SDLK_LEFT:
                    if (app.blendMode == AnimBlendMode::MANUAL_BLEND) {
                        app.blendAlpha = std::max(0.0f, app.blendAlpha - 0.05f);
                        printStatus(app);
                    }
                    break;

                case SDLK_RIGHT:
                    if (app.blendMode == AnimBlendMode::MANUAL_BLEND) {
                        app.blendAlpha = std::min(1.0f, app.blendAlpha + 0.05f);
                        printStatus(app);
                    }
                    break;

                case SDLK_p:
                    app.paused = !app.paused;
                    std::cout << (app.paused ? "[PAUSED]" : "[RESUMED]") << std::endl;
                    break;

                case SDLK_1:
                case SDLK_2:
                case SDLK_3:
                    {
                        size_t idx = event.key.keysym.sym - SDLK_1;
                        if (idx < app.animNames.size()) {
                            app.currentAnimIdx = idx;
                            app.blendMode = AnimBlendMode::IDLE;
                            app.animationTime = 0.0f;
                            std::cout << "\n[SWITCH] Playing: " << app.animNames[idx] << std::endl;
                        }
                    }
                    break;
            }
        }
    }
}

void update(App& app) {
    // Calculate delta time
    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> elapsed = now - app.lastFrameTime;
    app.deltaTime = elapsed.count();
    app.lastFrameTime = now;

    // Update animation time
    if (!app.paused) {
        app.animationTime += app.deltaTime;
    }

    // Update blend state
    switch (app.blendMode) {
        case AnimBlendMode::IDLE:
            // Normal playback
            if (app.animator->applyAnimationByName(app.animNames[app.currentAnimIdx].c_str(), app.animationTime)) {
                app.animator->updateBoneMatrices();
            }
            break;

        case AnimBlendMode::AUTO_BLEND:
            {
                // Calculate blend alpha based on time
                float blendProgress = (app.animationTime - app.blendStartTime) / app.blendDuration;
                app.blendAlpha = std::min(1.0f, blendProgress);

                // Apply current animation first
                if (app.animator->applyAnimationByName(app.animNames[app.currentAnimIdx].c_str(), app.animationTime)) {
                    // Then blend with previous
                    app.animator->applyCrossFadeByName(
                        app.animNames[app.previousAnimIdx].c_str(),
                        app.previousAnimTime,
                        app.blendAlpha
                    );
                    app.animator->updateBoneMatrices();
                }

                // Check if blend is complete
                if (app.blendAlpha >= 1.0f) {
                    app.blendMode = AnimBlendMode::IDLE;
                    std::cout << "[BLEND COMPLETE] Now playing: " << app.animNames[app.currentAnimIdx] << std::endl;
                }
            }
            break;

        case AnimBlendMode::MANUAL_BLEND:
            // Manual blend (alpha controlled by arrow keys)
            if (app.animator->applyAnimationByName(app.animNames[app.currentAnimIdx].c_str(), app.animationTime)) {
                app.animator->applyCrossFadeByName(
                    app.animNames[app.previousAnimIdx].c_str(),
                    app.previousAnimTime,
                    app.blendAlpha
                );
                app.animator->updateBoneMatrices();
            }
            break;
    }
}

void render(App& app) {
    if (app.renderer->beginFrame(app.swapChain)) {
        app.renderer->render(app.view);
        app.renderer->endFrame();
    }
}

void printInstructions() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Animation Blending Sample" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  SPACE      - Trigger auto blend to next animation" << std::endl;
    std::cout << "  A          - Auto mode (continuous playback)" << std::endl;
    std::cout << "  S          - Manual blend mode" << std::endl;
    std::cout << "  LEFT/RIGHT - Adjust blend alpha (manual mode)" << std::endl;
    std::cout << "  1/2/3      - Switch to specific animation" << std::endl;
    std::cout << "  P          - Pause/Resume" << std::endl;
    std::cout << "  Q/ESC      - Quit" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void printStatus(const App& app) {
    std::cout << "Alpha: " << (int)(app.blendAlpha * 100) << "% | "
              << "Mode: ";
    switch (app.blendMode) {
        case AnimBlendMode::IDLE:
            std::cout << "Auto | Current: " << app.animNames[app.currentAnimIdx];
            break;
        case AnimBlendMode::AUTO_BLEND:
            std::cout << "Auto Blend | " << app.animNames[app.previousAnimIdx]
                      << " → " << app.animNames[app.currentAnimIdx];
            break;
        case AnimBlendMode::MANUAL_BLEND:
            std::cout << "Manual | " << app.animNames[app.previousAnimIdx]
                      << " ↔ " << app.animNames[app.currentAnimIdx];
            break;
    }
    std::cout << std::endl;
}

void cleanup(App& app) {
    if (app.animator && app.animAsset) {
        app.animator->unloadAnimationsFromSource(ANIM_SOURCE_ID);
    }

    // AnimationAsset is unique_ptr, will be destroyed automatically

    if (app.meshAsset && app.assetLoader) {
        app.assetLoader->destroyAsset(app.meshAsset);
    }

    if (app.assetLoader) {
        gltfio_ext::AssetLoader::destroy(&app.assetLoader);
    }

    delete app.resourceLoader;
    delete app.nameManager;

    if (app.materialProvider) {
        app.materialProvider->destroyMaterials();
        delete app.materialProvider;
    }

    if (app.stbProvider) delete app.stbProvider;
    if (app.ktx2Provider) delete app.ktx2Provider;

    if (app.engine) {
        app.engine->destroy(app.renderer);
        app.engine->destroy(app.scene);
        app.engine->destroy(app.view);
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
