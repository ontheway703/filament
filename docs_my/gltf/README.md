# glTF 2.0 完整文档

本目录包含 glTF 2.0 格式规范、Filament 加载机制、Blender 工作流程以及性能优化的完整技术文档。

---

## 文档结构

### 基础部分

1. **[01-format-specification.md](01-format-specification.md)** - glTF 2.0 格式规范
   - glTF vs GLB 格式对比
   - JSON 结构和核心概念
   - 数据访问模式（Accessors, BufferViews, Buffers）
   - 资源引用和路径解析

2. **[02-core-concepts.md](02-core-concepts.md)** - glTF 核心概念详解
   - 场景（Scenes）和节点（Nodes）
   - 网格（Meshes）和几何体
   - 材质（Materials）系统
   - 纹理（Textures）系统
   - 动画（Animations）
   - 骨骼动画（Skins）

3. **[03-extensions-support.md](03-extensions-support.md)** - 扩展支持详解
   - 扩展系统概述（KHR, EXT, 厂商扩展）
   - cgltf 支持的扩展完整列表
   - Filament 支持的扩展详解
   - 扩展兼容性矩阵
   - 不支持扩展的降级策略

### Filament 集成部分

4. **[04-filament-loading.md](04-filament-loading.md)** - Filament 加载机制
   - AssetLoader 工作流程
   - ResourceLoader 系统
   - MaterialProvider 集成
   - 渐进式显示
   - 完整加载示例

5. **[05-animation-system.md](05-animation-system.md)** - 动画系统详解
   - Animator 架构
   - 动画播放和控制
   - 骨骼动画详解
   - 动画混合和交叉淡入淡出
   - 性能优化

### 工作流部分

6. **[06-blender-workflow.md](06-blender-workflow.md)** - Blender 工作流程
   - Blender 导出设置详解
   - 材质设置最佳实践
   - 常见问题和解决方案
   - 优化建议

7. **[07-complete-workflow.md](07-complete-workflow.md)** - 完整工作流程
   - 从建模到 Filament 的完整流程
   - 不同类型资产的工作流
   - 工具链整合

### 优化和进阶部分

8. **[08-performance-optimization.md](08-performance-optimization.md)** - 性能优化
   - 文件大小优化
   - 运行时性能优化
   - 内存优化
   - 加载性能优化

9. **[09-advanced-topics.md](09-advanced-topics.md)** - 高级主题
   - 多场景支持
   - 实例化（Instancing）
   - 自定义扩展处理
   - 与其他系统集成
   - 调试和验证

---

## 快速开始

### 学习路径

根据你的需求选择合适的学习路径：

#### 路径 1: glTF 格式学习者

如果你想深入理解 glTF 格式本身：

1. 阅读 `01-format-specification.md` 了解格式规范
2. 学习 `02-core-concepts.md` 掌握核心概念
3. 查看 `03-extensions-support.md` 了解扩展系统
4. 参考完整的 JSON 示例进行实践

**适合人群**: 工具开发者、引擎开发者、技术美术

#### 路径 2: Filament 使用者

如果你主要使用 Filament 加载 glTF 资产：

1. 快速浏览 `02-core-concepts.md` 了解基本概念
2. 重点阅读 `04-filament-loading.md` 学习加载机制
3. 如需动画，学习 `05-animation-system.md`
4. 查看 `08-performance-optimization.md` 进行优化

**适合人群**: Filament 应用开发者、游戏开发者

#### 路径 3: 美术工作流

如果你从 Blender 导出资产到 Filament：

1. 快速浏览 `02-core-concepts.md` 了解 glTF 的基本概念
2. 重点学习 `06-blender-workflow.md` 掌握导出设置
3. 阅读 `07-complete-workflow.md` 了解完整流程
4. 参考 `08-performance-optimization.md` 的优化建议

**适合人群**: 技术美术、3D 美术、关卡设计师

---

## glTF 2.0 概述

### 什么是 glTF？

**glTF** (Graphics Language Transmission Format) 是 Khronos Group 制定的 3D 场景传输格式，被称为"3D 界的 JPEG"。

### glTF 的优势

- ✅ **开放标准**: 由 Khronos Group 维护，广泛支持
- ✅ **高效传输**: 专为网络传输和实时渲染设计
- ✅ **完整功能**: 支持几何、材质、纹理、动画、骨骼等
- ✅ **可扩展**: 通过扩展系统支持新特性
- ✅ **工具链完善**: 大量导入/导出工具和验证工具

### glTF vs 其他格式

| 特性 | glTF | OBJ | FBX | COLLADA |
|------|------|-----|-----|---------|
| 开放标准 | ✅ | ✅ | ❌ | ✅ |
| 二进制格式 | ✅ (GLB) | ❌ | ✅ | ❌ |
| PBR 材质 | ✅ | ❌ | 部分 | 部分 |
| 动画 | ✅ | ❌ | ✅ | ✅ |
| 骨骼蒙皮 | ✅ | ❌ | ✅ | ✅ |
| 文件大小 | 小 | 中 | 大 | 大 |
| 扩展性 | ✅ | ❌ | ❌ | 部分 |

