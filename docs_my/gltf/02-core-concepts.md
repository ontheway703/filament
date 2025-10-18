# glTF 核心概念详解

本文档深入讲解 glTF 2.0 的核心概念，包括场景结构、网格、材质、纹理、动画和骨骼系统。

---

## 目录

1. [场景和节点](#场景和节点)
2. [网格和几何体](#网格和几何体)
3. [材质系统](#材质系统)
4. [纹理系统](#纹理系统)
5. [动画系统](#动画系统)
6. [骨骼蒙皮](#骨骼蒙皮)
7. [相机](#相机)
8. [变形目标](#变形目标)

---

## 场景和节点

### 场景（Scenes）

场景是 glTF 的顶层容器，包含一组根节点：

```json
{
  "scenes": [
    {
      "name": "MainScene",
      "nodes": [0, 1, 2],  // 根节点列表
      "extensions": {},
      "extras": {}
    },
    {
      "name": "AlternativeScene",
      "nodes": [3, 4]
    }
  ],
  "scene": 0  // 默认场景索引
}
```

**特点**:
- 一个 glTF 文件可以有多个场景
- `scene` 字段指定默认场景
- 场景之间不共享节点（不同的根节点）

### 节点（Nodes）

节点是场景图的基本单元，形成树状层级：

```json
{
  "nodes": [
    {
      "name": "Root",
      "children": [1, 2],
      "translation": [0, 0, 0],
      "rotation": [0, 0, 0, 1],
      "scale": [1, 1, 1]
    },
    {
      "name": "Body",
      "mesh": 0,
      "skin": 0,
      "children": [3, 4]
    },
    {
      "name": "Camera",
      "camera": 0,
      "translation": [0, 5, 10]
    }
  ]
}
```

### 变换（Transforms）

节点变换有两种表示方式：

#### 方式 1: TRS（推荐）

```json
{
  "translation": [x, y, z],           // 平移（默认 [0,0,0]）
  "rotation": [x, y, z, w],           // 四元数旋转（默认 [0,0,0,1]）
  "scale": [x, y, z]                  // 缩放（默认 [1,1,1]）
}
```

**四元数旋转**:
```javascript
// 从欧拉角（角度）转换为四元数
function eulerToQuaternion(pitch, yaw, roll) {
    const cy = Math.cos(yaw * 0.5);
    const sy = Math.sin(yaw * 0.5);
    const cp = Math.cos(pitch * 0.5);
    const sp = Math.sin(pitch * 0.5);
    const cr = Math.cos(roll * 0.5);
    const sr = Math.sin(roll * 0.5);

    return [
        sr * cp * cy - cr * sp * sy,  // x
        cr * sp * cy + sr * cp * sy,  // y
        cr * cp * sy - sr * sp * cy,  // z
        cr * cp * cy + sr * sp * sy   // w
    ];
}

// 示例: 绕 Y 轴旋转 45 度
const rotation = eulerToQuaternion(0, Math.PI / 4, 0);
// [0, 0.3826834, 0, 0.9238795]
```

**组合顺序**:
```
最终矩阵 = Translation × Rotation × Scale
```

#### 方式 2: 矩阵（不推荐）

```json
{
  "matrix": [
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1
  ]
}
```

**列主序 4×4 矩阵**:
```
[m0  m4  m8   m12]   [Xx  Yx  Zx  Tx]
[m1  m5  m9   m13] = [Xy  Yy  Zy  Ty]
[m2  m6  m10  m14]   [Xz  Yz  Zz  Tz]
[m3  m7  m11  m15]   [0   0   0   1 ]

X, Y, Z = 轴向量（包含缩放）
T = 平移向量
```

**限制**:
- `matrix` 和 TRS 不能同时存在
- 矩阵必须是可分解的（仿射变换）
- 不推荐使用（动画和编辑困难）

### 层级变换

子节点继承父节点的变换：

```json
{
  "nodes": [
    {
      "name": "Parent",
      "translation": [5, 0, 0],
      "children": [1]
    },
    {
      "name": "Child",
      "translation": [0, 3, 0]
    }
  ]
}
```

**计算世界坐标**:
```
Child 世界位置 = Parent 变换 × Child 局部位置
              = (5, 0, 0) + (0, 3, 0)
              = (5, 3, 0)
```

**完整世界矩阵**:
```javascript
function getWorldMatrix(nodeIndex, nodes) {
    const node = nodes[nodeIndex];
    let localMatrix = getLocalMatrix(node);

    // 递归计算父节点矩阵
    if (node.parent !== undefined) {
        const parentMatrix = getWorldMatrix(node.parent, nodes);
        return multiply(parentMatrix, localMatrix);
    }

    return localMatrix;
}
```

---

## 网格和几何体

### 网格（Meshes）

网格包含一个或多个图元（primitives）：

```json
{
  "meshes": [
    {
      "name": "Character",
      "primitives": [
        {
          "attributes": {
            "POSITION": 0,
            "NORMAL": 1,
            "TEXCOORD_0": 2,
            "JOINTS_0": 3,
            "WEIGHTS_0": 4
          },
          "indices": 5,
          "material": 0,
          "mode": 4
        },
        {
          "attributes": {
            "POSITION": 6,
            "NORMAL": 7
          },
          "indices": 8,
          "material": 1,
          "mode": 4
        }
      ],
      "weights": [0, 0]  // 变形目标默认权重
    }
  ]
}
```

### 图元（Primitives）

每个 primitive 是一个独立的渲染对象：

```json
{
  "primitives": [
    {
      "attributes": {
        "POSITION": 0,    // 必需
        "NORMAL": 1,      // 推荐
        "TANGENT": 2,     // 可选（用于法线贴图）
        "TEXCOORD_0": 3,  // UV set 0
        "TEXCOORD_1": 4,  // UV set 1
        "COLOR_0": 5,     // 顶点颜色
        "JOINTS_0": 6,    // 骨骼索引
        "WEIGHTS_0": 7    // 骨骼权重
      },
      "indices": 8,       // 可选（无索引 = 按顺序绘制）
      "material": 0,      // 可选（无材质 = 默认材质）
      "mode": 4,          // 渲染模式（默认 4 = TRIANGLES）
      "targets": [...]    // 变形目标（可选）
    }
  ]
}
```

### 顶点属性

#### POSITION（必需）

```json
{
  "accessors": [
    {
      "bufferView": 0,
      "componentType": 5126,  // FLOAT
      "count": 1024,
      "type": "VEC3",
      "min": [-1, -1, -1],    // 边界框最小值
      "max": [1, 1, 1]        // 边界框最大值
    }
  ]
}
```

**要求**:
- 类型: `VEC3`
- 组件类型: `FLOAT` (5126)
- 必须提供 `min` 和 `max`（用于剔除和 LOD）

#### NORMAL

```json
{
  "componentType": 5126,  // FLOAT
  "type": "VEC3",
  "normalized": false     // 单位向量，不需要归一化标志
}
```

**要求**:
- 单位向量（长度 = 1）
- 数量必须与 POSITION 相同

#### TANGENT

```json
{
  "componentType": 5126,  // FLOAT
  "type": "VEC4",         // 第4个分量是副切线方向（±1）
  "normalized": false
}
```

**格式**: `[tx, ty, tz, handedness]`
- `(tx, ty, tz)`: 切线向量（单位向量）
- `handedness`: ±1，确定副切线方向

**副切线计算**:
```glsl
vec3 bitangent = cross(normal, tangent.xyz) * tangent.w;
```

#### TEXCOORD_n

```json
{
  "componentType": 5126,  // FLOAT (推荐) 或 5121/5123 (归一化)
  "type": "VEC2",
  "normalized": false     // 或 true（如果使用整数）
}
```

**UV 坐标**:
- 原点: 左上角 (0, 0)
- 范围: 通常 [0, 1]，但可以超出（配合 wrap 模式）
- 支持多组 UV: `TEXCOORD_0`, `TEXCOORD_1`, ...

**整数 UV（优化内存）**:
```json
{
  "componentType": 5123,  // UNSIGNED_SHORT
  "type": "VEC2",
  "normalized": true      // 自动映射到 [0, 1]
}
```

#### COLOR_n

```json
{
  "componentType": 5126,  // FLOAT
  "type": "VEC3",         // RGB 或 VEC4 (RGBA)
  "normalized": false
}
```

**或使用归一化整数**:
```json
{
  "componentType": 5121,  // UNSIGNED_BYTE
  "type": "VEC4",
  "normalized": true      // [0,255] → [0,1]
}
```

#### JOINTS_n 和 WEIGHTS_n

用于骨骼蒙皮：

```json
// JOINTS_0
{
  "componentType": 5121,  // UNSIGNED_BYTE 或 5123 (UNSIGNED_SHORT)
  "type": "VEC4",         // 最多 4 个骨骼
  "normalized": false
}

// WEIGHTS_0
{
  "componentType": 5126,  // FLOAT
  "type": "VEC4",
  "normalized": false     // 或归一化整数
}
```

**要求**:
- JOINTS 和 WEIGHTS 必须成对出现
- 权重之和应为 1.0
- 支持多组: `JOINTS_0` + `WEIGHTS_0`, `JOINTS_1` + `WEIGHTS_1`, ...

### 渲染模式（mode）

```json
{
  "mode": 4  // TRIANGLES
}
```

**可用模式**:
```
0 = POINTS
1 = LINES
2 = LINE_LOOP
3 = LINE_STRIP
4 = TRIANGLES (默认)
5 = TRIANGLE_STRIP
6 = TRIANGLE_FAN
```

**注意**:
- 大多数引擎只支持 `TRIANGLES` (4)
- 其他模式可能需要转换

### 索引（Indices）

```json
{
  "indices": 0  // Accessor 索引
}
```

**Accessor 定义**:
```json
{
  "accessors": [
    {
      "bufferView": 3,
      "componentType": 5123,  // UNSIGNED_SHORT (推荐)
      "count": 3072,          // 索引数量（三角形 × 3）
      "type": "SCALAR"
    }
  ]
}
```

**组件类型**:
- `5121` = UNSIGNED_BYTE (0-255)
- `5123` = UNSIGNED_SHORT (0-65535，推荐)
- `5125` = UNSIGNED_INT (0-4294967295)

**无索引模式**:
省略 `indices` 字段，顶点按顺序绘制。

---

## 材质系统

glTF 使用 **PBR Metallic-Roughness** 工作流。

### 材质定义

```json
{
  "materials": [
    {
      "name": "CharacterSkin",

      "pbrMetallicRoughness": {
        "baseColorFactor": [1.0, 0.9, 0.8, 1.0],
        "baseColorTexture": {
          "index": 0,
          "texCoord": 0
        },
        "metallicFactor": 0.0,
        "roughnessFactor": 0.5,
        "metallicRoughnessTexture": {
          "index": 1,
          "texCoord": 0
        }
      },

      "normalTexture": {
        "index": 2,
        "texCoord": 0,
        "scale": 1.0
      },

      "occlusionTexture": {
        "index": 3,
        "texCoord": 0,
        "strength": 1.0
      },

      "emissiveTexture": {
        "index": 4,
        "texCoord": 0
      },
      "emissiveFactor": [0, 0, 0],

      "alphaMode": "OPAQUE",
      "alphaCutoff": 0.5,
      "doubleSided": false
    }
  ]
}
```

### PBR Metallic-Roughness 参数

#### baseColorFactor

```json
{
  "baseColorFactor": [r, g, b, a]  // 默认 [1, 1, 1, 1]
}
```

**含义**:
- RGB: 基础颜色（线性空间）
- A: 不透明度
- 与纹理相乘: `finalColor = factor × texture`

#### baseColorTexture

```json
{
  "baseColorTexture": {
    "index": 0,      // Texture 索引
    "texCoord": 0,   // UV set 索引（默认 0）
    "extensions": {}
  }
}
```

**要求**:
- 颜色空间: sRGB
- 格式: RGB 或 RGBA
- Alpha 通道用于透明度

#### metallicFactor

```json
{
  "metallicFactor": 0.0  // 范围 [0, 1]，默认 1.0
}
```

**含义**:
- `0.0` = 非金属（绝缘体）
- `1.0` = 金属
- 中间值通常不真实（除了生锈金属等过渡）

#### roughnessFactor

```json
{
  "roughnessFactor": 0.5  // 范围 [0, 1]，默认 1.0
}
```

**含义**:
- `0.0` = 光滑镜面
- `1.0` = 完全粗糙（漫反射）

#### metallicRoughnessTexture

```json
{
  "metallicRoughnessTexture": {
    "index": 1,
    "texCoord": 0
  }
}
```

**通道映射**:
- **R 通道**: 未使用
- **G 通道**: Roughness（粗糙度）
- **B 通道**: Metallic（金属度）
- **A 通道**: 未使用

**颜色空间**: 线性（Non-Color Data）

**组合**:
```glsl
float metallic = metallicFactor * texture.b;
float roughness = roughnessFactor * texture.g;
```

### 其他纹理

#### normalTexture

```json
{
  "normalTexture": {
    "index": 2,
    "texCoord": 0,
    "scale": 1.0  // 法线强度，默认 1.0
  }
}
```

**格式**:
- 切线空间法线贴图
- RGB 通道: `(x, y, z)` 映射到 `[0, 1]`
- 实际法线: `normal = texture.rgb * 2.0 - 1.0`
- 颜色空间: 线性

**scale 参数**:
```glsl
vec3 normal = texture(normalMap, uv).rgb * 2.0 - 1.0;
normal.xy *= scale;  // 调整法线强度
normal = normalize(normal);
```

#### occlusionTexture

```json
{
  "occlusionTexture": {
    "index": 3,
    "texCoord": 0,
    "strength": 1.0  // AO 强度，默认 1.0
  }
}
```

**格式**:
- R 通道: 遮蔽值（0 = 完全遮蔽，1 = 无遮蔽）
- 颜色空间: 线性

**应用**:
```glsl
float ao = texture(occlusionMap, uv).r;
ao = mix(1.0, ao, strength);
finalColor *= ao;
```

#### emissiveTexture 和 emissiveFactor

```json
{
  "emissiveTexture": {
    "index": 4,
    "texCoord": 0
  },
  "emissiveFactor": [1, 1, 1]  // 默认 [0, 0, 0]
}
```

**格式**:
- RGB: 自发光颜色
- 颜色空间: sRGB
- 不受光照影响

**组合**:
```glsl
vec3 emissive = emissiveFactor * texture(emissiveMap, uv).rgb;
finalColor += emissive;
```

### Alpha 模式

```json
{
  "alphaMode": "OPAQUE",  // OPAQUE | MASK | BLEND
  "alphaCutoff": 0.5      // 仅用于 MASK 模式
}
```

**模式详解**:

#### OPAQUE（默认）

完全不透明，忽略 alpha 值：
```glsl
// Alpha 被忽略
outputColor.a = 1.0;
```

#### MASK

基于阈值的 alpha 测试：
```glsl
if (baseColor.a < alphaCutoff) {
    discard;  // 丢弃片段
}
outputColor.a = 1.0;
```

**用途**: 树叶、栅栏等硬边透明

#### BLEND

Alpha 混合：
```glsl
outputColor = vec4(color.rgb, color.a);
// 需要启用混合: glEnable(GL_BLEND)
```

**用途**: 玻璃、水、半透明材质

**渲染顺序**:
- OPAQUE: 任意顺序
- MASK: 任意顺序
- BLEND: 必须从后向前排序

### doubleSided

```json
{
  "doubleSided": false  // 默认 false
}
```

**效果**:
- `false`: 背面剔除（只渲染正面）
- `true`: 双面渲染（禁用背面剔除）

**性能**: 双面渲染性能降低 ~50%

---

## 纹理系统

### 三层结构

```
Material → Texture → Image + Sampler
```

### 纹理（Textures）

```json
{
  "textures": [
    {
      "name": "BaseColorTexture",
      "source": 0,    // Image 索引
      "sampler": 0,   // Sampler 索引（可选）
      "extensions": {}
    }
  ]
}
```

### 图像（Images）

```json
{
  "images": [
    {
      "name": "baseColor",
      "uri": "textures/baseColor.png"
    },
    {
      "uri": "textures/normal.png"
    },
    {
      "bufferView": 2,
      "mimeType": "image/jpeg"
    }
  ]
}
```

**三种来源**:

#### 1. 外部文件（uri）

```json
{
  "uri": "textures/albedo.png"
}
```

#### 2. Data URI

```json
{
  "uri": "data:image/png;base64,iVBORw0KGgo..."
}
```

#### 3. BufferView（GLB 内嵌）

```json
{
  "bufferView": 2,
  "mimeType": "image/png"  // 或 "image/jpeg"
}
```

**支持格式**:
- PNG（推荐，无损）
- JPEG（有损，适合照片）
- KTX2 / Basis Universal（通过扩展）

### 采样器（Samplers）

```json
{
  "samplers": [
    {
      "name": "DefaultSampler",
      "magFilter": 9729,  // LINEAR
      "minFilter": 9987,  // LINEAR_MIPMAP_LINEAR
      "wrapS": 10497,     // REPEAT
      "wrapT": 10497      // REPEAT
    }
  ]
}
```

#### 放大过滤（magFilter）

```
9728 = NEAREST (nearest neighbor，像素化)
9729 = LINEAR (双线性插值，平滑，默认)
```

#### 缩小过滤（minFilter）

```
9728 = NEAREST
9729 = LINEAR
9984 = NEAREST_MIPMAP_NEAREST
9985 = LINEAR_MIPMAP_NEAREST
9986 = NEAREST_MIPMAP_LINEAR
9987 = LINEAR_MIPMAP_LINEAR (三线性，推荐，默认)
```

**Mipmap**: 预计算的缩小版本，提升性能和质量。

#### 包裹模式（wrapS, wrapT）

```
10497 = REPEAT (重复，默认)
33071 = CLAMP_TO_EDGE (边缘拉伸)
33648 = MIRRORED_REPEAT (镜像重复)
```

**示例**:
```
REPEAT:          [0,1] → [0,1], [1,2] → [0,1], ...
CLAMP_TO_EDGE:   [-1,2] → [0,1] (超出部分取边缘)
MIRRORED_REPEAT: [0,1] → [0,1], [1,2] → [1,0], [2,3] → [0,1], ...
```

### 默认采样器

如果 Texture 没有指定 sampler，使用默认值：
```json
{
  "magFilter": 9729,  // LINEAR
  "minFilter": 9987,  // LINEAR_MIPMAP_LINEAR
  "wrapS": 10497,     // REPEAT
  "wrapT": 10497      // REPEAT
}
```

---

## 动画系统

### 动画结构

```json
{
  "animations": [
    {
      "name": "Run",
      "channels": [
        {
          "sampler": 0,
          "target": {
            "node": 1,
            "path": "translation"
          }
        },
        {
          "sampler": 1,
          "target": {
            "node": 1,
            "path": "rotation"
          }
        }
      ],
      "samplers": [
        {
          "input": 0,   // 时间轴 Accessor
          "output": 1,  // 值 Accessor
          "interpolation": "LINEAR"
        },
        {
          "input": 0,
          "output": 2,
          "interpolation": "LINEAR"
        }
      ]
    }
  ]
}
```

### 动画通道（Channels）

```json
{
  "channels": [
    {
      "sampler": 0,       // Sampler 索引
      "target": {
        "node": 1,        // 目标节点
        "path": "translation"  // 动画属性
      }
    }
  ]
}
```

**path 值**:
- `"translation"`: 平移
- `"rotation"`: 旋转（四元数）
- `"scale"`: 缩放
- `"weights"`: 变形目标权重

### 动画采样器（Samplers）

```json
{
  "samplers": [
    {
      "input": 0,              // 时间 Accessor
      "output": 1,             // 值 Accessor
      "interpolation": "LINEAR" // 插值方式
    }
  ]
}
```

#### input - 时间轴

```json
{
  "accessors": [
    {
      "bufferView": 0,
      "componentType": 5126,  // FLOAT
      "count": 100,
      "type": "SCALAR",
      "min": [0.0],
      "max": [3.33]  // 动画时长
    }
  ]
}
```

**要求**:
- 类型: `SCALAR`
- 组件: `FLOAT`
- 严格递增（sorted）
- 单位: 秒

#### output - 值数据

根据 path 不同：

**translation**:
```json
{
  "componentType": 5126,  // FLOAT
  "type": "VEC3",
  "count": 100  // 与 input 相同
}
```

**rotation**:
```json
{
  "componentType": 5126,  // FLOAT
  "type": "VEC4",  // 四元数 (x, y, z, w)
  "count": 100
}
```

**scale**:
```json
{
  "componentType": 5126,  // FLOAT
  "type": "VEC3",
  "count": 100
}
```

**weights**:
```json
{
  "componentType": 5126,  // FLOAT
  "type": "SCALAR",  // 每个权重一个值
  "count": 500  // 100 关键帧 × 5 个变形目标
}
```

#### interpolation - 插值方式

```json
{
  "interpolation": "LINEAR"  // LINEAR | STEP | CUBICSPLINE
}
```

**LINEAR** (默认):
```glsl
value = mix(keyframe1, keyframe2, t);
```

**STEP**:
```glsl
value = (t < 1.0) ? keyframe1 : keyframe2;  // 阶跃，无插值
```

**CUBICSPLINE**:
Hermite 样条插值，每个关键帧需要 3 个值：
```
[in-tangent, value, out-tangent]
```

output count = input count × 3

---

## 骨骼蒙皮

### 蒙皮（Skins）

```json
{
  "skins": [
    {
      "name": "CharacterSkin",
      "inverseBindMatrices": 0,  // Accessor 索引
      "joints": [1, 2, 3, 4, 5], // 骨骼节点索引
      "skeleton": 1              // 根骨骼（可选）
    }
  ]
}
```

### inverseBindMatrices

```json
{
  "accessors": [
    {
      "bufferView": 0,
      "componentType": 5126,  // FLOAT
      "count": 5,             // 骨骼数量
      "type": "MAT4"          // 4×4 矩阵
    }
  ]
}
```

**逆绑定矩阵**: 将顶点从模型空间转换到骨骼空间。

### 蒙皮计算

```glsl
// 顶点着色器
uniform mat4 u_jointMatrices[NUM_JOINTS];  // 骨骼矩阵

vec4 skinPosition = vec4(0.0);
vec4 skinNormal = vec4(0.0);

for (int i = 0; i < 4; i++) {
    int jointIndex = int(a_joints[i]);
    float weight = a_weights[i];

    mat4 jointMatrix = u_jointMatrices[jointIndex];

    skinPosition += weight * (jointMatrix * vec4(a_position, 1.0));
    skinNormal += weight * (jointMatrix * vec4(a_normal, 0.0));
}

gl_Position = u_viewProjection * skinPosition;
```

**骨骼矩阵计算**:
```
jointMatrix = nodeWorldMatrix × inverseBindMatrix
```

---

## 相机

### 相机类型

```json
{
  "cameras": [
    {
      "name": "MainCamera",
      "type": "perspective",
      "perspective": {
        "aspectRatio": 1.7778,
        "yfov": 0.7854,
        "zfar": 1000,
        "znear": 0.1
      }
    },
    {
      "name": "OrthoCamera",
      "type": "orthographic",
      "orthographic": {
        "xmag": 10,
        "ymag": 10,
        "zfar": 100,
        "znear": 0.1
      }
    }
  ]
}
```

### 透视相机（Perspective）

```json
{
  "perspective": {
    "aspectRatio": 1.7778,  // 可选，宽高比
    "yfov": 0.7854,         // 必需，垂直 FOV（弧度）
    "zfar": 1000,           // 可选，远裁剪面
    "znear": 0.1            // 必需，近裁剪面
  }
}
```

**投影矩阵**:
```
aspect = aspectRatio (或由渲染目标计算)
f = 1 / tan(yfov / 2)

[f/aspect  0    0              0        ]
[0         f    0              0        ]
[0         0    (far+near)/    2*far*near/]
                (near-far)     (near-far)
[0         0   -1              0        ]
```

### 正交相机（Orthographic）

```json
{
  "orthographic": {
    "xmag": 10,    // 必需，水平缩放
    "ymag": 10,    // 必需，垂直缩放
    "zfar": 100,   // 必需，远裁剪面
    "znear": 0.1   // 必需，近裁剪面
  }
}
```

---

## 变形目标

### 定义

```json
{
  "meshes": [
    {
      "primitives": [
        {
          "attributes": {
            "POSITION": 0,
            "NORMAL": 1
          },
          "targets": [
            {
              "POSITION": 2,  // 位置偏移
              "NORMAL": 3     // 法线偏移
            },
            {
              "POSITION": 4,
              "NORMAL": 5
            }
          ]
        }
      ],
      "weights": [0.0, 0.0],  // 默认权重
      "extras": {
        "targetNames": ["Smile", "Frown"]
      }
    }
  ]
}
```

### 计算

```glsl
vec3 position = a_position;
vec3 normal = a_normal;

for (int i = 0; i < numTargets; i++) {
    position += weights[i] * a_target_position[i];
    normal += weights[i] * a_target_normal[i];
}
```

---

## 总结

glTF 核心概念：

1. **场景结构**: Scene → Nodes (树状层级) → Mesh/Camera/Skin
2. **网格**: Mesh → Primitives (材质分组) → Attributes + Indices
3. **材质**: PBR Metallic-Roughness 工作流
4. **纹理**: Image (数据) + Sampler (过滤) → Texture
5. **动画**: Channel (目标) + Sampler (时间 + 插值) → 关键帧
6. **骨骼**: Skin (逆绑定矩阵) + Joints + Weights → 蒙皮变形

**关键要点**:
- 使用 TRS 而非矩阵（动画友好）
- PBR 材质使用线性工作流
- 骨骼权重之和应为 1.0
- 动画时间轴必须严格递增
- 所有向量和矩阵都是列主序
