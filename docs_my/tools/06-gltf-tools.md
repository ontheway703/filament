# glTF 相关工具

## 概述

Filament 提供了完整的 glTF 2.0 工具链，用于预览、验证、优化和调试 glTF 资产。这些工具是 Filament 工作流程中的重要组成部分，帮助你在集成到应用之前确保 glTF 文件的质量和性能。

**核心工具**:
- **gltf_viewer** - 实时 glTF 预览器（Filament 官方）
- **gltf-transform** - glTF 优化和转换工具
- **gltf-validator** - Khronos 官方验证器
- **gltfpack** - meshoptimizer 压缩工具

## gltf_viewer - Filament glTF 查看器

### 概述

`gltf_viewer` 是 Filament 官方的 glTF 2.0 查看器，使用 Filament 引擎渲染，所见即所得。它是测试 glTF 资产在 Filament 中表现的最佳工具。

**特性**:
- ✅ 完整 glTF 2.0 支持（几何、材质、动画、蒙皮）
- ✅ IBL 环境光照
- ✅ 实时动画播放和控制
- ✅ 相机轨道控制
- ✅ 调试可视化（法线、UV、线框）
- ✅ 截图导出
- ✅ 性能统计

### 基本用法

```bash
# 最简单的用法
gltf_viewer model.glb

# 带 IBL 环境光
gltf_viewer --ibl=venice_sunset_ibl model.glb

# 指定 skybox
gltf_viewer --ibl=env_ibl --skybox=env_skybox.ktx model.glb

# 完整示例
gltf_viewer \
    --ibl=environment_ibl \
    --skybox=environment_skybox.ktx \
    --scale=10.0 \
    --camera=auto \
    model.glb
```

### 命令行参数

| 参数 | 说明 | 默认值 | 示例 |
|------|------|--------|------|
| `--ibl=PATH` | IBL 环境光 KTX 文件 | 无 | `--ibl=env_ibl` |
| `--skybox=PATH` | 天空盒 KTX 文件 | 无 | `--skybox=sky.ktx` |
| `--scale=N` | 模型缩放倍数 | 1.0 | `--scale=10.0` |
| `--camera=MODE` | 相机模式 | orbit | `--camera=flight` |
| `--actually-unlit` | 强制无光照 | false | `--actually-unlit` |
| `--recompute-aabb` | 重新计算包围盒 | false | `--recompute-aabb` |
| `--animation=INDEX` | 播放指定动画 | -1 (全部) | `--animation=0` |

**相机模式**:
- `orbit`: 轨道相机（默认，围绕模型旋转）
- `flight`: 飞行相机（自由移动）
- `auto`: 自动适配

### 交互控制

**鼠标控制**:
```
左键拖拽:   旋转相机 (orbit 模式)
中键拖拽:   平移相机
滚轮:       缩放
右键拖拽:   旋转光源方向
```

**键盘快捷键**:
```
空格:       播放/暂停动画
R:          重置相机
W:          线框模式
N:          显示法线
T:          显示切线
U:          显示 UV
B:          显示包围盒
L:          切换光照/无光照
S:          截图
F:          显示 FPS
ESC:        退出
```

### 完整工作流程示例

**示例 1: 预览基础模型**

```bash
# 1. 准备 IBL 环境光
cmgen -x ./ibl --format=ktx --size=256 environment.exr

# 2. 预览模型
gltf_viewer --ibl=./ibl/environment_ibl model.glb

# 3. 如果模型太小/太大，调整缩放
gltf_viewer --ibl=./ibl/environment_ibl --scale=5.0 model.glb

# 4. 检查动画
gltf_viewer --ibl=./ibl/environment_ibl model.glb
# (按空格播放动画)
```

**示例 2: 调试材质问题**

```bash
# 1. 预览模型
gltf_viewer --ibl=./ibl/env_ibl model.glb

# 2. 检查法线是否正确
# (按 N 键显示法线可视化)

# 3. 检查 UV 映射
# (按 U 键显示 UV 可视化)

# 4. 查看线框
# (按 W 键切换线框模式)

# 5. 比较有光照 vs 无光照
# (按 L 键切换)
```

