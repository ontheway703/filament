# glTF 扩展支持详解

本文档详细说明 glTF 2.0 的扩展系统、cgltf 和 Filament 的扩展支持情况。

---

## 目录

1. [扩展系统概述](#扩展系统概述)
2. [Filament 支持的扩展](#filament-支持的扩展)
3. [cgltf 支持的扩展](#cgltf-支持的扩展)
4. [扩展兼容性矩阵](#扩展兼容性矩阵)
5. [不支持扩展的处理](#不支持扩展的处理)

---

## 扩展系统概述

### 扩展机制

glTF 通过扩展机制保持核心规范精简，同时支持新特性：

```json
{
  "asset": {
    "version": "2.0"
  },

  "extensionsUsed": [
    "KHR_materials_clearcoat",
    "KHR_lights_punctual"
  ],

  "extensionsRequired": [
    "KHR_draco_mesh_compression"
  ],

  "materials": [
    {
      "extensions": {
        "KHR_materials_clearcoat": {
          "clearcoatFactor": 1.0,
          "clearcoatRoughnessFactor": 0.1
        }
      }
    }
  ]
}
```

### 扩展类型

#### KHR - Khronos 官方扩展

```
前缀: KHR_
维护: Khronos Group
状态: 正式标准
IP: Khronos IP 框架保护
```

**示例**:
- `KHR_materials_clearcoat`
- `KHR_lights_punctual`
- `KHR_texture_transform`

**特点**:
- 经过完整的规范化流程
- 广泛的工具和引擎支持
- 可能最终纳入核心规范

#### EXT - 多厂商扩展

```
前缀: EXT_
维护: 多个厂商合作
状态: 非官方但广泛支持
```

**示例**:
- `EXT_meshopt_compression`
- `EXT_mesh_gpu_instancing`
- `EXT_texture_webp`

**特点**:
- 两个或更多厂商实现
- 可能升级为 KHR 扩展

#### 厂商扩展

```
前缀: <VENDOR>_
示例: ADOBE_, MSFT_, GOOGLE_
维护: 特定厂商
状态: 实验性或专有
```

**示例**:
- `MSFT_texture_dds`
- `GOOGLE_texture_basis`
- `ADOBE_materials_thin_transparency`

### extensionsUsed vs extensionsRequired

#### extensionsUsed

```json
{
  "extensionsUsed": [
    "KHR_materials_clearcoat",
    "KHR_lights_punctual"
  ]
}
```

**含义**: 文件中使用了这些扩展

**加载器行为**:
- 不支持的扩展: 可以忽略，使用降级表现
- 文件仍可加载

#### extensionsRequired

```json
{
  "extensionsRequired": [
    "KHR_draco_mesh_compression"
  ]
}
```

**含义**: 必须支持这些扩展才能正确加载

**加载器行为**:
- 不支持的扩展: 必须拒绝加载或显示错误
- 无法降级

**规则**:
```
extensionsRequired ⊆ extensionsUsed
（required 必须是 used 的子集）
```

---

## Filament 支持的扩展

### 材质扩展

#### KHR_materials_pbrSpecularGlossiness ✅

**地位**: Ratified
**用途**: 传统 Specular-Glossiness PBR 工作流

```json
{
  "materials": [
    {
      "extensions": {
        "KHR_materials_pbrSpecularGlossiness": {
          "diffuseFactor": [1.0, 1.0, 1.0, 1.0],
          "diffuseTexture": {
            "index": 0
          },
          "specularFactor": [1.0, 1.0, 1.0],
          "glossinessFactor": 1.0,
          "specularGlossinessTexture": {
            "index": 1
          }
        }
      }
    }
  ]
}
```

**Filament 支持**:
- ✅ 完全支持
- 自动转换为 Metallic-Roughness 工作流
- JitShaderProvider 和 UbershaderProvider 均支持

**参数映射**:
```
diffuse → baseColor
specular → 用于计算 metallic 和 baseColor
glossiness → roughness = 1.0 - glossiness
```

#### KHR_materials_unlit ✅

**地位**: Ratified
**用途**: 无光照材质（自发光、UI、天空盒）

```json
{
  "materials": [
    {
      "extensions": {
        "KHR_materials_unlit": {}
      },
      "pbrMetallicRoughness": {
        "baseColorTexture": {
          "index": 0
        }
      }
    }
  ]
}
```

**Filament 支持**:
- ✅ 完全支持
- 使用 `Shading::UNLIT` 材质模型
- 颜色直接输出，不受光照影响

**用途**:
- UI 元素
- 天空盒
- 粒子效果
- 全息投影

#### KHR_materials_clearcoat ✅

**地位**: Ratified
**用途**: 清漆层（汽车漆、木质家具、湿润表面）

```json
{
  "extensions": {
    "KHR_materials_clearcoat": {
      "clearcoatFactor": 1.0,
      "clearcoatTexture": {
        "index": 2
      },
      "clearcoatRoughnessFactor": 0.1,
      "clearcoatRoughnessTexture": {
        "index": 3
      },
      "clearcoatNormalTexture": {
        "index": 4,
        "scale": 1.0
      }
    }
  }
}
```

**Filament 支持**:
- ✅ 完全支持
- 使用双层 BRDF 模型
- 支持独立的清漆法线

**参数**:
- `clearcoatFactor`: 清漆强度 [0, 1]
- `clearcoatRoughnessFactor`: 清漆粗糙度 [0, 1]
- `clearcoatNormalTexture`: 清漆层独立法线

#### KHR_materials_transmission ✅

**地位**: Ratified
**用途**: 透射/折射（玻璃、水、宝石）

```json
{
  "extensions": {
    "KHR_materials_transmission": {
      "transmissionFactor": 1.0,
      "transmissionTexture": {
        "index": 5
      }
    }
  }
}
```

**Filament 支持**:
- ✅ 完全支持
- 需要屏幕空间折射或环境贴图
- 自动调整 blending 模式

**参数**:
- `transmissionFactor`: 透射率 [0, 1]
  - 0 = 不透射（标准材质）
  - 1 = 完全透射（玻璃）

**常与以下扩展组合使用**:
- `KHR_materials_volume` (体积散射)
- `KHR_materials_ior` (折射率)

#### KHR_materials_volume ✅

**地位**: Ratified
**用途**: 体积散射（云、雾、半透明材质）

```json
{
  "extensions": {
    "KHR_materials_volume": {
      "thicknessFactor": 0.1,
      "thicknessTexture": {
        "index": 6
      },
      "attenuationDistance": 1.0,
      "attenuationColor": [1.0, 0.5, 0.5]
    }
  }
}
```

**Filament 支持**:
- ✅ 完全支持
- 使用体积散射模型

**参数**:
- `thicknessFactor`: 厚度（米）
- `attenuationDistance`: 衰减距离
- `attenuationColor`: 衰减颜色（吸收）

#### KHR_materials_ior ✅

**地位**: Ratified
**用途**: 折射率控制

```json
{
  "extensions": {
    "KHR_materials_ior": {
      "ior": 1.5
    }
  }
}
```

**Filament 支持**:
- ✅ 完全支持
- 影响菲涅尔反射和折射

**常见 IOR 值**:
```
空气:   1.0
水:     1.33
玻璃:   1.5-1.6
钻石:   2.42
```

#### KHR_materials_specular ✅

**地位**: Ratified
**用途**: 精细控制镜面反射

```json
{
  "extensions": {
    "KHR_materials_specular": {
      "specularFactor": 1.0,
      "specularTexture": {
        "index": 7
      },
      "specularColorFactor": [1.0, 1.0, 1.0],
      "specularColorTexture": {
        "index": 8
      }
    }
  }
}
```

**Filament 支持**:
- ✅ 完全支持
- 允许非金属材质的有色镜面反射

**用途**:
- 珠宝（有色宝石）
- 特殊材质（如珍珠层）

#### KHR_materials_sheen ✅

**地位**: Ratified
**用途**: 光泽层（织物、天鹅绒、桃皮）

```json
{
  "extensions": {
    "KHR_materials_sheen": {
      "sheenColorFactor": [1.0, 1.0, 1.0],
      "sheenColorTexture": {
        "index": 9
      },
      "sheenRoughnessFactor": 0.5,
      "sheenRoughnessTexture": {
        "index": 10
      }
    }
  }
}
```

**Filament 支持**:
- ✅ 完全支持
- 使用 Cloth shading model

**参数**:
- `sheenColorFactor`: 光泽颜色
- `sheenRoughnessFactor`: 光泽粗糙度

#### KHR_materials_emissive_strength ✅

**地位**: Ratified
**用途**: 高动态范围自发光

```json
{
  "emissiveFactor": [1.0, 0.5, 0.0],
  "extensions": {
    "KHR_materials_emissive_strength": {
      "emissiveStrength": 10.0
    }
  }
}
```

**Filament 支持**:
- ✅ 完全支持
- 允许 HDR 自发光值

**最终自发光**:
```
emissive = emissiveFactor × emissiveTexture × emissiveStrength
```

**用途**:
- 霓虹灯
- 显示屏
- 发光物体

#### KHR_materials_iridescence ❌

**地位**: Ratified
**用途**: 彩虹色/薄膜干涉

```json
{
  "extensions": {
    "KHR_materials_iridescence": {
      "iridescenceFactor": 1.0,
      "iridescenceIor": 1.3,
      "iridescenceThicknessMinimum": 100,
      "iridescenceThicknessMaximum": 400
    }
  }
}
```

**Filament 支持**:
- ❌ **不支持**
- cgltf 可以解析，但 Filament 会忽略

**降级**: 使用标准 PBR 材质

#### KHR_materials_anisotropy ❌

**地位**: Ratified
**用途**: 各向异性反射（拉丝金属、头发）

```json
{
  "extensions": {
    "KHR_materials_anisotropy": {
      "anisotropyStrength": 1.0,
      "anisotropyRotation": 0.0,
      "anisotropyTexture": {
        "index": 11
      }
    }
  }
}
```

**Filament 支持**:
- ❌ **不支持**

**降级**: 使用标准粗糙度

#### KHR_materials_dispersion ❌

**地位**: Ratified (新)
**用途**: 色散（棱镜效果）

```json
{
  "extensions": {
    "KHR_materials_dispersion": {
      "dispersion": 0.05
    }
  }
}
```

**Filament 支持**:
- ❌ **不支持**

**降级**: 使用标准折射

### 纹理扩展

#### KHR_texture_transform ✅

**地位**: Ratified
**用途**: 纹理坐标变换（偏移、旋转、缩放）

```json
{
  "pbrMetallicRoughness": {
    "baseColorTexture": {
      "index": 0,
      "extensions": {
        "KHR_texture_transform": {
          "offset": [0.5, 0.5],
          "rotation": 0.7854,
          "scale": [2.0, 2.0],
          "texCoord": 1
        }
      }
    }
  }
}
```

**Filament 支持**:
- ✅ 完全支持
- 在 fragment shader 中应用变换

**变换矩阵**:
```
uvTransform = translate(offset) × rotate(rotation) × scale(scale)
uv' = uvTransform × uv
```

#### KHR_texture_basisu ✅

**地位**: Ratified
**用途**: Basis Universal 纹理压缩（KTX2/Basis）

```json
{
  "textures": [
    {
      "extensions": {
        "KHR_texture_basisu": {
          "source": 5
        }
      }
    }
  ],
  "images": [
    {
      "mimeType": "image/ktx2",
      "bufferView": 10
    }
  ]
}
```

**Filament 支持**:
- ✅ 通过 `Ktx2Provider`
- 支持 ETC1s 和 UASTC 格式

**优势**:
- 文件大小减少 ~50-80%
- GPU 原生格式，快速解码
- 跨平台支持

### 光照扩展

#### KHR_lights_punctual ✅

**地位**: Ratified
**用途**: 点光源、聚光灯、方向光

```json
{
  "extensions": {
    "KHR_lights_punctual": {
      "lights": [
        {
          "type": "directional",
          "color": [1.0, 1.0, 1.0],
          "intensity": 1.0
        },
        {
          "type": "point",
          "color": [1.0, 0.8, 0.6],
          "intensity": 10.0,
          "range": 100.0
        },
        {
          "type": "spot",
          "color": [1.0, 1.0, 1.0],
          "intensity": 20.0,
          "range": 50.0,
          "spot": {
            "innerConeAngle": 0.5,
            "outerConeAngle": 0.7854
          }
        }
      ]
    }
  },
  "nodes": [
    {
      "extensions": {
        "KHR_lights_punctual": {
          "light": 0
        }
      }
    }
  ]
}
```

**Filament 支持**:
- ✅ 完全支持
- 自动创建 Filament LightManager 组件

**光源类型**:
- `directional`: 方向光（太阳光）
- `point`: 点光源（灯泡）
- `spot`: 聚光灯（手电筒）

### 几何压缩扩展

#### KHR_draco_mesh_compression ✅

**地位**: Ratified
**用途**: Draco 几何压缩

```json
{
  "primitives": [
    {
      "attributes": {
        "POSITION": 0,
        "NORMAL": 1
      },
      "extensions": {
        "KHR_draco_mesh_compression": {
          "bufferView": 10,
          "attributes": {
            "POSITION": 0,
            "NORMAL": 1,
            "TEXCOORD_0": 2
          }
        }
      }
    }
  ]
}
```

**Filament 支持**:
- ✅ 完全支持（通过 ResourceLoader）
- 自动解压缩

**压缩率**:
- 几何数据减少 ~90-95%
- 适合网络传输

**注意**:
- 解压缩需要时间（CPU 密集）
- 不适合实时生成的几何体

#### KHR_mesh_quantization ✅

**地位**: Ratified
**用途**: 顶点数据量化（使用整数代替浮点）

```json
{
  "accessors": [
    {
      "componentType": 5123,  // UNSIGNED_SHORT
      "type": "VEC3",
      "normalized": true,  // 自动归一化到 [-1, 1]
      "min": [-1, -1, -1],
      "max": [1, 1, 1]
    }
  ]
}
```

**Filament 支持**:
- ✅ 自动支持（cgltf 处理）
- 自动转换为浮点数

**优势**:
- 文件大小减少 ~50%
- GPU 内存减少

### 实例化扩展

#### EXT_mesh_gpu_instancing ⚠️

**地位**: Multi-vendor
**用途**: GPU 实例化（大量重复对象）

```json
{
  "nodes": [
    {
      "mesh": 0,
      "extensions": {
        "EXT_mesh_gpu_instancing": {
          "attributes": {
            "TRANSLATION": 0,
            "ROTATION": 1,
            "SCALE": 2
          }
        }
      }
    }
  ]
}
```

**Filament 支持**:
- ⚠️ **部分支持**
- 需要手动处理实例化数据

---

## cgltf 支持的扩展

cgltf 1.14 可以解析以下扩展（但不一定实现功能）：

### 已解析的扩展

| 扩展名 | cgltf 解析 | Filament 支持 |
|-------|-----------|--------------|
| KHR_materials_pbrSpecularGlossiness | ✅ | ✅ |
| KHR_materials_unlit | ✅ | ✅ |
| KHR_materials_clearcoat | ✅ | ✅ |
| KHR_materials_transmission | ✅ | ✅ |
| KHR_materials_volume | ✅ | ✅ |
| KHR_materials_ior | ✅ | ✅ |
| KHR_materials_specular | ✅ | ✅ |
| KHR_materials_sheen | ✅ | ✅ |
| KHR_materials_emissive_strength | ✅ | ✅ |
| KHR_materials_iridescence | ✅ | ❌ |
| KHR_materials_anisotropy | ✅ | ❌ |
| KHR_materials_dispersion | ✅ | ❌ |
| KHR_materials_variants | ✅ | ⚠️ |
| KHR_texture_transform | ✅ | ✅ |
| KHR_texture_basisu | ✅ | ✅ |
| KHR_lights_punctual | ✅ | ✅ |
| KHR_draco_mesh_compression | ✅ | ✅ |
| KHR_mesh_quantization | ✅ | ✅ |
| EXT_meshopt_compression | ✅ | ❌ |
| EXT_mesh_gpu_instancing | ✅ | ⚠️ |
| EXT_texture_webp | ✅ | ❌ |

---

## 扩展兼容性矩阵

### 按用途分类

#### PBR 材质增强

| 扩展 | Filament | Blender 导出 | 用途 |
|------|---------|-------------|------|
| KHR_materials_clearcoat | ✅ | ✅ | 汽车漆、清漆 |
| KHR_materials_sheen | ✅ | ✅ | 织物、天鹅绒 |
| KHR_materials_specular | ✅ | ✅ | 有色镜面反射 |
| KHR_materials_ior | ✅ | ✅ | 折射率控制 |
| KHR_materials_iridescence | ❌ | ✅ | 彩虹色 |
| KHR_materials_anisotropy | ❌ | ✅ | 拉丝金属 |

#### 透明/折射

| 扩展 | Filament | Blender 导出 | 用途 |
|------|---------|-------------|------|
| KHR_materials_transmission | ✅ | ✅ | 玻璃、透射 |
| KHR_materials_volume | ✅ | ✅ | 体积散射 |
| KHR_materials_dispersion | ❌ | ✅ (新) | 棱镜色散 |

#### 性能优化

| 扩展 | Filament | 压缩率 | 解压时间 |
|------|---------|--------|---------|
| KHR_draco_mesh_compression | ✅ | 90-95% | 中 (CPU) |
| KHR_mesh_quantization | ✅ | 50% | 无 (自动) |
| KHR_texture_basisu | ✅ | 50-80% | 低 (GPU) |
| EXT_meshopt_compression | ❌ | 95% | 低 |

### 平台兼容性

| 扩展 | Web | Android | iOS | Desktop |
|------|-----|---------|-----|---------|
| KHR_materials_clearcoat | ✅ | ✅ | ✅ | ✅ |
| KHR_materials_transmission | ✅ | ✅ | ✅ | ✅ |
| KHR_texture_basisu | ✅ | ✅ | ✅ | ✅ |
| KHR_draco_mesh_compression | ✅ | ✅ | ✅ | ✅ |

---

## 不支持扩展的处理

### 降级策略

#### 1. 忽略扩展（降级为基础功能）

**示例**: `KHR_materials_iridescence`

```json
{
  "materials": [
    {
      "pbrMetallicRoughness": {
        "baseColorFactor": [1.0, 0.8, 0.6, 1.0]
      },
      "extensions": {
        "KHR_materials_iridescence": {
          // 被忽略
        }
      }
    }
  ]
}
```

**结果**: 使用标准 PBR 材质，失去彩虹色效果。

#### 2. 使用替代方案

**示例**: `KHR_materials_anisotropy`

不支持各向异性，使用标准粗糙度：
```
roughness = max(anisotropyX, anisotropyY)
```

#### 3. 警告用户

```cpp
// Filament AssetLoader
if (material->has_iridescence) {
    slog.w << "Material '" << material->name
           << "' uses KHR_materials_iridescence which is not supported. "
           << "Falling back to standard PBR." << io::endl;
}
```

### 检查扩展支持

**在导出前检查**:

使用 Blender 导出时，注意这些选项：
- ✅ 关闭 `Iridescence`
- ✅ 关闭 `Anisotropy`
- ✅ 关闭 `Dispersion`

**在加载时验证**:

```cpp
// 检查必需扩展
for (size_t i = 0; i < data->extensions_required_count; ++i) {
    const char* ext = data->extensions_required[i];
    if (!isSupportedExtension(ext)) {
        slog.e << "Required extension not supported: " << ext << io::endl;
        return nullptr;
    }
}
```

### 扩展兼容性最佳实践

1. **仅使用 Filament 支持的扩展**
   - 导出前检查支持列表
   - 关闭不支持的扩展

2. **避免使用 extensionsRequired**
   - 除非确定必需（如 Draco）
   - 优先使用 extensionsUsed

3. **提供降级版本**
   - 同时导出带扩展和不带扩展的版本
   - 根据平台选择合适的版本

4. **测试多平台**
   - 在目标平台上测试加载
   - 验证降级效果可接受

---

## 扩展使用建议

### 推荐使用

**生产环境推荐**:
```json
{
  "extensionsUsed": [
    "KHR_materials_unlit",           // UI 材质
    "KHR_materials_clearcoat",       // 增强效果
    "KHR_texture_transform",         // 纹理优化
    "KHR_texture_basisu",            // 压缩
    "KHR_draco_mesh_compression",    // 几何压缩
    "KHR_lights_punctual"            // 光照
  ]
}
```

**避免使用** (Filament 不支持):
```json
{
  "extensionsUsed": [
    "KHR_materials_iridescence",   // ❌ 不支持
    "KHR_materials_anisotropy",    // ❌ 不支持
    "KHR_materials_dispersion",    // ❌ 不支持
    "EXT_meshopt_compression"      // ❌ 不支持
  ]
}
```

### 性能建议

**移动平台**:
- ✅ 使用 `KHR_texture_basisu` (KTX2)
- ✅ 使用 `KHR_draco_mesh_compression`
- ⚠️ 谨慎使用 `KHR_materials_transmission` (性能开销)

**桌面平台**:
- ✅ 可以使用所有支持的扩展
- ✅ `KHR_materials_clearcoat` + `transmission` 组合效果好

**Web 平台**:
- ✅ 强烈推荐压缩扩展（减少下载）
- ✅ 使用 UbershaderProvider（避免运行时编译）

---

## 总结

### Filament 扩展支持总览

**完全支持的扩展** (13 个):
1. KHR_materials_pbrSpecularGlossiness
2. KHR_materials_unlit
3. KHR_materials_clearcoat
4. KHR_materials_transmission
5. KHR_materials_volume
6. KHR_materials_ior
7. KHR_materials_specular
8. KHR_materials_sheen
9. KHR_materials_emissive_strength
10. KHR_texture_transform
11. KHR_texture_basisu
12. KHR_lights_punctual
13. KHR_draco_mesh_compression

**不支持的扩展** (3 个):
1. KHR_materials_iridescence
2. KHR_materials_anisotropy
3. KHR_materials_dispersion

**部分支持** (1 个):
1. EXT_mesh_gpu_instancing (需要手动处理)

### 使用建议

1. **检查兼容性**: 导出前确认扩展支持
2. **使用压缩**: 生产环境使用 Draco + KTX2
3. **避免 required**: 除非绝对必要
4. **测试降级**: 确保不支持的扩展有合理降级
5. **平台优化**: 根据目标平台选择合适的扩展
