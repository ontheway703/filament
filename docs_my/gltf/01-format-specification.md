# glTF 2.0 格式规范

本文档详细说明 glTF 2.0 文件格式的规范、结构和数据组织方式。

---

## 目录

1. [格式概述](#格式概述)
2. [文件格式变体](#文件格式变体)
3. [JSON 结构](#json-结构)
4. [数据访问模式](#数据访问模式)
5. [资源引用](#资源引用)
6. [完整示例](#完整示例)

---

## 格式概述

### glTF 是什么？

**glTF** (GL Transmission Format) 是 Khronos Group 定义的 3D 场景和模型传输格式。设计目标：

- **高效传输**: 最小化文件大小和解析时间
- **运行时友好**: 数据布局接近 GPU 格式
- **可扩展**: 通过扩展机制支持新特性
- **完整**: 支持几何、材质、纹理、动画、骨骼等

### 设计理念

glTF 2.0 的核心设计理念：

1. **最小化加载时间** - 数据直接映射到 GPU 缓冲，无需转换
2. **紧凑表示** - 二进制格式，支持压缩
3. **扩展性** - 通过扩展添加新功能，保持向后兼容
4. **互操作性** - 跨平台、跨引擎的标准格式

---

## 文件格式变体

### 1. glTF (.gltf) - JSON + 外部文件

**结构**:
```
project/
  ├── model.gltf           # JSON 场景描述
  ├── model.bin            # 二进制几何数据
  ├── textures/
  │   ├── baseColor.png
  │   ├── normal.png
  │   └── metallicRoughness.png
  └── animations.bin       # 动画数据（可选）
```

**JSON 示例**:
```json
{
  "asset": {
    "version": "2.0",
    "generator": "Blender 4.0.0"
  },
  "buffers": [
    {
      "uri": "model.bin",
      "byteLength": 102400
    }
  ],
  "images": [
    {
      "uri": "textures/baseColor.png"
    }
  ]
}
```

**优点**:
- ✅ JSON 可读，易于调试和编辑
- ✅ 可以选择性加载外部资源
- ✅ 适合版本控制（文本格式）
- ✅ 便于自动化工具处理

**缺点**:
- ❌ 多个文件，管理复杂
- ❌ HTTP 请求多（Web 应用）
- ❌ 总体积略大于 GLB

**适用场景**:
- 开发和调试阶段
- 需要频繁修改的项目
- 版本控制系统中

### 2. GLB (.glb) - 二进制自包含

**结构**:
```
model.glb  # 单一二进制文件
```

**GLB 文件格式**:
```
┌─────────────────────────────────────┐
│ Header (12 bytes)                   │
│  - magic: 0x46546C67 ("glTF")       │
│  - version: 2                       │
│  - length: total file size          │
├─────────────────────────────────────┤
│ Chunk 0: JSON (aligned to 4 bytes)  │
│  - chunkLength: JSON 字节数         │
│  - chunkType: 0x4E4F534A ("JSON")   │
│  - chunkData: JSON 内容             │
├─────────────────────────────────────┤
│ Chunk 1: Binary (aligned to 4 bytes)│
│  - chunkLength: 二进制字节数        │
│  - chunkType: 0x004E4942 ("BIN\0")  │
│  - chunkData: 几何和动画数据        │
└─────────────────────────────────────┘
```

**优点**:
- ✅ 单一文件，易于分发
- ✅ 文件体积最小（无 JSON 缩进）
- ✅ 加载最快（单次 I/O）
- ✅ 适合生产环境

**缺点**:
- ❌ 二进制，不可读
- ❌ 难以手动编辑和调试
- ❌ 版本控制不友好

**适用场景**:
- 生产环境
- Web 应用和移动应用
- 最终发布版本

### 3. glTF + Data URI（不推荐）

**结构**:
```json
{
  "buffers": [
    {
      "uri": "data:application/octet-stream;base64,AAABAAIAA...",
      "byteLength": 1024
    }
  ],
  "images": [
    {
      "uri": "data:image/png;base64,iVBORw0KGgoAAAANS..."
    }
  ]
}
```

**问题**:
- ❌ Base64 编码增大文件 ~33%
- ❌ 解析性能最差
- ❌ 内存占用高
- ❌ **Blender 4.0+ 已移除此选项**

**现状**: 此格式已不推荐使用，应使用 GLB 替代。

---

## JSON 结构

### 顶层结构

```json
{
  "asset": {
    "version": "2.0",
    "generator": "工具名称",
    "copyright": "版权信息（可选）",
    "minVersion": "2.0（可选）"
  },

  "scene": 0,              // 默认场景索引
  "scenes": [...],         // 场景数组
  "nodes": [...],          // 节点数组
  "meshes": [...],         // 网格数组
  "materials": [...],      // 材质数组
  "textures": [...],       // 纹理数组
  "images": [...],         // 图像数组
  "samplers": [...],       // 采样器数组
  "accessors": [...],      // 访问器数组
  "bufferViews": [...],    // 缓冲视图数组
  "buffers": [...],        // 缓冲数组
  "skins": [...],          // 蒙皮数组
  "animations": [...],     // 动画数组
  "cameras": [...],        // 相机数组

  "extensions": {},        // 扩展（可选）
  "extensionsUsed": [],    // 使用的扩展列表
  "extensionsRequired": [], // 必需的扩展列表
  "extras": {}             // 自定义数据（可选）
}
```

### asset - 资产信息

必需字段，描述文件元数据：

```json
{
  "asset": {
    "version": "2.0",                    // 必需: glTF 版本
    "generator": "Blender 4.0.0",        // 可选: 生成工具
    "copyright": "© 2025 My Company",    // 可选: 版权
    "minVersion": "2.0"                  // 可选: 最低支持版本
  }
}
```

### scenes - 场景

场景是节点的集合：

```json
{
  "scenes": [
    {
      "name": "MainScene",
      "nodes": [0, 1, 2],  // 根节点索引列表
      "extensions": {},
      "extras": {}
    }
  ],
  "scene": 0  // 默认场景
}
```

### nodes - 节点

节点定义场景层级和变换：

```json
{
  "nodes": [
    {
      "name": "Character",

      // 变换方式 1: TRS (推荐)
      "translation": [0, 1.5, 0],
      "rotation": [0, 0, 0, 1],  // 四元数 (x, y, z, w)
      "scale": [1, 1, 1],

      // 变换方式 2: 矩阵 (不能与 TRS 共存)
      // "matrix": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1],

      // 组件引用
      "mesh": 0,        // 网格索引
      "skin": 0,        // 蒙皮索引
      "camera": 0,      // 相机索引
      "weights": [0.5, 0.5],  // 变形目标权重

      // 层级
      "children": [1, 2, 3],  // 子节点索引

      "extensions": {},
      "extras": {}
    }
  ]
}
```

**注意**:
- `matrix` 和 `TRS` 不能同时存在
- 四元数必须是单位四元数（归一化）
- 默认值: `translation = [0,0,0]`, `rotation = [0,0,0,1]`, `scale = [1,1,1]`

---

## 数据访问模式

glTF 使用三层结构访问二进制数据：

```
Accessor → BufferView → Buffer → 实际二进制数据
```

### 层级详解

#### 1. Buffer - 数据源

Buffer 是原始二进制数据的容器：

```json
{
  "buffers": [
    {
      "uri": "model.bin",      // 文件路径或 Data URI
      "byteLength": 102400,    // 字节数（必需）
      "name": "GeometryData"   // 可选
    },
    {
      // GLB 的内嵌 buffer 不需要 uri
      "byteLength": 51200
    }
  ]
}
```

**uri 格式**:
- 相对路径: `"geometry.bin"`
- 绝对路径: `"/path/to/geometry.bin"`
- Data URI: `"data:application/octet-stream;base64,..."`
- GLB 内嵌: 省略 `uri` 字段

#### 2. BufferView - 数据视图

BufferView 定义 Buffer 中的一个连续区域：

```json
{
  "bufferViews": [
    {
      "buffer": 0,           // Buffer 索引（必需）
      "byteOffset": 0,       // 字节偏移（默认 0）
      "byteLength": 25600,   // 字节长度（必需）
      "byteStride": 12,      // 步长（可选，用于交错数据）
      "target": 34962,       // GL 目标提示（可选）
      "name": "PositionsView"
    }
  ]
}
```

**target 值**:
- `34962` = `GL_ARRAY_BUFFER` (顶点数据)
- `34963` = `GL_ELEMENT_ARRAY_BUFFER` (索引数据)
- 省略 = 其他用途（动画、蒙皮等）

**byteStride**:
- 用于交错顶点数据
- 必须是 4 的倍数
- 范围: 4-252 字节

示例 - 交错数据（Position + Normal）:
```json
{
  "byteStride": 24,  // 3 floats (position) + 3 floats (normal) = 24 bytes
  "byteLength": 2400 // 100 vertices × 24 bytes
}
```

#### 3. Accessor - 数据解释器

Accessor 定义如何解释 BufferView 中的数据：

```json
{
  "accessors": [
    {
      "bufferView": 0,        // BufferView 索引（必需）
      "byteOffset": 0,        // BufferView 内的偏移（默认 0）
      "componentType": 5126,  // 组件类型（必需）
      "count": 1024,          // 元素数量（必需）
      "type": "VEC3",         // 数据类型（必需）
      "min": [-1, -1, -1],    // 最小值（可选，但推荐）
      "max": [1, 1, 1],       // 最大值（可选，但推荐）
      "normalized": false,    // 是否归一化（默认 false）
      "name": "PositionAccessor",
      "sparse": { /* ... */ } // 稀疏数据（可选）
    }
  ]
}
```

**componentType 值**:
```
5120 = GL_BYTE           (signed 8-bit)
5121 = GL_UNSIGNED_BYTE  (unsigned 8-bit)
5122 = GL_SHORT          (signed 16-bit)
5123 = GL_UNSIGNED_SHORT (unsigned 16-bit)
5125 = GL_UNSIGNED_INT   (unsigned 32-bit)
5126 = GL_FLOAT          (32-bit float)
```

**type 值**:
```
"SCALAR" - 单个值
"VEC2"   - 2D 向量
"VEC3"   - 3D 向量
"VEC4"   - 4D 向量
"MAT2"   - 2×2 矩阵
"MAT3"   - 3×3 矩阵
"MAT4"   - 4×4 矩阵
```

**数据大小计算**:
```
元素字节数 = componentSize × componentsPerType

例如:
VEC3 + FLOAT = 4 bytes × 3 = 12 bytes/element
```

### 数据布局示例

#### 示例 1: 非交错位置数据

```json
// Buffer
{
  "uri": "geometry.bin",
  "byteLength": 12288  // 1024 vertices × 3 floats × 4 bytes
}

// BufferView
{
  "buffer": 0,
  "byteOffset": 0,
  "byteLength": 12288,
  "target": 34962  // ARRAY_BUFFER
}

// Accessor
{
  "bufferView": 0,
  "componentType": 5126,  // FLOAT
  "count": 1024,
  "type": "VEC3",
  "min": [-10, 0, -10],
  "max": [10, 20, 10]
}
```

**内存布局**:
```
[x0, y0, z0, x1, y1, z1, x2, y2, z2, ...]
```

#### 示例 2: 交错数据（Position + Normal）

```json
// BufferView (交错)
{
  "buffer": 0,
  "byteOffset": 0,
  "byteLength": 24576,  // 1024 × 24
  "byteStride": 24,     // 每个顶点 24 字节
  "target": 34962
}

// Accessor - Position
{
  "bufferView": 0,
  "byteOffset": 0,      // 位置在开头
  "componentType": 5126,
  "count": 1024,
  "type": "VEC3"
}

// Accessor - Normal
{
  "bufferView": 0,
  "byteOffset": 12,     // 法线偏移 12 字节
  "componentType": 5126,
  "count": 1024,
  "type": "VEC3"
}
```

**内存布局**:
```
[px0, py0, pz0, nx0, ny0, nz0, px1, py1, pz1, nx1, ny1, nz1, ...]
 <--position0--> <--normal0-->  <--position1--> <--normal1-->
```

#### 示例 3: 索引数据

```json
// BufferView
{
  "buffer": 0,
  "byteOffset": 24576,
  "byteLength": 6144,   // 2048 triangles × 3 indices × 2 bytes
  "target": 34963       // ELEMENT_ARRAY_BUFFER
}

// Accessor
{
  "bufferView": 1,
  "componentType": 5123,  // UNSIGNED_SHORT
  "count": 6144,          // 2048 × 3
  "type": "SCALAR"
}
```

### 稀疏访问器（Sparse Accessor）

用于大部分值相同、只有少数不同的数据（如变形目标）：

```json
{
  "accessors": [
    {
      "bufferView": 0,
      "componentType": 5126,
      "count": 10000,
      "type": "VEC3",

      "sparse": {
        "count": 50,  // 只有 50 个顶点不同

        "indices": {
          "bufferView": 1,
          "componentType": 5123,  // UNSIGNED_SHORT
          "byteOffset": 0
        },

        "values": {
          "bufferView": 2,
          "byteOffset": 0
        }
      }
    }
  ]
}
```

**工作原理**:
1. 基础数据从 `bufferView` 0 读取（10000 个默认值）
2. 从 `sparse.indices` 读取需要替换的索引（50 个）
3. 从 `sparse.values` 读取替换值（50 个 VEC3）
4. 用替换值覆盖对应索引的基础值

**用途**:
- 变形目标（Morph Targets）
- 稀疏变换矩阵
- 大部分为零的数据

---

## 资源引用

### URI 格式

glTF 支持多种 URI 格式：

#### 1. 相对路径

```json
{
  "uri": "textures/baseColor.png"
}
```

**解析规则**:
- 相对于 .gltf 文件的路径
- 支持 `../` 等相对路径符号

#### 2. 绝对路径（不推荐）

```json
{
  "uri": "/Users/me/project/texture.png"
}
```

**问题**:
- 平台相关
- 不可移植

#### 3. Data URI

```json
{
  "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAA..."
}
```

**格式**:
```
data:[<mediatype>][;base64],<data>
```

**常用 MIME 类型**:
- `application/octet-stream` - 二进制数据
- `image/png` - PNG 图像
- `image/jpeg` - JPEG 图像

#### 4. GLB 内嵌

```json
{
  "buffers": [
    {
      // 省略 uri，表示 GLB 的 BIN chunk
      "byteLength": 102400
    }
  ]
}
```

### URL 编码

URI 中的特殊字符需要编码：

```
空格   → %20
中文   → UTF-8 编码后 URL 编码
#      → %23
?      → %3F
```

示例:
```
"my texture.png"     → "my%20texture.png"
"材质/基础颜色.png"  → "%E6%9D%90%E8%B4%A8/%E5%9F%BA%E7%A1%80%E9%A2%9C%E8%89%B2.png"
```

---

## 完整示例

### 最小化 glTF 文件

一个三角形：

```json
{
  "asset": {
    "version": "2.0"
  },

  "scene": 0,
  "scenes": [
    {
      "nodes": [0]
    }
  ],

  "nodes": [
    {
      "mesh": 0
    }
  ],

  "meshes": [
    {
      "primitives": [
        {
          "attributes": {
            "POSITION": 0
          },
          "indices": 1
        }
      ]
    }
  ],

  "accessors": [
    {
      "bufferView": 0,
      "componentType": 5126,
      "count": 3,
      "type": "VEC3",
      "min": [0, 0, 0],
      "max": [1, 1, 0]
    },
    {
      "bufferView": 1,
      "componentType": 5123,
      "count": 3,
      "type": "SCALAR"
    }
  ],

  "bufferViews": [
    {
      "buffer": 0,
      "byteOffset": 0,
      "byteLength": 36
    },
    {
      "buffer": 0,
      "byteOffset": 36,
      "byteLength": 6
    }
  ],

  "buffers": [
    {
      "uri": "triangle.bin",
      "byteLength": 42
    }
  ]
}
```

**triangle.bin 内容** (42 字节):
```
Positions (36 bytes):
  [0.0, 0.0, 0.0]  // 顶点 0
  [1.0, 0.0, 0.0]  // 顶点 1
  [0.5, 1.0, 0.0]  // 顶点 2

Indices (6 bytes):
  [0, 1, 2]  // 三角形
```

### 完整示例：立方体 + 纹理

```json
{
  "asset": {
    "version": "2.0",
    "generator": "手工编写"
  },

  "scene": 0,
  "scenes": [{"nodes": [0]}],

  "nodes": [
    {
      "name": "Cube",
      "mesh": 0,
      "rotation": [0, 0.7071, 0, 0.7071]  // 45度旋转
    }
  ],

  "meshes": [
    {
      "name": "CubeMesh",
      "primitives": [
        {
          "attributes": {
            "POSITION": 0,
            "NORMAL": 1,
            "TEXCOORD_0": 2
          },
          "indices": 3,
          "material": 0
        }
      ]
    }
  ],

  "materials": [
    {
      "name": "CubeMaterial",
      "pbrMetallicRoughness": {
        "baseColorTexture": {
          "index": 0
        },
        "metallicFactor": 0.0,
        "roughnessFactor": 0.5
      }
    }
  ],

  "textures": [
    {
      "source": 0,
      "sampler": 0
    }
  ],

  "images": [
    {
      "uri": "cubemap.png"
    }
  ],

  "samplers": [
    {
      "magFilter": 9729,  // LINEAR
      "minFilter": 9987,  // LINEAR_MIPMAP_LINEAR
      "wrapS": 10497,     // REPEAT
      "wrapT": 10497      // REPEAT
    }
  ],

  "accessors": [
    {
      "bufferView": 0,
      "componentType": 5126,
      "count": 24,
      "type": "VEC3",
      "min": [-1, -1, -1],
      "max": [1, 1, 1]
    },
    {
      "bufferView": 1,
      "componentType": 5126,
      "count": 24,
      "type": "VEC3"
    },
    {
      "bufferView": 2,
      "componentType": 5126,
      "count": 24,
      "type": "VEC2"
    },
    {
      "bufferView": 3,
      "componentType": 5123,
      "count": 36,
      "type": "SCALAR"
    }
  ],

  "bufferViews": [
    {
      "buffer": 0,
      "byteOffset": 0,
      "byteLength": 288,
      "target": 34962
    },
    {
      "buffer": 0,
      "byteOffset": 288,
      "byteLength": 288,
      "target": 34962
    },
    {
      "buffer": 0,
      "byteOffset": 576,
      "byteLength": 192,
      "target": 34962
    },
    {
      "buffer": 0,
      "byteOffset": 768,
      "byteLength": 72,
      "target": 34963
    }
  ],

  "buffers": [
    {
      "uri": "cube.bin",
      "byteLength": 840
    }
  ]
}
```

---

## 数据对齐要求

### 对齐规则

glTF 2.0 有严格的对齐要求：

| 数据类型 | 对齐 |
|---------|------|
| BYTE, UNSIGNED_BYTE | 1 字节 |
| SHORT, UNSIGNED_SHORT | 2 字节 |
| UNSIGNED_INT, FLOAT | 4 字节 |
| VEC2 (float) | 4 字节 |
| VEC3 (float) | 4 字节 |
| VEC4 (float) | 4 字节 |
| MAT2 (float) | 4 字节 |
| MAT3 (float) | 4 字节 |
| MAT4 (float) | 4 字节 |

### Accessor 对齐

```json
{
  "bufferView": 0,
  "byteOffset": 0,  // 必须是 componentSize 的倍数
  "componentType": 5126,  // FLOAT = 4 bytes
  "type": "VEC3"
}
```

**正确**:
```
byteOffset = 0, 4, 8, 12, ...  (4 的倍数)
```

**错误**:
```
byteOffset = 1, 2, 3, 5, ...  (不是 4 的倍数)
```

### BufferView 对齐

- GLB 的 JSON chunk 必须 4 字节对齐（填充空格）
- GLB 的 BIN chunk 必须 4 字节对齐（填充 0x00）

---

## 验证和工具

### glTF Validator

官方验证工具：https://github.khronos.org/glTF-Validator/

**使用**:
```bash
# 在线验证
https://github.khronos.org/glTF-Validator/

# 命令行
npm install -g gltf-validator
gltf_validator model.gltf
```

**检查项**:
- JSON 格式正确性
- 数据对齐
- 索引越界
- 缺失的必需字段
- 扩展兼容性

### 常见错误

#### 错误 1: 对齐问题

```json
// 错误
{
  "byteOffset": 3,  // 不是 4 的倍数
  "componentType": 5126  // FLOAT
}

// 正确
{
  "byteOffset": 4,
  "componentType": 5126
}
```

#### 错误 2: 索引越界

```json
{
  "nodes": [
    {
      "mesh": 5  // 但只有 3 个 meshes
    }
  ],
  "meshes": [{}, {}, {}]
}
```

#### 错误 3: 数据长度不匹配

```json
{
  "accessor": {
    "count": 100,
    "componentType": 5126,  // 4 bytes
    "type": "VEC3"  // 3 components
  },
  "bufferView": {
    "byteLength": 1000  // 应该是 100 × 3 × 4 = 1200
  }
}
```

---

## 总结

glTF 2.0 格式的关键要点：

1. **两种格式**: glTF (JSON + 外部文件) 和 GLB (单一二进制)
2. **三层数据访问**: Accessor → BufferView → Buffer
3. **严格对齐**: 所有数据必须正确对齐
4. **资源引用**: 支持相对路径、Data URI、GLB 内嵌
5. **可扩展**: 通过 extensions 机制添加新功能

**最佳实践**:
- 生产环境使用 GLB
- 开发阶段使用 glTF
- 始终使用验证工具
- 提供 min/max 值（用于边界框优化）
- 避免使用 Data URI（使用 GLB 代替）
