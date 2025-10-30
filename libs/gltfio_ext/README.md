# gltfio_ext - Extended glTF Animation Loading

## 概述

`gltfio_ext` 是 Filament 的 glTF 加载库扩展，专注于**动画与网格解耦**的场景。它允许：

- **分离式资产管理**：网格几何体和骨骼动画存储在不同的 GLB 文件中
- **动画复用**：一套网格可以应用多套外部动画（如 idle、walk、run）
- **轻量级加载**：仅加载动画数据（骨骼层级 + 关键帧），无需加载完整的网格和材质
- **多动画支持**：单个 GLB 文件可包含多个动画片段，一次性全部加载

**典型工作流**：
1. Blender 导出网格 GLB（包含完整的蒙皮网格和材质）
2. Blender 导出纯动画 GLB（`export_meshes=False`，仅包含骨骼 + 动画）
3. 应用中加载网格 → 加载多个动画 → 通过骨骼名称绑定 → 播放动画

## 当前进度

### ✅ Phase 1: 多动画资产支持（已完成）

核心组件已实现并通过完整测试：

- **AnimationAsset**：轻量级动画容器，支持多个动画片段（共享骨骼层级）
- **AssetLoader**：从 GLB 文件加载所有动画，支持销毁动画资产
- **AnimationBinding**：骨骼名称映射（外部动画 ↔ 驻留网格实体），支持 90% 匹配率阈值

### ✅ Phase 2: Animator 基础扩展（已完成）

Animator 已扩展支持外部动画加载：

- **loadExternalAnimation()**：加载外部 AnimationAsset 的所有动画
- **unloadExternalAnimation()**：卸载外部动画并释放资源
- **hasExternalAnimation()**：检查是否已加载外部动画
- **生命周期管理**：安全的加载/卸载机制，支持动画替换

**测试覆盖**：**61 个测试用例** (23 + 6 + 21 + 12 - 1)，涵盖：
- 单元测试：数据结构验证、边界检查、错误处理
- 集成测试：完整工作流、多 Animator 同步、快速切换
- 生命周期测试：正确销毁顺序、内存稳定性、状态回滚

### 🔄 Phase 3: 高级播放控制（未来版本）

未来将进一步扩展 Animator 以支持：
- 动画混合（blend/crossfade）
- 播放控制（速度、循环、时间偏移）
- 动画状态机

## 重要前提条件

### ⚠️ 必须启用骨骼名称组件

外部动画系统依赖**骨骼名称匹配**来建立映射关系。必须在 `AssetConfiguration` 中启用 `names` 选项：

```cpp
#include <gltfio_ext/AssetLoader.h>
#include <utils/NameComponentManager.h>

using namespace filament::gltfio_ext;

// 创建 NameComponentManager
auto* nameManager = new NameComponentManager(EntityManager::get());

// 配置 AssetLoader（关键：必须设置 names）
AssetConfiguration config;
config.engine = engine;
config.materials = materialProvider;
config.names = nameManager;  // ⚠️ 必需！用于骨骼名称映射

AssetLoader* assetLoader = AssetLoader::create(config);
```

**如果不设置 `names`**：
- 骨骼匹配率将为 0%（所有骨骼无法匹配）
- `loadExternalAnimation()` 将失败并返回 false
- 错误日志："AnimationBinding: Bone mapping failed with 0% match rate (threshold: 90%)"

## 使用方式

### 1. 加载网格资产（完整的蒙皮网格）

```cpp
#include <gltfio_ext/AssetLoader.h>
#include <gltfio_ext/Animator.h>

using namespace filament::gltfio_ext;

// 加载包含网格和材质的 GLB
FilamentAsset* meshAsset = assetLoader->createAsset(meshData, meshDataSize);
resourceLoader->asyncBeginLoad(meshAsset);

// 等待资源加载完成
while (!resourceLoader->asyncUpdateLoad()) { /* poll */ }

// 添加到场景
scene->addEntities(meshAsset->getEntities(), meshAsset->getEntityCount());
```

