# 资源管理系统

本文档详细讲解 Filament Engine 的资源管理系统，包括 VertexBuffer、IndexBuffer、Texture、Material 等资源的创建、更新、销毁流程，以及资源的生命周期管理。

---

## 资源管理概述

Filament 中的资源包括：
- **几何资源**: VertexBuffer、IndexBuffer
- **纹理资源**: Texture、RenderTarget
- **材质资源**: Material、MaterialInstance
- **其他资源**: IndirectLight、Skybox

所有资源都由 Engine 创建和管理，遵循明确的生命周期规则。

---

## VertexBuffer (顶点缓冲)

### 创建 VertexBuffer

```cpp
#include <filament/VertexBuffer.h>

using namespace filament;

// 定义顶点结构
struct Vertex {
    math::float3 position;
    math::float3 normal;
    math::float2 uv;
};

// 顶点数据
Vertex vertices[] = {
    {{-1, -1, 0}, {0, 0, 1}, {0, 0}},
    {{ 1, -1, 0}, {0, 0, 1}, {1, 0}},
    {{ 0,  1, 0}, {0, 0, 1}, {0.5, 1}}
};

// 创建 VertexBuffer
VertexBuffer* vertexBuffer = VertexBuffer::Builder()
    .vertexCount(3)
    .bufferCount(1)
    .attribute(VertexAttribute::POSITION, 0,
               VertexBuffer::AttributeType::FLOAT3,
               offsetof(Vertex, position), sizeof(Vertex))
    .attribute(VertexAttribute::NORMAL, 0,
               VertexBuffer::AttributeType::FLOAT3,
               offsetof(Vertex, normal), sizeof(Vertex))
    .attribute(VertexAttribute::UV0, 0,
               VertexBuffer::AttributeType::FLOAT2,
               offsetof(Vertex, uv), sizeof(Vertex))
    .build(*engine);

// 上传数据
VertexBuffer::BufferDescriptor buffer(
    vertices,
    sizeof(vertices),
    [](void* buffer, size_t size, void* user) {
        // 数据释放回调（可选）
        // 如果数据是动态分配的，在这里释放
    }
);

vertexBuffer->setBufferAt(*engine, 0, std::move(buffer));
```

### VertexBuffer Builder 参数

| 方法 | 说明 | 必需 |
|------|------|------|
| **vertexCount()** | 顶点数量 | ✅ |
| **bufferCount()** | 缓冲区数量（通常为1） | ✅ |
| **attribute()** | 顶点属性定义 | ✅ |
| **normalized()** | 是否归一化（用于整数类型） | ❌ |
| **enableBufferObjects()** | 启用 BufferObject（用于共享） | ❌ |

### 顶点属性类型

```cpp
enum class VertexAttribute : uint8_t {
    POSITION,        // 位置 (必需)
    TANGENTS,        // 切线和副切线（四元数编码）
    COLOR,           // 顶点颜色
    UV0,             // 纹理坐标 0
    UV1,             // 纹理坐标 1
    BONE_INDICES,    // 骨骼索引（蒙皮）
    BONE_WEIGHTS,    // 骨骼权重（蒙皮）
    CUSTOM0,         // 自定义属性 0-7
    CUSTOM1,
    // ...
};

enum class AttributeType : uint8_t {
    BYTE,
    BYTE2,
    BYTE3,
    BYTE4,
    UBYTE,
    UBYTE2,
    UBYTE3,
    UBYTE4,
    SHORT,
    SHORT2,
    SHORT3,
    SHORT4,
    USHORT,
    USHORT2,
    USHORT3,
    USHORT4,
    INT,
    UINT,
    FLOAT,
    FLOAT2,
    FLOAT3,
    FLOAT4,
    HALF,
    HALF2,
    HALF3,
    HALF4,
};
```

### 多缓冲区布局

对于大型模型，可以使用多个缓冲区分离不同属性：

