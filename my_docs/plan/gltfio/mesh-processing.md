# Mesh 处理

## 概述

gltfio 的 Mesh 处理负责将 glTF 的 primitive 转换为 Filament 的 VertexBuffer 和 IndexBuffer。这个过程涉及顶点属性映射、数据格式转换、以及 MeshCache 的管理。

## 核心数据结构

### Primitive (libs/gltfio/src/FFilamentAsset.h:94-102)

```cpp
struct Primitive {
    VertexBuffer* vertices = nullptr;
    IndexBuffer* indices = nullptr;
    Aabb aabb;  // 对象空间包围盒
    UvMap uvmap;  // glTF UV 集到 Filament UV 的映射
    MorphTargetBuffer* morphTargetBuffer = nullptr;
    uint32_t morphTargetOffset;
    std::vector<int> slotIndices;  // 变形目标的 slot 索引
};
```

### MeshCache (libs/gltfio/src/FFilamentAsset.h:103)

```cpp
using MeshCache = utils::FixedCapacityVector<
    utils::FixedCapacityVector<Primitive>
>;
```

**结构**:
- 外层 vector: 每个 cgltf_mesh
- 内层 vector: 每个 cgltf_primitive

**目的**: 多个 glTF node 引用同一个 mesh 时，共享 VertexBuffer 和 IndexBuffer

### BufferSlot (libs/gltfio/src/FFilamentAsset.h:327-336)

```cpp
struct BufferSlot {
    const cgltf_accessor* accessor;
    cgltf_attribute_type attribute;
    int bufferIndex;  // VertexBuffer 或 MorphTargetBuffer 的 slot 索引
    VertexBuffer* vertexBuffer;
    IndexBuffer* indexBuffer;
    MorphTargetBuffer* morphTargetBuffer;
    uint32_t morphTargetOffset;
    uint32_t morphTargetCount;
};
```

**用途**: 延迟绑定 - 在 createPrimitive 时创建 slot，在 uploadBuffers 时绑定数据

## createPrimitives 流程

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:661-726`

```cpp
void createPrimitives(const cgltf_node* node, const char* name, FFilamentAsset* fAsset) {
    const cgltf_mesh* mesh = node->mesh;
    cgltf_data* gltf = fAsset->mSourceAsset->hierarchy;

    // 1. 获取或创建 Primitive 缓存
    FixedCapacityVector<Primitive>& prims = fAsset->mMeshCache[mesh - gltf->meshes];
    if (prims.empty()) {
        prims.reserve(mesh->primitives_count);
        prims.resize(mesh->primitives_count);
    }

    Aabb aabb;  // 累积包围盒

    // 2. 为每个 primitive 创建 VertexBuffer 和 IndexBuffer
    for (cgltf_size index = 0; index < mesh->primitives_count; ++index) {
        Primitive& outputPrim = prims[index];
        cgltf_primitive& inputPrim = mesh->primitives[index];

        // 如果已经创建过（被其他 node 引用），跳过
        if (!outputPrim.vertices) {
            if (mLoaderExtended) {
                // 使用扩展加载器（mikktspace tangents）
                // ...
            } else {
                // 标准加载路径
                createPrimitive(inputPrim, name, &outputPrim, fAsset);
            }
        }

        // 3. 扩展包围盒
        aabb.min = min(outputPrim.aabb.min, aabb.min);
        aabb.max = max(outputPrim.aabb.max, aabb.max);
    }

    // 4. 更新资源的世界空间包围盒
    mat4f worldTransform;
    cgltf_node_transform_world(node, &worldTransform[0][0]);
    const Aabb transformed = aabb.transform(worldTransform);
    fAsset->mBoundingBox.min = min(fAsset->mBoundingBox.min, transformed.min);
    fAsset->mBoundingBox.max = max(fAsset->mBoundingBox.max, transformed.max);
}
```

## createPrimitive 详解

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:931-1261`

### 阶段 1: 确定材质和所需属性

```cpp
bool createPrimitive(const cgltf_primitive& inPrim, const char* name,
        Primitive* outPrim, FFilamentAsset* fAsset) {

    // 1. 获取材质（用于确定需要哪些顶点属性）
    Material* material = getMaterial(fAsset->mSourceAsset->hierarchy,
        inPrim.material, &outPrim->uvmap, primitiveHasVertexColor(inPrim));

    AttributeBitset requiredAttributes = material->getRequiredAttributes();
```