**示例 3: 性能测试**

```bash
# 1. 启动查看器并显示 FPS
gltf_viewer --ibl=./ibl/env_ibl complex_scene.glb
# (按 F 键显示帧率)

# 2. 测试不同 IBL 设置的性能
gltf_viewer --ibl=./ibl/env_ibl_512 model.glb  # 高分辨率 IBL
gltf_viewer --ibl=./ibl/env_ibl_128 model.glb  # 低分辨率 IBL

# 3. 测试动画性能
gltf_viewer --ibl=./ibl/env_ibl --animation=0 character.glb
```

### 常见问题

**Q1: 模型显示全黑？**

```bash
# 原因: 缺少 IBL 环境光
# 解决: 添加 --ibl 参数
gltf_viewer --ibl=./ibl/environment_ibl model.glb
```

**Q2: 模型太小/太大看不见？**

```bash
# 解决: 调整缩放
gltf_viewer --scale=10.0 tiny_model.glb
gltf_viewer --scale=0.1 huge_model.glb

# 或重新计算包围盒
gltf_viewer --recompute-aabb model.glb
```

**Q3: 动画不播放？**

```bash
# 检查模型是否包含动画
gltf-transform inspect model.glb

# 手动指定动画索引
gltf_viewer --animation=0 model.glb

# 或在查看器中按空格键
```

## gltf-transform - glTF 优化工具

### 概述

`gltf-transform` 是强大的 glTF 命令行工具，支持优化、转换、验证等操作。

**安装**:
```bash
npm install -g @gltf-transform/cli
```

### 常用命令

#### 1. 模型信息查看

```bash
# 查看模型详细信息
gltf-transform inspect model.glb

# 输出示例:
# ┌──────────────────┬────────┐
# │ Property         │ Value  │
# ├──────────────────┼────────┤
# │ Nodes            │ 42     │
# │ Meshes           │ 15     │
# │ Materials        │ 8      │
# │ Textures         │ 12     │
# │ Animations       │ 3      │
# │ File Size        │ 5.2 MB │
# └──────────────────┴────────┘
```

#### 2. 模型优化

```bash
# 基础优化（推荐）
gltf-transform optimize input.glb output.glb

# 等价于执行:
# - dedup: 去重顶点和材质
# - prune: 删除未使用的资源
# - weld: 合并相近顶点
# - flatten: 扁平化节点层级

# 手动控制优化
gltf-transform dedup input.glb temp1.glb
gltf-transform prune temp1.glb temp2.glb
gltf-transform weld temp2.glb output.glb --tolerance=0.0001
```

#### 3. 网格压缩

```bash
# Draco 压缩 (高压缩比)
gltf-transform draco input.glb output.glb \
    --method=edgebreaker \
    --quantize-position=14 \
    --quantize-normal=10 \
    --quantize-texcoord=12

# meshopt 压缩 (解压快)
gltf-transform meshopt input.glb output.glb

# 两者对比:
# Draco:   高压缩比 (5-10x), 解压慢, 需要额外库
# meshopt: 中压缩比 (2-3x), 解压快, Filament 原生支持
```

#### 4. 纹理处理

```bash
# 纹理压缩 (KTX2 + Basis Universal)
gltf-transform etc1s input.glb output.glb --quality=128

# 或使用 UASTC (更高质量)
gltf-transform uastc input.glb output.glb

# 调整纹理分辨率
gltf-transform resize input.glb output.glb --width=1024 --height=1024

# WebP 压缩 (仅支持部分浏览器)
gltf-transform webp input.glb output.glb --quality=90
```

#### 5. 转换格式

```bash
# GLB → glTF (分离 JSON 和二进制)
gltf-transform copy model.glb model.gltf

# glTF → GLB (打包)
gltf-transform copy model.gltf model.glb

# 分离纹理为外部文件
gltf-transform partition input.glb output.gltf
```

#### 6. 其他实用命令

