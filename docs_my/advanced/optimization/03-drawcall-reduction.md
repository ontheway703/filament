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

## 0. Draw Call 为什么昂贵？

### 0.1 CPU-GPU 通信开销

**Draw Call 流程**：

```
CPU:                                    GPU:
1. 准备渲染状态                         1. 等待命令
   - 绑定 Shader
   - 绑定 Texture
   - 绑定 Uniform
   - 绑定 Vertex Buffer

2. 验证状态                             2. 接收命令
   - 检查状态一致性
   - Driver 层验证

3. 提交 Draw Call → Command Buffer →   3. 开始渲染
   (跨越 PCIe 总线)                        - Vertex Shader
                                           - Rasterization
                                           - Fragment Shader

4. 准备下一个 Draw Call                 4. 完成渲染
```

**开销分析**：

| 操作 | 耗时（估算） | 累积影响 |
|------|------------|---------|
| 状态切换（Shader、Texture） | 10-50 μs | 高频操作 |
| Driver 验证和转换 | 5-20 μs | 每次必须 |
| Command Buffer 提交 | 1-5 μs | 批量降低 |
| PCIe 传输 | < 1 μs | 带宽受限 |
| GPU 等待 CPU | 可变 | Pipeline Stall |

**示例计算**：

```
1000 Draw Calls × 20 μs = 20 ms (仅 CPU 侧)
→ 50 FPS (GPU 空闲时间长)

100 Draw Calls × 20 μs = 2 ms
→ 适配 16.7 ms 帧预算 (60 FPS)
```

### 0.2 渲染管线效率

**问题**：每次 Draw Call 都会清空 GPU 缓存和流水线。

```
Draw Call 1: [Setup | Vertex | Fragment] → Pipeline Full
Draw Call 2: [Setup | Vertex | Fragment] → Pipeline Full
             ↑ GPU Idle (等待 CPU 提交下一个)

Batched:     [Setup | Vertex | Fragment | Vertex | Fragment | ...]
                                         ↑ 持续运行，无 Idle
```

### 0.3 移动端更敏感

**移动 GPU 特性**：
- TBDR（Tile-Based Deferred Rendering）：每个 Draw Call 会触发 Tile 切换
- 功耗敏感：频繁的状态切换增加功耗
- 带宽受限：PCIe 带宽远低于桌面

**建议**：
- Desktop: < 2000 Draw Calls
- Mobile: < 500 Draw Calls
- VR: < 200 Draw Calls (90 FPS × 2 eyes)

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

## 5. Texture Atlas（纹理图集）

### 5.1 为什么需要 Texture Atlas？

**问题**：不同纹理需要不同的 Draw Call。

```
Object 1: Material A (Texture 1) → Draw Call 1
Object 2: Material B (Texture 2) → Draw Call 2
Object 3: Material A (Texture 1) → Draw Call 3 (状态切换)

Total: 3 Draw Calls
```

**方案**：合并纹理到一个大纹理（Atlas）。

```
Object 1-3: Material_Atlas (Texture Atlas) → Draw Call 1

Total: 1 Draw Call
```

### 5.2 Texture Atlas 生成

```cpp
// TextureAtlasBuilder.h
#include <vector>
#include <filament/Texture.h>

class TextureAtlasBuilder {
public:
    struct AtlasEntry {
        std::string name;
        uint32_t x, y, width, height;  // Atlas 中的位置
        float u0, v0, u1, v1;  // UV 坐标范围
    };

    struct Atlas {
        Texture* texture;
        std::vector<AtlasEntry> entries;
        uint32_t width, height;
    };

public:
    void addTexture(const std::string& name, const uint8_t* data,
                    uint32_t width, uint32_t height);

    Atlas build(Engine& engine, uint32_t maxSize = 2048);

private:
    struct TextureData {
        std::string name;
        std::vector<uint8_t> data;
        uint32_t width, height;
    };

    std::vector<TextureData> mTextures;

    // Simple bin packing (可替换为更高效算法)
    bool packTextures(uint32_t atlasWidth, uint32_t atlasHeight,
                      std::vector<AtlasEntry>& outEntries);
};
```

**实现**：

