# Filament Android AAR 打包与发布

## 概述

Filament Android 采用模块化的 AAR (Android Archive) 打包策略，将不同功能组件分离成独立的库。本文档详细介绍 AAR 的构建、打包、发布和使用流程。

## AAR 模块架构

### 模块组织结构
```
Filament Android AAR 生态系统
├── filament-android          # 核心渲染引擎
│   ├── libfilament-jni.so    # 核心 JNI 库
│   ├── libfilament.so        # 渲染引擎
│   ├── libbackend.so         # 后端驱动
│   └── libutils.so           # 工具库
├── filamat-android           # 材质编译器
│   ├── libfilamat-jni.so     # 材质编译 JNI
│   └── libfilamat.so         # 材质编译核心
├── gltfio-android            # glTF 加载器
│   ├── libgltfio-jni.so      # glTF JNI 绑定
│   └── libgltfio.so          # glTF 核心库
├── filament-utils-android    # 通用工具
│   └── libfilament-utils.so  # 工具实现
└── common                    # 共享 JNI 工具
    └── libcommon.so          # 共享组件
```

### 依赖关系图
```
应用层
    ↓
┌─────────────────────────────────────────┐
│  filament-utils-android (可选)          │
├─────────────────────────────────────────┤
│  gltfio-android (可选)                  │
├─────────────────────────────────────────┤
│  filamat-android (可选)                 │
├─────────────────────────────────────────┤
│  filament-android (核心,必需)           │
├─────────────────────────────────────────┤
│  common (共享基础)                      │
└─────────────────────────────────────────┘
```

## 构建配置详解

### 1. 根级 build.gradle
```gradle
// android/build.gradle
buildscript {
    ext {
        compileSdkVersion = 34
        targetSdkVersion = 34
        minSdkVersion = 21
        buildToolsVersion = "34.0.0"
        ndkVersion = "27.0.11718014"

        // 版本管理
        filamentVersion = getFilamentVersion()
        kotlinVersion = "1.9.10"
        gradleVersion = "8.1.1"
    }

    dependencies {
        classpath "com.android.tools.build:gradle:${gradleVersion}"
        classpath "org.jetbrains.kotlin:kotlin-gradle-plugin:${kotlinVersion}"
        classpath "digital.wup:android-maven-publish:3.6.3"
    }
}

// 版本号自动生成
def getFilamentVersion() {
    def gitCommit = 'git rev-parse --short HEAD'.execute().text.trim()
    def gitTag = 'git describe --tags --abbrev=0'.execute().text.trim()

    if (gitTag.isEmpty()) {
        return "1.0.0-${gitCommit}"
    } else {
        return "${gitTag}-${gitCommit}"
    }
}

allprojects {
    repositories {
        google()
        mavenCentral()
        mavenLocal()
    }
}
```

### 2. 核心模块 build.gradle
```gradle
// android/filament-android/build.gradle
plugins {
    id 'com.android.library'
    id 'kotlin-android'
    id 'digital.wup.android-maven-publish'
}

android {
    namespace 'com.google.android.filament'
    compileSdk rootProject.ext.compileSdkVersion
    buildToolsVersion rootProject.ext.buildToolsVersion
    ndkVersion rootProject.ext.ndkVersion

    defaultConfig {
        minSdk rootProject.ext.minSdkVersion
        targetSdk rootProject.ext.targetSdkVersion

        versionCode getVersionCode()
        versionName rootProject.ext.filamentVersion

        testInstrumentationRunner "androidx.test.runner.AndroidJUnitRunner"

        externalNativeBuild {
            cmake {
                cppFlags "-std=c++17", "-fno-rtti", "-fno-exceptions"
                arguments "-DANDROID_STL=c++_shared",
                         "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON",
                         "-DFILAMENT_ANDROID_CI_BUILD=ON"
                targets "filament-jni"
            }
        }

        ndk {
            abiFilters "arm64-v8a", "armeabi-v7a", "x86_64", "x86"
        }
    }

    buildTypes {
        debug {
            debuggable true
            jniDebuggable true
            minifyEnabled false
            externalNativeBuild {
                cmake {
                    arguments "-DCMAKE_BUILD_TYPE=Debug"
                }
            }
        }

        release {
            minifyEnabled false
            proguardFiles getDefaultProguardFile('proguard-android-optimize.txt'),
                         'proguard-rules.pro'
            externalNativeBuild {
                cmake {
                    arguments "-DCMAKE_BUILD_TYPE=Release"
                }
            }
        }
    }

    externalNativeBuild {
        cmake {
            path "CMakeLists.txt"
            version "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility JavaVersion.VERSION_1_8
        targetCompatibility JavaVersion.VERSION_1_8
    }

    kotlinOptions {
        jvmTarget = '1.8'
    }

    packagingOptions {
        pickFirst '**/libc++_shared.so'
        pickFirst '**/libjsc.so'
    }

    publishing {
        singleVariant("release") {
            withSourcesJar()
            withJavadocJar()
        }
    }
}

dependencies {
    implementation "androidx.annotation:annotation:1.7.0"

    testImplementation 'junit:junit:4.13.2'
    androidTestImplementation 'androidx.test.ext:junit:1.1.5'
    androidTestImplementation 'androidx.test.espresso:espresso-core:3.5.1'
}

// 版本码生成
def getVersionCode() {
    def gitCommitCount = 'git rev-list --count HEAD'.execute().text.trim().toInteger()
    return gitCommitCount
}
```

