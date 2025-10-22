# Filament 完整知识体系文档

## 📖 文档总览

欢迎来到 Filament 完整知识体系文档！本文档体系从 GPU 底层原理到应用层实践，提供了学习 Filament 渲染引擎所需的全部知识。

### 知识链路图

```
┌──────────────────────────────────────────────────────────────┐
│                   GPU 和图形学理论基础                          │
│                      graphics/                                 │
│  (渲染方程、PBR、GPU 管线、Shader、纹理、数学、动画、优化)        │
└────────────────────────┬─────────────────────────────────────┘
                         ↓
┌──────────────────────────────────────────────────────────────┐
│                   Backend 渲染抽象层                           │
│                      backend/                                  │
│  (Driver 接口、CommandStream、Handle 系统、OpenGL/Vulkan/Metal)│
└────────────────────────┬─────────────────────────────────────┘
                         ↓
┌──────────────────────────────────────────────────────────────┐
│                   Engine 核心架构                              │
│                      engine/                                   │
│  (ECS 系统、资源管理、Renderable、Camera、Light、渲染循环)      │
└────────────────────────┬─────────────────────────────────────┘
                         ↓
         ┌───────────────┴───────────────┬─────────────────────┐
         ↓                               ↓                     ↓
┌─────────────────┐        ┌──────────────────────┐  ┌─────────────┐
│   材质系统       │        │    glTF 资源系统      │  │  工具链     │
│   material/     │        │  gltf/ + gltfio/     │  │  tools/     │
│  (材质编译、     │        │  (格式、加载、        │  │  (matc、    │
│   Shader生成)   │        │   动画系统)          │  │   cmgen等)  │
└─────────────────┘        └──────────────────────┘  └─────────────┘
         │                               │                     │
         └───────────────┬───────────────┘                     │
                         ↓                                     │
                ┌─────────────────┐                            │
                │   平台集成       │←───────────────────────────┘
                │   platforms/    │
                │  (Android/iOS/  │
                │   Desktop/Web)  │
                └────────┬────────┘
                         ↓
                ┌─────────────────┐
                │   示例项目       │
                │   samples/      │
                │  (Hello→Game)   │
                └─────────────────┘
```

---

## 📚 目录结构

### 1. graphics/ - 图形学与 GPU 理论基础

**核心内容：** 计算机图形学和 GPU 编程的基础知识

| 文档 | 主题 | 重要性 |
|------|------|--------|
| 01-rendering-fundamentals.md | 渲染基础、渲染方程、光照模型 | ⭐⭐⭐⭐⭐ |
| 02-pbr-theory.md | PBR 理论、BRDF、材质模型 | ⭐⭐⭐⭐⭐ |
| 03-gpu-pipeline.md | GPU 渲染管线、顶点/片段着色 | ⭐⭐⭐⭐⭐ |
| 04-shader-programming.md | GLSL 编程、Shader 基础 | ⭐⭐⭐⭐ |
| 05-material-properties.md | 材质属性详解 (Albedo/Roughness等) | ⭐⭐⭐⭐ |
| 06-texture-system.md | 纹理系统、Mipmap、压缩 | ⭐⭐⭐⭐ |
| 07-3d-math.md | 3D 数学基础、矩阵变换 | ⭐⭐⭐⭐⭐ |
| 08-animation-theory.md | 动画原理、骨骼动画、蒙皮 | ⭐⭐⭐ |
| 09-gpu-optimization.md | GPU 优化技术 | ⭐⭐⭐⭐ |
| 10-color-spaces.md | 颜色空间、Gamma 校正 | ⭐⭐⭐ |

**适合人群：** 所有用户（必读基础）

---

### 2. backend/ - Backend 渲染抽象层

**核心内容：** Filament 如何抽象和封装图形 API

