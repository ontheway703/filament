# Phase 1 - Normal/Tangent 修复

**优先级**: P0-2（高优先级）
**预计工作量**: 1-2 个工作日
**依赖**: 无
**目标**: 修复法线贴图显示，使 gltfio_ext 支持完整的 TBN（Tangent-Bitangent-Normal）计算

---

## 1. 问题分析

### 1.1 当前状态

**问题表现**:
- 法线贴图（Normal Map）无法正确显示
- 模型表面细节丢失（应该有凹凸感，但实际平滑）
- 即使绑定了 normalMap 纹理，shader 也无法正确使用

**根本原因**:

当前实现只存储了 normal 向量，缺少 tangent 和 bitangent：

```cpp
// 当前实现（错误）
VertexBuffer::Builder builder;
builder.vertexCount(vertexCount)
    .bufferCount(1)
    .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3)
    .attribute(VertexAttribute::NORMAL, 0, VertexBuffer::AttributeType::FLOAT4)  // ❌ 只有 normal
    // .attribute(VertexAttribute::TANGENTS, ...)  // ❌ 缺失！
    .build();
```

**为什么需要 TBN**?

法线贴图存储的是**切线空间**（Tangent Space）的法线向量，需要 TBN 矩阵将其转换到**世界空间**：

```
世界空间法线 = TBN 矩阵 × 切线空间法线（从纹理读取）
```

TBN 矩阵由三个正交向量组成：
- **T** (Tangent): 切线方向（沿 U 方向）
- **B** (Bitangent): 副切线方向（沿 V 方向）
- **N** (Normal): 法线方向（垂直于表面）

### 1.2 与 gltfio 的差异

| 功能 | gltfio | gltfio_ext（当前） |
|------|--------|-------------------|
| NORMAL 属性 | ✅ 读取并存储 | ✅ 读取并存储 |
| TANGENT 属性 | ✅ 读取或计算 | ❌ 完全忽略 |
| TBN quaternion | ✅ 计算并打包到 TANGENTS | ❌ 缺失 |
| 法线贴图支持 | ✅ 正确显示 | ❌ 无法显示 |

---

## 2. 技术背景

### 2.1 glTF 规范

**TANGENT 属性**:
- 类型: `VEC4`
- 含义: `[tx, ty, tz, w]`
  - `(tx, ty, tz)`: 切线方向（单位向量）
  - `w`: 手性（handedness），值为 `+1` 或 `-1`

**Bitangent 计算**:
```
bitangent = cross(normal, tangent.xyz) * tangent.w
```

### 2.2 Filament 的 TANGENTS 属性

**存储格式**: Quaternion（四元数）

Filament 不直接存储 T/B/N 三个向量（占用 36 字节），而是将 TBN 矩阵转换为 quaternion（占用 16 字节）：

```cpp
// VertexAttribute::TANGENTS
// 类型: FLOAT4 或 SHORT4 (normalized)
// 含义: quaternion 表示的 TBN 矩阵
```

**优势**:
- 节省内存（16 字节 vs 36 字节）
- 插值平滑（quaternion 的 slerp 优于矩阵插值）

### 2.3 TBN → Quaternion 转换

**步骤**:
1. 构建 TBN 矩阵（3x3）
2. 将矩阵转换为 quaternion（使用 filament::math 库）
3. 打包为 `quatf` 类型

**数学原理**:
```
TBN 矩阵 = [T | B | N]  (3x3 矩阵，列向量)

quaternion q = matrixToQuaternion(TBN)
```

---

## 3. 实现方案

### 3.1 实现路径

**场景 1: glTF 提供了 TANGENT 属性**
```
读取 TANGENT → 计算 Bitangent → 构建 TBN 矩阵 → 转换为 quaternion
```

**场景 2: glTF 没有 TANGENT 属性**
```
从 NORMAL + UV 计算 Tangent → 计算 Bitangent → 构建 TBN 矩阵 → 转换为 quaternion
```

### 3.2 参考实现

gltfio 的实现位于 `libs/gltfio/src/AssetLoader.cpp:800-850`，我们需要将其核心逻辑移植到 gltfio_ext。

---

## 4. 详细实现

### 4.1 步骤 1: 读取 glTF TANGENT 属性

**修改文件**: `libs/gltfio_ext/src/MeshAsset.cpp`

**位置**: `loadMesh()` 函数中，解析 primitive 时

