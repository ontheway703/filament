# cmgen - IBL 环境贴图生成器

## 概述

`cmgen` (Cubemap Generator) 是 Filament 的 IBL (Image-Based Lighting) 资源生成工具。它将 HDR 环境贴图转换为 Filament 运行时需要的优化格式，包括镜面反射预过滤贴图、漫反射球谐系数和天空盒纹理。

### cmgen 的作用

```
输入: HDR 环境贴图 (.exr, .hdr)
  ↓
cmgen 处理
  ├─ 球谐系数计算 (漫反射 IBL)
  ├─ 镜面反射预过滤 (GGX 分布)
  ├─ 天空盒 Cubemap 生成
  └─ 格式转换和压缩
  ↓
输出:
  ├─ xxx_ibl.ktx (镜面反射 Cubemap)
  ├─ xxx_skybox.ktx (天空盒 Cubemap)
  └─ xxx.txt (球谐系数)
```

**为什么需要 IBL：**
- **真实感**: 基于真实环境的光照，比人工光源更自然
- **高效**: 预计算环境光，运行时只需简单采样
- **艺术控制**: 通过更换 HDRI 快速改变场景氛围

---

## 基本用法

### 最简单的生成

```bash
# 生成 IBL 资源
cmgen -x . environment.exr

# 输出文件：
# - environment_ibl.ktx
# - environment_skybox.ktx
# - environment.txt
```

### 推荐的生成命令

```bash
# 标准 IBL 生成（256x256 分辨率）
cmgen -x ./output \
      --format=ktx \
      --size=256 \
      --extract-blur=0.1 \
      environment.exr

# 高质量版本（512x512）
cmgen -x ./output \
      --format=ktx \
      --size=512 \
      --extract-blur=0.08 \
      environment.hdr
```

### 常用参数组合

```bash
# 移动端优化（小文件）
cmgen -x ./mobile \
      --format=ktx \
      --size=128 \
      --extract-blur=0.15 \
      --compression=etc2 \
      studio.exr

# 桌面端高质量
cmgen -x ./desktop \
      --format=ktx \
      --size=512 \
      --extract-blur=0.05 \
      outdoor.exr

# 仅生成球谐系数（不需要天空盒）
cmgen -x . --sh-compute environment.exr
```

---

## 命令行参数详解

### 核心参数

#### `-x, --extract <directory>`

指定输出目录：

```bash
# 输出到当前目录
cmgen -x . environment.exr

# 输出到指定目录
cmgen -x ./ibl_assets environment.exr

# 目录会自动创建
cmgen -x ./assets/ibl/outdoor outdoor.exr
```

#### `--size <width>`

指定输出 Cubemap 的分辨率（单个面的宽度）：

```bash
# 128x128 (移动端)
cmgen --size=128 -x . env.exr

# 256x256 (推荐，平衡质量和性能)
cmgen --size=256 -x . env.exr

# 512x512 (桌面高质量)
cmgen --size=512 -x . env.exr

# 1024x1024 (极高质量，文件大)
cmgen --size=1024 -x . env.exr
```

**分辨率选择建议：**

| 平台 | 推荐分辨率 | 用途 |
|------|-----------|------|
| 移动端 | 128-256 | 游戏、AR 应用 |
| 桌面端 | 256-512 | 实时渲染 |
| 离线渲染 | 512-1024 | 高质量预览 |

#### `--format <format>`

指定输出格式：

```bash
# KTX 格式（推荐，支持压缩）
cmgen --format=ktx -x . env.exr

# KTX2 格式（新标准，更好的压缩）
cmgen --format=ktx2 -x . env.exr

# PNG 格式（调试用，未压缩）
cmgen --format=png -x . env.exr

# HDR 格式（保留高动态范围）
cmgen --format=hdr -x . env.exr
```

**格式对比：**

| 格式 | 压缩支持 | 文件大小 | 质量 | 加载速度 |
|------|---------|---------|------|---------|
| PNG | 无 | 大 | 高 | 慢 |
| KTX | 是 | 小 | 高 | 快 |
| KTX2 | 是 | 很小 | 高 | 很快 |
| HDR | 无 | 中 | 极高 | 中 |

**推荐：生产环境使用 KTX/KTX2**

### IBL 质量参数

#### `--extract-blur <roughness>`

控制环境贴图模糊度（roughness 值）：

