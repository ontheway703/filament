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
- **AssetLoader**：从 GLB 文件加载所有动画（不再只加载第一个）
- **AnimationBinding**：骨骼名称映射（外部动画 ↔ 驻留网格实体）

**测试覆盖**：41 个测试用例（20 + 3 + 12 + 6），涵盖数据验证、加载流程、骨骼映射等。

### 🔄 Phase 2: Animator 扩展（计划中）

下一步将扩展 Animator 以支持：
- 多动画管理和切换
- 动画混合（blend/crossfade）
- 播放控制（速度、循环、时间偏移）

## 使用方式

### 1. 加载网格资产（完整的蒙皮网格）

```cpp
#include <gltfio_ext/AssetLoader.h>
#include <gltfio_ext/AnimationBinding.h>

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

### 3. 骨骼绑定（映射外部动画到网格实体）

```cpp
// 创建绑定（通过骨骼名称匹配）
AnimationBinding binding(meshAsset, animAsset);

// 检查匹配率
float matchRate = binding.getMatchRate();
if (matchRate < 0.9f) {
    utils::slog.w << "Warning: Only " << (matchRate * 100) << "% bones matched!"
                  << utils::io::endl;
}

// 获取映射结果（动画骨骼索引 → 网格 TransformManager::Instance）
const auto& boneMapping = binding.getBoneMapping();
utils::slog.i << "Successfully mapped " << boneMapping.size()
              << " bones" << utils::io::endl;
```

### 4. 多动画管理（Phase 2 计划）

```cpp
// 未来 API 示例（尚未实现）
Animator* animator = animAsset->createAnimator();

// 播放特定动画
animator->playAnimation("walk", /* loop */ true);

// 切换动画（带过渡）
animator->crossfade("run", /* duration */ 0.3f);

// 混合多个动画
animator->blendAnimations({
    {"idle", 0.3f},
    {"aim", 0.7f}
});
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
};
```

## 测试与验证

完整的测试文档请参见 [test/README.md](test/README.md)，包括：
- 41 个测试用例（4 个测试文件）
- 真实模型测试（ecorche 模型，327 个骨骼，3 个动画）
- 100% 骨骼匹配率验证

**快速运行测试**：
```bash
cd out/cmake-debug/libs/gltfio_ext
./test_animation_asset   # 20 tests - 数据结构验证
./test_asset_loader      # 12 tests - 加载流程测试
./test_animation_binding # 6 tests  - 骨骼映射测试
./test_gltfio_ext        # 3 tests  - 集成测试
```

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
