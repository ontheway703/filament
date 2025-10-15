# gltfio_ext 实施计划

本目录包含gltfio_ext库的完整实施计划，拆分为6个独立任务。

---

## 任务清单

| 任务 | 文档 | 预计时间 | 状态 |
|------|------|----------|------|
| **任务1** | [task_01_framework.md](task_01_framework.md) | 30分钟 | ⏳ 待执行 |
| **任务2** | [task_02_skeleton.md](task_02_skeleton.md) | 2小时 | ⏳ 待执行 |
| **任务3** | [task_03_mesh.md](task_03_mesh.md) | 2小时 | ⏳ 待执行 |
| **任务4** | [task_04_animation.md](task_04_animation.md) | 3小时 | ⏳ 待执行 |
| **任务5** | [task_05_animator.md](task_05_animator.md) | 3小时 | ⏳ 待执行 |
| **任务6** | [task_06_integration.md](task_06_integration.md) | 2小时 | ⏳ 待执行 |

**总计**: 约12-15小时纯开发时间

---

## 任务依赖关系

```
任务1 (框架) → 必须最先完成
    ↓
    ├─→ 任务2 (骨骼) → 任务5 (动画器)
    ├─→ 任务3 (网格) ─┘
    └─→ 任务4 (动画) → 任务5 (动画器)
                       ↓
                    任务6 (集成测试)
```

**推荐执行顺序**: 1 → 2 → 3 → 4 → 5 → 6

---

## 快速开始

### 执行单个任务

```bash
# 1. 阅读任务文档
cat docs/implement/task_01_framework.md

# 2. 按文档执行（复制粘贴代码，运行命令）

# 3. 验证完成标志
cd out && ninja gltfio_ext
```

### 执行所有任务

```bash
# 依次执行每个任务
for i in 01 02 03 04 05 06; do
    echo "=== Task $i ==="
    cat docs/implement/task_${i}_*.md
    # 手动执行文档中的步骤
done
```

### 最终验证

```bash
# 构建并运行所有测试
cd out
ninja gltfio_ext test_gltfio_ext
../libs/gltfio_ext/run_tests.sh
```

---

## 任务概览

### 任务1: 框架搭建
- 创建目录结构
- 配置CMakeLists.txt
- 定义所有公开API头文件
- 创建错误处理宏

### 任务2: SkeletonAsset实现
- 从cgltf_skin加载骨骼层次
- 提取逆绑定矩阵
- 建立骨骼名称索引
- 实现查询接口

### 任务3: MeshAsset实现
- 加载网格几何数据
- 验证BONE_INDICES/BONE_WEIGHTS
- 绑定骨骼
- 更新蒙皮矩阵

### 任务4: AnimationAsset + AnimationPack
- 从glTF加载动画数据
- 使用名称延迟绑定
- 批量加载动画包
- 实现查询接口

### 任务5: StandaloneAnimator
- 绑定SkeletonAsset
- 播放多个动画（支持权重）
- 名称到索引映射缓存
- 更新骨骼变换并计算矩阵

### 任务6: 集成测试
- 端到端测试程序
- 多网格共享骨骼演示
- README文档
- 测试脚本

---

## 关键技术决策

详见 [../plan/separate_loading_design.md](../../plan/separate_loading_design.md)

| 决策项 | 选择 |
|--------|------|
| 依赖关系 | 链接gltfio_core（不含filamat） |
| 测试资产 | 使用AnimatedMorphCube.glb模拟分离加载 |
| AnimationBlender | 暂不实现（非核心功能） |
| 错误处理 | utils::slog + nullptr返回值 |
| 性能优化 | 名称延迟绑定 + 索引缓存 |
| 向后兼容 | 独立新API，不兼容原FilamentAsset |

---

## 注意事项

1. **每个任务独立完成**：每个任务都有完整的代码和验证步骤
2. **按顺序执行**：后续任务依赖前面的任务
3. **验证后再继续**：确保每个任务的"完成标志"都满足
4. **token管理**：每个任务单独执行，避免一次性内容过多

---

## 相关文档

- [设计文档](../../plan/separate_loading_design.md) - 完整技术设计
- [使用示例](../../plan/usage_examples.md) - API使用示例
- [API对比](../../plan/api_comparison.md) - 与原gltfio的对比

---

**准备好开始了吗？从任务1开始执行！**

```bash
cat docs/implement/task_01_framework.md
```
