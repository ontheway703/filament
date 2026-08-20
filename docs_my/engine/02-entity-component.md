# Entity-Component 系统

本文档详细讲解 Filament 的 Entity-Component 系统设计，包括 Entity 的概念、ComponentManager 架构、各种组件类型，以及如何创建和管理场景对象。

---

## 什么是 Entity-Component 系统？

**Entity-Component System (ECS)** 是一种数据导向的架构模式，将游戏对象分解为：
- **Entity**: 唯一标识符（ID），代表一个对象
- **Component**: 数据容器，存储对象的属性
- **System**: 处理逻辑，操作具有特定组件的 Entity

### 传统 OOP vs ECS

```cpp
// ❌ 传统面向对象方式
class GameObject {
    Vector3 position;
    Quaternion rotation;
    Mesh mesh;
    Material material;
    Light light;  // 不是所有对象都有光源！
    
    void update() { /* ... */ }
    void render() { /* ... */ }
};

// 问题：
// 1. 继承层级复杂
// 2. 数据和逻辑耦合
// 3. 内存布局不友好（缓存miss）
// 4. 难以并行处理

// ✅ Filament ECS 方式
using Entity = uint32_t;  // 只是一个 ID

// 组件数据分离存储
TransformManager: [Entity -> Transform]
RenderableManager: [Entity -> Renderable]  
LightManager: [Entity -> Light]

// 优势：
// 1. 组合优于继承
// 2. 数据连续存储（缓存友好）
// 3. 易于并行处理
// 4. 灵活的对象组合
```

---

## Entity (实体)

### Entity 定义

在 Filament 中，Entity 只是一个 32 位整数 ID：

```cpp
// filament/include/filament/Entity.h
namespace utils {
    class Entity {
    public:
        using Type = uint32_t;
        
        constexpr Entity() noexcept : mIdentity(0) {}
        explicit constexpr Entity(Type identity) noexcept : mIdentity(identity) {}
        
        constexpr Type getId() const noexcept { return mIdentity; }
        
        constexpr bool operator==(Entity other) const noexcept {
            return mIdentity == other.mIdentity;
        }
        
    private:
        Type mIdentity;
    };
}

// 实际使用
using Entity = utils::Entity;
```

### EntityManager

EntityManager 负责创建和销毁 Entity：

```cpp
// 获取单例
EntityManager& em = EntityManager::get();

// 创建 Entity
Entity entity = em.create();
// 内部分配一个唯一 ID，例如 entity.getId() == 42

// 批量创建
size_t count = 100;
Entity* entities = em.create(count);

// 销毁 Entity
em.destroy(entity);

// 批量销毁
em.destroy(count, entities);

// 检查 Entity 是否有效
bool isAlive = em.isAlive(entity);
```

---

## ComponentManager 架构

### ComponentManager 基类

所有组件管理器都继承自 `SingleInstanceComponentManager`：

```cpp
template <typename COMPONENT_TYPE, typename MANAGER_TYPE>
class SingleInstanceComponentManager {
public:
    using Instance = /* 组件实例句柄 */;
    
    // 检查 Entity 是否有此组件
    bool hasComponent(Entity e) const noexcept;
    
    // 获取组件实例
    Instance getInstance(Entity e) const noexcept;
    
    // 销毁组件
    void destroy(Entity e) noexcept;
    
protected:
    // 内部存储
    std::vector<COMPONENT_TYPE> mComponents;
    std::unordered_map<Entity, Instance> mEntityToInstance;
};
```

### 内存布局

ComponentManager 使用结构体数组 (SoA) 而非数组结构体 (AoS)：

```cpp
// ❌ AoS (Array of Structures) - 不好
struct Component {
    Vector3 position;
    Quaternion rotation;
    Vector3 scale;
};
Component components[1000];  // 数据交错存储

// ✅ SoA (Structure of Arrays) - Filament 的做法
struct ComponentManager {
    Vector3 positions[1000];    // 连续存储
    Quaternion rotations[1000]; // 连续存储
    Vector3 scales[1000];       // 连续存储
};

// 优势：
// - 遍历 positions 时，数据连续，缓存命中率高
// - 易于 SIMD 优化
```

