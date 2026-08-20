# 架构关系和资源共享

本文档详细说明 gltfio 的整体架构和资源共享模式。

---

## 整体架构图

```
┌──────────────────────────────────────────────────────────────────┐
│                          用户应用层                                 │
│                                                                    │
│  AssetLoader* loader = AssetLoader::create(...);                 │
│  FilamentAsset* asset = loader->createAsset(glbData);            │
│  ResourceLoader resLoader(...);                                   │
│  resLoader.loadResources(asset);                                  │
│  Animator* anim = asset->getInstance()->getAnimator();           │
└──────────────────────────────────────────────────────────────────┘
                              ↓
┌──────────────────────────────────────────────────────────────────┐
│                        gltfio 公开 API                             │
│                                                                    │
│  ┌──────────────┐  ┌─────────────────┐  ┌──────────────────┐    │
│  │ AssetLoader  │  │ ResourceLoader  │  │ MaterialProvider │    │
│  └──────────────┘  └─────────────────┘  └──────────────────┘    │
│  ┌──────────────┐  ┌─────────────────┐  ┌──────────────────┐    │
│  │FilamentAsset │  │FilamentInstance │  │ TextureProvider  │    │
│  └──────────────┘  └─────────────────┘  └──────────────────┘    │
│  ┌──────────────┐                                                │
│  │  Animator    │                                                │
│  └──────────────┘                                                │
└──────────────────────────────────────────────────────────────────┘
                              ↓
┌──────────────────────────────────────────────────────────────────┐
│                      gltfio 内部实现                               │
│                                                                    │
│  ┌───────────────┐  ┌──────────────────┐  ┌─────────────────┐   │
│  │ FAssetLoader  │→│ FFilamentAsset   │→│FFilamentInstance│   │
│  └───────────────┘  └──────────────────┘  └─────────────────┘   │
│         ↓                    ↓                      ↓            │
│  ┌───────────────┐  ┌──────────────────┐  ┌─────────────────┐   │
│  │MaterialInstance│  │   MeshCache     │  │  AnimatorImpl   │   │
│  │    Cache      │  │                 │  │                 │   │
│  └───────────────┘  └──────────────────┘  └─────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
                              ↓
┌──────────────────────────────────────────────────────────────────┐
│                      Filament 核心层                               │
│                                                                    │
│  ┌──────────┐  ┌────────────┐  ┌───────────┐  ┌──────────────┐  │
│  │  Engine  │  │VertexBuffer│  │IndexBuffer│  │MaterialInstance│  │
│  └──────────┘  └────────────┘  └───────────┘  └──────────────┘  │
│  ┌──────────┐  ┌────────────┐  ┌───────────┐  ┌──────────────┐  │
│  │ Texture  │  │BufferObject│  │  Entity   │  │TransformMgr  │  │
│  └──────────┘  └────────────┘  └───────────┘  └──────────────┘  │
│  ┌──────────┐  ┌────────────┐                                    │
│  │Renderable│  │ LightMgr   │                                    │
│  │  Mgr     │  │            │                                    │
│  └──────────┘  └────────────┘                                    │
└──────────────────────────────────────────────────────────────────┘
                              ↓
┌──────────────────────────────────────────────────────────────────┐
│                     外部依赖库                                      │
│                                                                    │
│  ┌──────────┐  ┌────────────┐  ┌───────────┐  ┌──────────────┐  │
│  │  cgltf   │  │   stb_image│  │  Draco    │  │   meshopt    │  │
│  │(glTF解析)│  │  (图像解码)│  │ (网格解压)│  │  (网格解压)  │  │
│  └──────────┘  └────────────┘  └───────────┘  └──────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

---

## 核心类关系图

```
                    ┌─────────────────┐
                    │  AssetLoader    │
                    │ (工厂类)         │
                    └────────┬────────┘
                             │ creates
                             ↓
                    ┌─────────────────┐
                    │ FilamentAsset   │◄────┐
                    │                 │     │
                    │ • mVertexBuffers│     │
                    │ • mIndexBuffers │     │ owns
                    │ • mTextures     │     │
                    │ • mSkins        │     │
                    │ • mInstances    │     │
                    └────────┬────────┘     │
                             │              │
                             │ owns (1:N)   │
                             ↓              │
                    ┌─────────────────┐     │
                    │FilamentInstance │─────┘
                    │                 │
                    │ • mRoot         │
                    │ • mEntities     │
                    │ • mSkins        │
                    │ • mAnimator     │
                    │ • mMaterialInst │
                    └────────┬────────┘
                             │
                             │ owns
                             ↓
                    ┌─────────────────┐
                    │    Animator     │
                    │                 │
                    │ • mAnimations   │
                    │ • mChannels     │
                    └─────────────────┘

    共享资源 (Asset)          独立状态 (Instance)
    ────────────────         ──────────────────
    • VertexBuffer           • Entity
    • IndexBuffer            • MaterialInstance
    • Texture                • Animator
    • 逆绑定矩阵              • 关节 Entity 列表
