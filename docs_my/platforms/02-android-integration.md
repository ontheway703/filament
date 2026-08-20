# Android 集成详解

## 概述

本文档详细介绍如何在 Android 应用中集成 Filament，包括 NDK 开发环境配置、JNI 绑定、SurfaceView/TextureView 集成和完整示例。

## 环境配置

### 必需工具

```bash
# Android Studio
# 下载: https://developer.android.com/studio

# Android NDK
# 通过 Android Studio SDK Manager 安装
# 或命令行:
sdkmanager --install "ndk;25.2.9519653"

# CMake
sdkmanager --install "cmake;3.22.1"
```

### Gradle 配置

**build.gradle (Project):**

```gradle
buildscript {
    repositories {
        google()
        mavenCentral()
    }
    dependencies {
        classpath 'com.android.tools.build:gradle:8.1.0'
    }
}
```

**build.gradle (Module):**

```gradle
plugins {
    id 'com.android.application'
}

android {
    namespace 'com.example.filamentapp'
    compileSdk 34

    defaultConfig {
        applicationId "com.example.filamentapp"
        minSdk 21  // Android 5.0
        targetSdk 34
        versionCode 1
        versionName "1.0"

        ndk {
            abiFilters 'armeabi-v7a', 'arm64-v8a'
        }

        externalNativeBuild {
            cmake {
                cppFlags "-std=c++17"
                arguments "-DANDROID_STL=c++_shared"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path file('src/main/cpp/CMakeLists.txt')
            version '3.22.1'
        }
    }

    buildFeatures {
        viewBinding true
    }
}

dependencies {
    implementation 'androidx.appcompat:appcompat:1.6.1'
    implementation 'com.google.android.material:material:1.9.0'

    // Filament AAR
    implementation 'com.google.android.filament:filament-android:1.51.5'
    implementation 'com.google.android.filament:gltfio-android:1.51.5'
    implementation 'com.google.android.filament:filament-utils-android:1.51.5'
}
```

## CMakeLists.txt 配置

**src/main/cpp/CMakeLists.txt:**

```cmake
cmake_minimum_required(VERSION 3.22.1)
project(filament-jni)

set(CMAKE_CXX_STANDARD 17)

# Filament 路径
set(FILAMENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../filament")

# 包含 Filament 头文件
include_directories(${FILAMENT_DIR}/include)

# 链接 Filament 库
add_library(filament SHARED IMPORTED)
set_target_properties(filament PROPERTIES IMPORTED_LOCATION
    ${FILAMENT_DIR}/lib/${ANDROID_ABI}/libfilament.so)

add_library(backend SHARED IMPORTED)
set_target_properties(backend PROPERTIES IMPORTED_LOCATION
    ${FILAMENT_DIR}/lib/${ANDROID_ABI}/libbackend.so)

# JNI 库
add_library(filament-jni SHARED
    native-lib.cpp
    FilamentHelper.cpp
)

target_link_libraries(filament-jni
    filament
    backend
    android
    log
)
```

## Java/Kotlin 层

### Activity 实现

**MainActivity.java:**

```java
package com.example.filamentapp;

import android.app.Activity;
import android.os.Bundle;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.Choreographer;

public class MainActivity extends Activity implements Choreographer.FrameCallback {

    static {
        System.loadLibrary("filament-jni");
    }

    private SurfaceView surfaceView;
    private long nativeEngine;
    private long nativeSwapChain;
    private long nativeRenderer;
    private long nativeView;
    private Choreographer choreographer;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        surfaceView = new SurfaceView(this);
        setContentView(surfaceView);

        surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                Surface surface = holder.getSurface();
                nativeEngine = nCreateEngine();
                nativeSwapChain = nCreateSwapChain(nativeEngine, surface);
                nativeRenderer = nCreateRenderer(nativeEngine);
                nativeView = nCreateView(nativeEngine);

                // 启动渲染循环
                choreographer = Choreographer.getInstance();
                choreographer.postFrameCallback(MainActivity.this);
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                nSetViewport(nativeView, width, height);
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                choreographer.removeFrameCallback(MainActivity.this);
                nDestroyView(nativeView);
                nDestroyRenderer(nativeRenderer);
                nDestroySwapChain(nativeSwapChain);
                nDestroyEngine(nativeEngine);
            }
        });
    }

    @Override
    public void doFrame(long frameTimeNanos) {
        // 渲染一帧
        nRenderFrame(nativeEngine, nativeSwapChain, nativeRenderer, nativeView);

        // 继续下一帧
        choreographer.postFrameCallback(this);
    }

    // Native 方法
    private native long nCreateEngine();
    private native long nCreateSwapChain(long engine, Surface surface);
    private native long nCreateRenderer(long engine);
    private native long nCreateView(long engine);
    private native void nSetViewport(long view, int width, int height);
    private native void nRenderFrame(long engine, long swapChain, long renderer, long view);
    private native void nDestroyView(long view);
    private native void nDestroyRenderer(long renderer);
    private native void nDestroySwapChain(long swapChain);
    private native void nDestroyEngine(long engine);
}
```

