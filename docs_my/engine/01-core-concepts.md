# Engine 核心概念

本文档讲解 Filament Engine 的核心概念，包括 Engine、Scene、View、Renderer 的关系和作用，以及 Filament 的整体设计哲学。

---

## Filament Engine 是什么？

**Filament Engine** 是 Filament 的上层 API，提供场景管理、资源管理、渲染配置等功能。Engine 层连接应用代码和底层 Backend，负责将用户的渲染意图转换为 GPU 命令。

### 核心职责

1. **资源管理**: 创建和管理 VertexBuffer、Texture、Material 等
2. **场景组织**: 通过 Entity-Component 系统组织场景对象
3. **渲染配置**: 通过 View 配置渲染参数
4. **渲染执行**: 通过 Renderer 执行实际渲染

---

## 核心类关系

```
┌────────────────────────────────────────────────────────────┐
│                         Engine                             │
│  (引擎实例，管理所有资源和组件)                                 │
│                                                            │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌───────────┐   │
│  │  Scene   │  │   View   │  │ Renderer │  │  SwapChain│   │
│  └──────────┘  └──────────┘  └──────────┘  └───────────┘   │
│       ↓             ↓              ↓                       │
│  [Entity集合]    [渲染配置]      [渲染执行]                   │
│                                                            │
│  ┌────────────────────────────────────────────────────┐    │
│  │           Component Managers                       │    │
│  │  - TransformManager                                │    │
│  │  - RenderableManager                               │    │
│  │  - LightManager                                    │    │
│  │  - CameraManager                                   │    │
│  └────────────────────────────────────────────────────┘    │
└────────────────────────────────────────────────────────────┘
```

---

## Engine (引擎实例)

### 作用

Engine 是 Filament 的核心入口，管理所有资源和组件：

```cpp
// 创建引擎
Engine* engine = Engine::create();

// 创建资源
Scene* scene = engine->createScene();
View* view = engine->createView();
Renderer* renderer = engine->createRenderer();
Camera* camera = engine->createCamera(entity);

// 销毁引擎（会自动销毁所有资源）
Engine::destroy(&engine);
```

### 单例设计

Engine 通常作为单例使用（虽然可以创建多个）：

```cpp
class App {
public:
    App() {
        mEngine = Engine::create();
    }

    ~App() {
        Engine::destroy(&mEngine);
    }

    Engine* getEngine() { return mEngine; }

private:
    Engine* mEngine = nullptr;
};
```

---

## Scene (场景)

### 作用

Scene 是 Entity 的容器，代表一个三维场景：

```cpp
Scene* scene = engine->createScene();

// 添加 Entity
Entity entity = EntityManager::get().create();
scene->addEntity(entity);

// 移除 Entity
scene->remove(entity);

// 添加间接光
scene->setIndirectLight(ibl);

// 添加天空盒
scene->setSkybox(skybox);
```

### Scene 的内容

一个 Scene 可以包含：
- **Entity**: 场景对象（几何、灯光、相机等）
- **IndirectLight**: 间接光照（IBL）
- **Skybox**: 天空盒

---

## View (视图)

### 作用

View 配置渲染的各种参数，决定**如何**渲染场景：

```cpp
View* view = engine->createView();

// 设置要渲染的场景
view->setScene(scene);

// 设置相机
view->setCamera(camera);

// 设置视口
view->setViewport({0, 0, width, height});

// 渲染配置
view->setPostProcessingEnabled(true);
view->setAntiAliasing(View::AntiAliasing::FXAA);
view->setAmbientOcclusion(View::AmbientOcclusion::SSAO);
```

### View 的配置项

| 配置项 | 说明 |
|-------|------|
| **Scene** | 要渲染的场景 |
| **Camera** | 渲染视角 |
| **Viewport** | 渲染区域 |
| **Clear Flags** | 清除缓冲的方式 |
| **Post-Processing** | 后处理效果 |
| **Anti-Aliasing** | 抗锯齿 |
| **Ambient Occlusion** | 环境光遮蔽 |
| **Bloom** | 辉光效果 |
| **Tone Mapping** | 色调映射 |

### 多视图渲染

可以创建多个 View 渲染同一个 Scene：

```cpp
// 主视图（全屏）
View* mainView = engine->createView();
mainView->setViewport({0, 0, 1920, 1080});
mainView->setCamera(mainCamera);

// 小地图视图（右上角）
View* minimapView = engine->createView();
minimapView->setViewport({1600, 0, 320, 320});
minimapView->setCamera(minimapCamera);

// 渲染两个视图
renderer->render(mainView);
renderer->render(minimapView);
```

---

## Renderer (渲染器)

### 作用

Renderer 执行实际的渲染操作：

```cpp
Renderer* renderer = engine->createRenderer();

// 开始帧
renderer->beginFrame(swapChain);

// 渲染视图
renderer->render(view);

// 结束帧
renderer->endFrame();
```

### 渲染循环

典型的渲染循环：

