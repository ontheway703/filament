# 资源句柄系统

本文档详细讲解 Filament Backend 的 Handle<T> 资源句柄系统，包括设计原理、生命周期管理、线程安全机制、以及与底层资源的映射关系。

---

## 为什么需要 Handle 系统？

### 直接使用指针的问题

如果直接使用指针或原始 ID 管理 GPU 资源：

```cpp
// ❌ 方案1: 使用原始 OpenGL ID
GLuint textureId;
glGenTextures(1, &textureId);
// 问题：
// 1. 不知道这个 ID 是 Texture 还是 Buffer
// 2. Vulkan/Metal 不使用 GLuint
// 3. 没有类型安全

// ❌ 方案2: 使用指针
Texture* texture = new Texture();
// 问题：
// 1. 需要管理内存生命周期
// 2. 多线程访问不安全
// 3. 不同 Backend 的 Texture 实现不同
```

### Filament 的解决方案：Handle<T>

```cpp
// ✅ Filament 方案
Handle<HwTexture> texHandle = driver.createTexture(...);
// 优势：
// 1. 类型安全：不能将 Texture Handle 传给 Buffer 接口
// 2. 平台无关：统一的抽象，不依赖 OpenGL/Vulkan
// 3. 轻量级：只是一个 uint32_t，可以高效拷贝
// 4. 无生命周期：不需要 new/delete，由 Driver 管理
```

---

## Handle 设计

### 基本定义

**文件位置**: `filament/backend/include/backend/Handle.h`

```cpp
template<typename T>
struct Handle {
    // 内部类型定义
    using HandleId = uint32_t;

    // 数据成员（只有一个 ID）
    HandleId id;

    // 构造函数
    constexpr Handle() noexcept : id(0) {}
    explicit constexpr Handle(HandleId id) noexcept : id(id) {}

    // 类型转换
    explicit constexpr operator bool() const noexcept {
        return id != 0;
    }

    // 比较运算符
    constexpr bool operator==(const Handle& rhs) const noexcept {
        return id == rhs.id;
    }

    constexpr bool operator!=(const Handle& rhs) const noexcept {
        return id != rhs.id;
    }

    constexpr bool operator<(const Handle& rhs) const noexcept {
        return id < rhs.id;
    }

    // 获取原始 ID
    constexpr HandleId getId() const noexcept {
        return id;
    }

    // 清空句柄
    constexpr void clear() noexcept {
        id = 0;
    }
};
```

### 类型标签 (Type Tags)

通过空结构体实现类型安全：

```cpp
// 类型标签（只是标记，没有实际数据）
struct HwVertexBufferTag {};
struct HwIndexBufferTag {};
struct HwTextureTag {};
struct HwProgramTag {};
struct HwRenderTargetTag {};
struct HwSamplerGroupTag {};
struct HwBufferObjectTag {};
struct HwRenderPrimitiveTag {};
struct HwFenceTag {};
struct HwSwapChainTag {};
struct HwStreamTag {};
struct HwTimerQueryTag {};

// 类型别名
using HwVertexBuffer = HwVertexBufferTag;
using HwIndexBuffer = HwIndexBufferTag;
using HwTexture = HwTextureTag;
using HwProgram = HwProgramTag;
using HwRenderTarget = HwRenderTargetTag;
using HwSamplerGroup = HwSamplerGroupTag;
using HwBufferObject = HwBufferObjectTag;
using HwRenderPrimitive = HwRenderPrimitiveTag;
using HwFence = HwFenceTag;
using HwSwapChain = HwSwapChainTag;
using HwStream = HwStreamTag;
using HwTimerQuery = HwTimerQueryTag;
```

### 类型安全示例