```cpp
// TextureAtlasBuilder.cpp
void TextureAtlasBuilder::addTexture(const std::string& name, const uint8_t* data,
                                     uint32_t width, uint32_t height) {
    TextureData tex;
    tex.name = name;
    tex.width = width;
    tex.height = height;
    tex.data.assign(data, data + width * height * 4);  // RGBA

    mTextures.push_back(std::move(tex));
}

TextureAtlasBuilder::Atlas TextureAtlasBuilder::build(Engine& engine, uint32_t maxSize) {
    Atlas atlas;
    atlas.width = maxSize;
    atlas.height = maxSize;

    // 打包纹理
    if (!packTextures(maxSize, maxSize, atlas.entries)) {
        // 打包失败，尝试更大尺寸
        atlas.width = maxSize * 2;
        atlas.height = maxSize * 2;
        packTextures(atlas.width, atlas.height, atlas.entries);
    }

    // 创建 Atlas 纹理
    std::vector<uint8_t> atlasData(atlas.width * atlas.height * 4, 0);

    // 复制所有纹理到 Atlas
    for (size_t i = 0; i < mTextures.size(); ++i) {
        const auto& tex = mTextures[i];
        const auto& entry = atlas.entries[i];

        for (uint32_t y = 0; y < tex.height; ++y) {
            for (uint32_t x = 0; x < tex.width; ++x) {
                uint32_t srcIdx = (y * tex.width + x) * 4;
                uint32_t dstIdx = ((entry.y + y) * atlas.width + (entry.x + x)) * 4;

                atlasData[dstIdx + 0] = tex.data[srcIdx + 0];  // R
                atlasData[dstIdx + 1] = tex.data[srcIdx + 1];  // G
                atlasData[dstIdx + 2] = tex.data[srcIdx + 2];  // B
                atlasData[dstIdx + 3] = tex.data[srcIdx + 3];  // A
            }
        }
    }

    // 创建 Filament Texture
    atlas.texture = Texture::Builder()
        .width(atlas.width)
        .height(atlas.height)
        .levels(1)
        .format(Texture::InternalFormat::RGBA8)
        .build(engine);

    atlas.texture->setImage(engine, 0,
        Texture::PixelBufferDescriptor(atlasData.data(), atlasData.size(),
            Texture::Format::RGBA, Texture::Type::UBYTE)
    );

    return atlas;
}

bool TextureAtlasBuilder::packTextures(uint32_t atlasWidth, uint32_t atlasHeight,
                                       std::vector<AtlasEntry>& outEntries) {
    // 简单的水平打包（可替换为 Guillotine、MaxRects 等算法）
    outEntries.clear();

    uint32_t currentX = 0, currentY = 0, rowHeight = 0;

    for (const auto& tex : mTextures) {
        if (currentX + tex.width > atlasWidth) {
            // 换行
            currentX = 0;
            currentY += rowHeight;
            rowHeight = 0;
        }

        if (currentY + tex.height > atlasHeight) {
            // 超出范围
            return false;
        }

        AtlasEntry entry;
        entry.name = tex.name;
        entry.x = currentX;
        entry.y = currentY;
        entry.width = tex.width;
        entry.height = tex.height;

        // 计算 UV 坐标
        entry.u0 = (float)currentX / atlasWidth;
        entry.v0 = (float)currentY / atlasHeight;
        entry.u1 = (float)(currentX + tex.width) / atlasWidth;
        entry.v1 = (float)(currentY + tex.height) / atlasHeight;

        outEntries.push_back(entry);

        currentX += tex.width;
        rowHeight = std::max(rowHeight, tex.height);
    }

    return true;
}
```

### 5.3 使用 Texture Atlas

```cpp
// 构建 Atlas
TextureAtlasBuilder builder;
builder.addTexture("tree", treeTexData, 256, 256);
builder.addTexture("rock", rockTexData, 256, 256);
builder.addTexture("grass", grassTexData, 256, 256);

auto atlas = builder.build(*engine);

// 查找 UV 坐标
auto it = std::find_if(atlas.entries.begin(), atlas.entries.end(),
    [](const auto& e) { return e.name == "tree"; });

if (it != atlas.entries.end()) {
    float u0 = it->u0, v0 = it->v0;
    float u1 = it->u1, v1 = it->v1;

    // 调整网格 UV
    for (auto& uv : meshUVs) {
        uv.x = u0 + uv.x * (u1 - u0);
        uv.y = v0 + uv.y * (v1 - v0);
    }
}

// 所有物体使用同一个 Atlas Texture
materialInstance->setParameter("baseColorMap", atlas.texture, sampler);
```

**性能对比**：

```
Before (Individual Textures):
  100 objects × 5 different textures = 500 Draw Calls

After (Texture Atlas):
  100 objects × 1 atlas texture = 1 Draw Call (with instancing)

Improvement: 99.8% reduction
```

---

## 6. GPU 视锥剔除（Frustum Culling）

### 6.1 CPU-Side Frustum Culling

