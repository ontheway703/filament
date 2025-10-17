# 架构关键点

## 概述

本文档深入探讨 gltfio 的架构设计决策和关键技术点。

## Entity-Component 架构

### Filament ECS

gltfio 基于 Filament 的 Entity-Component-System (ECS) 架构：

**Entity**: 轻量级标识符 (uint32_t)
```cpp
namespace utils {
    struct Entity {
        uint32_t id;
    };
}
```

**Component**: 数据存储在 Manager 中

| Manager | 组件类型 | 用途 |
|---------|---------|------|
| TransformManager | Transform | 节点变换（层次结构） |
| RenderableManager | Renderable | 可渲染对象（mesh + material） |
| LightManager | Light | 灯光 |
| Camera | Camera | 相机 |
| NameComponentManager | Name | 实体名称 |

**System**: Manager 提供操作接口
```cpp
auto instance = transformManager.getInstance(entity);
transformManager.setTransform(instance, mat4f(...));
```

### gltfio 的 Component 扩展

**NodeManager** (`libs/gltfio/include/gltfio/NodeManager.h`):
- 存储 glTF 特有的节点信息
- SceneMask: 节点属于哪些 glTF 场景
- Extras: glTF extras 字符串
- MorphTargetNames: 变形目标名称

**TrsTransformManager** (`libs/gltfio/include/gltfio/TrsTransformManager.h`):
- 分别存储 Translation, Rotation, Scale
- 方便动画系统修改（无需矩阵分解）
- 与 TransformManager 配合使用

## Asset vs Instance 设计

### 设计目标

**问题**: 如何高效地渲染同一个 glTF 模型的多个副本？

**解决方案**: 分离共享资源（Asset）和独立资源（Instance）

### FilamentAsset (共享资源)

**拥有**:
- VertexBuffer 和 IndexBuffer (GPU 缓冲区)
- Texture (纹理)
- Material (shader)
- Skin 数据 (inverseBindMatrices)
- 动画数据 (Animation, Sampler)

**不拥有**:
- Entity (每个实例独立)
- Transform (每个实例独立)
- MaterialInstance (每个实例独立)

**代码位置**: `libs/gltfio/src/FFilamentAsset.h:105-373`

### FilamentInstance (独立资源)

**拥有**:
- Entity 列表
- Transform 层次结构
- MaterialInstance 列表
- Skin 目标 (targets) 和关节 (joints)
- Animator (可选)

**引用**:
- Asset 的共享资源 (VertexBuffer, Texture, etc.)

**代码位置**: `libs/gltfio/src/FFilamentInstance.h:56-129`

### 实例化流程

```
AssetLoader::createInstancedAsset(bytes, numBytes, instances, numInstances)
├─ createRootAsset(bytes)
│  ├─ 解析 glTF
│  ├─ 创建 VertexBuffer 和 IndexBuffer (共享)
│  └─ 缓存到 MeshCache
└─ createInstances(numInstances)
   ├─ 为每个实例调用 createInstance()
   │  ├─ 创建新的 Entity 树
   │  ├─ 创建新的 Transform 组件
   │  ├─ 创建新的 MaterialInstance (引用共享 Texture)
   │  └─ 创建新的 Renderable (引用共享 VertexBuffer)
   └─ ...
```

### 内存效率

**示例**: 渲染 100 个相同的角色

**传统方式**:
- 100 × VertexBuffer = 大量内存
- 100 × IndexBuffer = 大量内存
- 100 × Texture = 大量内存

**gltfio 方式**:
- 1 × VertexBuffer (共享)
- 1 × IndexBuffer (共享)
- 1 × Texture (共享)
- 100 × Entity (轻量)
- 100 × Transform (小)
- 100 × MaterialInstance (中等，但共享 Material 和 Texture)

**节省**: 主要节省 GPU 内存

## NodeManager 和场景管理

### SceneMask

**目的**: glTF 支持多个场景，需要跟踪节点属于哪些场景