```cpp
// 分离的缓冲区
float3 positions[10000];
float3 normals[10000];
float2 uvs[10000];

VertexBuffer* vb = VertexBuffer::Builder()
    .vertexCount(10000)
    .bufferCount(3)  // 3 个缓冲区
    // Buffer 0: 位置
    .attribute(VertexAttribute::POSITION, 0,
               VertexBuffer::AttributeType::FLOAT3, 0, sizeof(float3))
    // Buffer 1: 法线
    .attribute(VertexAttribute::NORMAL, 1,
               VertexBuffer::AttributeType::FLOAT3, 0, sizeof(float3))
    // Buffer 2: UV
    .attribute(VertexAttribute::UV0, 2,
               VertexBuffer::AttributeType::FLOAT2, 0, sizeof(float2))
    .build(*engine);

// 分别上传
vb->setBufferAt(*engine, 0, VertexBuffer::BufferDescriptor(positions, sizeof(positions)));
vb->setBufferAt(*engine, 1, VertexBuffer::BufferDescriptor(normals, sizeof(normals)));
vb->setBufferAt(*engine, 2, VertexBuffer::BufferDescriptor(uvs, sizeof(uvs)));
```

**优势**:
- 可以单独更新某个属性（如只更新位置）
- 某些 GPU 优化可能更好

---

## IndexBuffer (索引缓冲)

### 创建 IndexBuffer

```cpp
#include <filament/IndexBuffer.h>

// 索引数据
uint16_t indices[] = {0, 1, 2};

// 创建 IndexBuffer
IndexBuffer* indexBuffer = IndexBuffer::Builder()
    .indexCount(3)
    .bufferType(IndexBuffer::IndexType::USHORT)
    .build(*engine);

// 上传数据
IndexBuffer::BufferDescriptor buffer(
    indices,
    sizeof(indices)
);

indexBuffer->setBuffer(*engine, std::move(buffer));
```

### IndexBuffer Builder 参数

| 方法 | 说明 | 必需 |
|------|------|------|
| **indexCount()** | 索引数量 | ✅ |
| **bufferType()** | 索引类型（USHORT/UINT） | ✅ |

### 索引类型选择

```cpp
enum class IndexType : uint8_t {
    USHORT,  // 16位，最多 65535 个顶点
    UINT,    // 32位，最多 4294967295 个顶点
};

// 选择原则：
// - 顶点数 < 65536: 使用 USHORT（节省内存和带宽）
// - 顶点数 >= 65536: 使用 UINT
```

---

## Texture (纹理)

### 创建 2D 纹理

```cpp
#include <filament/Texture.h>

// 纹理数据（RGBA8，512x512）
uint8_t* pixels = new uint8_t[512 * 512 * 4];
// ... 填充像素数据

// 创建纹理
Texture* texture = Texture::Builder()
    .width(512)
    .height(512)
    .levels(9)  // mipmap 层级数（log2(512) + 1）
    .format(Texture::InternalFormat::RGBA8)
    .sampler(Texture::Sampler::SAMPLER_2D)
    .build(*engine);

// 上传数据（level 0）
Texture::PixelBufferDescriptor buffer(
    pixels,
    512 * 512 * 4,
    Texture::Format::RGBA,
    Texture::Type::UBYTE,
    [](void* buffer, size_t size, void* user) {
        delete[] static_cast<uint8_t*>(buffer);
    }
);

texture->setImage(*engine, 0, std::move(buffer));

// 生成 mipmap
texture->generateMipmaps(*engine);
```

### 纹理格式

