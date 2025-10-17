# gltfio_ext 改进 - 测试计划

**版本**: 1.0
**日期**: 2025-10-16
**覆盖范围**: Phase 1 (P0) + Phase 2 (P1)

---

## 1. 测试策略总览

### 1.1 测试层次

```
┌─────────────────────────────────┐
│   视觉测试 (Visual Testing)      │  人工验证渲染效果
├─────────────────────────────────┤
│   性能测试 (Performance)         │  内存、帧率、加载时间
├─────────────────────────────────┤
│   集成测试 (Integration)         │  真实场景、端到端流程
├─────────────────────────────────┤
│   单元测试 (Unit Testing)        │  函数级别、隔离测试
└─────────────────────────────────┘
```

### 1.2 测试覆盖目标

| 测试类型 | 覆盖目标 | 工具 |
|---------|---------|------|
| 单元测试 | 80% 代码覆盖率 | Google Test |
| 集成测试 | 所有核心场景 | 自定义测试 app |
| 性能测试 | 关键性能指标 | Benchmark + Profiler |
| 视觉测试 | 渲染正确性 | 人工 + glTF Viewer 对比 |

### 1.3 测试环境

| 平台 | 优先级 | 说明 |
|------|--------|------|
| macOS (x64) | P0 | 主要开发平台 |
| Android (ARM64) | P1 | 主要目标平台 |
| iOS (ARM64) | P2 | 次要目标平台 |
| Linux (x64) | P2 | CI/CD 环境 |

---

## 2. 单元测试计划

### 2.1 测试框架

使用 Google Test (gtest)，集成到现有测试框架。

**测试文件结构**:
```
libs/gltfio_ext/tests/
├── test_texture_loading.cpp    # P0-1: 纹理加载
├── test_tangent.cpp            # P0-2: Normal/Tangent
├── test_mesh_instance.cpp      # P1-1: MeshInstance
├── test_crossfade.cpp          # P1-2: CrossFade
├── test_cubic_interpolation.cpp # P1-3: CUBIC 插值
└── test_regression.cpp         # 回归测试
```

### 2.2 P0-1: 纹理加载测试

**文件**: `test_texture_loading.cpp`

**测试用例**:

```cpp
// 1. 基础功能
TEST(TextureLoading, GetRequiredTextures_ExternalUri) {
    // 验证外部 URI 纹理信息正确
}

TEST(TextureLoading, GetRequiredTextures_Embedded) {
    // 验证嵌入式纹理信息正确
}

TEST(TextureLoading, GetRequiredTextures_MultiplePrimitives) {
    // 验证多 primitive 场景
}

TEST(TextureLoading, GetRequiredTextures_NoTextures) {
    // 验证无纹理模型（返回空列表）
}

// 2. 纹理绑定
TEST(TextureLoading, BindTexture_ValidIndex) {
    // 验证正常绑定
}

TEST(TextureLoading, BindTexture_InvalidIndex) {
    // 验证无效索引（应触发断言）
    EXPECT_DEATH(mesh->bindTexture(999, "baseColorMap", tex, sampler), "");
}

TEST(TextureLoading, BindTexture_MultipleSlots) {
    // 验证绑定多个槽位（baseColorMap + normalMap）
}

// 3. Sampler 创建
TEST(TextureLoading, CreateSampler_AllWrapModes) {
    // 验证所有 Wrap 模式正确转换
}

TEST(TextureLoading, CreateSampler_AllFilterModes) {
    // 验证所有 Filter 模式正确转换
}

// 4. 边界情况
TEST(TextureLoading, BindTexture_NullTexture) {
    // 验证绑定 nullptr（应触发断言）
}

TEST(TextureLoading, BindTexture_BeforeUploadResources) {
    // 验证在 uploadResources() 之前绑定
}
```

**预计数量**: 10-15 个测试用例

### 2.3 P0-2: Normal/Tangent 测试

**文件**: `test_tangent.cpp`

**测试用例**:

```cpp
// 1. TBN Quaternion 计算
TEST(Tangent, BuildTbnQuaternion_Orthogonal) {
    // 测试正交 TBN 矩阵
    float3 t = {1, 0, 0};
    float3 b = {0, 1, 0};
    float3 n = {0, 0, 1};

    quatf q = buildTbnQuaternion(t, b, n);

    // 反向验证
    mat3f tbn = mat3f::unpackTangentFrame(q);
    EXPECT_NEAR(tbn[0].x, 1.0f, 0.01f);
}

TEST(Tangent, BuildTbnQuaternion_NonOrthogonal) {
    // 测试非完美正交的 TBN（应自动正交化）
}

// 2. 加载测试
TEST(Tangent, LoadMeshWithTangent) {
    // 加载带 TANGENT 属性的模型
}

TEST(Tangent, LoadMeshWithoutTangent) {
    // 加载没有 TANGENT 属性的模型（应自动计算）
}

// 3. 边界情况
TEST(Tangent, ComputeTangent_DegenerateUV) {
    // 测试退化 UV（UV 全为 0）
}
```

**预计数量**: 6-8 个测试用例

### 2.4 P1-1: MeshInstance 测试

**文件**: `test_mesh_instance.cpp`

**测试用例**:

```cpp
// 1. 创建和销毁
TEST(MeshInstance, CreateAndDestroy) {
    MeshAsset* mesh = loadTestMesh();
    MeshInstance* instance = mesh->createInstance();
    ASSERT_NE(instance, nullptr);
    EXPECT_EQ(mesh->getInstanceCount(), 1);

    mesh->destroyInstance(instance);
    EXPECT_EQ(mesh->getInstanceCount(), 0);
}

TEST(MeshInstance, CreateMultipleInstances) {
    // 创建 10 个实例
    MeshAsset* mesh = loadTestMesh();
    std::vector<MeshInstance*> instances;

    for (int i = 0; i < 10; i++) {
        instances.push_back(mesh->createInstance());
    }

    EXPECT_EQ(mesh->getInstanceCount(), 10);
}

// 2. 资源共享
TEST(MeshInstance, SharedVertexBuffer) {
    // 验证共享 VertexBuffer
}

TEST(MeshInstance, SharedIndexBuffer) {
    // 验证共享 IndexBuffer
}

TEST(MeshInstance, IndependentEntity) {
    // 验证独立 Entity
    MeshAsset* mesh = loadTestMesh();
    MeshInstance* inst1 = mesh->createInstance();
    MeshInstance* inst2 = mesh->createInstance();

    EXPECT_NE(inst1->getRenderableEntity(), inst2->getRenderableEntity());
}

TEST(MeshInstance, IndependentMaterialInstance) {
    // 验证独立 MaterialInstance
    MeshAsset* mesh = loadTestMesh();
    MeshInstance* inst1 = mesh->createInstance();
    MeshInstance* inst2 = mesh->createInstance();

    // 修改 inst1 的材质
    inst1->getMaterialInstance(0)->setParameter("baseColorFactor", float4{1, 0, 0, 1});

    // 验证 inst2 不受影响
    float4 color;
    inst2->getMaterialInstance(0)->getParameter("baseColorFactor", &color);
    EXPECT_NE(color.r, 1.0f);
}

// 3. 骨骼绑定
TEST(MeshInstance, BindSkeleton) {
    // 验证骨骼绑定
}

TEST(MeshInstance, UpdateSkinning) {
    // 验证蒙皮更新
}

// 4. 生命周期
TEST(MeshInstance, DestroyAssetCleansUpInstances) {
    // 验证销毁 MeshAsset 时自动清理所有实例
    MeshAsset* mesh = loadTestMesh();
    mesh->createInstance();
    mesh->createInstance();

    // MeshAsset 析构函数应清理所有实例（无内存泄漏）
    delete mesh;
    // Valgrind 验证
}
```

**预计数量**: 10-12 个测试用例

### 2.5 P1-2: CrossFade 测试

**文件**: `test_crossfade.cpp`

**测试用例**:

```cpp
// 1. 权重插值
TEST(CrossFade, WeightInterpolation) {
    StandaloneAnimator* animator = createTestAnimator();

    int anim1 = animator->playAnimation(idleAnim, 0, 1.0f, true);
    int anim2 = animator->playAnimation(runAnim, 0, 0.0f, true);

    animator->crossFade(anim1, anim2, 1.0f);

    // t=0.0s
    EXPECT_TRUE(animator->isCrossFading());
    EXPECT_FLOAT_EQ(animator->getCrossFadeProgress(), 0.0f);
    EXPECT_FLOAT_EQ(getAnimWeight(anim1), 1.0f);
    EXPECT_FLOAT_EQ(getAnimWeight(anim2), 0.0f);

    // t=0.5s
    animator->update(0.5f);
    EXPECT_FLOAT_EQ(animator->getCrossFadeProgress(), 0.5f);
    EXPECT_FLOAT_EQ(getAnimWeight(anim1), 0.5f);
    EXPECT_FLOAT_EQ(getAnimWeight(anim2), 0.5f);

    // t=1.0s
    animator->update(0.5f);
    EXPECT_FALSE(animator->isCrossFading());
    EXPECT_FLOAT_EQ(getAnimWeight(anim1), 0.0f);
    EXPECT_FLOAT_EQ(getAnimWeight(anim2), 1.0f);
}

// 2. 旧动画停止
TEST(CrossFade, OldAnimationStopped) {
    // 验证过渡完成后旧动画自动停止
}

// 3. 边界情况
TEST(CrossFade, ZeroDuration) {
    // 验证 duration=0（瞬间切换）
}

TEST(CrossFade, MultipleCrossFades) {
    // 验证连续多次 CrossFade（打断前一个）
}
```

**预计数量**: 6-8 个测试用例

### 2.6 P1-3: CUBIC 插值测试

**文件**: `test_cubic_interpolation.cpp`

**测试用例**:

```cpp
// 1. Hermite 样条
TEST(CubicInterpolation, HermiteSpline_Endpoints) {
    // 验证端点值
    float3 p0 = {0, 0, 0};
    float3 m0 = {1, 0, 0};
    float3 p1 = {2, 0, 0};
    float3 m1 = {1, 0, 0};

    float3 v0 = cubicHermiteSpline(0.0f, p0, m0, p1, m1);
    EXPECT_FLOAT_EQ(v0.x, 0.0f);

    float3 v1 = cubicHermiteSpline(1.0f, p0, m0, p1, m1);
    EXPECT_FLOAT_EQ(v1.x, 2.0f);
}

TEST(CubicInterpolation, HermiteSpline_Midpoint) {
    // 验证中点值合理
}

// 2. 加载 CUBIC 动画
TEST(CubicInterpolation, LoadCubicAnimation) {
    // 验证正确读取 CUBIC 格式数据
}

TEST(CubicInterpolation, CubicKeyframeData) {
    // 验证关键帧包含 in-tangent + value + out-tangent
}

// 3. 插值应用
TEST(CubicInterpolation, ApplyCubicToTranslation) {
    // 验证 CUBIC 应用到 Translation 通道
}

TEST(CubicInterpolation, ApplyCubicToScale) {
    // 验证 CUBIC 应用到 Scale 通道
}
```

**预计数量**: 6-8 个测试用例

### 2.7 回归测试

**文件**: `test_regression.cpp`

**目的**: 确保新功能不破坏现有功能

**测试用例**:

```cpp
// 1. 现有功能不受影响
TEST(Regression, BasicMeshLoading) {
    // 验证基础 mesh 加载仍可用
}

TEST(Regression, SkeletalAnimation) {
    // 验证骨骼动画仍可用
}

TEST(Regression, EquipmentSystem) {
    // 验证换装系统仍可用
}

// 2. API 兼容性
TEST(Regression, BackwardCompatibility) {
    // 验证旧代码仍可编译和运行
    MeshAsset* mesh = loader->loadMesh(data, size);
    mesh->uploadResources();
    scene->addEntity(mesh->getRenderableEntity());  // ✅
}
```

**预计数量**: 5-6 个测试用例

---

## 3. 集成测试计划

### 3.1 测试场景矩阵

| 场景 | 测试内容 | 涉及功能 | 优先级 |
|------|---------|---------|--------|
| **场景 1: 简单纹理模型** | BoxTextured 显示 | P0-1, P0-2 | P0 |
| **场景 2: 多纹理模型** | 所有纹理正确显示 | P0-1, P0-2 | P0 |
| **场景 3: 换装系统** | skeleton + mesh 分离加载 | P0-1, P0-2 | P0 |
| **场景 4: 实例化** | 100 个相同模型 | P1-1 | P1 |
| **场景 5: 动画过渡** | idle → run CrossFade | P1-2 | P1 |
| **场景 6: CUBIC 动画** | 平滑曲线动画 | P1-3 | P1 |
| **场景 7: 综合场景** | 所有功能组合 | All | P1 |