| 文档 | 主题 | 重要性 |
|------|------|--------|
| 01-architecture-overview.md | Backend 架构概览 | ⭐⭐⭐⭐⭐ |
| 02-driver-abstraction.md | Driver 接口设计 | ⭐⭐⭐⭐ |
| 03-command-stream.md | 命令流和多线程 | ⭐⭐⭐⭐ |
| 04-resource-handles.md | Handle 系统 | ⭐⭐⭐ |
| 05-opengl-backend.md | OpenGL 后端实现 | ⭐⭐⭐ |
| 06-vulkan-backend.md | Vulkan 后端实现 | ⭐⭐⭐⭐ |
| 07-metal-backend.md | Metal 后端实现 | ⭐⭐⭐ |
| 08-platform-abstraction.md | 平台抽象层 | ⭐⭐⭐⭐ |
| 09-backend-comparison.md | Backend 对比分析 | ⭐⭐⭐ |

**适合人群：** 引擎开发者、图形程序员、性能优化工程师

---

### 3. engine/ - Engine 核心架构

**核心内容：** Filament Engine 的上层架构和 API

| 文档 | 主题 | 重要性 |
|------|------|--------|
| 01-core-concepts.md | Engine/Scene/View/Renderer | ⭐⭐⭐⭐⭐ |
| 02-entity-component.md | ECS 系统 | ⭐⭐⭐⭐⭐ |
| 03-resource-management.md | VertexBuffer/Texture/Material | ⭐⭐⭐⭐⭐ |
| 04-renderable-system.md | Renderable 组件 | ⭐⭐⭐⭐ |
| 05-camera-view.md | Camera 和 View 系统 | ⭐⭐⭐⭐ |
| 06-lighting-system.md | 光照系统 (Direct + IBL) | ⭐⭐⭐⭐⭐ |
| 07-render-loop.md | 渲染循环 | ⭐⭐⭐⭐ |
| 08-scene-graph.md | 场景图和变换 | ⭐⭐⭐ |
| 09-complete-pipeline.md | 完整渲染管线 | ⭐⭐⭐⭐⭐ |

**适合人群：** 所有 Filament 用户（核心必读）

---

### 4. material/ - 材质系统

**核心内容：** 材质定义、编译和运行时使用

| 文档 | 主题 | 重要性 |
|------|------|--------|
| 01-core-classes.md | Material/MaterialInstance | ⭐⭐⭐⭐ |
| 02-compilation-pipeline.md | 材质编译流程 | ⭐⭐⭐⭐ |
| 03-shader-generation.md | Shader 代码生成 | ⭐⭐⭐ |
| 04-material-definition.md | .mat 文件语法 | ⭐⭐⭐⭐⭐ |
| 05-graphics-concepts.md | 图形学概念应用 | ⭐⭐⭐ |
| 06-examples.md | 材质示例 | ⭐⭐⭐⭐ |
| 07-tools-usage.md | matc 工具使用 | ⭐⭐⭐⭐ |
| 08-runtime-usage.md | 运行时材质加载 | ⭐⭐⭐⭐ |
| 09-gltfio-material-loading.md | glTF 材质加载 | ⭐⭐⭐ |

**适合人群：** 材质开发者、技术美术

---

### 5. gltf/ + gltfio/ - glTF 资源系统

**核心内容：** glTF 格式和 Filament 加载机制

**gltf/ (格式规范):**

| 文档 | 主题 | 重要性 |
|------|------|--------|
| 01-format-specification.md | glTF 格式详解 | ⭐⭐⭐⭐ |
| 02-core-concepts.md | 核心概念 (Scene/Node/Mesh) | ⭐⭐⭐⭐⭐ |
| 03-extensions-support.md | 扩展系统 | ⭐⭐⭐ |
| 04-filament-loading.md | Filament 加载机制 | ⭐⭐⭐⭐ |
| 05-animation-system.md | 动画系统 | ⭐⭐⭐⭐ |
| 06-blender-workflow.md | Blender 工作流 | ⭐⭐⭐⭐ |
| 07-complete-workflow.md | 完整工作流 | ⭐⭐⭐ |
| 08-performance-optimization.md | 性能优化 | ⭐⭐⭐⭐ |
| 09-advanced-topics.md | 高级主题 | ⭐⭐⭐ |

