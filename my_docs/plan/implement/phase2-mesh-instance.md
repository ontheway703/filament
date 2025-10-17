# Phase 2 - MeshInstance 实现

**优先级**: P1-1（重要）
**预计工作量**: 4-6 个工作日
**依赖**: Phase 1 完成
**目标**: 实现高效的实例化系统，与 Filament 的 FilamentAsset + FilamentInstance 模式对齐

---

## 1. 问题分析

### 1.1 当前状态

**使用场景**: 实例化场景（加载 100 个相同的敌人）

**当前实现**:
```cpp
// 当前做法（错误）
std::vector<MeshAsset*> enemies;
for (int i = 0; i < 100; i++) {
    MeshAsset* enemy = loader->loadMesh(enemyData, enemySize);  // ❌ 重复解析
    enemy->uploadResources();  // ❌ 重复上传 GPU 资源
    enemies.push_back(enemy);
}
```

**问题**:
1. **重复解析**: cgltf 解析 100 次（CPU 浪费）
2. **GPU 内存浪费**: 100 个 VertexBuffer + IndexBuffer（应该共享）
3. **用户负担**: 用户需要手动实现缓存机制

**内存占用**:
```
单个模型: VertexBuffer (1MB) + IndexBuffer (500KB) = 1.5MB
100 个实例（当前）: 1.5MB × 100 = 150MB  ❌
100 个实例（应该）: 1.5MB × 1 = 1.5MB    ✅
```

### 1.2 与 Filament 的 FilamentAsset/FilamentInstance 对比

**Filament 的设计** (`libs/gltfio/include/gltfio/FilamentAsset.h`):

```cpp
// FilamentAsset: 拥有共享的 GPU 资源
class FilamentAsset {
    VertexBuffer* mVertexBuffers;  // 共享
    IndexBuffer* mIndexBuffers;    // 共享
    Material* mMaterials;          // 共享

    FilamentInstance** getAssetInstances();  // 管理所有实例
};

// FilamentInstance: 独立的 Entity 和 MaterialInstance
class FilamentInstance {
    Entity* mEntities;                 // 独立
    MaterialInstance** mMaterialInstances;  // 独立（clone 自 Asset）
    Animator* mAnimator;               // 独立

    FilamentAsset const* getAsset() const;  // 反向引用
};
```

**使用方式**:
```cpp
// 1. 加载一次
FilamentAsset* asset = loader->createAsset(data, size);
loader->loadResources(asset);

// 2. 创建多个实例
for (int i = 0; i < 100; i++) {
    FilamentInstance* instance = asset->getInstance();  // 第一个实例（默认）
    // 或创建新实例
    FilamentInstance* newInstance = asset->createInstance();  // ✅ 共享 GPU 资源
}
```

**gltfio_ext 需要对齐这个设计**！

---

## 2. 设计目标

### 2.1 功能目标

1. **共享 GPU 资源**: 多个实例共享 VertexBuffer/IndexBuffer
2. **独立 CPU 状态**: 每个实例有独立的 Entity、MaterialInstance、Transform
3. **API 一致性**: 与 FilamentAsset/FilamentInstance 对齐
4. **向后兼容**: 现有代码仍可以工作（MeshAsset 本身是第一个实例）

### 2.2 非功能目标

1. **性能**: 100 个实例的 GPU 内存 ≈ 1 倍（而非 100 倍）
2. **易用性**: 用户无需手动管理缓存
3. **安全性**: 实例销毁时不影响 Asset 和其他实例

---

## 3. 架构设计

### 3.1 核心概念

```
MeshAsset (shared)
├─ VertexBuffer* (共享 GPU 资源)
├─ IndexBuffer* (共享 GPU 资源)
├─ Material* (共享材质模板)
├─ std::vector<MeshInstance*> mInstances (管理所有实例)
└─ createInstance() → MeshInstance*

MeshInstance (independent)
├─ Entity mRenderableEntity (独立)
├─ RenderableManager::Instance (独立)
├─ std::vector<MaterialInstance*> mMaterialInstances (独立，clone 自 MeshAsset)
├─ SkeletonAsset* mBoundSkeleton (独立，可以绑定不同骨骼)
└─ updateSkinning(animator) (独立)
```

### 3.2 关键设计决策

**决策 1: MeshAsset 是否还拥有 Entity？**

**选项 A**: MeshAsset 不再拥有 Entity（纯资源容器）
- 优点: 概念更清晰（Asset 只管资源，Instance 管场景对象）
- 缺点: 破坏向后兼容性

