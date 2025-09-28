# C++ 编译系统深度解析

## 1. CMake 现代构建系统架构

### 1.1 目标导向设计 (Target-based Design)

Filament 采用现代 CMake 的目标导向设计模式，每个编译单元都被抽象为一个目标 (Target)。

```cmake
# 传统 CMake (避免使用)
set(CMAKE_CXX_STANDARD 20)
include_directories(include/)
add_definitions(-DFILAMENT_ENGINE)

# 现代 CMake (Filament 采用)
add_library(filament STATIC ${FILAMENT_SRCS})
target_compile_features(filament PUBLIC cxx_std_20)
target_include_directories(filament
    PUBLIC include/
    PRIVATE src/
)
target_compile_definitions(filament
    PUBLIC FILAMENT_API_EXPORT
    PRIVATE FILAMENT_ENGINE_INTERNAL
)
```

**关键概念**:
- **PUBLIC**: 传播给依赖该目标的其他目标
- **PRIVATE**: 仅用于编译该目标本身
- **INTERFACE**: 仅传播给依赖目标，不用于自身编译

### 1.2 依赖图管理

```cmake
# Filament 核心依赖层次
target_link_libraries(filament
    PUBLIC
        backend         # 公共 API 依赖
        filabridge      # 桥接库
        utils           # 基础工具
        math            # 数学库
    PRIVATE
        # 内部实现依赖
)

target_link_libraries(backend
    PUBLIC
        utils
    PRIVATE
        bluegl          # OpenGL 绑定 (条件依赖)
        bluevk          # Vulkan 绑定 (条件依赖)
)
```

**依赖传递规则**:
```cmake
# A -> B (PUBLIC) -> C
# 结果: A 可以直接使用 C 的头文件和库

# A -> B (PRIVATE) -> C
# 结果: A 只能使用 B，无法直接访问 C
```

### 1.3 Generator Expressions 深度应用

```cmake
# 配置相关的编译选项
target_compile_options(filament PRIVATE
    $<$<CONFIG:Debug>:-g3 -O0>
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
    $<$<CONFIG:RelWithDebInfo>:-O2 -g>
)

# 平台相关的编译选项
target_compile_definitions(filament PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Darwin>:FILAMENT_SUPPORTS_METAL>
    $<$<PLATFORM_ID:Linux>:FILAMENT_SUPPORTS_X11>
)

# 编译器相关选项
target_compile_options(filament PRIVATE
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -fcolor-diagnostics>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /MP>
)

# 复杂的条件表达式
target_sources(backend PRIVATE
    $<$<BOOL:${FILAMENT_SUPPORTS_OPENGL}>:src/opengl/OpenGLDriver.cpp>
    $<$<BOOL:${FILAMENT_SUPPORTS_VULKAN}>:src/vulkan/VulkanDriver.cpp>
    $<$<BOOL:${FILAMENT_SUPPORTS_METAL}>:src/metal/MetalDriver.mm>
)
```

## 2. 交叉编译工具链机制

### 2.1 工具导入导出机制

Filament 的交叉编译核心是工具导入导出机制，确保在编译目标平台代码时能使用宿主平台的工具。

```cmake
# 导出工具 (宿主平台构建时)
if (NOT CMAKE_CROSSCOMPILING)
    export(TARGETS
        matc            # 材质编译器
        cmgen           # 环境贴图生成器
        filamesh        # 网格转换器
        mipgen          # Mipmap 生成器
        resgen          # 资源打包器
        uberz           # 压缩工具
        glslminifier    # GLSL 压缩器
        FILE ${IMPORT_EXECUTABLES}
    )
endif()

# 导入工具 (目标平台构建时)
if (CMAKE_CROSSCOMPILING)
    include(${IMPORT_EXECUTABLES})
endif()
```

**ImportExecutables 文件结构**:
```cmake
# ImportExecutables-Release.cmake (自动生成)
add_executable(matc IMPORTED)
set_target_properties(matc PROPERTIES
    IMPORTED_LOCATION "/path/to/host/build/tools/matc/matc"
)

add_executable(cmgen IMPORTED)
set_target_properties(cmgen PROPERTIES
    IMPORTED_LOCATION "/path/to/host/build/tools/cmgen/cmgen"
)
```

