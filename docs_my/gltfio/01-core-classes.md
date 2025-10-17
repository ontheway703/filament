# 核心类职责说明

本文档详细说明 gltfio 库中各个核心类的职责和作用。

---

## 1. AssetLoader

**位置**: `/libs/gltfio/include/gltfio/AssetLoader.h`

### 职责

AssetLoader 是 gltfio 的入口类,负责:

1. **解析 glTF/GLB 文件格式**
   - 调用 cgltf 库解析 JSON/二进制数据
   - 构建 cgltf_data 树状结构

2. **创建 FilamentAsset 对象**
   - 初始化资产根节点
   - 创建场景映射关系

3. **管理材质实例缓存**
   - MaterialInstanceCache: 跨资产复用材质
   - 减少材质实例创建开销

4. **管理网格缓存**
   - MeshCache: 同一网格被多个节点引用时共享
   - VertexBuffer/IndexBuffer 复用

5. **创建实例**
   - 支持单实例: `createAsset()`
   - 支持多实例: `createInstancedAsset()`
   - 后续添加实例: `createInstance()`

### 核心方法

```cpp
class AssetLoader {
    // 创建单实例资产
    FilamentAsset* createAsset(const uint8_t* bytes, uint32_t numBytes);

    // 创建多实例资产
    FilamentAsset* createInstancedAsset(
        const uint8_t* bytes, uint32_t numBytes,
        FilamentInstance** instances, size_t numInstances);

    // 为已有资产创建新实例
    FilamentInstance* createInstance(FilamentAsset* asset);

    // 销毁资产
    void destroyAsset(const FilamentAsset* asset);
};
```

### 内部实现 (FAssetLoader)

位置: `/libs/gltfio/src/AssetLoader.cpp`

```cpp
struct FAssetLoader : public AssetLoader {
    // 材质提供者
    MaterialProvider* mMaterialProvider;

    // 材质缓存
    MaterialInstanceCache mMaterialInstanceCache;

    // 实体管理器
    utils::EntityManager& mEntityManager;

    // 名称管理器
    utils::NameComponentManager* mNameManager;

    // 核心创建方法
    FFilamentAsset* createRootAsset(cgltf_data* srcAsset);
    void createInstances(FFilamentAsset* asset, size_t numInstances);
    FFilamentInstance* createInstance(FFilamentAsset* asset);

    // 第一次遍历: 创建 Primitives (VB/IB)
    void recursePrimitives(FFilamentAsset* asset);

    // 第二次遍历: 创建 Entities
    void recurseEntities(FFilamentAsset* asset, FFilamentInstance* instance);
};
```

---

## 2. FilamentAsset

**位置**: `/libs/gltfio/include/gltfio/FilamentAsset.h`

### 职责

FilamentAsset 代表一个加载的 glTF 资产,负责:

1. **持有所有 GPU 资源**
   - VertexBuffer: 顶点数据
   - IndexBuffer: 索引数据
   - Texture: 纹理
   - BufferObject: 通用缓冲区
   - MorphTargetBuffer: 变形目标

2. **管理实体层级**
   - Entity 列表 (所有节点)
   - 场景根节点映射
   - Entity 查询接口 (按名称/前缀)

3. **追踪资源加载状态**
   - DependencyGraph: 依赖关系图
   - 支持渐进式加载 (纹理逐步显示)

4. **管理所有实例**
   - 持有 Instance 列表
   - 资源在实例间共享

5. **提供资源查询接口**
   - 按名称查找 Entity
   - 按前缀查找 Entity
   - 查询 Morph Target 名称

### 核心方法

```cpp
class FilamentAsset {
    // 获取实体列表
    const Entity* getEntities() const noexcept;
    size_t getEntityCount() const noexcept;

    // 获取根节点
    Entity getRoot() const noexcept;

    // 查询接口
    Entity getFirstEntityByName(const char* name) noexcept;
    size_t getEntitiesByName(const char* name, Entity* entities, size_t maxCount) const;
    size_t getEntitiesByPrefix(const char* prefix, Entity* entities, size_t maxCount) const;

    // 渐进式加载
    Entity popRenderable() noexcept;
    size_t popRenderables(Entity* entities, size_t count) noexcept;

    // 资源管理
    void releaseSourceData() noexcept;
    const void* getSourceAsset() noexcept;

    // 实例管理
    FilamentInstance** getAssetInstances() noexcept;
    size_t getAssetInstanceCount() const noexcept;
};
```

### 内部实现 (FFilamentAsset)

位置: `/libs/gltfio/src/FFilamentAsset.h`