### 3. CMake 构建配置
```cmake
# android/filament-android/CMakeLists.txt
cmake_minimum_required(VERSION 3.19)
project(filament-android)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 Filament 库
set(FILAMENT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../out/android-release/filament)

find_library(filament-lib filament PATHS ${FILAMENT_DIR}/lib/${ANDROID_ABI} NO_CMAKE_FIND_ROOT_PATH)
find_library(backend-lib backend PATHS ${FILAMENT_DIR}/lib/${ANDROID_ABI} NO_CMAKE_FIND_ROOT_PATH)
find_library(utils-lib utils PATHS ${FILAMENT_DIR}/lib/${ANDROID_ABI} NO_CMAKE_FIND_ROOT_PATH)
find_library(math-lib math PATHS ${FILAMENT_DIR}/lib/${ANDROID_ABI} NO_CMAKE_FIND_ROOT_PATH)

# 检查库是否找到
if(NOT filament-lib)
    message(FATAL_ERROR "filament library not found. Please build Filament first with: ./build.sh -p android release")
endif()

# 定义 JNI 库
add_library(filament-jni SHARED
    src/main/cpp/Camera.cpp
    src/main/cpp/ColorGrading.cpp
    src/main/cpp/Engine.cpp
    src/main/cpp/EntityManager.cpp
    src/main/cpp/Fence.cpp
    src/main/cpp/Filament.cpp
    src/main/cpp/IndexBuffer.cpp
    src/main/cpp/IndirectLight.cpp
    src/main/cpp/LightManager.cpp
    src/main/cpp/Material.cpp
    src/main/cpp/MaterialInstance.cpp
    src/main/cpp/RenderableManager.cpp
    src/main/cpp/Renderer.cpp
    src/main/cpp/Scene.cpp
    src/main/cpp/Skybox.cpp
    src/main/cpp/Stream.cpp
    src/main/cpp/SwapChain.cpp
    src/main/cpp/Texture.cpp
    src/main/cpp/TextureSampler.cpp
    src/main/cpp/TransformManager.cpp
    src/main/cpp/VertexBuffer.cpp
    src/main/cpp/View.cpp
    src/main/cpp/Viewport.cpp
)

# 设置包含目录
target_include_directories(filament-jni PRIVATE
    ${FILAMENT_DIR}/include
    src/main/cpp
)

# 链接库
target_link_libraries(filament-jni
    ${filament-lib}
    ${backend-lib}
    ${utils-lib}
    ${math-lib}
    android
    log
    EGL
    GLESv3
)

# 编译器选项
target_compile_options(filament-jni PRIVATE
    -fvisibility=hidden
    -ffunction-sections
    -fdata-sections
    -fno-omit-frame-pointer
)

# 链接器选项
target_link_options(filament-jni PRIVATE
    -Wl,--gc-sections
    -Wl,--strip-all
)
```

