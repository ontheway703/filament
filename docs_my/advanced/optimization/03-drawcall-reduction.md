# Draw Call 优化

## 📖 概述

Draw Call 是 CPU 向 GPU 提交渲染命令的开销。过多的 Draw Call 会导致 CPU 成为瓶颈，即使 GPU 有充足的性能。本文档介绍减少 Draw Call 的各种技术。

**本文涵盖**:
- 静态批处理
- 动态批处理
- GPU Instancing
- Indirect Drawing
- Multi-Draw Indirect

**优化目标**:
- 减少 Draw Call 数量 50-90%
- 提升 CPU 性能 2-5倍
- 支持更多可见物体
- 降低 CPU 占用率

---

## 1. 静态批处理

### 1.1 网格合并器

```cpp
// StaticBatcher.h
#pragma once

#include <filament/Engine.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <vector>

using namespace filament;

/**
 * 静态批处理器
 *
 * 合并静态网格以减少 Draw Call
 */
class StaticBatcher {
public:
    struct MeshData {
        std::vector<float> positions;
        std::vector<float> normals;
        std::vector<float> uvs;
        std::vector<uint32_t> indices;
        math::mat4f transform;
        MaterialInstance* material;
    };

    struct BatchedMesh {
        VertexBuffer* vertexBuffer;
        IndexBuffer* indexBuffer;
        MaterialInstance* material;
        size_t triangleCount;
    };

public:
    explicit StaticBatcher(Engine* engine);
    ~StaticBatcher();

    /**
     * 添加网格到批处理
     */
    void addMesh(const MeshData& mesh);

    /**
     * 执行批处理（合并所有网格）
     */
    std::vector<BatchedMesh> batch();

    /**
     * 清空批处理缓冲
     */
    void clear();

private:
    Engine* mEngine;
    std::vector<MeshData> mMeshes;

    void mergeMeshes(
        const std::vector<MeshData>& meshes,
        std::vector<float>& outPositions,
        std::vector<float>& outNormals,
        std::vector<float>& outUVs,
        std::vector<uint32_t>& outIndices
    );

    BatchedMesh createBatchedMesh(
        const std::vector<float>& positions,
        const std::vector<float>& normals,
        const std::vector<float>& uvs,
        const std::vector<uint32_t>& indices,
        MaterialInstance* material
    );
};
```

