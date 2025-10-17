# 核心类职责说明

本文档详细说明 Filament 材质系统中各个核心类的职责和作用。

---

## 1. Material

**位置**: `/filament/include/filament/Material.h`

### 职责

Material 是编译后的 shader 程序容器，负责：

1. **封装 Shader 程序**
   - 包含顶点着色器和片段着色器
   - 支持多个渲染后端（OpenGL/Vulkan/Metal）
   - 包含所有编译变体（Variants）

2. **定义材质属性**
   - Shading Model (lit/unlit/subsurface/cloth)
   - Blending Mode (opaque/transparent/add/multiply)
   - Culling Mode (front/back/none)
   - Vertex Domain (object/world/view/device)
   - 其他渲染状态

3. **参数定义**
   - Uniform 参数列表
   - Sampler (纹理) 参数列表
   - Subpass 参数列表

4. **创建材质实例**
   - 通过 createInstance() 创建 MaterialInstance
   - 管理默认实例 (default instance)

5. **异步编译**
   - 支持 shader 变体异步编译
   - 提供编译进度回调

### 核心方法

```cpp
class Material {
    // 创建材质（从二进制包）
    class Builder {
        Builder& package(const void* payload, size_t size);
        Builder& constant(const char* name, T value);  // 设置编译时常量
        Material* build(Engine& engine);
    };

    // 异步编译变体
    void compile(
        CompilerPriorityQueue priority,
        UserVariantFilterMask variants,
        CallbackHandler* handler,
        Invocable<void(Material*)>&& callback
    );

    // 创建材质实例
    MaterialInstance* createInstance(const char* name = nullptr) const;

    // 查询材质属性
    const char* getName() const;
    Shading getShading() const;
    BlendingMode getBlendingMode() const;
    MaterialDomain getMaterialDomain() const;

    // 查询参数信息
    size_t getParameterCount() const;
    size_t getParameters(ParameterInfo* parameters, size_t count) const;
    bool hasParameter(const char* name) const;

    // 获取默认实例
    MaterialInstance* getDefaultInstance();
};
```

### 生命周期

```
MaterialBuilder::build()
    → Material 创建
    → Material::createInstance() 创建实例
    → 使用实例渲染
    → Engine::destroy(Material*) 销毁
```

### 参数信息

```cpp
struct ParameterInfo {
    const char* name;           // 参数名称
    bool isSampler;            // 是否是纹理采样器
    bool isSubpass;            // 是否是 subpass
    union {
        ParameterType type;     // Uniform 类型 (FLOAT, INT, etc)
        SamplerType samplerType; // 采样器类型 (SAMPLER_2D, etc)
        SubpassType subpassType; // Subpass 类型
    };
    uint32_t count;            // 数组大小
    Precision precision;        // 精度 (LOW, MEDIUM, HIGH)
};
```

---

## 2. MaterialInstance

**位置**: `/filament/include/filament/MaterialInstance.h`

### 职责

MaterialInstance 代表材质的一个参数化实例，负责：

1. **存储材质参数**
   - Uniform 参数值（颜色、数值等）
   - Texture 绑定和采样器设置
   - Subpass 绑定

2. **覆盖渲染状态**
   - 剔除模式 (Culling Mode)
   - 透明模式 (Transparency Mode)
   - 深度写入/测试
   - 颜色写入
   - 模板测试
   - 裁剪矩形 (Scissor)
   - 多边形偏移 (Polygon Offset)

3. **实例独立配置**
   - 每个实例可以有不同的参数值
   - 可以覆盖父材质的部分渲染状态
   - 支持实例复制 (duplicate)

### 核心方法

```cpp
class MaterialInstance {
    // 复制实例
    static MaterialInstance* duplicate(
        MaterialInstance const* other,
        const char* name = nullptr
    );

    // 获取关联的 Material
    Material const* getMaterial() const;
    const char* getName() const;

    // 设置 Uniform 参数
    template<typename T>
    void setParameter(const char* name, T const& value);

    // 设置 Uniform 数组
    template<typename T>
    void setParameter(const char* name, const T* values, size_t count);

    // 设置纹理参数
    void setParameter(const char* name,
                     Texture const* texture,
                     TextureSampler const& sampler);

    // 设置颜色参数
    void setParameter(const char* name, RgbType type, float3 color);
    void setParameter(const char* name, RgbaType type, float4 color);

    // 获取参数值
    template<typename T>
    T getParameter(const char* name) const;

    // 渲染状态覆盖
    void setCullingMode(CullingMode culling);
    void setDoubleSided(bool doubleSided);
    void setTransparencyMode(TransparencyMode mode);
    void setColorWrite(bool enable);
    void setDepthWrite(bool enable);
    void setDepthCulling(bool enable);
    void setDepthFunc(DepthFunc depthFunc);

    // 裁剪和偏移
    void setScissor(uint32_t left, uint32_t bottom,
                   uint32_t width, uint32_t height);
    void setPolygonOffset(float scale, float constant);

    // Alpha 遮罩阈值
    void setMaskThreshold(float threshold);
    float getMaskThreshold() const;
};
```

