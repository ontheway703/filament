# 性能优化要点

本文档详细说明 gltfio 的性能优化策略和最佳实践。

---

## 1. 资源共享优化

### 1.1 网格共享 (MeshCache)

**原理**: 同一网格被多个节点引用时,共享 VertexBuffer 和 IndexBuffer

**内存节省**:
```
示例场景: 100 个树使用同一网格
- 不共享: 100 × 5MB = 500MB
- 共享: 5MB (节省 99%)
```

**实现**:
```cpp
// MeshCache 自动处理共享
Primitive& prim = asset->mMeshCache[meshIndex][primitiveIndex];

// 多个 Renderable 引用同一 VB/IB
builder.geometry(i, TRIANGLES, prim.vertices, prim.indices);
```

**最佳实践**:
- ✅ glTF 中复用网格 (引用同一 mesh)
- ✅ 不要重复定义相同的网格
- ✅ 使用实例化而非复制节点

### 1.2 纹理共享 (TextureCache)

**原理**: 相同来源的纹理只创建一次

**缓存策略**:
```cpp
// BufferTextureCache: 嵌入纹理
if (image->buffer_view) {
    BufferViewKey key = makeKey(buffer_view);
    if (cache.contains(key)) {
        return cache[key];  // 复用
    }
}

// FilepathTextureCache: 外部文件
if (image->uri) {
    if (cache.contains(uri)) {
        return cache[uri];  // 复用
    }
}
```

**内存节省**:
```
示例: 10 个材质使用同一 albedo 纹理
- 不共享: 10 × 50MB = 500MB
- 共享: 50MB (节省 90%)
```

**最佳实践**:
- ✅ 在 glTF 中复用纹理定义
- ✅ 使用相同的 URI 或 buffer_view
- ✅ 不要为相同图像创建多个 image 对象

### 1.3 材质实例缓存 (MaterialInstanceCache)

**原理**: 相同材质和 UV 映射的实例复用

**缓存键**:
```cpp
struct Key {
    cgltf_material* material;
    UvMap uvmap;  // UV 通道映射
};
```

**适用场景**:
```
多个 primitive 使用相同材质:
Primitive A → Material X, UV{0→0, 1→1}
Primitive B → Material X, UV{0→0, 1→1}  ✅ 复用
Primitive C → Material X, UV{0→1, 1→0}  ❌ 不同 UV,不复用
```

**最佳实践**:
- ✅ 统一 UV 通道映射
- ✅ 避免不必要的材质变体
- ✅ 复用材质定义

### 1.4 Asset vs Instance 共享

**共享资源 (Asset 级)**: 

| 资源 | 大小 | 共享收益 |
|-----|------|---------|
| VertexBuffer | 10-100MB | ⭐⭐⭐⭐⭐ |
| IndexBuffer | 1-10MB | ⭐⭐⭐⭐ |
| Texture | 50-500MB | ⭐⭐⭐⭐⭐ |

**独立状态 (Instance 级)**:

| 资源 | 大小 | 独立必要性 |
|-----|------|----------|
| Entity | 8B/个 | ⭐⭐⭐⭐⭐ |
| Transform | 64B/个 | ⭐⭐⭐⭐⭐ |
| Animator | 几 KB | ⭐⭐⭐⭐⭐ |

**内存对比**:
```
3 个角色实例:
传统方式: 3 × 125MB = 375MB
Filament:   125MB + 3 × 10KB = 125.03MB (节省 66.7%)
```

---

## 2. 加载优化

### 2.1 压缩格式

**Draco 网格压缩**:
```json
{
  "extensions": {
    "KHR_draco_mesh_compression": {
      "bufferView": 0,
      "attributes": {
        "POSITION": 0,
        "NORMAL": 1
      }
    }
  }
}
```

**优势**:
- 压缩率: 75-95%
- 传输时间: 减少 10x
- 解压时间: ~100ms (可接受)

**Meshopt 压缩**:
```json
{
  "extensions": {
    "EXT_meshopt_compression": {
      "bufferView": 1,
      "mode": "TRIANGLES",
      "count": 1000
    }
  }
}
```

**优势**:
- 压缩率: 50-70%
- 解压速度: 比 Draco 快 10x
- 适合实时场景

**KTX2 纹理压缩**:
```json
{
  "textures": [{
    "source": 0,
    "extensions": {
      "KHR_texture_basisu": {
        "source": 0
      }
    }
  }]
}
```

**优势**:
- GPU 直接使用 (无需解压到 RGBA)
- 压缩率: 80-90%
- 支持 mipmap

