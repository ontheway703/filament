# PBR Model

## 概述

PBR (Physically Based Rendering) Model 示例展示如何使用基于物理的材质渲染真实感3D模型。本示例涵盖:

- **PBR 材质**: 使用金属度-粗糙度工作流
- **多纹理贴图**: Albedo、Normal、Metallic、Roughness、AO
- **IBL 环境光**: 基于图像的照明 (Image-Based Lighting)
- **直接光照**: 方向光、点光源、聚光灯
- **模型加载**: 从 OBJ/glTF 文件加载复杂模型
- **切线空间**: 正确的法线贴图计算

## PBR 理论基础

### 材质参数

```
PBR 材质包含以下核心参数:

1. BaseColor (Albedo)
   - 表面固有颜色
   - RGB 值，sRGB 色彩空间
   - 金属: 反射颜色 (如金色 RGB(1.0, 0.766, 0.336))
   - 非金属: 漫反射颜色

2. Metallic (金属度)
   - 0.0 = 非金属 (绝缘体)
   - 1.0 = 金属 (导体)
   - 中间值用于过渡/生锈金属

3. Roughness (粗糙度)
   - 0.0 = 完全光滑 (镜面反射)
   - 1.0 = 完全粗糙 (漫反射)

4. Normal Map (法线贴图)
   - 切线空间法线 (RGB 映射到 XYZ)
   - 增强表面细节，无需增加几何复杂度

5. Ambient Occlusion (环境光遮蔽)
   - 0.0 = 完全遮蔽
   - 1.0 = 无遮蔽
   - 模拟间接光照衰减
```

### 光照模型

```
Cook-Torrance BRDF:

f(l, v) = (D(h) * G(l, v, h) * F(l, h)) / (4 * (n·l) * (n·v))

其中:
- D: 法线分布函数 (GGX/Trowbridge-Reitz)
- G: 几何遮蔽函数 (Smith GGX)
- F: 菲涅尔函数 (Schlick 近似)
- l: 光源方向
- v: 视线方向
- h: 半角向量
- n: 表面法线
```

## 完整实现 (C++)

### 主程序

