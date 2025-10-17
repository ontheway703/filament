# Android 编译系统深度剖析

## 1. NDK 交叉编译工具链架构

### 1.1 NDK 版本管理和 Side-by-Side 安装

Android NDK 采用 side-by-side 安装模式，允许同时安装多个版本：

```bash
# NDK 目录结构
$ANDROID_HOME/ndk/
├── 25.1.8937393/          # NDK r25c
├── 25.2.9519653/          # NDK r25d
└── 26.1.10909125/         # NDK r26b

# Filament 的 NDK 版本检测机制
file(GLOB NDK_VERSIONS LIST_DIRECTORIES true ${ANDROID_HOME_UNIX}/ndk/${FILAMENT_NDK_VERSION}*)
list(SORT NDK_VERSIONS)
list(GET NDK_VERSIONS -1 NDK_VERSION)  # 选择最新的匹配版本
```

**NDK 版本选择策略**:
```cmake
# build/toolchain-aarch64-linux-android.cmake

# 优先使用指定版本
if (NOT FILAMENT_NDK_VERSION)
    file(READ "${CMAKE_CURRENT_LIST_DIR}/common/versions" VERSIONS_STR)
    string(REGEX MATCH "GITHUB_NDK_VERSION=(\\d+)" _UNUSED ${VERSIONS_STR})
    if(CMAKE_MATCH_1)
        set(FILAMENT_NDK_VERSION "${CMAKE_MATCH_1}")
    endif()
endif()

message(STATUS "Using NDK '${FILAMENT_NDK_VERSION}'")
```

### 1.2 LLVM 工具链配置

现代 NDK 基于 LLVM/Clang 工具链，提供统一的编译体验：

```cmake
# 工具链路径构建
set(TOOLCHAIN ${ANDROID_HOME_UNIX}/ndk/${NDK_VERSION}/toolchains/llvm/prebuilt/${HOST_NAME_L}-x86_64)

# 编译器配置
set(COMPILER_SUFFIX)
set(TOOL_SUFFIX)
if(WIN32)
    set(COMPILER_SUFFIX ".cmd")  # Windows 批处理文件
    set(TOOL_SUFFIX     ".exe")  # Windows 可执行文件
endif()

# 目标三元组编译器
set(CMAKE_C_COMPILER   ${TOOLCHAIN}/bin/${ARCH}${API_LEVEL}-clang${COMPILER_SUFFIX})
set(CMAKE_CXX_COMPILER ${TOOLCHAIN}/bin/${ARCH}${API_LEVEL}-clang++${COMPILER_SUFFIX})

# LLVM 工具套件
set(CMAKE_AR           ${TOOLCHAIN}/bin/llvm-ar${TOOL_SUFFIX})
set(CMAKE_RANLIB       ${TOOLCHAIN}/bin/llvm-ranlib${TOOL_SUFFIX})
set(CMAKE_STRIP        ${TOOLCHAIN}/bin/llvm-strip${TOOL_SUFFIX})
set(CMAKE_OBJCOPY      ${TOOLCHAIN}/bin/llvm-objcopy${TOOL_SUFFIX})
set(CMAKE_OBJDUMP      ${TOOLCHAIN}/bin/llvm-objdump${TOOL_SUFFIX})
```

### 1.3 API Level 选择策略

```cmake
# API Level 配置策略
set(API_LEVEL 21)  # Android 5.0 (Lollipop) 最低支持

# API Level 对应关系：
# API 21: Android 5.0  - 支持 64位架构，OpenGL ES 3.0
# API 23: Android 6.0  - 运行时权限
# API 24: Android 7.0  - Vulkan API 支持
# API 26: Android 8.0  - 动态链接器变更
# API 29: Android 10   - 作用域存储
```

**API Level 影响的功能**:
```cpp
// 条件编译示例
#if __ANDROID_API__ >= 24
    // Vulkan 支持
    #include <vulkan/vulkan_android.h>
#endif

#if __ANDROID_API__ >= 26
    // Android 8.0+ 特性
    #include <android/hardware_buffer.h>
#endif
```

## 2. 多架构 ABI 支持

### 2.1 支持的 ABI 类型