### 2.2 工具链文件架构

**Android 工具链示例** (`build/toolchain-aarch64-linux-android.cmake`):
```cmake
# 系统标识
set(CMAKE_SYSTEM_NAME Linux)       # 告诉 CMake 这是 Linux 系统
set(CMAKE_SYSTEM_VERSION 1)

# Android 特定配置
set(API_LEVEL 21)                  # 最低 API 级别
set(ARCH aarch64-linux-android)    # 目标架构
set(DIST_ARCH arm64-v8a)          # 分发架构名称

# NDK 路径检测
string(TOLOWER ${CMAKE_HOST_SYSTEM_NAME} HOST_NAME_L)
file(TO_CMAKE_PATH $ENV{ANDROID_HOME} ANDROID_HOME_UNIX)

# NDK 版本自动选择
file(GLOB NDK_VERSIONS LIST_DIRECTORIES true
     ${ANDROID_HOME_UNIX}/ndk/${FILAMENT_NDK_VERSION}*)
list(SORT NDK_VERSIONS)
list(GET NDK_VERSIONS -1 NDK_VERSION)
set(TOOLCHAIN ${ANDROID_HOME_UNIX}/ndk/${NDK_VERSION}/toolchains/llvm/prebuilt/${HOST_NAME_L}-x86_64)

# 编译器设置
set(CMAKE_C_COMPILER   ${TOOLCHAIN}/bin/${ARCH}${API_LEVEL}-clang)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN}/bin/${ARCH}${API_LEVEL}-clang++)
set(CMAKE_AR           ${TOOLCHAIN}/bin/llvm-ar)
set(CMAKE_RANLIB       ${TOOLCHAIN}/bin/llvm-ranlib)
set(CMAKE_STRIP        ${TOOLCHAIN}/bin/llvm-strip)

# 查找路径配置
set(CMAKE_FIND_ROOT_PATH ${TOOLCHAIN}/sysroot)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)   # 程序从宿主查找
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)    # 库从目标查找
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)    # 头文件从目标查找

# 编译和链接标志
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIE -mcpu=cortex-a57")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_C_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fPIE -pie -static-libstdc++")
```

### 2.3 条件编译配置

```cmake
# 平台检测逻辑
if (UNIX AND NOT APPLE AND NOT ANDROID AND NOT WEBGL)
    set(LINUX TRUE)
else()
    set(LINUX FALSE)
endif()

if (ANDROID OR WEBGL OR IOS OR FILAMENT_LINUX_IS_MOBILE)
    set(IS_MOBILE_TARGET TRUE)
endif()

if (NOT ANDROID AND NOT WEBGL AND NOT IOS)
    set(IS_HOST_PLATFORM TRUE)
endif()

# 基于平台的功能启用
if (IS_HOST_PLATFORM)
    # 仅宿主平台构建的组件
    add_subdirectory(${LIBRARIES}/bluegl)        # OpenGL 加载器
    add_subdirectory(${LIBRARIES}/filamentapp)   # SDL2 应用框架
    add_subdirectory(${LIBRARIES}/imageio)       # 图像 I/O
    add_subdirectory(${TOOLS}/cmgen)             # 工具集
    add_subdirectory(${TOOLS}/matc)
endif()

if (IS_MOBILE_TARGET)
    # 移动平台特定优化
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fno-exceptions -fno-rtti")
    set(MATC_TARGET mobile)
else()
    set(MATC_TARGET desktop)
endif()
```

## 3. C++20 编译技术深度

### 3.1 编译器检测和选择策略

```cmake
# 编译器版本要求
set(MIN_CLANG_VERSION "16.0")

# 严格的编译器检查
if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    if (CMAKE_C_COMPILER_VERSION VERSION_LESS MIN_CLANG_VERSION)
        message(FATAL_ERROR
            "Detected C compiler Clang ${CMAKE_C_COMPILER_VERSION} < ${MIN_CLANG_VERSION}")
    endif()
elseif (NOT MSVC)
    message(FATAL_ERROR
        "Detected C compiler ${CMAKE_C_COMPILER_ID} is unsupported")
endif()

# Clang-cl 前端检测和拒绝
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    if ("${CMAKE_CXX_SIMULATE_ID}" STREQUAL "MSVC")
        message(FATAL_ERROR
            "Building with Clang on Windows is no longer supported. Use MSVC 2019 instead.")
    endif()
endif()
```

