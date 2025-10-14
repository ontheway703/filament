# Filament 动画播放 API 对比

**对比对象**：
- 原始方式：使用 gltfio 加载完整 glTF 文件
- 新方式：使用 gltfio_ext 分离加载骨骼、网格和动画

---

## 完整代码对比

### 方式 1：Filament 原始 gltfio（一体化加载）

```cpp
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Renderer.h>

#include <gltfio/AssetLoader.h>           // 原始加载器
#include <gltfio/FilamentAsset.h>         // 资产对象
#include <gltfio/ResourceLoader.h>        // 纹理/缓冲加载
#include <gltfio/Animator.h>              // 动画器

using namespace filament;
using namespace filament::gltfio;

int main() {
    // ========== 1. 初始化 Filament ==========
    Engine* engine = Engine::create();
    Scene* scene = engine->createScene();
    View* view = engine->createView();
    Renderer* renderer = engine->createRenderer();
    SwapChain* swapChain = engine->createSwapChain(1280, 720);

    // ========== 2. 创建 gltfio 加载器 ==========
    MaterialProvider* materials = createUbershaderProvider(engine);
    AssetLoader* loader = AssetLoader::create({
        .engine = engine,
        .materials = materials
    });

    // ========== 3. 加载完整 glTF 文件（包含骨骼+网格+动画） ==========
    std::vector<uint8_t> buffer = readFile("character_with_animation.glb");
    FilamentAsset* asset = loader->createAsset(buffer.data(), buffer.size());

    if (!asset) {
        std::cerr << "Failed to load asset!" << std::endl;
        return -1;
    }

    // ========== 4. 加载资源（纹理、几何数据等） ==========
    ResourceLoader* resourceLoader = new ResourceLoader({
        .engine = engine,
        .normalizeSkinningWeights = true
    });

    resourceLoader->asyncBeginLoad(asset);

    // 等待资源加载完成（简化示例）
    while (!resourceLoader->asyncGetLoadProgress() == 1.0f) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // ========== 5. 释放资源（必须在使用后调用） ==========
    asset->releaseSourceData();

    // ========== 6. 创建动画器 ==========
    Animator* animator = asset->getInstance()->getAnimator();

    // 查看有哪些动画
    size_t animCount = animator->getAnimationCount();
    std::cout << "Found " << animCount << " animations:" << std::endl;
    for (size_t i = 0; i < animCount; ++i) {
        std::cout << "  [" << i << "] " << animator->getAnimationName(i)
                  << " (duration: " << animator->getAnimationDuration(i) << "s)" << std::endl;
    }

    // ========== 7. 添加到场景 ==========
    scene->addEntities(asset->getEntities(), asset->getEntityCount());

    // ========== 8. 渲染循环（播放动画） ==========
    auto startTime = std::chrono::high_resolution_clock::now();
    float animTime = 0.0f;
    size_t currentAnim = 0;  // 播放第一个动画

    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(now - startTime).count();
        startTime = now;

        animTime += deltaTime;
        float duration = animator->getAnimationDuration(currentAnim);
        if (animTime > duration) {
            animTime = fmod(animTime, duration);  // 循环播放
        }

        // ⭐ 关键调用 1：应用动画（更新 TransformManager）
        animator->applyAnimation(currentAnim, animTime);

        // ⭐ 关键调用 2：更新骨骼矩阵（调用 RenderableManager::setBones）
        animator->updateBoneMatrices();

        // 渲染
        if (renderer->beginFrame(swapChain)) {
            renderer->render(view);
            renderer->endFrame();
        }
    }

    // ========== 9. 清理 ==========
    scene->removeEntities(asset->getEntities(), asset->getEntityCount());

    loader->destroyAsset(asset);
    AssetLoader::destroy(&loader);
    delete resourceLoader;
    materials->destroyMaterials();
    delete materials;

    engine->destroy(renderer);
    engine->destroy(view);
    engine->destroy(scene);
    engine->destroy(swapChain);
    Engine::destroy(&engine);

    return 0;
}
```

**原始方式总结**：
- ✅ 一次加载所有内容（简单）
- ✅ 自动处理所有实体和关系
- ❌ 无法分离加载（必须把骨骼+网格+动画放在一个文件）
- ❌ 无法多个网格共享同一个骨骼
- ❌ 动画与资产绑定（不能用其他文件的动画）

---