```cpp
// StaticBatcher.cpp
#include "StaticBatcher.h"

StaticBatcher::StaticBatcher(Engine* engine)
    : mEngine(engine) {
}

StaticBatcher::~StaticBatcher() {
}

void StaticBatcher::addMesh(const MeshData& mesh) {
    mMeshes.push_back(mesh);
}

std::vector<StaticBatcher::BatchedMesh> StaticBatcher::batch() {
    std::vector<BatchedMesh> result;

    // 按材质分组
    std::unordered_map<MaterialInstance*, std::vector<MeshData>> groupedByMaterial;
    
    for (const auto& mesh : mMeshes) {
        groupedByMaterial[mesh.material].push_back(mesh);
    }

    // 为每个材质组创建批处理网格
    for (const auto& [material, meshes] : groupedByMaterial) {
        std::vector<float> positions, normals, uvs;
        std::vector<uint32_t> indices;

        mergeMeshes(meshes, positions, normals, uvs, indices);

        BatchedMesh batched = createBatchedMesh(
            positions, normals, uvs, indices, material
        );

        result.push_back(batched);
    }

    return result;
}

void StaticBatcher::mergeMeshes(
    const std::vector<MeshData>& meshes,
    std::vector<float>& outPositions,
    std::vector<float>& outNormals,
    std::vector<float>& outUVs,
    std::vector<uint32_t>& outIndices) {

    uint32_t vertexOffset = 0;

    for (const auto& mesh : meshes) {
        // 变换顶点
        for (size_t i = 0; i < mesh.positions.size(); i += 3) {
            math::float3 pos(mesh.positions[i], mesh.positions[i+1], mesh.positions[i+2]);
            math::float3 transformedPos = (mesh.transform * math::float4(pos, 1.0f)).xyz;

            outPositions.push_back(transformedPos.x);
            outPositions.push_back(transformedPos.y);
            outPositions.push_back(transformedPos.z);
        }

        // 变换法线
        math::mat3f normalMatrix = math::mat3f(mesh.transform);
        for (size_t i = 0; i < mesh.normals.size(); i += 3) {
            math::float3 normal(mesh.normals[i], mesh.normals[i+1], mesh.normals[i+2]);
            math::float3 transformedNormal = normalMatrix * normal;

            outNormals.push_back(transformedNormal.x);
            outNormals.push_back(transformedNormal.y);
            outNormals.push_back(transformedNormal.z);
        }

        // UV 不需要变换
        outUVs.insert(outUVs.end(), mesh.uvs.begin(), mesh.uvs.end());

        // 索引需要偏移
        for (uint32_t index : mesh.indices) {
            outIndices.push_back(index + vertexOffset);
        }

        vertexOffset += static_cast<uint32_t>(mesh.positions.size() / 3);
    }
}

StaticBatcher::BatchedMesh StaticBatcher::createBatchedMesh(
    const std::vector<float>& positions,
    const std::vector<float>& normals,
    const std::vector<float>& uvs,
    const std::vector<uint32_t>& indices,
    MaterialInstance* material) {

    BatchedMesh result;
    result.material = material;
    result.triangleCount = indices.size() / 3;

    size_t vertexCount = positions.size() / 3;

    // 创建顶点缓冲区
    result.vertexBuffer = VertexBuffer::Builder()
        .vertexCount(vertexCount)
        .bufferCount(3)
        .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3)
        .attribute(VertexAttribute::TANGENTS, 1, VertexBuffer::AttributeType::FLOAT3)
        .attribute(VertexAttribute::UV0, 2, VertexBuffer::AttributeType::FLOAT2)
        .build(*mEngine);

    result.vertexBuffer->setBufferAt(*mEngine, 0,
        VertexBuffer::BufferDescriptor(positions.data(), positions.size() * sizeof(float)));
    result.vertexBuffer->setBufferAt(*mEngine, 1,
        VertexBuffer::BufferDescriptor(normals.data(), normals.size() * sizeof(float)));
    result.vertexBuffer->setBufferAt(*mEngine, 2,
        VertexBuffer::BufferDescriptor(uvs.data(), uvs.size() * sizeof(float)));

    // 创建索引缓冲区
    result.indexBuffer = IndexBuffer::Builder()
        .indexCount(indices.size())
        .bufferType(IndexBuffer::IndexType::UINT)
        .build(*mEngine);

    result.indexBuffer->setBuffer(*mEngine,
        IndexBuffer::BufferDescriptor(indices.data(), indices.size() * sizeof(uint32_t)));

    return result;
}

void StaticBatcher::clear() {
    mMeshes.clear();
}
```

---

## 2. GPU Instancing

### 2.1 实例化管理器

```cpp
// InstanceManager.h
#pragma once

#include <filament/Engine.h>
#include <filament/RenderableManager.h>
#include <vector>

using namespace filament;

/**
 * 实例化管理器
 *
 * 使用 GPU Instancing 渲染大量相同物体
 */
class InstanceManager {
public:
    struct InstanceData {
        math::mat4f transform;
        math::float4 color;
    };

public:
    explicit InstanceManager(Engine* engine);
    ~InstanceManager();

    /**
     * 创建实例化组
     */
    utils::Entity createInstancedGroup(
        VertexBuffer* vertexBuffer,
        IndexBuffer* indexBuffer,
        MaterialInstance* material,
        size_t maxInstances
    );

    /**
     * 更新实例数据
     */
    void updateInstances(
        utils::Entity entity,
        const std::vector<InstanceData>& instances
    );

    /**
     * 设置可见实例数
     */
    void setInstanceCount(utils::Entity entity, size_t count);

private:
    Engine* mEngine;
    std::unordered_map<utils::Entity, VertexBuffer*> mInstanceBuffers;
};
```

