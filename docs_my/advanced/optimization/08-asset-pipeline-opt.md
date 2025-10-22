# 资产管线优化

## 📖 概述

高效的资产管线可以显著减少加载时间和运行时开销。本文档介绍资产优化流程。

**优化目标**:
- 减少加载时间 50-80%
- 优化运行时性能
- 减少包体积
- 自动化优化流程

---

## 1. 纹理压缩流程

```bash
#!/bin/bash
# compress_textures.sh

for file in textures/*.png; do
    filename=$(basename "$file" .png)
    
    # Android (ASTC)
    astcenc -cl "$file" "android/${filename}.astc" 4x4 -medium
    
    # iOS (ASTC)
    astcenc -cl "$file" "ios/${filename}.astc" 4x4 -medium
    
    # Desktop (BC7)
    CompressonatorCLI -fd BC7 "$file" "desktop/${filename}.dds"
done
```

---

## 2. 网格优化流程

```python
# optimize_meshes.py
import bpy

def optimize_mesh(mesh):
    # 简化
    bpy.ops.object.modifier_add(type='DECIMATE')
    bpy.context.object.modifiers["Decimate"].ratio = 0.5
    bpy.ops.object.modifier_apply(modifier="Decimate")
    
    # 优化顶点顺序
    # TODO: Call meshoptimizer
    
    # 导出
    bpy.ops.export_scene.gltf(filepath='output.glb')
```

---

## 3. CMake 集成

```cmake
# Custom target for asset optimization
add_custom_target(optimize_assets
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/compress_textures.sh
    COMMAND python ${CMAKE_SOURCE_DIR}/scripts/optimize_meshes.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Optimizing assets..."
)
```

---

## 4. 总结

资产管线优化关键点:
1. 离线压缩纹理
2. 网格优化（LOD、顶点缓存）
3. 材质合并
4. 自动化工具集成
5. 增量构建