```

---

## 资源共享模式

### 1. 共享资源 (Asset 级别)

**共享的资源** (所有 Instance 复用):

| 资源类型 | 存储位置 | 共享原因 | 内存节省 |
|---------|---------|---------|---------|
| **VertexBuffer** | `asset->mVertexBuffers[]` | 顶点数据不变 | 极大 (10-100MB) |
| **IndexBuffer** | `asset->mIndexBuffers[]` | 索引数据不变 | 中等 (1-10MB) |
| **Texture** | `asset->mTextures[]` | 纹理数据不变 | 极大 (50-500MB) |
| **BufferObject** | `asset->mBufferObjects[]` | GPU 缓冲区不变 | 极大 |
| **逆绑定矩阵** | `asset->mSkins[].inverseBindMatrices` | 蒙皮绑定姿态不变 | 小 (几 KB) |

**内存效率示例**:

```
假设一个角色模型:
- 顶点数据: 20 MB
- 索引数据: 5 MB
- 纹理数据: 100 MB
- 总计: 125 MB

传统做法 (每实例复制所有数据):
3 个实例 × 125 MB = 375 MB

Filament 做法 (共享资源):
共享资源: 125 MB
独立状态: 3 × 10 KB = 30 KB
总计: 125.03 MB  (节省 66.7%)
```

### 2. 独立状态 (Instance 级别)

**每个 Instance 独立拥有**:

| 资源类型 | 存储位置 | 独立原因 | 大小 |
|---------|---------|---------|-----|
| **Entity** | `instance->mEntities[]` | 场景位置不同 | 小 (每个 8 字节) |
| **Transform** | TransformManager | 位置/旋转/缩放不同 | 小 (每个 64 字节) |
| **MaterialInstance** | `instance->mMaterialInstances[]` | 材质参数可能不同 | 中 (每个 几百字节) |
| **Animator** | `instance->mAnimator` | 动画状态不同 | 小 (几 KB) |
| **关节 Entity** | `instance->mSkins[].joints` | 骨骼实例不同 | 小 (几百字节) |
| **蒙皮目标** | `instance->mSkins[].targets` | 网格实例不同 | 小 |

**独立性示例**:

```cpp
// 3 个实例,不同位置,不同动画

FilamentAsset* asset = loader->createAsset(glbData);
resourceLoader.loadResources(asset);

// 实例 1: 位置 (0,0,0), 播放走路动画
FilamentInstance* inst1 = asset->getInstance();
transformManager.setTransform(inst1->getRoot(), mat4f::translation({0,0,0}));
inst1->getAnimator()->applyAnimation(0, time);  // 走路

// 实例 2: 位置 (5,0,0), 播放跑步动画
FilamentInstance* inst2 = loader->createInstance(asset);
transformManager.setTransform(inst2->getRoot(), mat4f::translation({5,0,0}));
inst2->getAnimator()->applyAnimation(1, time);  // 跑步

// 实例 3: 位置 (-5,0,0), 播放攻击动画
FilamentInstance* inst3 = loader->createInstance(asset);
transformManager.setTransform(inst3->getRoot(), mat4f::translation({-5,0,0}));
inst3->getAnimator()->applyAnimation(2, time);  // 攻击

// 结果:
// - 共享同一套网格和纹理 (125 MB)
// - 独立的位置和动画状态
```

---

## 缓存机制

### 1. MeshCache (网格缓存)

**用途**: 同一网格被多个节点引用时共享

**实现**:
```cpp
// MeshCache 结构
using MeshCache = FixedCapacityVector<FixedCapacityVector<Primitive>>;
//                 ^^^^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^
//                 每个 mesh           每个 primitive

struct Primitive {
    VertexBuffer* vertices;
    IndexBuffer* indices;
    Aabb aabb;
    UvMap uvmap;
    MorphTargetBuffer* morphTargetBuffer;
};

// 使用
Primitive& prim = asset->mMeshCache[meshIndex][primitiveIndex];
builder.geometry(i, TRIANGLES, prim.vertices, prim.indices);
```

**场景**:
```
glTF 场景:
Node A → Mesh 0
Node B → Mesh 0  // 同一网格
Node C → Mesh 1

