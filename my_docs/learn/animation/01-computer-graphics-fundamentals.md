# 计算机图形学基础

本文档介绍与 Filament 动画系统相关的计算机图形学基础概念。

## 目录

1. [坐标系统与变换](#坐标系统与变换)
2. [矩阵运算](#矩阵运算)
3. [四元数与旋转](#四元数与旋转)
4. [渲染管线基础](#渲染管线基础)
5. [几何表示](#几何表示)

## 坐标系统与变换

### 3D 坐标系统

在 3D 图形学中，我们使用多个坐标系统来描述和变换对象：

#### 1. 本地坐标系 (Local/Object Space)
- 对象相对于自身原点的坐标
- 建模时使用的坐标系
- 便于对象的几何操作

#### 2. 世界坐标系 (World Space)
- 场景中的全局坐标系
- 所有对象都相对于同一个原点
- 用于物理模拟和碰撞检测

#### 3. 视图坐标系 (View/Camera Space)
- 相对于摄像机的坐标系
- 摄像机位于原点，看向 -Z 方向
- 用于光照计算

#### 4. 投影坐标系 (Clip Space)
- 经过投影变换后的坐标
- 通常是 [-1, 1] 的立方体
- 用于裁剪和深度测试

### 变换类型

#### 平移 (Translation)
```
T = [1  0  0  tx]
    [0  1  0  ty]
    [0  0  1  tz]
    [0  0  0  1 ]
```

#### 缩放 (Scale)
```
S = [sx 0  0  0]
    [0  sy 0  0]
    [0  0  sz 0]
    [0  0  0  1]
```

#### 旋转 (Rotation)
绕 Z 轴旋转角度 θ：
```
Rz = [cos(θ) -sin(θ) 0 0]
     [sin(θ)  cos(θ) 0 0]
     [0       0      1 0]
     [0       0      0 1]
```

### 变换组合

在 Filament 中，变换通常按以下顺序组合：
```
M = T × R × S
```
其中：
- S: 缩放矩阵
- R: 旋转矩阵
- T: 平移矩阵

## 矩阵运算

### 4x4 变换矩阵

在 3D 图形学中，我们使用 4x4 矩阵来表示仿射变换：

```cpp
// Filament 中的矩阵表示 (column-major)
mat4f transform = {
    {sx*xx, sx*xy, sx*xz, 0},  // 第一列
    {sy*yx, sy*yy, sy*yz, 0},  // 第二列
    {sz*zx, sz*zy, sz*zz, 0},  // 第三列
    {tx,    ty,    tz,    1}   // 第四列 (平移)
};
```

### 矩阵分解

Filament 的动画系统需要将变换矩阵分解为 TRS 组件：

```cpp
// 在 gltfio/math.h 中
void decomposeMatrix(const mat4f& matrix,
                    float3* translation,
                    quatf* rotation,
                    float3* scale);
```

### 逆矩阵计算

在骨骼动画中，经常需要计算逆矩阵：
- **逆绑定矩阵 (Inverse Bind Matrix)**: 将顶点从模型空间变换到骨骼空间
- **世界矩阵的逆**: 用于光照计算中的法线变换

## 四元数与旋转

### 为什么使用四元数？

相比欧拉角和旋转矩阵，四元数有以下优势：
1. **避免万向锁**: 不存在奇异点
2. **插值平滑**: SLERP 插值产生平滑的旋转
3. **存储紧凑**: 只需 4 个浮点数
4. **数值稳定**: 避免累积误差

### 四元数表示

```cpp
// 在 Filament 中
struct quatf {
    float x, y, z, w;  // w 是标量部分
};

// 从轴角创建四元数
quatf quat = normalize(quatf(sin(θ/2) * axis, cos(θ/2)));
```

### 四元数运算

#### 球面线性插值 (SLERP)
```cpp
quatf slerp(const quatf& q1, const quatf& q2, float t) {
    float dot = q1.x*q2.x + q1.y*q2.y + q1.z*q2.z + q1.w*q2.w;

    if (dot < 0.0f) {
        q2 = -q2;  // 选择较短路径
        dot = -dot;
    }

    if (dot > 0.9995f) {
        // 线性插值避免数值不稳定
        return normalize(q1 + t * (q2 - q1));
    }

    float theta = acos(abs(dot));
    float sin_theta = sin(theta);
    float t1 = sin((1-t) * theta) / sin_theta;
    float t2 = sin(t * theta) / sin_theta;

    return t1 * q1 + t2 * q2;
}
```

## 渲染管线基础

### 图形渲染管线概述

```
Vertices → Vertex Shader → Primitive Assembly → Rasterization → Fragment Shader → Output
```

### 在动画中的应用

#### 1. 顶点着色器阶段
- 骨骼变换 (Skinning)
- 顶点变形 (Morphing)
- 世界坐标变换

#### 2. 几何着色器阶段 (可选)
- 毛发渲染
- 粒子系统
- 细分表面

#### 3. 片段着色器阶段
- 材质属性插值
- 光照计算
- 纹理采样

### GPU 内存模型

#### 顶点属性
```glsl
// 骨骼动画所需的顶点属性
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec4 boneIndices;   // 骨骼索引
layout(location = 4) in vec4 boneWeights;   // 骨骼权重
```

#### Uniform 缓冲区对象 (UBO)
```glsl
// 骨骼矩阵 UBO
layout(std140, binding = 0) uniform BoneMatrices {
    mat4 bones[256];  // 最多 256 个骨骼
};
```

## 几何表示

### 网格数据结构

在 Filament 中，几何体通过以下方式表示：

#### 顶点缓冲区 (VertexBuffer)
```cpp
VertexBuffer* vertexBuffer = VertexBuffer::Builder()
    .vertexCount(vertexCount)
    .bufferCount(1)
    .attribute(VertexAttribute::POSITION, 0,
               VertexBuffer::AttributeType::FLOAT3)
    .attribute(VertexAttribute::NORMAL, 0,
               VertexBuffer::AttributeType::FLOAT3)
    .attribute(VertexAttribute::BONE_INDICES, 0,
               VertexBuffer::AttributeType::UINT4)
    .attribute(VertexAttribute::BONE_WEIGHTS, 0,
               VertexBuffer::AttributeType::FLOAT4)
    .build(engine);
```

#### 索引缓冲区 (IndexBuffer)
```cpp
IndexBuffer* indexBuffer = IndexBuffer::Builder()
    .indexCount(indexCount)
    .bufferType(IndexBuffer::IndexType::USHORT)
    .build(engine);
```

### 边界框计算

对于动画对象，需要计算包含所有动画状态的边界框：

```cpp
// 在 AssetLoader 中计算 AABB
Aabb computeAnimatedAABB(const VertexBuffer* vertices,
                        const std::vector<mat4f>& boneMatrices,
                        const std::vector<mat4f>& inverseBindMatrices) {
    Aabb result;
    // 遍历所有顶点和骨骼变换
    // 计算最大可能的位置范围
    return result;
}
```

## 数学库

Filament 使用自己的数学库，位于 `libs/math/`：

### 主要类型
```cpp
// 向量类型
using float2 = math::vec2<float>;
using float3 = math::vec3<float>;
using float4 = math::vec4<float>;

// 矩阵类型
using mat3f = math::mat3<float>;
using mat4f = math::mat4<float>;

// 四元数
using quatf = math::quat<float>;
```

### 常用函数
```cpp
// 向量运算
float3 cross(const float3& a, const float3& b);
float dot(const float3& a, const float3& b);
float3 normalize(const float3& v);

// 矩阵运算
mat4f inverse(const mat4f& m);
mat4f transpose(const mat4f& m);

// 插值函数
float3 mix(const float3& a, const float3& b, float t);
quatf slerp(const quatf& a, const quatf& b, float t);
```

## 总结

理解这些计算机图形学基础对于掌握 Filament 的动画系统至关重要：

1. **坐标系统**: 理解不同坐标空间之间的变换
2. **矩阵运算**: 掌握变换的数学表示和组合
3. **四元数**: 用于平滑的旋转插值
4. **渲染管线**: 了解动画在 GPU 上的执行流程
5. **几何表示**: 理解网格数据的存储和访问

这些基础知识为理解后续的动画理论和 Filament 具体实现奠定了基础。