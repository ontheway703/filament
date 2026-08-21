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

#include "materials/uberarchive.h"

#include <gltfio_ext/AnimationAsset.h>
#include <gltfio_ext/Animator.h>
#include <gltfio_ext/AssetLoader.h>
#include <gltfio_ext/FilamentAsset.h>
#include <gltfio_ext/FilamentInstance.h>
#include <gltfio_ext/MaterialProvider.h>
#include <gltfio_ext/ResourceLoader.h>

#include <filament/Engine.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>

#include <utils/EntityManager.h>
#include <utils/NameComponentManager.h>

#include <math/mat4.h>

#include <gtest/gtest.h>

#include <array>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

using namespace filament;
using namespace filament::gltfio_ext;
using namespace utils;

class InstanceLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        mEngine = Engine::create(Engine::Backend::NOOP);
        ASSERT_NE(mEngine, nullptr);

        mNameManager = new NameComponentManager(EntityManager::get());
        mMaterials = createUbershaderProvider(mEngine, UBERARCHIVE_DEFAULT_DATA,
                UBERARCHIVE_DEFAULT_SIZE);
        ASSERT_NE(mMaterials, nullptr);

        AssetConfiguration assetConfig;
        assetConfig.engine = mEngine;
        assetConfig.materials = mMaterials;
        assetConfig.names = mNameManager;
        mLoader = AssetLoader::create(assetConfig);
        ASSERT_NE(mLoader, nullptr);

        ResourceConfiguration resourceConfig;
        resourceConfig.engine = mEngine;
        resourceConfig.gltfPath = nullptr;
        resourceConfig.normalizeSkinningWeights = false;
        mResourceLoader = new ResourceLoader(resourceConfig);
    }

    void TearDown() override {
        for (Scene* scene : mScenes) {
            mEngine->destroy(scene);
        }
        for (FilamentAsset* asset : mAssets) {
            mLoader->destroyAsset(asset);
        }
        delete mResourceLoader;
        AssetLoader::destroy(&mLoader);
        if (mMaterials) {
            mMaterials->destroyMaterials();
            delete mMaterials;
        }
        delete mNameManager;
        Engine::destroy(&mEngine);
    }

    std::vector<uint8_t> readBinaryFile(const char* path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            return {};
        }
        const std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> bytes(size);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
            return {};
        }
        return bytes;
    }

    FilamentAsset* createAsset(const std::vector<uint8_t>& bytes,
            FilamentInstance** instances, size_t instanceCount) {
        FilamentAsset* asset = mLoader->createInstancedAsset(bytes.data(), bytes.size(),
                instances, instanceCount);
        if (asset) {
            mAssets.push_back(asset);
        }
        return asset;
    }

    Scene* createScene() {
        Scene* scene = mEngine->createScene();
        mScenes.push_back(scene);
        return scene;
    }

    static Entity findRenderable(const FilamentInstance& instance,
            const RenderableManager& renderableManager) {
        const Entity* entities = instance.getEntities();
        for (size_t i = 0, count = instance.getEntityCount(); i < count; ++i) {
            if (renderableManager.hasComponent(entities[i])) {
                return entities[i];
            }
        }
        return {};
    }

    Engine* mEngine = nullptr;
    NameComponentManager* mNameManager = nullptr;
    MaterialProvider* mMaterials = nullptr;
    AssetLoader* mLoader = nullptr;
    ResourceLoader* mResourceLoader = nullptr;
    std::vector<FilamentAsset*> mAssets;
    std::vector<Scene*> mScenes;
};

