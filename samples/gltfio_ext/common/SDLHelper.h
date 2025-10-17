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

#ifndef GLTFIO_EXT_SDL_HELPER_H
#define GLTFIO_EXT_SDL_HELPER_H

#include <SDL.h>
#include <functional>

/**
 * SDLHelper - Simplified SDL window and event management for gltfio_ext samples
 */
class SDLHelper {
public:
    // Event callbacks
    using QuitCallback = std::function<void()>;
    using KeyCallback = std::function<void(const SDL_KeyboardEvent&)>;
    using MouseButtonCallback = std::function<void(const SDL_MouseButtonEvent&)>;
    using MouseMotionCallback = std::function<void(const SDL_MouseMotionEvent&)>;
    using MouseWheelCallback = std::function<void(const SDL_MouseWheelEvent&)>;
    using WindowEventCallback = std::function<void(const SDL_WindowEvent&)>;

    struct EventCallbacks {
        QuitCallback onQuit;
        KeyCallback onKeyDown;
        KeyCallback onKeyUp;
        MouseButtonCallback onMouseButtonDown;
        MouseButtonCallback onMouseButtonUp;
        MouseMotionCallback onMouseMotion;
        MouseWheelCallback onMouseWheel;
        WindowEventCallback onWindowEvent;
    };

    /**
     * Initialize SDL and create window
     */
    static SDLHelper* create(const char* title, int width, int height);

    /**
     * Destructor - cleans up SDL resources
     */
    ~SDLHelper();

    /**
     * Get native window handle for Filament (Metal Layer on macOS, HWND on Windows, etc)
     */
    void* getNativeWindow() const;

    /**
     * Get drawable size (considers high DPI)
     */
    void getDrawableSize(int* width, int* height) const;

    /**
     * Process SDL events - returns false if should quit
     */
    bool processEvents(const EventCallbacks& callbacks);

    /**
     * Get SDL window
     */
    SDL_Window* getWindow() const { return mWindow; }

private:
    SDLHelper(SDL_Window* window);

    SDL_Window* mWindow = nullptr;
    void* mNativeWindow = nullptr;
};

#endif // GLTFIO_EXT_SDL_HELPER_H