```cpp
// 创建不同类型的句柄
Handle<HwVertexBuffer> vbHandle = driver.createVertexBuffer(...);
Handle<HwTexture> texHandle = driver.createTexture(...);

// ✅ 正确使用
driver.updateVertexBuffer(vbHandle, data);
driver.updateTexture(texHandle, pixels);

// ❌ 编译错误：类型不匹配
driver.updateVertexBuffer(texHandle, data);
// Error: cannot convert 'Handle<HwTexture>' to 'Handle<HwVertexBuffer>'

driver.updateTexture(vbHandle, pixels);
// Error: cannot convert 'Handle<HwVertexBuffer>' to 'Handle<HwTexture>'

// ✅ 判断句柄是否有效
if (texHandle) {
    // 有效句柄
}

// ✅ 句柄比较
if (texHandle == otherTexHandle) {
    // 相同的纹理
}

// ✅ 可以用于 std::map 或 std::set
std::map<Handle<HwTexture>, TextureInfo> textureMap;
std::set<Handle<HwVertexBuffer>> activeBuffers;
```

---

## Handle 到资源的映射

### HandleAllocator（句柄分配器）

Driver 使用 HandleAllocator 管理 Handle ID 和实际资源的映射：

```cpp
template<typename D, size_t P0 = 16, size_t P1 = 16>
class HandleAllocatorGL {
public:
    using HandleId = uint32_t;

    // 分配新句柄
    template<typename B, typename... ARGS>
    Handle<B> allocate(ARGS&&... args) noexcept;

    // 释放句柄
    template<typename B>
    void deallocate(Handle<B> handle) noexcept;

    // 根据句柄获取资源
    template<typename B>
    D* handle_cast(Handle<B> handle) noexcept;

    template<typename B>
    const D* handle_cast(Handle<B> handle) const noexcept;

private:
    // 内部存储（池化分配）
    Pool<D, P0, P1> mPool;
};
```

### 实际存储结构

以 OpenGLDriver 为例：

```cpp
class OpenGLDriver : public Driver {
private:
    // 每种资源类型都有自己的句柄分配器
    HandleAllocator<GLVertexBuffer> mVertexBufferAllocator;
    HandleAllocator<GLIndexBuffer> mIndexBufferAllocator;
    HandleAllocator<GLTexture> mTextureAllocator;
    HandleAllocator<GLProgram> mProgramAllocator;
    // ...

    // OpenGL 特定的资源结构
    struct GLTexture {
        GLuint id;              // OpenGL 纹理 ID
        GLenum target;          // GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP 等
        GLsizei width;
        GLsizei height;
        GLenum internalFormat;
        // ...
    };

    struct GLVertexBuffer {
        GLuint vbo;             // OpenGL VBO ID
        GLuint vao;             // OpenGL VAO ID
        uint32_t vertexCount;
        // ...
    };
};
```

### 创建和映射流程

```cpp
// 1. 用户调用
Handle<HwTexture> texHandle = driver.createTexture(
    SamplerType::SAMPLER_2D, 1, TextureFormat::RGBA8,
    1, 512, 512, 1
);

// 2. Driver 实现
Handle<HwTexture> OpenGLDriver::createTexture(...) {
    // 2.1 创建 OpenGL 纹理
    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 512, 512);

    // 2.2 分配句柄并存储映射
    Handle<HwTexture> handle = mTextureAllocator.allocate<HwTexture>(
        textureId,             // OpenGL ID
        GL_TEXTURE_2D,         // target
        512,                   // width
        512,                   // height
        GL_RGBA8               // internal format
    );

    // handle.id = 42 (假设)
    // 内部映射：42 -> GLTexture{id=123, target=GL_TEXTURE_2D, ...}

    return handle;
}

// 3. 后续使用句柄
void OpenGLDriver::updateTexture(Handle<HwTexture> th, ...) {
    // 3.1 根据句柄获取资源
    GLTexture* texture = mTextureAllocator.handle_cast(th);

    // 3.2 使用 OpenGL 资源
    glBindTexture(texture->target, texture->id);
    glTexSubImage2D(texture->target, 0, ...);
}
```

---

## 句柄池化分配

### Pool 数据结构

HandleAllocator 使用池化分配避免内存碎片：

