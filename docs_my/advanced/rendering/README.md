# 渲染算法和技术

## 📖 概述

本目录深入讲解现代渲染算法在 Filament 中的实现，从理论推导到代码实践，帮助你掌握高质量渲染的核心技术。

### 主题范围

- **阴影技术**: Shadow Mapping、级联阴影（CSM）、方差阴影（VSM）
- **全局光照**: IBL、Light Probes、反射探针、光照烘焙
- **环境光遮蔽**: SSAO、HBAO、GTAO 算法实现
- **高级光照**: 区域光源、体积光、光照传播
- **抗锯齿**: MSAA、FXAA、TAA、SMAA 技术对比
- **渲染管线**: Forward vs Deferred Rendering
- **剔除优化**: Frustum、Occlusion、Back-face Culling
- **HDR 管线**: HDR 渲染、色调映射算子
- **后处理**: Bloom、景深、运动模糊

### 学习目标

完成本目录学习后，你将能够：

✅ 理解各种渲染算法的数学原理和实现细节
✅ 在 Filament 中实现高质量的阴影、光照、后处理效果
✅ 根据项目需求选择合适的渲染技术和参数
✅ 优化渲染算法的性能
✅ 解决渲染质量相关的问题

---

## 📚 文档列表

| 文档 | 主题 | 难度 | 重要性 |
|------|------|------|--------|
| [01-shadow-techniques.md](./01-shadow-techniques.md) | 阴影技术详解 | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| [02-global-illumination.md](./02-global-illumination.md) | 全局光照系统 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| [03-ambient-occlusion.md](./03-ambient-occlusion.md) | 环境光遮蔽 | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| [04-advanced-lighting.md](./04-advanced-lighting.md) | 高级光照技术 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| [05-anti-aliasing.md](./05-anti-aliasing.md) | 抗锯齿技术 | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| [06-render-pipelines.md](./06-render-pipelines.md) | 渲染管线对比 | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| [07-culling-techniques.md](./07-culling-techniques.md) | 剔除技术 | ⭐⭐⭐ | ⭐⭐⭐⭐ |
| [08-hdr-tone-mapping.md](./08-hdr-tone-mapping.md) | HDR 和色调映射 | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| [09-post-effects-impl.md](./09-post-effects-impl.md) | 后处理效果实现 | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |

---

## 🎯 推荐学习顺序

### 初学者路径

建议按照以下顺序学习，从简单到复杂：

```
第 1 步: 剔除和基础优化
  └─ 07-culling-techniques.md

第 2 步: 阴影基础
  └─ 01-shadow-techniques.md

第 3 步: HDR 和后处理
  ├─ 08-hdr-tone-mapping.md
  └─ 09-post-effects-impl.md

第 4 步: 抗锯齿
  └─ 05-anti-aliasing.md

第 5 步: 高级光照
  ├─ 02-global-illumination.md
  ├─ 03-ambient-occlusion.md
  └─ 04-advanced-lighting.md

第 6 步: 渲染管线
  └─ 06-render-pipelines.md
```

### 进阶路径

如果已有渲染经验，可以按主题深入：

**光照专题**: 01 → 02 → 03 → 04
**后处理专题**: 08 → 09 → 05
**性能专题**: 07 → 06

---

## 📖 文档内容概览

### 01-shadow-techniques.md - 阴影技术详解

**核心内容**:
- Shadow Mapping 基础原理
- 级联阴影贴图（Cascaded Shadow Maps, CSM）
- 方差阴影贴图（Variance Shadow Maps, VSM）
- 软阴影实现（PCF、PCSS）
- 阴影走样和阴影失真解决方案

**实践项目**:
- 实现基础 Shadow Mapping
- 实现 4 级级联阴影
- 优化阴影性能（分辨率、剔除）

**适合场景**: 所有需要真实阴影的项目

---

### 02-global-illumination.md - 全局光照系统

**核心内容**:
- 全局光照理论（渲染方程）
- IBL（Image-Based Lighting）深度解析
- Light Probes 和反射探针
- 光照烘焙（Lightmap Baking）
- 实时 GI 近似技术

**实践项目**:
- 创建 IBL 环境
- 实现 Light Probe 系统
- 烘焙静态光照

