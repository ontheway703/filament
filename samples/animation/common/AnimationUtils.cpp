/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "AnimationUtils.h"

#include <SDL.h>
#include <SDL_syswm.h>

#include <fstream>
#include <iostream>
#include <filesystem>

#if defined(__APPLE__)
#include <Cocoa/Cocoa.h>
#include <QuartzCore/CAMetalLayer.h>
#endif

namespace animation_utils {

void* getNativeWindow(SDL_Window* window) {
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (!SDL_GetWindowWMInfo(window, &wmi)) {
        std::cerr << "Error: Unable to get window info: " << SDL_GetError() << std::endl;
        return nullptr;
    }

#if defined(__APPLE__)
    return wmi.info.cocoa.window;
#elif defined(_WIN32)
    return wmi.info.win.window;
#elif defined(__linux__)
    return (void*)(uintptr_t)wmi.info.x11.window;
#else
    std::cerr << "Error: Unsupported platform" << std::endl;
    return nullptr;
#endif
}

#if defined(__APPLE__)
void* setUpMetalLayer(void* nativeWindow) {
    NSWindow* nswindow = (__bridge NSWindow*)nativeWindow;
    NSView* contentView = [nswindow contentView];

    // Create and configure Metal layer
    CAMetalLayer* metalLayer = [CAMetalLayer layer];
    [contentView setWantsLayer:YES];
    [contentView setLayer:metalLayer];

    // Set the scale factor for Retina displays
    CGFloat scale = [[nswindow screen] backingScaleFactor];
    metalLayer.contentsScale = scale;

    // Set pixel format
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;

    return (__bridge void*)metalLayer;
}
#endif

std::string getAssetPath(const std::string& filename) {
    namespace fs = std::filesystem;

    // Try absolute path first
    if (fs::path(filename).is_absolute() && fs::exists(filename)) {
        return filename;
    }

    // Try test assets directory
    std::string testAssetsPath = "/Users/xuan/Desktop/Code/my/filament/tmp/test_assets/" + filename;
    if (fs::exists(testAssetsPath)) {
        return testAssetsPath;
    }

    // Try relative to current directory
    if (fs::exists(filename)) {
        return filename;
    }

    // Try in samples/animation directory
    std::string samplesPath = "samples/animation/" + filename;
    if (fs::exists(samplesPath)) {
        return samplesPath;
    }

    std::cerr << "Warning: Asset not found: " << filename << std::endl;
    return filename;  // Return original, let caller handle error
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Error: Unable to open file: " << path << std::endl;
        return {};
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        std::cerr << "Error: Failed to read file: " << path << std::endl;
        return {};
    }

    std::cout << "Successfully read file: " << path << " (" << size << " bytes)" << std::endl;
    return buffer;
}

} // namespace animation_utils
