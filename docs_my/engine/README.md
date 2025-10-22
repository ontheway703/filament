# Filament Engine 核心架构

本目录包含 Filament Engine 上层架构的完整技术文档，详细讲解引擎如何组织场景、管理资源、执行渲染，连接应用层和 Backend 层。

---

## 📚 文档结构

### 核心概念部分

1. **[01-core-concepts.md](01-core-concepts.md)** - Engine 核心概念
   - Engine、Scene、View、Renderer 的关系
   - Entity-Component 系统简介
   - 渲染流程总览
   - Filament 的设计哲学

2. **[02-entity-component.md](02-entity-component.md)** - Entity-Component 系统
   - Entity 和 Component 的概念
   - ComponentManager 架构
   - Transform、Renderable、Light 等组件
   - Entity 的创建和销毁

3. **[03-resource-management.md](03-resource-management.md)** - 资源管理系统
   - VertexBuffer/IndexBuffer 创建流程
   - Texture 资源管理
   - Material 和 MaterialInstance
   - 资源生命周期

### 渲染系统部分

4. **[04-renderable-system.md](04-renderable-system.md)** - Renderable 系统
   - Renderable 组件详解
   - RenderableManager
   - Bounding Box 和剔除
   - 几何实例化

5. **[05-camera-view.md](05-camera-view.md)** - Camera 和 View 系统
   - Camera 投影矩阵
   - View 渲染配置
   - Viewport 和 Scissor
   - 多视图渲染

6. **[06-lighting-system.md](06-lighting-system.md)** - 光照系统
   - 直接光照 (Directional、Point、Spot)
   - IBL (Image-Based Lighting)
   - LightManager
   - 阴影贴图

### 高级主题部分

7. **[07-render-loop.md](07-render-loop.md)** - 渲染循环
   - 帧流程详解
   - beginFrame/render/endFrame
   - SwapChain 集成
   - 多线程渲染

8. **[08-scene-graph.md](08-scene-graph.md)** - 场景图和变换
   - TransformManager
   - 父子层级关系
   - 世界坐标系转换
   - 动画集成

9. **[09-complete-pipeline.md](09-complete-pipeline.md)** - 完整渲染管线
   - 从应用到 GPU 的完整流程
   - 各系统协作关系
   - 性能分析和优化
   - 最佳实践

---

## 🎯 学习路径

### 路径 1: Filament 应用开发者

如果你想使用 Filament 开发应用:

1. **核心概念**: 先学习 `01-core-concepts.md` 了解整体架构
2. **资源创建**: 阅读 `03-resource-management.md` 学习如何创建资源
3. **场景构建**: 学习 `02-entity-component.md` 和 `04-renderable-system.md`
4. **相机视图**: 掌握 `05-camera-view.md` 设置渲染视角
5. **光照**: 学习 `06-lighting-system.md` 添加灯光
6. **渲染循环**: 阅读 `07-render-loop.md` 了解渲染流程
7. **变换动画**: 学习 `08-scene-graph.md` 进行场景组织

**推荐阅读顺序**: 01 → 03 → 02 → 04 → 05 → 06 → 07 → 08

**适合人群**: 应用开发者、游戏开发者、技术美术

### 路径 2: Filament 引擎开发者

如果你想深入理解或贡献 Filament 引擎:

1. **系统学习**: 按顺序阅读所有文档 (01-09)
2. **重点深入**:
   - Entity-Component 系统: `02-entity-component.md`
   - 渲染管线: `09-complete-pipeline.md`
   - 性能优化: `07-render-loop.md`
3. **Backend 集成**: 结合 `../backend/` 目录文档学习
4. **源码阅读**: 对照源码理解实现细节

**推荐阅读顺序**: 01 → 02 → 03 → 04 → 05 → 06 → 07 → 08 → 09

**适合人群**: 引擎开发者、图形程序员、开源贡献者

### 路径 3: 从 Unity/Unreal 迁移

如果你熟悉其他引擎，想学习 Filament:

1. **对比学习**: 阅读 `01-core-concepts.md` 理解设计差异
2. **Entity 系统**: 学习 `02-entity-component.md` (类似 Unity ECS)
3. **资源管理**: 阅读 `03-resource-management.md` (对比 Unity/Unreal 的资源系统)
4. **场景组织**: 学习 `08-scene-graph.md` (对比 Unity Transform Hierarchy)
5. **渲染流程**: 阅读 `07-render-loop.md` 了解渲染循环

**推荐阅读顺序**: 01 → 02 → 08 → 03 → 07

**对照表**: 

| Filament | Unity | Unreal |
|----------|-------|--------|
| Entity | GameObject | AActor |
| Component | Component | UActorComponent |
| Scene | Scene | UWorld |
| View | Camera.Render() | USceneViewport |
| Material | Material | UMaterial |

