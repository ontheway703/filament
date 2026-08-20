# 场景图和变换系统

## 概述

Filament 使用 **TransformManager** 实现层级化的场景图（Scene Graph），管理实体的位置、旋转、缩放以及它们之间的父子关系。与传统的面向对象场景图不同，Filament 采用基于组件的扁平化设计，同时提供层级变换功能。

### 场景图架构

```
Scene (容器)
   |
   +-- Entity (Root)
   |     |
   |     +-- Transform Component
   |     +-- Renderable Component
   |     |
   |     +-- Child Entity
   |           |
   |           +-- Transform Component (继承父变换)
   |           +-- Renderable Component
   |
   +-- Entity (另一个根节点)
         |
         +-- ...
```

**核心概念：**
- **Entity**：场景中的对象，本身无数据
- **Transform**：TransformManager 管理的变换组件
- **Hierarchy**：通过 parent/child 关系建立层级
- **Local vs World**：局部变换 vs 世界变换

---

## TransformManager 基础

### 添加变换组件

```cpp
#include <filament/TransformManager.h>

Entity entity = EntityManager::get().create();

// 方式 1: 创建时添加变换
auto& tcm = engine->getTransformManager();
tcm.create(entity);

// 方式 2: 带初始变换创建
tcm.create(entity, mat4f::translation(float3{0, 1, 0}));

// 方式 3: 带父节点创建
Entity parent = ...;
auto parentInstance = tcm.getInstance(parent);
tcm.create(entity, parentInstance, mat4f::identity());
```

### 变换操作

```cpp
auto& tcm = engine->getTransformManager();
auto instance = tcm.getInstance(entity);

// 1. 设置局部变换矩阵
mat4f transform = mat4f::translation({0, 1, 0}) *
                  mat4f::rotation(M_PI/4, {0, 1, 0}) *
                  mat4f::scaling({2, 2, 2});
tcm.setTransform(instance, transform);

// 2. 获取变换
mat4f localTransform = tcm.getTransform(instance);
mat4f worldTransform = tcm.getWorldTransform(instance);

// 3. TRS 分量操作（便捷方法）
// 注意：Filament 没有直接的 setPosition 等方法，需手动构建矩阵
```

### 矩阵构建

Filament 的 math 库提供矩阵构建工具：

```cpp
#include <math/mat4.h>
#include <math/quat.h>

using namespace filament::math;

// 平移矩阵
mat4f T = mat4f::translation(float3{x, y, z});

// 旋转矩阵
mat4f R1 = mat4f::rotation(angleRadians, float3{0, 1, 0});  // 轴角
quatf q = quatf::fromAxisAngle(float3{0, 1, 0}, angleRadians);
mat4f R2 = mat4f(q);  // 四元数转矩阵

// 缩放矩阵
mat4f S = mat4f::scaling(float3{sx, sy, sz});

// 组合变换 (顺序：Scale -> Rotate -> Translate)
mat4f M = T * R * S;
tcm.setTransform(instance, M);
```

---

## 层级变换

### 父子关系

```cpp
// 创建父节点
Entity parent = EntityManager::get().create();
auto& tcm = engine->getTransformManager();
tcm.create(parent);
tcm.setTransform(tcm.getInstance(parent),
                 mat4f::translation({0, 0, 0}));

// 创建子节点
Entity child = EntityManager::get().create();
tcm.create(child, tcm.getInstance(parent));  // 指定父节点
tcm.setTransform(tcm.getInstance(child),
                 mat4f::translation({1, 0, 0}));  // 相对父节点

// 世界位置 = parent.world * child.local
//          = (0,0,0) + (1,0,0) = (1,0,0)
```

### 动态重新父化

```cpp
// 修改父节点
Entity newParent = ...;
tcm.setParent(tcm.getInstance(child),
              tcm.getInstance(newParent));

// 移除父节点（成为根节点）
tcm.setParent(tcm.getInstance(child), 0);  // 0 表示无父节点
```

### 层级遍历

