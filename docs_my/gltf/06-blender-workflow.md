# Blender glTF 导出工作流

本文档详细说明从 Blender 导出 glTF 资产到 Filament 的完整工作流程和最佳实践。

---

## Blender glTF 导出器

### 版本要求

- **Blender 3.0+**: 内置 glTF 2.0 导出器
- **Blender 4.0+**: 推荐，支持最新扩展

### 导出步骤

```
File → Export → glTF 2.0 (.glb/.gltf)
```

---

## 导出设置详解

### 格式选择

| 选项 | 说明 | 推荐场景 |
|------|------|---------|
| **glTF Binary (.glb)** | 单一二进制文件 | ✅ 生产环境 |
| **glTF Separate (.gltf + .bin + textures)** | JSON + 外部文件 | 开发调试 |
| ~~**glTF Embedded (.gltf)**~~ | Base64 内嵌（已移除） | ❌ 不推荐 |

**最佳实践**: 生产环境使用 GLB，开发时使用 glTF Separate

### Include 选项

#### Selected Objects
```
☐ Selected Objects
```
- **未选中**: 导出所有对象（推荐）
- **选中**: 仅导出选中的对象

#### Custom Properties
```
☑ Custom Properties
```
导出自定义属性到 `extras` 字段（用于游戏逻辑）

### Transform 选项

#### +Y Up
```
☑ +Y Up
```
**必须启用**！glTF 规范使用 +Y 作为上方向。

Blender (+Z Up) → glTF (+Y Up)

### Geometry 选项

#### Apply Modifiers
```
☑ Apply Modifiers
```
- **启用**: 烘焙所有修改器到网格
- **注意**: 会阻止导出 Shape Keys

**建议**: 
- 有 Shape Keys（变形目标）→ 取消勾选，手动应用修改器
- 无 Shape Keys → 勾选

#### UVs
```
☑ UVs
```
导出 UV 坐标（纹理映射必需）

**注意**: Filament 最多支持 2 组 UV

#### Normals
```
☑ Normals
```
导出法线数据（光照必需）

#### Tangents
```
☑ Tangents
```
导出切线数据（法线贴图必需）

#### Vertex Colors
```
☐ Vertex Colors (按需)
```
导出顶点颜色

### Materials 选项

#### Materials
```
☑ Export: Materials
```
导出材质定义

#### Images
```
☑ Export: Images
```
导出纹理图片

**Format**:
- **Automatic**: 保持原格式（推荐）
- **JPEG**: 有损压缩
- **PNG**: 无损压缩

### Animation 选项

#### Animation
```
☑ Use Current Frame (静态模型)
☐ Use Current Frame (动画模型)
```

#### Shape Keys
```
☑ Shape Keys
```
导出变形目标（Morph Targets）

**与 Apply Modifiers 冲突**！

#### Skinning
```
☑ Skinning
```
导出骨骼蒙皮数据

#### Bake Animation
```
☐ Bake Animation (通常不需要)
```
烘焙约束和驱动为关键帧

---

## 材质设置最佳实践

### 使用 Principled BSDF

Blender 的 **Principled BSDF** 节点直接映射到 glTF PBR：

```
Principled BSDF           →  glTF PBR
├─ Base Color             →  baseColorFactor
├─ Metallic               →  metallicFactor
├─ Roughness              →  roughnessFactor
├─ Normal (Normal Map)    →  normalTexture
├─ Emission               →  emissiveFactor
└─ Alpha                  →  alpha (OPAQUE/MASK/BLEND)
```

### 纹理设置

#### Base Color 纹理

```
Image Texture → Principled BSDF (Base Color)
```

**颜色空间**: sRGB（自动）

#### Metallic + Roughness 纹理

glTF 要求 Metallic 和 Roughness 合并到一张纹理：

```
Image Texture (设置为 Non-Color)
   ↓
Separate RGB
   ├─ G (Green) → Roughness
   └─ B (Blue)  → Metallic
```

**重要**: 图像纹理必须设置为 **Non-Color Data**

#### Normal Map

```
Image Texture (Non-Color)
   ↓
Normal Map Node
   ↓
Principled BSDF (Normal)
```

**颜色空间**: Non-Color Data

#### Occlusion Map

```
Image Texture (Non-Color)
   ↓
Separate RGB → R
   ↓
Mix RGB (Multiply) with Base Color
```

或使用 glTF Blender 插件的自动识别。

### Alpha 模式

#### Opaque (不透明)

```
Principled BSDF:
  Alpha = 1.0
Blend Mode = Opaque
```

#### Alpha Clip (遮罩)

```
Principled BSDF:
  Alpha = < Texture >
Blend Mode = Alpha Clip
Clip Threshold = 0.5
```

用于：树叶、栅栏

#### Alpha Blend (混合)

```
Principled BSDF:
  Alpha = < Texture or Value >
Blend Mode = Alpha Blend
```

用于：玻璃、半透明

### 双面材质

```
Material Properties → Settings
☑ Backface Culling (off) → doubleSided = true
```

---

## 常见问题和解决方案

### 问题 1: UV 集过多

**错误**: 
```
Material uses 3 UV sets, but Filament supports max 2.
```

