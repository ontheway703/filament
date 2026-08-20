# 运行时使用

本文档详细说明如何在 Filament 应用中加载和使用材质。

---

## 材质加载流程

### 完整流程概览

```
.filamat 文件
    ↓ 读取二进制数据
uint8_t* data
    ↓ Material::Builder::package()
Material
    ↓ Material::createInstance()
MaterialInstance
    ↓ 设置参数
MaterialInstance (配置完成)
    ↓ RenderableManager::Builder::material()
Renderable
```

---

## 1. 加载材质

### 方法 1: 从文件加载

```cpp
#include <filament/Material.h>
#include <fstream>
#include <vector>

// 读取 .filamat 文件
std::vector<uint8_t> readMaterialFile(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open material file");
    }

    size_t size = file.tellg();
    std::vector<uint8_t> data(size);

    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(data.data()), size);
    file.close();

    return data;
}

// 创建 Material
Material* loadMaterial(Engine& engine, const char* path) {
    std::vector<uint8_t> data = readMaterialFile(path);

    Material* material = Material::Builder()
        .package(data.data(), data.size())
        .build(engine);

    if (!material) {
        throw std::runtime_error("Failed to create material");
    }

    return material;
}
```

### 方法 2: 从内存加载（嵌入资源）

```cpp
// 将 .filamat 编译进可执行文件
#include "materials/pbr_material.h"  // 包含 uint8_t PBR_MATERIAL_DATA[]

Material* material = Material::Builder()
    .package(PBR_MATERIAL_DATA, PBR_MATERIAL_SIZE)
    .build(engine);
```

### 方法 3: 从资源管理器加载

```cpp
class MaterialCache {
public:
    Material* load(Engine& engine, const std::string& name) {
        // 检查缓存
        auto it = mMaterials.find(name);
        if (it != mMaterials.end()) {
            return it->second;
        }

        // 加载材质
        std::string path = "materials/" + name + ".filamat";
        Material* material = loadMaterial(engine, path.c_str());

        // 缓存
        mMaterials[name] = material;
        return material;
    }

    void clear(Engine& engine) {
        for (auto& [name, material] : mMaterials) {
            engine.destroy(material);
        }
        mMaterials.clear();
    }

private:
    std::unordered_map<std::string, Material*> mMaterials;
};

// 使用
MaterialCache cache;
Material* pbr = cache.load(engine, "pbr_textured");
Material* glass = cache.load(engine, "glass");
```

### 特化常量（可选）

```cpp
// 在加载时特化常量参数
Material* material = Material::Builder()
    .package(data, size)
    .constant("maxLights", 8)           // int 常量
    .constant("useShadows", true)       // bool 常量
    .constant("fadeDistance", 100.0f)   // float 常量
    .build(engine);
```

---

## 2. 创建材质实例

### 基本创建

```cpp
// 创建默认实例
MaterialInstance* instance = material->createInstance();

// 创建命名实例
MaterialInstance* namedInstance = material->createInstance("RedMetal");
```

### 复制实例

```cpp
// 从现有实例创建副本
MaterialInstance* original = material->createInstance();
original->setParameter("baseColor", math::float3{1, 0, 0});

MaterialInstance* copy = MaterialInstance::duplicate(original, "CopyOfOriginal");
// copy 继承了 original 的所有参数设置
```

### 获取默认实例

```cpp
// 每个 Material 都有一个默认实例
MaterialInstance* defaultInstance = material->getDefaultInstance();

// 可以直接设置默认实例的参数
material->setDefaultParameter("roughness", 0.5f);
// 等价于
material->getDefaultInstance()->setParameter("roughness", 0.5f);
```

---

## 3. 设置材质参数

### Uniform 参数

```cpp
using namespace filament::math;

// 标量
instance->setParameter("roughness", 0.5f);
instance->setParameter("metallic", 1.0f);

// 向量
instance->setParameter("baseColor", float3{0.8f, 0.2f, 0.2f});
instance->setParameter("emissive", float4{1.0f, 0.5f, 0.0f, 5.0f});

// 矩阵
instance->setParameter("transform", mat4f{
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1
});
```

### 数组参数

```cpp
// float 数组
float weights[4] = {0.1f, 0.2f, 0.3f, 0.4f};
instance->setParameter("weights", weights, 4);

// float3 数组
float3 colors[3] = {
    {1, 0, 0},
    {0, 1, 0},
    {0, 0, 1}
};
instance->setParameter("colors", colors, 3);
```

### 纹理参数