```cpp
enum class InternalFormat : uint16_t {
    // 8位格式
    R8,
    R8_SNORM,
    R8UI,
    R8I,
    STENCIL8,

    // 16位格式
    R16F,
    R16UI,
    R16I,
    RG8,
    RG8_SNORM,
    RG8UI,
    RG8I,
    RGB565,
    RGB5_A1,
    RGBA4,
    DEPTH16,

    // 24位格式
    RGB8,
    SRGB8,
    RGB8_SNORM,
    RGB8UI,
    RGB8I,
    DEPTH24,

    // 32位格式
    R32F,
    R32UI,
    R32I,
    RG16F,
    RG16UI,
    RG16I,
    R11F_G11F_B10F,
    RGB9_E5,
    RGBA8,
    SRGB8_A8,
    RGBA8_SNORM,
    RGB10_A2,
    RGBA8UI,
    RGBA8I,
    DEPTH32F,
    DEPTH24_STENCIL8,
    DEPTH32F_STENCIL8,

    // 48位格式
    RGB16F,
    RGB16UI,
    RGB16I,

    // 64位格式
    RG32F,
    RG32UI,
    RG32I,
    RGBA16F,
    RGBA16UI,
    RGBA16I,

    // 96位格式
    RGB32F,
    RGB32UI,
    RGB32I,

    // 128位格式
    RGBA32F,
    RGBA32UI,
    RGBA32I,

    // 压缩格式
    EAC_R11,
    EAC_R11_SIGNED,
    EAC_RG11,
    EAC_RG11_SIGNED,
    ETC2_RGB8,
    ETC2_SRGB8,
    ETC2_RGB8_A1,
    ETC2_SRGB8_A1,
    ETC2_EAC_RGBA8,
    ETC2_EAC_SRGBA8,

    DXT1_RGB,
    DXT1_RGBA,
    DXT3_RGBA,
    DXT5_RGBA,
    DXT1_SRGB,
    DXT1_SRGBA,
    DXT3_SRGBA,
    DXT5_SRGBA,

    RGBA_ASTC_4x4,
    RGBA_ASTC_5x4,
    RGBA_ASTC_5x5,
    // ... 更多 ASTC 格式
};
```

### 创建 Cubemap

```cpp
// Cubemap 纹理
Texture* cubemap = Texture::Builder()
    .width(512)
    .height(512)
    .levels(1)
    .format(Texture::InternalFormat::RGBA8)
    .sampler(Texture::Sampler::SAMPLER_CUBEMAP)
    .build(*engine);

// 上传 6 个面
for (uint8_t face = 0; face < 6; face++) {
    Texture::FaceOffsets offsets(face);

    Texture::PixelBufferDescriptor buffer(
        facePixels[face],
        512 * 512 * 4,
        Texture::Format::RGBA,
        Texture::Type::UBYTE
    );

    cubemap->setImage(*engine, 0, std::move(buffer), offsets);
}
```

### 创建 3D 纹理

```cpp
Texture* texture3D = Texture::Builder()
    .width(256)
    .height(256)
    .depth(256)
    .levels(1)
    .format(Texture::InternalFormat::R8)
    .sampler(Texture::Sampler::SAMPLER_3D)
    .build(*engine);

Texture::PixelBufferDescriptor buffer(
    volumeData,
    256 * 256 * 256,
    Texture::Format::R,
    Texture::Type::UBYTE
);

texture3D->setImage(*engine, 0, std::move(buffer));
```

### 纹理压缩

使用压缩纹理减少内存和带宽：

```cpp
// Android: ETC2 压缩
Texture* compressedTexture = Texture::Builder()
    .width(512)
    .height(512)
    .levels(1)
    .format(Texture::InternalFormat::ETC2_RGB8)
    .sampler(Texture::Sampler::SAMPLER_2D)
    .build(*engine);

// 上传压缩数据
Texture::PixelBufferDescriptor buffer(
    compressedData,
    compressedSize,
    Texture::Format::ETC2_RGB8,  // 压缩格式
    Texture::Type::UBYTE
);

compressedTexture->setImage(*engine, 0, std::move(buffer));
```

**常用压缩格式**:
- **Android**: ETC2 (OpenGL ES 3.0+)
- **iOS**: ASTC 或 PVRTC
- **桌面**: DXT/BC (Windows), ASTC (通用)

---

## Material 和 MaterialInstance

### Material (材质)

Material 是编译后的 Shader 程序，由 matc 工具生成：

```cpp
#include <filament/Material.h>

// 加载编译好的材质包
std::vector<uint8_t> materialData = readFile("material.filamat");

Material* material = Material::Builder()
    .package(materialData.data(), materialData.size())
    .build(*engine);

// 查询材质信息
const char* name = material->getName();
Shading shading = material->getShading();
bool isDoubleSided = material->isDoubleSided();

// 获取参数信息
size_t paramCount = material->getParameterCount();
for (size_t i = 0; i < paramCount; i++) {
    Material::ParameterInfo info = material->getParameterInfo(i);
    const char* paramName = info.name;
    Material::ParameterType type = info.type;
}
```