---

## TransformManager (变换组件)

### 作用

TransformManager 管理 Entity 的位置、旋转、缩放：

```cpp
TransformManager& tcm = engine->getTransformManager();

// 创建 Transform 组件
TransformManager::Instance ti = tcm.create(entity);

// 设置变换矩阵
mat4f transform = mat4f::translation(vec3(0, 1, 0)) *
                  mat4f::rotation(quat::fromAxisAngle(vec3(0, 1, 0), M_PI/4)) *
                  mat4f::scaling(vec3(2, 2, 2));

tcm.setTransform(ti, transform);

// 或者分别设置
tcm.setTransform(ti, mat4f::translation(vec3(0, 1, 0)));

// 获取变换
mat4f worldTransform = tcm.getWorldTransform(ti);
mat4f localTransform = tcm.getTransform(ti);
```

### 父子层级

TransformManager 支持父子关系，实现场景图：

```cpp
Entity parent = em.create();
Entity child = em.create();

TransformManager::Instance parentTi = tcm.create(parent);
TransformManager::Instance childTi = tcm.create(child);

// 设置父子关系
tcm.setParent(childTi, parentTi);

// 父节点变换
tcm.setTransform(parentTi, mat4f::translation(vec3(10, 0, 0)));

// 子节点本地变换
tcm.setTransform(childTi, mat4f::translation(vec3(0, 5, 0)));

// 子节点世界变换 = 父变换 * 子本地变换
// 结果：世界位置为 (10, 5, 0)
mat4f childWorldTransform = tcm.getWorldTransform(childTi);
```

---

## RenderableManager (可渲染组件)

### 作用

RenderableManager 管理可渲染的几何体：

```cpp
RenderableManager& rm = engine->getRenderableManager();

// 创建 Renderable 组件（使用 Builder）
RenderableManager::Builder(1)  // 1 个图元
    .boundingBox({{-1, -1, -1}, {1, 1, 1}})
    .material(0, materialInstance)
    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,
              vertexBuffer, indexBuffer)
    .culling(true)
    .receiveShadows(true)
    .castShadows(true)
    .build(*engine, entity);

// 检查是否有 Renderable
bool hasRenderable = rm.hasComponent(entity);

// 获取实例
RenderableManager::Instance ri = rm.getInstance(entity);

// 更新材质
rm.setMaterialInstanceAt(ri, 0, newMaterialInstance);

// 销毁
rm.destroy(entity);
```

### Renderable Builder 参数

| 参数 | 说明 | 必需 |
|------|------|------|
| **geometry()** | 几何数据（VB/IB） | ✅ |
| **material()** | 材质实例 | ✅ |
| **boundingBox()** | 包围盒（用于剔除） | ✅ |
| **culling()** | 是否参与剔除 | ❌ (默认true) |
| **castShadows()** | 是否投射阴影 | ❌ (默认false) |
| **receiveShadows()** | 是否接收阴影 | ❌ (默认false) |
| **priority()** | 渲染优先级 | ❌ (默认4) |
| **skinning()** | 骨骼蒙皮 | ❌ |
| **morphing()** | 变形目标 | ❌ |

---

## LightManager (光照组件)

### 作用

LightManager 管理光源：

```cpp
LightManager& lm = engine->getLightManager();

// 创建方向光
LightManager::Builder(LightManager::Type::DIRECTIONAL)
    .color(Color::toLinear<ACCURATE>(sRGBColor(0.98, 0.92, 0.89)))
    .intensity(100000)  // 太阳光强度（lux）
    .direction({0, -1, 0})
    .castShadows(true)
    .build(*engine, sunEntity);

// 创建点光源
LightManager::Builder(LightManager::Type::POINT)
    .color({1.0f, 0.8f, 0.6f})
    .intensity(10000)  // 光通量（lumen）
    .position({0, 2, 0})
    .falloff(10.0f)  // 衰减距离
    .build(*engine, lampEntity);

// 创建聚光灯
LightManager::Builder(LightManager::Type::SPOT)
    .color({1.0f, 1.0f, 1.0f})
    .intensity(5000)
    .position({0, 3, 0})
    .direction({0, -1, 0})
    .spotLightCone(M_PI/6, M_PI/4)  // 内角、外角
    .build(*engine, spotEntity);
```