MeshCache:
[0] → [Primitive 0, Primitive 1]  // Mesh 0 的数据
[1] → [Primitive 0]               // Mesh 1 的数据

Node A 和 Node B 共享 MeshCache[0] 的数据
```

### 2. MaterialInstanceCache (材质实例缓存)

**用途**: 相同配置的材质实例复用

**实现**:
```cpp
class MaterialInstanceCache {
    struct Key {
        cgltf_material* material;
        UvMap uvmap;
        bool operator==(const Key& other) const;
    };

    std::unordered_map<Key, MaterialInstance*> mCache;

    MaterialInstance* getOrCreateInstance(cgltf_material* mat, UvMap uvmap) {
        Key key = {mat, uvmap};

        if (mCache.contains(key)) {
            return mCache[key];  // 复用
        }

        MaterialInstance* mi = createMaterialInstance(mat, uvmap);
        mCache[key] = mi;
        return mi;
    }
};
```

**缓存条件**:
- 相同的 glTF material
- 相同的 UV 映射

**场景**:
```
多个 primitive 使用相同材质:
Primitive A → Material X, UV映射 {0→0, 1→1}
Primitive B → Material X, UV映射 {0→0, 1→1}  // 复用 A 的 MaterialInstance
Primitive C → Material X, UV映射 {0→1, 1→0}  // 不同 UV,创建新实例
```

### 3. TextureCache (纹理缓存)

**用途**: 相同来源的纹理复用

**实现**:
```cpp
// 两种缓存
std::unordered_map<BufferViewKey, Texture*> mBufferTextureCache;
std::unordered_map<std::string, Texture*> mFilepathTextureCache;

Texture* getOrCreateTexture(cgltf_texture* srcTexture) {
    if (srcTexture->image->buffer_view) {
        // 从 buffer_view 缓存查找
        BufferViewKey key = makeKey(srcTexture->image->buffer_view);
        if (mBufferTextureCache.contains(key)) {
            return mBufferTextureCache[key];
        }
    } else if (srcTexture->image->uri) {
        // 从文件路径缓存查找
        if (mFilepathTextureCache.contains(uri)) {
            return mFilepathTextureCache[uri];
        }
    }

    // 创建新纹理
    Texture* texture = createTexture(srcTexture);
    // 添加到缓存
    return texture;
}
```

**场景**:
```
glTF 纹理引用:
Texture 0 → image 0 → URI "albedo.png"
Texture 1 → image 0 → URI "albedo.png"  // 同一文件,复用
Texture 2 → image 1 → buffer_view 0     // 嵌入数据
Texture 3 → image 2 → buffer_view 0     // 同一 buffer_view,复用
```

---

## 依赖关系图

### Phase 1: 创建依赖

```
AssetLoader
    ↓ creates
FFilamentAsset
    ↓ first pass
VertexBuffer, IndexBuffer (空)
    ↓ second pass
FFilamentInstance
    ↓ creates
Entity + Components
    ↓ uses
MeshCache (获取 VB/IB)
    ↓ creates
MaterialInstance
    ↓ uses
MaterialInstanceCache (复用)
```

### Phase 2: 资源依赖

```
ResourceLoader
    ↓ loads
Buffer 数据 (cgltf_data)
    ↓ uploads
VertexBuffer, IndexBuffer
    ↓ creates
BufferObject
    ↓ computes
Tangents (并行)
    ↓ loads
Texture (异步)
    ↓ uses
TextureCache (复用)
    ↓ binds
MaterialInstance.setParameter()
    ↓ creates
Animator
```

### Phase 3: 渲染依赖

```
Animator
    ↓ updates
TransformManager (T/R/S)
    ↓ computes
Bone Matrices
    ↓ sets
RenderableManager.setBones()
    ↓ uses
Renderer
    ↓ renders
GPU (顶点蒙皮)
```

---

## 数据流图

### 加载流程数据流

```
磁盘文件 (glb/gltf)
    ↓
cgltf_parse()
    ↓
cgltf_data (树状结构)
    ↓
┌──────────────────────┐
│   FAssetLoader       │
│  第一次遍历:          │
│  创建 VB/IB (空)     │
│  第二次遍历:          │
│  创建 Entity         │
└──────────────────────┘
    ↓
FFilamentAsset + FFilamentInstance
    ↓
ResourceLoader
    ↓