### 3.2 C++20 标准配置

```cmake
# C++20 标准设置
set(CXX_STANDARD "-std=c++20")
if (WIN32)
    set(CXX_STANDARD "/std:c++20")
endif()

if (MSVC)
    # MSVC 使用最新标准
    set(CXX_STANDARD "/std:c++latest")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_STANDARD} /W0 /Zc:__cplusplus")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_STANDARD} -fstrict-aliasing")
endif()

# C++20 特性验证
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-fconcepts" HAS_CONCEPTS)
if (NOT HAS_CONCEPTS)
    message(WARNING "Compiler does not support concepts")
endif()
```

**Filament 中使用的 C++20 特性**:
```cpp
// Concepts 示例
template<typename T>
concept Handle = requires(T t) {
    typename T::Tag;
    { t.getId() } -> std::convertible_to<uint32_t>;
};

template<Handle T>
void processHandle(T handle) {
    uint32_t id = handle.getId();
    // ...
}

// 指定初始化器
struct MaterialParams {
    float roughness = 0.5f;
    float3 baseColor = {1.0f, 1.0f, 1.0f};
    bool doubleSided = false;
};

MaterialParams params {
    .roughness = 0.8f,
    .baseColor = {0.5f, 0.2f, 0.1f}
};

// 三路比较
struct Entity {
    uint32_t id;
    auto operator<=>(const Entity&) const = default;
};
```

### 3.3 编译标志优化策略

#### Debug 配置优化
```cmake
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(TNT_DEV true)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTNT_DEV")

    # 调试符号优化
    if (ANDROID)
        # Android 保留完整调试信息
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-limit-debug-info")
    endif()

    if (WIN32)
        # Windows 内嵌调试信息 (避免 PDB 问题)
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Z7")
    endif()

    # 栈保护
    if (NOT MSVC AND NOT WEBGL)
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fstack-protector")
    endif()

    # 快速编译选项
    if (MSVC AND FILAMENT_SHORTEN_MSVC_COMPILATION)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")  # 多进程编译
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /D_ITERATOR_DEBUG_LEVEL=0")
    endif()
endif()
```

#### Release 配置优化
```cmake
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    # 性能优化
    if (NOT MSVC AND NOT IOS)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fomit-frame-pointer")
    endif()

    # 死代码消除
    if (NOT MSVC)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -ffunction-sections -fdata-sections")
    endif()

    # 移动平台大小优化
    if (ANDROID OR IOS OR WEBGL)
        # 异常和 RTTI 禁用 (节省约 85KB)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fno-exceptions -fno-rtti")
        set(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS ON)

        if (ANDROID OR WEBGL)
            # 展开表禁用 (节省约 50KB)
            set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fno-unwind-tables -fno-asynchronous-unwind-tables")
        endif()
    endif()
endif()
```

## 4. 链接时优化 (LTO/IPO)

### 4.1 LTO 配置和检测

```cmake
if (FILAMENT_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_SUPPORT OUTPUT error)

    if (IPO_SUPPORT)
        message(STATUS "LTO support is enabled")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

        # 为特定目标启用 LTO
        set_target_properties(filament PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION TRUE
        )
        set_target_properties(backend PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION TRUE
        )
    else()
        message(WARNING "LTO requested but not supported: ${error}")
    endif()
endif()
```

### 4.2 链接器优化配置

