# gltfio_ext 架构重审：基于"分离文件加载"场景

**审查日期**: 2025-10-16
**重审原因**: 基于用户澄清的设计目的重新评估
**设计前提**: gltfio_ext 用于加载**不同的文件**（skeleton.glb、mesh.glb、idle.glb），其余功能和 gltfio 保持一致

---

## 1. 设计前提重审

### 1.1 核心使用场景

**场景 1: 分离文件加载（核心设计目标）**

```cpp
// 加载不同的文件
AssetLoaderExt* loader = AssetLoaderExt::create(config);

SkeletonAsset* skeleton = loader->loadSkeleton(skeletonData, skeletonSize);  // skeleton.glb
MeshAsset* mesh = loader->loadMesh(meshData, meshSize);                      // mesh.glb
AnimationAsset* idle = loader->loadAnimation(idleData, idleSize);            // idle.glb
AnimationAsset* walk = loader->loadAnimation(walkData, walkSize);            // walk.glb

// 绑定和播放
mesh->uploadResources();
mesh->bindSkeleton(skeleton);
scene->addEntity(mesh->getRenderableEntity());

animator->bindSkeleton(skeleton);
animator->playAnimation(idle, 0, 1.0f, true);
```

**场景 2: 换装系统**

```cpp
SkeletonAsset* skeleton = loader->loadSkeleton(skeletonData, skeletonSize);  // 通用骨架

// 不同的装备 mesh，共享同一个 skeleton
MeshAsset* armor1 = loader->loadMesh(armor1Data, armor1Size);  // 装备1
MeshAsset* armor2 = loader->loadMesh(armor2Data, armor2Size);  // 装备2
MeshAsset* weapon1 = loader->loadMesh(weapon1Data, weapon1Size);  // 武器1

armor1->bindSkeleton(skeleton);
armor2->bindSkeleton(skeleton);
weapon1->bindSkeleton(skeleton);

// 切换装备
scene->removeEntity(armor1->getRenderableEntity());
scene->addEntity(armor2->getRenderableEntity());
```

**场景 3: 实例化（多次加载同一文件）**

```cpp
// 创建 10 个相同的敌人
std::vector<MeshAsset*> enemies;
for (int i = 0; i < 10; i++) {
    MeshAsset* mesh = loader->loadMesh(enemyData, enemySize);  // ⚠️ 同一文件
    mesh->uploadResources();
    mesh->bindSkeleton(skeleton);
    enemies.push_back(mesh);
}
```

---

## 2. 之前分析的纠正

### 2.1 ✅ 纠正：重复解析问题

**之前的判断**：
> ❌ AssetLoaderExt 每次 loadXxx() 都解析 glTF，浪费性能

**重新评估**：
如果使用场景是**不同文件**（skeleton.glb、mesh.glb、idle.glb），那么：
- ✅ **每个文件只解析一次，不存在重复解析问题**
- ✅ 设计合理

**但需要考虑**：
如果用户多次加载同一文件（场景 3：实例化），则：
- ❌ **会重复解析**（10 次加载 = 10 次解析）
- ❌ **需要提供可选的缓存机制**

---

### 2.2 ✅ 纠正：MeshCache 需求

**之前的判断**：
> ❌ gltfio_ext 没有 MeshCache，GPU 内存浪费

**重新评估**：
取决于使用场景：

| 场景 | 是否需要 MeshCache | 理由 |
|------|-------------------|------|
| **场景 1: 分离文件加载** | ❌ 不需要 | 每个文件只加载一次 |
| **场景 2: 换装系统** | ❌ 不需要 | 不同的 mesh 文件 |
| **场景 3: 实例化** | ✅ **需要** | 同一文件加载多次，浪费 GPU 内存 |

**建议**：
提供**可选的缓存机制**，而非强制缓存。

```cpp
// 方案 A: 用户自己管理缓存（当前可行）
std::unordered_map<std::string, MeshAsset*> meshCache;

MeshAsset* loadMeshCached(const std::string& path) {
    auto it = meshCache.find(path);
    if (it != meshCache.end()) {
        return it->second;  // 用户管理的缓存
    }

    MeshAsset* mesh = loader->loadMesh(fileData, size);
    meshCache[path] = mesh;
    return mesh;
}

// 方案 B: 提供可选的内置缓存（未来扩展）
AssetConfigurationExt config;
config.enableMeshCache = true;  // 可选开关
```

