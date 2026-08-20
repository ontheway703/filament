# Filament gltfio_ext Animation Samples 设计计划

## 1. 项目概述

### 1.1 目标

创建一系列 samples 来全面验证 `gltfio_ext` 库的核心功能：
- **外部动画加载**：从独立的 GLB 文件加载动画数据
- **运行时动态管理**：热加载/卸载动画资源
- **多实例共享**：多个 Animator 共享同一个 AnimationAsset
- **生命周期管理**：验证资源的正确创建和销毁

### 1.2 设计原则

- **独立性**：在 `samples/animation/` 目录独立组织，与 Filament 原生 samples 区分
- **完整性**：覆盖 gltfio_ext 的所有核心 API 和典型使用场景
- **实用性**：每个 sample 聚焦一个核心场景，可作为实际应用参考

### 1.3 技术背景

gltfio_ext 实现了动画与网格的解耦：

**Phase 1 完成**：
- `AnimationAsset`：轻量级动画数据容器
- `AnimationBinding`：基于骨骼名称的绑定机制（90% 匹配阈值）
- `AssetLoader::loadAnimationAsset()`：加载纯动画 GLB

**Phase 2 完成**：
- `Animator::loadExternalAnimation()`：运行时加载外部动画
- `Animator::unloadExternalAnimation()`：运行时卸载外部动画
- `Animator::hasExternalAnimation()`：状态查询

**核心优势**：
- 网格常驻内存（82 MB），动画按需加载（仅 679 KB）
- 内存占用降低 99%（679 KB vs 83 MB）
- 支持动画热切换，无需重新加载网格

---

## 2. 目录结构

```
samples/animation/
├── PLAN.md                           # 本文档
├── CMakeLists.txt                    # 构建配置
├── external_animation.cpp            # Sample 1: 基础工作流
├── animation_hotreload.cpp           # Sample 2: 运行时动态管理
├── multi_character_animation.cpp     # Sample 3: 多实例共享
└── common/                           # 共享工具
    ├── AnimationUtils.h
    └── AnimationUtils.cpp
```

**资源文件**：使用 `/Users/xuan/Desktop/Code/my/filament/tmp/test_assets/`
- `ecorche_full.glb` (83 MB) - 完整模型（网格+骨骼+动画）
- `ecorche_mesh_only.glb` (82 MB) - 仅网格和骨骼
- `ecorche_animation_only.glb` (679 KB) - 仅动画数据

---

## 3. Sample 设计思路和覆盖矩阵

### 3.1 设计思路

每个 sample 聚焦特定场景，覆盖不同的 API 组合和用例：

| Sample | 核心场景 | 设计目标 | 验证重点 |
|--------|---------|---------|---------|
| **external_animation** | 基础工作流 | 演示完整的加载-绑定-播放流程 | API 正确性、动画索引管理 |
| **animation_hotreload** | 运行时管理 | 演示动态加载/卸载能力 | 生命周期健壮性、内存安全 |
| **multi_character_animation** | 资源共享 | 演示多 Animator 共享 AnimationAsset | 并发安全、内存效率、性能 |

### 3.2 API 覆盖矩阵

| API | Sample 1 | Sample 2 | Sample 3 | 说明 |
|-----|----------|----------|----------|------|
| **AssetLoader::create()** | ✅ | ✅ | ✅ | 创建加载器 |
| **AssetLoader::loadAnimationAsset()** | ✅ | ✅ | ✅ | 加载动画资源 |
| **AssetLoader::destroyAnimationAsset()** | ✅ | ✅ | ✅ | 销毁动画资源 |
| **Animator::loadExternalAnimation()** | ✅ | ✅ | ✅ | 绑定外部动画 |
| **Animator::unloadExternalAnimation()** | ✅ | ✅ | ✅ | 卸载外部动画 |
| **Animator::hasExternalAnimation()** | ✅ | ✅ | ✅ | 查询状态 |
| **Animator::getAnimationCount()** | ✅ | ✅ | ✅ | 获取总数（内部+外部） |
| **Animator::applyAnimation()** | ✅ | ✅ | ✅ | 应用动画 |
| **Animator::updateBoneMatrices()** | ✅ | ✅ | ✅ | 更新骨骼 |
| **AnimationAsset::getAnimationCount()** | ✅ | ✅ | ✅ | 查询动画数量 |
| **多次 load/unload 循环** | ❌ | ✅ | ❌ | 验证内存安全 |
| **多 Animator 共享** | ❌ | ❌ | ✅ | 验证资源共享 |