### 阶段 2: 创建 IndexBuffer

```cpp
    // 2. 创建 IndexBuffer
    IndexBuffer* indices = nullptr;
    const cgltf_accessor* accessor = inPrim.indices;

    if (accessor) {
        // 有索引数据
        IndexBuffer::IndexType indexType;
        getIndexType(accessor->component_type, &indexType);

        indices = IndexBuffer::Builder()
            .indexCount(accessor->count)
            .bufferType(indexType)
            .build(mEngine);

        // 创建 BufferSlot（延迟上传数据）
        BufferSlot slot = { accessor };
        slot.indexBuffer = indices;
        addBufferSlot(slot);
    } else {
        // 无索引数据，生成 trivial 索引 [0, 1, 2, ...]
        const uint32_t vertexCount = inPrim.attributes[0].data->count;

        indices = IndexBuffer::Builder()
            .indexCount(vertexCount)
            .bufferType(IndexBuffer::IndexType::UINT)
            .build(mEngine);

        // 直接上传数据
        uint32_t* indexData = (uint32_t*) malloc(vertexCount * sizeof(uint32_t));
        for (size_t i = 0; i < vertexCount; ++i) {
            indexData[i] = i;
        }
        IndexBuffer::BufferDescriptor bd(indexData, indexDataSize, FREE_CALLBACK);
        indices->setBuffer(mEngine, std::move(bd));
    }

    fAsset->mIndexBuffers.push_back(indices);
```

### 阶段 3: 构建 VertexBuffer

```cpp
    // 3. 构建 VertexBuffer
    VertexBuffer::Builder vbb;
    vbb.enableBufferObjects();

    bool hasUv0 = false, hasUv1 = false, hasVertexColor = false, hasNormals = false;
    uint32_t vertexCount = 0;
    int slot = 0;

    // 遍历所有属性
    for (cgltf_size aindex = 0; aindex < inPrim.attributes_count; aindex++) {
        const cgltf_attribute& attribute = inPrim.attributes[aindex];
        const cgltf_accessor* accessor = attribute.data;
        const cgltf_attribute_type atype = attribute.type;

        // 跳过 tangent（会在 ResourceLoader 中生成）
        if (atype == cgltf_attribute_type_tangent) {
            continue;
        }

        // 处理 normal 属性 - 重新用于存储切线四元数
        if (atype == cgltf_attribute_type_normal) {
            vbb.attribute(VertexAttribute::TANGENTS, slot, VertexBuffer::AttributeType::SHORT4);
            vbb.normalized(VertexAttribute::TANGENTS);
            hasNormals = true;
            addBufferSlot({&fAsset->mGenerateTangents, atype, slot++});
            continue;
        }

        // 转换 glTF 属性类型到 Filament 语义
        VertexAttribute semantic;
        if (!getVertexAttrType(atype, &semantic)) {
            slog.e << "Unrecognized vertex semantic" << io::endl;
            return false;
        }

        // 处理 UV 映射
        if (atype == cgltf_attribute_type_texcoord) {
            UvSet uvset = outPrim->uvmap[attribute.index];
            switch (uvset) {
                case UV0:
                    semantic = VertexAttribute::UV0;
                    hasUv0 = true;
                    break;
                case UV1:
                    semantic = VertexAttribute::UV1;
                    hasUv1 = true;
                    break;
                case UNUSED:
                    // 如果有空闲 slot，包含这个未使用的 UV 集
                    if (!hasUv0 && getNumUvSets(outPrim->uvmap) == 0) {
                        semantic = VertexAttribute::UV0;
                        hasUv0 = true;
                    } else {
                        continue;  // 丢弃
                    }
            }
        }

        vertexCount = accessor->count;

        // 提取包围盒（position 属性必须有 min/max）
        if (atype == cgltf_attribute_type_position) {
            const float* minp = &accessor->min[0];
            const float* maxp = &accessor->max[0];
            outPrim->aabb.min = min(outPrim->aabb.min, float3(minp[0], minp[1], minp[2]));
            outPrim->aabb.max = max(outPrim->aabb.max, float3(maxp[0], maxp[1], maxp[2]));
        }

        // 确定数据类型
        VertexBuffer::AttributeType fatype;
        VertexBuffer::AttributeType actualType;
        getElementType(accessor->type, accessor->component_type, &fatype, &actualType);
        const int stride = (fatype == actualType) ? accessor->stride : 0;

        // 添加属性到 VertexBuffer
        vbb.attribute(semantic, slot, fatype, 0, stride);
        vbb.normalized(semantic, accessor->normalized);

        // 创建 BufferSlot（延迟上传数据）
        addBufferSlot({accessor, atype, slot++});
    }
```

