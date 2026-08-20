# 工具使用指南

本文档详细说明 matc (Material Compiler) 工具的使用方法和最佳实践。

---

## matc 命令行工具

**matc** 是 Filament 的材质编译器，将 `.mat` 文件编译成 `.filamat` 二进制包。

### 基本用法

```bash
matc [options] -o output.filamat input.mat
```

### 完整命令行参数

```
matc v1.x.x
Usage:
    matc [options] -o <output file> <input file>

Options:
   -h, --help                    Print this help
   -a, --api <backend>           Target API (all|opengl|vulkan|metal) [default: all]
   -p, --platform <platform>     Target platform (all|desktop|mobile) [default: all]
   -o, --output <file>           Output file name
   -f, --file <file>             Input file name (.mat)
   -O, --optimize <level>        Optimization level (none|preprocessor|size|performance) [default: performance]
   -g, --debug                   Generate debug info
   -E, --print                   Print generated GLSL
   -S, --save-variants           Save raw generated variants to files
   -V, --variant-filter <mask>   Filter variants (comma separated)
   -c, --check                   Check only, don't compile
   -q, --quiet                   Suppress all output except errors
   -I, --include <path>          Add include search path
   -D, --define <name=value>     Add preprocessor define

Specialization constants:
   --const-float <name=value>    Specialize float constant
   --const-int <name=value>      Specialize int constant
   --const-bool <name=value>     Specialize bool constant
```

---

## 常用参数详解

### -a, --api - 目标 API

指定目标渲染后端：

```bash
# 编译所有后端（默认）
matc -a all -o material.filamat material.mat

# 仅编译 OpenGL
matc -a opengl -o material.filamat material.mat

# 编译多个后端
matc -a opengl -a vulkan -o material.filamat material.mat
```

**可用值**:
- `all`: 所有后端（OpenGL + Vulkan + Metal）
- `opengl`: OpenGL ES / OpenGL
- `vulkan`: Vulkan
- `metal`: Metal (macOS/iOS)
- `webgpu`: WebGPU（如果启用）

**选择建议**:
- 开发时: 使用单一后端加速编译
- 发布时: 使用 `all` 确保兼容性

### -p, --platform - 目标平台

指定目标平台：

```bash
# 桌面平台（PC/Mac）
matc -p desktop -a opengl -o material.filamat material.mat

# 移动平台（Android/iOS）
matc -p mobile -a vulkan -o material.filamat material.mat

# 所有平台（默认）
matc -p all -a all -o material.filamat material.mat
```

**可用值**:
- `all`: 桌面 + 移动
- `desktop`: PC, Mac, Linux
- `mobile`: Android, iOS

**影响**:
- shader model 选择
- 优化策略
- 精度要求

### -O, --optimize - 优化级别

```bash
# 无优化（调试用，编译最快）
matc -O none -o debug.filamat material.mat

# 仅预处理器优化
matc -O preprocessor -o material.filamat material.mat

# 优化代码大小
matc -O size -o material.filamat material.mat

# 优化性能（默认，推荐发布使用）
matc -O performance -o material.filamat material.mat
```

**优化级别对比**:

| 级别 | 编译时间 | 文件大小 | 运行性能 | 用途 |
|-----|---------|---------|---------|------|
| none | 最快 | 大 | 慢 | 快速迭代 |
| preprocessor | 快 | 中 | 中 | 开发 |
| size | 慢 | 小 | 中 | 包大小敏感 |
| performance | 最慢 | 中 | 快 | 发布版本 |

### -g, --debug - 生成调试信息

```bash
# 生成调试信息（shader 包含行号、变量名等）
matc -g -o material_debug.filamat material.mat
```

**用途**:
- GPU 调试工具（RenderDoc, Nsight）
- 崩溃分析
- 性能分析

**注意**: 会增加文件大小

### -E, --print - 打印生成的 GLSL

```bash
# 打印生成的 shader 代码到 stdout
matc -E -o material.filamat material.mat > shaders.glsl
```

**用途**:
- 检查生成的代码
- 学习 shader 生成逻辑
- 调试材质问题

### -S, --save-variants - 保存变体

```bash
# 保存所有变体到单独文件
matc -S -o material.filamat material.mat

# 生成文件示例：
# material_0x00.vert  material_0x00.frag
# material_0x01.vert  material_0x01.frag
# ...
```

**用途**:
- 检查每个变体的代码
- 调试特定变体问题