---

## glTF 格式变体

### 1. glTF (.gltf)

JSON 格式 + 外部文件：

```
model.gltf          # JSON 描述文件
model.bin           # 二进制几何数据
texture_base.png    # 纹理图片
texture_normal.png
```

**优点**:
- 人类可读的 JSON
- 易于调试和编辑
- 可选择性加载资源

**缺点**:
- 多个文件，不便传输
- HTTP 请求多

**适用场景**: 开发调试、版本控制

### 2. GLB (.glb)

自包含的二进制格式：

```
model.glb  # 包含所有数据的单一文件
```

**优点**:
- 单一文件，便于分发
- 文件体积最小
- 加载速度快

**缺点**:
- 不可读
- 难以手动编辑

**适用场景**: 生产环境、Web 应用、移动应用

### 3. glTF + 内嵌

JSON 中嵌入 base64 编码的二进制数据：

```
model.gltf  # 包含 base64 数据的 JSON
```

**优点**:
- 单一文件
- 人类可读（部分）

**缺点**:
- Base64 编码增大文件 ~33%
- 性能最差
- **已不推荐使用**

---

## 核心概念速览

### 场景层级结构

```
Scene (场景)
  └─ Node (节点)
       ├─ Transform (变换)
       ├─ Mesh (网格)
       │    └─ Primitive (图元)
       │          ├─ Attributes (顶点属性)
       │          ├─ Indices (索引)
       │          └─ Material (材质)
       ├─ Camera (相机)
       ├─ Light (灯光) [扩展]
       └─ Children (子节点)
```

### 材质模型

glTF 使用 **PBR Metallic-Roughness** 工作流：

```
Material
  ├─ baseColor (基础颜色)
  ├─ metallic (金属度: 0-1)
  ├─ roughness (粗糙度: 0-1)
  ├─ normal (法线贴图)
  ├─ occlusion (环境光遮蔽)
  └─ emissive (自发光)
```

### 动画系统

```
Animation
  ├─ Channel (通道)
  │    ├─ Target (目标节点 + 属性路径)
  │    └─ Sampler (采样器)
  └─ Sampler
       ├─ Input (时间轴数据)
       ├─ Output (值数据)
       └─ Interpolation (插值方式)
```

---

## Filament 支持情况

### 核心特性

| 特性 | 支持情况 |
|------|---------|
| 场景和节点 | ✅ 完全支持 |
| 网格和几何体 | ✅ 完全支持 |
| PBR 材质 | ✅ 完全支持 |
| 纹理 | ✅ 完全支持 (PNG, JPEG, KTX2) |
| 动画 | ✅ 完全支持 (关键帧、骨骼) |
| 蒙皮 | ✅ 完全支持 (最多 512 骨骼) |
| 变形目标 | ✅ 完全支持 |
| 相机 | ✅ 完全支持 |

### 材质扩展支持

| 扩展 | 支持情况 | 备注 |
|------|---------|------|
| `KHR_materials_pbrSpecularGlossiness` | ✅ 完全支持 | 传统 PBR 工作流 |
| `KHR_materials_unlit` | ✅ 完全支持 | 无光照材质 |
| `KHR_materials_clearcoat` | ✅ 完全支持 | 清漆层 |
| `KHR_materials_transmission` | ✅ 完全支持 | 透射/玻璃 |
| `KHR_materials_volume` | ✅ 完全支持 | 体积散射 |
| `KHR_materials_ior` | ✅ 完全支持 | 折射率 |
| `KHR_materials_specular` | ✅ 完全支持 | 镜面反射控制 |
| `KHR_materials_sheen` | ✅ 完全支持 | 光泽层（织物） |
| `KHR_materials_emissive_strength` | ✅ 完全支持 | 自发光强度 |
| `KHR_materials_iridescence` | ❌ 不支持 | 彩虹色 |
| `KHR_materials_anisotropy` | ❌ 不支持 | 各向异性 |

### 其他扩展支持

| 扩展 | 支持情况 |
|------|---------|
| `KHR_texture_transform` | ✅ 完全支持 |
| `KHR_texture_basisu` | ✅ 完全支持（通过 Ktx2Provider） |
| `KHR_lights_punctual` | ✅ 完全支持 |
| `KHR_mesh_quantization` | ✅ 自动处理（cgltf） |
| `KHR_draco_mesh_compression` | ✅ 完全支持 |

---

## 工具链

### glTF 验证和优化工具