```bash
# 去除动画
gltf-transform prune input.glb output.glb --animation

# 合并场景
gltf-transform merge scene1.glb scene2.glb merged.glb

# 提取单个网格
gltf-transform select input.glb output.glb --node=ChairMesh

# 扁平化节点层级
gltf-transform flatten input.glb output.glb

# 中心化模型
gltf-transform center input.glb output.glb
```

### 完整优化流程

```bash
#!/bin/bash

INPUT="model_raw.glb"
OUTPUT="model_optimized.glb"

# 1. 查看原始文件信息
echo "=== Original Model ==="
gltf-transform inspect "$INPUT"

# 2. 基础优化
gltf-transform optimize "$INPUT" temp1.glb

# 3. 网格压缩 (meshopt, Filament 原生支持)
gltf-transform meshopt temp1.glb temp2.glb

# 4. 纹理压缩 (UASTC)
gltf-transform uastc temp2.glb "$OUTPUT"

# 5. 查看优化后信息
echo "=== Optimized Model ==="
gltf-transform inspect "$OUTPUT"

# 6. 清理临时文件
rm temp1.glb temp2.glb

echo "Optimization complete!"
```

## gltf-validator - Khronos 官方验证器

### 概述

`gltf-validator` 是 Khronos 官方的 glTF 验证工具，检查模型是否符合 glTF 2.0 规范。

**安装**:
```bash
# 作为 npm 包
npm install -g gltf-validator

# 或下载预编译二进制
# https://github.com/KhronosGroup/glTF-Validator/releases
```

### 基本用法

```bash
# 验证模型
gltf-validator model.glb

# 输出详细报告
gltf-validator model.glb -r report.json

# 检查特定问题
gltf-validator model.glb --max-issues 50
```

### 验证报告解读

```json
{
  "uri": "model.glb",
  "mimeType": "model/gltf-binary",
  "validatorVersion": "2.0.0-dev.3.9",
  "validatedAt": "2024-01-15T10:30:00.000Z",

  "issues": {
    "numErrors": 0,
    "numWarnings": 2,
    "numInfos": 5,
    "numHints": 3,

    "messages": [
      {
        "code": "UNUSED_OBJECT",
        "message": "This material is not used.",
        "severity": 1,  // 0=Error, 1=Warning, 2=Info, 3=Hint
        "pointer": "/materials/3"
      },
      {
        "code": "ACCESSOR_NON_UNIT",
        "message": "Normal vector is not of unit length.",
        "severity": 1,
        "pointer": "/accessors/12"
      }
    ]
  },

  "info": {
    "version": "2.0",
    "generator": "Blender 3.4",
    "extensionsUsed": ["KHR_materials_pbrSpecularGlossiness"],
    "primitivesCount": 42,
    "maxUVs": 1,
    "maxAttributes": 4
  }
}
```

**严重性级别**:
- **Error (0)**: 违反 glTF 规范，必须修复
- **Warning (1)**: 不规范但可用，建议修复
- **Info (2)**: 信息性提示
- **Hint (3)**: 优化建议

### 常见验证错误

**错误 1: ACCESSOR_INVALID_MIN_MAX**

```
问题: Accessor 的 min/max 值与实际数据不符
原因: DCC 工具导出错误
修复: 重新导出或手动修复
```

**错误 2: BUFFER_VIEW_TOO_SMALL**

```
问题: BufferView 长度小于 Accessor 需要的大小
原因: 数据损坏
修复: 重新导出模型
```

**错误 3: NODE_MATRIX_NON_TRS**

```
问题: 节点矩阵不是 TRS (Translation/Rotation/Scale) 可分解的
警告: 可能包含倾斜变换，某些引擎不支持
修复: 在 DCC 工具中冻结变换
```

**警告 1: UNUSED_OBJECT**

```
问题: 包含未使用的资源 (材质、纹理等)
影响: 浪费文件大小
修复: 使用 gltf-transform prune 删除
```

**警告 2: ACCESSOR_NON_UNIT**

