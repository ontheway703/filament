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

#ifndef ANIMATION_UTILS_H
#define ANIMATION_UTILS_H

#include <string>
#include <vector>
#include <cstdint>

struct SDL_Window;

namespace animation_utils {

/**
 * Get the native window handle from an SDL window.
 * Platform-specific: returns NSWindow* on macOS, HWND on Windows, etc.
 */
void* getNativeWindow(SDL_Window* window);

#if defined(__APPLE__)
/**
 * Set up a CAMetalLayer for the given native window view (macOS only).
 * Returns the Metal layer as void*.
 */
void* setUpMetalLayer(void* nativeWindow);
#endif

/**
 * Resolve asset path. Searches in multiple locations:
 * 1. Absolute path if provided
 * 2. tmp/test_assets/ (for test assets)
 * 3. Relative to current working directory
 */
std::string getAssetPath(const std::string& filename);

/**
 * Read entire file into memory as a byte vector.
 * Returns empty vector on error.
 */
std::vector<uint8_t> readFile(const std::string& path);

} // namespace animation_utils

#endif // ANIMATION_UTILS_H
