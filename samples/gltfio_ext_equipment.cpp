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
 * 1. Loading multiple animations from a single glTF file (AnimationPack)
 * 2. Runtime animation switching
 * 3. Seamless transition between different animations
 *
 * The model has 3 animations: Pull, Push, Squat
 * They switch every 3 seconds in a loop.
 */

#include "common/arguments.h"

#include <gltfio/AssetLoaderExt.h>
#include <gltfio/StandaloneAnimator.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/AnimationAsset.h>
#include <gltfio/AnimationPack.h>
#include <gltfio/MeshAsset.h>
#include <gltfio/MaterialProvider.h>

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/View.h>

#include <utils/EntityManager.h>

#include <filamentapp/Config.h>
#include <filamentapp/FilamentApp.h>

#include <getopt/getopt.h>

#include <iostream>
#include <fstream>
#include <vector>

#include "generated/resources/resources.h"
#include "materials/uberarchive.h"

using namespace filament;
using namespace filament::gltfio;
using namespace filament::math;
using utils::Entity;
using utils::EntityManager;

struct App {
    AssetLoaderExt* loader = nullptr;
    MaterialProvider* materials = nullptr;

    SkeletonAsset* skeleton = nullptr;
    AnimationPack* animPack = nullptr;
    MeshAsset* mesh = nullptr;
    StandaloneAnimator* animator = nullptr;

    int currentAnimId = -1;
    float timeSinceLastSwitch = 0.0f;
    int animIndex = 0;

    Camera* cam = nullptr;
    Entity camera;
    Skybox* skybox = nullptr;
};

static const char* MODEL_PATH = "../third_party/models/ecorche/ecorche_jpeg_compressed.glb";

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

static void printUsage(char* name) {
    std::string usage(
            "gltfio_ext_equipment - Animation switching demo\n"
            "Usage:\n"
            "    gltfio_ext_equipment [options]\n"
            "Options:\n"
            "   --help, -h\n"
            "       Prints this message\n\n"
            "API_USAGE"
    );
    const std::string apiUsage("API_USAGE");
    for (size_t pos = usage.find(apiUsage); pos != std::string::npos;
         pos = usage.find(apiUsage, pos)) {
        usage.replace(pos, apiUsage.length(), samples::getBackendAPIArgumentsUsage());
    }
    std::cout << usage;
}

static int handleCommandLineArguments(int argc, char* argv[], Config* config) {
    static constexpr const char* OPTSTR = "ha:";
    static const struct option OPTIONS[] = {
            { "help", no_argument,       nullptr, 'h' },
            { "api",  required_argument, nullptr, 'a' },
            { nullptr, 0,                nullptr, 0 }
    };
    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, OPTSTR, OPTIONS, &option_index)) >= 0) {
        std::string arg(optarg ? optarg : "");
        switch (opt) {
            default:
            case 'h':
                printUsage(argv[0]);
                exit(0);
            case 'a':
                config->backend = samples::parseArgumentsForBackend(arg);
                break;
        }
    }
    return optind;
}

