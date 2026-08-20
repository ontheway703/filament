# mipgen - Mipmap 生成器

## 概述

`mipgen` 是 Filament 的纹理处理工具，为图片生成 Mipmap 链并转换为优化的 KTX/KTX2 格式。Mipmap 是提升纹理质量和性能的关键技术，可以：

- **提高渲染质量**: 消除纹理走样和摩尔纹
- **提升性能**: 减少纹理带宽和缓存未命中
- **优化内存**: 现代 GPU 硬件针对 Mipmap 优化
- **支持压缩**: 转换为 GPU 原生压缩格式

## 什么是 Mipmap

### 基本概念

Mipmap 是同一纹理的多级分辨率金字塔：

```
Level 0: 1024x1024 (原始分辨率) - 1,048,576 像素
Level 1: 512x512                 - 262,144 像素
Level 2: 256x256                 - 65,536 像素
Level 3: 128x128                 - 16,384 像素
Level 4: 64x64                   - 4,096 像素
Level 5: 32x32                   - 1,024 像素
Level 6: 16x16                   - 256 像素
Level 7: 8x8                     - 64 像素
Level 8: 4x4                     - 16 像素
Level 9: 2x2                     - 4 像素
Level 10: 1x1                    - 1 像素

总内存: 1,398,101 像素 ≈ 原始大小的 1.33 倍
```

### 为什么需要 Mipmap

**问题 1: 纹理走样 (Aliasing)**

当屏幕像素对应多个纹理像素时，会产生闪烁和摩尔纹：

```
没有 Mipmap:           有 Mipmap:
[远处地板]              [远处地板]
████▓▓▓▓░░░░           ▓▓▓▓▓▓▓▓
▓▓▓▓░░░░████           ▓▓▓▓▓▓▓▓
(闪烁、摩尔纹)          (平滑、稳定)
```

**问题 2: 性能浪费**

远处物体采样高分辨率纹理浪费带宽：

```
屏幕像素: 1px
对应纹理: 16x16 像素区域
实际需要: 只需采样 1 个低分辨率纹理像素

使用 Mipmap: GPU 自动选择合适分辨率
```

**问题 3: 缓存效率**

Mipmap 提高纹理缓存命中率：

```
Cache Miss Rate:
无 Mipmap:  40-60% miss
有 Mipmap:  5-15% miss (提升 3-4 倍!)
```

## 基本用法

### 最简单的转换

```bash
# PNG → KTX (未压缩)
mipgen input.png output.ktx

# 等价于
mipgen --format=ktx --compression=none input.png output.ktx
```

### 常用命令

```bash
# 1. 生成标准 KTX 文件
mipgen --format=ktx albedo.png albedo.ktx

# 2. 使用 ETC2 压缩 (移动端)
mipgen --format=ktx --compression=etc2 albedo.png albedo_mobile.ktx

# 3. 使用 UASTC 通用压缩 (KTX2)
mipgen --format=ktx2 --compression=uastc albedo.png albedo.ktx2

# 4. sRGB 色彩空间处理
mipgen --format=ktx --linear albedo.png albedo.ktx

# 5. 法线贴图专用处理
mipgen --format=ktx --kernel=NORMALS normal.png normal.ktx

# 6. 批量处理
for f in textures/*.png; do
    mipgen --compression=uastc "$f" "ktx/${f%.png}.ktx2"
done
```

## 命令行参数详解

### 输出格式 (`--format`)

| 格式 | 说明 | 兼容性 | 推荐场景 |
|------|------|--------|---------|
| `ktx` | KTX 1.0 格式 | 所有平台 | 通用，兼容性最好 |
| `ktx2` | KTX 2.0 格式 | 较新 GPU | 支持 Basis Universal |
| `dds` | DirectDraw Surface | Windows | D3D 应用 |
| `png` | PNG 序列 | 所有平台 | 调试用 |

**默认**: `ktx`

**示例**:
```bash
# KTX 2.0 (推荐新项目)
mipgen --format=ktx2 --compression=uastc input.png output.ktx2

# DDS (Windows/D3D)
mipgen --format=dds --compression=bc7 input.png output.dds

# PNG 序列 (调试)
mipgen --format=png input.png output_mip.png
# 生成: output_mip_0.png, output_mip_1.png, ...
```

