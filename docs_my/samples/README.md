# Filament 示例项目文档

## 概述

本目录包含从简单到复杂的完整 Filament 示例项目文档。每个示例都提供完整的源代码、详细说明和最佳实践，帮助您快速上手并掌握 Filament 的各种功能。

## 📚 文档结构

### 基础示例

1. **[01-hello-triangle.md](01-hello-triangle.md)** - Hello Triangle
   - 最简单的 Filament 应用
   - Engine 和 SwapChain 创建
   - 基本渲染循环
   - 三角形绘制

2. **[02-textured-cube.md](02-textured-cube.md)** - 纹理立方体
   - VertexBuffer/IndexBuffer 创建
   - 纹理加载和采样
   - 相机控制
   - 完整 3D 渲染

3. **[03-pbr-model.md](03-pbr-model.md)** - PBR 模型渲染
   - PBR 材质系统
   - IBL 环境光
   - 材质参数控制
   - 真实感渲染

### 进阶示例

4. **[04-gltf-viewer.md](04-gltf-viewer.md)** - glTF 查看器
   - glTF 文件加载
   - 场景层级解析
   - 动画播放
   - 完整模型查看器

5. **[05-camera-control.md](05-camera-control.md)** - 相机控制
   - 轨道相机 (Orbit Camera)
   - 第一人称相机 (FPS Camera)
   - 相机平滑移动
   - 输入处理

6. **[06-animation-player.md](06-animation-player.md)** - 动画播放器
   - 骨骼动画系统
   - 动画混合
   - 动画控制 UI
   - 性能优化

### 高级示例

7. **[07-scene-manager.md](07-scene-manager.md)** - 场景管理器
   - 多场景管理
   - 资源异步加载
   - LOD 系统
   - 性能优化

8. **[08-ui-rendering.md](08-ui-rendering.md)** - UI 渲染
   - 2D UI 系统
   - 文本渲染
   - UI 布局
   - 事件处理

9. **[09-post-processing.md](09-post-processing.md)** - 后处理效果
   - Bloom 辉光
   - SSAO 环境光遮蔽
   - 色调映射
   - 自定义后处理

10. **[10-complete-game.md](10-complete-game.md)** - 完整游戏示例
    - 游戏架构设计
    - 多系统集成
    - 性能分析
    - 发布流程

## 🎯 学习路径

### 路径 1: 初学者

1. **Hello Triangle** (01) - 理解基本概念
2. **纹理立方体** (02) - 学习 3D 渲染
3. **PBR 模型** (03) - 掌握材质系统
4. **glTF 查看器** (04) - 加载实际模型

**适合人群**: Filament 初学者、图形编程新手

### 路径 2: 应用开发者

1. **glTF 查看器** (04) - 快速上手
2. **相机控制** (05) - 交互实现
3. **场景管理器** (07) - 项目组织
4. **UI 渲染** (08) - 完整应用

**适合人群**: 应用开发者、产品原型开发

### 路径 3: 游戏开发者

1. **动画播放器** (06) - 角色动画
2. **场景管理器** (07) - 关卡管理
3. **后处理** (09) - 视觉效果
4. **完整游戏** (10) - 系统集成

**适合人群**: 游戏开发者、技术美术

## 运行示例

### 从源码构建

```bash
# 克隆 Filament
git clone https://github.com/google/filament.git
cd filament

# 构建 Filament
./build.sh release

# 构建示例
cd samples
mkdir build && cd build
cmake ..
make

# 运行示例
./hello_triangle
```

### 使用预编译库

```bash
# 下载 Filament 库
wget https://github.com/google/filament/releases/download/v1.51.5/filament-v1.51.5-linux.tgz
tar -xzf filament-v1.51.5-linux.tgz

# 编译示例
g++ -std=c++17 \
    -I filament/include \
    -L filament/lib/x86_64 \
    -o hello_triangle \
    hello_triangle.cpp \
    -lfilament -lbackend
```

## 示例代码结构

每个示例都遵循统一的结构：

```
example_name/
├── src/
│   ├── main.cpp          # 主程序
│   ├── App.h/cpp         # 应用类
│   └── utils/            # 辅助工具
├── assets/
│   ├── materials/        # 材质文件
│   ├── models/           # 3D 模型
│   └── textures/         # 纹理
├── CMakeLists.txt        # 构建配置
└── README.md             # 说明文档
```

## 相关文档

- **[../engine/](../engine/)** - Engine 架构详解
- **[../material/](../material/)** - 材质系统
- **[../gltfio/](../gltfio/)** - glTF 加载
- **[../platforms/](../platforms/)** - 平台集成

## 开始学习

选择适合你的学习路径，从第一个示例开始，逐步掌握 Filament！

> "实践是最好的学习方式" - 运行示例，修改代码，理解原理！
