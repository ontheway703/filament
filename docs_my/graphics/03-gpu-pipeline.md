# GPU渲染管线

详细讲解GPU图形渲染管线的各个阶段,从顶点处理到像素输出的完整流程。

---

## GPU管线总览

```
顶点数据 → 顶点着色器 → 图元装配 → 光栅化 → 片段着色器 → 逐片段操作 → 帧缓冲
```

### 可编程 vs 固定功能

**可编程阶段** (你编写Shader控制):
- ✅ 顶点着色器 (Vertex Shader)
- ✅ 片段着色器 (Fragment Shader)  
- ✅ 几何着色器 (Geometry Shader, 可选)
- ✅ 细分着色器 (Tessellation, 可选)

**固定功能阶段** (GPU硬件控制):
- ❌ 图元装配
- ❌ 光栅化
- ❌ 深度/模板测试
- ❌ 混合

---

## 1. 顶点着色器 (Vertex Shader)

### 作用

处理每个顶点,进行坐标变换。

### 输入

```glsl
// Attribute (顶点属性)
in vec3 position;      // 位置
in vec3 normal;        // 法线
in vec2 uv;            // UV坐标
in vec4 color;         // 顶点颜色
in vec4 tangent;       // 切线

// Uniform (常量)
uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
```

### 输出

```glsl
// 必需输出: 裁剪空间坐标
out vec4 gl_Position;

// 自定义输出(传递给片段着色器)
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
```

### 典型任务

**1. 坐标变换**:
```glsl
void main() {
    // 局部 → 世界
    vec4 worldPos = modelMatrix * vec4(position, 1.0);

    // 世界 → 视图 → 裁剪
    vec4 clipPos = projectionMatrix * viewMatrix * worldPos;

    gl_Position = clipPos;
}
```

**2. 骨骼蒙皮**:
```glsl
mat4 skinMatrix =
    weights.x * boneMatrices[int(joints.x)] +
    weights.y * boneMatrices[int(joints.y)] +
    weights.z * boneMatrices[int(joints.z)] +
    weights.w * boneMatrices[int(joints.w)];

vec4 skinnedPos = skinMatrix * vec4(position, 1.0);
gl_Position = projectionMatrix * viewMatrix * modelMatrix * skinnedPos;
```

**3. 法线变换**:
```glsl
// 法线使用逆转置矩阵
mat3 normalMatrix = mat3(transpose(inverse(modelMatrix)));
vNormal = normalize(normalMatrix * normal);
```

---

## 2. 图元装配 (Primitive Assembly)

### 作用

将顶点组装成图元(点、线、三角形)。

### 图元类型

```
GL_POINTS:           GL_LINES:          GL_TRIANGLES:
  •  •  •             ─  ─  ─             ╱╲ ╱╲
  •  •  •             ─  ─  ─            ╱  ╲╱  ╲

GL_LINE_STRIP:       GL_TRIANGLE_STRIP:
  ───────────          ╱╲╱╲╱╲
                      ╱  ╱  ╱
```

### 裁剪 (Clipping)

裁掉视锥体外的图元:

```
视锥体:
   ╱│╲
  ╱ │ ╲
 ╱  │  ╲
────┼────
    │
    相机
```

---

## 3. 光栅化 (Rasterization)

### 作用

将三角形转换为片段(像素候选)。

### 三角形扫描

```
三角形:               片段:
   A                  ████
   ╱╲                ██████
  ╱  ╲              ████████
 ╱____╲            ██████████
B      C
```

### 重心坐标插值

对于三角形ABC内的点P:
```
P = α×A + β×B + γ×C

α + β + γ = 1
α, β, γ ≥ 0
```

**插值顶点属性**:
```glsl
// 顶点着色器输出
out vec2 vUV[3];  // 三个顶点的UV

// 片段着色器输入(自动插值)
in vec2 vUV;  // P点的UV = α×UV[0] + β×UV[1] + γ×UV[2]
```

### 透视校正插值

```
透视投影下,线性插值不正确:

错误(线性):           正确(透视校正):
━━━━━━━━━            ━━━━━━━━━
╲        ╱            ╲        ╱
 ╲______╱              ╲______╱
  纹理变形              纹理正确
```

GPU自动进行透视校正:
```
attribute' = attribute / w
w = 裁剪空间坐标的w分量
```

---

## 4. 片段着色器 (Fragment Shader)

### 作用

计算每个片段(像素)的颜色。

### 输入

```glsl
// 从顶点着色器插值而来
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

// Uniform
uniform sampler2D baseColorMap;
uniform vec3 lightDirection;
uniform vec3 cameraPosition;
```

### 输出

```glsl
// 颜色输出
out vec4 fragColor;
```

### 典型任务

**1. 纹理采样**:
```glsl
vec4 albedo = texture(baseColorMap, vUV);
```

**2. 光照计算**:
```glsl
vec3 N = normalize(vNormal);
vec3 L = normalize(-lightDirection);
vec3 V = normalize(cameraPosition - vWorldPos);

// Diffuse
float NoL = max(0.0, dot(N, L));
vec3 diffuse = albedo.rgb * NoL;

// Specular (Blinn-Phong)
vec3 H = normalize(L + V);
float NoH = max(0.0, dot(N, H));
float specular = pow(NoH, 32.0);

fragColor = vec4(diffuse + specular, 1.0);
```