**关键逻辑**:
- **normal → TANGENTS**: glTF 法线被用来生成切线四元数
- **UV 映射**: glTF 支持 8 个 UV 集，Filament 只支持 2 个
- **stride**: 如果数据需要类型转换，stride 设为 0

### 阶段 4: 处理缺失属性

```cpp
    // 4. 如果模型没有法线但材质需要，生成平面法线
    if (requiredAttributes.test(VertexAttribute::TANGENTS) && !hasNormals) {
        vbb.attribute(VertexAttribute::TANGENTS, slot, VertexBuffer::AttributeType::SHORT4);
        vbb.normalized(VertexAttribute::TANGENTS);
        addBufferSlot({&fAsset->mGenerateNormals, cgltf_attribute_type_normal, slot++});
    }
```

### 阶段 5: 处理变形目标 (Morph Targets)

```cpp
    // 5. 处理变形目标
    cgltf_size targetsCount = inPrim.targets_count;
    if (targetsCount > MAX_MORPH_TARGETS) {
        targetsCount = MAX_MORPH_TARGETS;
    }

    const Aabb baseAabb(outPrim->aabb);
    for (cgltf_size targetIndex = 0; targetIndex < targetsCount; targetIndex++) {
        const cgltf_morph_target& morphTarget = inPrim.targets[targetIndex];

        for (cgltf_size aindex = 0; aindex < morphTarget.attributes_count; aindex++) {
            const cgltf_attribute& attribute = morphTarget.attributes[aindex];
            const cgltf_accessor* accessor = attribute.data;

            // 只支持 position、normal、tangent
            if (attribute.type == cgltf_attribute_type_normal ||
                attribute.type == cgltf_attribute_type_tangent) {
                continue;
            }

            if (attribute.type != cgltf_attribute_type_position) {
                slog.e << "Only positions, normals, and tangents can be morphed." << io::endl;
                return false;
            }

            // 扩展包围盒（假设权重范围 [0, 1]）
            if (accessor->has_min && accessor->has_max) {
                Aabb targetAabb(baseAabb);
                const float* minp = &accessor->min[0];
                const float* maxp = &accessor->max[0];
                targetAabb.min += float3(minp[0], minp[1], minp[2]);
                targetAabb.max += float3(maxp[0], maxp[1], maxp[2]);

                outPrim->aabb.min = min(outPrim->aabb.min, targetAabb.min);
                outPrim->aabb.max = max(outPrim->aabb.max, targetAabb.max);
            }
        }
    }
```

### 阶段 6: 处理 Dummy 数据

```cpp
    // 6. 为 Ubershader 提供 Dummy 数据（如果材质需要但模型没有）
    bool needsDummyData = false;

    if (mMaterials.needsDummyData(VertexAttribute::UV0) && !hasUv0) {
        needsDummyData = true;
        vbb.attribute(VertexAttribute::UV0, slot, VertexBuffer::AttributeType::USHORT2);
        vbb.normalized(VertexAttribute::UV0);
    }

    if (mMaterials.needsDummyData(VertexAttribute::UV1) && !hasUv1) {
        needsDummyData = true;
        vbb.attribute(VertexAttribute::UV1, slot, VertexBuffer::AttributeType::USHORT2);
        vbb.normalized(VertexAttribute::UV1);
    }

    if (mMaterials.needsDummyData(VertexAttribute::COLOR) && !hasVertexColor) {
        needsDummyData = true;
        vbb.attribute(VertexAttribute::COLOR, slot, VertexBuffer::AttributeType::UBYTE4);
        vbb.normalized(VertexAttribute::COLOR);
    }

    vbb.bufferCount(needsDummyData ? slot + 1 : slot);
```

