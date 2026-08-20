# glTF Viewer

## 概述

glTF Viewer 是一个完整的 glTF 2.0 模型查看器，展示如何使用 Filament 的 gltfio 库加载和渲染标准 glTF 模型。本示例涵盖:

- **gltfio 库使用**: 完整的 glTF 2.0 加载流程
- **资源管理**: 纹理、材质、网格的异步加载
- **动画系统**: 播放 glTF 动画
- **相机控制**: 轨道相机交互
- **UI 集成**: ImGui 调试界面
- **性能优化**: 异步资源加载、LOD 管理

## glTF 2.0 规范

### 文件结构

```
glTF 2.0 文件格式:

1. .gltf (JSON)
   - 场景层级结构
   - 网格、材质、纹理引用
   - 动画、蒙皮数据
   - 外部资源引用 (.bin, 图片)

2. .glb (二进制)
   - 自包含单文件
   - JSON + 二进制数据
   - 更高效的加载

3. 资源组织:
   scenes/          场景列表
   nodes/           节点层级
   meshes/          网格数据
   materials/       PBR 材质
   textures/        纹理引用
   images/          图片数据
   animations/      动画数据
   skins/           骨骼蒙皮
```

### 坐标系统

```
glTF 使用右手坐标系:
- +X: 右
- +Y: 上
- +Z: 向外 (面向观察者)

Filament 使用右手坐标系 (相同):
- +X: 右
- +Y: 上
- +Z: 向外

因此无需坐标转换
```

## 完整实现 (C++)

### 主程序