```
问题: 法线向量不是单位向量
影响: 渲染可能不正确
修复: 在 DCC 工具中重新计算法线
```

### 批量验证

```bash
#!/bin/bash

# 验证目录下所有 glTF 文件
for file in models/*.glb; do
    echo "Validating: $file"
    gltf-validator "$file" -r "reports/$(basename $file).json"

    # 检查是否有错误
    if [ $? -ne 0 ]; then
        echo "❌ FAILED: $file"
    else
        echo "✅ PASSED: $file"
    fi
done
```

## gltfpack - meshoptimizer 压缩工具

### 概述

`gltfpack` 是基于 meshoptimizer 的 glTF 压缩工具，专注于性能优化。

**安装**:
```bash
# 下载预编译版本
# https://github.com/zeux/meshoptimizer/releases

# 或从源码构建
git clone https://github.com/zeux/meshoptimizer.git
cd meshoptimizer/gltf
make
```

### 基本用法

```bash
# 基础优化
gltfpack -i input.glb -o output.glb

# 完整优化 (推荐)
gltfpack -i input.glb -o output.glb -cc -tc

# 参数说明:
# -cc: 启用网格压缩
# -tc: 启用纹理压缩 (KTX2 + Basis)
```

### 高级参数

```bash
# 控制压缩级别
gltfpack -i input.glb -o output.glb \
    -cc \                           # 网格压缩
    -tc \                           # 纹理压缩
    -cf \                           # 合并缓冲区
    -si 1.0 \                       # 简化系数 (1.0=不简化)
    -vp 14 \                        # 位置量化精度
    -vt 12 \                        # UV 量化精度
    -vn 8 \                         # 法线量化精度
    -vc 8                           # 颜色量化精度

# 网格简化
gltfpack -i input.glb -o output.glb -si 0.5  # 简化到 50% 三角形
```

### 与 gltf-transform 对比

| 特性 | gltfpack | gltf-transform |
|------|----------|---------------|
| 网格优化 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| 纹理压缩 | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| 网格简化 | ⭐⭐⭐⭐⭐ | ❌ |
| 易用性 | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| 文档 | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| 推荐场景 | 性能关键 | 通用优化 |

**建议**: 对大多数项目使用 gltf-transform，对性能敏感的移动端使用 gltfpack。

## 完整 glTF 工作流程

### 从 DCC 工具到 Filament

```bash
#!/bin/bash

# 步骤 1: 从 Blender/Maya 导出 glTF
# (在 DCC 工具中执行)
# File → Export → glTF 2.0 (.glb)

# 步骤 2: 验证模型
echo "=== Validating ==="
gltf-validator model_export.glb -r validation_report.json

# 检查验证结果
if grep -q '"numErrors": 0' validation_report.json; then
    echo "✅ Validation passed"
else
    echo "❌ Validation failed, check report"
    exit 1
fi

# 步骤 3: 优化模型
echo "=== Optimizing ==="
gltf-transform optimize model_export.glb model_opt.glb

# 步骤 4: 压缩 (选择一种)
echo "=== Compressing ==="

# 方案 A: gltf-transform (推荐)
gltf-transform meshopt model_opt.glb model_compressed.glb
gltf-transform uastc model_compressed.glb model_final.glb

# 方案 B: gltfpack (性能优先)
# gltfpack -i model_opt.glb -o model_final.glb -cc -tc

# 步骤 5: 预览
echo "=== Previewing ==="
gltf_viewer --ibl=./ibl/env_ibl model_final.glb

# 步骤 6: 查看结果
echo "=== Original ==="
ls -lh model_export.glb
gltf-transform inspect model_export.glb

echo "=== Final ==="
ls -lh model_final.glb
gltf-transform inspect model_final.glb

echo "Done!"
```

### 移动端优化流程