**Dummy 数据的目的**:
- Ubershader 使用固定的 shader，必须有所有属性
- 如果模型缺少某个属性，提供全 0xFF 的 dummy 数据

### 阶段 7: 构建 VertexBuffer 并绑定 Dummy

```cpp
    // 7. 构建 VertexBuffer
    vbb.vertexCount(vertexCount);
    VertexBuffer* vertices = vbb.build(mEngine);

    outPrim->indices = indices;
    outPrim->vertices = vertices;
    fAsset->mVertexBuffers.push_back(vertices);

    // 绑定所有 BufferSlot 到这个 VertexBuffer
    for (size_t i = firstSlot; i < slots->size(); ++i) {
        (*slots)[i].vertexBuffer = vertices;
    }

    // 8. 创建并绑定 Dummy BufferObject
    if (needsDummyData) {
        const uint32_t requiredSize = sizeof(ubyte4) * vertexCount;

        // 复用或创建新的 dummy buffer
        if (mDummyBufferObject == nullptr || requiredSize > mDummyBufferObject->getByteCount()) {
            mDummyBufferObject = BufferObject::Builder().size(requiredSize).build(mEngine);
            fAsset->mBufferObjects.push_back(mDummyBufferObject);

            // 填充全 0xFF
            uint32_t* dummyData = (uint32_t*) malloc(requiredSize);
            memset(dummyData, 0xff, requiredSize);
            VertexBuffer::BufferDescriptor bd(dummyData, requiredSize, FREE_CALLBACK);
            mDummyBufferObject->setBuffer(mEngine, std::move(bd));
        }

        vertices->setBufferObjectAt(mEngine, slot, mDummyBufferObject);
    }

    return true;
}
```