### 光源类型

| 类型 | 说明 | 参数 |
|------|------|------|
| **DIRECTIONAL** | 方向光（太阳） | direction, intensity, color |
| **POINT** | 点光源（灯泡） | position, intensity, falloff |
| **SPOT** | 聚光灯 | position, direction, cone angles |
| **FOCUSED_SPOT** | 聚焦聚光灯 | + 聚焦参数 |

---

## CameraManager (相机组件)

虽然 Camera 不通过 Builder 创建，但它也是一种组件：

```cpp
// 创建相机 Entity
Entity cameraEntity = em.create();

// 创建 Camera 组件
Camera* camera = engine->createCamera(cameraEntity);

// 配置投影
camera->setProjection(45.0, aspect, 0.1, 100.0);

// 设置相机位置（通过 TransformManager）
TransformManager::Instance ti = tcm.create(cameraEntity);
tcm.setTransform(ti, mat4f::lookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0}));

// 获取 Camera 的 Entity
Entity e = camera->getEntity();
```

---

## 完整示例：创建场景对象

### 示例1：创建一个立方体

```cpp
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>

using namespace filament;

void createCube(Engine* engine, Scene* scene) {
    // 1. 创建 Entity
    Entity cube = EntityManager::get().create();
    
    // 2. 创建顶点和索引缓冲
    VertexBuffer* vb = /* ... */;
    IndexBuffer* ib = /* ... */;
    MaterialInstance* material = /* ... */;
    
    // 3. 添加 Renderable 组件
    RenderableManager::Builder(1)
        .boundingBox({{-1, -1, -1}, {1, 1, 1}})
        .material(0, material)
        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb, ib)
        .culling(true)
        .build(*engine, cube);
    
    // 4. 添加 Transform 组件
    TransformManager& tcm = engine->getTransformManager();
    TransformManager::Instance ti = tcm.create(cube);
    tcm.setTransform(ti, mat4f::translation(vec3(0, 0, 0)));
    
    // 5. 添加到场景
    scene->addEntity(cube);
}
```

### 示例2：创建光源

```cpp
void createLight(Engine* engine, Scene* scene) {
    // 1. 创建 Entity
    Entity light = EntityManager::get().create();
    
    // 2. 添加 Light 组件
    LightManager::Builder(LightManager::Type::POINT)
        .color({1.0f, 1.0f, 1.0f})
        .intensity(10000)
        .position({0, 5, 0})
        .falloff(10.0f)
        .build(*engine, light);
    
    // 3. 添加到场景
    scene->addEntity(light);
}
```

### 示例3：父子层级

```cpp
void createHierarchy(Engine* engine, Scene* scene) {
    EntityManager& em = EntityManager::get();
    TransformManager& tcm = engine->getTransformManager();
    
    // 父对象（车体）
    Entity car = em.create();
    TransformManager::Instance carTi = tcm.create(car);
    tcm.setTransform(carTi, mat4f::translation(vec3(0, 0, 0)));
    // ... 添加 Renderable
    scene->addEntity(car);
    
    // 子对象（车轮）
    Entity wheel1 = em.create();
    TransformManager::Instance wheel1Ti = tcm.create(wheel1);
    tcm.setParent(wheel1Ti, carTi);  // 设置父子关系
    tcm.setTransform(wheel1Ti, mat4f::translation(vec3(-1, -0.5, 1)));
    // ... 添加 Renderable
    scene->addEntity(wheel1);
    
    // 更多车轮...
    
    // 移动车体时，所有车轮自动跟随
    tcm.setTransform(carTi, mat4f::translation(vec3(10, 0, 0)));
}
```