### 压缩格式 (`--compression`)

#### 移动端压缩 (OpenGL ES)

| 格式 | 压缩比 | 质量 | 支持平台 | Alpha | 说明 |
|------|--------|------|----------|-------|------|
| `etc2` | 4:1 / 8:1 | 高 | Android, iOS | ✅ | **推荐移动端 RGB(A)** |
| `astc` | 可变 | 极高 | 新移动端 | ✅ | 灵活块大小 |
| `pvrtc` | 4:1 / 2:1 | 中 | iOS (旧) | ⚠️ | iOS 传统格式 |

**ETC2 示例**:
```bash
# RGB 纹理 (4:1 压缩)
mipgen --format=ktx --compression=etc2 albedo.png albedo.ktx

# RGBA 纹理 (8:1 压缩)
mipgen --format=ktx --compression=etc2 albedo_alpha.png albedo_alpha.ktx

# sRGB 色彩空间
mipgen --format=ktx --compression=etc2 --linear albedo.png albedo_srgb.ktx
```

**ASTC 示例**:
```bash
# 4x4 块 (最高质量, 8:1 压缩)
mipgen --format=ktx --compression=astc_4x4 albedo.png albedo_astc.ktx

# 6x6 块 (平衡, 18:1 压缩)
mipgen --format=ktx --compression=astc_6x6 albedo.png albedo_astc.ktx

# 8x8 块 (高压缩, 32:1 压缩)
mipgen --format=ktx --compression=astc_8x8 albedo.png albedo_astc.ktx
```

#### 桌面端压缩 (Desktop)

| 格式 | 压缩比 | 质量 | 支持平台 | Alpha | 说明 |
|------|--------|------|----------|-------|------|
| `bc1` (DXT1) | 6:1 | 中 | PC | ❌ | **RGB 纹理** |
| `bc3` (DXT5) | 4:1 | 中 | PC | ✅ | **RGBA 纹理** |
| `bc5` | 4:1 | 高 | PC | - | **法线贴图专用** |
| `bc7` | 4:1 | 极高 | PC (DX11+) | ✅ | **最佳质量 RGBA** |

**BC 系列示例**:
```bash
# BC1 - RGB 纹理 (无 Alpha)
mipgen --format=dds --compression=bc1 albedo.png albedo_bc1.dds

# BC3 - RGBA 纹理
mipgen --format=dds --compression=bc3 albedo_alpha.png albedo_bc3.dds

# BC5 - 法线贴图 (两通道)
mipgen --format=dds --compression=bc5 normal.png normal_bc5.dds

# BC7 - 最高质量 RGBA
mipgen --format=dds --compression=bc7 albedo.png albedo_bc7.dds
```

#### 通用压缩 (Universal)

| 格式 | 压缩比 | 质量 | 特点 | 推荐度 |
|------|--------|------|------|--------|
| `uastc` | 可变 | 极高 | Basis Universal, 运行时转码 | ⭐⭐⭐⭐⭐ |
| `etc1s` | 高 | 中 | Basis Universal, 高压缩 | ⭐⭐⭐ |

**UASTC (推荐)**:
```bash
# UASTC - 最高质量通用压缩
mipgen --format=ktx2 --compression=uastc albedo.png albedo.ktx2

# 运行时自动转码到:
# - Android: ETC2
# - iOS: ASTC
# - Desktop: BC7
# - WebGL: ETC2/ASTC/BC
```

### 色彩空间 (`--linear`)

纹理类型不同，色彩空间处理不同：

| 纹理类型 | 存储格式 | 使用 --linear | 说明 |
|----------|---------|--------------|------|
| BaseColor / Albedo | sRGB | **❌ 不使用** | 已经是 sRGB |
| Emissive | sRGB | **❌ 不使用** | 发光颜色 |
| Normal Map | Linear | **✅ 使用** | 向量数据 |
| Metallic | Linear | **✅ 使用** | 标量数据 |
| Roughness | Linear | **✅ 使用** | 标量数据 |
| AO | Linear | **✅ 使用** | 遮蔽系数 |
| Height | Linear | **✅ 使用** | 高度数据 |

