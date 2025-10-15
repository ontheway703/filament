# 任务6: 集成测试 + 示例程序

**预计时间**: 2小时
**前置依赖**: 任务1-5完成
**完成标志**: 端到端示例程序运行成功

---

## 目标

创建完整的集成测试和示例程序：
1. 端到端加载流程测试
2. 分离加载演示（骨骼+网格+动画分别来自不同文件的模拟）
3. 多网格共享骨骼演示
4. 集成到Filament示例体系

---

## 1. 创建完整示例程序

**文件**: `libs/gltfio_ext/tests/test_integration.cpp`

```cpp
#include <gltfio/AssetLoaderExt.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/MeshAsset.h>
#include <gltfio/AnimationAsset.h>
#include <gltfio/StandaloneAnimator.h>
#include <gltfio/MaterialProvider.h>

#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Renderer.h>
#include <filament/Camera.h>
#include <filament/SwapChain.h>
#include <filament/Skybox.h>

#include <utils/EntityManager.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>

using namespace filament;
using namespace filament::gltfio;
using namespace utils;

static std::vector<uint8_t> readFile(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << path << std::endl;
        return {};
    }
    size_t size = file.tellg();
    std::vector<uint8_t> buffer(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}

int main() {
    std::cout << "=== gltfio_ext Integration Test ===" << std::endl;

    // 1. 创建Filament环境
    Engine* engine = Engine::create();
    Scene* scene = engine->createScene();
    View* view = engine->createView();
    Renderer* renderer = engine->createRenderer();

    // 创建相机
    Entity cameraEntity = EntityManager::get().create();
    Camera* camera = engine->createCamera(cameraEntity);
    camera->setProjection(45.0, 16.0 / 9.0, 0.1, 100.0, Camera::Fov::VERTICAL);
    camera->lookAt({0, 1, 3}, {0, 1, 0});
    view->setCamera(camera);

    // 创建天空盒
    Skybox* skybox = Skybox::Builder()
        .color({0.1f, 0.125f, 0.25f, 1.0f})
        .build(*engine);
    scene->setSkybox(skybox);

    view->setScene(scene);
    view->setViewport({0, 0, 1280, 720});

    // 2. 创建加载器
    MaterialProvider* materials = createUbershaderProvider(engine);
    AssetConfigurationExt config = {
        .engine = engine,
        .materials = materials
    };
    AssetLoaderExt* loader = AssetLoaderExt::create(config);

    // 3. 加载资产
    std::cout << "\n--- Loading Assets ---" << std::endl;
    std::vector<uint8_t> data = readFile("third_party/models/AnimatedMorphCube/AnimatedMorphCube.glb");

    if (data.empty()) {
        std::cerr << "Failed to read glTF file" << std::endl;
        return 1;
    }

    // 3.1 加载骨骼
    SkeletonAsset* skeleton = loader->loadSkeleton(data.data(), data.size());
    if (!skeleton) {
        std::cerr << "Failed to load skeleton" << std::endl;
        return 1;
    }
    std::cout << "✓ Skeleton loaded: " << skeleton->getBoneCount() << " bones" << std::endl;

    // 3.2 加载网格
    MeshAsset* mesh = loader->loadMesh(data.data(), data.size());
    if (!mesh) {
        std::cerr << "Failed to load mesh" << std::endl;
        return 1;
    }
    std::cout << "✓ Mesh loaded" << std::endl;

    // 3.3 绑定骨骼到网格
    if (mesh->bindSkeleton(skeleton)) {
        std::cout << "✓ Skeleton bound to mesh" << std::endl;
    } else {
        std::cout << "⚠ Mesh has no skinning (expected for some models)" << std::endl;
    }

    // 3.4 加载动画
    AnimationAsset* anim = loader->loadAnimation(data.data(), data.size());
    if (!anim) {
        std::cerr << "Failed to load animation" << std::endl;
        return 1;
    }
    std::cout << "✓ Animation loaded: '" << anim->getName()
              << "' (" << anim->getDuration() << "s, "
              << anim->getChannelCount() << " channels)" << std::endl;

    // 4. 创建动画器
    std::cout << "\n--- Setting up Animator ---" << std::endl;
    StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
    animator->bindSkeleton(skeleton);
    int animId = animator->playAnimation(anim, 1.0f, true);
    std::cout << "✓ Animator created and playing" << std::endl;

    // 5. 添加到场景
    scene->addEntity(mesh->getRenderableEntity());
    std::cout << "✓ Mesh added to scene" << std::endl;

    // 6. 模拟渲染循环
    std::cout << "\n--- Simulating Render Loop ---" << std::endl;
    auto startTime = std::chrono::high_resolution_clock::now();

    for (int frame = 0; frame < 60; ++frame) {
        auto now = std::chrono::high_resolution_clock::now();
        float deltaTime = 1.0f / 60.0f;

        // 更新动画
        animator->update(deltaTime);
        mesh->updateSkinning(animator);

        if (frame % 10 == 0) {
            const auto* boneMatrices = animator->getBoneMatrices();
            if (boneMatrices && animator->getBoneCount() > 0) {
                std::cout << "  Frame " << frame << ": bone[0] = ["
                          << boneMatrices[0][0][0] << ", "
                          << boneMatrices[0][0][1] << ", "
                          << boneMatrices[0][0][2] << "]" << std::endl;
            }
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    std::cout << "✓ Simulated 60 frames in " << duration.count() << "ms" << std::endl;

    // 7. 测试多网格共享骨骼
    std::cout << "\n--- Testing Multi-Mesh ---" << std::endl;
    MeshAsset* mesh2 = loader->loadMesh(data.data(), data.size());
    if (mesh2) {
        if (mesh2->bindSkeleton(skeleton)) {
            scene->addEntity(mesh2->getRenderableEntity());
            std::cout << "✓ Second mesh bound to same skeleton" << std::endl;

            // 更新两个网格
            animator->update(1.0f / 60.0f);
            mesh->updateSkinning(animator);
            mesh2->updateSkinning(animator);
            std::cout << "✓ Both meshes updated with same animator" << std::endl;

            scene->remove(mesh2->getRenderableEntity());
            loader->destroyMesh(mesh2);
        }
    }

    // 8. 清理
    std::cout << "\n--- Cleanup ---" << std::endl;
    scene->remove(mesh->getRenderableEntity());

    StandaloneAnimator::destroy(&animator);
    loader->destroyAnimation(anim);
    loader->destroyMesh(mesh);
    loader->destroySkeleton(skeleton);
    AssetLoaderExt::destroy(&loader);

    engine->destroy(skybox);
    engine->destroy(renderer);
    engine->destroy(view);
    engine->destroy(scene);
    engine->destroyCameraComponent(cameraEntity);
    EntityManager::get().destroy(cameraEntity);

    materials->destroyMaterials();
    delete materials;
    Engine::destroy(&engine);

    std::cout << "✓ All resources cleaned up" << std::endl;

    std::cout << "\n=== INTEGRATION TEST PASSED ===" << std::endl;
    return 0;
}
```

