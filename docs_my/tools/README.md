# Filament 工具链完整文档

## 概述

Filament 提供了一套完整的命令行工具，用于资源准备、材质编译、环境贴图生成、网格转换等任务。掌握这些工具是从理论到实践的关键桥梁。本目录详细介绍每个工具的使用方法、参数说明和最佳实践。

### 工具链概览

```
资源创建流程:

  3D 模型 (.obj/.fbx)
      ↓
  filamesh → .filamesh (优化后的网格)
      ↓
  应用加载 → VertexBuffer/IndexBuffer

  材质定义 (.mat)
      ↓
  matc → .filamat (编译后的材质)
      ↓
  应用加载 → Material

  HDR 环境贴图 (.exr/.hdr)
      ↓
  cmgen → IBL 资源 (.ktx + SH系数)
      ↓
  应用加载 → IndirectLight + Skybox

  纹理图片 (.png/.jpg)
      ↓
  mipgen → Mipmap 纹理
      ↓
  应用加载 → Texture
```

---

## 📚 文档结构

### 核心工具部分

1. **[01-tools-overview.md](01-tools-overview.md)** - 工具链概览
   - Filament 工具生态
   - 工具安装和构建
   - 工具链在资产管线中的位置
   - 跨平台支持说明

2. **[02-matc-compiler.md](02-matc-compiler.md)** - matc 材质编译器
   - matc 命令行参数详解
   - 材质编译流程
   - 针对不同 Backend 的编译
   - 错误诊断和调试
   - 优化选项

3. **[03-cmgen-ibl.md](03-cmgen-ibl.md)** - cmgen IBL 生成器
   - IBL (Image-Based Lighting) 原理
   - cmgen 命令行参数
   - HDR 环境贴图处理
   - 球谐系数生成
   - 镜面反射预过滤
   - 天空盒生成

4. **[04-filamesh.md](04-filamesh.md)** - filamesh 网格转换器
   - 支持的输入格式 (OBJ, FBX, glTF)
   - 网格优化技术
   - 切线空间生成
   - UV 坐标处理
   - .filamesh 格式说明

5. **[05-mipgen.md](05-mipgen.md)** - mipgen Mipmap 生成器
   - Mipmap 原理和重要性
   - mipgen 使用方法
   - 滤波器选项
   - 纹理压缩集成
   - KTX/KTX2 格式支持

### 辅助工具部分

6. **[06-gltf-tools.md](06-gltf-tools.md)** - glTF 相关工具
   - gltf_viewer 查看器
   - gltf_baker 纹理烘焙
   - glTF 验证工具集成
   - Draco 压缩工具
   - 第三方工具介绍

7. **[07-debugging-tools.md](07-debugging-tools.md)** - 调试和分析工具
   - RenderDoc 集成
   - Nsight Graphics 使用
   - GPU Profiling
   - Shader 调试技巧
   - 性能分析工作流

8. **[08-asset-pipeline.md](08-asset-pipeline.md)** - 完整资产管线
   - 从 DCC 工具到 Filament 的完整流程
   - 资产管线自动化
   - 批处理脚本
   - CI/CD 集成
   - 版本控制最佳实践

9. **[09-advanced-usage.md](09-advanced-usage.md)** - 高级用法和技巧
   - 自定义工具集成
   - 工具源码扩展
   - 性能优化技巧
   - 故障排除指南
   - 常见问题 FAQ

---

## 🎯 学习路径

### 路径 1: 材质开发者

如果你主要开发和编译材质：

1. **快速入门**: 阅读 `01-tools-overview.md` 了解工具生态
2. **核心工具**: 深入学习 `02-matc-compiler.md` 掌握材质编译
3. **纹理处理**: 学习 `05-mipgen.md` 优化纹理
4. **调试技巧**: 参考 `07-debugging-tools.md` 调试 Shader
5. **完整流程**: 查看 `08-asset-pipeline.md` 组织工作流

**推荐阅读顺序**: 01 → 02 → 05 → 07 → 08

**适合人群**: 材质美术、技术美术、Shader 程序员

### 路径 2: 环境和光照美术

如果你负责场景光照和环境：

1. **工具概览**: 阅读 `01-tools-overview.md`
2. **IBL 核心**: 重点学习 `03-cmgen-ibl.md` 掌握环境贴图生成
3. **纹理优化**: 学习 `05-mipgen.md` 处理天空盒纹理
4. **完整流程**: 查看 `08-asset-pipeline.md` 建立工作流

**推荐阅读顺序**: 01 → 03 → 05 → 08

**参考文档**:
- `../engine/06-lighting-system.md` - Filament 光照系统
- `../graphics/02-pbr-theory.md` - PBR 理论基础

**适合人群**: 光照美术、环境美术、技术美术

### 路径 3: 3D 模型美术

如果你准备 3D 模型资产：

