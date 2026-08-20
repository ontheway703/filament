# Hello Triangle

## 概述

Hello Triangle 是学习 Filament 渲染引擎的第一个示例，展示如何创建最基本的三角形渲染。本示例涵盖：

- **Engine 初始化**：创建和配置 Filament Engine
- **几何数据**：设置顶点和索引缓冲区
- **材质创建**：编译和加载简单的着色器
- **渲染循环**：实现跨平台的渲染循环
- **资源管理**：正确释放所有 Filament 资源
- **平台适配**：支持 iOS、Android、Windows、macOS、Linux、Web

## 架构设计

### 组件关系

```
┌─────────────┐
│   Window    │ (Platform-specific)
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  SwapChain  │ (Connects window to engine)
└──────┬──────┘
       │
       ▼
┌─────────────┐
│   Engine    │ (Core rendering engine)
├─────────────┤
│  Renderer   │ (Manages rendering)
│    Scene    │ (Contains entities)
│    View     │ (Camera + Scene)
│   Camera    │ (Projection)
└─────────────┘
       │
       ▼
┌─────────────┐
│  Renderable │ (Triangle geometry)
├─────────────┤
│  Material   │ (Shader)
│ VertexBuffer│ (Positions, colors)
│ IndexBuffer │ (Triangle indices)
└─────────────┘
```

### 渲染流程

```
1. Initialize Engine
   ↓
2. Create SwapChain (connect to window)
   ↓
3. Create Scene, View, Camera
   ↓
4. Build Triangle Geometry
   - Create VertexBuffer (positions, colors)
   - Create IndexBuffer (0, 1, 2)
   ↓
5. Compile Material
   - Write .mat file
   - Run matc compiler
   - Load .filamat binary
   ↓
6. Create Entity + Renderable Component
   ↓
7. Render Loop:
   - Begin frame
   - Render view
   - End frame and present
   ↓
8. Cleanup (reverse order of creation)
```

## 完整实现 (C++)

### 基础版本 (跨平台)