```cpp
// pbr_model.cpp
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <filament/LightManager.h>
#include <filament/IndirectLight.h>
#include <filament/Skybox.h>
#include <filament/Texture.h>
#include <filament/Material.h>

#include <utils/EntityManager.h>
#include <math/vec3.h>
#include <math/mat4.h>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <unordered_map>

using namespace filament;
using namespace filament::math;
using namespace utils;

struct PBRTextures {
    Texture* albedo = nullptr;
    Texture* normal = nullptr;
    Texture* metallic = nullptr;
    Texture* roughness = nullptr;
    Texture* ao = nullptr;  // Ambient occlusion
};

class PBRModel {
public:
    PBRModel() = default;
    ~PBRModel() { cleanup(); }

    bool initialize(void* nativeWindow, int width, int height) {
        m_width = width;
        m_height = height;

        // Create engine
        m_engine = Engine::create(Engine::Backend::DEFAULT);
        if (!m_engine) return false;

        // Create swap chain
        m_swapChain = m_engine->createSwapChain(nativeWindow);
        if (!m_swapChain) return false;

        // Create renderer
        m_renderer = m_engine->createRenderer();
        m_renderer->setClearOptions({
            .clearColor = {0.0f, 0.0f, 0.0f, 1.0f},
            .clear = true
        });

        // Create scene
        m_scene = m_engine->createScene();

        // Create view
        m_view = m_engine->createView();
        m_view->setScene(m_scene);
        m_view->setViewport({0, 0, (uint32_t)width, (uint32_t)height});
        m_view->setDepthPrepass(View::DepthPrepass::ENABLED);

        // Anti-aliasing
        m_view->setAntiAliasing(View::AntiAliasing::FXAA);
        m_view->setSampleCount(4);  // MSAA

        // Tone mapping
        m_view->setPostProcessingEnabled(true);
        m_view->setBloomOptions({
            .enabled = true,
            .strength = 0.3f,
            .threshold = 1.0f
        });

        // Create camera
        m_camera = m_engine->createCamera(EntityManager::get().create());
        const float aspect = (float)width / (float)height;
        m_camera->setProjection(Camera::Projection::PERSPECTIVE,
            45.0, aspect, 0.1, 100.0);
        m_camera->lookAt({0, 2, 5}, {0, 0, 0}, {0, 1, 0});
        m_view->setCamera(m_camera);

        // Load IBL environment
        if (!loadEnvironment("environment.ktx")) {
            std::cerr << "Failed to load environment\n";
            return false;
        }

        // Setup lights
        setupLights();

        // Load PBR textures
        if (!loadPBRTextures("model/")) {
            std::cerr << "Failed to load PBR textures\n";
            return false;
        }

        // Load material
        if (!loadMaterial()) {
            std::cerr << "Failed to load material\n";
            return false;
        }

        // Load model
        if (!loadModel("model/helmet.gltf")) {
            std::cerr << "Failed to load model\n";
            return false;
        }

        return true;
    }

    void render() {
        if (!m_renderer || !m_swapChain || !m_view) return;

        // Update rotation
        updateTransform();

        if (m_renderer->beginFrame(m_swapChain)) {
            m_renderer->render(m_view);
            m_renderer->endFrame();
        }
    }

    void cleanup() {
        if (m_engine) {
            // Destroy model entities
            for (auto entity : m_modelEntities) {
                m_scene->remove(entity);
                m_engine->destroy(entity);
            }

            // Destroy lights
            for (auto light : m_lights) {
                m_scene->remove(light);
                m_engine->destroy(light);
            }

            // Destroy textures
            if (m_textures.albedo) m_engine->destroy(m_textures.albedo);
            if (m_textures.normal) m_engine->destroy(m_textures.normal);
            if (m_textures.metallic) m_engine->destroy(m_textures.metallic);
            if (m_textures.roughness) m_engine->destroy(m_textures.roughness);
            if (m_textures.ao) m_engine->destroy(m_textures.ao);

            if (m_material) m_engine->destroy(m_material);
            if (m_indirectLight) m_engine->destroy(m_indirectLight);
            if (m_skybox) m_engine->destroy(m_skybox);
            if (m_iblTexture) m_engine->destroy(m_iblTexture);

            if (m_camera) {
                auto entity = m_camera->getEntity();
                m_engine->destroyCameraComponent(entity);
                EntityManager::get().destroy(entity);
            }

            if (m_view) m_engine->destroy(m_view);
            if (m_scene) m_engine->destroy(m_scene);
            if (m_renderer) m_engine->destroy(m_renderer);
            if (m_swapChain) m_engine->destroy(m_swapChain);

            Engine::destroy(&m_engine);
        }
    }

private:
    bool loadPBRTextures(const std::string& basePath) {
        // Load albedo (base color)
        m_textures.albedo = loadTexture(basePath + "albedo.png", false);
        if (!m_textures.albedo) return false;

        // Load normal map (linear space, not sRGB)
        m_textures.normal = loadTexture(basePath + "normal.png", true);
        if (!m_textures.normal) return false;

        // Load metallic map (linear)
        m_textures.metallic = loadTexture(basePath + "metallic.png", true);
        if (!m_textures.metallic) return false;

        // Load roughness map (linear)
        m_textures.roughness = loadTexture(basePath + "roughness.png", true);
        if (!m_textures.roughness) return false;

        // Load ambient occlusion (linear)
        m_textures.ao = loadTexture(basePath + "ao.png", true);
        if (!m_textures.ao) return false;

        return true;
    }

    Texture* loadTexture(const std::string& path, bool linear) {
        int width, height, channels;
        unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
        if (!data) {
            std::cerr << "Failed to load texture: " << path << "\n";
            return nullptr;
        }

        Texture::InternalFormat format = linear ?
            Texture::InternalFormat::RGBA8 :
            Texture::InternalFormat::SRGB8_A8;

        Texture* texture = Texture::Builder()
            .width(static_cast<uint32_t>(width))
            .height(static_cast<uint32_t>(height))
            .levels(static_cast<uint8_t>(std::floor(std::log2(std::max(width, height))) + 1))
            .format(format)
            .sampler(Texture::Sampler::SAMPLER_2D)
            .build(*m_engine);

        if (!texture) {
            stbi_image_free(data);
            return nullptr;
        }

        // Upload base level
        Texture::PixelBufferDescriptor buffer(
            data,
            width * height * 4,
            Texture::Format::RGBA,
            Texture::Type::UBYTE,
            [](void* buffer, size_t size, void* user) {
                stbi_image_free(buffer);
            }
        );

        texture->setImage(*m_engine, 0, std::move(buffer));

        // Generate mipmaps
        texture->generateMipmaps(*m_engine);

        std::cout << "Loaded texture: " << path << " (" << width << "x" << height << ")\n";

        return texture;
    }

    bool loadEnvironment(const std::string& path) {
        // Load KTX environment map (generated with cmgen)
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            std::cerr << "Failed to open environment: " << path << "\n";
            return false;
        }

        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> data(size);
        file.read(reinterpret_cast<char*>(data.data()), size);

        // Create cubemap texture for IBL
        m_iblTexture = Texture::Builder()
            .width(256)
            .height(256)
            .levels(8)  // Mip levels for different roughness values
            .format(Texture::InternalFormat::RGBA16F)
            .sampler(Texture::Sampler::SAMPLER_CUBEMAP)
            .build(*m_engine);

        if (!m_iblTexture) return false;

        // TODO: Parse KTX and upload cubemap faces

        // Create indirect light from IBL texture
        m_indirectLight = IndirectLight::Builder()
            .reflections(m_iblTexture)
            .intensity(30000.0f)  // In lux
            .rotation(mat3f::rotation(0.5f, float3{0, 1, 0}))
            .build(*m_engine);

        m_scene->setIndirectLight(m_indirectLight);

        // Create skybox
        m_skybox = Skybox::Builder()
            .environment(m_iblTexture)
            .showSun(false)
            .build(*m_engine);

        m_scene->setSkybox(m_skybox);

        return true;
    }

    void setupLights() {
        // Main directional light (sun)
        {
            Entity sunlight = EntityManager::get().create();
            LightManager::Builder(LightManager::Type::SUN)
                .color(Color::toLinear<ACCURATE>({1.0f, 0.95f, 0.9f}))
                .intensity(100000.0f)  // In lux
                .direction({0.6f, -1.0f, -0.8f})
                .castShadows(true)
                .sunAngularRadius(1.9f)
                .sunHaloSize(10.0f)
                .sunHaloFalloff(80.0f)
                .build(*m_engine, sunlight);

            m_scene->addEntity(sunlight);
            m_lights.push_back(sunlight);
        }

        // Fill light (soft blue from opposite direction)
        {
            Entity fillLight = EntityManager::get().create();
            LightManager::Builder(LightManager::Type::DIRECTIONAL)
                .color(Color::toLinear<ACCURATE>({0.7f, 0.8f, 1.0f}))
                .intensity(10000.0f)
                .direction({-0.6f, -0.5f, 0.8f})
                .castShadows(false)
                .build(*m_engine, fillLight);

            m_scene->addEntity(fillLight);
            m_lights.push_back(fillLight);
        }

        // Point light (accent)
        {
            Entity pointLight = EntityManager::get().create();
            LightManager::Builder(LightManager::Type::POINT)
                .color(Color::toLinear<ACCURATE>({1.0f, 0.7f, 0.5f}))
                .intensity(100000.0f)
                .position({2.0f, 3.0f, 2.0f})
                .falloff(10.0f)
                .castShadows(false)
                .build(*m_engine, pointLight);

            m_scene->addEntity(pointLight);
            m_lights.push_back(pointLight);
        }
    }

    bool loadMaterial() {
        // Load PBR material
        std::ifstream file("pbr.filamat", std::ios::binary);
        if (!file) {
            std::cerr << "Failed to open pbr.filamat\n";
            return false;
        }

        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(size);
        file.read(reinterpret_cast<char*>(buffer.data()), size);

        m_material = Material::Builder()
            .package(buffer.data(), buffer.size())
            .build(*m_engine);

        if (!m_material) return false;

        // Create material instance with textures
        m_materialInstance = m_material->createInstance();

        TextureSampler sampler(
            TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
            TextureSampler::MagFilter::LINEAR,
            TextureSampler::WrapMode::REPEAT
        );

        m_materialInstance->setParameter("albedo", m_textures.albedo, sampler);
        m_materialInstance->setParameter("normal", m_textures.normal, sampler);
        m_materialInstance->setParameter("metallic", m_textures.metallic, sampler);
        m_materialInstance->setParameter("roughness", m_textures.roughness, sampler);
        m_materialInstance->setParameter("ao", m_textures.ao, sampler);

        return true;
    }

    bool loadModel(const std::string& path) {
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err, warn;

        bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);
        if (!warn.empty()) {
            std::cout << "glTF warning: " << warn << "\n";
        }
        if (!err.empty()) {
            std::cerr << "glTF error: " << err << "\n";
        }
        if (!ret) {
            return false;
        }

        // Process each mesh
        for (const auto& mesh : model.meshes) {
            for (const auto& primitive : mesh.primitives) {
                Entity entity = createEntityFromPrimitive(model, primitive);
                if (entity) {
                    m_modelEntities.push_back(entity);
                    m_scene->addEntity(entity);
                }
            }
        }

        std::cout << "Loaded model: " << path << " ("
                  << m_modelEntities.size() << " primitives)\n";

        return true;
    }

    Entity createEntityFromPrimitive(const tinygltf::Model& model,
                                     const tinygltf::Primitive& primitive) {
        // Extract vertex data from glTF accessors
        // This is simplified - real implementation needs to handle
        // different accessor types, sparse accessors, etc.

        // TODO: Full glTF primitive parsing
        // For now, return placeholder entity

        Entity entity = EntityManager::get().create();

        // Placeholder - should create actual geometry from glTF data
        RenderableManager::Builder(1)
            .boundingBox({{-1, -1, -1}, {1, 1, 1}})
            .material(0, m_materialInstance)
            .culling(true)
            .receiveShadows(true)
            .castShadows(true)
            .build(*m_engine, entity);

        return entity;
    }

    void updateTransform() {
        static float angle = 0.0f;
        angle += 0.01f;

        auto& tm = m_engine->getTransformManager();

        for (auto entity : m_modelEntities) {
            auto instance = tm.getInstance(entity);
            if (instance) {
                mat4f rotation = mat4f::rotation(angle, float3{0, 1, 0});
                tm.setTransform(instance, rotation);
            }
        }
    }

    Engine* m_engine = nullptr;
    SwapChain* m_swapChain = nullptr;
    Renderer* m_renderer = nullptr;
    Scene* m_scene = nullptr;
    View* m_view = nullptr;
    Camera* m_camera = nullptr;

    PBRTextures m_textures;
    Material* m_material = nullptr;
    MaterialInstance* m_materialInstance = nullptr;

    Texture* m_iblTexture = nullptr;
    IndirectLight* m_indirectLight = nullptr;
    Skybox* m_skybox = nullptr;

    std::vector<Entity> m_modelEntities;
    std::vector<Entity> m_lights;

    int m_width = 0;
    int m_height = 0;
};

int main(int argc, char** argv) {
    // Initialize GLFW and create window
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "PBR Model", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    void* nativeWindow = nullptr;
#if defined(__APPLE__)
    nativeWindow = glfwGetCocoaWindow(window);
#elif defined(_WIN32)
    nativeWindow = glfwGetWin32Window(window);
#elif defined(__linux__)
    nativeWindow = (void*)(uintptr_t)glfwGetX11Window(window);
#endif

    PBRModel app;
    if (!app.initialize(nativeWindow, 1280, 720)) {
        std::cerr << "Failed to initialize app\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    while (!glfwWindowShouldClose(window)) {
        app.render();
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
```

