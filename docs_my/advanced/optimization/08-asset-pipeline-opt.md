# 资产管线优化

## 📖 概述

高效的资产管线（Asset Pipeline）是性能优化的关键环节。通过离线优化资产，可以显著减少加载时间、运行时开销和包体积。

本文档介绍 Filament 资产管线的完整优化流程，包括纹理压缩、网格优化、材质编译、自动化工具集成等。

**优化目标**:
- 减少加载时间 50-80%
- 优化运行时性能（GPU 渲染效率）
- 减少包体积 60-90%（压缩纹理、网格）
- 自动化优化流程（CI/CD 集成）

**核心工具**:
- **matc**：Filament 材质编译器
- **cmgen**：IBL（Image-Based Lighting）生成器
- **filamesh**：网格转换工具
- **mipgen**：Mipmap 生成器
- **meshoptimizer**：网格优化库

---

## 1. 纹理优化

### 1.1 纹理压缩格式选择

**平台对应表**：

| 平台 | 推荐格式 | 备选格式 | 压缩比 | 质量 |
|------|---------|---------|--------|------|
| Android (Most) | ASTC 4x4 | ETC2 | 8:1 | 高 |
| iOS | ASTC 4x4 | PVRTC 4bpp | 8:1 | 高 |
| Desktop (NVIDIA/AMD) | BC7 (DX11+) | BC3 (DXT5) | 4:1 | 高 |
| WebGL | ASTC (WebGL 2) | S3TC | 8:1 | 高 |

### 1.2 自动化纹理压缩脚本

```bash
#!/bin/bash
# compress_textures.sh

set -e

INPUT_DIR="assets/textures/raw"
OUTPUT_DIR="assets/textures/compressed"

# 检测平台
PLATFORM=${1:-all}

# 工具路径
ASTCENC="astcenc"
COMPRESSONATOR="CompressonatorCLI"

# 创建输出目录
mkdir -p "$OUTPUT_DIR/android"
mkdir -p "$OUTPUT_DIR/ios"
mkdir -p "$OUTPUT_DIR/desktop"

# 遍历所有纹理
for file in "$INPUT_DIR"/*.png "$INPUT_DIR"/*.jpg; do
    [ -e "$file" ] || continue
    
    filename=$(basename "$file")
    name="${filename%.*}"
    
    echo "Processing: $filename"
    
    # Android (ASTC 4x4)
    if [ "$PLATFORM" = "all" ] || [ "$PLATFORM" = "android" ]; then
        echo "  → Android ASTC 4x4"
        $ASTCENC -cl "$file" "$OUTPUT_DIR/android/${name}.astc" 4x4 -medium
    fi
    
    # iOS (ASTC 4x4)
    if [ "$PLATFORM" = "all" ] || [ "$PLATFORM" = "ios" ]; then
        echo "  → iOS ASTC 4x4"
        $ASTCENC -cl "$file" "$OUTPUT_DIR/ios/${name}.astc" 4x4 -medium
    fi
    
    # Desktop (BC7)
    if [ "$PLATFORM" = "all" ] || [ "$PLATFORM" = "desktop" ]; then
        echo "  → Desktop BC7"
        $COMPRESSONATOR -fd BC7 "$file" "$OUTPUT_DIR/desktop/${name}.dds"
    fi
done

echo "Texture compression complete!"
```

**使用**：

```bash
# 压缩所有平台
./compress_textures.sh

# 仅压缩 Android
./compress_textures.sh android
```

### 1.3 Mipmap 生成

```bash
#!/bin/bash
# generate_mipmaps.sh

# 使用 Filament mipgen 工具
MIPGEN="./out/release/filament/bin/mipgen"

for file in assets/textures/compressed/desktop/*.ktx; do
    [ -e "$file" ] || continue
    
    filename=$(basename "$file")
    name="${filename%.*}"
    
    echo "Generating mipmaps for: $filename"
    
    # 生成 Mipmap
    $MIPGEN --compression=astc_fast_ldr_4x4 \
            --kernel=lanczos \
            --format=rgba \
            --linear \
            "$file"
done
```

### 1.4 纹理质量分级