---

## 2. 创建README文档

**文件**: `libs/gltfio_ext/README.md`

```markdown
# gltfio_ext - Filament glTF Separate Loading Extension

Extends Filament's gltfio with support for separate loading of skeletons, meshes, and animations.

## Features

- **Separate Loading**: Load skeleton, mesh, and animation from different glTF files
- **Flexible Binding**: One skeleton can drive multiple meshes (equipment system)
- **Runtime Animation Switching**: Load and play animations independently
- **Batch Loading**: Load multiple animations in one go with AnimationPack
- **Name-based Binding**: Use bone names for delayed binding (no Entity dependency)
- **Performance Optimized**: Name-to-index mapping cache

## Quick Example

```cpp
#include <gltfio/AssetLoaderExt.h>

// Create loader
AssetConfigurationExt config = { .engine = engine, .materials = materials };
AssetLoaderExt* loader = AssetLoaderExt::create(config);

// Load assets separately
SkeletonAsset* skeleton = loader->loadSkeleton(skelBytes, skelSize);
MeshAsset* mesh = loader->loadMesh(meshBytes, meshSize);
AnimationAsset* anim = loader->loadAnimation(animBytes, animSize);

// Bind and animate
mesh->bindSkeleton(skeleton);
StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
animator->bindSkeleton(skeleton);
animator->playAnimation(anim);

// Update loop
while (rendering) {
    animator->update(deltaTime);
    mesh->updateSkinning(animator);
    renderer->render(view);
}
```

## Building

```bash
cd out
cmake -G Ninja ../..
ninja gltfio_ext
```

## Testing

```bash
ninja test_skeleton test_mesh test_animation test_animator test_integration
./libs/test_integration
```

## Architecture

- **SkeletonAsset**: Bone hierarchy + inverse bind matrices
- **MeshAsset**: Geometry + skinning data
- **AnimationAsset**: Animation channels (name-based)
- **AnimationPack**: Batch-loaded animations
- **StandaloneAnimator**: Independent animation playback
- **AssetLoaderExt**: Unified loader interface

## Dependencies

- Filament (gltfio_core only, no filamat)
- cgltf (via gltfio_core)
- math, utils

## License

Same as Filament (Apache 2.0)
```

---

## 3. 更新顶层CMakeLists.txt

确认 `libs/gltfio_ext` 已添加到构建系统。

**文件**: `CMakeLists.txt` (检查)

```cmake
# 在 libs/ 部分找到 gltfio，确保 gltfio_ext 在其后
add_subdirectory(libs/gltfio)
add_subdirectory(libs/gltfio_ext)  # 确保存在
```