```cmake
# Filament 支持的 Android ABI
# arm64-v8a    : 64位 ARM (主要目标)
# armeabi-v7a  : 32位 ARM (向后兼容)
# x86_64       : 64位 Intel (模拟器/Intel设备)
# x86          : 32位 Intel (老旧模拟器)

# build.sh 中的 ABI 构建逻辑
if [[ "${ABI_ARM64_V8A}" == "true" ]]; then
    build_android_arch "aarch64" "aarch64-linux-android"
fi
if [[ "${ABI_ARMEABI_V7A}" == "true" ]]; then
    build_android_arch "arm7" "arm-linux-androideabi"
fi
if [[ "${ABI_X86_64}" == "true" ]]; then
    build_android_arch "x86_64" "x86_64-linux-android"
fi
if [[ "${ABI_X86}" == "true" ]]; then
    build_android_arch "x86" "i686-linux-android"
fi
```

### 2.2 架构特定优化

**ARM64 优化**:
```cmake
# build/toolchain-aarch64-linux-android.cmake
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIE -mcpu=cortex-a57")

# 高级 ARM64 优化
if (ANDROID_ARM64_ADVANCED_OPTIMIZATION)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcpu=cortex-a76")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mtune=cortex-a76")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mfpu=crypto-neon-fp-armv8")
endif()
```

**ARM32 优化**:
```cmake
# build/toolchain-arm7-linux-android.cmake
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv7-a")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mfloat-abi=softfp")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mfpu=neon")

# ARM32 特定链接配置
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--fix-cortex-a8")
```

### 2.3 ABI 过滤和分发

```gradle
// android/filament-android/build.gradle
android {
    defaultConfig {
        ndk {
            // 根据需要过滤 ABI
            abiFilters 'arm64-v8a', 'armeabi-v7a'
        }
    }

    // 产品风味 ABI 分离
    flavorDimensions 'cpuArch'
    productFlavors {
        arm8 {
            dimension 'cpuArch'
            ndk {
                abiFilters 'arm64-v8a'
            }
        }
        arm7 {
            dimension 'cpuArch'
            ndk {
                abiFilters 'armeabi-v7a'
            }
        }
        x86_64 {
            dimension 'cpuArch'
            ndk {
                abiFilters 'x86_64'
            }
        }
        universal {
            dimension 'cpuArch'
            // 包含所有 ABI
        }
    }
}
```

## 3. CMake 和 Gradle 深度集成

### 3.1 ExternalNativeBuild 配置

```gradle
// android/filament-android/build.gradle
android {
    externalNativeBuild {
        cmake {
            version "3.22.1+"
            path "CMakeLists.txt"
        }
    }

    defaultConfig {
        externalNativeBuild {
            cmake {
                // CMake 参数传递
                arguments "-DFILAMENT_DIST_DIR=${project.property('com.google.android.filament.dist-dir')}"
                arguments "-DFILAMENT_SUPPORTS_VULKAN=${project.hasProperty('com.google.android.filament.vulkan')}"

                // C++ 标准设置
                cppFlags "-std=c++20", "-fexceptions", "-frtti"
                cFlags "-DANDROID_PLATFORM_LEVEL=${compileSdkVersion}"

                // ABI 配置
                abiFilters project.property('com.google.android.filament.abis').split(',')

                // 构建目标
                targets "filament-jni"
            }
        }
    }
}
```

### 3.2 JNI 接口生成

```cmake
# android/filament-android/CMakeLists.txt
cmake_minimum_required(VERSION 3.19)
project(filament-android)

# 导入预构建的 Filament 库
set(FILAMENT_DIR ${FILAMENT_DIST_DIR})

add_library(filament STATIC IMPORTED)
set_target_properties(filament PROPERTIES IMPORTED_LOCATION
    ${FILAMENT_DIR}/lib/${ANDROID_ABI}/libfilament.a)

add_library(backend STATIC IMPORTED)
set_target_properties(backend PROPERTIES IMPORTED_LOCATION
    ${FILAMENT_DIR}/lib/${ANDROID_ABI}/libbackend.a)

# JNI 接口库
add_library(filament-jni SHARED
    src/main/cpp/Engine.cpp
    src/main/cpp/Renderer.cpp
    src/main/cpp/Material.cpp
    src/main/cpp/CallbackUtils.cpp
)

target_link_libraries(filament-jni
    filament
    backend
    utils
    math
    android
    log
    EGL
    GLESv3
)
```

### 3.3 AAR 打包流程

