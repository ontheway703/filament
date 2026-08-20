# 渲染循环

本文档详细讲解 Filament 的渲染循环流程，包括 beginFrame、render、endFrame 的详细过程，SwapChain 集成，以及多线程渲染架构。

---

## 渲染循环概述

Filament 的渲染循环由三个核心步骤组成：

```cpp
while (running) {
    // 1. 开始帧
    if (renderer->beginFrame(swapChain)) {
        // 2. 渲染视图
        renderer->render(view);
        
        // 3. 结束帧
        renderer->endFrame();
    }
}
```

---

## beginFrame (开始帧)

### 作用

beginFrame 准备新一帧的渲染：
- 获取 SwapChain 的下一个缓冲区
- 重置内部状态
- 准备命令缓冲区

```cpp
bool Renderer::beginFrame(SwapChain* swapChain, uint64_t vsyncSteadyClockTimeNano);

// 参数：
// - swapChain: 渲染目标（窗口）
// - vsyncSteadyClockTimeNano: VSync 时间戳（可选，用于性能分析）

// 返回值：
// - true: 可以继续渲染
// - false: 跳过本帧（SwapChain 不可用）

// 使用
if (renderer->beginFrame(swapChain)) {
    // 渲染代码...
}
```

### 内部流程

```
beginFrame() 内部：

1. Backend 准备
   ├─> 获取下一个 SwapChain 图像
   ├─> 等待上一帧完成（如果需要）
   └─> 重置命令缓冲区

2. Froxel 更新（用于光照）
   └─> 重新计算视锥体切片

3. 阴影贴图准备
   └─> 更新级联阴影贴图矩阵

4. 设置渲染目标
   └─> 绑定 SwapChain 的 RenderTarget
```

---

## render (渲染视图)

### 作用

render 执行实际的渲染工作：

```cpp
void Renderer::render(View const* view);

// 使用
renderer->render(view);

// 可以渲染多个视图
renderer->render(mainView);
renderer->render(minimapView);
renderer->render(uiView);
```

### 渲染流程

```
render(view) 内部：

1. 视锥剔除
   ├─> 遍历所有 Renderable
   ├─> 测试包围盒是否在视锥内
   └─> 构建可见对象列表

2. 排序
   ├─> 不透明物体：前到后（深度排序）
   └─> 透明物体：后到前（blendOrder排序）

3. 阴影 Pass（如果有方向光）
   ├─> 切换到阴影 RenderTarget
   ├─> 渲染投射阴影的物体
   └─> 生成阴影贴图

4. 主渲染 Pass
   ├─> 清除缓冲区
   ├─> 渲染天空盒（如果有）
   ├─> 渲染不透明物体
   ├─> 渲染透明物体
   └─> 应用后处理

5. 后处理 Pass（如果启用）
   ├─> Bloom（辉光）
   ├─> SSAO（环境光遮蔽）
   ├─> TAA（时间抗锯齿）
   ├─> FXAA（快速抗锯齿）
   └─> Tone Mapping（色调映射）
```

### 详细渲染步骤

#### 1. 视锥剔除

```cpp
// 伪代码
std::vector<RenderPrimitive> visibleObjects;

for (Entity entity : scene->getEntities()) {
    auto& rm = engine->getRenderableManager();
    if (!rm.hasComponent(entity)) continue;
    
    auto instance = rm.getInstance(entity);
    Box box = rm.getAxisAlignedBoundingBox(instance);
    
    // 变换到世界空间
    auto& tcm = engine->getTransformManager();
    mat4f worldTransform = tcm.getWorldTransform(tcm.getInstance(entity));
    Box worldBox = transformBox(box, worldTransform);
    
    // 视锥剔除测试
    if (frustum.intersects(worldBox)) {
        visibleObjects.push_back({entity, instance});
    }
}

// 结果：只渲染可见对象，提升性能
```

#### 2. 排序

```cpp
// 不透明物体：前到后排序（Early-Z 优化）
std::sort(opaqueObjects.begin(), opaqueObjects.end(),
    [](const auto& a, const auto& b) {
        float depthA = dot(a.position - cameraPos, cameraForward);
        float depthB = dot(b.position - cameraPos, cameraForward);
        return depthA < depthB;  // 前到后
    });

// 透明物体：后到前排序（正确混合）
std::sort(transparentObjects.begin(), transparentObjects.end(),
    [](const auto& a, const auto& b) {
        float depthA = dot(a.position - cameraPos, cameraForward);
        float depthB = dot(b.position - cameraPos, cameraForward);
        return depthA > depthB;  // 后到前
    });
```