## Native 层 (C++)

### native-lib.cpp

```cpp
#include <jni.h>
#include <android/native_window_jni.h>
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/SwapChain.h>
#include <utils/EntityManager.h>

using namespace filament;

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_example_filamentapp_MainActivity_nCreateEngine(JNIEnv* env, jobject thiz) {
    Engine* engine = Engine::create(Engine::Backend::VULKAN);
    return reinterpret_cast<jlong>(engine);
}

JNIEXPORT jlong JNICALL
Java_com_example_filamentapp_MainActivity_nCreateSwapChain(
        JNIEnv* env, jobject thiz, jlong nativeEngine, jobject surface) {

    Engine* engine = reinterpret_cast<Engine*>(nativeEngine);
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);

    SwapChain* swapChain = engine->createSwapChain(window);

    return reinterpret_cast<jlong>(swapChain);
}

JNIEXPORT jlong JNICALL
Java_com_example_filamentapp_MainActivity_nCreateRenderer(
        JNIEnv* env, jobject thiz, jlong nativeEngine) {

    Engine* engine = reinterpret_cast<Engine*>(nativeEngine);
    Renderer* renderer = engine->createRenderer();

    return reinterpret_cast<jlong>(renderer);
}

JNIEXPORT jlong JNICALL
Java_com_example_filamentapp_MainActivity_nCreateView(
        JNIEnv* env, jobject thiz, jlong nativeEngine) {

    Engine* engine = reinterpret_cast<Engine*>(nativeEngine);

    Scene* scene = engine->createScene();
    View* view = engine->createView();
    view->setScene(scene);

    // 创建相机
    utils::Entity cameraEntity = utils::EntityManager::get().create();
    Camera* camera = engine->createCamera(cameraEntity);
    view->setCamera(camera);

    return reinterpret_cast<jlong>(view);
}

JNIEXPORT void JNICALL
Java_com_example_filamentapp_MainActivity_nSetViewport(
        JNIEnv* env, jobject thiz, jlong nativeView, jint width, jint height) {

    View* view = reinterpret_cast<View*>(nativeView);
    view->setViewport({0, 0, (uint32_t)width, (uint32_t)height});
}

JNIEXPORT void JNICALL
Java_com_example_filamentapp_MainActivity_nRenderFrame(
        JNIEnv* env, jobject thiz,
        jlong nativeEngine, jlong nativeSwapChain,
        jlong nativeRenderer, jlong nativeView) {

    Engine* engine = reinterpret_cast<Engine*>(nativeEngine);
    SwapChain* swapChain = reinterpret_cast<SwapChain*>(nativeSwapChain);
    Renderer* renderer = reinterpret_cast<Renderer*>(nativeRenderer);
    View* view = reinterpret_cast<View*>(nativeView);

    if (renderer->beginFrame(swapChain)) {
        renderer->render(view);
        renderer->endFrame();
    }
}

JNIEXPORT void JNICALL
Java_com_example_filamentapp_MainActivity_nDestroyView(
        JNIEnv* env, jobject thiz, jlong nativeView) {
    View* view = reinterpret_cast<View*>(nativeView);
    Engine* engine = &view->getEngine();
    engine->destroy(view);
}

JNIEXPORT void JNICALL
Java_com_example_filamentapp_MainActivity_nDestroyRenderer(
        JNIEnv* env, jobject thiz, jlong nativeRenderer) {
    Renderer* renderer = reinterpret_cast<Renderer*>(nativeRenderer);
    Engine* engine = &renderer->getEngine();
    engine->destroy(renderer);
}

JNIEXPORT void JNICALL
Java_com_example_filamentapp_MainActivity_nDestroySwapChain(
        JNIEnv* env, jobject thiz, jlong nativeSwapChain) {
    SwapChain* swapChain = reinterpret_cast<SwapChain*>(nativeSwapChain);
    Engine* engine = &swapChain->getEngine();
    engine->destroy(swapChain);
}

JNIEXPORT void JNICALL
Java_com_example_filamentapp_MainActivity_nDestroyEngine(
        JNIEnv* env, jobject thiz, jlong nativeEngine) {
    Engine* engine = reinterpret_cast<Engine*>(nativeEngine);
    Engine::destroy(&engine);
}

} // extern "C"
```

## 生命周期管理

### 处理 Activity 生命周期

```java
@Override
protected void onPause() {
    super.onPause();
    choreographer.removeFrameCallback(this);
}

@Override
protected void onResume() {
    super.onResume();
    if (nativeEngine != 0) {
        choreographer.postFrameCallback(this);
    }
}

@Override
protected void onDestroy() {
    super.onDestroy();
    // 确保清理资源
    if (nativeEngine != 0) {
        nDestroyView(nativeView);
        nDestroyRenderer(nativeRenderer);
        nDestroySwapChain(nativeSwapChain);
        nDestroyEngine(nativeEngine);
    }
}
```

## 相关文档

- [08-swapchain-creation.md](./08-swapchain-creation.md) - SwapChain 详解
- [09-lifecycle-management.md](./09-lifecycle-management.md) - 生命周期管理
