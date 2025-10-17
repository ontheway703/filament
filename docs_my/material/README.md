# Filament 材质系统完整文档

本目录包含 Filament 材质系统从定义、编译到运行时使用的完整技术文档。

## 文档结构

1. **[01-core-classes.md](01-core-classes.md)** - 核心类职责说明
   - Material - 材质类
   - MaterialInstance - 材质实例
   - MaterialBuilder - 材质编译器建造者
   - TextureSampler - 纹理采样器
   - Package - 材质包

2. **[02-compilation-pipeline.md](02-compilation-pipeline.md)** - 材质编译流程
   - Phase 1: 解析 .mat 文件
   - Phase 2: GLSL 代码生成
   - Phase 3: SPIRV 编译与优化
   - Phase 4: 打包成 .filamat

3. **[03-shader-generation.md](03-shader-generation.md)** - Shader 代码生成详解
   - MaterialInfo 信息收集
   - UibGenerator - Uniform 块生成
   - SibGenerator - Sampler 块生成
   - CodeGenerator - 代码生成器

4. **[04-material-definition.md](04-material-definition.md)** - 材质定义语法
   - .mat 文件格式完整说明
   - material 块配置项详解
   - fragment/vertex 代码块
   - MaterialInputs 可用字段

5. **[05-graphics-concepts.md](05-graphics-concepts.md)** - 图形学概念
   - PBR 基础原理
   - Shading Models 详解
   - 材质属性说明
   - Shader 基础知识

6. **[06-examples.md](06-examples.md)** - 完整示例代码
   - 基础材质示例
   - 纹理材质示例
   - PBR 材质示例
   - 自定义着色器示例

7. **[07-tools-usage.md](07-tools-usage.md)** - 工具使用指南
   - matc 命令行详解
   - 编译选项说明
   - 调试和优化技巧
   - 最佳实践

8. **[08-runtime-usage.md](08-runtime-usage.md)** - 运行时使用
   - 材质加载
   - 材质实例创建和参数设置
   - 应用到 Renderable
   - 完整渲染流程集成

## 快速开始

如果你是第一次阅读，建议按以下顺序：

1. 先阅读 `05-graphics-concepts.md` 了解基础图形学概念
2. 再看 `04-material-definition.md` 学习如何定义材质
3. 查看 `06-examples.md` 的示例代码快速上手
4. 阅读 `07-tools-usage.md` 学习如何编译材质
5. 学习 `08-runtime-usage.md` 了解如何在应用中使用
6. 需要深入时参考 `01-core-classes.md` 和 `02-compilation-pipeline.md`

## 关键概念

### 材质 vs 材质实例

- **Material**: 编译后的 shader 程序，定义了渲染算法，是不可变的
- **MaterialInstance**: Material 的实例化，可以设置不同的参数（颜色、纹理等）
- 一个 Material 可以创建多个 MaterialInstance，每个实例可以有不同的外观

### 材质编译流程

```
.mat 源文件 → matc 编译器 → .filamat 二进制包 → Material::Builder → Material 对象
```

### 三个核心阶段

1. **定义阶段**: 编写 .mat 文件，定义材质属性和 shader 代码
2. **编译阶段**: 使用 matc 工具将 .mat 编译成 .filamat 二进制包
3. **运行时阶段**: 在应用中加载 .filamat，创建 Material 和 MaterialInstance

### 材质系统架构

```
材质定义 (.mat)
    ↓
材质编译 (matc/MaterialBuilder)
    ↓
材质包 (Package/filamat)
    ↓
材质对象 (Material)
    ↓
材质实例 (MaterialInstance) ← 设置参数
    ↓
渲染物体 (Renderable) ← 应用材质实例
```

## Shader 基础知识

### 顶点着色器 vs 片段着色器

- **顶点着色器**: 处理每个顶点，计算顶点位置、变换等
- **片段着色器**: 处理每个像素，计算最终颜色

### 材质参数类型

- **Uniform**: 常量参数（颜色、数值等）
- **Sampler**: 纹理采样器
- **Attribute**: 顶点属性（位置、法线、UV 等）

### PBR 核心概念

- **baseColor**: 基础颜色（反照率）
- **metallic**: 金属度 (0=非金属, 1=金属)
- **roughness**: 粗糙度 (0=光滑, 1=粗糙)
- **normal**: 法线贴图（表面细节）
- **ao**: 环境光遮蔽（阴影细节）

## 常用材质类型

| 材质类型 | Shading Model | 用途 |
|---------|--------------|------|
| **Lit** | lit | 标准 PBR 材质，支持光照 |
| **Unlit** | unlit | 无光照材质，用于 UI、天空盒等 |
| **Subsurface** | subsurface | 次表面散射，用于皮肤、蜡烛等 |
| **Cloth** | cloth | 布料材质，特殊的光照模型 |

## 材质参数示例

### 最简单的材质（纯色）

```glsl
material {
    name : SimpleColor,
    shadingModel : unlit
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        material.baseColor.rgb = vec3(1.0, 0.0, 0.0); // 红色
    }
}
```

### 带参数的材质

```glsl
material {
    name : ParameterizedColor,
    shadingModel : unlit,
    parameters : [
        { type : float3, name : color }
    ]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        material.baseColor.rgb = materialParams.color;
    }
}
```

## 版本信息

- **文档版本**: 1.0
- **创建日期**: 2025-10-18
- **适用 Filament 版本**: 最新主分支

## 相关资源

- Filament 官方文档: https://google.github.io/filament/
- 材质指南: https://google.github.io/filament/Materials.html
- Shader 语言参考: https://google.github.io/filament/Materials.html#materialmodel