### PBR 材质定义

创建 `pbr.mat`:

```glsl
material {
    name : PBR,
    shadingModel : lit,
    blending : opaque,

    parameters : [
        {
            type : sampler2d,
            name : albedo,
            precision: default
        },
        {
            type : sampler2d,
            name : normal,
            precision: default
        },
        {
            type : sampler2d,
            name : metallic,
            precision: default
        },
        {
            type : sampler2d,
            name : roughness,
            precision: default
        },
        {
            type : sampler2d,
            name : ao,
            precision: default
        }
    ],

    requires : [
        uv0,
        tangents
    ]
}

vertex {
    void materialVertex(inout MaterialVertexInputs material) {
        // Tangent space is automatically computed
    }
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        vec2 uv = getUV0();

        // Sample textures
        vec4 albedoSample = texture(materialParams_albedo, uv);
        vec3 normalSample = texture(materialParams_normal, uv).xyz;
        float metallicSample = texture(materialParams_metallic, uv).r;
        float roughnessSample = texture(materialParams_roughness, uv).r;
        float aoSample = texture(materialParams_ao, uv).r;

        // Set material properties
        material.baseColor = albedoSample;

        // Normal mapping (tangent space to world space)
        normalSample = normalSample * 2.0 - 1.0;  // [0,1] to [-1,1]
        material.normal = normalSample;

        material.metallic = metallicSample;
        material.roughness = roughnessSample;
        material.ambientOcclusion = aoSample;
    }
}
```

