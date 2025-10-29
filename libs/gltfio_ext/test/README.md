# gltfio_ext 测试指南

本目录包含 `gltfio_ext` 库的单元测试和集成测试。`gltfio_ext` 用于加载和管理 glTF 2.0 动画资产，支持将动画数据与网格几何体解耦。

## 快速开始

### 编译测试

从项目根目录执行：

```bash
# 配置构建（首次运行）
cd /Users/xuan/Desktop/Code/my/filament
cmake -B out/cmake-debug

# 编译所有测试
cmake --build out/cmake-debug --target test_animation_asset -j8
cmake --build out/cmake-debug --target test_gltfio_ext -j8
cmake --build out/cmake-debug --target test_asset_loader -j8
cmake --build out/cmake-debug --target test_animation_binding -j8
```

### 运行测试

> ⚠️ **关键提示：必须从 `out/cmake-debug/libs/gltfio_ext` 目录运行测试！**
>
> 测试所需的 GLB 资源文件通过 CMake 自动复制到该目录。从其他位置（如项目根目录或 `out/cmake-debug`）运行会导致文件找不到错误。

```bash
# 进入构建目录
cd out/cmake-debug/libs/gltfio_ext

# 运行单个测试
./test_animation_asset
./test_gltfio_ext
./test_asset_loader
./test_animation_binding

# 运行特定测试用例
./test_animation_asset --gtest_filter="AnimationAssetTest.FindNodeByName"
./test_asset_loader --gtest_filter="AssetLoaderTest.LoadValidAnimatedGLB"
./test_animation_binding --gtest_filter="AnimationBindingTest.BoneNameMapping"

# 显示详细输出
./test_animation_asset --gtest_color=yes --gtest_print_time=1
```

## 测试概览

总计：**4个测试可执行文件，41个测试用例**，覆盖核心功能和集成场景。

| 测试文件 | 类型 | 用例数 | 依赖库 | 需要资源 |
|---------|------|-------|--------|---------|
| test_animation_asset.cpp | 单元测试 | 20 | gltfio_ext_core | 否 |
| test_gltfio_ext.cpp | 集成测试 | 3 | gltfio_ext + uberarchive_ext | 是 |
| test_asset_loader.cpp | 集成测试 | 12 | gltfio_ext + uberarchive_ext | 是 |
| test_animation_binding.cpp | 集成测试 | 6 | gltfio_ext + uberarchive_ext | 是 |

**资源文件**：
- `AnimatedMorphCube.glb` - 从 `third_party/models/` 复制
- `ecorche_animation_only.glb` - 从 `tmp/test_assets/` 复制（animation-only GLB）
- `ecorche_full.glb` - 从 `tmp/test_assets/` 复制（包含完整蒙皮数据）

## 测试详情

### test_animation_asset.cpp

**功能**：测试 `AnimationAsset` 数据结构和验证逻辑。

**20个测试用例包括**：

#### 基础功能（5个）
- `EmptyAssetIsValid` - 空资产验证
- `FindNodeByName` - 节点名称查找（使用哈希表）
- `FindNodeByNameWithoutBoneMap` - 节点名称查找（线性搜索回退路径）
- `GetDuration` - 动画时长计算
- `NodeStructure` - 节点层级结构

#### 数据验证（9个）
- `ValidateValidAsset` - 完整数据验证
- `ValidateInvalidTargetNode` - 检测无效目标节点
- `ValidateInvalidSamplerIndex` - 检测无效采样器索引
- `ValidateEmptySamplerTimes` - 检测空时间数组
- `ValidateUnsortedTimes` - 检测未排序时间
- `ValidateCyclicHierarchy` - 检测循环层级
- `ValidateTranslationInsufficientValues` - 检测平移动画数据不足（精确值数量验证）
- `ValidateRotationInvalidValueCount` - 检测旋转动画数据错误（四元数格式验证）
- `ValidateCubicSplineInsufficientValues` - 检测三次样条插值数据不足（3倍数据验证）

#### 骨骼蒙皮（2个）
- `ValidateInverseBindMatrices` - 逆绑定矩阵验证
- `ValidateInvalidJointIndex` - 无效关节索引检测

#### 动画类型（4个）
- `RotationChannel` - 旋转动画通道（四元数）
- `CubicSplineInterpolation` - 三次样条插值
- `GetDurationEmptyAsset` - 空资产时长
- `MultipleAnimations` - 多动画支持（getAnimationCount, getAnimation, getAnimationByName）

**特点**：纯单元测试，不依赖外部资源，执行速度快。

---

### test_gltfio_ext.cpp

**功能**：测试完整的 glTF 资产加载流程。

**3个测试用例**：
- FilamentAsset 加载和验证
- 材质实例创建
- 变换矩阵和渲染组件
- Morph targets 支持

**特点**：集成测试，需要 `AnimatedMorphCube.glb` 资源文件。

---

### test_asset_loader.cpp

**功能**：测试 `AssetLoader::loadAnimationAsset()` 方法，验证从 GLB 文件加载纯动画数据。

**12个测试用例**：

#### 错误处理（2个）
- `LoadEmptyData` - 空数据处理
- `LoadInvalidGLB` - 无效 GLB 格式处理

#### 数据提取（5个）
- `LoadValidAnimatedGLB` - 成功加载有效动画文件
- `NodeTreeExtraction` - 节点树结构验证（改进了断言，检测自循环和空名称）
- `NodeNameLookup` - 节点名称查找功能
- `AnimationChannelExtraction` - 动画通道数据验证
- `AnimationSamplerExtraction` - 采样器关键帧数据验证

#### Animation-Only 测试（2个）
- `LoadAnimationOnlyGLB` - 加载 export_meshes=False 导出的纯动画GLB
- `NoSkinData` - 验证无蒙皮数据时 joints/inverseBindMatrices 为空

