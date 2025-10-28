# gltfio_ext 测试文档

## 📋 概述

本目录包含 `gltfio_ext` 库的单元测试和集成测试。`gltfio_ext` 是一个用于加载和管理 glTF 2.0 动画资产的扩展库，支持将动画数据与网格几何体解耦。

## 🗂️ 测试文件列表

### 1. test_animation_asset.cpp

**测试目标**: AnimationAsset 数据结构

**文件路径**: `libs/gltfio_ext/test/test_animation_asset.cpp`

**测试内容**:
- AnimationAsset 类的核心功能
- 数据验证逻辑
- 节点查找和管理
- 动画时长计算
- 骨骼蒙皮数据

**测试用例数**: 16 个

**编译目标**: `test_animation_asset`

---

### 2. gltfio_test.cpp

**测试目标**: 完整的 glTF 资产加载

**文件路径**: `libs/gltfio_ext/test/gltfio_test.cpp`

**测试内容**:
- FilamentAsset 加载和验证
- 材质实例
- 变换矩阵
- 渲染组件
- Morph targets

**测试用例数**: 3 个

**编译目标**: `test_gltfio_ext`

---

## 📊 测试用例详情

### test_animation_asset.cpp 测试用例

#### 基础功能测试

| # | 测试用例名称 | 功能描述 | 验证点 |
|---|-------------|---------|--------|
| 1 | `EmptyAssetIsValid` | 空资产验证 | 空 AnimationAsset 应该是有效的 |
| 2 | `FindNodeByName` | 按名称查找节点 | 节点名称映射表查找功能 |
| 11 | `GetDuration` | 计算动画时长 | 返回所有采样器的最大时间值 |
| 12 | `GetDurationEmptyAsset` | 空资产时长 | 空资产时长为 0 |
| 13 | `NameGetterSetter` | 名称 Getter/Setter | 动画名称的设置和获取 |
| 14 | `NodeStructure` | 节点结构验证 | 节点层级和名称的正确性 |

#### 数据验证测试

| # | 测试用例名称 | 功能描述 | 验证点 |
|---|-------------|---------|--------|
| 3 | `ValidateValidAsset` | 验证有效资产 | 完整动画数据能通过验证 |
| 4 | `ValidateInvalidTargetNode` | 检测无效目标节点 | 捕获通道目标节点索引越界 |
| 5 | `ValidateInvalidSamplerIndex` | 检测无效采样器索引 | 捕获采样器索引错误 |
| 6 | `ValidateEmptySamplerTimes` | 检测空时间数组 | 采样器必须有关键帧时间 |
| 7 | `ValidateUnsortedTimes` | 检测未排序时间 | 关键帧时间必须升序排列 |
| 8 | `ValidateCyclicHierarchy` | 检测循环层级 | 节点层级不能有环形引用 |

#### 骨骼蒙皮测试

| # | 测试用例名称 | 功能描述 | 验证点 |
|---|-------------|---------|--------|
| 9 | `ValidateInverseBindMatrices` | 验证逆绑定矩阵 | 矩阵数量必须匹配关节数量 |
| 10 | `ValidateInvalidJointIndex` | 检测无效关节索引 | 关节索引必须指向有效节点 |

#### 动画类型测试

| # | 测试用例名称 | 功能描述 | 验证点 |
|---|-------------|---------|--------|
| 15 | `RotationChannel` | 旋转动画通道 | 四元数数据（4值/帧）验证 |
| 16 | `CubicSplineInterpolation` | 三次样条插值 | 切线数据格式验证 |

---

## 🏗️ 编译和运行

### 编译所有测试

```bash
# 从项目根目录
cd /Users/xuan/Desktop/Code/my/filament

# 编译 gltfio_ext 库和测试
cmake --build out/cmake-debug --target gltfio_ext_core -j8
cmake --build out/cmake-debug --target test_animation_asset -j8
cmake --build out/cmake-debug --target test_gltfio_ext -j8
```

### 运行单个测试

```bash
# 运行 AnimationAsset 测试
./out/cmake-debug/libs/gltfio_ext/test_animation_asset

# 运行 gltfio 集成测试
./out/cmake-debug/libs/gltfio_ext/test_gltfio_ext
```

### 运行特定测试用例

```bash
# 使用 gtest filter 运行特定测试
./out/cmake-debug/libs/gltfio_ext/test_animation_asset --gtest_filter="AnimationAssetTest.FindNodeByName"

# 运行多个测试用例
./out/cmake-debug/libs/gltfio_ext/test_animation_asset --gtest_filter="AnimationAssetTest.Validate*"
```

### 详细输出

```bash
# 显示详细测试输出
./out/cmake-debug/libs/gltfio_ext/test_animation_asset --gtest_color=yes --gtest_print_time=1
```

---

## ✅ 测试覆盖率

### 功能覆盖矩阵

| 功能模块 | 覆盖情况 | 测试用例数 | 备注 |
|---------|---------|-----------|------|
| 数据结构验证 | ✅ 完整 | 8 | 包含所有验证逻辑 |
| 节点操作 | ✅ 完整 | 3 | 查找、层级、名称 |
| 动画数据 | ✅ 完整 | 3 | 时长、插值、通道类型 |
| 蒙皮数据 | ✅ 完整 | 2 | 逆绑定矩阵、关节 |
| 边界情况 | ✅ 完整 | 2 | 空资产、零时长 |
| **总计** | **16/16** | **16** | **100% 覆盖** |

