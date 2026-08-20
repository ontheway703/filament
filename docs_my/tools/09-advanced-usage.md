# 高级用法和技巧

## 概述

本文档介绍 Filament 工具链的高级用法、自定义扩展、性能优化技巧和实战问题解决方案。这些技巧适用于需要深度定制工作流程或解决复杂问题的高级用户。

**主题**:
- 🔧 自定义工具集成和扩展
- ⚡ 性能优化和批量处理
- 🐛 故障排除和调试技巧
- 🔬 高级材质技术
- 🎨 自定义着色器和效果
- 📦 资产优化策略

## 自定义工具集成

### 1. 使用 MaterialBuilder API

matc 实际上是 MaterialBuilder API 的命令行包装。你可以直接使用 API 进行自定义集成。

**C++ 集成示例**:

```cpp
#include <filamat/MaterialBuilder.h>

using namespace filamat;

bool compileMaterialProgrammatically(const char* source, const char* output) {
    MaterialBuilder builder;

    builder
        .name("CustomMaterial")
        .material(source)
        .platform(MaterialBuilder::Platform::MOBILE)
        .targetApi(MaterialBuilder::TargetApi::VULKAN)
        .optimization(MaterialBuilder::Optimization::PERFORMANCE);

    // 编译
    Package package = builder.build();

    if (!package.isValid()) {
        std::cerr << "Material compilation failed!" << std::endl;
        return false;
    }

    // 保存
    std::ofstream out(output, std::ios::binary);
    out.write((const char*)package.getData(), package.getSize());

    return true;
}
```

**Python 绑定示例**:

```python
# 使用 pybind11 创建 Python 绑定
import pyfilament

def compile_material(source_file, output_file, platform='mobile'):
    builder = pyfilament.MaterialBuilder()

    # 读取材质源码
    with open(source_file, 'r') as f:
        source = f.read()

    # 配置编译器
    builder.set_name(os.path.basename(source_file))
    builder.set_material(source)
    builder.set_platform(platform)
    builder.set_target_api('vulkan')
    builder.set_optimization('performance')

    # 编译
    package = builder.build()

    if not package.is_valid():
        raise Exception("Compilation failed")

    # 保存
    with open(output_file, 'wb') as f:
        f.write(package.get_data())

# 批量编译
for mat_file in glob.glob('materials/*.mat'):
    compile_material(mat_file, f'compiled/{mat_file}.filamat')
```

### 2. 自定义资产处理管线

**Node.js 资产服务器示例**:

```javascript
// asset-server.js
const express = require('express');
const { exec } = require('child_process');
const chokidar = require('chokidar');
const path = require('path');

const app = express();
const FILAMENT_TOOLS = '/path/to/filament/bin';

// 监视文件变化
const watcher = chokidar.watch('assets/source', {
    persistent: true
});

// 编译材质
function compileMaterial(srcPath) {
    const name = path.basename(srcPath, '.mat');
    const output = `assets/compiled/${name}.filamat`;

    const cmd = `${FILAMENT_TOOLS}/matc -p mobile -a vulkan -o ${output} ${srcPath}`;

    exec(cmd, (error, stdout, stderr) => {
        if (error) {
            console.error(`❌ Failed to compile ${name}:`, stderr);
        } else {
            console.log(`✅ Compiled ${name}`);
            // 通知客户端热重载
            notifyClients({ type: 'material_updated', name });
        }
    });
}

// 文件变化处理
watcher.on('change', (filePath) => {
    console.log(`File changed: ${filePath}`);

    if (filePath.endsWith('.mat')) {
        compileMaterial(filePath);
    } else if (filePath.endsWith('.png')) {
        processTexture(filePath);
    }
});

// WebSocket 热重载通知
const WebSocket = require('ws');
const wss = new WebSocket.Server({ port: 8080 });

function notifyClients(message) {
    wss.clients.forEach(client => {
        if (client.readyState === WebSocket.OPEN) {
            client.send(JSON.stringify(message));
        }
    });
}

app.listen(3000, () => {
    console.log('Asset server running on port 3000');
});
```

### 3. 自定义材质预处理器

**GLSL 预处理器示例**:

```python
#!/usr/bin/env python3
# material_preprocessor.py

import re
import sys

class MaterialPreprocessor:
    def __init__(self):
        self.includes = {}
        self.defines = {}

    def add_include_path(self, path):
        # 添加 include 搜索路径
        pass

    def process_includes(self, source):
        """处理 #include 指令"""
        def replace_include(match):
            filename = match.group(1)
            if filename in self.includes:
                return self.includes[filename]

            # 读取文件
            try:
                with open(f'includes/{filename}', 'r') as f:
                    content = f.read()
                    self.includes[filename] = content
                    return content
            except:
                return f'// Error: Could not find {filename}'

        return re.sub(r'#include\s+"([^"]+)"', replace_include, source)

    def process_defines(self, source):
        """处理自定义 #define"""
        for key, value in self.defines.items():
            source = source.replace(f'${{{key}}}', str(value))
        return source

    def process(self, source):
        """完整处理流程"""
        source = self.process_includes(source)
        source = self.process_defines(source)
        return source

# 使用示例
if __name__ == '__main__':
    preprocessor = MaterialPreprocessor()
    preprocessor.defines['USE_NORMAL_MAP'] = '1'
    preprocessor.defines['MAX_LIGHTS'] = '4'

    with open('material_template.mat', 'r') as f:
        source = f.read()

    processed = preprocessor.process(source)

    with open('material_processed.mat', 'w') as f:
        f.write(processed)
```

## 性能优化技巧

### 1. 并行构建

**Make/Ninja 并行化**:

```makefile
# Makefile
MATERIALS := $(wildcard materials/*.mat)
COMPILED := $(MATERIALS:materials/%.mat=compiled/%.filamat)

# 并行编译材质
.PHONY: materials
materials: $(COMPILED)

compiled/%.filamat: materials/%.mat
	@mkdir -p compiled
	matc -p mobile -a vulkan -O -o $@ $<

# 使用: make -j8 materials  # 8 并行任务
```

**GNU Parallel**:

```bash
#!/bin/bash
# 使用 GNU Parallel 并行处理

# 安装: brew install parallel (macOS)
#      apt-get install parallel (Linux)

# 并行编译材质
find materials -name "*.mat" | \
    parallel -j 8 \
    'matc -p mobile -a vulkan -O -o compiled/{/.}.filamat {}'

# 并行处理纹理
find textures -name "*.png" | \
    parallel -j 4 \
    'mipgen --compression=uastc {} ktx/{/.}.ktx2'

# 显示进度
find materials -name "*.mat" | \
    parallel --progress -j 8 \
    'matc -p mobile -a vulkan -O -o compiled/{/.}.filamat {}'
```

**xargs 并行**:

```bash
#!/bin/bash
# 使用 xargs 实现并行

# macOS
find materials -name "*.mat" -print0 | \
    xargs -0 -n 1 -P 8 -I {} \
    sh -c 'matc -p mobile -a vulkan -O -o compiled/$(basename {} .mat).filamat {}'

# Linux
find materials -name "*.mat" -print0 | \
    xargs -0 -n 1 -P 8 -I {} \
    bash -c 'matc -p mobile -a vulkan -O -o compiled/$(basename {} .mat).filamat {}'
```

### 2. 构建缓存

**ccache 风格的材质缓存**:

```bash
#!/bin/bash
# material_cache.sh

CACHE_DIR=".matcache"
mkdir -p "$CACHE_DIR"

compile_material_cached() {
    local input=$1
    local output=$2

    # 计算输入文件哈希
    local hash=$(shasum -a 256 "$input" | cut -d' ' -f1)
    local cache_file="$CACHE_DIR/$hash.filamat"

    # 检查缓存
    if [ -f "$cache_file" ]; then
        echo "✓ Cache hit: $(basename $input)"
        cp "$cache_file" "$output"
        return 0
    fi

    # 编译
    echo "⚙ Compiling: $(basename $input)"
    if matc -p mobile -a vulkan -O -o "$output" "$input"; then
        # 保存到缓存
        cp "$output" "$cache_file"
        return 0
    fi

    return 1
}

# 使用
compile_material_cached "material.mat" "output.filamat"
```

**内容寻址存储**:

