# Shader编程基础

GLSL (OpenGL Shading Language) 是GPU编程语言,用于编写顶点和片段着色器。

---

## GLSL语法基础

### 数据类型

**标量类型**:
```glsl
float f = 1.0;
int i = 1;
uint u = 1u;
bool b = true;
```

**向量类型**:
```glsl
vec2 v2 = vec2(1.0, 2.0);
vec3 v3 = vec3(1.0, 2.0, 3.0);
vec4 v4 = vec4(1.0, 2.0, 3.0, 4.0);

ivec3 iv = ivec3(1, 2, 3);  // 整数向量
bvec2 bv = bvec2(true, false);  // 布尔向量
```

**矩阵类型**:
```glsl
mat2 m2 = mat2(1.0);  // 2×2
mat3 m3 = mat3(1.0);  // 3×3
mat4 m4 = mat4(1.0);  // 4×4 (最常用)
```

**采样器类型**:
```glsl
sampler2D tex2D;        // 2D纹理
samplerCube texCube;    // 立方体贴图
sampler2DArray texArray;  // 纹理数组
```

### 向量操作

**Swizzling**(分量重组):
```glsl
vec4 v = vec4(1.0, 2.0, 3.0, 4.0);

vec2 xy = v.xy;  // (1.0, 2.0)
vec3 bgr = v.bgr;  // (3.0, 2.0, 1.0)
vec4 xxxx = v.xxxx;  // (1.0, 1.0, 1.0, 1.0)
```

**构造**:
```glsl
vec3 v3 = vec3(1.0);  // (1.0, 1.0, 1.0)
vec3 v3 = vec3(v2, 1.0);  // (v2.x, v2.y, 1.0)
vec4 v4 = vec4(v3, 1.0);  // (v3.x, v3.y, v3.z, 1.0)
```

---

## Uniform, Attribute, Varying

### Uniform (常量)

所有顶点/片段共享的值:

```glsl
// 顶点着色器
uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

// 片段着色器
uniform vec3 lightColor;
uniform sampler2D albedoMap;
```

**CPU端设置**:
```cpp
// Filament
materialInstance->setParameter("lightColor", LinearColor{1.0f, 1.0f, 1.0f});
materialInstance->setParameter("albedoMap", texture, sampler);
```

### Attribute (顶点属性)

每个顶点不同的值:

```glsl
// 仅在顶点着色器中
in vec3 position;
in vec3 normal;
in vec2 uv;
in vec4 color;
```

### Varying (顶点→片段传递)

```glsl
// 顶点着色器输出
out vec3 vWorldPos;
out vec2 vUV;

// 片段着色器输入(自动插值)
in vec3 vWorldPos;
in vec2 vUV;
```

---

## 内置函数

### 数学函数

```glsl
// 三角函数
sin(x), cos(x), tan(x)
asin(x), acos(x), atan(y, x)

// 指数函数
pow(x, y)      // x^y
exp(x)         // e^x
exp2(x)        // 2^x
log(x), log2(x)
sqrt(x)

// 通用函数
abs(x)         // 绝对值
sign(x)        // 符号 (-1, 0, 1)
floor(x), ceil(x)
fract(x)       // 小数部分
mod(x, y)      // x % y
min(x, y), max(x, y)
clamp(x, minVal, maxVal)
mix(x, y, a)   // x*(1-a) + y*a (线性插值)
step(edge, x)  // x < edge ? 0.0 : 1.0
smoothstep(edge0, edge1, x)  // 平滑插值
```

### 向量函数

```glsl
// 几何函数
length(v)      // 向量长度
distance(p1, p2)  // 两点距离
dot(v1, v2)    // 点积
cross(v1, v2)  // 叉积 (vec3)
normalize(v)   // 归一化
reflect(I, N)  // 反射向量
refract(I, N, eta)  // 折射向量
faceforward(N, I, Nref)  // 面向视线的法线
```

### 纹理采样

```glsl
// 2D纹理
vec4 color = texture(sampler2D, vec2 uv);
vec4 color = textureLod(sampler2D, vec2 uv, float lod);  // 指定mipmap级别

// 立方体贴图
vec4 color = texture(samplerCube, vec3 direction);

// 纹理查询
ivec2 size = textureSize(sampler2D, int lod);  // 纹理尺寸
```

---

## 精度修饰符

### 精度类型

```glsl
highp float f;    // 高精度 (32位)
mediump float f;  // 中精度 (16位)
lowp float f;     // 低精度 (8-10位)
```

