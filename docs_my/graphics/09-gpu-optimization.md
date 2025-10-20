# GPU优化技术详解

本文档详细说明GPU渲染优化的核心技术和最佳实践，帮助你构建高性能的实时渲染应用。

---

## 目录

1. [Draw Call优化](#draw-call优化)
2. [批处理技术](#批处理技术)
3. [GPU实例化](#gpu实例化)
4. [LOD系统](#lod系统)
5. [剔除技术](#剔除技术)
6. [几何压缩](#几何压缩)
7. [纹理优化](#纹理优化)
8. [GPU内存管理](#gpu内存管理)
9. [渲染管线优化](#渲染管线优化)
10. [移动端优化](#移动端优化)

---

## Draw Call优化

### Draw Call的成本

**什么是Draw Call**:
```cpp
// 每次调用都会产生CPU-GPU通信开销
glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
```

**性能影响**:
- **CPU开销**: 驱动验证状态、准备命令缓冲
- **GPU同步**: CPU等待GPU完成上一帧
- **状态切换**: 绑定VBO、IBO、纹理、材质
- **批次限制**: 桌面GPU: ~10000 draw calls/frame, 移动GPU: ~500-1000

### 减少Draw Call的策略

**1. 合并网格 (Static Batching)**

```cpp
// 错误：每个物体一个draw call (100 objects = 100 draw calls)
for (auto& obj : objects) {
    bindMaterial(obj.material);
    bindVertexBuffer(obj.vbo);
    glDrawElements(...);
}

// 正确：合并为单个网格 (1 draw call)
MergedMesh mergeMeshes(const std::vector<Mesh>& meshes) {
    MergedMesh result;
    
    for (auto& mesh : meshes) {
        // 合并顶点数据
        size_t baseVertex = result.vertices.size();
        result.vertices.insert(result.vertices.end(), 
                               mesh.vertices.begin(), 
                               mesh.vertices.end());
        
        // 调整索引偏移
        for (auto index : mesh.indices) {
            result.indices.push_back(baseVertex + index);
        }
    }
    
    return result;
}
```

**2. 纹理图集 (Texture Atlas)**

```cpp
// 问题：每个物体不同纹理需要切换状态
for (auto& obj : objects) {
    glBindTexture(GL_TEXTURE_2D, obj.textureID);  // 状态切换！
    glDrawElements(...);
}

// 解决：将多个纹理合并到图集
struct UVTransform {
    vec2 offset;  // 纹理在图集中的偏移
    vec2 scale;   // 纹理在图集中的缩放
};

// Vertex Shader
out vec2 vUV;
uniform vec2 uvOffset;
uniform vec2 uvScale;

void main() {
    vUV = aUV * uvScale + uvOffset;  // 调整UV到图集坐标
    gl_Position = ...;
}

// 一次draw call渲染所有物体
glBindTexture(GL_TEXTURE_2D, atlasTexture);
glDrawElements(...);
```

**3. 材质排序 (Material Sorting)**

```cpp
// 按材质排序减少状态切换
void sortByMaterial(std::vector<RenderCommand>& commands) {
    std::sort(commands.begin(), commands.end(),
        [](const RenderCommand& a, const RenderCommand& b) {
            // 优先级：Shader > 纹理 > VBO > IBO
            if (a.shader != b.shader) return a.shader < b.shader;
            if (a.texture != b.texture) return a.texture < b.texture;
            if (a.vbo != b.vbo) return a.vbo < b.vbo;
            return a.ibo < b.ibo;
        });
}
```

---

## 批处理技术

### Static Batching（静态批处理）

**适用场景**: 静态不动的物体（建筑、地形装饰）

```cpp
// 预处理阶段合并网格
class StaticBatcher {
    struct Batch {
        VertexBuffer vbo;
        IndexBuffer ibo;
        Material material;
    };
    
    std::vector<Batch> batches;
    
    void addObject(const Mesh& mesh, const Material& mat) {
        // 查找相同材质的batch
        for (auto& batch : batches) {
            if (batch.material == mat) {
                batch.vbo.append(mesh.vertices);
                batch.ibo.append(mesh.indices);
                return;
            }
        }
        
        // 创建新batch
        Batch newBatch;
        newBatch.material = mat;
        newBatch.vbo.append(mesh.vertices);
        newBatch.ibo.append(mesh.indices);
        batches.push_back(newBatch);
    }
    
    void render() {
        for (auto& batch : batches) {
            bindMaterial(batch.material);
            glDrawElements(...);  // 一次draw call渲染多个物体
        }
    }
};
```

### Dynamic Batching（动态批处理）

**适用场景**: 动态物体（粒子、UI元素）

```cpp
// 每帧重新合并
class DynamicBatcher {
    VertexBuffer dynamicVBO;
    IndexBuffer dynamicIBO;
    
    void beginFrame() {
        dynamicVBO.clear();
        dynamicIBO.clear();
    }
    
    void submit(const Mesh& mesh, const mat4& transform) {
        size_t baseVertex = dynamicVBO.size();
        
        // 变换顶点到世界空间
        for (auto& v : mesh.vertices) {
            Vertex worldV = v;
            worldV.position = transform * vec4(v.position, 1.0);
            worldV.normal = mat3(transform) * v.normal;
            dynamicVBO.push_back(worldV);
        }
        
        // 添加索引
        for (auto idx : mesh.indices) {
            dynamicIBO.push_back(baseVertex + idx);
        }
    }
    
    void endFrame() {
        // 上传合并后的数据
        dynamicVBO.upload();
        dynamicIBO.upload();
        glDrawElements(...);  // 单次draw call
    }
};
```

---

## GPU实例化

### Instancing基础

**原理**: 单次draw call渲染多个相同网格的实例

```cpp
// 传统方法：N个draw calls
for (int i = 0; i < 1000; ++i) {
    setUniform("modelMatrix", transforms[i]);
    glDrawElements(...);
}

// Instancing：1个draw call
glDrawElementsInstanced(GL_TRIANGLES, indexCount, 
                        GL_UNSIGNED_INT, 0, 1000);
```

### 实例化数据传递

**方法1: Uniform Buffer Object (UBO)**

```glsl
// Vertex Shader
layout(std140) uniform InstanceData {
    mat4 modelMatrices[1000];  // 最多1000个实例
};

void main() {
    mat4 model = modelMatrices[gl_InstanceID];
    gl_Position = projection * view * model * vec4(aPosition, 1.0);
}
```

```cpp
// C++ 代码
GLuint ubo;
glGenBuffers(1, &ubo);
glBindBuffer(GL_UNIFORM_BUFFER, ubo);
glBufferData(GL_UNIFORM_BUFFER, sizeof(mat4) * 1000, 
             modelMatrices.data(), GL_DYNAMIC_DRAW);
glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);

glDrawElementsInstanced(GL_TRIANGLES, indexCount, 
                        GL_UNSIGNED_INT, 0, 1000);
```

**方法2: Instance Attributes**

```cpp
// 准备实例数据缓冲
GLuint instanceVBO;
glGenBuffers(1, &instanceVBO);
glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
glBufferData(GL_ARRAY_BUFFER, sizeof(mat4) * 1000, 
             modelMatrices.data(), GL_STATIC_DRAW);

// 配置实例属性 (mat4 = 4个vec4)
for (int i = 0; i < 4; ++i) {
    glEnableVertexAttribArray(3 + i);
    glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE, 
                          sizeof(mat4), 
                          (void*)(sizeof(vec4) * i));
    glVertexAttribDivisor(3 + i, 1);  // 每个实例更新一次
}
```

```glsl
// Vertex Shader
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in mat4 aInstanceMatrix;  // Instance属性

void main() {
    gl_Position = projection * view * aInstanceMatrix * vec4(aPosition, 1.0);
}
```

**方法3: Texture Buffer (支持更多实例)**

```glsl
// Vertex Shader
uniform samplerBuffer instanceTransforms;  // TBO

mat4 getInstanceMatrix(int instanceID) {
    int base = instanceID * 4;
    return mat4(
        texelFetch(instanceTransforms, base + 0),
        texelFetch(instanceTransforms, base + 1),
        texelFetch(instanceTransforms, base + 2),
        texelFetch(instanceTransforms, base + 3)
    );
}

void main() {
    mat4 model = getInstanceMatrix(gl_InstanceID);
    gl_Position = projection * view * model * vec4(aPosition, 1.0);
}
```

### 实例化最佳实践

```cpp
// 完整的实例化渲染器
class InstancedRenderer {
    struct InstanceData {
        mat4 modelMatrix;
        vec4 color;
    };
    
    GLuint vao, vbo, ibo, instanceVBO;
    std::vector<InstanceData> instances;
    
    void init(const Mesh& mesh) {
        // 设置网格数据
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        
        // 顶点缓冲
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(Vertex),
                     mesh.vertices.data(), GL_STATIC_DRAW);
        
        // 索引缓冲
        glGenBuffers(1, &ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(uint32_t),
                     mesh.indices.data(), GL_STATIC_DRAW);
        
        // 顶点属性
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        
        // 实例缓冲
        glGenBuffers(1, &instanceVBO);
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(InstanceData) * 10000, 
                     nullptr, GL_DYNAMIC_DRAW);
        
        // 实例属性 (modelMatrix)
        for (int i = 0; i < 4; ++i) {
            glEnableVertexAttribArray(1 + i);
            glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, 
                                  sizeof(InstanceData), 
                                  (void*)(sizeof(vec4) * i));
            glVertexAttribDivisor(1 + i, 1);
        }
        
        // 实例属性 (color)
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, 
                              sizeof(InstanceData), 
                              (void*)(sizeof(mat4)));
        glVertexAttribDivisor(5, 1);
    }
    
    void addInstance(const mat4& transform, const vec4& color) {
        instances.push_back({transform, color});
    }
    
    void render() {
        // 更新实例数据
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, 
                        instances.size() * sizeof(InstanceData),
                        instances.data());
        
        // 绘制
        glBindVertexArray(vao);
        glDrawElementsInstanced(GL_TRIANGLES, indexCount, 
                                GL_UNSIGNED_INT, 0, 
                                instances.size());
        
        instances.clear();  // 清空准备下一帧
    }
};
```

---

## LOD系统

### 距离-based LOD

**概念**: 根据相机距离选择不同细节级别的网格

```cpp
class LODGroup {
    struct LODLevel {
        Mesh mesh;
        float distance;  // 切换距离
    };
    
    std::vector<LODLevel> levels;
    
    void addLevel(const Mesh& mesh, float distance) {
        levels.push_back({mesh, distance});
        // 按距离排序
        std::sort(levels.begin(), levels.end(),
            [](const LODLevel& a, const LODLevel& b) {
                return a.distance < b.distance;
            });
    }
    
    const Mesh& selectLOD(const vec3& objectPos, const vec3& cameraPos) {
        float distance = length(objectPos - cameraPos);
        
        for (auto& level : levels) {
            if (distance < level.distance) {
                return level.mesh;
            }
        }
        
        // 返回最低LOD
        return levels.back().mesh;
    }
};

// 使用示例
LODGroup tree;
tree.addLevel(treeHighPoly,   10.0f);  // 0-10m:  10000 triangles
tree.addLevel(treeMediumPoly, 50.0f);  // 10-50m: 2000 triangles
tree.addLevel(treeLowPoly,   200.0f);  // 50-200m: 500 triangles
tree.addLevel(treeBillboard, FLT_MAX); // 200m+: 2 triangles (广告牌)

// 渲染时选择LOD
const Mesh& lodMesh = tree.selectLOD(treePosition, camera.position);
render(lodMesh);
```

### Screen Space LOD

**原理**: 根据物体在屏幕上的像素大小选择LOD

```cpp
float calculateScreenCoverage(const BoundingBox& bbox, 
                               const mat4& mvp,
                               int screenWidth, int screenHeight) {
    // 将包围盒8个顶点投影到屏幕空间
    vec3 corners[8] = {
        {bbox.min.x, bbox.min.y, bbox.min.z},
        {bbox.max.x, bbox.min.y, bbox.min.z},
        // ... 其他6个顶点
    };
    
    vec2 minScreen(FLT_MAX, FLT_MAX);
    vec2 maxScreen(-FLT_MAX, -FLT_MAX);
    
    for (auto& corner : corners) {
        vec4 clip = mvp * vec4(corner, 1.0);
        vec3 ndc = vec3(clip) / clip.w;  // NDC: [-1, 1]
        
        // 转换到屏幕坐标
        vec2 screen;
        screen.x = (ndc.x * 0.5 + 0.5) * screenWidth;
        screen.y = (ndc.y * 0.5 + 0.5) * screenHeight;
        
        minScreen = min(minScreen, screen);
        maxScreen = max(maxScreen, screen);
    }
    
    // 计算屏幕上的像素面积
    vec2 size = maxScreen - minScreen;
    return size.x * size.y;
}

const Mesh& selectLODByScreenSize(float screenPixels) {
    if (screenPixels > 10000) return highPolyMesh;  // > 100x100 pixels
    if (screenPixels > 2500)  return mediumPolyMesh; // > 50x50 pixels
    if (screenPixels > 400)   return lowPolyMesh;    // > 20x20 pixels
    return culled;  // 太小，直接剔除
}
```

### 平滑LOD过渡

**Dithered LOD Transition**

```glsl
// Fragment Shader
uniform float lodBlendFactor;  // [0, 1], 0=LOD0, 1=LOD1

void main() {
    // 使用抖动模式混合两个LOD
    float threshold = (gl_FragCoord.x + gl_FragCoord.y) / 2.0;
    threshold = fract(threshold * 0.5);  // [0, 1]
    
    if (lodBlendFactor > threshold) {
        discard;  // 显示下一个LOD
    }
    
    // 当前LOD的渲染
    fragColor = calculateColor();
}
```

---

## 剔除技术

### 视锥剔除 (Frustum Culling)

**原理**: 移除视野外的物体

```cpp
struct Frustum {
    Plane planes[6];  // Left, Right, Top, Bottom, Near, Far
    
    static Frustum fromMatrix(const mat4& vp) {
        Frustum f;
        
        // 从VP矩阵提取6个平面
        // Left plane
        f.planes[0] = Plane(vp[0][3] + vp[0][0],
                            vp[1][3] + vp[1][0],
                            vp[2][3] + vp[2][0],
                            vp[3][3] + vp[3][0]);
        
        // Right plane
        f.planes[1] = Plane(vp[0][3] - vp[0][0],
                            vp[1][3] - vp[1][0],
                            vp[2][3] - vp[2][0],
                            vp[3][3] - vp[3][0]);
        
        // ... Top, Bottom, Near, Far (类似方法)
        
        // 归一化平面方程
        for (int i = 0; i < 6; ++i) {
            f.planes[i].normalize();
        }
        
        return f;
    }
    
    bool intersects(const BoundingBox& bbox) const {
        for (int i = 0; i < 6; ++i) {
            // 找到最正向的顶点
            vec3 positiveVertex;
            positiveVertex.x = (planes[i].normal.x > 0) ? bbox.max.x : bbox.min.x;
            positiveVertex.y = (planes[i].normal.y > 0) ? bbox.max.y : bbox.min.y;
            positiveVertex.z = (planes[i].normal.z > 0) ? bbox.max.z : bbox.min.z;
            
            // 如果最正向的顶点都在平面负侧，则完全在外
            if (planes[i].distance(positiveVertex) < 0) {
                return false;  // 完全在视锥外
            }
        }
        
        return true;  // 至少部分在视锥内
    }
};

// 使用示例
Frustum frustum = Frustum::fromMatrix(projectionMatrix * viewMatrix);

for (auto& obj : objects) {
    if (frustum.intersects(obj.boundingBox)) {
        renderQueue.push(obj);  // 在视野内，加入渲染队列
    }
}
```

### 遮挡剔除 (Occlusion Culling)

**GPU Occlusion Queries**

```cpp
class OcclusionQuery {
    GLuint queryID;
    bool resultAvailable = false;
    GLuint visiblePixels = 0;
    
public:
    void begin() {
        glBeginQuery(GL_ANY_SAMPLES_PASSED, queryID);
    }
    
    void end() {
        glEndQuery(GL_ANY_SAMPLES_PASSED);
        resultAvailable = false;
    }
    
    bool isVisible() {
        if (!resultAvailable) {
            GLint available;
            glGetQueryObjectiv(queryID, GL_QUERY_RESULT_AVAILABLE, &available);
            if (available) {
                glGetQueryObjectuiv(queryID, GL_QUERY_RESULT, &visiblePixels);
                resultAvailable = true;
            }
        }
        
        return visiblePixels > 0;
    }
};

// 两帧延迟的遮挡剔除
struct Object {
    Mesh mesh;
    BoundingBox bbox;
    OcclusionQuery query;
    bool wasVisibleLastFrame = true;
};

void renderWithOcclusionCulling(std::vector<Object>& objects) {
    // 第一遍：渲染包围盒进行遮挡测试
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);  // 不写入颜色
    glDepthMask(GL_FALSE);  // 不写入深度
    
    for (auto& obj : objects) {
        if (obj.wasVisibleLastFrame) {
            obj.query.begin();
            renderBoundingBox(obj.bbox);  // 渲染简化的包围盒
            obj.query.end();
        }
    }
    
    // 第二遍：渲染可见物体
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    
    for (auto& obj : objects) {
        // 使用上一帧的结果
        if (obj.query.isVisible()) {
            render(obj.mesh);
            obj.wasVisibleLastFrame = true;
        } else {
            obj.wasVisibleLastFrame = false;
        }
    }
}
```

**Hierarchical Z-Buffer (Hi-Z)**

```cpp
// 使用Mipmap链存储深度缓冲的最小值
void buildHiZBuffer(Texture2D depthBuffer) {
    int width = depthBuffer.width;
    int height = depthBuffer.height;
    int numLevels = log2(max(width, height));
    
    // 创建Hi-Z纹理
    Texture2D hiZ;
    hiZ.createMipmaps(width, height, numLevels);
    
    // Level 0 = 原始深度
    hiZ.setLevel(0, depthBuffer.data);
    
    // 生成Mipmap（每级取4个像素的最小深度）
    for (int level = 1; level < numLevels; ++level) {
        // Shader: sample 2x2 pixels and output min depth
        computeMinDepth(hiZ, level);
    }
    
    return hiZ;
}

// 在CPU端测试遮挡
bool isOccluded(const BoundingBox& bbox, const Texture2D& hiZ, const mat4& vp) {
    // 将包围盒投影到屏幕空间
    vec2 minScreen, maxScreen;
    projectBBoxToScreen(bbox, vp, minScreen, maxScreen);
    
    // 选择合适的mip level
    vec2 size = maxScreen - minScreen;
    int mipLevel = log2(max(size.x, size.y));
    
    // 采样Hi-Z缓冲的深度
    float bufferDepth = hiZ.sample(mipLevel, (minScreen + maxScreen) * 0.5);
    
    // 计算包围盒的最近深度
    float bboxDepth = calculateNearestDepth(bbox, vp);
    
    // 如果包围盒的最近点都在buffer深度后面，则被遮挡
    return bboxDepth > bufferDepth;
}
```

### 背面剔除 (Backface Culling)

```cpp
// 默认启用背面剔除
glEnable(GL_CULL_FACE);
glCullFace(GL_BACK);  // 剔除背面
glFrontFace(GL_CCW);  // 逆时针为正面

// 双面材质
glDisable(GL_CULL_FACE);  // 禁用剔除（性能损失）
```

---

## 几何压缩

### 顶点压缩

**法线压缩 (Octahedron Encoding)**

```cpp
// 将法线从12字节(3×float)压缩到4字节(2×int16)
vec2 encodeNormal(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));  // 投影到八面体
    
    if (n.z < 0.0) {
        // 折叠到正半球
        vec2 wrapped = (1.0 - abs(vec2(n.y, n.x))) * 
                       vec2(n.x >= 0.0 ? 1.0 : -1.0, 
                            n.y >= 0.0 ? 1.0 : -1.0);
        return wrapped;
    }
    
    return vec2(n.x, n.y);
}

vec3 decodeNormal(vec2 encoded) {
    vec3 n = vec3(encoded.x, encoded.y, 1.0 - abs(encoded.x) - abs(encoded.y));
    
    if (n.z < 0.0) {
        vec2 wrapped = (1.0 - abs(vec2(n.y, n.x))) * 
                       vec2(n.x >= 0.0 ? 1.0 : -1.0, 
                            n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = wrapped;
    }
    
    return normalize(n);
}

// 存储为16位整数
struct CompressedVertex {
    int16_t normalX, normalY;  // 4 bytes
};

void storeNormal(CompressedVertex& v, const vec3& normal) {
    vec2 encoded = encodeNormal(normal);
    v.normalX = int16_t(encoded.x * 32767.0f);
    v.normalY = int16_t(encoded.y * 32767.0f);
}
```

```glsl
// Vertex Shader
attribute vec2 aNormal;  // 16位整数

vec3 decodeNormal(vec2 encoded) {
    // 解压逻辑同上
}

void main() {
    vec3 normal = decodeNormal(aNormal / 32767.0);
    // 使用解压后的法线
}
```

**位置量化 (Quantization)**

```cpp
// 将float32位置压缩到uint16
struct BoundingBox {
    vec3 min, max;
};

uint16_t quantizePosition(float value, float min, float max) {
    float normalized = (value - min) / (max - min);  // [0, 1]
    return uint16_t(normalized * 65535.0f);
}

float dequantizePosition(uint16_t quantized, float min, float max) {
    float normalized = quantized / 65535.0f;
    return normalized * (max - min) + min;
}

// 压缩整个模型
struct CompressedMesh {
    BoundingBox bounds;
    std::vector<uvec3> positions;  // 每个顶点3×uint16
};

CompressedMesh compress(const Mesh& mesh) {
    CompressedMesh result;
    result.bounds = mesh.calculateBounds();
    
    for (auto& v : mesh.vertices) {
        uvec3 compressed;
        compressed.x = quantizePosition(v.position.x, 
                                        result.bounds.min.x, 
                                        result.bounds.max.x);
        compressed.y = quantizePosition(v.position.y, 
                                        result.bounds.min.y, 
                                        result.bounds.max.y);
        compressed.z = quantizePosition(v.position.z, 
                                        result.bounds.min.z, 
                                        result.bounds.max.z);
        result.positions.push_back(compressed);
    }
    
    return result;
}
```

```glsl
// Vertex Shader
attribute uvec3 aPosition;  // 16位无符号整数
uniform vec3 uBoundsMin;
uniform vec3 uBoundsMax;

vec3 dequantizePosition(uvec3 quantized) {
    vec3 normalized = vec3(quantized) / 65535.0;
    return normalized * (uBoundsMax - uBoundsMin) + uBoundsMin;
}

void main() {
    vec3 position = dequantizePosition(aPosition);
    gl_Position = projection * view * model * vec4(position, 1.0);
}
```

### Mesh简化

**边折叠 (Edge Collapse)**

```cpp
// 简化网格到目标三角形数量
Mesh simplifyMesh(const Mesh& original, int targetTriangleCount) {
    // 1. 为每条边计算折叠代价（QEM算法）
    std::priority_queue<EdgeCollapse> collapses;
    
    for (auto& edge : original.edges) {
        float cost = calculateCollapseCost(edge);
        collapses.push({edge, cost});
    }
    
    // 2. 按代价从小到大折叠边
    Mesh result = original;
    while (result.triangleCount() > targetTriangleCount && !collapses.empty()) {
        EdgeCollapse collapse = collapses.top();
        collapses.pop();
        
        // 执行边折叠
        result.collapseEdge(collapse.edge);
        
        // 更新相邻边的代价
        for (auto& adjacentEdge : collapse.edge.adjacentEdges) {
            float newCost = calculateCollapseCost(adjacentEdge);
            collapses.push({adjacentEdge, newCost});
        }
    }
    
    return result;
}

// 生成LOD链
std::vector<Mesh> generateLODs(const Mesh& highPoly) {
    std::vector<Mesh> lods;
    lods.push_back(highPoly);  // LOD0: 100%
    
    lods.push_back(simplifyMesh(highPoly, highPoly.triangleCount() * 0.5));  // LOD1: 50%
    lods.push_back(simplifyMesh(highPoly, highPoly.triangleCount() * 0.25)); // LOD2: 25%
    lods.push_back(simplifyMesh(highPoly, highPoly.triangleCount() * 0.1));  // LOD3: 10%
    
    return lods;
}
```

---

## 纹理优化

### Mipmap优化

```cpp
// 启用Mipmap
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
glGenerateMipmap(GL_TEXTURE_2D);

// Mipmap Bias (控制LOD选择)
glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, -0.5f);  // 更清晰
```

### 纹理压缩

**桌面: BC/DXT格式**

```cpp
// BC1 (DXT1): RGB, 6:1压缩, 无/1-bit Alpha
// BC3 (DXT5): RGBA, 4:1压缩, 平滑Alpha
// BC5: 双通道, 用于法线贴图
// BC7: 最高质量, 4:1压缩

// 加载压缩纹理
glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
                       width, height, 0, dataSize, data);
```

**移动: ETC2/ASTC格式**

```cpp
// ETC2: OpenGL ES 3.0标准
glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA8_ETC2_EAC,
                       width, height, 0, dataSize, data);

// ASTC: 可变压缩比
// 4x4:  8bpp (高质量)
// 6x6:  3.56bpp (平衡)
// 8x8:  2bpp (高压缩)
glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_ASTC_4x4_KHR,
                       width, height, 0, dataSize, data);
```

**Universal: Basis Universal / KTX2**

```cpp
// 运行时转码到目标格式
BasisTranscoder transcoder;
transcoder.init();

// 根据平台选择格式
TextureFormat targetFormat;
if (supportsBC7) targetFormat = TextureFormat::BC7;
else if (supportsASTC) targetFormat = TextureFormat::ASTC_4x4;
else targetFormat = TextureFormat::ETC2;

// 转码
transcoder.transcode(basisData, targetFormat, outputData);
glCompressedTexImage2D(..., outputData);
```

### 纹理流送 (Texture Streaming)

```cpp
class TextureStreamer {
    struct MipLevel {
        int level;
        size_t dataSize;
        uint8_t* data;
        bool loaded = false;
    };
    
    std::vector<MipLevel> mips;
    GLuint textureID;
    
    void init(int width, int height, int numMips) {
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);
        
        // 分配存储空间（但不上传数据）
        glTexStorage2D(GL_TEXTURE_2D, numMips, GL_RGBA8, width, height);
        
        // 初始加载最低分辨率mip
        loadMipLevel(numMips - 1);
    }
    
    void loadMipLevel(int level) {
        if (mips[level].loaded) return;
        
        // 从磁盘/网络加载
        mips[level].data = loadFromDisk(level);
        
        // 上传到GPU
        int mipWidth = std::max(1, width >> level);
        int mipHeight = std::max(1, height >> level);
        glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0,
                        mipWidth, mipHeight,
                        GL_RGBA, GL_UNSIGNED_BYTE,
                        mips[level].data);
        
        mips[level].loaded = true;
        delete[] mips[level].data;  // 释放CPU内存
    }
    
    void update(float screenCoverage) {
        // 根据屏幕覆盖率计算需要的mip level
        int requiredLevel = calculateRequiredMip(screenCoverage);
        
        // 逐步加载更高分辨率的mip
        for (int i = mips.size() - 1; i >= requiredLevel; --i) {
            if (!mips[i].loaded) {
                loadMipLevel(i);
                break;  // 每帧只加载一个mip
            }
        }
    }
};
```

---

## GPU内存管理

### 缓冲区管理

**Ring Buffer (循环缓冲)**

```cpp
// 避免GPU-CPU同步
class RingBuffer {
    GLuint vbo;
    size_t bufferSize;
    size_t offset = 0;
    const int NUM_FRAMES = 3;  // 三重缓冲
    
    void init(size_t size) {
        bufferSize = size * NUM_FRAMES;
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, bufferSize, nullptr, GL_DYNAMIC_DRAW);
    }
    
    void* allocate(size_t size) {
        // 循环到下一个区块
        offset = (offset + size) % bufferSize;
        
        // 映射内存
        void* ptr = glMapBufferRange(GL_ARRAY_BUFFER, offset, size,
                                      GL_MAP_WRITE_BIT | 
                                      GL_MAP_INVALIDATE_RANGE_BIT |
                                      GL_MAP_UNSYNCHRONIZED_BIT);
        return ptr;
    }
    
    void commit() {
        glUnmapBuffer(GL_ARRAY_BUFFER);
    }
};

// 使用示例
RingBuffer dynamicVBO;
dynamicVBO.init(1024 * 1024);  // 1MB × 3 = 3MB

void renderFrame() {
    Vertex* vertices = (Vertex*)dynamicVBO.allocate(sizeof(Vertex) * count);
    // 写入顶点数据
    memcpy(vertices, data, sizeof(Vertex) * count);
    dynamicVBO.commit();
    
    glDrawArrays(GL_TRIANGLES, 0, count);
}
```

### 资源池

```cpp
template<typename T>
class ResourcePool {
    std::vector<T> resources;
    std::queue<size_t> freeList;
    
public:
    size_t allocate() {
        if (freeList.empty()) {
            // 扩展池
            size_t index = resources.size();
            resources.emplace_back();
            return index;
        }
        
        size_t index = freeList.front();
        freeList.pop();
        return index;
    }
    
    void free(size_t index) {
        freeList.push(index);
    }
    
    T& get(size_t index) {
        return resources[index];
    }
};

// 使用示例
ResourcePool<Texture> texturePool;

size_t texID = texturePool.allocate();
Texture& tex = texturePool.get(texID);
tex.loadFromFile("albedo.png");

// 使用完后释放
texturePool.free(texID);  // 不销毁资源,放回池中复用
```

---

## 渲染管线优化

### Early-Z优化

```cpp
// 1. 先渲染不透明物体(由近到远)
glDepthFunc(GL_LESS);
glDepthMask(GL_TRUE);
glDisable(GL_BLEND);

// 由近到远排序
std::sort(opaqueObjects.begin(), opaqueObjects.end(),
    [&camera](const Object& a, const Object& b) {
        float distA = length(a.position - camera.position);
        float distB = length(b.position - camera.position);
        return distA < distB;  // 近的先渲染
    });

for (auto& obj : opaqueObjects) {
    render(obj);
}

// 2. 再渲染透明物体(由远到近)
glDepthMask(GL_FALSE);  // 不写入深度
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

// 由远到近排序
std::sort(transparentObjects.begin(), transparentObjects.end(),
    [&camera](const Object& a, const Object& b) {
        float distA = length(a.position - camera.position);
        float distB = length(b.position - camera.position);
        return distA > distB;  // 远的先渲染
    });

for (auto& obj : transparentObjects) {
    render(obj);
}
```

### Z-PrePass

```cpp
// 第一遍：只写入深度
glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
glDepthMask(GL_TRUE);
glDepthFunc(GL_LESS);

for (auto& obj : objects) {
    bindSimpleShader();  // 最简单的vertex shader
    render(obj);
}

// 第二遍：完整渲染(利用Early-Z剔除被遮挡的片段)
glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
glDepthFunc(GL_EQUAL);  // 只渲染深度相等的片段
glDepthMask(GL_FALSE);  // 不再写入深度

for (auto& obj : objects) {
    bindComplexShader();  // 完整的PBR shader
    render(obj);
}
```

---

## 移动端优化

### PowerVR Tile-Based渲染优化

```cpp
// 避免在片段着色器中读取深度/颜色
// PowerVR在tile内部处理,但读取会触发resolve

// 错误
glFramebufferTexture2D(..., GL_DEPTH_ATTACHMENT, depthTexture, 0);
// 在shader中: texture(depthTexture, uv);  // 触发resolve!

// 正确: 使用单独的深度纹理
```

### Mali GPU优化

```cpp
// 1. 减少寄存器使用
// 错误: 大量临时变量
vec3 temp1 = ...;
vec3 temp2 = ...;
// ... 很多临时变量

// 正确: 内联计算
fragColor = calculateFinal(inputs);

// 2. 避免依赖纹理读取
// 错误
vec2 uv = texture(uvMap, vUV).xy;  // 第一次采样
vec3 color = texture(albedoMap, uv).rgb;  // 依赖第一次结果

// 正确: 使用固定UV
vec3 color = texture(albedoMap, vUV).rgb;
```

### Adreno GPU优化

```cpp
// 使用FP16精度
// Adreno GPU对FP16有专门优化

// GLSL
precision mediump float;  // 使用FP16

// 或显式指定
mediump vec3 color;
mediump float roughness;
```

### 通用移动端优化

```cpp
// 1. 限制复杂度
// - 顶点着色器: < 100 ALU指令
// - 片段着色器: < 200 ALU指令
// - 纹理采样: < 4次/fragment

// 2. 使用精度修饰符
precision highp float;     // 位置、UV
precision mediump float;   // 颜色、法线
precision lowp float;      // 标量参数

// 3. 避免分支
// 错误
if (useTexture) {
    color = texture(tex, uv);
} else {
    color = baseColor;
}

// 正确: 使用mix
float useTex = useTexture ? 1.0 : 0.0;
color = mix(baseColor, texture(tex, uv), useTex);

// 4. 合并Uniform
// 错误: 多个uniform (每个占用一个slot)
uniform float roughness;
uniform float metallic;
uniform float reflectance;

// 正确: 打包到vec4
uniform vec4 materialParams;  // (roughness, metallic, reflectance, unused)
```

---

## 性能分析工具

### RenderDoc

```cpp
// 1. 捕获帧
// 在RenderDoc中启动应用或附加到进程
// 按F12捕获当前帧

// 2. 分析Draw Call
// - 查看每个draw call的输入/输出
// - 检查纹理内容
// - 查看Shader性能

// 3. 查找瓶颈
// - Pipeline Statistics: 查看顶点/片段数量
// - Texture Viewer: 检查纹理分辨率
// - Shader Debugger: 逐步调试Shader
```

### GPU Timer

```cpp
class GPUTimer {
    GLuint queryIDs[2];
    
    void begin() {
        glBeginQuery(GL_TIME_ELAPSED, queryIDs[0]);
    }
    
    void end() {
        glEndQuery(GL_TIME_ELAPSED);
    }
    
    float getElapsedMS() {
        GLuint64 elapsed;
        glGetQueryObjectui64v(queryIDs[0], GL_QUERY_RESULT, &elapsed);
        return elapsed / 1000000.0f;  // ns -> ms
    }
};

// 使用示例
GPUTimer timer;

timer.begin();
renderScene();
timer.end();

float gpuTime = timer.getElapsedMS();
slog.i << "GPU Time: " << gpuTime << "ms" << io::endl;
```

### 性能指标

```cpp
struct PerformanceMetrics {
    // CPU指标
    float cpuFrameTime;    // 总CPU时间
    float updateTime;      // 逻辑更新时间
    float cullingTime;     // 剔除时间
    float submitTime;      // 提交draw call时间
    
    // GPU指标
    float gpuFrameTime;    // 总GPU时间
    float vertexTime;      // 顶点处理时间
    float fragmentTime;    // 片段处理时间
    
    // 统计
    int drawCalls;
    int triangles;
    int vertices;
    int textureBinds;
    int shaderSwitches;
    
    void print() {
        slog.i << "=== Performance ===" << io::endl;
        slog.i << "FPS: " << (1000.0f / cpuFrameTime) << io::endl;
        slog.i << "CPU: " << cpuFrameTime << "ms" << io::endl;
        slog.i << "GPU: " << gpuFrameTime << "ms" << io::endl;
        slog.i << "Draw Calls: " << drawCalls << io::endl;
        slog.i << "Triangles: " << triangles << io::endl;
    }
};
```

---

## 优化清单

### 渲染优化
- [ ] 减少draw calls (batching, instancing)
- [ ] 按材质排序减少状态切换
- [ ] 启用视锥剔除和遮挡剔除
- [ ] 使用LOD系统
- [ ] 启用背面剔除
- [ ] Early-Z优化(由近到远渲染不透明物体)

### 内存优化
- [ ] 压缩纹理(BC/ETC2/ASTC)
- [ ] 生成Mipmap
- [ ] 压缩顶点数据(量化、八面体法线)
- [ ] 使用纹理图集减少纹理数量
- [ ] 实现纹理流送

### Shader优化
- [ ] 减少纹理采样次数
- [ ] 避免分支和循环
- [ ] 使用适当的精度修饰符(移动端)
- [ ] 内联小函数
- [ ] 移除unused变量和代码

### 几何优化
- [ ] 使用Mesh简化工具
- [ ] 合并小网格
- [ ] 移除不可见几何体
- [ ] 使用impostors/billboards替代远距离模型

### 移动端专项
- [ ] 限制Shader复杂度
- [ ] 使用FP16精度
- [ ] 避免Alpha Testing(使用Alpha Blending)
- [ ] 减少overdraw
- [ ] 限制骨骼数量(< 100)

---

## 总结

**优化优先级**:
1. **减少Draw Calls** - 最直接的性能提升
2. **剔除不可见物体** - 避免浪费GPU资源
3. **LOD系统** - 平衡质量和性能
4. **纹理压缩** - 减少带宽和内存
5. **Shader优化** - 降低每像素成本

**性能目标**:
- **桌面**: 60 FPS (16.6ms/frame)
- **移动端**: 30-60 FPS (33.3-16.6ms/frame)
- **VR**: 90+ FPS (11ms/frame)

**调试流程**:
1. 使用性能分析工具找到瓶颈
2. 如果CPU bound: 优化draw calls和剔除
3. 如果GPU bound: 优化Shader和几何复杂度
4. 如果带宽bound: 优化纹理和顶点数据

**参考资源**:
- [GDC GPU Performance Talks](https://gdconf.com/)
- [ARM Mali GPU Optimization Guide](https://developer.arm.com/solutions/graphics-and-gaming/gaming-engine/mali-gpu-optimization-guide)
- [Qualcomm Adreno GPU Developer Guide](https://developer.qualcomm.com/software/adreno-gpu-sdk)
- [NVIDIA GPU Gems](https://developer.nvidia.com/gpugems/gpugems/contributors)