```python
# texture_quality_tiers.py
import os
import subprocess

# 质量分级配置
QUALITY_TIERS = {
    'high': {
        'astc_block': '4x4',  # 8:1
        'bc_format': 'BC7',
    },
    'medium': {
        'astc_block': '6x6',  # 18:1
        'bc_format': 'BC3',
    },
    'low': {
        'astc_block': '8x8',  # 32:1
        'bc_format': 'BC1',
    }
}

def compress_texture(input_path, output_dir, tier='medium'):
    config = QUALITY_TIERS[tier]
    filename = os.path.basename(input_path)
    name, ext = os.path.splitext(filename)
    
    # ASTC (Mobile)
    astc_output = os.path.join(output_dir, f"{name}_{tier}.astc")
    subprocess.run([
        'astcenc', '-cl', input_path, astc_output,
        config['astc_block'], '-medium'
    ])
    
    # BC (Desktop)
    bc_output = os.path.join(output_dir, f"{name}_{tier}.dds")
    subprocess.run([
        'CompressonatorCLI', '-fd', config['bc_format'],
        input_path, bc_output
    ])
    
    print(f"Compressed {filename} → {tier} quality")

# 使用
compress_texture('assets/raw/albedo.png', 'assets/compressed', tier='high')
compress_texture('assets/raw/albedo.png', 'assets/compressed', tier='medium')
compress_texture('assets/raw/albedo.png', 'assets/compressed', tier='low')
```

**运行时选择质量**：

```cpp
#include <filament/Texture.h>

using namespace filament;

Texture* loadTextureWithQuality(Engine& engine, const std::string& baseName, QualityTier tier) {
    std::string suffix;
    switch (tier) {
        case QualityTier::HIGH:   suffix = "_high"; break;
        case QualityTier::MEDIUM: suffix = "_medium"; break;
        case QualityTier::LOW:    suffix = "_low"; break;
    }

    std::string path = "textures/" + baseName + suffix + ".astc";
    
    // 加载压缩纹理
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());

    Texture* texture = Texture::Builder()
        .width(width)
        .height(height)
        .levels(mipLevels)
        .format(Texture::InternalFormat::RGBA_ASTC_4x4)
        .build(engine);

    texture->setImage(engine, 0,
        Texture::PixelBufferDescriptor(data.data(), data.size(),
            Texture::Format::COMPRESSED, Texture::Type::COMPRESSED)
    );

    return texture;
}

// 根据设备性能选择质量
QualityTier tier = detectDeviceTier();
Texture* albedo = loadTextureWithQuality(*engine, "albedo", tier);
```

---

## 2. 网格优化

### 2.1 Vertex Cache 优化

**原理**：重排三角形顺序，使顶点在 GPU 缓存中复用。

```cpp
// 使用 meshoptimizer
#include <meshoptimizer.h>

struct Vertex {
    float position[3];
    float normal[3];
    float uv[2];
};

void optimizeVertexCache(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    // 1. 优化三角形顺序（提升 Vertex Cache 命中率）
    meshopt_optimizeVertexCache(
        indices.data(), indices.data(), indices.size(), vertices.size()
    );

    // 2. 优化 Overdraw（减少重复绘制）
    meshopt_optimizeOverdraw(
        indices.data(), indices.data(), indices.size(),
        &vertices[0].position[0], vertices.size(), sizeof(Vertex), 1.05f
    );

    // 3. 优化顶点数据访问（线性内存访问）
    meshopt_optimizeVertexFetch(
        vertices.data(), indices.data(), indices.size(),
        vertices.data(), vertices.size(), sizeof(Vertex)
    );
}
```

**性能提升**：

```
Before optimization:
  Vertex Cache Miss Rate: 85%
  GPU Time: 12.3 ms

After optimization:
  Vertex Cache Miss Rate: 25%
  GPU Time: 7.8 ms (37% faster)
```

### 2.2 网格简化（LOD）