```cpp
// hello_triangle.cpp
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
#include <utils/EntityManager.h>

#include <GLFW/glfw3.h>  // For window management

#include <iostream>
#include <fstream>
#include <vector>

using namespace filament;
using namespace utils;

// Triangle vertices (position + color)
static const float TRIANGLE_VERTICES[] = {
    // positions (x, y)    colors (RGB)
    -0.5f, -0.5f,          1.0f, 0.0f, 0.0f,  // bottom-left (red)
     0.5f, -0.5f,          0.0f, 1.0f, 0.0f,  // bottom-right (green)
     0.0f,  0.5f,          0.0f, 0.0f, 1.0f   // top (blue)
};

static const uint16_t TRIANGLE_INDICES[] = { 0, 1, 2 };

class HelloTriangle {
public:
    HelloTriangle() = default;
    ~HelloTriangle() { cleanup(); }

    bool initialize(void* nativeWindow) {
        // 1. Create engine with default backend
        m_engine = Engine::create(Engine::Backend::DEFAULT);
        if (!m_engine) {
            std::cerr << "Failed to create Filament engine\n";
            return false;
        }

        // 2. Create swap chain
        m_swapChain = m_engine->createSwapChain(nativeWindow);
        if (!m_swapChain) {
            std::cerr << "Failed to create swap chain\n";
            return false;
        }

        // 3. Create renderer
        m_renderer = m_engine->createRenderer();
        m_renderer->setClearOptions({
            .clearColor = {0.1f, 0.125f, 0.25f, 1.0f},  // Dark blue background
            .clear = true
        });

        // 4. Create scene
        m_scene = m_engine->createScene();

        // 5. Create view
        m_view = m_engine->createView();
        m_view->setScene(m_scene);
        m_view->setViewport({0, 0, 1024, 768});

        // 6. Create camera
        m_camera = m_engine->createCamera(EntityManager::get().create());
        m_camera->setProjection(Camera::Projection::ORTHO,
            -1.0, 1.0,  // left, right
            -1.0, 1.0,  // bottom, top
            0.0, 1.0);  // near, far
        m_view->setCamera(m_camera);

        // 7. Create geometry
        if (!createTriangleGeometry()) {
            std::cerr << "Failed to create triangle geometry\n";
            return false;
        }

        // 8. Load material
        if (!loadMaterial()) {
            std::cerr << "Failed to load material\n";
            return false;
        }

        // 9. Create renderable entity
        if (!createRenderable()) {
            std::cerr << "Failed to create renderable\n";
            return false;
        }

        return true;
    }

    void render() {
        if (!m_renderer || !m_swapChain || !m_view) return;

        // Begin frame
        if (m_renderer->beginFrame(m_swapChain)) {
            // Render the view
            m_renderer->render(m_view);

            // End frame and present
            m_renderer->endFrame();
        }
    }

    void cleanup() {
        // Destroy resources in reverse order
        if (m_engine) {
            if (m_triangleEntity) {
                m_scene->remove(m_triangleEntity);
                m_engine->destroy(m_triangleEntity);
            }
            if (m_vertexBuffer) m_engine->destroy(m_vertexBuffer);
            if (m_indexBuffer) m_engine->destroy(m_indexBuffer);
            if (m_material) m_engine->destroy(m_material);
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
    bool createTriangleGeometry() {
        // Create vertex buffer
        m_vertexBuffer = VertexBuffer::Builder()
            .vertexCount(3)
            .bufferCount(1)
            .attribute(VertexAttribute::POSITION, 0,
                      VertexBuffer::AttributeType::FLOAT2, 0, 20)  // 2 floats, stride 20
            .attribute(VertexAttribute::COLOR, 0,
                      VertexBuffer::AttributeType::FLOAT3, 8, 20)  // 3 floats, offset 8, stride 20
            .build(*m_engine);

        if (!m_vertexBuffer) return false;

        // Upload vertex data
        m_vertexBuffer->setBufferAt(*m_engine, 0,
            VertexBuffer::BufferDescriptor(
                TRIANGLE_VERTICES,
                sizeof(TRIANGLE_VERTICES),
                nullptr  // No callback needed
            )
        );

        // Create index buffer
        m_indexBuffer = IndexBuffer::Builder()
            .indexCount(3)
            .bufferType(IndexBuffer::IndexType::USHORT)
            .build(*m_engine);

        if (!m_indexBuffer) return false;

        // Upload index data
        m_indexBuffer->setBuffer(*m_engine,
            IndexBuffer::BufferDescriptor(
                TRIANGLE_INDICES,
                sizeof(TRIANGLE_INDICES),
                nullptr
            )
        );

        return true;
    }

    bool loadMaterial() {
        // Load precompiled material from file
        std::ifstream file("simple_color.filamat", std::ios::binary);
        if (!file) {
            std::cerr << "Failed to open simple_color.filamat\n";
            return false;
        }

        // Read entire file into buffer
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(size);
        file.read(reinterpret_cast<char*>(buffer.data()), size);

        // Create material from binary data
        m_material = Material::Builder()
            .package(buffer.data(), buffer.size())
            .build(*m_engine);

        return m_material != nullptr;
    }

    bool createRenderable() {
        // Create entity
        m_triangleEntity = EntityManager::get().create();

        // Build renderable component
        RenderableManager::Builder(1)  // 1 primitive
            .boundingBox({{-1, -1, -1}, {1, 1, 1}})
            .material(0, m_material->getDefaultInstance())
            .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,
                     m_vertexBuffer, m_indexBuffer, 0, 3)
            .culling(false)  // Disable culling for this example
            .receiveShadows(false)
            .castShadows(false)
            .build(*m_engine, m_triangleEntity);

        // Add to scene
        m_scene->addEntity(m_triangleEntity);

        return true;
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
    Entity m_triangleEntity;
};

int main(int argc, char** argv) {
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }

    // Create window
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // We're using Filament, not OpenGL directly
    GLFWwindow* window = glfwCreateWindow(1024, 768, "Hello Triangle", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return -1;
    }

    // Get native window handle (platform-specific)
    void* nativeWindow = nullptr;
#if defined(__APPLE__)
    nativeWindow = glfwGetCocoaWindow(window);
#elif defined(_WIN32)
    nativeWindow = glfwGetWin32Window(window);
#elif defined(__linux__)
    nativeWindow = (void*)(uintptr_t)glfwGetX11Window(window);
#endif

    // Initialize Filament
    HelloTriangle app;
    if (!app.initialize(nativeWindow)) {
        std::cerr << "Failed to initialize application\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        app.render();
        glfwPollEvents();
    }

    // Cleanup
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
```

### 材质定义文件

创建 `simple_color.mat`:

```glsl
material {
    name : SimpleColor,
    shadingModel : unlit,
    vertexDomain : device,

    requires : [
        color
    ]
}

vertex {
    void materialVertex(inout MaterialVertexInputs material) {
        // Position is already in device coordinates
    }
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        material.baseColor = getColor();
    }
}
```

编译材质：

