# 优化案例分析

## 📖 概述

通过真实案例学习性能优化。本文档分析多个优化案例及其效果。

**优化目标**:
- 学习实战经验
- 避免常见陷阱
- 掌握分析方法
- 量化优化效果

---

## 案例 1: 移动游戏优化

**问题**: FPS 从 60 掉到 30

**分析**:
```
1. 使用 Profiler 发现瓶颈在 Fragment Shader
2. 纹理采样过多（5 张纹理）
3. 复杂的光照计算
```

**优化方案**:
```
1. 合并纹理到 Atlas（5 → 2）
2. 简化光照模型
3. 使用 LOD 系统
```

**结果**:
- FPS: 30 → 58
- GPU Time: 33ms → 17ms
- 内存: 512MB → 320MB

---

## 案例 2: Draw Call 优化

**问题**: 2000+ Draw Calls，CPU Bound

**优化方案**:
```cpp
// 使用 Static Batching
StaticBatcher batcher(engine);

for (auto& mesh : staticMeshes) {
    batcher.addMesh(mesh);
}

auto batched = batcher.batch();
// Draw Call: 2000 → 150
```

**结果**:
- Draw Call: 2000 → 150
- CPU Time: 25ms → 8ms
- FPS: 40 → 60

---

## 案例 3: 内存优化

**问题**: 纹理内存占用 1.2GB

**优化方案**:
```
1. 使用 ASTC 4x4 压缩
2. 降低部分纹理分辨率
3. 实现流式加载
```

**结果**:
- 内存: 1.2GB → 350MB
- 加载时间: 8s → 2s
- 未降低视觉质量

---

## 总结

优化流程:
1. **测量基准** - 使用 Profiler
2. **识别瓶颈** - CPU/GPU/Memory
3. **针对性优化** - 选择合适技术
4. **验证效果** - 量化改进
5. **持续监控** - 防止性能回归
