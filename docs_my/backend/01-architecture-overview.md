# Backend 架构总览

本文档详细讲解 Filament Backend 系统的整体架构设计，包括核心组件、设计目标和与 Engine 层的接口关系。

---

## 什么是 Filament Backend？

**Filament Backend** 是 Filament 渲染引擎的底层渲染抽象层，负责将上层的渲染请求转换为底层图形 API (OpenGL/Vulkan/Metal) 的调用。

### 设计目标

1. **跨平台抽象**: 统一 OpenGL/Vulkan/Metal/WebGL 的接口差异
2. **高性能**: 多线程架构，最小化开销
3. **资源管理**: 统一的资源生命周期管理
4. **易用性**: 为上层 Engine 提供简洁的 API

---

## 整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                    Filament Engine (上层)                         │
│                                                                   │
│  Scene, View, Renderer, Material, VertexBuffer, Texture...       │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│                  Backend API (抽象接口层)                          │
│                                                                   │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                    Driver Interface                        │  │
│  │  - createVertexBuffer() / createTexture()                 │  │
│  │  - beginFrame() / draw() / endFrame()                     │  │
│  │  - 统一的资源创建、渲染命令、资源销毁接口                     │  │
│  └───────────────────────────────────────────────────────────┘  │
│                             │                                    │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                  CommandStream                             │  │
│  │  - 命令缓冲：主线程录制命令                                  │  │
│  │  - 命令执行：渲染线程执行命令                                │  │
│  │  - 线程同步：安全的跨线程通信                                │  │
│  └───────────────────────────────────────────────────────────┘  │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│              Backend Implementations (具体实现)                    │
│                                                                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │OpenGLDriver  │  │VulkanDriver  │  │ MetalDriver  │          │
│  │              │  │              │  │              │          │
│  │- 状态管理    │  │- 命令缓冲    │  │- 命令编码器  │          │
│  │- 扩展兼容    │  │- 描述符集    │  │- 资源堆      │          │
│  │- ES/Core兼容 │  │- 显式同步    │  │- Metal特性   │          │
│  └──────────────┘  └──────────────┘  └──────────────┘          │
│         │                  │                  │                  │
└─────────┼──────────────────┼──────────────────┼─────────────────┘
          ↓                  ↓                  ↓
┌─────────────────────────────────────────────────────────────────┐
│              Graphics API (底层图形库)                             │
│                                                                   │
│     OpenGL/ES          Vulkan              Metal                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 核心组件

### 1. Driver Interface (驱动接口)

**文件**: `filament/backend/include/private/backend/Driver.h`

Driver 是 Backend 的核心抽象接口，定义了所有图形操作的统一 API。

```cpp
class Driver {
public:
    // 资源创建
    virtual Handle<HwVertexBuffer> createVertexBuffer(
        uint8_t bufferCount,
        uint8_t attributeCount,
        uint32_t vertexCount,
        AttributeArray attributes) = 0;

    virtual Handle<HwIndexBuffer> createIndexBuffer(
        ElementType elementType,
        uint32_t indexCount,
        BufferUsage usage) = 0;

    virtual Handle<HwTexture> createTexture(
        SamplerType target,
        uint8_t levels,
        TextureFormat format,
        uint8_t samples,
        uint32_t width, uint32_t height, uint32_t depth,
        TextureUsage usage) = 0;

    virtual Handle<HwProgram> createProgram(Program&& program) = 0;

    // 渲染命令
    virtual void beginFrame(...) = 0;
    virtual void draw(PipelineState state, RenderPrimitive primitive) = 0;
    virtual void endFrame(...) = 0;

    // 资源更新
    virtual void updateVertexBuffer(...) = 0;
    virtual void updateIndexBuffer(...) = 0;

    // 资源销毁
    virtual void destroyVertexBuffer(Handle<HwVertexBuffer> vbh) = 0;
    virtual void destroyIndexBuffer(Handle<HwIndexBuffer> ibh) = 0;
    // ...
};
```

**关键特点**:
- 纯虚接口，各后端实现具体逻辑
- 使用 Handle<T> 管理资源
- 同步的 API 设计（实际通过 CommandStream 异步执行）

---

### 2. CommandStream (命令流)

**文件**: `filament/backend/src/CommandStream.h`

CommandStream 负责将主线程的渲染命令记录到缓冲区，然后在渲染线程执行。