### 支持的参数类型

```cpp
// 标量类型
float, int32_t, uint32_t, bool

// 向量类型
float2, float3, float4
int2, int3, int4
uint2, uint3, uint4
bool2, bool3, bool4

// 矩阵类型
mat3f, mat4f

// 纹理类型
Texture* + TextureSampler
```

### 使用示例

```cpp
// 创建实例
MaterialInstance* instance = material->createInstance("myInstance");

// 设置参数
instance->setParameter("baseColor", float3(1.0f, 0.0f, 0.0f));
instance->setParameter("roughness", 0.5f);
instance->setParameter("metallic", 0.0f);

// 设置纹理
TextureSampler sampler(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR);
instance->setParameter("albedoMap", albedoTexture, sampler);

// 覆盖渲染状态
instance->setDoubleSided(true);
instance->setCullingMode(CullingMode::NONE);
```

---

## 3. MaterialBuilder (filamat)

**位置**: `/libs/filamat/include/filamat/MaterialBuilder.h`

### 职责

MaterialBuilder 是材质编译器的核心类，负责：

1. **解析材质定义**
   - 读取 .mat 文件内容
   - 解析 material 块配置
   - 解析 fragment/vertex 代码

2. **生成 GLSL 代码**
   - 为不同 shader model 生成代码
   - 生成所有需要的变体
   - 注入内置函数和辅助代码

3. **编译 Shader**
   - 使用 glslang 编译 GLSL → SPIRV
   - 使用 spirv-tools 优化 SPIRV
   - 使用 spirv-cross 转译到目标语言

4. **打包材质**
   - 生成 Package (二进制材质包)
   - 包含所有后端的 shader 代码
   - 包含材质元数据

### 核心方法

```cpp
class MaterialBuilder {
    // 初始化/清理（全局调用）
    static void init();
    static void shutdown();

    // 材质基本信息
    MaterialBuilder& name(const char* name);
    MaterialBuilder& fileName(const char* name);

    // Shading 配置
    MaterialBuilder& shading(Shading shading);
    MaterialBuilder& interpolation(Interpolation interpolation);
    MaterialBuilder& materialDomain(MaterialDomain domain);

    // 参数定义
    MaterialBuilder& parameter(const char* name, UniformType type,
                              ParameterPrecision precision = DEFAULT);
    MaterialBuilder& parameter(const char* name, size_t size, UniformType type,
                              ParameterPrecision precision = DEFAULT);
    MaterialBuilder& parameter(const char* name, SamplerType samplerType,
                              SamplerFormat format = FLOAT,
                              ParameterPrecision precision = DEFAULT);

    // 常量参数（编译时常量）
    template<typename T>
    MaterialBuilder& constant(const char* name, ConstantType type, T defaultValue);

    // 自定义变量
    MaterialBuilder& variable(Variable v, const char* name);

    // 顶点属性要求
    MaterialBuilder& require(VertexAttribute attribute);

    // Shader 代码
    MaterialBuilder& material(const char* code, size_t line = 0);
    MaterialBuilder& materialVertex(const char* code, size_t line = 0);

    // 渲染状态
    MaterialBuilder& blending(BlendingMode blending);
    MaterialBuilder& culling(CullingMode culling);
    MaterialBuilder& colorWrite(bool enable);
    MaterialBuilder& depthWrite(bool enable);
    MaterialBuilder& depthCulling(bool enable);
    MaterialBuilder& doubleSided(bool doubleSided);
    MaterialBuilder& maskThreshold(float threshold);

    // 编译配置
    MaterialBuilder& platform(Platform platform);        // DESKTOP/MOBILE/ALL
    MaterialBuilder& targetApi(TargetApi targetApi);    // OPENGL/VULKAN/METAL
    MaterialBuilder& optimization(Optimization opt);     // NONE/SIZE/PERFORMANCE
    MaterialBuilder& featureLevel(FeatureLevel level);  // FL0/FL1/FL2/FL3

    // 质量和优化
    MaterialBuilder& quality(ShaderQuality quality);
    MaterialBuilder& variantFilter(UserVariantFilterMask filter);

    // 调试选项
    MaterialBuilder& printShaders(bool print);
    MaterialBuilder& saveRawVariants(bool save);
    MaterialBuilder& generateDebugInfo(bool debug);

    // 构建材质包
    Package build(utils::JobSystem& jobSystem);
};
```