## 自动化构建脚本

### 1. 构建脚本
```bash
#!/bin/bash
# android/build-aar.sh

set -e

# 配置
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR="${SCRIPT_DIR}/.."
ANDROID_DIR="${SCRIPT_DIR}"
BUILD_TYPE="release"
CLEAN_BUILD=false
PUBLISH_LOCAL=false

# 解析参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--clean)
            CLEAN_BUILD=true
            shift
            ;;
        -d|--debug)
            BUILD_TYPE="debug"
            shift
            ;;
        -p|--publish-local)
            PUBLISH_LOCAL=true
            shift
            ;;
        *)
            echo "Unknown option $1"
            exit 1
            ;;
    esac
done

echo "Building Filament Android AAR..."
echo "Build type: ${BUILD_TYPE}"
echo "Clean build: ${CLEAN_BUILD}"

# 1. 构建原生库
echo "Step 1: Building native libraries..."
cd "${ROOT_DIR}"

if [ "${CLEAN_BUILD}" = true ]; then
    ./build.sh -c -p android ${BUILD_TYPE}
else
    ./build.sh -p android ${BUILD_TYPE}
fi

# 2. 复制原生库到 Android 项目
echo "Step 2: Copying native libraries..."
copy_native_libs() {
    local module_name=$1
    local src_dir="${ROOT_DIR}/out/android-${BUILD_TYPE}/filament"
    local dest_dir="${ANDROID_DIR}/${module_name}/src/main/jniLibs"

    mkdir -p "${dest_dir}"

    for abi in arm64-v8a armeabi-v7a x86_64 x86; do
        if [ -d "${src_dir}/lib/${abi}" ]; then
            mkdir -p "${dest_dir}/${abi}"

            # 复制相关库文件
            case ${module_name} in
                "filament-android")
                    cp "${src_dir}/lib/${abi}/libfilament.so" "${dest_dir}/${abi}/"
                    cp "${src_dir}/lib/${abi}/libbackend.so" "${dest_dir}/${abi}/"
                    cp "${src_dir}/lib/${abi}/libutils.so" "${dest_dir}/${abi}/"
                    cp "${src_dir}/lib/${abi}/libmath.so" "${dest_dir}/${abi}/"
                    ;;
                "filamat-android")
                    cp "${src_dir}/lib/${abi}/libfilamat.so" "${dest_dir}/${abi}/"
                    ;;
                "gltfio-android")
                    cp "${src_dir}/lib/${abi}/libgltfio.so" "${dest_dir}/${abi}/"
                    ;;
            esac
        fi
    done
}

copy_native_libs "filament-android"
copy_native_libs "filamat-android"
copy_native_libs "gltfio-android"

# 3. 构建 AAR
echo "Step 3: Building AAR packages..."
cd "${ANDROID_DIR}"

if [ "${CLEAN_BUILD}" = true ]; then
    ./gradlew clean
fi

./gradlew assemble${BUILD_TYPE^}

# 4. 运行测试
echo "Step 4: Running tests..."
./gradlew test${BUILD_TYPE^}UnitTest

# 5. 发布到本地仓库 (可选)
if [ "${PUBLISH_LOCAL}" = true ]; then
    echo "Step 5: Publishing to local Maven repository..."
    ./gradlew publishToMavenLocal
fi

echo "AAR build completed successfully!"
echo "Output files:"
find . -name "*.aar" -type f | head -10
```