**适合场景**: 追求真实光照的高质量项目

---

### 03-ambient-occlusion.md - 环境光遮蔽

**核心内容**:
- SSAO（Screen Space Ambient Occlusion）
- HBAO（Horizon-Based Ambient Occlusion）
- GTAO（Ground Truth Ambient Occlusion）
- 算法对比和性能分析

**实践项目**:
- 实现 SSAO 效果
- 优化采样和模糊

**适合场景**: 需要增强深度感和细节的项目

---

### 04-advanced-lighting.md - 高级光照技术

**核心内容**:
- 区域光源（Area Lights）
- 体积光（Volumetric Lighting）
- 光照传播和 GI 近似
- LTC（Linearly Transformed Cosines）
- 光源重要性采样

**实践项目**:
- 实现矩形区域光
- 实现体积光效果

**适合场景**: 影视级渲染、建筑可视化

---

### 05-anti-aliasing.md - 抗锯齿技术

**核心内容**:
- MSAA（Multisample Anti-Aliasing）
- FXAA（Fast Approximate Anti-Aliasing）
- TAA（Temporal Anti-Aliasing）
- SMAA（Subpixel Morphological Anti-Aliasing）
- 技术对比和选择策略

**实践项目**:
- 实现 FXAA 后处理
- 实现 TAA 时域抗锯齿

**适合场景**: 所有需要高质量画面的项目

---

### 06-render-pipelines.md - 渲染管线对比

**核心内容**:
- Forward Rendering（前向渲染）
- Deferred Rendering（延迟渲染）
- Forward+ / Tiled Rendering
- 混合渲染管线
- Filament 的渲染管线设计

**实践项目**:
- 实现简单的 Deferred Renderer
- 对比 Forward vs Deferred 性能

**适合场景**: 引擎开发、大型场景渲染

---

### 07-culling-techniques.md - 剔除技术

**核心内容**:
- Frustum Culling（视锥剔除）
- Occlusion Culling（遮挡剔除）
- Back-face Culling（背面剔除）
- 层级包围盒（Bounding Volume Hierarchy）
- GPU-Driven Rendering

**实践项目**:
- 实现视锥剔除
- 实现简单的遮挡剔除

**适合场景**: 大型场景、开放世界游戏

---

### 08-hdr-tone-mapping.md - HDR 和色调映射

**核心内容**:
- HDR 渲染管线
- 色调映射算子（Reinhard、ACES、Filmic、Uncharted 2）
- 自动曝光（Auto Exposure）
- Bloom 和 Lens Flare
- 颜色分级（Color Grading）

**实践项目**:
- 实现 HDR 管线
- 实现 ACES 色调映射
- 实现自适应曝光

**适合场景**: 所有现代渲染项目（必备）

---

### 09-post-effects-impl.md - 后处理效果实现

**核心内容**:
- Bloom（泛光）
- Depth of Field（景深）
- Motion Blur（运动模糊）
- Color Correction（颜色校正）
- Vignette、Chromatic Aberration

**实践项目**:
- 实现高斯模糊 Bloom
- 实现 CoC-based DoF
- 实现相机运动模糊

**适合场景**: 影视级渲染、电影化游戏

---

## 🔗 相关文档

### 前置知识

学习本目录前，建议先掌握：

- [graphics/01-rendering-fundamentals.md](../../graphics/01-rendering-fundamentals.md) - 渲染基础
- [graphics/02-pbr-theory.md](../../graphics/02-pbr-theory.md) - PBR 理论
- [graphics/03-gpu-pipeline.md](../../graphics/03-gpu-pipeline.md) - GPU 管线
- [graphics/07-3d-math.md](../../graphics/07-3d-math.md) - 3D 数学
- [engine/06-lighting-system.md](../../engine/06-lighting-system.md) - 光照系统
- [material/04-material-definition.md](../../material/04-material-definition.md) - 材质定义

### 后续学习

完成本目录后，可以继续学习：

- [../optimization/](../optimization/) - 渲染优化实践
- [../shaders/](../shaders/) - 着色器开发
- [../debugging/](../debugging/) - 渲染调试
- [samples/09-post-processing.md](../../samples/09-post-processing.md) - 后处理示例

### 进阶资源