1. **工具生态**: 先学习 `01-tools-overview.md`
2. **网格处理**: 重点掌握 `04-filamesh.md` 网格转换
3. **glTF 工具**: 学习 `06-gltf-tools.md` 使用 glTF 工作流
4. **资产管线**: 阅读 `08-asset-pipeline.md` 建立完整流程

**推荐阅读顺序**: 01 → 04 → 06 → 08

**参考文档**:
- `../gltf/06-blender-workflow.md` - Blender 工作流程
- `../gltf/08-performance-optimization.md` - 资产优化

**适合人群**: 3D 美术、角色美术、场景美术

### 路径 4: 渲染工程师

如果你开发渲染系统或优化性能：

1. **全面学习**: 按顺序阅读所有文档 (01-09)
2. **重点深入**:
   - 调试和性能分析: `07-debugging-tools.md`
   - 资产管线自动化: `08-asset-pipeline.md`
   - 高级技巧: `09-advanced-usage.md`
3. **工具源码**: 研究 Filament 工具源码进行扩展

**推荐阅读顺序**: 01 → 02 → 03 → 04 → 05 → 06 → 07 → 08 → 09

**参考文档**:
- `../backend/` - 了解 Backend 实现
- `../engine/09-complete-pipeline.md` - 完整渲染管线

**适合人群**: 渲染工程师、引擎程序员、技术总监

---

## 🔗 与 docs_my 其他文档的关系

本目录的工具知识是连接理论和实践的桥梁：

### material 目录 (材质系统)
- `material/07-tools-usage.md` - **直接依赖**: matc 工具的基础使用
- `material/02-compilation-pipeline.md` - **底层原理**: matc 内部编译流程
- `02-matc-compiler.md` 本文档提供更详细的命令行用法和最佳实践

### engine 目录 (引擎架构)
- `engine/06-lighting-system.md` - **使用场景**: cmgen 生成的 IBL 资源如何在引擎中使用
- `03-cmgen-ibl.md` 本文档讲解如何生成这些资源

### graphics 目录 (图形学理论)
- `graphics/06-texture-system.md` - **理论基础**: Mipmap 原理
- `05-mipgen.md` 本文档讲解如何实际生成 Mipmap

### gltf 目录 (glTF 格式)
- `gltf/06-blender-workflow.md` - **工作流集成**: glTF 导出后的处理
- `06-gltf-tools.md` 本文档提供 glTF 相关工具的详细使用

---

## 🌟 核心工具速查

### matc - 材质编译器

```bash
# 基本用法
matc -o output.filamat input.mat

# 指定 Backend
matc -p mobile -a opengl -o material.filamat input.mat

# 优化编译
matc -O -o optimized.filamat input.mat
```

**用途**: 将 .mat 材质定义编译成 .filamat 二进制包

**详细文档**: [02-matc-compiler.md](02-matc-compiler.md)

### cmgen - IBL 生成器

```bash
# 生成 IBL 资源
cmgen -x . --format=ktx --size=256 --extract-blur=0.1 environment.exr

# 输出:
# - environment_ibl.ktx (镜面反射 cubemap)
# - environment_skybox.ktx (天空盒)
# - environment.txt (球谐系数)
```

**用途**: 从 HDR 环境贴图生成 IBL 资源

**详细文档**: [03-cmgen-ibl.md](03-cmgen-ibl.md)

### filamesh - 网格转换器

```bash
# OBJ 转 filamesh
filamesh input.obj output.filamesh

# 生成切线空间
filamesh --tangents input.obj output.filamesh
```

**用途**: 将各种 3D 格式转换为优化的 .filamesh

**详细文档**: [04-filamesh.md](04-filamesh.md)

### mipgen - Mipmap 生成器

```bash
# 生成 Mipmap
mipgen --format=ktx input.png output.ktx

# 使用 Kaiser 滤波器
mipgen --kernel=kaiser --format=ktx input.png output.ktx
```

**用途**: 为纹理生成 Mipmap 链

**详细文档**: [05-mipgen.md](05-mipgen.md)

### gltf_viewer - glTF 查看器

```bash
# 查看 glTF 文件
gltf_viewer model.glb

# 指定 IBL 环境
gltf_viewer --ibl=environment_ibl.ktx model.glb
```

**用途**: 实时预览 glTF 模型

**详细文档**: [06-gltf-tools.md](06-gltf-tools.md)

---

## ⚡ 常见工作流速查

### 材质开发工作流

```bash
# 1. 编写材质定义
vim my_material.mat

# 2. 编译材质 (桌面平台)
matc -p desktop -a vulkan -o my_material.filamat my_material.mat

# 3. 编译材质 (移动平台)
matc -p mobile -a opengl -o my_material_mobile.filamat my_material.mat

# 4. 在应用中加载
# C++ 代码:
# Material* mat = Material::Builder()
#     .package(data, size)
#     .build(*engine);
```

### 环境光照工作流