**gltfio/ (加载实现):**

| 文档 | 主题 | 重要性 |
|------|------|--------|
| 01-core-classes.md | AssetLoader/ResourceLoader | ⭐⭐⭐⭐⭐ |
| 02-loading-phases.md | 加载阶段 | ⭐⭐⭐⭐ |
| 03-call-chains.md | 调用链路 | ⭐⭐⭐ |
| 04-object-creation.md | 对象创建 | ⭐⭐⭐ |
| 05-architecture.md | 架构设计 | ⭐⭐⭐ |
| 06-examples.md | 使用示例 | ⭐⭐⭐⭐ |
| 07-optimization.md | 加载优化 | ⭐⭐⭐ |

**适合人群：** 3D 美术、模型导入开发者

---

### 6. tools/ - Filament 工具链

**核心内容：** 资源准备和转换工具

| 文档 | 主题 | 重要性 |
|------|------|--------|
| 01-tools-overview.md | 工具链概览 | ⭐⭐⭐⭐⭐ |
| 02-matc-compiler.md | matc 材质编译器 | ⭐⭐⭐⭐⭐ |
| 03-cmgen-ibl.md | cmgen IBL 生成器 | ⭐⭐⭐⭐⭐ |
| 04-filamesh.md | filamesh 网格转换 | ⭐⭐⭐ |
| 05-mipgen.md | mipgen Mipmap 生成 | ⭐⭐⭐⭐ |
| 06-gltf-tools.md | glTF 工具集 | ⭐⭐⭐ |
| 07-debugging-tools.md | 调试工具 | ⭐⭐⭐⭐ |
| 08-asset-pipeline.md | 完整资产管线 | ⭐⭐⭐⭐⭐ |
| 09-advanced-usage.md | 高级用法 | ⭐⭐⭐ |

**适合人群：** 所有用户（资源准备必备）

---

### 7. platforms/ - 平台集成

**核心内容：** 各平台的具体集成方法

| 文档 | 主题 | 重要性 |
|------|------|--------|
| 01-platform-overview.md | 平台支持概览 | ⭐⭐⭐⭐⭐ |
| 02-android-integration.md | Android 集成 | ⭐⭐⭐⭐⭐ |
| 03-ios-integration.md | iOS 集成 | ⭐⭐⭐⭐⭐ |
| 04-desktop-windows.md | Windows 集成 | ⭐⭐⭐⭐ |
| 05-desktop-macos.md | macOS 集成 | ⭐⭐⭐⭐ |
| 06-desktop-linux.md | Linux 集成 | ⭐⭐⭐⭐ |
| 07-web-integration.md | Web/Emscripten 集成 | ⭐⭐⭐⭐ |
| 08-swapchain-creation.md | SwapChain 创建 | ⭐⭐⭐⭐⭐ |
| 09-lifecycle-management.md | 生命周期管理 | ⭐⭐⭐⭐ |

**适合人群：** 应用开发者（根据目标平台选择）

---

### 8. samples/ - 示例项目

**核心内容：** 从简单到复杂的完整示例

| 文档 | 主题 | 难度 |
|------|------|------|
| 01-hello-triangle.md | Hello Triangle | ⭐ |
| 02-textured-cube.md | 纹理立方体 | ⭐⭐ |
| 03-pbr-model.md | PBR 模型渲染 | ⭐⭐⭐ |
| 04-gltf-viewer.md | glTF 查看器 | ⭐⭐⭐ |
| 05-camera-control.md | 相机控制 | ⭐⭐⭐ |
| 06-animation-player.md | 动画播放器 | ⭐⭐⭐⭐ |
| 07-scene-manager.md | 场景管理器 | ⭐⭐⭐⭐ |
| 08-ui-rendering.md | UI 渲染 | ⭐⭐⭐ |
| 09-post-processing.md | 后处理效果 | ⭐⭐⭐⭐ |
| 10-complete-game.md | 完整游戏 | ⭐⭐⭐⭐⭐ |