```cpp
#include <filament/View.h>
#include <math/mat4.h>

class FrustumCuller {
public:
    struct Plane {
        math::float3 normal;
        float distance;
    };

    struct Frustum {
        Plane planes[6];  // Left, Right, Top, Bottom, Near, Far
    };

public:
    static Frustum extractFrustum(const math::mat4f& viewProjection) {
        Frustum frustum;

        // Left plane
        frustum.planes[0].normal = math::float3(
            viewProjection[0][3] + viewProjection[0][0],
            viewProjection[1][3] + viewProjection[1][0],
            viewProjection[2][3] + viewProjection[2][0]
        );
        frustum.planes[0].distance = viewProjection[3][3] + viewProjection[3][0];

        // Right plane
        frustum.planes[1].normal = math::float3(
            viewProjection[0][3] - viewProjection[0][0],
            viewProjection[1][3] - viewProjection[1][0],
            viewProjection[2][3] - viewProjection[2][0]
        );
        frustum.planes[1].distance = viewProjection[3][3] - viewProjection[3][0];

        // ... (Top, Bottom, Near, Far 同理)

        // 归一化平面
        for (int i = 0; i < 6; ++i) {
            float len = length(frustum.planes[i].normal);
            frustum.planes[i].normal /= len;
            frustum.planes[i].distance /= len;
        }

        return frustum;
    }

    static bool isBoxVisible(const Frustum& frustum, const Box& box) {
        for (int i = 0; i < 6; ++i) {
            const Plane& plane = frustum.planes[i];

            // 获取 Box 的正顶点（Positive Vertex）
            math::float3 pVertex = box.min;
            if (plane.normal.x >= 0) pVertex.x = box.max.x;
            if (plane.normal.y >= 0) pVertex.y = box.max.y;
            if (plane.normal.z >= 0) pVertex.z = box.max.z;

            // 检查正顶点是否在平面外侧
            if (dot(plane.normal, pVertex) + plane.distance < 0) {
                return false;  // 完全在平面外侧
            }
        }

        return true;  // 在视锥内或相交
    }
};

// 使用
void renderScene(View* view, const std::vector<Object>& objects) {
    auto viewProjection = view->getViewProjectionMatrix();
    auto frustum = FrustumCuller::extractFrustum(viewProjection);

    for (const auto& obj : objects) {
        if (FrustumCuller::isBoxVisible(frustum, obj.boundingBox)) {
            // 在视锥内，渲染
            renderer->render(obj);
        }
        // 否则跳过
    }
}
```

### 6.2 GPU-Side Frustum Culling (Compute Shader)

```glsl
// frustum_cull.comp
#version 450

layout(local_size_x = 256) in;

struct DrawCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

struct ObjectData {
    mat4 modelMatrix;
    vec4 boundingBoxMin;
    vec4 boundingBoxMax;
};

layout(std140, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
};

layout(std140, binding = 1) writeonly buffer DrawCommandBuffer {
    DrawCommand commands[];
};

layout(std140, binding = 2) buffer DrawCountBuffer {
    uint drawCount;
};

uniform mat4 viewProjection;
uniform vec4 frustumPlanes[6];

bool isBoxVisible(vec3 boxMin, vec3 boxMax) {
    for (int i = 0; i < 6; ++i) {
        vec3 normal = frustumPlanes[i].xyz;
        float dist = frustumPlanes[i].w;

        // Positive vertex
        vec3 pVertex = boxMin;
        if (normal.x >= 0.0) pVertex.x = boxMax.x;
        if (normal.y >= 0.0) pVertex.y = boxMax.y;
        if (normal.z >= 0.0) pVertex.z = boxMax.z;

        if (dot(normal, pVertex) + dist < 0.0) {
            return false;
        }
    }

    return true;
}

void main() {
    uint objectID = gl_GlobalInvocationID.x;
    if (objectID >= objects.length()) return;

    ObjectData obj = objects[objectID];

    // 变换 Bounding Box 到世界空间
    vec3 worldMin = (obj.modelMatrix * vec4(obj.boundingBoxMin.xyz, 1.0)).xyz;
    vec3 worldMax = (obj.modelMatrix * vec4(obj.boundingBoxMax.xyz, 1.0)).xyz;

    // 视锥剔除
    if (isBoxVisible(worldMin, worldMax)) {
        // 可见，添加 Draw Command
        uint drawIndex = atomicAdd(drawCount, 1);

        commands[drawIndex].indexCount = 36;  // 假设是 Cube
        commands[drawIndex].instanceCount = 1;
        commands[drawIndex].firstIndex = 0;
        commands[drawIndex].vertexOffset = int(objectID) * 8;  // 8 vertices per cube
        commands[drawIndex].firstInstance = objectID;
    }
}
```

**性能对比**：