---

## 3. 真正的问题盘点

### 3.1 🔴 严重问题：纹理加载机制不明确

#### 问题描述

**mesh.glb 的材质定义**:
```json
{
  "materials": [{
    "pbrMetallicRoughness": {
      "baseColorTexture": { "index": 0 },
      "metallicRoughnessTexture": { "index": 1 }
    },
    "normalTexture": { "index": 2 }
  }],
  "textures": [
    { "source": 0 },
    { "source": 1 },
    { "source": 2 }
  ],
  "images": [
    { "uri": "textures/baseColor.png" },
    { "uri": "textures/metalRough.png" },
    { "uri": "textures/normal.png" }
  ]
}
```

**当前 gltfio_ext 实现**:

```cpp
// MeshAsset.cpp:465-491
MaterialKey matkey = getMaterialKey(prim->material, &uvmap, hasVertexColor);
MaterialInstance* mi = mMaterialProvider->createMaterialInstance(&matkey, &uvmap, name);

// ✅ MaterialKey 正确提取属性
matkey.hasBaseColorTexture = true;
matkey.hasNormalTexture = true;

// ✅ MaterialInstance 创建成功（Shader 支持纹理）

// ❌ 问题：纹理从未加载和绑定
//          mi 期望纹理，但纹理是 nullptr
//          结果：模型是黑色的
```

#### 设计疑问

**gltfio_ext 如何处理纹理？**

**假设 1: 用户需要手动使用 gltfio 的 ResourceLoader**
```cpp
// 用户代码（猜测）
MeshAsset* mesh = loaderExt->loadMesh(meshData, meshSize);

// ⚠️ 需要手动使用 gltfio 原生的 ResourceLoader 加载纹理？
ResourceLoader* resLoader = ResourceLoader::create({engine, normalizeSkinningWeights});
resLoader->loadResources(???);  // ❌ ResourceLoader 需要 FilamentAsset，不是 MeshAsset

// ❌ 这个方案行不通，API 不兼容
```

**假设 2: 依赖用户手动加载纹理并绑定**
```cpp
// 用户代码
MeshAsset* mesh = loaderExt->loadMesh(meshData, meshSize);

// 手动加载纹理
Texture* baseColorTex = loadTextureFromFile("textures/baseColor.png");
Texture* normalTex = loadTextureFromFile("textures/normal.png");

// 手动绑定到 MaterialInstance（❌ MeshAsset 没有提供这个 API）
for (size_t i = 0; i < mesh->getPrimitiveCount(); i++) {
    MaterialInstance* mi = mesh->getMaterialInstance(i);  // ❌ API 不存在
    mi->setParameter("baseColorMap", baseColorTex, sampler);
    mi->setParameter("normalMap", normalTex, sampler);
}
```

**假设 3: mesh.glb 不包含纹理，只有材质参数**
```cpp
// mesh.glb 只有材质参数（无纹理）
{
  "materials": [{
    "pbrMetallicRoughness": {
      "baseColorFactor": [0.8, 0.5, 0.3, 1.0],
      "metallicFactor": 1.0,
      "roughnessFactor": 0.5
    }
  }]
}

// ✅ 当前实现可以正确处理这种情况
mi->setParameter("baseColorFactor", float4(0.8, 0.5, 0.3, 1.0));
mi->setParameter("metallicFactor", 1.0f);
mi->setParameter("roughnessFactor", 0.5f);
```

#### 问题分析

**如果设计目标是"其余功能和 gltfio 保持一致"，那么：**

| 功能 | gltfio 原生 | gltfio_ext 当前 | 差距 |
|------|------------|----------------|------|
| 加载 Mesh | ✅ | ✅ | 一致 |
| 加载材质参数 | ✅ | ✅ | 一致 |
| **加载纹理** | ✅ ResourceLoader | ❌ **缺失** | **不一致** |
| 绑定纹理到材质 | ✅ 自动 | ❌ **缺失** | **不一致** |

**结论**：
- 🔴 **纹理加载机制缺失是严重问题**
- 🔴 **与"其余功能和 gltfio 保持一致"的设计目标不符**

---

### 3.2 🟡 中等问题：实例化场景缺少缓存

