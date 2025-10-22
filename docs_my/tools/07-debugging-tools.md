# 调试和分析工具

## 概述

调试 Filament 应用需要图形调试工具来检查渲染管线、着色器、性能瓶颈等。本文档介绍主要的调试工具及其使用方法。

**核心工具**:
- **RenderDoc** - 跨平台图形调试器（OpenGL, Vulkan）
- **Nsight Graphics** - NVIDIA GPU 调试器（强大的 Vulkan/OpenGL 分析）
- **Xcode GPU Debugger** - Metal 调试（iOS/macOS）
- **Android GPU Inspector** - Android GPU 分析
- **Chrome DevTools** - WebGL 调试
- **Filament 内置工具** - 性能计数器和可视化

## RenderDoc - 跨平台图形调试器

### 概述

RenderDoc 是开源的图形调试器，支持 OpenGL、Vulkan，是 Filament 调试的首选工具。

**优势**:
- ✅ 免费开源
- ✅ 跨平台（Windows, Linux, macOS, Android）
- ✅ 易用界面
- ✅ 完整的帧捕获
- ✅ 着色器调试
- ✅ 资源查看

**下载**: https://renderdoc.org/

### 基本使用

#### 1. 启动应用

**方法 A: 通过 RenderDoc 启动**

```bash
# 1. 打开 RenderDoc
# 2. File → Launch Application
# 3. 设置:
Executable Path: /path/to/your_filament_app
Working Directory: /path/to/working_dir
Command-line Arguments: (如需要)

# 4. 点击 "Launch"
```

**方法 B: 注入到运行中的进程**

```bash
# 1. 运行你的应用
./your_filament_app

# 2. RenderDoc → File → Attach to Running Instance
# 3. 选择你的进程
```

#### 2. 捕获帧

```
运行应用后:
1. 按 F12 或 Print Screen 捕获当前帧
2. 或在 RenderDoc 中点击 "Capture Frame(s)"
```

#### 3. 分析捕获

**Event Browser** (事件浏览器):
```
查看完整渲染序列:
├── vkBeginCommandBuffer
├── vkCmdBeginRenderPass
│   ├── vkCmdBindPipeline (材质 A)
│   ├── vkCmdBindDescriptorSets (纹理绑定)
│   ├── vkCmdDrawIndexed (绘制椅子)
│   ├── vkCmdBindPipeline (材质 B)
│   ├── vkCmdDrawIndexed (绘制桌子)
│   └── ...
└── vkEndCommandBuffer
```

**Pipeline State** (管线状态):
```
查看当前绘制调用的完整状态:
- Vertex Shader 源码
- Fragment Shader 源码
- Vertex Input (顶点属性布局)
- Rasterizer State (剔除模式、深度测试)
- Bound Resources (纹理、UBO)
```

**Texture Viewer** (纹理查看器):
```
查看所有纹理:
- Color Attachments (颜色缓冲)
- Depth/Stencil Buffer (深度缓冲)
- Input Textures (输入纹理)
- 可视化: RGB, Alpha, Depth, Stencil
```

### 常见调试场景

#### 场景 1: 模型不显示/全黑

**步骤**:
```
1. 捕获帧
2. Event Browser → 找到你的 DrawCall
3. 检查:
   a. Vertex Shader 输入:
      - 顶点位置是否正确? (查看 Input 标签)
      - MVP 矩阵是否正确? (查看 Uniform Buffers)

   b. Fragment Shader 输出:
      - 输出颜色是否为黑色? (查看 Texture Viewer)

   c. Pipeline State:
      - 深度测试是否错误?
      - 背面剔除是否错误剔除了所有面?
      - Blend State 是否错误?

4. 常见问题:
   - MVP 矩阵错误 → 物体在视锥外
   - 法线全黑 → 光照计算错误
   - Depth Test 错误 → 被深度缓冲剔除
```

#### 场景 2: 纹理显示错误

