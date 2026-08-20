# Filament ECS 最佳实践

## 📖 概述

本文档深入介绍 Filament 的 Entity-Component-System (ECS) 架构，以及如何高效使用 Manager 模式管理场景对象。Filament 的 ECS 设计注重性能和缓存友好性。

**核心概念**：
- **Entity**: 轻量级标识符（32位整数）
- **Component**: 数据容器（SoA布局）
- **Manager**: 组件管理器（RenderableManager、TransformManager等）
- **System**: 处理逻辑（通常在Manager中实现）

**设计原则**：
1. **数据导向**：组件数据紧密排列，缓存友好
2. **类型安全**：编译期检查组件类型
3. **高性能**：SoA布局优化内存访问模式
4. **可扩展**：易于添加新组件类型

---

## 1. ECS架构概述

### 1.1 Entity系统

Entity是场景中对象的唯一标识符，本身不包含数据，只是一个ID。

```cpp
// Entity只是一个32位整数
using Entity = uint32_t;

// Entity Manager负责分配和回收Entity
class EntityManager {
public:
    Entity create() {
        // 分配新Entity（从空闲列表或递增）
        if (!mFreeList.empty()) {
            Entity e = mFreeList.back();
            mFreeList.pop_back();
            return e;
        }
        return mNextEntity++;
    }

    void destroy(Entity e) {
        // 标记为可重用
        mFreeList.push_back(e);
    }

private:
    Entity mNextEntity = 0;
    std::vector<Entity> mFreeList;
};
```

### 1.2 Component Manager模式

Filament使用Manager模式管理组件，每种组件类型对应一个Manager。

**主要Manager**：
- **TransformManager**: 管理空间变换（位置、旋转、缩放）
- **RenderableManager**: 管理可渲染对象（网格、材质）
- **LightManager**: 管理光源
- **CameraManager**: 管理相机

### 1.3 SoA数据布局

Manager内部使用Structure of Arrays (SoA)布局存储组件数据，优化缓存命中率。

```cpp
// AoS布局（不好）- 数据分散
struct TransformAoS {
    Entity entity;
    mat4f matrix;
    vec3 position;
    quat rotation;
    vec3 scale;
};
std::vector<TransformAoS> transforms;  // 访问position时加载了很多无用数据

// SoA布局（好）- 数据紧密排列  
struct TransformSoA {
    std::vector<Entity> entities;
    std::vector<mat4f> matrices;
    std::vector<vec3> positions;
    std::vector<quat> rotations;
    std::vector<vec3> scales;
};
// 访问所有position时，数据连续，缓存友好！
```

---

## 2. TransformManager详解

TransformManager管理场景中所有对象的空间变换。

### 2.1 层次结构

TransformManager支持父子层次结构，子对象继承父对象的变换。

```cpp
class TransformManager {
public:
    // 创建Transform组件实例
    Instance create(Entity entity) {
        Instance instance = allocateInstance();
        mEntities[instance] = entity;
        mLocalTransforms[instance] = mat4f();
        mWorldTransforms[instance] = mat4f();
        mParents[instance] = Instance::INVALID;
        return instance;
    }

    // 设置父子关系
    void setParent(Instance child, Instance parent) {
        mParents[child] = parent;
        markDirty(child);  // 标记需要重新计算世界变换
    }

    // 设置局部变换
    void setTransform(Instance instance, mat4f const& transform) {
        mLocalTransforms[instance] = transform;
        markDirty(instance);
    }

    // 获取世界变换（自动计算层次）
    mat4f getWorldTransform(Instance instance) {
        if (isDirty(instance)) {
            updateWorldTransform(instance);
        }
        return mWorldTransforms[instance];
    }

private:
    void updateWorldTransform(Instance instance) {
        Instance parent = mParents[instance];
        if (parent != Instance::INVALID) {
            // 递归更新父变换
            mat4f parentWorld = getWorldTransform(parent);
            mWorldTransforms[instance] = parentWorld * mLocalTransforms[instance];
        } else {
            // 根节点，世界变换=局部变换
            mWorldTransforms[instance] = mLocalTransforms[instance];
        }
        clearDirty(instance);
    }

    // SoA数据布局
    std::vector<Entity> mEntities;
    std::vector<mat4f> mLocalTransforms;
    std::vector<mat4f> mWorldTransforms;
    std::vector<Instance> mParents;
    std::vector<uint32_t> mDirtyFlags;  // 脏标记
};
```

### 2.2 性能优化

**批量更新**：一次性更新所有脏变换，减少递归开销。