**实现**:
```cpp
using SceneMask = std::bitset<MAX_SCENE_COUNT>;
constexpr size_t MAX_SCENE_COUNT = 32;
```

**用法**:
```cpp
// 节点属于场景 0 和 2
SceneMask mask;
mask.set(0);
mask.set(2);

nodeManager.setSceneMembership(nodeInstance, mask);
```

**应用**:
```cpp
// 只添加属于场景 0 的实体
SceneMask filter;
filter.set(0);

asset->addEntitiesToScene(scene, entities, count, filter);
```

### RootNodes 映射

**代码位置**: `libs/gltfio/src/FFilamentAsset.h:302`

```cpp
tsl::robin_map<cgltf_node*, SceneMask> mRootNodes;
```

**用途**: 记录每个根节点属于哪些场景

**构建** (`libs/gltfio/src/AssetLoader.cpp:508-517`):
```cpp
for (size_t si = 0; si < srcAsset->scenes_count; ++si) {
    const cgltf_scene& scene = srcAsset->scenes[si];
    for (size_t ni = 0; ni < scene.nodes_count; ++ni) {
        fAsset->mRootNodes[scene.nodes[ni]].set(si);
    }
}
```

## TrsTransformManager

### 设计动机

**问题**: TransformManager 只存储 4×4 矩阵，动画需要分别修改 TRS

**解决方案**: 额外的 TrsTransformManager 存储分解后的 TRS

### 数据结构

```cpp
struct TrsTransform {
    float3 translation;
    quatf rotation;
    float3 scale;
};
```

### 协作流程

```
1. 创建实体
   ├─ TransformManager::create(entity, parent, localTransform)
   └─ TrsTransformManager::create(entity, translation, rotation, scale)

2. 动画修改
   ├─ TrsTransformManager::setTranslation(instance, newTranslation)
   ├─ TrsTransformManager::setRotation(instance, newRotation)
   └─ TrsTransformManager::setScale(instance, newScale)

3. 更新 TransformManager
   └─ mat4f transform = TrsTransformManager::getTransform(instance)
      └─ TransformManager::setTransform(instance, transform)
```

**代码位置**: `libs/gltfio/src/Animator.cpp:529`

```cpp
// 更新 TransformManager（从 TrsTransformManager 获取组合矩阵）
transformManager->setTransform(node, trsTransformManager->getTransform(trsNode));
```

### composeMatrix / decomposeMatrix

**组合**:
```cpp
mat4f composeMatrix(float3 translation, quatf rotation, float3 scale) {
    mat4f T = mat4f::translation(translation);
    mat4f R = mat4f(rotation);
    mat4f S = mat4f::scaling(scale);
    return T * R * S;
}
```

**分解**:
```cpp
void decomposeMatrix(const mat4f& matrix,
        float3* translation, quatf* rotation, float3* scale);
```

## 缓存机制

### MeshCache

**位置**: `FFilamentAsset::mMeshCache`

**结构**:
```cpp
using MeshCache = FixedCapacityVector<FixedCapacityVector<Primitive>>;
//                  ^^^^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^
//                  每个 cgltf_mesh     每个 cgltf_primitive
```

**索引**: `mesh - gltf->meshes` (指针偏移)

**好处**: O(1) 查找，避免重复创建

### MaterialInstanceCache

**位置**: AssetLoader 的临时成员（单次加载过程）

**结构**:
```cpp
FixedCapacityVector<Entry> mMaterialInstances;
FixedCapacityVector<Entry> mMaterialInstancesWithVertexColor;
```

**索引**: `material - gltf->materials`

**区分**: 有/无顶点颜色（影响 MaterialKey）

**生命周期**: createInstance() 调用期间

### TextureCache (ResourceLoader)

**BufferTextureCache**: 按内存地址缓存
**FilepathTextureCache**: 按文件路径缓存

**跨 Asset 共享**: 不支持（每次 loadResources 清空缓存）

## 两阶段遍历

### 第一阶段: recursePrimitives