## createRenderable 流程

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:728-907`

**职责**: 为 entity 创建 RenderableManager 组件

```cpp
void createRenderable(const cgltf_node* node, Entity entity, const char* name,
        FFilamentAsset* fAsset) {

    const cgltf_mesh* mesh = node->mesh;
    const cgltf_size primitiveCount = mesh->primitives_count;

    // 1. 从 MeshCache 获取 Primitive
    FixedCapacityVector<Primitive>& prims = fAsset->mMeshCache[mesh - srcAsset->meshes];
    Primitive* outputPrim = prims.data();
    const cgltf_primitive* inputPrim = &mesh->primitives[0];

    // 2. 创建 RenderableManager::Builder
    const cgltf_size numMorphTargets = inputPrim ? inputPrim->targets_count : 0;
    RenderableManager::Builder builder(primitiveCount);

    Aabb aabb;
    size_t morphingVertexCount = 0;

    // 3. 为每个 primitive 设置材质和几何体
    for (cgltf_size index = 0; index < primitiveCount; ++index, ++outputPrim, ++inputPrim) {
        // 确定 primitive 类型
        RenderableManager::PrimitiveType primType;
        getPrimitiveType(inputPrim->type, &primType);

        // 创建 MaterialInstance
        MaterialInstance* mi = createMaterialInstance(inputPrim->material, &uvmap,
            hasVertexColor, fAsset);

        // 添加到依赖图（用于渐进式加载）
        fAsset->mDependencyGraph.addEdge(entity, mi);

        builder.material(index, mi);

        // 设置几何体
        builder.geometry(index, primType, outputPrim->vertices, outputPrim->indices);

        // 处理变形目标
        if (numMorphTargets) {
            outputPrim->morphTargetOffset = morphingVertexCount;
            builder.morphing(0, index, morphingVertexCount);
            morphingVertexCount += outputPrim->vertices->getVertexCount();
        }

        // 累积包围盒
        aabb.min = min(outputPrim->aabb.min, aabb.min);
        aabb.max = max(outputPrim->aabb.max, aabb.max);
    }

    // 4. 创建 MorphTargetBuffer（如果有变形目标）
    if (numMorphTargets) {
        MorphTargetBuffer* morphTargetBuffer = MorphTargetBuffer::Builder()
            .count(numMorphTargets)
            .vertexCount(morphingVertexCount)
            .build(mEngine);

        fAsset->mMorphTargetBuffers.push_back(morphTargetBuffer);
        builder.morphing(morphTargetBuffer);

        // 为每个目标创建 BufferSlot
        outputPrim = prims.data();
        inputPrim = &mesh->primitives[0];
        for (cgltf_size index = 0; index < primitiveCount; ++index, ++outputPrim, ++inputPrim) {
            outputPrim->morphTargetBuffer = morphTargetBuffer;

            for (int tindex = 0; tindex < numMorphTargets; ++tindex) {
                const cgltf_morph_target& inTarget = inputPrim->targets[tindex];

                for (cgltf_size aindex = 0; aindex < inTarget.attributes_count; ++aindex) {
                    const cgltf_attribute& attribute = inTarget.attributes[aindex];
                    if (attribute.type == cgltf_attribute_type_position) {
                        // 创建 BufferSlot（延迟上传）
                        BufferSlot& slot = slots[outputPrim->slotIndices[tindex]];
                        slot.morphTargetBuffer = outputPrim->morphTargetBuffer;
                        slot.morphTargetOffset = outputPrim->morphTargetOffset;
                        slot.morphTargetCount = outputPrim->vertices->getVertexCount();
                        slot.bufferIndex = tindex;
                        break;
                    }
                }
            }
        }
    }

    // 5. 设置变形目标名称
    FixedCapacityVector<CString> morphTargetNames(numMorphTargets);
    for (cgltf_size i = 0; i < mesh->target_names_count; ++i) {
        morphTargetNames[i] = CString(mesh->target_names[i]);
    }
    mNodeManager.setMorphTargetNames(mNodeManager.getInstance(entity), std::move(morphTargetNames));

    // 6. 配置骨骼蒙皮
    if (node->skin) {
        builder.skinning(node->skin->joints_count);
    }

    // 7. 设置包围盒
    Box box = Box().set(aabb.min, aabb.max);
    if (box.isEmpty()) {
        slog.w << "Missing bounding box" << io::endl;
        box = Box().set(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max());
    }

    // 8. 构建 Renderable 组件
    builder
        .boundingBox(box)
        .culling(true)
        .castShadows(true)
        .receiveShadows(true)
        .build(mEngine, entity);

    // 9. 设置默认变形权重
    if (numMorphTargets > 0) {
        RenderableManager::Instance renderable = mRenderableManager.getInstance(entity);
        const auto size = std::min(MAX_MORPH_TARGETS, numMorphTargets);
        FixedCapacityVector<float> weights(size, 0.0f);

        // mesh 的默认权重
        for (cgltf_size i = 0; i < std::min(size, mesh->weights_count); ++i) {
            weights[i] = mesh->weights[i];
        }

        // node 的权重覆盖 mesh 权重
        for (cgltf_size i = 0; i < std::min(size, node->weights_count); ++i) {
            weights[i] = node->weights[i];
        }

        mRenderableManager.setMorphWeights(renderable, weights.data(), size);
    }
}
```

## 顶点属性映射

### glTF 到 Filament 的映射

| glTF 属性 | Filament 属性 | 说明 |
|----------|-------------|------|
| POSITION | POSITION | 顶点位置 |
| NORMAL | TANGENTS | 用于存储切线四元数 (生成) |
| TANGENT | (忽略) | glTF 的 tangent 被忽略，使用生成的 |
| TEXCOORD_0..7 | UV0 或 UV1 | 通过 UvMap 映射 |
| COLOR_0 | COLOR | 顶点颜色 |
| JOINTS_0 | BONE_INDICES | 骨骼索引 |
| WEIGHTS_0 | BONE_WEIGHTS | 骨骼权重 |

### UvMap 系统

**代码位置**: `libs/gltfio/include/gltfio/MaterialProvider.h:113-123`

```cpp
enum UvSet : uint8_t { UNUSED, UV0, UV1 };
constexpr int UvMapSize = 8;
using UvMap = std::array<UvSet, UvMapSize>;
```

**工作原理**:
1. MaterialProvider 根据材质需求确定哪些 UV 集被使用
2. 将使用的 UV 集映射到 UV0 或 UV1
3. 未使用的 UV 集标记为 UNUSED（可能被丢弃）

**示例**:
```cpp
// 材质使用 baseColorTexture(UV0) 和 normalTexture(UV1)
UvMap uvmap = { UV0, UV1, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED };
//              ^    ^    其余未使用
//              |    |
//           TEXCOORD_0  TEXCOORD_1
```

## 数据格式转换

### getElementType

**代码位置**: `libs/gltfio/src/Utility.cpp` (未提供，但被引用)

**功能**: 将 glTF 数据类型转换为 Filament 数据类型

**示例**:
- glTF: `VEC3 + FLOAT` → Filament: `FLOAT3`
- glTF: `VEC4 + UNSIGNED_BYTE (normalized)` → Filament: `UBYTE4` (normalized)
- glTF: `VEC2 + UNSIGNED_SHORT (normalized)` → Filament: `USHORT2` (normalized)

### requiresConversion

**条件**:
1. 数据类型不是 Filament 原生支持的
2. 归一化整数需要转换为浮点数

**处理**:
```cpp
if (utility::requiresConversion(accessor)) {
    // 使用 cgltf_accessor_unpack_floats 转换为浮点数
    float* floatsData = (float*) malloc(floatsByteCount);
    cgltf_accessor_unpack_floats(accessor, floatsData, floatsCount);

    // 上传浮点数据
    BufferObject* bo = BufferObject::Builder().size(floatsByteCount).build(engine);
    bo->setBuffer(engine, BufferDescriptor(floatsData, floatsByteCount, FREE_CALLBACK));
}
```

## 变形目标 (Morph Targets)

### 数据结构

**glTF**:
```json
{
  "primitives": [{
    "attributes": { "POSITION": 0, "NORMAL": 1 },
    "targets": [
      { "POSITION": 2, "NORMAL": 3 },  // Target 0
      { "POSITION": 4, "NORMAL": 5 }   // Target 1
    ]
  }]
}
```

**Filament**:
```cpp
MorphTargetBuffer* mtb = MorphTargetBuffer::Builder()
    .count(2)  // 2 个目标
    .vertexCount(totalVertexCount)
    .build(engine);