#### 问题场景

```cpp
// 创建 100 个相同的敌人
for (int i = 0; i < 100; i++) {
    MeshAsset* mesh = loader->loadMesh(enemyData, enemySize);
    mesh->uploadResources();
    mesh->bindSkeleton(skeleton);
    scene->addEntity(mesh->getRenderableEntity());
}

// ❌ 问题：
// 1. 重复解析 glTF 100 次（性能浪费）
// 2. 重复创建 VertexBuffer 100 次（GPU 内存浪费 100 倍）
// 3. 重复上传 GPU 数据 100 次（带宽浪费）
```

#### 对比 gltfio 原生

**gltfio 原生的解决方案**:
```cpp
// 加载一次
FilamentAsset* asset = loader->createAsset(enemyData, enemySize);
resourceLoader->loadResources(asset);

// 创建 100 个实例（共享 VertexBuffer）
for (int i = 0; i < 100; i++) {
    FilamentInstance* instance = asset->getInstance();  // ✅ 共享 GPU 资源
    scene->addEntities(instance->getEntities(), instance->getEntityCount());
}

// 结果：GPU 内存只用 1 倍，不是 100 倍
```

#### gltfio_ext 的设计困境

**问题**：
- gltfio_ext 的设计是**独立的 MeshAsset**，不是 FilamentAsset
- 没有 Asset + Instance 的分离架构

**可能的解决方案**：

**方案 A: 用户自己管理缓存**（当前可行）
```cpp
std::unordered_map<std::string, MeshAsset*> cache;

MeshAsset* getOrLoadMesh(const char* path) {
    auto it = cache.find(path);
    if (it != cache.end()) {
        return it->second;  // ✅ 复用
    }

    MeshAsset* mesh = loader->loadMesh(readFile(path));
    cache[path] = mesh;
    return mesh;
}

// 创建实例（共享 MeshAsset）
for (int i = 0; i < 100; i++) {
    MeshAsset* mesh = getOrLoadMesh("enemy.glb");  // ✅ 复用
    // ❌ 但所有实例共享同一个 Entity（无法独立控制位置）
}
```

**方案 B: 提供 MeshInstance（类似 FilamentInstance）**（未来扩展）
```cpp
class MeshAsset {
    // 创建独立实例（共享 VertexBuffer，独立 Entity）
    MeshInstance* createInstance();
};

// 用法
MeshAsset* mesh = loader->loadMesh(enemyData, enemySize);
mesh->uploadResources();
mesh->bindSkeleton(skeleton);

for (int i = 0; i < 100; i++) {
    MeshInstance* instance = mesh->createInstance();  // ✅ 共享 GPU 资源，独立 Entity
    scene->addEntity(instance->getRenderableEntity());
}
```

**方案 C: 在 AssetLoaderExt 中提供可选缓存**（推荐）
```cpp
struct AssetConfigurationExt {
    bool enableAssetCache = false;  // 可选缓存开关
};

// 如果启用缓存
AssetConfigurationExt config;
config.enableAssetCache = true;
AssetLoaderExt* loader = AssetLoaderExt::create(config);

// 第一次加载
MeshAsset* mesh1 = loader->loadMesh(enemyData, enemySize);  // 创建

// 第二次加载（相同数据）
MeshAsset* mesh2 = loader->loadMesh(enemyData, enemySize);  // ✅ 返回缓存的 mesh1

// 结果：mesh1 == mesh2，共享 GPU 资源
```

---

### 3.3 🟢 轻微问题：与 gltfio 原生的集成

#### 问题：MaterialProvider 的来源

**当前设计**:
```cpp
struct AssetConfigurationExt {
    MaterialProvider* materials;  // 从哪里来？
};
```

**用户需要先创建 gltfio 的 MaterialProvider**:
```cpp
// 必须依赖 gltfio 原生库
MaterialProvider* matProvider = createJitShaderProvider(engine);
// 或
MaterialProvider* matProvider = createUbershaderProvider(engine, ubershaderArchive);

// 然后传给 gltfio_ext
AssetConfigurationExt config;
config.engine = engine;
config.materials = matProvider;  // ✅ 来自 gltfio
AssetLoaderExt* loader = AssetLoaderExt::create(config);
```