**步骤**:
```
1. 捕获帧 → 选择 DrawCall
2. Pipeline State → Resources → Texture Bindings
3. 检查:
   a. 纹理是否绑定到正确的 Slot?
      Material shader: layout(set=1, binding=0) uniform sampler2D albedoMap;
      RenderDoc: Descriptor Set 1, Binding 0 → 查看绑定的纹理

   b. 纹理内容是否正确?
      - 点击纹理 → Texture Viewer 查看
      - 检查 Mipmap 级别
      - 检查 sRGB/Linear 格式

   c. 采样器设置:
      - Filter: Nearest/Linear/Anisotropic
      - Wrap Mode: Repeat/Clamp/Mirror

4. 常见问题:
   - 绑定到错误的 slot
   - 纹理未上传或损坏
   - sRGB 格式错误导致颜色错误
```

#### 场景 3: 着色器错误

**步骤**:
```
1. Event Browser → 选择 DrawCall
2. Pipeline State → Vertex/Fragment Shader → 点击 "Edit"
3. 进入着色器调试器:
   - 设置断点
   - 逐步执行
   - 查看变量值

4. Pixel Debugger (像素调试):
   - 右键 Texture Viewer 中的像素 → "Debug Pixel"
   - 查看该像素的完整计算过程
   - 查看每个变量的中间值
```

### 性能分析

#### 1. 识别性能瓶颈

```
Timeline (时间轴):
├── CPU Time: 5.2 ms
│   ├── Vertex Processing: 1.1 ms
│   ├── Rasterization: 0.8 ms
│   └── Fragment Processing: 3.3 ms ← 瓶颈!
└── GPU Time: 14.8 ms

分析:
- Fragment 阶段耗时最长 → 可能是复杂着色器或过度绘制
```

#### 2. 过度绘制检查

```
RenderDoc → Texture Viewer → Overlay:
- Overdraw Visualization → 显示每个像素被绘制的次数

颜色编码:
绿色: 1次
黄色: 2-3次
橙色: 4-5次
红色: 6+ 次 ← 严重过度绘制!

优化:
- 使用深度预通道 (Z-prepass)
- 从前往后排序不透明物体
- 剔除不可见物体
```

### RenderDoc 技巧

**1. 自定义标记**

在代码中添加标记方便调试:

```cpp
// Vulkan
vkCmdDebugMarkerBeginEXT(cmd, "Render Chairs");
vkCmdDrawIndexed(...);
vkCmdDebugMarkerEndEXT(cmd);

// OpenGL
glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Render Chairs");
glDrawElements(...);
glPopDebugGroup();
```

RenderDoc 中会显示:
```
Event Browser:
└── Render Chairs
    ├── vkCmdBindPipeline
    └── vkCmdDrawIndexed
```

**2. 比较帧**

```
Tools → Capture Comparison
- 对比两个捕获的差异
- 用于查找渐进式错误
```

**3. Python 脚本**

```python
# RenderDoc 支持 Python 脚本自动化

import renderdoc as rd

# 连接到 RenderDoc
cap = rd.OpenCaptureFile()

# 遍历所有绘制调用
for draw in cap.GetDrawcalls():
    if draw.name == "DrawIndexed":
        # 分析此 Draw Call
        print(f"Vertices: {draw.numIndices}")
```

## Nsight Graphics - NVIDIA 调试器

### 概述

Nsight Graphics 是 NVIDIA 的专业 GPU 调试器，功能比 RenderDoc 更强大，特别适合性能优化。

**下载**: https://developer.nvidia.com/nsight-graphics

**支持**:
- ✅ Vulkan (完整支持)
- ✅ OpenGL
- ✅ D3D11/D3D12
- ❌ 仅 NVIDIA GPU

### 核心功能

#### 1. GPU Trace (GPU 跟踪)

```
比 RenderDoc 更详细的性能分析:

Timeline:
├── CPU Queue Submit: 0.5 ms
├── GPU Execution: 12.3 ms
│   ├── Vertex Shader: 2.1 ms
│   ├── Rasterization: 1.5 ms
│   ├── Fragment Shader: 7.8 ms
│   │   ├── Texture Sampling: 4.2 ms ← 瓶颈
│   │   ├── Math Operations: 2.1 ms
│   │   └── Memory Access: 1.5 ms
│   └── ROP (Blend/Depth): 0.9 ms
└── Total: 12.8 ms
```

#### 2. Range Profiler (范围分析器)