```cpp
// 获取层级信息
auto instance = tcm.getInstance(entity);

// 获取父节点
auto parent = tcm.getParent(instance);

// 获取子节点数量
size_t childCount = tcm.getChildCount(instance);

// 获取所有子节点
std::vector<Entity> children;
auto* childInstances = tcm.getChildren(instance, &childCount);
for (size_t i = 0; i < childCount; ++i) {
    Entity child = tcm.getEntity(childInstances[i]);
    children.push_back(child);
}
```

**递归遍历示例：**

```cpp
void traverseHierarchy(TransformManager& tcm,
                       TransformManager::Instance node,
                       int depth = 0) {
    Entity entity = tcm.getEntity(node);

    // 处理当前节点
    std::string indent(depth * 2, ' ');
    std::cout << indent << "Entity: " << entity.getId() << std::endl;

    mat4f worldTransform = tcm.getWorldTransform(node);
    float3 worldPos = worldTransform[3].xyz;
    std::cout << indent << "  World Pos: " << worldPos << std::endl;

    // 递归处理子节点
    size_t childCount;
    auto* children = tcm.getChildren(node, &childCount);
    for (size_t i = 0; i < childCount; ++i) {
        traverseHierarchy(tcm, children[i], depth + 1);
    }
}

// 使用
auto& tcm = engine->getTransformManager();
auto rootInstance = tcm.getInstance(rootEntity);
traverseHierarchy(tcm, rootInstance);
```

---

## 实用变换封装

由于 Filament 没有提供高级的 Transform API，通常会封装一个辅助类：

### Transform 辅助类

```cpp
#include <filament/TransformManager.h>
#include <math/vec3.h>
#include <math/quat.h>
#include <math/mat4.h>

class Transform {
public:
    Transform(Engine* engine, Entity entity)
        : engine(engine), entity(entity) {
        auto& tcm = engine->getTransformManager();
        instance = tcm.getInstance(entity);
    }

    // === 位置 ===
    void setPosition(const float3& pos) {
        position = pos;
        updateMatrix();
    }

    float3 getPosition() const {
        return position;
    }

    float3 getWorldPosition() const {
        auto& tcm = engine->getTransformManager();
        mat4f worldTransform = tcm.getWorldTransform(instance);
        return worldTransform[3].xyz;
    }

    // === 旋转 ===
    void setRotation(const quatf& rot) {
        rotation = rot;
        updateMatrix();
    }

    void setRotation(const float3& eulerAngles) {
        // Euler angles (degrees) to quaternion
        quatf qx = quatf::fromAxisAngle({1, 0, 0}, radians(eulerAngles.x));
        quatf qy = quatf::fromAxisAngle({0, 1, 0}, radians(eulerAngles.y));
        quatf qz = quatf::fromAxisAngle({0, 0, 1}, radians(eulerAngles.z));
        rotation = qz * qy * qx;
        updateMatrix();
    }

    quatf getRotation() const {
        return rotation;
    }

    // === 缩放 ===
    void setScale(const float3& scl) {
        scale = scl;
        updateMatrix();
    }

    void setScale(float uniformScale) {
        scale = float3{uniformScale};
        updateMatrix();
    }

    float3 getScale() const {
        return scale;
    }

    // === 方向向量 ===
    float3 forward() const {
        return rotation * float3{0, 0, -1};  // -Z 是前方
    }

    float3 up() const {
        return rotation * float3{0, 1, 0};
    }

    float3 right() const {
        return rotation * float3{1, 0, 0};
    }

    // === 变换操作 ===
    void translate(const float3& delta) {
        position += delta;
        updateMatrix();
    }

    void rotate(const quatf& delta) {
        rotation = delta * rotation;
        updateMatrix();
    }

    void rotateAround(const float3& point, const float3& axis, float angle) {
        // 1. 移动到原点
        position -= point;

        // 2. 旋转
        quatf q = quatf::fromAxisAngle(axis, angle);
        position = q * position;
        rotation = q * rotation;

        // 3. 移回
        position += point;

        updateMatrix();
    }

    void lookAt(const float3& target, const float3& up = {0, 1, 0}) {
        float3 forward = normalize(target - position);
        float3 right = normalize(cross(up, forward));
        float3 newUp = cross(forward, right);

        mat3f rotationMatrix;
        rotationMatrix[0] = right;
        rotationMatrix[1] = newUp;
        rotationMatrix[2] = -forward;  // Filament uses -Z forward

        rotation = quatf::fromMatrix(rotationMatrix);
        updateMatrix();
    }

    // === 层级 ===
    void setParent(Entity parent) {
        auto& tcm = engine->getTransformManager();
        auto parentInstance = tcm.getInstance(parent);
        tcm.setParent(instance, parentInstance);
    }

    void setParent(const Transform& parent) {
        auto& tcm = engine->getTransformManager();
        tcm.setParent(instance, parent.instance);
    }

    Entity getParent() const {
        auto& tcm = engine->getTransformManager();
        auto parentInstance = tcm.getParent(instance);
        return tcm.getEntity(parentInstance);
    }

private:
    void updateMatrix() {
        mat4f T = mat4f::translation(position);
        mat4f R = mat4f(rotation);
        mat4f S = mat4f::scaling(scale);

        mat4f localTransform = T * R * S;

        auto& tcm = engine->getTransformManager();
        tcm.setTransform(instance, localTransform);
    }

    Engine* engine;
    Entity entity;
    TransformManager::Instance instance;

    float3 position = {0, 0, 0};
    quatf rotation = quatf();  // 单位四元数
    float3 scale = {1, 1, 1};
};

// 使用示例
Entity cube = createCube(engine);
Transform cubeTransform(engine, cube);

cubeTransform.setPosition({5, 2, 0});
cubeTransform.setRotation({0, 45, 0});  // Euler angles
cubeTransform.setScale(2.0f);

// 每帧旋转
cubeTransform.rotate(quatf::fromAxisAngle({0, 1, 0}, deltaTime));
```