**正确示例**:
```bash
# ❌ 错误 - BaseColor 不需要 --linear
mipgen --linear albedo.png albedo.ktx  # 颜色会过暗!

# ✅ 正确 - BaseColor 不使用 --linear
mipgen albedo.png albedo.ktx

# ✅ 正确 - 法线贴图使用 --linear
mipgen --linear --kernel=NORMALS normal.png normal.ktx

# ✅ 正确 - ORM 贴图使用 --linear
mipgen --linear orm.png orm.ktx
```

**Gamma 校正细节**:

```
不使用 --linear (默认):
  输入 sRGB → 转换到 Linear → 生成 Mipmap → 转换回 sRGB → 输出

使用 --linear:
  输入 Linear → 保持 Linear → 生成 Mipmap → 保持 Linear → 输出
```

### Mipmap 过滤核 (`--kernel`)

生成 Mipmap 的降采样算法：

| 核函数 | 说明 | 适用纹理 | 质量 |
|--------|------|----------|------|
| `BOX` | 简单平均 | - | 低 (快速) |
| `GAUSSIAN` | 高斯模糊 | Albedo, Emissive | 中 (默认) |
| `LANCZOS` | Lanczos 重采样 | 高细节纹理 | 高 (慢) |
| `NORMALS` | 法线归一化 | **法线贴图** | 特殊 |
| `MITCHELL` | Mitchell-Netravali | 通用 | 高 |

**默认**: `GAUSSIAN`

**法线贴图专用**:
```bash
# ✅ 正确 - 法线贴图必须使用 NORMALS 核
mipgen --kernel=NORMALS --linear normal.png normal.ktx

# ❌ 错误 - 不使用 NORMALS 会导致法线长度错误
mipgen --linear normal.png normal.ktx  # 法线不归一化!
```

**为什么法线贴图需要特殊处理**:

```
普通 Mipmap (错误):
  (0.5, 0.5, 1.0) + (0.5, 0.5, 1.0)
  --------------------------------- = (0.5, 0.5, 1.0)
                2
  长度 = sqrt(0.5² + 0.5² + 1²) = 1.22 ✗ (不是单位向量!)

NORMALS 核 (正确):
  normalize((0.5, 0.5, 1.0) + (0.5, 0.5, 1.0)) = (0.41, 0.41, 0.82) ✓
  长度 = 1.0 ✓
```

**高质量 Albedo**:
```bash
# Lanczos - 最高质量 (慢)
mipgen --kernel=LANCZOS albedo.png albedo_hq.ktx

# Mitchell - 高质量 (较快)
mipgen --kernel=MITCHELL albedo.png albedo_hq.ktx
```

### 其他参数

| 参数 | 说明 | 默认值 | 示例 |
|------|------|--------|------|
| `--levels=N` | 生成 N 级 Mipmap | 全部 | `--levels=4` |
| `--quiet` | 静默模式 | false | `--quiet` |
| `--strip-alpha` | 移除 Alpha 通道 | false | `--strip-alpha` |

**限制 Mipmap 级数**:
```bash
# 只生成 4 级 (1024 → 512 → 256 → 128)
mipgen --levels=4 input.png output.ktx

# 用途: 节省内存，避免过小纹理模糊
```

## 完整工作流程示例

### PBR 材质纹理处理

假设你有一套 PBR 纹理：
- `chair_albedo.png` (sRGB, RGBA)
- `chair_normal.png` (Linear, RGB)
- `chair_orm.png` (Linear, RGB - Occlusion/Roughness/Metallic)
- `chair_emissive.png` (sRGB, RGB)

**移动端 (Android/iOS)**:

```bash
#!/bin/bash

# Albedo - sRGB, ETC2 压缩
mipgen --format=ktx \
       --compression=etc2 \
       chair_albedo.png \
       chair_albedo_mobile.ktx

# Normal - Linear, ETC2, 法线核
mipgen --format=ktx \
       --compression=etc2 \
       --linear \
       --kernel=NORMALS \
       chair_normal.png \
       chair_normal_mobile.ktx

# ORM - Linear, ETC2
mipgen --format=ktx \
       --compression=etc2 \
       --linear \
       chair_orm.png \
       chair_orm_mobile.ktx

# Emissive - sRGB, ETC2
mipgen --format=ktx \
       --compression=etc2 \
       chair_emissive.png \
       chair_emissive_mobile.ktx
```