**选项 B**: MeshAsset 拥有默认 Entity（作为第一个实例）✅
- 优点: 向后兼容，现有代码仍可工作
- 缺点: 概念稍复杂（Asset 既是资源也是实例）

**选择**: 选项 B（与 FilamentAsset 对齐）

```cpp
// 向后兼容
MeshAsset* mesh = loader->loadMesh(...);
mesh->uploadResources();
scene->addEntity(mesh->getRenderableEntity());  // ✅ 仍然可用

// 新用法（实例化）
MeshInstance* instance1 = mesh->createInstance();
MeshInstance* instance2 = mesh->createInstance();
```

**决策 2: MaterialInstance 是否共享？**

**结论**: 不共享，每个实例 clone 一份

**理由**:
- 允许每个实例有不同的材质参数（如颜色、纹理）
- 换装系统需要独立的 MaterialInstance

**决策 3: SkeletonAsset 是否共享？**

**结论**: 不共享，每个实例可以绑定不同的骨骼

**理由**:
- 换装系统需要不同的骨骼（如：重型盔甲骨骼 vs 轻型盔甲骨骼）

---

## 4. API 设计

### 4.1 MeshAsset 扩展

```cpp
// MeshAsset.h

class MeshAsset {
public:
    // ... 现有 API ...

    /**
     * 创建新实例（共享 VertexBuffer/IndexBuffer）
     * @return 新的 MeshInstance 指针
     *
     * 说明：
     * - 新实例共享 GPU 资源（VertexBuffer/IndexBuffer）
     * - 新实例有独立的 Entity 和 MaterialInstance
     * - MeshAsset 拥有实例的生命周期
     */
    MeshInstance* createInstance();

    /**
     * 销毁实例
     * @param instance 要销毁的实例
     *
     * 说明：
     * - 释放 Entity 和 MaterialInstance
     * - 不影响其他实例
     * - 不能销毁 MeshAsset 自己（第一个实例）
     */
    void destroyInstance(MeshInstance* instance);

    /**
     * 获取所有实例（不包括 MeshAsset 自己）
     * @return 实例数组
     */
    MeshInstance** getInstances();

    /**
     * 获取实例数量（不包括 MeshAsset 自己）
     * @return 实例数量
     */
    size_t getInstanceCount() const;

    /**
     * 获取默认实例（MeshAsset 自己）
     * @return 默认 MeshInstance
     *
     * 说明：为了 API 一致性，MeshAsset 可以当作第一个实例使用
     */
    MeshInstance* getDefaultInstance();

private:
    std::vector<MeshInstance*> mInstances;  // 额外创建的实例
};
```

### 4.2 MeshInstance 新类

```cpp
// MeshInstance.h

namespace filament::gltfio {

/**
 * MeshInstance 表示 MeshAsset 的一个实例
 *
 * 特点：
 * - 共享 GPU 资源（VertexBuffer/IndexBuffer）
 * - 独立的 Entity 和 MaterialInstance
 * - 可以独立绑定骨骼和更新蒙皮
 */
class MeshInstance {
public:
    /**
     * 获取所属的 MeshAsset
     * @return MeshAsset 指针
     */
    MeshAsset const* getAsset() const;

    /**
     * 绑定骨骼
     * @param skeleton SkeletonAsset 指针
     * @return 绑定是否成功
     */
    bool bindSkeleton(SkeletonAsset* skeleton);

    /**
     * 获取绑定的骨骼
     * @return SkeletonAsset 指针，如果没有绑定则返回 nullptr
     */
    SkeletonAsset* getBoundSkeleton() const;

    /**
     * 更新蒙皮（计算骨骼矩阵并上传到 GPU）
     * @param animator StandaloneAnimator 指针
     */
    void updateSkinning(const StandaloneAnimator* animator);

    /**
     * 获取可渲染 Entity
     * @return Filament Entity
     */
    filament::Entity getRenderableEntity() const;

    /**
     * 获取 RenderableManager Instance
     * @return RenderableManager::Instance
     */
    filament::RenderableManager::Instance getRenderableInstance() const;

    /**
     * 获取包围盒
     * @return AABB 包围盒
     */
    filament::Aabb getBoundingBox() const;

    /**
     * 获取指定 primitive 的 MaterialInstance
     * @param primitiveIndex primitive 索引
     * @return MaterialInstance 指针
     *
     * 用途：修改材质参数（如颜色、纹理）
     */
    filament::MaterialInstance* getMaterialInstance(size_t primitiveIndex);

    /**
     * 获取 primitive 数量
     * @return primitive 数量
     */
    size_t getPrimitiveCount() const;

private:
    friend class MeshAsset;

    // 私有构造函数（只能由 MeshAsset 创建）
    MeshInstance(MeshAsset* asset, filament::Engine* engine);

    // 禁止拷贝和赋值
    MeshInstance(const MeshInstance&) = delete;
    MeshInstance& operator=(const MeshInstance&) = delete;

    MeshAsset* mAsset;
    filament::Engine* mEngine;
    filament::Entity mRenderableEntity;
    std::vector<filament::MaterialInstance*> mMaterialInstances;
    SkeletonAsset* mBoundSkeleton = nullptr;
};

} // namespace filament::gltfio
```