```bash
# 编译为所有平台
matc -a all -o simple_color.filamat simple_color.mat

# 或指定特定平台
matc -a opengl -a vulkan -a metal -o simple_color.filamat simple_color.mat
```

## iOS 实现

### Objective-C++ 版本

```objc
// TriangleViewController.mm
#import <UIKit/UIKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>

@interface TriangleViewController : UIViewController {
    filament::Engine* _engine;
    filament::SwapChain* _swapChain;
    filament::Renderer* _renderer;
    filament::Scene* _scene;
    filament::View* _view;
    filament::Camera* _camera;

    CAMetalLayer* _metalLayer;
    CADisplayLink* _displayLink;
}
@end

@implementation TriangleViewController

- (void)viewDidLoad {
    [super viewDidLoad];

    // Create Metal layer
    _metalLayer = [CAMetalLayer layer];
    _metalLayer.frame = self.view.bounds;
    _metalLayer.device = MTLCreateSystemDefaultDevice();
    _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    [self.view.layer addSublayer:_metalLayer];

    // Initialize Filament
    [self setupFilament];

    // Start render loop
    _displayLink = [CADisplayLink displayLinkWithTarget:self
                                               selector:@selector(render:)];
    [_displayLink addToRunLoop:[NSRunLoop currentRunLoop]
                       forMode:NSRunLoopCommonModes];
}

- (void)setupFilament {
    // Create engine with Metal backend
    _engine = filament::Engine::create(filament::Engine::Backend::METAL);

    // Create swap chain from Metal layer
    void* nativeLayer = (__bridge void*)_metalLayer;
    _swapChain = _engine->createSwapChain(nativeLayer);

    // Create renderer
    _renderer = _engine->createRenderer();
    _renderer->setClearOptions({
        .clearColor = {0.1f, 0.125f, 0.25f, 1.0f},
        .clear = true
    });

    // Create scene and view
    _scene = _engine->createScene();
    _view = _engine->createView();
    _view->setScene(_scene);

    CGRect bounds = self.view.bounds;
    _view->setViewport({0, 0,
        (uint32_t)bounds.size.width,
        (uint32_t)bounds.size.height});

    // Create camera
    using namespace filament;
    _camera = _engine->createCamera(utils::EntityManager::get().create());
    _camera->setProjection(Camera::Projection::ORTHO,
        -1.0, 1.0, -1.0, 1.0, 0.0, 1.0);
    _view->setCamera(_camera);

    // TODO: Create triangle geometry and material
}

- (void)render:(CADisplayLink*)displayLink {
    if (_renderer->beginFrame(_swapChain)) {
        _renderer->render(_view);
        _renderer->endFrame();
    }
}

- (void)dealloc {
    [_displayLink invalidate];

    // Cleanup Filament resources
    if (_engine) {
        _engine->destroy(_view);
        _engine->destroy(_scene);

        auto entity = _camera->getEntity();
        _engine->destroyCameraComponent(entity);
        utils::EntityManager::get().destroy(entity);

        _engine->destroy(_renderer);
        _engine->destroy(_swapChain);
        filament::Engine::destroy(&_engine);
    }
}

@end
```

### Swift 版本

```swift
// TriangleView.swift
import UIKit
import Metal
import QuartzCore

class TriangleView: UIView {
    private var metalLayer: CAMetalLayer!
    private var displayLink: CADisplayLink?

    // Filament objects (using unsafe pointers)
    private var engine: OpaquePointer?
    private var swapChain: OpaquePointer?
    private var renderer: OpaquePointer?
    private var scene: OpaquePointer?
    private var view: OpaquePointer?
    private var camera: OpaquePointer?

    override init(frame: CGRect) {
        super.init(frame: frame)
        setupMetal()
        setupFilament()
        startRenderLoop()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setupMetal()
        setupFilament()
        startRenderLoop()
    }

    private func setupMetal() {
        metalLayer = CAMetalLayer()
        metalLayer.frame = bounds
        metalLayer.device = MTLCreateSystemDefaultDevice()
        metalLayer.pixelFormat = .bgra8Unorm
        layer.addSublayer(metalLayer)
    }

    private func setupFilament() {
        // Create engine (using C wrapper or Objective-C bridge)
        engine = FilamentEngine_create(FilamentBackendMetal)

        // Create swap chain
        let layerPtr = Unmanaged.passUnretained(metalLayer).toOpaque()
        swapChain = FilamentEngine_createSwapChain(engine, layerPtr)

        // Create renderer, scene, view, camera
        renderer = FilamentEngine_createRenderer(engine)
        scene = FilamentEngine_createScene(engine)
        view = FilamentEngine_createView(engine)

        FilamentView_setScene(view, scene)
        FilamentView_setViewport(view, 0, 0,
            UInt32(bounds.width), UInt32(bounds.height))

        // TODO: Create triangle
    }

    private func startRenderLoop() {
        displayLink = CADisplayLink(target: self, selector: #selector(render))
        displayLink?.add(to: .current, forMode: .common)
    }

    @objc private func render() {
        guard let renderer = renderer,
              let swapChain = swapChain,
              let view = view else { return }

        if FilamentRenderer_beginFrame(renderer, swapChain) {
            FilamentRenderer_render(renderer, view)
            FilamentRenderer_endFrame(renderer)
        }
    }

    deinit {
        displayLink?.invalidate()
        // Cleanup Filament resources
        if let engine = engine {
            FilamentEngine_destroy(engine)
        }
    }
}
```