```cpp
// gltf_viewer.cpp
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/IndirectLight.h>
#include <filament/Skybox.h>

#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/Animator.h>

#include <utils/EntityManager.h>
#include <utils/Path.h>

#include <camutils/Manipulator.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>

#include <GLFW/glfw3.h>

#include <iostream>
#include <memory>
#include <vector>

using namespace filament;
using namespace filament::math;
using namespace gltfio;
using namespace utils;

class OrbitCamera {
public:
    OrbitCamera(int width, int height) {
        m_manipulator = camutils::Manipulator<float>::Builder()
            .targetPosition(0, 0, 0)
            .orbitHomePosition(0, 0, 5)
            .viewport(width, height)
            .build(camutils::Mode::ORBIT);
    }

    void setViewport(int width, int height) {
        m_manipulator->setViewport(width, height);
    }

    void grabBegin(int x, int y, bool pan) {
        m_manipulator->grabBegin(x, y, pan);
    }

    void grabUpdate(int x, int y) {
        m_manipulator->grabUpdate(x, y);
    }

    void grabEnd() {
        m_manipulator->grabEnd();
    }

    void scroll(float delta) {
        m_manipulator->scroll(0, 0, delta);
    }

    mat4f getViewMatrix() const {
        auto eye = m_manipulator->getLookAt();
        auto center = m_manipulator->getTargetPosition();
        auto up = m_manipulator->getUpVector();
        return mat4f::lookAt(eye, center, up);
    }

    float3 getEyePosition() const {
        return m_manipulator->getLookAt();
    }

private:
    std::unique_ptr<camutils::Manipulator<float>> m_manipulator;
};

class GLTFViewer {
public:
    GLTFViewer() = default;
    ~GLTFViewer() { cleanup(); }

    bool initialize(GLFWwindow* window, int width, int height) {
        m_window = window;
        m_width = width;
        m_height = height;

        // Create engine
        m_engine = Engine::create(Engine::Backend::DEFAULT);
        if (!m_engine) return false;

        // Get native window
        void* nativeWindow = nullptr;
#if defined(__APPLE__)
        nativeWindow = glfwGetCocoaWindow(window);
#elif defined(_WIN32)
        nativeWindow = glfwGetWin32Window(window);
#elif defined(__linux__)
        nativeWindow = (void*)(uintptr_t)glfwGetX11Window(window);
#endif

        // Create swap chain
        m_swapChain = m_engine->createSwapChain(nativeWindow);
        if (!m_swapChain) return false;

        // Create renderer
        m_renderer = m_engine->createRenderer();

        // Create scene
        m_scene = m_engine->createScene();

        // Create view
        m_view = m_engine->createView();
        m_view->setScene(m_scene);
        m_view->setViewport({0, 0, (uint32_t)width, (uint32_t)height});
        m_view->setAntiAliasing(View::AntiAliasing::FXAA);
        m_view->setPostProcessingEnabled(true);

        // Create camera
        m_cameraEntity = EntityManager::get().create();
        m_camera = m_engine->createCamera(m_cameraEntity);

        const float aspect = (float)width / (float)height;
        m_camera->setProjection(Camera::Projection::PERSPECTIVE,
            45.0, aspect, 0.1, 1000.0);

        m_view->setCamera(m_camera);

        // Create orbit camera controller
        m_orbitCamera = std::make_unique<OrbitCamera>(width, height);

        // Setup gltfio
        m_assetLoader = AssetLoader::create({m_engine});
        if (!m_assetLoader) return false;

        m_resourceLoader = new ResourceLoader({
            .engine = m_engine,
            .normalizeSkinningWeights = true,
            .recomputeBoundingBoxes = false
        });

        // Load IBL
        if (!loadIBL("environment_ibl.ktx")) {
            std::cerr << "Warning: Failed to load IBL\n";
        }

        // Setup ImGui
        setupImGui();

        return true;
    }

    bool loadModel(const std::string& path) {
        // Read glTF file
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            std::cerr << "Failed to open: " << path << "\n";
            return false;
        }

        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> data(size);
        file.read(reinterpret_cast<char*>(data.data()), size);

        // Destroy previous asset
        if (m_asset) {
            m_scene->removeEntities(m_asset->getEntities(), m_asset->getEntityCount());
            m_assetLoader->destroyAsset(m_asset);
            m_asset = nullptr;
            m_animator = nullptr;
        }

        // Load asset
        m_asset = m_assetLoader->createAsset(data.data(), data.size());
        if (!m_asset) {
            std::cerr << "Failed to parse glTF\n";
            return false;
        }

        // Get base path for external resources
        utils::Path basePath = utils::Path(path).getParent();

        // Load resources asynchronously
        m_resourceLoader->asyncBeginLoad(m_asset);

        // Load external resources
        for (size_t i = 0; i < m_asset->getResourceUriCount(); ++i) {
            const char* uri = m_asset->getResourceUri(i);
            if (uri && uri[0] != '\0') {
                utils::Path resourcePath = basePath + uri;

                std::ifstream resourceFile(resourcePath.c_str(), std::ios::binary);
                if (resourceFile) {
                    resourceFile.seekg(0, std::ios::end);
                    size_t resourceSize = resourceFile.tellg();
                    resourceFile.seekg(0, std::ios::beg);

                    auto buffer = new uint8_t[resourceSize];
                    resourceFile.read(reinterpret_cast<char*>(buffer), resourceSize);

                    m_resourceLoader->addResourceData(uri,
                        ResourceLoader::BufferDescriptor(buffer, resourceSize,
                            [](void* buffer, size_t size, void* user) {
                                delete[] static_cast<uint8_t*>(buffer);
                            }
                        )
                    );
                }
            }
        }

        m_resourceLoader->asyncUpdateLoad();

        // Add entities to scene
        m_scene->addEntities(m_asset->getEntities(), m_asset->getEntityCount());

        // Create animator if animations exist
        if (m_asset->getAnimationCount() > 0) {
            m_animator = m_asset->getInstance()->getAnimator();
            m_currentAnimation = 0;
            m_animator->applyAnimation(0, 0.0f);
        }

        // Fit camera to asset bounds
        fitCameraToAsset();

        std::cout << "Loaded: " << path << "\n";
        std::cout << "  Entities: " << m_asset->getEntityCount() << "\n";
        std::cout << "  Animations: " << m_asset->getAnimationCount() << "\n";

        return true;
    }

    void render(float deltaTime) {
        if (!m_renderer || !m_swapChain || !m_view) return;

        // Update camera
        updateCamera();

        // Update animator
        if (m_animator && m_animationPlaying) {
            m_animationTime += deltaTime;

            float duration = m_animator->getAnimationDuration(m_currentAnimation);
            if (m_animationTime > duration) {
                m_animationTime = 0.0f;  // Loop
            }

            m_animator->applyAnimation(m_currentAnimation, m_animationTime);
            m_animator->updateBoneMatrices();
        }

        // Update resource loading
        if (m_resourceLoader) {
            m_resourceLoader->asyncUpdateLoad();
        }

        // Render ImGui
        renderImGui();

        // Render scene
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
            45.0, aspect, 0.1, 1000.0);

        m_orbitCamera->setViewport(width, height);
    }

    void onMouseButton(int button, int action, int mods) {
        if (ImGui::GetIO().WantCaptureMouse) return;

        double x, y;
        glfwGetCursorPos(m_window, &x, &y);

        if (action == GLFW_PRESS) {
            bool pan = (button == GLFW_MOUSE_BUTTON_MIDDLE) ||
                      (button == GLFW_MOUSE_BUTTON_LEFT && (mods & GLFW_MOD_SHIFT));
            m_orbitCamera->grabBegin((int)x, (int)y, pan);
        } else if (action == GLFW_RELEASE) {
            m_orbitCamera->grabEnd();
        }
    }

    void onMouseMove(double x, double y) {
        if (ImGui::GetIO().WantCaptureMouse) return;
        m_orbitCamera->grabUpdate((int)x, (int)y);
    }

    void onScroll(double xoffset, double yoffset) {
        if (ImGui::GetIO().WantCaptureMouse) return;
        m_orbitCamera->scroll((float)yoffset);
    }

    void cleanup() {
        if (m_engine) {
            if (m_asset) {
                m_scene->removeEntities(m_asset->getEntities(), m_asset->getEntityCount());
                m_assetLoader->destroyAsset(m_asset);
            }

            delete m_resourceLoader;
            AssetLoader::destroy(&m_assetLoader);

            if (m_indirectLight) m_engine->destroy(m_indirectLight);
            if (m_skybox) m_engine->destroy(m_skybox);
            if (m_iblTexture) m_engine->destroy(m_iblTexture);

            m_engine->destroy(m_cameraEntity);
            m_engine->destroy(m_view);
            m_engine->destroy(m_scene);
            m_engine->destroy(m_renderer);
            m_engine->destroy(m_swapChain);

            Engine::destroy(&m_engine);
        }

        // Cleanup ImGui
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

private:
    bool loadIBL(const std::string& path) {
        // Load IBL texture (simplified - should parse KTX)
        m_iblTexture = Texture::Builder()
            .width(256)
            .height(256)
            .levels(8)
            .format(Texture::InternalFormat::RGBA16F)
            .sampler(Texture::Sampler::SAMPLER_CUBEMAP)
            .build(*m_engine);

        if (!m_iblTexture) return false;

        m_indirectLight = IndirectLight::Builder()
            .reflections(m_iblTexture)
            .intensity(30000.0f)
            .build(*m_engine);

        m_scene->setIndirectLight(m_indirectLight);

        m_skybox = Skybox::Builder()
            .environment(m_iblTexture)
            .build(*m_engine);

        m_scene->setSkybox(m_skybox);

        return true;
    }

    void fitCameraToAsset() {
        if (!m_asset) return;

        // Get asset bounds
        auto aabb = m_asset->getBoundingBox();
        float3 center = (aabb.min + aabb.max) * 0.5f;
        float3 extent = (aabb.max - aabb.min) * 0.5f;
        float radius = length(extent);

        // Position camera to fit the model
        float distance = radius / std::tan(radians(45.0f) * 0.5f) * 1.5f;

        // Update orbit camera
        // (This would require extending OrbitCamera with target/distance setters)
    }

    void updateCamera() {
        mat4f view = m_orbitCamera->getViewMatrix();
        m_camera->setModelMatrix(inverse(view));
    }

    void setupImGui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();

        ImGui_ImplGlfw_InitForOther(m_window, true);
        ImGui::StyleColorsDark();
    }

    void renderImGui() {
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("glTF Viewer");

        if (m_asset) {
            ImGui::Text("Entities: %zu", m_asset->getEntityCount());

            if (m_animator) {
                ImGui::Separator();
                ImGui::Text("Animations: %zu", m_asset->getAnimationCount());

                if (m_asset->getAnimationCount() > 0) {
                    ImGui::Checkbox("Play", &m_animationPlaying);

                    if (ImGui::SliderInt("Animation", &m_currentAnimation, 0,
                                        (int)m_asset->getAnimationCount() - 1)) {
                        m_animationTime = 0.0f;
                    }

                    float duration = m_animator->getAnimationDuration(m_currentAnimation);
                    ImGui::SliderFloat("Time", &m_animationTime, 0.0f, duration);
                }
            }
        } else {
            ImGui::Text("No model loaded");
            ImGui::Text("Drag & drop .gltf or .glb file");
        }

        ImGui::End();

        ImGui::Render();
        // Note: ImGui rendering to Filament requires additional integration
    }

    GLFWwindow* m_window = nullptr;
    Engine* m_engine = nullptr;
    SwapChain* m_swapChain = nullptr;
    Renderer* m_renderer = nullptr;
    Scene* m_scene = nullptr;
    View* m_view = nullptr;
    Camera* m_camera = nullptr;
    Entity m_cameraEntity;

    AssetLoader* m_assetLoader = nullptr;
    ResourceLoader* m_resourceLoader = nullptr;
    FilamentAsset* m_asset = nullptr;
    Animator* m_animator = nullptr;

    Texture* m_iblTexture = nullptr;
    IndirectLight* m_indirectLight = nullptr;
    Skybox* m_skybox = nullptr;

    std::unique_ptr<OrbitCamera> m_orbitCamera;

    int m_width = 0;
    int m_height = 0;

    int m_currentAnimation = 0;
    float m_animationTime = 0.0f;
    bool m_animationPlaying = true;
};

int main(int argc, char** argv) {
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720,
        "glTF Viewer", nullptr, nullptr);

    if (!window) {
        glfwTerminate();
        return -1;
    }

    GLTFViewer viewer;
    if (!viewer.initialize(window, 1280, 720)) {
        std::cerr << "Failed to initialize viewer\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // Load model from command line
    if (argc > 1) {
        viewer.loadModel(argv[1]);
    }

    // Setup callbacks
    glfwSetWindowUserPointer(window, &viewer);

    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int width, int height) {
        auto* v = static_cast<GLTFViewer*>(glfwGetWindowUserPointer(w));
        v->resize(width, height);
    });

    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int mods) {
        auto* v = static_cast<GLTFViewer*>(glfwGetWindowUserPointer(w));
        v->onMouseButton(button, action, mods);
    });

    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
        auto* v = static_cast<GLTFViewer*>(glfwGetWindowUserPointer(w));
        v->onMouseMove(x, y);
    });

    glfwSetScrollCallback(window, [](GLFWwindow* w, double x, double y) {
        auto* v = static_cast<GLTFViewer*>(glfwGetWindowUserPointer(w));
        v->onScroll(x, y);
    });

    // Main loop
    double lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        float deltaTime = (float)(currentTime - lastTime);
        lastTime = currentTime;

        viewer.render(deltaTime);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
```

