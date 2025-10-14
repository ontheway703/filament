# Filament 分离加载 - 使用示例集

**版本**: 1.0
**日期**: 2025-01-13

---

## 目录
- [示例 1: 基础使用](#示例-1-基础使用)
- [示例 2: 批量加载动画包](#示例-2-批量加载动画包)
- [示例 3: 动画混合与过渡](#示例-3-动画混合与过渡)
- [示例 4: 多角色共享骨骼](#示例-4-多角色共享骨骼)
- [示例 5: 运行时替换装备](#示例-5-运行时替换装备)
- [编译说明](#编译说明)

---

## 示例 1: 基础使用

**文件**: `samples/separate_loading_basic.cpp`

这是最简单的完整示例，展示如何加载骨骼、网格、动画并播放。

```cpp
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Renderer.h>
#include <filament/Camera.h>
#include <filament/SwapChain.h>
#include <filament/Skybox.h>

#include <gltfio/AssetLoaderExt.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/MeshAsset.h>
#include <gltfio/AnimationAsset.h>
#include <gltfio/StandaloneAnimator.h>
#include <gltfio/MaterialProvider.h>

#include <utils/EntityManager.h>
#include <utils/Path.h>

#include <fstream>
#include <vector>
#include <iostream>
#include <chrono>

using namespace filament;
using namespace filament::gltfio;
using namespace utils;

// 辅助函数：读取文件
static std::vector<uint8_t> readFile(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << path << std::endl;
        return {};
    }

    size_t size = file.tellg();
    std::vector<uint8_t> buffer(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}

struct App {
    Engine* engine = nullptr;
    SwapChain* swapChain = nullptr;
    Renderer* renderer = nullptr;
    Scene* scene = nullptr;
    View* view = nullptr;
    Camera* camera = nullptr;
    Entity cameraEntity;
    Skybox* skybox = nullptr;

    // gltfio_ext 组件
    AssetLoaderExt* loader = nullptr;
    SkeletonAsset* skeleton = nullptr;
    MeshAsset* mesh = nullptr;
    AnimationAsset* walkAnim = nullptr;
    StandaloneAnimator* animator = nullptr;
};

static void cleanup(App& app) {
    // 从场景移除实体
    app.scene->removeEntity(app.mesh->getRenderableEntity());

    // 清理动画和资产
    StandaloneAnimator::destroy(&app.animator);
    app.loader->destroyAnimation(app.walkAnim);
    app.loader->destroyMesh(app.mesh);
    app.loader->destroySkeleton(app.skeleton);
    AssetLoaderExt::destroy(&app.loader);

    // 清理 Filament 对象
    app.engine->destroy(app.skybox);
    app.engine->destroy(app.renderer);
    app.engine->destroy(app.view);
    app.engine->destroy(app.scene);
    app.engine->destroy(app.swapChain);
    app.engine->destroyCameraComponent(app.cameraEntity);
    EntityManager::get().destroy(app.cameraEntity);
    Engine::destroy(&app.engine);
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <skeleton.glb> <mesh.glb> <animation.glb>" << std::endl;
        return 1;
    }

    App app;

    // 1. 创建 Filament Engine
    app.engine = Engine::create();
    app.swapChain = app.engine->createSwapChain(1280, 720);
    app.renderer = app.engine->createRenderer();
    app.scene = app.engine->createScene();
    app.view = app.engine->createView();

    // 2. 设置摄像机
    app.cameraEntity = EntityManager::get().create();
    app.camera = app.engine->createCamera(app.cameraEntity);
    app.camera->setProjection(45.0, 16.0 / 9.0, 0.1, 100.0, Camera::Fov::VERTICAL);
    app.camera->lookAt({0, 1, 3}, {0, 1, 0});
    app.view->setCamera(app.camera);

    // 3. 设置天空盒
    app.skybox = Skybox::Builder()
        .color({0.1f, 0.125f, 0.25f, 1.0f})
        .build(*app.engine);
    app.scene->setSkybox(app.skybox);

    // 4. 配置 View
    app.view->setScene(app.scene);
    app.view->setViewport({0, 0, 1280, 720});
    app.view->setPostProcessingEnabled(false);

    // 5. 创建扩展加载器
    MaterialProvider* materials = createUbershaderProvider(app.engine);
    AssetConfiguration config = {
        .engine = app.engine,
        .materials = materials
    };
    app.loader = AssetLoaderExt::create(config);

    // 6. 加载骨骼
    std::cout << "Loading skeleton..." << std::endl;
    auto skeletonData = readFile(argv[1]);
    app.skeleton = app.loader->loadSkeleton(skeletonData.data(), skeletonData.size());
    if (!app.skeleton) {
        std::cerr << "Failed to load skeleton!" << std::endl;
        return 1;
    }
    std::cout << "Loaded " << app.skeleton->getBoneCount() << " bones" << std::endl;

    // 7. 加载网格
    std::cout << "Loading mesh..." << std::endl;
    auto meshData = readFile(argv[2]);
    app.mesh = app.loader->loadMesh(meshData.data(), meshData.size());
    if (!app.mesh) {
        std::cerr << "Failed to load mesh!" << std::endl;
        return 1;
    }

    // 8. 绑定骨骼到网格
    if (!app.mesh->bindSkeleton(app.skeleton)) {
        std::cerr << "Failed to bind skeleton to mesh!" << std::endl;
        return 1;
    }
    std::cout << "Mesh bound to skeleton" << std::endl;

    // 9. 加载动画
    std::cout << "Loading animation..." << std::endl;
    auto animData = readFile(argv[3]);
    app.walkAnim = app.loader->loadAnimation(animData.data(), animData.size());
    if (!app.walkAnim) {
        std::cerr << "Failed to load animation!" << std::endl;
        return 1;
    }
    std::cout << "Animation: " << app.walkAnim->getName()
              << " (duration: " << app.walkAnim->getDuration() << "s)" << std::endl;

    // 10. 创建动画器并播放动画
    app.animator = StandaloneAnimator::create(*app.engine);
    app.animator->bindSkeleton(app.skeleton);
    app.animator->playAnimation(app.walkAnim, 1.0f, true);

    // 11. 添加到场景
    app.scene->addEntity(app.mesh->getRenderableEntity());

    // 12. 渲染循环
    std::cout << "Rendering... (Press Ctrl+C to quit)" << std::endl;

    auto lastTime = std::chrono::high_resolution_clock::now();
    size_t frameCount = 0;

    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        // 更新动画
        app.animator->update(deltaTime);
        app.mesh->updateSkinning(app.animator);

        // 渲染
        if (app.renderer->beginFrame(app.swapChain)) {
            app.renderer->render(app.view);
            app.renderer->endFrame();
        }

        // 简单的帧率统计
        frameCount++;
        if (frameCount % 60 == 0) {
            std::cout << "Frame " << frameCount << " (FPS: ~"
                      << static_cast<int>(1.0f / deltaTime) << ")" << std::endl;
        }

        // 运行 5 秒后退出（实际应用中应监听窗口关闭事件）
        if (frameCount > 300) break;
    }

    // 13. 清理
    cleanup(app);
    materials->destroyMaterials();
    delete materials;

    std::cout << "Done!" << std::endl;
    return 0;
}
```

---

## 示例 2: 批量加载动画包

**文件**: `samples/separate_loading_animpack.cpp`

展示如何一次性加载多个动画，减少文件 IO 次数。

```cpp
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/View.h>
// ... (其他头文件同示例1)

#include <gltfio/AnimationPack.h>  // ⭐ 新增

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <skeleton.glb> <mesh.glb> <animation_pack.glb>" << std::endl;
        return 1;
    }

    App app;
    // ... (初始化同示例1)

    // 加载骨骼和网格（同示例1）
    // ...

    // ⭐ 批量加载动画包
    std::cout << "Loading animation pack..." << std::endl;
    auto packData = readFile(argv[3]);
    AnimationPack* animPack = app.loader->loadAnimationPack(packData.data(), packData.size());

    if (!animPack) {
        std::cerr << "Failed to load animation pack!" << std::endl;
        return 1;
    }

    // 列出所有动画
    std::cout << "Loaded " << animPack->getAnimationCount() << " animations:" << std::endl;
    for (size_t i = 0; i < animPack->getAnimationCount(); ++i) {
        const char* name = animPack->getAnimationNames()[i];
        AnimationAsset* anim = animPack->getAnimation(i);
        std::cout << "  [" << i << "] " << name
                  << " (duration: " << anim->getDuration() << "s)" << std::endl;
    }

    // 通过名称查找特定动画
    AnimationAsset* idleAnim = animPack->findAnimation("idle");
    AnimationAsset* walkAnim = animPack->findAnimation("walk");
    AnimationAsset* runAnim = animPack->findAnimation("run");

    if (!idleAnim || !walkAnim || !runAnim) {
        std::cerr << "Missing required animations!" << std::endl;
        return 1;
    }

    // 创建动画器
    StandaloneAnimator* animator = StandaloneAnimator::create(*app.engine);
    animator->bindSkeleton(app.skeleton);

    // 播放序列：idle → walk → run
    AnimationAsset* sequence[] = { idleAnim, walkAnim, runAnim };
    const char* sequenceNames[] = { "idle", "walk", "run" };
    size_t currentAnim = 0;

    app.scene->addEntity(app.mesh->getRenderableEntity());

    auto lastTime = std::chrono::high_resolution_clock::now();
    float animTime = 0.0f;

    animator->playAnimation(sequence[currentAnim]);
    std::cout << "Playing: " << sequenceNames[currentAnim] << std::endl;

    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        animTime += deltaTime;

        // 每 3 秒切换动画
        if (animTime > 3.0f) {
            animTime = 0.0f;
            currentAnim = (currentAnim + 1) % 3;
            animator->playAnimation(sequence[currentAnim]);
            std::cout << "Switching to: " << sequenceNames[currentAnim] << std::endl;
        }

        // 更新和渲染
        animator->update(deltaTime);
        app.mesh->updateSkinning(animator);

        if (app.renderer->beginFrame(app.swapChain)) {
            app.renderer->render(app.view);
            app.renderer->endFrame();
        }

        // 运行 10 秒后退出
        static size_t frameCount = 0;
        if (++frameCount > 600) break;
    }

    // 清理
    StandaloneAnimator::destroy(&animator);
    app.loader->destroyAnimationPack(animPack);  // ⭐ 自动销毁所有动画
    // ... (其他清理)

    return 0;
}
```

---

## 示例 3: 动画混合与过渡

**文件**: `samples/separate_loading_blender.cpp`

使用 AnimationBlender 实现平滑的动画过渡和混合。

```cpp
#include <gltfio/AnimationBlender.h>  // ⭐ 新增

int main(int argc, char* argv[]) {
    // ... (初始化同前)

    // 加载动画包
    AnimationPack* animPack = app.loader->loadAnimationPack(...);
    AnimationAsset* idleAnim = animPack->findAnimation("idle");
    AnimationAsset* walkAnim = animPack->findAnimation("walk");
    AnimationAsset* runAnim = animPack->findAnimation("run");
    AnimationAsset* jumpAnim = animPack->findAnimation("jump");

    // 创建动画器和混合器
    StandaloneAnimator* animator = StandaloneAnimator::create(*app.engine);
    animator->bindSkeleton(app.skeleton);

    // ⭐ 创建混合器
    AnimationBlender* blender = AnimationBlender::create(animator);

    app.scene->addEntity(app.mesh->getRenderableEntity());

    // 演示1：自动过渡
    std::cout << "\n=== Demo 1: Auto Transition ===" << std::endl;

    blender->playAnimation(idleAnim, 0.3f);  // 0.3秒过渡到 idle
    std::cout << "Playing idle..." << std::endl;

    auto startTime = std::chrono::high_resolution_clock::now();
    auto now = startTime;

    while (std::chrono::duration<float>(now - startTime).count() < 2.0f) {
        now = std::chrono::high_resolution_clock::now();
        float dt = 1.0f / 60.0f;

        blender->update(dt);  // ⭐ 只需调用一次
        app.mesh->updateSkinning(animator);

        if (app.renderer->beginFrame(app.swapChain)) {
            app.renderer->render(app.view);
            app.renderer->endFrame();
        }
    }

    // 切换到 walk（自动淡入淡出）
    blender->playAnimation(walkAnim, 0.5f);  // 0.5秒过渡
    std::cout << "Transitioning to walk..." << std::endl;

    startTime = std::chrono::high_resolution_clock::now();
    while (std::chrono::duration<float>(now - startTime).count() < 2.0f) {
        now = std::chrono::high_resolution_clock::now();
        blender->update(1.0f / 60.0f);
        app.mesh->updateSkinning(animator);
        // ... (渲染)
    }

    // 演示2：双动画混合（根据移动速度）
    std::cout << "\n=== Demo 2: Blend by Speed ===" << std::endl;

    for (float speed = 0.0f; speed <= 1.0f; speed += 0.1f) {
        // 速度 0=idle, 0.5=walk, 1.0=run
        float walkFactor = std::min(speed * 2.0f, 1.0f);
        float runFactor = std::max(speed * 2.0f - 1.0f, 0.0f);

        if (runFactor < 0.01f) {
            blender->blendAnimations(idleAnim, walkAnim, walkFactor);
            std::cout << "Blending: idle(" << (1-walkFactor)
                      << ") + walk(" << walkFactor << ")" << std::endl;
        } else {
            blender->blendAnimations(walkAnim, runAnim, runFactor);
            std::cout << "Blending: walk(" << (1-runFactor)
                      << ") + run(" << runFactor << ")" << std::endl;
        }

        // 渲染 0.5 秒
        startTime = std::chrono::high_resolution_clock::now();
        while (std::chrono::duration<float>(now - startTime).count() < 0.5f) {
            now = std::chrono::high_resolution_clock::now();
            blender->update(1.0f / 60.0f);
            app.mesh->updateSkinning(animator);
            // ... (渲染)
        }
    }

    // 演示3：附加动画（叠加跳跃）
    std::cout << "\n=== Demo 3: Additive Animation ===" << std::endl;

    blender->playAnimation(walkAnim);
    blender->addAdditiveAnimation(jumpAnim, 1.0f);  // ⭐ 叠加跳跃
    std::cout << "Walking + jumping..." << std::endl;

    startTime = std::chrono::high_resolution_clock::now();
    while (std::chrono::duration<float>(now - startTime).count() < 2.0f) {
        now = std::chrono::high_resolution_clock::now();
        blender->update(1.0f / 60.0f);
        app.mesh->updateSkinning(animator);
        // ... (渲染)
    }

    // 清理
    AnimationBlender::destroy(&blender);
    StandaloneAnimator::destroy(&animator);
    // ... (其他清理)

    return 0;
}
```

---

## 示例 4: 多角色共享骨骼

**文件**: `samples/separate_loading_multi_char.cpp`

一个骨骼驱动多个网格，实现角色换装等功能。

```cpp
int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0]
                  << " <skeleton.glb> <body.glb> <armor.glb> <animation.glb>" << std::endl;
        return 1;
    }

    App app;
    // ... (初始化)

    // 加载共享骨骼
    auto skeletonData = readFile(argv[1]);
    SkeletonAsset* skeleton = app.loader->loadSkeleton(skeletonData.data(), skeletonData.size());
    std::cout << "Loaded shared skeleton: " << skeleton->getBoneCount() << " bones" << std::endl;

    // 加载多个网格（身体、盔甲）
    auto bodyData = readFile(argv[2]);
    MeshAsset* bodyMesh = app.loader->loadMesh(bodyData.data(), bodyData.size());

    auto armorData = readFile(argv[3]);
    MeshAsset* armorMesh = app.loader->loadMesh(armorData.data(), armorData.size());

    // 两个网格绑定到同一个骨骼
    bodyMesh->bindSkeleton(skeleton);
    armorMesh->bindSkeleton(skeleton);
    std::cout << "Both meshes bound to skeleton" << std::endl;

    // 加载动画
    auto animData = readFile(argv[4]);
    AnimationAsset* walkAnim = app.loader->loadAnimation(animData.data(), animData.size());

    // 创建动画器
    StandaloneAnimator* animator = StandaloneAnimator::create(*app.engine);
    animator->bindSkeleton(skeleton);
    animator->playAnimation(walkAnim);

    // 添加两个网格到场景
    app.scene->addEntity(bodyMesh->getRenderableEntity());
    app.scene->addEntity(armorMesh->getRenderableEntity());

    // 渲染循环
    auto lastTime = std::chrono::high_resolution_clock::now();
    size_t frameCount = 0;
    bool showArmor = true;

    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        // 每 2 秒切换显示/隐藏盔甲
        if (++frameCount % 120 == 0) {
            showArmor = !showArmor;
            if (showArmor) {
                app.scene->addEntity(armorMesh->getRenderableEntity());
                std::cout << "Armor shown" << std::endl;
            } else {
                app.scene->removeEntity(armorMesh->getRenderableEntity());
                std::cout << "Armor hidden" << std::endl;
            }
        }

        // 更新动画（一次更新驱动所有网格）
        animator->update(deltaTime);
        bodyMesh->updateSkinning(animator);   // ⭐ 同一个 animator
        armorMesh->updateSkinning(animator);  // ⭐ 更新两个网格

        // 渲染
        if (app.renderer->beginFrame(app.swapChain)) {
            app.renderer->render(app.view);
            app.renderer->endFrame();
        }

        if (frameCount > 600) break;
    }

    // 清理
    app.scene->removeEntity(bodyMesh->getRenderableEntity());
    app.scene->removeEntity(armorMesh->getRenderableEntity());

    StandaloneAnimator::destroy(&animator);
    app.loader->destroyAnimation(walkAnim);
    app.loader->destroyMesh(armorMesh);
    app.loader->destroyMesh(bodyMesh);
    app.loader->destroySkeleton(skeleton);
    // ... (其他清理)

    return 0;
}
```

---

## 示例 5: 运行时替换装备

**文件**: `samples/separate_loading_swap_equipment.cpp`

演示如何在运行时动态替换角色装备（武器、盔甲等）。

```cpp
#include <map>
#include <string>

struct EquipmentSlot {
    MeshAsset* currentMesh = nullptr;
    std::string slotName;
};

int main(int argc, char* argv[]) {
    App app;
    // ... (初始化)

    // 加载骨骼
    SkeletonAsset* skeleton = app.loader->loadSkeleton(...);

    // 加载身体（基础网格）
    MeshAsset* bodyMesh = app.loader->loadMesh(...);
    bodyMesh->bindSkeleton(skeleton);

    // 准备装备槽
    std::map<std::string, EquipmentSlot> equipment;
    equipment["helmet"] = {};
    equipment["chest"] = {};
    equipment["weapon"] = {};

    // 预加载多套装备
    struct EquipmentSet {
        std::string name;
        MeshAsset* mesh;
    };

    std::vector<EquipmentSet> helmets;
    helmets.push_back({ "iron_helmet", app.loader->loadMesh(...) });
    helmets.push_back({ "steel_helmet", app.loader->loadMesh(...) });

    std::vector<EquipmentSet> weapons;
    weapons.push_back({ "sword", app.loader->loadMesh(...) });
    weapons.push_back({ "axe", app.loader->loadMesh(...) });

    // 为所有装备绑定骨骼
    for (auto& h : helmets) h.mesh->bindSkeleton(skeleton);
    for (auto& w : weapons) w.mesh->bindSkeleton(skeleton);

    // 装备切换函数
    auto equipItem = [&](const std::string& slot, MeshAsset* newMesh) {
        auto& equipSlot = equipment[slot];

        // 移除旧装备
        if (equipSlot.currentMesh) {
            app.scene->removeEntity(equipSlot.currentMesh->getRenderableEntity());
        }

        // 装备新装备
        equipSlot.currentMesh = newMesh;
        if (newMesh) {
            app.scene->addEntity(newMesh->getRenderableEntity());
        }

        std::cout << "Equipped: " << slot << std::endl;
    };

    // 添加身体
    app.scene->addEntity(bodyMesh->getRenderableEntity());

    // 初始装备
    equipItem("helmet", helmets[0].mesh);
    equipItem("weapon", weapons[0].mesh);

    // 创建动画
    AnimationAsset* idleAnim = app.loader->loadAnimation(...);
    StandaloneAnimator* animator = StandaloneAnimator::create(*app.engine);
    animator->bindSkeleton(skeleton);
    animator->playAnimation(idleAnim);

    // 渲染循环 + 装备切换演示
    auto lastTime = std::chrono::high_resolution_clock::now();
    size_t frameCount = 0;
    size_t helmetIndex = 0;
    size_t weaponIndex = 0;

    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        // 每 3 秒切换头盔
        if (frameCount % 180 == 0) {
            helmetIndex = (helmetIndex + 1) % helmets.size();
            equipItem("helmet", helmets[helmetIndex].mesh);
            std::cout << "Changed helmet to: " << helmets[helmetIndex].name << std::endl;
        }

        // 每 5 秒切换武器
        if (frameCount % 300 == 0) {
            weaponIndex = (weaponIndex + 1) % weapons.size();
            equipItem("weapon", weapons[weaponIndex].mesh);
            std::cout << "Changed weapon to: " << weapons[weaponIndex].name << std::endl;
        }

        // 更新动画（所有装备同时更新）
        animator->update(deltaTime);

        bodyMesh->updateSkinning(animator);
        if (equipment["helmet"].currentMesh) {
            equipment["helmet"].currentMesh->updateSkinning(animator);
        }
        if (equipment["weapon"].currentMesh) {
            equipment["weapon"].currentMesh->updateSkinning(animator);
        }

        // 渲染
        if (app.renderer->beginFrame(app.swapChain)) {
            app.renderer->render(app.view);
            app.renderer->endFrame();
        }

        frameCount++;
        if (frameCount > 600) break;
    }

    // 清理
    equipItem("helmet", nullptr);  // 移除所有装备
    equipItem("weapon", nullptr);

    for (auto& h : helmets) app.loader->destroyMesh(h.mesh);
    for (auto& w : weapons) app.loader->destroyMesh(w.mesh);
    // ... (其他清理)

    return 0;
}
```

---

## 编译说明

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.10)
project(SeparateLoadingExamples)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 Filament
find_package(filament REQUIRED)

# 示例 1: 基础使用
add_executable(separate_loading_basic
    separate_loading_basic.cpp
)
target_link_libraries(separate_loading_basic
    filament
    gltfio_ext
    utils
)

# 示例 2: 批量加载
add_executable(separate_loading_animpack
    separate_loading_animpack.cpp
)
target_link_libraries(separate_loading_animpack
    filament
    gltfio_ext
    utils
)

# 示例 3: 动画混合
add_executable(separate_loading_blender
    separate_loading_blender.cpp
)
target_link_libraries(separate_loading_blender
    filament
    gltfio_ext
    utils
)

# 示例 4: 多角色
add_executable(separate_loading_multi_char
    separate_loading_multi_char.cpp
)
target_link_libraries(separate_loading_multi_char
    filament
    gltfio_ext
    utils
)

# 示例 5: 替换装备
add_executable(separate_loading_swap_equipment
    separate_loading_swap_equipment.cpp
)
target_link_libraries(separate_loading_swap_equipment
    filament
    gltfio_ext
    utils
)
```

### 编译和运行

```bash
# 创建构建目录
mkdir build && cd build

# 配置
cmake -DCMAKE_PREFIX_PATH=/path/to/filament ..

# 编译
cmake --build .

# 运行示例 1
./separate_loading_basic \
    ../assets/character_skeleton.glb \
    ../assets/character_mesh.glb \
    ../assets/walk_animation.glb

# 运行示例 2（动画包）
./separate_loading_animpack \
    ../assets/character_skeleton.glb \
    ../assets/character_mesh.glb \
    ../assets/locomotion_pack.glb

# 运行示例 3（混合）
./separate_loading_blender \
    ../assets/character_skeleton.glb \
    ../assets/character_mesh.glb \
    ../assets/animation_pack.glb
```

### 准备测试资产

```bash
# 使用 Blender 导出分离的 glTF 文件

# 1. skeleton.glb - 只包含骨骼（Armature）
# 2. mesh.glb - 只包含网格 + BONE_INDICES + BONE_WEIGHTS
# 3. walk.glb - 只包含动画
# 4. locomotion_pack.glb - 包含多个动画（idle, walk, run, jump）

# 或者使用现有的完整 glTF，手动拆分：
# gltf-transform split character.glb --animations
```

---

## 总结

这些示例涵盖了从基础使用到高级功能的完整流程：

1. **示例 1**: 最基础的加载和播放流程
2. **示例 2**: 批量加载减少 IO（性能优化）
3. **示例 3**: 动画混合和过渡（提升表现力）
4. **示例 4**: 共享骨骼（节省内存）
5. **示例 5**: 运行时换装（游戏常见需求）

所有示例都遵循 Filament samples 的风格：
- 不使用 FilamentApp
- 直接管理 Engine/Scene/View
- 完整的清理逻辑
- 可以直接编译运行

需要更多示例？可以参考 `filament/samples/` 目录下的其他示例，了解更多 Filament 特性。
