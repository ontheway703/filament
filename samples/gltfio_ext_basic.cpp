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

#include "common/arguments.h"

#include <gltfio/AssetLoaderExt.h>
#include <gltfio/StandaloneAnimator.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/AnimationAsset.h>
#include <gltfio/MeshAsset.h>
#include <gltfio/MaterialProvider.h>

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/LightManager.h>
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
#include <algorithm>

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
    AnimationAsset* animation = nullptr;
    MeshAsset* mesh = nullptr;
    StandaloneAnimator* animator = nullptr;

    Camera* cam = nullptr;
    Entity camera;
    Skybox* skybox = nullptr;
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

static void printUsage(char* name) {
    std::string usage(
            "gltfio_ext_basic - Basic skeletal animation demo\n"
            "Usage:\n"
            "    gltfio_ext_basic [options]\n"
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
    config.title = "gltfio_ext_basic";

    handleCommandLineArguments(argc, argv, &config);

    App app;

    auto setup = [&app](Engine* engine, View* view, Scene* scene) {
        app.skybox = Skybox::Builder().color({0.1, 0.125, 0.25, 1.0}).build(*engine);
        scene->setSkybox(app.skybox);
        view->setPostProcessingEnabled(false);

        // Add directional light (CRITICAL for rendering PBR materials!)
        Entity light = EntityManager::get().create();
        LightManager::Builder(LightManager::Type::DIRECTIONAL)
            .color({0.98, 0.92, 0.89})
            .intensity(110000.0f)
            .direction({0.6, -1.0, -0.8})
            .castShadows(true)
            .build(*engine, light);
        scene->addEntity(light);

        std::cout << "\n=== gltfio_ext Basic Workflow Demo ===" << std::endl;
        std::cout << "Demonstrating: Load glTF → Extract skeleton/animation/mesh → Play\n" << std::endl;

        // Step 1: Read glTF file
        std::cout << "[1/6] Reading glTF file: " << MODEL_PATH << std::endl;
        std::vector<uint8_t> buffer = readFile(MODEL_PATH);
        if (buffer.empty()) {
            std::cerr << "Failed to read model file" << std::endl;
            return;
        }
        std::cout << "  Loaded " << buffer.size() / 1024 / 1024 << " MB" << std::endl;

        // Step 2: Create MaterialProvider and AssetLoaderExt
        std::cout << "[2/6] Creating AssetLoaderExt..." << std::endl;
        app.materials = createUbershaderProvider(engine, UBERARCHIVE_DEFAULT_DATA, UBERARCHIVE_DEFAULT_SIZE);

        AssetConfigurationExt loaderConfig;
        loaderConfig.engine = engine;
        loaderConfig.materials = app.materials;
        app.loader = AssetLoaderExt::create(loaderConfig);
        std::cout << "  AssetLoaderExt created" << std::endl;

        // Step 3: Load skeleton
        std::cout << "[3/6] Loading skeleton from glTF..." << std::endl;
        app.skeleton = app.loader->loadSkeleton(buffer.data(), buffer.size());
        if (!app.skeleton) {
            std::cerr << "Failed to load skeleton" << std::endl;
            return;
        }
        std::cout << "  Skeleton loaded: " << app.skeleton->getBoneCount() << " bones" << std::endl;

        // Step 4: Load animations (loads all animations from glTF file)
        std::cout << "[4/6] Loading animations from glTF..." << std::endl;
        app.animation = app.loader->loadAnimation(buffer.data(), buffer.size());
        if (!app.animation) {
            std::cerr << "Failed to load animations" << std::endl;
            return;
        }
        std::cout << "  Loaded " << app.animation->getAnimationCount() << " animation(s)" << std::endl;
        if (app.animation->getAnimationCount() > 0) {
            std::cout << "  Animation 0: \"" << app.animation->getAnimationName(0)
                      << "\" (duration: " << app.animation->getAnimationDuration(0) << "s)" << std::endl;
        }

        // Step 5: Load mesh
        std::cout << "[5/6] Loading mesh from glTF..." << std::endl;
        if (!app.loader) {
            std::cerr << "ERROR: app.loader is null!" << std::endl;
            return;
        }
        std::cout << "  Loader OK, buffer size: " << buffer.size() << std::endl;
        std::cout << "  Calling loadMesh()..." << std::flush;  // Force flush
        app.mesh = app.loader->loadMesh(buffer.data(), buffer.size());
        std::cout << " done" << std::endl;
        if (!app.mesh) {
            std::cerr << "Failed to load mesh" << std::endl;
            return;
        }
        std::cout << "  Mesh loaded (GPU data pending)" << std::endl;

        // Upload GPU resources
        std::cout << "  Uploading GPU resources..." << std::flush;
        app.mesh->uploadResources();
        std::cout << " done" << std::endl;

        // DEBUG: Check mesh properties
        auto bbox = app.mesh->getBoundingBox();
        std::cout << "  Mesh BoundingBox: min(" << bbox.min.x << ", " << bbox.min.y << ", " << bbox.min.z
                  << "), max(" << bbox.max.x << ", " << bbox.max.y << ", " << bbox.max.z << ")" << std::endl;

        Entity meshEntity = app.mesh->getRenderableEntity();
        std::cout << "  Mesh Entity ID: " << meshEntity.getId() << std::endl;

        auto& rm = engine->getRenderableManager();
        auto renderableInstance = app.mesh->getRenderableInstance();
        if (renderableInstance) {
            std::cout << "  Renderable instance is VALID" << std::endl;
            size_t primCount = rm.getPrimitiveCount(renderableInstance);
            std::cout << "  Primitive count: " << primCount << std::endl;
        } else {
            std::cout << "  WARNING: Renderable instance is INVALID" << std::endl;
        }

        // Bind mesh to skeleton
        if (app.mesh->bindSkeleton(app.skeleton)) {
            std::cout << "  ✓ Mesh bound to skeleton" << std::endl;
        } else {
            std::cout << "  WARNING: Failed to bind mesh to skeleton" << std::endl;
            std::cout << "  Setting identity matrices as fallback (model will display in bind pose)" << std::endl;

            // FALLBACK: Set identity matrices so model at least displays in bind pose
            auto& rm = engine->getRenderableManager();
            auto renderableInstance = app.mesh->getRenderableInstance();
            if (renderableInstance) {
                size_t boneCount = app.skeleton->getBoneCount();
                std::vector<math::mat4f> identityMatrices(boneCount);
                for (size_t i = 0; i < boneCount; ++i) {
                    identityMatrices[i] = math::mat4f();  // Identity matrix
                }
                rm.setBones(renderableInstance, identityMatrices.data(), boneCount);
                std::cout << "  Set " << boneCount << " identity bone matrices" << std::endl;
            }
        }

        // Add mesh to scene
        scene->addEntity(meshEntity);
        std::cout << "  ✓ Mesh added to scene" << std::endl;

        // Step 6: Create animator and play the first animation
        std::cout << "[6/6] Starting animation playback..." << std::endl;
        app.animator = StandaloneAnimator::create(*engine);
        app.animator->bindSkeleton(app.skeleton);

        // Play the first animation (index 0)
        if (app.animation->getAnimationCount() > 0) {
            int animId = app.animator->playAnimation(app.animation, 0, 1.0f, true);
            std::cout << "  Animation playing (ID: " << animId << ", animIndex: 0, looping)\n" << std::endl;
        } else {
            std::cout << "  WARNING: No animations to play\n" << std::endl;
        }

        // Setup camera
        app.camera = EntityManager::get().create();
        app.cam = engine->createCamera(app.camera);
        app.cam->setProjection(45.0, 1.0, 0.1, 100.0);

        // Calculate camera position based on bounding box
        float3 center = (bbox.min + bbox.max) * 0.5f;
        float3 size = bbox.max - bbox.min;
        float maxDim = std::max(std::max(size.x, size.y), size.z);
        float distance = maxDim * 2.5f; // 2.5x the size to see the whole model

        float3 cameraPos = center + float3(0, 0, distance);
        app.cam->lookAt(cameraPos, center, {0, 1, 0});

        std::cout << "  Camera: pos(" << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z
                  << "), lookAt(" << center.x << ", " << center.y << ", " << center.z << ")" << std::endl;

        view->setCamera(app.cam);

        std::cout << "Setup complete. Watch the skeletal animation!" << std::endl;
    };

    auto cleanup = [&app](Engine* engine, View*, Scene*) {
        engine->destroy(app.skybox);

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

        engine->destroyCameraComponent(app.camera);
        EntityManager::get().destroy(app.camera);
    };

    FilamentApp::get().animate([&app](Engine* engine, View* view, double now) {
        if (app.animator && app.mesh) {
            // Update animation (16ms frame time for 60fps)
            app.animator->update(0.016f);

            // Update mesh skinning
            app.mesh->updateSkinning(app.animator);
        }
    });

    FilamentApp::get().run(config, setup, cleanup);

    return 0;
}
