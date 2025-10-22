# Textured Cube

## 概述

Textured Cube 示例在 Hello Triangle 的基础上增加了三维渲染、纹理映射和变换动画。本示例涵盖:

- **3D 几何体**：创建立方体网格(8个顶点，12个三角形)
- **纹理加载**：从文件加载和上传纹理数据
- **UV 映射**:为每个立方体面设置正确的纹理坐标
- **3D 变换**：使用 TransformManager 实现旋转动画
- **透视投影**：配置透视相机实现3D效果
- **深度测试**：启用 Z-buffer 确保正确的遮挡关系

## 架构设计

### 渲染管线

```
┌──────────────┐
│  Texture     │ (PNG/JPG file)
│  Image File  │
└──────┬───────┘
       │ Load with stb_image
       ▼
┌──────────────┐
│   Texture    │ (GPU resource)
│   Object     │
└──────┬───────┘
       │
       ▼
┌──────────────┐
│   Material   │ (with texture sampler)
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Vertex Buffer│ (position, UV, normal)
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Renderable   │ (cube entity)
└──────┬───────┘
       │ TransformManager
       ▼
┌──────────────┐
│  Animation   │ (rotation)
└──────────────┘
```

### 立方体几何结构

```
      v6 ────────── v7
      /|           /|
     / |          / |
   v2 ────────── v3 |
    |  |         |  |
    | v4 ────────│─ v5
    | /          | /
    |/           |/
   v0 ────────── v1

顶点索引:
- v0: (-1, -1, -1)  左下前
- v1: ( 1, -1, -1)  右下前
- v2: (-1,  1, -1)  左上前
- v3: ( 1,  1, -1)  右上前
- v4: (-1, -1,  1)  左下后
- v5: ( 1, -1,  1)  右下后
- v6: (-1,  1,  1)  左上后
- v7: ( 1,  1,  1)  右上后

6个面，每面2个三角形:
- Front:  v0, v1, v2, v3
- Back:   v5, v4, v7, v6
- Left:   v4, v0, v6, v2
- Right:  v1, v5, v3, v7
- Top:    v2, v3, v6, v7
- Bottom: v4, v5, v0, v1
```

## 完整实现 (C++)

### 主程序