### 方式 2：新设计 gltfio_ext（分离加载）

```cpp
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Renderer.h>

#include <gltfio/AssetLoaderExt.h>        // ⭐ 新增：扩展加载器
#include <gltfio/SkeletonAsset.h>         // ⭐ 新增：骨骼资产
#include <gltfio/MeshAsset.h>             // ⭐ 新增：网格资产
#include <gltfio/AnimationAsset.h>        // ⭐ 新增：动画资产
#include <gltfio/StandaloneAnimator.h>    // ⭐ 新增：独立动画器

using namespace filament;
using namespace filament::gltfio;

int main() {
    // ========== 1. 初始化 Filament ==========
    Engine* engine = Engine::create();
    Scene* scene = engine->createScene();
    View* view = engine->createView();
    Renderer* renderer = engine->createRenderer();
    SwapChain* swapChain = engine->createSwapChain(1280, 720);

    // ========== 2. 创建扩展加载器 ==========
    MaterialProvider* materials = createUbershaderProvider(engine);
    AssetLoaderExt* loader = AssetLoaderExt::create({
        .engine = engine,
        .materials = materials
    });

    // ========== 3. 分离加载骨骼、网格、动画 ==========

    // 3.1 加载骨骼（从 skeleton.glb）
    std::vector<uint8_t> skelData = readFile("character_skeleton.glb");
    SkeletonAsset* skeleton = loader->loadSkeleton(skelData.data(), skelData.size());

    if (!skeleton) {
        std::cerr << "Failed to load skeleton!" << std::endl;
        return -1;
    }
    std::cout << "Loaded skeleton: " << skeleton->getBoneCount() << " bones" << std::endl;

    // 3.2 加载网格（从 mesh.glb）
    std::vector<uint8_t> meshData = readFile("character_mesh.glb");
    MeshAsset* mesh = loader->loadMesh(meshData.data(), meshData.size());

    if (!mesh) {
        std::cerr << "Failed to load mesh!" << std::endl;
        return -1;
    }

    // 3.3 绑定骨骼到网格
    if (!mesh->bindSkeleton(skeleton)) {
        std::cerr << "Failed to bind skeleton to mesh!" << std::endl;
        return -1;
    }
    std::cout << "Mesh bound to skeleton" << std::endl;

    // 3.4 加载动画（从 animation.glb）
    std::vector<uint8_t> animData = readFile("walk_animation.glb");
    AnimationAsset* walkAnim = loader->loadAnimation(animData.data(), animData.size());

    if (!walkAnim) {
        std::cerr << "Failed to load animation!" << std::endl;
        return -1;
    }
    std::cout << "Loaded animation: " << walkAnim->getName()
              << " (duration: " << walkAnim->getDuration() << "s)" << std::endl;

    // ========== 4. 创建独立动画器 ==========
    StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
    animator->bindSkeleton(skeleton);
    animator->playAnimation(walkAnim, 1.0f, true);  // 权重 1.0，循环播放

    // ========== 5. 添加到场景 ==========
    scene->addEntity(mesh->getRenderableEntity());

    // ========== 6. 渲染循环（播放动画） ==========
    auto startTime = std::chrono::high_resolution_clock::now();

    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(now - startTime).count();
        startTime = now;

        // ⭐ 关键调用 1：更新动画器（计算骨骼变换）
        animator->update(deltaTime);

        // ⭐ 关键调用 2：更新网格蒙皮（上传到 GPU）
        mesh->updateSkinning(animator);

        // 渲染
        if (renderer->beginFrame(swapChain)) {
            renderer->render(view);
            renderer->endFrame();
        }
    }

    // ========== 7. 清理 ==========
    scene->removeEntity(mesh->getRenderableEntity());

    StandaloneAnimator::destroy(&animator);
    loader->destroyAnimation(walkAnim);
    loader->destroyMesh(mesh);
    loader->destroySkeleton(skeleton);
    AssetLoaderExt::destroy(&loader);

    materials->destroyMaterials();
    delete materials;

    engine->destroy(renderer);
    engine->destroy(view);
    engine->destroy(scene);
    engine->destroy(swapChain);
    Engine::destroy(&engine);

    return 0;
}
```

**新方式总结**：
- ✅ 分离加载（骨骼、网格、动画可以来自不同文件）
- ✅ 一个骨骼可以驱动多个网格
- ✅ 运行时自由组合
- ❌ 需要手动管理绑定关系