**分析**：
- ✅ **设计合理**：复用 gltfio 的材质系统，避免重复实现
- ⚠️ **文档需要说明**：用户必须先创建 MaterialProvider
- ⚠️ **依赖关系**：gltfio_ext 依赖 gltfio（需要在文档中说明）

---

### 3.4 🟢 轻微问题：换装系统的绑定验证

#### 潜在问题

```cpp
// skeleton.glb: 53 bones
SkeletonAsset* skeleton = loader->loadSkeleton(skeletonData, skeletonSize);

// mesh1.glb: 使用 bone index 0-52（正确）
MeshAsset* mesh1 = loader->loadMesh(mesh1Data, mesh1Size);
mesh1->bindSkeleton(skeleton);  // ✅ 成功

// mesh2.glb: 使用 bone index 0-60（超出范围）
MeshAsset* mesh2 = loader->loadMesh(mesh2Data, mesh2Size);
mesh2->bindSkeleton(skeleton);  // ❌ 失败？还是运行时崩溃？
```

**当前实现** (MeshAsset.cpp:727-751):
```cpp
bool MeshAsset::bindSkeleton(SkeletonAsset* skeleton) noexcept {
    // ✅ 验证骨骼索引范围
    if (self->mMaxBoneIndex >= skeleton->getBoneCount()) {
        GLTFIO_EXT_WARN("Bone index out of range: maxBoneIndex=" << self->mMaxBoneIndex
                       << ", skeletonBoneCount=" << skeleton->getBoneCount());
        return false;  // ✅ 安全失败
    }

    self->mBoundSkeleton = skeleton;
    return true;
}
```

**分析**：
- ✅ **验证正确**：bindSkeleton 会检查骨骼索引范围
- ✅ **安全失败**：不兼容的 mesh 绑定会返回 false

---

## 4. 场景适配性分析

### 4.1 场景 1: 分离文件加载

**适配性**: ✅ **优秀**

**示例**:
```cpp
// character.glb = skeleton.glb + mesh.glb 分离
SkeletonAsset* skeleton = loader->loadSkeleton("skeleton.glb");
MeshAsset* mesh = loader->loadMesh("mesh.glb");

// animations/ 目录下多个动画文件
AnimationAsset* idle = loader->loadAnimation("animations/idle.glb");
AnimationAsset* walk = loader->loadAnimation("animations/walk.glb");
AnimationAsset* run = loader->loadAnimation("animations/run.glb");
```

**优点**：
- ✅ 避免加载不需要的资源（如只需要动画，不加载 mesh）
- ✅ 支持运行时动态加载/卸载动画（内存管理）
- ✅ 适合大型项目（资源模块化）

**缺点**：
- ⚠️ 纹理加载机制不明确（如果 mesh.glb 包含纹理）

---

### 4.2 场景 2: 换装系统

**适配性**: ✅ **优秀**

**示例**:
```cpp
SkeletonAsset* skeleton = loader->loadSkeleton("base_skeleton.glb");

// 不同的装备 mesh
MeshAsset* armor_A = loader->loadMesh("armors/heavy_armor.glb");
MeshAsset* armor_B = loader->loadMesh("armors/light_armor.glb");
MeshAsset* weapon_A = loader->loadMesh("weapons/sword.glb");
MeshAsset* weapon_B = loader->loadMesh("weapons/axe.glb");

// 所有 mesh 共享同一个 skeleton
armor_A->bindSkeleton(skeleton);
armor_B->bindSkeleton(skeleton);
weapon_A->bindSkeleton(skeleton);
weapon_B->bindSkeleton(skeleton);

// 切换装备
scene->removeEntity(armor_A->getRenderableEntity());
scene->addEntity(armor_B->getRenderableEntity());
```

**优点**：
- ✅ Mesh 和 Skeleton 解耦
- ✅ 支持运行时切换装备
- ✅ 适合游戏换装系统

---

### 4.3 场景 3: 实例化

**适配性**: ⚠️ **需要用户自己管理缓存**

**当前用法**:
```cpp
// ❌ 错误用法（每次都创建新的 VertexBuffer）
for (int i = 0; i < 100; i++) {
    MeshAsset* mesh = loader->loadMesh(enemyData, enemySize);
    // GPU 内存浪费 100 倍
}

// ✅ 正确用法（用户自己缓存）
MeshAsset* sharedMesh = loader->loadMesh(enemyData, enemySize);
sharedMesh->uploadResources();
sharedMesh->bindSkeleton(skeleton);

for (int i = 0; i < 100; i++) {
    // ⚠️ 所有实例共享同一个 Entity（无法独立控制位置）
    // 需要用户自己管理 Transform
}
```