### -V, --variant-filter - 过滤变体

```bash
# 只编译基础变体（加速编译）
matc -V directionalLighting,dynamicLighting -o material.filamat material.mat

# 排除不需要的变体
matc -V ~shadowReceiver,~skinning -o material.filamat material.mat
```

**可用变体标志**:
- `directionalLighting`: 方向光
- `dynamicLighting`: 动态光源
- `shadowReceiver`: 接收阴影
- `skinning`: 骨骼动画
- `fog`: 雾效
- `vsm`: VSM 阴影
- `ssr`: 屏幕空间反射
- `picking`: 拾取

### -I, --include - 包含路径

```bash
# 添加 include 搜索路径
matc -I /path/to/shaders -I ./common -o material.filamat material.mat
```

在 `.mat` 文件中使用：

```glsl
fragment {
    #include "common_functions.glsl"

    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        material.baseColor = myCustomFunction();
    }
}
```

### -D, --define - 预处理器定义

```bash
# 定义预处理器宏
matc -D USE_NORMAL_MAP=1 -D MAX_LIGHTS=4 -o material.filamat material.mat
```

在 shader 中使用：

```glsl
#ifdef USE_NORMAL_MAP
    material.normal = texture(normalMap, uv).xyz * 2.0 - 1.0;
#endif
```

### 常量特化

运行时可修改的编译时常量：

```bash
# 特化浮点常量
matc --const-float pi=3.14159 --const-float maxDistance=100.0 -o material.filamat material.mat

# 特化整数常量
matc --const-int maxIterations=16 -o material.filamat material.mat

# 特化布尔常量
matc --const-bool useAdvancedShading=true -o material.filamat material.mat
```

在运行时修改：

```cpp
Material* material = Material::Builder()
    .package(data, size)
    .constant("maxIterations", 32)  // 运行时特化
    .build(engine);
```

---

## 实战示例

### 示例 1: 快速开发模式

```bash
# 开发时：快速编译，单一后端
matc \
    -p desktop \
    -a opengl \
    -O none \
    -E \
    -o debug.filamat \
    material.mat
```

### 示例 2: 发布模式

```bash
# 发布时：完整优化，所有后端
matc \
    -p all \
    -a all \
    -O performance \
    -V ~picking \
    -o release.filamat \
    material.mat
```

### 示例 3: 移动平台优化

```bash
# 移动平台：优化大小，Vulkan 优先
matc \
    -p mobile \
    -a vulkan \
    -a opengl \
    -O size \
    -V directionalLighting,dynamicLighting \
    -o mobile.filamat \
    material.mat
```

### 示例 4: 调试特定变体

```bash
# 调试阴影接收变体
matc \
    -S \
    -E \
    -g \
    -V shadowReceiver \
    -o shadow_debug.filamat \
    material.mat

# 检查生成的文件
ls -lh material_*.vert material_*.frag
```

### 示例 5: 批量编译

```bash
#!/bin/bash
# compile_materials.sh

MATERIALS=(
    "pbr_textured"
    "glass"
    "metal"
    "ui_element"
)

for mat in "${MATERIALS[@]}"; do
    echo "Compiling $mat..."
    matc \
        -p all \
        -a all \
        -O performance \
        -q \
        -o "materials/${mat}.filamat" \
        "materials/${mat}.mat"
done

echo "Done!"
```

---

## 构建系统集成

### CMake 集成

```cmake
# FindMatc.cmake
find_program(MATC_EXECUTABLE matc
    HINTS ${FILAMENT_DIR}/bin
)

function(compile_material TARGET MAT_FILE)
    get_filename_component(MAT_NAME ${MAT_FILE} NAME_WE)
    set(OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/${MAT_NAME}.filamat")

    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${MATC_EXECUTABLE}
            -p all
            -a all
            -O performance
            -o ${OUTPUT_FILE}
            ${MAT_FILE}
        DEPENDS ${MAT_FILE}
        COMMENT "Compiling material: ${MAT_NAME}"
    )

    target_sources(${TARGET} PRIVATE ${OUTPUT_FILE})
endfunction()

# 使用
compile_material(MyApp materials/pbr.mat)
compile_material(MyApp materials/glass.mat)
```

### Gradle 集成（Android）

