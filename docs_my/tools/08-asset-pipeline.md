# 完整资产管线

## 概述

资产管线（Asset Pipeline）是将美术资源从 DCC 工具（Blender, Maya等）转换为 Filament 运行时资产的完整工作流程。一个良好的资产管线可以：

- ✅ **自动化构建** - 减少手动操作，避免错误
- ✅ **增量构建** - 只重新构建修改的资源
- ✅ **多平台支持** - 自动生成各平台优化的资源
- ✅ **版本控制** - 可追溯的构建历史
- ✅ **持续集成** - 与 CI/CD 集成

本文档介绍如何构建完整的 Filament 资产管线。

## 资产管线架构

### 完整流程图

```
┌─────────────────────────────────────────────────────────────┐
│                         DCC Tools                            │
│  (Blender, Maya, Substance Painter, Photoshop, etc.)       │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│                    Source Assets                             │
│  models/*.blend, textures/*.psd, materials/*.sbsar          │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│                   Export / Bake                              │
│  .blend → .glb, .psd → .png, .sbsar → textures             │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│                 Intermediate Assets                          │
│  models/*.glb, textures/*.png, environments/*.exr           │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│                  Filament Tools                              │
│  matc, cmgen, mipgen, gltf-transform                        │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│                   Compiled Assets                            │
│  *.filamat, *.ktx, *_ibl, *.glb (optimized)                 │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│                  Asset Packaging                             │
│  Bundle into .pak or filesystem structure                    │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│                  Filament Runtime                            │
│  Load and render assets in application                      │
└─────────────────────────────────────────────────────────────┘
```

### 目录结构

```
project/
├── assets/
│   ├── source/                    # 源文件（版本控制）
│   │   ├── models/
│   │   │   ├── chair.blend
│   │   │   └── table.ma
│   │   ├── textures/
│   │   │   ├── wood_albedo.psd
│   │   │   └── metal_roughness.tga
│   │   ├── materials/
│   │   │   └── wood.sbsar
│   │   └── environments/
│   │       └── studio.exr
│   │
│   ├── intermediate/              # 导出的中间格式（不提交）
│   │   ├── models/
│   │   │   ├── chair.glb
│   │   │   └── table.glb
│   │   ├── textures/
│   │   │   ├── wood_albedo.png
│   │   │   └── metal_orm.png
│   │   └── environments/
│   │       └── studio.exr
│   │
│   ├── compiled/                  # Filament 编译后资源（不提交）
│   │   ├── materials/
│   │   │   └── wood.filamat
│   │   ├── textures/
│   │   │   ├── wood_albedo.ktx
│   │   │   └── metal_orm.ktx
│   │   ├── models/
│   │   │   ├── chair.glb (optimized)
│   │   │   └── table.glb
│   │   └── ibl/
│   │       ├── studio_ibl/
│   │       └── studio_skybox.ktx
│   │
│   └── final/                     # 打包后资源（发布）
│       └── assets.pak
│
├── tools/
│   ├── build_assets.sh            # 主构建脚本
│   ├── build_materials.sh
│   ├── build_textures.sh
│   ├── build_models.sh
│   ├── build_ibl.sh
│   └── package.sh
│
└── CMakeLists.txt
```

## 完整构建脚本

### 主构建脚本