```cpp
template<typename T, size_t GROWTH = 64>
class Pool {
public:
    // 分配对象
    template<typename... ARGS>
    uint32_t allocate(ARGS&&... args) {
        if (mFreeList.empty()) {
            grow();
        }

        uint32_t index = mFreeList.back();
        mFreeList.pop_back();

        new(&mStorage[index]) T(std::forward<ARGS>(args)...);
        return index;
    }

    // 释放对象
    void deallocate(uint32_t index) {
        mStorage[index].~T();
        mFreeList.push_back(index);
    }

    // 访问对象
    T* get(uint32_t index) {
        return &mStorage[index];
    }

private:
    void grow() {
        size_t oldSize = mStorage.size();
        mStorage.resize(oldSize + GROWTH);

        for (size_t i = oldSize; i < mStorage.size(); i++) {
            mFreeList.push_back(i);
        }
    }

    std::vector<T> mStorage;       // 对象存储
    std::vector<uint32_t> mFreeList; // 空闲列表
};
```

### 内存布局

```
mStorage (连续内存)
┌──────────┬──────────┬──────────┬──────────┬──────────┐
│ Texture0 │ Texture1 │ Texture2 │ Texture3 │ Texture4 │
└──────────┴──────────┴──────────┴──────────┴──────────┘
     0          1          2          3          4       (索引 = Handle ID)

mFreeList
┌───┬───┬───┐
│ 2 │ 4 │ 1 │  (可分配的索引)
└───┴───┴───┘

分配流程：
1. allocate() -> 从 mFreeList 取出 index=1
2. new(&mStorage[1]) Texture(...)
3. 返回 Handle<HwTexture>(1)

释放流程：
1. deallocate(1) -> mStorage[1].~Texture()
2. mFreeList.push_back(1)
```

---

## 生命周期管理

### 句柄的生命周期

```cpp
// 1. 创建句柄
Handle<HwTexture> texHandle = driver.createTexture(...);
// 内部：分配 ID，创建 GPU 资源

// 2. 使用句柄
driver.updateTexture(texHandle, pixels);
driver.setTexture(0, texHandle);

// 3. 销毁句柄
driver.destroyTexture(texHandle);
// 内部：释放 GPU 资源，回收 ID

// 4. 句柄失效
texHandle.clear();  // id = 0
// 或者简单地让 Handle 超出作用域（它是值类型）
```

### 延迟销毁

由于多线程架构，资源不能立即销毁：

```cpp
void OpenGLDriver::destroyTexture(Handle<HwTexture> th) {
    // ❌ 不能立即销毁！渲染线程可能还在使用

    // ✅ 记录到销毁队列，延迟 N 帧销毁
    mDestroyQueue.emplace_back(th, mCurrentFrame + FRAME_DELAY);
}

void OpenGLDriver::beginFrame() {
    mCurrentFrame++;

    // 清理过期资源
    while (!mDestroyQueue.empty()) {
        auto& [handle, frameId] = mDestroyQueue.front();

        if (frameId > mCurrentFrame) {
            break;  // 还未到销毁时间
        }

        // 现在可以安全销毁
        GLTexture* texture = mTextureAllocator.handle_cast(handle);
        glDeleteTextures(1, &texture->id);
        mTextureAllocator.deallocate(handle);

        mDestroyQueue.pop_front();
    }
}
```

**延迟销毁的原因**:
```
帧 N: 主线程调用 destroyTexture(tex)
      ├─> 命令录制到 CommandStream
      │
帧 N+1: 渲染线程执行销毁命令
      ├─> 但 GPU 可能还在使用该纹理！
      │
帧 N+2: GPU 完成使用
      └─> 现在可以安全删除

因此延迟至少 2-3 帧
```

---

## 线程安全

### Handle 本身是线程安全的

```cpp
// Handle 只是一个 uint32_t，拷贝是原子的
Handle<HwTexture> texHandle = driver.createTexture(...);

// ✅ 可以安全地在线程间传递 Handle
std::thread thread([texHandle]() {
    // 可以使用 texHandle
});
```