```
选择渲染阶段 → 详细分析:

Selected Range: Shadow Pass
- Draw Calls: 150
- Triangles: 250K
- GPU Time: 3.2 ms
- Bottleneck: Memory Bandwidth (85% usage)

建议:
- 降低 Shadow Map 分辨率
- 使用 LOD
```

#### 3. 着色器性能分析

```
Shader Profiler:
Fragment Shader "pbr_material.frag":
├── Instruction Count: 245
├── Register Usage: 32/64
├── Occupancy: 75% (良好)
├── Hotspots:
│   ├── Line 45: normalize(normal) - 12% 时间
│   ├── Line 67: texture() - 35% 时间 ← 优化点
│   └── Line 89: pow(roughness, 2) - 8% 时间
└── 建议: 减少纹理采样次数
```

### 使用流程

```bash
# 1. 启动 Nsight Graphics
nsight-gfx

# 2. Launch → 选择应用
Executable: /path/to/your_app
API: Auto-detect

# 3. 运行并捕获帧
按 Space 或点击 "Capture Frame"

# 4. 分析
- GPU Trace: 查看完整时间线
- Range Profiler: 分析特定阶段
- Shader Profiler: 优化着色器
```

## Android GPU Inspector (AGI)

### 概述

Google 的 Android GPU 调试工具，专为移动设备优化。

**下载**: https://gpuinspector.dev/

**支持**:
- ✅ Vulkan (Android)
- ✅ OpenGL ES (Android)
- ✅ 性能计数器
- ✅ 系统级性能分析

### 使用

```bash
# 1. 安装 AGI
# 下载并解压

# 2. 连接设备
adb devices

# 3. 启动 AGI
./agi

# 4. Capture → System Profile
- 选择你的应用
- 捕获帧或性能跟踪

# 5. 查看:
- GPU Timeline
- CPU/GPU Usage
- Memory Bandwidth
- Thermal Throttling (过热降频)
```

### 移动端特定分析

```
AGI 显示:
├── GPU Utilization: 85% (高)
├── Memory Bandwidth: 12 GB/s (接近峰值 15 GB/s)
├── Shader Core Active: 92%
├── Texture Unit Active: 45%
├── ROP Active: 30%
└── Bottleneck: Memory Bandwidth ← 纹理带宽瓶颈

优化建议:
1. 使用压缩纹理 (ETC2/ASTC)
2. 降低纹理分辨率
3. 减少 Mipmap 级别
```

## Metal Debugger (Xcode)

### 使用 (iOS/macOS)

```
1. Xcode → Open Developer Tool → Instruments
2. 选择 "Metal System Trace" 模板
3. 选择你的应用
4. 录制

查看:
- GPU Timeline
- Shader Execution
- Buffer/Texture Usage
- Performance Metrics
```

**Frame Capture**:
```
Xcode → Debug → Capture GPU Frame

查看:
- Render Pass 结构
- Draw Call 列表
- Shader 源码
- Resource Inspector
```

## Chrome DevTools (WebGL)

### 使用

```
1. Chrome → F12 打开 DevTools
2. Performance 标签 → Record
3. 渲染几帧 → Stop

查看:
- JavaScript 性能
- WebGL Calls
- GPU Time
- Frame Rate

Rendering 标签:
- FPS Meter
- Paint Flashing
- Layer Borders
```

**SPECTOR.js** (WebGL 调试扩展):
```
1. 安装 Chrome 扩展: SPECTOR.js
2. 打开你的 WebGL 应用
3. 点击扩展图标 → Capture
4. 查看完整 WebGL 调用序列
```

## Filament 内置调试工具

### 1. 性能计数器

```cpp
// 启用性能计数器
Renderer::Builder()
    .featureLevel(FeatureLevel::FEATURE_LEVEL_3)
    .build(*engine);

// 读取计数器
auto& debug = engine->getDebugRegistry();
auto fps = debug.getPropertyInfo("d.renderer.fps");
auto triangles = debug.getPropertyInfo("d.renderer.triangles");

std::cout << "FPS: " << fps.value << std::endl;
std::cout << "Triangles: " << triangles.value << std::endl;
```

### 2. 可视化调试