---

## 5. 实现步骤

### 5.1 步骤 1: 重构 MeshAsset 结构

**修改文件**: `libs/gltfio_ext/include/gltfio/MeshAsset.h`

**变更**:
```cpp
class MeshAsset {
private:
    // 原有字段保持不变
    Engine* mEngine;
    VertexBuffer** mVertexBuffers;  // 共享资源
    IndexBuffer** mIndexBuffers;    // 共享资源

    // 新增字段
    std::vector<MeshInstance*> mInstances;  // 管理的实例列表

    // 将 Entity 和 MaterialInstance 视为"默认实例"的状态
    // 保持向后兼容
};
```

**工作量**: 0.5 天

### 5.2 步骤 2: 实现 MeshInstance 类

**新建文件**: `libs/gltfio_ext/include/gltfio/MeshInstance.h`
**新建文件**: `libs/gltfio_ext/src/MeshInstance.cpp`

**实现**:

```cpp
// MeshInstance.cpp

MeshInstance::MeshInstance(MeshAsset* asset, Engine* engine)
    : mAsset(asset), mEngine(engine) {

    // 1. 创建独立的 Entity
    mRenderableEntity = EntityManager::get().create();

    // 2. Clone MaterialInstances
    size_t primCount = asset->getPrimitiveCount();
    mMaterialInstances.resize(primCount);

    for (size_t i = 0; i < primCount; i++) {
        MaterialInstance* original = asset->getMaterialInstance(i);
        mMaterialInstances[i] = original->getMaterial()->createInstance();

        // 复制所有参数（纹理、颜色等）
        // 注意：这里需要实现参数复制逻辑
        copyMaterialParameters(original, mMaterialInstances[i]);
    }

    // 3. 创建 RenderableManager 组件
    RenderableManager::Builder builder(primCount);

    for (size_t i = 0; i < primCount; i++) {
        // 共享 VertexBuffer 和 IndexBuffer
        builder.geometry(i,
                        RenderableManager::PrimitiveType::TRIANGLES,
                        asset->getVertexBuffer(i),
                        asset->getIndexBuffer(i));

        // 使用 clone 的 MaterialInstance
        builder.material(i, mMaterialInstances[i]);

        // 设置包围盒
        builder.boundingBox(asset->getBoundingBox());
    }

    builder.culling(true)
           .receiveShadows(true)
           .castShadows(true)
           .build(*mEngine, mRenderableEntity);
}

MeshInstance::~MeshInstance() {
    // 销毁 MaterialInstances
    for (auto* mi : mMaterialInstances) {
        mEngine->destroy(mi);
    }

    // 销毁 Entity
    EntityManager::get().destroy(mRenderableEntity);
}
```

**工作量**: 2 天

### 5.3 步骤 3: 实现 MeshAsset::createInstance()

**修改文件**: `libs/gltfio_ext/src/MeshAsset.cpp`

**实现**:
```cpp
MeshInstance* MeshAsset::createInstance() {
    MeshInstance* instance = new MeshInstance(this, mEngine);
    mInstances.push_back(instance);
    return instance;
}

void MeshAsset::destroyInstance(MeshInstance* instance) {
    auto it = std::find(mInstances.begin(), mInstances.end(), instance);
    if (it != mInstances.end()) {
        mInstances.erase(it);
        delete instance;
    }
}

MeshInstance** MeshAsset::getInstances() {
    return mInstances.data();
}

size_t MeshAsset::getInstanceCount() const {
    return mInstances.size();
}
```

**工作量**: 0.5 天