### 3.3 Sample 1: external_animation.cpp

**设计目标**：演示完整的外部动画加载工作流

**使用资源**：
- `ecorche_mesh_only.glb` - 仅网格（模拟常驻内存的网格资源）
- `ecorche_animation_only.glb` - 外部动画（按需加载）

**为什么这样设计**：
- 使用 `mesh_only` 而非 `full`，清晰展示"网格常驻 + 动画按需"的模式
- 单次加载流程，聚焦基础 API 的正确使用
- 用户交互（数字键切换动画），验证动画索引管理

**核心 API 调用序列**：
```cpp
// Setup 阶段
gltfio_ext::AssetLoader* animLoader = gltfio_ext::AssetLoader::create(engine);
AnimationAsset* animAsset = animLoader->loadAnimationAsset(bytes, size);

Animator* animator = meshAsset->getInstance()->getAnimator();
size_t internalCount = animator->getAnimationCount();  // 记录内部动画数量

bool success = animator->loadExternalAnimation(animAsset);
// 此时 animator->getAnimationCount() == internalCount + externalCount

// Animate 阶段
animator->applyAnimation(currentIndex, time);
animator->updateBoneMatrices();

// Cleanup 阶段
animator->unloadExternalAnimation();
animLoader->destroyAnimationAsset(animAsset);
AssetLoader::destroy(&animLoader);
```

**验证点**：
- 加载后动画总数正确（内部 + 外部）
- 可以切换播放所有动画（内部索引 [0, N)，外部索引 [N, N+M)）
- 显示正确的动画元数据（名称、时长）

### 3.4 Sample 2: animation_hotreload.cpp

**设计目标**：演示运行时动态加载/卸载的健壮性

**使用资源**：
- `ecorche_mesh_only.glb` - 网格
- `ecorche_animation_only.glb` - 可动态加载/卸载的动画

**为什么这样设计**：
- 初始状态不加载外部动画，通过交互触发加载
- 支持多次 load/unload 循环，验证内存管理
- 卸载后验证状态回退（动画总数恢复、索引调整）

**核心 API 调用序列**：
```cpp
// 初始 Setup：只加载网格，不加载外部动画
Animator* animator = meshAsset->getInstance()->getAnimator();
size_t internalCount = animator->getAnimationCount();  // 仅内部动画

// 按 'R' 键：动态加载外部动画
AnimationAsset* animAsset = animLoader->loadAnimationAsset(bytes, size);
bool success = animator->loadExternalAnimation(animAsset);
// 此时 getAnimationCount() 增加

// 按 'U' 键：动态卸载外部动画
animator->unloadExternalAnimation();
animLoader->destroyAnimationAsset(animAsset);
// 此时 getAnimationCount() 恢复为 internalCount

// 支持多次循环
for (int cycle = 0; cycle < 100; cycle++) {
    animAsset = animLoader->loadAnimationAsset(bytes, size);
    animator->loadExternalAnimation(animAsset);
    // ... 使用动画 ...
    animator->unloadExternalAnimation();
    animLoader->destroyAnimationAsset(animAsset);
}
```

**验证点**：
- 初始无外部动画，`hasExternalAnimation()` 返回 false
- 加载后 `hasExternalAnimation()` 返回 true
- 卸载后状态正确恢复
- 多次循环无内存泄漏
- 卸载后当前播放索引正确处理（如索引 >= internalCount，则回退到 0）

### 3.5 Sample 3: multi_character_animation.cpp

**设计目标**：演示多个 Animator 共享同一个 AnimationAsset

**使用资源**：
- `ecorche_mesh_only.glb` - 加载 5 次，创建 5 个角色实例
- `ecorche_animation_only.glb` - 仅加载 1 次，被所有 Animator 共享

