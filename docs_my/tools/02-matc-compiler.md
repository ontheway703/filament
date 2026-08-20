# matc - 材质编译器详解

## 概述

`matc` (Material Compiler) 是 Filament 的材质编译器，将人类可读的 `.mat` 材质定义文件编译成优化的 `.filamat` 二进制包。这是材质开发工作流中最核心的工具。

### matc 的作用

```
输入: .mat 文件 (文本格式，包含 GLSL 代码)
  ↓
matc 编译器
  ├─ 解析材质定义
  ├─ 生成 Shader 变体
  ├─ GLSL → SPIR-V 编译
  ├─ SPIR-V 优化
  └─ 针对目标 Backend 转换
  ↓
输出: .filamat 文件 (二进制包，可在运行时加载)
```

**为什么需要编译：**
- **跨平台**: 同一个 .mat 可编译为 OpenGL/Vulkan/Metal 版本
- **优化**: 编译时优化 Shader 代码，移除未使用的代码
- **安全**: 保护 Shader 源码，只分发二进制
- **效率**: 运行时直接加载，无需解析和编译

---

## 基本用法

### 最简单的编译

```bash
# 编译材质
matc -o output.filamat input.mat

# 示例
matc -o my_material.filamat my_material.mat
```

### 指定目标平台

```bash
# 移动平台 (Android/iOS)
matc -p mobile -a opengl -o material_mobile.filamat input.mat

# 桌面平台
matc -p desktop -a vulkan -o material_desktop.filamat input.mat

# 所有平台 (默认)
matc -p all -o material_all.filamat input.mat
```

### 常用命令组合

```bash
# 优化编译 (生产环境)
matc -O -o optimized.filamat input.mat

# 调试编译 (保留符号信息)
matc -g -o debug.filamat input.mat

# 详细输出 (查看编译过程)
matc -v -o output.filamat input.mat

# 打印材质信息 (不编译)
matc --print input.mat
```

---

## 命令行参数详解

### 平台和后端选项

#### `-p, --platform <platform>`

指定目标平台：

| 平台值 | 说明 | 适用设备 |
|--------|------|----------|
| `mobile` | 移动平台 | Android, iOS |
| `desktop` | 桌面平台 | Windows, macOS, Linux |
| `all` | 所有平台 (默认) | 全平台 |

```bash
# 仅为移动端编译
matc -p mobile -o material.filamat input.mat
```

**注意：**
- `mobile` 会生成针对移动 GPU 优化的代码
- `all` 会包含所有平台的 Shader 变体，文件更大

#### `-a, --api <backend>`

指定图形 API：

| API 值 | 说明 | 平台支持 |
|--------|------|----------|
| `opengl` | OpenGL / OpenGL ES | 全平台 |
| `vulkan` | Vulkan | Android, Windows, Linux |
| `metal` | Metal | iOS, macOS |
| `all` | 所有 API (默认) | - |

```bash
# 仅生成 Vulkan 后端
matc -a vulkan -o material.filamat input.mat

# 移动端 OpenGL ES
matc -p mobile -a opengl -o material.filamat input.mat
```

**API 组合建议：**

```bash
# Android (推荐 Vulkan + OpenGL 回退)
matc -p mobile -a vulkan -o material_android.filamat input.mat

# iOS (仅 Metal)
matc -p mobile -a metal -o material_ios.filamat input.mat

# 桌面通用
matc -p desktop -a all -o material_desktop.filamat input.mat
```

### 优化选项

#### `-O, --optimize`

启用 Shader 优化：

```bash
matc -O -o optimized.filamat input.mat
```

**优化效果：**
- 死代码消除 (Dead Code Elimination)
- 常量折叠 (Constant Folding)
- 循环展开 (Loop Unrolling)
- 寄存器分配优化

**性能对比：**

| 版本 | Shader 大小 | 运行时性能 | 编译时间 |
|------|------------|-----------|---------|
| 未优化 | 100% | 基准 | 快 |
| 优化 `-O` | ~60% | +15% | 慢 2-3x |

**建议：**
- 开发时不优化，快速迭代
- 发布时必须优化

#### `-g, --generate-debug-info`

生成调试信息：

```bash
matc -g -o debug.filamat input.mat
```

**调试信息包括：**
- 源码行号映射
- 变量名保留
- 中间代码输出

**用途：**
- 使用 RenderDoc/Nsight 调试 Shader
- 定位 Shader 错误

**注意：**
- 调试版本文件更大
- 生产环境不要使用

### 输出控制

#### `-o, --output <file>`

指定输出文件：

```bash
matc -o materials/pbr_material.filamat input.mat
```

#### `--print`

打印材质信息，不编译：

```bash
matc --print input.mat
```

**输出示例：**

```
Material:
  Name: MyPBRMaterial
  Shading Model: lit
  Vertex Domain: object
  Parameters:
    - baseColor (float3)
    - metallic (float)
    - roughness (float)
  Samplers:
    - albedoMap (sampler2D)
  Variants: 16
```

#### `-v, --verbose`

详细输出：

```bash
matc -v -o output.filamat input.mat
```