```gradle
// AAR 构建和发布
task assembleAar(type: Copy) {
    dependsOn assembleRelease

    from 'build/outputs/aar/'
    into '../../../out/'
    include '**/*-release.aar'

    rename { filename ->
        filename.replace('-release', '')
    }
}

// 多 ABI AAR 生成
task assembleUniversalAar {
    dependsOn ':filament-android:assembleRelease'

    doLast {
        // 验证所有 ABI 都存在
        def requiredAbis = ['arm64-v8a', 'armeabi-v7a', 'x86_64', 'x86']
        def libDir = file("${project.property('com.google.android.filament.dist-dir')}/lib")

        requiredAbis.each { abi ->
            def abiDir = file("${libDir}/${abi}")
            if (!abiDir.exists()) {
                throw new GradleException("Missing ABI: ${abi}")
            }
        }
    }
}
```

## 4. Android 特定编译优化

### 4.1 二进制大小优化

```cmake
# Android 大小优化策略
if (ANDROID)
    # 启用大小优化
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Os")

    # 异常和 RTTI 禁用 (节省约 75KB + 10KB)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fno-exceptions -fno-rtti")

    # 展开表禁用 (节省约 50KB)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fno-unwind-tables -fno-asynchronous-unwind-tables")

    # 函数和数据段分离 (配合链接器 gc-sections)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -ffunction-sections -fdata-sections")

    # 死代码消除
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections")

    # 符号剥离
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--strip-all")
endif()
```

### 4.2 动态链接优化

```cmake
# Android 动态链接配置
if (ANDROID)
    # 位置无关代码 (PIC) - 必需
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

    # 位置无关可执行文件 (PIE) - Android 5.0+ 必需
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIE")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fPIE -pie")

    # 静态 C++ 库链接 (避免设备兼容性问题)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libstdc++")

    # 内存页对齐优化
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384")

    # 未定义符号弱引用 (兼容性)
    add_definitions(-D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__)
endif()
```

### 4.3 内存对齐策略

```cpp
// Android 内存对齐优化
namespace android {

// GPU 缓冲区对齐 (64字节边界)
struct alignas(64) GPUBuffer {
    uint8_t data[4096];
};

// CPU 缓存行对齐 (64字节)
struct alignas(64) CacheAlignedData {
    float matrix[16];
    float vector[4];
};

// Android 页面对齐 (4KB)
class PageAlignedAllocator {
public:
    void* allocate(size_t size) {
        size_t aligned_size = (size + 4095) & ~4095;  // 向上对齐到 4KB
        return aligned_alloc(4096, aligned_size);
    }
};

}  // namespace android
```

## 5. STL 选择和配置

### 5.1 libc++ 配置

现代 NDK 默认使用 libc++，提供完整的 C++20 支持：

```cmake
# NDK r18+ 默认使用 libc++
# 无需额外配置，工具链自动处理

# 验证 STL 配置
if (ANDROID)
    # 检查 C++20 特性支持
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("
        #include <concepts>
        template<typename T>
        concept Integral = std::integral<T>;
        int main() { return 0; }
    " HAS_CONCEPTS)

    if (NOT HAS_CONCEPTS)
        message(WARNING "Concepts not supported on this Android configuration")
    endif()
endif()
```

### 5.2 系统库链接策略

```cmake
# Android 系统库配置
if (ANDROID)
    find_library(log-lib log)
    find_library(android-lib android)

    # OpenGL ES 库
    find_library(EGL-lib EGL)
    find_library(GLESv3-lib GLESv3)

    # Vulkan 库 (API 24+)
    if (FILAMENT_SUPPORTS_VULKAN)
        find_library(vulkan-lib vulkan)
    endif()

    # 链接系统库
    target_link_libraries(filament-jni
        ${log-lib}
        ${android-lib}
        ${EGL-lib}
        ${GLESv3-lib}
        $<$<BOOL:${FILAMENT_SUPPORTS_VULKAN}>:${vulkan-lib}>
    )
endif()
```

### 5.3 JNI 错误处理

```cpp
// JNI 错误处理最佳实践
namespace jni {

class ExceptionHandler {
public:
    static bool checkAndClearException(JNIEnv* env) {
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();  // 打印异常堆栈
            env->ExceptionClear();     // 清除异常
            return true;
        }
        return false;
    }

    static void throwRuntimeException(JNIEnv* env, const char* message) {
        jclass exceptionClass = env->FindClass("java/lang/RuntimeException");
        if (exceptionClass != nullptr) {
            env->ThrowNew(exceptionClass, message);
        }
        env->DeleteLocalRef(exceptionClass);
    }
};

// RAII 本地引用管理
class LocalRefGuard {
private:
    JNIEnv* env_;
    jobject ref_;

public:
    LocalRefGuard(JNIEnv* env, jobject ref) : env_(env), ref_(ref) {}
    ~LocalRefGuard() {
        if (ref_) {
            env_->DeleteLocalRef(ref_);
        }
    }

    LocalRefGuard(const LocalRefGuard&) = delete;
    LocalRefGuard& operator=(const LocalRefGuard&) = delete;
};

}  // namespace jni
```

