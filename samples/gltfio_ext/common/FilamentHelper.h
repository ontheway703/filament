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

#ifndef GLTFIO_EXT_FILAMENT_HELPER_H
#define GLTFIO_EXT_FILAMENT_HELPER_H

#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/SwapChain.h>
#include <filament/LightManager.h>
#include <utils/Entity.h>
#include <math/vec3.h>

/**
 * FilamentHelper - Simplified Filament engine management for gltfio_ext samples
 * Provides basic rendering setup, camera control, and render loop
 */
class FilamentHelper {
public:
    /**
     * Create FilamentHelper with native window
     */
    static FilamentHelper* create(void* nativeWindow, int width, int height,
                                   filament::Engine::Backend backend = filament::Engine::Backend::DEFAULT);

    /**
     * Destructor - cleans up Filament resources
     */
    ~FilamentHelper();

    /**
     * Begin frame rendering
     */
    bool beginFrame();

    /**
     * Render the scene
     */
    void render();

    /**
     * End frame and present
     */
    void endFrame();

    /**
     * Convenience: beginFrame + render + endFrame
     */
    void renderFrame();

    /**
     * Handle window resize
     */
    void resize(int width, int height);

    /**
     * Simple camera control - rotate around target
     */
    void rotateCamera(float deltaX, float deltaY);

    /**
     * Simple camera control - pan camera
     */
    void panCamera(float deltaX, float deltaY);

    /**
     * Simple camera control - zoom (dolly)
     */
    void zoomCamera(float delta);

    /**
     * Set camera position and target
     */
    void setCameraTransform(const filament::math::float3& position,
                           const filament::math::float3& target);

    /**
     * Add a light to the scene
     */
    utils::Entity addDirectionalLight(const filament::math::float3& direction,
                                      const filament::math::float3& color,
                                      float intensity);

    // Getters
    filament::Engine* getEngine() const { return mEngine; }
    filament::Scene* getScene() const { return mScene; }
    filament::View* getView() const { return mView; }
    filament::Camera* getCamera() const { return mCamera; }

private:
    FilamentHelper(filament::Engine* engine, filament::Renderer* renderer,
                   filament::SwapChain* swapChain, filament::Scene* scene,
                   filament::View* view, filament::Camera* camera,
                   utils::Entity cameraEntity);

    void updateCameraProjection();

    filament::Engine* mEngine = nullptr;
    filament::Renderer* mRenderer = nullptr;
    filament::SwapChain* mSwapChain = nullptr;
    filament::Scene* mScene = nullptr;
    filament::View* mView = nullptr;
    filament::Camera* mCamera = nullptr;
    utils::Entity mCameraEntity;

    int mWidth = 0;
    int mHeight = 0;

    // Camera state
    filament::math::float3 mCameraPosition = {0, 0, 5};
    filament::math::float3 mCameraTarget = {0, 0, 0};
    float mCameraDistance = 5.0f;
    float mCameraYaw = 0.0f;
    float mCameraPitch = 0.0f;
};

#endif // GLTFIO_EXT_FILAMENT_HELPER_H