```cpp
// 在解析 NORMAL 之后添加
cgltf_attribute* tangentAttr = nullptr;
for (size_t j = 0; j < prim->attributes_count; j++) {
    if (prim->attributes[j].type == cgltf_attribute_type_tangent) {
        tangentAttr = &prim->attributes[j];
        break;
    }
}

bool hasTangent = (tangentAttr != nullptr);
```

### 4.2 步骤 2: 添加 TANGENTS 属性到 VertexBuffer

**修改文件**: `libs/gltfio_ext/src/MeshAsset.cpp`

**位置**: `uploadResources()` 函数中，创建 VertexBuffer 时

```cpp
VertexBuffer::Builder builder;
builder.vertexCount(vertexCount)
    .bufferCount(1)
    .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3)
    .attribute(VertexAttribute::NORMAL, 0, VertexBuffer::AttributeType::FLOAT4)
    .attribute(VertexAttribute::TANGENTS, 0, VertexBuffer::AttributeType::FLOAT4)  // ✅ 新增
    // ... 其他属性 ...
    .build(*mEngine);
```

### 4.3 步骤 3: 实现 TBN 计算

**新建辅助函数**: `libs/gltfio_ext/src/MeshAsset.cpp`

```cpp
#include <filament/math/quat.h>
#include <filament/math/mat3.h>

using namespace filament::math;

namespace {

/**
 * 从 TBN 向量构建 quaternion
 * @param tangent 切线向量（已归一化）
 * @param bitangent 副切线向量（已归一化）
 * @param normal 法线向量（已归一化）
 * @return quaternion 表示的 TBN 矩阵
 */
quatf buildTbnQuaternion(const float3& tangent, const float3& bitangent, const float3& normal) {
    // 1. 构建 TBN 矩阵（列向量）
    mat3f tbn = mat3f{
        tangent,      // 第 1 列
        bitangent,    // 第 2 列
        normal        // 第 3 列
    };

    // 2. 转换为 quaternion
    quatf q = mat3f::packTangentFrame(tbn);

    return q;
}

/**
 * 从 Normal 和 UV 计算 Tangent（MikkTSpace 算法简化版）
 * 注意：这是简化实现，完整实现需要 MikkTSpace 库
 */
float3 computeTangent(const float3& normal, const float2* uvs, size_t vertexIndex) {
    // 简化版：从 normal 构建一个正交的 tangent
    // 完整实现应使用 MikkTSpace 算法

    float3 c1 = cross(normal, float3{0.0f, 0.0f, 1.0f});
    float3 c2 = cross(normal, float3{0.0f, 1.0f, 0.0f});

    float3 tangent = length2(c1) > length2(c2) ? c1 : c2;
    return normalize(tangent);
}

} // anonymous namespace
```

### 4.4 步骤 4: 填充 TANGENTS 数据

**修改文件**: `libs/gltfio_ext/src/MeshAsset.cpp`

**位置**: `uploadResources()` 函数中，准备顶点数据时

```cpp
// 准备 TANGENTS 数据
std::vector<quatf> tangents(vertexCount);

if (hasTangent) {
    // 场景 1: glTF 提供了 TANGENT 属性
    cgltf_accessor* tangentAccessor = tangentAttr->data;

    for (size_t i = 0; i < vertexCount; i++) {
        // 读取 tangent (vec4)
        float4 tangent;
        cgltf_accessor_read_float(tangentAccessor, i, &tangent.x, 4);

        // 读取 normal (vec3)
        float3 normal;
        cgltf_accessor_read_float(normalAccessor, i, &normal.x, 3);

        // 计算 bitangent
        float3 bitangent = cross(normal, tangent.xyz) * tangent.w;

        // 构建 quaternion
        tangents[i] = buildTbnQuaternion(tangent.xyz, bitangent, normal);
    }
} else {
    // 场景 2: 没有 TANGENT，从 normal + UV 计算
    for (size_t i = 0; i < vertexCount; i++) {
        float3 normal;
        cgltf_accessor_read_float(normalAccessor, i, &normal.x, 3);

        // 计算 tangent（简化版）
        float3 tangent = computeTangent(normal, uvData, i);

        // 计算 bitangent
        float3 bitangent = cross(normal, tangent);

        // 构建 quaternion
        tangents[i] = buildTbnQuaternion(tangent, bitangent, normal);
    }
}

// 上传到 VertexBuffer
VertexBuffer::BufferDescriptor tangentBuffer(
    tangents.data(),
    tangents.size() * sizeof(quatf),
    [](void*, size_t, void*) {}
);
vertexBuffer->setBufferAt(*mEngine, 0, std::move(tangentBuffer));
```