```cpp
void TransformManager::updateAll() {
    // 按层次深度排序，保证父节点先更新
    std::vector<Instance> sorted = topologicalSort(mDirtyInstances);

    for (Instance inst : sorted) {
        updateWorldTransform(inst);
    }
}
```

---

## 3. RenderableManager详解

RenderableManager管理可渲染对象的几何和材质数据。

### 3.1 Renderable组件

每个Renderable包含：
- **Primitive**: 几何数据（顶点、索引缓冲区）
- **MaterialInstance**: 材质实例
- **Bounding Box**: 包围盒（用于剔除）
- **Priority**: 渲染优先级

```cpp
class RenderableManager {
public:
    struct Builder {
        Builder(size_t count) : mPrimitiveCount(count) {}

        // 设置几何体
        Builder& geometry(size_t index,
            PrimitiveType type,
            VertexBuffer* vertices,
            IndexBuffer* indices) {
            mPrimitives[index] = {type, vertices, indices};
            return *this;
        }

        // 设置材质
        Builder& material(size_t index, MaterialInstance* mi) {
            mMaterialInstances[index] = mi;
            return *this;
        }

        // 设置包围盒
        Builder& boundingBox(Box const& box) {
            mAABB = box;
            return *this;
        }

        // 构建Renderable
        void build(Engine& engine, Entity entity) {
            auto& rm = engine.getRenderableManager();
            Instance inst = rm.create(entity);

            rm.setPrimitives(inst, mPrimitives);
            rm.setMaterialInstances(inst, mMaterialInstances);
            rm.setAABB(inst, mAABB);
        }

    private:
        size_t mPrimitiveCount;
        std::vector<Primitive> mPrimitives;
        std::vector<MaterialInstance*> mMaterialInstances;
        Box mAABB;
    };

private:
    // SoA数据
    std::vector<Entity> mEntities;
    std::vector<std::vector<Primitive>> mPrimitives;
    std::vector<std::vector<MaterialInstance*>> mMaterials;
    std::vector<Box> mAABBs;
    std::vector<uint8_t> mPriorities;
};
```

### 3.2 剔除优化

RenderableManager提供高效的视锥剔除接口。

```cpp
// 并行剔除
void RenderableManager::cull(
    Frustum const& frustum,
    std::vector<Instance>& outVisible) {

    JobSystem& js = getJobSystem();

    // 并行处理每个Renderable
    jobs::parallel_for(js, 0, mEntities.size(),
        [&](size_t i) {
            Box const& aabb = mAABBs[i];
            if (frustum.intersects(aabb)) {
                // 线程安全地添加到结果（使用thread-local）
                thread_local std::vector<Instance> localVisible;
                localVisible.push_back(Instance(i));
            }
        });

    // 合并结果
    // ...
}
```

---

## 4. LightManager详解

LightManager管理场景中的光源。

### 4.1 光源类型

```cpp
enum class LightType {
    DIRECTIONAL,  // 方向光（太阳光）
    POINT,        // 点光源
    SPOT,         // 聚光灯
    FOCUSED_SPOT  // 带纹理的聚光灯
};

class LightManager {
public:
    struct Builder {
        Builder(LightType type) : mType(type) {}

        // 设置颜色
        Builder& color(LinearColor const& color) {
            mColor = color;
            return *this;
        }

        // 设置强度
        Builder& intensity(float intensity) {
            mIntensity = intensity;
            return *this;
        }

        // 设置方向（方向光）
        Builder& direction(vec3 const& direction) {
            mDirection = normalize(direction);
            return *this;
        }

        // 设置位置（点光源）
        Builder& position(vec3 const& pos) {
            mPosition = pos;
            return *this;
        }

        // 设置衰减
        Builder& falloff(float radius) {
            mFalloffRadius = radius;
            return *this;
        }

        void build(Engine& engine, Entity entity) {
            auto& lm = engine.getLightManager();
            Instance inst = lm.create(entity, mType);

            lm.setColor(inst, mColor);
            lm.setIntensity(inst, mIntensity);

            if (mType == LightType::DIRECTIONAL) {
                lm.setDirection(inst, mDirection);
            } else {
                lm.setPosition(inst, mPosition);
                lm.setFalloff(inst, mFalloffRadius);
            }
        }

    private:
        LightType mType;
        LinearColor mColor = {1, 1, 1};
        float mIntensity = 100000.0f;
        vec3 mDirection = {0, -1, 0};
        vec3 mPosition = {0, 0, 0};
        float mFalloffRadius = 10.0f;
    };
};
```

### 4.2 阴影配置

