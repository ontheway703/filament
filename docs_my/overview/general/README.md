# Filament 项目学习文档

欢迎来到 Filament 项目的学习文档！这个目录包含了详细的技术文档，帮助你深入理解 Filament 的编译系统、架构设计和相关技术。

## 📚 文档目录

### 1. [编译系统概览](01-build-system-overview.md)
- CMake 构建系统架构
- 项目整体结构分析
- 主要构建脚本和工具
- 支持的平台和构建目标
- 构建产物和目录结构

### 2. [工具链要求详解](02-toolchain-requirements.md)
- 编译器要求 (Clang 16+、MSVC 2019+)
- CMake 3.22.1+ 配置详解
- 平台特定工具链 (Android NDK、iOS、WebAssembly)
- 构建辅助工具 (Ninja、ccache)
- 环境验证和问题排查

### 3. [核心库架构](03-core-libraries.md)
- Filament 核心引擎库结构
- 渲染后端系统 (OpenGL、Vulkan、Metal)
- 材质系统 (filamat) 详解
- 工具库 (utils、math、geometry) 功能
- 库依赖关系和构建顺序

### 4. [第三方依赖详解](04-third-party-dependencies.md)
- 强制依赖库列表及用途
- SPIR-V 工具链 (glslang、spirv-tools、spirv-cross)
- 图形和渲染依赖 (Vulkan Memory Allocator、Dawn)
- 格式支持库 (glTF、图像、压缩)
- 依赖管理策略和版本控制

### 5. [平台特定构建](05-platform-specific-builds.md)
- Desktop 平台构建 (Linux/macOS/Windows)
- 移动平台构建 (Android/iOS)
- Web 平台构建 (WebAssembly + WebGL)
- 交叉编译工具链配置
- 平台优化策略

### 6. [材质编译系统](06-material-system.md)
- matc 材质编译器工作原理
- 材质定义语言语法
- Shader 生成和优化流程
- SPIR-V 字节码处理
- 运行时材质加载机制

### 7. [构建优化与调试](07-build-optimization-debug.md)
- Debug vs Release 配置策略
- 链接时优化 (LTO) 应用
- Sanitizers (ASAN/UBSAN/TSAN) 使用
- 材质调试器和性能分析工具
- 编译加速和缓存优化

### 8. [相关背景知识](08-background-knowledge.md)
- CMake 现代最佳实践
- C++20 新特性应用
- 物理渲染 (PBR) 理论基础
- 图形 API 对比 (OpenGL/Vulkan/Metal)
- 着色器编程和线性代数基础

## 🎯 快速开始

如果你是第一次接触 Filament 项目，推荐按以下顺序阅读：

1. **新手入门**: 先阅读 [编译系统概览](01-build-system-overview.md) 了解整体架构
2. **环境搭建**: 参考 [工具链要求](02-toolchain-requirements.md) 配置开发环境
3. **架构理解**: 学习 [核心库架构](03-core-libraries.md) 理解代码组织
4. **实践操作**: 根据 [平台特定构建](05-platform-specific-builds.md) 进行实际构建

## 🔧 实用工具

### 构建脚本
```bash
# 快速调试构建
./build.sh debug

# 发布版本构建
./build.sh release

# Android 构建
./build.sh -p android release

# 运行测试
./build.sh test
```

### 材质编译
```bash
# 编译材质
matc -o output.filamat input.mat

# 多平台材质
matc -p mobile -a opengl -a vulkan -o mobile.filamat input.mat
```

### 调试工具
```bash
# 启用材质调试器
./samples/gltf_viewer --matdbg model.gltf

# 内存检查
cmake -DFILAMENT_ENABLE_ASAN_UBSAN=ON ..
```

## 📖 学习路径建议

### 初学者路径
1. 阅读编译系统概览，理解项目结构
2. 搭建开发环境，成功构建项目
3. 学习核心库架构，理解模块划分
4. 尝试编写简单的材质文件

### 进阶开发者路径
1. 深入理解渲染后端抽象层设计
2. 学习材质编译系统的实现原理
3. 掌握平台特定的优化技巧
4. 使用调试工具进行性能分析

### 系统架构师路径
1. 分析整体依赖关系和模块设计
2. 理解跨平台抽象的实现策略
3. 学习构建系统的最佳实践
4. 掌握大型 C++ 项目的组织方法

## 🔗 相关资源

### 官方文档
- [Filament 官方文档](https://google.github.io/filament/)
- [Materials 材质系统文档](https://google.github.io/filament/Materials.html)
- [Filament API 参考](https://google.github.io/filament/Filament.html)

### 技术规范
- [物理渲染基础](https://google.github.io/filament/Filament.html#materialsystem)
- [glTF 2.0 规范](https://www.khronos.org/gltf/)
- [SPIR-V 规范](https://www.khronos.org/registry/SPIR-V/)

### 开发工具
- [CMake 官方文档](https://cmake.org/documentation/)
- [RenderDoc 图形调试器](https://renderdoc.org/)
- [Perfetto 性能分析](https://perfetto.dev/)

## 🤝 贡献指南

这些文档是基于对 Filament 项目源码的深入分析编写的。如果你发现任何错误或希望补充内容，欢迎：

1. 提出改进建议
2. 补充实际使用经验
3. 添加更多示例代码
4. 更新最新版本的变化

## 📝 版本说明

- **文档版本**: v1.0
- **基于 Filament 版本**: 最新 main 分支 (2024年)
- **最后更新**: 2024年9月

---

希望这些文档能帮助你更好地理解和使用 Filament 项目！如果你有任何问题或建议，欢迎提出讨论。