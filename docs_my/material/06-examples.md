# 完整示例代码

本文档提供各种材质的完整示例，从基础到高级。

---

## 示例 1: 最简单的材质

### 默认灰色材质

```glsl
material {
    name : DefaultGray,
    shadingModel : unlit
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        material.baseColor.rgb = vec3(0.8);  // 80% 灰
    }
}
```

**编译**:
```bash
matc -p mobile -a all -o default_gray.filamat default_gray.mat
```

**使用**:
```cpp
Material* material = Material::Builder()
    .package(data, size)
    .build(engine);

MaterialInstance* instance = material->createInstance();
```

---

## 示例 2: 参数化纯色材质

### 材质定义

```glsl
material {
    name : SolidColor,
    shadingModel : lit,
    parameters : [
        { type : float3, name : baseColor },
        { type : float, name : roughness },
        { type : float, name : metallic },
        { type : float, name : reflectance }
    ]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        material.baseColor.rgb = materialParams.baseColor;
        material.roughness = materialParams.roughness;
        material.metallic = materialParams.metallic;
        material.reflectance = materialParams.reflectance;
    }
}
```

### 运行时使用

```cpp
// 创建材质
Material* material = loadMaterial("solid_color.filamat");

// 创建实例 - 红色塑料
MaterialInstance* redPlastic = material->createInstance("RedPlastic");
redPlastic->setParameter("baseColor", math::float3{0.8f, 0.1f, 0.1f});
redPlastic->setParameter("roughness", 0.5f);
redPlastic->setParameter("metallic", 0.0f);
redPlastic->setParameter("reflectance", 0.5f);

// 创建实例 - 金色金属
MaterialInstance* gold = material->createInstance("Gold");
gold->setParameter("baseColor", math::float3{1.0f, 0.86f, 0.57f});
gold->setParameter("roughness", 0.3f);
gold->setParameter("metallic", 1.0f);
gold->setParameter("reflectance", 0.5f);

// 应用到物体
RenderableManager::Builder(1)
    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb, ib)
    .material(0, redPlastic)
    .build(engine, entity);
```

---

## 示例 3: 纹理材质

### 基础纹理材质

```glsl
material {
    name : Textured,
    shadingModel : lit,
    requires : [uv0],
    parameters : [
        { type : sampler2d, name : albedoMap },
        { type : float, name : roughness }
    ]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        vec2 uv = getUV0();
        material.baseColor = texture(materialParams_albedoMap, uv);
        material.roughness = materialParams.roughness;
    }
}
```

### 完整 PBR 纹理材质

```glsl
material {
    name : PBRTextured,
    shadingModel : lit,
    requires : [uv0, tangents],
    parameters : [
        { type : sampler2d, name : albedoMap },
        { type : sampler2d, name : normalMap },
        { type : sampler2d, name : roughnessMap },
        { type : sampler2d, name : metallicMap },
        { type : sampler2d, name : aoMap },
        { type : sampler2d, name : emissiveMap },
        { type : float, name : emissiveStrength }
    ]
}

fragment {
    void material(inout MaterialInputs material) {
        vec2 uv = getUV0();

        // 法线贴图（在 prepareMaterial 之前）
        vec3 normalSample = texture(materialParams_normalMap, uv).xyz;
        material.normal = normalSample * 2.0 - 1.0;  // [0,1] → [-1,1]

        prepareMaterial(material);

        // 基础颜色
        material.baseColor = texture(materialParams_albedoMap, uv);

        // PBR 属性
        material.roughness = texture(materialParams_roughnessMap, uv).r;
        material.metallic = texture(materialParams_metallicMap, uv).r;
        material.ambientOcclusion = texture(materialParams_aoMap, uv).r;

        // 自发光
        vec3 emissive = texture(materialParams_emissiveMap, uv).rgb;
        material.emissive = vec4(emissive * materialParams.emissiveStrength, 0.0);
    }
}
```

### 运行时使用

