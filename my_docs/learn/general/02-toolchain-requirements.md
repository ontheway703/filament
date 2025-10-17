# Filament 工具链要求详解

## 1. 核心工具链要求

### 1.1 CMake 要求

**最低版本**: CMake 3.22.1

```cmake
cmake_minimum_required(VERSION 3.22.1)
```

**关键特性需求**:
- CMP0074 策略支持 (CMake 3.12+)
- 现代 C++20 支持
- 交叉编译工具链支持
- Generator expressions 支持

**安装验证**:
```bash
cmake --version
# 输出应该 >= 3.22.1
```

### 1.2 编译器要求

#### 支持的编译器

**主要编译器**: Clang 16.0+

```cmake
set(MIN_CLANG_VERSION "6.0")  # 实际推荐 16.0+

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS MIN_CLANG_VERSION)
        message(FATAL_ERROR "Detected CXX compiler Clang ${CMAKE_CXX_COMPILER_VERSION} < ${MIN_CLANG_VERSION}")
    endif()
elseif (NOT MSVC)
    message(FATAL_ERROR "Detected CXX compiler ${CMAKE_CXX_COMPILER_ID} is unsupported")
endif()
```

**平台特定编译器**:

| 平台 | 支持的编译器 | 最低版本 |
|------|-------------|----------|
| Linux | Clang | 16.0+ |
| macOS | Clang (Xcode) | 14.0+ |
| Windows | MSVC | 2019+ |
| Android | Clang (NDK) | NDK 25.1+ |
| iOS | Clang (Xcode) | 14.0+ |
| WebGL | Emscripten | 3.1.60+ |

#### C++20 标准要求

```cmake
set(CXX_STANDARD "-std=c++20")
if (WIN32)
    set(CXX_STANDARD "/std:c++20")
endif()

if (MSVC)
    set(CXX_STANDARD "/std:c++latest")
endif()
```

**使用的 C++20 特性**:
- Concepts
- Modules (部分)
- Coroutines (计划中)
- Ranges library
- Three-way comparison operator

## 2. 平台特定工具链

### 2.1 Linux 工具链

**必需依赖**:
```bash
# Ubuntu/Debian
sudo apt install clang-16 \
  libglu1-mesa-dev \
  libc++-16-dev \
  libc++abi-16-dev \
  ninja-build \
  libxi-dev \
  libxcomposite-dev \
  libxxf86vm-dev

# 设置 Clang 为默认编译器
export CC=/usr/bin/clang-16
export CXX=/usr/bin/clang++-16
export CXXFLAGS=-stdlib=libc++
```

**CMake 配置**:
```bash
CC=/usr/bin/clang CXX=/usr/bin/clang++ CXXFLAGS=-stdlib=libc++ \
  cmake -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=../release/filament \
  ../..
```

### 2.2 macOS 工具链

**Xcode 要求**:
```bash
# 安装 Xcode 命令行工具
xcode-select --install

# 验证安装
clang --version
# 应该显示 Apple clang version 14.0+
```

**可选 Vulkan 支持**:
```bash
# 安装 LunarG SDK (可选)
# 下载: https://vulkan.lunarg.com/sdk/home
# 安装后需要重启机器启用系统全局组件
```

**CMake 配置**:
```bash
# macOS 部署目标
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.15 CACHE STRING "")

# 多架构支持
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")  # Universal binary
```

### 2.3 Windows 工具链

**Visual Studio 要求**:
- Visual Studio 2019 或更高版本
- Windows SDK 10.0.18362.0 或更高版本
- CMake 3.14+ (VS 内置或独立安装)

**必需组件**:
```
Visual Studio 组件:
- MSVC v142 编译器工具集
- Windows 10 SDK
- CMake tools for VS
- Git for Windows
```

**环境配置**:
```batch
REM 使用 VS Developer Command Prompt
"C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\Tools\VsDevCmd.bat"

REM 或使用 x64 Native Tools
"C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
```

### 2.4 Android 工具链

**Android Studio 要求**:
- Android Studio Flamingo 或更新版本
- Android SDK
- Android NDK 25.1 或更高版本
- Java 17

**环境变量设置**:
```bash
export ANDROID_HOME=/path/to/android/sdk
export NDK_ROOT=$ANDROID_HOME/ndk/25.2.9519653
```

**工具链配置文件**:
```cmake
# build/toolchain-aarch64-linux-android.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(API_LEVEL 21)
set(ARCH aarch64-linux-android)
set(DIST_ARCH arm64-v8a)

# 自动检测 NDK 版本
file(GLOB NDK_VERSIONS LIST_DIRECTORIES true ${ANDROID_HOME_UNIX}/ndk/${FILAMENT_NDK_VERSION}*)
```

**支持的架构**:
- arm64-v8a (主要目标)
- armeabi-v7a
- x86_64
- x86

### 2.5 iOS 工具链

**Xcode 要求**:
- Xcode 14.0+
- iOS SDK 11.0+
- macOS 作为宿主系统

**构建命令**:
```bash
./build.sh -p ios debug
```

**特殊配置**:
```cmake
if (IOS)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-exceptions")
    set(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS ON)
endif()
```

### 2.6 WebAssembly 工具链