**桌面端 (PC)**:

```bash
#!/bin/bash

# Albedo - sRGB, BC7
mipgen --format=dds \
       --compression=bc7 \
       chair_albedo.png \
       chair_albedo_pc.dds

# Normal - Linear, BC5 (两通道优化)
mipgen --format=dds \
       --compression=bc5 \
       --linear \
       --kernel=NORMALS \
       chair_normal.png \
       chair_normal_pc.dds

# ORM - Linear, BC7
mipgen --format=dds \
       --compression=bc7 \
       --linear \
       chair_orm.png \
       chair_orm_pc.dds

# Emissive - sRGB, BC7
mipgen --format=dds \
       --compression=bc7 \
       chair_emissive.png \
       chair_emissive_pc.dds
```

**通用方案 (UASTC)**:

```bash
#!/bin/bash

# 所有平台使用一套资源!
for tex in chair_albedo chair_emissive; do
    mipgen --format=ktx2 \
           --compression=uastc \
           "${tex}.png" \
           "${tex}.ktx2"
done

for tex in chair_normal chair_orm; do
    mipgen --format=ktx2 \
           --compression=uastc \
           --linear \
           "${tex}.png" \
           "${tex}.ktx2"
done

# 特殊处理法线
mipgen --format=ktx2 \
       --compression=uastc \
       --linear \
       --kernel=NORMALS \
       chair_normal.png \
       chair_normal.ktx2
```

### 批量转换脚本

**基础批量处理**:

```bash
#!/bin/bash

# 处理所有 PNG 到 KTX
for f in textures/*.png; do
    base=$(basename "$f" .png)

    # 识别纹理类型
    if [[ "$base" == *"albedo"* ]] || [[ "$base" == *"basecolor"* ]]; then
        mipgen --compression=uastc "$f" "ktx/${base}.ktx2"
    elif [[ "$base" == *"normal"* ]]; then
        mipgen --compression=uastc --linear --kernel=NORMALS "$f" "ktx/${base}.ktx2"
    elif [[ "$base" == *"orm"* ]] || [[ "$base" == *"roughness"* ]] || [[ "$base" == *"metallic"* ]]; then
        mipgen --compression=uastc --linear "$f" "ktx/${base}.ktx2"
    else
        mipgen --compression=uastc "$f" "ktx/${base}.ktx2"
    fi

    echo "Processed: $base"
done
```

**多平台批量处理**:

```bash
#!/bin/bash

MOBILE_COMPRESSION="etc2"
DESKTOP_COMPRESSION="bc7"

process_texture() {
    local input=$1
    local name=$(basename "$input" .png)
    local type=$2

    case $type in
        albedo|emissive)
            # sRGB 纹理
            mipgen --format=ktx --compression=$MOBILE_COMPRESSION \
                   "$input" "mobile/${name}.ktx"
            mipgen --format=dds --compression=$DESKTOP_COMPRESSION \
                   "$input" "desktop/${name}.dds"
            ;;
        normal)
            # 法线贴图
            mipgen --format=ktx --compression=$MOBILE_COMPRESSION \
                   --linear --kernel=NORMALS \
                   "$input" "mobile/${name}.ktx"
            mipgen --format=dds --compression=bc5 \
                   --linear --kernel=NORMALS \
                   "$input" "desktop/${name}.dds"
            ;;
        orm|roughness|metallic|ao)
            # Linear 数据
            mipgen --format=ktx --compression=$MOBILE_COMPRESSION \
                   --linear "$input" "mobile/${name}.ktx"
            mipgen --format=dds --compression=$DESKTOP_COMPRESSION \
                   --linear "$input" "desktop/${name}.dds"
            ;;
    esac
}

# 使用
process_texture "chair_albedo.png" "albedo"
process_texture "chair_normal.png" "normal"
process_texture "chair_orm.png" "orm"
```

## 压缩格式对比

### 文件大小对比

原始纹理: `1024x1024 RGBA PNG`