## Android 实现

### Kotlin 版本

```kotlin
// TriangleActivity.kt
package com.example.filament.hellotriangle

import android.os.Bundle
import android.view.Choreographer
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.appcompat.app.AppCompatActivity
import com.google.android.filament.*
import com.google.android.filament.android.UiHelper

class TriangleActivity : AppCompatActivity() {
    private lateinit var surfaceView: SurfaceView
    private lateinit var uiHelper: UiHelper
    private lateinit var choreographer: Choreographer

    private var engine: Engine? = null
    private var renderer: Renderer? = null
    private var scene: Scene? = null
    private var view: View? = null
    private var camera: Camera? = null
    private var swapChain: SwapChain? = null

    private var vertexBuffer: VertexBuffer? = null
    private var indexBuffer: IndexBuffer? = null
    private var material: Material? = null
    @Entity private var triangleEntity = 0

    private val frameCallback = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            choreographer.postFrameCallback(this)
            render()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        surfaceView = SurfaceView(this)
        setContentView(surfaceView)

        choreographer = Choreographer.getInstance()

        setupFilament()
        setupSurface()
    }

    private fun setupFilament() {
        // Create engine
        engine = Engine.create()

        // Create renderer
        renderer = engine!!.createRenderer()
        renderer!!.clearOptions = Renderer.ClearOptions().apply {
            clear = true
            clearColor = floatArrayOf(0.1f, 0.125f, 0.25f, 1.0f)
        }

        // Create scene
        scene = engine!!.createScene()

        // Create view
        view = engine!!.createView()
        view!!.scene = scene

        // Create camera
        camera = engine!!.createCamera(engine!!.entityManager.create()).apply {
            setProjection(
                Camera.Projection.ORTHO,
                -1.0, 1.0,  // left, right
                -1.0, 1.0,  // bottom, top
                0.0, 1.0    // near, far
            )
        }
        view!!.camera = camera

        // Create triangle
        createTriangle()
    }

    private fun createTriangle() {
        // Vertex data
        val vertices = floatArrayOf(
            // positions          colors
            -0.5f, -0.5f,        1.0f, 0.0f, 0.0f,  // red
             0.5f, -0.5f,        0.0f, 1.0f, 0.0f,  // green
             0.0f,  0.5f,        0.0f, 0.0f, 1.0f   // blue
        )

        val indices = shortArrayOf(0, 1, 2)

        // Create vertex buffer
        vertexBuffer = VertexBuffer.Builder()
            .vertexCount(3)
            .bufferCount(1)
            .attribute(
                VertexBuffer.VertexAttribute.POSITION,
                0,
                VertexBuffer.AttributeType.FLOAT2,
                0,
                20
            )
            .attribute(
                VertexBuffer.VertexAttribute.COLOR,
                0,
                VertexBuffer.AttributeType.FLOAT3,
                8,
                20
            )
            .build(engine!!)

        vertexBuffer!!.setBufferAt(
            engine!!,
            0,
            vertices.toFloatBuffer()
        )

        // Create index buffer
        indexBuffer = IndexBuffer.Builder()
            .indexCount(3)
            .bufferType(IndexBuffer.Builder.IndexType.USHORT)
            .build(engine!!)

        indexBuffer!!.setBuffer(engine!!, indices.toShortBuffer())

        // Load material from assets
        val materialBytes = assets.open("simple_color.filamat").readBytes()
        material = Material.Builder()
            .payload(materialBytes, materialBytes.size)
            .build(engine!!)

        // Create renderable
        triangleEntity = EntityManager.get().create()
        RenderableManager.Builder(1)
            .boundingBox(Box(-1f, -1f, -1f, 1f, 1f, 1f))
            .material(0, material!!.defaultInstance)
            .geometry(
                0,
                RenderableManager.PrimitiveType.TRIANGLES,
                vertexBuffer!!,
                indexBuffer!!,
                0,
                3
            )
            .culling(false)
            .build(engine!!, triangleEntity)

        scene!!.addEntity(triangleEntity)
    }

    private fun setupSurface() {
        uiHelper = UiHelper(UiHelper.ContextErrorPolicy.DONT_CHECK).apply {
            renderCallback = object : UiHelper.RendererCallback {
                override fun onNativeWindowChanged(surface: Surface) {
                    swapChain?.let { engine?.destroySwapChain(it) }
                    swapChain = engine?.createSwapChain(surface)
                }

                override fun onDetachedFromSurface() {
                    swapChain?.let { engine?.destroySwapChain(it) }
                    swapChain = null
                }

                override fun onResized(width: Int, height: Int) {
                    view?.viewport = Viewport(0, 0, width, height)
                }
            }

            attachTo(surfaceView)
        }
    }

    override fun onResume() {
        super.onResume()
        choreographer.postFrameCallback(frameCallback)
    }

    override fun onPause() {
        super.onPause()
        choreographer.removeFrameCallback(frameCallback)
    }

    override fun onDestroy() {
        super.onDestroy()

        // Cleanup
        choreographer.removeFrameCallback(frameCallback)
        uiHelper.detach()

        engine?.let { eng ->
            scene?.removeEntity(triangleEntity)
            eng.destroyEntity(triangleEntity)

            vertexBuffer?.let { eng.destroyVertexBuffer(it) }
            indexBuffer?.let { eng.destroyIndexBuffer(it) }
            material?.let { eng.destroyMaterial(it) }

            eng.destroyRenderer(renderer!!)
            eng.destroyView(view!!)
            eng.destroyScene(scene!!)
            eng.destroyCameraComponent(camera!!.entity)
            EntityManager.get().destroy(camera!!.entity)

            Engine.destroyEngine(eng)
        }
    }

    private fun render() {
        val eng = engine ?: return
        val rend = renderer ?: return
        val swap = swapChain ?: return
        val v = view ?: return

        if (rend.beginFrame(swap, frameCallback.frameTimeNanos)) {
            rend.render(v)
            rend.endFrame()
        }
    }
}

// Helper extensions
private fun FloatArray.toFloatBuffer(): java.nio.FloatBuffer {
    return java.nio.ByteBuffer.allocateDirect(this.size * 4)
        .order(java.nio.ByteOrder.nativeOrder())
        .asFloatBuffer()
        .put(this)
        .apply { rewind() }
}

private fun ShortArray.toShortBuffer(): java.nio.ShortBuffer {
    return java.nio.ByteBuffer.allocateDirect(this.size * 2)
        .order(java.nio.ByteOrder.nativeOrder())
        .asShortBuffer()
        .put(this)
        .apply { rewind() }
}
```

