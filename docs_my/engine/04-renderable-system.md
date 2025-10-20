# Renderable 系统

本文档详细讲解 Filament 的 Renderable 系统，包括 RenderableManager 的使用、几何实例化、包围盒和剔除、以及渲染优化技巧。

---

## Renderable 概述

**Renderable** 是 Filament 中表示可渲染几何体的组件。每个 Renderable 包含：
- **几何数据**: VertexBuffer 和 IndexBuffer
- **材质**: MaterialInstance
- **包围盒**: 用于视锥剔除
- **渲染属性**: 阴影、优先级等

---

## 创建 Renderable

### 基本创建

```cpp
#include <filament/RenderableManager.h>

using namespace filament;

// 准备资源
VertexBuffer* vb = /* ... */;
IndexBuffer* ib = /* ... */;
MaterialInstance* material = /* ... */;

// 创建 Entity
Entity entity = EntityManager::get().create();

// 创建 Renderable
RenderableManager::Builder(1)  // 1 个图元
    .boundingBox({{-1, -1, -1}, {1, 1, 1}})
    .material(0, material)
    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb, ib)
    .build(*engine, entity);

// 添加到场景
scene->addEntity(entity);
```

### RenderableManager::Builder 参数

| 方法 | 说明 | 必需 |
|------|------|------|
| **构造函数(count)** | 图元数量 | ✅ |
| **boundingBox()** | 包围盒 | ✅ |
| **material(index, mat)** | 材质实例 | ✅ |
| **geometry(index, type, vb, ib)** | 几何数据 | ✅ |
| **culling(bool)** | 是否参与剔除 | ❌ (默认true) |
| **castShadows(bool)** | 是否投射阴影 | ❌ (默认false) |
| **receiveShadows(bool)** | 是否接收阴影 | ❌ (默认false) |
| **priority(uint8_t)** | 渲染优先级 0-7 | ❌ (默认4) |
| **layerMask(uint8_t, uint8_t)** | 图层掩码 | ❌ |
| **skinning(size_t)** | 骨骼数量（蒙皮） | ❌ |
| **morphing(size_t)** | 变形目标数量 | ❌ |
| **blendOrder(uint16_t, uint16_t)** | 透明混合顺序 | ❌ |
| **globalBlendOrderEnabled(bool)** | 全局混合排序 | ❌ |

---

## 多图元 Renderable

一个 Renderable 可以包含多个图元（Primitive），每个图元有独立的材质和几何：

```cpp
// 创建一个车体（3个图元：车身、车窗、车灯）
Entity car = EntityManager::get().create();

RenderableManager::Builder(3)
    // 整体包围盒
    .boundingBox({{-2, 0, -4}, {2, 1.5, 4}})
    
    // 图元0：车身
    .material(0, bodyMaterial)
    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,
              bodyVB, bodyIB, 0, bodyIB->getIndexCount())
    
    // 图元1：车窗（透明）
    .material(1, windowMaterial)
    .geometry(1, RenderableManager::PrimitiveType::TRIANGLES,
              windowVB, windowIB)
    .blendOrder(1, 100)  // 透明物体需要混合顺序
    
    // 图元2：车灯（自发光）
    .material(2, lightMaterial)
    .geometry(2, RenderableManager::PrimitiveType::TRIANGLES,
              lightVB, lightIB)
    
    .build(*engine, car);
```

### geometry() 参数详解

```cpp
void geometry(
    size_t index,                    // 图元索引
    PrimitiveType type,              // 图元类型
    VertexBuffer* vertices,          // 顶点缓冲
    IndexBuffer* indices,            // 索引缓冲
    size_t offset = 0,               // 索引起始位置
    size_t count = 0                 // 索引数量（0=全部）
);

// 示例：使用部分索引
RenderableManager::Builder(1)
    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,
              vb, ib,
              100,   // 从索引100开始
              300)   // 使用300个索引
    .build(*engine, entity);
```

---

## 图元类型

```cpp
enum class PrimitiveType : uint8_t {
    POINTS,           // 点
    LINES,            // 线段
    LINE_STRIP,       // 线条
    TRIANGLES,        // 三角形（最常用）
    TRIANGLE_STRIP,   // 三角形带
};

// 示例：绘制线框
RenderableManager::Builder(1)
    .geometry(0, RenderableManager::PrimitiveType::LINES, vb, ib)
    .material(0, wireframeMaterial)
    .build(*engine, entity);
```

---

## 包围盒和剔除

### 包围盒的重要性

包围盒用于视锥剔除，正确设置可以显著提升性能：