```cpp
// InstanceManager.cpp
#include "InstanceManager.h"

InstanceManager::InstanceManager(Engine* engine)
    : mEngine(engine) {
}

InstanceManager::~InstanceManager() {
    for (auto& [entity, buffer] : mInstanceBuffers) {
        mEngine->destroy(buffer);
    }
}

utils::Entity InstanceManager::createInstancedGroup(
    VertexBuffer* vertexBuffer,
    IndexBuffer* indexBuffer,
    MaterialInstance* material,
    size_t maxInstances) {

    // 创建实例化缓冲区
    VertexBuffer* instanceBuffer = VertexBuffer::Builder()
        .vertexCount(maxInstances)
        .bufferCount(1)
        // 每个实例的变换矩阵（4个 float4）
        .attribute(VertexAttribute::CUSTOM0, 0, VertexBuffer::AttributeType::FLOAT4, 0, 0)
        .attribute(VertexAttribute::CUSTOM1, 0, VertexBuffer::AttributeType::FLOAT4, 0, 16)
        .attribute(VertexAttribute::CUSTOM2, 0, VertexBuffer::AttributeType::FLOAT4, 0, 32)
        .attribute(VertexAttribute::CUSTOM3, 0, VertexBuffer::AttributeType::FLOAT4, 0, 48)
        // 颜色
        .attribute(VertexAttribute::COLOR, 0, VertexBuffer::AttributeType::FLOAT4, 0, 64)
        .build(*mEngine);

    // 创建 Entity
    utils::Entity entity = utils::EntityManager::get().create();

    // 创建 Renderable
    RenderableManager::Builder(1)
        .boundingBox({{-1, -1, -1}, {1, 1, 1}})
        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,
                 vertexBuffer, indexBuffer, 0, indexBuffer->getIndexCount())
        .material(0, material)
        .instances(maxInstances, instanceBuffer)
        .culling(false)
        .build(*mEngine, entity);

    mInstanceBuffers[entity] = instanceBuffer;

    return entity;
}

void InstanceManager::updateInstances(
    utils::Entity entity,
    const std::vector<InstanceData>& instances) {

    auto it = mInstanceBuffers.find(entity);
    if (it == mInstanceBuffers.end()) return;

    // 准备实例数据
    std::vector<float> instanceData;
    instanceData.reserve(instances.size() * 20); // 16 (mat4) + 4 (color)

    for (const auto& inst : instances) {
        // 变换矩阵（列主序）
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                instanceData.push_back(inst.transform[col][row]);
            }
        }

        // 颜色
        instanceData.push_back(inst.color.r);
        instanceData.push_back(inst.color.g);
        instanceData.push_back(inst.color.b);
        instanceData.push_back(inst.color.a);
    }

    // 上传到 GPU
    it->second->setBufferAt(*mEngine, 0,
        VertexBuffer::BufferDescriptor(
            instanceData.data(),
            instanceData.size() * sizeof(float)
        )
    );

    // 更新实例数
    setInstanceCount(entity, instances.size());
}

void InstanceManager::setInstanceCount(utils::Entity entity, size_t count) {
    auto& rcm = mEngine->getRenderableManager();
    auto instance = rcm.getInstance(entity);
    
    if (instance) {
        rcm.setInstanceCount(instance, count);
    }
}
```

---

## 3. Indirect Drawing

### 3.1 Multi-Draw Indirect

```cpp
// IndirectDrawManager.h
#pragma once

#include <filament/Engine.h>
#include <vector>

using namespace filament;

/**
 * Indirect Draw 管理器
 *
 * 使用 Multi-Draw Indirect 减少 CPU 开销
 */
class IndirectDrawManager {
public:
    struct DrawCommand {
        uint32_t indexCount;
        uint32_t instanceCount;
        uint32_t firstIndex;
        uint32_t baseVertex;
        uint32_t baseInstance;
    };

public:
    explicit IndirectDrawManager(Engine* engine);
    ~IndirectDrawManager();

    /**
     * 创建 Indirect Draw Buffer
     */
    void createDrawBuffer(size_t maxCommands);

    /**
     * 更新 Draw Commands
     */
    void updateCommands(const std::vector<DrawCommand>& commands);

    /**
     * 执行 Indirect Draw
     */
    void draw();

private:
    Engine* mEngine;
    void* mDrawBuffer;  // GPU buffer handle
    size_t mMaxCommands;
};
```