```python
#!/usr/bin/env python3
# content_addressed_cache.py

import hashlib
import os
import shutil
import subprocess

class ContentAddressedCache:
    def __init__(self, cache_dir='.cache'):
        self.cache_dir = cache_dir
        os.makedirs(cache_dir, exist_ok=True)

    def get_hash(self, filepath):
        """计算文件内容哈希"""
        sha256 = hashlib.sha256()
        with open(filepath, 'rb') as f:
            for chunk in iter(lambda: f.read(4096), b''):
                sha256.update(chunk)
        return sha256.hexdigest()

    def get(self, key):
        """从缓存获取"""
        cache_file = os.path.join(self.cache_dir, key)
        if os.path.exists(cache_file):
            return cache_file
        return None

    def put(self, key, filepath):
        """添加到缓存"""
        cache_file = os.path.join(self.cache_dir, key)
        shutil.copy2(filepath, cache_file)

    def compile_material(self, input_file, output_file):
        """编译材质（带缓存）"""
        # 计算输入哈希
        input_hash = self.get_hash(input_file)

        # 检查缓存
        cached = self.get(input_hash)
        if cached:
            print(f"✓ Cache hit: {input_file}")
            shutil.copy2(cached, output_file)
            return True

        # 编译
        print(f"⚙ Compiling: {input_file}")
        result = subprocess.run([
            'matc', '-p', 'mobile', '-a', 'vulkan',
            '-O', '-o', output_file, input_file
        ])

        if result.returncode == 0:
            # 保存到缓存
            self.put(input_hash, output_file)
            return True

        return False

# 使用
cache = ContentAddressedCache()
cache.compile_material('material.mat', 'output.filamat')
```

### 3. 增量构建优化

**基于依赖图的增量构建**:

```python
#!/usr/bin/env python3
# dependency_tracker.py

import os
import json
import time

class DependencyTracker:
    def __init__(self, db_file='.deps.json'):
        self.db_file = db_file
        self.deps = self.load()

    def load(self):
        if os.path.exists(self.db_file):
            with open(self.db_file, 'r') as f:
                return json.load(f)
        return {}

    def save(self):
        with open(self.db_file, 'w') as f:
            json.dump(self.deps, f, indent=2)

    def get_mtime(self, filepath):
        return os.path.getmtime(filepath) if os.path.exists(filepath) else 0

    def needs_rebuild(self, target, sources):
        """检查是否需要重建"""
        # 目标不存在
        if not os.path.exists(target):
            return True

        target_mtime = self.get_mtime(target)

        # 任何源文件比目标新
        for source in sources:
            if self.get_mtime(source) > target_mtime:
                return True

        # 检查依赖变化
        dep_key = target
        if dep_key in self.deps:
            old_deps = set(self.deps[dep_key])
            new_deps = set(sources)
            if old_deps != new_deps:
                return True

        return False

    def update(self, target, sources):
        """更新依赖记录"""
        self.deps[target] = sources
        self.save()

# 使用示例
tracker = DependencyTracker()

material_file = 'materials/wood.mat'
include_files = ['includes/common.glsl', 'includes/pbr.glsl']
output_file = 'compiled/wood.filamat'

sources = [material_file] + include_files

if tracker.needs_rebuild(output_file, sources):
    print(f"Building {output_file}...")
    # 执行构建
    subprocess.run(['matc', '-o', output_file, material_file])
    tracker.update(output_file, sources)
else:
    print(f"Skipping {output_file} (up to date)")
```

## 高级材质技术

### 1. 动态材质变体生成

**材质变体系统**:

```python
#!/usr/bin/env python3
# material_variants.py

import itertools

class MaterialVariantGenerator:
    def __init__(self, template):
        self.template = template

    def generate_variants(self, options):
        """生成所有变体组合"""
        # 获取所有选项的笛卡尔积
        keys = options.keys()
        values = options.values()

        variants = []
        for combo in itertools.product(*values):
            defines = dict(zip(keys, combo))
            variant = self.apply_defines(defines)
            variants.append((defines, variant))

        return variants

    def apply_defines(self, defines):
        """应用 defines 到模板"""
        source = self.template

        for key, value in defines.items():
            if value:
                source = source.replace(f'// ${key}', f'#define {key}')
            else:
                source = source.replace(f'// ${key}', f'// #define {key}')

        return source

# 使用示例
template = """
material {
    name: PBRMaterial,
    // $USE_NORMAL_MAP
    // $USE_AO_MAP
    // $USE_EMISSIVE_MAP

    fragment {
        void material(inout MaterialInputs material) {
            #ifdef USE_NORMAL_MAP
                material.normal = texture(materialParams_normalMap, getUV0()).xyz;
            #endif

            #ifdef USE_AO_MAP
                material.ambientOcclusion = texture(materialParams_aoMap, getUV0()).r;
            #endif

            #ifdef USE_EMISSIVE_MAP
                material.emissive = texture(materialParams_emissiveMap, getUV0()).rgb;
            #endif
        }
    }
}
"""

generator = MaterialVariantGenerator(template)

# 定义选项
options = {
    'USE_NORMAL_MAP': [True, False],
    'USE_AO_MAP': [True, False],
    'USE_EMISSIVE_MAP': [True, False]
}

# 生成所有变体 (2^3 = 8 个)
variants = generator.generate_variants(options)

for i, (defines, source) in enumerate(variants):
    filename = f'variant_{i}.mat'
    print(f"Generating {filename}: {defines}")
    with open(filename, 'w') as f:
        f.write(source)
```

### 2. 着色器代码生成

**程序化着色器生成**:

```python
#!/usr/bin/env python3
# shader_codegen.py

class ShaderCodeGen:
    def __init__(self):
        self.code = []

    def add_line(self, line, indent=0):
        self.code.append('    ' * indent + line)

    def generate_texture_sampling(self, textures):
        """生成纹理采样代码"""
        for tex_name, tex_type in textures.items():
            if tex_type == 'color':
                self.add_line(f'vec3 {tex_name} = texture(materialParams_{tex_name}, getUV0()).rgb;')
            elif tex_type == 'scalar':
                self.add_line(f'float {tex_name} = texture(materialParams_{tex_name}, getUV0()).r;')
            elif tex_type == 'normal':
                self.add_line(f'vec3 {tex_name} = texture(materialParams_{tex_name}, getUV0()).xyz * 2.0 - 1.0;')

    def generate_material_func(self, textures):
        """生成完整 material 函数"""
        self.add_line('void material(inout MaterialInputs material) {')

        self.generate_texture_sampling(textures)

        self.add_line('material.baseColor.rgb = albedo;', 1)
        self.add_line('material.metallic = metallic;', 1)
        self.add_line('material.roughness = roughness;', 1)

        if 'normal' in textures:
            self.add_line('material.normal = normal;', 1)

        self.add_line('}')

        return '\n'.join(self.code)

# 使用
codegen = ShaderCodeGen()
textures = {
    'albedo': 'color',
    'metallic': 'scalar',
    'roughness': 'scalar',
    'normal': 'normal'
}

shader_code = codegen.generate_material_func(textures)
print(shader_code)
```

## 故障排除

### 1. 常见错误诊断

**材质编译错误诊断器**:

```python
#!/usr/bin/env python3
# matc_error_analyzer.py

import re
import subprocess

class MatcErrorAnalyzer:
    def __init__(self):
        self.error_patterns = {
            'syntax_error': r'(\w+\.glsl):(\d+):(\d+): error: (.+)',
            'undefined_var': r"error: '(\w+)' : undeclared identifier",
            'type_mismatch': r'error: cannot convert from \'(.+)\' to \'(.+)\'',
        }

    def analyze(self, stderr):
        """分析错误输出"""
        errors = []

        for line in stderr.split('\n'):
            for error_type, pattern in self.error_patterns.items():
                match = re.search(pattern, line)
                if match:
                    errors.append({
                        'type': error_type,
                        'line': line,
                        'match': match.groups()
                    })

        return errors

    def suggest_fix(self, error):
        """建议修复方案"""
        error_type = error['type']

        if error_type == 'undefined_var':
            var_name = error['match'][0]
            return f"变量 '{var_name}' 未定义。检查:\n" \
                   f"  1. 是否在 parameters 中声明?\n" \
                   f"  2. 是否有拼写错误?\n" \
                   f"  3. 是否在正确的作用域?"

        elif error_type == 'type_mismatch':
            from_type, to_type = error['match']
            return f"类型不匹配: {from_type} → {to_type}\n" \
                   f"  建议: 添加显式类型转换"

        return "未知错误类型"

    def compile_and_analyze(self, mat_file):
        """编译并分析错误"""
        result = subprocess.run(
            ['matc', '-o', '/dev/null', mat_file],
            capture_output=True,
            text=True
        )

        if result.returncode != 0:
            print(f"❌ 编译失败: {mat_file}\n")

            errors = self.analyze(result.stderr)

            for i, error in enumerate(errors, 1):
                print(f"错误 {i}:")
                print(f"  {error['line']}")
                print(f"  建议: {self.suggest_fix(error)}\n")
        else:
            print(f"✅ 编译成功: {mat_file}")

# 使用
analyzer = MatcErrorAnalyzer()
analyzer.compile_and_analyze('material.mat')
```