### 4.5 步骤 5: 使用 gltfio 的完整实现（推荐）

如果需要更完整的实现（支持 MikkTSpace 算法），可以直接复用 gltfio 的代码：

**复制文件**:
```
libs/gltfio/src/TangentSpaceMesh.h → libs/gltfio_ext/src/
libs/gltfio/src/TangentSpaceMesh.cpp → libs/gltfio_ext/src/
```

**使用方式**:
```cpp
#include "TangentSpaceMesh.h"

// 在 loadMesh() 中
if (!hasTangent && hasNormal && hasUV) {
    // 使用 MikkTSpace 计算 tangent
    TangentSpaceMesh tsm;
    tsm.compute(positions, normals, uvs, indices, vertexCount, indexCount);

    // 获取计算结果
    const float4* tangents = tsm.getTangents();
}
```

---

## 5. 测试计划

### 5.1 单元测试

**新建文件**: `libs/gltfio_ext/tests/test_tangent.cpp`

**测试用例**:

```cpp
TEST(Tangent, BuildTbnQuaternion) {
    // 测试正交 TBN
    float3 tangent = float3{1, 0, 0};
    float3 bitangent = float3{0, 1, 0};
    float3 normal = float3{0, 0, 1};

    quatf q = buildTbnQuaternion(tangent, bitangent, normal);

    // 反向验证
    mat3f tbn = mat3f::unpackTangentFrame(q);
    EXPECT_NEAR(tbn[0].x, 1.0f, 0.01f);  // tangent.x
    EXPECT_NEAR(tbn[1].y, 1.0f, 0.01f);  // bitangent.y
    EXPECT_NEAR(tbn[2].z, 1.0f, 0.01f);  // normal.z
}

TEST(Tangent, LoadWithTangentAttribute) {
    // 加载带 TANGENT 属性的模型
    MeshAsset* mesh = loader->loadMesh(meshWithTangent, size);
    mesh->uploadResources();

    // 验证 VertexBuffer 有 TANGENTS 属性
    // （需要暴露内部状态，或通过渲染测试验证）
}

TEST(Tangent, LoadWithoutTangentAttribute) {
    // 加载没有 TANGENT 属性的模型
    MeshAsset* mesh = loader->loadMesh(meshWithoutTangent, size);
    mesh->uploadResources();

    // 验证 TANGENTS 被自动计算
}
```

### 5.2 集成测试

**测试场景 1: 带法线贴图的模型**
- 文件: Khronos glTF Sample Models/NormalTangentTest
- 验证: 法线贴图正确显示（凹凸细节可见）

**测试场景 2: 没有 TANGENT 属性的模型**
- 文件: 自定义模型（只有 NORMAL + UV）
- 验证: 自动计算的 TANGENT 可以正确显示法线贴图

**测试场景 3: 法线贴图 + 换装系统**
- 文件: skeleton.glb + mesh.glb（mesh 有法线贴图）
- 验证: 换装后法线贴图正确显示

### 5.3 视觉测试

**对比测试**:
1. 使用 glTF Viewer（https://gltf-viewer.donmccurdy.com/）查看模型
2. 使用 gltfio_ext 渲染同一模型
3. 对比法线贴图效果是否一致

**测试模型**:
- BoxTextured（带法线贴图版本）
- NormalTangentTest（Khronos 官方测试模型）

---

## 6. 验收标准

### 6.1 功能验收

- ✅ 支持读取 glTF 的 TANGENT 属性
- ✅ 当没有 TANGENT 时，可以从 NORMAL + UV 计算
- ✅ TBN quaternion 正确打包到 TANGENTS 属性
- ✅ 法线贴图可以正确显示
- ✅ NormalTangentTest 模型渲染正确

### 6.2 质量验收

- ✅ 单元测试通过
- ✅ 集成测试通过
- ✅ 与 glTF Viewer 渲染效果一致
- ✅ 无性能回退（TANGENTS 计算耗时 < 5ms per 1000 vertices）

### 6.3 兼容性验收

- ✅ 不影响现有功能（没有法线贴图的模型仍正常渲染）
- ✅ 支持有/无 TANGENT 属性的模型

---

## 7. 实现路线图

### 7.1 简化实现（推荐先做）

