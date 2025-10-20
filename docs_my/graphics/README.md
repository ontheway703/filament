# 图形学与GPU知识体系

本目录包含理解Filament渲染引擎所需的计算机图形学和GPU编程核心知识。这些文档从底层原理出发,帮助你深入掌握docs_my中其他文档(material、gltf、gltfio)的技术细节。

---

## 📚 文档结构

### 基础理论部分

1. **[01-rendering-fundamentals.md](01-rendering-fundamentals.md)** - 渲染基础
   - 实时渲染概述
   - 渲染方程和光的物理特性
   - 渲染管线总览
   - 光照类型(直接光、间接光)

2. **[02-pbr-theory.md](02-pbr-theory.md)** - PBR理论详解
   - 什么是PBR及其优势
   - BRDF(双向反射分布函数)
   - Cook-Torrance微表面模型
   - 能量守恒原则
   - 菲涅尔效应

3. **[10-color-spaces.md](10-color-spaces.md)** - 颜色空间
   - sRGB vs Linear空间
   - Gamma校正原理
   - 色彩管理流程
   - HDR和色调映射

### GPU技术部分

4. **[03-gpu-pipeline.md](03-gpu-pipeline.md)** - GPU渲染管线
   - 顶点处理阶段
   - 图元装配和光栅化
   - 片段着色
   - 深度测试、模板测试和混合
   - 帧缓冲输出

5. **[04-shader-programming.md](04-shader-programming.md)** - Shader编程基础
   - GLSL语法基础
   - Uniform vs Attribute vs Varying
   - 纹理采样技术
   - Shader内置函数
   - 精度修饰符(highp/mediump/lowp)

6. **[09-gpu-optimization.md](09-gpu-optimization.md)** - GPU优化技术
   - Draw Call优化
   - 批处理和GPU实例化
   - LOD(细节层次)系统
   - 视锥剔除和遮挡剔除
   - 几何和纹理压缩
   - GPU内存管理

### 材质与纹理部分

7. **[05-material-properties.md](05-material-properties.md)** - 材质属性详解
   - BaseColor/Albedo(反照率)
   - Metallic/Roughness工作流
   - Normal Mapping(法线贴图)
   - Ambient Occlusion(环境光遮蔽)
   - Emissive(自发光)
   - 特殊属性(clearCoat, subsurface等)

8. **[06-texture-system.md](06-texture-system.md)** - 纹理系统
   - 纹理过滤(nearest, linear, mipmap)
   - 包裹模式(repeat, clamp, mirror)
   - 纹理压缩格式(Draco, KTX2, Basis Universal)
   - Mipmap原理和生成
   - 各向异性过滤

### 数学与动画部分

9. **[07-3d-math.md](07-3d-math.md)** - 3D数学基础
   - 向量运算(点积、叉积)
   - 矩阵变换(平移、旋转、缩放)
   - 四元数旋转
   - 坐标系统(模型、世界、视图、裁剪空间)
   - TBN矩阵和切线空间

10. **[08-animation-theory.md](08-animation-theory.md)** - 动画原理
    - 关键帧动画原理
    - 骨骼动画和蒙皮(Skinning)
    - 动画插值算法
    - 动画混合技术
    - 变形目标(Morph Targets/BlendShapes)

---

## 🎯 学习路径

### 路径 1: 图形学基础学习者

如果你是图形学新手,想系统学习渲染原理:

1. **基础铺垫**: 先学习 `07-3d-math.md` 掌握数学基础
2. **渲染入门**: 阅读 `01-rendering-fundamentals.md` 了解渲染管线
3. **光照理论**: 学习 `02-pbr-theory.md` 理解PBR工作流
4. **材质系统**: 掌握 `05-material-properties.md` 的材质属性
5. **GPU实现**: 阅读 `03-gpu-pipeline.md` 和 `04-shader-programming.md`
6. **进阶主题**: 学习纹理、动画和优化相关文档

**推荐阅读顺序**: 07 → 01 → 02 → 10 → 05 → 03 → 04 → 06 → 09 → 08