## 简化版本 (使用 gltf_viewer 工具)

Filament 提供了预构建的 `gltf_viewer` 工具:

```bash
# 直接使用
gltf_viewer model.glb

# 指定 IBL 环境
gltf_viewer --ibl=environment model.glb

# 启用阴影
gltf_viewer --shadow model.glb

# 自动播放动画
gltf_viewer --auto-play model.glb
```

## Android 实现

### Kotlin 版本

```kotlin
// GLTFViewerActivity.kt
package com.example.filament.gltfviewer

import android.os.Bundle
import android.view.Choreographer
import android.view.GestureDetector
import android.view.MotionEvent
import androidx.appcompat.app.AppCompatActivity
import com.google.android.filament.*
import com.google.android.filament.gltfio.AssetLoader
import com.google.android.filament.gltfio.FilamentAsset
import com.google.android.filament.gltfio.ResourceLoader
import com.google.android.filament.gltfio.Animator

class GLTFViewerActivity : AppCompatActivity() {
    private var engine: Engine? = null
    private var assetLoader: AssetLoader? = null
    private var resourceLoader: ResourceLoader? = null
    private var asset: FilamentAsset? = null
    private var animator: Animator? = null

    private var animationIndex = 0
    private var animationTime = 0.0f

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setupFilament()
        loadModel("model.glb")
    }

    private fun setupFilament() {
        engine = Engine.create()

        assetLoader = AssetLoader(engine!!,
            MaterialProvider(engine!!),
            EntityManager.get())

        resourceLoader = ResourceLoader(engine!!,
            normalizeSkinningWeights = true)
    }

    private fun loadModel(path: String) {
        // Load glTF from assets
        val buffer = assets.open(path).readBytes()

        // Create asset
        asset = assetLoader!!.createAsset(buffer)
        if (asset == null) {
            android.util.Log.e("glTF", "Failed to load model")
            return
        }

        // Load resources
        resourceLoader!!.asyncBeginLoad(asset!!)

        // Load external resources
        for (i in 0 until asset!!.resourceUriCount) {
            val uri = asset!!.getResourceUri(i)
            if (uri != null && uri.isNotEmpty()) {
                try {
                    val resourceData = assets.open("model/$uri").readBytes()
                    resourceLoader!!.addResourceData(uri,
                        ByteBuffer.wrap(resourceData))
                } catch (e: Exception) {
                    android.util.Log.e("glTF", "Failed to load resource: $uri")
                }
            }
        }

        // Add to scene
        scene?.addEntities(asset!!.entities)

        // Setup animator
        if (asset!!.animationCount > 0) {
            animator = asset!!.instance.animator
            animator?.applyAnimation(0, 0.0f)
        }
    }

    private fun updateAnimation(deltaTime: Float) {
        animator?.let { anim ->
            animationTime += deltaTime

            val duration = anim.getAnimationDuration(animationIndex)
            if (animationTime > duration) {
                animationTime = 0.0f
            }

            anim.applyAnimation(animationIndex, animationTime)
            anim.updateBoneMatrices()
        }
    }

    override fun onDestroy() {
        super.onDestroy()

        asset?.let {
            scene?.removeEntities(it.entities)
            assetLoader?.destroyAsset(it)
        }

        resourceLoader?.destroy()
        assetLoader?.destroy()
    }
}
```