```cpp
// textured_cube.cpp
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <utils/EntityManager.h>

#include <math/vec3.h>
#include <math/mat4.h>
#include <math/quat.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>

using namespace filament;
using namespace filament::math;
using namespace utils;

// Cube vertices: position (3), UV (2), normal (3)
static const float CUBE_VERTICES[] = {
    // Front face (z = -1)
    -1.0f, -1.0f, -1.0f,   0.0f, 0.0f,   0.0f, 0.0f, -1.0f,  // v0
     1.0f, -1.0f, -1.0f,   1.0f, 0.0f,   0.0f, 0.0f, -1.0f,  // v1
    -1.0f,  1.0f, -1.0f,   0.0f, 1.0f,   0.0f, 0.0f, -1.0f,  // v2
     1.0f,  1.0f, -1.0f,   1.0f, 1.0f,   0.0f, 0.0f, -1.0f,  // v3

    // Back face (z = 1)
     1.0f, -1.0f,  1.0f,   0.0f, 0.0f,   0.0f, 0.0f, 1.0f,   // v5
    -1.0f, -1.0f,  1.0f,   1.0f, 0.0f,   0.0f, 0.0f, 1.0f,   // v4
     1.0f,  1.0f,  1.0f,   0.0f, 1.0f,   0.0f, 0.0f, 1.0f,   // v7
    -1.0f,  1.0f,  1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 1.0f,   // v6

    // Left face (x = -1)
    -1.0f, -1.0f,  1.0f,   0.0f, 0.0f,   -1.0f, 0.0f, 0.0f,  // v4
    -1.0f, -1.0f, -1.0f,   1.0f, 0.0f,   -1.0f, 0.0f, 0.0f,  // v0
    -1.0f,  1.0f,  1.0f,   0.0f, 1.0f,   -1.0f, 0.0f, 0.0f,  // v6
    -1.0f,  1.0f, -1.0f,   1.0f, 1.0f,   -1.0f, 0.0f, 0.0f,  // v2

    // Right face (x = 1)
     1.0f, -1.0f, -1.0f,   0.0f, 0.0f,   1.0f, 0.0f, 0.0f,   // v1
     1.0f, -1.0f,  1.0f,   1.0f, 0.0f,   1.0f, 0.0f, 0.0f,   // v5
     1.0f,  1.0f, -1.0f,   0.0f, 1.0f,   1.0f, 0.0f, 0.0f,   // v3
     1.0f,  1.0f,  1.0f,   1.0f, 1.0f,   1.0f, 0.0f, 0.0f,   // v7

    // Top face (y = 1)
    -1.0f,  1.0f, -1.0f,   0.0f, 0.0f,   0.0f, 1.0f, 0.0f,   // v2
     1.0f,  1.0f, -1.0f,   1.0f, 0.0f,   0.0f, 1.0f, 0.0f,   // v3
    -1.0f,  1.0f,  1.0f,   0.0f, 1.0f,   0.0f, 1.0f, 0.0f,   // v6
     1.0f,  1.0f,  1.0f,   1.0f, 1.0f,   0.0f, 1.0f, 0.0f,   // v7

    // Bottom face (y = -1)
    -1.0f, -1.0f,  1.0f,   0.0f, 0.0f,   0.0f, -1.0f, 0.0f,  // v4
     1.0f, -1.0f,  1.0f,   1.0f, 0.0f,   0.0f, -1.0f, 0.0f,  // v5
    -1.0f, -1.0f, -1.0f,   0.0f, 1.0f,   0.0f, -1.0f, 0.0f,  // v0
     1.0f, -1.0f, -1.0f,   1.0f, 1.0f,   0.0f, -1.0f, 0.0f   // v1
};

// Cube indices (2 triangles per face, 6 faces)
static const uint16_t CUBE_INDICES[] = {
    // Front
    0, 1, 2,    2, 1, 3,
    // Back
    4, 5, 6,    6, 5, 7,
    // Left
    8, 9, 10,   10, 9, 11,
    // Right
    12, 13, 14, 14, 13, 15,
    // Top
    16, 17, 18, 18, 17, 19,
    // Bottom
    20, 21, 22, 22, 21, 23
};

class TexturedCube {
public:
    TexturedCube() = default;
    ~TexturedCube() { cleanup(); }

    bool initialize(void* nativeWindow, int width, int height) {
        m_width = width;
        m_height = height;
        m_startTime = std::chrono::high_resolution_clock::now();

        // Create engine
        m_engine = Engine::create(Engine::Backend::DEFAULT);
        if (!m_engine) {
            std::cerr << "Failed to create engine\n";
            return false;
        }

        // Create swap chain
        m_swapChain = m_engine->createSwapChain(nativeWindow);
        if (!m_swapChain) {
            std::cerr << "Failed to create swap chain\n";
            return false;
        }

        // Create renderer
        m_renderer = m_engine->createRenderer();
        m_renderer->setClearOptions({
            .clearColor = {0.1f, 0.125f, 0.25f, 1.0f},
            .clear = true
        });

        // Create scene
        m_scene = m_engine->createScene();

        // Create view
        m_view = m_engine->createView();
        m_view->setScene(m_scene);
        m_view->setViewport({0, 0, (uint32_t)width, (uint32_t)height});

        // Enable depth testing
        m_view->setDepthPrepass(View::DepthPrepass::ENABLED);

        // Create camera with perspective projection
        m_camera = m_engine->createCamera(EntityManager::get().create());
        const float aspect = (float)width / (float)height;
        m_camera->setProjection(Camera::Projection::PERSPECTIVE,
            45.0,       // fov in degrees
            aspect,     // aspect ratio
            0.1,        // near plane
            100.0);     // far plane

        // Position camera
        m_camera->lookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
        m_view->setCamera(m_camera);

        // Load texture
        if (!loadTexture("texture.png")) {
            std::cerr << "Failed to load texture\n";
            return false;
        }

        // Load material
        if (!loadMaterial()) {
            std::cerr << "Failed to load material\n";
            return false;
        }

        // Create cube geometry
        if (!createCubeGeometry()) {
            std::cerr << "Failed to create cube geometry\n";
            return false;
        }

        // Create cube renderable
        if (!createRenderable()) {
            std::cerr << "Failed to create renderable\n";
            return false;
        }

        return true;
    }

    void render() {
        if (!m_renderer || !m_swapChain || !m_view) return;

        // Update cube rotation
        updateTransform();

        // Render frame
        if (m_renderer->beginFrame(m_swapChain)) {
            m_renderer->render(m_view);
            m_renderer->endFrame();
        }
    }

    void resize(int width, int height) {
        m_width = width;
        m_height = height;
        m_view->setViewport({0, 0, (uint32_t)width, (uint32_t)height});

        const float aspect = (float)width / (float)height;
        m_camera->setProjection(Camera::Projection::PERSPECTIVE,
            45.0, aspect, 0.1, 100.0);
    }

    void cleanup() {
        if (m_engine) {
            if (m_cubeEntity) {
                m_scene->remove(m_cubeEntity);
                m_engine->destroy(m_cubeEntity);
            }
            if (m_vertexBuffer) m_engine->destroy(m_vertexBuffer);
            if (m_indexBuffer) m_engine->destroy(m_indexBuffer);
            if (m_material) m_engine->destroy(m_material);
            if (m_texture) m_engine->destroy(m_texture);
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
    bool loadTexture(const char* path) {
        // Load image data using stb_image
        int width, height, channels;
        unsigned char* data = stbi_load(path, &width, &height, &channels, 4);
        if (!data) {
            std::cerr << "Failed to load image: " << path << "\n";
            std::cerr << "stbi error: " << stbi_failure_reason() << "\n";
            return false;
        }

        // Calculate required size
        const size_t size = width * height * 4;

        // Create texture
        m_texture = Texture::Builder()
            .width(static_cast<uint32_t>(width))
            .height(static_cast<uint32_t>(height))
            .levels(1)
            .format(Texture::InternalFormat::RGBA8)
            .sampler(Texture::Sampler::SAMPLER_2D)
            .build(*m_engine);

        if (!m_texture) {
            stbi_image_free(data);
            return false;
        }

        // Upload texture data
        Texture::PixelBufferDescriptor buffer(
            data,
            size,
            Texture::Format::RGBA,
            Texture::Type::UBYTE,
            [](void* buffer, size_t size, void* user) {
                stbi_image_free(buffer);
            }
        );

        m_texture->setImage(*m_engine, 0, std::move(buffer));

        std::cout << "Loaded texture: " << width << "x" << height
                  << " (" << channels << " channels)\n";

        return true;
    }

    bool loadMaterial() {
        // Load precompiled material
        std::ifstream file("textured.filamat", std::ios::binary);
        if (!file) {
            std::cerr << "Failed to open textured.filamat\n";
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

        // Set texture parameter
        m_materialInstance = m_material->createInstance();
        TextureSampler sampler(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
                               TextureSampler::MagFilter::LINEAR);
        m_materialInstance->setParameter("albedo", m_texture, sampler);

        return true;
    }

    bool createCubeGeometry() {
        // Create vertex buffer
        m_vertexBuffer = VertexBuffer::Builder()
            .vertexCount(24)  // 4 vertices per face * 6 faces
            .bufferCount(1)
            .attribute(VertexAttribute::POSITION, 0,
                      VertexBuffer::AttributeType::FLOAT3, 0, 32)
            .attribute(VertexAttribute::UV0, 0,
                      VertexBuffer::AttributeType::FLOAT2, 12, 32)
            .attribute(VertexAttribute::TANGENTS, 0,
                      VertexBuffer::AttributeType::FLOAT3, 20, 32)
            .build(*m_engine);

        if (!m_vertexBuffer) return false;

        m_vertexBuffer->setBufferAt(*m_engine, 0,
            VertexBuffer::BufferDescriptor(
                CUBE_VERTICES,
                sizeof(CUBE_VERTICES),
                nullptr
            )
        );

        // Create index buffer
        m_indexBuffer = IndexBuffer::Builder()
            .indexCount(36)  // 6 faces * 2 triangles * 3 vertices
            .bufferType(IndexBuffer::IndexType::USHORT)
            .build(*m_engine);

        if (!m_indexBuffer) return false;

        m_indexBuffer->setBuffer(*m_engine,
            IndexBuffer::BufferDescriptor(
                CUBE_INDICES,
                sizeof(CUBE_INDICES),
                nullptr
            )
        );

        return true;
    }

    bool createRenderable() {
        m_cubeEntity = EntityManager::get().create();

        RenderableManager::Builder(1)
            .boundingBox({{-1, -1, -1}, {1, 1, 1}})
            .material(0, m_materialInstance)
            .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,
                     m_vertexBuffer, m_indexBuffer, 0, 36)
            .culling(true)
            .receiveShadows(false)
            .castShadows(false)
            .build(*m_engine, m_cubeEntity);

        m_scene->addEntity(m_cubeEntity);

        return true;
    }

    void updateTransform() {
        auto now = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(now - m_startTime).count();

        // Rotate cube around Y and X axes
        float angleY = elapsed * 0.5f;  // 0.5 rad/s
        float angleX = elapsed * 0.3f;  // 0.3 rad/s

        auto& tm = m_engine->getTransformManager();
        auto instance = tm.getInstance(m_cubeEntity);

        mat4f rotation = mat4f::rotation(angleY, float3{0, 1, 0})
                       * mat4f::rotation(angleX, float3{1, 0, 0});

        tm.setTransform(instance, rotation);
    }

    Engine* m_engine = nullptr;
    SwapChain* m_swapChain = nullptr;
    Renderer* m_renderer = nullptr;
    Scene* m_scene = nullptr;
    View* m_view = nullptr;
    Camera* m_camera = nullptr;
    VertexBuffer* m_vertexBuffer = nullptr;
    IndexBuffer* m_indexBuffer = nullptr;
    Material* m_material = nullptr;
    MaterialInstance* m_materialInstance = nullptr;
    Texture* m_texture = nullptr;
    Entity m_cubeEntity;

    int m_width = 0;
    int m_height = 0;
    std::chrono::high_resolution_clock::time_point m_startTime;
};

int main(int argc, char** argv) {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }

    const int width = 1024;
    const int height = 768;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(width, height,
        "Textured Cube", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return -1;
    }

    // Get native window handle
    void* nativeWindow = nullptr;
#if defined(__APPLE__)
    nativeWindow = glfwGetCocoaWindow(window);
#elif defined(_WIN32)
    nativeWindow = glfwGetWin32Window(window);
#elif defined(__linux__)
    nativeWindow = (void*)(uintptr_t)glfwGetX11Window(window);
#endif

    TexturedCube app;
    if (!app.initialize(nativeWindow, width, height)) {
        std::cerr << "Failed to initialize app\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // Window resize callback
    glfwSetWindowUserPointer(window, &app);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int width, int height) {
        auto* app = static_cast<TexturedCube*>(glfwGetWindowUserPointer(w));
        app->resize(width, height);
    });

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        app.render();
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
```