// 设置目标 0 的位置偏移
mtb->setPositionsAt(engine, 0, positions0, vertexCount, offset);

// 设置目标 1 的位置偏移
mtb->setPositionsAt(engine, 1, positions1, vertexCount, offset);
```

### morphing() 配置

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:792`

```cpp
builder.morphing(0, primitiveIndex, morphingVertexCount);
//               ^  ^                ^
//               |  |                |
//      level    |  |                |-- 累积的顶点偏移
//               |  |-- primitive 索引
//               |-- LOD level (总是 0)
```

**vertexCount 累积**:
```
Primitive 0: 100 vertices, offset = 0
Primitive 1: 150 vertices, offset = 100
Primitive 2: 200 vertices, offset = 250
Total: 450 vertices in MorphTargetBuffer
```

## 骨骼蒙皮配置

### skinning() 配置

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:874`

```cpp
if (node->skin) {
    builder.skinning(node->skin->joints_count);
}
```

**效果**:
- 告诉 RenderableManager 这个 Renderable 使用骨骼蒙皮
- 预留空间存储 bone matrices

**后续步骤**:
1. Animator 计算 bone matrices
2. `RenderableManager::setBones()` 更新矩阵
3. GPU shader 使用矩阵变换顶点

## 包围盒计算

### 对象空间包围盒

**来源**: glTF accessor 的 `min` 和 `max` 属性

```cpp
if (atype == cgltf_attribute_type_position) {
    const float* minp = &accessor->min[0];
    const float* maxp = &accessor->max[0];
    outPrim->aabb.min = min(outPrim->aabb.min, float3(minp[0], minp[1], minp[2]));
    outPrim->aabb.max = max(outPrim->aabb.max, float3(maxp[0], maxp[1], maxp[2]));
}
```

### 世界空间包围盒

**计算**: 对象空间 AABB × 节点的世界变换

**代码位置**: `libs/gltfio/src/AssetLoader.cpp:720-725`

```cpp
mat4f worldTransform;
cgltf_node_transform_world(node, &worldTransform[0][0]);

const Aabb transformed = aabb.transform(worldTransform);
fAsset->mBoundingBox.min = min(fAsset->mBoundingBox.min, transformed.min);
fAsset->mBoundingBox.max = max(fAsset->mBoundingBox.max, transformed.max);
```

### 变形目标的包围盒

假设权重范围 `[0, 1]`:

```cpp
Aabb targetAabb(baseAabb);
targetAabb.min += float3(minp[0], minp[1], minp[2]);
targetAabb.max += float3(maxp[0], maxp[1], maxp[2]);