**目的**: 创建共享的 GPU 资源

**遍历**: 深度优先遍历节点树

**操作**:
- 检查 `node->mesh`
- 调用 `createPrimitives()`
- 创建 VertexBuffer 和 IndexBuffer
- 缓存到 MeshCache

**不创建**: Entity, Transform, Renderable

### 第二阶段: recurseEntities

**目的**: 创建实例特定的资源

**遍历**: 深度优先遍历节点树

**操作**:
- 创建 Entity
- 创建 Transform 和 TrsTransform
- 创建 Renderable (引用 MeshCache 中的 VertexBuffer)
- 创建 MaterialInstance
- 创建 Light 和 Camera

### 为什么分两阶段？

**原因 1**: 共享资源只需创建一次
**原因 2**: MeshCache 必须在创建 Renderable 前完成
**原因 3**: 支持后续动态创建 Instance（只需第二阶段）

## BufferSlot 延迟绑定

### 设计动机

**问题**: VertexBuffer 创建时，数据可能还未加载

**解决方案**: 延迟绑定 - 先创建 VertexBuffer（空壳），后绑定数据

### 工作流程

```
1. AssetLoader::createPrimitive()
   ├─ 创建 VertexBuffer（空壳）
   ├─ 创建 BufferSlot { accessor, vertexBuffer, slot }
   └─ 将 BufferSlot 添加到 mBufferSlots

2. ResourceLoader::uploadBuffers()
   ├─ 遍历 mBufferSlots
   ├─ 从 accessor 读取数据（可能需要从网络下载）
   ├─ 创建 BufferObject
   ├─ BufferObject::setBuffer(data)
   └─ VertexBuffer::setBufferObjectAt(slot, bo)
```

### BufferSlot 结构

**代码位置**: `libs/gltfio/src/FFilamentAsset.h:327-336`

```cpp
struct BufferSlot {
    const cgltf_accessor* accessor;  // 指向数据源
    cgltf_attribute_type attribute;  // 属性类型
    int bufferIndex;                 // VertexBuffer 的 slot 索引
    VertexBuffer* vertexBuffer;
    IndexBuffer* indexBuffer;
    MorphTargetBuffer* morphTargetBuffer;
    uint32_t morphTargetOffset;
    uint32_t morphTargetCount;
};
```

## 依赖图和渐进式加载

### 问题

**场景**: 异步加载纹理，用户希望尽快看到模型

**挑战**: 如何知道一个实体的所有纹理都准备好了？

### 解决方案: DependencyGraph

**边的类型**:
```
Entity ─depends_on→ MaterialInstance
MaterialInstance ─depends_on→ Texture
```

**算法**: 拓扑排序 + 依赖计数

```cpp
struct Node {
    NodeType type;
    void* pointer;
    int incomingEdgeCount;  // 未满足的依赖数
    std::vector<Node*> outgoingEdges;
};
```

**标记就绪**:
```cpp
void markAsReady(Texture* texture) {
    Node* texNode = findNode(texture);

    // 遍历所有依赖此纹理的 MaterialInstance
    for (Node* miNode : texNode->outgoingEdges) {
        miNode->incomingEdgeCount--;

        if (miNode->incomingEdgeCount == 0) {
            // MaterialInstance 的所有纹理都就绪
            // 检查依赖此 MaterialInstance 的 Entity
            for (Node* entityNode : miNode->outgoingEdges) {
                entityNode->incomingEdgeCount--;

                if (entityNode->incomingEdgeCount == 0) {
                    // Entity 的所有 MaterialInstance 都就绪
                    readyQueue.push(entityNode->pointer);
                }
            }
        }
    }
}
```

## cgltf 集成

### 为什么选择 cgltf？

1. **单头文件**: 易于集成
2. **零拷贝**: 指针直接指向原始 JSON/二进制数据
3. **完整支持**: glTF 2.0 所有特性 + 扩展
4. **活跃维护**: 社区支持良好

### 生命周期管理