### 材质定义

创建 `textured.mat`:

```glsl
material {
    name : Textured,
    shadingModel : lit,

    parameters : [
        {
            type : sampler2d,
            name : albedo
        }
    ],

    requires : [
        uv0
    ]
}

vertex {
    void materialVertex(inout MaterialVertexInputs material) {
        // Pass UV coordinates to fragment shader
    }
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        // Sample texture
        vec4 color = texture(materialParams_albedo, getUV0());
        material.baseColor = color;
    }
}
```

编译材质:

```bash
matc -a all -o textured.filamat textured.mat
```

## iOS 实现

### Objective-C++ 版本

```objc
// TexturedCubeViewController.mm
#import <UIKit/UIKit.h>
#import <Metal/Metal.h>

#include <filament/Engine.h>
#include <filament/Texture.h>
#include <filament/TransformManager.h>
#include <math/mat4.h>

@interface TexturedCubeViewController : UIViewController {
    filament::Engine* _engine;
    filament::Texture* _texture;
    filament::math::mat4f _rotation;

    CAMetalLayer* _metalLayer;
    CADisplayLink* _displayLink;
    CFTimeInterval _startTime;
}
@end

@implementation TexturedCubeViewController

- (void)viewDidLoad {
    [super viewDidLoad];

    _startTime = CACurrentMediaTime();

    // Setup Metal layer
    _metalLayer = [CAMetalLayer layer];
    _metalLayer.frame = self.view.bounds;
    _metalLayer.device = MTLCreateSystemDefaultDevice();
    [self.view.layer addSublayer:_metalLayer];

    [self setupFilament];
    [self loadTexture];

    // Start render loop
    _displayLink = [CADisplayLink displayLinkWithTarget:self
                                               selector:@selector(render:)];
    [_displayLink addToRunLoop:[NSRunLoop currentRunLoop]
                       forMode:NSRunLoopCommonModes];
}

- (void)loadTexture {
    UIImage* image = [UIImage imageNamed:@"texture.png"];
    if (!image) {
        NSLog(@"Failed to load texture image");
        return;
    }

    // Convert UIImage to raw RGBA data
    CGImageRef cgImage = image.CGImage;
    size_t width = CGImageGetWidth(cgImage);
    size_t height = CGImageGetHeight(cgImage);

    NSMutableData* imageData = [NSMutableData dataWithLength:width * height * 4];
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(
        [imageData mutableBytes],
        width, height,
        8, width * 4,
        colorSpace,
        kCGImageAlphaPremultipliedLast
    );

    CGContextDrawImage(context, CGRectMake(0, 0, width, height), cgImage);
    CGContextRelease(context);
    CGColorSpaceRelease(colorSpace);

    // Create Filament texture
    using namespace filament;
    _texture = Texture::Builder()
        .width((uint32_t)width)
        .height((uint32_t)height)
        .levels(1)
        .format(Texture::InternalFormat::RGBA8)
        .sampler(Texture::Sampler::SAMPLER_2D)
        .build(*_engine);

    // Upload texture data
    Texture::PixelBufferDescriptor buffer(
        [imageData mutableBytes],
        width * height * 4,
        Texture::Format::RGBA,
        Texture::Type::UBYTE
    );

    _texture->setImage(*_engine, 0, std::move(buffer));
}

- (void)render:(CADisplayLink*)displayLink {
    CFTimeInterval elapsed = CACurrentMediaTime() - _startTime;

    // Update rotation
    float angleY = elapsed * 0.5f;
    float angleX = elapsed * 0.3f;

    using namespace filament::math;
    _rotation = mat4f::rotation(angleY, float3{0, 1, 0})
              * mat4f::rotation(angleX, float3{1, 0, 0});

    // TODO: Apply transform and render
}

- (void)dealloc {
    [_displayLink invalidate];
    if (_engine && _texture) {
        _engine->destroy(_texture);
    }
}

@end
```