**适合人群：** 所有用户（实践学习）

---

### 9. advanced/ - 高级主题 🔥 NEW

**核心内容：** 深入的渲染算法、优化技术和架构设计

#### 9.1 rendering/ - 渲染算法和技术

**核心内容：** 现代渲染算法的理论和实践

| 文档 | 主题 | 难度 |
|------|------|------|
| 01-shadow-techniques.md | 阴影技术详解 (CSM/VSM/PCF) | ⭐⭐⭐⭐ |
| 02-global-illumination.md | 全局光照系统 (IBL/Light Probes) | ⭐⭐⭐⭐⭐ |
| 03-ambient-occlusion.md | 环境光遮蔽 (SSAO/HBAO/GTAO) | ⭐⭐⭐⭐ |
| 04-advanced-lighting.md | 高级光照技术 (Area Lights/Volumetric) | ⭐⭐⭐⭐⭐ |
| 05-anti-aliasing.md | 抗锯齿技术 (MSAA/FXAA/TAA/SMAA) | ⭐⭐⭐⭐ |
| 06-render-pipelines.md | 渲染管线对比 (Forward/Deferred) | ⭐⭐⭐⭐ |
| 07-culling-techniques.md | 剔除技术 (Frustum/Occlusion) | ⭐⭐⭐ |
| 08-hdr-tone-mapping.md | HDR 和色调映射 (ACES/Filmic) | ⭐⭐⭐⭐ |
| 09-post-effects-impl.md | 后处理效果实现 (Bloom/DoF/Motion Blur) | ⭐⭐⭐⭐ |

#### 9.2 debugging/ - 调试和性能分析

**核心内容：** 系统化的调试方法和性能分析技术

| 文档 | 主题 | 难度 |
|------|------|------|
| 01-debugging-workflow.md | 调试工作流程 | ⭐⭐⭐ |
| 02-renderdoc-usage.md | RenderDoc 使用 | ⭐⭐⭐⭐ |
| 03-platform-debuggers.md | 平台调试工具 (Nsight/PIX/Metal) | ⭐⭐⭐⭐ |
| 04-performance-profiling.md | 性能分析方法 | ⭐⭐⭐⭐⭐ |
| 05-common-issues.md | 常见问题快速参考 | ⭐⭐⭐ |
| 06-benchmark-testing.md | 基准测试 | ⭐⭐⭐⭐ |
| 07-mobile-debugging.md | 移动端调试 | ⭐⭐⭐⭐ |
| 08-memory-profiling.md | 内存性能分析 | ⭐⭐⭐⭐ |
| 09-shader-debugging.md | 着色器调试 | ⭐⭐⭐⭐ |

#### 9.3 optimization/ - 优化实践

**核心内容：** 从理论到实践的完整性能优化指南

| 文档 | 主题 | 难度 |
|------|------|------|
| 01-texture-optimization.md | 纹理优化 | ⭐⭐⭐⭐ |
| 02-mesh-optimization.md | 网格优化 | ⭐⭐⭐⭐ |
| 03-drawcall-reduction.md | Draw Call 优化 | ⭐⭐⭐⭐⭐ |
| 04-memory-management.md | 内存管理 | ⭐⭐⭐⭐ |
| 05-mobile-optimization.md | 移动端优化 | ⭐⭐⭐⭐⭐ |
| 06-shader-optimization.md | 着色器优化 | ⭐⭐⭐⭐ |
| 07-cpu-gpu-sync.md | CPU-GPU 同步优化 | ⭐⭐⭐⭐ |
| 08-asset-pipeline-opt.md | 资产管线优化 | ⭐⭐⭐⭐ |
| 09-case-studies.md | 优化案例分析 | ⭐⭐⭐⭐⭐ |