```bash
#!/bin/bash
# tools/build_assets.sh

set -e  # 遇到错误立即退出

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASSETS_SOURCE="$PROJECT_ROOT/assets/source"
ASSETS_INTERMEDIATE="$PROJECT_ROOT/assets/intermediate"
ASSETS_COMPILED="$PROJECT_ROOT/assets/compiled"
ASSETS_FINAL="$PROJECT_ROOT/assets/final"

FILAMENT_TOOLS="/path/to/filament/bin"  # 修改为实际路径

# 颜色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 创建目录
create_directories() {
    log_info "Creating directories..."
    mkdir -p "$ASSETS_INTERMEDIATE"/{models,textures,materials,environments}
    mkdir -p "$ASSETS_COMPILED"/{models,textures,materials,ibl}
    mkdir -p "$ASSETS_FINAL"
}

# 清理旧文件
clean() {
    log_info "Cleaning old assets..."
    rm -rf "$ASSETS_INTERMEDIATE"/*
    rm -rf "$ASSETS_COMPILED"/*
    rm -rf "$ASSETS_FINAL"/*
}

# 检查依赖
check_dependencies() {
    log_info "Checking dependencies..."

    local missing_deps=0

    # 检查 Filament 工具
    for tool in matc cmgen mipgen; do
        if [ ! -f "$FILAMENT_TOOLS/$tool" ]; then
            log_error "Missing tool: $tool"
            missing_deps=1
        fi
    done

    # 检查其他工具
    for cmd in blender gltf-transform gltf-validator; do
        if ! command -v $cmd &> /dev/null; then
            log_warn "Optional tool not found: $cmd"
        fi
    done

    if [ $missing_deps -eq 1 ]; then
        log_error "Missing required dependencies!"
        exit 1
    fi
}

# 主构建流程
main() {
    log_info "Starting asset build..."

    create_directories

    # 可选：清理
    if [ "$1" == "clean" ]; then
        clean
    fi

    check_dependencies

    # 执行各阶段构建
    log_info "Building materials..."
    ./tools/build_materials.sh

    log_info "Building textures..."
    ./tools/build_textures.sh

    log_info "Building models..."
    ./tools/build_models.sh

    log_info "Building IBL..."
    ./tools/build_ibl.sh

    log_info "Packaging assets..."
    ./tools/package.sh

    log_info "Asset build complete!"

    # 显示统计
    show_statistics
}

# 显示统计信息
show_statistics() {
    echo ""
    log_info "Build Statistics:"
    echo "  Source assets:       $(find $ASSETS_SOURCE -type f | wc -l)"
    echo "  Compiled materials:  $(find $ASSETS_COMPILED/materials -name "*.filamat" | wc -l)"
    echo "  Compiled textures:   $(find $ASSETS_COMPILED/textures -name "*.ktx" | wc -l)"
    echo "  Optimized models:    $(find $ASSETS_COMPILED/models -name "*.glb" | wc -l)"

    if [ -f "$ASSETS_FINAL/assets.pak" ]; then
        local size=$(du -h "$ASSETS_FINAL/assets.pak" | cut -f1)
        echo "  Final package size:  $size"
    fi
}

# 运行
main "$@"
```

### 材质构建脚本

```bash
#!/bin/bash
# tools/build_materials.sh

MATERIALS_SOURCE="$PROJECT_ROOT/assets/source/materials"
MATERIALS_COMPILED="$PROJECT_ROOT/assets/compiled/materials"
MATC="$FILAMENT_TOOLS/matc"

# 平台和 API 配置
PLATFORMS=("mobile" "desktop")
APIS_MOBILE=("opengl" "vulkan")
APIS_DESKTOP=("opengl" "vulkan" "metal")

build_material() {
    local input=$1
    local name=$(basename "$input" .mat)

    log_info "Building material: $name"

    # 移动端
    for api in "${APIS_MOBILE[@]}"; do
        local output="$MATERIALS_COMPILED/${name}_mobile_${api}.filamat"

        $MATC \
            -p mobile \
            -a $api \
            -O \
            -o "$output" \
            "$input"

        if [ $? -eq 0 ]; then
            log_info "  ✓ mobile/$api: $(du -h $output | cut -f1)"
        else
            log_error "  ✗ mobile/$api failed"
        fi
    done

    # 桌面端
    for api in "${APIS_DESKTOP[@]}"; do
        local output="$MATERIALS_COMPILED/${name}_desktop_${api}.filamat"

        $MATC \
            -p desktop \
            -a $api \
            -O \
            -o "$output" \
            "$input"

        if [ $? -eq 0 ]; then
            log_info "  ✓ desktop/$api: $(du -h $output | cut -f1)"
        else
            log_error "  ✗ desktop/$api failed"
        fi
    done
}

# 构建所有材质
find "$MATERIALS_SOURCE" -name "*.mat" | while read mat_file; do
    build_material "$mat_file"
done

log_info "Material build complete"
```

### 纹理构建脚本