### 2. 版本管理脚本
```bash
#!/bin/bash
# android/version-management.sh

# 版本号生成策略
generate_version() {
    local git_tag=$(git describe --tags --abbrev=0 2>/dev/null || echo "")
    local git_commit=$(git rev-parse --short HEAD)
    local git_count=$(git rev-list --count HEAD)
    local build_time=$(date +%Y%m%d%H%M)

    if [ -n "$git_tag" ]; then
        # 有标签: 使用标签版本
        echo "${git_tag}-${git_commit}"
    else
        # 无标签: 使用开发版本
        echo "1.0.0-dev.${git_count}.${git_commit}"
    fi
}

# 版本码生成 (用于 Android versionCode)
generate_version_code() {
    local git_count=$(git rev-list --count HEAD)
    echo $git_count
}

# 写入版本信息到配置文件
write_version_info() {
    local version=$(generate_version)
    local version_code=$(generate_version_code)

    cat > android/version.gradle << EOF
ext {
    filamentVersion = "${version}"
    filamentVersionCode = ${version_code}
}
EOF

    echo "Version information written to android/version.gradle:"
    echo "  Version: ${version}"
    echo "  Version Code: ${version_code}"
}

# 创建发布标签
create_release_tag() {
    local version=$1
    if [ -z "$version" ]; then
        echo "Usage: create_release_tag <version>"
        exit 1
    fi

    # 检查是否有未提交的更改
    if ! git diff-index --quiet HEAD --; then
        echo "Error: Working directory not clean"
        exit 1
    fi

    # 创建标签
    git tag -a "v${version}" -m "Release version ${version}"
    echo "Created tag: v${version}"
    echo "Push with: git push origin v${version}"
}

case "$1" in
    "generate")
        generate_version
        ;;
    "write")
        write_version_info
        ;;
    "tag")
        create_release_tag "$2"
        ;;
    *)
        echo "Usage: $0 {generate|write|tag <version>}"
        exit 1
        ;;
esac
```

## Maven 发布配置

### 1. 发布插件配置
```gradle
// android/publish.gradle
apply plugin: 'digital.wup.android-maven-publish'

publishing {
    publications {
        maven(MavenPublication) {
            from components.release

            artifactId project.name
            groupId 'com.google.android.filament'
            version rootProject.ext.filamentVersion

            pom {
                name = project.name
                description = 'Filament is a real-time physically based rendering engine'
                url = 'https://github.com/google/filament'

                licenses {
                    license {
                        name = 'The Apache License, Version 2.0'
                        url = 'http://www.apache.org/licenses/LICENSE-2.0.txt'
                    }
                }

                developers {
                    developer {
                        id = 'google'
                        name = 'Google'
                        email = 'filament@google.com'
                    }
                }

                scm {
                    connection = 'scm:git:git://github.com/google/filament.git'
                    developerConnection = 'scm:git:ssh://github.com:google/filament.git'
                    url = 'https://github.com/google/filament/tree/main'
                }
            }
        }
    }

    repositories {
        maven {
            name = "local"
            url = uri("${rootProject.buildDir}/repo")
        }

        maven {
            name = "sonatype"
            url = uri("https://oss.sonatype.org/service/local/staging/deploy/maven2/")
            credentials {
                username = project.findProperty("sonatype.username") ?: ""
                password = project.findProperty("sonatype.password") ?: ""
            }
        }
    }
}

// 签名配置 (发布到 Maven Central 时需要)
if (project.hasProperty("signing.keyId")) {
    apply plugin: 'signing'

    signing {
        sign publishing.publications.maven
    }
}
```

### 2. 发布脚本
```bash
#!/bin/bash
# android/publish.sh

set -e

PUBLISH_TARGET="local"
SIGN_ARTIFACTS=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --target)
            PUBLISH_TARGET="$2"
            shift 2
            ;;
        --sign)
            SIGN_ARTIFACTS=true
            shift
            ;;
        *)
            echo "Unknown option $1"
            exit 1
            ;;
    esac
done

echo "Publishing Filament Android AAR..."
echo "Target: ${PUBLISH_TARGET}"
echo "Sign artifacts: ${SIGN_ARTIFACTS}"

# 构建发布版本
./gradlew clean assembleRelease

# 运行测试
./gradlew testReleaseUnitTest

# 根据目标选择发布命令
case ${PUBLISH_TARGET} in
    "local")
        ./gradlew publishToMavenLocal
        echo "Published to local Maven repository (~/.m2/repository)"
        ;;
    "sonatype")
        if [ "${SIGN_ARTIFACTS}" = true ]; then
            ./gradlew publishMavenPublicationToSonatypeRepository
        else
            echo "Error: Sonatype publishing requires artifact signing"
            exit 1
        fi
        echo "Published to Sonatype repository"
        ;;
    *)
        echo "Unknown publish target: ${PUBLISH_TARGET}"
        exit 1
        ;;
esac
```

