# Driver 抽象层设计

本文档详细讲解 Filament Driver 接口的设计，包括资源管理 API、渲染命令 API，以及如何统一不同图形 API 的差异。

---

## Driver 接口概述

**Driver** 是 Filament Backend 的核心抽象接口，定义了所有图形操作的统一 API。

**文件位置**: `filament/backend/include/private/backend/Driver.h`

### 设计原则

1. **API 中立**: 不偏向任何特定图形 API
2. **高性能**: 最小化抽象开销
3. **完整性**: 覆盖所有渲染功能
4. **类型安全**: 使用强类型 Handle

---

## 资源创建 API

### VertexBuffer (顶点缓冲)

```cpp
Handle<HwVertexBuffer> createVertexBuffer(
    uint8_t bufferCount,      // 缓冲区数量(通常1)
    uint8_t attributeCount,   // 顶点属性数量
    uint32_t vertexCount,     // 顶点数量
    AttributeArray attributes // 属性描述
);

void updateVertexBuffer(
    Handle<HwVertexBuffer> vbh,
    size_t index,             // 缓冲区索引
    BufferDescriptor&& data,  // 数据
    uint32_t byteOffset       // 偏移
);

void destroyVertexBuffer(Handle<HwVertexBuffer> vbh);
```

**AttributeArray 定义**:
```cpp
struct Attribute {
    uint8_t buffer;       // 缓冲区索引
    uint8_t offset;       // 属性偏移
    uint8_t stride;       // 步长
    ElementType type;     // 数据类型(FLOAT3, FLOAT2等)
};

using AttributeArray = std::array<Attribute, MAX_VERTEX_ATTRIBUTE_COUNT>;
```

**各后端实现**:
- **OpenGL**: `glGenBuffers()` + `glBindBuffer(GL_ARRAY_BUFFER)`
- **Vulkan**: `vkCreateBuffer()` + `VK_BUFFER_USAGE_VERTEX_BUFFER_BIT`
- **Metal**: `[device newBufferWithLength:]`

---

### IndexBuffer (索引缓冲)

```cpp
Handle<HwIndexBuffer> createIndexBuffer(
    ElementType elementType,  // UINT, USHORT
    uint32_t indexCount,
    BufferUsage usage        // STATIC, DYNAMIC, STREAM
);

void updateIndexBuffer(
    Handle<HwIndexBuffer> ibh,
    BufferDescriptor&& data,
    uint32_t byteOffset
);

void destroyIndexBuffer(Handle<HwIndexBuffer> ibh);
```

---

### Texture (纹理)

```cpp
Handle<HwTexture> createTexture(
    SamplerType target,       // SAMPLER_2D, SAMPLER_CUBEMAP
    uint8_t levels,           // mipmap 层级
    TextureFormat format,     // RGBA8, RGB16F, DEPTH24
    uint8_t samples,          // MSAA 采样数
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    TextureUsage usage        // COLOR_ATTACHMENT, DEPTH_ATTACHMENT
);

void updateTexture(
    Handle<HwTexture> th,
    uint8_t level,
    PixelBufferDescriptor&& data
);

void destroyTexture(Handle<HwTexture> th);
```

**TextureFormat 常用值**:
```cpp
enum class TextureFormat : uint16_t {
    RGBA8,          // 标准颜色纹理
    RGB16F,         // HDR 纹理
    DEPTH24,        // 深度纹理
    DEPTH32F,       // 高精度深度
    RGBA16F,        // HDR 颜色
    R8,             // 单通道
    // ...
};
```

---

### Program (着色器程序)

```cpp
Handle<HwProgram> createProgram(Program&& program);
void destroyProgram(Handle<HwProgram> ph);
```

**Program 结构**:
```cpp
struct Program {
    Shader vertexShader;      // 顶点着色器
    Shader fragmentShader;    // 片段着色器
    UniformBlockInfo uniformBlocks;
    SamplerGroupInfo samplerGroups;
};
```

