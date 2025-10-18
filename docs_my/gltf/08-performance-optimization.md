# 性能优化指南

本文档详细介绍 glTF 资产在 Filament 中的性能优化策略，涵盖文件大小、加载速度、运行时性能和内存使用等方面。

## 目录

1. [性能优化概览](#性能优化概览)
2. [文件大小优化](#文件大小优化)
3. [加载性能优化](#加载性能优化)
4. [运行时性能优化](#运行时性能优化)
5. [内存优化](#内存优化)
6. [平台特定优化](#平台特定优化)
7. [性能分析工具](#性能分析工具)
8. [优化检查清单](#优化检查清单)

---

## 性能优化概览

### 性能指标

| 指标 | 目标值 (移动端) | 目标值 (桌面端) | 测量方法 |
|------|----------------|----------------|---------|
| **文件大小** | <5 MB | <20 MB | 文件系统 |
| **加载时间** | <2 秒 | <1 秒 | 性能计时器 |
| **内存占用** | <100 MB | <500 MB | 内存分析器 |
| **帧率** | 30+ FPS | 60+ FPS | 渲染循环 |
| **三角形数** | <50K | <500K | 资产统计 |

### 优化优先级

```
高优先级 (必须做)
├─ 文件压缩 (Draco, KTX2)
├─ 纹理优化 (分辨率, 格式)
└─ 几何简化 (LOD)

中优先级 (应该做)
├─ 材质合并
├─ 异步加载
└─ 资源缓存

低优先级 (可以做)
├─ 网格量化
├─ 属性压缩
└─ 微优化
```

---

## 文件大小优化

### 1. 格式选择：GLB vs glTF

**GLB (二进制格式) - 推荐**
```
优势:
✅ 单文件，易于管理
✅ 二进制存储，更小更快
✅ 网络传输友好
✅ 支持所有压缩方式

劣势:
❌ 不易手动编辑
❌ 纹理无法单独更新
```

**glTF (JSON + 外部文件)**
```
优势:
✅ 可读性好，便于调试
✅ 纹理可独立更新
✅ 支持外部引用

劣势:
❌ 多文件管理复杂
❌ HTTP 请求数多
❌ 解析稍慢
```

**转换命令：**
```bash
# glTF → GLB (推荐生产环境)
gltf-pipeline -i model.gltf -o model.glb

# GLB → glTF (调试时使用)
gltf-pipeline -i model.glb -o model.gltf -s
```

### 2. Draco 几何压缩

Draco 是 Google 开发的 3D 几何压缩算法，可将网格数据压缩 90% 以上。

**压缩效果对比：**
| 数据类型 | 原始大小 | Draco 压缩后 | 压缩率 |
|---------|---------|------------|--------|
| 顶点位置 | 1.2 MB | 120 KB | 90% |
| 顶点法线 | 800 KB | 80 KB | 90% |
| UV 坐标 | 600 KB | 100 KB | 83% |
| 索引数据 | 400 KB | 50 KB | 87% |
| **总计** | **3.0 MB** | **350 KB** | **88%** |

**使用 gltfpack 应用 Draco：**
```bash
# 基础 Draco 压缩
gltfpack -i model.glb -o model_draco.glb -cc

# 高级配置
gltfpack -i model.glb -o model_draco.glb \
  -cc \                    # 启用 Draco
  -cp 14 \                 # 位置量化 14 bits (默认)
  -ct 12 \                 # UV 量化 12 bits
  -cn 10 \                 # 法线量化 10 bits
  -cg 8 \                  # 通用属性 8 bits
  -si 1.0                  # 不简化网格

# 查看压缩统计
gltfpack -i model.glb -o /dev/null -cc -v
```

**量化精度建议：**
| 属性 | 推荐精度 (bits) | 说明 |
|------|----------------|------|
| 位置 (Position) | 14-16 | 高精度，避免抖动 |
| 法线 (Normal) | 10-12 | 中精度，视觉影响小 |
| UV (TexCoord) | 10-12 | 中精度，避免纹理接缝 |
| 颜色 (Color) | 8-10 | 低精度，通常足够 |
| 权重 (Weights) | 8 | 低精度，骨骼蒙皮 |

**Filament 加载 Draco 资产：**
```cpp
// Filament 的 AssetLoader 自动支持 Draco 解压
// 需要确保链接了 Draco 库

// CMakeLists.txt
find_package(draco REQUIRED)
target_link_libraries(your_app PRIVATE draco)

// 代码中无需特殊处理
FilamentAsset* asset = assetLoader->createAsset(data, size);
// Draco 数据会自动解压
```

### 3. KTX2/Basis Universal 纹理压缩

Basis Universal 是一种通用纹理压缩格式，可以转码为各平台的原生格式。

**压缩效果对比：**
| 纹理 (2048×2048) | PNG | KTX2 (ETC1S) | KTX2 (UASTC) | 压缩率 |
|-----------------|-----|--------------|--------------|--------|
| BaseColor (sRGB) | 4.2 MB | 280 KB | 850 KB | 93% / 80% |
| Normal (Linear) | 8.1 MB | 420 KB | 1.2 MB | 95% / 85% |
| MetallicRoughness | 4.0 MB | 250 KB | 700 KB | 94% / 82% |
| **总计** | **16.3 MB** | **950 KB** | **2.75 MB** | **94% / 83%** |

**使用 gltfpack 应用 KTX2：**
```bash
# 基础 KTX2 压缩
gltfpack -i model.glb -o model_ktx2.glb -tc

# 高级配置
gltfpack -i model.glb -o model_ktx2.glb \
  -tc \                    # 启用 KTX2
  -tq 8 \                  # 纹理质量 0-10 (8=高质量)
  -tu \                    # 使用 UASTC (更高质量)
  -tp                      # 生成 mipmap

# ETC1S vs UASTC 对比
gltfpack -i model.glb -o model_etc1s.glb -tc -tq 8      # ETC1S: 更小，质量略低
gltfpack -i model.glb -o model_uastc.glb -tc -tu -tq 9  # UASTC: 更大，质量高
```

**手动使用 toktx 工具：**
```bash
# 安装 KTX-Software
brew install ktx  # macOS
# 或从源码构建

# PNG → KTX2 (Basis Universal)
toktx --bcmp --genmipmap output.ktx2 input.png

# 带质量控制
toktx --bcmp \
  --clevel 4 \        # 压缩级别 0-5 (4=推荐)
  --qlevel 255 \      # 质量级别 1-255 (255=最高)
  --genmipmap \       # 生成 mipmaps
  output.ktx2 input.png

# 法线贴图（使用 UASTC）
toktx --uastc \
  --uastc_quality 2 \ # UASTC 质量 0-4
  --normal_map \      # 标记为法线贴图
  --genmipmap \
  normal.ktx2 normal.png

# 批量处理
for f in textures/*.png; do
  toktx --bcmp --clevel 4 --qlevel 255 --genmipmap "${f%.png}.ktx2" "$f"
done
```

**Filament 加载 KTX2：**
```cpp
#include <ktxreader/Ktx2Reader.h>

Texture* loadKtx2(Engine* engine, const void* data, size_t size) {
    ktxreader::Ktx2Reader reader(*engine);

    // 解析 KTX2 数据
    Texture* texture = reader.load(data, size,
        ktxreader::Ktx2Reader::TransferFunction::sRGB);  // 或 LINEAR

    if (!texture) {
        utils::slog.e << "Failed to load KTX2 texture" << utils::io::endl;
        return nullptr;
    }

    return texture;
}

// 从文件加载
auto data = readFile("texture.ktx2");
Texture* tex = loadKtx2(engine, data.data(), data.size());
```

### 4. 网格简化

减少三角形数量以降低渲染开销。

**使用 gltfpack 简化：**
```bash
# 简化到 50% 三角形
gltfpack -i model.glb -o model_lod1.glb -si 0.5

# 简化到 25% (LOD2)
gltfpack -i model.glb -o model_lod2.glb -si 0.25 -sa 0.05

# 保护特定属性
gltfpack -i model.glb -o model_simplified.glb \
  -si 0.7 \        # 简化到 70%
  -slb \           # 锁定边界 (保护 UV 接缝)
  -sa 0.02 \       # 激进度 (允许 2% 误差)
  -kn              # 保留顶点法线

# 生成多级 LOD
gltfpack -i model.glb -o model_lod0.glb -si 1.0   # 原始
gltfpack -i model.glb -o model_lod1.glb -si 0.6   # 60%
gltfpack -i model.glb -o model_lod2.glb -si 0.3   # 30%
gltfpack -i model.glb -o model_lod3.glb -si 0.1   # 10%
```

**在 Blender 中使用 Decimate 修改器：**
```python
import bpy

def create_lods(obj, levels=[1.0, 0.6, 0.3, 0.1]):
    """为对象创建多级 LOD"""

    base_name = obj.name

    for i, ratio in enumerate(levels):
        # 复制对象
        lod_obj = obj.copy()
        lod_obj.data = obj.data.copy()
        lod_obj.name = f"{base_name}_LOD{i}"
        bpy.context.collection.objects.link(lod_obj)

        if ratio < 1.0:
            # 添加 Decimate 修改器
            mod = lod_obj.modifiers.new(name="Decimate", type='DECIMATE')
            mod.ratio = ratio
            mod.use_collapse_triangulate = True

            # 应用修改器
            bpy.context.view_layer.objects.active = lod_obj
            bpy.ops.object.modifier_apply(modifier="Decimate")

        print(f"Created {lod_obj.name}: {len(lod_obj.data.polygons)} polygons")

# 使用示例
create_lods(bpy.context.active_object)
```

### 5. 属性优化

**移除不需要的顶点属性：**
```bash
# 移除顶点颜色
gltfpack -i model.glb -o model_no_colors.glb -kc 0

# 移除切线（如果不使用法线贴图）
# (gltfpack 会自动检测并移除未使用的属性)

# 仅保留必需属性
gltfpack -i model.glb -o model_minimal.glb \
  -kn \    # 保留法线
  -kt \    # 保留切线（如果有法线贴图）
  -kc 0    # 移除顶点颜色
```

**在 Blender 导出时控制：**
```python
bpy.ops.export_scene.gltf(
    filepath="model.glb",
    export_colors=False,      # 不导出顶点颜色
    export_tangents=True,     # 仅在有法线贴图时
    export_extras=False       # 不导出自定义属性
)
```

---

## 加载性能优化

### 1. 异步加载

避免阻塞主线程，提供更流畅的用户体验。

**异步资源加载：**
```cpp
#include <thread>
#include <atomic>

class AsyncAssetLoader {
public:
    AsyncAssetLoader(Engine* engine, MaterialProvider* materials)
        : mEngine(engine), mMaterials(materials) {
        mAssetLoader = AssetLoader::create({engine, materials});
        mResourceLoader = new ResourceLoader({engine, true});  // 异步模式
    }

    void loadAssetAsync(const std::string& path, std::function<void(FilamentAsset*)> callback) {
        // 启动后台线程加载
        std::thread([this, path, callback]() {
            // 1. 读取文件（IO 密集）
            auto data = readFile(path);

            // 2. 解析 glTF (CPU 密集)
            FilamentAsset* asset = mAssetLoader->createAsset(data.data(), data.size());

            if (!asset) {
                callback(nullptr);
                return;
            }

            // 3. 异步加载资源（纹理、缓冲区）
            mResourceLoader->asyncBeginLoad(asset);

            // 4. 在主线程中完成
            mEngine->execute([this, asset, callback]() {
                // 等待资源加载完成
                while (!mResourceLoader->asyncGetLoadProgress() < 1.0f) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                }

                callback(asset);
            });
        }).detach();
    }

    float getProgress() const {
        return mResourceLoader->asyncGetLoadProgress();
    }

private:
    Engine* mEngine;
    MaterialProvider* mMaterials;
    AssetLoader* mAssetLoader;
    ResourceLoader* mResourceLoader;
};

// 使用示例
AsyncAssetLoader loader(engine, materials);

loader.loadAssetAsync("model.glb", [scene](FilamentAsset* asset) {
    if (asset) {
        scene->addEntities(asset->getEntities(), asset->getEntityCount());
        utils::slog.i << "Asset loaded successfully!" << utils::io::endl;
    }
});

// 显示加载进度
while (loader.getProgress() < 1.0f) {
    float progress = loader.getProgress();
    // 更新进度条 UI
    updateProgressBar(progress * 100.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
```

### 2. 渐进式加载

先加载低分辨率版本，再逐步加载高质量版本。

**LOD 流式加载：**
```cpp
class StreamingLODLoader {
public:
    void loadWithLOD(const std::string& basePath) {
        // 阶段 1: 加载最低 LOD (立即显示)
        loadLOD(basePath + "_lod3.glb", 0);

        // 阶段 2: 后台加载中等 LOD
        loadLODAsync(basePath + "_lod1.glb", 1);

        // 阶段 3: 最后加载完整模型
        loadLODAsync(basePath + "_lod0.glb", 2);
    }

private:
    void loadLOD(const std::string& path, int level) {
        auto data = readFile(path);
        mAssets[level] = mAssetLoader->createAsset(data.data(), data.size());
        mResourceLoader->loadResources(mAssets[level]);

        // 显示当前 LOD
        if (mCurrentAsset) {
            mScene->removeEntities(mCurrentAsset->getEntities(),
                                   mCurrentAsset->getEntityCount());
        }
        mScene->addEntities(mAssets[level]->getEntities(),
                            mAssets[level]->getEntityCount());
        mCurrentAsset = mAssets[level];
    }

    void loadLODAsync(const std::string& path, int level) {
        std::thread([this, path, level]() {
            auto data = readFile(path);
            mAssets[level] = mAssetLoader->createAsset(data.data(), data.size());
            mResourceLoader->asyncBeginLoad(mAssets[level]);

            // 加载完成后在主线程切换
            mEngine->execute([this, level]() {
                if (mResourceLoader->asyncGetLoadProgress() >= 1.0f) {
                    swapLOD(level);
                }
            });
        }).detach();
    }

    void swapLOD(int level) {
        if (mCurrentAsset) {
            mScene->removeEntities(mCurrentAsset->getEntities(),
                                   mCurrentAsset->getEntityCount());
        }
        mScene->addEntities(mAssets[level]->getEntities(),
                            mAssets[level]->getEntityCount());
        mCurrentAsset = mAssets[level];

        utils::slog.i << "Swapped to LOD " << level << utils::io::endl;
    }

    std::map<int, FilamentAsset*> mAssets;
    FilamentAsset* mCurrentAsset = nullptr;
    AssetLoader* mAssetLoader;
    ResourceLoader* mResourceLoader;
    Scene* mScene;
    Engine* mEngine;
};
```

### 3. MaterialProvider 选择

根据平台选择合适的材质提供器。

**性能对比：**
| MaterialProvider | 加载时间 | 内存占用 | 适用平台 | 特点 |
|-----------------|---------|---------|---------|------|
| **JitShaderProvider** | 慢 (500-2000ms) | 低 (~50MB) | 桌面 | 运行时生成，灵活 |
| **UbershaderProvider** | 快 (50-200ms) | 高 (~200MB) | 移动 | 预编译，性能好 |

**智能选择策略：**
```cpp
MaterialProvider* createOptimalMaterialProvider(Engine* engine) {
    #if defined(__ANDROID__)
        // Android: 使用 Ubershader (更快)
        return createUbershaderProvider(engine, UBERARCHIVE_DEFAULT_DATA,
                                        UBERARCHIVE_DEFAULT_SIZE);

    #elif defined(__IPHONE__)
        // iOS: 也推荐 Ubershader
        return createUbershaderProvider(engine, UBERARCHIVE_DEFAULT_DATA,
                                        UBERARCHIVE_DEFAULT_SIZE);

    #else
        // 桌面: 使用 JIT (更灵活，支持更多扩展)
        return createJitShaderProvider(engine);
    #endif
}
```

### 4. 资源预加载

在需要之前预先加载资源。

**预加载系统：**
```cpp
class AssetPreloader {
public:
    void preloadAssets(const std::vector<std::string>& paths) {
        for (const auto& path : paths) {
            std::thread([this, path]() {
                auto data = readFile(path);
                FilamentAsset* asset = mAssetLoader->createAsset(data.data(), data.size());

                if (asset) {
                    mResourceLoader->loadResources(asset);
                    asset->releaseSourceData();  // 释放解析数据
                    mPreloadedAssets[path] = asset;
                }
            }).detach();
        }
    }

    FilamentAsset* getAsset(const std::string& path) {
        auto it = mPreloadedAssets.find(path);
        if (it != mPreloadedAssets.end()) {
            return it->second;
        }
        return nullptr;  // 需要同步加载
    }

private:
    std::unordered_map<std::string, FilamentAsset*> mPreloadedAssets;
    AssetLoader* mAssetLoader;
    ResourceLoader* mResourceLoader;
};

// 使用示例：游戏关卡加载
AssetPreloader preloader(assetLoader, resourceLoader);

// 在主菜单时预加载第一关资源
preloader.preloadAssets({
    "level1/environment.glb",
    "level1/characters.glb",
    "level1/props.glb"
});

// 进入关卡时直接使用（几乎瞬间）
FilamentAsset* env = preloader.getAsset("level1/environment.glb");
scene->addEntities(env->getEntities(), env->getEntityCount());
```

---

## 运行时性能优化

### 1. LOD (Level of Detail) 系统

根据相机距离自动切换模型细节。

**距离 LOD：**
```cpp
#include <filament/TransformManager.h>
#include <math/vec3.h>

class LODSystem {
public:
    struct LODLevel {
        FilamentAsset* asset;
        float distance;  // 最大显示距离
    };

    void addLODGroup(const std::string& name, const std::vector<LODLevel>& levels) {
        mLODGroups[name] = levels;
        mCurrentLODs[name] = 0;  // 默认使用最高 LOD
    }

    void update(const math::float3& cameraPos, EntityManager& em, Scene* scene) {
        auto& tcm = mEngine->getTransformManager();

        for (auto& [name, levels] : mLODGroups) {
            // 计算到对象的距离
            auto entity = levels[0].asset->getRoot();
            auto worldPos = tcm.getWorldTransform(tcm.getInstance(entity))[3].xyz;
            float distance = math::distance(cameraPos, worldPos);

            // 确定应使用的 LOD
            size_t targetLOD = levels.size() - 1;
            for (size_t i = 0; i < levels.size(); i++) {
                if (distance < levels[i].distance) {
                    targetLOD = i;
                    break;
                }
            }

            // 如果 LOD 需要切换
            if (mCurrentLODs[name] != targetLOD) {
                switchLOD(name, mCurrentLODs[name], targetLOD, scene);
                mCurrentLODs[name] = targetLOD;
            }
        }
    }

private:
    void switchLOD(const std::string& name, size_t from, size_t to, Scene* scene) {
        auto& levels = mLODGroups[name];

        // 移除旧 LOD
        scene->removeEntities(levels[from].asset->getEntities(),
                              levels[from].asset->getEntityCount());

        // 添加新 LOD
        scene->addEntities(levels[to].asset->getEntities(),
                           levels[to].asset->getEntityCount());

        utils::slog.i << name << ": LOD " << from << " -> " << to << utils::io::endl;
    }

    std::unordered_map<std::string, std::vector<LODLevel>> mLODGroups;
    std::unordered_map<std::string, size_t> mCurrentLODs;
    Engine* mEngine;
};

// 使用示例
LODSystem lodSystem(engine);

// 添加 LOD 组（距离从近到远）
lodSystem.addLODGroup("tree", {
    {treeLOD0, 50.0f},    // 0-50m: 高细节
    {treeLOD1, 150.0f},   // 50-150m: 中细节
    {treeLOD2, 500.0f},   // 150-500m: 低细节
    {treeLOD3, 1000.0f}   // 500-1000m: 极简
});

// 每帧更新
math::float3 cameraPos = getCameraPosition();
lodSystem.update(cameraPos, entityManager, scene);
```

### 2. GPU 实例化

使用一个 draw call 渲染多个相同对象。

**实例化加载：**
```cpp
#include <gltfio/AssetLoader.h>

// 加载基础资产
FilamentAsset* baseAsset = assetLoader->createAsset(data, size);
resourceLoader->loadResources(baseAsset);

// 创建 100 个实例
std::vector<FilamentInstance*> instances;
for (int i = 0; i < 100; i++) {
    FilamentInstance* instance = assetLoader->createInstance(baseAsset);
    instances.push_back(instance);

    // 设置每个实例的位置
    auto& tcm = engine->getTransformManager();
    auto root = instance->getRoot();
    auto ti = tcm.getInstance(root);

    math::mat4f transform = math::mat4f::translation(
        math::float3{i * 2.0f, 0.0f, 0.0f}
    );
    tcm.setTransform(ti, transform);

    // 添加到场景
    scene->addEntities(instance->getEntities(), instance->getEntityCount());
}

// 性能对比：
// 100 个独立资产: 100 draw calls, ~500MB 内存
// 100 个实例: 1 draw call, ~50MB 内存
```

**动态实例化（树木、草地）：**
```cpp
class InstancedForest {
public:
    InstancedForest(Engine* engine, FilamentAsset* treeAsset, int count)
        : mEngine(engine), mBaseAsset(treeAsset) {

        auto& tcm = engine->getTransformManager();

        for (int i = 0; i < count; i++) {
            FilamentInstance* instance = mAssetLoader->createInstance(treeAsset);

            // 随机位置
            float x = randomFloat(-100.0f, 100.0f);
            float z = randomFloat(-100.0f, 100.0f);
            float scale = randomFloat(0.8f, 1.2f);
            float rotation = randomFloat(0.0f, 2.0f * M_PI);

            math::mat4f transform =
                math::mat4f::translation(math::float3{x, 0, z}) *
                math::mat4f::rotation(rotation, math::float3{0, 1, 0}) *
                math::mat4f::scaling(scale);

            auto ti = tcm.getInstance(instance->getRoot());
            tcm.setTransform(ti, transform);

            mInstances.push_back(instance);
        }
    }

    void addToScene(Scene* scene) {
        for (auto* instance : mInstances) {
            scene->addEntities(instance->getEntities(), instance->getEntityCount());
        }
    }

private:
    Engine* mEngine;
    AssetLoader* mAssetLoader;
    FilamentAsset* mBaseAsset;
    std::vector<FilamentInstance*> mInstances;
};

// 创建 1000 棵树的森林（几乎零开销）
InstancedForest forest(engine, treeAsset, 1000);
forest.addToScene(scene);
```

### 3. 视锥剔除

只渲染相机视野内的对象。

**基础剔除：**
```cpp
#include <filament/RenderableManager.h>
#include <filament/Box.h>

class FrustumCuller {
public:
    void updateVisibility(Camera* camera, Scene* scene, EntityManager& em) {
        auto& rcm = mEngine->getRenderableManager();
        auto& tcm = mEngine->getTransformManager();

        // 获取相机视锥体
        math::mat4f viewProj = camera->getProjectionMatrix() * camera->getViewMatrix();
        Frustum frustum(viewProj);

        size_t culledCount = 0;

        for (Entity entity : mAllEntities) {
            auto ri = rcm.getInstance(entity);
            if (!ri) continue;

            // 获取世界空间 AABB
            Box aabb = rcm.getAxisAlignedBoundingBox(ri);
            auto ti = tcm.getInstance(entity);
            math::mat4f worldTransform = tcm.getWorldTransform(ti);

            Box worldAABB = aabb.transform(worldTransform);

            // 视锥剔除检测
            bool visible = frustum.intersects(worldAABB);

            // 设置可见性
            rcm.setLayerMask(ri, visible ? 0xFF : 0x00);

            if (!visible) culledCount++;
        }

        utils::slog.i << "Culled " << culledCount << " / "
                      << mAllEntities.size() << " objects" << utils::io::endl;
    }

private:
    Engine* mEngine;
    std::vector<Entity> mAllEntities;
};
```

### 4. 材质批处理

合并使用相同材质的对象，减少状态切换。

**材质排序渲染：**
```cpp
// Filament 自动进行材质批处理，但可以优化资产结构

// ❌ 不好：每个对象使用不同材质
model1.mat -> red_material
model2.mat -> blue_material
model3.mat -> green_material
// 结果：3 个 draw calls

// ✅ 好：对象共享材质，使用顶点颜色或纹理区分
model1.mat -> shared_material (使用 baseColorFactor 或纹理 atlas)
model2.mat -> shared_material
model3.mat -> shared_material
// 结果：1 个 draw call（如果启用实例化）
```

**在 Blender 中合并材质：**
```python
import bpy

def merge_materials(objects, base_material_name="Merged_Material"):
    """合并多个对象的材质到一个材质"""

    # 创建合并后的材质
    merged_mat = bpy.data.materials.new(name=base_material_name)
    merged_mat.use_nodes = True

    # 替换所有对象的材质
    for obj in objects:
        if obj.type != 'MESH':
            continue

        # 清除现有材质
        obj.data.materials.clear()

        # 分配合并材质
        obj.data.materials.append(merged_mat)

    print(f"Merged {len(objects)} objects to use {base_material_name}")

# 使用示例：合并所有选中对象的材质
merge_materials(bpy.context.selected_objects)
```

---

## 内存优化

### 1. 释放源数据

加载完成后释放不再需要的数据。

```cpp
// 加载资产
FilamentAsset* asset = assetLoader->createAsset(data, size);
resourceLoader->loadResources(asset);

// 添加到场景
scene->addEntities(asset->getEntities(), asset->getEntityCount());

// ✅ 释放源数据（节省内存）
asset->releaseSourceData();

// 内存节省：
// 保留源数据: 资产大小 + GPU 数据 (例如 10MB + 5MB = 15MB)
// 释放源数据: 仅 GPU 数据 (5MB)
```

### 2. 资源共享

多个资产共享纹理和材质。

**纹理共享：**
```cpp
class SharedTextureManager {
public:
    Texture* getTexture(const std::string& path) {
        // 检查缓存
        auto it = mTextureCache.find(path);
        if (it != mTextureCache.end()) {
            return it->second;
        }

        // 加载新纹理
        auto data = readFile(path);
        Texture* texture = loadTexture(mEngine, data.data(), data.size());
        mTextureCache[path] = texture;

        return texture;
    }

    void clear() {
        for (auto& [path, texture] : mTextureCache) {
            mEngine->destroy(texture);
        }
        mTextureCache.clear();
    }

private:
    Engine* mEngine;
    std::unordered_map<std::string, Texture*> mTextureCache;
};

// 使用示例
SharedTextureManager textures(engine);

// 多个材质实例共享同一纹理
Texture* baseColor = textures.getTexture("basecolor.png");
materialInstance1->setParameter("baseColorMap", baseColor, sampler);
materialInstance2->setParameter("baseColorMap", baseColor, sampler);
// 内存：1 份纹理数据，而非 2 份
```

### 3. 纹理 Atlas

合并多个小纹理为一个大纹理。

**Blender 纹理打包：**
```python
import bpy

def create_texture_atlas(objects, atlas_size=2048):
    """将多个对象的纹理合并为一个 atlas"""

    # 1. 创建新材质和图像
    atlas_mat = bpy.data.materials.new(name="Atlas_Material")
    atlas_mat.use_nodes = True
    nodes = atlas_mat.node_tree.nodes

    atlas_image = bpy.data.images.new("Atlas", width=atlas_size, height=atlas_size)

    # 2. 为所有对象重新打包 UV
    for obj in objects:
        if obj.type != 'MESH':
            continue

        # 清除材质
        obj.data.materials.clear()
        obj.data.materials.append(atlas_mat)

    # 3. 选择所有对象并打包 UV
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.select_set(True)

    bpy.ops.object.join()  # 合并对象
    bpy.context.view_layer.objects.active = bpy.context.selected_objects[0]

    # 4. 智能 UV 打包
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(island_margin=0.02)
    bpy.ops.object.mode_set(mode='OBJECT')

    print("Texture atlas created")

# 使用示例
create_texture_atlas(bpy.context.selected_objects)
```

### 4. Mipmap 生成

生成多级渐远纹理，减少显存占用和带宽。

```bash
# 使用 toktx 生成 mipmaps
toktx --genmipmap texture_mip.ktx2 texture.png

# 使用 gltfpack 自动生成
gltfpack -i model.glb -o model_mip.glb -tc -tp
```

**Filament 中启用 mipmaps：**
```cpp
TextureSampler sampler(
    TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,  // 使用 mipmaps
    TextureSampler::MagFilter::LINEAR
);

materialInstance->setParameter("baseColorMap", texture, sampler);

// 内存对比（2048×2048 纹理）：
// 无 mipmap: 16 MB
// 有 mipmap: 21 MB (16 + 4 + 1 + ...)
// 但显存带宽降低 50-70%，渲染更快
```

---

## 平台特定优化

### 1. Android 优化

```cpp
// 使用 Ubershader (更快的材质加载)
MaterialProvider* materials = createUbershaderProvider(
    engine, UBERARCHIVE_DEFAULT_DATA, UBERARCHIVE_DEFAULT_SIZE
);

// 激进的几何压缩
gltfpack -i model.glb -o model_android.glb \
  -cc -tc -si 0.5 -mi

// 限制纹理分辨率
gltfpack -i model.glb -o model_android.glb \
  -tc -ts 1024  # 最大 1024×1024

// 使用 ETC2 纹理压缩 (Android 原生)
toktx --target_type ETC2 --genmipmap output.ktx2 input.png
```

### 2. iOS 优化

```cpp
// 使用 ASTC 纹理压缩 (iOS 原生)
toktx --target_type ASTC --astc_blk_d 6x6 --genmipmap output.ktx2 input.png

// Metal 后端特定优化
Engine::Config engineConfig;
engineConfig.backend = Engine::Backend::METAL;
Engine* engine = Engine::create(engineConfig);
```

### 3. WebGL 优化

```bash
# 极致压缩（网络传输）
gltfpack -i model.glb -o model_web.glb \
  -cc -tc -si 0.3 -mi -tq 6

# 文件大小对比：
# 原始: 10 MB
# 优化后: 800 KB (-92%)
```

---

## 性能分析工具

### 1. Filament 性能分析

**帧率监控：**
```cpp
#include <chrono>

class PerformanceMonitor {
public:
    void beginFrame() {
        mFrameStart = std::chrono::high_resolution_clock::now();
    }

    void endFrame() {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            now - mFrameStart);

        mFrameTimes.push_back(duration.count() / 1000.0f);  // ms

        if (mFrameTimes.size() > 60) {
            mFrameTimes.erase(mFrameTimes.begin());
        }

        // 每秒打印统计
        if (++mFrameCount % 60 == 0) {
            printStats();
        }
    }

    void printStats() {
        float avg = 0.0f;
        float min = std::numeric_limits<float>::max();
        float max = 0.0f;

        for (float t : mFrameTimes) {
            avg += t;
            min = std::min(min, t);
            max = std::max(max, t);
        }
        avg /= mFrameTimes.size();

        float fps = 1000.0f / avg;

        utils::slog.i << "FPS: " << fps
                      << " | Frame: " << avg << "ms"
                      << " (min: " << min << ", max: " << max << ")"
                      << utils::io::endl;
    }

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> mFrameStart;
    std::vector<float> mFrameTimes;
    int mFrameCount = 0;
};
```

### 2. 资产统计工具

```bash
# gltf-validator 详细报告
gltf-validator model.glb -r report.json -a

# 查看统计
cat report.json | jq '.info.totalTriangleCount'
cat report.json | jq '.info.totalVertexCount'
cat report.json | jq '.info.resources'

# gltfpack 统计
gltfpack -i model.glb -o /dev/null -v
```

### 3. 内存分析

```cpp
// GPU 内存使用
size_t textureMemory = engine->getDriver()->getTextureMemoryUsage();
size_t bufferMemory = engine->getDriver()->getBufferObjectMemoryUsage();

utils::slog.i << "GPU Memory: "
              << (textureMemory + bufferMemory) / (1024 * 1024) << " MB"
              << utils::io::endl;
```

---

## 优化检查清单

### 文件大小
- [ ] 使用 GLB 格式
- [ ] 启用 Draco 压缩 (-cc)
- [ ] 启用 KTX2 纹理压缩 (-tc)
- [ ] 简化网格 (-si 0.5-0.8)
- [ ] 移除不需要的属性
- [ ] 验证文件大小 <5MB (移动端)

### 加载性能
- [ ] 使用异步加载
- [ ] 选择合适的 MaterialProvider
- [ ] 预加载关键资源
- [ ] 实现渐进式加载
- [ ] 验证加载时间 <2秒

### 运行时性能
- [ ] 实现 LOD 系统
- [ ] 使用 GPU 实例化
- [ ] 启用视锥剔除
- [ ] 合并材质
- [ ] 目标 30+ FPS (移动) / 60+ FPS (桌面)

### 内存优化
- [ ] 释放源数据 (releaseSourceData)
- [ ] 共享纹理资源
- [ ] 使用纹理 atlas
- [ ] 生成 mipmaps
- [ ] 验证内存 <100MB (移动)

### 平台特定
- [ ] Android: Ubershader + ETC2
- [ ] iOS: Ubershader + ASTC
- [ ] WebGL: 激进压缩
- [ ] 桌面: JIT + 高质量纹理

使用这份检查清单确保你的 glTF 资产在所有平台上都能达到最佳性能！