### 平台和 API 选择

```cpp
enum class Platform {
    DESKTOP,    // PC/Mac/Linux
    MOBILE,     // Android/iOS
    ALL         // 两者都支持
};

enum class TargetApi {
    OPENGL  = 0x01,
    VULKAN  = 0x02,
    METAL   = 0x04,
    ALL     = OPENGL | VULKAN | METAL
};
```

### 优化级别

```cpp
enum class Optimization {
    NONE,           // 无优化（调试用）
    PREPROCESSOR,   // 仅预处理器优化
    SIZE,           // 优化代码大小
    PERFORMANCE     // 优化性能（默认）
};
```

### 使用示例

```cpp
#include <filamat/MaterialBuilder.h>

MaterialBuilder::init();

MaterialBuilder builder;
builder
    .name("MyMaterial")
    .shading(MaterialBuilder::Shading::LIT)
    .parameter("baseColor", UniformType::FLOAT3)
    .parameter("roughness", UniformType::FLOAT)
    .parameter("albedoMap", SamplerType::SAMPLER_2D)
    .require(VertexAttribute::UV0)
    .material(R"(
        void material(inout MaterialInputs material) {
            prepareMaterial(material);
            material.baseColor = texture(materialParams_albedoMap, getUV0());
            material.roughness = materialParams.roughness;
        }
    )")
    .targetApi(MaterialBuilder::TargetApi::ALL)
    .platform(MaterialBuilder::Platform::ALL)
    .optimization(MaterialBuilder::Optimization::PERFORMANCE);

Package package = builder.build(jobSystem);

MaterialBuilder::shutdown();
```

---

## 4. Package

**位置**: `/libs/filamat/include/filamat/Package.h`

### 职责

Package 是编译后的材质二进制包，负责：

1. **存储编译结果**
   - 所有后端的 shader 代码
   - 材质元数据
   - 参数定义
   - 变体信息

2. **序列化/反序列化**
   - 可以保存为 .filamat 文件
   - 可以从内存加载

3. **传递给 Material::Builder**
   - 作为 Material::Builder::package() 的输入

### 核心方法

```cpp
class Package {
    // 检查是否有效
    bool isValid() const;

    // 获取二进制数据
    const uint8_t* getData() const;
    size_t getSize() const;
};
```

### 使用示例

```cpp
// 编译材质
Package package = materialBuilder.build(jobSystem);

if (package.isValid()) {
    // 保存到文件
    std::ofstream out("material.filamat", std::ios::binary);
    out.write((const char*)package.getData(), package.getSize());
    out.close();

    // 或者直接创建 Material
    Material* material = Material::Builder()
        .package(package.getData(), package.getSize())
        .build(engine);
}
```

---

## 5. TextureSampler

**位置**: `/filament/include/filament/TextureSampler.h`

### 职责

TextureSampler 定义纹理采样参数，负责：

1. **过滤模式**
   - MinFilter: 缩小过滤 (NEAREST/LINEAR/LINEAR_MIPMAP_LINEAR)
   - MagFilter: 放大过滤 (NEAREST/LINEAR)

2. **包裹模式**
   - WrapMode: 纹理坐标超出 [0,1] 时的行为
   - CLAMP_TO_EDGE, REPEAT, MIRRORED_REPEAT

3. **各向异性过滤**
   - Anisotropy: 各向异性过滤级别 (1, 2, 4, 8, 16)

4. **比较模式**
   - CompareMode: 深度纹理比较 (NONE, COMPARE_TO_TEXTURE)
   - CompareFunc: 比较函数 (LESS, GREATER, etc)

### 核心方法