**输出内容：**
- 编译阶段信息
- Shader 变体数量
- 每个变体的编译结果
- 警告和错误详情

### 高级选项

#### `--variant-filter <filter>`

过滤 Shader 变体：

```bash
# 仅编译有阴影的变体
matc --variant-filter=directionalLighting,shadow -o material.filamat input.mat
```

**常用过滤器：**
- `directionalLighting` - 方向光
- `dynamicLighting` - 动态光源
- `shadow` - 阴影
- `skinning` - 骨骼动画
- `fog` - 雾效

**用途：**
- 减小文件大小
- 加快编译速度
- 移除不需要的功能

#### `--java`

生成 Java 材质定义（Android）：

```bash
matc --java -o MyMaterial.java input.mat
```

**生成的 Java 代码：**

```java
public class MyMaterial {
    public static final String NAME = "MyPBRMaterial";

    public static class Parameter {
        public static final String BASE_COLOR = "baseColor";
        public static final String METALLIC = "metallic";
        public static final String ROUGHNESS = "roughness";
    }
}
```

#### `--metadata <json>`

嵌入元数据：

```bash
matc --metadata='{"author":"John","version":"1.0"}' -o material.filamat input.mat
```

---

## 完整示例

### 示例 1: 基础 PBR 材质编译

**输入文件 `pbr_material.mat`：**

```glsl
material {
    name : PBRMaterial,
    shadingModel : lit,
    parameters : [
        { type : float3, name : baseColor },
        { type : float,  name : metallic },
        { type : float,  name : roughness }
    ],
    requires : [ uv0 ]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        material.baseColor.rgb = materialParams.baseColor;
        material.metallic = materialParams.metallic;
        material.roughness = materialParams.roughness;
    }
}
```

**编译命令：**

```bash
# 桌面版本（开发）
matc -p desktop -a vulkan -v -o pbr_desktop.filamat pbr_material.mat

# 移动版本（生产）
matc -p mobile -a opengl -O -o pbr_mobile.filamat pbr_material.mat

# 所有平台（通用）
matc -p all -O -o pbr_all.filamat pbr_material.mat
```

### 示例 2: 带纹理的材质

**输入文件 `textured_material.mat`：**

```glsl
material {
    name : TexturedMaterial,
    shadingModel : lit,
    parameters : [
        {
            type : sampler2d,
            name : albedoMap,
            usage : color
        },
        {
            type : sampler2d,
            name : normalMap,
            usage : normal
        }
    ],
    requires : [ uv0, tangents ]
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);

        vec2 uv = getUV0();
        material.baseColor = texture(materialParams_albedoMap, uv);
        material.normal = texture(materialParams_normalMap, uv).xyz * 2.0 - 1.0;
    }
}
```

**编译：**

```bash
matc -O -o textured.filamat textured_material.mat
```

### 示例 3: 批量编译脚本

**Bash 脚本 `compile_materials.sh`：**

```bash
#!/bin/bash

MATERIALS_DIR="materials"
OUTPUT_DIR="compiled"
PLATFORM="mobile"
API="opengl"

mkdir -p "$OUTPUT_DIR"

for mat_file in "$MATERIALS_DIR"/*.mat; do
    filename=$(basename "$mat_file" .mat)
    echo "Compiling $filename..."

    matc -p "$PLATFORM" \
         -a "$API" \
         -O \
         -o "$OUTPUT_DIR/${filename}.filamat" \
         "$mat_file"

    if [ $? -eq 0 ]; then
        echo "✓ $filename compiled successfully"
    else
        echo "✗ $filename compilation failed"
        exit 1
    fi
done

echo "All materials compiled!"
```

**使用：**

```bash
chmod +x compile_materials.sh
./compile_materials.sh
```

---

## 错误诊断

### 常见错误和解决方案

#### 错误 1: Syntax Error

```
Error: syntax error at line 15
    material.baseColor.rgb = materialParams.baseColor
                                                       ^
Expected ';'
```

**原因：** GLSL 语法错误，缺少分号

**解决：**
```glsl
// 错误
material.baseColor.rgb = materialParams.baseColor

// 正确
material.baseColor.rgb = materialParams.baseColor;
```

#### 错误 2: Undefined Parameter

```
Error: undefined parameter 'baseColorMap'
```

**原因：** 使用了未声明的参数

**解决：**
在 `parameters` 块中声明：

```glsl
material {
    parameters : [
        { type : sampler2d, name : baseColorMap }
    ]
}
```

#### 错误 3: Incompatible Shading Model

```
Error: 'clearCoat' is only available in 'lit' shading model
```

**原因：** 某些材质属性仅特定 Shading Model 支持

**解决：**
```glsl
material {
    shadingModel : lit,  // 使用 lit 模型
}
```

#### 错误 4: Missing Required Attribute

```
Error: material requires 'tangents' but it's not provided
```

**原因：** 材质需要切线空间，但未声明

**解决：**
```glsl
material {
    requires : [ uv0, tangents ]  // 显式声明需求
}
```

### 调试技巧

