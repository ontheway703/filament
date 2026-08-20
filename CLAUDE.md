# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Filament is a real-time physically based rendering engine for Android, iOS, Linux, macOS, Windows, and WebGL. It is designed to be small and efficient, particularly optimized for Android. The project uses CMake as its build system and supports multiple rendering backends (OpenGL, Metal, Vulkan, WebGL).

## Build Commands

### Build Script (Recommended)
The `build.sh` script is the easiest way to build Filament on macOS and Linux:

```bash
# Debug build
./build.sh debug

# Release build
./build.sh release

# Both debug and release
./build.sh debug release

# Clean build (force rebuild)
./build.sh -c debug

# Install libraries and executables
./build.sh -i debug

# Run tests (triggers debug build if needed)
./build.sh test

# Platform-specific builds
./build.sh -p android release    # Android
./build.sh -p ios debug         # iOS
./build.sh -p webgl release     # WebGL
```

### Manual CMake Build
```bash
mkdir out/cmake-release
cd out/cmake-release
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../release/filament ../..
ninja
```

### Testing
- Use `./build.sh test` to run all unit tests
- Individual tool tests are located in `tools/*/tests/`
- Backend-specific tests are in `test/` directory

## Code Architecture

### Core Components

**Engine (`filament/`)**: The main rendering engine with minimal dependencies
- `filament/backend/`: Rendering backends (Vulkan, Metal, OpenGL/ES drivers)
- `filament/src/`: Core engine implementation
- `filament/include/`: Public API headers

**Libraries (`libs/`)**:
- `bluegl`: OpenGL bindings for desktop platforms
- `bluevk`: Vulkan bindings
- `filamat`: Material generation library
- `filabridge`: Shared library between engine and host tools
- `gltfio`: glTF 2.0 loader
- `utils`: Core utility library (threads, memory, data structures)
- `math`: Math library for graphics operations
- `filamentapp`: SDL2 framework for sample applications

**Tools (`tools/`)**:
- `matc`: Material compiler (converts materials to binary format)
- `cmgen`: Image-based lighting asset generator
- `filamesh`: Mesh converter for various 3D formats
- `mipgen`: Mipmap generator
- `glslminifier`: GLSL shader minifier

### Key Concepts

**Materials**: Materials are compiled using `matc` tool and loaded as binary blobs. Always use tools from the same release as the runtime library.

**Backends**: The engine supports multiple rendering backends abstracted through the `backend/` layer. Backend selection happens at Engine creation time.

**Entity-Component System**: Uses an entity-component architecture for scene management with `EntityManager`.

**Build Artifacts**:
- Debug builds go to `out/debug/`
- Release builds go to `out/release/`
- Android builds go to `out/android-release/`

## Development Workflow

### Material Development
1. Write material definition files
2. Compile with `matc` tool: `matc -o material.filamat material.mat`
3. Load in engine as binary blob

### Asset Pipeline
1. Convert meshes: `filamesh input.obj output.filamesh`
2. Generate IBL: `cmgen -f ktx -x ./ibls/ environment.exr`
3. Process textures with `mipgen` for mipmaps

### Code Style
- Uses Android code style (4 spaces indent, 100 columns)
- Headers use `.h`, implementation uses `.cpp`
- Public headers in `include/`, source files in `src/`
- Use `#include <>` for public headers, `#include ""` for private headers

### Platform-Specific Notes

**Android**:
- Requires Android Studio, NDK 25.1+, Java 17
- Set `ANDROID_HOME` environment variable
- Build native libraries first, then AAR with `./gradlew assembleRelease`

**iOS**:
- Build with `./build.sh -p ios debug`
- Supports Metal (preferred) and OpenGL ES

**WebGL**:
- Requires Emscripten SDK
- Set `EMSDK` environment variable
- Use `emrun` or live-server for local testing

## Common Issues

- Always use matching tool versions with runtime library
- For Android builds, build host tools first
- WebGL builds cannot be opened directly from filesystem (use local server)
- Material compiler output must match the target backend capabilities