```bash
# 轻微模糊（清晰反射）
cmgen --extract-blur=0.05 -x . env.exr

# 标准模糊（推荐）
cmgen --extract-blur=0.1 -x . env.exr

# 较大模糊（柔和环境光）
cmgen --extract-blur=0.2 -x . env.exr
```

**参数说明：**
- 值范围：0.0 - 1.0
- 值越小 → 反射越清晰，但需要更高的 mipmap 级数
- 值越大 → 反射越模糊，但文件更小

**视觉效果：**

| blur 值 | 效果 | 适用场景 |
|---------|------|---------|
| 0.05 | 镜面清晰 | 金属、玻璃 |
| 0.1 | 标准反射 | 通用场景 |
| 0.2 | 柔和反射 | 粗糙表面 |

#### `--sh-compute`

仅计算球谐系数（漫反射 IBL）：

```bash
cmgen --sh-compute -x . environment.exr
```

**用途：**
- 快速生成漫反射光照
- 不需要天空盒时使用
- 调试球谐系数

#### `--no-mirror`

禁用镜像优化：

```bash
cmgen --no-mirror -x . environment.exr
```

**说明：**
- 默认情况下，cmgen 假设环境是镜像对称的
- 使用此选项可以保留完整的非对称环境

### 压缩选项

#### `--compression <method>`

指定纹理压缩方式：

```bash
# ETC2 压缩（Android）
cmgen --format=ktx --compression=etc2 -x . env.exr

# ASTC 压缩（移动端，最佳质量）
cmgen --format=ktx --compression=astc -x . env.exr

# BC7 压缩（桌面 Windows）
cmgen --format=ktx --compression=bc7 -x . env.exr

# 无压缩
cmgen --format=ktx --compression=none -x . env.exr
```

**压缩格式对比：**

| 压缩格式 | 平台 | 压缩比 | 质量 | 解码速度 |
|---------|------|--------|------|---------|
| ETC2 | Android | 4:1 | 中 | 快 |
| ASTC | 移动端 | 可变 | 高 | 快 |
| BC7 | Windows | 4:1 | 高 | 快 |
| None | 全平台 | 1:1 | 最高 | 最快 |

**平台建议：**

```bash
# Android
cmgen --compression=etc2 -x ./android env.exr

# iOS
cmgen --compression=astc -x ./ios env.exr

# Desktop
cmgen --compression=bc7 -x ./desktop env.exr

# 通用（未压缩）
cmgen --compression=none -x ./universal env.exr
```

### 输出控制

#### `--deploy <directory>`

部署到目标目录（自动创建结构）：

```bash
cmgen --deploy=./assets/ibl environment.exr
```

#### `--quiet`

静默模式，不输出详细信息：

```bash
cmgen --quiet -x . environment.exr
```

#### `--jobs <count>`

指定并行任务数：

```bash
# 使用 4 个线程
cmgen --jobs=4 -x . environment.exr

# 使用所有可用核心
cmgen --jobs=0 -x . environment.exr
```

---

## 完整示例

### 示例 1: 室内场景 IBL

**场景：** 室内工作室光照

**输入：** `studio_small.exr` (4K HDR 环境贴图)

```bash
# 生成标准质量 IBL
cmgen -x ./ibl/studio \
      --format=ktx \
      --size=256 \
      --extract-blur=0.1 \
      --compression=etc2 \
      studio_small.exr

# 输出：
# ./ibl/studio/studio_small_ibl.ktx (镜面反射)
# ./ibl/studio/studio_small_skybox.ktx (天空盒)
# ./ibl/studio/studio_small.txt (SH 系数)
```

**在 Filament 中加载：**

```cpp
// 加载 IBL 资源
Texture* reflectionMap = loadKTX(engine, "ibl/studio/studio_small_ibl.ktx");
Texture* skyboxMap = loadKTX(engine, "ibl/studio/studio_small_skybox.ktx");
float3 sh[9] = loadSH("ibl/studio/studio_small.txt");

// 创建 IndirectLight
IndirectLight* ibl = IndirectLight::Builder()
    .reflections(reflectionMap)
    .irradiance(3, sh)
    .intensity(30000.0f)
    .build(*engine);

scene->setIndirectLight(ibl);

// 创建 Skybox
Skybox* skybox = Skybox::Builder()
    .environment(skyboxMap)
    .build(*engine);

scene->setSkybox(skybox);
```