#### 蒙皮数据测试（1个）
- `SkinDataExtraction` - 验证 joints 和 inverseBindMatrices 的提取和一致性

#### 内存管理（2个）
- `AssetDestruction` - 资源释放和内存管理
- `MultipleLoadsOfSameFile` - 多次加载独立性验证

**特点**：
- 测试动画与网格解耦的核心功能
- 验证 cgltf 集成和数据提取
- 全面覆盖蒙皮数据的提取和验证
- 支持 animation-only GLB（export_meshes=False）
- 检查内存泄漏（可配合 valgrind/ASan）

---

### test_animation_binding.cpp

**功能**：测试 `AnimationBinding` 类，验证外部动画骨骼与驻留网格实体的名称映射。

**6个测试用例**：

#### 基础功能（2个）
- `EmptyBinding` - 空绑定验证（无网格或动画）
- `MultipleAnimationsInAsset` - 多动画资产加载（验证从单个 GLB 文件加载多个动画片段）

#### 骨骼映射（2个）
- `BoneNameMapping` - 骨骼名称匹配（使用 ecorche_full.glb，327个骨骼，100%匹配率）
- `PartialMatch` - 部分匹配场景（动画骨骼数量与网格不同时的处理）

#### 变换实例（2个）
- `TransformInstancesValid` - 变换实例验证（确保所有匹配的骨骼都有有效的 TransformManager::Instance）
- `GetMatchRate` - 匹配率计算（匹配数量 / 总动画骨骼数）

**特点**：
- 测试动画复用的核心功能（一套网格 + 多套外部动画）
- 验证 NameComponentManager 集成（通过 getName() 查询实体名称）
- 使用真实模型（ecorche 有 327 个关节骨骼）
- 支持多动画 GLB 文件（ecorche_full.glb 包含 Pull/Push/Squat 三个动画）
- 测试匹配容差（90% 匹配率阈值）

**关键测试数据**：
- ecorche_full.glb: 327 个关节骨骼，3 个动画片段
- 100% 骨骼匹配率（所有骨骼名称完全对应）
- 验证从 animation-only 到 full-mesh 的骨骼绑定

## 故障排查

### 问题1：测试编译失败

**现象**：CMake 报错找不到 `AnimationAsset.h` 或链接失败。

**解决方案**：
```bash
# 清理并重新配置
rm -rf out/cmake-debug
cmake -B out/cmake-debug -DCMAKE_BUILD_TYPE=Debug

# 确保核心库先编译
cmake --build out/cmake-debug --target gltfio_ext_core -j8
```

### 问题2：找不到测试文件

**现象**：运行测试时提示 "Test file not found" 或 "SKIPPED"。

**原因**：工作目录错误，测试从项目根目录运行而不是构建目录。

**解决方案**：
```bash
# 必须从这个目录运行！
cd out/cmake-debug/libs/gltfio_ext
./test_asset_loader

# 验证文件存在
ls -lh AnimatedMorphCube.glb  # 应该显示 6.6K 的文件
```

### 问题3：段错误或初始化失败

**现象**：测试运行时崩溃或 Engine 初始化失败。

**排查步骤**：

1. 检查依赖库链接：
```bash
# test_asset_loader 需要这些库
ldd ./test_asset_loader | grep -E "gltfio|filament|uberarchive"
```

2. 使用 Address Sanitizer：
```bash
cmake -B out/cmake-debug -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build out/cmake-debug --target test_asset_loader
cd out/cmake-debug/libs/gltfio_ext && ./test_asset_loader
```

3. 检查 MaterialProvider 清理：
   - 确保 `destroyMaterials()` 在析构前调用
   - 查看 test_asset_loader.cpp:76 的 TearDown() 实现

### 问题4：特定测试用例失败

**ValidateCyclicHierarchy 等验证测试失败**：
- 检查 `AnimationAsset::validate()` 实现（src/AnimationAsset.cpp）
- 确认测试数据中有足够的 channels/samplers（否则会提前返回）

## 开发指南

### 添加新测试用例

1. **选择合适的测试文件**：
   - 数据结构验证 → test_animation_asset.cpp
   - 加载流程测试 → test_asset_loader.cpp
   - 完整集成测试 → test_gltfio_ext.cpp

2. **编写测试**：
```cpp
TEST_F(AnimationAssetTest, NewFeature) {
    // Arrange: 准备测试数据

    // Act: 执行被测试功能

    // Assert: 验证结果
    EXPECT_EQ(expected, actual);
}
```

3. **更新文档**：在本 README 的"测试详情"部分添加说明。

4. **验证测试**：
```bash
cmake --build out/cmake-debug --target test_animation_asset -j8
cd out/cmake-debug/libs/gltfio_ext && ./test_animation_asset
```

### CMakeLists.txt 配置

测试配置位于 `libs/gltfio_ext/CMakeLists.txt`：

- **第 228-242 行**：`add_test_gltf()` 函数，定义测试文件复制机制
- **第 247-257 行**：`test_gltfio_ext` 配置
- **第 260-266 行**：`test_animation_asset` 配置
- **第 269-276 行**：`test_asset_loader` 配置

**关键点**：
- `add_dependencies(test_asset_loader test_gltfio_ext_files)` 确保资源文件被复制
- test_animation_asset 只需要 gltfio_ext_core（快速编译）
- 其他测试需要完整的 gltfio_ext + uberarchive_ext

### 相关文档

- **glTF 2.0 规范**：https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
- **Google Test 文档**：https://google.github.io/googletest/
- **Filament 文档**：https://google.github.io/filament/
- **项目架构**：../../CLAUDE.md