### 2. 加载外部动画资产（多个动画片段）

```cpp
// 加载纯动画 GLB（可能包含多个动画）
AnimationAsset* animAsset = assetLoader->loadAnimationAsset(animData, animDataSize);

// 查看包含的动画
size_t animCount = animAsset->getAnimationCount();
for (size_t i = 0; i < animCount; ++i) {
    const auto& anim = animAsset->getAnimation(i);
    utils::slog.i << "Animation " << i << ": " << anim.name
                  << " (duration: " << anim.getDuration() << "s)" << utils::io::endl;
}

// 按名称查找动画
const auto* walkAnim = animAsset->getAnimationByName("walk");
if (walkAnim) {
    utils::slog.i << "Found 'walk' animation with "
                  << walkAnim->channels.size() << " channels" << utils::io::endl;
}
```

### 3. 使用 Animator 加载和播放外部动画

```cpp
// 获取 Animator 实例
FilamentInstance* instance = meshAsset->getInstance();
Animator* animator = instance->getAnimator();

// 记录内部动画数量（嵌入在网格 GLB 中的动画）
size_t internalAnimCount = animator->getAnimationCount();

// 加载外部动画（所有动画一次性加载）
bool success = animator->loadExternalAnimation(animAsset);
if (!success) {
    utils::slog.e << "Failed to load external animation (bone mismatch?)" << utils::io::endl;
    return;
}

// 验证加载状态
assert(animator->hasExternalAnimation());

// 外部动画索引从内部动画数量开始
// 索引范围：[internalAnimCount, internalAnimCount + animAsset->getAnimationCount())
size_t totalAnimCount = animator->getAnimationCount();
utils::slog.i << "Total animations: " << totalAnimCount
              << " (internal: " << internalAnimCount
              << ", external: " << (totalAnimCount - internalAnimCount) << ")"
              << utils::io::endl;

// 播放第一个外部动画
size_t firstExternalAnimIndex = internalAnimCount;
float animDuration = animator->getAnimationDuration(firstExternalAnimIndex);
const char* animName = animator->getAnimationName(firstExternalAnimIndex);

utils::slog.i << "Playing external animation: " << animName
              << " (duration: " << animDuration << "s)" << utils::io::endl;

// 在渲染循环中更新动画
float currentTime = 0.0f;
while (running) {
    currentTime += deltaTime;

    // 应用动画（更新骨骼的局部变换）
    animator->applyAnimation(firstExternalAnimIndex, currentTime);

    // 更新骨骼矩阵（将局部变换传播到世界空间并设置到 RenderableManager）
    animator->updateBoneMatrices();

    // 渲染帧...
}

// 切换到另一个外部动画
if (totalAnimCount > internalAnimCount + 1) {
    size_t secondExternalAnimIndex = internalAnimCount + 1;
    animator->applyAnimation(secondExternalAnimIndex, 0.0f);
    animator->updateBoneMatrices();
}

// 卸载外部动画（释放资源）
animator->unloadExternalAnimation();
assert(!animator->hasExternalAnimation());
assert(animator->getAnimationCount() == internalAnimCount);  // 恢复到内部动画数量

// 销毁动画资产（必须在卸载后）
assetLoader->destroyAnimationAsset(animAsset);
```

### 4. 生命周期管理（正确的销毁顺序）

```cpp
// ⚠️ 重要：正确的销毁顺序

// 1. 卸载 Animator 的外部动画引用
animator->unloadExternalAnimation();

// 2. 销毁 AnimationAsset
assetLoader->destroyAnimationAsset(animAsset);

// 3. 销毁 FilamentAsset（会自动销毁 Animator）
assetLoader->destroyAsset(meshAsset);

// ❌ 错误顺序：先销毁 AnimationAsset，后卸载 Animator
// 这样做是安全的（有保护机制），但会输出警告日志
```

