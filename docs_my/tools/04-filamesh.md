# filamesh - 网格转换器

## 概述

`filamesh` 是 Filament 的网格转换工具，将各种 3D 模型格式（OBJ, FBX等）转换为优化的 `.filamesh` 二进制格式。

**重要说明**:
- ⚠️ **推荐使用 glTF 2.0** 作为主要格式
- filamesh 适用于：简单静态网格、传统工作流程、特殊优化需求
- glTF 提供更完整的功能：场景层级、材质、动画、蒙皮等

**filamesh 的使用场景**:
- ✅ 转换传统 OBJ/FBX 格式
- ✅ 简单静态网格（无动画）
- ✅ 需要极致压缩的场景
- ✅ 运行时程序化生成网格
- ❌ 不适合复杂场景、动画、材质

## 基本用法

### 最简单的转换

```bash
# OBJ → filamesh
filamesh input.obj output.filamesh

# 查看帮助
filamesh --help
```

### 常用命令

```bash
# 1. 基础转换（仅几何）
filamesh chair.obj chair.filamesh

# 2. 生成切线空间（PBR 法线贴图必需）
filamesh --tangents chair.obj chair.filamesh

# 3. 压缩顶点数据（减小文件大小）
filamesh --compress chair.obj chair.filamesh

# 4. 完整 PBR 模型（推荐）
filamesh --tangents --compress chair.obj chair.filamesh

# 5. 优化为 triangle strips（减少索引数）
filamesh --strip chair.obj chair.filamesh

# 6. 组合所有优化
filamesh --tangents --compress --strip chair.obj chair.filamesh
```

## 命令行参数详解

### 核心参数

| 参数 | 说明 | 默认值 | 使用场景 |
|------|------|--------|---------|
| `--tangents` | 生成切线向量和副切线 | false | **PBR 材质必需**（法线贴图） |
| `--compress` | 压缩顶点数据 | false | 减小文件大小 |
| `--strip` | 转换为 triangle strips | false | 减少索引数量 |
| `--uvs` | 包含 UV 坐标 | true | 纹理映射 |
| `--normals` | 包含法线 | true | 光照计算 |

### 详细说明

#### 1. `--tangents` (切线空间)

**用途**: 生成切线 (Tangent) 和副切线 (Bitangent) 向量

**必需场景**:
- 使用法线贴图 (Normal Map) 的 PBR 材质
- 各向异性材质 (Anisotropic)

**工作原理**:
```
顶点属性:
├── Position (xyz)
├── Normal (xyz)
├── UV (uv)
├── Tangent (xyzw) ← --tangents 生成
│   └── w: Bitangent 符号 (+1/-1)
└── Bitangent = cross(Normal, Tangent.xyz) * Tangent.w
```

**示例**:
```bash
# ❌ 错误 - PBR 材质没有切线
filamesh chair.obj chair.filamesh
# 结果: 法线贴图显示错误!

# ✅ 正确 - PBR 材质必须有切线
filamesh --tangents chair.obj chair.filamesh
```

#### 2. `--compress` (压缩)

**用途**: 压缩顶点数据以减小文件大小

**压缩方式**:
```
未压缩:
  Position: float3 (12 bytes)
  Normal:   float3 (12 bytes)
  UV:       float2 (8 bytes)
  Total:    32 bytes/vertex

压缩后:
  Position: half3 (6 bytes) - 16位浮点
  Normal:   snorm3 (4 bytes) - 归一化整数
  UV:       half2 (4 bytes)
  Total:    14 bytes/vertex (节省 56%!)
```

**精度损失**:
- Position: 半精度浮点，足够大多数场景
- Normal: 整数编码，视觉上无差异
- UV: 半精度，纹理采样充足

**使用建议**:
```bash
# 大模型 - 推荐压缩
filamesh --compress large_building.obj large_building.filamesh

# 小精密模型 - 可不压缩
filamesh jewelry.obj jewelry.filamesh
```

#### 3. `--strip` (Triangle Strips)