**为什么这样设计**：
- 验证 gltfio_ext 的核心优势：资源共享
- 内存对比：
  - **传统方式**（每个角色独立 full.glb）：5 × 83 MB = 415 MB
  - **gltfio_ext 方式**（共享动画）：5 × 82 MB + 1 × 679 KB ≈ 410 MB，节约 5 MB（仅动画部分）
- 性能测试：快速切换动画，验证无竞态条件

**核心 API 调用序列**：
```cpp
// Setup：创建多个角色
std::vector<Animator*> animators;
for (int i = 0; i < 5; i++) {
    FilamentAsset* meshAsset = meshLoader->createAsset(meshBytes, meshSize);
    Animator* animator = meshAsset->getInstance()->getAnimator();
    animators.push_back(animator);
}

// 加载共享的动画资源（只加载一次）
AnimationAsset* sharedAnimAsset = animLoader->loadAnimationAsset(animBytes, animSize);

// 所有 Animator 绑定同一个 AnimationAsset
for (Animator* animator : animators) {
    animator->loadExternalAnimation(sharedAnimAsset);
}

// Animate：每个角色独立播放
for (int i = 0; i < animators.size(); i++) {
    animators[i]->applyAnimation(indices[i], times[i]);
    animators[i]->updateBoneMatrices();
}

// Cleanup：先卸载，再销毁（只销毁一次）
for (Animator* animator : animators) {
    animator->unloadExternalAnimation();
}
animLoader->destroyAnimationAsset(sharedAnimAsset);  // 只销毁一次
```

**验证点**：
- 所有 Animator 成功加载同一个 AnimationAsset
- 可以独立播放不同动画（独立模式）
- 可以同步播放相同动画（同步模式）
- 性能测试：每帧随机切换 5 个角色的动画，持续 10 秒，统计切换次数
- 内存验证：通过 Instruments/Valgrind 确认动画数据只有一份

---

## 4. 核心 API 调用示例

### 4.1 基础工作流（Sample 1）

```cpp
// 1. 创建加载器
auto* meshLoader = gltfio::AssetLoader::create({engine});
auto* animLoader = gltfio_ext::AssetLoader::create(engine);

// 2. 加载网格
FilamentAsset* meshAsset = meshLoader->createAsset(meshBytes, meshSize);
Animator* animator = meshAsset->getInstance()->getAnimator();

// 3. 记录内部动画数量
size_t internalCount = animator->getAnimationCount();

// 4. 加载外部动画
AnimationAsset* animAsset = animLoader->loadAnimationAsset(animBytes, animSize);

// 5. 绑定到 Animator
bool success = animator->loadExternalAnimation(animAsset);

// 6. 验证总数
size_t totalCount = animator->getAnimationCount();
// totalCount == internalCount + animAsset->getAnimationCount()

// 7. 播放动画
animator->applyAnimation(index, time);
animator->updateBoneMatrices();

// 8. 清理
animator->unloadExternalAnimation();
animLoader->destroyAnimationAsset(animAsset);
```

### 4.2 运行时热重载（Sample 2）

```cpp
// 初始状态：无外部动画
assert(!animator->hasExternalAnimation());
size_t count = animator->getAnimationCount();  // 仅内部动画

// 运行时加载
AnimationAsset* animAsset = animLoader->loadAnimationAsset(bytes, size);
animator->loadExternalAnimation(animAsset);
assert(animator->hasExternalAnimation());
assert(animator->getAnimationCount() > count);

// 运行时卸载
animator->unloadExternalAnimation();
animLoader->destroyAnimationAsset(animAsset);
assert(!animator->hasExternalAnimation());
assert(animator->getAnimationCount() == count);
```

### 4.3 多实例共享（Sample 3）