TEST_F(InstanceLifecycleTest, InstancedAssetHasIndependentMutableState) {
    const std::vector<uint8_t> mesh = readBinaryFile("ecorche_full.glb");
    ASSERT_FALSE(mesh.empty());

    std::array<FilamentInstance*, 2> instances = {};
    FilamentAsset* asset = createAsset(mesh, instances.data(), instances.size());
    ASSERT_NE(asset, nullptr);
    ASSERT_TRUE(mResourceLoader->loadResources(asset));
    ASSERT_NE(instances[0], nullptr);
    ASSERT_NE(instances[1], nullptr);

    EXPECT_EQ(asset->getAssetInstanceCount(), instances.size());
    EXPECT_EQ(asset->getAssetInstances()[0], instances[0]);
    EXPECT_EQ(asset->getAssetInstances()[1], instances[1]);
    EXPECT_NE(instances[0]->getRoot(), instances[1]->getRoot());
    EXPECT_NE(instances[0]->getAnimator(), instances[1]->getAnimator());
    ASSERT_EQ(instances[0]->getEntityCount(), instances[1]->getEntityCount());

    std::unordered_set<Entity, Entity::Hasher> firstEntities;
    for (size_t i = 0, count = instances[0]->getEntityCount(); i < count; ++i) {
        firstEntities.insert(instances[0]->getEntities()[i]);
    }
    for (size_t i = 0, count = instances[1]->getEntityCount(); i < count; ++i) {
        EXPECT_EQ(firstEntities.count(instances[1]->getEntities()[i]), 0u);
    }

    TransformManager& transforms = mEngine->getTransformManager();
    const TransformManager::Instance firstRoot = transforms.getInstance(instances[0]->getRoot());
    const TransformManager::Instance secondRoot = transforms.getInstance(instances[1]->getRoot());
    ASSERT_TRUE(firstRoot);
    ASSERT_TRUE(secondRoot);
    const float secondRootX = transforms.getTransform(secondRoot)[3].x;
    transforms.setTransform(firstRoot, math::mat4f::translation(math::float3{7.0f, 0.0f, 0.0f}));
    EXPECT_FLOAT_EQ(transforms.getTransform(firstRoot)[3].x, 7.0f);
    EXPECT_FLOAT_EQ(transforms.getTransform(secondRoot)[3].x, secondRootX);

    RenderableManager& renderables = mEngine->getRenderableManager();
    const Entity firstRenderable = findRenderable(*instances[0], renderables);
    const Entity secondRenderable = findRenderable(*instances[1], renderables);
    ASSERT_TRUE(firstRenderable);
    ASSERT_TRUE(secondRenderable);
    const RenderableManager::Instance firstRenderableInstance =
            renderables.getInstance(firstRenderable);
    const RenderableManager::Instance secondRenderableInstance =
            renderables.getInstance(secondRenderable);
    const uint8_t secondMask = renderables.getLayerMask(secondRenderableInstance);
    const uint8_t changedMask = renderables.getLayerMask(firstRenderableInstance) ^ 0x80u;
    renderables.setLayerMask(firstRenderableInstance, 0xffu, changedMask);
    EXPECT_EQ(renderables.getLayerMask(firstRenderableInstance), changedMask);
    EXPECT_EQ(renderables.getLayerMask(secondRenderableInstance), secondMask);

    ASSERT_EQ(instances[0]->getMaterialInstanceCount(),
            instances[1]->getMaterialInstanceCount());
    ASSERT_GT(instances[0]->getMaterialInstanceCount(), 0u);
    EXPECT_NE(instances[0]->getMaterialInstances()[0],
            instances[1]->getMaterialInstances()[0]);
}

TEST_F(InstanceLifecycleTest, InstanceEntitiesMoveBetweenScenesIndependently) {
    const std::vector<uint8_t> mesh = readBinaryFile("ecorche_full.glb");
    ASSERT_FALSE(mesh.empty());

    std::array<FilamentInstance*, 2> instances = {};
    FilamentAsset* asset = createAsset(mesh, instances.data(), instances.size());
    ASSERT_NE(asset, nullptr);
    ASSERT_TRUE(mResourceLoader->loadResources(asset));

    Scene* firstScene = createScene();
    Scene* secondScene = createScene();
    firstScene->addEntities(instances[0]->getEntities(), instances[0]->getEntityCount());
    secondScene->addEntities(instances[1]->getEntities(), instances[1]->getEntityCount());

    for (size_t i = 0, count = instances[0]->getEntityCount(); i < count; ++i) {
        const Entity entity = instances[0]->getEntities()[i];
        EXPECT_TRUE(firstScene->hasEntity(entity));
        EXPECT_FALSE(secondScene->hasEntity(entity));
    }
    for (size_t i = 0, count = instances[1]->getEntityCount(); i < count; ++i) {
        const Entity entity = instances[1]->getEntities()[i];
        EXPECT_FALSE(firstScene->hasEntity(entity));
        EXPECT_TRUE(secondScene->hasEntity(entity));
    }

    firstScene->removeEntities(instances[0]->getEntities(), instances[0]->getEntityCount());
    secondScene->addEntities(instances[0]->getEntities(), instances[0]->getEntityCount());
    for (size_t i = 0, count = instances[0]->getEntityCount(); i < count; ++i) {
        const Entity entity = instances[0]->getEntities()[i];
        EXPECT_FALSE(firstScene->hasEntity(entity));
        EXPECT_TRUE(secondScene->hasEntity(entity));
    }

    secondScene->removeEntities(instances[0]->getEntities(), instances[0]->getEntityCount());
    firstScene->addEntities(instances[0]->getEntities(), instances[0]->getEntityCount());
    for (size_t i = 0, count = instances[0]->getEntityCount(); i < count; ++i) {
        const Entity entity = instances[0]->getEntities()[i];
        EXPECT_TRUE(firstScene->hasEntity(entity));
        EXPECT_FALSE(secondScene->hasEntity(entity));
    }

    firstScene->removeAllEntities();
    secondScene->removeAllEntities();
}