```bash
# 1. 准备 HDR 环境贴图 (从 HDRI Haven 等网站下载)
# 文件: studio_small_08_4k.exr

# 2. 生成 IBL 资源
cmgen -x ./ibl_output \
      --format=ktx \
      --size=256 \
      --extract-blur=0.1 \
      studio_small_08_4k.exr

# 3. 在应用中加载
# 加载 ibl_output/studio_small_08_4k_ibl.ktx (反射)
# 加载 ibl_output/studio_small_08_4k_skybox.ktx (天空盒)
# 加载 ibl_output/studio_small_08_4k.txt (SH 系数)
```

### 模型准备工作流

```bash
# 方法 1: 使用 filamesh (传统格式)
filamesh --tangents model.obj model.filamesh

# 方法 2: 使用 glTF (推荐)
# 在 Blender 中导出 .glb
# 直接使用 gltfio 加载，无需转换

# 方法 3: glTF 优化
gltfpack -i model.glb -o model_optimized.glb
```

### 纹理准备工作流

```bash
# 1. 生成 Mipmap
mipgen --format=ktx --compression=etc2 albedo.png albedo.ktx

# 2. 如果是 HDR 纹理
mipgen --format=ktx --compression=astc_hdr skybox.exr skybox.ktx

# 3. 批处理多个纹理
for file in textures/*.png; do
    mipgen --format=ktx "$file" "${file%.png}.ktx"
done
```

---

## 📦 工具获取和安装

### 预编译二进制 (推荐)

从 [Filament Releases](https://github.com/google/filament/releases) 下载预编译包：

```bash
# macOS / Linux
wget https://github.com/google/filament/releases/download/v1.x.x/filament-v1.x.x-linux.tgz
tar -xzf filament-v1.x.x-linux.tgz
export PATH=$PATH:$PWD/filament/bin

# Windows
# 下载 filament-v1.x.x-windows.tgz
# 解压后将 bin/ 目录添加到 PATH
```

### 从源码编译

```bash
# 克隆仓库
git clone https://github.com/google/filament.git
cd filament

# 构建 (macOS/Linux)
./build.sh release

# 构建 (Windows)
build.bat release

# 工具位置
# out/release/filament/bin/
```

### 验证安装

```bash
# 检查工具版本
matc --version
cmgen --version
filamesh --version
mipgen --version
gltf_viewer --version
```

---

## 🛠 推荐工具组合

### 基础组合 (必备)

- **matc**: 材质编译
- **cmgen**: IBL 生成
- **mipgen**: 纹理处理

### 完整组合 (推荐)

上述基础工具 +

- **gltf_viewer**: glTF 预览
- **filamesh**: 传统格式转换

### 专业组合 (高级)

上述完整工具 +

- **RenderDoc**: GPU 调试
- **gltfpack**: glTF 优化
- **Substance Painter**: PBR 纹理创作
- **Blender**: 3D 建模和导出

---

## 📝 版本信息

- **文档版本**: 1.0
- **创建日期**: 2025-10-20
- **适用 Filament 版本**: 最新主分支
- **更新策略**: 随 Filament 工具更新同步维护

---

## 🎓 使用建议

### 对于初学者

1. **先学理论**: 在使用工具前，先学习 `material/` 和 `graphics/` 相关理论
2. **从简单开始**: 从最简单的材质和纹理开始练习
3. **查看示例**: Filament 的 `samples/` 目录有完整示例
4. **阅读错误**: 工具的错误信息通常很详细，认真阅读

### 对于进阶用户

1. **自动化流程**: 使用脚本批处理资源
2. **优化编译**: 研究编译器优化选项
3. **性能分析**: 使用 Profiling 工具分析瓶颈
4. **扩展工具**: 根据需求修改工具源码

### 文档约定

- 📐 **流程图**: 使用 ASCII 图展示工作流
- 💡 **命令示例**: 完整的命令行示例
- ⚠️ **注意事项**: 常见陷阱和最佳实践
- 🔗 **跨文档引用**: 相关知识点链接

---

## 💡 核心理念

### 工具链的作用

```
理论知识 (docs_my/graphics, material, engine)
    ↓
工具链 (本目录)  ← 连接理论和实践
    ↓
运行时资源 (应用中加载使用)
```

工具链是将你的创意和设计转化为 Filament 可以理解和渲染的资源的桥梁。掌握工具链，是高效开发的关键。

### 最佳实践

1. **版本匹配**: 工具版本必须与运行时 Filament 版本匹配
2. **平台针对**: 为不同平台编译不同的材质包
3. **资源优化**: 使用压缩和优化选项减小资源大小
4. **自动化**: 建立自动化的资产管线，避免手动重复操作

---

## 🚀 开始学习

选择适合你的学习路径，从第一篇文档开始，系统掌握 Filament 工具链！

> "工具链是理论到实践的桥梁" - 掌握工具，让你的创意变为现实！