## Web 实现

### JavaScript 版本

```javascript
// hello-triangle.js
import * as Filament from 'filament';

class HelloTriangle {
    constructor(canvas) {
        this.canvas = canvas;
        this.engine = null;
        this.swapChain = null;
        this.renderer = null;
        this.scene = null;
        this.view = null;
        this.camera = null;
        this.vertexBuffer = null;
        this.indexBuffer = null;
        this.material = null;
        this.triangle = null;
    }

    async init() {
        // Initialize Filament
        await Filament.init(['filament.wasm'], () => {});

        // Create engine
        this.engine = Filament.Engine.create(this.canvas);

        // Create swap chain
        this.swapChain = this.engine.createSwapChain();

        // Create renderer
        this.renderer = this.engine.createRenderer();
        this.renderer.setClearOptions({
            clearColor: [0.1, 0.125, 0.25, 1.0],
            clear: true
        });

        // Create scene
        this.scene = this.engine.createScene();

        // Create view
        this.view = this.engine.createView();
        this.view.setScene(this.scene);
        this.view.setViewport([0, 0, this.canvas.width, this.canvas.height]);

        // Create camera
        const Fov = Filament.Camera$Fov;
        const cameraEntity = Filament.EntityManager.get().create();
        this.camera = this.engine.createCamera(cameraEntity);
        this.camera.setProjection(
            Filament.Camera$Projection.ORTHO,
            -1, 1,  // left, right
            -1, 1,  // bottom, top
            0, 1    // near, far
        );
        this.view.setCamera(this.camera);

        // Create triangle
        await this.createTriangle();

        console.log('Filament initialized successfully');
    }

    async createTriangle() {
        // Vertex data (interleaved positions and colors)
        const vertices = new Float32Array([
            // positions       colors
            -0.5, -0.5,       1.0, 0.0, 0.0,  // red
             0.5, -0.5,       0.0, 1.0, 0.0,  // green
             0.0,  0.5,       0.0, 0.0, 1.0   // blue
        ]);

        const indices = new Uint16Array([0, 1, 2]);

        // Create vertex buffer
        const VertexAttribute = Filament.VertexAttribute;
        const AttributeType = Filament.VertexBuffer$AttributeType;

        this.vertexBuffer = Filament.VertexBuffer.Builder()
            .vertexCount(3)
            .bufferCount(1)
            .attribute(VertexAttribute.POSITION, 0, AttributeType.FLOAT2, 0, 20)
            .attribute(VertexAttribute.COLOR, 0, AttributeType.FLOAT3, 8, 20)
            .build(this.engine);

        this.vertexBuffer.setBufferAt(this.engine, 0, vertices);

        // Create index buffer
        this.indexBuffer = Filament.IndexBuffer.Builder()
            .indexCount(3)
            .bufferType(Filament.IndexBuffer$IndexType.USHORT)
            .build(this.engine);

        this.indexBuffer.setBuffer(this.engine, indices);

        // Load material
        const response = await fetch('simple_color.filamat');
        const materialData = await response.arrayBuffer();

        this.material = this.engine.createMaterial(new Uint8Array(materialData));

        // Create entity and renderable
        const utils = Filament.EntityManager.get();
        this.triangle = utils.create();

        Filament.RenderableManager.Builder(1)
            .boundingBox({
                center: [0, 0, 0],
                halfExtent: [1, 1, 1]
            })
            .material(0, this.material.getDefaultInstance())
            .geometry(0, Filament.RenderableManager$PrimitiveType.TRIANGLES,
                     this.vertexBuffer, this.indexBuffer)
            .culling(false)
            .build(this.engine, this.triangle);

        this.scene.addEntity(this.triangle);
    }

    render() {
        if (this.renderer.beginFrame(this.swapChain)) {
            this.renderer.render(this.view);
            this.renderer.endFrame();
        }
    }

    resize(width, height) {
        this.canvas.width = width;
        this.canvas.height = height;
        this.view.setViewport([0, 0, width, height]);
    }

    destroy() {
        // Cleanup resources
        if (this.engine) {
            this.scene.removeEntity(this.triangle);
            this.engine.destroyEntity(this.triangle);

            this.engine.destroyVertexBuffer(this.vertexBuffer);
            this.engine.destroyIndexBuffer(this.indexBuffer);
            this.engine.destroyMaterial(this.material);

            this.engine.destroyRenderer(this.renderer);
            this.engine.destroyView(this.view);
            this.engine.destroyScene(this.scene);

            const cameraEntity = this.camera.getEntity();
            this.engine.destroyCameraComponent(cameraEntity);
            Filament.EntityManager.get().destroy(cameraEntity);

            this.engine.destroySwapChain(this.swapChain);
            this.engine.destroy();
        }
    }
}

// Usage
const canvas = document.getElementById('canvas');
const app = new HelloTriangle(canvas);

app.init().then(() => {
    // Start render loop
    function animate() {
        app.render();
        requestAnimationFrame(animate);
    }
    animate();
});

// Handle window resize
window.addEventListener('resize', () => {
    app.resize(window.innerWidth, window.innerHeight);
});
```

