# Filament 分离加载骨骼、网格和动画 - 技术设计文档

**版本**: 1.0
**日期**: 2025-01-13
**作者**: 基于 Filament 架构分析

---

## 目录
- [1. 方案概述](#1-方案概述)
- [2. 技术选型](#2-技术选型)
- [3. 架构设计](#3-架构设计)
- [4. API 设计](#4-api-设计)
- [5. 数据结构设计](#5-数据结构设计)
- [6. 实现要点](#6-实现要点)
- [7. 升级兼容性策略](#7-升级兼容性策略)
- [8. 使用示例](#8-使用示例)
- [9. 实施步骤](#9-实施步骤)

---

## 1. 方案概述

### 1.1 目标

实现 Filament 中骨骼、网格和动画的分离加载，支持：
- 从不同 glTF 文件加载骨骼结构、网格几何、动画数据
- 运行时自由组合和绑定
- 一个骨骼配多个网格
- 一个骨骼播放多个动画（混合）

### 1.2 理想使用方式

```cpp
// 1. 加载骨骼（核心资产）
SkeletonAsset* skeleton = loader->loadSkeleton("character_skeleton.glb");

// 2. 加载网格并绑定骨骼
MeshAsset* mesh = loader->loadMesh("character_mesh.glb");
mesh->bindSkeleton(skeleton);

// 3. 加载动画并绑定骨骼
AnimationAsset* walkAnim = loader->loadAnimation("walk.glb");
AnimationAsset* runAnim = loader->loadAnimation("run.glb");

// 4. 创建 Animator 播放动画
StandaloneAnimator* animator = StandaloneAnimator::create(engine);
animator->bindSkeleton(skeleton);
animator->playAnimation(walkAnim);

// 5. 渲染循环
while (rendering) {
    animator->update(deltaTime);
    mesh->updateSkinning(animator);
    renderer->render(view);
}
```

---

## 2. 技术选型

### 2.1 方案对比

| 方案 | 优点 | 缺点 | 适用场景 |
|------|------|------|----------|
| **A. 外部插件** | 完全独立、升级无忧 | 重复实现解析逻辑 | 第三方库 |
| **B. Fork 修改** | 完全控制 | 合并冲突、维护成本高 | 深度定制 |
| **C. 官方 PR** | 长期最优 | 周期长、不确定性 | 通用功能 |
| **D. 扩展库（推荐）** | 平衡可控性和兼容性 | 需谨慎管理内部依赖 | **当前需求** |

### 2.2 最终选择：方案 D - 独立扩展库

**核心理念**: "在 Filament 内部，但不是 Filament"

```
filament/
├── libs/
│   ├── gltfio/              # 原有库（不修改）
│   └── gltfio_ext/          # 新增扩展库 ⭐
│       ├── include/gltfio/
│       │   ├── SkeletonAsset.h
│       │   ├── MeshAsset.h
│       │   ├── AnimationAsset.h
│       │   └── StandaloneAnimator.h
│       ├── src/
│       │   └── ... (实现)
│       └── CMakeLists.txt
```

**优势**：
- ✅ 复用 gltfio 的 cgltf 解析能力
- ✅ 使用 Filament 内部工具（Engine、EntityManager）
- ✅ 独立编译，不影响原有代码
- ✅ 预留向官方贡献的路径

---

## 3. 架构设计

### 3.1 核心组件关系图

```
┌─────────────────┐
│  SkeletonAsset  │──┐
│  - Bone Tree    │  │
│  - Inv Matrices │  │  bindSkeleton()
└─────────────────┘  │
                     ↓
┌─────────────────┐  ┌──────────────────┐
│ AnimationAsset  │→ │StandaloneAnimator│
│  - Channels     │  │  - Transform     │
│  - Keyframes    │  │  - Bone Matrices │
└─────────────────┘  └──────────────────┘
                              │ applyTo()
                              ↓
                     ┌─────────────────┐
                     │    MeshAsset    │
                     │  - VertexBuffer │
                     │  - Renderable   │
                     └─────────────────┘
```

### 3.2 数据流

```
glTF File (skeleton.glb)
    ↓ [AssetLoaderExt::loadSkeleton()]
SkeletonAsset {
    - Entity[] mBoneEntities
    - mat4f[] mInverseBindMatrices
    - map<string, size_t> mBoneNameMap
}
    ↓ [bindSkeleton()]
StandaloneAnimator ←── AnimationAsset (walk.glb)
    ↓ [update(dt)]
Bone Transforms (mat4f[])
    ↓ [updateSkinning()]
RenderableManager::setBones()
```

---

## 4. API 设计

### 4.1 SkeletonAsset

**文件**: `libs/gltfio_ext/include/gltfio/SkeletonAsset.h`

```cpp
namespace filament::gltfio {

/**
 * 骨骼资产 - 管理骨骼层次结构和绑定姿态
 */
class UTILS_PUBLIC SkeletonAsset {
public:
    /**
     * 获取骨骼数量
     */
    size_t getBoneCount() const noexcept;

    /**
     * 通过名称获取骨骼索引
     * @return 如果骨骼不存在返回 -1
     */
    int getBoneIndex(const char* boneName) const noexcept;

    /**
     * 获取骨骼名称
     */
    const char* getBoneName(size_t boneIndex) const noexcept;

    /**
     * 获取骨骼 Entity（用于 TransformManager）
     */
    utils::Entity getBoneEntity(size_t boneIndex) const noexcept;

    /**
     * 获取逆绑定矩阵
     */
    const math::mat4f* getInverseBindMatrices() const noexcept;

    /**
     * 获取根骨骼 Entity
     */
    utils::Entity getRoot() const noexcept;

    /**
     * 验证骨骼索引是否在有效范围内（用于 BONE_INDICES 验证）
     */
    bool validateBoneIndices(const uint16_t* indices, size_t count) const noexcept;

private:
    friend class AssetLoaderExt;
    SkeletonAsset() = default;
    ~SkeletonAsset() = default;
};

} // namespace filament::gltfio
```

### 4.2 MeshAsset

**文件**: `libs/gltfio_ext/include/gltfio/MeshAsset.h`

```cpp
namespace filament::gltfio {

/**
 * 网格资产 - 管理几何数据和蒙皮绑定
 */
class UTILS_PUBLIC MeshAsset {
public:
    /**
     * 绑定骨骼到此网格
     * 会验证 BONE_INDICES 的有效性
     * @return true 如果绑定成功
     */
    bool bindSkeleton(SkeletonAsset* skeleton) noexcept;

    /**
     * 获取当前绑定的骨骼
     */
    SkeletonAsset* getBoundSkeleton() const noexcept;

    /**
     * 应用动画器计算的骨骼变换到 Renderable
     * 内部调用 RenderableManager::setBones()
     */
    void updateSkinning(const StandaloneAnimator* animator) noexcept;

    /**
     * 获取渲染实体
     */
    utils::Entity getRenderableEntity() const noexcept;

    /**
     * 获取 RenderableManager 实例
     */
    RenderableManager::Instance getRenderableInstance() const noexcept;

    /**
     * 获取 AABB（对象空间）
     */
    filament::Aabb getBoundingBox() const noexcept;

private:
    friend class AssetLoaderExt;
    MeshAsset() = default;
    ~MeshAsset() = default;
};

} // namespace filament::gltfio
```

### 4.3 AnimationAsset

**文件**: `libs/gltfio_ext/include/gltfio/AnimationAsset.h`

```cpp
namespace filament::gltfio {

/**
 * 动画资产 - 存储动画通道和关键帧数据
 */
class UTILS_PUBLIC AnimationAsset {
public:
    /**
     * 获取动画名称
     */
    const char* getName() const noexcept;

    /**
     * 获取动画时长（秒）
     */
    float getDuration() const noexcept;

    /**
     * 获取动画通道数量
     */
    size_t getChannelCount() const noexcept;

    /**
     * 获取通道目标骨骼名称
     */
    const char* getChannelTargetBone(size_t channelIndex) const noexcept;

    /**
     * 获取通道类型（TRANSLATION, ROTATION, SCALE）
     */
    enum class ChannelType { TRANSLATION, ROTATION, SCALE, WEIGHTS };
    ChannelType getChannelType(size_t channelIndex) const noexcept;

private:
    friend class AssetLoaderExt;
    friend class StandaloneAnimator;
    friend class AnimationBlender;
    AnimationAsset() = default;
    ~AnimationAsset() = default;
};

} // namespace filament::gltfio
```

### 4.3.1 AnimationPack（批量动画包）⭐ 新增

**文件**: `libs/gltfio_ext/include/gltfio/AnimationPack.h`

```cpp
namespace filament::gltfio {

/**
 * 动画包 - 从单个 glTF 文件加载多个动画
 *
 * 用途：减少文件 IO 次数，一次加载 20+ 个动画
 * 示例：attack_pack.glb 包含 attack1, attack2, attack3...attack20
 */
class UTILS_PUBLIC AnimationPack {
public:
    /**
     * 获取动画包中的动画数量
     */
    size_t getAnimationCount() const noexcept;

    /**
     * 通过索引获取动画
     */
    AnimationAsset* getAnimation(size_t index) noexcept;

    /**
     * 通过名称查找动画
     * @return 找不到返回 nullptr
     */
    AnimationAsset* findAnimation(const char* name) noexcept;

    /**
     * 获取所有动画名称列表
     */
    const char* const* getAnimationNames() const noexcept;

private:
    friend class AssetLoaderExt;
    AnimationPack() = default;
    ~AnimationPack() = default;
};

} // namespace filament::gltfio
```

### 4.4 StandaloneAnimator

**文件**: `libs/gltfio_ext/include/gltfio/StandaloneAnimator.h`

```cpp
namespace filament::gltfio {

/**
 * 独立动画器 - 不依赖 FilamentAsset 的动画系统
 */
class UTILS_PUBLIC StandaloneAnimator {
public:
    /**
     * 创建动画器
     */
    static StandaloneAnimator* create(Engine& engine);

    /**
     * 销毁动画器
     */
    static void destroy(StandaloneAnimator** animator);

    /**
     * 绑定骨骼
     * 必须在播放动画之前调用
     */
    void bindSkeleton(SkeletonAsset* skeleton) noexcept;

    /**
     * 播放动画
     * @param animation 动画资产
     * @param weight 混合权重（默认 1.0）
     * @param loop 是否循环（默认 true）
     * @return 动画 ID，用于后续控制
     */
    int playAnimation(AnimationAsset* animation, float weight = 1.0f, bool loop = true) noexcept;

    /**
     * 停止动画
     */
    void stopAnimation(int animationId) noexcept;

    /**
     * 设置动画权重（用于混合）
     */
    void setAnimationWeight(int animationId, float weight) noexcept;

    /**
     * 更新动画（每帧调用）
     * @param deltaTime 时间增量（秒）
     */
    void update(float deltaTime) noexcept;

    /**
     * 获取当前骨骼变换矩阵
     * 格式：世界空间骨骼变换 × 逆绑定矩阵
     */
    const math::mat4f* getBoneMatrices() const noexcept;

    /**
     * 获取骨骼数量
     */
    size_t getBoneCount() const noexcept;

    /**
     * 重置到绑定姿态
     */
    void resetToBindPose() noexcept;

private:
    StandaloneAnimator(Engine& engine);
    ~StandaloneAnimator();
};

} // namespace filament::gltfio
```

### 4.5 AnimationBlender（动画混合器）⭐ 新增

**文件**: `libs/gltfio_ext/include/gltfio/AnimationBlender.h`

```cpp
namespace filament::gltfio {

/**
 * 动画混合器 - 封装复杂的动画过渡和混合逻辑
 *
 * 解决问题：
 * - 手动管理动画权重繁琐易错
 * - 平滑过渡需要计算插值
 * - 多层混合（如上半身/下半身分离）复杂
 */
class UTILS_PUBLIC AnimationBlender {
public:
    /**
     * 创建动画混合器
     */
    static AnimationBlender* create(StandaloneAnimator* animator);

    /**
     * 销毁混合器
     */
    static void destroy(AnimationBlender** blender);

    /**
     * 播放动画（替换当前动画）
     * @param animation 目标动画
     * @param transitionDuration 过渡时长（秒），0 表示立即切换
     * @param loop 是否循环
     */
    void playAnimation(AnimationAsset* animation, float transitionDuration = 0.3f, bool loop = true) noexcept;

    /**
     * 混合两个动画（适用于待机/移动混合）
     * @param anim1 动画 1
     * @param anim2 动画 2
     * @param blendFactor 混合因子 [0, 1]，0 完全是 anim1，1 完全是 anim2
     */
    void blendAnimations(AnimationAsset* anim1, AnimationAsset* anim2, float blendFactor) noexcept;

    /**
     * 添加附加动画（适用于表情、手势等叠加动画）
     * @param animation 附加动画
     * @param weight 权重 [0, 1]
     * @param maskBones 受影响的骨骼列表（nullptr 表示全部）
     * @param maskBoneCount 骨骼数量
     */
    void addAdditiveAnimation(AnimationAsset* animation, float weight = 1.0f,
                              const char* const* maskBones = nullptr, size_t maskBoneCount = 0) noexcept;

    /**
     * 停止所有动画
     */
    void stopAll() noexcept;

    /**
     * 更新混合器（每帧调用，内部调用 animator->update()）
     * @param deltaTime 时间增量（秒）
     */
    void update(float deltaTime) noexcept;

    /**
     * 获取当前播放的主动画
     */
    AnimationAsset* getCurrentAnimation() const noexcept;

    /**
     * 是否正在过渡中
     */
    bool isTransitioning() const noexcept;

private:
    AnimationBlender(StandaloneAnimator* animator);
    ~AnimationBlender();
};

} // namespace filament::gltfio
```

### 4.6 AssetLoaderExt（扩展加载器）

**文件**: `libs/gltfio_ext/include/gltfio/AssetLoaderExt.h`

```cpp
namespace filament::gltfio {

/**
 * 扩展资产加载器 - 支持分离加载
 */
class UTILS_PUBLIC AssetLoaderExt {
public:
    /**
     * 创建扩展加载器
     */
    static AssetLoaderExt* create(const AssetConfiguration& config);

    /**
     * 销毁加载器
     */
    static void destroy(AssetLoaderExt** loader);

    /**
     * 加载骨骼
     * 从 glTF 的 skin 定义中提取骨骼层次和逆绑定矩阵
     */
    SkeletonAsset* loadSkeleton(const uint8_t* bytes, uint32_t nbytes);

    /**
     * 加载网格
     * 提取几何数据和蒙皮属性（BONE_INDICES, BONE_WEIGHTS）
     */
    MeshAsset* loadMesh(const uint8_t* bytes, uint32_t nbytes);

    /**
     * 加载单个动画
     * 提取动画通道和关键帧数据
     */
    AnimationAsset* loadAnimation(const uint8_t* bytes, uint32_t nbytes);

    /**
     * ⭐ 新增：批量加载动画包（减少文件IO）
     * 从一个 glTF 文件中加载所有动画
     * @param bytes glTF 文件数据
     * @param nbytes 数据大小
     * @return 动画包对象，包含文件中的所有动画
     *
     * 典型用途：
     * - attack_pack.glb 包含 20 个攻击动画
     * - locomotion_pack.glb 包含 idle、walk、run、jump
     */
    AnimationPack* loadAnimationPack(const uint8_t* bytes, uint32_t nbytes);

    /**
     * 销毁骨骼资产
     */
    void destroySkeleton(SkeletonAsset* skeleton);

    /**
     * 销毁网格资产
     */
    void destroyMesh(MeshAsset* mesh);

    /**
     * 销毁动画资产
     */
    void destroyAnimation(AnimationAsset* animation);

    /**
     * 销毁动画包（会自动销毁包含的所有动画）
     */
    void destroyAnimationPack(AnimationPack* pack);

private:
    AssetLoaderExt(const AssetConfiguration& config);
    ~AssetLoaderExt();
};

} // namespace filament::gltfio
```

---

## 5. 数据结构设计

### 5.1 内部实现类

#### FSkeletonAsset

**文件**: `libs/gltfio_ext/src/FSkeletonAsset.h`

```cpp
struct FSkeletonAsset : public SkeletonAsset {
    Engine* mEngine;
    utils::EntityManager* mEntityManager;
    TransformManager* mTransformManager;

    // 骨骼层次结构
    utils::FixedCapacityVector<utils::Entity> mBoneEntities;
    utils::FixedCapacityVector<utils::CString> mBoneNames;
    std::unordered_map<std::string, size_t> mBoneNameToIndex;

    // 绑定姿态数据（自主存储，不依赖 cgltf）
    utils::FixedCapacityVector<math::mat4f> mInverseBindMatrices;

    // 根节点
    utils::Entity mRoot;

    // 从 cgltf_skin 加载
    void loadFromGltfSkin(const cgltf_skin* skin, const cgltf_data* data);

    ~FSkeletonAsset() {
        // 清理 Entities
        if (mEntityManager) {
            for (auto entity : mBoneEntities) {
                mEntityManager->destroy(entity);
            }
        }
    }
};
```

#### FMeshAsset

```cpp
struct FMeshAsset : public MeshAsset {
    Engine* mEngine;
    RenderableManager* mRenderableManager;

    // 几何数据（从 gltfio 复用）
    VertexBuffer* mVertexBuffer = nullptr;
    IndexBuffer* mIndexBuffer = nullptr;
    MaterialInstance* mMaterialInstance = nullptr;

    // 渲染实体
    utils::Entity mRenderableEntity;
    RenderableManager::Instance mRenderableInstance;

    // 蒙皮绑定
    SkeletonAsset* mBoundSkeleton = nullptr;

    // AABB
    filament::Aabb mBoundingBox;

    // 从 cgltf_mesh 加载
    void loadFromGltfMesh(const cgltf_mesh* mesh, const cgltf_data* data);

    ~FMeshAsset() {
        // 清理 Filament 资源
        if (mEngine) {
            mEngine->destroy(mVertexBuffer);
            mEngine->destroy(mIndexBuffer);
            mEngine->destroy(mMaterialInstance);
            mEngine->destroy(mRenderableEntity);
        }
    }
};
```

#### FAnimationAsset

```cpp
struct Sampler {
    std::map<float, size_t> times;
    std::vector<float> values;
    enum { LINEAR, STEP, CUBIC } interpolation;
};

struct Channel {
    const Sampler* sampler;
    std::string targetBoneName;  // 使用名称而非 Entity
    enum { TRANSLATION, ROTATION, SCALE, WEIGHTS } transformType;
};

struct FAnimationAsset : public AnimationAsset {
    utils::CString mName;
    float mDuration = 0.0f;

    // 动画数据（自主存储）
    std::vector<Sampler> mSamplers;
    std::vector<Channel> mChannels;

    // 从 cgltf_animation 加载
    void loadFromGltfAnimation(const cgltf_animation* anim);
};
```

#### FStandaloneAnimator

```cpp
struct AnimationState {
    AnimationAsset* animation;
    float time = 0.0f;
    float weight = 1.0f;
    bool loop = true;
    bool playing = true;
    int id;
};

struct FStandaloneAnimator : public StandaloneAnimator {
    Engine* mEngine;
    TransformManager* mTransformManager;

    // 绑定的骨骼
    SkeletonAsset* mSkeleton = nullptr;

    // 播放中的动画
    std::vector<AnimationState> mAnimations;
    int mNextAnimationId = 1;

    // 骨骼变换缓存
    std::vector<math::mat4f> mBoneMatrices;

    // 更新逻辑
    void update(float deltaTime);
    void applyAnimation(const AnimationState& state);
    void computeBoneMatrices();
};
```

---

## 6. 实现要点

### 6.1 骨骼加载 (loadSkeleton)

**核心流程**：

```cpp
SkeletonAsset* AssetLoaderExt::loadSkeleton(const uint8_t* bytes, uint32_t nbytes) {
    // 1. 解析 glTF
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse(&options, bytes, nbytes, &data);

    // 2. 找到第一个 skin
    if (data->skins_count == 0) return nullptr;
    const cgltf_skin* skin = &data->skins[0];

    // 3. 创建骨骼实体层次
    FSkeletonAsset* asset = new FSkeletonAsset();
    asset->mEngine = mEngine;
    asset->mBoneEntities.reserve(skin->joints_count);

    for (size_t i = 0; i < skin->joints_count; ++i) {
        const cgltf_node* joint = skin->joints[i];

        // 创建 Entity
        utils::Entity entity = mEntityManager->create();
        asset->mBoneEntities.push_back(entity);

        // 设置 Transform
        TransformManager::Instance ti = mTransformManager->getInstance(entity);
        // ... 从 joint->translation/rotation/scale 设置

        // 存储名称
        if (joint->name) {
            asset->mBoneNames.push_back(joint->name);
            asset->mBoneNameToIndex[joint->name] = i;
        }
    }

    // 4. 提取逆绑定矩阵
    const cgltf_accessor* ibm = skin->inverse_bind_matrices;
    asset->mInverseBindMatrices.resize(skin->joints_count);
    cgltf_accessor_unpack_floats(ibm,
        reinterpret_cast<float*>(asset->mInverseBindMatrices.data()),
        ibm->count * 16);

    // 5. 立即释放 cgltf（数据已复制）
    cgltf_free(data);

    return asset;
}
```

**关键点**：
- ✅ 数据自主复制（不依赖 cgltf_data 生命周期）
- ✅ 使用公开 API（EntityManager, TransformManager）
- ✅ 建立名称索引（便于动画绑定）

### 6.2 网格加载 (loadMesh)

**核心流程**：

```cpp
MeshAsset* AssetLoaderExt::loadMesh(const uint8_t* bytes, uint32_t nbytes) {
    // 1. 解析 glTF
    cgltf_data* data = nullptr;
    cgltf_parse(&options, bytes, nbytes, &data);

    const cgltf_mesh* mesh = &data->meshes[0];
    const cgltf_primitive* prim = &mesh->primitives[0];

    // 2. 创建 VertexBuffer（必须包含 BONE_INDICES, BONE_WEIGHTS）
    VertexBuffer::Builder vbb;
    vbb.vertexCount(prim->attributes[0].data->count);

    for (size_t i = 0; i < prim->attributes_count; ++i) {
        const cgltf_attribute& attr = prim->attributes[i];
        switch (attr.type) {
            case cgltf_attribute_type_position:
                vbb.attribute(VertexAttribute::POSITION, ...);
                break;
            case cgltf_attribute_type_joints:
                vbb.attribute(VertexAttribute::BONE_INDICES, ...);
                break;
            case cgltf_attribute_type_weights:
                vbb.attribute(VertexAttribute::BONE_WEIGHTS, ...);
                break;
        }
    }

    VertexBuffer* vb = vbb.build(*mEngine);

    // 3. 创建 IndexBuffer
    IndexBuffer* ib = IndexBuffer::Builder()
        .indexCount(prim->indices->count)
        .bufferType(IndexBuffer::IndexType::USHORT)
        .build(*mEngine);

    // 4. 创建 Renderable（启用 skinning）
    FMeshAsset* asset = new FMeshAsset();
    asset->mEngine = mEngine;
    asset->mVertexBuffer = vb;
    asset->mIndexBuffer = ib;
    asset->mRenderableEntity = mEntityManager->create();

    RenderableManager::Builder(1)
        .geometry(0, PrimitiveType::TRIANGLES, vb, ib)
        .material(0, defaultMaterial)
        .skinning(255)  // 预分配最大骨骼数
        .enableSkinningBuffers(false)  // 使用直接 setBones 模式
        .boundingBox(computeAABB(prim))
        .build(*mEngine, asset->mRenderableEntity);

    asset->mRenderableInstance =
        mRenderableManager->getInstance(asset->mRenderableEntity);

    cgltf_free(data);
    return asset;
}
```

**关键点**：
- ⚠️ 必须验证 BONE_INDICES 和 BONE_WEIGHTS 存在
- ✅ 预分配足够的骨骼槽位
- ✅ 使用 RenderableManager 公开 API

### 6.3 动画加载 (loadAnimation)

**核心流程**：

```cpp
AnimationAsset* AssetLoaderExt::loadAnimation(const uint8_t* bytes, uint32_t nbytes) {
    cgltf_data* data = nullptr;
    cgltf_parse(&options, bytes, nbytes, &data);

    const cgltf_animation* anim = &data->animations[0];

    FAnimationAsset* asset = new FAnimationAsset();
    asset->mName = anim->name ? anim->name : "";
    asset->mDuration = 0.0f;

    // 1. 加载 Samplers
    asset->mSamplers.resize(anim->samplers_count);
    for (size_t i = 0; i < anim->samplers_count; ++i) {
        const cgltf_animation_sampler& src = anim->samplers[i];
        Sampler& dst = asset->mSamplers[i];

        // 复制时间轴
        const cgltf_accessor* times = src.input;
        float* timeData = new float[times->count];
        cgltf_accessor_unpack_floats(times, timeData, times->count);
        for (size_t j = 0; j < times->count; ++j) {
            dst.times[timeData[j]] = j;
        }
        delete[] timeData;

        // 复制值
        const cgltf_accessor* values = src.output;
        dst.values.resize(values->count * cgltf_num_components(values->type));
        cgltf_accessor_unpack_floats(values, dst.values.data(), dst.values.size());

        // 设置插值类型
        dst.interpolation = (src.interpolation == cgltf_interpolation_type_linear)
            ? Sampler::LINEAR : Sampler::STEP;

        // 更新 duration
        if (!dst.times.empty()) {
            float maxTime = dst.times.rbegin()->first;
            asset->mDuration = std::max(asset->mDuration, maxTime);
        }
    }

    // 2. 加载 Channels（使用骨骼名称）
    for (size_t i = 0; i < anim->channels_count; ++i) {
        const cgltf_animation_channel& srcChan = anim->channels[i];

        Channel dstChan;
        dstChan.sampler = &asset->mSamplers[srcChan.sampler - anim->samplers];

        // ⭐ 关键：存储骨骼名称而非 Entity
        if (srcChan.target_node && srcChan.target_node->name) {
            dstChan.targetBoneName = srcChan.target_node->name;
        } else {
            continue;  // 跳过无名称的节点
        }

        // 设置变换类型
        switch (srcChan.target_path) {
            case cgltf_animation_path_type_translation:
                dstChan.transformType = Channel::TRANSLATION;
                break;
            case cgltf_animation_path_type_rotation:
                dstChan.transformType = Channel::ROTATION;
                break;
            case cgltf_animation_path_type_scale:
                dstChan.transformType = Channel::SCALE;
                break;
        }

        asset->mChannels.push_back(dstChan);
    }

    cgltf_free(data);
    return asset;
}
```

**关键点**：
- ⭐ **使用名称延迟绑定**（核心创新）
- ✅ 完全复制数据，不依赖 cgltf_data
- ✅ 支持多种插值类型

### 6.4 动画更新 (StandaloneAnimator::update)

**核心流程**：

```cpp
void FStandaloneAnimator::update(float deltaTime) {
    if (!mSkeleton) return;

    // 1. 更新所有动画时间
    for (auto& state : mAnimations) {
        if (!state.playing) continue;

        state.time += deltaTime;
        if (state.loop && state.time > state.animation->getDuration()) {
            state.time = fmod(state.time, state.animation->getDuration());
        }
    }

    // 2. 应用动画到骨骼变换（可能混合多个动画）
    mTransformManager->openLocalTransformTransaction();

    for (const auto& state : mAnimations) {
        if (!state.playing) continue;
        applyAnimation(state);
    }

    mTransformManager->commitLocalTransformTransaction();

    // 3. 计算最终骨骼矩阵
    computeBoneMatrices();
}

void FStandaloneAnimator::applyAnimation(const AnimationState& state) {
    FAnimationAsset* anim = static_cast<FAnimationAsset*>(state.animation);

    for (const Channel& channel : anim->mChannels) {
        // 通过名称查找骨骼 Entity
        int boneIndex = mSkeleton->getBoneIndex(channel.targetBoneName.c_str());
        if (boneIndex < 0) continue;

        utils::Entity boneEntity = mSkeleton->getBoneEntity(boneIndex);
        TransformManager::Instance ti = mTransformManager->getInstance(boneEntity);

        // 插值计算当前值
        const Sampler* sampler = channel.sampler;
        auto iter = sampler->times.lower_bound(state.time);

        // ... 插值逻辑（与 gltfio::Animator 类似）

        // 应用变换
        switch (channel.transformType) {
            case Channel::TRANSLATION:
                mTransformManager->setTransform(ti, /* interpolated transform */);
                break;
            case Channel::ROTATION:
                // ...
                break;
        }
    }
}

void FStandaloneAnimator::computeBoneMatrices() {
    size_t boneCount = mSkeleton->getBoneCount();
    mBoneMatrices.resize(boneCount);

    const math::mat4f* invBindMatrices = mSkeleton->getInverseBindMatrices();

    for (size_t i = 0; i < boneCount; ++i) {
        utils::Entity boneEntity = mSkeleton->getBoneEntity(i);
        TransformManager::Instance ti = mTransformManager->getInstance(boneEntity);

        // 骨骼矩阵 = 世界变换 × 逆绑定矩阵
        math::mat4f worldTransform = mTransformManager->getWorldTransform(ti);
        mBoneMatrices[i] = worldTransform * invBindMatrices[i];
    }
}
```

### 6.5 蒙皮更新 (MeshAsset::updateSkinning)

```cpp
void FMeshAsset::updateSkinning(const StandaloneAnimator* animator) {
    if (!mBoundSkeleton || !animator) return;

    const math::mat4f* boneMatrices = animator->getBoneMatrices();
    size_t boneCount = animator->getBoneCount();

    // 直接调用 RenderableManager 公开 API
    mRenderableManager->setBones(mRenderableInstance, boneMatrices, boneCount);
}
```

---

## 7. 升级兼容性策略

### 7.1 依赖隔离层

创建 `GltfioCompat.h` 封装内部依赖：

```cpp
// libs/gltfio_ext/src/GltfioCompat.h

namespace filament::gltfio::compat {

/**
 * 封装 cgltf 访问，隔离版本差异
 */
class GltfParser {
public:
    static cgltf_result parse(const uint8_t* data, size_t size, cgltf_data** out);
    static void free(cgltf_data* data);

    // 安全访问器
    static size_t getSkinJointCount(const cgltf_skin* skin);
    static const cgltf_node* getSkinJoint(const cgltf_skin* skin, size_t index);
    // ...
};

} // namespace filament::gltfio::compat
```

### 7.2 版本兼容性检查

```cpp
// 编译时检查
#if FILAMENT_VERSION_MAJOR >= 2
    // 使用新 API
#else
    // 使用旧 API
#endif
```

### 7.3 单元测试

```cpp
// libs/gltfio_ext/tests/CompatibilityTest.cpp

TEST(GltfioExt, FilamentVersionCompatibility) {
    // 验证关键 API 存在
    EXPECT_TRUE(hasRenderableManagerSetBones());
    EXPECT_TRUE(hasTransformManagerGetWorldTransform());
    EXPECT_TRUE(hasSkinningBuffer());
}
```

### 7.4 升级检查清单

```markdown
## Filament 升级检查清单

### 每次升级前
- [ ] 检查 Release Notes 中的 Breaking Changes
- [ ] 搜索 "RenderableManager", "TransformManager", "SkinningBuffer" 相关变更
- [ ] 运行兼容性测试套件

### 升级后
- [ ] 编译 gltfio_ext 库
- [ ] 运行单元测试
- [ ] 运行示例程序
- [ ] 性能基准测试

### 如果测试失败
1. 检查 GltfioCompat.h 是否需要更新
2. 查看 Filament 迁移指南
3. 更新受影响的 API 调用
```

---

## 8. 使用示例

### 8.1 完整示例

**文件**: `samples/separate_loading_example.cpp`

```cpp
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Renderer.h>

#include <gltfio/AssetLoaderExt.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/MeshAsset.h>
#include <gltfio/AnimationAsset.h>
#include <gltfio/StandaloneAnimator.h>

using namespace filament;
using namespace filament::gltfio;

int main() {
    // 1. 初始化 Filament
    Engine* engine = Engine::create();
    Scene* scene = engine->createScene();

    // 2. 创建加载器
    AssetConfiguration config {
        .engine = engine,
        .materials = createUbershaderProvider(engine)
    };
    AssetLoaderExt* loader = AssetLoaderExt::create(config);

    // 3. 加载资产
    std::vector<uint8_t> skeletonData = readFile("character_skeleton.glb");
    SkeletonAsset* skeleton = loader->loadSkeleton(
        skeletonData.data(), skeletonData.size()
    );

    std::vector<uint8_t> meshData = readFile("character_mesh.glb");
    MeshAsset* mesh = loader->loadMesh(meshData.data(), meshData.size());

    std::vector<uint8_t> walkData = readFile("walk_animation.glb");
    AnimationAsset* walkAnim = loader->loadAnimation(walkData.data(), walkData.size());

    // 4. 绑定骨骼
    if (!mesh->bindSkeleton(skeleton)) {
        std::cerr << "Failed to bind skeleton!" << std::endl;
        return -1;
    }

    // 5. 创建动画器
    StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
    animator->bindSkeleton(skeleton);
    animator->playAnimation(walkAnim, 1.0f, true);

    // 6. 添加到场景
    scene->addEntity(mesh->getRenderableEntity());

    // 7. 渲染循环
    auto startTime = std::chrono::high_resolution_clock::now();
    while (!shouldClose) {
        auto now = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(now - startTime).count();
        startTime = now;

        // 更新动画
        animator->update(deltaTime);
        mesh->updateSkinning(animator);

        // 渲染
        if (renderer->beginFrame(swapChain)) {
            renderer->render(view);
            renderer->endFrame();
        }
    }

    // 8. 清理
    scene->removeEntity(mesh->getRenderableEntity());

    StandaloneAnimator::destroy(&animator);
    loader->destroySkeleton(skeleton);
    loader->destroyMesh(mesh);
    loader->destroyAnimation(walkAnim);

    AssetLoaderExt::destroy(&loader);
    Engine::destroy(&engine);

    return 0;
}
```

### 8.2 ⭐ 新增：批量加载动画包

```cpp
// 一次加载多个动画（减少文件IO）
std::vector<uint8_t> packData = readFile("attack_animations.glb");
AnimationPack* attackPack = loader->loadAnimationPack(packData.data(), packData.size());

// 查看包含的动画
std::cout << "Loaded " << attackPack->getAnimationCount() << " animations:" << std::endl;
for (size_t i = 0; i < attackPack->getAnimationCount(); ++i) {
    std::cout << "  - " << attackPack->getAnimationNames()[i] << std::endl;
}

// 通过名称获取特定动画
AnimationAsset* attack1 = attackPack->findAnimation("attack_light");
AnimationAsset* attack2 = attackPack->findAnimation("attack_heavy");

// 使用动画
animator->playAnimation(attack1);

// 清理（会自动销毁所有动画）
loader->destroyAnimationPack(attackPack);
```

### 8.3 ⭐ 新增：使用 AnimationBlender 简化动画过渡

**原始方式（繁琐）**：
```cpp
// 手动管理权重
int idleId = animator->playAnimation(idleAnim, 1.0f);
int walkId = animator->playAnimation(walkAnim, 0.0f);

// 过渡需要手动计算
float transition = 0.0f;
while (transitioning) {
    transition = std::min(transition + deltaTime / 0.3f, 1.0f);
    animator->setAnimationWeight(idleId, 1.0f - transition);
    animator->setAnimationWeight(walkId, transition);
    animator->update(deltaTime);
}
```

**改进方式（使用 AnimationBlender）**：
```cpp
// 创建混合器
AnimationBlender* blender = AnimationBlender::create(animator);

// 播放动画（自动处理 0.3 秒过渡）
blender->playAnimation(idleAnim, 0.3f);

// 切换到走路（自动淡入淡出）
blender->playAnimation(walkAnim, 0.3f);

// 每帧只需调用一次
blender->update(deltaTime);
```

### 8.4 高级用法：动画混合

```cpp
// 方式1：混合两个动画（如待机→移动）
AnimationBlender* blender = AnimationBlender::create(animator);
float speed = 0.5f;  // 移动速度 [0, 1]
blender->blendAnimations(idleAnim, walkAnim, speed);
blender->update(deltaTime);

// 方式2：添加附加动画（如表情、手势）
blender->playAnimation(walkAnim);  // 基础动画
blender->addAdditiveAnimation(waveAnim, 1.0f);  // 叠加挥手

// 方式3：骨骼遮罩（上半身播放不同动画）
const char* upperBodyBones[] = { "spine", "chest", "arm_L", "arm_R", "head" };
blender->playAnimation(walkAnim);  // 下半身走路
blender->addAdditiveAnimation(shootAnim, 1.0f, upperBodyBones, 5);  // 上半身射击
```

### 8.5 多角色共享骨骼

```cpp
// 一个骨骼，多个网格
SkeletonAsset* skeleton = loader->loadSkeleton("base_skeleton.glb");

MeshAsset* mesh1 = loader->loadMesh("character_a_mesh.glb");
MeshAsset* mesh2 = loader->loadMesh("character_b_mesh.glb");

mesh1->bindSkeleton(skeleton);
mesh2->bindSkeleton(skeleton);

// 同一个动画器驱动多个网格
animator->bindSkeleton(skeleton);
animator->playAnimation(walkAnim);

while (rendering) {
    animator->update(deltaTime);
    mesh1->updateSkinning(animator);
    mesh2->updateSkinning(animator);
}
```

---

## 9. 实施步骤

### 阶段 1: 基础框架（1-2 天）

```
✅ 任务 1.1: 创建目录结构
   - mkdir -p libs/gltfio_ext/{include/gltfio,src,tests}
   - 创建 CMakeLists.txt

✅ 任务 1.2: 定义公开 API
   - SkeletonAsset.h
   - MeshAsset.h
   - AnimationAsset.h
   - StandaloneAnimator.h
   - AssetLoaderExt.h

✅ 任务 1.3: 定义内部实现类
   - FSkeletonAsset.h
   - FMeshAsset.h
   - FAnimationAsset.h
   - FStandaloneAnimator.h
```

### 阶段 2: 骨骼系统（2-3 天）

```
✅ 任务 2.1: 实现 SkeletonAsset
   - loadFromGltfSkin()
   - 骨骼层次构建
   - 逆绑定矩阵提取

✅ 任务 2.2: 实现 AssetLoaderExt::loadSkeleton()
   - cgltf 解析
   - 数据复制
   - 单元测试

✅ 任务 2.3: 测试骨骼加载
   - 加载标准 glTF 文件（带 skin）
   - 验证骨骼数量
   - 验证逆绑定矩阵
```

### 阶段 3: 网格系统（2-3 天）

```
✅ 任务 3.1: 实现 MeshAsset
   - VertexBuffer/IndexBuffer 创建
   - Renderable 构建
   - bindSkeleton() 逻辑

✅ 任务 3.2: 实现 AssetLoaderExt::loadMesh()
   - 几何数据提取
   - 蒙皮属性验证
   - 单元测试

✅ 任务 3.3: 测试网格加载和绑定
   - 加载带 BONE_INDICES 的网格
   - 绑定到骨骼
   - 验证渲染正确
```

### 阶段 4: 动画系统（3-4 天）

```
✅ 任务 4.1: 实现 AnimationAsset
   - Sampler/Channel 数据结构
   - loadFromGltfAnimation()
   - 名称映射

✅ 任务 4.2: 实现 StandaloneAnimator
   - update() 主循环
   - applyAnimation() 插值
   - computeBoneMatrices() 计算

✅ 任务 4.3: 实现 MeshAsset::updateSkinning()
   - RenderableManager::setBones() 调用

✅ 任务 4.4: 端到端测试
   - 完整加载流程
   - 播放动画
   - 验证视觉效果
```

### 阶段 5: 高级功能（2-3 天）

```
✅ 任务 5.1: 动画混合
   - 多动画权重系统
   - 平滑过渡

✅ 任务 5.2: 性能优化
   - 骨骼矩阵计算缓存
   - 减少 Transform 更新

✅ 任务 5.3: 错误处理
   - 骨骼名称不匹配
   - BONE_INDICES 越界
```

### 阶段 6: 文档和示例（1-2 天）

```
✅ 任务 6.1: 示例程序
   - samples/separate_loading_example.cpp
   - 资产准备（测试 glTF 文件）

✅ 任务 6.2: API 文档
   - Doxygen 注释
   - 使用指南

✅ 任务 6.3: 兼容性文档
   - 升级检查清单
   - 版本测试报告
```

---

## 10. 附录

### 10.1 关键 Filament API 参考

| API | 文件 | 稳定性 | 用途 |
|-----|------|--------|------|
| `Engine` | Engine.h | ⭐⭐⭐⭐⭐ | 核心引擎 |
| `EntityManager` | Entity.h | ⭐⭐⭐⭐⭐ | 实体管理 |
| `TransformManager` | TransformManager.h | ⭐⭐⭐⭐⭐ | 变换管理 |
| `RenderableManager` | RenderableManager.h | ⭐⭐⭐⭐⭐ | 渲染管理 |
| `RenderableManager::setBones` | RenderableManager.h | ⭐⭐⭐⭐⭐ | 设置骨骼 |
| `SkinningBuffer` | SkinningBuffer.h | ⭐⭐⭐⭐ | 骨骼缓冲 |
| `cgltf` | cgltf.h | ⭐⭐⭐⭐ | glTF 解析 |

### 10.2 测试用 glTF 文件准备

```bash
# 从一个完整的角色 glTF 拆分
# 1. skeleton.glb - 只保留 skin 和骨骼节点
# 2. mesh.glb - 只保留 mesh 和 BONE_INDICES/WEIGHTS
# 3. walk.glb - 只保留 animation

# 使用 glTF Pipeline 工具
npm install -g gltf-pipeline

gltf-pipeline -i character.glb -o skeleton.glb \
  --draco.compressionLevel 10 \
  --keepUnusedElements skin,nodes
```

### 10.3 性能基准

**目标**：
- 加载时间：< 100ms per asset
- 动画更新：< 1ms for 100 bones
- 内存占用：< 10MB per skeleton

---

**结语**

这个设计方案在保持 Filament 升级兼容性的同时，实现了灵活的分离加载功能。核心策略是：
1. 数据自主管理（不依赖 cgltf_data 生命周期）
2. 使用名称延迟绑定（解耦 Entity 依赖）
3. 接口隔离层（封装内部 API）
4. 独立编译（不修改原始代码）

可以先实现核心功能验证可行性，后续考虑向 Filament 官方贡献。