**最佳实践**:
```cpp
// Draco 解码
utility::decodeDracoMeshes(gltf, dracoCache);

// Meshopt 解码
utility::decodeMeshoptCompression(gltf);

// KTX2 解码
auto ktx2Provider = createKtx2Provider(engine);
resourceLoader.addTextureProvider("image/ktx2", ktx2Provider);
```

### 2.2 异步加载

**同步加载问题**:
```cpp
// ❌ 阻塞主线程
resourceLoader.loadResources(asset);  // 可能需要 1-5 秒
```

**异步加载方案**:
```cpp
// ✅ 非阻塞
resourceLoader.asyncBeginLoad(asset);

while (!quit) {
    resourceLoader.asyncUpdateLoad();  // 增量更新

    float progress = resourceLoader.asyncGetLoadProgress();
    updateProgressBar(progress);

    // 渐进式显示
    utils::Entity entity;
    while ((entity = asset->popRenderable())) {
        scene->addEntity(entity);
    }

    renderer->render(view);
}
```

**性能对比**:

| 方法 | 首帧时间 | 用户体验 |
|-----|---------|---------|
| 同步加载 | 3-5s | ❌ 黑屏等待 |
| 异步加载 | <100ms | ✅ 立即看到场景 |

### 2.3 渐进式加载

**DependencyGraph 机制**:
```cpp
// 纹理加载完成前,网格不显示
asset->mDependencyGraph.addEdge(entity, textureIndex);

// 纹理加载完成
asset->applyTextureBinding(textureIndex, slot);
asset->mDependencyGraph.commitEdges();

// 网格变为可见
Entity ready = asset->popRenderable();
```

**效果**:
- 避免白模闪烁
- 逐步显示高质量内容
- 保持 60fps

**最佳实践**:
```cpp
// 启用渐进式加载
resourceLoader.asyncBeginLoad(asset);

// 禁用渐进式 (全部加载完再显示)
asset->mDependencyGraph.disableProgressiveReveal();
resourceLoader.loadResources(asset);
scene->addEntities(asset->getEntities(), asset->getEntityCount());
```

### 2.4 并行计算

**切线计算并行化**:
```cpp
JobSystem* js = engine->getJobSystem();

// 为每个 primitive 创建任务
for (auto& [vb, prim] : primitives) {
    Job* job = jobs::create(js, nullptr, [vb, prim]() {
        // 计算切线四元数
        quatf* tangents = geometry::SurfaceOrientation::Builder()
            .normals(normals)
            .uvs(uvs)
            .positions(positions)
            .triangles(triangles, triangleCount)
            .build()
            ->getQuaternions();

        // 上传到 GPU
        vb->setBufferObjectAt(...);
    });
    js->run(job);
}

// 等待所有任务完成
js->waitAndRelease(parentJob);
```

**性能提升**:
- 单线程: 500ms
- 8 线程: 70ms (7x 提升)

---

## 3. 内存优化

### 3.1 及时释放 CPU 内存

**glTF 源数据**:
```cpp
// 资源加载完成后释放
resourceLoader.loadResources(asset);
asset->releaseSourceData();  // 释放 cgltf_data
```

**Buffer 数据**:
```cpp
// 上传后自动释放
bo->setBuffer(*engine, BufferDescriptor(
    data, size,
    [](void* buffer, size_t size, void* user) {
        free(buffer);  // GPU 上传完成后回调
    }
));
```

**纹理数据**:
```cpp
// 解码后立即上传并释放
texture->setImage(*engine, 0, PixelBufferDescriptor(
    pixels, size, format, type,
    [](void* buffer, ...) {
        stbi_image_free(buffer);  // 回调释放
    }
));
```

**内存时间线**:
```
加载 glTF → cgltf_data 占用 10MB
              ↓
上传 VB/IB  → 临时 buffer 100MB
              ↓ (回调释放)
              → 0MB (GPU 已有数据)
              ↓
解码纹理    → 临时 pixels 200MB
              ↓ (回调释放)
              → 0MB (GPU 已有数据)
              ↓
释放源数据  → cgltf_data 0MB
              ↓
最终 CPU 内存: ~0MB (仅保留控制结构)
```

### 3.2 GPU 内存管理

**资源生命周期**:
```cpp
Asset 创建 → VB/IB/Texture 创建 (GPU 分配)
              ↓
Asset 销毁 → engine->destroy(vb)
            engine->destroy(ib)
            engine->destroy(texture)
              ↓
            GPU 内存释放
```

**销毁顺序**:
```cpp
AssetLoader::destroyAsset(asset)
    ↓
FFilamentAsset::~FFilamentAsset()
    ↓
for (vb : mVertexBuffers) engine->destroy(vb);
for (ib : mIndexBuffers) engine->destroy(ib);
for (tex : mTextures) engine->destroy(tex);
    ↓
GPU 内存释放 (异步)
```