**用途**: 将三角形列表转换为三角形条带

**优化效果**:
```
Triangle List (普通):
  三角形 1: v0, v1, v2
  三角形 2: v2, v1, v3  (重复 v2, v1)
  三角形 3: v2, v3, v4  (重复 v2, v3)
  索引数: 9

Triangle Strip:
  v0, v1, v2, v3, v4
  索引数: 5 (节省 44%!)
```

**注意事项**:
- 不是所有网格都能有效条带化
- 现代 GPU 有 Post-Transform Cache，优势减弱
- 可能增加 Draw Call 数量

**建议**: 仅在索引数量非常大时使用

### 高级参数

| 参数 | 说明 | 示例 |
|------|------|------|
| `--help` | 显示帮助 | `filamesh --help` |
| `--license` | 显示许可证 | `filamesh --license` |

## 支持的输入格式

### 1. OBJ (.obj) - 推荐

**优势**:
- ✅ 广泛支持
- ✅ 文本格式，易调试
- ✅ 简单可靠

**限制**:
- ❌ 无动画
- ❌ 无场景层级
- ❌ 材质支持有限

**示例**:
```bash
# Blender 导出 OBJ
File → Export → Wavefront (.obj)
✅ Include: UVs
✅ Include: Normals
✅ Triangulate Faces

# 转换
filamesh --tangents model.obj model.filamesh
```

### 2. FBX (.fbx)

**说明**: FBX 支持需要 Autodesk FBX SDK

**安装 FBX SDK**:
```bash
# macOS/Linux
# 从 Autodesk 下载 FBX SDK
# 重新编译 Filament 并启用 FBX

# 检查是否支持 FBX
filamesh --help | grep fbx
```

**使用**:
```bash
# 转换 FBX
filamesh --tangents model.fbx model.filamesh

# FBX 通常包含多个网格，filamesh 会合并它们
```

**限制**:
- ❌ 不支持动画（会被忽略）
- ❌ 不支持材质（需手动创建）
- ⚠️ 大部分项目不推荐，建议转 glTF

### 3. glTF (.gltf/.glb)

**说明**: filamesh 可以转换 glTF，但**不推荐**

```bash
# 可以转换，但会丢失信息
filamesh model.glb model.filamesh
# 丢失: 材质、动画、场景层级

# ✅ 推荐: 直接使用 gltfio 加载 glTF
AssetLoader* loader = AssetLoader::create({engine, materials});
FilamentAsset* asset = loader->createAsset(data, size);
```

## .filamesh 文件格式

### 二进制结构

```
.filamesh 文件结构:

+---------------------------+
| Header (32 bytes)         |
|  - Magic: "FILA" (4)      |
|  - Version: uint32 (4)    |
|  - Vertex Count (4)       |
|  - Index Count (4)        |
|  - Parts Count (4)        |
|  - Vertex Stride (4)      |
|  - Flags (4)              |
|  - Reserved (4)           |
+---------------------------+
| Vertex Data               |
|  (VertexCount * Stride)   |
+---------------------------+
| Index Data                |
|  (IndexCount * 2/4 bytes) |
+---------------------------+
| Bounding Box (24 bytes)   |
|  - Min (xyz float3)       |
|  - Max (xyz float3)       |
+---------------------------+
| Parts (可选)               |
|  - Offset, Count, MatID   |
+---------------------------+
```

### 顶点属性布局

```cpp
// 未压缩顶点 (--compress 未使用)
struct Vertex {
    float3 position;    // 12 bytes
    float3 normal;      // 12 bytes
    float2 uv;          // 8 bytes
    float4 tangent;     // 16 bytes (如果 --tangents)
};

// 压缩顶点 (--compress 使用)
struct CompressedVertex {
    half3 position;     // 6 bytes
    int normal;         // 4 bytes (packed snorm)
    half2 uv;           // 4 bytes
    int tangent;        // 4 bytes (packed, 如果 --tangents)
};
```

### 运行时加载

