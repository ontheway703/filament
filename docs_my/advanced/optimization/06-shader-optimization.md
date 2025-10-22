# 着色器优化

## 📖 概述

着色器是 GPU 性能的核心。本文档介绍着色器优化技术，从算法到指令级优化。

**优化目标**:
- 减少 GPU 时间 30-60%
- 降低寄存器压力
- 优化内存访问
- 提升并行度

---

## 1. 减少纹理采样

```glsl
// 不好的做法
vec3 color = texture(albedo, uv).rgb;
float roughness = texture(roughnessMap, uv).r;
float metallic = texture(metallicMap, uv).r;

// 好的做法 - 打包到一张纹理
vec4 packed = texture(packedTexture, uv);
vec3 color = packed.rgb;
float roughness = packed.a;
```

---

## 2. 使用精度限定符

```glsl
// 移动设备
precision mediump float;

// 高精度只用于必要的地方
highp vec3 worldPosition;
mediump vec3 normal;
lowp vec4 color;
```

---

## 3. 避免分支

```glsl
// 不好
if (condition) {
    color = vec3(1.0);
} else {
    color = vec3(0.0);
}

// 好
color = vec3(float(condition));
// 或使用 mix
color = mix(vec3(0.0), vec3(1.0), float(condition));
```

---

## 4. 总结

着色器优化关键点:
1. 减少分支和循环
2. 优化纹理采样
3. 使用精度限定符
4. 避免复杂数学运算
5. 使用查找表（LUT）