- **[glTF Validator](https://github.khronos.org/glTF-Validator/)**: 在线验证工具
- **[gltf-pipeline](https://github.com/CesiumGS/gltf-pipeline)**: Node.js 命令行优化工具
- **[gltfpack](https://github.com/zeux/meshoptimizer)**: 高性能压缩工具
- **[Draco](https://github.com/google/draco)**: Google 的几何压缩工具

### Filament 工具

- **`gltf_viewer`**: Filament 提供的 glTF 查看器
- **`matc`**: 材质编译器（用于自定义材质）
- **`cmgen`**: IBL 环境贴图生成器

### 内容创建工具

- **Blender**: 开源 3D 建模软件，内置 glTF 导出器
- **Substance Painter**: PBR 纹理绘制工具
- **Marmoset Toolbag**: 实时渲染和烘焙工具
- **Unity / Unreal**: 游戏引擎，支持 glTF 导入/导出

---

## 常见使用场景

### Web 应用

```javascript
// Three.js 加载 glTF
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';

const loader = new GLTFLoader();
loader.load('model.glb', (gltf) => {
    scene.add(gltf.scene);
});
```

### 移动应用（Filament）

```cpp
// Filament C++ 加载 glTF
auto engine = Engine::create();
auto materials = createUbershaderProvider(engine, UBERSHADER_DATA, UBERSHADER_SIZE);
auto loader = AssetLoader::create({engine, materials});

std::vector<uint8_t> data = readFile("model.glb");
FilamentAsset* asset = loader->createAsset(data.data(), data.size());
```

### AR/VR 应用

- **ARCore / ARKit**: 原生支持 glTF
- **WebXR**: glTF 是推荐格式
- **Oculus / Meta Quest**: 支持 glTF 导入

---

## 文件大小参考

典型 glTF 资产的文件大小（未压缩）：

| 资产类型 | 顶点数 | 纹理 | 动画 | glTF 大小 | GLB 大小 |
|---------|--------|------|------|-----------|----------|
| 简单道具 | ~5K | 2K×2K ×2 | 无 | ~8 MB | ~6 MB |
| 角色模型 | ~15K | 2K×2K ×4 | 无 | ~18 MB | ~15 MB |
| 动画角色 | ~15K | 2K×2K ×4 | 10s | ~22 MB | ~18 MB |
| 场景 | ~100K | 4K×4K ×10 | 无 | ~150 MB | ~120 MB |

**优化后（Draco + KTX2）**:

| 资产类型 | 优化前 GLB | 优化后 GLB | 压缩率 |
|---------|-----------|-----------|-------|
| 简单道具 | ~6 MB | ~2 MB | 66% |
| 角色模型 | ~15 MB | ~4 MB | 73% |
| 动画角色 | ~18 MB | ~5 MB | 72% |

---

## 性能指标

### 加载性能（移动设备，中端）

| 操作 | JitShaderProvider | UbershaderProvider |
|------|------------------|-------------------|
| 解析 glTF | ~50ms | ~50ms |
| 创建材质 | ~2000ms (首次) | ~50ms |
| 加载纹理 | ~500ms | ~500ms |
| 创建几何体 | ~200ms | ~200ms |
| **总计** | **~2750ms** | **~800ms** |

### 运行时性能

- **三角形预算**: 移动端 ~100K, 桌面 ~500K
- **纹理内存**: 移动端 ~200MB, 桌面 ~1GB
- **骨骼数量**: Filament 支持最多 512 个骨骼
- **Draw Call**: 尽量保持 < 100 (移动端)

---

## 版本信息

- **文档版本**: 1.0
- **创建日期**: 2025-10-18
- **glTF 规范版本**: 2.0
- **适用 Filament 版本**: 最新主分支
- **cgltf 版本**: 1.14

---

## 相关资源

### 官方资源

- **[glTF 2.0 规范](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)**: 官方规范文档
- **[glTF 扩展注册表](https://github.com/KhronosGroup/glTF/tree/main/extensions)**: 官方扩展列表
- **[glTF 示例模型](https://github.com/KhronosGroup/glTF-Sample-Models)**: 官方示例资产
- **[glTF Validator](https://github.khronos.org/glTF-Validator/)**: 在线验证工具

### Filament 资源

- **[Filament 官方文档](https://google.github.io/filament/)**: Filament 引擎文档
- **[gltfio API 文档](https://google.github.io/filament/gltfio.html)**: gltfio 库 API
- **[材质系统文档](../material/README.md)**: Filament 材质系统
- **[gltfio 详细文档](../gltfio/README.md)**: gltfio 库详解

### 教程和工具

- **[Blender glTF 文档](https://docs.blender.org/manual/en/latest/addons/import_export/scene_gltf2.html)**: Blender 导出器文档
- **[glTF Tutorials](https://github.com/KhronosGroup/glTF-Tutorials)**: 官方教程
- **[cgltf GitHub](https://github.com/jkuhlmann/cgltf)**: cgltf 解析库

### 社区

- **[Khronos Forums](https://community.khronos.org/c/gltf-general)**: Khronos 官方论坛
- **[glTF Discord](https://discord.gg/khronos)**: glTF 社区 Discord
- **[Filament Discord](https://discord.gg/filament)**: Filament 社区