### 5.4 步骤 4: 实现 MeshInstance::bindSkeleton() 和 updateSkinning()

**修改文件**: `libs/gltfio_ext/src/MeshInstance.cpp`

**实现**:
```cpp
bool MeshInstance::bindSkeleton(SkeletonAsset* skeleton) {
    // 验证兼容性
    if (!mAsset->isSkeletonCompatible(skeleton)) {
        return false;
    }

    mBoundSkeleton = skeleton;
    return true;
}

void MeshInstance::updateSkinning(const StandaloneAnimator* animator) {
    if (!mBoundSkeleton || !animator) {
        return;
    }

    // 计算骨骼矩阵
    const mat4f* boneMatrices = animator->getBoneMatrices();
    size_t boneCount = animator->getBoneCount();

    // 上传到 GPU
    RenderableManager& rm = mEngine->getRenderableManager();
    RenderableManager::Instance instance = rm.getInstance(mRenderableEntity);

    rm.setBones(instance, boneMatrices, boneCount);
}
```

**工作量**: 0.5 天

### 5.5 步骤 5: 修改 MeshAsset 析构函数

**修改文件**: `libs/gltfio_ext/src/MeshAsset.cpp`

**实现**:
```cpp
MeshAsset::~MeshAsset() {
    // 1. 销毁所有实例
    for (MeshInstance* instance : mInstances) {
        delete instance;
    }
    mInstances.clear();

    // 2. 销毁共享资源（VertexBuffer/IndexBuffer）
    for (size_t i = 0; i < mPrimitiveCount; i++) {
        mEngine->destroy(mVertexBuffers[i]);
        mEngine->destroy(mIndexBuffers[i]);
    }

    // 3. 销毁默认实例的资源
    for (auto* mi : mMaterialInstances) {
        mEngine->destroy(mi);
    }
    EntityManager::get().destroy(mRenderableEntity);
}
```

**工作量**: 0.5 天

### 5.6 步骤 6: 实现 MaterialInstance 参数复制

**新建辅助函数**: `libs/gltfio_ext/src/MeshInstance.cpp`

```cpp
namespace {

void copyMaterialParameters(MaterialInstance* src, MaterialInstance* dst) {
    // 注意：Filament 没有提供直接的参数复制 API
    // 需要手动复制常见参数

    // 1. 复制纹理参数
    const char* textureParams[] = {
        "baseColorMap", "normalMap", "metallicRoughnessMap",
        "occlusionMap", "emissiveMap"
    };

    for (const char* param : textureParams) {
        // 获取纹理（需要反射或提前存储）
        // 这里假设我们在 MeshAsset 中保存了纹理映射
        // 实际实现需要扩展 MeshAsset 存储纹理信息
    }

    // 2. 复制颜色参数
    float4 baseColor;
    if (src->getParameter("baseColorFactor", &baseColor)) {
        dst->setParameter("baseColorFactor", baseColor);
    }

    // 3. 复制其他标量参数
    float metallicFactor, roughnessFactor;
    if (src->getParameter("metallicFactor", &metallicFactor)) {
        dst->setParameter("metallicFactor", metallicFactor);
    }
    if (src->getParameter("roughnessFactor", &roughnessFactor)) {
        dst->setParameter("roughnessFactor", roughnessFactor);
    }
}

} // anonymous namespace
```

**注意**: Filament 的 MaterialInstance 不提供参数枚举 API，需要手动复制已知参数。

**工作量**: 1 天

---

## 6. 使用示例

### 6.1 基础实例化场景