#### 3. 实际绘制

```cpp
// 遍历可见对象
for (const auto& primitive : visibleObjects) {
    // 绑定材质
    driver.setProgram(primitive.material->getProgram());
    driver.bindUniforms(primitive.material->getUniforms());
    driver.bindTextures(primitive.material->getTextures());
    
    // 绑定几何
    driver.setVertexBuffer(primitive.vertexBuffer);
    driver.setIndexBuffer(primitive.indexBuffer);
    
    // 设置 Uniform（MVP 矩阵等）
    driver.setUniform("modelMatrix", primitive.worldTransform);
    driver.setUniform("viewMatrix", view->getViewMatrix());
    driver.setUniform("projectionMatrix", view->getProjectionMatrix());
    
    // 绘制
    driver.draw(primitive);
}
```

---

## endFrame (结束帧)

### 作用

endFrame 完成当前帧的渲染：
- 提交所有命令
- 标记帧结束
- 准备呈现

```cpp
void Renderer::endFrame();

// 使用
renderer->endFrame();

// 完整流程
renderer->beginFrame(swapChain);
renderer->render(view);
renderer->endFrame();  // 必须调用！
```

### 内部流程

```
endFrame() 内部：

1. 完成后处理
   └─> 应用最终的色调映射和 Gamma 校正

2. 提交命令
   └─> Backend 提交所有渲染命令

3. 交换缓冲区
   └─> SwapChain 准备呈现

4. 性能统计
   └─> 记录帧时间、draw call 数量等
```

---

## 完整渲染循环示例

### 基础循环

```cpp
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/View.h>
#include <filament/Scene.h>

using namespace filament;

void renderLoop(Engine* engine, Renderer* renderer,
                View* view, SwapChain* swapChain) {
    while (running) {
        // 更新应用逻辑
        updateScene(deltaTime);
        
        // 渲染
        if (renderer->beginFrame(swapChain)) {
            renderer->render(view);
            renderer->endFrame();
        }
        
        // 可选：等待GPU完成
        // engine->flushAndWait();
    }
}
```

### 带时间控制的循环

```cpp
#include <chrono>

void renderLoopWithTiming(Engine* engine, Renderer* renderer,
                          View* view, SwapChain* swapChain) {
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono;
    
    auto lastTime = Clock::now();
    
    while (running) {
        // 计算 deltaTime
        auto currentTime = Clock::now();
        float deltaTime = duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        
        // 限制帧率（可选）
        if (deltaTime < 1.0f / 60.0f) {
            std::this_thread::sleep_for(
                milliseconds(int((1.0f/60.0f - deltaTime) * 1000))
            );
            continue;
        }
        
        // 更新场景
        updateScene(deltaTime);
        
        // 渲染
        if (renderer->beginFrame(swapChain)) {
            renderer->render(view);
            renderer->endFrame();
        }
    }
}
```

### 多视图渲染

```cpp
void renderMultiView(Engine* engine, Renderer* renderer,
                     View* mainView, View* minimapView,
                     SwapChain* swapChain) {
    while (running) {
        if (renderer->beginFrame(swapChain)) {
            // 主视图（占据大部分屏幕）
            mainView->setViewport({0, 0, 1600, 1200});
            renderer->render(mainView);
            
            // 小地图视图（右上角）
            minimapView->setViewport({1400, 1000, 200, 200});
            renderer->render(minimapView);
            
            renderer->endFrame();
        }
    }
}
```

---

## 多线程渲染架构

### Filament 的线程模型

```
┌─────────────────────────────────────────────────────────┐
│                主线程 (Application Thread)                │
│                                                           │
│  while (running) {                                        │
│      updateScene(deltaTime);  // CPU 工作                 │
│                                                           │
│      renderer->beginFrame(swapChain);                     │
│      renderer->render(view);  // 录制命令（非阻塞）        │
│      renderer->endFrame();                                │
│  }                                                        │
└─────────────────────────┬───────────────────────────────┘
                          │ CommandStream
                          ↓
┌─────────────────────────────────────────────────────────┐
│                渲染线程 (Render Thread)                    │
│                                                           │
│  while (running) {                                        │
│      commandStream.wait();                                │
│      commandStream.execute();  // 执行 GPU 命令            │
│         ├─> driver.draw()                                 │
│         ├─> driver.bindTexture()                          │
│         └─> ...                                           │
│  }                                                        │
└───────────────────────────────────────────────────────────┘
```

