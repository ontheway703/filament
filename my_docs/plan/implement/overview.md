# gltfio_ext 改进实施计划 - 总览

**制定日期**: 2025-10-16
**基于文档**: my_docs/plan/gltfio_ext/architecture-review-v2.md
**项目目标**: 修复 gltfio_ext 的关键问题，提升功能完整性和性能

---

## 1. 执行摘要

### 1.1 当前状态

**优势**：
- ✅ 模块化加载架构优秀（Skeleton/Mesh/Animation 分离）
- ✅ 换装系统支持良好
- ✅ 动画系统实现正确（TRS 分解/组合）
- ✅ 分帧上传设计优秀

**关键问题**：
- 🔴 纹理加载机制缺失（严重影响可用性）
- 🔴 Normal/Tangent 处理不完整（法线贴图无法显示）
- 🟡 实例化场景缺少缓存（GPU 内存浪费）
- 🟡 缺少高级动画功能（CrossFade/CUBIC 插值）

### 1.2 改进目标

**短期目标（2-3 周）**：
- 修复 P0 问题，使 gltfio_ext 达到可用状态
- 纹理可以正确显示
- 法线贴图生效

**中期目标（4-6 周）**：
- 修复 P1 问题，提升性能和用户体验
- 实例化场景优化（MeshInstance）
- 动画过渡自然（CrossFade）

---

## 2. 问题优先级

| 优先级 | 任务 | 影响范围 | 工作量 | 预计时间 |
|--------|------|---------|--------|---------|
| 🔴 **P0-1** | 纹理加载机制 | 所有场景 | 中 | 3-5 天 |
| 🔴 **P0-2** | Normal/Tangent 修复 | 所有场景 | 小 | 1-2 天 |
| 🟡 **P1-1** | MeshInstance 实现 | 实例化场景 | 中 | 4-6 天 |
| 🟡 **P1-2** | CrossFade 实现 | 所有场景 | 小 | 2-3 天 |
| 🟡 **P1-3** | CUBIC 插值 | 所有场景 | 小 | 1-2 天 |

---

## 3. 阶段划分

### 阶段 1: 基础可用（P0，7-10 个工作日）

**目标**: 与 gltfio 功能保持一致

**任务**:
1. **P0-1: 纹理加载机制**
   - 方案 B: 在 MeshAsset 中添加纹理绑定 API
   - 提供 `getRequiredTextures()` 和 `bindTexture()` API
   - 用户可以手动加载和绑定纹理

2. **P0-2: Normal/Tangent 修复**
   - 实现完整的 TBN quaternion 计算
   - 参考 gltfio/src/AssetLoader.cpp:800-850
   - 支持 tangent 属性或自动计算

**验收标准**:
- ✅ mesh.glb 的纹理可以正确显示（用户手动绑定）
- ✅ 法线贴图生效
- ✅ 测试用例通过

**风险**:
- 低：参考实现清晰，技术成熟

---

### 阶段 2: 性能优化（P1，7-11 个工作日）

**目标**: 优化实例化场景，提升动画质量

**任务**:
1. **P1-1: MeshInstance 实现**
   - 与 Filament 的 FilamentAsset + FilamentInstance 模式对齐
   - MeshAsset 拥有 VertexBuffer/IndexBuffer（共享 GPU 资源）
   - MeshInstance 拥有独立 Entity 和 MaterialInstance
   - 支持 `MeshAsset::createInstance()` API

2. **P1-2: CrossFade 实现**
   - 添加 `StandaloneAnimator::crossFade(fromId, toId, duration)`
   - 自动管理权重渐变
   - 过渡完成后自动停止旧动画

3. **P1-3: CUBIC 插值**
   - 实现 Hermite spline 插值
   - 处理 CUBIC 格式的关键帧数据（in-tangent + value + out-tangent）
   - 支持所有通道类型（T/R/S）

**验收标准**:
- ✅ 实例化场景 GPU 内存占用合理（100 个实例 ≈ 1 倍内存）
- ✅ 动画过渡自然（CrossFade 测试）
- ✅ CUBIC 动画平滑（与 glTF Viewer 对比）

**风险**:
- 中：MeshInstance 架构调整较大，需要仔细测试

---

## 4. 技术方案概览