### MaterialInstance (材质实例)

MaterialInstance 是 Material 的实例，可以设置不同的参数：

```cpp
// 创建材质实例
MaterialInstance* matInstance = material->createInstance();

// 设置标量参数
matInstance->setParameter("roughness", 0.5f);
matInstance->setParameter("metallic", 1.0f);

// 设置向量参数
matInstance->setParameter("baseColor", math::float3(1.0, 0.0, 0.0));

// 设置纹理参数
TextureSampler sampler(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
                       TextureSampler::MagFilter::LINEAR);
matInstance->setParameter("albedo", texture, sampler);

// 设置纹理数组参数
Texture* textures[4] = {tex0, tex1, tex2, tex3};
matInstance->setParameter("texArray", textures, 4);
```

### TextureSampler 配置

```cpp
TextureSampler sampler;

// 过滤模式
sampler.setMinFilter(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR);
sampler.setMagFilter(TextureSampler::MagFilter::LINEAR);

// 包裹模式
sampler.setWrapModeS(TextureSampler::WrapMode::REPEAT);
sampler.setWrapModeT(TextureSampler::WrapMode::CLAMP_TO_EDGE);

// 各向异性过滤
sampler.setAnisotropy(16.0f);

// 应用
matInstance->setParameter("albedo", texture, sampler);
```

---

## IndirectLight (间接光照)

### 创建 IBL

```cpp
#include <filament/IndirectLight.h>

// 从 KTX 文件加载 IBL
Texture* iblTexture = /* 加载 IBL 纹理 */;

// 创建间接光
IndirectLight* ibl = IndirectLight::Builder()
    .reflections(iblTexture)
    .intensity(30000.0f)  // 强度（lux）
    .rotation(mat3f::rotation(M_PI/4, float3{0, 1, 0}))
    .build(*engine);

// 应用到场景
scene->setIndirectLight(ibl);
```

### IBL 球谐系数

可以使用球谐系数代替全分辨率 IBL：

```cpp
// 预计算的球谐系数（由 cmgen 工具生成）
const float3 sh[9] = {/* ... */};

IndirectLight* ibl = IndirectLight::Builder()
    .irradiance(3, sh)  // 3 阶球谐
    .reflections(reflectionTexture)
    .intensity(30000.0f)
    .build(*engine);
```

---

## Skybox (天空盒)

### 创建天空盒

```cpp
#include <filament/Skybox.h>

// Cubemap 纹理
Texture* skyTexture = /* ... */;

// 创建天空盒
Skybox* skybox = Skybox::Builder()
    .environment(skyTexture)
    .intensity(30000.0f)
    .build(*engine);

// 应用到场景
scene->setSkybox(skybox);
```

### 程序化天空盒

```cpp
// 使用颜色创建简单天空盒
Skybox* skybox = Skybox::Builder()
    .color({0.5f, 0.7f, 1.0f, 1.0f})
    .build(*engine);
```

---

## RenderTarget (渲染目标)

### 创建离屏渲染目标

```cpp
#include <filament/RenderTarget.h>

// 创建颜色纹理
Texture* colorTexture = Texture::Builder()
    .width(1920)
    .height(1080)
    .levels(1)
    .format(Texture::InternalFormat::RGBA8)
    .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)
    .build(*engine);

// 创建深度纹理
Texture* depthTexture = Texture::Builder()
    .width(1920)
    .height(1080)
    .levels(1)
    .format(Texture::InternalFormat::DEPTH24)
    .usage(Texture::Usage::DEPTH_ATTACHMENT)
    .build(*engine);

// 创建 RenderTarget
RenderTarget* renderTarget = RenderTarget::Builder()
    .texture(RenderTarget::AttachmentPoint::COLOR, colorTexture)
    .texture(RenderTarget::AttachmentPoint::DEPTH, depthTexture)
    .build(*engine);

// 渲染到 RenderTarget
view->setRenderTarget(renderTarget);
renderer->render(view);

// 恢复默认渲染目标
view->setRenderTarget(nullptr);
```