---

## 核心 API 对比表

| 功能 | Filament 原始 gltfio | 新设计 gltfio_ext | 变化 |
|------|---------------------|------------------|------|
| **加载器** | `AssetLoader` | `AssetLoaderExt` | ⭐ 新增 |
| **资产类型** | `FilamentAsset`（一体化） | `SkeletonAsset` + `MeshAsset` + `AnimationAsset` | ⭐ 新增分离类型 |
| **动画器** | `Animator`（绑定到 FilamentAsset） | `StandaloneAnimator`（独立） | ⭐ 新增独立动画器 |
| **加载接口** | `loader->createAsset(bytes, size)` | `loader->loadSkeleton()`<br>`loader->loadMesh()`<br>`loader->loadAnimation()` | ⭐ 新增分离接口 |
| **绑定骨骼** | 自动（文件中已定义） | `mesh->bindSkeleton(skeleton)` | ⭐ 新增手动绑定 |
| **播放动画** | `animator->applyAnimation(index, time)` | `animator->playAnimation(anim, weight, loop)` | ⭐ 改进：支持权重和循环 |
| **更新骨骼** | `animator->updateBoneMatrices()` | `mesh->updateSkinning(animator)` | ⭐ 改进：支持多网格 |
| **资源加载** | `ResourceLoader`（异步） | 内置（同步） | 简化 |
| **动画查询** | `animator->getAnimationCount()`<br>`animator->getAnimationName(index)` | `animationAsset->getName()`<br>`animationAsset->getDuration()` | ⭐ 改进：对象化 |

---

## 渲染循环中的核心调用对比

### Filament 原始方式

```cpp
while (rendering) {
    // 计算当前动画时间
    animTime += deltaTime;
    if (animTime > duration) {
        animTime = fmod(animTime, duration);
    }

    // ⭐ 步骤 1：应用动画（根据时间插值）
    animator->applyAnimation(animationIndex, animTime);

    // ⭐ 步骤 2：更新骨骼矩阵
    animator->updateBoneMatrices();

    // 渲染
    renderer->render(view);
}
```

**特点**：
- 需要手动管理动画时间
- 使用索引引用动画（`animationIndex`）
- 无法直接支持动画混合（需要 `applyCrossFade`）

---

### 新设计方式

```cpp
while (rendering) {
    // ⭐ 步骤 1：更新动画器（自动推进时间）
    animator->update(deltaTime);

    // ⭐ 步骤 2：更新网格蒙皮
    mesh->updateSkinning(animator);

    // 渲染
    renderer->render(view);
}
```

**特点**：
- 时间管理自动化（内部推进）
- 使用对象引用动画（`AnimationAsset*`）
- 支持多个网格复用同一个 animator

---

## 新增内容总结

### 1. 新增的类

| 类名 | 作用 | 对应原始概念 |
|------|------|-------------|
| `AssetLoaderExt` | 扩展加载器 | `AssetLoader` |
| `SkeletonAsset` | 骨骼资产 | `FilamentAsset::skin` |
| `MeshAsset` | 网格资产 | `FilamentAsset::renderables` |
| `AnimationAsset` | 动画资产 | `FilamentAsset::animations` |
| `StandaloneAnimator` | 独立动画器 | `Animator` |
| `AnimationPack` ⭐ | 动画包（批量加载） | 无（新增） |
| `AnimationBlender` ⭐ | 动画混合器 | 无（新增） |

### 2. 新增的加载方法

```cpp
// 原始方式：一次加载所有
FilamentAsset* asset = loader->createAsset(bytes, size);

// 新方式：分离加载
SkeletonAsset* skeleton = loader->loadSkeleton(bytes, size);
MeshAsset* mesh = loader->loadMesh(bytes, size);
AnimationAsset* anim = loader->loadAnimation(bytes, size);
AnimationPack* pack = loader->loadAnimationPack(bytes, size);  // ⭐ 批量加载
```

### 3. 新增的绑定方法

```cpp
// 原始方式：文件中预定义关系
// （无需代码）

// 新方式：运行时绑定
mesh->bindSkeleton(skeleton);       // 网格绑定骨骼
animator->bindSkeleton(skeleton);   // 动画器绑定骨骼
```

### 4. 新增的动画控制方法