```cpp
// 加载材质
Material* material = loadMaterial("pbr_textured.filamat");
MaterialInstance* instance = material->createInstance();

// 创建纹理
Texture* albedo = loadTexture("albedo.png");
Texture* normal = loadTexture("normal.png");
Texture* roughness = loadTexture("roughness.png");
Texture* metallic = loadTexture("metallic.png");
Texture* ao = loadTexture("ao.png");
Texture* emissive = loadTexture("emissive.png");

// 配置采样器
TextureSampler sampler(
    TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
    TextureSampler::MagFilter::LINEAR
);
sampler.setWrapModeS(TextureSampler::WrapMode::REPEAT);
sampler.setWrapModeT(TextureSampler::WrapMode::REPEAT);
sampler.setAnisotropy(16.0f);

// 绑定纹理
instance->setParameter("albedoMap", albedo, sampler);
instance->setParameter("normalMap", normal, sampler);
instance->setParameter("roughnessMap", roughness, sampler);
instance->setParameter("metallicMap", metallic, sampler);
instance->setParameter("aoMap", ao, sampler);
instance->setParameter("emissiveMap", emissive, sampler);
instance->setParameter("emissiveStrength", 2.0f);
```

---

## 示例 4: 透明材质

### 玻璃材质

```glsl
material {
    name : Glass,
    shadingModel : lit,
    blending : transparent,
    transparencyMode : twoPassesOneSide,
    depthWrite : false,
    refractionMode : screenSpace,
    refractionType : thin,
    parameters : [
        { type : float3, name : tint },
        { type : float, name : transmission },
        { type : float, name : ior }
    ]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        material.baseColor = vec4(materialParams.tint, materialParams.transmission);
        material.roughness = 0.1;  // 光滑玻璃
        material.metallic = 0.0;
        material.reflectance = 0.5;

        // 折射
        material.thickness = 0.5;
        material.ior = materialParams.ior;  // 玻璃约 1.5
    }
}
```

### 半透明塑料

```glsl
material {
    name : TranslucentPlastic,
    shadingModel : lit,
    blending : fade,
    depthWrite : false,
    parameters : [
        { type : float4, name : color }  // RGBA
    ]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        material.baseColor = materialParams.color;
        material.roughness = 0.5;
        material.metallic = 0.0;
    }
}
```

---

## 示例 5: 自定义顶点着色器

### 顶点动画

```glsl
material {
    name : VertexWave,
    shadingModel : lit,
    parameters : [
        { type : float, name : time },
        { type : float, name : amplitude },
        { type : float, name : frequency }
    ]
}

vertex {
    void materialVertex(inout MaterialVertexInputs material) {
        vec3 pos = material.worldPosition;

        // 正弦波动画
        float wave = sin(pos.x * materialParams.frequency + materialParams.time);
        pos.y += wave * materialParams.amplitude;

        material.worldPosition = pos;
    }
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        material.baseColor.rgb = vec3(0.2, 0.5, 0.8);
    }
}
```

### 运行时使用

```cpp
MaterialInstance* instance = material->createInstance();

// 每帧更新时间
float currentTime = 0.0f;
void onFrame(float deltaTime) {
    currentTime += deltaTime;
    instance->setParameter("time", currentTime);
    instance->setParameter("amplitude", 0.5f);
    instance->setParameter("frequency", 2.0f);
}
```

---

## 示例 6: UI 材质

### UI 元素材质

```glsl
material {
    name : UIElement,
    shadingModel : unlit,
    blending : transparent,
    depthWrite : false,
    depthCulling : false,
    requires : [uv0, color],
    parameters : [
        { type : sampler2d, name : texture },
        { type : float4, name : tint }
    ]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        vec2 uv = getUV0();
        vec4 texColor = texture(materialParams_texture, uv);
        vec4 vertexColor = getColor();

        material.baseColor = texColor * materialParams.tint * vertexColor;
    }
}
```

### 运行时使用

```cpp
// UI 使用正交相机
auto& rcm = engine->getRenderableManager();

// 创建 UI 材质实例
MaterialInstance* uiInstance = uiMaterial->createInstance();
uiInstance->setParameter("texture", uiTexture, sampler);
uiInstance->setParameter("tint", math::float4{1, 1, 1, 0.8f});  // 80% 不透明

// 禁用背面剔除（UI 可能需要双面）
uiInstance->setCullingMode(CullingMode::NONE);
```