### HTML 页面

```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Filament Hello Triangle</title>
    <style>
        body {
            margin: 0;
            overflow: hidden;
            background-color: #1a1f33;
        }
        #canvas {
            width: 100vw;
            height: 100vh;
            display: block;
        }
    </style>
</head>
<body>
    <canvas id="canvas" width="1024" height="768"></canvas>
    <script type="module" src="hello-triangle.js"></script>
</body>
</html>
```

## 构建和运行

### 桌面平台 (macOS/Linux/Windows)

```bash
# 创建构建目录
mkdir -p build && cd build

# CMake 配置
cmake .. -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DFILAMENT_SUPPORTS_OPENGL=ON \
    -DFILAMENT_SUPPORTS_VULKAN=ON

# 构建
ninja hello-triangle

# 运行
./hello-triangle
```

**CMakeLists.txt**:

```cmake
cmake_minimum_required(VERSION 3.19)
project(HelloTriangle)

set(CMAKE_CXX_STANDARD 17)

# 查找 Filament
find_package(filament REQUIRED)
find_package(glfw3 REQUIRED)

# 可执行文件
add_executable(hello-triangle hello_triangle.cpp)

# 链接库
target_link_libraries(hello-triangle
    filament
    glfw
)

# 复制材质文件
add_custom_command(TARGET hello-triangle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/simple_color.filamat
        $<TARGET_FILE_DIR:hello-triangle>/simple_color.filamat
)
```