### 资源访问需要同步

```cpp
// ❌ 不安全：多线程访问同一资源
std::thread t1([&driver, texHandle]() {
    driver.updateTexture(texHandle, pixels1);
});

std::thread t2([&driver, texHandle]() {
    driver.updateTexture(texHandle, pixels2);
});

// ✅ 安全：通过 CommandStream 串行化
// 所有 driver 调用都会通过 CommandStream，
// 在渲染线程按顺序执行
```

### HandleAllocator 的线程安全

```cpp
class HandleAllocator {
private:
    std::mutex mLock;  // 保护内部数据结构

public:
    template<typename... ARGS>
    Handle<T> allocate(ARGS&&... args) {
        std::lock_guard<std::mutex> lock(mLock);
        // 安全地分配句柄
    }

    void deallocate(Handle<T> handle) {
        std::lock_guard<std::mutex> lock(mLock);
        // 安全地释放句柄
    }
};
```

---

## 不同 Backend 的实现差异

### OpenGL Backend

```cpp
struct GLTexture {
    GLuint id;              // OpenGL 纹理 ID (uint32_t)
    GLenum target;
    GLsizei width;
    GLsizei height;
};

Handle<HwTexture> -> uint32_t -> GLTexture*
```

### Vulkan Backend

```cpp
struct VulkanTexture {
    VkImage image;          // Vulkan 图像句柄
    VkImageView imageView;  // 图像视图
    VkDeviceMemory memory;  // 设备内存
    uint32_t width;
    uint32_t height;
};

Handle<HwTexture> -> uint32_t -> VulkanTexture*
```

### Metal Backend

```cpp
struct MetalTexture {
    id<MTLTexture> texture;  // Metal 纹理对象 (Objective-C 对象)
    uint32_t width;
    uint32_t height;
};

Handle<HwTexture> -> uint32_t -> MetalTexture*
```

**关键点**: 无论底层是什么，上层都使用统一的 `Handle<HwTexture>`

---

## Handle 的高级用法

### 在容器中使用

```cpp
// std::map
std::map<Handle<HwTexture>, std::string> textureNames;
textureNames[texHandle] = "diffuse_map";

// std::unordered_map (需要自定义哈希)
namespace std {
    template<typename T>
    struct hash<Handle<T>> {
        size_t operator()(const Handle<T>& h) const {
            return std::hash<uint32_t>()(h.id);
        }
    };
}

std::unordered_map<Handle<HwTexture>, TextureInfo> textureCache;

// std::vector
std::vector<Handle<HwTexture>> activeTextures;
activeTextures.push_back(texHandle);
```

### 序列化

```cpp
// 保存句柄
void save(Handle<HwTexture> texHandle, std::ostream& out) {
    uint32_t id = texHandle.getId();
    out.write(reinterpret_cast<const char*>(&id), sizeof(id));
}

// 加载句柄（需要重新映射）
Handle<HwTexture> load(std::istream& in, TextureRecreator& recreator) {
    uint32_t oldId;
    in.read(reinterpret_cast<char*>(&oldId), sizeof(oldId));

    // 重新创建纹理，获得新的句柄
    return recreator.recreateTexture(oldId);
}
```

### 弱引用模式

```cpp
// 有时需要引用句柄但不延长生命周期
class TextureUser {
public:
    void setTexture(Handle<HwTexture> tex) {
        mTextureHandle = tex;
    }

    void render(Driver& driver) {
        if (mTextureHandle) {  // 检查是否有效
            driver.setTexture(0, mTextureHandle);
        }
    }

private:
    Handle<HwTexture> mTextureHandle;  // 弱引用
};

// 注意：如果纹理被销毁，句柄会失效
// 需要外部保证生命周期正确性
```

---

## 调试和验证

### 句柄验证