## 质量保证

### 1. ProGuard 配置
```pro
# android/filament-android/proguard-rules.pro

# 保留 JNI 方法
-keepclasseswithmembernames class * {
    native <methods>;
}

# 保留 Filament 公共 API
-keep class com.google.android.filament.** {
    public *;
}

# 保留枚举
-keepclassmembers enum * {
    public static **[] values();
    public static ** valueOf(java.lang.String);
}

# 保留 Parcelable 实现
-keep class * implements android.os.Parcelable {
    public static final android.os.Parcelable$Creator *;
}

# 避免警告
-dontwarn java.awt.**
-dontwarn javax.swing.**
-dontwarn sun.misc.Unsafe
```

### 2. 单元测试
```java
// android/filament-android/src/test/java/com/google/android/filament/EngineTest.java
package com.google.android.filament;

import org.junit.Test;
import org.junit.Before;
import org.junit.After;
import static org.junit.Assert.*;

public class EngineTest {
    private Engine mEngine;

    @Before
    public void setUp() {
        // 在测试环境中，使用软件渲染后端
        mEngine = Engine.create(Engine.Backend.NOOP);
    }

    @After
    public void tearDown() {
        if (mEngine != null) {
            mEngine.destroy();
        }
    }

    @Test
    public void testEngineCreation() {
        assertNotNull("Engine should be created", mEngine);
    }

    @Test
    public void testSceneCreation() {
        Scene scene = mEngine.createScene();
        assertNotNull("Scene should be created", scene);

        mEngine.destroyScene(scene);
    }

    @Test
    public void testRendererCreation() {
        Renderer renderer = mEngine.createRenderer();
        assertNotNull("Renderer should be created", renderer);

        mEngine.destroyRenderer(renderer);
    }
}
```

### 3. 集成测试
```java
// android/filament-android/src/androidTest/java/com/google/android/filament/FilamentIntegrationTest.java
package com.google.android.filament;

import android.content.Context;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public class FilamentIntegrationTest {

    @Test
    public void testFullRenderingPipeline() {
        Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();

        // 创建引擎
        Engine engine = Engine.create();
        assertNotNull(engine);

        // 创建渲染组件
        Renderer renderer = engine.createRenderer();
        Scene scene = engine.createScene();
        View view = engine.createView();
        Camera camera = engine.createCamera(engine.getEntityManager().create());

        // 设置视图
        view.setScene(scene);
        view.setCamera(camera);

        // 创建简单几何体
        createTriangleGeometry(engine, scene);

        // 模拟渲染循环
        for (int i = 0; i < 10; i++) {
            // 这里在实际设备上会渲染到 Surface
            if (renderer.beginFrame(null, 0)) {
                renderer.render(view);
                renderer.endFrame();
            }
        }

        // 清理资源
        engine.destroyRenderer(renderer);
        engine.destroyScene(scene);
        engine.destroyView(view);
        engine.destroyCamera(camera);
        engine.destroy();
    }

    private void createTriangleGeometry(Engine engine, Scene scene) {
        // 创建三角形几何体的代码
        // ... (类似示例应用中的实现)
    }
}
```

## 使用指南

### 1. 添加依赖
```gradle
// app/build.gradle
dependencies {
    // 核心 Filament 引擎 (必需)
    implementation 'com.google.android.filament:filament-android:1.17.1'

    // 可选模块
    implementation 'com.google.android.filament:filamat-android:1.17.1'   // 材质编译
    implementation 'com.google.android.filament:gltfio-android:1.17.1'    // glTF 支持
    implementation 'com.google.android.filament:filament-utils-android:1.17.1' // 工具库
}
```

### 2. 初始化配置
```java
public class FilamentApplication extends Application {
    @Override
    public void onCreate() {
        super.onCreate();

        // 加载 Filament 原生库
        Filament.init();
    }
}
```