### Swift 版本

```swift
// TexturedCubeView.swift
import UIKit
import Metal
import CoreGraphics

class TexturedCubeView: UIView {
    private var metalLayer: CAMetalLayer!
    private var displayLink: CADisplayLink?
    private var startTime: CFTimeInterval = 0

    private var engine: OpaquePointer?
    private var texture: OpaquePointer?
    private var cubeEntity: UInt32 = 0

    override init(frame: CGRect) {
        super.init(frame: frame)
        setupMetal()
        setupFilament()
        loadTexture()
        startRenderLoop()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupMetal()
        setupFilament()
        loadTexture()
        startRenderLoop()
    }

    private func loadTexture() {
        guard let image = UIImage(named: "texture.png"),
              let cgImage = image.cgImage else {
            print("Failed to load texture")
            return
        }

        let width = cgImage.width
        let height = cgImage.height

        // Convert to RGBA
        var imageData = Data(count: width * height * 4)
        imageData.withUnsafeMutableBytes { ptr in
            guard let context = CGContext(
                data: ptr.baseAddress,
                width: width,
                height: height,
                bitsPerComponent: 8,
                bytesPerRow: width * 4,
                space: CGColorSpaceCreateDeviceRGB(),
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
            ) else { return }

            context.draw(cgImage, in: CGRect(x: 0, y: 0,
                                             width: width,
                                             height: height))
        }

        // Create Filament texture
        imageData.withUnsafeMutableBytes { ptr in
            texture = FilamentTexture_create(engine,
                UInt32(width), UInt32(height),
                ptr.baseAddress!)
        }
    }

    @objc private func render() {
        let elapsed = CACurrentMediaTime() - startTime
        let angleY = Float(elapsed) * 0.5
        let angleX = Float(elapsed) * 0.3

        // Update cube rotation
        FilamentTransform_setRotation(engine, cubeEntity, angleX, angleY, 0)

        // Render frame
        FilamentRenderer_renderFrame(engine)
    }

    private func startRenderLoop() {
        startTime = CACurrentMediaTime()
        displayLink = CADisplayLink(target: self, selector: #selector(render))
        displayLink?.add(to: .current, forMode: .common)
    }

    deinit {
        displayLink?.invalidate()
        if let tex = texture {
            FilamentTexture_destroy(engine, tex)
        }
    }
}
```