**适合人群**: Unity/Unreal 开发者

### 路径 4: 性能优化工程师

如果你专注于性能优化:

1. **完整流程**: 重点学习 `09-complete-pipeline.md`
2. **剔除优化**: 阅读 `04-renderable-system.md` 的剔除机制
3. **渲染循环**: 学习 `07-render-loop.md` 的多线程和同步
4. **资源优化**: 阅读 `03-resource-management.md` 的资源管理
5. **Backend 对接**: 结合 `../backend/` 文档理解底层调用

**推荐阅读顺序**: 09 → 07 → 04 → 03 → `../backend/09-backend-comparison.md`

**参考文档**:
- `../graphics/09-gpu-optimization.md`: GPU 优化技术
- `../backend/03-command-stream.md`: 命令流优化

**适合人群**: 性能工程师、优化专家

---

## 🔗 与 docs_my 其他文档的关系

本目录的 Engine 知识是应用层和底层的连接桥梁:

### backend 目录 (底层依赖)
- `backend/01-architecture-overview.md` - **底层基础**: Engine 调用 Backend API
- `backend/02-driver-abstraction.md` - **接口依赖**: Engine 通过 Driver 创建资源
- `backend/03-command-stream.md` - **命令提交**: 渲染循环中的命令提交
- `backend/04-resource-handles.md` - **句柄管理**: Engine 持有 Backend 资源句柄

### material 目录 (材质系统)
- `material/08-runtime-usage.md` - **直接集成**: Engine 加载和使用材质
- `material/01-core-classes.md` - **Material/MaterialInstance**: Engine 的材质管理
- `material/05-graphics-concepts.md` - **PBR 材质**: 光照系统的理论基础

### gltfio 目录 (资源加载)
- `gltfio/04-object-creation.md` - **资源创建**: 通过 Engine API 创建 Entity 和 Renderable
- `gltfio/05-architecture.md` - **架构对接**: FilamentAsset 如何集成到 Scene
- `gltfio/02-loading-phases.md` - **加载流程**: 资源加载后如何添加到场景

### graphics 目录 (理论基础)
- `graphics/01-rendering-fundamentals.md` - **渲染理论**: 光照和 PBR 的数学基础
- `graphics/07-3d-math.md` - **变换数学**: TransformManager 的数学原理
- `graphics/09-gpu-optimization.md` - **优化技术**: 渲染优化的理论指导

---

## 🌟 核心概念速查

### Filament 架构

```
┌────────────────────────────────────────────────────────┐
│                     应用层 (User Code)                  │
│                                                        │
│  gltfio::AssetLoader, App Logic, User Input            │
└────────────────────────┬───────────────────────────────┘
                         ↓
┌────────────────────────────────────────────────────────┐
│                  Filament Engine (本目录)               │
│                                                        │
│  ┌────────┐  ┌────────┐  ┌────────┐  ┌────────────┐    │
│  │ Engine │  │ Scene  │  │  View  │  │  Renderer  │    │
│  └────────┘  └────────┘  └────────┘  └────────────┘    │
│                                                        │
│  ┌─────────────────┐  ┌────────────────────┐           │
│  │ Entity-Component│  │ Resource Management│           │
│  │   System        │  │  (VB/IB/Texture)   │           │
│  └─────────────────┘  └────────────────────┘           │
└────────────────────────┬───────────────────────────────┘
                         ↓
┌────────────────────────────────────────────────────────┐
│               Backend (渲染抽象层)                       │
│                                                        │
│  Driver, CommandStream, Handle<T>                      │
└────────────────────────┬───────────────────────────────┘
                         ↓
┌────────────────────────────────────────────────────────┐
│           Graphics API (OpenGL/Vulkan/Metal)           │
└────────────────────────────────────────────────────────┘
```

### Engine 核心类

```cpp
// 引擎实例（单例）
Engine* engine = Engine::create();

// 场景（容纳 Entity）
Scene* scene = engine->createScene();

// 视图（渲染配置）
View* view = engine->createView();
view->setScene(scene);

// 渲染器（执行渲染）
Renderer* renderer = engine->createRenderer();

// 相机
Camera* camera = engine->createCamera(Entity);
view->setCamera(camera);

// Entity（场景对象）
Entity entity = EntityManager::get().create();

// 添加组件
RenderableManager::Builder(1)
    .geometry(0, primitive, vertexBuffer, indexBuffer)
    .material(0, materialInstance)
    .build(*engine, entity);

// 添加到场景
scene->addEntity(entity);

// 渲染
renderer->render(view);
```

### Entity-Component 关系

```
Entity (只是一个 ID)
  ├─> TransformComponent (位置、旋转、缩放)
  ├─> RenderableComponent (几何、材质)
  ├─> LightComponent (灯光属性)
  └─> ...

ComponentManager 管理组件:
  - TransformManager
  - RenderableManager
  - LightManager
  - CameraManager
```