```cpp
// 启用阴影
lm.setShadowCaster(lightInstance, true);

// 配置阴影参数
lm.setShadowOptions(lightInstance, {
    .mapSize = 2048,
    .cascades = 4,          // CSM级联数
    .splitPositions = {0.05f, 0.15f, 0.5f, 1.0f},
    .constantBias = 0.001f,
    .normalBias = 1.0f
});
```

---

## 5. 最佳实践

### 5.1 Entity生命周期管理

```cpp
class SceneManager {
    Engine& mEngine;
    std::vector<Entity> mEntities;

public:
    // 创建场景对象
    Entity createObject(
        vec3 position,
        VertexBuffer* vb,
        IndexBuffer* ib,
        MaterialInstance* mi) {

        // 1. 创建Entity
        Entity entity = EntityManager::get().create();

        // 2. 添加Transform组件
        auto& tcm = mEngine.getTransformManager();
        auto ti = tcm.create(entity);
        tcm.setTransform(ti, mat4f::translation(position));

        // 3. 添加Renderable组件
        RenderableManager::Builder(1)
            .geometry(0, PrimitiveType::TRIANGLES, vb, ib)
            .material(0, mi)
            .boundingBox({{-1,-1,-1}, {1,1,1}})
            .build(mEngine, entity);

        // 4. 添加到Scene
        mScene->addEntity(entity);

        // 5. 跟踪Entity
        mEntities.push_back(entity);

        return entity;
    }

    // 销毁对象
    void destroyObject(Entity entity) {
        // 1. 从Scene移除
        mScene->remove(entity);

        // 2. 销毁组件（Manager会自动清理）
        mEngine.destroy(entity);

        // 3. 从跟踪列表移除
        auto it = std::find(mEntities.begin(), mEntities.end(), entity);
        if (it != mEntities.end()) {
            mEntities.erase(it);
        }
    }

    // 批量销毁
    ~SceneManager() {
        for (Entity e : mEntities) {
            mEngine.destroy(e);
        }
    }
};
```

### 5.2 组件查询模式

```cpp
// 查询所有具有Transform和Renderable的Entity
void updateRenderables() {
    auto& tcm = engine.getTransformManager();
    auto& rcm = engine.getRenderableManager();

    // 遍历所有Renderable
    for (size_t i = 0; i < rcm.getComponentCount(); ++i) {
        Entity entity = rcm.getEntity(i);

        // 检查是否有Transform组件
        if (tcm.hasComponent(entity)) {
            auto ti = tcm.getInstance(entity);
            auto ri = rcm.getInstance(entity);

            // 更新逻辑...
            mat4f worldTransform = tcm.getWorldTransform(ti);
            updateRenderableTransform(ri, worldTransform);
        }
    }
}
```

### 5.3 缓存友好的迭代

```cpp
// ✅ 好的做法：按组件类型迭代（缓存友好）
void updateAllTransforms(TransformManager& tcm) {
    size_t count = tcm.getComponentCount();

    // 连续访问同类型数据
    for (size_t i = 0; i < count; ++i) {
        mat4f& transform = tcm.getTransformRef(i);
        // 处理transform...
    }
}

// ❌ 不好的做法：随机访问Entity（缓存不友好）
void badUpdate(std::vector<Entity> const& entities) {
    auto& tcm = engine.getTransformManager();

    for (Entity e : entities) {
        // 每次查询都可能cache miss
        if (tcm.hasComponent(e)) {
            auto inst = tcm.getInstance(e);
            mat4f t = tcm.getTransform(inst);
        }
    }
}
```

### 5.4 组件组合模式

```cpp
// 可复用的组件组合
class GameObject {
    Engine& mEngine;
    Entity mEntity;

public:
    GameObject(Engine& engine) : mEngine(engine) {
        mEntity = EntityManager::get().create();
    }

    // Fluent API
    GameObject& withTransform(vec3 position) {
        auto& tcm = mEngine.getTransformManager();
        auto inst = tcm.create(mEntity);
        tcm.setTransform(inst, mat4f::translation(position));
        return *this;
    }

    GameObject& withRenderable(
        VertexBuffer* vb,
        IndexBuffer* ib,
        MaterialInstance* mi) {

        RenderableManager::Builder(1)
            .geometry(0, PrimitiveType::TRIANGLES, vb, ib)
            .material(0, mi)
            .build(mEngine, mEntity);
        return *this;
    }

    GameObject& withLight(LightType type, float intensity) {
        LightManager::Builder(type)
            .intensity(intensity)
            .build(mEngine, mEntity);
        return *this;
    }

    Entity getEntity() const { return mEntity; }
};

// 使用示例
GameObject obj(engine);
obj.withTransform({0, 1, 0})
   .withRenderable(vb, ib, material)
   .withLight(LightType::POINT, 1000.0f);
```