| 格式 | 文件大小 | 压缩比 | 加载速度 | 运行时内存 | 质量 |
|------|---------|--------|----------|------------|------|
| PNG (原始) | 2.5 MB | 1:1 | 慢 (CPU 解压) | 5.33 MB | 最高 |
| KTX (未压缩) | 5.33 MB | 0.5:1 | 极快 (直接上传) | 5.33 MB | 最高 |
| KTX + ETC2 | 0.67 MB | 8:1 | 极快 | 0.67 MB | 高 |
| KTX + ASTC 4x4 | 0.67 MB | 8:1 | 极快 | 0.67 MB | 极高 |
| KTX + ASTC 8x8 | 0.17 MB | 32:1 | 极快 | 0.17 MB | 中 |
| DDS + BC1 | 0.67 MB | 6:1 | 极快 | 0.67 MB | 中 |
| DDS + BC7 | 1.33 MB | 4:1 | 极快 | 1.33 MB | 极高 |
| KTX2 + UASTC | 1.5 MB | 3.5:1 | 快 (转码) | 0.67-1.33 MB | 极高 |

**内存计算**:
```
RGBA8 未压缩 = 1024 * 1024 * 4 bytes * 1.33 (Mipmap) = 5.33 MB
ETC2 = 1024 * 1024 / 2 bytes * 1.33 = 0.67 MB (8:1 压缩)
BC7 = 1024 * 1024 bytes * 1.33 = 1.33 MB (4:1 压缩)
```

### 视觉质量对比

| 纹理类型 | BC1 | BC3 | BC7 | ETC2 | ASTC 4x4 | UASTC |
|---------|-----|-----|-----|------|----------|-------|
| Albedo (平滑) | 7/10 | 8/10 | 9.5/10 | 8.5/10 | 9.5/10 | 9.5/10 |
| Albedo (高频) | 6/10 | 7/10 | 9/10 | 8/10 | 9/10 | 9/10 |
| Normal Map | 5/10 | 6/10 | 8/10 | 8/10 | 9/10 | 9/10 |
| UI Texture | 6/10 | 7/10 | 9.5/10 | 8/10 | 9.5/10 | 9.5/10 |

**建议**:
- **移动端**: ASTC 4x4 (新设备) 或 ETC2 (兼容性)
- **桌面端**: BC7 (DX11+) 或 BC3 (兼容)
- **跨平台**: UASTC (KTX2 + Basis Universal)

### 性能基准测试

**纹理加载时间** (1024x1024 纹理):

```
硬件: Snapdragon 888, Adreno 660

PNG → GPU:          45 ms (CPU 解码 + 上传)
KTX (未压缩) → GPU: 12 ms (直接上传)
KTX + ETC2 → GPU:   3 ms (直接上传, 压缩格式)
KTX2 + UASTC → GPU: 8 ms (硬件转码 + 上传)

结论: 压缩纹理快 15 倍!
```

**渲染性能** (带宽节省):

```
场景: 100 个物体, 每个 4 张 1024x1024 纹理

未压缩 RGBA8:
  总带宽: 100 * 4 * 5.33 MB = 2.13 GB/frame @ 1 mip level
  @ 60 FPS: 127 GB/s (超出移动端带宽!)

ETC2 压缩:
  总带宽: 100 * 4 * 0.67 MB = 268 MB/frame
  @ 60 FPS: 16 GB/s (在移动端带宽内)

性能提升: 8 倍带宽节省!
```

## 最佳实践

### 1. 按纹理类型选择设置

```bash
# Albedo / BaseColor (sRGB 颜色)
mipgen --compression=uastc albedo.png albedo.ktx2

# Normal Map (Linear, 需要归一化)
mipgen --compression=uastc --linear --kernel=NORMALS normal.png normal.ktx2

# ORM (Occlusion/Roughness/Metallic, Linear)
mipgen --compression=uastc --linear orm.png orm.ktx2

# Metallic 单通道 (Linear)
mipgen --compression=uastc --linear --strip-alpha metallic.png metallic.ktx2

# Roughness 单通道 (Linear)
mipgen --compression=uastc --linear roughness.png roughness.ktx2

# Emissive (sRGB, 可能需要 HDR)
mipgen --compression=uastc emissive.png emissive.ktx2

# Height / Displacement (Linear)
mipgen --compression=uastc --linear height.png height.ktx2
```