## 构建和运行

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.19)
project(GLTFViewer)

set(CMAKE_CXX_STANDARD 17)

find_package(filament REQUIRED)
find_package(glfw3 REQUIRED)

add_executable(gltf-viewer gltf_viewer.cpp)

target_link_libraries(gltf-viewer
    filament
    gltfio
    camutils
    glfw
    imgui
)

# Copy environment
file(COPY environment_ibl.ktx DESTINATION ${CMAKE_BINARY_DIR})
```

### 编译命令

```bash
# Desktop
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja gltf-viewer

# 运行
./gltf-viewer model.glb
```

## 常见问题

### Q1: 模型加载失败

**原因**:
- glTF 文件损坏
- 外部资源路径错误
- 不支持的 glTF 扩展

**解决**:

```cpp
// 启用详细日志
utils::slog.setLevel(utils::slog.Level::DEBUG);

// 检查加载结果
if (!asset) {
    std::cerr << "Failed to parse glTF\n";
    return false;
}

// 验证资源
for (size_t i = 0; i < asset->getResourceUriCount(); ++i) {
    const char* uri = asset->getResourceUri(i);
    utils::slog.i << "Resource " << i << ": " << uri << utils::io::endl;
}
```

### Q2: 纹理未显示

**原因**:
- 资源未异步加载完成
- 外部纹理文件缺失

**解决**:

```cpp
// 持续更新资源加载
void render() {
    resourceLoader->asyncUpdateLoad();

    // 检查加载进度
    float progress = resourceLoader->asyncGetLoadProgress();
    if (progress < 1.0f) {
        std::cout << "Loading: " << (progress * 100.0f) << "%\n";
    }
}
```

### Q3: 动画不播放

**原因**:
- 未调用 updateBoneMatrices()
- 动画索引错误

**解决**:

```cpp
// 正确的动画更新流程
if (animator) {
    animator->applyAnimation(animIndex, time);
    animator->updateBoneMatrices();  // 必须调用
}