```cpp
struct FFilamentAsset : public FilamentAsset {
    // Filament 引擎
    Engine* const mEngine;

    // 实体管理
    std::vector<utils::Entity> mEntities;           // 所有实体
    std::vector<utils::Entity> mLightEntities;      // 灯光实体
    std::vector<utils::Entity> mCameraEntities;     // 相机实体
    size_t mRenderableCount = 0;

    // GPU 资源
    std::vector<VertexBuffer*> mVertexBuffers;
    std::vector<IndexBuffer*> mIndexBuffers;
    std::vector<BufferObject*> mBufferObjects;
    std::vector<MorphTargetBuffer*> mMorphTargetBuffers;

    // 纹理信息
    struct TextureInfo {
        std::vector<TextureSlot> bindings;  // 材质绑定
        Texture* texture;
        TextureProvider::TextureFlags flags;
        bool isOwner;
    };
    utils::FixedCapacityVector<TextureInfo> mTextures;

    // 蒙皮数据
    struct Skin {
        utils::CString name;
        utils::FixedCapacityVector<math::mat4f> inverseBindMatrices;
    };
    utils::FixedCapacityVector<Skin> mSkins;

    // 实例列表
    std::vector<FFilamentInstance*> mInstances;

    // cgltf 源数据
    struct SourceAsset {
        cgltf_data* hierarchy;
        DracoCache dracoCache;
        utils::FixedCapacityVector<uint8_t> glbData;
    };
    std::shared_ptr<SourceAsset> mSourceAsset;

    // 网格缓存
    using MeshCache = utils::FixedCapacityVector<utils::FixedCapacityVector<Primitive>>;
    MeshCache mMeshCache;

    // 依赖图 (渐进式加载)
    DependencyGraph mDependencyGraph;

    // 资源加载状态
    bool mResourcesLoaded = false;
};
```

---

## 3. FilamentInstance

**位置**: `/libs/gltfio/include/gltfio/FilamentInstance.h`

### 职责

FilamentInstance 代表资产的一个运行时实例,负责:

1. **管理实例独立状态**
   - 独立的 Entity 列表
   - 独立的 Transform 矩阵
   - 独立的动画状态

2. **持有材质实例**
   - 每个实例有独立的 MaterialInstance
   - 可以应用不同的材质参数

3. **管理蒙皮数据**
   - 关节 Entity 列表
   - 蒙皮目标列表
   - 绑定关系

4. **支持材质变体**
   - 切换预定义的材质变体
   - 动态材质切换

5. **提供动画接口**
   - 获取 Animator
   - 每个实例独立播放动画

### 核心方法

```cpp
class FilamentInstance {
    // 获取所属资产
    FilamentAsset const* getAsset() const noexcept;

    // 实体管理
    const utils::Entity* getEntities() const noexcept;
    size_t getEntityCount() const noexcept;
    utils::Entity getRoot() const noexcept;

    // 材质变体
    void applyMaterialVariant(size_t variantIndex) noexcept;
    size_t getMaterialVariantCount() const noexcept;
    const char* getMaterialVariantName(size_t variantIndex) const noexcept;

    // 动画
    Animator* getAnimator() noexcept;

    // 蒙皮
    size_t getSkinCount() const noexcept;
    const char* getSkinNameAt(size_t skinIndex) const noexcept;
    size_t getJointCountAt(size_t skinIndex) const noexcept;
    const utils::Entity* getJointsAt(size_t skinIndex) const noexcept;
    void attachSkin(size_t skinIndex, utils::Entity target) noexcept;
    void detachSkin(size_t skinIndex, utils::Entity target) noexcept;

    // 材质实例
    MaterialInstance* const* getMaterialInstances() noexcept;
    size_t getMaterialInstanceCount() const noexcept;
};
```

### 内部实现 (FFilamentInstance)

位置: `/libs/gltfio/src/FFilamentInstance.h`

```cpp
struct FFilamentInstance : public FilamentInstance {
    // 根节点
    const utils::Entity mRoot;

    // 所属资产
    FFilamentAsset const* mOwner;

    // 实体列表
    std::vector<utils::Entity> mEntities;

    // 节点映射 (cgltf_node → Entity)
    utils::FixedCapacityVector<utils::Entity> mNodeMap;

    // 材质变体
    struct Variant {
        utils::CString name;
        std::vector<VariantMapping> mappings;
    };
    utils::FixedCapacityVector<Variant> mVariants;

    // 动画器
    Animator* mAnimator = nullptr;

    // 蒙皮数据 (实例独立)
    struct Skin {
        utils::FixedCapacityVector<utils::Entity> joints;  // 关节实体
        tsl::robin_set<utils::Entity, utils::Entity::Hasher> targets;  // 蒙皮目标
    };
    utils::FixedCapacityVector<Skin> mSkins;

    // 材质实例
    utils::FixedCapacityVector<MaterialInstance*> mMaterialInstances;

    // 包围盒
    Aabb mBoundingBox;
};
```

---

## 4. ResourceLoader

**位置**: `/libs/gltfio/include/gltfio/ResourceLoader.h`

### 职责

ResourceLoader 负责加载和上传资源到 GPU:

1. **加载外部资源**
   - 从文件加载 Buffer 数据
   - 从 URI 缓存加载数据
   - 支持 base64 嵌入数据

2. **上传数据到 GPU**
   - VertexBuffer 数据上传
   - IndexBuffer 数据上传
   - BufferObject 数据上传

3. **纹理处理**
   - 调用 TextureProvider 解码图像
   - 支持多种格式 (PNG, JPEG, KTX2, DDS)
   - 创建 Filament Texture 对象