### 2. 平台特定优化

**Android**:
```bash
# 推荐: ETC2 (所有 Android 设备)
mipgen --format=ktx --compression=etc2 input.png output.ktx

# 高端设备: ASTC 4x4
mipgen --format=ktx --compression=astc_4x4 input.png output.ktx

# 运行时检测:
if (hasASTCSupport) {
    load("texture_astc.ktx");
} else {
    load("texture_etc2.ktx");
}
```

**iOS**:
```bash
# iOS 12+: ASTC (推荐)
mipgen --format=ktx --compression=astc_4x4 input.png output.ktx

# iOS 旧设备: PVRTC (兼容)
mipgen --format=ktx --compression=pvrtc input.png output.ktx
```

**Windows**:
```bash
# DX11+: BC7 (最高质量)
mipgen --format=dds --compression=bc7 albedo.png albedo.dds

# 法线贴图: BC5 (两通道优化)
mipgen --format=dds --compression=bc5 --linear --kernel=NORMALS normal.png normal.dds

# DX9 兼容: BC3
mipgen --format=dds --compression=bc3 albedo.png albedo.dds
```

**Web (WebGL)**:
```bash
# 通用方案: UASTC (转码到 ETC2/BC)
mipgen --format=ktx2 --compression=uastc input.png output.ktx2

# 手动检测:
if (isDesktop) {
    transcode_to_BC7();
} else {
    transcode_to_ETC2();
}
```

### 3. 内存和质量平衡

```bash
# 最高质量 (大文件)
mipgen --kernel=LANCZOS --compression=uastc input.png output_hq.ktx2

# 平衡 (推荐)
mipgen --kernel=GAUSSIAN --compression=uastc input.png output.ktx2

# 最小文件 (质量损失)
mipgen --kernel=BOX --compression=etc1s input.png output_small.ktx2

# 自适应 ASTC 块大小
mipgen --compression=astc_4x4 important_texture.png important.ktx  # 高质量
mipgen --compression=astc_8x8 background.png background.ktx         # 高压缩
```

### 4. 与构建系统集成

**CMake 集成**:

```cmake
# FindMipgen.cmake
find_program(MIPGEN_EXECUTABLE mipgen
    HINTS ${FILAMENT_DIR}/bin
)

# 自定义命令
function(add_texture_target target_name input_file)
    get_filename_component(name ${input_file} NAME_WE)
    set(output_file ${CMAKE_CURRENT_BINARY_DIR}/${name}.ktx2)

    add_custom_command(
        OUTPUT ${output_file}
        COMMAND ${MIPGEN_EXECUTABLE}
            --format=ktx2
            --compression=uastc
            ${input_file}
            ${output_file}
        DEPENDS ${input_file}
        COMMENT "Generating Mipmap for ${input_file}"
    )

    add_custom_target(${target_name} ALL DEPENDS ${output_file})
endfunction()

# 使用
add_texture_target(chair_albedo_texture textures/chair_albedo.png)
```

**Gradle 集成 (Android)**:

```gradle
// build.gradle
task procesTextures {
    def mipgen = "${filamentDir}/bin/mipgen"
    def inputDir = file("src/main/assets/textures_src")
    def outputDir = file("src/main/assets/textures")

    doLast {
        fileTree(inputDir).matching { include "**/*.png" }.each { file ->
            def relativePath = inputDir.toPath().relativize(file.toPath())
            def outputFile = new File(outputDir, relativePath.toString().replace(".png", ".ktx"))

            exec {
                commandLine mipgen,
                    "--format=ktx",
                    "--compression=etc2",
                    file.absolutePath,
                    outputFile.absolutePath
            }
        }
    }
}

preBuild.dependsOn processTextures
```

### 5. 性能调优

**减少文件大小**:
```bash
# 使用更高压缩率
mipgen --compression=astc_8x8 background.png bg.ktx  # 32:1 压缩

# 限制 Mipmap 级数
mipgen --levels=6 huge_texture.png output.ktx  # 节省小 mip 空间

# 使用 ETC1S (高压缩)
mipgen --format=ktx2 --compression=etc1s input.png output.ktx2
```