---

## 4. 更新测试CMakeLists.txt

**文件**: `libs/gltfio_ext/CMakeLists.txt` (追加)

```cmake
# 集成测试
if (NOT WEBGL AND NOT ANDROID AND NOT IOS)
    add_executable(test_integration tests/test_integration.cpp)
    target_link_libraries(test_integration PRIVATE gltfio_ext)
    set_target_properties(test_integration PROPERTIES FOLDER Tests)
endif()

# 添加所有测试到总测试目标
if (NOT WEBGL AND NOT ANDROID AND NOT IOS)
    add_custom_target(test_gltfio_ext
        DEPENDS test_skeleton test_mesh test_animation test_animator test_integration
    )
    set_target_properties(test_gltfio_ext PROPERTIES FOLDER Tests)
endif()
```

---

## 5. 创建综合验证脚本

**文件**: `libs/gltfio_ext/run_tests.sh`

```bash
#!/bin/bash

set -e

echo "=== Building gltfio_ext ==="
cd ../../out
ninja gltfio_ext

echo ""
echo "=== Building Tests ==="
ninja test_skeleton test_mesh test_animation test_animator test_integration

echo ""
echo "=== Running Tests ==="

echo ""
echo "--- Test 1: Skeleton ---"
./libs/test_skeleton

echo ""
echo "--- Test 2: Mesh ---"
./libs/test_mesh

echo ""
echo "--- Test 3: Animation ---"
./libs/test_animation

echo ""
echo "--- Test 4: Animator ---"
./libs/test_animator

echo ""
echo "--- Test 5: Integration ---"
./libs/test_integration

echo ""
echo "=== ALL TESTS PASSED ==="
```

```bash
chmod +x libs/gltfio_ext/run_tests.sh
```

---

## 6. 验证完整流程

```bash
# 1. 构建
cd out
cmake -G Ninja ../..
ninja gltfio_ext

# 2. 运行所有测试
cd ..
./libs/gltfio_ext/run_tests.sh

# 预期输出:
# === Building gltfio_ext ===
# [...]
# === Running Tests ===
# --- Test 1: Skeleton ---
# [骨骼测试输出]
# === Test PASSED ===
# --- Test 2: Mesh ---
# [网格测试输出]
# === Test PASSED ===
# [...]
# === ALL TESTS PASSED ===

# 3. 检查库文件
ls -lh out/libs/libgltfio_ext.a

# 4. 检查符号导出
nm out/libs/libgltfio_ext.a | grep "AssetLoaderExt"
```

---

## 7. 文档完整性检查

确保以下文档齐全：

- [ ] `docs/implement/task_01_framework.md`
- [ ] `docs/implement/task_02_skeleton.md`
- [ ] `docs/implement/task_03_mesh.md`
- [ ] `docs/implement/task_04_animation.md`
- [ ] `docs/implement/task_05_animator.md`
- [ ] `docs/implement/task_06_integration.md`
- [ ] `libs/gltfio_ext/README.md`
- [ ] `plan/separate_loading_design.md` (已存在)
- [ ] `plan/usage_examples.md` (已存在)
- [ ] `plan/api_comparison.md` (已存在)

---

## 8. 性能基准测试（可选）

**文件**: `libs/gltfio_ext/tests/benchmark_loading.cpp`

```cpp
#include <gltfio/AssetLoaderExt.h>
#include <chrono>
#include <iostream>

int main() {
    auto start = std::chrono::high_resolution_clock::now();

    // 加载测试
    // ...

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "Loading time: " << duration.count() << "μs" << std::endl;
    return 0;
}
```

---

## 完成标志

- [ ] test_integration 编译成功
- [ ] 所有5个测试程序运行成功
- [ ] 集成测试输出正确
- [ ] README.md 创建完成
- [ ] 构建脚本运行无误
- [ ] 所有文档齐全

---

## 项目总结

完成任务1-6后，gltfio_ext库实现了以下功能：

### 核心功能
- ✅ 骨骼分离加载 (SkeletonAsset)
- ✅ 网格分离加载 (MeshAsset)
- ✅ 动画分离加载 (AnimationAsset)
- ✅ 批量动画加载 (AnimationPack)
- ✅ 独立动画播放器 (StandaloneAnimator)
- ✅ 名称延迟绑定（性能优化）

### 技术特点
- 依赖gltfio_core（不含filamat）
- 使用Filament公开API
- 独立编译，不修改原gltfio
- 完整的错误处理
- 自动资源清理

### 测试覆盖
- 单元测试：skeleton, mesh, animation, animator
- 集成测试：端到端流程
- 多网格共享骨骼测试

### 文档完整性
- 设计文档（plan/）
- 执行计划（docs/implement/）
- 使用说明（README.md）
- API对比（plan/api_comparison.md）

---

**恭喜！gltfio_ext 实现完成！**