int main(int argc, char** argv) {
    Config config;
    config.title = "gltfio_ext_equipment";

    handleCommandLineArguments(argc, argv, &config);

    App app;

    auto setup = [&app](Engine* engine, View* view, Scene* scene) {
        app.skybox = Skybox::Builder().color({0.1, 0.125, 0.25, 1.0}).build(*engine);
        scene->setSkybox(app.skybox);
        view->setPostProcessingEnabled(false);

        std::cout << "\n=== gltfio_ext Animation Switching Demo ===" << std::endl;
        std::cout << "Demonstrating: Runtime animation switching\n" << std::endl;

        // Step 1: Read glTF file
        std::cout << "[1/5] Reading glTF file..." << std::endl;
        std::vector<uint8_t> buffer = readFile(MODEL_PATH);
        if (buffer.empty()) {
            std::cerr << "Failed to read model file" << std::endl;
            return;
        }
        std::cout << "  Loaded " << buffer.size() / 1024 / 1024 << " MB" << std::endl;

        // Step 2: Create MaterialProvider and AssetLoaderExt
        std::cout << "[2/5] Creating AssetLoaderExt..." << std::endl;
        app.materials = createUbershaderProvider(engine, UBERARCHIVE_DEFAULT_DATA, UBERARCHIVE_DEFAULT_SIZE);
        AssetConfigurationExt loaderConfig;
        loaderConfig.engine = engine;
        loaderConfig.materials = app.materials;
        app.loader = AssetLoaderExt::create(loaderConfig);

        // Step 3: Load skeleton
        std::cout << "[3/5] Loading skeleton..." << std::endl;
        app.skeleton = app.loader->loadSkeleton(buffer.data(), buffer.size());
        if (!app.skeleton) {
            std::cerr << "Failed to load skeleton" << std::endl;
            return;
        }
        std::cout << "  Skeleton loaded: " << app.skeleton->getBoneCount() << " bones" << std::endl;

        // Step 4: Load ALL animations using AnimationPack
        std::cout << "[4/5] Loading animation pack..." << std::endl;
        app.animPack = app.loader->loadAnimationPack(buffer.data(), buffer.size());
        if (!app.animPack) {
            std::cerr << "Failed to load animations" << std::endl;
            return;
        }
        std::cout << "  Loaded " << app.animPack->getAnimationCount() << " animations:" << std::endl;
        for (size_t i = 0; i < app.animPack->getAnimationCount(); ++i) {
            AnimationAsset* anim = app.animPack->getAnimation(i);
            std::cout << "    [" << i << "] \"" << (anim ? anim->getName() : "unnamed") << "\"" << std::endl;
        }

        // Step 5: Load and bind mesh
        std::cout << "[5/5] Loading and binding mesh..." << std::endl;
        app.mesh = app.loader->loadMesh(buffer.data(), buffer.size());
        if (!app.mesh) {
            std::cerr << "Failed to load mesh" << std::endl;
            return;
        }
        std::cout << "  Uploading GPU resources..." << std::flush;
        app.mesh->uploadResources();
        std::cout << " done" << std::endl;
        app.mesh->bindSkeleton(app.skeleton);
        scene->addEntity(app.mesh->getRenderableEntity());

        // Create animator and start with first animation
        app.animator = StandaloneAnimator::create(*engine);
        app.animator->bindSkeleton(app.skeleton);

        if (app.animPack->getAnimationCount() > 0) {
            AnimationAsset* firstAnim = app.animPack->getAnimation(0);
            app.currentAnimId = app.animator->playAnimation(firstAnim, 1.0f, true);
            std::cout << "\n  ▶ Now playing: \"" << (firstAnim ? firstAnim->getName() : "unnamed") << "\"" << std::endl;
        }

        // Setup camera
        app.camera = EntityManager::get().create();
        app.cam = engine->createCamera(app.camera);
        app.cam->setProjection(45.0, 1.0, 0.1, 100.0);
        app.cam->lookAt({0, 1.5, 3}, {0, 1, 0}, {0, 1, 0});
        view->setCamera(app.cam);

        std::cout << "\nDemo behavior:" << std::endl;
        std::cout << "  - Animation switches every 3 seconds" << std::endl;
        std::cout << "  - Cycling through: " << app.animPack->getAnimationCount() << " animations\n" << std::endl;
    };

    auto cleanup = [&app](Engine* engine, View*, Scene*) {
        engine->destroy(app.skybox);

        if (app.animator) {
            StandaloneAnimator::destroy(&app.animator);
        }

        if (app.loader) {
            if (app.mesh) app.loader->destroyMesh(app.mesh);
            if (app.animPack) app.loader->destroyAnimationPack(app.animPack);
            if (app.skeleton) app.loader->destroySkeleton(app.skeleton);
            AssetLoaderExt::destroy(&app.loader);
        }

        if (app.materials) {
            app.materials->destroyMaterials();
            delete app.materials;
        }

        engine->destroyCameraComponent(app.camera);
        EntityManager::get().destroy(app.camera);
    };

    FilamentApp::get().animate([&app](Engine* engine, View* view, double now) {
        if (app.animator && app.mesh && app.animPack) {
            // Update animation
            app.animator->update(0.016f);
            app.mesh->updateSkinning(app.animator);

            // Switch animations every 3 seconds
            app.timeSinceLastSwitch += 0.016f;
            if (app.timeSinceLastSwitch >= 3.0f) {
                app.timeSinceLastSwitch = 0.0f;

                // Stop current animation
                app.animator->stopAnimation(app.currentAnimId);

                // Switch to next animation
                app.animIndex = (app.animIndex + 1) % app.animPack->getAnimationCount();
                AnimationAsset* nextAnim = app.animPack->getAnimation(app.animIndex);
                app.currentAnimId = app.animator->playAnimation(nextAnim, 1.0f, true);

                std::cout << "\n[Animation Switch] ▶ Now playing: \""
                          << (nextAnim ? nextAnim->getName() : "unnamed") << "\"" << std::endl;
            }
        }
    });

    FilamentApp::get().run(config, setup, cleanup);

    return 0;
}
