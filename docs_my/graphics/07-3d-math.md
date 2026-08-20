# 3D数学基础

详细讲解3D图形编程中必需的数学知识,包括向量、矩阵、四元数和坐标变换。

---

## 目录

1. [向量运算](#向量运算)
2. [矩阵变换](#矩阵变换)
3. [四元数](#四元数)
4. [坐标空间](#坐标空间)
5. [投影变换](#投影变换)
6. [实用技巧](#实用技巧)

---

## 向量运算

### 向量基础

**向量**: 具有大小和方向的量。

```
2D向量: v = (x, y)
3D向量: v = (x, y, z)
4D向量: v = (x, y, z, w)  // 齐次坐标
```

### 向量长度(模)

```
|v| = √(x² + y² + z²)
```

```glsl
float length(vec3 v) {
    return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// 内置函数
float len = length(v);
```

**单位向量(归一化)**:
```
v̂ = v / |v|

|v̂| = 1
```

```glsl
vec3 normalize(vec3 v) {
    float len = length(v);
    return v / len;  // 或 v * (1.0 / len)
}

// 内置函数
vec3 normalized = normalize(v);
```

### 向量加法和减法

```
加法: v + w = (vx+wx, vy+wy, vz+wz)
减法: v - w = (vx-wx, vy-wy, vz-wz)
```

**几何意义**:
```
加法(平移):       减法(方向):
   w                 B
   ↗                 •
  ↗                 ↗
 v    结果: v+w    ↗ B-A
↗     ↗          ↗
    ↗           •
                A
```

```glsl
vec3 sum = v + w;
vec3 diff = v - w;
```

### 标量乘法

```
k × v = (k×vx, k×vy, k×vz)
```

**几何意义**: 缩放向量长度,k<0时反向。

```glsl
vec3 scaled = v * 2.0;  // 长度翻倍
vec3 half = v * 0.5;    // 长度减半
vec3 reversed = v * -1.0;  // 反向
```

### 点积(Dot Product)

```
v · w = vx×wx + vy×wy + vz×wz
     = |v| × |w| × cos(θ)

θ: v和w之间的夹角
```

**几何意义**:
```
v · w > 0: 夹角 < 90° (同向)
v · w = 0: 夹角 = 90° (垂直)
v · w < 0: 夹角 > 90° (反向)
```

```glsl
float dot(vec3 v, vec3 w) {
    return v.x * w.x + v.y * w.y + v.z * w.z;
}

// 内置函数
float d = dot(v, w);
```

**应用**:

**1. 计算夹角**:
```glsl
float cosTheta = dot(normalize(v), normalize(w));
float theta = acos(cosTheta);  // 弧度
```

**2. 投影**:
```glsl
// v在w上的投影长度
float projLength = dot(v, normalize(w));

// v在w上的投影向量
vec3 proj = dot(v, w) / dot(w, w) * w;
```

**3. 光照计算**:
```glsl
// Lambertian漫反射
float NoL = max(0.0, dot(normal, lightDir));
vec3 diffuse = albedo * lightColor * NoL;
```

### 叉积(Cross Product)

**仅3D向量有叉积**:

```
v × w = (vy×wz - vz×wy,
         vz×wx - vx×wz,
         vx×wy - vy×wx)
```

**几何意义**:
```
结果向量垂直于v和w构成的平面
大小 = |v| × |w| × sin(θ) = 平行四边形面积
方向: 右手法则
```

```
      v × w (向上)
        ↑
        │
    v   │
    ↗   │
   ↗    │
  ↗─────┘
        w
```

```glsl
vec3 cross(vec3 v, vec3 w) {
    return vec3(
        v.y * w.z - v.z * w.y,
        v.z * w.x - v.x * w.z,
        v.x * w.y - v.y * w.x
    );
}

// 内置函数
vec3 c = cross(v, w);
```

**应用**:

**1. 计算法线**:
```glsl
// 三角形ABC的法线
vec3 AB = B - A;
vec3 AC = C - A;
vec3 normal = normalize(cross(AB, AC));
```

**2. 构建坐标系**:
```glsl
// 给定法线N,构建切线T和副切线B
vec3 N = normalize(normal);
vec3 up = abs(N.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0);
vec3 T = normalize(cross(up, N));
vec3 B = cross(N, T);
// 现在有了正交坐标系: T, B, N
```

**3. 判断方向**:
```glsl
// 判断点P在AB的左侧还是右侧(2D)
vec2 AB = B - A;
vec2 AP = P - A;
float cross2D = AB.x * AP.y - AB.y * AP.x;
// cross2D > 0: 左侧
// cross2D < 0: 右侧
```

### 向量反射

```
r = v - 2(v·n)n

v: 入射向量
n: 法线(单位向量)
r: 反射向量
```

```
      n
      ↑
      │
  v ↗ │ ↖ r
   ↗  │  ↖
  ↗   │   ↖
─────────────
    表面
```

```glsl
vec3 reflect(vec3 v, vec3 n) {
    return v - 2.0 * dot(v, n) * n;
}

// 内置函数
vec3 r = reflect(v, n);
```

### 向量折射

**Snell定律**:
```
η₁ sin(θ₁) = η₂ sin(θ₂)

η: 折射率
θ: 入射/折射角
```

```glsl
vec3 refract(vec3 I, vec3 N, float eta) {
    float cosI = dot(N, I);
    float k = 1.0 - eta * eta * (1.0 - cosI * cosI);
    if (k < 0.0)
        return vec3(0.0);  // 全反射
    else
        return eta * I - (eta * cosI + sqrt(k)) * N;
}

// 内置函数
vec3 t = refract(I, N, eta);

// eta = η₁/η₂
// 空气→水: eta = 1.0/1.33 ≈ 0.75
// 水→空气: eta = 1.33/1.0 = 1.33
```

---

## 矩阵变换

### 矩阵基础

**3×3矩阵**:
```
[ m00  m01  m02 ]
[ m10  m11  m12 ]
[ m20  m21  m22 ]
```

**4×4矩阵** (图形学常用):
```
[ m00  m01  m02  m03 ]
[ m10  m11  m12  m13 ]
[ m20  m21  m22  m23 ]
[ m30  m31  m32  m33 ]
```

### 矩阵乘法

**矩阵×向量**:
```
     [ m00  m01  m02  m03 ]   [ x ]
M×v =[ m10  m11  m12  m13 ] × [ y ]
     [ m20  m21  m22  m23 ]   [ z ]
     [ m30  m31  m32  m33 ]   [ w ]

结果[i] = m[i][0]*x + m[i][1]*y + m[i][2]*z + m[i][3]*w
```

**矩阵×矩阵**:
```
C = A × B

C[i][j] = Σ A[i][k] × B[k][j]
          k
```

**注意**: 矩阵乘法**不可交换**:
```
A × B ≠ B × A
```

### 单位矩阵

```
I = [ 1  0  0  0 ]
    [ 0  1  0  0 ]
    [ 0  0  1  0 ]
    [ 0  0  0  1 ]

I × M = M × I = M
I × v = v
```

```glsl
mat4 identity = mat4(1.0);
```

### 平移矩阵

```
T(tx, ty, tz) = [ 1  0  0  tx ]
                [ 0  1  0  ty ]
                [ 0  0  1  tz ]
                [ 0  0  0   1 ]
```

**效果**:
```
T × (x,y,z,1) = (x+tx, y+ty, z+tz, 1)
```

```glsl
mat4 translate(vec3 t) {
    return mat4(
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        t.x, t.y, t.z, 1.0
    );
}

// Filament
mat4f T = mat4f::translation(float3{tx, ty, tz});
```

### 缩放矩阵

```
S(sx, sy, sz) = [ sx  0   0   0 ]
                [  0 sy   0   0 ]
                [  0  0  sz   0 ]
                [  0  0   0   1 ]
```

**效果**:
```
S × (x,y,z,1) = (sx×x, sy×y, sz×z, 1)
```

```glsl
mat4 scale(vec3 s) {
    return mat4(
        s.x, 0.0, 0.0, 0.0,
        0.0, s.y, 0.0, 0.0,
        0.0, 0.0, s.z, 0.0,
        0.0, 0.0, 0.0, 1.0
    );
}

// Filament
mat4f S = mat4f::scaling(float3{sx, sy, sz});
```

### 旋转矩阵

#### 绕X轴旋转

```
Rx(θ) = [ 1    0       0    0 ]
        [ 0  cos(θ) -sin(θ) 0 ]
        [ 0  sin(θ)  cos(θ) 0 ]
        [ 0    0       0    1 ]
```

```glsl
mat4 rotateX(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return mat4(
        1.0, 0.0, 0.0, 0.0,
        0.0,   c,  -s, 0.0,
        0.0,   s,   c, 0.0,
        0.0, 0.0, 0.0, 1.0
    );
}
```

#### 绕Y轴旋转

```
Ry(θ) = [  cos(θ)  0  sin(θ)  0 ]
        [    0     1    0     0 ]
        [ -sin(θ)  0  cos(θ)  0 ]
        [    0     0    0     1 ]
```

#### 绕Z轴旋转

```
Rz(θ) = [ cos(θ) -sin(θ)  0  0 ]
        [ sin(θ)  cos(θ)  0  0 ]
        [   0       0     1  0 ]
        [   0       0     0  1 ]
```

#### 绕任意轴旋转(Rodrigues公式)

```
R(axis, θ) = I + sin(θ)K + (1-cos(θ))K²

axis = (x,y,z): 归一化旋转轴
K: 叉积矩阵
```

```glsl
mat4 rotate(vec3 axis, float angle) {
    axis = normalize(axis);
    float s = sin(angle);
    float c = cos(angle);
    float oc = 1.0 - c;
    
    return mat4(
        oc*axis.x*axis.x + c,           oc*axis.x*axis.y - axis.z*s,  oc*axis.z*axis.x + axis.y*s,  0.0,
        oc*axis.x*axis.y + axis.z*s,    oc*axis.y*axis.y + c,          oc*axis.y*axis.z - axis.x*s,  0.0,
        oc*axis.z*axis.x - axis.y*s,    oc*axis.y*axis.z + axis.x*s,  oc*axis.z*axis.z + c,          0.0,
        0.0,                            0.0,                          0.0,                          1.0
    );
}

// Filament
mat4f R = mat4f::rotation(angle, float3{x, y, z});
```

### 变换组合

**顺序很重要**:

```
先缩放,后旋转,最后平移(TRS):

M = T × R × S

不同顺序产生不同结果:
T×R×S ≠ R×T×S ≠ S×R×T
```

**例子**:
```
点(1,0,0)

1. 先平移(2,0,0),后旋转90°(绕Z):
   T×R×(1,0,0) = T×(0,1,0) = (2,1,0)

2. 先旋转90°,后平移(2,0,0):
   R×T×(1,0,0) = R×(3,0,0) = (0,3,0)

结果不同!
```

```glsl
// 标准TRS变换
mat4 modelMatrix = translation * rotation * scale;

// Filament
mat4f M = mat4f::translation(t) * mat4f::rotation(angle, axis) * mat4f::scaling(s);
```

### 逆矩阵

```
M × M⁻¹ = I

用途: 反向变换
```

**特殊矩阵的逆**:

```
平移: T⁻¹ = T(-tx, -ty, -tz)
缩放: S⁻¹ = S(1/sx, 1/sy, 1/sz)
旋转: R⁻¹ = Rᵀ (转置,因为旋转矩阵是正交矩阵)
```

```glsl
// GLSL
mat4 inv = inverse(M);

// Filament
mat4f inv = inverse(M);
```

### 转置矩阵

```
Mᵀ[i][j] = M[j][i]

交换行列
```

**法线变换**:
```glsl
// 位置变换: P' = M × P
// 法线变换: N' = (M⁻¹)ᵀ × N

mat3 normalMatrix = transpose(inverse(mat3(modelMatrix)));
vec3 worldNormal = normalize(normalMatrix * normal);
```

---

## 四元数

### 为什么需要四元数

**欧拉角的问题**:

**1. 万向节锁(Gimbal Lock)**:
```
当pitch=±90°时,失去一个自由度
导致旋转轴重合
```

**2. 顺序依赖**:
```
Yaw-Pitch-Roll ≠ Roll-Pitch-Yaw
```

**3. 插值不平滑**:
```
(0°,0°,0°) → (0°,0°,360°)
插值经过0°-360°,产生旋转
实际应该静止
```

**四元数的优势**:
- ✅ 无万向节锁
- ✅ 插值平滑(Slerp)
- ✅ 紧凑(4个float vs 9个float的3×3矩阵)
- ✅ 数值稳定

### 四元数定义

```
q = w + xi + yj + zk = (w, x, y, z)

i² = j² = k² = ijk = -1
```

**单位四元数**:
```
|q| = √(w² + x² + y² + z²) = 1
```

### 四元数与旋转

**绕轴axis旋转θ角**:
```
q = (cos(θ/2), sin(θ/2)×axis)

  = (cos(θ/2), sin(θ/2)×ax, sin(θ/2)×ay, sin(θ/2)×az)
```

**例子**:
```
绕Z轴旋转90°:
axis = (0,0,1)
θ = π/2

q = (cos(π/4), 0, 0, sin(π/4))
  = (0.707, 0, 0, 0.707)
```

### 四元数运算

**乘法**:
```
q₁ × q₂ = (w₁w₂ - v₁·v₂, w₁v₂ + w₂v₁ + v₁×v₂)

v = (x,y,z): 向量部分
w: 标量部分
```

**共轭**:
```
q* = (w, -x, -y, -z)
```

**逆**:
```
q⁻¹ = q* / |q|²

单位四元数: q⁻¹ = q*
```

**旋转向量**:
```
v' = q × v × q*

v = (0, vx, vy, vz): 纯四元数形式的向量
```

### 四元数插值

**线性插值(Lerp)** - 不推荐:
```glsl
quat lerp(quat q1, quat q2, float t) {
    return normalize(q1 * (1.0 - t) + q2 * t);
}
```

**球面线性插值(Slerp)** - 推荐:
```glsl
quat slerp(quat q1, quat q2, float t) {
    float cosHalfTheta = dot(q1, q2);
    
    // 处理反向
    if (cosHalfTheta < 0.0) {
        q2 = -q2;
        cosHalfTheta = -cosHalfTheta;
    }
    
    // 接近时使用lerp
    if (abs(cosHalfTheta) >= 1.0) {
        return q1;
    }
    
    float halfTheta = acos(cosHalfTheta);
    float sinHalfTheta = sqrt(1.0 - cosHalfTheta*cosHalfTheta);
    
    if (abs(sinHalfTheta) < 0.001) {
        return (q1 + q2) * 0.5;
    }
    
    float ratioA = sin((1.0 - t) * halfTheta) / sinHalfTheta;
    float ratioB = sin(t * halfTheta) / sinHalfTheta;
    
    return q1 * ratioA + q2 * ratioB;
}
```

### 四元数转矩阵

```glsl
mat4 quatToMat4(vec4 q) {
    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    return mat4(
        1.0 - 2.0 * (yy + zz), 2.0 * (xy - wz),       2.0 * (xz + wy),       0.0,
        2.0 * (xy + wz),       1.0 - 2.0 * (xx + zz), 2.0 * (yz - wx),       0.0,
        2.0 * (xz - wy),       2.0 * (yz + wx),       1.0 - 2.0 * (xx + yy), 0.0,
        0.0,                   0.0,                   0.0,                   1.0
    );
}
```

### Filament中使用四元数

```cpp
#include <math/quat.h>

using namespace filament::math;

// 创建四元数(绕Y轴旋转45°)
quatf q = quatf::fromAxisAngle(float3{0, 1, 0}, M_PI / 4.0f);

// 四元数插值
quatf q1 = quatf::fromAxisAngle(float3{0,1,0}, 0.0f);
quatf q2 = quatf::fromAxisAngle(float3{0,1,0}, M_PI);
quatf interpolated = slerp(q1, q2, 0.5f);  // 中间状态

// 转为矩阵
mat4f rotationMatrix = mat4f(interpolated);

// 应用到TransformManager
auto& tcm = engine->getTransformManager();
tcm.setTransform(instance, rotationMatrix);
```

---

## 坐标空间

### 坐标空间层级

```
局部/模型空间 (Local/Model Space)
    ↓ 模型矩阵(Model Matrix)
世界空间 (World Space)
    ↓ 视图矩阵(View Matrix)
观察/相机空间 (View/Camera Space)
    ↓ 投影矩阵(Projection Matrix)
裁剪空间 (Clip Space)
    ↓ 透视除法(Perspective Division: w除法)
归一化设备坐标 (NDC, Normalized Device Coordinates)
    ↓ 视口变换(Viewport Transform)
屏幕空间 (Screen Space)
```

### 1. 局部空间

**定义**: 模型自身的坐标系,原点在模型中心。

```
立方体顶点(局部空间):
(-1,-1,-1), (1,-1,-1), (1,1,-1), ...

好处: 建模方便,可复用
```

### 2. 世界空间

**定义**: 场景的全局坐标系。

**模型矩阵变换**:
```
P_world = M × P_local

M = T × R × S (平移、旋转、缩放)
```

```glsl
// 顶点着色器
vec4 worldPos = modelMatrix * vec4(position, 1.0);
```

### 3. 观察空间

**定义**: 以相机为原点的坐标系。

**视图矩阵变换**:
```
P_view = V × P_world

V = LookAt(eye, center, up)
```

**LookAt矩阵构建**:
```
forward = normalize(center - eye)
right = normalize(cross(forward, up))
up' = cross(right, forward)

V = [ right.x   right.y   right.z   -dot(right, eye)   ]
    [ up'.x     up'.y     up'.z     -dot(up', eye)     ]
    [ -fwd.x    -fwd.y    -fwd.z    dot(forward, eye)  ]
    [ 0         0         0         1                  ]
```

```glsl
vec4 viewPos = viewMatrix * worldPos;
```

### 4. 裁剪空间

**投影矩阵变换**:
```
P_clip = P × P_view

P: 投影矩阵(透视或正交)
```

```glsl
gl_Position = projectionMatrix * viewPos;
```

### 5. NDC空间

**透视除法**:
```
P_ndc = P_clip / P_clip.w

范围: [-1,1] × [-1,1] × [-1,1] (OpenGL)
     或 [-1,1] × [-1,1] × [0,1] (DirectX/Vulkan)
```

GPU自动执行。

### 6. 屏幕空间

**视口变换**:
```
x_screen = (x_ndc + 1) × width / 2
y_screen = (y_ndc + 1) × height / 2

范围: [0, width] × [0, height]
```

GPU自动执行。

### 法线变换

**法线需要特殊变换矩阵**:

```
位置: P' = M × P
法线: N' = (M⁻¹)ᵀ × N
```

**为什么?**
```
非均匀缩放会改变法线方向:

缩放(2,1,1):
  │     ╱
  │    ╱ 法线变化
  │   ╱
  └──┘

使用(M⁻¹)ᵀ保持垂直性
```

```glsl
// 顶点着色器
mat3 normalMatrix = transpose(inverse(mat3(modelMatrix)));
vec3 worldNormal = normalize(normalMatrix * normal);
```

**优化**: 如果仅平移+旋转(无缩放):
```glsl
// normalMatrix = mat3(modelMatrix) 即可
vec3 worldNormal = normalize(mat3(modelMatrix) * normal);
```

### 切线空间(Tangent Space)

**用于法线贴图**:

```
X轴: Tangent(切线)
Y轴: Bitangent(副切线)
Z轴: Normal(法线)
```

**TBN矩阵**:
```glsl
// 顶点着色器
vec3 T = normalize(normalMatrix * tangent.xyz);
vec3 N = normalize(normalMatrix * normal);
vec3 B = cross(N, T) * tangent.w;

mat3 TBN = mat3(T, B, N);  // 切线空间→世界空间

// 或传递TBN⁻¹到片段着色器
mat3 TBN_inv = transpose(TBN);  // 世界空间→切线空间
```

```glsl
// 片段着色器
vec3 tangentNormal = texture(normalMap, uv).xyz * 2.0 - 1.0;
vec3 worldNormal = normalize(TBN * tangentNormal);
```

---

## 投影变换

### 透视投影

**定义**: 模拟人眼/相机,远小近大。

```
视锥体:
     near
      ║
     ╱║╲
    ╱ ║ ╲
   ╱  ║  ╲
  ╱   ║   ╲
 ╱____║____╲
      far
```

**透视投影矩阵**:
```
P = [ f/aspect   0       0              0          ]
    [ 0          f       0              0          ]
    [ 0          0   -(far+near)  -2×far×near     ]
                    /(near-far)    /(near-far)
    [ 0          0      -1             0          ]

f = 1 / tan(fov/2)
aspect = width / height
```

**构建**:
```cpp
// Filament
Camera* camera = engine->createCamera();
camera->setProjection(
    45.0,           // fov (度)
    aspectRatio,    // 宽高比
    0.1,            // near
    100.0           // far
);
```

```glsl
// GLSL手动构建(不常用,通常由引擎提供)
mat4 perspective(float fovy, float aspect, float near, float far) {
    float f = 1.0 / tan(fovy / 2.0);
    return mat4(
        f/aspect, 0.0, 0.0,                            0.0,
        0.0,      f,   0.0,                            0.0,
        0.0,      0.0, (far+near)/(near-far),         -1.0,
        0.0,      0.0, (2.0*far*near)/(near-far),     0.0
    );
}
```

### 正交投影

**定义**: 平行投影,无透视效果。

```
视锥体(长方体):
  ┌────────┐
  │        │
  │        │
  │        │
  └────────┘
 near    far
```

**正交投影矩阵**:
```
P = [ 2/(r-l)      0          0       -(r+l)/(r-l) ]
    [ 0        2/(t-b)        0       -(t+b)/(t-b) ]
    [ 0            0      -2/(f-n)    -(f+n)/(f-n) ]
    [ 0            0          0            1       ]

l,r: left, right
b,t: bottom, top
n,f: near, far
```

**构建**:
```cpp
// Filament
camera->setProjection(
    Camera::Projection::ORTHO,
    -10.0, 10.0,  // left, right
    -10.0, 10.0,  // bottom, top
    0.1, 100.0    // near, far
);
```

**用途**:
- UI渲染
- 2D游戏
- CAD软件
- 阴影贴图

---

## 实用技巧

### 1. MVP矩阵组合

```cpp
// 预计算MVP矩阵(CPU)
mat4f MVP = projection * view * model;

// Shader中直接使用
uniform mat4 MVP;

void main() {
    gl_Position = MVP * vec4(position, 1.0);
}
```

**优势**: 减少Shader计算量。

### 2. 视锥剔除

**检查AABB是否在视锥内**:

```cpp
struct Frustum {
    Plane planes[6];  // 6个平面: near, far, left, right, top, bottom
};

bool AABBInFrustum(const AABB& box, const Frustum& frustum) {
    for (int i = 0; i < 6; i++) {
        if (AABBPlaneDistance(box, frustum.planes[i]) < 0)
            return false;  // 完全在平面外侧
    }
    return true;
}
```

### 3. Billboard(广告牌)

**物体总是面向相机**:

```glsl
// 方法1: 直接使用视图矩阵的逆(仅旋转部分)
mat3 billboard = transpose(mat3(viewMatrix));
vec3 billboardNormal = billboard * vec3(0, 0, 1);

// 方法2: 计算朝向
vec3 forward = normalize(cameraPos - worldPos);
vec3 right = normalize(cross(vec3(0,1,0), forward));
vec3 up = cross(forward, right);
mat3 billboard = mat3(right, up, forward);
```

### 4. 距离计算

```glsl
// 欧几里得距离
float dist = length(pos1 - pos2);

// 平方距离(避免sqrt,用于比较)
float distSq = dot(pos1 - pos2, pos1 - pos2);

// 曼哈顿距离
float manhattan = abs(pos1.x - pos2.x) + abs(pos1.y - pos2.y) + abs(pos1.z - pos2.z);
```

### 5. 平滑插值

```glsl
// 线性插值
float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

// 平滑阶跃
float smoothstep(float edge0, float edge1, float x) {
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// 更平滑(smootherstep)
float smootherstep(float edge0, float edge1, float x) {
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}
```

### 6. 球面坐标

**转换**:
```glsl
// 笛卡尔 → 球面
vec3 cartesianToSpherical(vec3 xyz) {
    float r = length(xyz);
    float theta = atan(xyz.y, xyz.x);      // 方位角
    float phi = acos(xyz.z / r);           // 极角
    return vec3(r, theta, phi);
}

// 球面 → 笛卡尔
vec3 sphericalToCartesian(vec3 rtp) {
    float r = rtp.x;
    float theta = rtp.y;
    float phi = rtp.z;
    return vec3(
        r * sin(phi) * cos(theta),
        r * sin(phi) * sin(theta),
        r * cos(phi)
    );
}
```

**应用**: 环境贴图采样、天空盒。

---

## 总结

### 核心概念

**向量**:
- 点积: 投影、夹角、光照
- 叉积: 法线、正交基、方向判定

**矩阵**:
- 变换: TRS (平移、旋转、缩放)
- 组合: M = T × R × S
- 法线: (M⁻¹)ᵀ

**四元数**:
- 旋转: 无万向节锁
- 插值: Slerp平滑

**坐标空间**:
```
局部 →[M]→ 世界 →[V]→ 观察 →[P]→ 裁剪 →[/w]→ NDC →[Viewport]→ 屏幕
```

### 常用公式

```glsl
// 归一化
v̂ = v / length(v)

// 点积
v · w = |v||w|cos(θ)

// 叉积
v × w: 垂直于v和w,大小=|v||w|sin(θ)

// 反射
r = v - 2(v·n)n

// Lerp
lerp(a, b, t) = a + t(b - a)

// MVP
gl_Position = P × V × M × position
```

### 下一步学习

- **[08-animation-theory.md](08-animation-theory.md)**: 动画中的数学应用
- **[01-rendering-fundamentals.md](01-rendering-fundamentals.md)**: 坐标变换在渲染中的应用
- `../gltf/02-core-concepts.md`: glTF中的变换和动画

---

**掌握3D数学,理解图形编程本质!**