编译材质:

```bash
matc -a all -o pbr.filamat pbr.mat
```

### 生成 IBL 环境

使用 `cmgen` 工具生成 IBL 环境贴图:

```bash
# 从 HDR 环境图生成 IBL
cmgen -x . --format=ktx --size=256 --extract-blur=0.1 environment.hdr

# 输出:
# - environment_ibl.ktx (IBL 立方体贴图)
# - environment_skybox.ktx (天空盒)
# - environment_sh.txt (球谐系数)
```

## Android 实现

### Kotlin 版本

```kotlin
// PBRModelActivity.kt
package com.example.filament.pbrmodel

import android.graphics.BitmapFactory
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.google.android.filament.*
import java.nio.ByteBuffer

class PBRModelActivity : AppCompatActivity() {
    private var engine: Engine? = null
    private var indirectLight: IndirectLight? = null
    private var skybox: Skybox? = null

    private val textures = mutableMapOf<String, Texture>()
    private var materialInstance: MaterialInstance? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setupFilament()
        loadPBRTextures()
        loadIBL()
        setupLights()
        loadModel()
    }

    private fun loadPBRTextures() {
        textures["albedo"] = loadTexture(R.drawable.albedo, false)
        textures["normal"] = loadTexture(R.drawable.normal, true)
        textures["metallic"] = loadTexture(R.drawable.metallic, true)
        textures["roughness"] = loadTexture(R.drawable.roughness, true)
        textures["ao"] = loadTexture(R.drawable.ao, true)
    }

    private fun loadTexture(resId: Int, linear: Boolean): Texture {
        val bitmap = BitmapFactory.decodeResource(resources, resId)
        val width = bitmap.width
        val height = bitmap.height

        val buffer = ByteBuffer.allocateDirect(width * height * 4)
        val pixels = IntArray(width * height)
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height)

        for (pixel in pixels) {
            buffer.put(((pixel shr 16) and 0xFF).toByte())
            buffer.put(((pixel shr 8) and 0xFF).toByte())
            buffer.put((pixel and 0xFF).toByte())
            buffer.put(((pixel shr 24) and 0xFF).toByte())
        }
        buffer.flip()

        val format = if (linear) {
            Texture.InternalFormat.RGBA8
        } else {
            Texture.InternalFormat.SRGB8_A8
        }

        val texture = Texture.Builder()
            .width(width)
            .height(height)
            .levels(0xFF)  // Auto mipmap levels
            .format(format)
            .sampler(Texture.Sampler.SAMPLER_2D)
            .build(engine!!)

        texture.setImage(engine!!, 0, Texture.PixelBufferDescriptor(
            buffer,
            Texture.Format.RGBA,
            Texture.Type.UBYTE
        ))

        texture.generateMipmaps(engine!!)

        return texture
    }

    private fun loadIBL() {
        // Load IBL from assets
        val iblData = assets.open("environment_ibl.ktx").readBytes()

        // Create IBL texture (simplified - real impl parses KTX)
        val iblTexture = Texture.Builder()
            .width(256)
            .height(256)
            .levels(8)
            .format(Texture.InternalFormat.RGBA16F)
            .sampler(Texture.Sampler.SAMPLER_CUBEMAP)
            .build(engine!!)

        // Create indirect light
        indirectLight = IndirectLight.Builder()
            .reflections(iblTexture)
            .intensity(30000.0f)
            .build(engine!!)

        scene?.indirectLight = indirectLight

        // Create skybox
        skybox = Skybox.Builder()
            .environment(iblTexture)
            .build(engine!!)

        scene?.skybox = skybox
    }

    private fun setupLights() {
        // Sun light
        val sunEntity = EntityManager.get().create()
        LightManager.Builder(LightManager.Type.SUN)
            .color(1.0f, 0.95f, 0.9f)
            .intensity(100000.0f)
            .direction(0.6f, -1.0f, -0.8f)
            .castShadows(true)
            .build(engine!!, sunEntity)

        scene?.addEntity(sunEntity)
    }

    override fun onDestroy() {
        super.onDestroy()

        engine?.let { eng ->
            textures.values.forEach { eng.destroyTexture(it) }
            materialInstance?.let { eng.destroyMaterialInstance(it) }
            indirectLight?.let { eng.destroyIndirectLight(it) }
            skybox?.let { eng.destroySkybox(it) }
        }
    }
}
```