---

## 4. 动态批处理

### 4.1 动态合并小物体

```cpp
// DynamicBatcher.h
#pragma once

#include <filament/Engine.h>
#include <vector>
#include <queue>

using namespace filament;

/**
 * 动态批处理器
 *
 * 运行时合并小物体
 */
class DynamicBatcher {
public:
    struct Batch {
        VertexBuffer* vertexBuffer;
        IndexBuffer* indexBuffer;
        MaterialInstance* material;
        size_t usedVertices;
        size_t usedIndices;
    };

public:
    explicit DynamicBatcher(Engine* engine, size_t maxVerticesPerBatch = 65536);
    ~DynamicBatcher();

    /**
     * 添加可渲染物体
     */
    void submit(
        const float* vertices,
        size_t vertexCount,
        const uint32_t* indices,
        size_t indexCount,
        MaterialInstance* material,
        const math::mat4f& transform
    );

    /**
     * 执行批处理并渲染
     */
    void flush();

private:
    Engine* mEngine;
    size_t mMaxVerticesPerBatch;
    std::unordered_map<MaterialInstance*, std::vector<Batch>> mBatches;

    Batch* findOrCreateBatch(MaterialInstance* material, size_t vertexCount, size_t indexCount);
};
```

---

## 5. CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.19)
project(FilamentDrawCallOptimization)

set(CMAKE_CXX_STANDARD 17)

set(SOURCES
    src/StaticBatcher.cpp
    src/InstanceManager.cpp
    src/IndirectDrawManager.cpp
    src/DynamicBatcher.cpp
)

add_library(drawcall_optimization STATIC ${SOURCES})

target_link_libraries(drawcall_optimization PUBLIC
    filament
    utils
    math
)

target_include_directories(drawcall_optimization PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
```

---

## 6. 常见问题

### Q1: 什么时候使用静态批处理 vs Instancing?

**A**: 选择指南:

```
静态批处理:
- 物体永不移动
- 使用相同材质
- 少量大网格
- 示例: 建筑物、地形块

GPU Instancing:
- 大量相同物体
- 可以移动/旋转
- 每个实例可以有不同参数
- 示例: 树木、草、粒子

Indirect Drawing:
- 海量物体（10K+）
- GPU 驱动的渲染
- Frustum/Occlusion Culling 在 GPU
- 示例: 开放世界
```

### Q2: Instancing 最多支持多少实例?

**A**: 取决于平台:

```cpp
// Desktop: 通常 100K+
// Mobile: 10K - 50K

// 检查限制:
GLint maxInstances;
glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxInstances);

// 建议分批:
const size_t BATCH_SIZE = 10000;
for (size_t i = 0; i < totalInstances; i += BATCH_SIZE) {
    size_t count = std::min(BATCH_SIZE, totalInstances - i);
    drawInstanced(i, count);
}
```

---

## 7. 相关文档

- [optimization/02-mesh-optimization.md](./02-mesh-optimization.md)
- [optimization/04-memory-management.md](./04-memory-management.md)

---

## 8. 总结

Draw Call 优化关键技术:

1. **静态批处理**: 合并静态网格
2. **GPU Instancing**: 渲染大量相同物体
3. **Indirect Drawing**: GPU 驱动的渲染
4. **动态批处理**: 运行时合并小物体
5. **材质合并**: 使用 Texture Atlas

通过 Draw Call 优化可以实现:
- **Draw Call 减少 50-90%**
- **CPU 性能提升 2-5倍**
- **支持更多可见物体**