**解决**:
1. 在 Blender 中合并 UV 集
2. 使用 UV 图集（Texture Atlas）
3. 减少纹理种类

### 问题 2: 纹理路径错误

**现象**: 纹理无法加载

**解决**:
```
File → External Data → Pack Resources
```
然后重新导出为 GLB（纹理内嵌）

### 问题 3: 骨骼权重未归一化

**现象**: 蒙皮变形不正确

**解决**:
```
Blender:
  Select Mesh → Weight Paint → Weights → Normalize All
  
Filament:
  ResourceConfiguration {
    .normalizeSkinningWeights = true
  }
```

### 问题 4: 法线贴图不生效

**检查**:
1. 纹理颜色空间是否为 **Non-Color**
2. 是否连接到 **Normal Map** 节点
3. 是否启用了 **Tangents** 导出选项

### 问题 5: 骨骼数量过多

**现象**: 
```
Skin has 600 joints, but Filament supports max 512.
```

**解决**:
1. 简化骨骼层级（合并不重要的骨骼）
2. 使用 LOD（不同距离不同骨骼数量）
3. 分割模型（多个子网格）

---

## 优化建议

### 几何体优化

#### 面数控制

| 平台 | 推荐面数 | 最大面数 |
|------|---------|---------|
| 移动端 | < 10K | 30K |
| 桌面端 | < 50K | 100K |

**Blender 简化**:
```
Modifiers → Decimate
  Ratio = 0.5 (减少 50%)
```

#### 顶点数优化

- 删除重复顶点: `Mesh → Clean Up → Merge by Distance`
- 删除未使用的顶点组: `Vertex Groups → Remove Unused Groups`

### 纹理优化

#### 尺寸建议

| 用途 | 分辨率 |
|------|--------|
| 角色（主角） | 2048×2048 或 4096×4096 |
| 角色（配角） | 1024×1024 或 2048×2048 |
| 道具 | 512×512 或 1024×1024 |
| UI 元素 | 256×256 或 512×512 |

#### 压缩格式

**导出时**:
- Blender: PNG（无损）
- 然后使用工具转换为 KTX2/Basis

**工具**:
```bash
# 使用 toktx (KTX-Software)
toktx --genmipmap --bcmp output.ktx2 input.png

# 使用 basisu
basisu -output_file output.ktx2 input.png
```

#### 纹理复用

- 多个对象共享相同材质
- 使用 UV 图集（多个对象共用一张纹理）

### 材质优化

- 合并相似材质（减少 draw call）
- 避免过度使用 Alpha Blend（性能开销大）
- 优先使用 Alpha Clip（性能较好）

---

## 完整导出检查清单

### 导出前

- [ ] 应用缩放: `Ctrl+A → Scale`
- [ ] 检查法线方向: `Face Orientation Overlay`
- [ ] 删除多余对象（灯光、相机，除非需要）
- [ ] 检查 UV 集数量 ≤ 2
- [ ] 纹理颜色空间正确（sRGB vs Non-Color）
- [ ] 骨骼权重归一化
- [ ] 骨骼数量 ≤ 512（推荐 < 100）

### 导出设置

- [ ] 格式: GLB (生产) 或 glTF Separate (开发)
- [ ] ✅ +Y Up
- [ ] ✅ Apply Modifiers (如果没有 Shape Keys)
- [ ] ✅ UVs
- [ ] ✅ Normals
- [ ] ✅ Tangents (如果使用法线贴图)
- [ ] ✅ Materials
- [ ] ✅ Skinning (如果有骨骼动画)

### 导出后验证

```bash
# 使用 glTF Validator
npm install -g gltf-validator
gltf_validator model.glb

# 或在线验证
# https://github.khronos.org/glTF-Validator/
```

---

## 工具链集成

### Blender Python 脚本

自动化导出：

```python
import bpy

# 配置导出设置
export_settings = {
    'filepath': '/path/to/output.glb',
    'export_format': 'GLB',
    'export_apply': True,  # Apply modifiers
    'export_yup': True,
    'export_materials': 'EXPORT',
    'export_texcoords': True,
    'export_normals': True,
    'export_tangents': True,
    'export_skins': True,
    'export_animations': True,
}

# 导出
bpy.ops.export_scene.gltf(**export_settings)
```

### 批量导出

```python
import bpy
import os

# 导出所有集合为单独的 GLB
for collection in bpy.data.collections:
    # 隐藏其他集合
    for col in bpy.data.collections:
        col.hide_viewport = (col != collection)
    
    # 导出
    filepath = f"/output/{collection.name}.glb"
    bpy.ops.export_scene.gltf(
        filepath=filepath,
        export_format='GLB',
        use_visible=True
    )
```

---

## 总结

**Blender → glTF → Filament 关键要点**:

1. **材质**: 使用 Principled BSDF
2. **UV**: 最多 2 组，正确设置
3. **纹理**: 正确的颜色空间（sRGB vs Non-Color）
4. **骨骼**: ≤ 512 个，权重归一化
5. **导出**: GLB 格式，启用 +Y Up
6. **验证**: 使用 glTF Validator

**优化策略**:
- 控制面数（移动端 < 30K）
- 优化纹理尺寸
- 复用材质和纹理
- 使用压缩格式（KTX2）