### 多重采样渲染目标 (MSAA)

```cpp
Texture* colorTexture = Texture::Builder()
    .width(1920)
    .height(1080)
    .levels(1)
    .format(Texture::InternalFormat::RGBA8)
    .usage(Texture::Usage::COLOR_ATTACHMENT)
    .samples(4)  // 4x MSAA
    .build(*engine);

RenderTarget* msaaTarget = RenderTarget::Builder()
    .texture(RenderTarget::AttachmentPoint::COLOR, colorTexture)
    .build(*engine);
```

---

## 资源生命周期管理

### 创建资源

```cpp
// 1. 所有资源由 Engine 创建
VertexBuffer* vb = VertexBuffer::Builder()./* ... */.build(*engine);
Texture* tex = Texture::Builder()./* ... */.build(*engine);
Material* mat = Material::Builder()./* ... */.build(*engine);

// 2. 资源立即可用
vb->setBufferAt(*engine, 0, buffer);
```

### 销毁资源

```cpp
// 必须显式销毁，按相反顺序
engine->destroy(materialInstance);
engine->destroy(material);
engine->destroy(texture);
engine->destroy(indexBuffer);
engine->destroy(vertexBuffer);

// Engine 销毁时会自动销毁所有未销毁的资源
Engine::destroy(&engine);
```

### 资源引用计数

Filament 不使用引用计数，资源生命周期由用户管理：

```cpp
// ❌ 错误：提前销毁
Texture* tex = Texture::Builder().build(*engine);
matInstance->setParameter("albedo", tex);
engine->destroy(tex);  // 错误！MaterialInstance 还在使用

renderer->render(view);  // 崩溃！

// ✅ 正确：确保资源在使用期间有效
Texture* tex = Texture::Builder().build(*engine);
matInstance->setParameter("albedo", tex);

// 渲染...
renderer->render(view);

// 不再使用后才销毁
engine->destroy(tex);
```

### 资源共享

多个对象可以共享同一资源：

```cpp
// 创建一个纹理
Texture* tex = Texture::Builder().build(*engine);

// 多个材质实例共享
matInstance1->setParameter("albedo", tex);
matInstance2->setParameter("albedo", tex);
matInstance3->setParameter("albedo", tex);

// 只需要销毁一次
engine->destroy(tex);
```

---

## BufferDescriptor 和回调

### 数据生命周期

BufferDescriptor 管理上传数据的生命周期：

```cpp
// 方式1：使用栈数据（立即上传）
float vertices[] = {/* ... */};
VertexBuffer::BufferDescriptor buffer(vertices, sizeof(vertices));
vb->setBufferAt(*engine, 0, std::move(buffer));
// vertices 可以在上传后销毁

// 方式2：使用堆数据（带释放回调）
float* vertices = new float[1000];
// ... 填充数据

VertexBuffer::BufferDescriptor buffer(
    vertices,
    sizeof(float) * 1000,
    [](void* buffer, size_t size, void* user) {
        delete[] static_cast<float*>(buffer);
    }
);

vb->setBufferAt(*engine, 0, std::move(buffer));
// Filament 会在适当时机调用回调释放内存

// 方式3：自定义用户数据
struct Context {
    float* data;
    size_t count;
};

Context* ctx = new Context{vertices, 1000};

VertexBuffer::BufferDescriptor buffer(
    ctx->data,
    ctx->count * sizeof(float),
    [](void* buffer, size_t size, void* user) {
        Context* ctx = static_cast<Context*>(user);
        delete[] ctx->data;
        delete ctx;
    },
    ctx  // 用户数据
);
```

---

## 完整示例：加载模型