---

## 场景图构建示例

### 人物骨骼层级

```cpp
struct Character {
    Entity root;
    Entity spine;
    Entity head;
    Entity leftArm;
    Entity rightArm;
    Entity leftLeg;
    Entity rightLeg;

    static Character create(Engine* engine) {
        Character character;
        auto& tcm = engine->getTransformManager();

        // 根节点
        character.root = EntityManager::get().create();
        tcm.create(character.root);

        // 脊柱
        character.spine = EntityManager::get().create();
        tcm.create(character.spine, tcm.getInstance(character.root));
        tcm.setTransform(tcm.getInstance(character.spine),
                        mat4f::translation({0, 1, 0}));

        // 头部
        character.head = EntityManager::get().create();
        tcm.create(character.head, tcm.getInstance(character.spine));
        tcm.setTransform(tcm.getInstance(character.head),
                        mat4f::translation({0, 0.5, 0}));

        // 左臂
        character.leftArm = EntityManager::get().create();
        tcm.create(character.leftArm, tcm.getInstance(character.spine));
        tcm.setTransform(tcm.getInstance(character.leftArm),
                        mat4f::translation({-0.5, 0.3, 0}));

        // 右臂
        character.rightArm = EntityManager::get().create();
        tcm.create(character.rightArm, tcm.getInstance(character.spine));
        tcm.setTransform(tcm.getInstance(character.rightArm),
                        mat4f::translation({0.5, 0.3, 0}));

        // ... 腿部类似

        return character;
    }

    void wave(float time) {
        auto& tcm = engine->getTransformManager();

        // 右臂挥手动画
        float angle = sin(time * 2.0f) * M_PI / 4;  // -45° 到 45°
        mat4f armRotation = mat4f::translation({0.5, 0.3, 0}) *
                            mat4f::rotation(angle, {0, 0, 1});
        tcm.setTransform(tcm.getInstance(rightArm), armRotation);
    }
};
```

### 太阳系示例