```cpp
// ❌ 错误：包围盒过大
.boundingBox({{-1000, -1000, -1000}, {1000, 1000, 1000}})
// 结果：对象永远不会被剔除，浪费 GPU

// ✅ 正确：紧致的包围盒
.boundingBox({{-1, -1, -1}, {1, 1, 1}})
// 结果：对象离开视锥后立即剔除

// ✅ 动态计算包围盒
Box computeBoundingBox(const std::vector<float3>& vertices) {
    Box box;
    for (const auto& v : vertices) {
        box.min = min(box.min, v);
        box.max = max(box.max, v);
    }
    return box;
}
```

### 禁用剔除

某些对象（如天空盒）应该禁用剔除：

```cpp
RenderableManager::Builder(1)
    .culling(false)  // 禁用剔除，始终渲染
    .castShadows(false)
    .receiveShadows(false)
    .build(*engine, skyboxEntity);
```

---

## 阴影

### 投射和接收阴影

```cpp
// 投射阴影的对象（建筑、角色等）
RenderableManager::Builder(1)
    .castShadows(true)
    .receiveShadows(true)
    .build(*engine, entity);

// 只接收阴影（地面）
RenderableManager::Builder(1)
    .castShadows(false)
    .receiveShadows(true)
    .build(*engine, groundEntity);

// 不参与阴影（粒子效果等）
RenderableManager::Builder(1)
    .castShadows(false)
    .receiveShadows(false)
    .build(*engine, particleEntity);
```

### 阴影性能考虑

- **castShadows(true)**: 需要在阴影pass中渲染，有性能开销
- **receiveShadows(true)**: 在主pass中采样阴影贴图，开销较小

```cpp
// 优化建议：
// 1. 小物体关闭投射阴影
// 2. 远处物体关闭投射阴影（使用LOD）
// 3. 天空盒、粒子等关闭阴影
```

---

## 渲染优先级

### priority() 参数

priority 控制渲染顺序（0-7）：

```cpp
// 优先级规则：
// - 不透明物体：从前往后渲染（减少overdraw）
// - 透明物体：从后往前渲染（正确混合）

// 不透明物体（priority 不重要，深度排序自动处理）
RenderableManager::Builder(1)
    .priority(4)  // 默认值
    .build(*engine, opaqueEntity);

// 透明物体（priority 控制绘制顺序）
RenderableManager::Builder(1)
    .priority(6)  // 更高优先级，后渲染
    .blendOrder(0, 10)
    .build(*engine, glassEntity);

// UI 元素（最后渲染）
RenderableManager::Builder(1)
    .priority(7)
    .build(*engine, uiEntity);
```

### blendOrder() 透明排序

```cpp
// blendOrder(primitiveIndex, order)
// order 值越大，越晚渲染

RenderableManager::Builder(3)
    // 不透明车身（不需要 blendOrder）
    .material(0, bodyMaterial)
    .geometry(0, PrimitiveType::TRIANGLES, vb, ib)
    
    // 半透明车窗（需要排序）
    .material(1, windowMaterial)
    .geometry(1, PrimitiveType::TRIANGLES, windowVB, windowIB)
    .blendOrder(1, 100)  // 在车身之后渲染
    
    // 透明反光镜（需要排序）
    .material(2, mirrorMaterial)
    .geometry(2, PrimitiveType::TRIANGLES, mirrorVB, mirrorIB)
    .blendOrder(2, 101)  // 在车窗之后渲染
    
    .build(*engine, carEntity);
```

---

## 图层掩码

### layerMask() 用途

图层掩码用于选择性渲染：

```cpp
// 定义图层
constexpr uint8_t LAYER_DEFAULT = 0x01;
constexpr uint8_t LAYER_UI = 0x02;
constexpr uint8_t LAYER_DEBUG = 0x04;
constexpr uint8_t LAYER_SHADOW_ONLY = 0x08;

// 设置 Renderable 的图层
RenderableManager::Builder(1)
    .layerMask(0, LAYER_DEFAULT)  // 图元0在默认图层
    .material(0, material)
    .geometry(0, PrimitiveType::TRIANGLES, vb, ib)
    .build(*engine, entity);

// UI 元素
RenderableManager::Builder(1)
    .layerMask(0, LAYER_UI)
    .build(*engine, uiEntity);

// View 选择渲染哪些图层
view->setVisibleLayers(LAYER_DEFAULT | LAYER_UI, 0xFF);
// 第一个参数：选择的图层
// 第二个参数：启用的图层位（通常是0xFF）
```

### 应用场景

```cpp
// 主视图：渲染场景和UI
mainView->setVisibleLayers(LAYER_DEFAULT | LAYER_UI, 0xFF);

// 阴影视图：只渲染投射阴影的对象
shadowView->setVisibleLayers(LAYER_DEFAULT, 0xFF);

// 调试视图：只渲染调试对象
debugView->setVisibleLayers(LAYER_DEBUG, 0xFF);
```

---

## 骨骼蒙皮

### 创建带蒙皮的 Renderable

