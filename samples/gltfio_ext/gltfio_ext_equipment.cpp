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

/**
 * gltfio_ext_equipment - Animation switching demonstration
 *
 * This sample demonstrates:
 * 1. Loading multiple animations from a single glTF file (AnimationAsset)
 * 2. Runtime animation switching using animation indices
 * 3. Seamless transition between different animations
 *
 * The model has 3 animations: Pull, Push, Squat
 * They switch every 3 seconds in a loop.
 */

#include "common/SDLHelper.h"
#include "common/FilamentHelper.h"

#include <gltfio/AssetLoaderExt.h>
#include <gltfio/StandaloneAnimator.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/AnimationAsset.h>
#include <gltfio/MeshAsset.h>
#include <gltfio/MaterialProvider.h>

#include <utils/EntityManager.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>

#include "generated/resources/resources.h"
#include "materials/uberarchive.h"

using namespace filament;
using namespace filament::gltfio;
using namespace filament::math;
using utils::Entity;
using utils::EntityManager;

struct App {
    SDLHelper* sdl = nullptr;
    FilamentHelper* filament = nullptr;

    AssetLoaderExt* loader = nullptr;
    MaterialProvider* materials = nullptr;

    SkeletonAsset* skeleton = nullptr;
    AnimationAsset* animations = nullptr;  // Contains multiple animations
    MeshAsset* mesh = nullptr;
    StandaloneAnimator* animator = nullptr;

    int currentAnimId = -1;
    float timeSinceLastSwitch = 0.0f;
    size_t animIndex = 0;  // Current animation index

    bool running = true;
    bool leftMouseDown = false;
    int lastMouseX = 0;
    int lastMouseY = 0;

    std::chrono::steady_clock::time_point lastFrameTime;
};

static const char* MODEL_PATH = "../../third_party/models/ecorche/ecorche_jpeg_compressed.glb";

// Helper: Read file into memory
static std::vector<uint8_t> readFile(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << path << std::endl;
        return {};
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read((char*)buffer.data(), size)) {
        std::cerr << "Failed to read file: " << path << std::endl;
        return {};
    }

    return buffer;
}