**各后端编译差异**:
- **OpenGL**: GLSL → `glCompileShader()` + `glLinkProgram()`
- **Vulkan**: SPIRV → `vkCreateShaderModule()`
- **Metal**: MSL → `[device newLibraryWithSource:]`

---

### RenderTarget (渲染目标)

```cpp
Handle<HwRenderTarget> createRenderTarget(
    TargetBufferFlags targets,  // COLOR0|DEPTH|STENCIL
    uint32_t width,
    uint32_t height,
    uint8_t samples,
    TargetBufferInfo color,     // 颜色附件
    TargetBufferInfo depth,     // 深度附件
    TargetBufferInfo stencil    // 模板附件
);

void destroyRenderTarget(Handle<HwRenderTarget> rth);
```

---

## 渲染命令 API

### 帧控制

```cpp
void beginFrame(
    int64_t monotonic_clock_ns,
    uint32_t frameId
);

void endFrame(uint32_t frameId);

void flush(int dummy = 0);  // 提交命令到渲染线程

void finish(int dummy = 0); // 等待渲染完成
```

**调用时序**:
```
beginFrame()
  ├─> setRenderTarget()
  ├─> draw() (多次)
  ├─> draw()
  └─> ...
endFrame()
flush()  // 提交到渲染线程
```

---

### 绘制命令

```cpp
void draw(
    PipelineState pipelineState,
    RenderPrimitive primitive
);
```

**PipelineState** (渲染管线状态):
```cpp
struct PipelineState {
    Handle<HwProgram> program;
    RasterState rasterState;
    PolygonOffset polygonOffset;
    // ...
};

struct RasterState {
    CullingMode culling;           // BACK, FRONT, NONE
    BlendFunction blending;
    DepthFunc depthFunc;
    bool colorWrite;
    bool depthWrite;
    // ...
};
```

**RenderPrimitive** (渲染图元):
```cpp
struct RenderPrimitive {
    Handle<HwVertexBuffer> vertexBuffer;
    Handle<HwIndexBuffer> indexBuffer;
    PrimitiveType type;            // TRIANGLES, LINES, POINTS
    uint32_t offset;               // 起始索引
    uint32_t count;                // 索引数量
};
```

**各后端实现**:
- **OpenGL**: `glDrawElements()` / `glDrawArrays()`
- **Vulkan**: `vkCmdDrawIndexed()` / `vkCmdDraw()`
- **Metal**: `[encoder drawIndexedPrimitives:]`

---

### 状态设置

```cpp
void setViewport(Viewport viewport);

struct Viewport {
    int32_t left;
    int32_t bottom;
    uint32_t width;
    uint32_t height;
};

void setScissor(Viewport scissor);

void setRenderTarget(
    Handle<HwRenderTarget> rth,
    uint8_t level
);
```

---

### 资源绑定

```cpp
void bindUniformBuffer(
    size_t index,
    Handle<HwBufferObject> ubh
);

void bindSamplers(
    size_t index,
    Handle<HwSamplerGroup> sbh
);

void setVertexBufferObject(
    Handle<HwVertexBuffer> vbh,
    uint32_t index,
    Handle<HwBufferObject> boh
);
```

---

## 统一抽象的挑战

### 挑战 1: 状态管理差异

**OpenGL**: 隐式状态机
```cpp
// OpenGL 需要绑定当前对象
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
```

**Vulkan**: 显式命令缓冲
```cpp
// Vulkan 直接指定对象
vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vbo, offsets);
```

**Filament 的解决**:
- Driver 接口直接传递 Handle，不依赖全局状态
- OpenGLDriver 内部维护状态缓存

---

### 挑战 2: 同步模型差异

**OpenGL**: 隐式同步，API 调用阻塞
```cpp
glTexImage2D(...);  // 立即生效（可能阻塞）
```

**Vulkan/Metal**: 显式同步，需要 Fence/Semaphore
```cpp
vkQueueSubmit(...);
vkWaitForFences(...);  // 显式等待
```

**Filament 的解决**:
- CommandStream 统一异步执行
- 提供 `flush()` 和 `finish()` 控制同步点

---

### 挑战 3: 资源生命周期