---

## 示例 7: 后处理材质

### 模糊后处理

```glsl
material {
    name : BlurPostProcess,
    materialDomain : postProcess,
    parameters : [
        { type : sampler2d, name : colorBuffer },
        { type : float2, name : blurDirection },  // (1, 0) 水平, (0, 1) 垂直
        { type : float, name : blurSize }
    ]
}

fragment {
    void postProcess(inout PostProcessInputs postProcess) {
        vec2 uv = variable_vertex.xy;  // 屏幕 UV

        vec4 color = vec4(0.0);
        float totalWeight = 0.0;

        // 5 点高斯模糊
        const int samples = 5;
        const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

        for (int i = 0; i < samples; i++) {
            vec2 offset = materialParams.blurDirection * float(i) * materialParams.blurSize;

            color += texture(materialParams_colorBuffer, uv + offset) * weights[i];
            color += texture(materialParams_colorBuffer, uv - offset) * weights[i];

            totalWeight += weights[i] * 2.0;
        }

        postProcess.color = color / totalWeight;
    }
}
```

---

## 示例 8: 特殊效果

### 全息图效果

```glsl
material {
    name : Hologram,
    shadingModel : unlit,
    blending : add,
    depthWrite : false,
    doubleSided : true,
    parameters : [
        { type : float3, name : color },
        { type : float, name : time },
        { type : float, name : scanSpeed },
        { type : float, name : glitchIntensity }
    ]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        vec3 worldPos = getWorldPosition();
        vec3 viewDir = getWorldViewVector();
        vec3 normal = getWorldNormal();

        // 菲涅尔边缘光
        float fresnel = pow(1.0 - abs(dot(viewDir, normal)), 3.0);

        // 扫描线
        float scan = fract(worldPos.y * 10.0 - materialParams.time * materialParams.scanSpeed);
        scan = smoothstep(0.9, 1.0, scan);

        // 随机闪烁
        float glitch = fract(sin(worldPos.x * 12.9898 + materialParams.time) * 43758.5453);
        glitch = step(0.95, glitch) * materialParams.glitchIntensity;

        float intensity = fresnel + scan + glitch;
        material.baseColor = vec4(materialParams.color * intensity, 1.0);
    }
}
```

### 溶解效果

```glsl
material {
    name : Dissolve,
    shadingModel : lit,
    blending : masked,
    maskThreshold : 0.5,
    requires : [uv0],
    parameters : [
        { type : sampler2d, name : albedoMap },
        { type : sampler2d, name : noiseMap },
        { type : float, name : dissolveAmount },
        { type : float3, name : edgeColor },
        { type : float, name : edgeWidth }
    ]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        vec2 uv = getUV0();

        // 采样
        vec4 albedo = texture(materialParams_albedoMap, uv);
        float noise = texture(materialParams_noiseMap, uv).r;

        // 溶解阈值
        float threshold = materialParams.dissolveAmount;

        // 边缘发光
        float edge = smoothstep(threshold, threshold + materialParams.edgeWidth, noise);
        vec3 edgeGlow = materialParams.edgeColor * (1.0 - edge);

        // 基础颜色
        material.baseColor = albedo;
        material.emissive = vec4(edgeGlow, 0.0);

        // Alpha 裁剪
        material.baseColor.a = step(threshold, noise);
    }
}
```

---

## 示例 9: 优化的打包纹理

### 打包多个贴图