**缺点**：
- ⚠️ 需要用户自己管理缓存（容易出错）
- ⚠️ 所有实例共享同一个 Entity（限制了独立控制）

**建议优化**：
提供 MeshInstance 或内置缓存机制（见 3.2 的方案 B/C）

---

### 4.4 场景 4: 与 gltfio 原生混用

**适配性**: ✅ **可以共存**

**示例**:
```cpp
// gltfio 原生：加载完整的静态场景
AssetLoader* nativeLoader = AssetLoader::create({engine});
FilamentAsset* scene = nativeLoader->createAsset("level.glb");
ResourceLoader* resLoader = ResourceLoader::create({engine});
resLoader->loadResources(scene);

// gltfio_ext：加载角色的分离资源
MaterialProvider* matProvider = scene->getMaterialProvider();  // 复用
AssetLoaderExt* extLoader = AssetLoaderExt::create({engine, matProvider});
SkeletonAsset* charSkeleton = extLoader->loadSkeleton("character_skeleton.glb");
MeshAsset* charMesh = extLoader->loadMesh("character_mesh.glb");

// 混合使用
scene->getRoot();  // 场景根节点
charMesh->getRenderableEntity();  // 角色实体
```

**优点**：
- ✅ 可以混用（不冲突）
- ✅ 可以复用 MaterialProvider

---

## 5. 与 gltfio 原生的功能对比

### 5.1 核心功能对比表

| 功能 | gltfio 原生 | gltfio_ext | 备注 |
|------|------------|-----------|------|
| **资源加载** |
| 整体加载 | ✅ | ❌ | 设计目标不同 |
| 模块化加载 | ❌ | ✅ | gltfio_ext 优势 |
| 换装系统 | ❌ | ✅ | gltfio_ext 优势 |
| **Mesh 处理** |
| Skinned Mesh | ✅ | ✅ | 功能一致 |
| Static Mesh | ✅ | ❌ | gltfio_ext 只加载 skinned primitives |
| Morph Targets | ✅ | ❌ | gltfio_ext 缺失 |
| **材质系统** |
| MaterialKey 提取 | ✅ | ✅ | 功能一致 |
| 材质参数设置 | ✅ | ✅ | 功能一致 |
| **纹理加载** | ✅ ResourceLoader | ❌ **缺失** | 🔴 **严重问题** |
| 纹理缓存 | ✅ TextureCache | ❌ | gltfio_ext 缺失 |
| **动画系统** |
| 基础动画 | ✅ | ✅ | 功能一致 |
| TRS 处理 | ✅ TrsTransformManager | ✅ 手动分解/组合 | 实现不同，功能一致 |
| 动画混合 | ⚠️ 有限 | ✅ | gltfio_ext 更灵活 |
| CrossFade | ✅ | ❌ | gltfio_ext 缺失 |
| **GPU 交互** |
| Buffer 上传 | ✅ | ✅ | 功能一致 |
| 分帧上传 | ❌ | ✅ uploadNextBatch | gltfio_ext 优势 |
| 纹理异步解码 | ✅ TextureProvider | ❌ | gltfio_ext 缺失 |
| DependencyGraph | ✅ | ❌ | gltfio_ext 缺失 |
| **实例化** |
| Asset + Instance | ✅ | ❌ | gltfio 原生优势 |
| MeshCache | ✅ | ❌ | gltfio 原生优势 |
| **压缩支持** |
| Draco | ✅ | ❌ | gltfio_ext 缺失 |
| meshopt | ✅ | ❌ | gltfio_ext 缺失 |
| KTX2 | ✅ | ❌ | gltfio_ext 缺失 |

---

### 5.2 设计目标对齐检查

**声称**: "其余功能和 gltfio 保持一致"

**实际对比**:

| 功能类别 | 一致性 | 评分 |
|---------|--------|------|
| Mesh 处理（几何数据） | ✅ 一致 | 5/5 |
| 材质参数提取 | ✅ 一致 | 5/5 |
| **纹理加载** | ❌ **缺失** | 0/5 |
| 动画播放（基础） | ✅ 一致 | 5/5 |
| 动画混合 | ✅ 更好 | 5/5 |
| 骨骼蒙皮 | ✅ 一致 | 5/5 |
| 压缩格式 | ❌ 缺失 | 0/5 |

**综合评分**: 25/35 = **71%** 一致

**结论**:
- 🔴 **纹理加载缺失严重影响"功能一致"的目标**
- 其他核心功能（Mesh、材质参数、动画）基本一致

---

## 6. 真正的问题总结

基于"分离文件加载"的设计前提，重新评估问题优先级：

### 6.1 🔴 P0 - 严重问题（必须修复）

#### 问题 1: 纹理加载机制缺失

**影响**：
- ❌ mesh.glb 中的纹理无法加载
- ❌ 模型只有颜色，没有纹理细节
- ❌ 与"功能和 gltfio 保持一致"的目标不符

**用户反馈**:
```
❌ "为什么加载的模型是纯色的？"
❌ "glTF 文件在 Blender 里有纹理，但在我的应用里没有"
```

**修复方案**:

**方案 A: 集成 gltfio 的 ResourceLoader**（推荐）
```cpp
// 修改 AssetLoaderExt API
class AssetLoaderExt {
    MeshAsset* loadMesh(const uint8_t* bytes, uint32_t nbytes,
                       ResourceLoader* resLoader = nullptr);  // 可选参数
};

// 用户代码
ResourceLoader* resLoader = ResourceLoader::create({engine});
MeshAsset* mesh = loader->loadMesh(meshData, meshSize, resLoader);

// loadMesh 内部
if (resLoader) {
    // 使用 ResourceLoader 加载纹理（复用 gltfio 逻辑）
    resLoader->loadResources(/* ... */);
}
```

**方案 B: 在 MeshAsset 中添加纹理绑定 API**
```cpp
class MeshAsset {
    // 获取需要的纹理列表
    std::vector<TextureInfo> getRequiredTextures() const;

    // 绑定纹理
    void bindTexture(size_t primitiveIndex, const char* slot, Texture* texture);
};

// 用户代码
MeshAsset* mesh = loader->loadMesh(meshData, meshSize);
auto textures = mesh->getRequiredTextures();

for (const auto& texInfo : textures) {
    Texture* tex = loadTextureFromFile(texInfo.uri);
    mesh->bindTexture(texInfo.primitiveIndex, texInfo.slot, tex);
}
```

**方案 C: 提供独立的 TextureLoader**（中等推荐）
```cpp
class TextureLoaderExt {
    Texture* loadTexture(const uint8_t* data, size_t size, const char* mimeType);
};

// 用户代码
TextureLoaderExt* texLoader = TextureLoaderExt::create(engine);
MeshAsset* mesh = loader->loadMesh(meshData, meshSize);

// 手动加载纹理（从 mesh.glb 提取纹理数据）
// ⚠️ 用户需要自己解析 glTF 提取纹理（繁琐）
```

**推荐**: **方案 A**（最小化工作量，复用 gltfio）

---

#### 问题 2: Normal/Tangent 处理不完整

**影响**：
- ❌ 法线贴图无法正确显示
- ❌ PBR 材质细节丢失

**修复**: 参考 gltfio/src/AssetLoader.cpp:800-850 实现完整的 TBN quaternion

---

### 6.2 🟡 P1 - 重要优化（建议修复）

#### 问题 3: 实例化场景缺少缓存机制

**影响**：
- 场景 3（实例化）性能和内存浪费

**修复方案**：
- 提供可选的 MeshCache（参见 3.2）
- 或提供 MeshInstance 机制

---

#### 问题 4: 缺少高级动画功能

**影响**：
- CrossFade 缺失（动画过渡生硬）
- CUBIC 插值缺失（平滑度下降）

**修复**: 参考之前的报告（优先级 P1）

---

### 6.3 🟢 P2 - 可选优化（未来扩展）

#### 问题 5: 压缩格式不支持

**影响**：
- 文件体积大（Draco 可减少 80%）
- GPU 内存占用大（KTX2 可减少 75%）

**修复**: 集成 Draco/meshopt/KTX2（参考之前的报告）

---