- [Real-Time Rendering 4th](http://www.realtimerendering.com/) - 第 7-11 章
- [GPU Gems 系列](https://developer.nvidia.com/gpugems/) - 光照和阴影章节
- [Filament 官方文档](https://google.github.io/filament/Materials.html) - 材质和光照

---

## 💡 学习建议

### 1. 理论与实践结合

每个算法都包含：
- **数学推导**: 理解原理
- **伪代码**: 理解流程
- **C++ 实现**: 直接可用的代码
- **效果对比**: 视觉验证

建议边学边写代码，在 Filament 中实际实现。

### 2. 使用调试工具

学习渲染算法时，调试工具至关重要：
- **RenderDoc**: 查看 Shadow Map、G-Buffer、后处理 Pass
- **ShaderToy**: 快速验证算法原型
- **Desmos**: 可视化数学公式

### 3. 参数调优

每个算法都有大量参数，建议：
- 创建参数 UI（使用 ImGui）
- 实时调整观察效果
- 记录最佳参数组合

### 4. 性能分析

每实现一个算法，都要分析性能：
- GPU 时间（使用 RenderDoc/Nsight）
- 内存占用
- 带宽消耗
- 移动端适配

### 5. 对比学习

对比不同算法的效果和性能：
- SSAO vs HBAO vs GTAO
- Reinhard vs ACES vs Filmic
- Forward vs Deferred

---

## 🎯 评估标准

### 初级掌握

- ✅ 理解各种渲染算法的基本原理
- ✅ 能在 Filament 中使用内置的渲染功能
- ✅ 能调整参数优化渲染质量

### 中级掌握

- ✅ 能实现基础的渲染算法（Shadow Mapping、SSAO、Bloom）
- ✅ 理解算法的性能特性
- ✅ 能根据项目需求选择合适的技术

### 高级掌握

- ✅ 能实现复杂的渲染算法（CSM、GI、TAA）
- ✅ 能优化算法性能
- ✅ 能扩展和改进现有算法
- ✅ 能设计自定义渲染管线

---

## 🔍 常见问题

### Q1: 需要先学习哪些数学知识？

**A:** 主要需要：
- 线性代数（矩阵、向量运算）
- 微积分基础（积分、微分）
- 概率统计（采样、蒙特卡洛）

推荐先学习 [graphics/07-3d-math.md](../../graphics/07-3d-math.md)。

### Q2: 如何选择合适的渲染技术？

**A:** 根据项目类型和目标平台：

| 项目类型 | 推荐技术 |
|---------|---------|
| 移动游戏 | Forward Rendering + FXAA + 简化阴影 |
| 桌面游戏 | Deferred/Forward+ + TAA + CSM + SSAO |
| 建筑可视化 | Deferred + 光照烘焙 + GTAO + ACES |
| 影视渲染 | 离线渲染 + 完整 GI + 多重采样 |

### Q3: 哪些技术是必须掌握的？

**A:** 优先级排序：

1. **必备**: HDR + 色调映射、基础阴影、剔除
2. **重要**: SSAO、抗锯齿、Bloom
3. **进阶**: GI、高级阴影、TAA
4. **可选**: 体积光、高级后处理

### Q4: 如何平衡质量和性能？

**A:** 使用质量档位系统：

```cpp
enum class QualityLevel {
    LOW,    // 移动端低端设备
    MEDIUM, // 移动端高端设备、桌面低端
    HIGH,   // 桌面中端
    ULTRA   // 桌面高端
};
```

根据档位动态调整：阴影分辨率、SSAO 采样数、后处理质量等。

### Q5: 学习顺序如何安排？

**A:** 建议顺序（基于依赖关系）：

```
07 剔除技术（基础优化）
  ↓
01 阴影技术（视觉提升）
  ↓
08 HDR + 色调映射（必备管线）
  ↓
09 后处理效果（视觉增强）
  ↓
05 抗锯齿（质量提升）
  ↓
03 SSAO（细节增强）
  ↓
02 全局光照（真实感）
  ↓
04 高级光照（顶级质量）
  ↓
06 渲染管线（架构理解）
```

---

**准备好深入学习渲染算法了吗？开始你的第一个文档吧！**

> "理解原理 → 实现代码 → 优化性能 → 掌握渲染！"