```cmake
# 链接器死代码消除
if (NOT WEBGL)
    set(GC_SECTIONS "-Wl,--gc-sections")
endif()

# 栈不可执行保护 (Linux)
if (LINUX)
    set(NO_EXEC_STACK "-Wl,-z,noexecstack")
endif()

# 符号绑定优化
set(B_SYMBOLIC_FUNCTIONS "-Wl,-Bsymbolic-functions")

# Android 内存对齐优化
if (ANDROID)
    set(BINARY_ALIGNMENT "-Wl,-z,max-page-size=16384")
endif()

# macOS 链接器配置
if (APPLE)
    set(GC_SECTIONS "-Wl,-dead_strip")
    set(B_SYMBOLIC_FUNCTIONS "")  # macOS 不需要

    # ranlib 配置
    set(CMAKE_C_ARCHIVE_FINISH   "<CMAKE_RANLIB> -no_warning_for_no_symbols <TARGET>")
    set(CMAKE_CXX_ARCHIVE_FINISH "<CMAKE_RANLIB> -no_warning_for_no_symbols <TARGET>")
endif()

# 应用链接器标志
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${GC_SECTIONS} ${NO_EXEC_STACK}")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${GC_SECTIONS} ${B_SYMBOLIC_FUNCTIONS} ${BINARY_ALIGNMENT}")
```

## 5. 静态库合并技术

### 5.1 combine_static_libs 函数

```cmake
# 静态库合并函数 (移动平台优化)
function(combine_static_libs TARGET OUTPUT DEPS)
    # 收集依赖库文件路径
    set(DEPS_FILES)
    foreach(DEPENDENCY ${DEPS})
        if (TARGET ${DEPENDENCY})
            get_property(dep_type TARGET ${DEPENDENCY} PROPERTY TYPE)
            if (dep_type STREQUAL "STATIC_LIBRARY")
                list(APPEND DEPS_FILES "$<TARGET_FILE:${DEPENDENCY}>")
            endif()
        endif()
    endforeach()

    if (WIN32)
        # Windows lib.exe 工具
        add_custom_command(
            TARGET ${TARGET} POST_BUILD
            COMMAND lib.exe /nologo /out:${OUTPUT} ${DEPS_FILES}
            COMMENT "Combining static libraries for ${TARGET}"
            VERBATIM
        )
    else()
        # Unix ar 工具
        add_custom_command(
            TARGET ${TARGET} POST_BUILD
            COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/build/linux/combine-static-libs.sh"
                    "${CMAKE_AR}" "${OUTPUT}" ${DEPS_FILES}
            COMMENT "Combining static libraries for ${TARGET}"
            VERBATIM
        )
    endif()
endfunction()

# 使用示例 (Android/iOS)
if (ANDROID OR IOS)
    combine_static_libs(filament-combined
        ${CMAKE_CURRENT_BINARY_DIR}/libfilament-combined.a
        filament backend utils math filabridge
    )
endif()
```

### 5.2 combine-static-libs.sh 脚本

```bash
#!/bin/bash
# build/linux/combine-static-libs.sh

AR_TOOL=$1
OUTPUT_LIB=$2
shift 2
INPUT_LIBS=$@

# 创建临时目录
TEMP_DIR=$(mktemp -d)
EXTRACT_DIR="${TEMP_DIR}/extract"
mkdir -p "${EXTRACT_DIR}"

# 提取所有静态库的对象文件
for lib in ${INPUT_LIBS}; do
    if [[ -f "${lib}" ]]; then
        lib_name=$(basename "${lib}" .a)
        lib_dir="${EXTRACT_DIR}/${lib_name}"
        mkdir -p "${lib_dir}"

        pushd "${lib_dir}" > /dev/null
        ${AR_TOOL} x "${lib}"
        popd > /dev/null
    fi
done

# 合并所有对象文件
pushd "${EXTRACT_DIR}" > /dev/null
find . -name "*.o" -exec ${AR_TOOL} rcs "${OUTPUT_LIB}" {} +
popd > /dev/null

# 清理临时文件
rm -rf "${TEMP_DIR}"
```

## 6. ccache 编译加速

### 6.1 ccache 自动检测和配置