## 常见问题与错误处理

### ❌ 问题 1：骨骼匹配率为 0%

**错误现象**：
```
AnimationBinding: Bone mapping built successfully: 0/327 named nodes matched (0%)
AnimationBinding: Bone mapping failed with 0% match rate (threshold: 90%)
```

**原因**：未设置 `AssetConfiguration.names`

**解决方案**：
```cpp
// 创建 NameComponentManager 并传给 AssetLoader
auto* nameManager = new NameComponentManager(EntityManager::get());

AssetConfiguration config;
config.names = nameManager;  // ⚠️ 必须设置
AssetLoader* loader = AssetLoader::create(config);
```

### ❌ 问题 2：骨骼匹配率低于 90%

**错误现象**：
```
AnimationBinding: Bone mapping built successfully: 250/327 named nodes matched (76%)
AnimationBinding: Bone mapping failed with 76% match rate (threshold: 90%)
```

**可能原因**：
1. 网格和动画来自不同的模型文件（骨骼名称不匹配）
2. Blender 导出时骨骼名称被修改
3. 动画文件的骨骼层级与网格不一致

**解决方案**：
1. 确保网格和动画来自同一个 Blender 文件
2. 检查 Blender 的导出设置，确保骨骼名称一致
3. 使用 `AnimationBinding::getUnmatchedBones()` 查看哪些骨骼未匹配

```cpp
// 获取未匹配的骨骼列表
const auto& unmatchedBones = binding.getUnmatchedBones();
for (size_t nodeIndex : unmatchedBones) {
    const auto& node = animAsset->nodes[nodeIndex];
    utils::slog.w << "Unmatched bone: " << node.name << utils::io::endl;
}
```

### ❌ 问题 3：加载外部动画后程序崩溃

**错误现象**：`applyAnimation()` 或 `updateBoneMatrices()` 后崩溃

**可能原因**：
1. 在 `unloadExternalAnimation()` 后仍尝试播放外部动画
2. `AnimationAsset` 被过早销毁（在 `unloadExternalAnimation()` 之前）

**解决方案**：
```cpp
// 方案 1：检查是否已加载外部动画
if (animator->hasExternalAnimation()) {
    animator->applyAnimation(externalAnimIndex, time);
} else {
    utils::slog.w << "External animation not loaded" << utils::io::endl;
}

// 方案 2：正确的销毁顺序
animator->unloadExternalAnimation();                      // 1. 先卸载
assetLoader->destroyAnimationAsset(animAsset);            // 2. 再销毁资产
assetLoader->destroyAsset(meshAsset);                     // 3. 最后销毁网格
```

### ❌ 问题 4：无效的动画索引

**错误现象**：
```
Invalid animation index: 5
```

**原因**：尝试访问不存在的动画索引

**解决方案**：
```cpp
size_t internalCount = animator->getAnimationCount();  // 假设为 3
animator->loadExternalAnimation(animAsset);            // animAsset 有 2 个动画

size_t totalCount = animator->getAnimationCount();     // 现在为 5

// ✅ 正确：外部动画索引为 [3, 5)
animator->applyAnimation(3, time);  // 第一个外部动画
animator->applyAnimation(4, time);  // 第二个外部动画

// ❌ 错误：索引 5 超出范围
animator->applyAnimation(5, time);  // 崩溃！
```

### ✅ 最佳实践

#### 1. 始终检查返回值

```cpp
// 加载动画资产
AnimationAsset* animAsset = assetLoader->loadAnimationAsset(data, size);
if (!animAsset) {
    utils::slog.e << "Failed to load animation asset (invalid GLB?)" << utils::io::endl;
    return;
}

// 加载外部动画
bool success = animator->loadExternalAnimation(animAsset);
if (!success) {
    utils::slog.e << "Failed to load external animation (bone mismatch?)" << utils::io::endl;
    assetLoader->destroyAnimationAsset(animAsset);
    return;
}
```