### 2. 性能分析

**着色器性能分析**:

```python
#!/usr/bin/env python3
# shader_profiler.py

import subprocess
import re

class ShaderProfiler:
    def profile_material(self, filamat_file):
        """分析编译后材质的性能"""
        # 使用 spirv-dis 反汇编 SPIR-V
        result = subprocess.run(
            ['spirv-dis', filamat_file],
            capture_output=True,
            text=True
        )

        spirv = result.stdout

        # 统计指令
        stats = {
            'texture_samples': spirv.count('OpImageSampleImplicitLod'),
            'arithmetic_ops': spirv.count('OpFMul') + spirv.count('OpFAdd'),
            'branches': spirv.count('OpBranch'),
            'loops': spirv.count('OpLoopMerge'),
        }

        return stats

    def suggest_optimizations(self, stats):
        """建议优化"""
        suggestions = []

        if stats['texture_samples'] > 8:
            suggestions.append(
                f"⚠️ 纹理采样过多 ({stats['texture_samples']})\n"
                "   建议: 合并纹理图集，使用 atlas"
            )

        if stats['loops'] > 0:
            suggestions.append(
                "⚠️ 包含循环\n"
                "   建议: 展开循环或使用常量"
            )

        return suggestions

# 使用
profiler = ShaderProfiler()
stats = profiler.profile_material('material.filamat')
print(f"性能统计: {stats}")

for suggestion in profiler.suggest_optimizations(stats):
    print(suggestion)
```

### 3. 调试技巧

**材质参数可视化**:

```cpp
// 调试材质 - 可视化不同参数

material {
    name: DebugMaterial,

    parameters: [
        { type: int, name: debugMode },
        { type: sampler2D, name: albedoMap },
        { type: sampler2D, name: normalMap },
        { type: sampler2D, name: ormMap }
    ],

    fragment {
        void material(inout MaterialInputs material) {
            vec2 uv = getUV0();

            if (materialParams.debugMode == 0) {
                // 正常模式
                material.baseColor = texture(materialParams_albedoMap, uv);
            } else if (materialParams.debugMode == 1) {
                // UV 可视化
                material.baseColor.rgb = vec3(uv, 0.0);
            } else if (materialParams.debugMode == 2) {
                // 法线可视化
                vec3 normal = texture(materialParams_normalMap, uv).xyz * 2.0 - 1.0;
                material.baseColor.rgb = normal * 0.5 + 0.5;
            } else if (materialParams.debugMode == 3) {
                // Roughness 可视化
                float roughness = texture(materialParams_ormMap, uv).g;
                material.baseColor.rgb = vec3(roughness);
            } else if (materialParams.debugMode == 4) {
                // Metallic 可视化
                float metallic = texture(materialParams_ormMap, uv).b;
                material.baseColor.rgb = vec3(metallic);
            }
        }
    }
}
```

## 资产优化策略

### 1. 自动LOD生成

```python
#!/usr/bin/env python3
# generate_lods.py

import subprocess

def generate_lod(input_glb, output_glb, simplify_ratio):
    """使用 gltfpack 生成 LOD"""
    subprocess.run([
        'gltfpack',
        '-i', input_glb,
        '-o', output_glb,
        '-si', str(simplify_ratio)  # 简化比例
    ])

# 生成多级 LOD
lod_levels = [
    (1.0, 'model_lod0.glb'),    # 原始
    (0.7, 'model_lod1.glb'),    # 70%
    (0.4, 'model_lod2.glb'),    # 40%
    (0.2, 'model_lod3.glb'),    # 20%
]

for ratio, output in lod_levels:
    print(f"Generating LOD: {output} ({ratio*100}%)")
    generate_lod('model_original.glb', output, ratio)
```