#### 1. 使用 `--print` 检查材质信息

```bash
matc --print material.mat
```

#### 2. 启用详细输出

```bash
matc -v -o output.filamat material.mat
```

#### 3. 单独测试每个变体

```bash
# 仅编译最简单的变体
matc --variant-filter=none -o test.filamat material.mat
```

#### 4. 分阶段编译

```bash
# 先编译到 SPIR-V
matc --spirv -o material.spv material.mat

# 检查 SPIR-V
spirv-dis material.spv > material.spvasm
```

---

## 性能优化

### 编译时优化

#### 1. 使用 `-O` 优化

```bash
# 对比未优化和优化版本
matc -o unoptimized.filamat material.mat
matc -O -o optimized.filamat material.mat

# 查看大小差异
ls -lh *.filamat
```

**典型优化效果：**
- 文件大小减少 30-50%
- 运行时性能提升 10-20%

#### 2. 移除不需要的变体

```bash
# 如果不需要阴影
matc --variant-filter=-shadow -o material.filamat material.mat

# 如果不需要骨骼动画
matc --variant-filter=-skinning -o material.filamat material.mat
```

#### 3. 针对特定平台编译

```bash
# 不要编译全平台（文件大）
matc -p all -o large.filamat material.mat

# 针对目标平台
matc -p mobile -a opengl -o small.filamat material.mat
```

### 材质代码优化

#### 1. 避免复杂计算

```glsl
// 不推荐：复杂的数学运算
float complexValue = pow(sin(uv.x * 10.0), 2.0) * cos(uv.y * 10.0);

// 推荐：预计算或简化
float simpleValue = texture(precomputedMap, uv).r;
```

#### 2. 使用精度修饰符（移动端）

```glsl
// 移动端使用 mediump
mediump vec3 color = texture(albedoMap, uv).rgb;

// 关键计算使用 highp
highp vec3 worldPos = vec3(getWorldPosition());
```

#### 3. 减少纹理采样

```glsl
// 不推荐：多次采样同一纹理
vec3 color1 = texture(albedoMap, uv).rgb;
vec3 color2 = texture(albedoMap, uv + offset).rgb;

// 推荐：缓存采样结果
vec4 sample = texture(albedoMap, uv);
vec3 color1 = sample.rgb;
float alpha = sample.a;
```

---

## 集成到构建系统

### CMake 集成

```cmake
# FindMatc.cmake
find_program(MATC_EXECUTABLE
    NAMES matc
    PATHS ${FILAMENT_DIR}/bin
)

function(compile_material MAT_FILE OUTPUT_DIR)
    get_filename_component(MAT_NAME ${MAT_FILE} NAME_WE)
    set(OUTPUT_FILE ${OUTPUT_DIR}/${MAT_NAME}.filamat)

    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${MATC_EXECUTABLE}
            -p mobile
            -a opengl
            -O
            -o ${OUTPUT_FILE}
            ${MAT_FILE}
        DEPENDS ${MAT_FILE}
        COMMENT "Compiling material ${MAT_NAME}"
    )

    list(APPEND COMPILED_MATERIALS ${OUTPUT_FILE})
endfunction()

# 使用
compile_material(materials/pbr.mat ${CMAKE_BINARY_DIR}/materials)
```

### Gradle 集成（Android）

```gradle
// build.gradle
task compileMaterials {
    def matcPath = "${projectDir}/../filament/bin/matc"
    def materialsDir = "${projectDir}/src/main/materials"
    def outputDir = "${projectDir}/src/main/assets/materials"

    doLast {
        fileTree(materialsDir).include('**/*.mat').each { file ->
            def outputFile = "${outputDir}/${file.name.replace('.mat', '.filamat')}"

            exec {
                commandLine matcPath,
                    '-p', 'mobile',
                    '-a', 'opengl',
                    '-O',
                    '-o', outputFile,
                    file.absolutePath
            }
        }
    }
}

preBuild.dependsOn compileMaterials
```

---

## 总结

### matc 工作流

```
开发阶段:
  .mat 文件 → matc (无优化) → .filamat (快速迭代)

测试阶段:
  .mat 文件 → matc -g → .filamat (调试版本)

生产阶段:
  .mat 文件 → matc -O → .filamat (优化版本)
```

### 最佳实践

1. **版本匹配**: matc 版本必须与运行时 Filament 匹配
2. **平台针对**: 为不同平台编译不同版本
3. **优化编译**: 生产环境必须使用 `-O`
4. **变体过滤**: 移除不需要的 Shader 变体
5. **自动化**: 集成到构建系统，避免手动编译

### 相关文档

- **[../material/04-material-definition.md](../material/04-material-definition.md)** - .mat 文件语法
- **[../material/02-compilation-pipeline.md](../material/02-compilation-pipeline.md)** - 编译流程详解
- **[../material/07-tools-usage.md](../material/07-tools-usage.md)** - 材质工具基础
- **[07-debugging-tools.md](./07-debugging-tools.md)** - Shader 调试技巧

通过掌握 matc，您可以高效地将材质设计转化为 Filament 可使用的优化资源！
