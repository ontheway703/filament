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

#include "SDLHelper.h"
#include <SDL_syswm.h>
#include <iostream>

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#endif

SDLHelper* SDLHelper::create(const char* title, int width, int height) {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
        return nullptr;
    }

    // Create window
    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;

    SDL_Window* window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        flags
    );

    if (!window) {
        std::cerr << "Failed to create SDL window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return nullptr;
    }

    return new SDLHelper(window);
}

SDLHelper::SDLHelper(SDL_Window* window) : mWindow(window) {
    // Get native window handle
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);

    if (!SDL_GetWindowWMInfo(mWindow, &wmInfo)) {
        std::cerr << "Failed to get window info: " << SDL_GetError() << std::endl;
        return;
    }

#ifdef __APPLE__
    // macOS: Create Metal layer
    NSWindow* nswindow = wmInfo.info.cocoa.window;
    NSView* contentView = [nswindow contentView];

    // Create Metal layer
    CAMetalLayer* metalLayer = [CAMetalLayer layer];
    [contentView setLayer:metalLayer];
    [contentView setWantsLayer:YES];

    // Set drawable size
    int drawableWidth, drawableHeight;
    SDL_GL_GetDrawableSize(mWindow, &drawableWidth, &drawableHeight);
    CGSize drawableSize = CGSizeMake(drawableWidth, drawableHeight);
    metalLayer.drawableSize = drawableSize;

    mNativeWindow = (__bridge void*)metalLayer;

#elif defined(_WIN32)
    // Windows: HWND
    mNativeWindow = wmInfo.info.win.window;

#elif defined(__linux__)
    // Linux: X11 Window
    mNativeWindow = (void*)(uintptr_t)wmInfo.info.x11.window;
#endif
}

SDLHelper::~SDLHelper() {
    if (mWindow) {
        SDL_DestroyWindow(mWindow);
    }
    SDL_Quit();
}

void* SDLHelper::getNativeWindow() const {
    return mNativeWindow;
}

void SDLHelper::getDrawableSize(int* width, int* height) const {
    SDL_GL_GetDrawableSize(mWindow, width, height);
}

bool SDLHelper::processEvents(const EventCallbacks& callbacks) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                if (callbacks.onQuit) callbacks.onQuit();
                return false;

            case SDL_KEYDOWN:
                if (callbacks.onKeyDown) callbacks.onKeyDown(event.key);
                break;

            case SDL_KEYUP:
                if (callbacks.onKeyUp) callbacks.onKeyUp(event.key);
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (callbacks.onMouseButtonDown) callbacks.onMouseButtonDown(event.button);
                break;

            case SDL_MOUSEBUTTONUP:
                if (callbacks.onMouseButtonUp) callbacks.onMouseButtonUp(event.button);
                break;

            case SDL_MOUSEMOTION:
                if (callbacks.onMouseMotion) callbacks.onMouseMotion(event.motion);
                break;

            case SDL_MOUSEWHEEL:
                if (callbacks.onMouseWheel) callbacks.onMouseWheel(event.wheel);
                break;

            case SDL_WINDOWEVENT:
                if (callbacks.onWindowEvent) callbacks.onWindowEvent(event.window);
                break;
        }
    }
    return true;
}