## Web 实现

### JavaScript 版本

```javascript
// pbr-model.js
import * as Filament from 'filament';

class PBRModel {
    constructor(canvas) {
        this.canvas = canvas;
        this.engine = null;
        this.textures = {};
        this.materialInstance = null;
        this.ibl = null;
    }

    async init() {
        await Filament.init(['filament.wasm'], () => {});

        this.engine = Filament.Engine.create(this.canvas);
        // Setup renderer, scene, view, camera...

        await this.loadPBRTextures();
        await this.loadIBL();
        this.setupLights();
        await this.loadModel();
    }

    async loadPBRTextures() {
        const textureNames = ['albedo', 'normal', 'metallic', 'roughness', 'ao'];

        for (const name of textureNames) {
            this.textures[name] = await this.loadTexture(`textures/${name}.png`,
                name === 'albedo' ? false : true);
        }
    }

    async loadTexture(url, linear) {
        const img = await this.loadImage(url);

        // Extract RGBA data
        const canvas = document.createElement('canvas');
        canvas.width = img.width;
        canvas.height = img.height;
        const ctx = canvas.getContext('2d');
        ctx.drawImage(img, 0, 0);
        const imageData = ctx.getImageData(0, 0, img.width, img.height);

        const format = linear ?
            Filament.Texture$InternalFormat.RGBA8 :
            Filament.Texture$InternalFormat.SRGB8_A8;

        const texture = Filament.Texture.Builder()
            .width(img.width)
            .height(img.height)
            .levels(0xFF)
            .format(format)
            .sampler(Filament.Texture$Sampler.SAMPLER_2D)
            .build(this.engine);

        texture.setImage(this.engine, 0, {
            buffer: new Uint8Array(imageData.data),
            format: Filament.Texture$Format.RGBA,
            type: Filament.Texture$Type.UBYTE
        });

        texture.generateMipmaps(this.engine);

        return texture;
    }

    async loadIBL() {
        // Load IBL KTX file
        const response = await fetch('environment_ibl.ktx');
        const ktxData = await response.arrayBuffer();

        // Parse KTX and create texture
        // (simplified - real implementation needs KTX parser)

        const iblTexture = Filament.Texture.Builder()
            .width(256)
            .height(256)
            .levels(8)
            .format(Filament.Texture$InternalFormat.RGBA16F)
            .sampler(Filament.Texture$Sampler.SAMPLER_CUBEMAP)
            .build(this.engine);

        this.ibl = Filament.IndirectLight.Builder()
            .reflections(iblTexture)
            .intensity(30000.0)
            .build(this.engine);

        this.scene.setIndirectLight(this.ibl);

        const skybox = Filament.Skybox.Builder()
            .environment(iblTexture)
            .build(this.engine);

        this.scene.setSkybox(skybox);
    }

    setupLights() {
        // Sun light
        const sun = Filament.EntityManager.get().create();
        Filament.LightManager.Builder(Filament.LightManager$Type.SUN)
            .color([1.0, 0.95, 0.9])
            .intensity(100000.0)
            .direction([0.6, -1.0, -0.8])
            .castShadows(true)
            .build(this.engine, sun);

        this.scene.addEntity(sun);
    }

    async loadImage(url) {
        return new Promise((resolve, reject) => {
            const img = new Image();
            img.onload = () => resolve(img);
            img.onerror = reject;
            img.src = url;
        });
    }

    render() {
        if (this.renderer.beginFrame(this.swapChain)) {
            this.renderer.render(this.view);
            this.renderer.endFrame();
        }
    }
}

const canvas = document.getElementById('canvas');
const app = new PBRModel(canvas);

app.init().then(() => {
    function animate() {
        app.render();
        requestAnimationFrame(animate);
    }
    animate();
});
```