```cpp
// 骨骼数量
constexpr size_t BONE_COUNT = 64;

// 创建骨骼 Entities
Entity bones[BONE_COUNT];
for (size_t i = 0; i < BONE_COUNT; i++) {
    bones[i] = EntityManager::get().create();
    // 设置骨骼 Transform...
}

// 创建蒙皮 Renderable
RenderableManager::Builder(1)
    .boundingBox({{-1, 0, -1}, {1, 2, 1}})  // 角色包围盒
    .material(0, characterMaterial)
    .geometry(0, PrimitiveType::TRIANGLES, vb, ib)
    .skinning(BONE_COUNT, bones)  // 指定骨骼
    .build(*engine, characterEntity);

// 更新骨骼变换
auto& rm = engine->getRenderableManager();
auto& tcm = engine->getTransformManager();

for (size_t i = 0; i < BONE_COUNT; i++) {
    mat4f boneTransform = /* 动画系统计算的骨骼变换 */;
    tcm.setTransform(tcm.getInstance(bones[i]), boneTransform);
}
```

### 蒙皮顶点格式

蒙皮需要额外的顶点属性：

```cpp
struct SkinnedVertex {
    float3 position;
    float3 normal;
    float2 uv;
    uint4 boneIndices;   // 4个骨骼索引
    float4 boneWeights;  // 4个骨骼权重（和为1）
};

VertexBuffer* vb = VertexBuffer::Builder()
    .vertexCount(vertexCount)
    .bufferCount(1)
    .attribute(VertexAttribute::POSITION, 0,
               VertexBuffer::AttributeType::FLOAT3,
               offsetof(SkinnedVertex, position), sizeof(SkinnedVertex))
    .attribute(VertexAttribute::NORMAL, 0,
               VertexBuffer::AttributeType::FLOAT3,
               offsetof(SkinnedVertex, normal), sizeof(SkinnedVertex))
    .attribute(VertexAttribute::UV0, 0,
               VertexBuffer::AttributeType::FLOAT2,
               offsetof(SkinnedVertex, uv), sizeof(SkinnedVertex))
    .attribute(VertexAttribute::BONE_INDICES, 0,
               VertexBuffer::AttributeType::UBYTE4,
               offsetof(SkinnedVertex, boneIndices), sizeof(SkinnedVertex))
    .attribute(VertexAttribute::BONE_WEIGHTS, 0,
               VertexBuffer::AttributeType::FLOAT4,
               offsetof(SkinnedVertex, boneWeights), sizeof(SkinnedVertex))
    .build(*engine);
```

---

## 变形目标 (Morph Targets)

### 创建带变形的 Renderable

```cpp
// 变形目标数量
constexpr size_t MORPH_COUNT = 4;

// 创建变形目标缓冲
MorphTargetBuffer* morphTargets = MorphTargetBuffer::Builder()
    .vertexCount(vertexCount)
    .count(MORPH_COUNT)
    .build(*engine);

// 设置变形数据（位置、切线偏移）
for (size_t i = 0; i < MORPH_COUNT; i++) {
    morphTargets->setPositionsAt(*engine, i,
        /* position deltas */, vertexCount);
    morphTargets->setTangentsAt(*engine, i,
        /* tangent deltas */, vertexCount);
}

// 创建 Renderable
RenderableManager::Builder(1)
    .boundingBox(box)
    .material(0, material)
    .geometry(0, PrimitiveType::TRIANGLES, vb, ib)
    .morphing(MORPH_COUNT)
    .build(*engine, entity);

// 设置变形目标
auto& rm = engine->getRenderableManager();
auto instance = rm.getInstance(entity);
rm.setMorphTargetBufferAt(instance, 0, morphTargets);

// 控制变形权重（0-1）
rm.setMorphWeights(instance, {0.5f, 0.3f, 0.0f, 0.2f}, 4);
```

---

## 实例化渲染

### 几何实例化（GPU Instancing）

绘制大量相同几何体（如树木、草地）：

```cpp
// 创建实例数据缓冲
constexpr size_t INSTANCE_COUNT = 1000;

struct InstanceData {
    mat4f transform;
    float4 color;
};

InstanceData instances[INSTANCE_COUNT];
for (size_t i = 0; i < INSTANCE_COUNT; i++) {
    instances[i].transform = /* ... */;
    instances[i].color = /* ... */;
}

// 创建实例缓冲
InstanceBuffer* instanceBuffer = InstanceBuffer::Builder()
    .instanceCount(INSTANCE_COUNT)
    .build(*engine);

instanceBuffer->setBuffer(*engine,
    InstanceBuffer::BufferDescriptor(instances, sizeof(instances)));

// 创建实例化 Renderable
RenderableManager::Builder(1)
    .boundingBox(box)
    .material(0, material)
    .geometry(0, PrimitiveType::TRIANGLES, vb, ib)
    .instances(INSTANCE_COUNT, instanceBuffer)  // 启用实例化
    .build(*engine, entity);

// 一次draw call绘制1000个实例！
```

