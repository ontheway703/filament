# 动画原理

详细讲解3D动画的原理,包括关键帧动画、骨骼动画、蒙皮、插值算法和动画混合技术。

---

## 目录

1. [动画基础](#动画基础)
2. [关键帧动画](#关键帧动画)
3. [骨骼动画](#骨骼动画)
4. [蒙皮技术](#蒙皮技术)
5. [动画插值](#动画插值)
6. [动画混合](#动画混合)
7. [变形目标](#变形目标)
8. [动画优化](#动画优化)

---

## 动画基础

### 什么是动画

**动画**: 通过快速播放一系列静态图像产生运动的错觉。

```
帧率(FPS): 每秒显示的帧数

电影: 24 FPS
电视: 30/60 FPS
游戏: 30/60/120 FPS

人眼感知流畅: >24 FPS
```

### 动画类型

#### 1. 关键帧动画(Keyframe Animation)
```
定义关键帧,插值计算中间帧

时间:  0s    1s    2s    3s
位置:  A ──── B ──── C ──── D
        └插值→ └插值→ └插值→
```

#### 2. 骨骼动画(Skeletal Animation)
```
通过骨骼控制网格变形

骨骼动 → 网格跟随
```

#### 3. 变形目标(Morph Target/Blend Shape)
```
预定义多个形状,混合产生动画

基础形状 + 微笑 + 眨眼 = 表情
```

#### 4. 过程动画(Procedural Animation)
```
通过算法实时生成

布料模拟、毛发物理、IK
```

---

## 关键帧动画

### 关键帧定义

**关键帧**: 定义动画中重要时刻的状态。

```
动画曲线:
值
 │  关键帧●
 │     ╱╲
 │    ╱  ╲
 │   ╱    ╲
 │  ●      ●
 └──────────── 时间
   t0  t1  t2
```

### 动画通道(Channel)

**通道**: 控制特定属性的动画轨道。

```
节点动画:
├─ Translation通道: 控制位置
├─ Rotation通道: 控制旋转
└─ Scale通道: 控制缩放

每个通道有独立的关键帧
```

**glTF动画结构**:
```json
{
  "animations": [{
    "name": "Walk",
    "channels": [
      {
        "sampler": 0,
        "target": {
          "node": 1,
          "path": "translation"
        }
      },
      {
        "sampler": 1,
        "target": {
          "node": 1,
          "path": "rotation"
        }
      }
    ],
    "samplers": [
      {
        "input": 0,   // 时间轴Accessor
        "output": 1,  // 值Accessor
        "interpolation": "LINEAR"
      },
      {
        "input": 0,
        "output": 2,
        "interpolation": "LINEAR"
      }
    ]
  }]
}
```

### 时间轴

**时间轴数据**:
```
input Accessor: [0.0, 0.5, 1.0, 1.5, 2.0]  // 秒

要求:
- 严格递增
- 浮点数
- 最小值: 动画开始时间
- 最大值: 动画时长
```

### 输出数据

**根据path不同**:

| Path | 类型 | 说明 |
|------|------|------|
| **translation** | VEC3 | (x,y,z) 位置 |
| **rotation** | VEC4 | (x,y,z,w) 四元数 |
| **scale** | VEC3 | (x,y,z) 缩放 |
| **weights** | SCALAR | 变形目标权重 |

**数据对应**:
```
时间:  [0.0,  0.5,  1.0,  1.5,  2.0]
位置:  [(0,0,0), (1,0,0), (2,0,0), (3,0,0), (4,0,0)]

关键帧数 = 时间轴长度 = 输出数据长度
```

### 动画采样

```cpp
// 伪代码
void sampleAnimation(Animation* anim, float time) {
    for (Channel& channel : anim->channels) {
        Sampler& sampler = anim->samplers[channel.sampler];
        
        // 1. 查找时间在哪两个关键帧之间
        int i0 = findKeyframe(sampler.input, time);
        int i1 = i0 + 1;
        
        if (i1 >= sampler.input.size()) {
            i1 = i0;  // 最后一帧,不插值
        }
        
        float t0 = sampler.input[i0];
        float t1 = sampler.input[i1];
        float alpha = (time - t0) / (t1 - t0);  // [0,1]
        
        // 2. 获取关键帧值
        Value v0 = sampler.output[i0];
        Value v1 = sampler.output[i1];
        
        // 3. 插值
        Value interpolated = interpolate(v0, v1, alpha, sampler.interpolation);
        
        // 4. 应用到目标
        apply(channel.target, interpolated);
    }
}
```

---

## 骨骼动画

### 骨骼层级

**骨骼(Bone/Joint)**: 虚拟的骨架节点。

```
骨骼层级(人体):
       Head
        │
    ┌───┴───┐
  L_Arm   R_Arm
    │       │
  L_Hand  R_Hand
        │
      Spine
    ┌───┴───┐
  L_Leg   R_Leg
    │       │
  L_Foot  R_Foot
```

**父子关系**:
```
子骨骼继承父骨骼的变换

父旋转45° → 子也旋转45° (相对世界坐标)
```

### 蒙皮(Skinning)

**蒙皮**: 网格顶点绑定到骨骼,随骨骼变形。

```
骨骼(不可见):       网格(可见):
    │                  ╱│╲
    │                 ╱ │ ╲
    ●─骨骼            ●──●──●
   ╱│                顶点绑定到骨骼
  ╱ │
 ●  │
```

### 绑定姿态(Bind Pose)

**绑定姿态**: 网格绑定到骨骼时的初始姿态,通常是T-Pose或A-Pose。

```
T-Pose:              A-Pose:
  ─┴─                 ╲│╱
   │                   │
  ╱│╲                 ╱│╲
```

**逆绑定矩阵(Inverse Bind Matrix)**:

```
作用: 将顶点从模型空间转到骨骼局部空间

IBM[i] = BoneWorldMatrix[i]⁻¹ (在绑定姿态时)

用途: 蒙皮计算
```

### 蒙皮算法

#### 刚性蒙皮(Rigid Skinning)

**每个顶点仅受一个骨骼影响**:

```glsl
// 顶点受骨骼joints[0]控制
mat4 boneMatrix = jointMatrices[int(joints.x)];
vec4 skinnedPos = boneMatrix * vec4(position, 1.0);
```

**问题**: 关节处有断裂。

```
关节弯曲:
   ╲
    ╲ ← 断裂!
     │
```

#### 线性混合蒙皮(LBS, Linear Blend Skinning)

**每个顶点受多个骨骼影响,加权混合**:

```glsl
// 最多4个骨骼影响
in vec4 joints;   // 骨骼索引: (0,1,2,3)
in vec4 weights;  // 权重: (0.5, 0.3, 0.2, 0.0)

uniform mat4 jointMatrices[MAX_JOINTS];  // 所有骨骼矩阵

void main() {
    mat4 skinMatrix =
        weights.x * jointMatrices[int(joints.x)] +
        weights.y * jointMatrices[int(joints.y)] +
        weights.z * jointMatrices[int(joints.z)] +
        weights.w * jointMatrices[int(joints.w)];
    
    vec4 skinnedPos = skinMatrix * vec4(position, 1.0);
    vec3 skinnedNormal = mat3(skinMatrix) * normal;
    
    gl_Position = MVP * skinnedPos;
}
```

**权重归一化**:
```
weights.x + weights.y + weights.z + weights.w = 1.0
```

**优点**: 平滑过渡,无断裂
**缺点**: 关节处体积损失(糖果纸效应)

```
弯曲90°:
理想: │    实际: │
      │          ╲  ← 体积损失
     ╱│           ╲
    ╱ │            │
```

#### 对偶四元数蒙皮(DQS)

**解决体积损失问题**:

```glsl
// 使用对偶四元数表示变换
// 更复杂但效果更好
// Filament暂不直接支持,需自定义Shader
```

### 骨骼矩阵计算

```
最终骨骼矩阵 = 骨骼世界矩阵 × 逆绑定矩阵

jointMatrix[i] = jointWorldMatrix[i] × inverseBindMatrix[i]
```

**步骤**:

1. **更新骨骼局部变换**(来自动画):
```cpp
joint[i].localTransform = TRS(translation, rotation, scale);
```

2. **计算世界矩阵**(考虑父子关系):
```cpp
void updateJointWorldMatrix(int jointIndex) {
    if (joint[jointIndex].parent != -1) {
        joint[jointIndex].worldMatrix =
            joint[parent].worldMatrix * joint[jointIndex].localTransform;
    } else {
        joint[jointIndex].worldMatrix = joint[jointIndex].localTransform;
    }
}
```

3. **乘以逆绑定矩阵**:
```cpp
jointMatrix[i] = joint[i].worldMatrix * joint[i].inverseBindMatrix;
```

4. **上传到GPU**:
```cpp
glUniformMatrix4fv(location, numJoints, GL_FALSE, &jointMatrices[0]);
```

### Filament骨骼动画

```cpp
// 1. 加载glTF资产
FilamentAsset* asset = assetLoader->createAsset(data, size);
resourceLoader->loadResources(asset);

// 2. 获取Animator
Animator* animator = asset->getInstance()->getAnimator();

// 3. 渲染循环
float time = 0.0f;
while (rendering) {
    time += deltaTime;
    
    // 应用动画(更新骨骼变换)
    animator->applyAnimation(animIndex, time);
    
    // 更新骨骼矩阵(计算jointMatrix并上传GPU)
    animator->updateBoneMatrices();
    
    renderer->render(view);
}
```

---

## 蒙皮技术

### CPU蒙皮 vs GPU蒙皮

#### CPU蒙皮
```cpp
// CPU计算顶点位置
for (Vertex& v : vertices) {
    mat4 skinMatrix = mat4(0);
    for (int i = 0; i < 4; i++) {
        skinMatrix += v.weights[i] * jointMatrices[v.joints[i]];
    }
    v.position = skinMatrix * v.originalPosition;
}
// 上传到GPU
```

**优点**: 可用于碰撞检测、物理模拟
**缺点**: 慢,占用CPU

#### GPU蒙皮(推荐)
```glsl
// 顶点着色器计算
vec4 skinnedPos = skinMatrix * vec4(position, 1.0);
```

**优点**: 快,GPU并行
**缺点**: CPU无法访问蒙皮后的顶点

### 骨骼数量限制

**Uniform Buffer大小限制**:

```
Filament: 最多512个骨骼

UBO大小 = 512 × 16 floats × 4 bytes = 32KB
```

**移动端建议**: <100个骨骼

**超出限制怎么办**:
1. 简化骨骼层级
2. 分割模型(多个Mesh,每个<512骨骼)
3. 使用纹理存储骨骼矩阵

### 骨骼Palette

**Palette优化**: 每个Mesh仅使用部分骨骼。

```
全局骨骼: 512个
Mesh A使用: 骨骼[0,1,2,5,10]
Mesh B使用: 骨骼[3,4,6,7,8,9]

Palette A: [0→0, 1→1, 2→2, 5→3, 10→4]  // 局部索引
仅上传5个骨骼矩阵
```

---

## 动画插值

### 插值算法

#### 1. Step插值(阶跃)

**无插值,直接跳跃**:

```glsl
value = (t < 1.0) ? keyframe0 : keyframe1;
```

```
值
 │  ●────●
 │  │
 │  │
 │  ●
 └──────── 时间
    0  1
```

**用途**: 离散状态(开/关、显示/隐藏)

#### 2. Linear插值(线性)

**最常用**:

```glsl
value = mix(keyframe0, keyframe1, t);
// 或
value = keyframe0 + t * (keyframe1 - keyframe0);
```

```
值
 │    ●
 │   ╱
 │  ╱
 │ ╱
 │●
 └──────── 时间
  0    1
```

**特点**: 简单快速,但不够平滑

#### 3. Cubic Spline插值(三次样条)

**Hermite样条,最平滑**:

```
每个关键帧存储3个值:
- in-tangent (入切线)
- value (值)
- out-tangent (出切线)

output数组长度 = input长度 × 3
```

**公式**:
```glsl
float t2 = t * t;
float t3 = t2 * t;

// Hermite基函数
float h00 = 2.0*t3 - 3.0*t2 + 1.0;
float h10 = t3 - 2.0*t2 + t;
float h01 = -2.0*t3 + 3.0*t2;
float h11 = t3 - t2;

vec3 value = h00 * v0 +
             h10 * outTangent0 * deltaTime +
             h01 * v1 +
             h11 * inTangent1 * deltaTime;
```

```
值
 │      ●
 │    ╱‾
 │  ╱
 │_╱
 │●
 └──────── 时间
  0     1
```

**用途**: 高质量角色动画

### 四元数插值

**旋转不能用线性插值**!

#### Slerp (球面线性插值)

```glsl
quat slerp(quat q0, quat q1, float t) {
    float cosHalfTheta = dot(q0, q1);
    
    // 选择最短路径
    if (cosHalfTheta < 0.0) {
        q1 = -q1;
        cosHalfTheta = -cosHalfTheta;
    }
    
    // 接近时用lerp
    if (cosHalfTheta >= 1.0) {
        return q0;
    }
    
    float halfTheta = acos(cosHalfTheta);
    float sinHalfTheta = sqrt(1.0 - cosHalfTheta*cosHalfTheta);
    
    if (abs(sinHalfTheta) < 0.001) {
        return normalize((q0 + q1) * 0.5);
    }
    
    float ratioA = sin((1.0-t) * halfTheta) / sinHalfTheta;
    float ratioB = sin(t * halfTheta) / sinHalfTheta;
    
    return q0 * ratioA + q1 * ratioB;
}
```

**特点**: 
- 恒定角速度
- 最短路径
- 平滑

#### Nlerp (归一化线性插值)

```glsl
quat nlerp(quat q0, quat q1, float t) {
    return normalize(q0 * (1.0-t) + q1 * t);
}
```

**特点**:
- 快(无三角函数)
- 角速度不恒定(中间慢)
- 近似Slerp

**选择**: 实时游戏用Nlerp,电影用Slerp

---

## 动画混合

### 为什么需要混合

**平滑过渡**:
```
Idle动画 ──────→ Walk动画
        混合0.5s
```

**多动画叠加**:
```
Walk动画(下半身) + Wave动画(上半身) = 边走边挥手
```

### 线性混合(Lerp)

**两个动画混合**:

```glsl
pose = lerp(anim1, anim2, alpha);

// 每个变换分量分别插值
translation = mix(anim1.translation, anim2.translation, alpha);
rotation = slerp(anim1.rotation, anim2.rotation, alpha);
scale = mix(anim1.scale, anim2.scale, alpha);
```

**Filament实现**:
```cpp
// 先应用第二个动画
animator->applyAnimation(anim2Index, time2);

// 再混合第一个动画
animator->applyCrossFade(anim1Index, time1, 1.0-alpha);

// 更新骨骼
animator->updateBoneMatrices();
```

### 交叉淡入淡出(Crossfade)

**时间控制的混合**:

```cpp
float blendTime = 0.5f;  // 0.5秒过渡
float blendProgress = 0.0f;

while (blendProgress < blendTime) {
    blendProgress += deltaTime;
    float alpha = blendProgress / blendTime;  // [0,1]
    
    // 混合
    animator->applyAnimation(newAnim, newTime);
    animator->applyCrossFade(oldAnim, oldTime, 1.0-alpha);
    animator->updateBoneMatrices();
    
    oldTime += deltaTime;
    newTime += deltaTime;
}
```

**平滑曲线**:
```glsl
// 线性
alpha = t;

// SmoothStep (更平滑)
alpha = smoothstep(0.0, 1.0, t);

// 自定义曲线
alpha = pow(t, 2.0);  // 加速
alpha = sqrt(t);      // 减速
```

### 层级混合(Layered Blending)

**不同骨骼使用不同动画**:

```
上半身: Wave动画
下半身: Walk动画
```

```cpp
// 伪代码
for (Joint& joint : joints) {
    if (joint.name.startsWith("Spine") || joint.name.startsWith("Arm")) {
        // 上半身用Wave
        joint.transform = waveAnim.getJointTransform(joint.id, time);
    } else {
        // 下半身用Walk
        joint.transform = walkAnim.getJointTransform(joint.id, time);
    }
}
```

### 加法混合(Additive Blending)

**叠加小幅度动画**:

```
基础动画 + 偏移动画 = 最终结果

Idle + Breathing = 呼吸的Idle
Walk + Lean = 转弯的Walk
```

**计算**:
```glsl
// 偏移 = 加法动画 - 加法动画参考帧
offset = additiveAnim.frame[t] - additiveAnim.frame[0];

// 最终 = 基础 + 偏移
final = baseAnim.frame[t] + offset;
```

### 状态机(State Machine)

**管理动画转换**:

```
  Idle ←─→ Walk ←─→ Run
   ↓        ↓        ↓
  Jump    Jump    Jump
   ↓        ↓        ↓
  Land    Land    Land
   ↓        ↓        ↓
  Idle    Walk    Run
```

**转换条件**:
```cpp
if (speed > 0.1 && currentState == IDLE) {
    transition(WALK, 0.2f);  // 0.2s过渡到Walk
}

if (speed > 5.0 && currentState == WALK) {
    transition(RUN, 0.3f);
}

if (jumpPressed) {
    transition(JUMP, 0.1f);
}
```

---

## 变形目标

### 什么是变形目标

**变形目标**(Morph Target/Blend Shape): 预定义的网格形状变体。

```
基础形状: 中性脸
目标1: 微笑
目标2: 皱眉
目标3: 眨眼

混合: 基础 + 0.5×微笑 + 0.3×眨眼 = 微笑眨眼
```

### 数据存储

**glTF格式**:

```json
{
  "primitives": [{
    "attributes": {
      "POSITION": 0,  // 基础位置
      "NORMAL": 1     // 基础法线
    },
    "targets": [
      {
        "POSITION": 2,  // 目标1的位置偏移
        "NORMAL": 3     // 目标1的法线偏移
      },
      {
        "POSITION": 4,  // 目标2的位置偏移
        "NORMAL": 5     // 目标2的法线偏移
      }
    ]
  }],
  "weights": [0.0, 0.0]  // 默认权重
}
```

**偏移存储**:
```
存储的是偏移量,不是绝对位置:

target_position = offset  // 不是最终位置
target_normal = offset    // 不是最终法线
```

### 变形计算

**CPU计算**:
```cpp
for (Vertex& v : vertices) {
    vec3 position = v.basePosition;
    vec3 normal = v.baseNormal;
    
    for (int i = 0; i < numTargets; i++) {
        position += weights[i] * targets[i].positionOffset[v.index];
        normal += weights[i] * targets[i].normalOffset[v.index];
    }
    
    v.finalPosition = position;
    v.finalNormal = normalize(normal);
}
```

**GPU计算**(Shader):
```glsl
// 顶点着色器
in vec3 position;
in vec3 normal;
in vec3 target0_position;
in vec3 target0_normal;
in vec3 target1_position;
in vec3 target1_normal;

uniform float weights[2];

void main() {
    vec3 pos = position;
    vec3 norm = normal;
    
    pos += weights[0] * target0_position;
    norm += weights[0] * target0_normal;
    
    pos += weights[1] * target1_position;
    norm += weights[1] * target1_normal;
    
    norm = normalize(norm);
    
    gl_Position = MVP * vec4(pos, 1.0);
}
```

### 权重动画

**控制权重实现动画**:

```cpp
// 微笑动画
float smileWeight = 0.0f;
while (smiling) {
    smileWeight = min(smileWeight + deltaTime * 2.0f, 1.0f);  // 0.5s到1
    morphTarget.setWeight(SMILE_INDEX, smileWeight);
}

// 眨眼动画(循环)
float blinkPhase = sin(time * 5.0f);  // 5Hz
float blinkWeight = max(blinkPhase, 0.0f);
morphTarget.setWeight(BLINK_INDEX, blinkWeight);
```

### 应用场景

**1. 面部表情**:
```
中性脸
+ 微笑 × 0.8
+ 眨左眼 × 1.0
+ 抬眉 × 0.5
= 表情
```

**2. 口型同步(Lip Sync)**:
```
音素对应的目标:
"A": 0.9
"E": 0.1
"O": 0.3
...
```

**3. 肌肉变形**:
```
放松状态
+ 收缩 × 0.7
= 肌肉鼓起
```

### 性能考虑

**内存**:
```
基础网格: 10K顶点 × 12 bytes = 120KB
每个目标: 10K顶点 × 12 bytes = 120KB
10个目标: 120KB × 10 = 1.2MB

总计: 120KB + 1.2MB = 1.32MB
```

**计算**:
```
GPU每顶点计算:
  基础位置
  + 目标0偏移 × 权重0
  + 目标1偏移 × 权重1
  + ...

建议目标数 < 8 (移动端)
```

---

## 动画优化

### 1. 关键帧优化

**减少关键帧数量**:

```
优化前: 60帧/秒,10秒动画 = 600关键帧
优化后: 30帧/秒,10秒动画 = 300关键帧 (50%压缩)

质量损失小,文件大小减半
```

**关键帧压缩**:
```cpp
// 移除冗余关键帧
if (keyframe[i] ≈ lerp(keyframe[i-1], keyframe[i+1])) {
    remove(keyframe[i]);  // 可以通过插值重建
}
```

### 2. 量化(Quantization)

**位置量化**:
```cpp
// Float32 → Int16
vec3 quantize(vec3 position, vec3 min, vec3 max) {
    vec3 normalized = (position - min) / (max - min);
    ivec3 quantized = ivec3(normalized * 65535.0);
    return quantized;
}

// 32 bits × 3 → 16 bits × 3 (50%压缩)
```

**旋转量化**:
```cpp
// 四元数最小3分量
// 存储3个最小分量,重建第4个
// 128 bits → 48 bits (62.5%压缩)
```

### 3. LOD动画

**距离-based动画质量**:

```cpp
float distance = length(cameraPos - objectPos);

if (distance < 10.0f) {
    animator->update(60.0f);  // 60Hz,高质量
} else if (distance < 50.0f) {
    if (frame % 2 == 0)  // 30Hz
        animator->update(30.0f);
} else {
    if (frame % 4 == 0)  // 15Hz
        animator->update(15.0f);
}
```

### 4. 动画实例化

**多个对象共享动画数据**:

```cpp
// 共享
AnimationClip* walkAnim = loadAnimation("walk.gltf");

// 实例
AnimationPlayer player1(walkAnim);
AnimationPlayer player2(walkAnim);

player1.setTime(0.5f);  // 不同时间
player2.setTime(1.2f);

// 节省内存: 1份动画数据,多个播放状态
```

### 5. 动画剔除

**不可见对象不更新动画**:

```cpp
if (frustum.contains(object.boundingBox)) {
    animator->update(deltaTime);
    animator->updateBoneMatrices();
} else {
    // 不在视野内,跳过更新
}
```

### 6. 异步更新

**动画更新在后台线程**:

```cpp
// 渲染线程
void render() {
    // 使用上一帧的动画结果
    renderMesh(cachedBoneMatrices);
}

// 动画线程
void animationThread() {
    while (running) {
        animator->update(deltaTime);
        animator->getBoneMatrices(cachedBoneMatrices);
        sleep(16ms);  // 60Hz
    }
}
```

---

## 总结

### 核心概念

**关键帧动画**:
- 时间轴 + 输出值
- 插值: Linear, Step, Cubic Spline
- 通道: Translation, Rotation, Scale

**骨骼动画**:
- 骨骼层级
- 逆绑定矩阵
- 线性混合蒙皮(LBS)

**动画混合**:
- 交叉淡入淡出
- 层级混合
- 加法混合

**变形目标**:
- 偏移存储
- 权重控制
- 面部表情

### 动画公式

```glsl
// 插值
lerp(a, b, t) = a + t(b - a)
slerp(q0, q1, t) = ...  // 四元数

// 蒙皮
skinMatrix = Σ weights[i] × jointMatrices[joints[i]]
skinnedPos = skinMatrix × position

// 变形
finalPos = basePos + Σ weights[i] × targets[i].offset
```

### 性能优化

1. 减少关键帧
2. 量化数据
3. LOD动画
4. 动画剔除
5. 实例化
6. 异步更新

### 下一步学习

- **[07-3d-math.md](07-3d-math.md)**: 四元数、矩阵数学基础
- `../gltf/05-animation-system.md`: glTF动画详解
- `../gltfio/02-loading-phases.md`: Filament动画加载

---

**理解动画原理,创造生动角色!**