#### 问题 6: Morph Targets 不支持

**影响**：
- 无法加载带面部动画的模型
- 限制应用场景

**修复**: 参考 gltfio/src/AssetLoader.cpp:700-750

---

## 7. 使用建议和最佳实践

### 7.1 当前推荐用法

**场景 1: 分离文件加载（最佳适配）**

```cpp
// 1. 创建 loader
MaterialProvider* matProvider = createJitShaderProvider(engine);
AssetLoaderExt* loader = AssetLoaderExt::create({engine, matProvider});

// 2. 加载不同的文件
SkeletonAsset* skeleton = loader->loadSkeleton(readFile("skeleton.glb"));
MeshAsset* mesh = loader->loadMesh(readFile("mesh.glb"));
AnimationAsset* idle = loader->loadAnimation(readFile("idle.glb"));
AnimationAsset* walk = loader->loadAnimation(readFile("walk.glb"));

// 3. 绑定和播放
mesh->uploadResources();
mesh->bindSkeleton(skeleton);
scene->addEntity(mesh->getRenderableEntity());

animator->bindSkeleton(skeleton);
animator->playAnimation(idle, 0, 1.0f, true);
```

---

**场景 2: 换装系统（最佳适配）**

```cpp
// 加载通用骨架
SkeletonAsset* skeleton = loader->loadSkeleton(readFile("base_skeleton.glb"));

// 加载不同装备
std::vector<MeshAsset*> armors;
armors.push_back(loader->loadMesh(readFile("heavy_armor.glb")));
armors.push_back(loader->loadMesh(readFile("light_armor.glb")));

for (auto* armor : armors) {
    armor->uploadResources();
    armor->bindSkeleton(skeleton);
}

// 切换装备
void equipArmor(int index) {
    for (size_t i = 0; i < armors.size(); i++) {
        Entity e = armors[i]->getRenderableEntity();
        if (i == index) {
            scene->addEntity(e);  // 显示选中的
        } else {
            scene->removeEntity(e);  // 隐藏其他的
        }
    }
}
```

---

**场景 3: 实例化（需要用户管理缓存）**

```cpp
// ✅ 正确用法：用户管理缓存
std::unordered_map<std::string, MeshAsset*> meshCache;

MeshAsset* getOrLoadMesh(const char* path) {
    auto it = meshCache.find(path);
    if (it != meshCache.end()) {
        return it->second;  // 复用
    }

    MeshAsset* mesh = loader->loadMesh(readFile(path));
    mesh->uploadResources();
    mesh->bindSkeleton(skeleton);
    meshCache[path] = mesh;
    return mesh;
}

// 创建 100 个实例（共享 GPU 资源）
for (int i = 0; i < 100; i++) {
    MeshAsset* mesh = getOrLoadMesh("enemy.glb");  // 复用

    // ⚠️ 限制：所有实例共享同一个 Entity
    //          需要用户自己管理 Transform（如父子关系）
}
```

---

### 7.2 不推荐的用法

**❌ 错误 1: 重复加载同一文件（实例化）**

```cpp
// ❌ 每次都创建新的 VertexBuffer（GPU 内存浪费）
for (int i = 0; i < 100; i++) {
    MeshAsset* mesh = loader->loadMesh(enemyData, enemySize);
    mesh->uploadResources();
    // GPU 内存浪费 100 倍
}
```

**✅ 正确做法**：用户自己管理缓存（见 7.1）

---

**❌ 错误 2: 期望自动加载纹理**

```cpp
// ❌ 期望纹理自动加载
MeshAsset* mesh = loader->loadMesh(meshData, meshSize);
mesh->uploadResources();

// ❌ 结果：模型是黑色的（纹理未加载）
```

**✅ 临时方案**：使用只有材质参数的 mesh.glb（不包含纹理）

**✅ 未来方案**：等待纹理加载功能实现

---

## 8. 改进建议（优先级重排）

### 8.1 基于"分离文件加载"的优先级