**OpenGL**: 可以立即删除（引擎计数）
```cpp
glDeleteBuffers(1, &vbo);  // 立即或延迟删除
```

**Vulkan**: 必须等待 GPU 完成
```cpp
vkDestroyBuffer(device, buffer, nullptr);  // 必须确保未使用
```

**Filament 的解决**:
- 使用帧延迟删除 (Deferred Destruction)
- 等待 N 帧后才真正销毁资源

---

## 类型安全的 Handle 设计

### Handle 模板

```cpp
template<typename T>
struct Handle {
    using HandleId = uint32_t;
    HandleId id;

    explicit Handle(HandleId id) noexcept : id(id) {}
    Handle() noexcept : id(0) {}

    explicit operator bool() const noexcept { return id != 0; }
};
```

### 类型标签

```cpp
// 不同类型的 Handle 使用不同的标签
struct HwVertexBufferTag {};
struct HwIndexBufferTag {};
struct HwTextureTag {};
struct HwProgramTag {};

using HwVertexBuffer = HwVertexBufferTag;
using HwIndexBuffer = HwIndexBufferTag;
using HwTexture = HwTextureTag;
using HwProgram = HwProgramTag;
```

### 类型安全保证

```cpp
// ✅ 正确
Handle<HwVertexBuffer> vb = driver.createVertexBuffer(...);
driver.updateVertexBuffer(vb, ...);

// ❌ 编译错误: 类型不匹配
Handle<HwTexture> tex = driver.createTexture(...);
driver.updateVertexBuffer(tex, ...);  // ERROR!
```

---

## Driver 工厂模式

### 创建 Driver

```cpp
// filament/backend/src/Backend.cpp
Driver* create(
    Backend backend,
    Platform* platform,
    void* sharedContext
) {
    switch (backend) {
        case Backend::OPENGL:
            return new OpenGLDriver(platform);
        case Backend::VULKAN:
            return new VulkanDriver(platform);
        case Backend::METAL:
            return new MetalDriver(platform);
        default:
            return nullptr;
    }
}
```

---

## 完整示例：创建和渲染三角形

```cpp
// 1. 创建顶点缓冲
float vertices[] = {
    0.0f,  0.5f, 0.0f,
   -0.5f, -0.5f, 0.0f,
    0.5f, -0.5f, 0.0f
};

AttributeArray attributes;
attributes[0] = {
    .buffer = 0,
    .offset = 0,
    .stride = 12,
    .type = ElementType::FLOAT3
};

Handle<HwVertexBuffer> vbh = driver.createVertexBuffer(
    1, 1, 3, attributes
);

driver.updateVertexBuffer(vbh, 0,
    BufferDescriptor(vertices, sizeof(vertices))
);

// 2. 创建着色器程序
Program program;
program.vertexShader = /* GLSL/SPIRV/MSL */;
program.fragmentShader = /* ... */;
Handle<HwProgram> programHandle = driver.createProgram(std::move(program));

// 3. 渲染
driver.beginFrame(timestamp, frameId);

PipelineState state;
state.program = programHandle;
state.rasterState.culling = CullingMode::BACK;

RenderPrimitive primitive;
primitive.vertexBuffer = vbh;
primitive.type = PrimitiveType::TRIANGLES;
primitive.count = 3;

driver.draw(state, primitive);

driver.endFrame(frameId);
driver.flush();
```

---

## 相关文档

- **[01-architecture-overview.md](01-architecture-overview.md)**: Backend 架构总览
- **[03-command-stream.md](03-command-stream.md)**: 命令流机制
- **[04-resource-handles.md](04-resource-handles.md)**: Handle 详解
- **[05-opengl-backend.md](05-opengl-backend.md)**: OpenGL 实现

**上层调用**:
- `../engine/03-resource-management.md`: Engine 如何使用 Driver

---

## 总结

Driver 抽象层通过**统一的 API**、**类型安全的 Handle**、**异步命令流**，成功抽象了 OpenGL/Vulkan/Metal 的差异，为上层 Engine 提供了简洁、高性能的渲染接口。