**Emscripten SDK 安装**:
```bash
# 下载指定版本
curl -L https://github.com/emscripten-core/emsdk/archive/refs/tags/3.1.60.zip > emsdk.zip
unzip emsdk.zip
cd emsdk-*

# 安装和激活
python ./emsdk.py install latest
python ./emsdk.py activate latest
source ./emsdk_env.sh
```

**环境变量**:
```bash
export EMSDK=/path/to/emsdk
```

**构建命令**:
```bash
./build.sh -p webgl release
```

## 3. 构建工具

### 3.1 Ninja 构建系统

**推荐构建系统**: Ninja (比 Make 更快)

**安装**:
```bash
# Linux
sudo apt install ninja-build

# macOS
brew install ninja

# Windows
choco install ninja
```

**CMake 配置**:
```bash
cmake -G Ninja ...
```

### 3.2 ccache 构建加速

**自动检测和配置**:
```cmake
find_program(CCACHE_PROGRAM ccache)
if (CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()
```

**手动配置**:
```bash
# 安装 ccache
sudo apt install ccache  # Linux
brew install ccache       # macOS

# 设置缓存大小
ccache -M 10G

# 查看统计信息
ccache -s
```

### 3.3 Python 依赖

**版本要求**: Python 3.7+

**用途**:
- Emscripten SDK 管理
- 构建脚本辅助
- 材质编译优化脚本

## 4. 开发环境推荐

### 4.1 推荐 IDE

**CLion** (官方推荐):
```bash
# 直接打开根目录的 CMakeLists.txt
# CLion 会自动配置项目
```

**配置**:
- 自动生成 `compile_commands.json`
- 支持多个构建配置
- 集成调试器支持

### 4.2 编辑器配置

**VS Code 支持**:
```json
// .vscode/settings.json
{
    "cmake.configureOnOpen": true,
    "C_Cpp.default.compileCommands": "${workspaceFolder}/build/compile_commands.json"
}
```

**Vim 支持**:
```cmake
# 自动生成 compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

## 5. 工具链验证脚本

### 5.1 环境检查脚本

```bash
#!/bin/bash
# check_toolchain.sh

echo "=== Filament 工具链检查 ==="

# 检查 CMake
echo "检查 CMake..."
cmake --version | head -n1
if [ $? -ne 0 ]; then
    echo "❌ CMake 未安装"
    exit 1
fi

# 检查编译器
echo "检查编译器..."
if command -v clang++ &> /dev/null; then
    clang++ --version | head -n1
elif command -v g++ &> /dev/null; then
    echo "⚠️  使用 GCC，推荐使用 Clang"
    g++ --version | head -n1
else
    echo "❌ 未找到 C++ 编译器"
    exit 1
fi

# 检查 Ninja
echo "检查 Ninja..."
if command -v ninja &> /dev/null; then
    ninja --version
else
    echo "⚠️  Ninja 未安装，将使用 Make"
fi

# 检查平台特定依赖
case "$OSTYPE" in
    linux*)
        echo "检查 Linux 依赖..."
        dpkg -l | grep -E "(libglu1-mesa-dev|libc\+\+-.*-dev)" || echo "⚠️  缺少 Linux 开发库"
        ;;
    darwin*)
        echo "检查 macOS 环境..."
        xcode-select -p || echo "❌ 需要安装 Xcode 命令行工具"
        ;;
esac

echo "✅ 工具链检查完成"
```

### 5.2 快速验证构建

```bash
#!/bin/bash
# quick_build_test.sh

# 创建最小构建测试
mkdir -p build_test
cd build_test

# 配置最小构建
cmake .. \
  -DFILAMENT_SKIP_SAMPLES=ON \
  -DFILAMENT_BUILD_FILAMAT=OFF \
  -DCMAKE_BUILD_TYPE=Release

# 构建核心库
cmake --build . --target filament --parallel $(nproc)

if [ $? -eq 0 ]; then
    echo "✅ 快速构建测试成功"
else
    echo "❌ 构建失败，请检查工具链配置"
    exit 1
fi
```

## 6. 常见问题和解决方案

### 6.1 编译器问题

**问题**: Linux 上默认使用 GCC
```bash
# 解决方案：强制使用 Clang
export CC=clang
export CXX=clang++
export CXXFLAGS=-stdlib=libc++
```

**问题**: Windows 上 Clang 不被支持
```cmake
# Filament 不再支持 Windows 上的 Clang
# 必须使用 MSVC 2019+
```

### 6.2 依赖库问题

**问题**: Linux 缺少系统库
```bash
# 安装完整的开发依赖
sudo apt-get build-dep mesa
sudo apt install build-essential
```

**问题**: macOS 缺少命令行工具
```bash
# 重新安装命令行工具
sudo rm -rf /Library/Developer/CommandLineTools
xcode-select --install
```

### 6.3 Android 构建问题

**问题**: NDK 版本不匹配
```bash
# 检查 NDK 版本
ls $ANDROID_HOME/ndk/

# 设置特定版本
export FILAMENT_NDK_VERSION=25.2.9519653
```

## 总结

Filament 的工具链要求相对现代但合理：

1. **核心要求**: CMake 3.22.1+, Clang 16.0+, C++20
2. **平台支持**: 全面的跨平台工具链
3. **构建工具**: Ninja + ccache 组合提供最佳性能
4. **开发环境**: CLion 或 VS Code 推荐
5. **验证机制**: 提供完整的环境检查方案

理解这些工具链要求对于成功构建和开发 Filament 项目至关重要。