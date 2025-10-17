# 蒙皮和变形技术

本文档详细解析骨骼蒙皮和顶点变形的数学原理与实现技术。

## 目录

1. [骨骼蒙皮基础](#骨骼蒙皮基础)
2. [线性混合蒙皮 (LBS)](#线性混合蒙皮-lbs)
3. [双四元数蒙皮 (DQS)](#双四元数蒙皮-dqs)
4. [Morph Targets 变形](#morph-targets-变形)
5. [GPU 实现优化](#gpu-实现优化)
6. [常见问题与解决方案](#常见问题与解决方案)

## 骨骼蒙皮基础

### 蒙皮的目的
骨骼蒙皮将刚体骨骼变换应用到柔性网格上，实现自然的角色变形。

### 核心概念

```cpp
// 顶点蒙皮的基本公式
vertex_final = sum(weight_i * bone_matrix_i * vertex_original)

// 其中：
// weight_i: 第i个骨骼对该顶点的影响权重
// bone_matrix_i: 第i个骨骼的变换矩阵
// vertex_original: 顶点的原始位置
```

### 权重归一化
```cpp
// 确保权重和为1
float totalWeight = 0;
for (int i = 0; i < 4; ++i) {
    totalWeight += weights[i];
}

for (int i = 0; i < 4; ++i) {
    weights[i] /= totalWeight;
}
```

## 线性混合蒙皮 (LBS)

### 算法实现

```glsl
// 顶点着色器中的 LBS 实现
attribute vec3 position;
attribute vec4 boneIndices;
attribute vec4 boneWeights;

uniform mat4 boneMatrices[256];

void main() {
    vec4 skinnedPosition = vec4(0.0);
    
    for (int i = 0; i < 4; ++i) {
        int boneIndex = int(boneIndices[i]);
        float weight = boneWeights[i];
        
        if (weight > 0.0) {
            skinnedPosition += weight * (boneMatrices[boneIndex] * vec4(position, 1.0));
        }
    }
    
    gl_Position = projectionMatrix * modelViewMatrix * skinnedPosition;
}
```

### LBS 的问题

1. **糖果包装效应**: 关节旋转时出现体积损失
2. **塌陷**: 180度旋转时顶点可能穿过表面

## 双四元数蒙皮 (DQS)

### 双四元数表示
```cpp
struct DualQuaternion {
    quat real;  // 旋转部分
    quat dual;  // 平移部分
    
    static DualQuaternion fromMatrix(const mat4& m) {
        quat rotation = extractRotation(m);
        vec3 translation = extractTranslation(m);
        
        return DualQuaternion{
            rotation,
            0.5f * quat(translation.x, translation.y, translation.z, 0) * rotation
        };
    }
};
```

### DQS 顶点变换
```glsl
vec3 transformDualQuat(vec3 position, DualQuaternion dq) {
    vec4 real = dq.real;
    vec4 dual = dq.dual;
    
    // 旋转
    vec3 rotated = position + 2.0 * cross(real.xyz, 
                   cross(real.xyz, position) + real.w * position);
    
    // 平移
    vec3 translation = 2.0 * (real.w * dual.xyz - dual.w * real.xyz + 
                              cross(real.xyz, dual.xyz));
    
    return rotated + translation;
}
```

## Morph Targets 变形

### 基本原理
```glsl
// 顶点变形公式
vec3 morphedPosition = basePosition;
for (int i = 0; i < numTargets; ++i) {
    morphedPosition += morphWeights[i] * morphTargets[i].position;
}
```

### 优化存储
```cpp
// 稀疏存储：只保存变化的顶点
struct SparseMorphTarget {
    vector<uint32_t> vertexIndices;
    vector<vec3> positionDeltas;
    vector<vec3> normalDeltas;
};
```

## GPU 实现优化

### 纹理存储骨骼矩阵
```cpp
// 将骨骼矩阵存储为纹理
Texture2D* createBoneTexture(const vector<mat4>& bones) {
    const int width = 4;
    const int height = bones.size();
    
    vector<float> data(width * height * 4);
    for (size_t i = 0; i < bones.size(); ++i) {
        memcpy(&data[i * 16], &bones[i], sizeof(mat4));
    }
    
    return Texture2D::create(width, height, TextureFormat::RGBA32F, data.data());
}
```

### 计算着色器蒙皮
```glsl
layout(local_size_x = 64) in;

layout(std430, binding = 0) buffer VertexBuffer {
    vec4 vertices[];
};

layout(std430, binding = 1) buffer OutputBuffer {
    vec4 skinnedVertices[];
};

void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= vertices.length()) return;
    
    // 执行蒙皮计算
    skinnedVertices[index] = skinVertex(vertices[index], index);
}
```

## 常见问题与解决方案

### 权重绘制问题
```cpp
// 权重平滑算法
void smoothWeights(Mesh& mesh, int iterations) {
    for (int iter = 0; iter < iterations; ++iter) {
        for (auto& vertex : mesh.vertices) {
            vec4 smoothedWeights = vec4(0);
            
            for (auto neighbor : getNeighbors(vertex)) {
                smoothedWeights += neighbor.weights;
            }
            
            vertex.weights = normalize(smoothedWeights / getNeighborCount(vertex));
        }
    }
}
```

### 性能优化技巧
1. 限制每个顶点的骨骼影响数量（通常4个）
2. 使用16位索引减少内存带宽
3. 预计算逆绑定矩阵
4. 批量更新骨骼矩阵

### 质量改进
1. 使用双四元数蒙皮减少变形失真
2. 实现corrective blendshapes修正关节变形
3. 添加肌肉模拟系统
4. 使用physics-based secondary animation