**架构**:
```
主线程 (Application Thread)          渲染线程 (Render Thread)
    │                                        │
    ├─> driver.createTexture()               │
    │     ↓                                   │
    │   CommandStream::queueCommand()         │
    │     ↓                                   │
    │   [命令缓冲区]                          │
    │     │                                   │
    │     │ (写入命令和参数)                  │
    │     │                                   │
    ├─> driver.draw()                         │
    │     ↓                                   │
    │   CommandStream::queueCommand()         │
    │     │                                   │
    ├─> flush() ─────────────────────────────>│
    │                                   CommandStream::execute()
    │                                          ↓
    │                                   遍历命令缓冲区
    │                                          ↓
    │                                   调用 Driver 的实际实现
    │                                          ↓
    │                                   OpenGL/Vulkan/Metal API
```

**关键方法**:
```cpp
class CommandStream {
public:
    // 录制命令
    void queueCommand(void (Driver::*method)(...), Args... args);

    // 执行命令
    void execute();

    // 等待完成
    void wait();
};
```

**优势**:
- ✅ 主线程不阻塞，可以继续准备下一帧
- ✅ 命令批处理，减少同步开销
- ✅ 线程安全，避免竞态条件

---

### 3. Handle<T> (资源句柄)

**文件**: `filament/backend/include/backend/Handle.h`

Handle 是类型安全的资源句柄，用于引用 GPU 资源。

```cpp
template<typename T>
struct Handle {
    using HandleId = uint32_t;
    HandleId id = 0;

    explicit Handle(HandleId id) noexcept : id(id) {}
    Handle() noexcept = default;

    explicit operator bool() const noexcept { return id != 0; }
    bool operator==(const Handle& rhs) const { return id == rhs.id; }
};

// 类型别名
using HwVertexBuffer = struct HwVertexBufferTag;
using HwIndexBuffer = struct HwIndexBufferTag;
using HwTexture = struct HwTextureTag;
using HwProgram = struct HwProgramTag;

// 使用示例
Handle<HwVertexBuffer> vbh;
Handle<HwTexture> texh;
```

**特点**:
- 类型安全（不能将 VertexBuffer 句柄传给 Texture 接口）
- 轻量级（只是一个 uint32_t ID）
- 无生命周期管理（需要手动销毁）

---

### 4. Backend Implementations (后端实现)

每个图形 API 都有对应的 Driver 实现：

#### OpenGLDriver

**文件**: `filament/backend/src/opengl/OpenGLDriver.h`

```cpp
class OpenGLDriver final : public Driver {
public:
    Handle<HwVertexBuffer> createVertexBuffer(...) override;
    Handle<HwTexture> createTexture(...) override;
    void draw(PipelineState state, RenderPrimitive primitive) override;
    // ...
private:
    GLuint mDefaultVAO;
    OpenGLContext& mContext;
    GLStateCache mStateCache;  // OpenGL 状态缓存
};
```

**特点**:
- 管理 OpenGL 状态机
- 兼容 OpenGL 3.3+ 和 OpenGL ES 3.0+
- 使用 VAO 管理顶点输入状态

#### VulkanDriver

**文件**: `filament/backend/src/vulkan/VulkanDriver.h`

```cpp
class VulkanDriver final : public Driver {
public:
    Handle<HwVertexBuffer> createVertexBuffer(...) override;
    void draw(PipelineState state, RenderPrimitive primitive) override;
    // ...
private:
    VkDevice mDevice;
    VkCommandPool mCommandPool;
    VulkanDescriptorSetManager mDescriptorSetManager;
};
```

**特点**:
- 显式管理命令缓冲
- 使用描述符集绑定资源
- 需要手动同步

#### MetalDriver

**文件**: `filament/backend/src/metal/MetalDriver.h`

```cpp
class MetalDriver final : public Driver {
public:
    Handle<HwVertexBuffer> createVertexBuffer(...) override;
    void draw(PipelineState state, RenderPrimitive primitive) override;
    // ...
private:
    id<MTLDevice> mDevice;
    id<MTLCommandQueue> mCommandQueue;
    id<MTLCommandBuffer> mCurrentCommandBuffer;
};
```

**特点**:
- 使用 Metal 命令编码器
- 自动资源追踪
- Apple 平台优化

---

## 完整调用流程

### 示例：创建并渲染一个三角形

```cpp
// 1. 引擎层创建资源
VertexBuffer* vb = VertexBuffer::Builder()
    .vertexCount(3)
    .bufferCount(1)
    .attribute(VertexAttribute::POSITION, 0,
               VertexBuffer::AttributeType::FLOAT3)
    .build(*engine);

// 2. Backend API 调用 (Engine 内部调用)
FEngine* fengine = downcast(engine);
auto& driver = fengine->getDriver();

// 这会调用 CommandStream::queueCommand()
Handle<HwVertexBuffer> vbh = driver.createVertexBuffer(
    1, 1, 3, attributes);

// 3. CommandStream 录制命令
// (主线程，非阻塞)
commandStream.queueCommand(
    &Driver::createVertexBufferImpl,  // 实际方法
    1, 1, 3, attributes
);

// 4. 渲染线程执行
// commandStream.execute() 在渲染线程被调用
void execute() {
    // 遍历命令缓冲区
    while (hasCommands()) {
        Command cmd = readCommand();

        // 调用实际的 Driver 实现
        if (cmd.method == &Driver::createVertexBufferImpl) {
            // 调用 OpenGLDriver::createVertexBufferImpl()
            openglDriver->createVertexBufferImpl(...);
        }
    }
}

// 5. OpenGLDriver 实际实现
Handle<HwVertexBuffer> OpenGLDriver::createVertexBufferImpl(...) {
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    // ...
    return Handle<HwVertexBuffer>(allocateHandleId());
}
```