**3. PBR光照**: 见 [02-pbr-theory.md](02-pbr-theory.md)

---

## 5. 逐片段操作 (Per-Fragment Operations)

### 深度测试 (Depth Test)

```
if (fragDepth < depthBuffer[x,y]):
    通过,写入颜色和深度
else:
    失败,丢弃片段
```

**深度函数**:
- `GL_LESS`: fragDepth < stored (默认,更近的通过)
- `GL_LEQUAL`: ≤
- `GL_GREATER`: >
- `GL_ALWAYS`: 总是通过

**Early-Z优化**:
现代GPU在片段着色器之前进行深度测试,提前剔除被遮挡的片段。

### 模板测试 (Stencil Test)

用于实现特殊效果:

```glsl
if ((stencilRef & stencilMask) op (stencilBuffer[x,y] & stencilMask)):
    通过
else:
    失败

op = GL_EQUAL, GL_NOTEQUAL, GL_LESS, GL_LEQUAL, ...
```

**应用**:
- 阴影体(Shadow Volumes)
- 镜面反射
- 描边效果
- 遮罩

### Alpha混合 (Blending)

半透明物体需要混合:

```
finalColor = srcColor × srcFactor + dstColor × dstFactor

srcColor = 片段着色器输出
dstColor = 帧缓冲现有颜色
```

**常见混合模式**:

**透明混合**:
```
srcFactor = GL_SRC_ALPHA          (α)
dstFactor = GL_ONE_MINUS_SRC_ALPHA (1-α)

finalColor = srcColor × α + dstColor × (1-α)
```

**加法混合**(发光效果):
```
srcFactor = GL_ONE
dstFactor = GL_ONE

finalColor = srcColor + dstColor
```

**乘法混合**(阴影):
```
srcFactor = GL_ZERO
dstFactor = GL_SRC_COLOR

finalColor = dstColor × srcColor
```

---

## 完整示例

### 简单的Phong光照

**顶点着色器**:
```glsl
#version 300 es
precision highp float;

in vec3 position;
in vec3 normal;
in vec2 uv;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main() {
    vec4 worldPos = modelMatrix * vec4(position, 1.0);
    vWorldPos = worldPos.xyz;

    mat3 normalMatrix = mat3(transpose(inverse(modelMatrix)));
    vNormal = normalize(normalMatrix * normal);

    vUV = uv;

    gl_Position = projectionMatrix * viewMatrix * worldPos;
}
```

**片段着色器**:
```glsl
#version 300 es
precision mediump float;

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

uniform sampler2D albedoMap;
uniform vec3 lightPos;
uniform vec3 lightColor;
uniform vec3 cameraPos;

out vec4 fragColor;

void main() {
    // 采样纹理
    vec4 albedo = texture(albedoMap, vUV);

    // 归一化向量
    vec3 N = normalize(vNormal);
    vec3 L = normalize(lightPos - vWorldPos);
    vec3 V = normalize(cameraPos - vWorldPos);
    vec3 R = reflect(-L, N);

    // Ambient
    vec3 ambient = 0.1 * albedo.rgb;

    // Diffuse
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * lightColor * albedo.rgb;

    // Specular (Blinn-Phong)
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 32.0);
    vec3 specular = spec * lightColor;

    fragColor = vec4(ambient + diffuse + specular, 1.0);
}
```

---

## 性能优化

### 1. 减少顶点着色器开销

```glsl
// ❌ 差: 重复计算
uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

void main() {
    gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(position, 1.0);
}

// ✅ 好: 预乘矩阵
uniform mat4 MVP;  // = projection * view * model

void main() {
    gl_Position = MVP * vec4(position, 1.0);
}
```

### 2. Early-Z优化

```glsl
// 先渲染不透明物体(从前到后)
// 后渲染透明物体(从后到前)

// 启用Early-Z
glEnable(GL_DEPTH_TEST);
glDepthFunc(GL_LESS);
```

### 3. 避免片段着色器中的复杂计算

```glsl
// ❌ 差: 每个片段计算
in vec3 vPosition;
uniform mat4 viewMatrix;

void main() {
    vec3 viewPos = (viewMatrix * vec4(vPosition, 1.0)).xyz;  // 慢!
}

// ✅ 好: 在顶点着色器计算
out vec3 vViewPos;

void main() {
    vViewPos = (viewMatrix * vec4(position, 1.0)).xyz;  // 只计算一次
}
```

---

## 总结

**GPU管线的5个核心阶段**:
1. **顶点着色器**: 坐标变换、顶点处理
2. **图元装配**: 组装三角形、裁剪
3. **光栅化**: 三角形 → 片段、属性插值
4. **片段着色器**: 计算颜色、纹理、光照
5. **逐片段操作**: 深度测试、混合、输出

**下一步学习**:
- **[04-shader-programming.md](04-shader-programming.md)**: Shader编程详解
- **[09-gpu-optimization.md](09-gpu-optimization.md)**: GPU性能优化

---

**理解GPU管线是高效渲染的关键!**