**最佳实践**:
- ✅ 不用的 Asset 及时销毁
- ✅ 避免内存泄漏 (检查析构)
- ✅ 监控 GPU 内存使用

### 3.3 URI 缓存

**UriDataCache 机制**:
```cpp
UriDataCacheHandle cache = std::make_shared<UriDataCache>();

// 第一次加载
const uint8_t* data = cache->get("albedo.png");  // 从磁盘读取

// 第二次加载 (复用)
const uint8_t* data = cache->get("albedo.png");  // 从缓存

// 用完释放
cache->evictResourceData();
```

**适用场景**:
- 多个 glTF 文件引用相同 URI
- 异步加载多个资产

**内存权衡**:
- 优势: 避免重复读取磁盘
- 劣势: 占用 CPU 内存
- 策略: 加载完成后 evict

---

## 4. 渲染优化

### 4.1 批量变换更新

**问题**: 逐个更新 Transform 慢
```cpp
// ❌ 慢: 每次更新触发 ECS 同步
for (entity : entities) {
    trsTransformManager->setTranslation(entity, pos);
    trsTransformManager->setRotation(entity, rot);
}
```

**优化**: 使用事务批量更新
```cpp
// ✅ 快: 批量更新,一次同步
trsTransformManager->openLocalTransformTransaction();

for (entity : entities) {
    trsTransformManager->setTranslation(entity, pos);
    trsTransformManager->setRotation(entity, rot);
}

trsTransformManager->commitLocalTransformTransaction();
trsTransformManager->updateTransformMatrices(transformManager);
```

**性能提升**:
- 100 个节点: 10ms → 1ms (10x)

### 4.2 骨骼矩阵优化

**问题**: 每帧重复计算骨骼矩阵
```cpp
// ❌ 每帧都计算
animator->applyAnimation(0, time);
animator->updateBoneMatrices();  // 200 个骨骼 × 16 个矩阵元素
```

**优化 1**: 只在需要时更新
```cpp
// ✅ 动画暂停时不更新
if (isPlaying) {
    animator->applyAnimation(0, time);
    animator->updateBoneMatrices();
}
```

**优化 2**: 降低更新频率
```cpp
// ✅ 30fps 动画更新,60fps 渲染
static float accumTime = 0.0f;
accumTime += deltaTime;

if (accumTime >= 1.0f / 30.0f) {
    animator->applyAnimation(0, time);
    animator->updateBoneMatrices();
    accumTime = 0.0f;
}

renderer->render(view);  // 60fps
```

**性能提升**:
- 复杂骨骼: 5ms → 2.5ms (2x)

### 4.3 LOD (Level of Detail)

**手动 LOD**:
```cpp
// 根据距离切换实例
float distance = length(cameraPos - modelPos);

if (distance < 10.0f) {
    scene->addEntity(highLodInstance->getRoot());
    scene->removeEntity(lowLodInstance->getRoot());
} else {
    scene->addEntity(lowLodInstance->getRoot());
    scene->removeEntity(highLodInstance->getRoot());
}
```

**glTF LOD 扩展**:
```json
{
  "extensions": {
    "MSFT_lod": {
      "ids": [0, 1, 2]  // 高 → 中 → 低
    }
  }
}
```

**性能提升**:
- 远距离: 10K 三角形 → 1K 三角形 (10x)

---

## 5. 高级优化

### 5.1 Instancing (GPU 实例化)

**适用场景**: 大量相同模型 (草地、树木、石头)

**传统方式** (CPU 实例化):
```cpp
// 1000 个树,1000 个 draw call
for (int i = 0; i < 1000; i++) {
    FilamentInstance* tree = loader->createInstance(treeAsset);
    // ...
}
```

**优化**: GPU 实例化
```cpp
// 1000 个树,1 个 draw call
InstanceBuffer* instances = InstanceBuffer::Builder()
    .instanceCount(1000)
    .build(*engine);

// 设置实例变换
mat4f* transforms = new mat4f[1000];
for (int i = 0; i < 1000; i++) {
    transforms[i] = mat4f::translation({x[i], y[i], z[i]});
}
instances->setBuffer(*engine, BufferDescriptor(transforms, ...));

// 单次绘制
renderableManager->setInstanceBuffer(entity, instances);
```

**性能提升**:
- 1000 个实例: 50ms → 2ms (25x)

### 5.2 Frustum Culling

**原理**: 只渲染可见的实体

**实现**:
```cpp
// Filament 自动剔除
view->setFrustumCullingEnabled(true);

// 确保包围盒正确
RenderableManager::Builder()
    .boundingBox(aabb)  // 重要!
    .build(*engine, entity);
```