### 并行渲染

```cpp
// 主线程准备第 N 帧，渲染线程执行第 N-1 帧

帧 N:
  主线程:   准备场景 N    → 录制命令 N
  渲染线程:                           执行命令 N-1

帧 N+1:
  主线程:   准备场景 N+1  → 录制命令 N+1
  渲染线程:                           执行命令 N

// 优势：CPU 和 GPU 并行工作，提高吞吐量
```

---

## SwapChain 集成

### 创建 SwapChain

```cpp
// 从原生窗口创建
void* nativeWindow = /* 平台窗口句柄 */;
SwapChain* swapChain = engine->createSwapChain(nativeWindow);

// 或创建离屏 SwapChain
SwapChain* offscreenSwapChain = engine->createSwapChain(1920, 1080);
```

### SwapChain 生命周期

```cpp
// 1. 创建
SwapChain* swapChain = engine->createSwapChain(nativeWindow);

// 2. 使用（在渲染循环中）
renderer->beginFrame(swapChain);
renderer->render(view);
renderer->endFrame();

// 3. 销毁
engine->destroy(swapChain);
```

### 窗口大小改变

```cpp
void handleWindowResize(int newWidth, int newHeight) {
    // 1. 销毁旧 SwapChain
    engine->destroy(swapChain);
    
    // 2. 创建新 SwapChain
    swapChain = engine->createSwapChain(nativeWindow);
    
    // 3. 更新 View 的 Viewport
    view->setViewport({0, 0, (uint32_t)newWidth, (uint32_t)newHeight});
    
    // 4. 更新相机的宽高比
    float aspect = (float)newWidth / newHeight;
    camera->setProjection(45.0, aspect, 0.1, 100.0);
}
```

---

## 同步和性能

### flushAndWait (强制同步)

```cpp
// 立即执行所有命令并等待完成
engine->flushAndWait();

// 使用场景：
// 1. 准备截图
// 2. 读取 RenderTarget 内容
// 3. 程序退出前清理

// 示例：截图
renderer->beginFrame(swapChain);
renderer->render(view);
renderer->endFrame();

engine->flushAndWait();  // 等待渲染完成

// 现在可以安全读取纹理数据
uint8_t* pixels = readPixels(renderTarget);
```

### 帧率限制

```cpp
// 方式1：VSync
// SwapChain 创建时默认启用 VSync，锁定 60 FPS

// 方式2：手动限制
void renderLoopWithFrameLimit(int targetFPS) {
    using namespace std::chrono;
    
    auto frameDuration = milliseconds(1000 / targetFPS);
    auto lastTime = steady_clock::now();
    
    while (running) {
        auto currentTime = steady_clock::now();
        auto elapsed = duration_cast<milliseconds>(currentTime - lastTime);
        
        if (elapsed < frameDuration) {
            std::this_thread::sleep_for(frameDuration - elapsed);
        }
        
        lastTime = steady_clock::now();
        
        // 渲染
        if (renderer->beginFrame(swapChain)) {
            renderer->render(view);
            renderer->endFrame();
        }
    }
}
```

---

## 性能分析

### 帧时间测量

```cpp
#include <chrono>

struct FrameStats {
    float cpuTime;
    float gpuTime;
    int drawCalls;
    int triangles;
};

FrameStats measureFrame(Engine* engine, Renderer* renderer,
                        View* view, SwapChain* swapChain) {
    using namespace std::chrono;
    
    auto cpuStart = high_resolution_clock::now();
    
    if (renderer->beginFrame(swapChain)) {
        renderer->render(view);
        renderer->endFrame();
    }
    
    auto cpuEnd = high_resolution_clock::now();
    
    // CPU 时间
    float cpuTime = duration<float, std::milli>(cpuEnd - cpuStart).count();
    
    // GPU 时间（需要查询）
    // float gpuTime = /* 通过 GPU timer query 获取 */;
    
    return {cpuTime, 0 /* gpuTime */, 0, 0};
}
```

### Draw Call 统计

```cpp
// 获取渲染统计
void printRenderStats(Renderer* renderer) {
    // Filament 内部统计（需要编译时启用）
    #if FILAMENT_ENABLE_MATDBG
        auto stats = renderer->getStats();
        LOG(INFO) << "Draw calls: " << stats.drawCalls;
        LOG(INFO) << "Triangles: " << stats.triangles;
    #endif
}
```