### 2. 纹理自动降级

```python
#!/usr/bin/env python3
# texture_downsample.py

from PIL import Image
import os

def generate_texture_mip_chain(input_file, output_dir, levels=4):
    """生成纹理 mip 链"""
    img = Image.open(input_file)
    basename = os.path.splitext(os.path.basename(input_file))[0]

    for i in range(levels):
        scale = 2 ** i
        size = (img.width // scale, img.height // scale)

        if size[0] < 4 or size[1] < 4:
            break

        resized = img.resize(size, Image.LANCZOS)
        output_file = f"{output_dir}/{basename}_{size[0]}x{size[1]}.png"
        resized.save(output_file)
        print(f"Generated: {output_file}")

# 使用
generate_texture_mip_chain('albedo_4k.png', 'textures', levels=5)
# 生成: 4096, 2048, 1024, 512, 256
```

## 高级脚本示例

### 完整自动化工作流

```bash
#!/bin/bash
# ultimate_asset_builder.sh

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FILAMENT_TOOLS="${FILAMENT_DIR}/bin"

# 配置
PLATFORMS=("android" "ios" "desktop")
BUILD_TYPES=("debug" "release")

# 日志
LOG_FILE="build_$(date +%Y%m%d_%H%M%S).log"

log() {
    echo "[$(date +'%H:%M:%S')] $1" | tee -a "$LOG_FILE"
}

# 清理
clean_all() {
    log "Cleaning..."
    rm -rf compiled/*
    rm -rf final/*
}

# 验证工具
check_tools() {
    for tool in matc cmgen mipgen gltf-transform; do
        if ! command -v $tool &> /dev/null; then
            log "ERROR: Missing tool: $tool"
            exit 1
        fi
    done
}

# 并行构建材质
build_materials_parallel() {
    log "Building materials (parallel)..."

    find materials -name "*.mat" | parallel -j 8 --bar \
        'matc -p mobile -a vulkan -O -o compiled/materials/{/.}.filamat {}'
}

# 智能纹理压缩
build_textures_smart() {
    log "Building textures (smart)..."

    for tex in textures/*.png; do
        name=$(basename "$tex" .png)

        # 检测纹理类型并选择最佳压缩
        if [[ "$name" == *"normal"* ]]; then
            mipgen --compression=uastc --linear --kernel=NORMALS "$tex" "compiled/textures/${name}.ktx2"
        elif [[ "$name" == *"albedo"* ]]; then
            mipgen --compression=uastc "$tex" "compiled/textures/${name}.ktx2"
        else
            mipgen --compression=uastc --linear "$tex" "compiled/textures/${name}.ktx2"
        fi
    done
}

# 主函数
main() {
    log "=== Ultimate Asset Builder ===\"

    check_tools
    clean_all

    build_materials_parallel
    build_textures_smart

    log "✅ Build complete!"
    log "See log: $LOG_FILE"
}

main "$@"
```

## 相关文档

- [02-matc-compiler.md](02-matc-compiler.md) - 材质编译器
- [08-asset-pipeline.md](08-asset-pipeline.md) - 资产管线
- [../material/](../material/) - 材质系统
- [../engine/](../engine/) - Engine 架构

## 总结

**高级用法核心要点**:

1. **自定义集成** - 使用 API 而非命令行工具
2. **并行化** - 充分利用多核 CPU
3. **缓存** - 避免重复构建
4. **自动化** - 脚本化所有流程
5. **优化** - 资产大小和性能
6. **调试** - 可视化和分析工具

**最佳实践**:

```bash
# 1. 使用并行构建
make -j$(nproc) materials

# 2. 启用缓存
export MATC_CACHE=~/.matcache

# 3. 智能增量构建
./build_incremental.sh

# 4. 性能分析
./profile_materials.sh

# 5. 自动化测试
./validate_all_assets.sh
```

掌握这些高级技巧，你可以构建高效、可维护的 Filament 资产工作流程！