```cpp
#include <filament/Texture.h>
#include <filament/TextureSampler.h>

// 创建纹理
Texture* albedoTexture = loadTexture(engine, "albedo.png");
Texture* normalTexture = loadTexture(engine, "normal.png");

// 配置采样器
TextureSampler sampler(
    TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
    TextureSampler::MagFilter::LINEAR
);
sampler.setWrapModeS(TextureSampler::WrapMode::REPEAT);
sampler.setWrapModeT(TextureSampler::WrapMode::REPEAT);
sampler.setAnisotropy(16.0f);

// 绑定纹理
instance->setParameter("albedoMap", albedoTexture, sampler);
instance->setParameter("normalMap", normalTexture, sampler);
```

### 颜色参数

```cpp
// Linear RGB
instance->setParameter("tint", RgbType::LINEAR, float3{1, 0.5f, 0});

// sRGB
instance->setParameter("tint", RgbType::sRGB, float3{1, 0.5f, 0});

// Linear RGBA
instance->setParameter("color", RgbaType::LINEAR, float4{1, 0.5f, 0, 0.8f});

// sRGB + premultipled alpha
instance->setParameter("color", RgbaType::PREMULTIPLIED_sRGB, float4{0.8f, 0.4f, 0, 0.8f});
```

### 获取参数值

```cpp
// 获取参数
float roughness = instance->getParameter<float>("roughness");
float3 color = instance->getParameter<float3>("baseColor");
mat4f transform = instance->getParameter<mat4f>("transform");
```

---

## 4. 覆盖渲染状态

### 剔除模式

```cpp
// 覆盖材质的剔除模式
instance->setCullingMode(CullingMode::NONE);        // 双面
instance->setCullingMode(CullingMode::BACK);        // 剔除背面
instance->setCullingMode(CullingMode::FRONT);       // 剔除正面
instance->setCullingMode(CullingMode::FRONT_AND_BACK); // 剔除所有

// 针对阴影通道单独设置
instance->setCullingMode(
    CullingMode::BACK,   // 颜色通道
    CullingMode::FRONT   // 阴影通道（防止 peter-panning）
);
```

### 双面光照

```cpp
// 启用双面光照
instance->setDoubleSided(true);

// 检查状态
bool isDoubleSided = instance->isDoubleSided();
```

### 深度设置

```cpp
// 深度写入
instance->setDepthWrite(true);   // 启用
instance->setDepthWrite(false);  // 禁用（透明物体）

// 深度测试
instance->setDepthCulling(true);   // 启用
instance->setDepthCulling(false);  // 禁用（UI、天空盒）

// 深度函数
instance->setDepthFunc(DepthFunc::LESS_OR_EQUAL);  // 默认
instance->setDepthFunc(DepthFunc::GREATER);
instance->setDepthFunc(DepthFunc::ALWAYS);
```

### 颜色写入

```cpp
// 控制颜色写入（通常用于只写深度的物体）
instance->setColorWrite(false);  // 只写深度
instance->setColorWrite(true);   // 写颜色和深度
```

### 透明模式

```cpp
instance->setTransparencyMode(TransparencyMode::DEFAULT);
instance->setTransparencyMode(TransparencyMode::TWO_PASSES_ONE_SIDE);
instance->setTransparencyMode(TransparencyMode::TWO_PASSES_TWO_SIDES);
```

### Alpha 遮罩阈值

```cpp
// 设置 alpha 遮罩阈值（masked 混合模式）
instance->setMaskThreshold(0.5f);  // alpha < 0.5 丢弃

// 获取阈值
float threshold = instance->getMaskThreshold();
```

### 裁剪矩形

```cpp
// 设置裁剪矩形（相对于视口）
instance->setScissor(100, 100, 800, 600);

// 禁用裁剪
instance->unsetScissor();
```

### 多边形偏移

```cpp
// 设置多边形偏移（避免 Z-fighting）
instance->setPolygonOffset(1.0f, 1.0f);

// 警告：性能影响大，谨慎使用
```

### 模板测试

```cpp
// 启用模板写入
instance->setStencilWrite(true);

// 设置模板比较函数
instance->setStencilCompareFunction(
    StencilCompareFunc::EQUAL,
    StencilFace::FRONT_AND_BACK
);

// 设置模板操作
instance->setStencilOpStencilFail(StencilOperation::KEEP);
instance->setStencilOpDepthFail(StencilOperation::DECREMENT);
instance->setStencilOpDepthStencilPass(StencilOperation::INCREMENT);

// 设置模板参考值和掩码
instance->setStencilReferenceValue(1);
instance->setStencilReadMask(0xFF);
instance->setStencilWriteMask(0xFF);
```

---

## 5. 应用到 Renderable

### 基本使用

```cpp
#include <filament/RenderableManager.h>

using Builder = RenderableManager::Builder;

Builder(1)  // 1 个 primitive
    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb, ib)
    .material(0, instance)
    .boundingBox({{-1, -1, -1}, {1, 1, 1}})
    .build(engine, entity);
```

### 多材质物体