**性能提升**:
- 大场景: 100ms → 20ms (5x)

### 5.3 材质优化

**Uber Shader 问题**:
```cpp
// ❌ Uber shader 包含所有功能,慢
MaterialProvider* materials = createUbershaderProvider(engine, ...);
```

**JIT Shader 优化**:
```cpp
// ✅ JIT shader 按需生成,快
MaterialProvider* materials = createJitShaderProvider(engine);
```

**性能对比**:
| Shader 类型 | 编译时间 | 运行性能 |
|-----------|---------|---------|
| Uber Shader | 快 (预编译) | 慢 (分支多) |
| JIT Shader | 慢 (运行时) | 快 (专用) |

**最佳实践**:
- 开发: Uber Shader (快速迭代)
- 发布: JIT Shader (最佳性能)

---

## 6. 性能测量

### 6.1 加载性能

**关键指标**:
```cpp
auto start = std::chrono::high_resolution_clock::now();

// 解析
asset = loader->createAsset(data, size);
auto parseTime = getDuration(start);

// 资源加载
resourceLoader.loadResources(asset);
auto loadTime = getDuration(start);

// 总时间
std::cout << "Parse: " << parseTime << "ms" << std::endl;
std::cout << "Load: " << loadTime << "ms" << std::endl;
```

**优化目标**:
- Parse: < 100ms
- Load: < 1000ms
- 异步: < 50ms (首帧)

### 6.2 渲染性能

**帧时间分析**:
```cpp
auto frameStart = std::chrono::high_resolution_clock::now();

// 动画
auto animStart = now();
animator->applyAnimation(0, time);
animator->updateBoneMatrices();
auto animTime = getDuration(animStart);

// 渲染
auto renderStart = now();
renderer->render(view);
auto renderTime = getDuration(renderStart);

auto frameTime = getDuration(frameStart);

std::cout << "Anim: " << animTime << "ms" << std::endl;
std::cout << "Render: " << renderTime << "ms" << std::endl;
std::cout << "Frame: " << frameTime << "ms (";
std::cout << (1000.0f / frameTime) << " fps)" << std::endl;
```

**性能预算** (60fps):
| 阶段 | 预算 | 说明 |
|-----|------|-----|
| 动画更新 | < 2ms | applyAnimation + updateBoneMatrices |
| 渲染 | < 14ms | GPU 渲染 |
| 总帧时间 | < 16.67ms | 60 fps |

### 6.3 内存监控

**CPU 内存**:
```cpp
// 加载前
size_t memBefore = getCurrentRSS();

// 加载资产
asset = loader->createAsset(data, size);
resourceLoader.loadResources(asset);

// 释放源数据
asset->releaseSourceData();

// 加载后
size_t memAfter = getCurrentRSS();

std::cout << "Memory usage: " << (memAfter - memBefore) / 1024 / 1024 << " MB" << std::endl;
```

**GPU 内存** (估算):
```cpp
size_t gpuMem = 0;

// VertexBuffer
for (vb : asset->mVertexBuffers) {
    gpuMem += vb->getVertexCount() * vb->getStride();
}

// IndexBuffer
for (ib : asset->mIndexBuffers) {
    gpuMem += ib->getIndexCount() * sizeof(index);
}

// Texture
for (tex : asset->mTextures) {
    gpuMem += tex->getWidth() * tex->getHeight() * bytesPerPixel;
}

std::cout << "GPU memory: " << gpuMem / 1024 / 1024 << " MB" << std::endl;
```

---

## 性能优化清单

### 加载优化
- [ ] 使用 Draco/Meshopt 压缩网格
- [ ] 使用 KTX2 压缩纹理
- [ ] 启用异步加载
- [ ] 启用渐进式加载
- [ ] 启用 URI 缓存
- [ ] 加载完成后释放源数据

### 内存优化
- [ ] 复用网格 (MeshCache)
- [ ] 复用纹理 (TextureCache)
- [ ] 复用材质实例 (MaterialInstanceCache)
- [ ] 使用多实例 (Asset/Instance 分离)
- [ ] 及时释放 CPU 内存
- [ ] 监控 GPU 内存使用

### 渲染优化
- [ ] 使用批量变换更新
- [ ] 降低动画更新频率 (可选)
- [ ] 启用视锥剔除
- [ ] 使用 LOD
- [ ] 使用 GPU 实例化 (大量相同模型)
- [ ] 使用 JIT Shader (发布版)

### 性能监控
- [ ] 测量加载时间
- [ ] 测量帧时间
- [ ] 监控内存使用
- [ ] 设置性能预算