---

## 与 Engine 层的接口

### Engine 层看到的接口

Engine 层通过 `DriverApi` 调用 Backend:

```cpp
// filament/src/details/Engine.cpp
class FEngine {
    backend::DriverApi& getDriverApi() noexcept {
        return *mDriver;
    }
};

// 使用示例
auto& driver = engine->getDriverApi();
Handle<HwVertexBuffer> vbh = driver.createVertexBuffer(...);
driver.updateVertexBuffer(vbh, bufferData);
```

### Backend 提供的功能

| 功能类别 | Driver 方法 | 说明 |
|---------|------------|------|
| **资源创建** | `create*()` | 创建 VertexBuffer, Texture 等 |
| **资源更新** | `update*()` | 更新资源数据 |
| **资源销毁** | `destroy*()` | 销毁资源 |
| **渲染控制** | `beginFrame()`, `endFrame()` | 帧控制 |
| **绘制命令** | `draw()`, `blit()` | 实际渲染 |
| **状态设置** | `setViewport()`, `setScissor()` | 设置渲染状态 |

---

## 线程模型

```
┌─────────────────────────────────────────────────────────────────┐
│                        主线程 (UI Thread)                          │
│                                                                   │
│  App Code → Engine API → Driver API → CommandStream.queue()     │
│                                                                   │
│  [准备下一帧的数据]                                               │
└────────────────────────────┬────────────────────────────────────┘
                             │
                        flush() / commit()
                             │
                             ↓
┌─────────────────────────────────────────────────────────────────┐
│                      渲染线程 (Render Thread)                      │
│                                                                   │
│  CommandStream.execute() → Driver Impl → Graphics API            │
│                                                                   │
│  [执行上一帧的命令]                                               │
└─────────────────────────────────────────────────────────────────┘
```

**同步点**:
1. `flush()`: 提交命令到渲染线程
2. `wait()`: 等待渲染线程完成
3. Fence: 用于 CPU-GPU 同步

---

## 平台支持矩阵

| 平台 | OpenGL | Vulkan | Metal |
|------|--------|--------|-------|
| **Android** | ✅ ES 3.0+ | ✅ 1.0+ | - |
| **iOS** | ✅ ES 3.0+ | - | ✅ 2.0+ |
| **macOS** | ✅ 4.1+ | ✅ 1.0+ (MoltenVK) | ✅ 2.0+ |
| **Windows** | ✅ 4.5+ | ✅ 1.0+ | - |
| **Linux** | ✅ 4.5+ | ✅ 1.0+ | - |
| **Web** | ✅ WebGL 2.0 | - | - |

---

## 设计优势

### 1. 抽象统一
- 上层代码无需关心底层 API 差异
- 一次编写，多平台运行

### 2. 性能优化
- CommandStream 实现多线程并行
- 各后端针对平台特性优化
- 最小化状态切换和同步

### 3. 易于扩展
- 新增后端只需实现 Driver 接口
- 不影响上层 Engine 代码

### 4. 资源管理
- 统一的 Handle 系统
- 明确的生命周期管理

---

## 相关文档

- **[02-driver-abstraction.md](02-driver-abstraction.md)**: Driver 接口详解
- **[03-command-stream.md](03-command-stream.md)**: CommandStream 机制
- **[04-resource-handles.md](04-resource-handles.md)**: Handle 系统
- **[09-backend-comparison.md](09-backend-comparison.md)**: 各后端对比

**上层依赖**:
- `../engine/03-resource-management.md`: Engine 资源管理
- `../engine/07-render-loop.md`: 渲染循环

**底层依赖**:
- `../graphics/03-gpu-pipeline.md`: GPU 管线理论

---

## 总结

Filament Backend 通过 **Driver 抽象接口** + **CommandStream 多线程** + **Handle 资源管理**，实现了高性能、跨平台的渲染抽象层，是连接上层 Engine 和底层 Graphics API 的关键桥梁。

> 理解 Backend 架构，是深入 Filament 引擎的第一步！