**挑战**: cgltf_data 必须在访问期间保持有效

**解决方案**: shared_ptr 引用计数

```cpp
struct SourceAsset {
    ~SourceAsset() { cgltf_free(hierarchy); }
    cgltf_data* hierarchy;
    // ...
};

using SourceHandle = std::shared_ptr<SourceAsset>;
```

**持有者**:
1. `FFilamentAsset::mSourceAsset`
2. `UploadEvent` (每个异步上传)

**释放**: 所有引用都释放后自动调用 `cgltf_free()`

### glbData 管理

**问题**: cgltf_parse 不持有输入数据，需要客户端保持

**解决方案**: 拷贝到 SourceAsset

```cpp
utils::FixedCapacityVector<uint8_t> glbdata(byteCount);
std::copy_n(bytes, byteCount, glbdata.data());

cgltf_parse(&options, glbdata.data(), byteCount, &sourceAsset);

// 转移所有权
glbdata.swap(fAsset->mSourceAsset->glbData);
```

## 扩展加载算法

### AssetLoaderExtended

**代码位置**: `libs/gltfio/src/extended/AssetLoaderExtended.h`

**目的**: 使用 mikktspace 算法计算切线

**平台限制**: 仅桌面（需要文件系统）

**启用**:
```cpp
AssetConfigurationExtended ext = {
    .gltfPath = "path/to/model.gltf"
};

AssetConfiguration config = {
    .engine = engine,
    .materials = materials,
    .ext = &ext
};
```

**差异**:
- 标准算法: 使用 Filament 的 TangentsJob (MikkTSpace 的简化版本)
- 扩展算法: 使用完整的 mikktspace 库

**实现**: 通过多态调用不同的 `createPrimitive()` 实现

## 错误处理策略

### 优雅降级

**原则**: 尽量加载，即使部分数据损坏

**示例**:
```cpp
// 纹理加载失败 → 使用默认纹理或跳过
if (!texture) {
    slog.e << "Unable to create texture, skipping." << io::endl;
    continue;
}

// 缺少包围盒 → 使用极大值
if (box.isEmpty()) {
    slog.w << "Missing bounding box, using default." << io::endl;
    box = Box().set(std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::max());
}
```

### 错误标志

**mError**: AssetLoader 的成员变量

```cpp
if (mError) {
    delete fAsset;
    return nullptr;
}
```

**用途**: 在多步骤过程中传播错误

## 平台抽象

### 文件系统

```cpp
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__) || defined(FILAMENT_IOS)
#define GLTFIO_USE_FILESYSTEM 0
#else
#define GLTFIO_USE_FILESYSTEM 1
#endif
```

**无文件系统平台**: 要求用户通过 `addResourceData()` 提供数据

### MaterialProvider 选择

**JitShaderProvider**: 桌面（有 filamat）
**UbershaderProvider**: 移动/Web（预编译 shader）

## 性能考虑

### 内存池

**FixedCapacityVector**: 预分配容量，避免重新分配

**使用场景**:
- mEntities
- mSkins
- mScenes
- mTextures

### 延迟计算

**包围盒**: 只在需要时计算（`recomputeBoundingBoxes()`）

**切线**: 只在 ResourceLoader 阶段计算

### 并行化

**切线计算**: 使用 JobSystem 并行计算

**纹理解码**: 多线程解码

**GPU 上传**: Filament backend 自动并行

## 总结

gltfio 的架构设计体现了以下原则：

1. **分离关注点**: Asset vs Instance, AssetLoader vs ResourceLoader
2. **延迟绑定**: BufferSlot, 纹理绑定
3. **缓存和复用**: MeshCache, MaterialInstanceCache, TextureCache
4. **引用计数**: SourceAsset shared_ptr
5. **渐进式加载**: DependencyGraph
6. **平台抽象**: 文件系统, MaterialProvider
7. **优雅降级**: 错误处理策略

这些设计使得 gltfio 能够高效、灵活地加载和渲染 glTF 资源。