```cpp
#include <meshoptimizer.h>

// 生成 LOD 级别
std::vector<uint32_t> generateLOD(const std::vector<Vertex>& vertices,
                                  const std::vector<uint32_t>& indices,
                                  float targetRatio) {
    size_t targetIndexCount = size_t(indices.size() * targetRatio);
    float targetError = 1e-2f;

    std::vector<uint32_t> lodIndices(indices.size());
    
    size_t lodIndexCount = meshopt_simplify(
        lodIndices.data(),
        indices.data(), indices.size(),
        &vertices[0].position[0], vertices.size(), sizeof(Vertex),
        targetIndexCount,
        targetError
    );

    lodIndices.resize(lodIndexCount);
    return lodIndices;
}

// 生成多级 LOD
struct MeshLOD {
    std::vector<Vertex> vertices;
    std::vector<std::vector<uint32_t>> lodLevels;  // LOD 0, 1, 2, ...
};

MeshLOD createMeshWithLODs(const std::vector<Vertex>& vertices,
                           const std::vector<uint32_t>& indices) {
    MeshLOD lod;
    lod.vertices = vertices;

    // LOD 0 (原始)
    lod.lodLevels.push_back(indices);

    // LOD 1 (50%)
    lod.lodLevels.push_back(generateLOD(vertices, indices, 0.5f));

    // LOD 2 (25%)
    lod.lodLevels.push_back(generateLOD(vertices, indices, 0.25f));

    // LOD 3 (10%)
    lod.lodLevels.push_back(generateLOD(vertices, indices, 0.1f));

    return lod;
}
```

### 2.3 网格压缩

```cpp
#include <meshoptimizer.h>

// 压缩顶点数据
struct CompressedMesh {
    std::vector<uint8_t> vertexData;
    std::vector<uint8_t> indexData;
    size_t vertexCount;
    size_t indexCount;
};

CompressedMesh compressMesh(const std::vector<Vertex>& vertices,
                            const std::vector<uint32_t>& indices) {
    CompressedMesh compressed;
    compressed.vertexCount = vertices.size();
    compressed.indexCount = indices.size();

    // 压缩顶点数据
    size_t vertexDataBound = meshopt_encodeVertexBufferBound(vertices.size(), sizeof(Vertex));
    compressed.vertexData.resize(vertexDataBound);

    compressed.vertexData.resize(
        meshopt_encodeVertexBuffer(
            compressed.vertexData.data(), compressed.vertexData.size(),
            vertices.data(), vertices.size(), sizeof(Vertex)
        )
    );

    // 压缩索引数据
    size_t indexDataBound = meshopt_encodeIndexBufferBound(indices.size(), vertices.size());
    compressed.indexData.resize(indexDataBound);

    compressed.indexData.resize(
        meshopt_encodeIndexBuffer(
            compressed.indexData.data(), compressed.indexData.size(),
            indices.data(), indices.size()
        )
    );

    return compressed;
}

// 解压
void decompressMesh(const CompressedMesh& compressed,
                    std::vector<Vertex>& vertices,
                    std::vector<uint32_t>& indices) {
    vertices.resize(compressed.vertexCount);
    indices.resize(compressed.indexCount);

    meshopt_decodeVertexBuffer(
        vertices.data(), compressed.vertexCount, sizeof(Vertex),
        compressed.vertexData.data(), compressed.vertexData.size()
    );

    meshopt_decodeIndexBuffer(
        indices.data(), compressed.indexCount,
        compressed.indexData.data(), compressed.indexData.size()
    );
}
```

**压缩效果**：

```
Original mesh:
  Vertices: 100,000 × 32 bytes = 3.2 MB
  Indices:  300,000 × 4 bytes  = 1.2 MB
  Total: 4.4 MB

Compressed:
  Vertex data: 0.8 MB
  Index data:  0.3 MB
  Total: 1.1 MB (75% reduction)
```

### 2.4 Blender 导出脚本

```python
# export_optimized_mesh.py
import bpy
import os
import subprocess

def optimize_and_export(obj, output_path):
    # 1. 应用所有修改器
    bpy.context.view_layer.objects.active = obj
    for modifier in obj.modifiers:
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    
    # 2. 三角化
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.quads_convert_to_tris()
    bpy.ops.object.mode_set(mode='OBJECT')
    
    # 3. 导出为 glTF
    temp_path = output_path + ".temp.glb"
    bpy.ops.export_scene.gltf(
        filepath=temp_path,
        export_format='GLB',
        export_apply=True,
        export_yup=True
    )
    
    # 4. 使用 gltfpack 优化
    subprocess.run([
        'gltfpack',
        '-i', temp_path,
        '-o', output_path,
        '-cc',  # 压缩
        '-tc',  # 纹理压缩
        '-si', '0.5'  # 简化 50%
    ])
    
    # 5. 清理临时文件
    os.remove(temp_path)
    
    print(f"Exported optimized mesh: {output_path}")

# 批量导出
for obj in bpy.data.objects:
    if obj.type == 'MESH':
        output_path = f"assets/meshes/{obj.name}.glb"
        optimize_and_export(obj, output_path)
```