**范围**: 只支持 glTF 提供 TANGENT 属性的模型

**优势**:
- 实现简单（1 天）
- 覆盖大部分使用场景（大部分建模软件导出时会生成 TANGENT）

**代码量**: ~100 行

### 7.2 完整实现（可选）

**范围**: 支持没有 TANGENT 属性的模型（MikkTSpace 计算）

**优势**:
- 与 gltfio 功能一致
- 兼容性更好

**代码量**: ~500 行（包括 TangentSpaceMesh 移植）

**建议**: 先做简化实现，测试通过后再考虑完整实现。

---

## 8. 代码示例

### 8.1 完整实现（简化版）

```cpp
// MeshAsset.cpp

void MeshAsset::uploadResources() {
    // ... 现有代码 ...

    // 新增：准备 TANGENTS 数据
    std::vector<quatf> tangents(vertexCount);

    cgltf_accessor* tangentAccessor = findAttribute(prim, cgltf_attribute_type_tangent);
    cgltf_accessor* normalAccessor = findAttribute(prim, cgltf_attribute_type_normal);

    if (tangentAccessor) {
        // 有 TANGENT 属性
        for (size_t i = 0; i < vertexCount; i++) {
            float4 t;
            float3 n;
            cgltf_accessor_read_float(tangentAccessor, i, &t.x, 4);
            cgltf_accessor_read_float(normalAccessor, i, &n.x, 3);

            float3 tangent = normalize(t.xyz);
            float3 bitangent = normalize(cross(n, tangent)) * t.w;
            float3 normal = normalize(n);

            mat3f tbn = mat3f{tangent, bitangent, normal};
            tangents[i] = mat3f::packTangentFrame(tbn);
        }
    } else {
        // 没有 TANGENT，使用简化算法
        for (size_t i = 0; i < vertexCount; i++) {
            float3 n;
            cgltf_accessor_read_float(normalAccessor, i, &n.x, 3);

            float3 normal = normalize(n);
            float3 tangent = normalize(cross(normal, float3{0, 0, 1}));
            if (length2(tangent) < 0.01f) {
                tangent = normalize(cross(normal, float3{0, 1, 0}));
            }
            float3 bitangent = normalize(cross(normal, tangent));

            mat3f tbn = mat3f{tangent, bitangent, normal};
            tangents[i] = mat3f::packTangentFrame(tbn);
        }
    }

    // 上传到 VertexBuffer
    VertexBuffer::BufferDescriptor tangentBuffer(
        tangents.data(),
        tangents.size() * sizeof(quatf),
        [](void*, size_t, void*) {}
    );
    vertexBuffer->setBufferAt(*mEngine, 0, std::move(tangentBuffer));
}
```

---

## 9. 风险和缓解

### 9.1 风险 1: Quaternion 计算错误

**影响**: 法线贴图显示错误（倒置、旋转）
**概率**: 中
**缓解**:
- 参考 gltfio 实现
- 单元测试验证 TBN 矩阵正交性
- 视觉对比测试

### 9.2 风险 2: MikkTSpace 算法移植复杂

**影响**: 没有 TANGENT 属性的模型无法正确显示
**概率**: 高（如果做完整实现）
**缓解**:
- 先做简化实现
- 或直接复用 gltfio 的 TangentSpaceMesh 代码

### 9.3 风险 3: 性能问题

**影响**: TANGENTS 计算耗时长
**概率**: 低
**缓解**:
- Quaternion 计算非常快（每顶点 < 1μs）
- 如需优化，可以使用 SIMD 加速

---

## 10. 参考资料

1. **glTF 规范**: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes
2. **Filament 文档**: https://google.github.io/filament/Filament.md.html#materialsystem/attributes
3. **gltfio 实现**: `libs/gltfio/src/AssetLoader.cpp:800-850`
4. **MikkTSpace 算法**: http://www.mikktspace.com/

---

## 11. 里程碑

| 里程碑 | 完成标准 | 预计时间 |
|--------|---------|---------|
| **M1: 简化实现完成** | 支持有 TANGENT 属性的模型 | 第 1 天 |
| **M2: 测试完成** | 单元测试 + 视觉测试通过 | 第 2 天 |
| **M3: 完整实现（可选）** | 支持 MikkTSpace 计算 | 第 3-4 天 |

---

**下一步**:
1. 先实现简化版（支持 glTF 提供 TANGENT 的情况）
2. 测试通过后再考虑是否需要 MikkTSpace 完整实现