```cpp
// 加载 filamesh
#include <filamesh/MeshReader.h>

using namespace filamesh;

// 读取文件
std::ifstream file("model.filamesh", std::ios::binary);
std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());

// 解析
MeshReader::Mesh mesh = MeshReader::loadMeshFromBuffer(
    engine, data.data(), nullptr, nullptr, nullptr);

// 创建 Renderable
utils::Entity entity = utils::EntityManager::get().create();
RenderableManager::Builder(1)
    .boundingBox(mesh.aabb)
    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,
              mesh.vertexBuffer, mesh.indexBuffer)
    .material(0, materialInstance)
    .build(*engine, entity);
```

## 完整工作流程示例

### 示例 1: 单个 OBJ 转换

```bash
#!/bin/bash

INPUT="chair.obj"
OUTPUT="chair.filamesh"

# 检查输入文件
if [ ! -f "$INPUT" ]; then
    echo "Error: $INPUT not found"
    exit 1
fi

# 转换（PBR 材质）
echo "Converting $INPUT..."
filamesh --tangents --compress "$INPUT" "$OUTPUT"

# 检查输出
if [ -f "$OUTPUT" ]; then
    echo "Success!"
    ls -lh "$OUTPUT"
else
    echo "Failed!"
    exit 1
fi
```

### 示例 2: 批量转换目录

```bash
#!/bin/bash

INPUT_DIR="models/obj"
OUTPUT_DIR="models/filamesh"

# 创建输出目录
mkdir -p "$OUTPUT_DIR"

# 转换所有 OBJ 文件
for obj in "$INPUT_DIR"/*.obj; do
    # 获取文件名
    filename=$(basename "$obj" .obj)
    output="$OUTPUT_DIR/${filename}.filamesh"

    echo "Converting: $filename"
    filamesh --tangents --compress "$obj" "$output"

    # 显示文件大小
    if [ -f "$output" ]; then
        obj_size=$(ls -lh "$obj" | awk '{print $5}')
        mesh_size=$(ls -lh "$output" | awk '{print $5}')
        echo "  $obj_size → $mesh_size"
    fi
done

echo "Done!"
```

### 示例 3: 与 CMake 集成

```cmake
# FindFilamesh.cmake
find_program(FILAMESH_EXECUTABLE filamesh
    HINTS ${FILAMENT_DIR}/bin
)

# 自定义函数
function(add_filamesh_target target_name input_file)
    get_filename_component(name ${input_file} NAME_WE)
    set(output_file ${CMAKE_CURRENT_BINARY_DIR}/${name}.filamesh)

    add_custom_command(
        OUTPUT ${output_file}
        COMMAND ${FILAMESH_EXECUTABLE}
            --tangents
            --compress
            ${input_file}
            ${output_file}
        DEPENDS ${input_file}
        COMMENT "Converting ${input_file} to filamesh"
    )

    add_custom_target(${target_name} ALL DEPENDS ${output_file})
endfunction()

# 使用
add_filamesh_target(chair_mesh models/chair.obj)
```

### 示例 4: 程序化网格保存

```cpp
// 创建程序化网格并保存为 filamesh
#include <filamesh/MeshWriter.h>

using namespace filamesh;

// 创建立方体顶点
std::vector<Vertex> vertices = {
    // 顶点位置、法线、UV...
};
std::vector<uint16_t> indices = {
    // 三角形索引...
};

// 计算包围盒
Box aabb = computeAABB(vertices);

// 保存为 filamesh
MeshWriter::Mesh mesh;
mesh.vertices = vertices.data();
mesh.vertexCount = vertices.size();
mesh.indices = indices.data();
mesh.indexCount = indices.size();
mesh.aabb = aabb;

std::ofstream file("procedural.filamesh", std::ios::binary);
MeshWriter::writeMesh(file, mesh);
```

## 与 glTF 对比

### 功能对比