#### 9.4 shaders/ - 着色器开发

**核心内容：** Filament Material 语言和自定义着色器开发

| 文档 | 主题 | 难度 |
|------|------|------|
| 01-material-lang-reference.md | Material 语言参考 | ⭐⭐⭐⭐⭐ |
| 02-custom-brdf.md | 自定义 BRDF | ⭐⭐⭐⭐⭐ |
| 03-effect-shaders.md | 特效着色器 | ⭐⭐⭐⭐ |
| 04-compute-shaders.md | 计算着色器 | ⭐⭐⭐⭐ |
| 05-shader-debugging.md | 着色器调试 | ⭐⭐⭐⭐ |
| 06-shader-patterns.md | 着色器模式 | ⭐⭐⭐ |
| 07-node-based-materials.md | 节点化材质 | ⭐⭐⭐⭐ |
| 08-shader-variants.md | 着色器变体 | ⭐⭐⭐⭐ |
| 09-advanced-techniques.md | 高级着色技术 | ⭐⭐⭐⭐⭐ |

#### 9.5 architecture/ - 架构设计

**核心内容：** 大型 Filament 项目的架构设计和最佳实践

| 文档 | 主题 | 难度 |
|------|------|------|
| 01-data-flow-analysis.md | 数据流分析 | ⭐⭐⭐⭐⭐ |
| 02-multithreading-design.md | 多线程设计 | ⭐⭐⭐⭐⭐ |
| 03-pipeline-architecture.md | 渲染管线架构 | ⭐⭐⭐⭐⭐ |
| 04-ecs-best-practices.md | ECS 最佳实践 | ⭐⭐⭐⭐ |
| 05-large-project-structure.md | 大型项目组织 | ⭐⭐⭐⭐⭐ |
| 06-plugin-system.md | 插件系统设计 | ⭐⭐⭐⭐ |
| 07-async-loading.md | 异步加载架构 | ⭐⭐⭐⭐⭐ |
| 08-resource-streaming.md | 资源流式加载 | ⭐⭐⭐⭐ |
| 09-cross-platform-arch.md | 跨平台架构 | ⭐⭐⭐⭐⭐ |

**适合人群：** 有一定经验的 Filament 开发者、性能工程师、架构师