```cpp
// 一个物体包含多个 primitive，每个使用不同材质
MaterialInstance* mat1 = material1->createInstance();
MaterialInstance* mat2 = material2->createInstance();
MaterialInstance* mat3 = material3->createInstance();

Builder(3)  // 3 个 primitives
    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb1, ib1)
    .geometry(1, RenderableManager::PrimitiveType::TRIANGLES, vb2, ib2)
    .geometry(2, RenderableManager::PrimitiveType::TRIANGLES, vb3, ib3)
    .material(0, mat1)
    .material(1, mat2)
    .material(2, mat3)
    .boundingBox({{-10, -10, -10}, {10, 10, 10}})
    .build(engine, entity);
```

### 运行时更换材质

```cpp
// 获取 Renderable 实例
auto& rcm = engine.getRenderableManager();
auto instance = rcm.getInstance(entity);

// 更换材质
rcm.setMaterialInstanceAt(instance, 0, newMaterialInstance);
```

---

## 6. 完整示例

### 示例 1: PBR 金属球

```cpp
// 1. 加载材质
Material* pbrMaterial = loadMaterial(engine, "pbr_textured.filamat");

// 2. 创建实例
MaterialInstance* goldInstance = pbrMaterial->createInstance("Gold");

// 3. 设置参数
goldInstance->setParameter("baseColor", float3{1.0f, 0.86f, 0.57f});
goldInstance->setParameter("roughness", 0.3f);
goldInstance->setParameter("metallic", 1.0f);

// 4. 创建几何体
VertexBuffer* vb = createSphereVertexBuffer(engine);
IndexBuffer* ib = createSphereIndexBuffer(engine);

// 5. 创建实体
Entity sphere = EntityManager::get().create();
RenderableManager::Builder(1)
    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb, ib)
    .material(0, goldInstance)
    .boundingBox({{-1, -1, -1}, {1, 1, 1}})
    .castShadows(true)
    .receiveShadows(true)
    .build(engine, sphere);

// 6. 添加变换
auto& tcm = engine.getTransformManager();
tcm.setTransform(tcm.getInstance(sphere),
    mat4f::translation(float3{0, 0, -5}));

// 7. 添加到场景
scene->addEntity(sphere);
```

### 示例 2: 纹理物体

```cpp
// 1. 加载材质和纹理
Material* material = loadMaterial(engine, "pbr_textured.filamat");
Texture* albedo = loadTexture(engine, "albedo.png");
Texture* normal = loadTexture(engine, "normal.png");
Texture* roughness = loadTexture(engine, "roughness.png");
Texture* ao = loadTexture(engine, "ao.png");

// 2. 创建实例
MaterialInstance* instance = material->createInstance();

// 3. 配置采样器
TextureSampler sampler;
sampler.setWrapModeS(TextureSampler::WrapMode::REPEAT);
sampler.setWrapModeT(TextureSampler::WrapMode::REPEAT);
sampler.setAnisotropy(16.0f);

// 4. 绑定纹理
instance->setParameter("albedoMap", albedo, sampler);
instance->setParameter("normalMap", normal, sampler);
instance->setParameter("roughnessMap", roughness, sampler);
instance->setParameter("aoMap", ao, sampler);

// 5. 创建物体
Entity entity = EntityManager::get().create();
RenderableManager::Builder(1)
    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb, ib)
    .material(0, instance)
    .boundingBox(box)
    .build(engine, entity);

scene->addEntity(entity);
```

### 示例 3: 动画材质

```cpp
class AnimatedMaterial {
public:
    AnimatedMaterial(Material* material) {
        mInstance = material->createInstance();
    }

    void update(float deltaTime) {
        mTime += deltaTime;

        // 更新参数
        mInstance->setParameter("time", mTime);

        // 动画粗糙度
        float roughness = 0.5f + 0.3f * std::sin(mTime);
        mInstance->setParameter("roughness", roughness);

        // 动画颜色
        float3 color{
            0.5f + 0.5f * std::sin(mTime),
            0.5f + 0.5f * std::sin(mTime + 2.0f),
            0.5f + 0.5f * std::sin(mTime + 4.0f)
        };
        mInstance->setParameter("baseColor", color);
    }

    MaterialInstance* getInstance() { return mInstance; }

private:
    MaterialInstance* mInstance;
    float mTime = 0.0f;
};

// 使用
AnimatedMaterial animMat(material);

// 渲染循环
void onFrame(float deltaTime) {
    animMat.update(deltaTime);
    // ... 渲染
}
```

---

## 7. 材质系统管理

### 材质管理器