// 检查动画数量
size_t count = asset->getAnimationCount();
std::cout << "Animations: " << count << "\n";
for (size_t i = 0; i < count; ++i) {
    const char* name = asset->getAnimationName(i);
    float duration = animator->getAnimationDuration(i);
    std::cout << "  [" << i << "] " << name
              << " (" << duration << "s)\n";
}
```

### Q4: 相机位置不合适

**解决**:

```cpp
// 自动适配相机到模型
void fitCamera(FilamentAsset* asset, Camera* camera) {
    Aabb bounds = asset->getBoundingBox();
    float3 center = (bounds.min + bounds.max) * 0.5f;
    float3 extent = (bounds.max - bounds.min) * 0.5f;
    float radius = length(extent);

    // 计算相机距离
    float fov = 45.0f;
    float distance = radius / std::tan(radians(fov) * 0.5f) * 1.5f;

    // 设置相机位置
    float3 eye = center + float3{0, 0, distance};
    camera->lookAt(eye, center, {0, 1, 0});
}
```

## 性能优化

### 1. 异步加载

```cpp
// 分帧加载资源
class AsyncLoader {
public:
    void update() {
        const int maxPerFrame = 5;  // 每帧最多处理5个资源

        for (int i = 0; i < maxPerFrame && !m_queue.empty(); ++i) {
            auto task = m_queue.front();
            m_queue.pop();

            // 处理单个资源
            processResource(task);
        }
    }

private:
    std::queue<LoadTask> m_queue;
};
```

### 2. LOD 管理

```cpp
// 根据距离切换 LOD
void updateLOD(FilamentAsset* asset, float3 cameraPos) {
    for (size_t i = 0; i < asset->getEntityCount(); ++i) {
        Entity entity = asset->getEntities()[i];

        // 获取实体位置
        auto& tm = engine->getTransformManager();
        auto instance = tm.getInstance(entity);
        float3 pos = tm.getWorldTransform(instance)[3].xyz;

        // 计算距离
        float distance = length(cameraPos - pos);

        // 选择 LOD (如果模型支持)
        int lodLevel = distance < 10.0f ? 0 :
                      distance < 30.0f ? 1 : 2;
    }
}
```

### 3. 视锥剔除

```cpp
// Filament 自动进行视锥剔除
// 确保正确设置边界框
RenderableManager::Builder builder(1);
builder.boundingBox({{minX, minY, minZ}, {maxX, maxY, maxZ}});
```

## 扩展功能

### 1. 拖放加载

```cpp
// GLFW 拖放回调
glfwSetDropCallback(window, [](GLFWwindow* w, int count, const char** paths) {
    auto* viewer = static_cast<GLTFViewer*>(glfwGetWindowUserPointer(w));
    if (count > 0) {
        viewer->loadModel(paths[0]);
    }
});
```

### 2. 截图功能

```cpp
void saveScreenshot(const std::string& path) {
    // 读取帧缓冲
    auto pixels = renderer->readPixels(0, 0, width, height);

    // 保存为 PNG
    stbi_write_png(path.c_str(), width, height, 4,
                   pixels.data(), width * 4);
}
```

### 3. 材质编辑

```cpp
// 运行时修改材质参数
void setBaseColor(Entity entity, float3 color) {
    auto& rm = engine->getRenderableManager();
    auto instance = rm.getInstance(entity);

    MaterialInstance* matInstance = rm.getMaterialInstanceAt(instance, 0);
    matInstance->setParameter("baseColorFactor", RgbType::sRGB, color);
}
```

## 相关文档

- [./03-pbr-model.md](./03-pbr-model.md) - 前一个示例
- [./05-camera-control.md](./05-camera-control.md) - 下一个示例：相机控制
- [../tools/05-gltf_viewer.md](../tools/05-gltf_viewer.md) - gltf_viewer 工具详解
- [gltfio API文档](https://google.github.io/filament/gltfio.html) - gltfio 库参考