```cpp
#include <gltfio/AssetLoaderExt.h>
#include <gltfio/MeshInstance.h>

using namespace filament;
using namespace gltfio;

// 1. 加载 MeshAsset 一次
AssetLoaderExt* loader = AssetLoaderExt::create(engine, matProvider);
MeshAsset* enemyAsset = loader->loadMesh(enemyData, enemySize);

// 加载纹理
auto textures = enemyAsset->getRequiredTextures();
for (const auto& texInfo : textures) {
    Texture* tex = loadTexture(texInfo);
    enemyAsset->bindTexture(texInfo.primitiveIndex, texInfo.slot, tex, sampler);
}

enemyAsset->uploadResources();

// 2. 创建 100 个实例
std::vector<MeshInstance*> enemies;
for (int i = 0; i < 100; i++) {
    MeshInstance* instance = enemyAsset->createInstance();  // ✅ 共享 GPU 资源

    // 设置位置
    TransformManager& tm = engine->getTransformManager();
    auto transform = tm.getInstance(instance->getRenderableEntity());
    mat4f matrix = mat4f::translation(float3{i * 2.0f, 0, 0});
    tm.setTransform(transform, matrix);

    // 添加到场景
    scene->addEntity(instance->getRenderableEntity());

    enemies.push_back(instance);
}

// 3. 独立动画（每个实例可以播放不同动画）
StandaloneAnimator* animator1 = StandaloneAnimator::create(skeleton);
animator1->playAnimation(idleAnim, 0);

StandaloneAnimator* animator2 = StandaloneAnimator::create(skeleton);
animator2->playAnimation(runAnim, 0);

// 更新循环
while (rendering) {
    animator1->update(deltaTime);
    animator2->update(deltaTime);

    enemies[0]->updateSkinning(animator1);  // 第一个敌人播放 idle
    enemies[1]->updateSkinning(animator2);  // 第二个敌人播放 run
}

// 4. 清理
for (auto* instance : enemies) {
    enemyAsset->destroyInstance(instance);
}
delete enemyAsset;
```

### 6.2 换装系统 + 实例化

```cpp
// 1. 加载共享资源
SkeletonAsset* skeleton = loader->loadSkeleton(skeletonData, skeletonSize);
MeshAsset* lightArmor = loader->loadMesh(lightArmorData, lightArmorSize);
MeshAsset* heavyArmor = loader->loadMesh(heavyArmorData, heavyArmorSize);

// 2. 创建玩家实例（使用轻型盔甲）
MeshInstance* player = lightArmor->createInstance();
player->bindSkeleton(skeleton);
scene->addEntity(player->getRenderableEntity());

// 3. 创建 10 个 NPC（5 个轻型，5 个重型）
std::vector<MeshInstance*> npcs;
for (int i = 0; i < 5; i++) {
    MeshInstance* npc = lightArmor->createInstance();
    npc->bindSkeleton(skeleton);
    scene->addEntity(npc->getRenderableEntity());
    npcs.push_back(npc);
}

for (int i = 0; i < 5; i++) {
    MeshInstance* npc = heavyArmor->createInstance();
    npc->bindSkeleton(skeleton);
    scene->addEntity(npc->getRenderableEntity());
    npcs.push_back(npc);
}

// GPU 内存占用：
// Skeleton: 1x
// Light Armor: 1x (6 个实例共享)
// Heavy Armor: 1x (5 个实例共享)
// 总计: ≈ 3x（而非 11x）
```

---

## 7. 测试计划

### 7.1 单元测试

**新建文件**: `libs/gltfio_ext/tests/test_mesh_instance.cpp`

**测试用例**:

```cpp
TEST(MeshInstance, CreateAndDestroy) {
    MeshAsset* mesh = loader->loadMesh(meshData, meshSize);
    mesh->uploadResources();

    MeshInstance* instance = mesh->createInstance();
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(mesh->getInstanceCount(), 1);

    mesh->destroyInstance(instance);
    EXPECT_EQ(mesh->getInstanceCount(), 0);

    delete mesh;
}

TEST(MeshInstance, SharedGpuResources) {
    MeshAsset* mesh = loader->loadMesh(meshData, meshSize);
    mesh->uploadResources();

    MeshInstance* instance1 = mesh->createInstance();
    MeshInstance* instance2 = mesh->createInstance();

    // 验证共享 VertexBuffer
    EXPECT_EQ(instance1->getAsset()->getVertexBuffer(0),
              instance2->getAsset()->getVertexBuffer(0));

    // 但 Entity 不同
    EXPECT_NE(instance1->getRenderableEntity(),
              instance2->getRenderableEntity());

    delete mesh;
}

TEST(MeshInstance, IndependentMaterialInstance) {
    MeshAsset* mesh = loader->loadMesh(meshData, meshSize);
    mesh->uploadResources();

    MeshInstance* instance1 = mesh->createInstance();
    MeshInstance* instance2 = mesh->createInstance();

    // 修改 instance1 的颜色
    instance1->getMaterialInstance(0)->setParameter("baseColorFactor", float4{1, 0, 0, 1});

    // 验证 instance2 不受影响
    float4 color;
    instance2->getMaterialInstance(0)->getParameter("baseColorFactor", &color);
    EXPECT_NE(color.r, 1.0f);

    delete mesh;
}
```

### 7.2 集成测试