## Android 实现

### Kotlin 版本

```kotlin
// TexturedCubeActivity.kt
package com.example.filament.texturedcube

import android.graphics.BitmapFactory
import android.os.Bundle
import android.view.Choreographer
import androidx.appcompat.app.AppCompatActivity
import com.google.android.filament.*
import com.google.android.filament.android.UiHelper
import java.nio.ByteBuffer

class TexturedCubeActivity : AppCompatActivity() {
    private lateinit var choreographer: Choreographer

    private var engine: Engine? = null
    private var texture: Texture? = null
    private var materialInstance: MaterialInstance? = null
    @Entity private var cubeEntity = 0

    private var startTime: Long = 0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        startTime = System.nanoTime()
        choreographer = Choreographer.getInstance()

        setupFilament()
        loadTexture()
        createCube()
    }

    private fun loadTexture() {
        val bitmap = BitmapFactory.decodeResource(resources, R.drawable.texture)
        val width = bitmap.width
        val height = bitmap.height

        // Convert bitmap to RGBA buffer
        val buffer = ByteBuffer.allocateDirect(width * height * 4)
        val pixels = IntArray(width * height)
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height)

        for (pixel in pixels) {
            buffer.put(((pixel shr 16) and 0xFF).toByte())  // R
            buffer.put(((pixel shr 8) and 0xFF).toByte())   // G
            buffer.put((pixel and 0xFF).toByte())            // B
            buffer.put(((pixel shr 24) and 0xFF).toByte())  // A
        }
        buffer.flip()

        // Create Filament texture
        texture = Texture.Builder()
            .width(width)
            .height(height)
            .levels(1)
            .format(Texture.InternalFormat.RGBA8)
            .sampler(Texture.Sampler.SAMPLER_2D)
            .build(engine!!)

        texture!!.setImage(engine!!, 0, Texture.PixelBufferDescriptor(
            buffer,
            Texture.Format.RGBA,
            Texture.Type.UBYTE
        ))

        // Set texture on material
        materialInstance?.setParameter("albedo", texture!!,
            TextureSampler(TextureSampler.MinFilter.LINEAR,
                          TextureSampler.MagFilter.LINEAR))
    }

    private fun updateCubeRotation() {
        val elapsed = (System.nanoTime() - startTime) / 1_000_000_000.0f
        val angleY = elapsed * 0.5f
        val angleX = elapsed * 0.3f

        val tm = engine!!.transformManager
        val instance = tm.getInstance(cubeEntity)

        val rotY = FloatArray(16)
        val rotX = FloatArray(16)
        val rotation = FloatArray(16)

        android.opengl.Matrix.setRotateM(rotY, 0, Math.toDegrees(angleY.toDouble()).toFloat(), 0f, 1f, 0f)
        android.opengl.Matrix.setRotateM(rotX, 0, Math.toDegrees(angleX.toDouble()).toFloat(), 1f, 0f, 0f)
        android.opengl.Matrix.multiplyMM(rotation, 0, rotY, 0, rotX, 0)

        tm.setTransform(instance, rotation)
    }

    private fun render() {
        updateCubeRotation()
        // Render logic...
    }

    override fun onDestroy() {
        super.onDestroy()

        engine?.let { eng ->
            texture?.let { eng.destroyTexture(it) }
            materialInstance?.let { eng.destroyMaterialInstance(it) }
        }
    }
}
```

