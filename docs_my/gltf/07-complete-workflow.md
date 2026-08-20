# 完整工作流程：从 Blender 到 Filament

本文档详细描述从 3D 建模到实时渲染的完整工作流程，包括各个阶段的工具链集成和最佳实践。

## 目录

1. [工作流程概览](#工作流程概览)
2. [静态模型工作流](#静态模型工作流)
3. [角色动画工作流](#角色动画工作流)
4. [环境场景工作流](#环境场景工作流)
5. [工具链集成](#工具链集成)
6. [资产验证与调试](#资产验证与调试)
7. [生产环境部署](#生产环境部署)

---

## 工作流程概览

### 完整流程图

```
┌──────────────┐
│  Blender     │ 3D 建模、材质、动画制作
│  Modeling    │
└──────┬───────┘
       │
       ↓
┌──────────────┐
│  glTF 2.0    │ 导出为标准格式
│  Export      │ (.gltf/.glb)
└──────┬───────┘
       │
       ↓
┌──────────────┐
│  Optimization│ 压缩、优化、验证
│  Tools       │ (gltfpack, Draco, KTX2)
└──────┬───────┘
       │
       ↓
┌──────────────┐
│  Asset       │ 打包为应用资产
│  Integration │
└──────┬───────┘
       │
       ↓
┌──────────────┐
│  Filament    │ 运行时加载与渲染
│  Runtime     │
└──────────────┘
```

### 核心阶段

| 阶段 | 工具 | 输出 | 关键点 |
|------|------|------|--------|
| **建模** | Blender | .blend | 模型拓扑、UV 展开 |
| **材质** | Blender (Principled BSDF) | 纹理文件 | PBR 工作流 |
| **动画** | Blender | 骨骼/关键帧 | 动画烘焙 |
| **导出** | Blender glTF Exporter | .gltf/.glb | 扩展选择、压缩设置 |
| **优化** | gltfpack/Draco | 优化后的 glTF | 文件大小、加载速度 |
| **验证** | gltf-validator | 验证报告 | 规范合规性 |
| **加载** | Filament AssetLoader | FilamentAsset | 材质生成、资源管理 |
| **渲染** | Filament Engine | 屏幕输出 | 场景管理、动画播放 |

---

## 静态模型工作流

适用于场景道具、建筑、静态装饰等。

### 1. Blender 建模阶段

**模型要求：**
```python
# 检查模型规范
import bpy

def validate_static_model():
    obj = bpy.context.active_object

    # 检查面数（移动端建议 <50K 三角形）
    triangles = sum([len(p.vertices) - 2 for p in obj.data.polygons])
    print(f"三角形数量: {triangles}")

    # 检查 UV 映射
    if not obj.data.uv_layers:
        print("警告: 缺少 UV 映射")

    # 检查法线
    if not obj.data.has_custom_normals:
        print("提示: 可以使用自定义法线优化")

    # 检查材质槽
    if len(obj.material_slots) > 5:
        print(f"警告: 材质槽过多 ({len(obj.material_slots)}), 建议合并")
```

**UV 映射最佳实践：**
- 使用 2 个 UV 集：`UVMap` (纹理) + `UVMap_Lightmap` (光照贴图)
- 确保 UV 岛没有重叠（除非故意镜像）
- 合理利用纹理空间（使用 UV 打包工具）

### 2. 材质设置

**Principled BSDF 配置：**
```
基础颜色 (Base Color)
  ├─ 纹理: {name}_BaseColor.png (sRGB)
  └─ 颜色空间: sRGB

金属度 + 粗糙度 (Metallic-Roughness)
  ├─ 纹理: {name}_MetallicRoughness.png (非颜色数据)
  └─ 通道: R=未使用, G=粗糙度, B=金属度

法线贴图 (Normal Map)
  ├─ 纹理: {name}_Normal.png (非颜色数据)
  └─ 强度: 0.5-1.0

环境光遮蔽 (Ambient Occlusion)
  ├─ 纹理: {name}_AO.png (非颜色数据)
  └─ 可选: 烘焙到 BaseColor 或 Roughness 的 R 通道
```

**纹理优化建议：**
- 基础颜色: 1024×1024 (主要物体) 或 512×512 (次要物体)
- 法线贴图: 可以比基础颜色低一级 (512×512)
- 金属粗糙度: 512×512 通常足够
- 使用 2 的幂次方分辨率以支持 mipmaps

### 3. 导出设置

**Blender glTF 导出器配置：**
```python
# 自动化导出脚本
import bpy

def export_static_model(filepath):
    bpy.ops.export_scene.gltf(
        filepath=filepath,
        export_format='GLB',  # 二进制格式，更小更快

        # 包含内容
        use_selection=False,  # 导出整个场景
        use_active_collection=False,

        # 变换选项
        export_yup=True,  # Y-up 坐标系

        # 几何数据
        export_apply=True,  # 应用修改器
        export_texcoords=True,
        export_normals=True,
        export_tangents=True,  # 法线贴图需要

        # 材质
        export_materials='EXPORT',
        export_colors=False,  # 不导出顶点色（除非需要）

        # 压缩（可选，见第 5 节工具链）
        export_draco_mesh_compression_enable=False,  # 使用 gltfpack 处理更好

        # 不需要的功能
        export_animations=False,
        export_skins=False,
        export_cameras=False,
        export_lights=False
    )

# 使用示例
export_static_model("/path/to/model.glb")
```

### 4. 优化与压缩

**使用 gltfpack 优化：**
```bash
# 基础优化（推荐用于所有资产）
gltfpack -i model.glb -o model_optimized.glb

# 激进优化（移动平台）
gltfpack -i model.glb -o model_mobile.glb \
  -cc          # 启用 Draco 压缩 (几何数据)
  -tc          # 启用 KTX2/Basis 纹理压缩
  -tq 8        # 纹理质量 (0-10, 8 为高质量)
  -si 0.5      # 简化到 50% 顶点数
  -mi          # 网格量化优化

# 查看优化效果
ls -lh model.glb model_optimized.glb
```

**压缩效果对比：**
| 方法 | 文件大小 | 加载时间 | 视觉质量 |
|------|---------|---------|---------|
| 原始 GLB | 5.2 MB | 100% | 100% |
| gltfpack 基础 | 3.1 MB | 60% | 99% |
| gltfpack + Draco | 1.8 MB | 50% | 98% |
| gltfpack + KTX2 | 1.2 MB | 45% | 95% |
| 完全优化 | 0.8 MB | 40% | 92% |

### 5. Filament 加载代码

**完整加载示例：**
```cpp
#include <filament/Engine.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/MaterialProvider.h>
#include <gltfio/ResourceLoader.h>
#include <utils/Path.h>

using namespace filament;
using namespace gltfio;

class StaticModelLoader {
public:
    StaticModelLoader(Engine* engine) : mEngine(engine) {
        // 创建材质提供器（桌面用 JIT，移动用 Ubershader）
        #if defined(__ANDROID__) || defined(__IPHONE__)
            mMaterials = createUbershaderProvider(engine, UBERARCHIVE_DEFAULT_DATA, UBERARCHIVE_DEFAULT_SIZE);
        #else
            mMaterials = createJitShaderProvider(engine);
        #endif

        // 创建资产加载器
        mAssetLoader = AssetLoader::create({engine, mMaterials});

        // 创建资源加载器
        mResourceLoader = new ResourceLoader({engine});
    }

    ~StaticModelLoader() {
        delete mResourceLoader;
        AssetLoader::destroy(&mAssetLoader);
        delete mMaterials;
    }

    FilamentAsset* loadModel(const uint8_t* data, size_t size) {
        // 1. 创建资产
        FilamentAsset* asset = mAssetLoader->createAsset(data, size);
        if (!asset) {
            return nullptr;
        }

        // 2. 加载外部资源（纹理、缓冲区）
        mResourceLoader->loadResources(asset);

        // 3. 异步等待资源加载完成
        while (!mResourceLoader->asyncGetLoadProgress() == 1.0f) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        // 4. 将实体添加到场景
        Scene* scene = mEngine->createScene();
        scene->addEntities(asset->getEntities(), asset->getEntityCount());

        // 5. 释放源数据（可选，节省内存）
        asset->releaseSourceData();

        return asset;
    }

private:
    Engine* mEngine;
    MaterialProvider* mMaterials;
    AssetLoader* mAssetLoader;
    ResourceLoader* mResourceLoader;
};

// 使用示例
int main() {
    Engine* engine = Engine::create();
    StaticModelLoader loader(engine);

    // 从文件加载
    std::vector<uint8_t> buffer = readFile("model_optimized.glb");
    FilamentAsset* asset = loader.loadModel(buffer.data(), buffer.size());

    // 渲染循环...

    // 清理
    loader.mAssetLoader->destroyAsset(asset);
    Engine::destroy(&engine);
}
```

---

## 角色动画工作流

适用于游戏角色、动画角色、虚拟人物等。

### 1. 骨架与蒙皮设置

**骨骼命名规范：**
```
Root
├─ Hips
│  ├─ Spine
│  │  ├─ Spine1
│  │  │  ├─ Spine2
│  │  │  │  ├─ Neck
│  │  │  │  │  └─ Head
│  │  │  │  ├─ LeftShoulder
│  │  │  │  │  └─ LeftArm
│  │  │  │  │     └─ LeftForeArm
│  │  │  │  │        └─ LeftHand
│  │  │  │  └─ RightShoulder
│  │  │     └─ RightArm
│  │        └─ RightForeArm
│  │           └─ RightHand
│  ├─ LeftUpLeg
│  │  └─ LeftLeg
│  │     └─ LeftFoot
│  └─ RightUpLeg
│     └─ RightLeg
│        └─ RightFoot
```

**蒙皮权重检查：**
```python
import bpy

def validate_skinning():
    obj = bpy.context.active_object

    if obj.type != 'MESH':
        return

    # 检查顶点组（骨骼权重）
    if not obj.vertex_groups:
        print("错误: 没有顶点组（未蒙皮）")
        return

    # 检查 Armature 修改器
    armature_mod = None
    for mod in obj.modifiers:
        if mod.type == 'ARMATURE':
            armature_mod = mod
            break

    if not armature_mod:
        print("错误: 缺少 Armature 修改器")
        return

    # 检查每个顶点的权重
    mesh = obj.data
    issues = []

    for v in mesh.vertices:
        total_weight = 0.0
        bone_count = 0

        for g in v.groups:
            total_weight += g.weight
            bone_count += 1

        # Filament 限制：每个顶点最多 4 个骨骼影响
        if bone_count > 4:
            issues.append(f"顶点 {v.index}: {bone_count} 个骨骼 (最大 4)")

        # 权重归一化检查
        if abs(total_weight - 1.0) > 0.01:
            issues.append(f"顶点 {v.index}: 权重和 = {total_weight:.3f} (应为 1.0)")

    if issues:
        print(f"发现 {len(issues)} 个蒙皮问题:")
        for issue in issues[:10]:  # 只显示前 10 个
            print(f"  - {issue}")
    else:
        print("蒙皮验证通过！")
```

### 2. 动画创建与导出

**动画烘焙：**
```python
import bpy

def bake_animations():
    """烘焙所有约束和 IK 到关键帧"""

    # 选择骨架
    armature = bpy.data.objects['Armature']
    bpy.context.view_layer.objects.active = armature

    # 进入姿态模式
    bpy.ops.object.mode_set(mode='POSE')

    # 选择所有骨骼
    bpy.ops.pose.select_all(action='SELECT')

    # 烘焙动作
    bpy.ops.nla.bake(
        frame_start=1,
        frame_end=250,
        only_selected=False,
        visual_keying=True,  # 烘焙视觉变换（包括约束）
        clear_constraints=True,  # 清除约束
        clear_parents=False,
        use_current_action=True,
        bake_types={'POSE'}
    )

    bpy.ops.object.mode_set(mode='OBJECT')
    print("动画烘焙完成")

# 执行烘焙
bake_animations()
```

**导出动画角色：**
```python
def export_animated_character(filepath):
    bpy.ops.export_scene.gltf(
        filepath=filepath,
        export_format='GLB',

        # 动画相关
        export_animations=True,
        export_frame_range=True,  # 使用场景帧范围
        export_current_frame=False,
        export_nla_strips=True,  # 导出 NLA 条带作为独立动画
        export_anim_single_armature=True,
        export_reset_pose_bones=True,

        # 蒙皮
        export_skins=True,
        export_all_influences=False,  # 限制为每顶点 4 个骨骼
        export_def_bones=True,  # 只导出有变形权重的骨骼

        # 几何与材质
        export_apply=False,  # 不应用修改器（保留 Armature）
        export_texcoords=True,
        export_normals=True,
        export_tangents=True,
        export_materials='EXPORT',

        # 优化
        export_extras=False,
        export_yup=True
    )

export_animated_character("/path/to/character.glb")
```

### 3. Filament 动画播放

**完整动画系统：**
```cpp
#include <gltfio/Animator.h>
#include <utils/EntityManager.h>

class CharacterController {
public:
    CharacterController(Engine* engine, FilamentAsset* asset)
        : mEngine(engine), mAsset(asset) {

        // 创建动画控制器
        mAnimator = asset->getAnimator();
        mAnimationCount = mAnimator->getAnimationCount();

        // 打印所有动画名称
        for (size_t i = 0; i < mAnimationCount; i++) {
            const char* name = mAnimator->getAnimationName(i);
            float duration = mAnimator->getAnimationDuration(i);
            printf("动画 %zu: %s (%.2f 秒)\n", i, name, duration);
        }
    }

    void update(float deltaTime) {
        mTime += deltaTime;

        // 循环播放当前动画
        if (mCurrentAnimation < mAnimationCount) {
            float duration = mAnimator->getAnimationDuration(mCurrentAnimation);
            float loopTime = fmod(mTime, duration);

            // 应用动画
            mAnimator->applyAnimation(mCurrentAnimation, loopTime);

            // 更新骨骼变换矩阵
            mAnimator->updateBoneMatrices();
        }
    }

    void playAnimation(size_t index, bool blend = false, float blendDuration = 0.3f) {
        if (index >= mAnimationCount) return;

        if (blend && mCurrentAnimation < mAnimationCount) {
            // 混合到新动画
            startCrossFade(mCurrentAnimation, index, blendDuration);
        } else {
            // 直接切换
            mCurrentAnimation = index;
            mTime = 0.0f;
        }
    }

    void startCrossFade(size_t fromAnim, size_t toAnim, float duration) {
        mCrossFade.active = true;
        mCrossFade.fromAnim = fromAnim;
        mCrossFade.toAnim = toAnim;
        mCrossFade.duration = duration;
        mCrossFade.elapsed = 0.0f;
        mCrossFade.fromTime = mTime;
    }

    void updateWithCrossFade(float deltaTime) {
        if (!mCrossFade.active) {
            update(deltaTime);
            return;
        }

        mTime += deltaTime;
        mCrossFade.elapsed += deltaTime;

        float alpha = std::min(mCrossFade.elapsed / mCrossFade.duration, 1.0f);

        // 应用两个动画的混合
        float fromDuration = mAnimator->getAnimationDuration(mCrossFade.fromAnim);
        float toDuration = mAnimator->getAnimationDuration(mCrossFade.toAnim);

        float fromTime = fmod(mCrossFade.fromTime + mCrossFade.elapsed, fromDuration);
        float toTime = fmod(mTime, toDuration);

        mAnimator->applyAnimation(mCrossFade.fromAnim, fromTime);
        mAnimator->applyCrossFade(mCrossFade.toAnim, toTime, alpha);
        mAnimator->updateBoneMatrices();

        // 混合完成
        if (alpha >= 1.0f) {
            mCrossFade.active = false;
            mCurrentAnimation = mCrossFade.toAnim;
        }
    }

private:
    Engine* mEngine;
    FilamentAsset* mAsset;
    Animator* mAnimator;

    size_t mAnimationCount = 0;
    size_t mCurrentAnimation = 0;
    float mTime = 0.0f;

    struct CrossFade {
        bool active = false;
        size_t fromAnim;
        size_t toAnim;
        float duration;
        float elapsed;
        float fromTime;
    } mCrossFade;
};

// 使用示例
CharacterController character(engine, asset);

// 渲染循环
while (rendering) {
    float deltaTime = calculateDeltaTime();

    // 更新动画
    character.updateWithCrossFade(deltaTime);

    // 渲染场景
    renderer->render(view);
}

// 切换动画（例如从 idle 到 walk）
character.playAnimation(1, true, 0.2f);  // 0.2 秒混合
```

---

## 环境场景工作流

适用于游戏关卡、建筑可视化、虚拟展厅等大型场景。

### 1. 场景组织策略

**场景层级结构：**
```
Scene.glb
├─ Environment
│  ├─ Terrain
│  ├─ Vegetation
│  │  ├─ Trees_LOD0
│  │  ├─ Trees_LOD1
│  │  └─ Grass
│  └─ Water
├─ Architecture
│  ├─ Building_Exterior
│  ├─ Building_Interior
│  └─ Props
└─ Lighting
   ├─ DirectionalLight
   ├─ PointLights
   └─ SpotLights
```

**分块加载（推荐）：**
```cpp
// 场景分块定义
struct SceneChunk {
    std::string name;
    std::string filepath;
    filament::math::float3 position;
    float loadDistance;  // 玩家距离多远时加载
    FilamentAsset* asset = nullptr;
    bool loaded = false;
};

class StreamingSceneManager {
public:
    void addChunk(const SceneChunk& chunk) {
        mChunks.push_back(chunk);
    }

    void update(const filament::math::float3& playerPos) {
        for (auto& chunk : mChunks) {
            float distance = length(playerPos - chunk.position);

            // 加载距离内的块
            if (distance < chunk.loadDistance && !chunk.loaded) {
                loadChunk(chunk);
            }
            // 卸载距离外的块
            else if (distance > chunk.loadDistance * 1.5f && chunk.loaded) {
                unloadChunk(chunk);
            }
        }
    }

private:
    void loadChunk(SceneChunk& chunk) {
        auto data = readFile(chunk.filepath);
        chunk.asset = mAssetLoader->createAsset(data.data(), data.size());
        mResourceLoader->loadResources(chunk.asset);
        mScene->addEntities(chunk.asset->getEntities(), chunk.asset->getEntityCount());
        chunk.loaded = true;
    }

    void unloadChunk(SceneChunk& chunk) {
        if (chunk.asset) {
            mScene->removeEntities(chunk.asset->getEntities(), chunk.asset->getEntityCount());
            mAssetLoader->destroyAsset(chunk.asset);
            chunk.asset = nullptr;
        }
        chunk.loaded = false;
    }

    std::vector<SceneChunk> mChunks;
    AssetLoader* mAssetLoader;
    ResourceLoader* mResourceLoader;
    Scene* mScene;
};
```

### 2. 光照烘焙工作流

**Blender 光照烘焙：**
```python
import bpy

def bake_lightmap():
    """烘焙场景光照到纹理"""

    # 1. 确保所有物体有第二套 UV（用于光照贴图）
    for obj in bpy.context.scene.objects:
        if obj.type != 'MESH':
            continue

        # 检查是否有 UVMap_Lightmap
        if 'UVMap_Lightmap' not in obj.data.uv_layers:
            # 创建并智能 UV 投影
            obj.data.uv_layers.new(name='UVMap_Lightmap')
            bpy.context.view_layer.objects.active = obj
            bpy.ops.object.mode_set(mode='EDIT')
            bpy.ops.mesh.select_all(action='SELECT')
            bpy.ops.uv.smart_project(angle_limit=66, island_margin=0.02)
            bpy.ops.object.mode_set(mode='OBJECT')

    # 2. 为每个对象创建烘焙纹理
    for obj in bpy.context.selected_objects:
        if obj.type != 'MESH':
            continue

        # 创建图像
        image_name = f"{obj.name}_Lightmap"
        image = bpy.data.images.new(image_name, width=1024, height=1024)

        # 创建或获取材质
        if not obj.data.materials:
            mat = bpy.data.materials.new(name=f"{obj.name}_Material")
            obj.data.materials.append(mat)
        else:
            mat = obj.data.materials[0]

        mat.use_nodes = True
        nodes = mat.node_tree.nodes

        # 添加图像纹理节点（用于烘焙目标）
        tex_node = nodes.new('ShaderNodeTexImage')
        tex_node.image = image
        nodes.active = tex_node

    # 3. 烘焙设置
    bpy.context.scene.render.engine = 'CYCLES'
    bpy.context.scene.cycles.samples = 128
    bpy.context.scene.render.bake.use_pass_direct = True
    bpy.context.scene.render.bake.use_pass_indirect = True
    bpy.context.scene.render.bake.use_pass_color = True

    # 4. 执行烘焙
    bpy.ops.object.bake(type='COMBINED', use_clear=True)

    # 5. 保存图像
    for img in bpy.data.images:
        if img.name.endswith('_Lightmap') and img.is_dirty:
            img.filepath_raw = f"/tmp/{img.name}.png"
            img.file_format = 'PNG'
            img.save()

    print("光照烘焙完成")

# 执行烘焙
bake_lightmap()
```

### 3. 环境纹理与 IBL

**导出 IBL 环境贴图：**
```bash
# 使用 Filament 的 cmgen 工具生成 IBL
cmgen -x out/ibl \
      --format=ktx \
      --size=256 \
      --extract-blur=0.1 \
      environment.hdr

# 输出文件：
# out/ibl/environment_ibl.ktx      (预过滤环境贴图)
# out/ibl/environment_skybox.ktx   (天空盒)
```

**在 Filament 中加载 IBL：**
```cpp
#include <filament/IndirectLight.h>
#include <filament/Skybox.h>
#include <ktxreader/Ktx2Reader.h>

// 加载 IBL
Texture* loadKtxTexture(Engine* engine, const std::string& path) {
    auto data = readFile(path);

    ktxreader::Ktx2Reader reader(*engine);
    return reader.load(data.data(), data.size());
}

// 设置间接光照
IndirectLight* ibl = IndirectLight::Builder()
    .reflections(loadKtxTexture(engine, "environment_ibl.ktx"))
    .intensity(30000.0f)
    .build(*engine);

scene->setIndirectLight(ibl);

// 设置天空盒
Skybox* skybox = Skybox::Builder()
    .environment(loadKtxTexture(engine, "environment_skybox.ktx"))
    .build(*engine);

scene->setSkybox(skybox);
```

---

## 工具链集成

### 1. gltf-pipeline

用于验证、优化和转换 glTF 文件。

**安装：**
```bash
npm install -g gltf-pipeline
```

**常用操作：**
```bash
# GLB 转 glTF（带独立文件）
gltf-pipeline -i model.glb -o model.gltf -s

# glTF 转 GLB（嵌入所有资源）
gltf-pipeline -i model.gltf -o model.glb

# Draco 压缩
gltf-pipeline -i model.glb -o model_draco.glb --draco.compressionLevel=10

# 优化几何数据
gltf-pipeline -i model.glb -o model_opt.glb --optimize
```

### 2. gltfpack

Meshoptimizer 团队开发的 glTF 优化工具，提供最佳压缩效果。

**安装：**
```bash
# macOS
brew install gltfpack

# 或从源码构建
git clone https://github.com/zeux/meshoptimizer.git
cd meshoptimizer/gltf
make
```

**高级用法：**
```bash
# 完整优化管线
gltfpack \
  -i input.glb \
  -o output.glb \
  -cc \                    # Draco 压缩
  -tc \                    # KTX2/Basis 纹理压缩
  -kn \                    # 保留顶点法线
  -si 0.7 \                # 简化到 70% 三角形
  -sa 0.02 \               # 简化激进度（允许 2% 误差）
  -slb \                   # 锁定边界（避免破坏 UV 接缝）
  -te \                    # 纹理嵌入到 GLB
  -noq \                   # 不量化位置（保留精度）
  -mi \                    # 网格索引优化
  -vpi 16 \                # 位置量化精度 (bits)
  -vti 8 \                 # UV 量化精度
  -vni 8                   # 法线量化精度

# 仅压缩（不简化）
gltfpack -i model.glb -o model_compressed.glb -cc -tc

# 查看统计
gltfpack -i model.glb -o /dev/null -v
```

**效果对比：**
```bash
# 原始文件
-rw-r--r--  1 user  staff   8.2M  model_original.glb

# gltfpack 基础优化
-rw-r--r--  1 user  staff   4.1M  model_packed.glb         (-50%)

# gltfpack + Draco
-rw-r--r--  1 user  staff   2.3M  model_draco.glb          (-72%)

# gltfpack + KTX2
-rw-r--r--  1 user  staff   1.8M  model_ktx2.glb           (-78%)

# 完全优化
-rw-r--r--  1 user  staff   1.1M  model_full_opt.glb       (-87%)
```

### 3. Draco 独立工具

Google 开发的 3D 几何压缩库。

**编译 Draco 工具：**
```bash
git clone https://github.com/google/draco.git
cd draco
mkdir build && cd build
cmake ..
make -j8
```

**使用 Draco 压缩：**
```bash
# 直接压缩 OBJ/PLY 文件
./draco_encoder -i model.obj -o model.drc -cl 10

# 通过 gltf-pipeline 压缩 glTF
gltf-pipeline -i model.glb -o model_draco.glb \
  --draco.compressionLevel=10 \
  --draco.quantizePositionBits=14 \
  --draco.quantizeTexcoordBits=12 \
  --draco.quantizeNormalBits=10
```

### 4. KTX-Software

Khronos 纹理压缩工具。

**安装：**
```bash
# macOS
brew install ktx

# 或从源码构建
git clone https://github.com/KhronosGroup/KTX-Software.git
cd KTX-Software
mkdir build && cd build
cmake ..
make -j8
```

**纹理压缩：**
```bash
# PNG 转 KTX2 (Basis Universal)
toktx --bcmp --genmipmap output.ktx2 input.png

# 带质量控制
toktx --bcmp --clevel 4 --qlevel 255 output.ktx2 input.png

# 法线贴图优化
toktx --normal_map --genmipmap normal.ktx2 normal.png

# HDR 环境贴图
toktx --bcmp --cubemap skybox.ktx2 \
  px.hdr nx.hdr py.hdr ny.hdr pz.hdr nz.hdr
```

---

## 资产验证与调试

### 1. glTF Validator

官方验证工具，检查 glTF 规范合规性。

**在线验证：**
https://github.khronos.org/glTF-Validator/

**命令行验证：**
```bash
# 安装
npm install -g gltf-validator

# 验证文件
gltf-validator model.glb

# 详细输出
gltf-validator model.glb -r report.json -a

# 查看报告
cat report.json | jq .
```

**典型问题修复：**

| 错误 | 原因 | 解决方法 |
|------|------|---------|
| `ACCESSOR_MIN_MISMATCH` | 访问器 min/max 不正确 | Blender 重新导出 |
| `UNSUPPORTED_EXTENSION` | 使用了不支持的扩展 | 移除或标记为可选 |
| `MESH_PRIMITIVE_INCOMPATIBLE_MODE` | 不兼容的图元类型 | 转换为三角形网格 |
| `NODE_WEIGHTS_INVALID` | 变形目标权重无效 | 检查 Morph Targets 设置 |
| `ANIMATION_SAMPLER_INPUT_ACCESSOR_TOO_FEW_ELEMENTS` | 动画采样器数据不足 | 至少需要 2 个关键帧 |

### 2. Filament 调试工具

**启用调试日志：**
```cpp
#include <utils/Log.h>

// 启用详细日志
utils::slog.i << "AssetLoader: Creating asset from " << filepath << utils::io::endl;

// 检查资产加载错误
FilamentAsset* asset = assetLoader->createAsset(data, size);
if (!asset) {
    utils::slog.e << "Failed to load asset!" << utils::io::endl;
    return nullptr;
}

// 验证资产完整性
if (asset->getEntityCount() == 0) {
    utils::slog.w << "Asset has no entities" << utils::io::endl;
}
```

**检查材质生成：**
```cpp
// 获取生成的材质
const Material* const* materials = asset->getMaterials();
size_t materialCount = asset->getMaterialInstanceCount();

for (size_t i = 0; i < materialCount; i++) {
    const Material* mat = materials[i];
    utils::slog.i << "Material " << i << ": "
                  << mat->getName() << utils::io::endl;

    // 检查材质属性
    if (mat->hasParameter("baseColorFactor")) {
        utils::slog.i << "  Has baseColorFactor" << utils::io::endl;
    }
}
```

---

## 生产环境部署

### 1. 资产打包策略

**Android APK 打包：**
```gradle
android {
    aaptOptions {
        // 不压缩 .glb 和 .filamat 文件
        noCompress "glb", "filamat", "ktx", "ktx2"
    }

    sourceSets {
        main {
            assets.srcDirs = ['src/main/assets']
        }
    }
}
```

**资产组织结构：**
```
assets/
├─ models/
│  ├─ characters/
│  │  ├─ player.glb
│  │  └─ enemy.glb
│  ├─ environment/
│  │  ├─ level1.glb
│  │  └─ level2.glb
│  └─ props/
│     └─ items.glb
├─ materials/
│  ├─ ubershader.filamat
│  └─ custom.filamat
└─ ibl/
   ├─ studio_ibl.ktx
   └─ outdoor_ibl.ktx
```

### 2. 运行时资源管理

**资产缓存系统：**
```cpp
#include <unordered_map>
#include <memory>

class AssetCache {
public:
    FilamentAsset* loadAsset(const std::string& path) {
        // 检查缓存
        auto it = mCache.find(path);
        if (it != mCache.end()) {
            return it->second.get();
        }

        // 加载新资产
        auto data = loadFile(path);
        FilamentAsset* asset = mAssetLoader->createAsset(data.data(), data.size());

        if (asset) {
            mResourceLoader->loadResources(asset);
            mCache[path] = std::unique_ptr<FilamentAsset, AssetDeleter>(
                asset, AssetDeleter{mAssetLoader}
            );
        }

        return asset;
    }

    void clear() {
        mCache.clear();
    }

private:
    struct AssetDeleter {
        AssetLoader* loader;
        void operator()(FilamentAsset* asset) {
            loader->destroyAsset(asset);
        }
    };

    std::unordered_map<std::string, std::unique_ptr<FilamentAsset, AssetDeleter>> mCache;
    AssetLoader* mAssetLoader;
    ResourceLoader* mResourceLoader;
};
```

### 3. 性能监控

**加载时间测量：**
```cpp
#include <chrono>

class PerformanceMonitor {
public:
    void startTimer(const std::string& name) {
        mTimers[name] = std::chrono::high_resolution_clock::now();
    }

    void endTimer(const std::string& name) {
        auto end = std::chrono::high_resolution_clock::now();
        auto start = mTimers[name];
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        utils::slog.i << name << " took " << duration.count() << " ms" << utils::io::endl;
    }

private:
    std::unordered_map<std::string, std::chrono::time_point<std::chrono::high_resolution_clock>> mTimers;
};

// 使用示例
PerformanceMonitor monitor;

monitor.startTimer("AssetLoad");
FilamentAsset* asset = loadAsset("model.glb");
monitor.endTimer("AssetLoad");

monitor.startTimer("TextureLoad");
resourceLoader->loadResources(asset);
monitor.endTimer("TextureLoad");
```

**内存使用监控：**
```cpp
// 检查 GPU 内存使用
size_t getGpuMemoryUsage(Engine* engine) {
    return engine->getDriver()->getTextureMemoryUsage() +
           engine->getDriver()->getBufferObjectMemoryUsage();
}

// 资产内存统计
struct AssetStats {
    size_t vertexCount;
    size_t indexCount;
    size_t textureMemory;
    size_t bufferMemory;
};

AssetStats getAssetStats(FilamentAsset* asset) {
    AssetStats stats = {};

    // 统计顶点和索引
    const RenderableManager::Instance* renderables = asset->getRenderableEntities();
    for (size_t i = 0; i < asset->getRenderableEntityCount(); i++) {
        // ... 统计逻辑
    }

    return stats;
}
```

---

## 最佳实践总结

### 静态模型
1. ✅ 保持三角形数 <50K (移动端)
2. ✅ 使用 2 的幂次方纹理分辨率
3. ✅ 合并材质槽（<5 个）
4. ✅ 应用 gltfpack 优化
5. ✅ 使用 GLB 格式（单文件）

### 角色动画
1. ✅ 每顶点最多 4 个骨骼影响
2. ✅ 烘焙 IK 和约束到关键帧
3. ✅ 使用 NLA 条带组织多个动画
4. ✅ 骨骼数 <128 (移动端)
5. ✅ 动画混合时间 0.2-0.3 秒

### 环境场景
1. ✅ 分块加载大场景
2. ✅ 使用 LOD (细节层次)
3. ✅ 烘焙静态光照
4. ✅ 共享材质和纹理
5. ✅ 使用 IBL 环境光照

### 工具链
1. ✅ 验证：gltf-validator
2. ✅ 压缩：gltfpack
3. ✅ 纹理：KTX2/Basis Universal
4. ✅ 几何：Draco 压缩
5. ✅ 调试：Filament 日志

### 部署
1. ✅ 缓存已加载资产
2. ✅ 监控加载时间和内存
3. ✅ 异步加载非关键资源
4. ✅ 使用平台适配的 MaterialProvider
5. ✅ 定期测试目标设备性能