---

## 错误处理

### beginFrame 返回 false

```cpp
if (!renderer->beginFrame(swapChain)) {
    // SwapChain 不可用，可能原因：
    // 1. 窗口最小化
    // 2. 窗口被遮挡
    // 3. SwapChain 正在重建
    
    // 跳过本帧，不调用 render() 和 endFrame()
    continue;
}
```

### 渲染错误恢复

```cpp
try {
    if (renderer->beginFrame(swapChain)) {
        renderer->render(view);
        renderer->endFrame();
    }
} catch (const std::exception& e) {
    LOG(ERROR) << "Render error: " << e.what();
    
    // 尝试恢复：
    // 1. 重建 SwapChain
    engine->destroy(swapChain);
    swapChain = engine->createSwapChain(nativeWindow);
    
    // 2. 或者退出
    running = false;
}
```

---

## 完整应用示例

```cpp
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/View.h>
#include <filament/Scene.h>
#include <filament/Camera.h>
#include <chrono>

using namespace filament;
using namespace std::chrono;

class FilamentApp {
public:
    FilamentApp(void* nativeWindow) {
        // 创建引擎
        mEngine = Engine::create();
        
        // 创建 SwapChain
        mSwapChain = mEngine->createSwapChain(nativeWindow);
        
        // 创建渲染器
        mRenderer = mEngine->createRenderer();
        
        // 创建场景
        mScene = mEngine->createScene();
        
        // 创建相机
        Entity cameraEntity = EntityManager::get().create();
        mCamera = mEngine->createCamera(cameraEntity);
        mCamera->setProjection(45.0, 16.0/9.0, 0.1, 100.0);
        
        // 创建视图
        mView = mEngine->createView();
        mView->setScene(mScene);
        mView->setCamera(mCamera);
        mView->setViewport({0, 0, 1920, 1080});
        
        // 启用后处理
        mView->setPostProcessingEnabled(true);
        mView->setAntiAliasing(View::AntiAliasing::FXAA);
    }
    
    ~FilamentApp() {
        // 清理（按创建的相反顺序）
        mEngine->destroy(mView);
        EntityManager::get().destroy(mCamera->getEntity());
        mEngine->destroy(mCamera);
        mEngine->destroy(mScene);
        mEngine->destroy(mRenderer);
        mEngine->destroy(mSwapChain);
        Engine::destroy(&mEngine);
    }
    
    void run() {
        auto lastTime = steady_clock::now();
        
        while (!shouldQuit()) {
            // 计算 deltaTime
            auto currentTime = steady_clock::now();
            float deltaTime = duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;
            
            // 更新场景
            update(deltaTime);
            
            // 渲染
            render();
        }
    }
    
private:
    void update(float deltaTime) {
        // 更新场景逻辑
        // 例如：更新动画、物理、相机等
    }
    
    void render() {
        if (mRenderer->beginFrame(mSwapChain)) {
            mRenderer->render(mView);
            mRenderer->endFrame();
        }
    }
    
    bool shouldQuit() const {
        // 检查退出条件
        return false;
    }
    
    Engine* mEngine;
    SwapChain* mSwapChain;
    Renderer* mRenderer;
    Scene* mScene;
    View* mView;
    Camera* mCamera;
};

int main() {
    void* nativeWindow = /* 获取平台窗口 */;
    
    FilamentApp app(nativeWindow);
    app.run();
    
    return 0;
}
```

---

## 相关文档

- **[01-core-concepts.md](01-core-concepts.md)**: Engine、Renderer、View 核心概念
- **[05-camera-view.md](05-camera-view.md)**: View 配置详解
- **[09-complete-pipeline.md](09-complete-pipeline.md)**: 完整渲染管线

**底层实现**:
- `../backend/03-command-stream.md`: CommandStream 机制
- `../backend/08-platform-abstraction.md`: SwapChain 实现

---

## 总结

Filament 的渲染循环通过 **beginFrame/render/endFrame** 三步骤、**多线程架构**、**SwapChain 抽象**，实现了高效、灵活的渲染流程。

**核心要点**:
- ✅ beginFrame 准备帧，可能返回 false
- ✅ render 执行实际渲染，可多次调用
- ✅ endFrame 必须调用，完成帧
- ✅ 主线程和渲染线程并行工作
- ✅ 通过 flushAndWait 强制同步