4. **生成几何数据**
   - 计算切线四元数 (Tangent Quaternion)
   - 并行计算优化

5. **蒙皮数据处理**
   - 读取逆绑定矩阵
   - 规范化蒙皮权重

6. **支持异步加载**
   - asyncBeginLoad: 开始异步加载
   - asyncUpdateLoad: 更新加载进度
   - asyncGetLoadProgress: 查询进度

### 核心方法

```cpp
class ResourceLoader {
    // 同步加载
    bool loadResources(FilamentAsset* asset);

    // 异步加载
    void asyncBeginLoad(FilamentAsset* asset);
    void asyncUpdateLoad();
    float asyncGetLoadProgress() const;
    void asyncCancelLoad();

    // 纹理提供者
    void addTextureProvider(const char* mimeType, TextureProvider* provider);

    // 资源 URI
    bool hasResourceData(const char* uri) const;
    void addResourceData(const char* uri, BufferDescriptor&& buffer);
    void evictResourceData();
};
```

### 内部实现

位置: `/libs/gltfio/src/ResourceLoader.cpp`

```cpp
struct ResourceLoaderImpl {
    Engine* mEngine;

    // 纹理提供者 (按 MIME 类型)
    std::unordered_map<std::string, TextureProvider*> mTextureProviders;

    // URI 数据缓存
    UriDataCacheHandle mUriDataCache;

    // 关键加载方法
    void uploadBuffers(FFilamentAsset* asset);
    void createTextures(FFilamentAsset* asset, bool async);
    void createSkins(FFilamentAsset* asset);
    void computeTangents(FFilamentAsset* asset);

    // 异步加载状态
    struct AsyncContext {
        FFilamentAsset* asset;
        std::atomic<size_t> texturesReady;
        size_t textureCount;
    };
};
```

---

## 5. Animator

**位置**: `/libs/gltfio/include/gltfio/Animator.h`

### 职责

Animator 负责播放 glTF 动画:

1. **应用骨骼动画**
   - 更新节点的 Transform 组件
   - 插值计算 (线性/球面/三次样条)

2. **计算蒙皮矩阵**
   - 计算骨骼世界矩阵
   - 应用逆绑定矩阵
   - 更新 RenderableManager 骨骼数据

3. **变形目标动画**
   - 更新 MorphTarget 权重
   - 支持多个变形目标混合

4. **动画混合**
   - 交叉淡入淡出 (Cross-Fade)
   - 多动画层混合

5. **动画查询**
   - 获取动画数量/名称
   - 获取动画时长
   - 获取动画通道信息

### 核心方法

```cpp
class Animator {
    // 应用动画
    void applyAnimation(size_t animationIndex, float time) const;
    void applyCrossFade(size_t previousAnimIndex, float previousAnimTime,
                       float alpha) const;

    // 更新骨骼
    void updateBoneMatrices();

    // 动画查询
    size_t getAnimationCount() const;
    const char* getAnimationName(size_t animationIndex) const;
    float getAnimationDuration(size_t animationIndex) const;

    // 重置动画
    void resetBoneMatrices();
};
```

### 内部实现 (AnimatorImpl)

位置: `/libs/gltfio/src/Animator.cpp`

```cpp
class AnimatorImpl : public Animator {
    FFilamentAsset* mAsset;
    FFilamentInstance* mInstance;

    // 动画数据
    struct AnimationData {
        utils::CString name;
        float duration;
        std::vector<Channel> channels;
    };
    std::vector<AnimationData> mAnimations;

    // 动画通道
    struct Channel {
        size_t nodeIndex;
        TransformType transformType;  // TRANSLATION/ROTATION/SCALE/WEIGHTS
        Interpolation interpolation;   // LINEAR/STEP/CUBICSPLINE
        std::vector<float> times;
        std::vector<float> values;
    };

    // 核心方法
    void applyAnimation(size_t index, float time);
    void updateBoneMatrices(FFilamentInstance* instance);
    void computeBoneMatrices(Skin const& skin, Entity const* joints);
};
```

---

## 类职责总结

| 类 | 主要职责 | 生命周期 | 拥有的资源 |
|----|---------|---------|----------|
| **AssetLoader** | 解析 glTF,创建资产和实例 | 全局单例 | MaterialInstanceCache |
| **FilamentAsset** | 管理 GPU 资源,持有实例 | 用户控制 | VB/IB/Texture/Entity |
| **FilamentInstance** | 管理实例状态,独立动画 | 由 Asset 持有 | MaterialInstance/Animator |
| **ResourceLoader** | 加载资源,上传 GPU | 临时使用 | TextureProvider |
| **Animator** | 播放动画,更新骨骼 | 由 Instance 持有 | 动画数据 |

---

## 关键设计模式

1. **Factory 模式**: AssetLoader 作为工厂创建 Asset 和 Instance
2. **Flyweight 模式**: 资源在多个 Instance 间共享 (VB/IB/Texture)
3. **Observer 模式**: DependencyGraph 追踪资源加载状态
4. **Strategy 模式**: TextureProvider 可插拔的纹理解码器
5. **Facade 模式**: ResourceLoader 封装复杂的资源加载流程