**适合人群**: 图形学初学者、技术美术、游戏开发新手

### 路径 2: Filament材质开发者

如果你主要使用Filament开发材质:

1. **PBR核心**: 重点学习 `02-pbr-theory.md` 理解物理材质
2. **材质属性**: 深入 `05-material-properties.md` 掌握参数含义
3. **Shader编程**: 学习 `04-shader-programming.md` 编写自定义材质
4. **纹理应用**: 阅读 `06-texture-system.md` 优化纹理使用
5. **颜色管理**: 学习 `10-color-spaces.md` 处理色彩问题
6. **性能优化**: 参考 `09-gpu-optimization.md` 提升渲染性能

**推荐阅读顺序**: 02 → 05 → 04 → 06 → 10 → 09

**参考文档**:
- `../material/05-graphics-concepts.md` - Filament材质系统中的图形学应用
- `../material/04-material-definition.md` - 材质定义语法

**适合人群**: Filament用户、材质开发者、技术美术

### 路径 3: glTF资产优化工程师

如果你负责优化glTF资产性能:

1. **优化基础**: 先学习 `09-gpu-optimization.md` 了解优化技术
2. **纹理压缩**: 重点掌握 `06-texture-system.md` 的压缩方法
3. **3D数学**: 学习 `07-3d-math.md` 理解变换和空间转换
4. **动画优化**: 阅读 `08-animation-theory.md` 优化动画数据
5. **材质简化**: 参考 `05-material-properties.md` 简化材质
6. **GPU管线**: 了解 `03-gpu-pipeline.md` 掌握渲染瓶颈

**推荐阅读顺序**: 09 → 06 → 07 → 08 → 05 → 03

**参考文档**:
- `../gltf/08-performance-optimization.md` - glTF性能优化实践
- `../gltf/02-core-concepts.md` - glTF数据结构

**适合人群**: 性能工程师、3D美术、技术美术

### 路径 4: 游戏引擎开发者

如果你开发或研究渲染引擎:

1. **全面学习**: 按顺序阅读所有文档 (01-10)
2. **重点深入**:
   - GPU管线实现: `03-gpu-pipeline.md`
   - Shader系统: `04-shader-programming.md`
   - 优化技术: `09-gpu-optimization.md`
3. **实践参考**:
   - `../material/02-compilation-pipeline.md` - Filament材质编译流程
   - `../material/03-shader-generation.md` - Shader代码生成

**适合人群**: 引擎开发者、图形程序员、研究人员

---

## 🔗 与docs_my其他文档的关系

本目录的图形学知识是理解以下文档的理论基础:

### material 目录
- `material/05-graphics-concepts.md` - **直接应用**:本目录知识在Filament材质系统中的实现
- `material/02-compilation-pipeline.md` - **相关知识**: Shader编程(04)、GPU管线(03)
- `material/03-shader-generation.md` - **相关知识**: Shader编程(04)

### gltf 目录
- `gltf/02-core-concepts.md` - **相关知识**: 3D数学(07)、材质属性(05)
- `gltf/05-animation-system.md` - **相关知识**: 动画原理(08)、3D数学(07)
- `gltf/08-performance-optimization.md` - **相关知识**: GPU优化(09)、纹理系统(06)

### gltfio 目录
- `gltfio/07-optimization.md` - **相关知识**: GPU优化(09)

---

## 🌟 核心概念速查

### 渲染基础
- **渲染方程**: `Lo = ∫ BRDF × Li × cosθ dω`
- **PBR三要素**: BaseColor、Metallic、Roughness
- **光照模型**: 直接光照 + 间接光照(IBL)

### GPU管线
```
顶点数据 → 顶点着色器 → 图元装配 → 光栅化 → 片段着色器 → 帧缓冲
```

### BRDF公式(Cook-Torrance)
```
f(l,v) = D(h)×G(l,v,h)×F(v,h) / [4×(n·l)×(n·v)]

D = 法线分布函数 (GGX)
G = 几何函数 (Smith)
F = 菲涅尔项 (Schlick近似)
```