### 3.2 场景 1: 简单纹理模型

**测试模型**: Khronos glTF Sample Models/BoxTextured

**测试步骤**:
```cpp
// 1. 加载 mesh
MeshAsset* mesh = loader->loadMesh("BoxTextured.glb");

// 2. 加载纹理
auto textures = mesh->getRequiredTextures();
for (const auto& texInfo : textures) {
    Texture* tex = loadTexture(texInfo);
    mesh->bindTexture(texInfo.primitiveIndex, texInfo.slot, tex, sampler);
}

// 3. 上传资源
mesh->uploadResources();

// 4. 渲染
scene->addEntity(mesh->getRenderableEntity());
renderFrame();

// 5. 截图对比
saveScreenshot("BoxTextured_gltfio_ext.png");
```

**验收标准**:
- ✅ 纹理正确显示（与 glTF Viewer 对比）
- ✅ 无黑色区域
- ✅ UV 坐标正确

### 3.3 场景 2: 多纹理模型

**测试模型**: 自定义模型（Base Color + Normal + Metallic Roughness）

**验收标准**:
- ✅ Base Color Map 显示
- ✅ Normal Map 生效（表面有凹凸细节）
- ✅ Metallic Roughness Map 生效（金属感正确）

### 3.4 场景 3: 换装系统

**测试模型**: skeleton.glb + lightArmor.glb + heavyArmor.glb

**测试步骤**:
```cpp
// 1. 加载骨骼
SkeletonAsset* skeleton = loader->loadSkeleton("skeleton.glb");

// 2. 加载两套装备
MeshAsset* lightArmor = loader->loadMesh("lightArmor.glb");
MeshAsset* heavyArmor = loader->loadMesh("heavyArmor.glb");

// 3. 加载纹理
loadTexturesForMesh(lightArmor);
loadTexturesForMesh(heavyArmor);

// 4. 绑定骨骼
lightArmor->bindSkeleton(skeleton);
heavyArmor->bindSkeleton(skeleton);

// 5. 上传资源
lightArmor->uploadResources();
heavyArmor->uploadResources();

// 6. 切换装备
scene->addEntity(lightArmor->getRenderableEntity());
// ... 切换到重型盔甲 ...
scene->removeEntity(lightArmor->getRenderableEntity());
scene->addEntity(heavyArmor->getRenderableEntity());
```

**验收标准**:
- ✅ 轻型盔甲纹理正确
- ✅ 重型盔甲纹理正确
- ✅ 切换无闪烁

### 3.5 场景 4: 实例化

**测试模型**: enemy.glb × 100

**测试步骤**:
```cpp
// 1. 加载一次
MeshAsset* enemyAsset = loader->loadMesh("enemy.glb");
loadTexturesForMesh(enemyAsset);
enemyAsset->uploadResources();

// 2. 创建 100 个实例
for (int i = 0; i < 100; i++) {
    MeshInstance* instance = enemyAsset->createInstance();
    setPosition(instance, float3{i * 2.0f, 0, 0});
    scene->addEntity(instance->getRenderableEntity());
}

// 3. 渲染
renderFrame();

// 4. 测量 GPU 内存
size_t gpuMemory = measureGpuMemory();
```

**验收标准**:
- ✅ GPU 内存 ≤ 1.1x（单个模型的大小）
- ✅ 所有 100 个实例正确显示
- ✅ 帧率 > 30 FPS

### 3.6 场景 5: 动画过渡

**测试模型**: skeleton.glb + mesh.glb + idle.glb + run.glb