### 渲染循环

```cpp
// 每帧调用
void renderFrame() {
    // 1. 开始帧
    renderer->beginFrame(swapChain);

    // 2. 渲染视图
    renderer->render(view);

    // 3. 结束帧
    renderer->endFrame();

    // 4. 提交（内部调用 Backend）
    engine->flushAndWait();
}
```

---

## ⚡ 快速概念对照表

| 概念 | 说明 | 相关文档 |
|-----|------|---------|
| Engine | 引擎实例，管理所有资源 | 01 核心概念 |
| Scene | 场景，容纳 Entity | 01 核心概念 |
| View | 视图，配置渲染参数 | 05 Camera 和 View |
| Renderer | 渲染器，执行渲染 | 07 渲染循环 |
| Entity | 实体，场景对象 ID | 02 Entity-Component |
| Component | 组件，附加到 Entity 的数据 | 02 Entity-Component |
| Renderable | 可渲染组件 | 04 Renderable 系统 |
| Transform | 变换组件 | 08 场景图 |
| Light | 光照组件 | 06 光照系统 |
| Camera | 相机 | 05 Camera 和 View |
| Material | 材质 | 03 资源管理 |
| VertexBuffer | 顶点缓冲 | 03 资源管理 |
| IndexBuffer | 索引缓冲 | 03 资源管理 |
| Texture | 纹理 | 03 资源管理 |

---

## 📖 推荐学习资源

### Filament 官方资源
- **[Filament 官方文档](https://google.github.io/filament/)**: 完整的 API 文档
- **[Filament 设计文档](https://google.github.io/filament/Filament.html)**: 设计哲学和理论
- **[Filament 示例](https://github.com/google/filament/tree/main/samples)**: 官方示例代码

### Entity-Component 系统
- **[Unity ECS](https://docs.unity3d.com/Packages/com.unity.entities@latest)**: Unity 的 ECS 实现
- **[EnTT](https://github.com/skypjack/entt)**: C++ ECS 库
- **[Data-Oriented Design](https://www.dataorienteddesign.com/)**: 数据导向设计

### 渲染引擎设计
- **[Game Engine Architecture](https://www.gameenginebook.com/)**: Jason Gregory 的游戏引擎架构
- **[Physically Based Rendering](https://www.pbr-book.org/)**: PBR 圣经
- **[Real-Time Rendering](http://www.realtimerendering.com/)**: 实时渲染

---

## 🎓 使用建议

### 对于初学者
1. **先理解概念**: 不要急于写代码，先理解 Entity、Component、Scene 的关系
2. **从简单场景开始**: 创建一个三角形，逐步添加复杂度
3. **参考官方示例**: Filament 的示例代码非常清晰
4. **理解生命周期**: 特别注意资源的创建和销毁

### 对于进阶用户
1. **深入 Backend**: 理解 Engine 如何调用 Backend
2. **性能分析**: 使用 Profiler 分析渲染瓶颈
3. **自定义扩展**: 尝试扩展 ComponentManager
4. **多线程优化**: 理解 Filament 的多线程架构

### 文档约定
- 📐 **架构图**: 使用 ASCII 图展示系统关系
- 💡 **代码示例**: C++ 示例代码
- ⚠️ **注意事项**: 常见陷阱和最佳实践
- 🔗 **跨文档引用**: 相关知识点链接

---

## 📝 版本信息

- **文档版本**: 1.0
- **创建日期**: 2025-10-20
- **适用 Filament 版本**: 最新主分支
- **更新策略**: 随 Filament Engine 更新同步维护

---

## 💡 Filament 的设计哲学

### 1. 简洁的 API
Filament 提供简洁易用的 API，隐藏复杂的底层细节：
```cpp
// 创建一个可渲染对象只需几行代码
RenderableManager::Builder(1)
    .geometry(0, primitive, vb, ib)
    .material(0, material)
    .build(*engine, entity);
```

### 2. 数据导向设计
使用 Entity-Component 系统，优化缓存友好性和性能：
- Entity 只是 ID
- Component 数据连续存储
- 批量处理提高效率

### 3. 平台无关
Engine 层完全不依赖具体的图形 API，通过 Backend 抽象：
- 一次编写，多平台运行
- 自动选择最优 Backend

### 4. 现代渲染技术
内置 PBR、IBL、阴影等现代渲染技术：
- 物理真实的渲染
- 开箱即用的高级特性

---

## 🚀 开始学习

选择适合你的学习路径，从第一篇文档开始，系统掌握 Filament Engine 的核心架构！

> "Engine 是连接应用和渲染的桥梁" - 理解 Engine 层，是掌握 Filament 的关键！