---

## 3. 材质编译优化

### 3.1 使用 Filament matc

```bash
#!/bin/bash
# compile_materials.sh

MATC="./out/release/filament/bin/matc"

# 输入材质定义
INPUT_DIR="materials/src"
OUTPUT_DIR="assets/materials"

mkdir -p "$OUTPUT_DIR"

for mat_file in "$INPUT_DIR"/*.mat; do
    [ -e "$mat_file" ] || continue
    
    filename=$(basename "$mat_file" .mat)
    
    echo "Compiling material: $filename"
    
    # 编译 Mobile (OpenGL ES)
    $MATC -p mobile \
          -a opengl \
          -o "$OUTPUT_DIR/${filename}_mobile.filamat" \
          "$mat_file"
    
    # 编译 Desktop (Vulkan)
    $MATC -p desktop \
          -a vulkan \
          -o "$OUTPUT_DIR/${filename}_desktop.filamat" \
          "$mat_file"
    
    # 编译 Desktop (Metal - macOS/iOS)
    $MATC -p desktop \
          -a metal \
          -o "$OUTPUT_DIR/${filename}_metal.filamat" \
          "$mat_file"
done

echo "Material compilation complete!"
```

### 3.2 材质变体优化

**问题**：过多的材质变体导致编译时间长、包体大。

```cpp
// material_variants.mat
material {
    name : PBR,
    
    // ❌ 不好：所有功能都作为变体
    // 生成 2^8 = 256 个变体
    requires : [
        skinning,
        color,
        uv0,
        uv1,
        tangents,
        baseColor,
        normalMap,
        metallicRoughnessMap
    ],
    
    shadingModel : lit,
    
    // ...
}
```

**优化方案**：

```cpp
// material_variants_optimized.mat
material {
    name : PBR_Optimized,
    
    // ✅ 好：仅高频功能作为变体
    // 生成 2^3 = 8 个变体
    requires : [
        skinning,  // 常用
        color,     // 常用
        uv0        // 常用
    ],
    
    shadingModel : lit,
    
    // 其他功能用 Uniform 控制
    parameters : [
        { type : bool,  name : hasNormalMap },
        { type : bool,  name : hasMetallicRoughnessMap },
        { type : bool,  name : useVertexColor }
    ],
    
    fragment {
        void material(inout MaterialInputs material) {
            // 运行时分支（编译器会优化）
            if (materialParams.hasNormalMap) {
                material.normal = texture(materialParams_normalMap, getUV0()).xyz;
            }
            
            // ...
        }
    }
}
```

**效果**：

```
Before: 256 variants × 50 KB = 12.8 MB
After:  8 variants × 50 KB = 400 KB (96.9% reduction!)
```

### 3.3 材质预编译 Cache

```python
# material_cache.py
import hashlib
import os
import subprocess

CACHE_DIR = ".material_cache"

def get_file_hash(filepath):
    with open(filepath, 'rb') as f:
        return hashlib.md5(f.read()).hexdigest()

def compile_material_cached(mat_file, output_file, platform):
    # 计算输入文件哈希
    mat_hash = get_file_hash(mat_file)
    cache_key = f"{mat_hash}_{platform}"
    cache_path = os.path.join(CACHE_DIR, cache_key)
    
    # 检查缓存
    if os.path.exists(cache_path):
        print(f"Using cached material: {mat_file}")
        os.link(cache_path, output_file)  # Hard link
        return
    
    # 编译
    print(f"Compiling material: {mat_file}")
    subprocess.run([
        'matc', '-p', platform, '-o', output_file, mat_file
    ], check=True)
    
    # 保存到缓存
    os.makedirs(CACHE_DIR, exist_ok=True)
    os.link(output_file, cache_path)

# 使用
compile_material_cached('pbr.mat', 'pbr_mobile.filamat', 'mobile')
```

---

## 4. IBL (Image-Based Lighting) 生成

### 4.1 使用 cmgen 生成 IBL