**测试步骤**:
```cpp
// 1. 加载资源
SkeletonAsset* skeleton = loader->loadSkeleton("skeleton.glb");
MeshAsset* mesh = loader->loadMesh("mesh.glb");
AnimationAsset* idleAnim = loader->loadAnimation("idle.glb");
AnimationAsset* runAnim = loader->loadAnimation("run.glb");

// 2. 设置
mesh->bindSkeleton(skeleton);
StandaloneAnimator* animator = StandaloneAnimator::create(skeleton);
int idleId = animator->playAnimation(idleAnim, 0, 1.0f, true);

// 3. 3 秒后切换到 run
sleep(3.0f);
int runId = animator->playAnimation(runAnim, 0, 0.0f, true);
animator->crossFade(idleId, runId, 0.5f);

// 4. 录制视频
recordVideo("crossfade_test.mp4", 2.0f);
```

**验收标准**:
- ✅ 过渡平滑（无跳变）
- ✅ 过渡时长正确（0.5 秒）
- ✅ 旧动画停止

### 3.7 场景 6: CUBIC 动画

**测试模型**: cubicAnimation.glb（包含 CUBICSPLINE 插值）

**验收标准**:
- ✅ 动画曲线平滑（与 LINEAR 对比有明显区别）
- ✅ 与 glTF Viewer 渲染一致

### 3.8 场景 7: 综合场景

**测试内容**: 组合所有功能

**场景描述**:
- 10 个玩家角色（MeshInstance）
- 每个角色有纹理 + 法线贴图
- 每个角色播放不同动画（idle, run, attack）
- 支持动画过渡（CrossFade）

**验收标准**:
- ✅ 所有功能正常
- ✅ 帧率 > 30 FPS
- ✅ 无内存泄漏

---

## 4. 性能测试计划

### 4.1 测试指标

| 指标 | 目标 | 测量方法 |
|------|------|---------|
| GPU 内存占用 | 100 个实例 ≤ 1.1x | Xcode Instruments / Android Profiler |
| 实例创建时间 | < 1ms per instance | Benchmark |
| 纹理绑定时间 | < 1ms per texture | Benchmark |
| CrossFade 开销 | < 0.1ms per frame | Profiler |
| CUBIC 插值开销 | < 2x LINEAR | Benchmark |

### 4.2 性能测试用例

**测试 1: MeshInstance 创建性能**

```cpp
TEST(Performance, CreateInstances) {
    MeshAsset* mesh = loadTestMesh();
    mesh->uploadResources();

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 100; i++) {
        mesh->createInstance();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_LT(duration.count(), 100);  // < 100ms for 100 instances (< 1ms each)
}
```

**测试 2: GPU 内存占用**

```cpp
TEST(Performance, GpuMemoryInstancing) {
    // 1. 测量单个实例的 GPU 内存
    MeshAsset* mesh = loadTestMesh();
    mesh->uploadResources();
    size_t baselineMemory = measureGpuMemory();

    // 2. 创建 100 个实例
    for (int i = 0; i < 100; i++) {
        mesh->createInstance();
    }
    size_t instancedMemory = measureGpuMemory();

    // 3. 验证
    size_t overhead = instancedMemory - baselineMemory;
    EXPECT_LT(overhead, baselineMemory * 0.1);  // < 10% overhead
}
```

**测试 3: CrossFade 性能**

```cpp
TEST(Performance, CrossFadeOverhead) {
    StandaloneAnimator* animator = createTestAnimator();
    int anim1 = animator->playAnimation(idleAnim, 0, 1.0f, true);
    int anim2 = animator->playAnimation(runAnim, 0, 0.0f, true);

    // 不使用 CrossFade 的基准
    auto baselineTime = benchmarkAnimatorUpdate(animator, 1000);

    // 使用 CrossFade
    animator->crossFade(anim1, anim2, 1.0f);
    auto crossfadeTime = benchmarkAnimatorUpdate(animator, 1000);

    // 开销应 < 10%
    EXPECT_LT(crossfadeTime, baselineTime * 1.1);
}
```

### 4.3 内存泄漏检测

**工具**:
- macOS: Xcode Instruments (Leaks)
- Linux: Valgrind
- Android: Android Studio Memory Profiler

**测试场景**:
```cpp
TEST(MemoryLeak, CreateAndDestroyInstances) {
    // 创建和销毁 1000 次
    for (int i = 0; i < 1000; i++) {
        MeshAsset* mesh = loadTestMesh();
        mesh->uploadResources();

        MeshInstance* instance = mesh->createInstance();
        mesh->destroyInstance(instance);

        delete mesh;
    }

    // Valgrind 应报告 0 bytes leaked
}
```