---

## 组件的生命周期

### 创建顺序

推荐的创建顺序：

```cpp
// 1. 创建 Entity
Entity entity = EntityManager::get().create();

// 2. 创建 Transform（如果需要）
TransformManager::Instance ti = tcm.create(entity);

// 3. 创建其他组件（Renderable、Light等）
RenderableManager::Builder(1)
    ./* ... */
    .build(*engine, entity);

// 4. 添加到 Scene
scene->addEntity(entity);
```

### 销毁顺序

推荐的销毁顺序：

```cpp
// 1. 从 Scene 移除
scene->remove(entity);

// 2. 销毁各个组件
engine->destroy(renderable);
engine->destroy(light);
// Transform 会自动销毁

// 3. 销毁 Entity
EntityManager::get().destroy(entity);
```

---

## 高级特性

### 组件迭代

批量处理具有特定组件的 Entity：

```cpp
// 遍历所有 Renderable
auto& rm = engine->getRenderableManager();
for (size_t i = 0; i < rm.getComponentCount(); i++) {
    Entity e = rm.getEntity(i);
    RenderableManager::Instance ri = rm.getInstance(e);
    
    // 处理 Renderable
    // ...
}
```

### 组件查询

```cpp
// 查询具有 Transform 和 Renderable 的 Entity
auto& tcm = engine->getTransformManager();
auto& rm = engine->getRenderableManager();

for (Entity entity : getAllEntities()) {
    if (tcm.hasComponent(entity) && rm.hasComponent(entity)) {
        // 同时具有两个组件
        TransformManager::Instance ti = tcm.getInstance(entity);
        RenderableManager::Instance ri = rm.getInstance(entity);
        
        // 处理...
    }
}
```

---

## 性能考虑

### 内存布局

```cpp
// SoA 布局的优势
// 遍历所有 Transform 的位置：
for (size_t i = 0; i < count; i++) {
    Vector3 pos = positions[i];  // 连续访问，缓存友好
    // 处理位置...
}

// 而不是：
for (size_t i = 0; i < count; i++) {
    Transform& t = transforms[i];
    Vector3 pos = t.position;  // 可能跳过 rotation、scale 字段，缓存浪费
}
```

### 批量创建

```cpp
// ✅ 批量创建（高效）
size_t count = 1000;
Entity* entities = EntityManager::get().create(count);

for (size_t i = 0; i < count; i++) {
    RenderableManager::Builder(1)
        ./* ... */
        .build(*engine, entities[i]);
}

// ❌ 逐个创建（低效）
for (size_t i = 0; i < 1000; i++) {
    Entity e = EntityManager::get().create();
    RenderableManager::Builder(1)
        ./* ... */
        .build(*engine, e);
}
```

---

## 与其他引擎对比

| 引擎 | Entity 类型 | 组件方式 | 特点 |
|------|-----------|---------|------|
| **Filament** | uint32_t ID | ComponentManager | 纯 ECS |
| **Unity (ECS)** | Entity struct | IComponentData | 纯 ECS，Job System |
| **Unity (传统)** | GameObject class | MonoBehaviour | OOP |
| **Unreal** | AActor class | UActorComponent | OOP + Component |

---

## 相关文档

- **[01-core-concepts.md](01-core-concepts.md)**: Engine 核心概念
- **[03-resource-management.md](03-resource-management.md)**: 资源管理
- **[04-renderable-system.md](04-renderable-system.md)**: Renderable 详解
- **[08-scene-graph.md](08-scene-graph.md)**: 场景图和变换

---

## 总结

Filament 的 Entity-Component 系统通过**数据导向设计**、**SoA 内存布局**、**灵活的组件组合**，实现了高性能、缓存友好的场景管理架构。

**核心优势**:
- ✅ 组合优于继承，灵活性高
- ✅ 数据连续存储，缓存友好
- ✅ 易于并行处理和优化
- ✅ 简洁的 API