**学习建议：**
- 先掌握基础知识（graphics/、engine/、material/）
- 根据项目需求选择性学习
- **debugging/** 和 **optimization/** 优先级最高
- **rendering/** 适合追求高质量渲染的项目
- **shaders/** 适合需要自定义材质效果
- **architecture/** 适合大型项目和引擎开发

---

## 🎯 推荐学习路径

### 路径 1: 完全新手 → Filament 开发者

```
第 1 周: 图形学基础
  ├─ graphics/07-3d-math.md (数学基础)
  ├─ graphics/01-rendering-fundamentals.md (渲染基础)
  └─ graphics/02-pbr-theory.md (PBR 理论)

第 2 周: Engine 核心
  ├─ engine/01-core-concepts.md (核心概念)
  ├─ engine/02-entity-component.md (ECS)
  └─ engine/03-resource-management.md (资源管理)

第 3 周: 材质和工具
  ├─ material/04-material-definition.md (材质定义)
  ├─ tools/02-matc-compiler.md (材质编译)
  └─ tools/03-cmgen-ibl.md (IBL 生成)

第 4 周: 实践项目
  ├─ samples/01-hello-triangle.md (Hello Triangle)
  ├─ samples/02-textured-cube.md (纹理立方体)
  └─ samples/03-pbr-model.md (PBR 模型)

第 5 周: 平台集成
  ├─ platforms/01-platform-overview.md (平台概览)
  └─ platforms/02-android-integration.md 或 03-ios-integration.md
```

### 路径 2: 有图形学基础 → Filament 应用

```
第 1 周: Engine 和 Backend
  ├─ engine/01-core-concepts.md
  ├─ engine/09-complete-pipeline.md
  └─ backend/01-architecture-overview.md

第 2 周: glTF 工作流
  ├─ gltf/02-core-concepts.md
  ├─ gltfio/01-core-classes.md
  └─ samples/04-gltf-viewer.md

第 3 周: 完整应用
  ├─ platforms/(选择目标平台)
  ├─ samples/05-camera-control.md
  └─ samples/07-scene-manager.md
```

### 路径 3: Unity/Unreal 开发者 → Filament

```
对比学习:
  ├─ engine/01-core-concepts.md (对比 Unity/Unreal 概念)
  ├─ engine/02-entity-component.md (对比 Unity ECS)
  └─ engine/08-scene-graph.md (对比 Transform Hierarchy)

快速上手:
  ├─ samples/04-gltf-viewer.md
  ├─ tools/08-asset-pipeline.md
  └─ platforms/(选择平台)
```

---

## 🔍 快速查找

### 按需求查找

| 需求 | 推荐文档 |
|------|---------|
| **学习 PBR 渲染** | graphics/02-pbr-theory.md, material/04-material-definition.md |
| **加载 glTF 模型** | gltf/02-core-concepts.md, gltfio/01-core-classes.md |
| **编译材质** | tools/02-matc-compiler.md, material/07-tools-usage.md |
| **生成 IBL** | tools/03-cmgen-ibl.md, engine/06-lighting-system.md |
| **Android 集成** | platforms/02-android-integration.md |
| **iOS 集成** | platforms/03-ios-integration.md |
| **性能优化** | graphics/09-gpu-optimization.md, tools/07-debugging-tools.md |
| **动画系统** | gltf/05-animation-system.md, samples/06-animation-player.md |
| **后处理效果** | engine/05-camera-view.md, samples/09-post-processing.md |

### 按角色查找

| 角色 | 核心文档 |
|------|---------|
| **应用开发者** | engine/, platforms/, samples/ |
| **技术美术** | graphics/, material/, tools/ |
| **引擎开发者** | backend/, engine/, material/ |
| **3D 美术** | gltf/, tools/03-cmgen-ibl.md, tools/08-asset-pipeline.md |
| **性能工程师** | graphics/09, backend/09, engine/09, tools/07 |

---

## 📖 术语表

详见 [GLOSSARY.md](./GLOSSARY.md)

---

## 🚀 开始学习

1. **确定学习路径**: 根据你的背景选择合适的学习路径
2. **循序渐进**: 从基础开始，不要跳过重要章节
3. **动手实践**: 每学完一个主题，运行相应的示例代码
4. **建立联系**: 理解各个模块之间的关联和数据流向

---

## 📝 文档约定

### 重要性标记

- ⭐⭐⭐⭐⭐ 核心必读
- ⭐⭐⭐⭐ 强烈推荐
- ⭐⭐⭐ 推荐阅读
- ⭐⭐ 可选阅读
- ⭐ 参考资料

### 难度标记

- ⭐ 初级（入门）
- ⭐⭐ 初级-中级
- ⭐⭐⭐ 中级
- ⭐⭐⭐⭐ 中级-高级
- ⭐⭐⭐⭐⭐ 高级

---

## 版本信息

- **文档版本**: 1.0
- **创建日期**: 2025-10-20
- **适用 Filament 版本**: v1.51.5+
- **维护状态**: 活跃维护

---

## 贡献指南

如果您发现文档错误或有改进建议：
1. 提交 Issue 说明问题
2. 提交 Pull Request 改进文档
3. 补充实际应用案例

---

**开始你的 Filament 学习之旅吧！**

> "从 GPU 到应用的完整知识链路，帮助你深入理解和掌握 Filament 渲染引擎！"