---

## 运行时修改

### 更新材质

```cpp
auto& rm = engine->getRenderableManager();
auto instance = rm.getInstance(entity);

// 更改材质
rm.setMaterialInstanceAt(instance, 0, newMaterial);
```

### 更新几何

```cpp
// 更改几何数据（顶点/索引缓冲）
rm.setGeometryAt(instance, 0, PrimitiveType::TRIANGLES, newVB, newIB);
```

### 更新包围盒

```cpp
// 当几何变化时，需要更新包围盒
Box newBox = computeBoundingBox(newVertices);
rm.setAxisAlignedBoundingBox(instance, newBox);
```

---

## 性能优化

### 1. 减少 Draw Call

```cpp
// ❌ 低效：1000个独立对象 = 1000个draw call
for (int i = 0; i < 1000; i++) {
    Entity e = em.create();
    RenderableManager::Builder(1)./* ... */.build(*engine, e);
}

// ✅ 高效：1个实例化对象 = 1个draw call
RenderableManager::Builder(1)
    .instances(1000, instanceBuffer)
    .build(*engine, entity);
```

### 2. 合并几何

```cpp
// ❌ 低效：多个小对象
Entity box1, box2, box3;
// 3个draw call

// ✅ 高效：合并成一个对象
Entity mergedBoxes;
RenderableManager::Builder(1)
    .geometry(0, PrimitiveType::TRIANGLES, mergedVB, mergedIB)
    .build(*engine, mergedBoxes);
// 1个draw call
```

### 3. 正确的剔除

```cpp
// ✅ 紧致的包围盒，高效剔除
.boundingBox(computeTightBoundingBox(vertices))

// ❌ 过大的包围盒，无法剔除
.boundingBox({{-9999, -9999, -9999}, {9999, 9999, 9999}})
```

### 4. LOD (Level of Detail)

```cpp
// 根据距离选择不同细节的模型
float distance = length(cameraPos - objectPos);

Entity entity;
if (distance < 10) {
    entity = highDetailEntity;  // 高模
} else if (distance < 50) {
    entity = mediumDetailEntity;  // 中模
} else {
    entity = lowDetailEntity;  // 低模
}

scene->addEntity(entity);
```

---

## 完整示例

```cpp
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/RenderableManager.h>

using namespace filament;

void createCompleteRenderable(Engine* engine, Scene* scene) {
    // 1. 创建资源
    VertexBuffer* vb = /* ... */;
    IndexBuffer* ib = /* ... */;
    MaterialInstance* bodyMat = /* ... */;
    MaterialInstance* windowMat = /* ... */;

    // 2. 创建 Entity
    Entity car = EntityManager::get().create();

    // 3. 创建多图元 Renderable
    RenderableManager::Builder(2)
        // 整体包围盒
        .boundingBox({{-2, 0, -4}, {2, 1.5, 4}})
        
        // 图元0：不透明车身
        .material(0, bodyMat)
        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,
                  vb, ib, 0, 1000)
        .castShadows(true)
        .receiveShadows(true)
        
        // 图元1：透明车窗
        .material(1, windowMat)
        .geometry(1, RenderableManager::PrimitiveType::TRIANGLES,
                  vb, ib, 1000, 500)
        .blendOrder(1, 100)
        .castShadows(false)  // 玻璃不投射阴影
        .receiveShadows(false)
        
        // 渲染配置
        .culling(true)
        .priority(4)
        .layerMask(0, 0x01)
        .layerMask(1, 0x01)
        
        .build(*engine, car);

    // 4. 添加 Transform
    auto& tcm = engine->getTransformManager();
    auto ti = tcm.create(car);
    tcm.setTransform(ti, mat4f::translation(float3(0, 0, 0)));

    // 5. 添加到场景
    scene->addEntity(car);
}
```

---

## 相关文档

- **[02-entity-component.md](02-entity-component.md)**: RenderableManager
- **[03-resource-management.md](03-resource-management.md)**: VertexBuffer/IndexBuffer
- **[07-render-loop.md](07-render-loop.md)**: 渲染流程

---

## 总结

Renderable 系统通过 **Builder 模式**、**多图元支持**、**实例化渲染**、**灵活的剔除和阴影配置**，提供了强大而高效的几何渲染能力。

**核心要点**:
- ✅ 使用紧致的包围盒提升剔除效率
- ✅ 合理使用实例化减少draw call
- ✅ 正确配置阴影属性平衡质量和性能
- ✅ 使用图层掩码实现选择性渲染
- ✅ 多图元支持复杂对象的渲染