## 6. Gradle 优化技巧

### 6.1 增量构建优化

```gradle
// 增量构建配置
android {
    // 启用增量编译
    compileOptions {
        incremental true
    }

    // 缓存 NDK 构建
    externalNativeBuild {
        cmake {
            // 启用构建缓存
            arguments "-DCMAKE_BUILD_TYPE=RelWithDebInfo"  // 调试信息 + 优化
            arguments "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"  // 生成编译数据库
        }
    }

    // 并行构建
    gradle.projectsEvaluated {
        tasks.withType(JavaCompile) {
            options.compilerArgs << "-Xlint:unchecked" << "-Xlint:deprecation"
            options.fork = true
            options.forkOptions.jvmArgs = ['-Xmx2g']
        }
    }
}

// Gradle 缓存配置
def buildCacheDir = file("${project.rootDir}/.gradle/build-cache")
buildCache {
    local {
        directory = buildCacheDir
        removeUnusedEntriesAfterDays = 30
    }
}
```

### 6.2 依赖管理优化

```gradle
// 依赖解析优化
configurations.all {
    // 缓存动态版本 1 小时
    resolutionStrategy.cacheDynamicVersionsFor 1, 'hours'

    // 缓存变化模块 1 小时
    resolutionStrategy.cacheChangingModulesFor 1, 'hours'

    // 强制使用特定版本
    resolutionStrategy.force 'com.android.support:support-annotations:28.0.0'
}

// Maven 仓库优化
repositories {
    // 优先使用本地仓库
    mavenLocal()

    // Google 仓库
    google {
        content {
            includeGroupByRegex "com\\.android.*"
            includeGroupByRegex "com\\.google.*"
            includeGroupByRegex "androidx.*"
        }
    }

    // Maven Central
    mavenCentral()
}
```

### 6.3 多模块构建优化

```gradle
// 并行构建配置
org.gradle.parallel=true
org.gradle.workers.max=4
org.gradle.configureondemand=true

// 内存优化
org.gradle.jvmargs=-Xmx4g -XX:MaxMetaspaceSize=512m -XX:+HeapDumpOnOutOfMemoryError

// 构建缓存
org.gradle.caching=true

// Kotlin 编译优化
kotlin.incremental=true
kotlin.incremental.android=true
kotlin.caching.enabled=true
```

## 7. Perfetto 性能追踪集成

### 7.1 Perfetto SDK 配置

```cmake
# Perfetto 集成配置
if (FILAMENT_ENABLE_PERFETTO)
    add_definitions(-DFILAMENT_ENABLE_PERFETTO)
    add_subdirectory(${EXTERNAL}/perfetto/tnt)

    target_link_libraries(filament-jni
        perfetto
    )
endif()
```

### 7.2 性能追踪实现

```cpp
// Android Perfetto 集成
#ifdef FILAMENT_ENABLE_PERFETTO
#include <perfetto/perfetto.h>

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category("filament.rendering")
        .SetDescription("Filament rendering events"),
    perfetto::Category("filament.materials")
        .SetDescription("Material compilation and loading"),
    perfetto::Category("filament.memory")
        .SetDescription("Memory allocation events")
);

class AndroidProfiler {
public:
    static void initialize() {
        perfetto::TraceConfig cfg;
        cfg.add_buffers()->set_size_kb(1024);

        auto* ds_cfg = cfg.add_data_sources();
        ds_cfg->set_name("track_event");

        perfetto::Tracing::Initialize(cfg);
        perfetto::Tracing::EnableTracing(cfg);
    }

    static void shutdown() {
        perfetto::Tracing::Shutdown();
    }
};

// 使用宏进行性能追踪
#define FILAMENT_TRACE_EVENT(category, name) \
    TRACE_EVENT(category, name)

#define FILAMENT_TRACE_COUNTER(category, name, value) \
    TRACE_COUNTER(category, name, value)

#else

#define FILAMENT_TRACE_EVENT(category, name)
#define FILAMENT_TRACE_COUNTER(category, name, value)

#endif

// 渲染追踪示例
void Renderer::render(View* view) {
    FILAMENT_TRACE_EVENT("filament.rendering", "Renderer::render");

    FILAMENT_TRACE_COUNTER("filament.rendering", "DrawCalls", mDrawCallCount);
    FILAMENT_TRACE_COUNTER("filament.rendering", "Triangles", mTriangleCount);

    // 渲染逻辑...
}
```