### 材质参数范围
- **baseColor**: RGB [0,1], sRGB空间
- **metallic**: [0,1] (0=非金属, 1=金属)
- **roughness**: [0,1] (0=光滑, 1=粗糙)
- **reflectance**: [0.35,1] (默认0.5)

### 纹理过滤
- **Nearest**: 最近邻(像素化)
- **Linear**: 双线性插值(平滑)
- **Mipmap**: 三线性插值(最佳质量)

### 坐标空间
```
模型空间 → 世界空间 → 视图空间 → 裁剪空间 → NDC → 屏幕空间
   ↓           ↓            ↓            ↓
Model      World        View      Projection
Matrix     Matrix       Matrix     Matrix
```

---

## 📖 推荐学习资源

### 在线资源
- **LearnOpenGL**: https://learnopengl.com/ (基础教程,英文)
- **Real-Time Rendering**: http://www.realtimerendering.com/ (权威书籍)
- **PBR Book**: https://pbr-book.org/ (离线渲染理论)
- **Filament文档**: https://google.github.io/filament/Filament.html

### 数学基础
- **3Blue1Brown**: https://www.youtube.com/c/3blue1brown (线性代数可视化)
- **Essence of Linear Algebra**: 矩阵和向量的几何直觉

### Shader编程
- **The Book of Shaders**: https://thebookofshaders.com/
- **Shadertoy**: https://www.shadertoy.com/ (Shader实验平台)

---

## ⚡ 快速概念对照表

| 概念 | 英文 | 作用 | 相关文档 |
|-----|------|-----|---------|
| 反照率 | Albedo | 物体固有颜色 | 05 材质属性 |
| 粗糙度 | Roughness | 表面光滑程度 | 02 PBR理论 |
| 金属度 | Metallic | 金属/非金属 | 02 PBR理论 |
| 法线贴图 | Normal Map | 表面细节 | 05 材质属性 |
| 环境光遮蔽 | AO | 缝隙阴影 | 05 材质属性 |
| 菲涅尔效应 | Fresnel | 边缘高光 | 02 PBR理论 |
| 各向异性 | Anisotropy | 拉丝金属效果 | 05 材质属性 |
| 次表面散射 | Subsurface | 半透明材质 | 05 材质属性 |
| 视差贴图 | Parallax | 深度错觉 | 06 纹理系统 |
| Mipmap | - | 纹理LOD | 06 纹理系统 |

---

## 🎓 使用建议

### 对于初学者
1. **不要跳过数学基础**(07-3d-math.md),它是一切的基础
2. **动手实践**:每学完一章,在Filament中创建示例验证
3. **由浅入深**:先理解概念,再深入公式推导
4. **参考可视化**:使用Shadertoy等工具可视化Shader效果

### 对于进阶用户
1. **关联阅读**:图形学知识与Filament实现对照学习
2. **性能优先**:重点关注优化相关章节(06, 09)
3. **实际应用**:将理论应用到实际项目优化中
4. **深入源码**:结合Filament源码理解实现细节

### 文档约定
- 📐 **公式推导**:重要公式提供数学推导过程
- 💡 **示例代码**:GLSL和C++代码示例
- ⚠️ **注意事项**:常见误区和陷阱
- 🔗 **跨文档引用**:相关知识点的链接

---

## 📝 版本信息

- **文档版本**: 1.0
- **创建日期**: 2025-10-19
- **适用Filament版本**: 最新主分支
- **更新策略**: 随Filament引擎更新同步维护

---

## 📬 反馈与贡献

如果你发现文档有误或有改进建议,欢迎:
1. 提交Issue反馈问题
2. 提交Pull Request改进文档
3. 补充实际应用案例

---

**开始学习**

选择适合你的学习路径,从第一篇文档开始,系统掌握计算机图形学核心知识!

> "理解原理,才能驾驭技术" - 深入图形学底层,让你的渲染技术更上一层楼!
