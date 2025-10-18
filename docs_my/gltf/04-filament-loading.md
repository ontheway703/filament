# Filament glTF 加载机制

本文档详细说明 Filament 如何加载和处理 glTF 资产，包括 AssetLoader、ResourceLoader 和渐进式显示机制。

---

## 目录

1. [加载流程概述](#加载流程概述)
2. [AssetLoader 详解](#assetloader-详解)
3. [ResourceLoader 系统](#resourceloader-系统)
4. [MaterialProvider 集成](#materialprovider-集成)
5. [渐进式显示](#渐进式显示)
6. [完整示例](#完整示例)

---

## 加载流程概述

### 完整加载流程

```
1. 读取文件
   ↓
2. AssetLoader::createAsset()
   - cgltf 解析 JSON/GLB
   - 创建 Filament 实体和组件
   - 创建材质实例（通过 MaterialProvider）
   ↓
3. ResourceLoader::asyncBeginLoad()
   - 加载纹理
   - 上传几何数据到 GPU
   - 解码 Draco 压缩（如果有）
   ↓
4. 渐进式显示（可选）
   - popRenderable() 获取就绪的渲染对象
   - 逐步添加到场景
   ↓
5. 释放源数据
   - asset->releaseSourceData()
   ↓
6. 渲染
```

### 核心组件

| 组件 | 职责 | 时机 |
|------|------|------|
| **AssetLoader** | 解析 glTF, 创建 Filament 对象 | 同步（快） |
| **ResourceLoader** | 加载资源（纹理、缓冲） | 异步（慢） |
| **MaterialProvider** | 创建材质 | AssetLoader 期间 |
| **TextureProvider** | 解码纹理 | ResourceLoader 期间 |

---

## AssetLoader 详解

### 创建 AssetLoader

```cpp
#include <gltfio/AssetLoader.h>
#include <gltfio/MaterialProvider.h>

using namespace filament;
using namespace filament::gltfio;

// 1. 创建引擎
Engine* engine = Engine::create();

// 2. 创建 MaterialProvider（选择一种）
MaterialProvider* materials = createJitShaderProvider(engine);
// 或
// MaterialProvider* materials = createUbershaderProvider(
//     engine, UBERSHADER_DATA, UBERSHADER_SIZE);

// 3. 配置 AssetLoader
AssetConfiguration config {
    .engine = engine,
    .materials = materials,
    .names = nullptr,      // 可选：NameComponentManager
    .entities = nullptr    // 可选：EntityManager（默认使用全局）
};

// 4. 创建 AssetLoader
AssetLoader* loader = AssetLoader::create(config);
```

### 加载资产

#### 方式 1: 从内存加载

```cpp
// 读取文件到内存
std::vector<uint8_t> data = readFile("model.glb");

// 创建资产
FilamentAsset* asset = loader->createAsset(data.data(), data.size());

if (!asset) {
    slog.e << "Failed to load glTF asset" << io::endl;
    return;
}

// data 可以立即释放（cgltf 已复制数据）
data.clear();
```

#### 方式 2: 创建多实例

```cpp
const size_t instanceCount = 10;
FilamentInstance* instances[instanceCount];

FilamentAsset* asset = loader->createInstancedAsset(
    data.data(), data.size(),
    instances, instanceCount
);

// asset 是主资产，拥有所有实例
// instances[i] 是独立的实例，共享材质和几何
```

### AssetLoader 做了什么？

#### 1. 解析 glTF

```cpp
// 内部使用 cgltf
cgltf_data* gltfData;
cgltf_result result = cgltf_parse(&options, data, size, &gltfData);

if (result != cgltf_result_success) {
    return nullptr;
}
```

#### 2. 创建节点层级

```cpp
// 为每个 glTF node 创建 Filament entity
for (size_t i = 0; i < gltfData->nodes_count; ++i) {
    Entity entity = entityManager->create();
    
    // 设置变换
    TransformManager::Instance tmi = tm->getInstance(entity);
    tm->setTransform(tmi, localMatrix);
    
    // 设置父子关系
    if (node->parent) {
        tm->setParent(tmi, parentInstance);
    }
}
```

#### 3. 创建网格和材质

```cpp
// 为每个 primitive 创建 VertexBuffer 和 IndexBuffer
for (auto& primitive : mesh->primitives) {
    // 创建 VertexBuffer（延迟上传数据）
    VertexBuffer* vb = VertexBuffer::Builder()
        .vertexCount(accessor->count)
        .bufferCount(attributeCount)
        .attribute(VertexAttribute::POSITION, ...)
        .attribute(VertexAttribute::NORMAL, ...)
        .build(*engine);
    
    // 创建 IndexBuffer（延迟上传数据）
    IndexBuffer* ib = IndexBuffer::Builder()
        .indexCount(indicesAccessor->count)
        .bufferType(IndexBuffer::IndexType::USHORT)
        .build(*engine);
    
    // 创建 MaterialInstance
    MaterialInstance* mi = materials->createMaterialInstance(
        &materialKey, &uvmap, material->name, extras);
    
    // 创建 Renderable
    RenderableManager::Builder(1)
        .geometry(0, primitiveType, vb, ib)
        .material(0, mi)
        .culling(true)
        .receiveShadows(true)
        .castShadows(true)
        .build(*engine, entity);
}
```

#### 4. 创建动画和骨骼

```cpp
// 创建 Animator
if (gltfData->animations_count > 0) {
    Animator* animator = new Animator(asset, gltfData);
    asset->mAnimator = animator;
}

// 创建 Skin（蒙皮）
for (auto& skin : gltfData->skins) {
    // 设置骨骼层级
    // 上传逆绑定矩阵
}
```

### FilamentAsset API

```cpp
// 获取实体列表
const Entity* entities = asset->getEntities();
size_t entityCount = asset->getEntityCount();

// 获取可渲染实体
const Entity* renderables = asset->getRenderableEntities();
size_t renderableCount = asset->getRenderableEntityCount();

// 获取光源实体
const Entity* lights = asset->getLightEntities();
size_t lightCount = asset->getLightEntityCount();

// 获取相机实体
const Entity* cameras = asset->getCameraEntities();
size_t cameraCount = asset->getCameraEntityCount();

// 获取根实体（用于整体变换）
Entity root = asset->getRoot();

// 获取边界框
Aabb boundingBox = asset->getBoundingBox();

// 获取动画器
Animator* animator = asset->getInstance()->getAnimator();

// 释放 glTF 源数据（节省内存）
asset->releaseSourceData();
```

---

## ResourceLoader 系统

### 创建 ResourceLoader

```cpp
#include <gltfio/ResourceLoader.h>

// 配置
ResourceConfiguration resourceConfig {
    .engine = engine,
    .gltfPath = "/path/to/model.gltf",  // 用于解析相对路径
    .normalizeSkinningWeights = true    // 归一化骨骼权重
};

ResourceLoader* resourceLoader = new ResourceLoader(resourceConfig);
```

### 添加 TextureProvider

```cpp
#include <gltfio/TextureProvider.h>

// STB Provider（PNG, JPEG）
TextureProvider* stbProvider = createStbProvider(engine);
resourceLoader->addTextureProvider("image/png", stbProvider);
resourceLoader->addTextureProvider("image/jpeg", stbProvider);

// KTX2 Provider（Basis Universal）
TextureProvider* ktx2Provider = createKtx2Provider(engine);
resourceLoader->addTextureProvider("image/ktx2", ktx2Provider);
```

### 异步加载

```cpp
// 开始异步加载
bool success = resourceLoader->asyncBeginLoad(asset);

if (!success) {
    slog.e << "Failed to start resource loading" << io::endl;
    return;
}

// 在渲染循环中更新
while (rendering) {
    // 更新资源加载（上传 GPU 数据）
    resourceLoader->asyncUpdateLoad();
    
    // 检查进度
    float progress = resourceLoader->asyncGetLoadProgress();
    
    // 渲染
    renderer->render(view);
    
    // 加载完成
    if (progress >= 1.0f) {
        break;
    }
}
```

### ResourceLoader 做了什么？

#### 1. 加载纹理

```cpp
// 读取图像文件
for (auto& image : gltfData->images) {
    if (image->uri) {
        // 从文件或 HTTP 加载
        std::string path = resolvePath(gltfPath, image->uri);
        std::vector<uint8_t> imageData = readFile(path);
        
        // 使用 TextureProvider 解码
        TextureProvider::FulfillParams params {
            .data = imageData.data(),
            .size = imageData.size(),
            .mimeType = "image/png"
        };
        
        textureProvider->fulfill(texture, params);
    } else {
        // 从 BufferView 加载（GLB 内嵌）
        const uint8_t* data = bufferView->buffer->data + bufferView->offset;
        // ...
    }
}
```

#### 2. 上传几何数据

```cpp
// 上传顶点数据
for (auto& bufferSlot : assetInfo->bufferSlots) {
    VertexBuffer* vb = bufferSlot.vertexBuffer;
    const cgltf_accessor* accessor = bufferSlot.accessor;
    
    // 获取数据指针
    const uint8_t* data = getAccessorData(accessor);
    
    // 上传到 GPU
    vb->setBufferAt(*engine, bufferSlot.bufferIndex,
        VertexBuffer::BufferDescriptor(data, accessor->count * stride));
}

// 上传索引数据
for (auto& bufferSlot : assetInfo->indexBuffers) {
    IndexBuffer* ib = bufferSlot.indexBuffer;
    const cgltf_accessor* accessor = bufferSlot.accessor;
    
    const uint8_t* data = getAccessorData(accessor);
    
    ib->setBuffer(*engine,
        IndexBuffer::BufferDescriptor(data, accessor->count * componentSize));
}
```

#### 3. 解码 Draco 压缩

```cpp
// 如果使用 KHR_draco_mesh_compression
if (primitive->extensions && primitive->extensions->KHR_draco_mesh_compression) {
    auto& draco = primitive->extensions->KHR_draco_mesh_compression;
    
    // 解码 Draco 数据
    draco::Decoder decoder;
    std::unique_ptr<draco::Mesh> mesh = 
        decoder.DecodeMeshFromBuffer(dracoData, dracoSize);
    
    // 提取属性数据
    for (auto& attr : draco->attributes) {
        // 获取解码后的数据
        // 上传到 VertexBuffer
    }
}
```

#### 4. 计算切线（可选）

```cpp
// 如果需要法线贴图但没有 TANGENT 属性
if (material->hasNormalTexture && !primitive->hasTangents) {
    // 使用 MikkTSpace 算法计算切线
    computeTangents(positions, normals, uvs, indices, tangents);
    
    // 添加到 VertexBuffer
    vb->setBufferAt(*engine, tangentSlot,
        VertexBuffer::BufferDescriptor(tangents, vertexCount * 16));
}
```

### 同步加载（不推荐）

```cpp
// 阻塞直到全部加载完成
resourceLoader->loadResources(asset);

// 等价于
resourceLoader->asyncBeginLoad(asset);
while (resourceLoader->asyncGetLoadProgress() < 1.0f) {
    resourceLoader->asyncUpdateLoad();
}
```

---

## MaterialProvider 集成

详见 [材质加载文档](../material/09-gltfio-material-loading.md)，这里简要说明：

### JitShaderProvider

```cpp
// 创建
MaterialProvider* provider = createJitShaderProvider(engine);

// 内部流程
MaterialInstance* JitShaderProvider::createMaterialInstance(
        MaterialKey* config, UvMap* uvmap,
        const char* label, const char* extras) {
    
    // 1. 应用 UV 约束（Filament 最多 2 组 UV）
    constrainMaterial(config, uvmap);
    
    // 2. 计算哈希
    uint32_t hash = hash_value(*config);
    
    // 3. 查找缓存
    auto iter = mMaterials.find(hash);
    if (iter != mMaterials.end()) {
        return iter.value()->createInstance(label);
    }
    
    // 4. 生成 shader 代码
    std::string shaderCode = generateShader(*config, uvmap);
    
    // 5. 使用 MaterialBuilder 编译
    MaterialBuilder builder;
    builder.material(shaderCode.c_str());
    // ... 配置参数
    Package pkg = builder.build(*engine);
    
    // 6. 创建 Material
    Material* material = Material::Builder()
        .package(pkg.getData(), pkg.getSize())
        .build(*engine);
    
    // 7. 缓存并创建实例
    mMaterials[hash] = material;
    return material->createInstance(label);
}
```

### UbershaderProvider

```cpp
// 创建（使用预编译的材质包）
#include "gltf_ubershaders.h"
MaterialProvider* provider = createUbershaderProvider(
    engine, GLTF_UBERSHADERS_DATA, GLTF_UBERSHADERS_SIZE);

// 内部流程
MaterialInstance* UbershaderProvider::createMaterialInstance(...) {
    // 1. 应用 UV 约束
    constrainMaterial(config, uvmap);
    
    // 2. 查找匹配的 ubershader
    Material* material = findClosestUbershader(*config);
    
    // 3. 创建实例并设置特性开关
    MaterialInstance* mi = material->createInstance(label);
    mi->setParameter("hasBaseColorTexture", config->hasBaseColorTexture);
    mi->setParameter("hasNormalTexture", config->hasNormalTexture);
    // ...
    
    return mi;
}
```

---

## 渐进式显示

Filament 支持渐进式显示：纹理加载完成后逐步显示渲染对象。

### DependencyGraph

AssetLoader 内部维护依赖图：

```
Renderable → Material → Textures
```

只有当 Renderable 的所有纹理都加载完成时，才标记为"就绪"。

### 使用 popRenderable

```cpp
// 创建资产
FilamentAsset* asset = loader->createAsset(data.data(), data.size());

// 开始加载资源
resourceLoader->asyncBeginLoad(asset);

// 渲染循环
while (rendering) {
    // 更新资源加载
    resourceLoader->asyncUpdateLoad();
    
    // 弹出就绪的渲染对象
    Entity entity;
    while ((entity = asset->popRenderable()).getId() != 0) {
        scene->addEntity(entity);
        slog.i << "Added renderable: " << entity.getId() << io::endl;
    }
    
    // 渲染
    renderer->render(view);
}
```

### popRenderables（批量）

```cpp
const size_t MAX_POP = 10;
Entity entities[MAX_POP];

while (true) {
    size_t count = asset->popRenderables(entities, MAX_POP);
    if (count == 0) break;
    
    scene->addEntities(entities, count);
    slog.i << "Added " << count << " renderables" << io::endl;
}
```

### 进度跟踪

```cpp
while (rendering) {
    resourceLoader->asyncUpdateLoad();
    
    // 获取进度 [0.0, 1.0]
    float progress = resourceLoader->asyncGetLoadProgress();
    
    // 显示进度条
    updateProgressBar(progress);
    
    // 弹出就绪对象
    while (Entity e = asset->popRenderable()) {
        scene->addEntity(e);
    }
    
    renderer->render(view);
    
    if (progress >= 1.0f) {
        slog.i << "Loading complete!" << io::endl;
        break;
    }
}
```

---

## 完整示例

### 基础加载（同步）

```cpp
#include <filament/Engine.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/MaterialProvider.h>
#include <gltfio/TextureProvider.h>

void loadModel(const char* path) {
    // 1. 创建引擎和基础对象
    Engine* engine = Engine::create();
    Scene* scene = engine->createScene();
    
    // 2. 创建 MaterialProvider
    MaterialProvider* materials = createJitShaderProvider(engine);
    
    // 3. 创建 AssetLoader
    AssetLoader* loader = AssetLoader::create({engine, materials});
    
    // 4. 读取 glTF 文件
    std::vector<uint8_t> data = readFile(path);
    
    // 5. 创建资产
    FilamentAsset* asset = loader->createAsset(data.data(), data.size());
    if (!asset) {
        slog.e << "Failed to create asset" << io::endl;
        return;
    }
    data.clear();
    
    // 6. 创建 ResourceLoader 和 TextureProvider
    TextureProvider* stbProvider = createStbProvider(engine);
    ResourceLoader* resourceLoader = new ResourceLoader({engine, path});
    resourceLoader->addTextureProvider("image/png", stbProvider);
    resourceLoader->addTextureProvider("image/jpeg", stbProvider);
    
    // 7. 加载资源
    resourceLoader->loadResources(asset);
    
    // 8. 释放源数据
    asset->releaseSourceData();
    
    // 9. 添加到场景
    scene->addEntities(asset->getEntities(), asset->getEntityCount());
    
    // 10. 渲染循环...
    
    // 11. 清理
    scene->removeEntities(asset->getEntities(), asset->getEntityCount());
    loader->destroyAsset(asset);
    materials->destroyMaterials();
    delete materials;
    delete stbProvider;
    delete resourceLoader;
    AssetLoader::destroy(&loader);
    Engine::destroy(&engine);
}
```

### 异步加载 + 渐进式显示

```cpp
void loadModelAsync(const char* path) {
    // 1-5. 同上...
    Engine* engine = Engine::create();
    Scene* scene = engine->createScene();
    MaterialProvider* materials = createJitShaderProvider(engine);
    AssetLoader* loader = AssetLoader::create({engine, materials});
    
    std::vector<uint8_t> data = readFile(path);
    FilamentAsset* asset = loader->createAsset(data.data(), data.size());
    data.clear();
    
    // 6. 创建 ResourceLoader
    TextureProvider* stbProvider = createStbProvider(engine);
    ResourceLoader* resourceLoader = new ResourceLoader({engine, path});
    resourceLoader->addTextureProvider("image/png", stbProvider);
    resourceLoader->addTextureProvider("image/jpeg", stbProvider);
    
    // 7. 开始异步加载
    if (!resourceLoader->asyncBeginLoad(asset)) {
        slog.e << "Failed to begin loading" << io::endl;
        return;
    }
    
    // 8. 渲染循环
    bool loadingComplete = false;
    while (rendering) {
        // 更新加载
        resourceLoader->asyncUpdateLoad();
        
        // 渐进式添加渲染对象
        Entity entities[10];
        size_t count = asset->popRenderables(entities, 10);
        if (count > 0) {
            scene->addEntities(entities, count);
            slog.i << "Added " << count << " renderables" << io::endl;
        }
        
        // 检查进度
        float progress = resourceLoader->asyncGetLoadProgress();
        if (progress >= 1.0f && !loadingComplete) {
            slog.i << "Loading complete!" << io::endl;
            asset->releaseSourceData();
            loadingComplete = true;
        }
        
        // 显示进度
        if (!loadingComplete) {
            drawProgressBar(progress);
        }
        
        // 渲染
        if (renderer->beginFrame(swapChain)) {
            renderer->render(view);
            renderer->endFrame();
        }
    }
    
    // 9. 清理...
}
```

### 多实例加载

```cpp
void loadInstanced(const char* path, size_t instanceCount) {
    // 创建引擎和加载器...
    Engine* engine = Engine::create();
    MaterialProvider* materials = createJitShaderProvider(engine);
    AssetLoader* loader = AssetLoader::create({engine, materials});
    
    std::vector<uint8_t> data = readFile(path);
    
    // 创建多实例资产
    std::vector<FilamentInstance*> instances(instanceCount);
    FilamentAsset* asset = loader->createInstancedAsset(
        data.data(), data.size(),
        instances.data(), instanceCount
    );
    
    // 加载资源（只需加载一次，所有实例共享）
    TextureProvider* stbProvider = createStbProvider(engine);
    ResourceLoader* resourceLoader = new ResourceLoader({engine, path});
    resourceLoader->addTextureProvider("image/png", stbProvider);
    resourceLoader->loadResources(asset);
    
    asset->releaseSourceData();
    
    // 设置每个实例的变换
    auto tm = engine->getTransformManager();
    for (size_t i = 0; i < instanceCount; ++i) {
        Entity root = instances[i]->getRoot();
        auto ti = tm.getInstance(root);
        
        // 设置位置（例如：排成一行）
        math::mat4f transform = math::mat4f::translation(math::float3(i * 5.0f, 0, 0));
        tm.setTransform(ti, transform);
        
        // 添加到场景
        scene->addEntities(
            instances[i]->getEntities(),
            instances[i]->getEntityCount()
        );
    }
    
    // 渲染...
    
    // 清理（销毁主资产会销毁所有实例）
    loader->destroyAsset(asset);
}
```

---

## 最佳实践

### 1. 选择合适的 MaterialProvider

```cpp
// 桌面平台 - JIT（首次慢，运行时快）
#if defined(__APPLE__) || defined(_WIN32) || defined(__linux__)
    MaterialProvider* materials = createJitShaderProvider(engine);
#else
    // 移动平台 - Ubershader（加载快）
    MaterialProvider* materials = createUbershaderProvider(
        engine, GLTF_UBERSHADERS_DATA, GLTF_UBERSHADERS_SIZE);
#endif
```

### 2. 使用异步加载

```cpp
// ❌ 不推荐（阻塞主线程）
resourceLoader->loadResources(asset);

// ✅ 推荐（非阻塞）
resourceLoader->asyncBeginLoad(asset);
while (rendering) {
    resourceLoader->asyncUpdateLoad();
    // ...
}
```

### 3. 及时释放源数据

```cpp
// 资源加载完成后
if (resourceLoader->asyncGetLoadProgress() >= 1.0f) {
    asset->releaseSourceData();  // 释放 cgltf_data（节省内存）
}
```

### 4. 使用渐进式显示

```cpp
// 大资产使用渐进式显示（提升用户体验）
if (assetSizeInMB > 10) {
    while (Entity e = asset->popRenderable()) {
        scene->addEntity(e);
    }
}
```

### 5. 错误处理

```cpp
FilamentAsset* asset = loader->createAsset(data.data(), data.size());
if (!asset) {
    slog.e << "Failed to parse glTF" << io::endl;
    // 检查 glTF 文件是否有效
    return;
}

if (!resourceLoader->asyncBeginLoad(asset)) {
    slog.e << "Failed to start loading resources" << io::endl;
    // 检查文件路径、权限等
    return;
}
```

---

## 总结

Filament glTF 加载的关键要点：

1. **AssetLoader**: 解析 glTF，创建 Filament 对象（实体、组件）
2. **ResourceLoader**: 异步加载资源（纹理、缓冲数据）
3. **MaterialProvider**: 创建材质（JIT 或 Ubershader）
4. **渐进式显示**: 纹理就绪后逐步显示（popRenderable）
5. **内存管理**: 加载完成后释放源数据（releaseSourceData）

**加载流程总结**:
```
读取文件 → AssetLoader（创建对象）→ ResourceLoader（加载资源）
→ 渐进式显示 → 释放源数据 → 渲染
```

**性能优化**:
- 使用异步加载（避免阻塞）
- 选择合适的 MaterialProvider（平台优化）
- 使用渐进式显示（大资产）
- 及时释放不需要的数据（内存优化）