## 构建和运行

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.19)
project(PBRModel)

set(CMAKE_CXX_STANDARD 17)

find_package(filament REQUIRED)
find_package(glfw3 REQUIRED)

add_executable(pbr-model pbr_model.cpp)

target_link_libraries(pbr-model
    filament
    glfw
)

# Copy assets
file(COPY
    ${CMAKE_SOURCE_DIR}/pbr.filamat
    ${CMAKE_SOURCE_DIR}/model/
    ${CMAKE_SOURCE_DIR}/environment.ktx
    DESTINATION ${CMAKE_BINARY_DIR}
)
```

### 构建命令

```bash
# Desktop
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja pbr-model
./pbr-model

# Generate IBL first
cmgen -x ../assets/ --format=ktx --size=256 environment.hdr
```

## 常见问题

### Q1: 材质看起来过亮或过暗

**原因**: 光照强度或 IBL 强度不正确

**解决**:

```cpp
// 调整 IBL 强度 (lux)
IndirectLight::Builder()
    .intensity(30000.0f)  // 尝试 10000-50000
    .build(*engine);

// 调整直接光强度
LightManager::Builder(LightManager::Type::SUN)
    .intensity(100000.0f)  // 尝试 50000-150000
    .build(*engine, entity);
```

### Q2: 法线贴图不起作用

**原因**:
- 未提供切线数据
- 法线贴图在 sRGB 色彩空间

**解决**:

```cpp
// 1. 确保切线属性存在
.attribute(VertexAttribute::TANGENTS, ...)