```gradle
// build.gradle

task compileMaterials {
    doLast {
        fileTree('src/main/materials').matching {
            include '**/*.mat'
        }.each { file ->
            def outFile = file.path.replace('.mat', '.filamat')
            exec {
                executable 'matc'
                args = [
                    '-p', 'mobile',
                    '-a', 'all',
                    '-O', 'performance',
                    '-o', outFile,
                    file.path
                ]
            }
        }
    }
}

preBuild.dependsOn compileMaterials
```

---

## 常见问题和解决方案

### 问题 1: 编译错误 - 未找到 include 文件

```
Error: Could not find include file: "common.glsl"
```

**解决**:
```bash
# 添加 include 路径
matc -I ./shaders/include -o material.filamat material.mat
```

### 问题 2: 编译时间过长

**解决**:
1. 使用 `-O none` 跳过优化
2. 使用 `-a opengl` 只编译单一后端
3. 使用 `-V` 过滤不需要的变体
4. 使用增量编译（只编译修改的材质）

### 问题 3: 文件过大

**解决**:
```bash
# 使用大小优化
matc -O size -o material.filamat material.mat

# 过滤变体
matc -V ~skinning,~fog,~ssr -o material.filamat material.mat

# 只编译需要的后端
matc -a vulkan -o material.filamat material.mat
```

### 问题 4: Shader 编译错误

```
Error: 'undeclared identifier: myVariable'
```

**调试步骤**:
1. 使用 `-E` 打印生成的代码
2. 检查生成的 shader
3. 确认 MaterialInputs 字段正确
4. 检查 prepareMaterial() 调用位置

```bash
# 打印并检查
matc -E -o material.filamat material.mat > generated.glsl
# 查看 generated.glsl 找到错误行
```

---

## 最佳实践

### 1. 开发工作流

```bash
# 开发脚本 (dev_compile.sh)
#!/bin/bash
matc \
    -p desktop \
    -a opengl \
    -O none \
    -g \
    -o build/debug.filamat \
    $1

# 使用
./dev_compile.sh materials/test.mat
```

### 2. 发布工作流

```bash
# 发布脚本 (release_compile.sh)
#!/bin/bash
matc \
    -p all \
    -a all \
    -O performance \
    -V ~picking \
    -q \
    -o release/$1.filamat \
    materials/$1.mat
```

### 3. 持续集成

```yaml
# .github/workflows/materials.yml
name: Compile Materials

on: [push]

jobs:
  compile:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2

      - name: Install Filament
        run: |
          wget https://github.com/google/filament/releases/download/v1.x.x/filament-v1.x.x-linux.tgz
          tar -xzf filament-*.tgz

      - name: Compile Materials
        run: |
          for mat in materials/*.mat; do
            filament/bin/matc \
              -p all \
              -a all \
              -O performance \
              -o "$(basename $mat .mat).filamat" \
              $mat
          done

      - name: Upload Artifacts
        uses: actions/upload-artifact@v2
        with:
          name: materials
          path: '*.filamat'
```

### 4. 版本管理

```bash
# 在材质文件中添加版本信息
material {
    name : "MyMaterial_v1.2.3",
    // ...
}

# 使用命名约定
mymat_v1.mat → mymat_v1.filamat
mymat_v2.mat → mymat_v2.filamat
```

### 5. 性能分析

```bash
# 编译并测量时间
time matc -O performance -o material.filamat material.mat

# 检查文件大小
ls -lh material.filamat

# 统计变体数量
matc -S -o material.filamat material.mat
ls material_*.vert | wc -l  # 变体数量
```

---

## 故障排除

### 收集诊断信息

```bash
# 完整诊断编译
matc \
    -E \
    -S \
    -g \
    --print \
    -o diagnostic.filamat \
    material.mat \
    2>&1 | tee compile.log

# 检查：
# - compile.log: 编译日志
# - diagnostic.filamat: 输出文件
# - material_*.vert/frag: 变体文件
```

### 验证材质包

```bash
# 使用 matinfo 工具检查材质包（如果可用）
matinfo material.filamat
```

---

## 总结

matc 工具的关键使用要点：

1. **开发时**: `-O none -a opengl -p desktop` 快速迭代
2. **发布时**: `-O performance -a all -p all` 完整优化
3. **调试**: 使用 `-E -S -g` 检查生成的代码
4. **优化**: 使用 `-V` 过滤不需要的变体
5. **集成**: 集成到构建系统自动化编译

掌握这些技巧可以大大提升材质开发效率。