```bash
#!/bin/bash
# tools/build_textures.sh

TEXTURES_SOURCE="$PROJECT_ROOT/assets/intermediate/textures"
TEXTURES_COMPILED="$PROJECT_ROOT/assets/compiled/textures"
MIPGEN="$FILAMENT_TOOLS/mipgen"

build_texture() {
    local input=$1
    local name=$(basename "$input" .png)
    local type=$2  # albedo, normal, orm, etc.

    log_info "Building texture: $name ($type)"

    # 移动端 (ETC2)
    local output_mobile="$TEXTURES_COMPILED/${name}_mobile.ktx"

    if [ "$type" == "normal" ]; then
        $MIPGEN \
            --format=ktx \
            --compression=etc2 \
            --linear \
            --kernel=NORMALS \
            "$input" \
            "$output_mobile"
    elif [ "$type" == "albedo" ] || [ "$type" == "emissive" ]; then
        $MIPGEN \
            --format=ktx \
            --compression=etc2 \
            "$input" \
            "$output_mobile"
    else
        # orm, roughness, metallic, ao
        $MIPGEN \
            --format=ktx \
            --compression=etc2 \
            --linear \
            "$input" \
            "$output_mobile"
    fi

    # 桌面端 (BC7/BC5)
    local output_desktop="$TEXTURES_COMPILED/${name}_desktop.dds"

    if [ "$type" == "normal" ]; then
        $MIPGEN \
            --format=dds \
            --compression=bc5 \
            --linear \
            --kernel=NORMALS \
            "$input" \
            "$output_desktop"
    else
        $MIPGEN \
            --format=dds \
            --compression=bc7 \
            --linear \
            "$input" \
            "$output_desktop"
    fi

    log_info "  ✓ Mobile: $(du -h $output_mobile | cut -f1)"
    log_info "  ✓ Desktop: $(du -h $output_desktop | cut -f1)"
}

# 自动检测纹理类型
detect_texture_type() {
    local name=$1

    if [[ "$name" == *"albedo"* ]] || [[ "$name" == *"basecolor"* ]]; then
        echo "albedo"
    elif [[ "$name" == *"normal"* ]]; then
        echo "normal"
    elif [[ "$name" == *"orm"* ]]; then
        echo "orm"
    elif [[ "$name" == *"emissive"* ]]; then
        echo "emissive"
    else
        echo "generic"
    fi
}

# 构建所有纹理
find "$TEXTURES_SOURCE" -name "*.png" | while read tex_file; do
    local name=$(basename "$tex_file" .png)
    local type=$(detect_texture_type "$name")
    build_texture "$tex_file" "$type"
done

log_info "Texture build complete"
```

### 模型构建脚本

```bash
#!/bin/bash
# tools/build_models.sh

MODELS_INTERMEDIATE="$PROJECT_ROOT/assets/intermediate/models"
MODELS_COMPILED="$PROJECT_ROOT/assets/compiled/models"

build_model() {
    local input=$1
    local name=$(basename "$input" .glb)

    log_info "Building model: $name"

    # 验证
    gltf-validator "$input" -r "$MODELS_COMPILED/${name}_validation.json"

    if [ $? -ne 0 ]; then
        log_warn "  ⚠ Validation issues found"
    fi

    # 优化
    local temp1="$MODELS_COMPILED/${name}_temp1.glb"
    local temp2="$MODELS_COMPILED/${name}_temp2.glb"
    local output="$MODELS_COMPILED/${name}.glb"

    gltf-transform optimize "$input" "$temp1"
    gltf-transform meshopt "$temp1" "$temp2"
    gltf-transform uastc "$temp2" "$output"

    rm "$temp1" "$temp2"

    local input_size=$(du -h "$input" | cut -f1)
    local output_size=$(du -h "$output" | cut -f1)

    log_info "  $input_size → $output_size"
}

# 构建所有模型
find "$MODELS_INTERMEDIATE" -name "*.glb" | while read model_file; do
    build_model "$model_file"
done

log_info "Model build complete"
```

### IBL 构建脚本

```bash
#!/bin/bash
# tools/build_ibl.sh

IBL_SOURCE="$PROJECT_ROOT/assets/source/environments"
IBL_COMPILED="$PROJECT_ROOT/assets/compiled/ibl"
CMGEN="$FILAMENT_TOOLS/cmgen"

build_ibl() {
    local input=$1
    local name=$(basename "$input" .exr)

    log_info "Building IBL: $name"

    local output_dir="$IBL_COMPILED/${name}_ibl"

    # 生成 IBL
    $CMGEN \
        -x "$output_dir" \
        --format=ktx \
        --size=256 \
        --extract-blur=0.1 \
        "$input"

    # 生成 Skybox
    $CMGEN \
        -x "$IBL_COMPILED" \
        --format=ktx \
        --size=1024 \
        --extract="${name}_skybox.ktx" \
        "$input"

    log_info "  ✓ IBL generated"
}

# 构建所有 IBL
find "$IBL_SOURCE" -name "*.exr" | while read exr_file; do
    build_ibl "$exr_file"
done

log_info "IBL build complete"
```