```
CPU Frustum Culling:
  10,000 objects × 50 ns (AABB test) = 0.5 ms
  Visible: 2,000 objects → 2,000 Draw Calls

GPU Frustum Culling:
  10,000 objects on GPU: 0.1 ms (Compute Shader)
  + 1 Multi-Draw Indirect: 0.01 ms
  Total: 0.11 ms (78% faster)
```

---

## 7. 性能分析与测试

### 7.1 Draw Call 统计

```cpp
class DrawCallProfiler {
public:
    void beginFrame() {
        mDrawCallCount = 0;
        mTriangleCount = 0;
        mFrameStart = std::chrono::high_resolution_clock::now();
    }

    void recordDrawCall(uint32_t triangles) {
        mDrawCallCount++;
        mTriangleCount += triangles;
    }

    void endFrame() {
        auto now = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(now - mFrameStart).count();

        printf("Frame Stats:\n");
        printf("  Draw Calls: %u\n", mDrawCallCount);
        printf("  Triangles: %u\n", mTriangleCount);
        printf("  Tris/DC: %.1f\n", (float)mTriangleCount / mDrawCallCount);
        printf("  Frame Time: %.2f ms\n", ms);
        printf("  FPS: %.1f\n", 1000.0 / ms);
    }

private:
    uint32_t mDrawCallCount = 0;
    uint32_t mTriangleCount = 0;
    std::chrono::time_point<std::chrono::high_resolution_clock> mFrameStart;
};

// 使用
DrawCallProfiler profiler;

void onFrame() {
    profiler.beginFrame();

    for (const auto& obj : objects) {
        renderer->render(obj);
        profiler.recordDrawCall(obj.triangleCount);
    }

    profiler.endFrame();
}

// 输出示例：
// Frame Stats:
//   Draw Calls: 1234
//   Triangles: 567890
//   Tris/DC: 460.2
//   Frame Time: 16.7 ms
//   FPS: 59.9
```

### 7.2 批处理效果对比

```cpp
class BatchingBenchmark {
public:
    struct Result {
        std::string name;
        uint32_t drawCalls;
        double cpuTimeMs;
        double gpuTimeMs;
        double fps;
    };

    void benchmark(const std::string& name, std::function<void()> renderFunc) {
        // Warmup
        for (int i = 0; i < 10; ++i) {
            renderFunc();
        }

        // Measure
        auto start = std::chrono::high_resolution_clock::now();

        uint32_t frameCount = 100;
        for (uint32_t i = 0; i < frameCount; ++i) {
            renderFunc();
        }

        auto end = std::chrono::high_resolution_clock::now();
        double totalMs = std::chrono::duration<double, std::milli>(end - start).count();

        Result result;
        result.name = name;
        result.cpuTimeMs = totalMs / frameCount;
        result.fps = 1000.0 / result.cpuTimeMs;

        mResults.push_back(result);
    }

    void printResults() {
        printf("Batching Benchmark Results:\n");
        printf("%-30s %15s %15s %15s\n", "Method", "Draw Calls", "CPU Time (ms)", "FPS");
        printf("%-30s %15s %15s %15s\n", "------", "----------", "-------------", "---");

        for (const auto& r : mResults) {
            printf("%-30s %15u %15.2f %15.1f\n",
                   r.name.c_str(), r.drawCalls, r.cpuTimeMs, r.fps);
        }

        // 对比
        if (mResults.size() >= 2) {
            double baseline = mResults[0].cpuTimeMs;
            printf("\nImprovement vs Baseline:\n");
            for (size_t i = 1; i < mResults.size(); ++i) {
                double improvement = (baseline - mResults[i].cpuTimeMs) / baseline * 100.0;
                printf("  %s: %.1f%% faster\n", mResults[i].name.c_str(), improvement);
            }
        }
    }

private:
    std::vector<Result> mResults;
};

// 使用
BatchingBenchmark bench;

bench.benchmark("No Batching", []() {
    for (auto& obj : objects) {
        renderer->render(obj);  // Individual draw calls
    }
});

bench.benchmark("Static Batching", []() {
    renderer->render(staticBatchedMesh);  // 1 draw call
});

bench.benchmark("GPU Instancing", []() {
    renderer->renderInstanced(mesh, instanceCount);  // 1 draw call
});

bench.printResults();

// 输出示例：
// Batching Benchmark Results:
// Method                         Draw Calls    CPU Time (ms)            FPS
// ------                         ----------    -------------            ---
// No Batching                          1000            16.70           59.9
// Static Batching                         1             2.30          434.8
// GPU Instancing                          1             1.80          555.6
//
// Improvement vs Baseline:
//   Static Batching: 86.2% faster
//   GPU Instancing: 89.2% faster
```

---

## 8. CMakeLists.txt

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