#### 2. 使用断言验证状态

```cpp
assert(animator->hasExternalAnimation());
assert(animator->getAnimationCount() == internalCount + externalCount);
```

#### 3. 记录内部动画数量

```cpp
// 在加载外部动画前记录
size_t internalAnimCount = animator->getAnimationCount();

// 加载外部动画后
size_t firstExternalIndex = internalAnimCount;
size_t lastExternalIndex = animator->getAnimationCount() - 1;

// 遍历所有外部动画
for (size_t i = firstExternalIndex; i <= lastExternalIndex; i++) {
    const char* name = animator->getAnimationName(i);
    float duration = animator->getAnimationDuration(i);
    utils::slog.i << "External animation " << (i - firstExternalIndex)
                  << ": " << name << " (" << duration << "s)" << utils::io::endl;
}
```

## 关键 API

### AnimationAsset

```cpp
class AnimationAsset {
public:
    struct Animation {
        std::string name;
        std::vector<AnimationChannel> channels;
        std::vector<AnimationSampler> samplers;

        float getDuration() const;
        bool validate() const;
    };

    // 多动画访问
    size_t getAnimationCount() const;
    const Animation& getAnimation(size_t index) const;
    const Animation* getAnimationByName(const std::string& name) const;

    // 节点查询
    int findNodeByName(const std::string& name) const;
    void buildBoneNameMap();

    // 数据成员
    std::vector<AnimationNode> nodes;           // 骨骼层级（所有动画共享）
    std::vector<Animation> animations;          // 多个动画片段
    std::vector<filament::math::mat4f> inverseBindMatrices;  // 蒙皮数据
    std::vector<int> joints;
};
```

### AssetLoader

```cpp
class AssetLoader {
public:
    // 加载所有动画（不再只加载第一个）
    AnimationAsset* loadAnimationAsset(const uint8_t* data, uint32_t size);

    // 加载完整资产（网格 + 材质）
    FilamentAsset* createAsset(const uint8_t* data, uint32_t size);
};
```

### AnimationBinding

```cpp
class AnimationBinding {
public:
    AnimationBinding(const FilamentAsset* resident, const AnimationAsset* external);

    // 获取映射结果（动画骨骼索引 → 网格变换实例）
    const std::vector<TransformManager::Instance>& getBoneMapping() const;

    // 获取匹配率（0.0 - 1.0）
    float getMatchRate() const;

    // 获取未匹配的骨骼索引列表
    const std::vector<size_t>& getUnmatchedBones() const;
};
```

### Animator (扩展 API)

```cpp
class Animator {
public:
    // ========== 外部动画支持 (Phase 2 已完成) ==========

    /**
     * 加载外部动画资产（所有动画）
     * @param animAsset 外部动画资产（Animator 不持有所有权）
     * @return true 成功，false 失败（骨骼匹配率 < 90%）
     *
     * 注意：
     * - 加载所有动画，索引范围：[N, N+M)
     *   N = 内部动画数量，M = animAsset->getAnimationCount()
     * - 重复调用会替换之前加载的外部动画
     * - animAsset 必须在 Animator 生命周期内保持有效
     */
    bool loadExternalAnimation(AnimationAsset* animAsset);

    /**
     * 卸载外部动画
     * 释放 AnimationBinding 和转换后的动画数据
     * 不影响 animAsset 本身（由调用者负责销毁）
     */
    void unloadExternalAnimation();

    /**
     * 检查是否已加载外部动画
     * @return true 已加载，false 未加载或已卸载
     */
    bool hasExternalAnimation() const;

    // ========== 原有 API（继承自 gltfio） ==========

    /**
     * 应用动画（更新骨骼的局部变换）
     * @param animationIndex 动画索引（内部动画 + 外部动画）
     * @param time 时间（秒）
     */
    void applyAnimation(size_t animationIndex, float time) const;

    /**
     * 更新骨骼矩阵
     * 计算根到骨骼的世界变换，并设置到 RenderableManager
     */
    void updateBoneMatrices();

    /**
     * 应用混合动画（交叉淡入淡出）
     * @param previousAnimIndex 上一个动画索引
     * @param previousAnimTime 上一个动画时间
     * @param alpha 混合系数（0.0 = 完全使用 previous，1.0 = 完全使用当前）
     */
    void applyCrossFade(size_t previousAnimIndex, float previousAnimTime, float alpha);

    /**
     * 获取动画数量（内部 + 外部）
     * @return 总动画数量
     */
    size_t getAnimationCount() const;

    /**
     * 获取动画时长
     * @param animationIndex 动画索引
     * @return 时长（秒），无效索引返回 0.0f
     */
    float getAnimationDuration(size_t animationIndex) const;

    /**
     * 获取动画名称
     * @param animationIndex 动画索引
     * @return 名称字符串，无效索引返回空字符串
     */
    const char* getAnimationName(size_t animationIndex) const;
};
```