### 打包脚本

```bash
#!/bin/bash
# tools/package.sh

ASSETS_COMPILED="$PROJECT_ROOT/assets/compiled"
ASSETS_FINAL="$PROJECT_ROOT/assets/final"

package_assets() {
    log_info "Packaging assets..."

    # 创建资产清单
    cat > "$ASSETS_FINAL/manifest.json" << EOF
{
  "version": "1.0",
  "build_date": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "assets": {
    "materials": [],
    "textures": [],
    "models": [],
    "ibl": []
  }
}
EOF

    # 方案 A: 文件系统打包
    log_info "Creating filesystem package..."
    cp -r "$ASSETS_COMPILED"/* "$ASSETS_FINAL/"

    # 方案 B: ZIP 打包
    log_info "Creating ZIP package..."
    cd "$ASSETS_COMPILED"
    zip -r "$ASSETS_FINAL/assets.zip" ./*
    cd -

    # 方案 C: 自定义 .pak 格式（需要自定义工具）
    # ./tools/pakgen "$ASSETS_COMPILED" "$ASSETS_FINAL/assets.pak"

    log_info "Packaging complete"
}

package_assets
```

## 增量构建

### 基于文件修改时间

```bash
#!/bin/bash
# tools/incremental_build.sh

is_newer() {
    local source=$1
    local target=$2

    # 如果目标不存在，需要构建
    if [ ! -f "$target" ]; then
        return 0
    fi

    # 如果源比目标新，需要构建
    if [ "$source" -nt "$target" ]; then
        return 0
    fi

    return 1
}

# 增量材质构建
build_materials_incremental() {
    find "$MATERIALS_SOURCE" -name "*.mat" | while read mat_file; do
        local name=$(basename "$mat_file" .mat)
        local target="$MATERIALS_COMPILED/${name}_mobile_vulkan.filamat"

        if is_newer "$mat_file" "$target"; then
            log_info "Rebuilding: $name (modified)"
            build_material "$mat_file"
        else
            log_info "Skipping: $name (up to date)"
        fi
    done
}
```

### 基于内容哈希

```bash
#!/bin/bash
# tools/hash_build.sh

HASH_CACHE="$PROJECT_ROOT/.build_cache/hashes.txt"

get_file_hash() {
    local file=$1
    shasum -a 256 "$file" | cut -d' ' -f1
}

needs_rebuild() {
    local source=$1
    local cache_key=$2

    local current_hash=$(get_file_hash "$source")
    local cached_hash=$(grep "^$cache_key:" "$HASH_CACHE" | cut -d':' -f2)

    if [ "$current_hash" != "$cached_hash" ]; then
        return 0  # 需要重建
    fi

    return 1  # 无需重建
}

update_hash() {
    local source=$1
    local cache_key=$2

    local current_hash=$(get_file_hash "$source")

    # 更新或添加哈希
    sed -i '' "/^$cache_key:/d" "$HASH_CACHE" 2>/dev/null || true
    echo "$cache_key:$current_hash" >> "$HASH_CACHE"
}

# 使用示例
if needs_rebuild "$mat_file" "material_$name"; then
    build_material "$mat_file"
    update_hash "$mat_file" "material_$name"
fi
```

## CMake 集成