**减少加载时间**:
```bash
# 使用原生压缩格式 (避免运行时转码)
mipgen --format=ktx --compression=etc2 input.png output.ktx  # Android 直接用

# 预生成 Mipmap (避免运行时生成)
mipgen input.png output.ktx  # 已包含所有 mip level
```

**提高质量**:
```bash
# 使用高质量过滤核
mipgen --kernel=LANCZOS albedo.png albedo_hq.ktx

# 使用低压缩格式
mipgen --compression=astc_4x4 albedo.png albedo_hq.ktx  # vs 8x8
```

## 常见问题

### Q1: 为什么 Albedo 使用 `--linear` 后变暗了？

**原因**: Albedo 是 sRGB 格式，不应使用 `--linear`。

```bash
# ❌ 错误
mipgen --linear albedo.png albedo.ktx  # 颜色变暗!

# ✅ 正确
mipgen albedo.png albedo.ktx
```

### Q2: 法线贴图为什么看起来不对？

**原因**: 没有使用 `--kernel=NORMALS` 归一化法线。

```bash
# ❌ 错误
mipgen --linear normal.png normal.ktx  # 法线长度错误

# ✅ 正确
mipgen --linear --kernel=NORMALS normal.png normal.ktx
```

### Q3: 如何选择压缩格式？

**决策树**:

```
需要跨平台?
  ├─ 是 → UASTC (KTX2)
  └─ 否 → 平台特定
      ├─ Android → ETC2 (兼容) / ASTC (高端)
      ├─ iOS → ASTC
      ├─ Windows → BC7 (DX11+) / BC3 (兼容)
      └─ Web → UASTC (转码)

法线贴图?
  └─ Windows → BC5 (两通道优化)
```

### Q4: 为什么 KTX 文件比 PNG 大？

**原因**: 未压缩 KTX 包含所有 Mipmap 级别和未压缩数据。

```
PNG:  2 MB (压缩的 Level 0)
KTX:  5.33 MB (未压缩的 Level 0-10)

解决: 使用压缩格式
KTX + ETC2: 0.67 MB (压缩的 Level 0-10)
```

### Q5: Mipmap 导致远处纹理模糊？

**原因**: 这是正常的 Mipmap 行为。可以调整 Mipmap bias。

```cpp
// Filament 中调整
materialInstance->setParameter("anisotropy", 8.0f);

// 或使用高质量过滤核
mipgen --kernel=LANCZOS input.png output.ktx
```

### Q6: 如何验证生成的 Mipmap？

```bash
# 生成 PNG 序列调试
mipgen --format=png input.png output_mip.png

# 查看所有级别
ls output_mip_*.png
output_mip_0.png  # 1024x1024
output_mip_1.png  # 512x512
output_mip_2.png  # 256x256
...
```

## 相关文档

- [04-filamesh.md](04-filamesh.md) - 网格转换
- [02-matc-compiler.md](02-matc-compiler.md) - 材质编译
- [03-cmgen-ibl.md](03-cmgen-ibl.md) - IBL 生成
- [../graphics/04-textures.md](../graphics/04-textures.md) - 纹理系统理论
- [../engine/03-resource-management.md](../engine/03-resource-management.md) - 纹理资源管理

## 总结

**mipgen 核心要点**:

1. **Mipmap 是必须的** - 提升质量和性能
2. **压缩格式节省内存** - ETC2/ASTC/BC 系列
3. **色彩空间很重要** - sRGB vs Linear
4. **法线贴图需要特殊处理** - `--kernel=NORMALS`
5. **跨平台使用 UASTC** - KTX2 + Basis Universal

**推荐工作流程**:

```bash
# 1. Albedo (sRGB)
mipgen --compression=uastc albedo.png albedo.ktx2

# 2. Normal (Linear + NORMALS 核)
mipgen --compression=uastc --linear --kernel=NORMALS normal.png normal.ktx2

# 3. ORM (Linear)
mipgen --compression=uastc --linear orm.png orm.ktx2

# 4. 批量处理集成到构建系统
```

通过正确使用 mipgen，你可以显著提升渲染质量和性能！