```cpp
while (running) {
    // 1. 更新场景（应用层逻辑）
    updateScene(deltaTime);

    // 2. 开始渲染帧
    if (renderer->beginFrame(swapChain)) {
        // 3. 渲染视图
        renderer->render(view);

        // 4. 结束渲染帧
        renderer->endFrame();
    }

    // 5. 显示
    swapChain->present();
}
```

---

## Camera (相机)

### 作用

Camera 定义渲染的视角和投影：

```cpp
// 创建相机 Entity
Entity cameraEntity = EntityManager::get().create();

// 创建 Camera 组件
Camera* camera = engine->createCamera(cameraEntity);

// 设置投影
camera->setProjection(45.0, aspect, 0.1, 100.0); // 透视投影
// 或
camera->setProjection(Camera::Projection::ORTHO,
                     -10, 10, -10, 10, 0, 100); // 正交投影

// 设置位置
TransformManager& tcm = engine->getTransformManager();
tcm.setTransform(tcm.getInstance(cameraEntity),
                 mat4::translation(vec3(0, 0, 5)));

// 应用到 View
view->setCamera(camera);
```

### 投影类型

| 投影 | 用途 | 特点 |
|------|------|------|
| **透视投影** | 3D 场景 | 近大远小，有深度感 |
| **正交投影** | 2D UI、CAD | 平行线保持平行，无透视 |

---

## SwapChain (交换链)

### 作用

SwapChain 代表渲染目标（通常是窗口）：

```cpp
// 从窗口创建 SwapChain
void* nativeWindow = /* 平台窗口句柄 */;
SwapChain* swapChain = engine->createSwapChain(nativeWindow);

// 渲染到 SwapChain
renderer->beginFrame(swapChain);
renderer->render(view);
renderer->endFrame();

// 销毁
engine->destroy(swapChain);
```

---

## 完整示例：最小渲染程序

```cpp
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Renderer.h>
#include <filament/Camera.h>

using namespace filament;

int main() {
    // 1. 创建 Engine
    Engine* engine = Engine::create();

    // 2. 创建 SwapChain
    void* nativeWindow = /* ... */;
    SwapChain* swapChain = engine->createSwapChain(nativeWindow);

    // 3. 创建 Scene
    Scene* scene = engine->createScene();

    // 4. 创建 Camera
    Entity cameraEntity = EntityManager::get().create();
    Camera* camera = engine->createCamera(cameraEntity);
    camera->setProjection(45.0, 16.0/9.0, 0.1, 100.0);

    // 5. 创建 View
    View* view = engine->createView();
    view->setScene(scene);
    view->setCamera(camera);
    view->setViewport({0, 0, 1920, 1080});

    // 6. 创建 Renderer
    Renderer* renderer = engine->createRenderer();

    // 7. 渲染循环
    while (running) {
        if (renderer->beginFrame(swapChain)) {
            renderer->render(view);
            renderer->endFrame();
        }
    }

    // 8. 清理
    engine->destroy(swapChain);
    engine->destroy(renderer);
    engine->destroy(view);
    engine->destroy(scene);
    engine->destroy(camera);
    EntityManager::get().destroy(cameraEntity);
    Engine::destroy(&engine);

    return 0;
}
```

---

## Filament 设计哲学

### 1. 数据导向设计

Filament 使用 Entity-Component 系统，优化缓存友好性：

```cpp
// ❌ 面向对象方式
class GameObject {
    Transform transform;
    Renderable renderable;
    Light light;
    // ... 大量数据混在一起
};

// ✅ Filament 方式
Entity entity = 42; // 只是一个 ID

TransformManager 存储: [entity42 -> Transform数据]
RenderableManager 存储: [entity42 -> Renderable数据]
LightManager 存储: [entity42 -> Light数据]

// 数据连续存储，缓存友好，易于批处理
```

### 2. 分离关注点

- **Scene**: 管理**什么**要渲染
- **View**: 配置**如何**渲染
- **Renderer**: 执行渲染

### 3. 资源生命周期明确

所有资源由 Engine 创建和销毁：

```cpp
// 创建
Texture* tex = Texture::Builder().build(*engine);

// 使用
materialInstance->setParameter("albedo", tex);

// 销毁（必须显式）
engine->destroy(tex);
```

### 4. 平台无关

Engine API 完全不依赖具体的图形 API，通过 Backend 抽象：

```cpp
// 相同的代码在 OpenGL/Vulkan/Metal 上运行
renderer->render(view); // Backend 自动选择
```

---

## 相关文档

- **[02-entity-component.md](02-entity-component.md)**: Entity-Component 系统详解
- **[03-resource-management.md](03-resource-management.md)**: 资源管理
- **[07-render-loop.md](07-render-loop.md)**: 渲染循环详解

**底层依赖**:
- `../backend/01-architecture-overview.md`: Backend 架构

---

## 总结

Filament Engine 通过 **Engine、Scene、View、Renderer** 四个核心类，提供了简洁而强大的渲染 API。理解这些核心概念是使用 Filament 的基础。