```cmake
# CMakeLists.txt

# 查找 Filament 工具
find_program(MATC_EXECUTABLE matc HINTS ${FILAMENT_DIR}/bin)
find_program(CMGEN_EXECUTABLE cmgen HINTS ${FILAMENT_DIR}/bin)
find_program(MIPGEN_EXECUTABLE mipgen HINTS ${FILAMENT_DIR}/bin)

# 自定义函数：编译材质
function(add_material_target target_name mat_file)
    get_filename_component(mat_name ${mat_file} NAME_WE)
    set(output_file ${CMAKE_CURRENT_BINARY_DIR}/materials/${mat_name}.filamat)

    add_custom_command(
        OUTPUT ${output_file}
        COMMAND ${MATC_EXECUTABLE}
            -p mobile
            -a vulkan
            -O
            -o ${output_file}
            ${mat_file}
        DEPENDS ${mat_file}
        COMMENT "Compiling material: ${mat_name}"
    )

    add_custom_target(${target_name} ALL DEPENDS ${output_file})
endfunction()

# 自定义函数：生成 IBL
function(add_ibl_target target_name exr_file)
    get_filename_component(env_name ${exr_file} NAME_WE)
    set(output_dir ${CMAKE_CURRENT_BINARY_DIR}/ibl/${env_name}_ibl)
    set(output_marker ${output_dir}/.generated)

    add_custom_command(
        OUTPUT ${output_marker}
        COMMAND ${CMGEN_EXECUTABLE}
            -x ${output_dir}
            --format=ktx
            --size=256
            ${exr_file}
        COMMAND ${CMAKE_COMMAND} -E touch ${output_marker}
        DEPENDS ${exr_file}
        COMMENT "Generating IBL: ${env_name}"
    )

    add_custom_target(${target_name} ALL DEPENDS ${output_marker})
endfunction()

# 使用
add_material_target(material_wood assets/source/materials/wood.mat)
add_ibl_target(ibl_studio assets/source/environments/studio.exr)

# 构建所有资产
add_custom_target(assets ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Building all assets..."
    COMMAND ${PROJECT_SOURCE_DIR}/tools/build_assets.sh
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
```

## 持续集成 (CI/CD)

### GitHub Actions 示例

```yaml
# .github/workflows/build-assets.yml
name: Build Assets

on:
  push:
    paths:
      - 'assets/source/**'
  pull_request:
    paths:
      - 'assets/source/**'

jobs:
  build:
    runs-on: ubuntu-latest

    steps:
    - uses: actions/checkout@v3

    - name: Setup Filament
      run: |
        wget https://github.com/google/filament/releases/download/v1.51.5/filament-v1.51.5-linux.tgz
        tar -xzf filament-v1.51.5-linux.tgz
        echo "$PWD/filament/bin" >> $GITHUB_PATH

    - name: Install Dependencies
      run: |
        npm install -g @gltf-transform/cli
        npm install -g gltf-validator

    - name: Build Assets
      run: |
        export FILAMENT_TOOLS=$PWD/filament/bin
        ./tools/build_assets.sh

    - name: Upload Artifacts
      uses: actions/upload-artifact@v3
      with:
        name: compiled-assets
        path: assets/final/

    - name: Validate Assets
      run: |
        # 检查必要文件是否生成
        test -f assets/final/assets.zip

        # 验证 glTF 文件
        find assets/compiled/models -name "*.glb" -exec gltf-validator {} \;
```

## 最佳实践

### 1. 版本控制策略

```gitignore
# .gitignore

# 提交源文件
assets/source/

# 不提交中间和编译文件
assets/intermediate/
assets/compiled/
assets/final/

# 不提交构建缓存
.build_cache/

# 提交构建脚本
tools/
```

### 2. 资产命名规范

```
模型:
  chair_wood.glb
  table_metal.glb
  character_hero.glb

纹理:
  chair_wood_albedo.png
  chair_wood_normal.png
  chair_wood_orm.png (occlusion/roughness/metallic)

材质:
  wood_glossy.mat
  metal_brushed.mat

环境:
  studio_soft.exr
  outdoor_sunset.exr
```

### 3. 错误处理

```bash
# 构建失败时的处理
build_with_error_handling() {
    local input=$1
    local output=$2

    if ! some_command "$input" "$output" 2>&1 | tee build.log; then
        log_error "Build failed: $input"

        # 发送通知
        send_slack_notification "Asset build failed: $input"

        # 保存日志
        cp build.log "$PROJECT_ROOT/logs/error_$(date +%s).log"

        # 可选：回滚
        # restore_previous_version "$output"

        return 1
    fi

    return 0
}
```

### 4. 性能优化

```bash
# 并行构建
build_parallel() {
    local max_jobs=4
    local job_count=0

    find "$MATERIALS_SOURCE" -name "*.mat" | while read mat_file; do
        build_material "$mat_file" &

        ((job_count++))

        if [ $job_count -ge $max_jobs ]; then
            wait  # 等待所有后台任务完成
            job_count=0
        fi
    done

    wait  # 等待剩余任务
}
```