**测试场景 1: 实例化场景（100 个相同模型）**
- 验证 GPU 内存占用 ≈ 1x
- 验证渲染正确（100 个模型都显示）

**测试场景 2: 换装系统 + 实例化**
- 1 个骨骼 + 2 个 MeshAsset（轻型/重型盔甲）+ 10 个实例
- 验证 GPU 内存占用 ≈ 3x（而非 11x）

**测试场景 3: 独立动画**
- 2 个实例播放不同动画
- 验证动画互不影响

### 7.3 性能测试

**指标**:
- **GPU 内存占用**: 100 个实例 ≈ 1 倍（允许 1.1 倍，MaterialInstance 占用）
- **实例创建时间**: < 1ms per instance
- **渲染性能**: 100 个实例 vs 1 个实例，帧率下降 < 10%

---

## 8. 验收标准

### 8.1 功能验收

- ✅ `MeshAsset::createInstance()` 可以创建实例
- ✅ 多个实例共享 VertexBuffer/IndexBuffer
- ✅ 每个实例有独立的 Entity 和 MaterialInstance
- ✅ 实例可以独立绑定骨骼和更新蒙皮
- ✅ `destroyInstance()` 正确释放资源

### 8.2 性能验收

- ✅ 100 个实例 GPU 内存 ≤ 1.1x
- ✅ 实例创建时间 < 1ms
- ✅ 无内存泄漏

### 8.3 兼容性验收

- ✅ 现有代码仍可工作（向后兼容）
- ✅ 不破坏现有 API

---

## 9. 迁移指南

### 9.1 场景 1: 不需要实例化（无需修改）

```cpp
// 现有代码（不变）
MeshAsset* mesh = loader->loadMesh(meshData, meshSize);
mesh->uploadResources();
scene->addEntity(mesh->getRenderableEntity());  // ✅ 仍然可用
```

### 9.2 场景 2: 需要实例化（修改）

**修改前**:
```cpp
std::vector<MeshAsset*> enemies;
for (int i = 0; i < 100; i++) {
    MeshAsset* enemy = loader->loadMesh(enemyData, enemySize);  // ❌
    enemy->uploadResources();
    enemies.push_back(enemy);
}
```

**修改后**:
```cpp
// 1. 只加载一次
MeshAsset* enemyAsset = loader->loadMesh(enemyData, enemySize);
enemyAsset->uploadResources();

// 2. 创建实例
std::vector<MeshInstance*> enemies;
for (int i = 0; i < 100; i++) {
    MeshInstance* enemy = enemyAsset->createInstance();  // ✅
    scene->addEntity(enemy->getRenderableEntity());
    enemies.push_back(enemy);
}

// 3. 清理（修改）
for (auto* enemy : enemies) {
    enemyAsset->destroyInstance(enemy);  // ✅
}
delete enemyAsset;
```

---

## 10. 风险和缓解

### 10.1 风险 1: MaterialInstance 参数复制不完整

**影响**: 实例的材质参数不正确
**概率**: 中
**缓解**:
- 提供辅助函数 `copyMaterialParameters()`
- 测试覆盖常见参数（颜色、金属度、粗糙度）
- 文档说明限制

### 10.2 风险 2: 向后兼容性破坏

**影响**: 现有代码无法编译或运行
**概率**: 低
**缓解**:
- 保持现有 API 不变
- MeshAsset 仍可作为第一个实例使用
- 充分测试现有用例

### 10.3 风险 3: 内存泄漏

**影响**: 长时间运行后内存耗尽
**概率**: 中
**缓解**:
- 仔细管理实例生命周期
- 使用 Valgrind 检测泄漏
- 单元测试覆盖创建/销毁

---

## 11. 后续优化（可选）

1. **智能指针管理**: 使用 `std::unique_ptr` 管理实例生命周期
2. **实例池**: 实现对象池避免频繁创建/销毁
3. **材质变体**: 支持预定义的材质变体（如：红色/蓝色/绿色版本）

---

## 12. 里程碑

| 里程碑 | 完成标准 | 预计时间 |
|--------|---------|---------|
| **M1: MeshInstance 类完成** | 基础结构和 API 实现 | 第 2 天 |
| **M2: 实例化功能完成** | createInstance() 可用 | 第 4 天 |
| **M3: 测试完成** | 单元测试 + 性能测试通过 | 第 6 天 |

---

**下一步**: 从步骤 1 开始实现，先完成基础结构