### 3. 版本兼容性检查
```java
public class FilamentUtils {
    public static boolean isFilamentSupported() {
        try {
            // 尝试创建引擎来测试兼容性
            Engine engine = Engine.create();
            if (engine != null) {
                engine.destroy();
                return true;
            }
        } catch (Exception e) {
            Log.e("Filament", "Filament not supported", e);
        }
        return false;
    }

    public static String getFilamentVersion() {
        return Engine.getFilamentVersion();
    }

    public static Engine.Backend[] getSupportedBackends() {
        List<Engine.Backend> backends = new ArrayList<>();

        // 检查各个后端支持情况
        if (Engine.isBackendSupported(Engine.Backend.OPENGL)) {
            backends.add(Engine.Backend.OPENGL);
        }
        if (Engine.isBackendSupported(Engine.Backend.VULKAN)) {
            backends.add(Engine.Backend.VULKAN);
        }

        return backends.toArray(new Engine.Backend[0]);
    }
}
```

## 持续集成

### 1. GitHub Actions 配置
```yaml
# .github/workflows/android-build.yml
name: Android Build

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main ]

jobs:
  build:
    runs-on: ubuntu-latest

    steps:
    - uses: actions/checkout@v3
      with:
        submodules: recursive

    - name: Set up JDK 17
      uses: actions/setup-java@v3
      with:
        java-version: '17'
        distribution: 'temurin'

    - name: Setup Android SDK
      uses: android-actions/setup-android@v2
      with:
        api-level: 34
        ndk-version: '27.0.11718014'

    - name: Cache Gradle dependencies
      uses: actions/cache@v3
      with:
        path: |
          ~/.gradle/caches
          ~/.gradle/wrapper
        key: ${{ runner.os }}-gradle-${{ hashFiles('**/*.gradle*', '**/gradle-wrapper.properties') }}
        restore-keys: |
          ${{ runner.os }}-gradle-

    - name: Build native libraries
      run: ./build.sh -p android release

    - name: Build AAR
      run: |
        cd android
        ./gradlew assembleRelease

    - name: Run tests
      run: |
        cd android
        ./gradlew testReleaseUnitTest

    - name: Upload artifacts
      uses: actions/upload-artifact@v3
      with:
        name: filament-android-aar
        path: android/*/build/outputs/aar/*.aar
```

### 2. 自动发布流程
```yaml
# .github/workflows/android-release.yml
name: Android Release

on:
  push:
    tags:
      - 'v*'

jobs:
  release:
    runs-on: ubuntu-latest

    steps:
    - uses: actions/checkout@v3
      with:
        submodules: recursive

    - name: Extract version
      id: get_version
      run: echo "VERSION=${GITHUB_REF#refs/tags/v}" >> $GITHUB_OUTPUT

    - name: Setup build environment
      # ... (与构建流程相同)

    - name: Build and test
      run: |
        ./build.sh -p android release
        cd android
        ./gradlew assembleRelease testReleaseUnitTest

    - name: Sign artifacts
      env:
        SIGNING_KEY: ${{ secrets.SIGNING_KEY }}
        SIGNING_PASSWORD: ${{ secrets.SIGNING_PASSWORD }}
      run: |
        echo "$SIGNING_KEY" | base64 -d > signing-key.gpg
        # 配置签名...

    - name: Publish to Maven Central
      env:
        SONATYPE_USERNAME: ${{ secrets.SONATYPE_USERNAME }}
        SONATYPE_PASSWORD: ${{ secrets.SONATYPE_PASSWORD }}
      run: |
        cd android
        ./gradlew publishMavenPublicationToSonatypeRepository

    - name: Create GitHub Release
      uses: actions/create-release@v1
      env:
        GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
      with:
        tag_name: ${{ github.ref }}
        release_name: Release ${{ steps.get_version.outputs.VERSION }}
        body: |
          Filament Android ${{ steps.get_version.outputs.VERSION }}

          Download the AAR files from the artifacts below.
        draft: false
        prerelease: false
```

## 总结

Filament Android AAR 打包与发布系统提供了：

1. **模块化架构** - 灵活的依赖管理
2. **自动化构建** - 完整的 CI/CD 流程
3. **版本管理** - 语义化版本控制
4. **质量保证** - 全面的测试覆盖
5. **发布流程** - 标准化的发布流程

这套系统确保了 Filament Android 库的高质量交付和用户友好的集成体验。开发者可以根据项目需求选择合适的模块，实现最优的包大小和功能覆盖。