```cmake
find_program(CCACHE_PROGRAM ccache)
if (CCACHE_PROGRAM)
    message(STATUS "Found ccache: ${CCACHE_PROGRAM}")

    if (WIN32)
        # Windows 直接设置
        set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    else()
        # Unix 使用包装脚本
        set(C_LAUNCHER   "${CCACHE_PROGRAM}")
        set(CXX_LAUNCHER "${CCACHE_PROGRAM}")

        # 生成包装脚本
        configure_file(build/launch-c.in   launch-c)
        configure_file(build/launch-cxx.in launch-cxx)

        execute_process(COMMAND chmod a+rx
            "${CMAKE_CURRENT_BINARY_DIR}/launch-c"
            "${CMAKE_CURRENT_BINARY_DIR}/launch-cxx"
        )

        if (CMAKE_GENERATOR STREQUAL "Xcode")
            # Xcode 特殊配置
            set(CMAKE_XCODE_ATTRIBUTE_CC         "${CMAKE_CURRENT_BINARY_DIR}/launch-c")
            set(CMAKE_XCODE_ATTRIBUTE_CXX        "${CMAKE_CURRENT_BINARY_DIR}/launch-cxx")
        else()
            set(CMAKE_C_COMPILER_LAUNCHER        "${CMAKE_CURRENT_BINARY_DIR}/launch-c")
            set(CMAKE_CXX_COMPILER_LAUNCHER      "${CMAKE_CURRENT_BINARY_DIR}/launch-cxx")
        endif()
    endif()
else()
    message(STATUS "ccache not found")
endif()
```

### 6.2 ccache 包装脚本

**build/launch-c.in**:
```bash
#!/bin/bash
export CCACHE_CPP2=true
export CCACHE_HASHDIR=true
exec "${C_LAUNCHER}" "$@"
```

**build/launch-cxx.in**:
```bash
#!/bin/bash
export CCACHE_CPP2=true
export CCACHE_HASHDIR=true
exec "${CXX_LAUNCHER}" "$@"
```

### 6.3 ccache 优化配置

```bash
# 设置缓存大小
ccache -M 10G

# 启用压缩
ccache -o compression=true
ccache -o compression_level=6

# 设置缓存目录
export CCACHE_DIR=~/.ccache

# 优化选项
export CCACHE_CPP2=true          # 预处理器缓存
export CCACHE_HASHDIR=true       # 目录路径哈希
export CCACHE_SLOPPINESS=time_macros,file_macro  # 放宽时间相关检查

# 统计信息
ccache -s
ccache -z  # 重置统计
```

## 7. 符号可见性控制

### 7.1 符号导出策略

```cmake
# 默认隐藏符号
if (NOT WIN32)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
endif()

# Windows DLL 导出
if (WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
```

**API 导出宏定义**:
```cpp
// include/filament/FilamentAPI.h
#ifdef _WIN32
    #ifdef FILAMENT_BUILD_DLL
        #define FILAMENT_API __declspec(dllexport)
    #else
        #define FILAMENT_API __declspec(dllimport)
    #endif
#else
    #define FILAMENT_API __attribute__((visibility("default")))
#endif

// 使用示例
class FILAMENT_API Engine {
public:
    static Engine* create();
    void destroy();
};
```

### 7.2 链接器脚本 (可选)

```ld
# filament.lds (链接器脚本示例)
FILAMENT_1.0 {
  global:
    # 导出的 C++ 符号
    _Z*Engine*;
    _Z*Renderer*;
    _Z*Material*;

  local:
    # 隐藏内部符号
    *;
};
```

## 8. 编译优化技巧总结

### 8.1 编译时间优化

**并行编译**:
```cmake
# Ninja 并行构建
set(CMAKE_GENERATOR "Ninja")

# MSVC 多进程编译
if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
endif()
```

**预编译头 (未来功能)**:
```cmake
# CMake 3.16+ 预编译头支持
target_precompile_headers(filament PRIVATE
    <vector>
    <memory>
    <string>
    "filament/Engine.h"
)
```

### 8.2 二进制大小优化

**移动平台优化**:
```cmake
if (IS_MOBILE_TARGET)
    # 大小优先
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Os")

    # 禁用不必要功能
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fno-exceptions -fno-rtti")

    # 字符串优化
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fno-asynchronous-unwind-tables")
endif()
```

**链接器优化**:
```bash
# 使用 gold 链接器 (Linux)
-fuse-ld=gold

# 使用 lld 链接器 (更快)
-fuse-ld=lld

# 链接时死代码消除
-Wl,--gc-sections
```

这个深度解析展示了 Filament 项目中 C++ 编译系统的核心技术，从现代 CMake 设计模式到具体的编译优化策略，为理解大型 C++ 项目的编译系统提供了完整的技术框架。