```cpp
class SolarSystem {
public:
    SolarSystem(Engine* engine, Scene* scene)
        : engine(engine), scene(scene) {
        createHierarchy();
    }

    void update(float time) {
        auto& tcm = engine->getTransformManager();

        // 地球公转
        float earthOrbitAngle = time * 0.5f;  // rad/s
        mat4f earthOrbit = mat4f::rotation(earthOrbitAngle, {0, 1, 0}) *
                           mat4f::translation({10, 0, 0});
        tcm.setTransform(tcm.getInstance(earthOrbitNode), earthOrbit);

        // 地球自转
        float earthRotAngle = time * 2.0f;
        mat4f earthRot = mat4f::rotation(earthRotAngle, {0, 1, 0});
        tcm.setTransform(tcm.getInstance(earthEntity), earthRot);

        // 月球公转（围绕地球）
        float moonOrbitAngle = time * 1.0f;
        mat4f moonOrbit = mat4f::rotation(moonOrbitAngle, {0, 1, 0}) *
                          mat4f::translation({2, 0, 0});
        tcm.setTransform(tcm.getInstance(moonEntity), moonOrbit);
    }

private:
    void createHierarchy() {
        auto& tcm = engine->getTransformManager();

        // 太阳（根节点）
        sunEntity = createSphere(engine, 2.0f);
        tcm.create(sunEntity);
        scene->addEntity(sunEntity);

        // 地球轨道节点（不可见）
        earthOrbitNode = EntityManager::get().create();
        tcm.create(earthOrbitNode, tcm.getInstance(sunEntity));

        // 地球
        earthEntity = createSphere(engine, 1.0f);
        tcm.create(earthEntity, tcm.getInstance(earthOrbitNode));
        scene->addEntity(earthEntity);

        // 月球
        moonEntity = createSphere(engine, 0.3f);
        tcm.create(moonEntity, tcm.getInstance(earthEntity));
        scene->addEntity(moonEntity);
    }

    Engine* engine;
    Scene* scene;
    Entity sunEntity;
    Entity earthOrbitNode;
    Entity earthEntity;
    Entity moonEntity;
};
```

---

## 世界空间和局部空间转换

### 坐标空间转换

```cpp
class CoordinateTransform {
public:
    // 局部坐标 -> 世界坐标
    static float3 localToWorld(const TransformManager& tcm,
                               TransformManager::Instance instance,
                               const float3& localPoint) {
        mat4f worldTransform = tcm.getWorldTransform(instance);
        return (worldTransform * float4(localPoint, 1.0f)).xyz;
    }

    // 世界坐标 -> 局部坐标
    static float3 worldToLocal(const TransformManager& tcm,
                               TransformManager::Instance instance,
                               const float3& worldPoint) {
        mat4f worldTransform = tcm.getWorldTransform(instance);
        mat4f invWorld = inverse(worldTransform);
        return (invWorld * float4(worldPoint, 1.0f)).xyz;
    }

    // 局部方向 -> 世界方向
    static float3 localDirToWorld(const TransformManager& tcm,
                                  TransformManager::Instance instance,
                                  const float3& localDir) {
        mat4f worldTransform = tcm.getWorldTransform(instance);
        return normalize((worldTransform * float4(localDir, 0.0f)).xyz);
    }

    // 世界方向 -> 局部方向
    static float3 worldDirToLocal(const TransformManager& tcm,
                                  TransformManager::Instance instance,
                                  const float3& worldDir) {
        mat4f worldTransform = tcm.getWorldTransform(instance);
        mat4f invWorld = inverse(worldTransform);
        return normalize((invWorld * float4(worldDir, 0.0f)).xyz);
    }
};

// 使用示例：射线检测
bool rayIntersectEntity(const Ray& worldRay, Entity entity) {
    auto& tcm = engine->getTransformManager();
    auto instance = tcm.getInstance(entity);

    // 将世界空间射线转换到局部空间
    float3 localOrigin = CoordinateTransform::worldToLocal(
        tcm, instance, worldRay.origin
    );
    float3 localDir = CoordinateTransform::worldDirToLocal(
        tcm, instance, worldRay.direction
    );

    // 在局部空间进行碰撞检测（例如与单位球）
    return intersectSphere(localOrigin, localDir, 1.0f);
}
```