### 5. 资产验证

```bash
# 验证编译后的资产
validate_assets() {
    local errors=0

    # 检查材质
    log_info "Validating materials..."
    find "$MATERIALS_COMPILED" -name "*.filamat" | while read filamat; do
        # 检查文件大小
        local size=$(stat -f%z "$filamat" 2>/dev/null || stat -c%s "$filamat")
        if [ $size -lt 100 ]; then
            log_error "Suspicious material size: $filamat ($size bytes)"
            ((errors++))
        fi
    done

    # 验证 glTF 文件
    log_info "Validating models..."
    find "$MODELS_COMPILED" -name "*.glb" -exec gltf-validator {} \; || ((errors++))

    if [ $errors -gt 0 ]; then
        log_error "Validation failed with $errors errors"
        return 1
    fi

    log_info "✓ All assets validated successfully"
    return 0
}
```

## 高级技巧

### 1. 条件编译

```bash
# 根据目标平台选择性构建
build_for_platform() {
    local platform=$1  # android, ios, desktop, web

    case $platform in
        android)
            COMPRESSION="etc2"
            APIS=("opengl" "vulkan")
            ;;
        ios)
            COMPRESSION="astc_4x4"
            APIS=("metal")
            ;;
        desktop)
            COMPRESSION="bc7"
            APIS=("vulkan" "opengl" "metal")
            ;;
        web)
            COMPRESSION="uastc"
            APIS=("webgl")
            ;;
    esac

    # 使用特定平台参数构建
    build_all_with_params "$COMPRESSION" "${APIS[@]}"
}
```

### 2. 资产压缩

```bash
# 压缩最终资产包
compress_final_assets() {
    log_info "Compressing final assets..."

    # 使用高压缩比
    tar -czf "$ASSETS_FINAL/assets.tar.gz" \
        -C "$ASSETS_COMPILED" \
        --options='compression-level=9' \
        .

    # 或使用 7z
    7z a -t7z -m0=lzma2 -mx=9 \
        "$ASSETS_FINAL/assets.7z" \
        "$ASSETS_COMPILED"/*
}
```

### 3. 资产热重载支持

```bash
# 生成资产清单用于热重载
generate_manifest() {
    log_info "Generating asset manifest..."

    cat > "$ASSETS_FINAL/manifest.json" << EOF
{
  "version": "$(git rev-parse --short HEAD)",
  "timestamp": "$(date -u +%s)",
  "assets": {
EOF

    # 添加所有资产信息
    find "$ASSETS_COMPILED" -type f | while read file; do
        local rel_path=${file#$ASSETS_COMPILED/}
        local hash=$(shasum -a 256 "$file" | cut -d' ' -f1)

        echo "    \"$rel_path\": \"$hash\","
    done >> "$ASSETS_FINAL/manifest.json"

    echo "  }" >> "$ASSETS_FINAL/manifest.json"
    echo "}" >> "$ASSETS_FINAL/manifest.json"
}
```

## 相关文档

- [02-matc-compiler.md](02-matc-compiler.md) - 材质编译
- [03-cmgen-ibl.md](03-cmgen-ibl.md) - IBL 生成
- [05-mipgen.md](05-mipgen.md) - 纹理处理
- [06-gltf-tools.md](06-gltf-tools.md) - glTF 工具链

## 总结

**资产管线核心要点**:

1. **自动化** - 脚本化所有构建步骤
2. **增量构建** - 只构建修改的资源
3. **多平台** - 一次构建，多平台优化
4. **版本控制** - 只提交源文件和脚本
5. **CI/CD** - 集成到持续集成流程
6. **验证** - 自动验证构建结果
7. **错误处理** - 优雅处理构建失败

**典型工作流程**:

```bash
# 开发时
./tools/build_assets.sh          # 完整构建
./tools/build_assets.sh --watch  # 监视文件变化

# CI/CD
./tools/build_assets.sh clean    # 清理并完整构建
./tools/validate_assets.sh       # 验证

# 发布
./tools/package.sh               # 打包发布
```

一个良好的资产管线可以大大提高团队效率，减少手动错误，支持快速迭代！
