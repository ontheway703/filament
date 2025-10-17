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

#include "FilamentHelper.h"
#include <filament/Viewport.h>
#include <filament/Skybox.h>
#include <utils/EntityManager.h>
#include <math/mat4.h>
#include <math/scalar.h>
#include <cmath>
#include <iostream>

using namespace filament;
using namespace filament::math;
using namespace utils;

FilamentHelper* FilamentHelper::create(void* nativeWindow, int width, int height,
                                        Engine::Backend backend) {
    // Create Engine
    Engine* engine = Engine::create(backend);
    if (!engine) {
        std::cerr << "Failed to create Filament engine" << std::endl;
        return nullptr;
    }

    // Create SwapChain
    SwapChain* swapChain = engine->createSwapChain(nativeWindow);
    if (!swapChain) {
        std::cerr << "Failed to create SwapChain" << std::endl;
        Engine::destroy(&engine);
        return nullptr;
    }

    // Create Renderer
    Renderer* renderer = engine->createRenderer();
    if (!renderer) {
        std::cerr << "Failed to create Renderer" << std::endl;
        engine->destroy(swapChain);
        Engine::destroy(&engine);
        return nullptr;
    }

    // Create Scene
    Scene* scene = engine->createScene();

    // Create View
    View* view = engine->createView();
    view->setScene(scene);
    view->setViewport({0, 0, (uint32_t)width, (uint32_t)height});
    view->setPostProcessingEnabled(false);

    // Create Camera
    Entity cameraEntity = EntityManager::get().create();
    Camera* camera = engine->createCamera(cameraEntity);
    view->setCamera(camera);

    // Create skybox (simple color)
    Skybox* skybox = Skybox::Builder().color({0.1f, 0.125f, 0.25f, 1.0f}).build(*engine);
    scene->setSkybox(skybox);

    return new FilamentHelper(engine, renderer, swapChain, scene, view, camera, cameraEntity);
}

FilamentHelper::FilamentHelper(Engine* engine, Renderer* renderer, SwapChain* swapChain,
                                 Scene* scene, View* view, Camera* camera, Entity cameraEntity)
    : mEngine(engine), mRenderer(renderer), mSwapChain(swapChain),
      mScene(scene), mView(view), mCamera(camera), mCameraEntity(cameraEntity) {
    // Initialize camera
    setCameraTransform({0, 1.5f, 5}, {0, 1, 0});
}

FilamentHelper::~FilamentHelper() {
    if (mEngine) {
        // Destroy skybox
        Skybox* skybox = mScene->getSkybox();
        if (skybox) {
            mEngine->destroy(skybox);
        }

        mEngine->destroy(mView);
        mEngine->destroy(mScene);
        mEngine->destroy(mRenderer);
        mEngine->destroy(mSwapChain);
        mEngine->destroyCameraComponent(mCameraEntity);
        EntityManager::get().destroy(mCameraEntity);
        Engine::destroy(&mEngine);
    }
}

bool FilamentHelper::beginFrame() {
    if (!mRenderer->beginFrame(mSwapChain)) {
        return false;
    }
    return true;
}

void FilamentHelper::render() {
    mRenderer->render(mView);
}

void FilamentHelper::endFrame() {
    mRenderer->endFrame();
}

void FilamentHelper::renderFrame() {
    if (beginFrame()) {
        render();
        endFrame();
    }
}

void FilamentHelper::resize(int width, int height) {
    mWidth = width;
    mHeight = height;
    mView->setViewport({0, 0, (uint32_t)width, (uint32_t)height});
    updateCameraProjection();
}

void FilamentHelper::setCameraTransform(const float3& position, const float3& target) {
    mCameraPosition = position;
    mCameraTarget = target;
    mCameraDistance = length(position - target);

    // Calculate yaw and pitch from position relative to target
    float3 dir = normalize(position - target);
    mCameraYaw = std::atan2(dir.x, dir.z);
    mCameraPitch = std::asin(dir.y);

    mCamera->lookAt(position, target, {0, 1, 0});
    updateCameraProjection();
}

void FilamentHelper::rotateCamera(float deltaX, float deltaY) {
    // Update yaw and pitch
    mCameraYaw += deltaX * 0.01f;
    mCameraPitch += deltaY * 0.01f;

    // Clamp pitch to avoid gimbal lock
    float minPitch = -F_PI / 2.0f + 0.01f;
    float maxPitch = F_PI / 2.0f - 0.01f;
    mCameraPitch = std::max(minPitch, std::min(maxPitch, mCameraPitch));

    // Calculate new camera position
    float3 offset;
    offset.x = mCameraDistance * std::sin(mCameraYaw) * std::cos(mCameraPitch);
    offset.y = mCameraDistance * std::sin(mCameraPitch);
    offset.z = mCameraDistance * std::cos(mCameraYaw) * std::cos(mCameraPitch);

    mCameraPosition = mCameraTarget + offset;
    mCamera->lookAt(mCameraPosition, mCameraTarget, {0, 1, 0});
}

void FilamentHelper::panCamera(float deltaX, float deltaY) {
    // Get camera right and up vectors
    float3 forward = normalize(mCameraTarget - mCameraPosition);
    float3 worldUp = float3{0, 1, 0};
    float3 right = normalize(cross(forward, worldUp));
    float3 up = cross(right, forward);

    // Pan speed based on distance
    float panSpeed = mCameraDistance * 0.001f;

    float3 offset = -right * deltaX * panSpeed + up * deltaY * panSpeed;
    mCameraPosition += offset;
    mCameraTarget += offset;

    mCamera->lookAt(mCameraPosition, mCameraTarget, {0, 1, 0});
}

void FilamentHelper::zoomCamera(float delta) {
    mCameraDistance *= (1.0f - delta * 0.1f);
    mCameraDistance = std::max(0.1f, std::min(100.0f, mCameraDistance));

    // Recalculate position
    float3 dir = normalize(mCameraPosition - mCameraTarget);
    mCameraPosition = mCameraTarget + dir * mCameraDistance;
    mCamera->lookAt(mCameraPosition, mCameraTarget, {0, 1, 0});
}

Entity FilamentHelper::addDirectionalLight(const float3& direction, const float3& color,
                                           float intensity) {
    Entity light = EntityManager::get().create();
    LightManager::Builder(LightManager::Type::DIRECTIONAL)
        .color(color)
        .intensity(intensity)
        .direction(direction)
        .castShadows(true)
        .build(*mEngine, light);
    mScene->addEntity(light);
    return light;
}

void FilamentHelper::updateCameraProjection() {
    if (mWidth == 0 || mHeight == 0) return;

    double aspect = (double)mWidth / (double)mHeight;
    mCamera->setProjection(45.0, aspect, 0.1, 100.0, Camera::Fov::VERTICAL);
}