```bash
#!/bin/bash
# generate_ibl.sh

CMGEN="./out/release/filament/bin/cmgen"

INPUT_HDR="env/studio.hdr"
OUTPUT_DIR="assets/ibl/studio"

mkdir -p "$OUTPUT_DIR"

# 生成 IBL 贴图
$CMGEN \
    --format=ktx \
    --size=256 \
    --extract-blur=0.1 \
    --extract="$OUTPUT_DIR" \
    "$INPUT_HDR"

echo "IBL generation complete!"
echo "  Irradiance: $OUTPUT_DIR/studio_ibl.ktx"
echo "  Reflections: $OUTPUT_DIR/studio_skybox.ktx"
```

### 4.2 批量 IBL 处理

```python
# batch_ibl.py
import os
import subprocess

INPUT_DIR = "env/hdr"
OUTPUT_DIR = "assets/ibl"
CMGEN = "./out/release/filament/bin/cmgen"

# IBL 质量配置
QUALITY_CONFIGS = {
    'high': {'size': 512, 'blur': 0.05},
    'medium': {'size': 256, 'blur': 0.1},
    'low': {'size': 128, 'blur': 0.15}
}

def generate_ibl(hdr_file, quality='medium'):
    config = QUALITY_CONFIGS[quality]
    filename = os.path.splitext(os.path.basename(hdr_file))[0]
    output_dir = os.path.join(OUTPUT_DIR, f"{filename}_{quality}")
    
    os.makedirs(output_dir, exist_ok=True)
    
    subprocess.run([
        CMGEN,
        '--format=ktx',
        f'--size={config["size"]}',
        f'--extract-blur={config["blur"]}',
        f'--extract={output_dir}',
        hdr_file
    ], check=True)
    
    print(f"Generated IBL: {filename} ({quality})")

# 批量处理
for hdr_file in os.listdir(INPUT_DIR):
    if hdr_file.endswith('.hdr'):
        full_path = os.path.join(INPUT_DIR, hdr_file)
        
        generate_ibl(full_path, 'high')
        generate_ibl(full_path, 'medium')
        generate_ibl(full_path, 'low')
```

---

## 5. 自动化构建集成

### 5.1 CMake 集成

```cmake
# CMakeLists.txt

# === Asset Optimization Targets ===

# 1. 纹理压缩
add_custom_target(compress_textures
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/compress_textures.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Compressing textures..."
)

# 2. 网格优化
add_custom_target(optimize_meshes
    COMMAND python ${CMAKE_SOURCE_DIR}/scripts/optimize_meshes.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Optimizing meshes..."
)

# 3. 材质编译
add_custom_target(compile_materials
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/compile_materials.sh
    DEPENDS matc  # 确保 matc 已构建
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Compiling materials..."
)

# 4. IBL 生成
add_custom_target(generate_ibl
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/generate_ibl.sh
    DEPENDS cmgen  # 确保 cmgen 已构建
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating IBL..."
)

# 5. 总资产优化目标
add_custom_target(optimize_all_assets
    DEPENDS compress_textures optimize_meshes compile_materials generate_ibl
    COMMENT "Optimizing all assets..."
)

# 6. 集成到主构建
add_dependencies(${PROJECT_NAME} optimize_all_assets)
```

**使用**：

```bash
# 仅优化资产
cmake --build . --target optimize_all_assets

# 完整构建（包含资产优化）
cmake --build .
```

### 5.2 增量构建

```python
# incremental_build.py
import os
import time
import hashlib

CACHE_FILE = ".asset_cache.json"

def get_file_mtime(filepath):
    return os.path.getmtime(filepath)

def needs_rebuild(input_file, output_file, cache):
    # 检查输出文件是否存在
    if not os.path.exists(output_file):
        return True
    
    # 检查输入文件是否修改
    input_hash = hashlib.md5(open(input_file, 'rb').read()).hexdigest()
    cached_hash = cache.get(input_file)
    
    if input_hash != cached_hash:
        cache[input_file] = input_hash
        return True
    
    return False

def optimize_asset(input_file, output_file, cache):
    if needs_rebuild(input_file, output_file, cache):
        print(f"Building: {input_file} → {output_file}")
        # 执行优化
        # ...
        return True
    else:
        print(f"Cached: {input_file}")
        return False

# 加载缓存
import json
cache = {}
if os.path.exists(CACHE_FILE):
    with open(CACHE_FILE, 'r') as f:
        cache = json.load(f)

# 批量处理
rebuilt_count = 0
for input_file in get_all_assets():
    output_file = get_output_path(input_file)
    if optimize_asset(input_file, output_file, cache):
        rebuilt_count += 1

# 保存缓存
with open(CACHE_FILE, 'w') as f:
    json.dump(cache, f)

print(f"Rebuilt {rebuilt_count} assets")
```

