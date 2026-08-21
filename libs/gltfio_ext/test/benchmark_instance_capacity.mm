/*
 * Copyright (C) 2026 The Android Open Source Project
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

#import <Metal/Metal.h>

#include "materials/uberarchive.h"

#include <gltfio_ext/AssetLoader.h>
#include <gltfio_ext/FilamentAsset.h>
#include <gltfio_ext/FilamentInstance.h>
#include <gltfio_ext/MaterialProvider.h>
#include <gltfio_ext/ResourceLoader.h>
#include <gltfio_ext/TextureProvider.h>

#include <filament/Engine.h>
#include <filament/Scene.h>

#include <utils/EntityManager.h>
#include <utils/NameComponentManager.h>

#include <mach/mach.h>
#include <malloc/malloc.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace filament;
using namespace filament::gltfio_ext;
using namespace utils;

namespace {

using Clock = std::chrono::steady_clock;

struct MemorySnapshot {
    uint64_t cpuFootprintBytes = 0;
    uint64_t heapInUseBytes = 0;
    uint64_t metalAllocatedBytes = 0;
};

uint64_t getProcessFootprint() {
    task_vm_info_data_t info = {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    const kern_return_t result = task_info(mach_task_self(), TASK_VM_INFO,
            reinterpret_cast<task_info_t>(&info), &count);
    return result == KERN_SUCCESS ? info.phys_footprint : 0;
}

MemorySnapshot snapshot(id<MTLDevice> device) {
    malloc_statistics_t heapStatistics = {};
    malloc_zone_statistics(malloc_default_zone(), &heapStatistics);
    return { getProcessFootprint(), heapStatistics.size_in_use,
        device ? uint64_t(device.currentAllocatedSize) : 0 };
}

int64_t delta(uint64_t value, uint64_t baseline) {
    return static_cast<int64_t>(value) - static_cast<int64_t>(baseline);
}

double milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

std::vector<uint8_t> readBinaryFile(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return {};
    }
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size), uint8_t{});
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        return {};
    }
    return bytes;
}

void printUsage(const char* executable) {
    std::cerr << "Usage: " << executable
              << " <model.glb> <shared|late|independent> <count> <noop|metal>"
                 " <retain|release>\n";
}

int runBenchmark(int argc, char** argv) {
    if (argc != 6) {
        printUsage(argv[0]);
        return 2;
    }

    const std::string mode = argv[2];
    const size_t instanceCount = std::strtoul(argv[3], nullptr, 10);
    const std::string backendName = argv[4];
    const bool releaseSourceData = std::string(argv[5]) == "release";
    if ((mode != "shared" && mode != "late" && mode != "independent") ||
            instanceCount == 0 ||
            (backendName != "noop" && backendName != "metal") ||
            (!releaseSourceData && std::string(argv[5]) != "retain")) {
        printUsage(argv[0]);
        return 2;
    }

    std::vector<uint8_t> modelBytes = readBinaryFile(argv[1]);
    if (modelBytes.empty()) {
        std::cerr << "Unable to read model: " << argv[1] << '\n';
        return 3;
    }

    id<MTLDevice> metalDevice = MTLCreateSystemDefaultDevice();
    const MemorySnapshot processStart = snapshot(metalDevice);
    const Engine::Backend backend = backendName == "metal" ?
            Engine::Backend::METAL : Engine::Backend::NOOP;
    Engine* engine = Engine::create(backend);
    if (!engine) {
        std::cerr << "Unable to create Filament engine\n";
        return 4;
    }

    auto* names = new NameComponentManager(EntityManager::get());
    MaterialProvider* materials = createUbershaderProvider(engine, UBERARCHIVE_DEFAULT_DATA,
            UBERARCHIVE_DEFAULT_SIZE);
    AssetConfiguration assetConfiguration;
    assetConfiguration.engine = engine;
    assetConfiguration.materials = materials;
    assetConfiguration.names = names;
    AssetLoader* assetLoader = materials ? AssetLoader::create(assetConfiguration) : nullptr;

    ResourceConfiguration resourceConfiguration;
    resourceConfiguration.engine = engine;
    resourceConfiguration.gltfPath = nullptr;
    resourceConfiguration.normalizeSkinningWeights = false;
    auto* resourceLoader = assetLoader ? new ResourceLoader(resourceConfiguration) : nullptr;
    TextureProvider* stbProvider = resourceLoader ? createStbProvider(engine) : nullptr;
    TextureProvider* ktx2Provider = resourceLoader ? createKtx2Provider(engine) : nullptr;
    TextureProvider* webpProvider = resourceLoader ? createWebpProvider(engine) : nullptr;
    if (resourceLoader && stbProvider) {
        resourceLoader->addTextureProvider("image/png", stbProvider);
        resourceLoader->addTextureProvider("image/jpeg", stbProvider);
    }
    if (resourceLoader && ktx2Provider) {
        resourceLoader->addTextureProvider("image/ktx2", ktx2Provider);
    }
    if (resourceLoader && webpProvider) {
        resourceLoader->addTextureProvider("image/webp", webpProvider);
    }
    if (!materials || !assetLoader || !resourceLoader || !stbProvider) {
        std::cerr << "Unable to create gltfio_ext infrastructure\n";
        delete resourceLoader;
        delete stbProvider;
        delete ktx2Provider;
        delete webpProvider;
        if (assetLoader) {
            AssetLoader::destroy(&assetLoader);
        }
        if (materials) {
            materials->destroyMaterials();
            delete materials;
        }
        delete names;
        Engine::destroy(&engine);
        return 5;
    }

    engine->flushAndWait();
    const MemorySnapshot infrastructureReady = snapshot(metalDevice);
    std::vector<FilamentAsset*> assets;
    std::vector<FilamentInstance*> instances;
    assets.reserve(mode == "independent" ? instanceCount : 1);
    instances.reserve(instanceCount);

    const auto createStart = Clock::now();
    if (mode == "shared") {
        instances.resize(instanceCount);
        FilamentAsset* asset = assetLoader->createInstancedAsset(modelBytes.data(),
                modelBytes.size(), instances.data(), instanceCount);
        if (asset) {
            assets.push_back(asset);
        }
    } else if (mode == "late") {
        FilamentAsset* asset = assetLoader->createAsset(modelBytes.data(), modelBytes.size());
        if (asset) {
            assets.push_back(asset);
            instances.push_back(asset->getInstance());
        }
    } else {
        for (size_t i = 0; i < instanceCount; ++i) {
            FilamentAsset* asset = assetLoader->createAsset(modelBytes.data(), modelBytes.size());
            if (!asset) {
                break;
            }
            assets.push_back(asset);
            instances.push_back(asset->getInstance());
        }
    }
    const auto createEnd = Clock::now();
    const size_t expectedInitialAssetCount = mode == "independent" ? instanceCount : 1u;
    const size_t expectedInitialInstanceCount = mode == "late" ? 1u : instanceCount;
    if (assets.size() != expectedInitialAssetCount ||
            instances.size() != expectedInitialInstanceCount) {
        std::cerr << "Unable to create requested assets or instances\n";
        for (FilamentAsset* asset : assets) {
            assetLoader->destroyAsset(asset);
        }
        delete resourceLoader;
        AssetLoader::destroy(&assetLoader);
        delete stbProvider;
        delete ktx2Provider;
        delete webpProvider;
        materials->destroyMaterials();
        delete materials;
        delete names;
        Engine::destroy(&engine);
        return 6;
    }

    engine->flushAndWait();
    const MemorySnapshot afterCreate = snapshot(metalDevice);
    const auto loadStart = Clock::now();
    bool loaded = true;
    for (FilamentAsset* asset : assets) {
        loaded = resourceLoader->loadResources(asset) && loaded;
    }
    engine->flushAndWait();
    const auto loadEnd = Clock::now();
    const MemorySnapshot afterLoad = snapshot(metalDevice);
    if (!loaded) {
        std::cerr << "Unable to load model resources\n";
    }

    const auto lateCreateStart = Clock::now();
    if (mode == "late") {
        for (size_t i = 1; i < instanceCount; ++i) {
            FilamentInstance* instance = assetLoader->createInstance(assets.front());
            if (!instance) {
                break;
            }
            instances.push_back(instance);
        }
    }
    engine->flushAndWait();
    const auto lateCreateEnd = Clock::now();
    const MemorySnapshot afterLateCreate = snapshot(metalDevice);
    if (instances.size() != instanceCount) {
        std::cerr << "Unable to create requested late instances\n";
        loaded = false;
    }

    if (releaseSourceData) {
        for (FilamentAsset* asset : assets) {
            asset->releaseSourceData();
        }
    }
    engine->flushAndWait();
    const MemorySnapshot afterRelease = snapshot(metalDevice);

    Scene* scene = engine->createScene();
    for (FilamentInstance* instance : instances) {
        scene->addEntities(instance->getEntities(), instance->getEntityCount());
    }
    constexpr size_t SWITCH_ITERATIONS = 1000;
    FilamentInstance* primary = instances.front();
    const auto switchStart = Clock::now();
    for (size_t i = 0; i < SWITCH_ITERATIONS; ++i) {
        scene->removeEntities(primary->getEntities(), primary->getEntityCount());
        scene->addEntities(primary->getEntities(), primary->getEntityCount());
    }
    const auto switchEnd = Clock::now();
    scene->removeAllEntities();
    engine->destroy(scene);

    for (FilamentAsset* asset : assets) {
        assetLoader->destroyAsset(asset);
    }
    delete resourceLoader;
    AssetLoader::destroy(&assetLoader);
    delete stbProvider;
    delete ktx2Provider;
    delete webpProvider;
    materials->destroyMaterials();
    delete materials;
    delete names;
    engine->flushAndWait();
    Engine::destroy(&engine);
    const MemorySnapshot afterCleanup = snapshot(metalDevice);

    std::cout << "{"
              << "\"mode\":\"" << mode << "\","
              << "\"count\":" << instanceCount << ','
              << "\"backend\":\"" << backendName << "\","
              << "\"source_data\":\"" << argv[5] << "\","
              << "\"model_bytes\":" << modelBytes.size() << ','
              << "\"create_ms\":" << milliseconds(createEnd - createStart) << ','
              << "\"load_ms\":" << milliseconds(loadEnd - loadStart) << ','
              << "\"late_create_ms\":"
              << milliseconds(lateCreateEnd - lateCreateStart) << ','
              << "\"scene_restore_us\":"
              << milliseconds(switchEnd - switchStart) * 1000.0 / SWITCH_ITERATIONS << ','
              << "\"cpu_process_start\":" << processStart.cpuFootprintBytes << ','
              << "\"cpu_infrastructure\":" << infrastructureReady.cpuFootprintBytes << ','
              << "\"cpu_after_create\":" << afterCreate.cpuFootprintBytes << ','
              << "\"cpu_after_load\":" << afterLoad.cpuFootprintBytes << ','
              << "\"cpu_after_late_create\":" << afterLateCreate.cpuFootprintBytes << ','
              << "\"cpu_after_release\":" << afterRelease.cpuFootprintBytes << ','
              << "\"cpu_after_cleanup\":" << afterCleanup.cpuFootprintBytes << ','
              << "\"cpu_resource_delta\":"
              << delta(afterRelease.cpuFootprintBytes, infrastructureReady.cpuFootprintBytes) << ','
              << "\"heap_process_start\":" << processStart.heapInUseBytes << ','
              << "\"heap_infrastructure\":" << infrastructureReady.heapInUseBytes << ','
              << "\"heap_after_create\":" << afterCreate.heapInUseBytes << ','
              << "\"heap_after_load\":" << afterLoad.heapInUseBytes << ','
              << "\"heap_after_late_create\":" << afterLateCreate.heapInUseBytes << ','
              << "\"heap_after_release\":" << afterRelease.heapInUseBytes << ','
              << "\"heap_after_cleanup\":" << afterCleanup.heapInUseBytes << ','
              << "\"heap_resource_delta\":"
              << delta(afterRelease.heapInUseBytes, infrastructureReady.heapInUseBytes) << ','
              << "\"metal_process_start\":" << processStart.metalAllocatedBytes << ','
              << "\"metal_infrastructure\":" << infrastructureReady.metalAllocatedBytes << ','
              << "\"metal_after_create\":" << afterCreate.metalAllocatedBytes << ','
              << "\"metal_after_load\":" << afterLoad.metalAllocatedBytes << ','
              << "\"metal_after_late_create\":"
              << afterLateCreate.metalAllocatedBytes << ','
              << "\"metal_after_release\":" << afterRelease.metalAllocatedBytes << ','
              << "\"metal_after_cleanup\":" << afterCleanup.metalAllocatedBytes << ','
              << "\"metal_resource_delta\":"
              << delta(afterRelease.metalAllocatedBytes, infrastructureReady.metalAllocatedBytes)
              << "}\n";
    return loaded ? 0 : 7;
}

} // namespace

int main(int argc, char** argv) {
    @autoreleasepool {
        return runBenchmark(argc, argv);
    }
}