---

## 性能优化

### 批量更新

```cpp
// 避免频繁更新变换
class TransformBatch {
    std::vector<Entity> entities;
    std::vector<mat4f> transforms;
    bool dirty = false;

public:
    void setTransform(Entity entity, const mat4f& transform) {
        // 缓存变换
        auto it = std::find(entities.begin(), entities.end(), entity);
        if (it == entities.end()) {
            entities.push_back(entity);
            transforms.push_back(transform);
        } else {
            transforms[it - entities.begin()] = transform;
        }
        dirty = true;
    }

    void flush(Engine* engine) {
        if (!dirty) return;

        auto& tcm = engine->getTransformManager();
        for (size_t i = 0; i < entities.size(); ++i) {
            auto instance = tcm.getInstance(entities[i]);
            tcm.setTransform(instance, transforms[i]);
        }

        dirty = false;
    }
};
```

### 静态物体优化

```cpp
// 标记静态物体（减少矩阵更新）
// Filament 会自动缓存不变的世界矩阵

// 如果一个对象永远不会移动，考虑：
// 1. 不创建 Transform 组件（直接烘焙到顶点）
// 2. 或者创建后永不修改
```

### 层级深度控制

```cpp
// 避免过深的层级（每层需要矩阵乘法）
// 推荐最大深度：10-15 层

// 扁平化优化：
// 不良：Root -> A -> B -> C -> D -> E -> F (深度 6)
// 优化：Root -> A, B, C, D, E, F (深度 1，手动计算世界变换)
```

---

## 完整示例：动态场景管理

```cpp
class SceneGraph {
public:
    SceneGraph(Engine* engine, Scene* scene)
        : engine(engine), scene(scene) {}

    // 创建节点（带自动场景注册）
    Entity createNode(const std::string& name,
                      Entity parent = Entity()) {
        Entity entity = EntityManager::get().create();
        auto& tcm = engine->getTransformManager();

        if (parent.isNull()) {
            tcm.create(entity);
        } else {
            tcm.create(entity, tcm.getInstance(parent));
        }

        nodeNames[entity] = name;
        scene->addEntity(entity);

        return entity;
    }

    // 查找节点
    Entity findNode(const std::string& name) {
        for (const auto& [entity, nodeName] : nodeNames) {
            if (nodeName == name) return entity;
        }
        return Entity();
    }

    // 设置节点变换（TRS）
    void setNodeTransform(Entity entity,
                         const float3& position,
                         const quatf& rotation,
                         const float3& scale) {
        Transform(engine, entity).setPosition(position);
        Transform(engine, entity).setRotation(rotation);
        Transform(engine, entity).setScale(scale);
    }

    // 动画节点
    void animateNode(Entity entity, float time) {
        Transform transform(engine, entity);

        // 示例：圆周运动
        float angle = time;
        float radius = 5.0f;
        float3 position = {
            cos(angle) * radius,
            sin(time * 2.0f),
            sin(angle) * radius
        };

        transform.setPosition(position);
        transform.setRotation(quatf::fromAxisAngle({0, 1, 0}, angle));
    }

    // 销毁节点及其子节点
    void destroyNode(Entity entity) {
        auto& tcm = engine->getTransformManager();
        auto instance = tcm.getInstance(entity);

        // 递归销毁子节点
        size_t childCount;
        auto* children = tcm.getChildren(instance, &childCount);
        for (size_t i = 0; i < childCount; ++i) {
            Entity child = tcm.getEntity(children[i]);
            destroyNode(child);  // 递归
        }

        // 销毁当前节点
        scene->remove(entity);
        engine->destroy(entity);
        nodeNames.erase(entity);
    }

    // 调试打印层级
    void printHierarchy() {
        auto& tcm = engine->getTransformManager();

        // 查找所有根节点
        for (const auto& [entity, name] : nodeNames) {
            auto instance = tcm.getInstance(entity);
            if (tcm.getParent(instance) == 0) {  // 根节点
                printNode(entity, 0);
            }
        }
    }

private:
    void printNode(Entity entity, int depth) {
        auto& tcm = engine->getTransformManager();
        auto instance = tcm.getInstance(entity);

        std::string indent(depth * 2, ' ');
        std::cout << indent << nodeNames[entity] << std::endl;

        size_t childCount;
        auto* children = tcm.getChildren(instance, &childCount);
        for (size_t i = 0; i < childCount; ++i) {
            Entity child = tcm.getEntity(children[i]);
            printNode(child, depth + 1);
        }
    }

    Engine* engine;
    Scene* scene;
    std::unordered_map<Entity, std::string> nodeNames;
};

// 使用示例
SceneGraph graph(engine, scene);

Entity root = graph.createNode("Root");
Entity car = graph.createNode("Car", root);
Entity wheel1 = graph.createNode("Wheel_FL", car);
Entity wheel2 = graph.createNode("Wheel_FR", car);
Entity wheel3 = graph.createNode("Wheel_RL", car);
Entity wheel4 = graph.createNode("Wheel_RR", car);

graph.setNodeTransform(car, {0, 0, 0}, quatf(), {1, 1, 1});
graph.setNodeTransform(wheel1, {-1, -0.5, 1.5}, quatf(), {0.5, 0.5, 0.5});

// 每帧更新
graph.animateNode(car, currentTime);

// 调试
graph.printHierarchy();
// 输出：
// Root
//   Car
//     Wheel_FL
//     Wheel_FR
//     Wheel_RL
//     Wheel_RR
```