### 7.3 内存分配追踪

```cpp
// 自定义内存分配器追踪
class TrackedAllocator {
private:
    std::atomic<size_t> allocated_bytes_{0};
    std::atomic<size_t> allocation_count_{0};

public:
    void* allocate(size_t size) {
        void* ptr = std::malloc(size);
        if (ptr) {
            allocated_bytes_ += size;
            allocation_count_++;

            FILAMENT_TRACE_COUNTER("filament.memory", "AllocatedBytes", allocated_bytes_.load());
            FILAMENT_TRACE_COUNTER("filament.memory", "AllocationCount", allocation_count_.load());
        }
        return ptr;
    }

    void deallocate(void* ptr, size_t size) {
        if (ptr) {
            std::free(ptr);
            allocated_bytes_ -= size;
            allocation_count_--;

            FILAMENT_TRACE_COUNTER("filament.memory", "AllocatedBytes", allocated_bytes_.load());
            FILAMENT_TRACE_COUNTER("filament.memory", "AllocationCount", allocation_count_.load());
        }
    }
};
```

## 8. 调试和问题诊断

### 8.1 NDK 调试配置

```gradle
android {
    buildTypes {
        debug {
            debuggable true
            jniDebuggable true
            renderscriptDebuggable true

            // 保留调试符号
            packagingOptions {
                doNotStrip "*/armeabi-v7a/*.so"
                doNotStrip "*/arm64-v8a/*.so"
                doNotStrip "*/x86/*.so"
                doNotStrip "*/x86_64/*.so"
            }
        }
    }
}
```

### 8.2 崩溃日志分析

```bash
# 使用 ndk-stack 分析崩溃
$ANDROID_HOME/ndk/25.2.9519653/ndk-stack -sym ./obj/local/arm64-v8a/ -dump crash.log

# 使用 addr2line 进行符号化
$ANDROID_HOME/ndk/25.2.9519653/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-addr2line \
    -f -C -e ./obj/local/arm64-v8a/libfilament.so 0x00000000000abcdef

# 使用 objdump 分析二进制
$ANDROID_HOME/ndk/25.2.9519653/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objdump \
    -d -C ./obj/local/arm64-v8a/libfilament.so
```

### 8.3 性能分析工具

```cpp
// GPU 性能测量 (Android)
class AndroidGPUProfiler {
private:
    EGLDisplay display_;
    std::vector<GLuint> queries_;

public:
    void beginFrame() {
        GLuint query;
        glGenQueries(1, &query);
        glBeginQuery(GL_TIME_ELAPSED, query);
        queries_.push_back(query);
    }

    void endFrame() {
        glEndQuery(GL_TIME_ELAPSED);
    }

    float getFrameTimeMS() {
        if (queries_.empty()) return 0.0f;

        GLuint query = queries_.back();
        queries_.pop_back();

        GLuint64 timeElapsed;
        glGetQueryObjectui64v(query, GL_QUERY_RESULT, &timeElapsed);
        glDeleteQueries(1, &query);

        return timeElapsed / 1000000.0f;  // 纳秒转毫秒
    }
};

// CPU 性能测量
#include <chrono>

class CPUProfiler {
private:
    std::chrono::high_resolution_clock::time_point start_time_;

public:
    void begin() {
        start_time_ = std::chrono::high_resolution_clock::now();
    }

    float endMS() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time_);
        return duration.count() / 1000.0f;
    }
};
```

## 总结

Android 编译系统的核心要点：

1. **NDK 工具链** - 基于 LLVM，支持多架构交叉编译
2. **ABI 管理** - 主要支持 ARM64，兼容 ARM32 和 x86
3. **构建集成** - CMake + Gradle 深度集成，支持 AAR 打包
4. **大小优化** - 异常禁用、死代码消除、符号剥离
5. **性能追踪** - Perfetto 集成，支持 GPU/CPU 性能分析
6. **调试支持** - 符号化崩溃、性能分析工具

理解这些技术细节对于在 Android 平台成功构建和优化大型 C++ 项目至关重要。