outPrim->aabb.min = min(outPrim->aabb.min, targetAabb.min);
outPrim->aabb.max = max(outPrim->aabb.max, targetAabb.max);
```

## Primitive 类型支持

**代码位置**: `libs/gltfio/src/GltfEnums.h` (未提供，但被引用)

```cpp
bool getPrimitiveType(cgltf_primitive_type gltfType, RenderableManager::PrimitiveType* out) {
    switch (gltfType) {
        case cgltf_primitive_type_points:
            *out = RenderableManager::PrimitiveType::POINTS;
            return true;
        case cgltf_primitive_type_lines:
            *out = RenderableManager::PrimitiveType::LINES;
            return true;
        case cgltf_primitive_type_line_strip:
            *out = RenderableManager::PrimitiveType::LINE_STRIP;
            return true;
        case cgltf_primitive_type_triangles:
            *out = RenderableManager::PrimitiveType::TRIANGLES;
            return true;
        case cgltf_primitive_type_triangle_strip:
            *out = RenderableManager::PrimitiveType::TRIANGLE_STRIP;
            return true;
        default:
            return false;  // 不支持 triangle_fan, line_loop
    }
}
```

## Draco 压缩支持

**检测和解压**: `libs/gltfio/src/ResourceLoader.cpp:436-444`

```cpp
for (auto& [prim, vertexBuffer]: primitives) {
    if (prim->has_draco_mesh_compression) {
        utility::decodeDracoMeshes(gltf, prim, dracoCache);
    }
}
```

**DracoCache**: 缓存解压后的数据，避免重复解压

## meshopt 压缩支持

**解压**: `libs/gltfio/src/ResourceLoader.cpp:445`

```cpp
utility::decodeMeshoptCompression((cgltf_data*) gltf);
```

**工作原理**:
- meshopt 压缩存储在 buffer_view 中
- cgltf 支持检测 `EXT_meshopt_compression`
- 解压后数据存储在 `buffer_view->data`

## 性能优化

### MeshCache 复用

**场景**: 多个 node 引用同一个 mesh

```
Node A ──┐
         ├──> Mesh 0 ──> Primitive 0 (VertexBuffer, IndexBuffer)
Node B ──┘                Primitive 1 (VertexBuffer, IndexBuffer)
```

**好处**:
- 节省内存（共享 GPU 缓冲区）
- 加快加载速度（只创建一次）

### BufferSlot 延迟上传

**流程**:
1. AssetLoader 创建 VertexBuffer/IndexBuffer（空壳）
2. AssetLoader 创建 BufferSlot（记录 accessor）
3. ResourceLoader 读取数据并上传

**好处**:
- 分离结构创建和数据加载
- 支持异步数据获取
- 更灵活的资源管理

### Dummy BufferObject 复用

**策略**:
```cpp
if (mDummyBufferObject == nullptr || requiredSize > mDummyBufferObject->getByteCount()) {
    // 创建更大的 dummy buffer
    mDummyBufferObject = BufferObject::Builder().size(requiredSize).build(mEngine);
}

// 所有需要 dummy 数据的 VertexBuffer 共享同一个 BufferObject
vertices->setBufferObjectAt(mEngine, slot, mDummyBufferObject);
```

**好处**: 节省内存，所有 dummy 数据共享一个 BufferObject

## 错误处理

### 常见错误

1. **缺少包围盒**:
```cpp
if (box.isEmpty()) {
    slog.w << "Missing bounding box in " << name << io::endl;
    box = Box().set(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max());
}
```

2. **不支持的索引类型**:
```cpp
if (!getIndexType(accessor->component_type, &indexType)) {
    slog.e << "Unrecognized index type in " << name << io::endl;
    return false;
}
```

3. **变形目标数量超限**:
```cpp
if (targetsCount > MAX_MORPH_TARGETS) {
    slog.w << "Exceeded max morph target count of " << MAX_MORPH_TARGETS << io::endl;
    targetsCount = MAX_MORPH_TARGETS;
}
```

## 下一步阅读

- [材质系统](./material-system.md) - MaterialInstance 创建和纹理绑定
- [GPU 交互](./gpu-interaction.md) - BufferObject 和数据上传细节
- [骨骼动画](./skeletal-animation.md) - 骨骼蒙皮的完整流程