```bash
#!/bin/bash

INPUT="model.glb"
OUTPUT="model_mobile.glb"

# 1. 基础优化
gltf-transform optimize "$INPUT" temp1.glb

# 2. 降低三角形数 (如果模型复杂)
gltfpack -i temp1.glb -o temp2.glb -si 0.7  # 减少到 70%

# 3. 网格压缩
gltf-transform meshopt temp2.glb temp3.glb

# 4. 纹理压缩 (ETC1S, 更小)
gltf-transform etc1s temp3.glb "$OUTPUT" --quality=128

# 5. 验证
gltf-validator "$OUTPUT"

# 6. 预览
gltf_viewer --ibl=./ibl/env_ibl "$OUTPUT"

# 清理
rm temp*.glb

echo "Mobile optimization complete!"
echo "Size reduction:"
ls -lh "$INPUT" "$OUTPUT"
```

## 调试技巧

### 1. 可视化调试

```bash
# 使用 gltf_viewer 调试
gltf_viewer model.glb

# 检查法线
# (按 N 键，应该看到蓝色法线向外)

# 检查 UV
# (按 U 键，应该看到渐变色)

# 检查切线
# (按 T 键，切线应垂直于法线)
```

### 2. 性能分析

```bash
# 查看模型统计
gltf-transform inspect model.glb

# 关注:
# - Primitives count (越少越好)
# - Materials count (合并材质)
# - Textures count (合并纹理图集)
# - Draw calls ≈ Primitives count
```

### 3. 文件大小分析

```bash
# 分析 glTF 组成
gltf-transform inspect model.glb | grep -A 20 "File Size"

# 典型分布:
# Geometry (buffer):  30-50%
# Textures:           40-60%
# JSON metadata:      5-10%

# 优化建议:
# - Geometry 大 → 使用 meshopt 或 Draco
# - Textures 大 → 使用 UASTC/ETC1S 压缩
```

## 最佳实践

### 1. DCC 导出设置

**Blender**:
```
Format: glTF Binary (.glb)
✅ Include: Selected Objects
✅ Transform: +Y Up
✅ Geometry: Apply Modifiers
✅ Geometry: UVs
✅ Geometry: Normals
✅ Geometry: Tangents (if using normal maps)
✅ Materials: Export
✅ Compression: None (稍后用工具压缩)
```

**Maya**:
```
File Format: glTF 2.0 Binary (.glb)
✅ Export Selection
✅ Bake Animation
✅ Export Normals
✅ Export Tangents
```

### 2. 优化检查清单

```
✅ 验证: gltf-validator 无错误
✅ 去重: gltf-transform dedup
✅ 清理: gltf-transform prune (删除未使用资源)
✅ 压缩: meshopt 网格压缩
✅ 纹理: UASTC/ETC1S 压缩
✅ 预览: gltf_viewer 检查结果
✅ 测试: 在目标设备上测试性能
```

### 3. 性能目标

| 平台 | 三角形数 | 纹理大小 | 文件大小 |
|------|---------|---------|---------|
| 移动端 | < 50K | 1024x1024 | < 5 MB |
| 桌面端 | < 200K | 2048x2048 | < 20 MB |
| Web | < 30K | 512x512 | < 2 MB |

## 相关文档

- [../gltf/01-format-spec.md](../gltf/01-format-spec.md) - glTF 格式规范
- [../gltfio/01-overview.md](../gltfio/01-overview.md) - Filament glTF 加载
- [02-matc-compiler.md](02-matc-compiler.md) - 材质编译
- [05-mipgen.md](05-mipgen.md) - 纹理处理

## 总结

**glTF 工具链要点**:

1. **预览**: gltf_viewer (Filament 官方)
2. **优化**: gltf-transform (通用) 或 gltfpack (性能)
3. **验证**: gltf-validator (必须)
4. **工作流程**: 验证 → 优化 → 压缩 → 预览

**推荐工作流**:

```bash
# 1. 验证
gltf-validator input.glb

# 2. 优化
gltf-transform optimize input.glb temp.glb

# 3. 压缩
gltf-transform meshopt temp.glb temp2.glb
gltf-transform uastc temp2.glb output.glb

# 4. 预览
gltf_viewer --ibl=./ibl/env output.glb
```

掌握这些工具，你可以高效地处理 glTF 资产并集成到 Filament 应用中！