TEST_F(InstanceLifecycleTest, PreallocatedInstancesSurviveSourceDataRelease) {
    const std::vector<uint8_t> mesh = readBinaryFile("ecorche_full.glb");
    const std::vector<uint8_t> animation = readBinaryFile("ecorche_animation_only.glb");
    ASSERT_FALSE(mesh.empty());
    ASSERT_FALSE(animation.empty());

    std::array<FilamentInstance*, 2> instances = {};
    FilamentAsset* asset = createAsset(mesh, instances.data(), instances.size());
    ASSERT_NE(asset, nullptr);
    ASSERT_TRUE(mResourceLoader->loadResources(asset));
    asset->releaseSourceData();

    EXPECT_EQ(mLoader->createInstance(asset), nullptr);
    EXPECT_NE(instances[0]->getAnimator(), nullptr);
    EXPECT_NE(instances[1]->getAnimator(), nullptr);

    std::unique_ptr<AnimationAsset> animationAsset =
            mLoader->loadAnimationAsset(animation.data(), animation.size());
    ASSERT_NE(animationAsset, nullptr);
    Animator* firstAnimator = instances[0]->getAnimator();
    Animator* secondAnimator = instances[1]->getAnimator();
    const size_t firstCount = firstAnimator->loadAnimationsFromSource("phase0", animationAsset.get());
    const size_t secondCount =
            secondAnimator->loadAnimationsFromSource("phase0", animationAsset.get());
    ASSERT_GT(firstCount, 0u);
    ASSERT_EQ(firstCount, secondCount);

    const std::vector<std::string> names = firstAnimator->getAnimationsInSource("phase0");
    ASSERT_FALSE(names.empty());
    EXPECT_TRUE(firstAnimator->applyAnimation("phase0", names.front().c_str(), 0.1f));
    EXPECT_TRUE(secondAnimator->applyAnimation("phase0", names.back().c_str(), 0.7f));
    firstAnimator->unloadAnimationsFromSource("phase0");
    secondAnimator->unloadAnimationsFromSource("phase0");
}

TEST_F(InstanceLifecycleTest, RetainedSourceDataSupportsLateInstanceExpansion) {
    const std::vector<uint8_t> mesh = readBinaryFile("ecorche_full.glb");
    ASSERT_FALSE(mesh.empty());

    FilamentAsset* asset = mLoader->createAsset(mesh.data(), mesh.size());
    ASSERT_NE(asset, nullptr);
    mAssets.push_back(asset);
    ASSERT_TRUE(mResourceLoader->loadResources(asset));
    FilamentInstance* first = asset->getInstance();
    ASSERT_NE(first, nullptr);

    FilamentInstance* second = mLoader->createInstance(asset);
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(asset->getAssetInstanceCount(), 2u);
    EXPECT_NE(first->getRoot(), second->getRoot());
    EXPECT_NE(first->getAnimator(), second->getAnimator());

    Scene* firstScene = createScene();
    Scene* secondScene = createScene();
    firstScene->addEntities(first->getEntities(), first->getEntityCount());
    secondScene->addEntities(second->getEntities(), second->getEntityCount());
    for (size_t i = 0, count = second->getEntityCount(); i < count; ++i) {
        const Entity entity = second->getEntities()[i];
        EXPECT_FALSE(firstScene->hasEntity(entity));
        EXPECT_TRUE(secondScene->hasEntity(entity));
    }
    firstScene->removeAllEntities();
    secondScene->removeAllEntities();

    asset->releaseSourceData();
    EXPECT_EQ(mLoader->createInstance(asset), nullptr);
}

TEST_F(InstanceLifecycleTest, AssetOwnsEveryInstanceEntity) {
    const std::vector<uint8_t> mesh = readBinaryFile("ecorche_full.glb");
    ASSERT_FALSE(mesh.empty());

    std::array<FilamentInstance*, 2> instances = {};
    FilamentAsset* asset = createAsset(mesh, instances.data(), instances.size());
    ASSERT_NE(asset, nullptr);

    std::vector<Entity> entities;
    for (FilamentInstance* instance : instances) {
        entities.push_back(instance->getRoot());
        entities.insert(entities.end(), instance->getEntities(),
                instance->getEntities() + instance->getEntityCount());
    }
    for (Entity entity : entities) {
        ASSERT_TRUE(EntityManager::get().isAlive(entity));
    }

    mLoader->destroyAsset(asset);
    mAssets.clear();
    for (Entity entity : entities) {
        EXPECT_FALSE(EntityManager::get().isAlive(entity));
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