## Web 实现

### JavaScript 版本

```javascript
// textured-cube.js
import * as Filament from 'filament';

class TexturedCube {
    constructor(canvas) {
        this.canvas = canvas;
        this.engine = null;
        this.texture = null;
        this.materialInstance = null;
        this.cubeEntity = null;
        this.startTime = Date.now();
    }

    async init() {
        await Filament.init(['filament.wasm'], () => {});

        this.engine = Filament.Engine.create(this.canvas);
        // ... setup engine, renderer, scene, view, camera ...

        await this.loadTexture('texture.png');
        await this.createCube();
    }

    async loadTexture(url) {
        // Load image
        const img = await new Promise((resolve, reject) => {
            const image = new Image();
            image.onload = () => resolve(image);
            image.onerror = reject;
            image.src = url;
        });

        // Create canvas to extract RGBA data
        const canvas = document.createElement('canvas');
        canvas.width = img.width;
        canvas.height = img.height;

        const ctx = canvas.getContext('2d');
        ctx.drawImage(img, 0, 0);

        const imageData = ctx.getImageData(0, 0, img.width, img.height);
        const rgbaData = new Uint8Array(imageData.data);

        // Create Filament texture
        this.texture = Filament.Texture.Builder()
            .width(img.width)
            .height(img.height)
            .levels(1)
            .format(Filament.Texture$InternalFormat.RGBA8)
            .sampler(Filament.Texture$Sampler.SAMPLER_2D)
            .build(this.engine);

        this.texture.setImage(this.engine, 0, {
            buffer: rgbaData,
            format: Filament.Texture$Format.RGBA,
            type: Filament.Texture$Type.UBYTE
        });

        // Set on material
        const sampler = new Filament.TextureSampler(
            Filament.MinFilter.LINEAR,
            Filament.MagFilter.LINEAR
        );
        this.materialInstance.setTextureParameter('albedo',
            this.texture, sampler);
    }

    async createCube() {
        // Create cube geometry with UVs
        const vertices = new Float32Array(CUBE_VERTICES);
        const indices = new Uint16Array(CUBE_INDICES);

        this.vertexBuffer = Filament.VertexBuffer.Builder()
            .vertexCount(24)
            .bufferCount(1)
            .attribute(Filament.VertexAttribute.POSITION, 0,
                      Filament.VertexBuffer$AttributeType.FLOAT3, 0, 32)
            .attribute(Filament.VertexAttribute.UV0, 0,
                      Filament.VertexBuffer$AttributeType.FLOAT2, 12, 32)
            .build(this.engine);

        this.vertexBuffer.setBufferAt(this.engine, 0, vertices);

        this.indexBuffer = Filament.IndexBuffer.Builder()
            .indexCount(36)
            .bufferType(Filament.IndexBuffer$IndexType.USHORT)
            .build(this.engine);

        this.indexBuffer.setBuffer(this.engine, indices);

        // Load material
        const response = await fetch('textured.filamat');
        const materialData = await response.arrayBuffer();
        const material = this.engine.createMaterial(new Uint8Array(materialData));
        this.materialInstance = material.createInstance();

        // Create entity
        const utils = Filament.EntityManager.get();
        this.cubeEntity = utils.create();

        Filament.RenderableManager.Builder(1)
            .boundingBox({ center: [0, 0, 0], halfExtent: [1, 1, 1] })
            .material(0, this.materialInstance)
            .geometry(0, Filament.RenderableManager$PrimitiveType.TRIANGLES,
                     this.vertexBuffer, this.indexBuffer)
            .build(this.engine, this.cubeEntity);

        this.scene.addEntity(this.cubeEntity);
    }

    render() {
        const elapsed = (Date.now() - this.startTime) / 1000.0;
        const angleY = elapsed * 0.5;
        const angleX = elapsed * 0.3;

        // Update transform
        const tm = this.engine.getTransformManager();
        const instance = tm.getInstance(this.cubeEntity);

        const rotation = Filament.mat4.create();
        Filament.mat4.rotateY(rotation, rotation, angleY);
        Filament.mat4.rotateX(rotation, rotation, angleX);

        tm.setTransform(instance, rotation);

        // Render
        if (this.renderer.beginFrame(this.swapChain)) {
            this.renderer.render(this.view);
            this.renderer.endFrame();
        }
    }

    destroy() {
        if (this.engine) {
            this.engine.destroyTexture(this.texture);
            this.engine.destroyMaterialInstance(this.materialInstance);
            // ... destroy other resources
        }
    }
}

// Usage
const canvas = document.getElementById('canvas');
const app = new TexturedCube(canvas);

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
project(TexturedCube)

set(CMAKE_CXX_STANDARD 17)

find_package(filament REQUIRED)
find_package(glfw3 REQUIRED)

add_executable(textured-cube textured_cube.cpp)

target_link_libraries(textured-cube
    filament
    glfw
)

# Copy assets
add_custom_command(TARGET textured-cube POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/textured.filamat
        $<TARGET_FILE_DIR:textured-cube>/textured.filamat
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/texture.png
        $<TARGET_FILE_DIR:textured-cube>/texture.png
)
```