| 特性 | .filamesh | glTF 2.0 |
|------|----------|---------|
| **几何** | ✅ 单个网格 | ✅ 多网格、层级 |
| **材质** | ❌ 无 | ✅ PBR 材质 |
| **纹理** | ❌ 无 | ✅ 完整纹理 |
| **动画** | ❌ 无 | ✅ 骨骼+关键帧 |
| **蒙皮** | ❌ 无 | ✅ 骨骼蒙皮 |
| **场景层级** | ❌ 无 | ✅ 节点树 |
| **扩展性** | ❌ 固定格式 | ✅ 扩展系统 |
| **文件大小** | 小 | 中（可压缩） |
| **加载速度** | 极快 | 快 |
| **工具支持** | 少 | 广泛 |

### 文件大小对比

```
模型: 斯坦福兔子 (69,451 三角形)

OBJ:           4.2 MB (文本)
filamesh:      1.1 MB (未压缩)
filamesh:      0.6 MB (--compress)
glTF (GLB):    1.8 MB (未压缩)
glTF + Draco:  0.3 MB (压缩)
```

### 加载性能对比

```
加载时间 (69K 三角形):

filamesh:      0.8 ms (直接内存映射)
glTF (GLB):    12 ms (JSON解析 + 缓冲区)
glTF + Draco:  45 ms (解压缩)

结论: filamesh 加载最快，但功能最少
```

### 使用建议

**使用 filamesh 的场景**:
```
✅ 简单静态网格（无动画）
✅ 运行时程序化生成的网格
✅ 需要极致加载性能
✅ 传统工作流程（OBJ/FBX）
✅ 内存/存储极度受限
```

**使用 glTF 的场景** (推荐):
```
✅ 所有正常项目 (99% 情况)
✅ 有动画的模型
✅ 复杂场景
✅ 需要材质和纹理
✅ 跨平台、跨引擎
✅ 与 DCC 工具良好集成
```

**决策树**:
```
需要动画? → 是 → glTF
需要材质? → 是 → glTF
需要场景层级? → 是 → glTF
需要极致性能? → 是 → filamesh
简单静态网格? → 是 → filamesh 或 glTF (都可以)
默认选择 → glTF
```

## 常见问题

### Q1: filamesh 支持多材质吗？

**答**: 部分支持

```bash
# OBJ 文件中的多材质会被转换为 Parts
# 但材质数据本身不会保存

# chair.obj 包含 3 个材质组
# 转换后 chair.filamesh 包含 3 个 Parts
# 但材质定义需要在运行时手动创建

// 运行时代码
RenderableManager::Builder(3)  // 3 个 parts
    .geometry(0, ..., part0_offset, part0_count)
    .geometry(1, ..., part1_offset, part1_count)
    .geometry(2, ..., part2_offset, part2_count)
    .material(0, woodMaterial)
    .material(1, metalMaterial)
    .material(2, fabricMaterial)
    .build(*engine, entity);
```

### Q2: 转换后法线/切线不对？

**检查**:
```bash
# 1. 确保源文件有法线
# OBJ 文件应包含 vn 行

# 2. 使用 --tangents 生成切线
filamesh --tangents input.obj output.filamesh

# 3. 检查 DCC 工具导出设置
# Blender: ✅ Include Normals, ✅ Include UVs
```

### Q3: 文件太大？

**优化**:
```bash
# 1. 使用压缩
filamesh --compress model.obj model.filamesh

# 2. 减少顶点数（在 DCC 工具中）
# Blender: Decimate Modifier

# 3. 移除不必要的属性
# 如果不需要纹理，移除 UV

# 4. 考虑使用 glTF + Draco
gltf-transform draco model.glb compressed.glb
```

### Q4: 如何调试 filamesh 文件？