### 精度建议(移动端)

```glsl
// 顶点着色器
precision highp float;

in highp vec3 position;      // 位置需要高精度
in mediump vec3 normal;      // 法线中精度足够
in mediump vec2 uv;          // UV中精度

uniform highp mat4 mvpMatrix;  // 矩阵高精度

// 片段着色器
precision mediump float;  // 默认中精度

uniform lowp sampler2D albedoMap;  // 纹理低精度
in mediump vec2 vUV;
out lowp vec4 fragColor;  // 输出颜色低精度
```

**性能影响**:
- `highp`: 慢但精确
- `mediump`: 平衡(推荐)
- `lowp`: 快但可能有精度问题

---

## 常见Shader模式

### 1. 基础纹理映射

```glsl
// 顶点着色器
in vec3 position;
in vec2 uv;
out vec2 vUV;

void main() {
    vUV = uv;
    gl_Position = mvpMatrix * vec4(position, 1.0);
}

// 片段着色器
in vec2 vUV;
uniform sampler2D albedoMap;
out vec4 fragColor;

void main() {
    fragColor = texture(albedoMap, vUV);
}
```

### 2. 法线贴图

```glsl
// 顶点着色器
in vec3 position;
in vec3 normal;
in vec4 tangent;
in vec2 uv;

out mat3 vTBN;
out vec2 vUV;

void main() {
    vec3 N = normalize(normalMatrix * normal);
    vec3 T = normalize(normalMatrix * tangent.xyz);
    vec3 B = cross(N, T) * tangent.w;

    vTBN = mat3(T, B, N);  // 切线空间→世界空间
    vUV = uv;

    gl_Position = mvpMatrix * vec4(position, 1.0);
}

// 片段着色器
in mat3 vTBN;
in vec2 vUV;

uniform sampler2D normalMap;

void main() {
    // 采样法线贴图
    vec3 tangentNormal = texture(normalMap, vUV).xyz * 2.0 - 1.0;

    // 转换到世界空间
    vec3 worldNormal = normalize(vTBN * tangentNormal);

    // 使用worldNormal进行光照计算
}
```

### 3. 视差贴图(Parallax Mapping)

```glsl
vec2 parallaxMapping(vec2 texCoords, vec3 viewDir) {
    float height = texture(heightMap, texCoords).r;
    vec2 p = viewDir.xy / viewDir.z * (height * heightScale);
    return texCoords - p;
}

void main() {
    vec3 viewDir = normalize(vViewDir);
    vec2 texCoords = parallaxMapping(vUV, viewDir);

    vec4 color = texture(albedoMap, texCoords);
}
```

---

## 调试技巧

### 1. 可视化法线

```glsl
fragColor = vec4(normal * 0.5 + 0.5, 1.0);
// 将 [-1,1] 映射到 [0,1] 显示
```

### 2. 可视化UV

```glsl
fragColor = vec4(fract(vUV), 0.0, 1.0);
```

### 3. 检查NaN

```glsl
if (isnan(value) || isinf(value)) {
    fragColor = vec4(1.0, 0.0, 0.0, 1.0);  // 红色警告
    return;
}
```

---

## 性能优化

### 1. 避免分支

```glsl
// ❌ 差
if (useTexture) {
    color = texture(tex, uv);
} else {
    color = vec4(1.0);
}

// ✅ 好
color = mix(vec4(1.0), texture(tex, uv), float(useTexture));
```

### 2. 预计算常量

```glsl
// ❌ 差
const float PI = 3.14159;
float invPI = 1.0 / PI;  // 每次计算

// ✅ 好
const float PI = 3.14159;
const float INV_PI = 0.318309886;  // 预计算
```

### 3. 减少纹理采样

```glsl
// ❌ 差: 3次采样
float r = texture(tex, uv).r;
float g = texture(tex, uv).g;
float b = texture(tex, uv).b;

// ✅ 好: 1次采样
vec3 rgb = texture(tex, uv).rgb;
```

---

## 总结

**Shader编程核心**:
- **数据类型**: 标量、向量、矩阵、采样器
- **变量修饰符**: uniform, attribute, varying
- **内置函数**: 数学、几何、纹理采样
- **精度控制**: highp/mediump/lowp

**下一步**:
- **[02-pbr-theory.md](02-pbr-theory.md)**: 实现PBR材质
- **[06-texture-system.md](06-texture-system.md)**: 纹理采样技巧
- `../material/04-material-definition.md`: Filament材质定义

---

**掌握Shader编程,开启GPU的无限可能!**