### 4.1 P0-1: 纹理加载机制

**选择方案**: 方案 B（MeshAsset 纹理绑定 API）

**理由**:
- 简单直接，用户可控
- 不依赖 ResourceLoader（避免 API 不兼容）
- 灵活性高（用户可以自己实现纹理加载策略）

**核心 API**:
```cpp
// MeshAsset.h
struct TextureInfo {
    size_t primitiveIndex;
    const char* slot;        // "baseColorMap", "normalMap", etc.
    const char* uri;         // 纹理 URI（如果有）
    const uint8_t* data;     // Embedded 数据（如果有）
    size_t dataSize;
    const char* mimeType;
};

class MeshAsset {
    // 获取需要的纹理列表
    std::vector<TextureInfo> getRequiredTextures() const;

    // 绑定纹理到指定 primitive 的指定 slot
    void bindTexture(size_t primitiveIndex, const char* slot, Texture* texture,
                     const TextureSampler& sampler);
};
```

**用户代码**:
```cpp
MeshAsset* mesh = loader->loadMesh(meshData, meshSize);
auto textures = mesh->getRequiredTextures();

for (const auto& texInfo : textures) {
    Texture* tex = myLoadTexture(texInfo.uri, texInfo.data, texInfo.dataSize, texInfo.mimeType);
    TextureSampler sampler = createSampler(texInfo.samplerParams);
    mesh->bindTexture(texInfo.primitiveIndex, texInfo.slot, tex, sampler);
}

mesh->uploadResources();
```

**详细设计**: 见 `phase1-texture-loading.md`

---

### 4.2 P0-2: Normal/Tangent 修复

**问题**: 当前只存储 normal，缺少 tangent/bitangent

**解决方案**:
1. 读取 glTF 的 TANGENT 属性（如果有）
2. 如果没有，从 normal 和 UV 计算 tangent
3. 计算 bitangent = cross(normal, tangent) * tangentW
4. 构建 TBN 矩阵并转换为 quaternion
5. 打包 quaternion 到 TANGENTS 属性（vec4）

**实现参考**: gltfio/src/AssetLoader.cpp:800-850

**详细设计**: 见 `phase1-tangent-fix.md`

---

### 4.3 P1-1: MeshInstance 实现

**设计模式**: 与 FilamentAsset + FilamentInstance 对齐

**架构**:
```
MeshAsset (shared)
├─ VertexBuffer* (共享 GPU 资源)
├─ IndexBuffer* (共享 GPU 资源)
├─ std::vector<MeshInstance*> mInstances
└─ createInstance() → MeshInstance*

MeshInstance (independent)
├─ Entity mRenderableEntity (独立)
├─ RenderableManager::Instance
├─ std::vector<MaterialInstance*> (独立，clone 自 MeshAsset)
├─ SkeletonAsset* mBoundSkeleton
└─ updateSkinning(animator)
```

**核心 API**:
```cpp
class MeshAsset {
    // 创建实例（共享 VertexBuffer/IndexBuffer）
    MeshInstance* createInstance();

    // 销毁实例
    void destroyInstance(MeshInstance* instance);

    // 获取所有实例
    MeshInstance** getInstances();
    size_t getInstanceCount() const;
};

class MeshInstance {
    // 获取所属的 MeshAsset
    MeshAsset const* getAsset() const;

    // 绑定骨骼
    bool bindSkeleton(SkeletonAsset* skeleton);
    SkeletonAsset* getBoundSkeleton() const;

    // 更新蒙皮
    void updateSkinning(const StandaloneAnimator* animator);

    // 获取 Entity
    Entity getRenderableEntity() const;
    RenderableManager::Instance getRenderableInstance() const;

    // 包围盒
    Aabb getBoundingBox() const;

    // 材质变体（可选）
    MaterialInstance* getMaterialInstance(size_t primitiveIndex);
};
```

**用户代码**:
```cpp
// 加载一次
MeshAsset* mesh = loader->loadMesh(enemyData, enemySize);
mesh->uploadResources();

// 创建 100 个实例
for (int i = 0; i < 100; i++) {
    MeshInstance* instance = mesh->createInstance();  // 共享 GPU 资源
    instance->bindSkeleton(skeleton);
    scene->addEntity(instance->getRenderableEntity());
}
```