```cpp
// 原始方式
animator->applyAnimation(index, time);
animator->applyCrossFade(prevIndex, prevTime, alpha);

// 新方式
animator->playAnimation(anim, weight, loop);        // 播放动画
animator->stopAnimation(animId);                    // 停止动画
animator->setAnimationWeight(animId, weight);       // 设置权重

// ⭐ 新增：AnimationBlender（简化版）
blender->playAnimation(anim, transitionDuration);   // 自动过渡
blender->blendAnimations(anim1, anim2, factor);     // 混合两个动画
blender->addAdditiveAnimation(anim, weight);        // 附加动画
```

### 5. 新增的更新方法

```cpp
// 原始方式
animator->applyAnimation(index, time);
animator->updateBoneMatrices();

// 新方式
animator->update(deltaTime);        // ⭐ 自动管理时间
mesh->updateSkinning(animator);     // ⭐ 支持多网格

// ⭐ 使用 AnimationBlender
blender->update(deltaTime);         // 一步完成所有更新
```

---

## 高级用例对比

### 用例 1：一个骨骼驱动多个网格（装备系统）

**原始方式**：❌ 不支持（FilamentAsset 是一体化的）

**新方式**：
```cpp
SkeletonAsset* skeleton = loader->loadSkeleton("skeleton.glb");

MeshAsset* body = loader->loadMesh("body.glb");
MeshAsset* armor = loader->loadMesh("armor.glb");
MeshAsset* weapon = loader->loadMesh("weapon.glb");

body->bindSkeleton(skeleton);
armor->bindSkeleton(skeleton);
weapon->bindSkeleton(skeleton);

StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
animator->bindSkeleton(skeleton);
animator->playAnimation(walkAnim);

// 渲染循环
while (rendering) {
    animator->update(deltaTime);

    body->updateSkinning(animator);    // ⭐ 同一个 animator
    armor->updateSkinning(animator);   // ⭐ 驱动多个网格
    weapon->updateSkinning(animator);

    renderer->render(view);
}
```

---

### 用例 2：运行时更换动画（从不同文件）

**原始方式**：需要重新加载整个资产
```cpp
// 切换动画需要加载新的 glTF 文件
loader->destroyAsset(currentAsset);
currentAsset = loader->createAsset(newGltfBytes, size);
// 需要重新设置场景、animator 等
```

**新方式**：直接加载新动画
```cpp
// 初始动画
AnimationAsset* walk = loader->loadAnimation("walk.glb");
animator->playAnimation(walk);

// 运行时切换（不需要重新加载骨骼和网格）
AnimationAsset* run = loader->loadAnimation("run.glb");
animator->playAnimation(run);

// 清理旧动画
loader->destroyAnimation(walk);
```

---

### 用例 3：批量加载多个动画

**原始方式**：只能加载文件中的所有动画（无法选择）
```cpp
FilamentAsset* asset = loader->createAsset("character_with_20_animations.glb");
Animator* animator = asset->getInstance()->getAnimator();

// 通过索引访问
animator->applyAnimation(0, time);  // idle
animator->applyAnimation(1, time);  // walk
// ... 需要记住每个索引对应的动画
```

**新方式**：使用 AnimationPack
```cpp
// ⭐ 一次读取文件，加载所有动画
AnimationPack* pack = loader->loadAnimationPack("locomotion_pack.glb");

// 通过名称查找
AnimationAsset* idle = pack->findAnimation("idle");
AnimationAsset* walk = pack->findAnimation("walk");
AnimationAsset* run = pack->findAnimation("run");

// 使用
animator->playAnimation(walk);

// 清理（自动销毁所有动画）
loader->destroyAnimationPack(pack);
```

---

### 用例 4：平滑动画过渡

**原始方式**：手动计算混合
```cpp
int idleId = 0, walkId = 1;
float transitionTime = 0.0f;
const float TRANSITION_DURATION = 0.3f;

while (transitioning) {
    transitionTime += deltaTime;
    float alpha = std::min(transitionTime / TRANSITION_DURATION, 1.0f);

    // 手动计算过渡
    animator->applyAnimation(idleId, idleTime);
    animator->applyCrossFade(walkId, walkTime, alpha);
    animator->updateBoneMatrices();
}
```