### 构建命令

```bash
# 桌面平台
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja textured-cube
./textured-cube

# Android
./gradlew assembleRelease
adb install app/build/outputs/apk/release/app-release.apk

# Web
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
emmake make
python3 -m http.server 8000
```

## 常见问题

### Q1: 纹理显示为黑色或白色

**原因**:
- 纹理未正确加载
- UV 坐标错误
- 材质参数未设置

**解决**:

```cpp
// 检查纹理创建
if (!texture) {
    std::cerr << "Texture creation failed\n";
}

// 验证 UV 范围 [0, 1]
// UV 坐标必须在 0.0 到 1.0 之间

// 确保设置了材质参数
materialInstance->setParameter("albedo", texture, sampler);
```

### Q2: 立方体面显示不正确

**原因**: 顶点顺序或法线方向错误

**解决**:

```cpp
// 禁用背面剔除测试
RenderableManager::Builder(1)
    .culling(false)  // 临时禁用
    // ...

// 确保索引顺序为逆时针 (CCW)
// 确保法线方向正确
```

### Q3: 深度测试问题

**原因**: 未启用深度缓冲

**解决**:

```cpp
// 启用深度预处理
view->setDepthPrepass(View::DepthPrepass::ENABLED);

// 确保清除深度缓冲
renderer->setClearOptions({
    .clearColor = {...},
    .clear = true,
    .discard = true
});
```