**详细设计**: 见 `phase2-mesh-instance.md`

---

### 4.4 P1-2/P1-3: 动画增强

**CrossFade**:
```cpp
// StandaloneAnimator.h
class StandaloneAnimator {
    // 交叉淡入淡出
    void crossFade(int fromAnimId, int toAnimId, float duration);

private:
    struct CrossFadeState {
        int fromAnimId, toAnimId;
        float duration, elapsed;
    };
    CrossFadeState mCrossFade;
};
```

**CUBIC 插值**:
```cpp
// StandaloneAnimator.cpp
float3 cubicInterpolate(float t,
                        const float3& inTangent,
                        const float3& p0,
                        const float3& outTangent0,
                        const float3& inTangent1,
                        const float3& p1);
```

**详细设计**: 见 `phase2-animation-enhance.md`

---

## 5. 风险评估

### 5.1 技术风险

| 风险 | 等级 | 影响 | 缓解措施 |
|------|------|------|---------|
| MeshInstance 架构调整破坏现有功能 | 中 | 高 | 充分测试，保持向后兼容 |
| 纹理绑定 API 使用复杂 | 低 | 中 | 提供示例代码和文档 |
| TBN quaternion 计算错误 | 低 | 中 | 参考 gltfio 原生实现，单元测试 |

### 5.2 时间风险

| 风险 | 等级 | 影响 | 缓解措施 |
|------|------|------|---------|
| 测试覆盖不足导致返工 | 中 | 中 | 每个阶段都有详细测试计划 |
| 边界情况处理遗漏 | 中 | 中 | Code review 和边界测试 |

---

## 6. 依赖和资源

### 6.1 外部依赖

| 依赖项 | 用途 | 状态 |
|--------|------|------|
| gltfio 原生库 | 参考实现（TBN、纹理） | ✅ 可用 |
| Filament Engine | 核心渲染 | ✅ 可用 |
| cgltf | glTF 解析 | ✅ 已集成 |

### 6.2 开发资源

- 开发人员：1 人
- 预计总时间：14-21 个工作日（3-4 周）
- 测试环境：macOS, Android, iOS（可选）

---

## 7. 测试策略

### 7.1 单元测试

**覆盖范围**：
- 纹理绑定 API
- TBN quaternion 计算
- MeshInstance 创建/销毁
- CrossFade 权重计算
- CUBIC 插值计算

**测试框架**：Google Test

### 7.2 集成测试

**测试场景**：
1. 场景 1: 分离文件加载 + 纹理绑定
2. 场景 2: 换装系统
3. 场景 3: 实例化（100 个实例）
4. 场景 4: 动画过渡（CrossFade）

**测试资源**：
- Khronos glTF Sample Models
- 自定义测试模型（带纹理、法线贴图、CUBIC 动画）

### 7.3 性能测试

**指标**：
- GPU 内存占用（实例化场景）
- 加载时间（纹理绑定）
- 动画帧率（CrossFade + CUBIC）

**详细测试计划**: 见 `testing-plan.md`

---

## 8. 里程碑

| 里程碑 | 完成标准 | 预计时间 |
|--------|---------|---------|
| **M1: P0 完成** | 纹理可显示，法线贴图生效 | 第 2 周结束 |
| **M2: P1 完成** | MeshInstance 可用，动画增强完成 | 第 4 周结束 |
| **M3: 测试完成** | 所有测试通过，文档完善 | 第 5 周结束 |

---

## 9. 后续计划（P2，可选）

**未来扩展**：
- 支持 Draco 网格压缩
- 支持 Morph Targets
- 支持 KTX2 纹理压缩
- 自动纹理加载（集成 TextureProvider）

**优先级**：低（在 P0 和 P1 完成后再考虑）

---

## 10. 参考文档

- **架构分析**: `my_docs/plan/gltfio_ext/architecture-review-v2.md`
- **详细设计文档**:
  - `phase1-texture-loading.md`
  - `phase1-tangent-fix.md`
  - `phase2-mesh-instance.md`
  - `phase2-animation-enhance.md`
- **测试计划**: `testing-plan.md`
- **gltfio 原生源码**: `libs/gltfio/src/`

---

**文档状态**: 初稿
**下一步**: 制定各阶段的详细设计文档