### iOS

```bash
# 使用 Xcode
xcodebuild -project HelloTriangle.xcodeproj \
    -scheme HelloTriangle \
    -configuration Release \
    -sdk iphoneos

# 或使用 CMake (需要 iOS toolchain)
cmake .. -G Xcode \
    -DCMAKE_TOOLCHAIN_FILE=../ios.toolchain.cmake \
    -DPLATFORM=OS64

xcodebuild -project HelloTriangle.xcodeproj -scheme ALL_BUILD
```

### Android

```bash
# 使用 Gradle
./gradlew assembleRelease

# 或使用 Android Studio
# 打开项目 -> Build -> Make Project

# 安装到设备
adb install -r app/build/outputs/apk/release/app-release.apk

# 运行
adb shell am start -n com.example.filament.hellotriangle/.TriangleActivity
```

**build.gradle** (app):

```gradle
android {
    compileSdk 33

    defaultConfig {
        applicationId "com.example.filament.hellotriangle"
        minSdk 24
        targetSdk 33
        versionCode 1
        versionName "1.0"

        ndk {
            abiFilters 'arm64-v8a', 'armeabi-v7a', 'x86_64'
        }
    }
}

dependencies {
    implementation 'com.google.android.filament:filament-android:1.46.0'
    implementation 'com.google.android.filament:filament-utils-android:1.46.0'
}
```

### Web

```bash
# 使用 Emscripten 构建
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
emmake make

# 启动本地服务器
python3 -m http.server 8000

# 或使用 live-server
npx live-server

# 访问 http://localhost:8000
```

## 调试技巧

### 1. 检查渲染结果

```cpp
// 启用调试输出
#include <utils/Log.h>
utils::slog.i << "Triangle entity: " << triangleEntity << utils::io::endl;

// 验证缓冲区大小
utils::slog.i << "Vertex count: " << vertexBuffer->getVertexCount()
              << utils::io::endl;
utils::slog.i << "Index count: " << indexBuffer->getIndexCount()
              << utils::io::endl;
```

### 2. 验证着色器编译

```bash
# 查看编译后的材质信息
matinfo simple_color.filamat

# 输出示例：
# Name: SimpleColor
# Shading model: unlit
# Variants: 2
# Material domains: surface
```

### 3. 性能分析

```cpp
// 启用帧率显示
#include <utils/FrameRateTracker.h>

utils::FrameRateTracker tracker;
tracker.beginFrame();
// ... render ...
tracker.endFrame();

if (tracker.getFrameCount() % 60 == 0) {
    utils::slog.i << "FPS: " << tracker.getAverageFPS() << utils::io::endl;
}
```

### 4. Renderdoc 集成 (桌面)

```cpp
// 触发 RenderDoc 捕获
#if defined(RENDERDOC_INTEGRATION)
    #include <renderdoc_app.h>
    RENDERDOC_API_1_1_2* rdoc_api = nullptr;

    if (rdoc_api && keyPressed) {
        rdoc_api->StartFrameCapture(nullptr, nullptr);
        render();
        rdoc_api->EndFrameCapture(nullptr, nullptr);
    }
#endif
```

## 常见问题

### Q1: 黑屏，没有看到三角形

**可能原因**:
- 材质未正确加载
- 顶点坐标超出裁剪空间 (-1 到 1)
- SwapChain 未正确创建
- 相机投影矩阵不正确

**解决方案**:

```cpp
// 1. 检查材质加载
if (!material) {
    utils::slog.e << "Material is null!" << utils::io::endl;
}

// 2. 验证顶点在裁剪空间内
// 确保顶点 x, y 在 [-1, 1] 范围

// 3. 检查 SwapChain
if (!swapChain) {
    utils::slog.e << "SwapChain creation failed!" << utils::io::endl;
}

// 4. 禁用背面剔除测试
RenderableManager::Builder(1)
    .culling(false)  // 禁用剔除
    // ...
```

### Q2: 编译材质失败

**错误示例**:
```
ERROR: simple_color.mat:3: Unknown shading model
```