---

## 5. 视觉测试计划

### 5.1 测试方法

**对比测试**:
1. 使用 glTF Viewer (https://gltf-viewer.donmccurdy.com/) 查看模型
2. 使用 gltfio_ext 渲染同一模型
3. 截图对比（side-by-side）

**工具**:
- glTF Viewer (Web)
- gltfio (Filament 原生库，作为参考)
- gltfio_ext (待测试)

### 5.2 视觉测试矩阵

| 测试项 | 模型 | 验证内容 | 对比基准 |
|--------|------|---------|---------|
| Base Color 纹理 | BoxTextured | 颜色正确 | glTF Viewer |
| Normal Map | NormalTangentTest | 凹凸细节 | glTF Viewer |
| Metallic Roughness | MetalRoughSpheres | 金属感 | glTF Viewer |
| CUBIC 动画 | CubicAnimation | 曲线平滑 | glTF Viewer |
| CrossFade | 自定义 | 过渡平滑 | 人工验证 |

### 5.3 视觉回归测试

**目的**: 确保修改不破坏现有渲染效果

**方法**:
1. 在修改前渲染参考图像（baseline）
2. 修改后重新渲染
3. 对比图像差异（image diff）

**工具**: ImageMagick (compare 命令)

```bash
compare baseline.png current.png diff.png
# 如果 diff.png 全黑，则无差异
```

---

## 6. 测试资源

### 6.1 测试模型

**来源**: Khronos glTF Sample Models (https://github.com/KhronosGroup/glTF-Sample-Models)

**推荐模型**:

| 模型名称 | 用途 | 大小 |
|---------|------|------|
| BoxTextured | 基础纹理测试 | 10KB |
| NormalTangentTest | Normal/Tangent 测试 | 50KB |
| MetalRoughSpheres | PBR 材质测试 | 100KB |
| CesiumMan | 骨骼动画测试 | 200KB |
| RiggedSimple | 简单蒙皮测试 | 50KB |

**自定义模型**:
- `cubicAnimation.glb` - 包含 CUBICSPLINE 动画
- `separateFiles/` - skeleton.glb + mesh.glb + idle.glb
- `equipment/` - lightArmor.glb + heavyArmor.glb

### 6.2 测试数据组织

```
libs/gltfio_ext/tests/data/
├── models/
│   ├── khronos/              # Khronos Sample Models
│   │   ├── BoxTextured/
│   │   ├── NormalTangentTest/
│   │   └── ...
│   └── custom/               # 自定义测试模型
│       ├── cubicAnimation.glb
│       └── separateFiles/
├── baselines/                # 视觉测试基准图像
│   ├── BoxTextured.png
│   └── ...
└── scripts/
    ├── download_models.sh    # 下载 Khronos 模型
    └── generate_baselines.sh # 生成基准图像
```

---

## 7. CI/CD 集成

### 7.1 持续集成流程

```yaml
# .github/workflows/gltfio_ext_tests.yml

name: gltfio_ext Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest]

    steps:
      - uses: actions/checkout@v2

      - name: Install dependencies
        run: ./build.sh dependencies

      - name: Build
        run: ./build.sh debug

      - name: Run unit tests
        run: ./out/debug/libs/gltfio_ext/test_gltfio_ext

      - name: Run integration tests
        run: ./scripts/run_integration_tests.sh

      - name: Check memory leaks (Linux only)
        if: matrix.os == 'ubuntu-latest'
        run: valgrind --leak-check=full ./out/debug/libs/gltfio_ext/test_gltfio_ext

      - name: Upload test results
        uses: actions/upload-artifact@v2
        with:
          name: test-results
          path: test-results/
```

### 7.2 覆盖率报告

**工具**: lcov (Linux), Xcode Coverage (macOS)

**目标**: 80% 代码覆盖率

```bash
# 生成覆盖率报告
./build.sh debug -c
./out/debug/libs/gltfio_ext/test_gltfio_ext
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage-report
```

---

## 8. 验收标准

### 8.1 Phase 1 (P0) 验收

**必须满足**:
- ✅ 所有单元测试通过（P0-1, P0-2）
- ✅ BoxTextured 显示正确（与 glTF Viewer 一致）
- ✅ NormalTangentTest 显示正确（法线贴图生效）
- ✅ 换装系统正常（场景 3）
- ✅ 无内存泄漏
- ✅ 代码覆盖率 ≥ 75%

### 8.2 Phase 2 (P1) 验收

**必须满足**:
- ✅ 所有单元测试通过（P1-1, P1-2, P1-3）
- ✅ 100 个实例 GPU 内存 ≤ 1.1x（场景 4）
- ✅ CrossFade 平滑过渡（场景 5）
- ✅ CUBIC 动画显示正确（场景 6）
- ✅ 综合场景正常（场景 7）
- ✅ 无性能回退（帧率 ≥ 30 FPS）
- ✅ 代码覆盖率 ≥ 80%

---

## 9. 测试时间表

| 阶段 | 测试内容 | 预计时间 |
|------|---------|---------|
| **Phase 1 - 单元测试** | P0-1, P0-2 单元测试 | 2 天 |
| **Phase 1 - 集成测试** | 场景 1-3 | 1 天 |
| **Phase 1 - 视觉测试** | 纹理 + Normal Map | 0.5 天 |
| **Phase 2 - 单元测试** | P1-1, P1-2, P1-3 单元测试 | 2 天 |
| **Phase 2 - 集成测试** | 场景 4-6 | 1.5 天 |
| **Phase 2 - 性能测试** | GPU 内存 + 帧率 | 1 天 |
| **Phase 2 - 综合测试** | 场景 7 | 0.5 天 |
| **总计** | | **8.5 天** |

---

## 10. 风险和缓解

### 10.1 风险 1: 测试模型不完整

**影响**: 无法覆盖所有场景
**概率**: 中
**缓解**:
- 提前下载 Khronos Sample Models
- 准备自定义测试模型

### 10.2 风险 2: 视觉测试主观性

**影响**: 难以量化验收
**概率**: 高
**缓解**:
- 使用 glTF Viewer 作为权威参考
- 截图对比（image diff）

### 10.3 风险 3: 平台差异

**影响**: macOS 通过，Android 失败
**概率**: 中
**缓解**:
- 优先在 macOS 稳定，再移植 Android
- CI/CD 覆盖多平台

---

## 11. 测试 Checklist

### 11.1 Phase 1 (P0) Checklist

- [ ] P0-1: 纹理加载
  - [ ] 单元测试通过（10+ 用例）
  - [ ] BoxTextured 显示正确
  - [ ] Embedded 纹理支持
  - [ ] 多 primitive 场景支持

- [ ] P0-2: Normal/Tangent
  - [ ] 单元测试通过（6+ 用例）
  - [ ] NormalTangentTest 显示正确
  - [ ] 法线贴图生效
  - [ ] 自动计算 Tangent 支持

- [ ] 集成测试
  - [ ] 场景 1: 简单纹理模型
  - [ ] 场景 2: 多纹理模型
  - [ ] 场景 3: 换装系统

- [ ] 质量保证
  - [ ] 无内存泄漏（Valgrind）
  - [ ] 代码覆盖率 ≥ 75%
  - [ ] 向后兼容

### 11.2 Phase 2 (P1) Checklist

- [ ] P1-1: MeshInstance
  - [ ] 单元测试通过（10+ 用例）
  - [ ] 场景 4 通过（100 个实例）
  - [ ] GPU 内存 ≤ 1.1x

- [ ] P1-2: CrossFade
  - [ ] 单元测试通过（6+ 用例）
  - [ ] 场景 5 通过（动画过渡）
  - [ ] 过渡平滑

- [ ] P1-3: CUBIC 插值
  - [ ] 单元测试通过（6+ 用例）
  - [ ] 场景 6 通过（CUBIC 动画）
  - [ ] 与 glTF Viewer 一致

- [ ] 性能测试
  - [ ] 帧率 ≥ 30 FPS
  - [ ] 无性能回退

- [ ] 质量保证
  - [ ] 代码覆盖率 ≥ 80%
  - [ ] 综合场景通过

---

**下一步**: 根据实施进度，逐步执行测试计划