// 2. 法线贴图必须是线性空间
Texture::Builder()
    .format(Texture::InternalFormat::RGBA8)  // 不是 SRGB8_A8
    .build(*engine);

// 3. 在着色器中解码法线
vec3 normal = texture(materialParams_normal, uv).xyz;
normal = normal * 2.0 - 1.0;  // [0,1] -> [-1,1]
```

### Q3: 金属材质反射不正确

**原因**: 未使用 IBL 或 IBL 质量不足

**解决**:

```bash
# 生成高质量 IBL
cmgen -x . \
    --format=ktx \
    --size=512 \       # 更大的分辨率
    --extract-blur=0.1 \
    --sh=2 \           # 球谐阶数
    environment.hdr
```

### Q4: 性能问题

**解决**:

```cpp
// 1. 减少阴影质量
LightManager::Builder(...)
    .castShadows(false)  // 禁用阴影
    .build(*engine, entity);

// 2. 降低 MSAA
view->setSampleCount(1);  // 禁用 MSAA

// 3. 禁用后处理
view->setPostProcessingEnabled(false);

// 4. 降低 IBL 分辨率
// 使用 128x128 而非 512x512
```

## 性能优化

### 1. 纹理压缩

```cpp
// 使用压缩格式
Texture::Builder()
    .format(Texture::InternalFormat::ETC2_RGB8)  // Android
    .format(Texture::InternalFormat.BC7_RGBA)    // Desktop
    .build(*engine);
```

### 2. LOD 管理

```cpp
// 基于距离切换 LOD
float distance = length(cameraPos - objectPos);
int lodLevel = distance < 10.0f ? 0 :
               distance < 30.0f ? 1 : 2;

// 设置对应的模型
```

### 3. 视锥剔除

```cpp
// Filament 自动进行视锥剔除
// 确保正确设置 bounding box
RenderableManager::Builder(1)
    .boundingBox({{minX, minY, minZ}, {maxX, maxY, maxZ}})
    .build(*engine, entity);
```

## 扩展练习

1. **清漆层**: 添加 clear coat 层模拟车漆
2. **次表面散射**: 实现 SSS 渲染皮肤/大理石
3. **各向异性**: 拉丝金属效果
4. **布料材质**: Sheen 参数模拟丝绸
5. **动态环境反射**: 实时反射探针

## 相关文档

- [./02-textured-cube.md](./02-textured-cube.md) - 前一个示例
- [./04-gltf-viewer.md](./04-gltf-viewer.md) - 下一个示例：glTF 查看器
- [../material/03-pbr-材质.md](../material/03-pbr-材质.md) - PBR 材质详解
- [../lighting/01-直接光照.md](../lighting/01-直接光照.md) - 光照系统
- [../lighting/02-间接光照.md](../lighting/02-间接光照.md) - IBL 详解
- [../tools/02-cmgen.md](../tools/02-cmgen.md) - cmgen 工具使用