---

## 与 glTF 场景集成

### 加载 glTF 场景图

```cpp
#include <gltfio/AssetLoader.h>
#include <gltfio/ResourceLoader.h>

FilamentAsset* asset = assetLoader->createAsset(gltfData, gltfSize);
resourceLoader->loadResources(asset);

// glTF 的节点层级自动映射到 TransformManager
const Entity* entities = asset->getEntities();
size_t entityCount = asset->getEntityCount();

// 访问特定节点
const char* nodeName = asset->getName(entities[0]);
auto& tcm = engine->getTransformManager();
auto instance = tcm.getInstance(entities[0]);
mat4f transform = tcm.getWorldTransform(instance);

// 动画驱动节点变换
Animator* animator = asset->getAnimator();
animator->applyAnimation(0, currentTime);
animator->updateBoneMatrices();  // 更新所有变换
```

---

## 总结

### TransformManager 核心功能

1. **组件化设计**：Transform 作为独立组件附加到 Entity
2. **层级变换**：支持父子关系，自动计算世界变换
3. **高效更新**：扁平化存储，缓存友好
4. **ECS 兼容**：与其他 Manager（Renderable, Light）无缝协作

### 最佳实践

```cpp
// ✓ 推荐
Transform transform(engine, entity);
transform.setPosition({1, 2, 3});
transform.rotate(quatf::fromAxisAngle({0, 1, 0}, deltaTime));

// ✗ 避免
// 每帧手动构建完整矩阵（低效）
tcm.setTransform(instance, mat4f::translation(...) * mat4f::rotation(...) * ...);
```

### 相关文档

- **[01-core-concepts.md](./01-core-concepts.md)** - Entity 和组件基础
- **[02-entity-component.md](./02-entity-component.md)** - ECS 架构详解
- **[04-renderable-system.md](./04-renderable-system.md)** - Renderable 与 Transform 交互
- **[../gltfio/01-asset-structure.md](../gltfio/01-asset-structure.md)** - glTF 场景图加载

通过理解 TransformManager 和场景图系统,您可以构建复杂的层级化 3D 场景，实现骨骼动画、粒子系统、UI 布局等高级功能。