```cpp
struct Model {
    VertexBuffer* vertexBuffer;
    IndexBuffer* indexBuffer;
    Material* material;
    MaterialInstance* materialInstance;
    Texture* albedoTexture;
    Entity entity;
};

Model* loadModel(Engine* engine, Scene* scene, const char* path) {
    Model* model = new Model();

    // 1. 加载几何数据
    MeshData meshData = loadMesh(path);

    model->vertexBuffer = VertexBuffer::Builder()
        .vertexCount(meshData.vertexCount)
        .bufferCount(1)
        .attribute(VertexAttribute::POSITION, 0,
                   VertexBuffer::AttributeType::FLOAT3, 0, sizeof(Vertex))
        .attribute(VertexAttribute::NORMAL, 0,
                   VertexBuffer::AttributeType::FLOAT3, 12, sizeof(Vertex))
        .attribute(VertexAttribute::UV0, 0,
                   VertexBuffer::AttributeType::FLOAT2, 24, sizeof(Vertex))
        .build(*engine);

    model->vertexBuffer->setBufferAt(*engine, 0,
        VertexBuffer::BufferDescriptor(meshData.vertices, meshData.vertexDataSize,
            [](void* buf, size_t, void*) { free(buf); }));

    model->indexBuffer = IndexBuffer::Builder()
        .indexCount(meshData.indexCount)
        .bufferType(IndexBuffer::IndexType::UINT)
        .build(*engine);

    model->indexBuffer->setBuffer(*engine,
        IndexBuffer::BufferDescriptor(meshData.indices, meshData.indexDataSize,
            [](void* buf, size_t, void*) { free(buf); }));

    // 2. 加载纹理
    TextureData texData = loadTexture(meshData.texturePath);

    model->albedoTexture = Texture::Builder()
        .width(texData.width)
        .height(texData.height)
        .levels(1)
        .format(Texture::InternalFormat::RGBA8)
        .build(*engine);

    model->albedoTexture->setImage(*engine, 0,
        Texture::PixelBufferDescriptor(texData.pixels, texData.size,
            Texture::Format::RGBA, Texture::Type::UBYTE,
            [](void* buf, size_t, void*) { free(buf); }));

    // 3. 加载材质
    std::vector<uint8_t> matData = readFile("pbr.filamat");
    model->material = Material::Builder()
        .package(matData.data(), matData.size())
        .build(*engine);

    model->materialInstance = model->material->createInstance();
    model->materialInstance->setParameter("baseColor", math::float4(1.0f));
    model->materialInstance->setParameter("metallic", 0.0f);
    model->materialInstance->setParameter("roughness", 0.5f);
    model->materialInstance->setParameter("albedo", model->albedoTexture,
        TextureSampler(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
                       TextureSampler::MagFilter::LINEAR));

    // 4. 创建 Renderable
    model->entity = EntityManager::get().create();

    RenderableManager::Builder(1)
        .boundingBox(meshData.boundingBox)
        .material(0, model->materialInstance)
        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,
                  model->vertexBuffer, model->indexBuffer)
        .culling(true)
        .receiveShadows(true)
        .castShadows(true)
        .build(*engine, model->entity);

    // 5. 添加到场景
    scene->addEntity(model->entity);

    return model;
}

void destroyModel(Engine* engine, Scene* scene, Model* model) {
    scene->remove(model->entity);
    EntityManager::get().destroy(model->entity);

    engine->destroy(model->materialInstance);
    engine->destroy(model->material);
    engine->destroy(model->albedoTexture);
    engine->destroy(model->indexBuffer);
    engine->destroy(model->vertexBuffer);

    delete model;
}
```

---

## 相关文档

- **[01-core-concepts.md](01-core-concepts.md)**: Engine 核心概念
- **[02-entity-component.md](02-entity-component.md)**: Entity-Component 系统
- **[04-renderable-system.md](04-renderable-system.md)**: Renderable 系统

**底层实现**:
- `../backend/02-driver-abstraction.md`: Driver 资源创建
- `../backend/04-resource-handles.md`: Handle 系统

**材质系统**:
- `../material/08-runtime-usage.md`: Material 运行时使用

---

## 总结

Filament 的资源管理系统通过 **Builder 模式**、**明确的生命周期**、**BufferDescriptor 回调机制**，提供了简洁而强大的资源管理 API。

**核心要点**:
- ✅ 所有资源由 Engine 创建和销毁
- ✅ 使用 Builder 模式创建资源
- ✅ BufferDescriptor 管理数据生命周期
- ✅ 资源可以共享，但生命周期需手动管理
- ✅ MaterialInstance 可以设置不同参数
