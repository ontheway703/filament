# GPU 动画技术

本文档深入探讨在 GPU 上实现动画的各种技术，包括硬件特性、算法优化和性能考量。

## 目录

1. [GPU 架构基础](#gpu-架构基础)
2. [GPU Skinning 技术](#gpu-skinning-技术)
3. [GPU Morphing 技术](#gpu-morphing-技术)
4. [Compute Shader 动画](#compute-shader-动画)
5. [内存管理与优化](#内存管理与优化)
6. [批处理与实例化](#批处理与实例化)
7. [性能分析与优化](#性能分析与优化)

## GPU 架构基础

### GPU vs CPU 的架构差异

#### CPU 特点
- 少量强大的核心（通常 4-16 个）
- 大容量缓存（MB 级别）
- 复杂的分支预测和乱序执行
- 适合复杂的控制逻辑

#### GPU 特点
- 大量简单的核心（数千个）
- 相对较小的缓存（KB 级别）
- SIMD（单指令多数据）执行模式
- 适合并行数据处理

### GPU 内存层次结构

```
全局内存 (Global Memory)     - 慢，大容量 (GB)
├── 纹理内存 (Texture Memory) - 缓存优化，只读
├── 常量内存 (Constant Memory) - 广播读取，只读
└── 共享内存 (Shared Memory)  - 快，小容量 (KB)
```

#### 内存访问模式优化

1. **合并访问 (Coalesced Access)**
```glsl
// 好的访问模式 - 连续访问
layout(std430, binding = 0) buffer PositionBuffer {
    vec4 positions[];  // 线程 i 访问 positions[i]
};

// 差的访问模式 - 跨步访问
layout(std430, binding = 0) buffer InterleavedBuffer {
    float data[];  // 线程 i 访问 data[i * stride + offset]
};
```

2. **对齐访问**
```cpp
// 确保数据结构是 16 字节对齐的
struct alignas(16) BoneMatrix {
    mat4 matrix;    // 64 bytes
    float padding;  // 使总大小为 80 字节，不是理想的
};

// 更好的方式
struct alignas(16) BoneMatrix {
    mat4 matrix;           // 64 bytes
    vec4 additionalData;   // 16 bytes，总共 80 字节
};
```

### GPU 并行计算模型

#### SIMD 执行
GPU 以 warp（NVIDIA）或 wavefront（AMD）为单位执行，通常 32 个线程一组：

```glsl
// 所有线程执行相同指令
void main() {
    int threadId = gl_GlobalInvocationID.x;

    // 如果有分支，会导致性能下降
    if (threadId % 2 == 0) {
        // 一半线程执行这里
        result[threadId] = computeEven(threadId);
    } else {
        // 另一半线程执行这里
        result[threadId] = computeOdd(threadId);
    }
}
```

#### 分支发散优化
```glsl
// 优化前 - 分支发散
if (boneWeights.x > 0.0) skinned += boneWeights.x * transform0 * position;
if (boneWeights.y > 0.0) skinned += boneWeights.y * transform1 * position;
if (boneWeights.z > 0.0) skinned += boneWeights.z * transform2 * position;
if (boneWeights.w > 0.0) skinned += boneWeights.w * transform3 * position;

// 优化后 - 消除分支
skinned += boneWeights.x * transform0 * position;
skinned += boneWeights.y * transform1 * position;
skinned += boneWeights.z * transform2 * position;
skinned += boneWeights.w * transform3 * position;
```

## GPU Skinning 技术

### 标准 GPU Skinning

#### 顶点着色器实现
```glsl
#version 450 core

// 顶点属性
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in uvec4 boneIndices;  // 骨骼索引
layout(location = 4) in vec4 boneWeights;   // 骨骼权重

// Uniform 缓冲区
layout(std140, binding = 0) uniform BoneMatrices {
    mat4 bones[256];  // 最多 256 个骨骼
};

layout(std140, binding = 1) uniform Matrices {
    mat4 modelMatrix;
    mat4 viewMatrix;
    mat4 projectionMatrix;
    mat3 normalMatrix;
};

// 输出
out vec3 worldNormal;
out vec2 uv;

void main() {
    // 计算蒙皮后的位置
    vec4 skinnedPosition = vec4(0.0);
    vec3 skinnedNormal = vec3(0.0);

    // 混合最多 4 个骨骼的影响
    for (int i = 0; i < 4; ++i) {
        float weight = boneWeights[i];
        if (weight > 0.0) {
            mat4 boneMatrix = bones[boneIndices[i]];
            skinnedPosition += weight * (boneMatrix * vec4(position, 1.0));
            skinnedNormal += weight * (mat3(boneMatrix) * normal);
        }
    }

    // 变换到世界空间
    vec4 worldPosition = modelMatrix * skinnedPosition;
    worldNormal = normalMatrix * skinnedNormal;
    uv = texCoord;

    // 投影变换
    gl_Position = projectionMatrix * viewMatrix * worldPosition;
}
```

### 优化的 GPU Skinning

#### 1. 预计算优化
```glsl
// 预先计算 bone * weight，减少计算量
layout(std140, binding = 0) uniform PrecomputedBones {
    mat4 weightedBones[256 * 4];  // 预计算权重*骨骼矩阵
};

void main() {
    vec4 skinnedPosition =
        weightedBones[boneIndices.x] * vec4(position, 1.0) +
        weightedBones[boneIndices.y] * vec4(position, 1.0) +
        weightedBones[boneIndices.z] * vec4(position, 1.0) +
        weightedBones[boneIndices.w] * vec4(position, 1.0);

    // ... 其余代码
}
```

#### 2. 矩阵调色板技术
只存储实际使用的骨骼矩阵：
```cpp
struct MatrixPalette {
    std::vector<mat4> matrices;
    std::unordered_map<int, int> globalToLocal;  // 全局索引到本地索引

    int addBone(int globalIndex, const mat4& matrix) {
        auto it = globalToLocal.find(globalIndex);
        if (it != globalToLocal.end()) {
            return it->second;  // 已存在
        }

        int localIndex = matrices.size();
        matrices.push_back(matrix);
        globalToLocal[globalIndex] = localIndex;
        return localIndex;
    }
};
```

#### 3. 四元数+平移优化
使用更紧凑的表示：
```glsl
struct DualQuaternion {
    vec4 real;      // 旋转四元数
    vec4 dual;      // 平移信息
};

layout(std140, binding = 0) uniform BoneDualQuats {
    DualQuaternion boneDQ[256];
};

vec3 transformWithDualQuat(vec3 position, DualQuaternion dq, float weight) {
    // 双四元数蒙皮，避免体积损失
    vec4 realPart = dq.real;
    vec4 dualPart = dq.dual;

    vec3 rotated = position + 2.0 * cross(realPart.xyz,
                   cross(realPart.xyz, position) + realPart.w * position);
    vec3 translated = rotated + 2.0 * (realPart.w * dualPart.xyz -
                      dualPart.w * realPart.xyz + cross(realPart.xyz, dualPart.xyz));

    return weight * translated;
}
```

### SkinningBuffer 实现

Filament 使用专门的 SkinningBuffer 来优化骨骼数据传输：

```cpp
// 在 Filament 中
SkinningBuffer* skinningBuffer = SkinningBuffer::Builder()
    .boneCount(boneCount)
    .initialize(true)  // 初始化为单位矩阵
    .build(engine);

// 更新骨骼数据
skinningBuffer->setBones(engine, boneMatrices.data(), boneCount);

// 绑定到 Renderable
RenderableManager::Builder()
    .skinning(skinningBuffer, boneCount, 0)
    .build(engine, entity);
```

## GPU Morphing 技术

### 标准 Morph Target 实现

#### 顶点着色器
```glsl
#version 450 core

layout(location = 0) in vec3 basePosition;
layout(location = 1) in vec3 baseNormal;
layout(location = 2) in vec2 texCoord;

// Morph target 数据
layout(location = 3) in vec3 morphPosition0;
layout(location = 4) in vec3 morphNormal0;
layout(location = 5) in vec3 morphPosition1;
layout(location = 6) in vec3 morphNormal1;
// ... 更多 morph targets

uniform float morphWeights[8];  // 最多 8 个 morph targets

void main() {
    vec3 position = basePosition;
    vec3 normal = baseNormal;

    // 应用 morph targets
    position += morphWeights[0] * morphPosition0;
    normal += morphWeights[0] * morphNormal0;
    position += morphWeights[1] * morphPosition1;
    normal += morphWeights[1] * morphNormal1;
    // ... 其余 targets

    // 标准变换
    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
```

### 优化的 Morph Target 实现

#### 1. 纹理存储
```glsl
// 使用纹理存储 morph target 数据
uniform sampler2D morphTargetTexture;
uniform float morphWeights[32];

vec3 getMorphDelta(int targetIndex, int vertexIndex) {
    // 计算纹理坐标
    int texWidth = textureSize(morphTargetTexture, 0).x;
    int x = vertexIndex % texWidth;
    int y = (vertexIndex / texWidth) + targetIndex * vertexHeight;

    return texelFetch(morphTargetTexture, ivec2(x, y), 0).xyz;
}

void main() {
    vec3 position = basePosition;
    int vertexId = gl_VertexID;

    // 应用所有 morph targets
    for (int i = 0; i < 32; ++i) {
        if (morphWeights[i] != 0.0) {
            position += morphWeights[i] * getMorphDelta(i, vertexId);
        }
    }

    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
```

#### 2. 稀疏 Morph Targets
只存储发生变化的顶点：
```glsl
layout(std430, binding = 0) buffer MorphTargetIndices {
    uint morphIndices[];     // 变形顶点的索引
};

layout(std430, binding = 1) buffer MorphTargetDeltas {
    vec3 morphDeltas[];      // 对应的位置差值
};

uniform int morphTargetCount;
uniform float morphWeights[32];

void main() {
    vec3 position = basePosition;
    int vertexId = gl_VertexID;

    // 查找当前顶点是否有 morph data
    for (int i = 0; i < morphTargetCount; ++i) {
        if (morphIndices[i] == vertexId) {
            for (int j = 0; j < 32; ++j) {
                if (morphWeights[j] != 0.0) {
                    position += morphWeights[j] * morphDeltas[i * 32 + j];
                }
            }
            break;
        }
    }

    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
```

### MorphTargetBuffer 优化

```cpp
// Filament 的 MorphTargetBuffer 实现
MorphTargetBuffer* morphBuffer = MorphTargetBuffer::Builder()
    .count(morphTargetCount)
    .vertexCount(vertexCount)
    .build(engine);

// 设置 morph target 数据
for (int i = 0; i < morphTargetCount; ++i) {
    morphBuffer->setPositionsAt(engine, i,
        morphPositions[i].data(), morphPositions[i].size());
    morphBuffer->setNormalsAt(engine, i,
        morphNormals[i].data(), morphNormals[i].size());
}

// 绑定到 Renderable
RenderableManager::Builder()
    .morphing(morphBuffer)
    .build(engine, entity);
```

## Compute Shader 动画

### 基础 Compute Shader 结构

```glsl
#version 450 core

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// 输入数据
layout(std430, binding = 0) buffer InputPositions {
    vec4 inputPositions[];
};

layout(std430, binding = 1) buffer InputNormals {
    vec4 inputNormals[];
};

// 输出数据
layout(std430, binding = 2) buffer OutputPositions {
    vec4 outputPositions[];
};

layout(std430, binding = 3) buffer OutputNormals {
    vec4 outputNormals[];
};

// 动画参数
layout(std140, binding = 0) uniform AnimationParams {
    mat4 boneMatrices[256];
    float morphWeights[32];
    float time;
    int vertexCount;
};

void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= vertexCount) return;

    // 执行动画计算
    vec4 position = inputPositions[index];
    vec4 normal = inputNormals[index];

    // 应用骨骼动画
    // ... skinning code ...

    // 应用 morph targets
    // ... morphing code ...

    // 写入结果
    outputPositions[index] = position;
    outputNormals[index] = normal;
}
```

### 高级 Compute Shader 技术

#### 1. 波前级优化
```glsl
// 使用共享内存优化骨骼矩阵访问
shared mat4 sharedBoneMatrices[32];  // 工作组共享的骨骼矩阵

void main() {
    uint localId = gl_LocalInvocationID.x;
    uint groupId = gl_WorkGroupID.x;
    uint globalId = gl_GlobalInvocationID.x;

    // 协作加载骨骼矩阵到共享内存
    if (localId < 32) {
        sharedBoneMatrices[localId] = boneMatrices[groupId * 32 + localId];
    }
    barrier();  // 等待所有线程完成加载

    // 使用共享内存中的骨骼矩阵进行计算
    // ...
}
```

#### 2. 异步计算
```cpp
// CPU 端的异步调度
class AsyncAnimationCompute {
    GLuint computeProgram;
    GLuint fence;

public:
    void dispatchAnimation(int vertexCount) {
        glUseProgram(computeProgram);

        // 设置参数
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, inputPositionBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, outputPositionBuffer);

        // 分发计算
        int workGroups = (vertexCount + 63) / 64;
        glDispatchCompute(workGroups, 1, 1);

        // 插入内存屏障
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // 创建同步点
        fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }

    bool isComplete() {
        if (!fence) return true;

        GLenum result = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
        if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED) {
            glDeleteSync(fence);
            fence = nullptr;
            return true;
        }
        return false;
    }
};
```

### GPU 动画管线

```cpp
class GPUAnimationPipeline {
    struct AnimationStage {
        GLuint computeProgram;
        std::vector<GLuint> inputBuffers;
        std::vector<GLuint> outputBuffers;
    };

    std::vector<AnimationStage> stages;

public:
    void addStage(AnimationStage stage) {
        stages.push_back(stage);
    }

    void execute() {
        for (const auto& stage : stages) {
            glUseProgram(stage.computeProgram);

            // 绑定输入输出缓冲区
            for (size_t i = 0; i < stage.inputBuffers.size(); ++i) {
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, stage.inputBuffers[i]);
            }

            for (size_t i = 0; i < stage.outputBuffers.size(); ++i) {
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                    stage.inputBuffers.size() + i, stage.outputBuffers[i]);
            }

            // 分发计算
            glDispatchCompute(workGroups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
    }
};
```

## 内存管理与优化

### GPU 内存分配策略

#### 1. 静态分配
```cpp
class GPUMemoryPool {
    GLuint buffer;
    size_t totalSize;
    size_t used;

public:
    GPUMemoryPool(size_t size) : totalSize(size), used(0) {
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
    }

    size_t allocate(size_t size) {
        if (used + size > totalSize) {
            return SIZE_MAX;  // 分配失败
        }

        size_t offset = used;
        used += size;
        return offset;
    }

    void upload(size_t offset, const void* data, size_t size) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size, data);
    }
};
```

#### 2. 动态分配
```cpp
class DynamicGPUAllocator {
    struct Block {
        size_t offset;
        size_t size;
        bool free;
    };

    GLuint buffer;
    std::vector<Block> blocks;

public:
    size_t allocate(size_t size) {
        // 查找合适的空闲块
        for (auto& block : blocks) {
            if (block.free && block.size >= size) {
                block.free = false;

                // 分割块
                if (block.size > size) {
                    blocks.insert(blocks.begin() + (&block - blocks.data()) + 1,
                        {block.offset + size, block.size - size, true});
                    block.size = size;
                }

                return block.offset;
            }
        }

        return SIZE_MAX;  // 分配失败
    }

    void deallocate(size_t offset) {
        // 标记为空闲并合并相邻块
        for (auto& block : blocks) {
            if (block.offset == offset) {
                block.free = true;
                mergeAdjacentBlocks();
                break;
            }
        }
    }
};
```

### 缓存优化

#### 1. 数据重排
```cpp
// 将相关数据放在一起，提高缓存局部性
struct SOAVertexData {
    std::vector<vec3> positions;    // 所有位置连续存储
    std::vector<vec3> normals;      // 所有法线连续存储
    std::vector<vec2> texCoords;    // 所有纹理坐标连续存储
    std::vector<uvec4> boneIndices; // 所有骨骼索引连续存储
};

// 转换为 GPU 友好的格式
void uploadToGPU(const SOAVertexData& data) {
    glBindBuffer(GL_ARRAY_BUFFER, positionBuffer);
    glBufferData(GL_ARRAY_BUFFER,
        data.positions.size() * sizeof(vec3),
        data.positions.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, normalBuffer);
    glBufferData(GL_ARRAY_BUFFER,
        data.normals.size() * sizeof(vec3),
        data.normals.data(), GL_STATIC_DRAW);

    // ... 其他缓冲区
}
```

#### 2. 预取策略
```glsl
// 在 compute shader 中预取数据
void main() {
    uint index = gl_GlobalInvocationID.x;

    // 预取下一个工作组需要的数据
    if (gl_LocalInvocationID.x == 0) {
        prefetchData(index + gl_WorkGroupSize.x);
    }

    // 处理当前数据
    processVertex(index);
}
```

## 批处理与实例化

### GPU 实例化渲染

#### 1. 基础实例化
```glsl
// 顶点着色器
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

// 实例化属性（每个实例一个值）
layout(location = 2) in mat4 instanceMatrix;
layout(location = 6) in float animationTime;

uniform sampler2D animationTexture;

void main() {
    // 从纹理中采样动画数据
    vec3 animatedPosition = sampleAnimation(position, animationTime);

    // 应用实例矩阵
    vec4 worldPosition = instanceMatrix * vec4(animatedPosition, 1.0);

    gl_Position = projectionMatrix * viewMatrix * worldPosition;
}
```

#### 2. 动画实例化
```cpp
class AnimatedInstanceRenderer {
    struct InstanceData {
        mat4 transform;
        float animationTime;
        int animationIndex;
        float padding[2];  // 确保 16 字节对齐
    };

    GLuint instanceBuffer;
    std::vector<InstanceData> instances;

public:
    void addInstance(const mat4& transform, float animTime, int animIndex) {
        instances.push_back({transform, animTime, animIndex, {0, 0}});
    }

    void render() {
        // 更新实例数据
        glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
        glBufferData(GL_ARRAY_BUFFER,
            instances.size() * sizeof(InstanceData),
            instances.data(), GL_DYNAMIC_DRAW);

        // 设置实例化属性
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE,
            sizeof(InstanceData), (void*)offsetof(InstanceData, transform));
        glVertexAttribDivisor(2, 1);  // 每个实例一个值

        // 渲染所有实例
        glDrawElementsInstanced(GL_TRIANGLES, indexCount,
            GL_UNSIGNED_SHORT, 0, instances.size());
    }
};
```

### 多重绘制调用

```cpp
class MultiDrawAnimationRenderer {
    struct DrawCommand {
        GLuint count;
        GLuint instanceCount;
        GLuint firstIndex;
        GLuint baseVertex;
        GLuint baseInstance;
    };

    std::vector<DrawCommand> drawCommands;
    GLuint commandBuffer;

public:
    void addDrawCall(GLuint indexCount, GLuint instanceCount,
                     GLuint firstIndex, GLuint baseVertex) {
        drawCommands.push_back({
            indexCount, instanceCount, firstIndex, baseVertex,
            static_cast<GLuint>(drawCommands.size())
        });
    }

    void render() {
        // 上传绘制命令
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, commandBuffer);
        glBufferData(GL_DRAW_INDIRECT_BUFFER,
            drawCommands.size() * sizeof(DrawCommand),
            drawCommands.data(), GL_DYNAMIC_DRAW);

        // 执行多重绘制
        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_SHORT,
            nullptr, drawCommands.size(), sizeof(DrawCommand));
    }
};
```

## 性能分析与优化

### GPU 性能计数器

```cpp
class GPUProfiler {
    GLuint queryObjects[16];
    int currentQuery = 0;

public:
    void beginProfile(const std::string& name) {
        glBeginQuery(GL_TIME_ELAPSED, queryObjects[currentQuery]);
    }

    void endProfile() {
        glEndQuery(GL_TIME_ELAPSED);
        currentQuery = (currentQuery + 1) % 16;
    }

    uint64_t getElapsedTime(int queryIndex) {
        uint64_t elapsed;
        glGetQueryObjectui64v(queryObjects[queryIndex], GL_QUERY_RESULT, &elapsed);
        return elapsed;  // 纳秒
    }
};
```

### 瓶颈分析

#### 1. 带宽限制
```glsl
// 测试内存带宽
layout(local_size_x = 256) in;

layout(std430, binding = 0) buffer TestBuffer {
    float data[];
};

void main() {
    uint index = gl_GlobalInvocationID.x;

    // 简单的内存访问模式
    data[index] = data[index] * 2.0;  // 测试带宽
}
```

#### 2. 计算限制
```glsl
// 测试计算性能
void main() {
    uint index = gl_GlobalInvocationID.x;

    float result = 0.0;
    // 大量浮点运算
    for (int i = 0; i < 1000; ++i) {
        result += sin(float(i) * 0.01) * cos(float(i) * 0.02);
    }

    data[index] = result;
}
```

### 优化策略

#### 1. 工作组大小调优
```cpp
// 测试不同工作组大小的性能
void benchmarkWorkGroupSizes(int vertexCount) {
    int sizes[] = {32, 64, 128, 256, 512};

    for (int size : sizes) {
        auto start = std::chrono::high_resolution_clock::now();

        int workGroups = (vertexCount + size - 1) / size;
        glDispatchCompute(workGroups, 1, 1);
        glFinish();  // 等待完成

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "Work group size " << size << ": " << duration.count() << " us\n";
    }
}
```

#### 2. 内存访问优化
```cpp
// 优化内存访问模式
class OptimizedVertexLayout {
    // 将频繁访问的数据放在一起
    struct HotData {
        vec3 position;
        vec3 normal;
    };

    struct ColdData {
        vec2 texCoord;
        vec4 color;
        vec4 tangent;
    };

    std::vector<HotData> hotData;
    std::vector<ColdData> coldData;

public:
    void uploadOptimized() {
        // 热数据使用更快的缓冲区
        glBindBuffer(GL_ARRAY_BUFFER, hotDataBuffer);
        glBufferData(GL_ARRAY_BUFFER, hotData.size() * sizeof(HotData),
            hotData.data(), GL_STATIC_DRAW);

        // 冷数据可以使用较慢的缓冲区
        glBindBuffer(GL_ARRAY_BUFFER, coldDataBuffer);
        glBufferData(GL_ARRAY_BUFFER, coldData.size() * sizeof(ColdData),
            coldData.data(), GL_STATIC_DRAW);
    }
};
```

## 总结

GPU 动画技术的关键要点：

### 硬件特性
1. **SIMD 执行模型**: 避免分支发散
2. **内存层次结构**: 优化访问模式
3. **并行度**: 充分利用 GPU 核心

### 算法优化
1. **Skinning**: 矩阵调色板、双四元数
2. **Morphing**: 稀疏存储、纹理压缩
3. **Compute Shader**: 异步计算、流水线

### 性能策略
1. **内存管理**: 池化分配、缓存优化
2. **批处理**: 实例化、多重绘制
3. **瓶颈分析**: 带宽 vs 计算限制

GPU 动画技术的发展趋势是向更高的并行度、更智能的内存管理和更灵活的计算模型发展，这要求开发者深入理解硬件特性并设计相应的算法。