### 5.3 CI/CD 集成（GitHub Actions）

```yaml
# .github/workflows/optimize_assets.yml
name: Optimize Assets

on:
  push:
    paths:
      - 'assets/raw/**'
      - 'materials/src/**'
  pull_request:
    paths:
      - 'assets/raw/**'
      - 'materials/src/**'

jobs:
  optimize:
    runs-on: ubuntu-latest
    
    steps:
      - uses: actions/checkout@v3
      
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y astcenc compressonatorcli
      
      - name: Build Filament tools
        run: |
          ./build.sh release
      
      - name: Optimize textures
        run: ./scripts/compress_textures.sh
      
      - name: Optimize meshes
        run: python scripts/optimize_meshes.py
      
      - name: Compile materials
        run: ./scripts/compile_materials.sh
      
      - name: Generate IBL
        run: ./scripts/generate_ibl.sh
      
      - name: Upload artifacts
        uses: actions/upload-artifact@v3
        with:
          name: optimized-assets
          path: assets/optimized/
```

---

## 6. 运行时资产加载优化

### 6.1 异步加载

```cpp
#include <filament/Engine.h>
#include <utils/EntityManager.h>
#include <thread>
#include <queue>

class AssetLoader {
public:
    AssetLoader(Engine& engine) : mEngine(engine), mRunning(true) {
        // 启动后台加载线程
        mWorkerThread = std::thread(&AssetLoader::workerLoop, this);
    }

    ~AssetLoader() {
        mRunning = false;
        mCondition.notify_all();
        if (mWorkerThread.joinable()) {
            mWorkerThread.join();
        }
    }

    // 异步加载纹理
    void loadTextureAsync(const std::string& path, std::function<void(Texture*)> callback) {
        std::unique_lock<std::mutex> lock(mMutex);
        mQueue.push([this, path, callback]() {
            // 后台线程：加载文件
            std::ifstream file(path, std::ios::binary);
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());

            // 主线程：创建 GPU 资源
            mMainThreadTasks.push([this, data, callback]() {
                Texture* texture = Texture::Builder()
                    .width(width)
                    .height(height)
                    .format(Texture::InternalFormat::RGBA8)
                    .build(mEngine);

                texture->setImage(mEngine, 0,
                    Texture::PixelBufferDescriptor(data.data(), data.size(),
                        Texture::Format::RGBA, Texture::Type::UBYTE)
                );

                callback(texture);
            });
        });
        mCondition.notify_one();
    }

    // 每帧调用（在主线程）
    void update() {
        std::unique_lock<std::mutex> lock(mMainThreadMutex);
        while (!mMainThreadTasks.empty()) {
            auto task = mMainThreadTasks.front();
            mMainThreadTasks.pop();
            lock.unlock();

            task();  // 执行 GPU 资源创建

            lock.lock();
        }
    }

private:
    void workerLoop() {
        while (mRunning) {
            std::unique_lock<std::mutex> lock(mMutex);
            mCondition.wait(lock, [this]() { return !mQueue.empty() || !mRunning; });

            if (!mRunning) break;

            if (!mQueue.empty()) {
                auto task = mQueue.front();
                mQueue.pop();
                lock.unlock();

                task();  // 后台执行

                lock.lock();
            }
        }
    }

    Engine& mEngine;
    std::thread mWorkerThread;
    std::atomic<bool> mRunning;

    std::mutex mMutex;
    std::condition_variable mCondition;
    std::queue<std::function<void()>> mQueue;

    std::mutex mMainThreadMutex;
    std::queue<std::function<void()>> mMainThreadTasks;
};

// 使用
AssetLoader loader(*engine);

loader.loadTextureAsync("albedo.ktx", [](Texture* texture) {
    printf("Texture loaded: %p\n", texture);
    // 设置到材质
});

// 每帧
void onFrame() {
    loader.update();  // 处理完成的加载任务
    renderer->render(view);
}
```

### 6.2 资产包（Bundle）