**方法**:
```bash
# 1. 查看十六进制
hexdump -C model.filamesh | head -n 20

# 应该看到:
# 00000000  46 49 4c 41  # "FILA" magic

# 2. 编写小工具读取 Header
import struct

with open('model.filamesh', 'rb') as f:
    magic = f.read(4)
    version = struct.unpack('I', f.read(4))[0]
    vertex_count = struct.unpack('I', f.read(4))[0]
    index_count = struct.unpack('I', f.read(4))[0]

    print(f"Magic: {magic}")
    print(f"Version: {version}")
    print(f"Vertices: {vertex_count}")
    print(f"Indices: {index_count}")
```

### Q5: filamesh vs 直接创建 VertexBuffer？

**对比**:
```cpp
// 方法 A: 使用 filamesh
MeshReader::Mesh mesh = MeshReader::loadMeshFromBuffer(...);
// 优势: 简单、快速、格式统一
// 劣势: 固定格式、灵活性低

// 方法 B: 手动创建
VertexBuffer* vb = VertexBuffer::Builder()
    .vertexCount(count)
    .bufferCount(1)
    .attribute(VertexAttribute::POSITION, ...)
    .attribute(VertexAttribute::NORMAL, ...)
    .build(*engine);
// 优势: 完全控制、自定义属性
// 劣势: 代码多、容易出错

// 建议:
// - 静态资产 → filamesh
// - 程序化内容 → 手动创建
```

## 最佳实践

### 1. DCC 工具导出设置

**Blender**:
```
File → Export → Wavefront (.obj)

✅ Selection Only (如需要)
✅ Apply Modifiers
✅ Include UVs
✅ Include Normals
✅ Triangulate Faces (重要!)
✅ Y Forward, Z Up (Filament 标准)
```

**Maya**:
```
File → Export Selection → OBJ

✅ Triangulate
✅ Normals
✅ UVs
```

### 2. 转换参数选择

```bash
# 标准 PBR 模型（推荐）
filamesh --tangents --compress model.obj model.filamesh

# 无纹理模型
filamesh model.obj model.filamesh

# 大模型、性能优先
filamesh --tangents --compress --strip model.obj model.filamesh

# 调试、保持精度
filamesh --tangents model.obj model.filamesh
```

### 3. 集成到资产管线

```bash
# 资产管线结构
project/
├── assets_source/        # 源文件
│   └── models/
│       └── chair.obj
├── assets_intermediate/  # 中间文件
│   └── models/
│       └── chair.filamesh
└── assets_final/         # 最终打包
    └── models.pak

# 构建脚本
./build_assets.sh
  → 转换 OBJ → filamesh
  → 编译材质
  → 压缩纹理
  → 打包所有资源
```

## 相关文档

- [06-gltf-tools.md](06-gltf-tools.md) - glTF 工具链（推荐）
- [../gltf/01-format-spec.md](../gltf/01-format-spec.md) - glTF 格式
- [../engine/03-resource-management.md](../engine/03-resource-management.md) - 网格资源管理
- [02-matc-compiler.md](02-matc-compiler.md) - 材质编译

## 总结

**filamesh 核心要点**:

1. **定位**: 简单静态网格转换工具
2. **推荐**: 优先使用 glTF，filamesh 仅在特定场景
3. **优势**: 文件小、加载快、格式简单
4. **劣势**: 无材质、无动画、无场景层级
5. **参数**: `--tangents` (PBR必需), `--compress` (推荐)

**典型工作流程**:

```bash
# 1. Blender 导出 OBJ (✅ Triangulate, ✅ Normals, ✅ UVs)
# 2. 转换
filamesh --tangents --compress model.obj model.filamesh

# 3. 加载
MeshReader::Mesh mesh = MeshReader::loadMeshFromBuffer(...);

# 4. 创建 Renderable
RenderableManager::Builder(1)
    .geometry(0, ..., mesh.vertexBuffer, mesh.indexBuffer)
    .material(0, materialInstance)
    .build(*engine, entity);
```

**迁移到 glTF**:
如果你当前使用 filamesh，考虑迁移到 glTF 以获得更完整的功能和更好的工具支持。glTF 是行业标准，Filament 也优先推荐使用 glTF。

filamesh 会继续维护，但新项目强烈建议直接使用 glTF！