## 测试与验证

完整的测试文档请参见 [test/README.md](test/README.md)，包括：
- **61 个测试用例**（4 个测试文件）
- 真实模型测试（ecorche 模型，327 个骨骼，3 个动画）
- 100% 骨骼匹配率验证
- 生命周期安全性测试（正确销毁顺序、状态回滚）
- 集成测试（完整工作流、多 Animator 同步、快速切换）

**快速运行测试**：
```bash
cd out/cmake-debug/libs/gltfio_ext

# 单元测试
./test_animation_asset   # 21 tests - 数据结构验证、多动画支持
./test_animation_binding #  6 tests - 骨骼映射、匹配率验证
./test_asset_loader      # 12 tests - 加载流程、节点提取

# 集成测试
./test_animator          # 23 tests - Animator 扩展、生命周期、集成测试
                         #           (包含外部动画加载、卸载、替换、崩溃保护)

# 运行所有测试
for test in test_animation_asset test_animation_binding test_asset_loader test_animator; do
    echo "=== Running $test ==="
    ./$test
done
```

**测试覆盖率**：
- ✅ 单元测试（~35 tests）：边界检查、错误处理、数据验证
- ✅ 集成测试（~15 tests）：完整工作流、多 Animator、快速切换
- ✅ 生命周期测试（~8 tests）：销毁顺序、内存稳定性、状态回滚
- ✅ 边界情况（~3 tests）：无效索引、空输入、未排序数据

## MaterialProvider & TextureProvider（继承自 gltfio）

gltfio_ext 继承了 gltfio 的插件接口，用于加载完整的网格资产：

- **MaterialProvider**：创建 Filament 材质
    - `UbershaderProvider` - 预编译材质（快速启动）
    - `JitShaderProvider` - 运行时编译（使用 filamat）
- **TextureProvider**：创建和填充纹理
    - `StbProvider` - PNG/JPEG 支持（STB 库）
    - `Ktx2Provider` - KTX2 支持（BasisU 库）

### UbershaderProvider

预编译材质加载器，适合需要快速启动的应用：

```cpp
// 加载 ubershader archive
auto* provider = UbershaderProvider::create(engine);
provider->loadArchive("materials.uberz");

// 在 AssetLoader 中使用
AssetConfiguration config;
config.materials = provider;
AssetLoader* loader = AssetLoader::create(config);
```

使用 `uberz` 工具打包材质：
```bash
uberz -o materials.uberz material1.spec material1.filamat material2.spec material2.filamat
```

更多细节请参见 `libs/uberz/README.md`。

## 相关文档

- **API 文档**：查看头文件中的 docstring（`include/gltfio_ext/*.h`）
- **测试文档**：[test/README.md](test/README.md)
- **glTF 规范**：https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
- **Filament 文档**：https://google.github.io/filament/