```glsl
material {
    name : PackedMaterial,
    shadingModel : lit,
    requires : [uv0, tangents],
    parameters : [
        // RGB: Albedo, A: AO
        { type : sampler2d, name : albedoAO },
        // RG: Normal XY, B: Roughness, A: Metallic
        { type : sampler2d, name : normalRoughMetal }
    ]
}

fragment {
    void material(inout MaterialInputs material) {
        vec2 uv = getUV0();

        // 采样打包纹理
        vec4 albedoAO = texture(materialParams_albedoAO, uv);
        vec4 normalRoughMetal = texture(materialParams_normalRoughMetal, uv);

        // 解包法线 (BC5 格式: RG 存储 XY)
        vec2 normalXY = normalRoughMetal.rg * 2.0 - 1.0;
        float normalZ = sqrt(max(0.0, 1.0 - dot(normalXY, normalXY)));
        material.normal = vec3(normalXY, normalZ);

        prepareMaterial(material);

        // 设置属性
        material.baseColor.rgb = albedoAO.rgb;
        material.roughness = normalRoughMetal.b;
        material.metallic = normalRoughMetal.a;
        material.ambientOcclusion = albedoAO.a;
    }
}
```

---

## 完整工作流示例

### 从创建到渲染

```cpp
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/Material.h>
#include <filament/Texture.h>
#include <filamat/MaterialBuilder.h>

using namespace filament;

// 1. 初始化 Filament
Engine* engine = Engine::create();
Scene* scene = engine->createScene();
Camera* camera = /* 创建相机 */;
View* view = /* 创建视图 */;

// 2. 编译材质（离线或运行时）
filamat::MaterialBuilder::init();

filamat::MaterialBuilder builder;
builder
    .name("MyMaterial")
    .shading(filamat::MaterialBuilder::Shading::LIT)
    .parameter("albedoMap", filamat::MaterialBuilder::SamplerType::SAMPLER_2D)
    .parameter("roughness", filamat::MaterialBuilder::UniformType::FLOAT)
    .require(filamat::MaterialBuilder::VertexAttribute::UV0)
    .material(R"(
        void material(inout MaterialInputs material) {
            prepareMaterial(material);
            material.baseColor = texture(materialParams_albedoMap, getUV0());
            material.roughness = materialParams.roughness;
        }
    )")
    .targetApi(filamat::MaterialBuilder::TargetApi::ALL)
    .platform(filamat::MaterialBuilder::Platform::ALL);

filamat::Package package = builder.build(*engine->getJobSystem());

filamat::MaterialBuilder::shutdown();

// 3. 创建 Material
Material* material = Material::Builder()
    .package(package.getData(), package.getSize())
    .build(*engine);

// 4. 加载纹理
Texture* albedoTexture = /* 加载纹理 */;

// 5. 创建 MaterialInstance
MaterialInstance* instance = material->createInstance();
TextureSampler sampler(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR);
instance->setParameter("albedoMap", albedoTexture, sampler);
instance->setParameter("roughness", 0.5f);

// 6. 创建几何体
VertexBuffer* vb = /* 创建顶点缓冲 */;
IndexBuffer* ib = /* 创建索引缓冲 */;

// 7. 创建 Renderable
Entity entity = EntityManager::get().create();
RenderableManager::Builder(1)
    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb, ib)
    .material(0, instance)
    .boundingBox({{0, 0, 0}, {1, 1, 1}})
    .build(*engine, entity);

// 8. 添加到场景
scene->addEntity(entity);

// 9. 渲染循环
while (running) {
    // 更新参数
    instance->setParameter("roughness", animatedRoughness);

    // 渲染
    if (renderer->beginFrame(swapChain)) {
        renderer->render(view);
        renderer->endFrame();
    }
}

// 10. 清理
engine->destroy(instance);
engine->destroy(material);
engine->destroy(entity);
engine->destroy(vb);
engine->destroy(ib);
Engine::destroy(&engine);
```

---

## 总结

这些示例涵盖了：

1. **基础材质**: 从最简单到参数化
2. **纹理材质**: 单纹理到完整 PBR
3. **透明材质**: 玻璃、半透明
4. **动画材质**: 顶点动画
5. **UI 材质**: 2D 界面元素
6. **后处理**: 全屏效果
7. **特殊效果**: 全息图、溶解
8. **优化技巧**: 打包纹理
9. **完整工作流**: 从创建到渲染

使用这些示例作为起点，可以创建各种复杂的材质效果。