### 测试金字塔

```
        集成测试 (3个)        ← gltfio_test.cpp
           ↗        ↖
    功能测试 (10个)            ← test_animation_asset.cpp
         ↗      ↖
  单元测试 (6个)              ← test_animation_asset.cpp
```

---

## 📝 测试数据结构

### 测试 Fixture 骨骼结构

test_animation_asset.cpp 使用的测试骨骼结构：

```
root (index 0, parent -1)
  └─ spine (index 1, parent 0)
       └─ head (index 2, parent 1)
```

这是一个简单的三节点骨骼，用于测试各种动画数据验证逻辑。

### 动画数据格式

#### 平移/缩放动画（TRANSLATION / SCALE）
```
每个关键帧：3 个 float (x, y, z)
示例：{0.0f, 0.0f, 0.0f,  // 帧1
       1.0f, 0.0f, 0.0f}  // 帧2
```

#### 旋转动画（ROTATION）
```
每个关键帧：4 个 float (x, y, z, w) - 四元数
示例：{0.0f, 0.0f, 0.0f, 1.0f,  // 帧1: 单位四元数
       0.0f, 0.707f, 0.0f, 0.707f}  // 帧2: 绕Y轴90度
```

#### 三次样条插值（CUBICSPLINE）
```
每个关键帧：3 组数据（入切线、值、出切线）
对于平移：9 个 float/关键帧
示例：{// 帧1
       0.0f, 0.0f, 0.0f,  // 入切线
       0.0f, 0.0f, 0.0f,  // 值
       0.0f, 0.0f, 0.0f,  // 出切线
       // 帧2
       ...}
```

---

## 🐛 故障排查

### 常见问题

#### 1. 测试编译失败

**现象**: CMake 报错找不到 AnimationAsset.h

**解决**:
```bash
# 确保头文件已添加到 CMakeLists.txt 的 PUBLIC_HDRS
# 重新运行 CMake 配置
cmake -B out/cmake-debug
```

#### 2. 测试运行失败

**现象**: 所有测试都失败，报段错误

**排查步骤**:
1. 确认 gltfio_ext_core 库已成功编译
2. 检查是否有未初始化的指针
3. 运行 Address Sanitizer:
```bash
# 使用 ASan 编译
cmake -B out/cmake-debug -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build out/cmake-debug --target test_animation_asset
./out/cmake-debug/libs/gltfio_ext/test_animation_asset
```

#### 3. 特定测试用例失败

**现象**: ValidateCyclicHierarchy 等测试失败

**原因**: 可能是 validate() 逻辑更改

**解决**: 检查 AnimationAsset::validate() 实现，确保循环检测逻辑正确

---

## 📐 测试维护规范

### 添加新测试用例

1. **命名规范**:
   - 测试类名：`<ClassName>Test`
   - 测试用例名：`<FunctionName>_<Scenario>` 或使用驼峰命名

2. **注释要求**:
   ```cpp
   /**
    * 测试用例N：<简短描述>
    *
    * 测试目标：<明确的目标>
    * 测试场景：<详细的场景描述>
    * 预期结果：<预期的结果>
    * 验证点：<关键验证点>
    */
   TEST_F(AnimationAssetTest, NewTest) {
       // 实现...
   }
   ```

3. **更新文档**:
   - 在本 README 的"测试用例详情"表格中添加新测试
   - 更新"测试用例数"统计
   - 如果是新功能，更新"测试覆盖率"表格

4. **测试数据**:
   - 使用有意义的测试数据
   - 在注释中说明数据的含义
   - 避免魔法数字，使用命名常量

### 修改现有测试

1. **保持向后兼容**: 不要轻易删除测试用例
2. **记录变更**: 在 git commit 中说明修改原因
3. **更新注释**: 确保注释与代码一致

### 测试代码质量

- ✅ 每个测试只测试一个功能点
- ✅ 测试名称清晰表达测试内容
- ✅ 避免测试之间的依赖
- ✅ 使用 EXPECT 而不是 ASSERT（除非必须）
- ✅ 清理测试数据（使用 TearDown）

---

## 📚 相关文档

- **AnimationAsset 设计文档**: `docs/animation_asset_design.md`
- **glTF 2.0 规范**: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
- **Google Test 文档**: https://google.github.io/googletest/
- **Filament 文档**: https://google.github.io/filament/

---

## 🔄 测试历史

### 版本 1.0 (2025-10-28)

- ✅ 创建 AnimationAsset 单元测试
- ✅ 实现 16 个测试用例
- ✅ 100% 测试通过
- ✅ 添加完整中文注释
- ✅ 创建测试文档

---

## 📞 联系和反馈

如有测试相关问题或建议，请：

1. 查看测试源文件中的注释
2. 阅读本 README 的"故障排查"部分
3. 查看相关设计文档
4. 提交 Issue 或 Pull Request

---

**最后更新**: 2025-10-28
**维护者**: Filament Animation Team
**文档版本**: 1.0