```cpp
class HandleAllocator {
public:
    bool isValid(Handle<T> handle) const {
        uint32_t id = handle.getId();
        return id > 0 && id < mStorage.size() && !isFree(id);
    }

private:
    bool isFree(uint32_t id) const {
        return std::find(mFreeList.begin(), mFreeList.end(), id) != mFreeList.end();
    }
};

// 使用
if (!allocator.isValid(texHandle)) {
    LOG(ERROR) << "Invalid texture handle: " << texHandle.getId();
}
```

### Debug 模式的额外检查

```cpp
#if FILAMENT_DEBUG
void OpenGLDriver::updateTexture(Handle<HwTexture> th, ...) {
    // 检查句柄有效性
    assert(mTextureAllocator.isValid(th));

    GLTexture* texture = mTextureAllocator.handle_cast(th);

    // 检查 OpenGL 对象有效性
    assert(glIsTexture(texture->id));

    // 实际更新
    glBindTexture(texture->target, texture->id);
    // ...
}
#endif
```

### 句柄泄漏检测

```cpp
class HandleAllocator {
public:
    ~HandleAllocator() {
        #if FILAMENT_DEBUG
        size_t leakedCount = mStorage.size() - mFreeList.size();
        if (leakedCount > 0) {
            LOG(WARNING) << "Leaked " << leakedCount << " handles!";
        }
        #endif
    }
};
```

---

## 性能考虑

### 内存占用

```cpp
// Handle 本身非常小
sizeof(Handle<HwTexture>) == sizeof(uint32_t) == 4 字节

// 对比原始指针
sizeof(Texture*) == 8 字节 (64位系统)

// Handle 更节省内存！
```

### 访问性能

```cpp
// 通过 Handle 访问资源
GLTexture* texture = mTextureAllocator.handle_cast(texHandle);
// 实际操作：
// 1. 获取 ID: texHandle.id (直接读取)
// 2. 数组访问: mStorage[id] (一次间接访问)
// 3. 返回指针

// 性能：~5-10ns (现代 CPU)
// 几乎等同于直接指针访问
```

### 缓存友好性

```cpp
// Pool 使用连续内存，缓存友好
struct GLTexture {
    GLuint id;         // 4 字节
    GLenum target;     // 4 字节
    GLsizei width;     // 4 字节
    GLsizei height;    // 4 字节
};  // 16 字节，对齐良好

// 遍历所有纹理
for (uint32_t i = 0; i < textureCount; i++) {
    GLTexture* tex = pool.get(i);
    // 连续访问，缓存命中率高
}
```

---

## 与其他引擎的对比

### Unity (EntityComponentSystem)

```cpp
// Unity DOTS 使用 Entity (类似 Handle)
struct Entity {
    int index;
    int version;  // 增加版本号检测悬空引用
};
```

### Unreal Engine

```cpp
// Unreal 使用智能指针
TSharedPtr<FTexture> Texture;
// 优点：自动管理生命周期
// 缺点：性能开销（引用计数）
```

### Filament 的选择

```cpp
// Filament 使用轻量级 Handle
Handle<HwTexture> texHandle;
// 优点：零开销抽象，高性能
// 缺点：需要手动管理生命周期
```

---

## 相关文档

- **[01-architecture-overview.md](01-architecture-overview.md)**: Backend 架构总览
- **[02-driver-abstraction.md](02-driver-abstraction.md)**: Driver 接口
- **[03-command-stream.md](03-command-stream.md)**: 命令流机制

**上层使用**:
- `../engine/03-resource-management.md`: Engine 资源管理

**实现细节**:
- `../graphics/09-gpu-optimization.md`: 性能优化

---

## 总结

Filament 的 Handle 系统通过**类型安全**、**轻量级设计**、**池化分配**，实现了高性能、跨平台的资源管理。Handle 是连接上层 Engine 和底层 GPU 资源的关键抽象，既简单又强大。

**核心优势**:
- ✅ 类型安全：编译期检查，避免类型错误
- ✅ 轻量级：只有 4 字节，可高效拷贝
- ✅ 平台无关：统一抽象，不依赖具体 API
- ✅ 高性能：零开销抽象，接近原生指针性能
- ✅ 内存友好：池化分配，避免碎片