```cpp
class TextureSampler {
    enum class MinFilter {
        NEAREST,
        LINEAR,
        NEAREST_MIPMAP_NEAREST,
        LINEAR_MIPMAP_NEAREST,
        NEAREST_MIPMAP_LINEAR,
        LINEAR_MIPMAP_LINEAR
    };

    enum class MagFilter {
        NEAREST,
        LINEAR
    };

    enum class WrapMode {
        CLAMP_TO_EDGE,
        REPEAT,
        MIRRORED_REPEAT
    };

    // 构造函数
    TextureSampler(MinFilter min = LINEAR_MIPMAP_LINEAR,
                   MagFilter mag = LINEAR);

    // 设置包裹模式
    TextureSampler& setWrapModeS(WrapMode mode);
    TextureSampler& setWrapModeT(WrapMode mode);
    TextureSampler& setWrapModeR(WrapMode mode);

    // 设置各向异性
    TextureSampler& setAnisotropy(float anisotropy);

    // 设置比较模式（深度纹理）
    TextureSampler& setCompareMode(CompareMode mode);
    TextureSampler& setCompareFunc(CompareFunc func);
};
```

### 使用示例

```cpp
// 默认采样器（线性过滤 + mipmap）
TextureSampler sampler;

// 自定义采样器
TextureSampler customSampler(
    TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
    TextureSampler::MagFilter::LINEAR
);
customSampler.setWrapModeS(TextureSampler::WrapMode::REPEAT);
customSampler.setWrapModeT(TextureSampler::WrapMode::REPEAT);
customSampler.setAnisotropy(16.0f);

// 应用到材质实例
instance->setParameter("albedoMap", albedoTexture, customSampler);

// 深度纹理采样器（用于阴影）
TextureSampler shadowSampler;
shadowSampler.setCompareMode(TextureSampler::CompareMode::COMPARE_TO_TEXTURE);
shadowSampler.setCompareFunc(SamplerCompareFunc::LESS_OR_EQUAL);
```

---

## 类职责总结

| 类 | 主要职责 | 生命周期 | 可变性 |
|----|---------|---------|-------|
| **Material** | 封装编译后的 shader | Engine 管理 | 不可变 |
| **MaterialInstance** | 参数化的材质实例 | 用户管理/Engine 销毁 | 可变 |
| **MaterialBuilder** | 编译材质 | 临时对象 | 建造者模式 |
| **Package** | 材质二进制包 | 临时对象 | 不可变 |
| **TextureSampler** | 纹理采样配置 | 值类型 | 不可变（值） |

---

## 关键设计模式

1. **Builder 模式**
   - Material::Builder
   - MaterialBuilder
   - 分步配置复杂对象

2. **Flyweight 模式**
   - Material 在多个 MaterialInstance 间共享
   - Shader 代码只编译一次

3. **Factory 模式**
   - Material::createInstance() 创建实例
   - MaterialBuilder::build() 创建 Package

4. **Immutable Object**
   - Material 一旦创建不可修改
   - MaterialInstance 可以修改参数，但不能改变关联的 Material

---

## 对象关系图

```
MaterialBuilder (编译时)
    ↓ build()
Package (二进制包)
    ↓ Material::Builder::package()
Material (shader 程序)
    ↓ createInstance()
MaterialInstance (实例 1)
MaterialInstance (实例 2)
MaterialInstance (实例 N)
    ↓ RenderableManager::Builder::material()
Renderable (渲染物体)
```

---

## 常见使用模式

### 模式 1: 从文件加载材质

```cpp
// 1. 读取 .filamat 文件
std::ifstream in("material.filamat", std::ios::binary);
std::vector<uint8_t> data(
    (std::istreambuf_iterator<char>(in)),
    std::istreambuf_iterator<char>()
);

// 2. 创建 Material
Material* material = Material::Builder()
    .package(data.data(), data.size())
    .build(engine);

// 3. 创建实例并设置参数
MaterialInstance* instance = material->createInstance();
instance->setParameter("color", float3(1, 0, 0));

// 4. 应用到 Renderable
RenderableManager::Builder(1)
    .material(0, instance)
    .build(engine, entity);
```

### 模式 2: 运行时编译材质

```cpp
MaterialBuilder::init();

MaterialBuilder builder;
builder.name("RuntimeMaterial")
       .material("void material(inout MaterialInputs m) { "
                "prepareMaterial(m); m.baseColor = vec4(1); }")
       .shading(Shading::UNLIT);

Package package = builder.build(jobSystem);

Material* material = Material::Builder()
    .package(package.getData(), package.getSize())
    .build(engine);

MaterialBuilder::shutdown();
```