```cpp
// 线框模式
RenderableManager::Builder(1)
    .geometry(0, RenderableManager::PrimitiveType::LINES, ...)
    .build(*engine, entity);

// 法线可视化 (需要 Geometry Shader 或预处理)
// 在材质中添加调试输出
material {
    fragment {
        void material(inout MaterialInputs material) {
            material.baseColor.rgb = normalize(getWorldNormal()) * 0.5 + 0.5;
        }
    }
}
```

### 3. 日志和断言

```cpp
// 启用 Filament 调试日志
utils::slog.i << "Debug info" << utils::io::endl;

// 调试构建会启用额外检查
#ifndef NDEBUG
    // 额外验证
#endif
```

## 调试工作流程

### 典型问题排查流程

**问题: 渲染结果不正确**

```
1. 捕获帧 (RenderDoc/Nsight)
   ↓
2. 定位问题 DrawCall
   - Event Browser 查找绘制调用
   ↓
3. 检查输入数据
   - Vertex Buffer: 顶点位置、法线、UV
   - Index Buffer: 索引
   - Uniform Buffers: MVP 矩阵、材质参数
   ↓
4. 检查着色器
   - Vertex Shader 输出是否正确?
   - Fragment Shader 计算是否正确?
   - 使用 Pixel Debugger 逐步调试
   ↓
5. 检查管线状态
   - Depth Test
   - Blend State
   - Culling
   ↓
6. 检查输出
   - Color Attachment
   - Depth Buffer
```

**问题: 性能低**

```
1. GPU Trace (Nsight/AGI)
   ↓
2. 识别瓶颈阶段
   - Vertex? Fragment? ROP? Memory?
   ↓
3. 分析瓶颈
   Vertex 瓶颈:
   - 减少顶点数
   - 优化 Vertex Shader

   Fragment 瓶颈:
   - 降低分辨率
   - 优化 Fragment Shader
   - 减少过度绘制

   Memory 瓶颈:
   - 压缩纹理
   - 降低纹理分辨率
   - 减少带宽使用
   ↓
4. 优化并验证
   - 应用优化
   - 重新测试
   - 对比前后性能
```

## 最佳实践

### 1. 常规调试

```
✅ 使用 Debug Markers 标记渲染阶段
✅ 定期捕获帧检查正确性
✅ 保持 RenderDoc/Nsight 随时可用
✅ 在多个设备上测试
✅ 使用 Validation Layers (Vulkan)
```

### 2. 性能优化

```
✅ 先测量再优化 (不要盲目优化)
✅ 关注最大瓶颈
✅ 一次优化一个问题
✅ 验证优化效果
✅ 记录性能基准
```

### 3. 移动端特殊考虑

```
✅ 测试过热降频 (Thermal Throttling)
✅ 测试低端设备
✅ 监控功耗
✅ 使用移动端特定工具 (AGI)
✅ 检查内存带宽使用
```

## 常见问题

**Q1: RenderDoc 无法捕获我的应用?**

```
检查:
1. Backend 是否支持? (OpenGL/Vulkan 支持，Metal 不支持)
2. 是否以正确权限运行?
3. 是否有冲突的调试工具?

解决:
- 尝试注入模式
- 检查 RenderDoc 日志
- 使用最新版本 RenderDoc
```

**Q2: 性能在调试器中更差?**

```
原因: 调试器有性能开销

解决:
1. 分析时关闭不必要的可视化
2. 使用 Range Profiler 而不是全帧分析
3. 对比相对性能而非绝对值
```

## 相关文档

- [../engine/06-rendering-pipeline.md](../engine/06-rendering-pipeline.md) - 渲染管线
- [../backend/](../backend/) - 后端架构
- [02-matc-compiler.md](02-matc-compiler.md) - 着色器编译

## 总结

**调试工具选择**:

| 平台 | 推荐工具 | 备选 |
|------|---------|------|
| Desktop (NVIDIA) | Nsight Graphics | RenderDoc |
| Desktop (AMD/Intel) | RenderDoc | - |
| Android | AGI | RenderDoc |
| iOS/macOS | Xcode Metal Debugger | - |
| Web | Chrome DevTools | SPECTOR.js |

**调试流程**:
1. 捕获帧
2. 定位问题
3. 分析数据
4. 修复验证

掌握这些工具，你可以高效地调试和优化 Filament 应用！