**新方式**：使用 AnimationBlender
```cpp
AnimationBlender* blender = AnimationBlender::create(animator);

// ⭐ 一行代码完成过渡（自动管理权重和时间）
blender->playAnimation(idleAnim, 0.3f);  // 0.3秒过渡到 idle
blender->playAnimation(walkAnim, 0.5f);  // 0.5秒过渡到 walk

// 每帧只需调用一次
while (rendering) {
    blender->update(deltaTime);  // 自动处理过渡
    mesh->updateSkinning(animator);
}
```

---

## 复杂度对比

| 任务 | Filament 原始 | 新设计 gltfio_ext | 优势 |
|------|--------------|------------------|------|
| 基础播放 | 简单（3 行） | 简单（2 行） | 相当 |
| 分离加载 | ❌ 不支持 | ✅ 支持 | 新功能 |
| 多网格共享骨骼 | ❌ 不支持 | ✅ 3 行代码 | 新功能 |
| 运行时换动画 | 复杂（重新加载） | 简单（1 行） | 大幅简化 |
| 批量加载 20 个动画 | 20 次文件 IO | 1 次文件 IO | 性能提升 20x |
| 动画过渡 | 复杂（手动计算） | 简单（1 行） | 大幅简化 |
| 装备系统 | ❌ 需要 hack | ✅ 原生支持 | 新功能 |

---

## 迁移检查清单

从原始 gltfio 迁移到 gltfio_ext：

```diff
- #include <gltfio/AssetLoader.h>
- #include <gltfio/FilamentAsset.h>
- #include <gltfio/ResourceLoader.h>
- #include <gltfio/Animator.h>

+ #include <gltfio/AssetLoaderExt.h>
+ #include <gltfio/SkeletonAsset.h>
+ #include <gltfio/MeshAsset.h>
+ #include <gltfio/AnimationAsset.h>
+ #include <gltfio/StandaloneAnimator.h>
+ #include <gltfio/AnimationBlender.h>    // 可选

- AssetLoader* loader = AssetLoader::create(config);
+ AssetLoaderExt* loader = AssetLoaderExt::create(config);

- FilamentAsset* asset = loader->createAsset(bytes, size);
+ SkeletonAsset* skeleton = loader->loadSkeleton(skelBytes, skelSize);
+ MeshAsset* mesh = loader->loadMesh(meshBytes, meshSize);
+ AnimationAsset* anim = loader->loadAnimation(animBytes, animSize);

- Animator* animator = asset->getInstance()->getAnimator();
+ StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
+ animator->bindSkeleton(skeleton);

- animator->applyAnimation(index, time);
- animator->updateBoneMatrices();
+ animator->playAnimation(anim);
+ animator->update(deltaTime);
+ mesh->updateSkinning(animator);

- loader->destroyAsset(asset);
+ loader->destroyAnimation(anim);
+ loader->destroyMesh(mesh);
+ loader->destroySkeleton(skeleton);
+ StandaloneAnimator::destroy(&animator);
```

---

## 总结

### Filament 原始方式的限制
1. **一体化加载**：骨骼、网格、动画必须在同一个 glTF 文件
2. **无法共享骨骼**：每个资产独立，无法多角色共享
3. **动画绑定死**：动画只能来自同一个文件
4. **手动时间管理**：需要自己计算 `animTime`
5. **复杂的混合逻辑**：需要手动计算权重和过渡

### 新设计的优势
1. ✅ **分离加载**：骨骼、网格、动画可以来自不同文件
2. ✅ **骨骼复用**：一个骨骼驱动多个网格（装备系统）
3. ✅ **动画自由组合**：运行时加载和切换任意动画
4. ✅ **自动时间管理**：`update(deltaTime)` 自动推进
5. ✅ **批量加载优化**：`AnimationPack` 减少文件 IO
6. ✅ **简化混合**：`AnimationBlender` 一行代码实现过渡

### 新增内容总结
- **7 个新类**：SkeletonAsset, MeshAsset, AnimationAsset, StandaloneAnimator, AssetLoaderExt, AnimationPack, AnimationBlender
- **5 个新加载方法**：loadSkeleton, loadMesh, loadAnimation, loadAnimationPack, destroyXxx
- **3 个新绑定方法**：bindSkeleton, playAnimation, updateSkinning
- **1 个新混合器**：AnimationBlender（简化 90% 的混合代码）

新设计是原始 gltfio 的**增强版**，保留了核心概念（Animator, applyAnimation, updateBoneMatrices），但增加了灵活性和易用性。