int main(int argc, char** argv) {
    std::cout << "\n=== gltfio_ext Animation Switching Demo ===" << std::endl;
    std::cout << "Demonstrating: Runtime animation switching\n" << std::endl;

    App app;
    app.lastFrameTime = std::chrono::steady_clock::now();

    // 1. Initialize SDL and create window
    std::cout << "[1/6] Initializing SDL and creating window..." << std::endl;
    app.sdl = SDLHelper::create("gltfio_ext_equipment", 1024, 768);
    if (!app.sdl) {
        return 1;
    }

    // 2. Initialize Filament
    std::cout << "[2/6] Initializing Filament engine..." << std::endl;
    int width, height;
    app.sdl->getDrawableSize(&width, &height);
    app.filament = FilamentHelper::create(app.sdl->getNativeWindow(), width, height);
    if (!app.filament) {
        delete app.sdl;
        return 1;
    }

    // 3. Read glTF file
    std::cout << "[3/6] Reading glTF file..." << std::endl;
    std::vector<uint8_t> buffer = readFile(MODEL_PATH);
    if (buffer.empty()) {
        std::cerr << "Failed to read model file" << std::endl;
        delete app.filament;
        delete app.sdl;
        return 1;
    }
    std::cout << "  Loaded " << buffer.size() / 1024 / 1024 << " MB" << std::endl;

    // 4. Create MaterialProvider and AssetLoaderExt
    std::cout << "[4/6] Creating AssetLoaderExt..." << std::endl;
    app.materials = createUbershaderProvider(app.filament->getEngine(),
                                              UBERARCHIVE_DEFAULT_DATA,
                                              UBERARCHIVE_DEFAULT_SIZE);

    AssetConfigurationExt loaderConfig;
    loaderConfig.engine = app.filament->getEngine();
    loaderConfig.materials = app.materials;
    app.loader = AssetLoaderExt::create(loaderConfig);

    // 5. Load skeleton
    std::cout << "[5/6] Loading skeleton..." << std::endl;
    app.skeleton = app.loader->loadSkeleton(buffer.data(), buffer.size());
    if (!app.skeleton) {
        std::cerr << "Failed to load skeleton" << std::endl;
        delete app.filament;
        delete app.sdl;
        return 1;
    }
    std::cout << "  Skeleton loaded: " << app.skeleton->getBoneCount() << " bones" << std::endl;

    // 6. Load ALL animations (AnimationAsset contains multiple animations)
    std::cout << "[5/6] Loading animations..." << std::endl;
    app.animations = app.loader->loadAnimation(buffer.data(), buffer.size());
    if (!app.animations) {
        std::cerr << "Failed to load animations" << std::endl;
        delete app.filament;
        delete app.sdl;
        return 1;
    }
    std::cout << "  Loaded " << app.animations->getAnimationCount() << " animations:" << std::endl;
    for (size_t i = 0; i < app.animations->getAnimationCount(); ++i) {
        std::cout << "    [" << i << "] \"" << app.animations->getAnimationName(i) << "\"" << std::endl;
    }

    // 7. Load and bind mesh
    std::cout << "[6/6] Loading and binding mesh..." << std::endl;
    app.mesh = app.loader->loadMesh(buffer.data(), buffer.size());
    if (!app.mesh) {
        std::cerr << "Failed to load mesh" << std::endl;
        delete app.filament;
        delete app.sdl;
        return 1;
    }
    std::cout << "  Uploading GPU resources..." << std::flush;
    app.mesh->uploadResources();
    std::cout << " done" << std::endl;
    app.mesh->bindSkeleton(app.skeleton);
    app.filament->getScene()->addEntity(app.mesh->getRenderableEntity());

    // Create animator and start with first animation
    app.animator = StandaloneAnimator::create(*app.filament->getEngine());
    app.animator->bindSkeleton(app.skeleton);

    if (app.animations->getAnimationCount() > 0) {
        app.currentAnimId = app.animator->playAnimation(app.animations, 0, 1.0f, true);
        std::cout << "\n  ▶ Now playing: \"" << app.animations->getAnimationName(0) << "\"" << std::endl;
    }

    // Setup camera
    app.filament->setCameraTransform({0, 1.5f, 3}, {0, 1, 0});

    std::cout << "\nDemo behavior:" << std::endl;
    std::cout << "  - Animation switches every 3 seconds" << std::endl;
    std::cout << "  - Cycling through: " << app.animations->getAnimationCount() << " animations" << std::endl;
    std::cout << "Controls: Left mouse to rotate, ESC to quit\n" << std::endl;

    // Main loop
    SDLHelper::EventCallbacks callbacks;

    callbacks.onQuit = [&app]() {
        app.running = false;
    };

    callbacks.onKeyDown = [&app](const SDL_KeyboardEvent& e) {
        if (e.keysym.sym == SDLK_ESCAPE) {
            app.running = false;
        }
    };

    callbacks.onMouseButtonDown = [&app](const SDL_MouseButtonEvent& e) {
        if (e.button == SDL_BUTTON_LEFT) {
            app.leftMouseDown = true;
            app.lastMouseX = e.x;
            app.lastMouseY = e.y;
        }
    };

    callbacks.onMouseButtonUp = [&app](const SDL_MouseButtonEvent& e) {
        if (e.button == SDL_BUTTON_LEFT) {
            app.leftMouseDown = false;
        }
    };

    callbacks.onMouseMotion = [&app](const SDL_MouseMotionEvent& e) {
        if (app.leftMouseDown) {
            int deltaX = e.x - app.lastMouseX;
            int deltaY = e.y - app.lastMouseY;
            app.filament->rotateCamera(deltaX, deltaY);
            app.lastMouseX = e.x;
            app.lastMouseY = e.y;
        }
    };

    callbacks.onWindowEvent = [&app](const SDL_WindowEvent& e) {
        if (e.event == SDL_WINDOWEVENT_RESIZED || e.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            int newWidth, newHeight;
            app.sdl->getDrawableSize(&newWidth, &newHeight);
            app.filament->resize(newWidth, newHeight);
        }
    };

    while (app.running) {
        if (!app.sdl->processEvents(callbacks)) {
            break;
        }

        // Calculate delta time
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            currentTime - app.lastFrameTime);
        float deltaTime = elapsed.count() / 1000000.0f;
        app.lastFrameTime = currentTime;

        // Update animation
        if (app.animator && app.mesh && app.animations) {
            app.animator->update(deltaTime);
            app.mesh->updateSkinning(app.animator);

            // Switch animations every 3 seconds
            app.timeSinceLastSwitch += deltaTime;
            if (app.timeSinceLastSwitch >= 3.0f) {
                app.timeSinceLastSwitch = 0.0f;

                // Stop current animation
                app.animator->stopAnimation(app.currentAnimId);

                // Switch to next animation
                app.animIndex = (app.animIndex + 1) % app.animations->getAnimationCount();
                app.currentAnimId = app.animator->playAnimation(app.animations, app.animIndex, 1.0f, true);

                std::cout << "\n[Animation Switch] ▶ Now playing: \""
                          << app.animations->getAnimationName(app.animIndex) << "\"" << std::endl;
            }
        }

        // Render
        app.filament->renderFrame();

        // Simple frame rate limiting
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
    }

    // Cleanup
    std::cout << "\nCleaning up..." << std::endl;

    if (app.animator) {
        StandaloneAnimator::destroy(&app.animator);
    }

    if (app.loader) {
        if (app.mesh) app.loader->destroyMesh(app.mesh);
        if (app.animations) app.loader->destroyAnimation(app.animations);
        if (app.skeleton) app.loader->destroySkeleton(app.skeleton);
        AssetLoaderExt::destroy(&app.loader);
    }

    if (app.materials) {
        app.materials->destroyMaterials();
        delete app.materials;
    }

    delete app.filament;
    delete app.sdl;

    std::cout << "Done!" << std::endl;
    return 0;
}