```cpp
// 创建多个 Animator
std::vector<Animator*> animators;
for (int i = 0; i < 5; i++) {
    FilamentAsset* asset = meshLoader->createAsset(meshBytes, meshSize);
    animators.push_back(asset->getInstance()->getAnimator());
}

// 加载一个动画资源
AnimationAsset* sharedAnim = animLoader->loadAnimationAsset(animBytes, animSize);

// 共享给所有 Animator
for (Animator* anim : animators) {
    anim->loadExternalAnimation(sharedAnim);
}

// 独立播放
for (int i = 0; i < animators.size(); i++) {
    animators[i]->applyAnimation(randomIndices[i], times[i]);
    animators[i]->updateBoneMatrices();
}
```

---

## 5. 共享工具设计

### 5.1 AnimationUtils 核心功能

```cpp
namespace animation_utils {

// 资源路径解析（支持 tmp/test_assets/）
std::string getAssetPath(const std::string& filename);

// 加载网格（带错误处理）
gltfio::FilamentAsset* loadMeshAsset(
    gltfio::AssetLoader* loader,
    const char* path
);

// 加载动画（带错误处理）
gltfio_ext::AnimationAsset* loadAnimationAsset(
    gltfio_ext::AssetLoader* loader,
    const char* path
);

// 相机自适应设置
void setupCamera(filament::View* view, gltfio::FilamentAsset* asset);

} // namespace animation_utils
```

### 5.2 资源路径解析策略

```cpp
std::string getAssetPath(const std::string& filename) {
    std::vector<std::string> searchPaths = {
        filename,  // 直接路径
        "tmp/test_assets/" + filename,
        "../tmp/test_assets/" + filename,
        "../../tmp/test_assets/" + filename,
        "/Users/xuan/Desktop/Code/my/filament/tmp/test_assets/" + filename
    };

    for (const auto& path : searchPaths) {
        if (fileExists(path)) return path;
    }

    return "";  // 未找到
}
```

---

## 6. 构建配置

### 6.1 samples/animation/CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.19)

# 共享工具库
add_library(animation-common STATIC common/AnimationUtils.cpp)
target_link_libraries(animation-common PUBLIC gltfio_ext gltfio filament utils)

# Sample 构建函数
function(add_animation_demo NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE
        animation-common
        gltfio_ext
        gltfio
        viewer
        filamentapp
        sample-resources
    )
endfunction()

# 添加所有 Samples
add_animation_demo(external_animation)
add_animation_demo(animation_hotreload)
add_animation_demo(multi_character_animation)
```

### 6.2 修改 samples/CMakeLists.txt

在文件末尾添加：

```cmake
# gltfio_ext animation samples
if(NOT WEBGL AND NOT ANDROID AND NOT IOS)
    add_subdirectory(animation)
endif()
```

---

## 7. 验证标准

### 7.1 功能验证

| Sample | 验证项 |
|--------|--------|
| **external_animation** | ✅ 加载网格和外部动画<br>✅ 动画总数正确<br>✅ 切换所有动画<br>✅ 元数据正确 |
| **animation_hotreload** | ✅ 初始无外部动画<br>✅ 运行时加载成功<br>✅ 运行时卸载成功<br>✅ 多次循环无泄漏 |
| **multi_character_animation** | ✅ 5 个角色加载成功<br>✅ 共享一个 AnimationAsset<br>✅ 独立/同步模式<br>✅ 性能测试通过 |

### 7.2 性能基准

- **内存**：多角色示例中，动画数据只有一份（通过 Instruments 验证）
- **帧率**：5 角色同时播放不同动画，保持 60 FPS
- **切换**：单次动画切换 < 1ms（参考 test_animator.cpp）

### 7.3 健壮性验证

- **资源缺失**：优雅处理 GLB 文件不存在
- **重复操作**：100 次 load/unload 循环无崩溃
- **内存泄漏**：Valgrind/Instruments 检测通过

---

## 8. 参考资料

- **gltfio_ext API**: `libs/gltfio_ext/include/gltfio_ext/`
- **测试代码**: `libs/gltfio_ext/test/test_animator.cpp`
- **Filament samples**: `samples/gltf_viewer.cpp`
- **资源来源**: `/Users/xuan/Desktop/Code/my/filament/tmp/test_assets/`

---

**文档版本**: 2.0
**创建日期**: 2025-10-29
**维护者**: gltfio_ext Team