```cpp
// 资产打包格式
struct AssetBundle {
    struct Entry {
        uint32_t offset;
        uint32_t size;
        char name[64];
    };

    uint32_t entryCount;
    Entry entries[];
    // ... followed by asset data
};

// 打包工具
class AssetBundler {
public:
    void addAsset(const std::string& name, const std::vector<uint8_t>& data) {
        mAssets[name] = data;
    }

    void write(const std::string& outputPath) {
        std::ofstream file(outputPath, std::ios::binary);

        // Header
        uint32_t entryCount = mAssets.size();
        file.write(reinterpret_cast<const char*>(&entryCount), sizeof(entryCount));

        // Entry table
        uint32_t currentOffset = sizeof(uint32_t) + entryCount * sizeof(AssetBundle::Entry);
        for (const auto& [name, data] : mAssets) {
            AssetBundle::Entry entry;
            entry.offset = currentOffset;
            entry.size = data.size();
            strncpy(entry.name, name.c_str(), sizeof(entry.name));

            file.write(reinterpret_cast<const char*>(&entry), sizeof(entry));

            currentOffset += data.size();
        }

        // Asset data
        for (const auto& [name, data] : mAssets) {
            file.write(reinterpret_cast<const char*>(data.data()), data.size());
        }

        printf("Bundle created: %s (%zu assets)\n", outputPath.c_str(), mAssets.size());
    }

private:
    std::map<std::string, std::vector<uint8_t>> mAssets;
};

// 使用
AssetBundler bundler;
bundler.addAsset("albedo.ktx", readFile("albedo.ktx"));
bundler.addAsset("normal.ktx", readFile("normal.ktx"));
bundler.addAsset("mesh.filamesh", readFile("mesh.filamesh"));
bundler.write("level1.bundle");
```

---

## 7. 最佳实践总结

1. **纹理优化**：
   - 使用平台对应的压缩格式（ASTC、BC7）
   - 生成 Mipmap
   - 质量分级（高/中/低）

2. **网格优化**：
   - Vertex Cache 优化（meshoptimizer）
   - 生成 LOD 级别
   - 网格压缩（减少包体）

3. **材质优化**：
   - 减少变体数量（高频功能变体化，低频 Uniform）
   - 预编译材质（离线 matc）
   - 使用缓存（增量构建）

4. **IBL 优化**：
   - 离线生成（cmgen）
   - 质量分级
   - 批量处理

5. **自动化**：
   - CMake 集成
   - 增量构建
   - CI/CD 管线

6. **运行时加载**：
   - 异步加载
   - 资产打包
   - 分帧加载

---

## 8. 常见问题

### Q1: 如何选择纹理压缩格式？

**A**: 根据平台选择：
- Android (Most): ASTC 4x4
- iOS: ASTC 4x4
- Desktop (NVIDIA/AMD): BC7
- 兼容性优先: ETC2 (广泛支持)

### Q2: LOD 级别如何确定？

**A**: 根据距离和屏幕占比：
```cpp
float screenSize = getScreenSize(object);
if (screenSize > 0.5) {
    useLOD(0);  // 高细节
} else if (screenSize > 0.2) {
    useLOD(1);
} else if (screenSize > 0.05) {
    useLOD(2);
} else {
    useLOD(3);  // 低细节
}
```

### Q3: 材质变体过多怎么办？

**A**: 
1. 仅高频功能变体化（skinning、shadow）
2. 低频功能用 Uniform + 运行时分支
3. 使用 Specialization Constants（Vulkan）

---

## 9. 相关文档

- [optimization/01-texture-optimization.md](./01-texture-optimization.md)
- [optimization/02-mesh-optimization.md](./02-mesh-optimization.md)
- [materials/01-material-system.md](../materials/01-material-system.md)

---

## 10. 总结

资产管线优化关键技术：

1. **纹理压缩**：ASTC、BC7，Mipmap 生成
2. **网格优化**：Vertex Cache、LOD、压缩
3. **材质编译**：离线 matc、变体优化、缓存
4. **IBL 生成**：离线 cmgen、质量分级
5. **自动化**：CMake、增量构建、CI/CD
6. **运行时优化**：异步加载、资产打包

通过这些技术，可实现：
- **加载时间减少 50-80%**
- **包体积减少 60-90%**
- **运行时性能提升 20-40%**
- **开发迭代效率提升 3-5 倍**

资产管线优化是一次性投入、长期受益的工作，建议在项目早期建立完善的自动化流程。