### 示例 2: 室外场景 IBL

**场景：** 晴天室外环境

**输入：** `outdoor_sunny.hdr`

```bash
# 高质量室外 IBL（桌面端）
cmgen -x ./ibl/outdoor \
      --format=ktx \
      --size=512 \
      --extract-blur=0.08 \
      --compression=bc7 \
      outdoor_sunny.hdr

# 移动端优化版本
cmgen -x ./ibl/outdoor_mobile \
      --format=ktx \
      --size=128 \
      --extract-blur=0.15 \
      --compression=etc2 \
      outdoor_sunny.hdr
```

### 示例 3: 批量生成脚本

**Bash 脚本 `generate_ibl.sh`：**

```bash
#!/bin/bash

HDR_DIR="hdri_source"
OUTPUT_DIR="ibl_assets"
SIZE=256
BLUR=0.1
FORMAT="ktx"
COMPRESSION="etc2"

mkdir -p "$OUTPUT_DIR"

for hdr_file in "$HDR_DIR"/*.{exr,hdr}; do
    [ -f "$hdr_file" ] || continue

    filename=$(basename "$hdr_file")
    name="${filename%.*}"

    echo "Processing $name..."

    cmgen -x "$OUTPUT_DIR/$name" \
          --format="$FORMAT" \
          --size="$SIZE" \
          --extract-blur="$BLUR" \
          --compression="$COMPRESSION" \
          "$hdr_file"

    if [ $? -eq 0 ]; then
        echo "✓ $name generated successfully"
    else
        echo "✗ $name generation failed"
        exit 1
    fi
done

echo "All IBL assets generated!"
```

**使用：**

```bash
chmod +x generate_ibl.sh
./generate_ibl.sh
```

### 示例 4: 多平台构建

**多平台 IBL 生成脚本：**

```bash
#!/bin/bash

INPUT="environment.exr"
BASE_NAME="environment"

# Android (ETC2)
cmgen -x ./android \
      --format=ktx \
      --size=256 \
      --compression=etc2 \
      "$INPUT"

# iOS (ASTC)
cmgen -x ./ios \
      --format=ktx \
      --size=256 \
      --compression=astc \
      "$INPUT"

# Desktop (BC7)
cmgen -x ./desktop \
      --format=ktx \
      --size=512 \
      --compression=bc7 \
      "$INPUT"

# Web (未压缩，支持最广)
cmgen -x ./web \
      --format=ktx \
      --size=256 \
      --compression=none \
      "$INPUT"

echo "Multi-platform IBL assets generated!"
```

---

## IBL 理论基础

### 基于图像的光照 (IBL)

IBL 使用环境贴图提供真实的光照和反射，分为两部分：

#### 1. 漫反射 (Diffuse IBL)

使用**球谐函数 (Spherical Harmonics)** 表示：

```cpp
// 球谐系数（9 个 float3）
float3 sh[9];

// 计算漫反射光照
float3 irradiance = evaluateSH(normal, sh);
```

**cmgen 生成的 `.txt` 文件：**

```
# environment.txt
# Band 0
0.5 0.5 0.5
# Band 1
0.1 0.1 0.1
0.2 0.2 0.2
0.15 0.15 0.15
# Band 2
...
```

#### 2. 镜面反射 (Specular IBL)

使用**预过滤环境贴图 (Pre-filtered Environment Map)**：

```
Cubemap Mipmap 链:
  Level 0: roughness = 0.0 (镜面反射)
  Level 1: roughness = 0.2
  Level 2: roughness = 0.4
  Level 3: roughness = 0.6
  Level 4: roughness = 0.8
  Level 5: roughness = 1.0 (完全粗糙)
```

**在 Shader 中采样：**

```glsl
vec3 R = reflect(-V, N);  // 反射向量
float lod = roughness * maxLOD;  // 根据粗糙度选择 mipmap
vec3 reflection = textureLod(envMap, R, lod).rgb;
```

### 预过滤技术

cmgen 使用 **GGX 重要性采样** 预过滤镜面反射：