---

## 6. 性能优化技巧

### 6.1 预分配容量

```cpp
// 避免动态扩容
class TransformManager {
public:
    void reserve(size_t count) {
        mEntities.reserve(count);
        mLocalTransforms.reserve(count);
        mWorldTransforms.reserve(count);
        // ...预分配所有vector
    }
};

// 使用
tcm.reserve(10000);  // 预期有10000个transform
```

### 6.2 批量操作

```cpp
// 批量创建Entity
std::vector<Entity> createEntities(size_t count) {
    std::vector<Entity> entities(count);

    // 一次性分配
    EntityManager::get().create(count, entities.data());

    return entities;
}

// 批量添加组件
void addTransforms(std::vector<Entity> const& entities) {
    auto& tcm = engine.getTransformManager();

    // 预分配空间
    tcm.reserve(tcm.getComponentCount() + entities.size());

    // 批量创建
    for (Entity e : entities) {
        tcm.create(e);
    }
}
```

### 6.3 脏标记优化

```cpp
class TransformManager {
    // 只更新脏的Transform
    void updateDirty() {
        for (Instance inst : mDirtyList) {
            updateWorldTransform(inst);
        }
        mDirtyList.clear();
    }

    void markDirty(Instance inst) {
        if (!isDirty(inst)) {
            mDirtyFlags[inst] = true;
            mDirtyList.push_back(inst);

            // 递归标记子节点
            for (Instance child : getChildren(inst)) {
                markDirty(child);
            }
        }
    }
};
```

---

## 7. 调试工具

### 7.1 组件检查器

```cpp
class ComponentInspector {
public:
    static void printEntity(Engine& engine, Entity entity) {
        std::cout << "Entity: " << entity << "\n";

        // 检查各个组件
        auto& tcm = engine.getTransformManager();
        if (tcm.hasComponent(entity)) {
            auto inst = tcm.getInstance(entity);
            std::cout << "  Transform: " << tcm.getTransform(inst) << "\n";
        }

        auto& rcm = engine.getRenderableManager();
        if (rcm.hasComponent(entity)) {
            std::cout << "  Renderable: yes\n";
            auto inst = rcm.getInstance(entity);
            std::cout << "    Primitives: " << rcm.getPrimitiveCount(inst) << "\n";
        }

        auto& lm = engine.getLightManager();
        if (lm.hasComponent(entity)) {
            std::cout << "  Light: yes\n";
        }
    }
};
```

### 7.2 性能分析

```cpp
// 统计组件数量
void printStats(Engine& engine) {
    auto& tcm = engine.getTransformManager();
    auto& rcm = engine.getRenderableManager();
    auto& lm = engine.getLightManager();

    std::cout << "Component Statistics:\n";
    std::cout << "  Transforms:  " << tcm.getComponentCount() << "\n";
    std::cout << "  Renderables: " << rcm.getComponentCount() << "\n";
    std::cout << "  Lights:      " << lm.getComponentCount() << "\n";
}
```

---

## 8. 常见问题

### Q1: 如何实现组件间通信？

**A**: 通过Entity作为桥梁，在不同Manager中查询相同Entity的组件。

### Q2: 为什么使用SoA而不是AoS？

**A**: SoA布局使得同类型数据连续存储，遍历时缓存友好，性能更好。

### Q3: Instance和Entity有什么区别？

**A**: Entity是全局唯一标识符，Instance是组件在Manager中的索引。一个Entity可以有多个不同类型的Instance（在不同Manager中）。

### Q4: 如何高效地查找具有特定组件组合的Entity？

**A**: 遍历数量较少的组件类型，然后检查其他组件是否存在。

---

## 9. 相关文档

- [01-data-flow-analysis.md](./01-data-flow-analysis.md) - 数据流架构
- [02-multithreading-design.md](./02-multithreading-design.md) - 多线程设计
- [../optimization/04-memory-management.md](../optimization/04-memory-management.md) - 内存管理

---

## 10. 总结

Filament的ECS架构体现了现代游戏引擎的设计理念：

1. **数据导向**：SoA布局优化缓存访问
2. **类型安全**：编译期检查组件类型
3. **高性能**：缓存友好、批量操作
4. **可扩展**：易于添加新组件类型
5. **简洁API**：Builder模式简化对象创建

理解ECS是高效使用Filament的关键。通过合理组织组件和数据布局，可以构建高性能的3D应用。