┌──────────────────────────────────────┐
│ loadCgltfBuffers()                  │
│   ↓                                 │
│ 磁盘/URI → cgltf_buffer->data       │
│   ↓                                 │
│ uploadBuffers()                     │
│   ↓                                 │
│ CPU → BufferObject → GPU            │
│   ↓                                 │
│ createTextures()                    │
│   ↓                                 │
│ 图像数据 → TextureProvider → Texture │
│   ↓                                 │
│ applyTextureBinding()               │
│   ↓                                 │
│ MaterialInstance.setParameter()     │
└──────────────────────────────────────┘
    ↓
资源就绪,可渲染
```

### 渲染流程数据流

```
用户代码
    ↓
animator->applyAnimation(index, time)
    ↓
┌─────────────────────────────────┐
│ 查找关键帧                        │
│   ↓                             │
│ 插值计算 (T/R/S/W)               │
│   ↓                             │
│ TrsTransformManager.set()       │
│   ↓                             │
│ TransformManager (更新矩阵)      │
└─────────────────────────────────┘
    ↓
animator->updateBoneMatrices()
    ↓
┌─────────────────────────────────┐
│ 获取关节世界矩阵                  │
│   ↓                             │
│ 获取逆绑定矩阵                    │
│   ↓                             │
│ 计算骨骼矩阵                      │
│   ↓                             │
│ RenderableManager.setBones()    │
│   ↓                             │
│ Driver.updateUniformBuffer()    │
└─────────────────────────────────┘
    ↓
renderer->render(view)
    ↓
GPU (顶点蒙皮 + 渲染)
```

---

## 线程模型

### 主线程

**职责**:
- glTF 解析 (cgltf_parse)
- Entity 创建
- 组件创建
- 资源管理
- 渲染循环

### JobSystem 线程池

**异步任务**:
1. **切线计算** (TangentsJob)
   ```cpp
   Job* job = jobs::create(js, nullptr, [vb, prim]() {
       quatf* tangents = computeTangentQuaternions(...);
       vb->setBufferObjectAt(...);
   });
   js->run(job);
   ```

2. **纹理解码** (TextureProvider)
   ```cpp
   JobSystem::run([imageData, callback]() {
       uint8_t* pixels = stbi_load_from_memory(...);
       Texture* texture = createTexture(...);
       callback(texture);
   });
   ```

3. **Draco 解压** (DracoCache)
   ```cpp
   JobSystem::run([compressedData]() {
       draco::Decoder::Decode(...);
   });
   ```

### 后端线程

**GPU 命令**:
- VertexBuffer 上传
- IndexBuffer 上传
- Texture 上传
- Uniform 更新

---

## 内存管理策略

### 1. CPU 内存释放

**cgltf_data**:
```cpp
// 加载后可释放
asset->releaseSourceData();
// cgltf_free(mSourceAsset->hierarchy);
```

**Buffer 数据**:
```cpp
// 上传后异步释放
bo->setBuffer(*engine, BufferDescriptor(
    data, size,
    [](void* buffer, size_t size, void* user) {
        free(buffer);  // GPU 上传完成后回调
    }
));
```

**纹理数据**:
```cpp
// 解码后立即上传,回调释放
texture->setImage(*engine, 0, PixelBufferDescriptor(
    pixels, size, format, type,
    [](void* buffer, ...) {
        stbi_image_free(buffer);
    }
));
```

### 2. GPU 内存管理

**资源生命周期**:
- BufferObject: 由 VertexBuffer/IndexBuffer 持有
- Texture: 由 Asset 持有
- Uniform: 由 RenderableManager 管理

**释放时机**:
```cpp
AssetLoader::destroyAsset(asset)
    ↓
FFilamentAsset::~FFilamentAsset()
    ↓
for (VertexBuffer* vb : mVertexBuffers)
    engine->destroy(vb);  // GPU 内存释放
for (IndexBuffer* ib : mIndexBuffers)
    engine->destroy(ib);
for (Texture* tex : mTextures)
    engine->destroy(tex);
```

---

## 性能优化要点

### 1. 减少重复创建

- ✅ MeshCache: 网格复用
- ✅ MaterialInstanceCache: 材质实例复用
- ✅ TextureCache: 纹理复用

### 2. 批量操作

- ✅ TransformManager Transaction: 批量更新变换
- ✅ JobSystem 并行: 切线计算并行

### 3. 异步加载

- ✅ Texture 异步解码
- ✅ Draco 异步解压
- ✅ 回调驱动,非阻塞

### 4. 内存优化

- ✅ 资源共享 (节省 60-80%)
- ✅ CPU 数据及时释放
- ✅ GPU 数据生命周期管理

### 5. 渐进式加载

- ✅ DependencyGraph 追踪
- ✅ popRenderables() 逐步显示
- ✅ 纹理异步加载不阻塞