```cpp
// 对于每个像素和每个 mipmap 级别
for (int level = 0; level < mipLevels; ++level) {
    float roughness = level / (mipLevels - 1);

    for (each pixel) {
        vec3 N = pixelDirection;
        vec3 color = vec3(0);

        // 重要性采样
        for (int i = 0; i < sampleCount; ++i) {
            vec3 H = importanceSampleGGX(xi, roughness, N);
            vec3 L = reflect(-V, H);
            color += sampleEnvironment(L) * weight;
        }

        outputPixel = color / totalWeight;
    }
}
```

**优势：**
- 运行时只需简单的 Cubemap 采样
- 支持不同粗糙度的材质
- 性能高效

---

## HDR 环境贴图资源

### 推荐的 HDRI 网站

| 网站 | 类型 | 费用 | 质量 |
|------|------|------|------|
| [HDRI Haven](https://hdri-haven.com/) | 室内/室外 | 免费 | 高 |
| [Poly Haven](https://polyhaven.com/) | 综合 | 免费 | 极高 |
| [sIBL Archive](http://www.hdrlabs.com/sibl/archive.html) | 经典 | 免费 | 中 |
| [HDR Labs](http://www.hdrlabs.com/) | 精选 | 免费/付费 | 高 |

### HDRI 格式说明

#### .EXR (OpenEXR)

```bash
# 推荐：工业标准，支持高精度 HDR
cmgen -x . environment.exr
```

**特点：**
- 支持 16/32 位浮点
- 无损压缩
- 元数据丰富

#### .HDR (Radiance HDR)

```bash
# 常用：较小的文件
cmgen -x . environment.hdr
```

**特点：**
- RGBE 编码
- 文件较小
- 兼容性好

### HDRI 质量要求

**分辨率建议：**

| 用途 | 输入分辨率 | 输出分辨率 |
|------|-----------|----------|
| 移动游戏 | 2K (2048x1024) | 128-256 |
| 桌面游戏 | 4K (4096x2048) | 256-512 |
| 高质量渲染 | 8K (8192x4096) | 512-1024 |

**动态范围：**
- 最小：3-4 档 (EV)
- 推荐：6-8 档
- 理想：10+ 档

---

## 性能优化

### 文件大小优化

#### 1. 降低分辨率

```bash
# 128x128 (最小，~500KB)
cmgen --size=128 --compression=etc2 -x . env.exr

# 256x256 (推荐，~2MB)
cmgen --size=256 --compression=etc2 -x . env.exr

# 512x512 (高质量，~8MB)
cmgen --size=512 --compression=etc2 -x . env.exr
```

#### 2. 使用压缩

```bash
# 未压缩 (~10MB)
cmgen --size=256 --compression=none -x . env.exr

# ETC2 压缩 (~2MB)
cmgen --size=256 --compression=etc2 -x . env.exr

# ASTC 压缩 (~1.5MB)
cmgen --size=256 --compression=astc -x . env.exr
```

#### 3. 增大模糊度

```bash
# 清晰反射（更多 mipmap 级别，文件大）
cmgen --extract-blur=0.05 -x . env.exr

# 柔和反射（较少 mipmap 级别，文件小）
cmgen --extract-blur=0.2 -x . env.exr
```

### 运行时性能优化

#### 1. 仅加载必要的资源

```cpp
// 如果不需要天空盒，不加载
// 仅加载 IBL 反射贴图和 SH 系数
IndirectLight* ibl = IndirectLight::Builder()
    .reflections(reflectionMap)
    .irradiance(3, sh)
    .build(*engine);

// 不设置 Skybox
// scene->setSkybox(nullptr);
```

#### 2. 降低 IBL 强度

```cpp
// 较低强度可以使用更低精度的纹理
IndirectLight* ibl = IndirectLight::Builder()
    .reflections(reflectionMap)
    .intensity(5000.0f)  // 降低强度
    .build(*engine);
```

#### 3. 移动端特殊优化

```bash
# 移动端：低分辨率 + 高模糊度 + 压缩
cmgen --size=128 \
      --extract-blur=0.2 \
      --compression=etc2 \
      -x ./mobile \
      env.exr
```

---

## 调试和验证

### 验证生成的资源

#### 1. 检查文件

```bash
# 列出生成的文件
ls -lh ibl_output/

# 输出示例：
# environment_ibl.ktx (2.5 MB)
# environment_skybox.ktx (3.0 MB)
# environment.txt (1 KB)
```

#### 2. 使用 gltf_viewer 预览

```bash
gltf_viewer --ibl=environment_ibl.ktx model.glb
```

#### 3. 检查球谐系数

```bash
cat environment.txt
```

**正常输出：**

```
0.789474 0.743155 0.687277
-0.375488 -0.340539 -0.309106
...
```

### 常见问题排查

#### 问题 1: 环境贴图过暗/过亮

```bash
# 检查 HDR 范围
exrheader environment.exr

# 调整 IBL 强度（Filament 代码）
ibl->setIntensity(50000.0f);  // 增大强度
```

#### 问题 2: 反射不清晰

```bash
# 减小模糊度
cmgen --extract-blur=0.05 -x . env.exr

# 增大输出分辨率
cmgen --size=512 -x . env.exr
```

#### 问题 3: 文件过大

```bash
# 使用压缩
cmgen --compression=etc2 -x . env.exr

# 降低分辨率
cmgen --size=128 -x . env.exr
```

---

## 集成到构建系统

### CMake 集成

```cmake
# FindCmgen.cmake
find_program(CMGEN_EXECUTABLE
    NAMES cmgen
    PATHS ${FILAMENT_DIR}/bin
)

function(generate_ibl HDR_FILE OUTPUT_DIR)
    get_filename_component(HDR_NAME ${HDR_FILE} NAME_WE)

    add_custom_command(
        OUTPUT
            ${OUTPUT_DIR}/${HDR_NAME}_ibl.ktx
            ${OUTPUT_DIR}/${HDR_NAME}_skybox.ktx
            ${OUTPUT_DIR}/${HDR_NAME}.txt
        COMMAND ${CMGEN_EXECUTABLE}
            -x ${OUTPUT_DIR}
            --format=ktx
            --size=256
            --extract-blur=0.1
            --compression=etc2
            ${HDR_FILE}
        DEPENDS ${HDR_FILE}
        COMMENT "Generating IBL for ${HDR_NAME}"
    )
endfunction()

# 使用
generate_ibl(hdri/studio.exr ${CMAKE_BINARY_DIR}/ibl)
```

### Python 自动化脚本

```python
#!/usr/bin/env python3
import subprocess
import os
from pathlib import Path

def generate_ibl(input_hdr, output_dir, platform='mobile'):
    """生成 IBL 资源"""

    # 平台配置
    configs = {
        'mobile': {
            'size': 256,
            'blur': 0.1,
            'compression': 'etc2'
        },
        'desktop': {
            'size': 512,
            'blur': 0.08,
            'compression': 'bc7'
        }
    }

    config = configs.get(platform, configs['mobile'])

    # 构建命令
    cmd = [
        'cmgen',
        '-x', output_dir,
        '--format=ktx',
        f'--size={config["size"]}',
        f'--extract-blur={config["blur"]}',
        f'--compression={config["compression"]}',
        input_hdr
    ]

    # 执行
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode == 0:
        print(f'✓ Generated IBL for {input_hdr}')
    else:
        print(f'✗ Failed: {result.stderr}')
        return False

    return True

# 批量处理
hdr_dir = Path('hdri_source')
output_dir = Path('ibl_assets')

for hdr_file in hdr_dir.glob('*.{exr,hdr}'):
    generate_ibl(str(hdr_file), str(output_dir / hdr_file.stem))
```

---

## 总结

### cmgen 工作流

```
准备阶段:
  获取 HDR 环境贴图 (HDRI Haven 等)

生成阶段:
  HDR 贴图 → cmgen → IBL 资源 (KTX + SH)

集成阶段:
  加载 IBL 资源到 Filament Scene
```

### 最佳实践

1. **使用 KTX 格式**: 支持压缩，加载快
2. **合适的分辨率**: 移动端 128-256，桌面 256-512
3. **启用压缩**: 减小文件大小，降低内存占用
4. **调整模糊度**: 根据材质粗糙度范围调整
5. **多平台构建**: 为不同平台生成优化版本

### 相关文档

- **[../engine/06-lighting-system.md](../engine/06-lighting-system.md)** - Filament 光照系统
- **[../graphics/02-pbr-theory.md](../graphics/02-pbr-theory.md)** - PBR 理论和 IBL
- **[06-gltf-tools.md](./06-gltf-tools.md)** - gltf_viewer 使用 IBL

通过掌握 cmgen，您可以为 Filament 场景创建真实感的环境光照！