```cpp
class MaterialManager {
public:
    MaterialManager(Engine& engine) : mEngine(engine) {}

    ~MaterialManager() {
        clear();
    }

    Material* load(const std::string& name) {
        auto it = mMaterials.find(name);
        if (it != mMaterials.end()) {
            return it->second;
        }

        std::string path = "materials/" + name + ".filamat";
        Material* material = loadMaterial(mEngine, path.c_str());
        mMaterials[name] = material;

        return material;
    }

    MaterialInstance* createInstance(const std::string& materialName,
                                     const std::string& instanceName = "") {
        Material* material = load(materialName);
        MaterialInstance* instance = material->createInstance(
            instanceName.empty() ? nullptr : instanceName.c_str()
        );

        mInstances.push_back(instance);
        return instance;
    }

    void destroyInstance(MaterialInstance* instance) {
        mEngine.destroy(instance);
        mInstances.erase(
            std::remove(mInstances.begin(), mInstances.end(), instance),
            mInstances.end()
        );
    }

    void clear() {
        // 销毁实例
        for (auto* instance : mInstances) {
            mEngine.destroy(instance);
        }
        mInstances.clear();

        // 销毁材质
        for (auto& [name, material] : mMaterials) {
            mEngine.destroy(material);
        }
        mMaterials.clear();
    }

private:
    Engine& mEngine;
    std::unordered_map<std::string, Material*> mMaterials;
    std::vector<MaterialInstance*> mInstances;
};

// 使用
MaterialManager matMgr(engine);

MaterialInstance* pbr = matMgr.createInstance("pbr_textured", "RedMetal");
pbr->setParameter("baseColor", float3{0.8f, 0.1f, 0.1f});

MaterialInstance* glass = matMgr.createInstance("glass", "Window");
glass->setParameter("transmission", 0.9f);

// 自动清理
matMgr.clear();
```

---

## 8. 性能优化

### 实例复用

```cpp
// 差：为每个物体创建新实例
for (int i = 0; i < 100; i++) {
    MaterialInstance* inst = material->createInstance();
    inst->setParameter("baseColor", float3{1, 0, 0});
    // 使用 inst...
}

// 好：共享同一实例
MaterialInstance* sharedInstance = material->createInstance();
sharedInstance->setParameter("baseColor", float3{1, 0, 0});

for (int i = 0; i < 100; i++) {
    // 所有物体使用同一实例
    RenderableManager::Builder(1)
        .material(0, sharedInstance)
        // ...
        .build(engine, entities[i]);
}
```

### 批量更新

```cpp
// 差：频繁设置参数
void onFrame() {
    for (auto* instance : instances) {
        instance->setParameter("time", currentTime);
    }
}

// 好：减少更新频率
void onFrame() {
    static float lastUpdate = 0.0f;
    if (currentTime - lastUpdate > 0.016f) {  // 60 FPS
        for (auto* instance : instances) {
            instance->setParameter("time", currentTime);
        }
        lastUpdate = currentTime;
    }
}
```

### 避免不必要的状态覆盖

```cpp
// 差：总是覆盖状态
instance->setCullingMode(CullingMode::BACK);
instance->setDepthWrite(true);

// 好：只在需要时覆盖
if (needsDoubleSided) {
    instance->setCullingMode(CullingMode::NONE);
}
// 其他使用材质默认值
```

---

## 9. 调试技巧

### 检查材质信息

```cpp
// 查询材质属性
const char* name = material->getName();
Shading shading = material->getShading();
BlendingMode blending = material->getBlendingMode();

std::cout << "Material: " << name << "\n";
std::cout << "Shading: " << (int)shading << "\n";
std::cout << "Blending: " << (int)blending << "\n";

// 查询参数
size_t count = material->getParameterCount();
std::vector<Material::ParameterInfo> params(count);
material->getParameters(params.data(), count);

for (const auto& param : params) {
    std::cout << "Parameter: " << param.name;
    if (param.isSampler) {
        std::cout << " (sampler)\n";
    } else {
        std::cout << " (uniform)\n";
    }
}
```

### 可视化材质属性

```cpp
// 创建调试材质实例
MaterialInstance* debugInstance = material->createInstance("Debug");

// 可视化法线
debugInstance->setParameter("visualizeNormals", true);

// 可视化粗糙度
debugInstance->setParameter("visualizeRoughness", true);

// 禁用光照（查看 base color）
debugInstance->setParameter("disableLighting", true);
```

---

## 总结

运行时材质使用的关键步骤：

1. **加载**: Material::Builder::package() 创建 Material
2. **实例化**: material->createInstance() 创建实例
3. **配置**: setParameter() 设置参数和纹理
4. **覆盖**: 必要时覆盖渲染状态
5. **应用**: RenderableManager 关联到实体
6. **更新**: 动态更新参数值
7. **管理**: 使用管理器统一管理资源
8. **优化**: 复用实例，批量更新

遵循这些最佳实践可以高效、正确地使用 Filament 材质系统。
