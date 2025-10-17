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
 * gltfio_ext_basic - Basic gltfio_ext workflow demonstration
 *
 * This sample demonstrates the fundamental workflow of gltfio_ext:
 * 1. Loading a skeleton from glTF file
 * 2. Loading an animation from the same glTF file
 * 3. Loading a mesh from the glTF file
 * 4. Using StandaloneAnimator to play the animation
 * 5. Updating skinning and rendering
 *
 * This is the simplest entry point to understand gltfio_ext basics.
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
#include <algorithm>
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
    AnimationAsset* animation = nullptr;
    MeshAsset* mesh = nullptr;
    StandaloneAnimator* animator = nullptr;

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
    std::cout << "\n=== gltfio_ext Basic Workflow Demo ===" << std::endl;
    std::cout << "Demonstrating: Load glTF → Extract skeleton/animation/mesh → Play\n" << std::endl;

    App app;
    app.lastFrameTime = std::chrono::steady_clock::now();

    // 1. Initialize SDL and create window
    std::cout << "[1/7] Initializing SDL and creating window..." << std::endl;
    app.sdl = SDLHelper::create("gltfio_ext_basic", 1024, 768);
    if (!app.sdl) {
        return 1;
    }

    // 2. Initialize Filament
    std::cout << "[2/7] Initializing Filament engine..." << std::endl;
    int width, height;
    app.sdl->getDrawableSize(&width, &height);
    app.filament = FilamentHelper::create(app.sdl->getNativeWindow(), width, height);
    if (!app.filament) {
        delete app.sdl;
        return 1;
    }

    // Add directional light
    app.filament->addDirectionalLight({0.6f, -1.0f, -0.8f}, {0.98f, 0.92f, 0.89f}, 110000.0f);

    // 3. Read glTF file
    std::cout << "[3/7] Reading glTF file: " << MODEL_PATH << std::endl;
    std::vector<uint8_t> buffer = readFile(MODEL_PATH);
    if (buffer.empty()) {
        std::cerr << "Failed to read model file" << std::endl;
        delete app.filament;
        delete app.sdl;
        return 1;
    }
    std::cout << "  Loaded " << buffer.size() / 1024 / 1024 << " MB" << std::endl;

    // 4. Create MaterialProvider and AssetLoaderExt
    std::cout << "[4/7] Creating AssetLoaderExt..." << std::endl;
    app.materials = createUbershaderProvider(app.filament->getEngine(),
                                              UBERARCHIVE_DEFAULT_DATA,
                                              UBERARCHIVE_DEFAULT_SIZE);

    AssetConfigurationExt loaderConfig;
    loaderConfig.engine = app.filament->getEngine();
    loaderConfig.materials = app.materials;
    app.loader = AssetLoaderExt::create(loaderConfig);
    std::cout << "  AssetLoaderExt created" << std::endl;

    // 5. Load skeleton, animation, and mesh
    std::cout << "[5/7] Loading skeleton from glTF..." << std::endl;
    app.skeleton = app.loader->loadSkeleton(buffer.data(), buffer.size());
    if (!app.skeleton) {
        std::cerr << "Failed to load skeleton" << std::endl;
        delete app.filament;
        delete app.sdl;
        return 1;
    }
    std::cout << "  Skeleton loaded: " << app.skeleton->getBoneCount() << " bones" << std::endl;

    std::cout << "[5/7] Loading animations from glTF..." << std::endl;
    app.animation = app.loader->loadAnimation(buffer.data(), buffer.size());
    if (!app.animation) {
        std::cerr << "Failed to load animations" << std::endl;
        delete app.filament;
        delete app.sdl;
        return 1;
    }
    std::cout << "  Loaded " << app.animation->getAnimationCount() << " animation(s)" << std::endl;
    if (app.animation->getAnimationCount() > 0) {
        std::cout << "  Animation 0: \"" << app.animation->getAnimationName(0)
                  << "\" (duration: " << app.animation->getAnimationDuration(0) << "s)" << std::endl;
    }

    std::cout << "[5/7] Loading mesh from glTF..." << std::endl;
    app.mesh = app.loader->loadMesh(buffer.data(), buffer.size());
    if (!app.mesh) {
        std::cerr << "Failed to load mesh" << std::endl;
        delete app.filament;
        delete app.sdl;
        return 1;
    }
    std::cout << "  Mesh loaded" << std::endl;

    // Upload GPU resources
    std::cout << "  Uploading GPU resources..." << std::flush;
    app.mesh->uploadResources();
    std::cout << " done" << std::endl;

    // Bind mesh to skeleton
    if (app.mesh->bindSkeleton(app.skeleton)) {
        std::cout << "  ✓ Mesh bound to skeleton" << std::endl;
    } else {
        std::cout << "  WARNING: Failed to bind mesh to skeleton" << std::endl;
    }

    // Add mesh to scene
    app.filament->getScene()->addEntity(app.mesh->getRenderableEntity());
    std::cout << "  ✓ Mesh added to scene" << std::endl;

    // 6. Create animator and play animation
    std::cout << "[6/7] Starting animation playback..." << std::endl;
    app.animator = StandaloneAnimator::create(*app.filament->getEngine());
    app.animator->bindSkeleton(app.skeleton);

    if (app.animation->getAnimationCount() > 0) {
        int animId = app.animator->playAnimation(app.animation, 0, 1.0f, true);
        std::cout << "  Animation playing (ID: " << animId << ", looping)\n" << std::endl;
    }

    // Setup camera
    auto bbox = app.mesh->getBoundingBox();
    float3 center = (bbox.min + bbox.max) * 0.5f;
    float3 size = bbox.max - bbox.min;
    float maxDim = std::max(std::max(size.x, size.y), size.z);
    float distance = maxDim * 2.5f;
    float3 cameraPos = center + float3(0, 0, distance);
    app.filament->setCameraTransform(cameraPos, center);

    std::cout << "[7/7] Setup complete. Entering render loop..." << std::endl;
    std::cout << "Controls: Left mouse to rotate, ESC to quit\n" << std::endl;

    // 7. Main loop
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
        if (app.animator && app.mesh) {
            app.animator->update(deltaTime);
            app.mesh->updateSkinning(app.animator);
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
        if (app.animation) app.loader->destroyAnimation(app.animation);
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
