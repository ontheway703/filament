# gltfio 加载流程完整文档

本目录包含 Filament gltfio 库从资源加载到动画生成的完整技术文档。

## 文档结构

1. **[01-core-classes.md](01-core-classes.md)** - 核心类职责说明
   - AssetLoader
   - FilamentAsset
   - FilamentInstance
   - ResourceLoader
   - Animator

2. **[02-loading-phases.md](02-loading-phases.md)** - 加载流程三个阶段
   - Phase 1: 解析 glTF 文件
   - Phase 2: 加载资源
   - Phase 3: 渲染使用

3. **[03-call-chains.md](03-call-chains.md)** - 关键调用链详解
   - 文件加载调用链
   - 资源加载调用链
   - 动画播放调用链

4. **[04-object-creation.md](04-object-creation.md)** - 对象创建时机
   - 各种对象的创建时机和位置
   - 拥有者和生命周期

5. **[05-architecture.md](05-architecture.md)** - 架构关系图
   - 类之间的关系
   - 资源共享模式

6. **[06-examples.md](06-examples.md)** - 完整示例代码
   - 初始化到渲染的完整流程
   - 最佳实践

7. **[07-optimization.md](07-optimization.md)** - 性能优化要点
   - 资源共享
   - 延迟创建
   - 批量更新

## 快速开始

如果你是第一次阅读,建议按以下顺序:

1. 先阅读 `01-core-classes.md` 了解核心概念
2. 再看 `02-loading-phases.md` 理解整体流程
3. 查看 `06-examples.md` 的示例代码
4. 需要深入时参考 `03-call-chains.md` 和 `04-object-creation.md`

## 关键概念

- **Asset vs Instance**: 一个 Asset 可以创建多个 Instance,共享资源
- **两次遍历**: 第一次创建 Buffer,第二次创建 Entity
- **延迟 Animator**: Animator 只在资源加载完成后创建
- **资源共享**: VertexBuffer, IndexBuffer, Texture 在实例间共享

## 版本信息

- **文档版本**: 1.0
- **创建日期**: 2025-10-17
- **适用 Filament 版本**: 最新主分支