### Q4: 旋转动画不流畅

**原因**:
- 未使用 VSync
- 时间步长计算错误

**解决**:

```cpp
// 使用高精度时钟
auto now = std::chrono::high_resolution_clock::now();
float dt = std::chrono::duration<float>(now - lastTime).count();

// 基于实际时间更新，而非帧数
float angle = elapsed * angularVelocity;
```

## 性能优化

### 1. 纹理压缩

```cpp
// 使用压缩格式减少内存占用
Texture::Builder()
    .format(Texture::InternalFormat::ETC2_RGB8)  // Android
    .format(Texture::InternalFormat::BC1_RGB)    // Desktop
    .build(*engine);
```

### 2. Mipmap 生成

```cpp
// 使用 mipgen 工具生成 mipmap
// mipgen --compression=etc texture.png texture.ktx

// 或运行时生成
Texture::Builder()
    .levels(0xFF)  // 自动计算 mip 层级数
    .build(*engine);

texture->generateMipmaps(*engine);
```

### 3. 纹理采样优化

```cpp
// 使用三线性过滤
TextureSampler sampler(
    TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
    TextureSampler::MagFilter::LINEAR,
    TextureSampler::WrapMode::REPEAT
);
```

## 扩展练习

1. **天空盒**: 加载立方体贴图作为背景
2. **法线贴图**: 添加法线纹理增强细节
3. **多纹理**: 混合多个纹理(diffuse + specular)
4. **交互控制**: 鼠标/触摸旋转立方体
5. **光照**: 添加方向光或点光源

## 相关文档

- [./01-hello-triangle.md](./01-hello-triangle.md) - 前一个示例
- [./03-pbr-model.md](./03-pbr-model.md) - 下一个示例：PBR 模型
- [../material/02-texture-系统.md](../material/02-texture-系统.md) - 纹理系统详解
- [../engine/05-transform-管理.md](../engine/05-transform-管理.md) - TransformManager 详解
