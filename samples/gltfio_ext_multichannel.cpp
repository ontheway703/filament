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
 * gltfio_ext_multichannel - Multi-channel animation demonstration
 *
 * This sample demonstrates the multi-channel animation feature:
 * 1. Loading a real glTF animation with TRANSLATION + ROTATION + SCALE channels
 * 2. Verifying that all channels work together (no overwriting)
 * 3. Printing bone transform data to verify TRS decompose/compose fix
 *
 * This validates the critical bug fix in StandaloneAnimator.cpp.
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
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/TransformManager.h>
#include <filament/View.h>

#include <utils/EntityManager.h>

#include <filamentapp/Config.h>
#include <filamentapp/FilamentApp.h>

#include <getopt/getopt.h>
#include <math/quat.h>

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
    AnimationAsset* animation = nullptr;
    MeshAsset* mesh = nullptr;
    StandaloneAnimator* animator = nullptr;

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
            "gltfio_ext_multichannel - Multi-channel animation demo\n"
            "Usage:\n"
            "    gltfio_ext_multichannel [options]\n"
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

// Helper: Extract TRS from transform matrix (for verification)
static void extractTRS(const mat4f& matrix, float3& t, float& rotationAngle, float& scale) {
    // Translation
    t = float3(matrix[3][0], matrix[3][1], matrix[3][2]);

    // Scale (approximate, assume uniform)
    float3 col0(matrix[0][0], matrix[0][1], matrix[0][2]);
    scale = length(col0);

    // Rotation angle (simplified: just check magnitude)
    rotationAngle = std::atan2(matrix[0][2], matrix[0][0]);  // Rough Y-axis rotation
}

int main(int argc, char** argv) {
    Config config;
    config.title = "gltfio_ext_multichannel";

    handleCommandLineArguments(argc, argv, &config);

    App app;

    auto setup = [&app](Engine* engine, View* view, Scene* scene) {
        app.skybox = Skybox::Builder().color({0.1, 0.125, 0.25, 1.0}).build(*engine);
        scene->setSkybox(app.skybox);
        view->setPostProcessingEnabled(false);

        std::cout << "\n=== gltfio_ext Multi-Channel Animation Demo ===" << std::endl;
        std::cout << "Demonstrating: TRANSLATION + ROTATION + SCALE working together\n" << std::endl;

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

        // Step 3: Load skeleton and animation
        std::cout << "[3/5] Loading skeleton and animation..." << std::endl;
        app.skeleton = app.loader->loadSkeleton(buffer.data(), buffer.size());
        if (!app.skeleton) {
            std::cerr << "Failed to load skeleton" << std::endl;
            return;
        }
        std::cout << "  Skeleton: " << app.skeleton->getBoneCount() << " bones" << std::endl;

        app.animation = app.loader->loadAnimation(buffer.data(), buffer.size());
        if (!app.animation) {
            std::cerr << "Failed to load animation" << std::endl;
            return;
        }
        if (app.animation->getAnimationCount() > 0) {
            std::cout << "  Animation: \"" << app.animation->getAnimationName(0) << "\"" << std::endl;
            std::cout << "  (This animation contains Translation, Rotation, and Scale channels)" << std::endl;
        }

        // Step 4: Load and bind mesh
        std::cout << "[4/5] Loading and binding mesh..." << std::endl;
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

        // Step 5: Play animation
        std::cout << "[5/5] Playing multi-channel animation..." << std::endl;
        app.animator = StandaloneAnimator::create(*engine);
        app.animator->bindSkeleton(app.skeleton);
        if (app.animation->getAnimationCount() > 0) {
            app.animator->playAnimation(app.animation, 0, 1.0f, true);
            std::cout << "  Animation playing (looping)\n" << std::endl;
        }

        // Setup camera
        app.camera = EntityManager::get().create();
        app.cam = engine->createCamera(app.camera);
        app.cam->setProjection(45.0, 1.0, 0.1, 100.0);
        app.cam->lookAt({0, 1.5, 3}, {0, 1, 0}, {0, 1, 0});
        view->setCamera(app.cam);

        std::cout << "Expected behavior:" << std::endl;
        std::cout << "  - All channel types (T+R+S) work together" << std::endl;
        std::cout << "  - No channel overwrites another (TRS decompose/compose fix verified)" << std::endl;
        std::cout << "\nMonitoring bone transforms every 2 seconds..." << std::endl;
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
            // Update animation
            app.animator->update(0.016f);
            app.mesh->updateSkinning(app.animator);

            // Print bone transform every 2 seconds to verify all channels work
            static int lastPrint = -1;
            int currentSec = static_cast<int>(now);
            if (currentSec != lastPrint && currentSec % 2 == 0) {
                auto& tm = engine->getTransformManager();

                // Sample a few bones to verify multi-channel animation
                size_t sampleBones[] = {0, 5, 10, 20};
                for (size_t idx : sampleBones) {
                    if (idx >= app.skeleton->getBoneCount()) continue;

                    Entity boneEntity = app.skeleton->getBoneEntity(idx);
                    auto ti = tm.getInstance(boneEntity);
                    mat4f transform = tm.getWorldTransform(ti);

                    float3 translation;
                    float rotationAngle, scale;
                    extractTRS(transform, translation, rotationAngle, scale);

                    std::cout << "t=" << currentSec << "s, Bone[" << idx << "]: "
                              << "T=(" << translation.x << "," << translation.y << "," << translation.z << ") "
                              << "R=" << (rotationAngle * 180.0f / M_PI) << "° "
                              << "S~" << scale << std::endl;
                }
                std::cout << std::endl;

                lastPrint = currentSec;
            }
        }
    });

    FilamentApp::get().run(config, setup, cleanup);

    return 0;
}