**解决方案**:

```bash
# 检查 matc 版本与 Filament 运行时版本一致
matc --version

# 使用正确的语法
material {
    name : SimpleColor,
    shadingModel : unlit,  # 使用 unlit 而不是 unlighted
    vertexDomain : device,
}
```

### Q3: Android 上崩溃

**可能原因**:
- 材质文件未包含在 assets
- 未在主线程创建 Engine
- SwapChain 在 Surface 就绪前创建

**解决方案**:

```kotlin
// 1. 确保材质在 assets 目录
// app/src/main/assets/simple_color.filamat

// 2. 在主线程创建引擎
runOnUiThread {
    engine = Engine.create()
}

// 3. 等待 Surface 就绪
uiHelper.renderCallback = object : UiHelper.RendererCallback {
    override fun onNativeWindowChanged(surface: Surface) {
        // 在这里创建 SwapChain
        swapChain = engine?.createSwapChain(surface)
    }
}
```

### Q4: Web 上加载卡住

**可能原因**:
- WASM 文件路径错误
- CORS 问题
- 材质文件未找到

**解决方案**:

```javascript
// 1. 检查文件路径
Filament.init(['./filament.wasm'], () => {
    console.log('Filament loaded');
});

// 2. 使用本地服务器 (避免 CORS)
# 不要直接用 file:// 打开，使用:
python3 -m http.server 8000

// 3. 添加错误处理
fetch('simple_color.filamat')
    .then(response => {
        if (!response.ok) {
            throw new Error('Material not found: ' + response.status);
        }
        return response.arrayBuffer();
    })
    .catch(error => console.error('Failed to load material:', error));
```

### Q5: iOS 上帧率低

**可能原因**:
- 未使用 CVDisplayLink/CADisplayLink
- 未启用 Metal backend
- ProMotion 设备未设置正确刷新率

**解决方案**:

```objc
// 1. 使用 CADisplayLink (自动匹配设备刷新率)
_displayLink = [CADisplayLink displayLinkWithTarget:self
                                           selector:@selector(render:)];
[_displayLink addToRunLoop:[NSRunLoop mainRunLoop]
                   forMode:NSRunLoopCommonModes];

// 2. 确认使用 Metal backend
_engine = filament::Engine::create(filament::Engine::Backend::METAL);

// 3. ProMotion 120Hz 支持 (iOS 13.0+)
if (@available(iOS 13.0, *)) {
    _displayLink.preferredFrameRateRange = CAFrameRateRangeMake(80, 120, 120);
}
```

## 性能优化

### 1. 减少资源创建开销

```cpp
// 不好的做法：每帧创建
void render() {
    Material* mat = loadMaterial();  // ❌ 不要这样做
    // ...
}

// 好的做法：初始化时创建一次
void initialize() {
    m_material = loadMaterial();  // ✅ 只创建一次
}
```

### 2. 使用合适的缓冲区类型

```cpp
// 对于静态几何体，使用 STATIC
VertexBuffer::Builder()
    .vertexCount(3)
    .bufferCount(1)
    // ... attributes ...
    .build(*m_engine);

// 对于动态更新的几何体
// (目前示例不需要，但如果顶点需要频繁更新)
```

### 3. 批处理渲染

```cpp
// 如果有多个三角形，合并为一个 VertexBuffer
const int triangleCount = 10;
VertexBuffer::Builder()
    .vertexCount(triangleCount * 3)
    // ...
    .build(*m_engine);

RenderableManager::Builder(1)
    .geometry(0, PrimitiveType::TRIANGLES,
             vertexBuffer, indexBuffer, 0, triangleCount * 3)
    // ...
```

## 扩展练习

1. **添加动画**: 使用 TransformManager 旋转三角形
2. **多个三角形**: 创建多个实体，不同颜色和位置
3. **纹理映射**: 替换顶点颜色为纹理坐标
4. **交互**: 响应鼠标/触摸输入移动三角形
5. **3D 透视**: 将正交投影改为透视投影，添加 Z 坐标

## 相关文档

- [../engine/01-engine-初始化.md](../engine/01-engine-初始化.md) - Engine 创建详解
- [../engine/03-scene-管理.md](../engine/03-scene-管理.md) - Scene 和 Entity 系统
- [../material/01-material-系统.md](../material/01-material-系统.md) - 材质编写和编译
- [../platforms/08-swapchain-创建.md](../platforms/08-swapchain-创建.md) - SwapChain 详解
- [./02-textured-cube.md](./02-textured-cube.md) - 下一个示例：纹理立方体