| 优先级 | 改进项 | 工作量 | 影响场景 | 备注 |
|--------|--------|--------|---------|------|
| 🔴 **P0** | 集成纹理加载（方案 A：复用 ResourceLoader） | 中 | 所有场景 | 与 gltfio 保持一致 |
| 🔴 **P0** | 修复 Normal/Tangent 处理 | 小 | 所有场景 | 法线贴图生效 |
| 🟡 **P1** | 提供可选的 MeshCache 或 MeshInstance | 中 | 场景 3（实例化） | 避免 GPU 内存浪费 |
| 🟡 **P1** | 实现 CrossFade | 小 | 所有场景 | 动画过渡自然 |
| 🟡 **P1** | 实现 CUBIC 插值 | 小 | 所有场景 | 动画平滑度 |
| 🟢 **P2** | 支持 Draco 压缩 | 大 | 所有场景 | 文件体积优化 |
| 🟢 **P2** | 支持 Morph Targets | 中 | 面部动画场景 | 扩展应用场景 |
| 🟢 **P2** | 支持 KTX2 纹理压缩 | 大 | 所有场景 | GPU 内存优化 |

---

### 8.2 阶段性目标

#### 阶段 1: 基础可用（1-2 周，P0）
**目标**：与 gltfio 功能保持一致

- [ ] 集成纹理加载（复用 ResourceLoader）
- [ ] 修复 Normal/Tangent 处理
- [ ] 补充文档（说明 MaterialProvider 依赖）

**验收标准**：
- ✅ mesh.glb 的纹理可以正确显示
- ✅ 法线贴图生效
- ✅ 文档清晰说明使用方法

---

#### 阶段 2: 性能优化（2-3 周，P1）
**目标**：优化实例化场景

- [ ] 提供可选的 MeshCache
- [ ] 或提供 MeshInstance 机制
- [ ] 实现 CrossFade 和 CUBIC 插值

**验收标准**：
- ✅ 实例化场景 GPU 内存占用合理
- ✅ 动画过渡自然

---

#### 阶段 3: 高级功能（可选，P2）
**目标**：接近 gltfio 原生库

- [ ] 支持 Draco 压缩
- [ ] 支持 Morph Targets
- [ ] 支持 KTX2 纹理压缩

---

## 9. 总结

### 9.1 设计评估

**基于"分离文件加载"的设计前提**：

✅ **核心设计优秀**:
- 模块化加载架构合理
- 换装系统支持良好
- 动画系统实现正确
- 分帧上传设计优秀

❌ **关键功能缺失**:
- **纹理加载机制缺失**（严重影响可用性）
- 实例化场景缺少缓存（性能问题）

⚠️ **与设计目标的差距**:
- 声称"其余功能和 gltfio 保持一致"
- 实际一致性：**71%**（纹理加载缺失是主要差距）

---

### 9.2 适用场景评分

| 场景 | 适配性 | 评分 | 说明 |
|------|--------|------|------|
| 分离文件加载 | ✅ 优秀 | 5/5 | 核心设计目标，完美适配 |
| 换装系统 | ✅ 优秀 | 5/5 | Mesh/Skeleton 解耦，支持良好 |
| 实例化 | ⚠️ 需要用户缓存 | 3/5 | 缺少内置缓存机制 |
| 与 gltfio 混用 | ✅ 可以共存 | 4/5 | 可以复用 MaterialProvider |

---

### 9.3 最终建议

#### 对于开发者
1. 🔴 **优先修复纹理加载**（方案 A：复用 ResourceLoader）
2. 🔴 修复 Normal/Tangent 处理
3. 🟡 提供可选的缓存机制（MeshCache 或 MeshInstance）
4. 📝 补充文档（说明与 gltfio 的依赖关系）

#### 对于用户
1. **当前推荐场景**：
   - ✅ 分离文件加载（skeleton.glb、mesh.glb、idle.glb）
   - ✅ 换装系统
   - ⚠️ 实例化（需要自己管理缓存）

2. **当前限制**：
   - ❌ mesh.glb 的纹理无法自动加载（临时方案：只用材质参数）
   - ⚠️ 实例化需要用户管理缓存

3. **与 gltfio 原生的选择**：
   - 需要模块化加载/换装系统 → 使用 gltfio_ext
   - 需要完整纹理支持/压缩格式 → 使用 gltfio 原生
   - 两者可以混用（不冲突）

---

**报告结束**

补充说明：
- 之前报告中关于"重复解析"的问题判断有误，已纠正
- 重点问题是**纹理加载缺失**，优先修复
- 其他设计基本合理，符合"分离文件加载"的目标
