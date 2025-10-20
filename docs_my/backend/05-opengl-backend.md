# OpenGL Backend 实现详解

本文档详细讲解 Filament OpenGLDriver 的实现，包括 OpenGL 状态管理、VAO/VBO 管理、扩展支持、以及 OpenGL 和 OpenGL ES 的兼容性处理。

---

## OpenGL Backend 概述

**文件位置**: `filament/backend/src/opengl/OpenGLDriver.h`

OpenGLDriver 是 Filament 在 OpenGL/OpenGL ES 平台上的 Driver 实现，需要兼容：
- **OpenGL 4.1+** (桌面)
- **OpenGL ES 3.0+** (移动)
- **WebGL 2.0** (Web)

### 设计挑战

1. **状态机模型**: OpenGL 是全局状态机，需要仔细管理状态
2. **扩展兼容**: 不同平台和驱动的扩展支持不同
3. **性能优化**: 避免不必要的状态切换和 API 调用
4. **ES/Core 差异**: OpenGL ES 和 Core Profile 有细微差异

---

## OpenGLDriver 类结构

### 类定义

```cpp
class OpenGLDriver final : public Driver {
public:
    // 构造和初始化
    explicit OpenGLDriver(Platform* platform) noexcept;
    ~OpenGLDriver() noexcept override;

    // Backend 能力查询
    ShaderModel getShaderModel() const noexcept override;
    ShaderLanguage getShaderLanguage() const noexcept override;

    // 资源创建 (实现 Driver 接口)
    Handle<HwVertexBuffer> createVertexBuffer(...) override;
    Handle<HwIndexBuffer> createIndexBuffer(...) override;
    Handle<HwTexture> createTexture(...) override;
    Handle<HwProgram> createProgram(...) override;
    Handle<HwRenderTarget> createRenderTarget(...) override;

    // 渲染命令
    void beginFrame(...) override;
    void endFrame(...) override;
    void draw(PipelineState state, RenderPrimitive primitive) override;

    // 资源更新
    void updateVertexBuffer(...) override;
    void updateIndexBuffer(...) override;
    void updateTexture(...) override;

    // 资源销毁
    void destroyVertexBuffer(Handle<HwVertexBuffer> vbh) override;
    void destroyIndexBuffer(Handle<HwIndexBuffer> ibh) override;
    void destroyTexture(Handle<HwTexture> th) override;
    // ...

private:
    // OpenGL 上下文
    OpenGLContext& mContext;

    // 状态缓存
    OpenGLStateCache mStateCache;

    // 资源分配器
    HandleAllocator<GLVertexBuffer> mVertexBufferAllocator;
    HandleAllocator<GLIndexBuffer> mIndexBufferAllocator;
    HandleAllocator<GLTexture> mTextureAllocator;
    HandleAllocator<GLProgram> mProgramAllocator;
    HandleAllocator<GLRenderTarget> mRenderTargetAllocator;

    // 默认资源
    GLuint mDefaultVAO;
    GLuint mDefaultFBO;

    // 辅助方法
    void bindTexture(GLuint unit, GLuint target, GLuint texture);
    void bindBuffer(GLenum target, GLuint buffer);
    void useProgram(GLuint program);
};
```

---

## OpenGL 资源结构

### GLVertexBuffer

```cpp
struct GLVertexBuffer {
    struct {
        GLuint vbo;                  // Vertex Buffer Object
        uint8_t age;                 // 年龄标记（用于缓存）
    } gl;

    uint8_t bufferCount;             // 缓冲区数量
    uint32_t vertexCount;            // 顶点数量
    AttributeArray attributes;       // 顶点属性

    // 构造
    GLVertexBuffer(uint8_t bufferCount, uint8_t attributeCount,
                   uint32_t vertexCount, AttributeArray attributes)
        : bufferCount(bufferCount)
        , vertexCount(vertexCount)
        , attributes(attributes)
    {
        gl.vbo = 0;
        gl.age = 0;
    }
};

// 创建实现
Handle<HwVertexBuffer> OpenGLDriver::createVertexBuffer(
    uint8_t bufferCount,
    uint8_t attributeCount,
    uint32_t vertexCount,
    AttributeArray attributes
) {
    GLVertexBuffer* vb = new GLVertexBuffer(
        bufferCount, attributeCount, vertexCount, attributes
    );

    // 创建 OpenGL VBO
    glGenBuffers(1, &vb->gl.vbo);

    return mVertexBufferAllocator.allocate<HwVertexBuffer>(vb);
}
```

### GLIndexBuffer

```cpp
struct GLIndexBuffer {
    struct {
        GLuint ibo;                  // Index Buffer Object
        uint8_t age;
    } gl;

    ElementType elementType;         // UINT, USHORT
    uint32_t indexCount;
    GLenum glType;                   // GL_UNSIGNED_INT, GL_UNSIGNED_SHORT

    GLIndexBuffer(ElementType elementType, uint32_t indexCount)
        : elementType(elementType)
        , indexCount(indexCount)
    {
        gl.ibo = 0;
        gl.age = 0;

        // 转换为 OpenGL 类型
        switch (elementType) {
            case ElementType::UINT:
                glType = GL_UNSIGNED_INT;
                break;
            case ElementType::USHORT:
                glType = GL_UNSIGNED_SHORT;
                break;
        }
    }
};

// 创建实现
Handle<HwIndexBuffer> OpenGLDriver::createIndexBuffer(
    ElementType elementType,
    uint32_t indexCount,
    BufferUsage usage
) {
    GLIndexBuffer* ib = new GLIndexBuffer(elementType, indexCount);

    glGenBuffers(1, &ib->gl.ibo);

    return mIndexBufferAllocator.allocate<HwIndexBuffer>(ib);
}
```

### GLTexture

```cpp
struct GLTexture {
    struct {
        GLuint id;                   // OpenGL 纹理 ID
        GLenum target;               // GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP 等
        GLenum internalFormat;       // GL_RGBA8, GL_RGB16F 等
        uint8_t age;
    } gl;

    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint8_t levels;                  // mipmap 层级
    uint8_t samples;                 // MSAA 采样数
    TextureFormat format;
    SamplerType target;
    TextureUsage usage;

    GLTexture(SamplerType target, uint8_t levels, TextureFormat format,
              uint8_t samples, uint32_t width, uint32_t height, uint32_t depth,
              TextureUsage usage)
        : width(width), height(height), depth(depth)
        , levels(levels), samples(samples)
        , format(format), target(target), usage(usage)
    {
        gl.id = 0;
        gl.age = 0;

        // 转换为 OpenGL 类型
        gl.target = getGLTextureTarget(target);
        gl.internalFormat = getGLInternalFormat(format);
    }
};

// 创建实现
Handle<HwTexture> OpenGLDriver::createTexture(
    SamplerType target,
    uint8_t levels,
    TextureFormat format,
    uint8_t samples,
    uint32_t width, uint32_t height, uint32_t depth,
    TextureUsage usage
) {
    GLTexture* texture = new GLTexture(
        target, levels, format, samples, width, height, depth, usage
    );

    // 创建 OpenGL 纹理
    glGenTextures(1, &texture->gl.id);
    glBindTexture(texture->gl.target, texture->gl.id);

    // 分配存储（使用 glTexStorage2D/3D）
    switch (texture->gl.target) {
        case GL_TEXTURE_2D:
            glTexStorage2D(GL_TEXTURE_2D, levels,
                          texture->gl.internalFormat, width, height);
            break;
        case GL_TEXTURE_CUBE_MAP:
            glTexStorage2D(GL_TEXTURE_CUBE_MAP, levels,
                          texture->gl.internalFormat, width, height);
            break;
        case GL_TEXTURE_2D_ARRAY:
            glTexStorage3D(GL_TEXTURE_2D_ARRAY, levels,
                          texture->gl.internalFormat, width, height, depth);
            break;
        // ...
    }

    return mTextureAllocator.allocate<HwTexture>(texture);
}
```

### GLProgram

```cpp
struct GLProgram {
    struct {
        GLuint program;              // OpenGL 程序对象
        GLuint shaders[2];           // VS 和 FS shader 对象
        uint8_t age;
    } gl;

    UniformBlockInfo uniformBlocks;  // Uniform 块信息
    SamplerGroupInfo samplerGroups;  // 采样器组信息

    GLProgram() {
        gl.program = 0;
        gl.shaders[0] = 0;
        gl.shaders[1] = 0;
        gl.age = 0;
    }
};

// 创建实现
Handle<HwProgram> OpenGLDriver::createProgram(Program&& program) {
    GLProgram* glProgram = new GLProgram();

    // 创建 OpenGL 程序
    glProgram->gl.program = glCreateProgram();

    // 编译顶点着色器
    glProgram->gl.shaders[0] = compileShader(
        GL_VERTEX_SHADER,
        program.vertexShader.source,
        program.vertexShader.sourceLength
    );

    // 编译片段着色器
    glProgram->gl.shaders[1] = compileShader(
        GL_FRAGMENT_SHADER,
        program.fragmentShader.source,
        program.fragmentShader.sourceLength
    );

    // 链接程序
    glAttachShader(glProgram->gl.program, glProgram->gl.shaders[0]);
    glAttachShader(glProgram->gl.program, glProgram->gl.shaders[1]);
    glLinkProgram(glProgram->gl.program);

    // 检查链接状态
    GLint status;
    glGetProgramiv(glProgram->gl.program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        GLchar infoLog[512];
        glGetProgramInfoLog(glProgram->gl.program, 512, nullptr, infoLog);
        LOG(ERROR) << "Program link failed: " << infoLog;
    }

    // 保存 uniform 和 sampler 信息
    glProgram->uniformBlocks = std::move(program.uniformBlocks);
    glProgram->samplerGroups = std::move(program.samplerGroups);

    return mProgramAllocator.allocate<HwProgram>(glProgram);
}

// Shader 编译辅助函数
GLuint OpenGLDriver::compileShader(GLenum type, const char* source, size_t length) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    // 检查编译状态
    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLchar infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        LOG(ERROR) << "Shader compilation failed: " << infoLog;
    }

    return shader;
}
```

---

## OpenGL 状态管理

### OpenGLStateCache

为了避免不必要的 OpenGL 状态切换，OpenGLDriver 维护了一个状态缓存：

```cpp
class OpenGLStateCache {
public:
    // 纹理绑定
    void bindTexture(GLuint unit, GLenum target, GLuint texture) {
        if (mTextureUnits[unit].target != target ||
            mTextureUnits[unit].texture != texture) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(target, texture);
            mTextureUnits[unit].target = target;
            mTextureUnits[unit].texture = texture;
        }
    }

    // 缓冲区绑定
    void bindBuffer(GLenum target, GLuint buffer) {
        GLuint* cache = getBufferCache(target);
        if (*cache != buffer) {
            glBindBuffer(target, buffer);
            *cache = buffer;
        }
    }

    // 程序使用
    void useProgram(GLuint program) {
        if (mCurrentProgram != program) {
            glUseProgram(program);
            mCurrentProgram = program;
        }
    }

    // VAO 绑定
    void bindVertexArray(GLuint vao) {
        if (mCurrentVAO != vao) {
            glBindVertexArray(vao);
            mCurrentVAO = vao;
        }
    }

    // 帧缓冲绑定
    void bindFramebuffer(GLenum target, GLuint fbo) {
        if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) {
            if (mDrawFBO != fbo) {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
                mDrawFBO = fbo;
            }
        }
        if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) {
            if (mReadFBO != fbo) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
                mReadFBO = fbo;
            }
        }
    }

    // 重置所有缓存
    void reset() {
        for (auto& unit : mTextureUnits) {
            unit.target = 0;
            unit.texture = 0;
        }
        mArrayBuffer = 0;
        mElementArrayBuffer = 0;
        mCurrentProgram = 0;
        mCurrentVAO = 0;
        mDrawFBO = 0;
        mReadFBO = 0;
    }

private:
    struct TextureUnit {
        GLenum target = 0;
        GLuint texture = 0;
    };

    std::array<TextureUnit, MAX_TEXTURE_UNITS> mTextureUnits;
    GLuint mArrayBuffer = 0;
    GLuint mElementArrayBuffer = 0;
    GLuint mCurrentProgram = 0;
    GLuint mCurrentVAO = 0;
    GLuint mDrawFBO = 0;
    GLuint mReadFBO = 0;

    GLuint* getBufferCache(GLenum target) {
        switch (target) {
            case GL_ARRAY_BUFFER:
                return &mArrayBuffer;
            case GL_ELEMENT_ARRAY_BUFFER:
                return &mElementArrayBuffer;
            default:
                return nullptr;
        }
    }
};
```

**性能优势**:
```cpp
// ❌ 无缓存：每次都调用 OpenGL
glBindTexture(GL_TEXTURE_2D, tex1);  // 实际调用
glBindTexture(GL_TEXTURE_2D, tex1);  // 重复调用！浪费
glBindTexture(GL_TEXTURE_2D, tex2);  // 实际调用

// ✅ 有缓存：避免重复调用
stateCache.bindTexture(0, GL_TEXTURE_2D, tex1);  // 实际调用
stateCache.bindTexture(0, GL_TEXTURE_2D, tex1);  // 跳过！
stateCache.bindTexture(0, GL_TEXTURE_2D, tex2);  // 实际调用
```

---

## VAO (Vertex Array Object) 管理

### VAO 的作用

VAO 保存顶点属性配置，避免每次绘制都重新设置：

```cpp
// ❌ 无 VAO：每次 draw 都需要设置
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, 0);  // position
glEnableVertexAttribArray(0);
glDrawArrays(GL_TRIANGLES, 0, 3);

// ✅ 使用 VAO：一次设置，多次使用
// 设置阶段
glBindVertexArray(vao);
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, 0);
glEnableVertexAttribArray(0);

// 绘制阶段（多次）
glBindVertexArray(vao);
glDrawArrays(GL_TRIANGLES, 0, 3);  // 属性配置已保存在 VAO
```

### Filament 的 VAO 管理

```cpp
struct GLVertexBufferInfo {
    Handle<HwVertexBuffer> vertexBuffer;
    AttributeArray attributes;

    bool operator==(const GLVertexBufferInfo& rhs) const {
        return vertexBuffer == rhs.vertexBuffer &&
               attributes == rhs.attributes;
    }
};

// VAO 缓存：避免重复创建
std::unordered_map<GLVertexBufferInfo, GLuint> mVAOCache;

GLuint OpenGLDriver::getOrCreateVAO(const GLVertexBufferInfo& info) {
    auto it = mVAOCache.find(info);
    if (it != mVAOCache.end()) {
        return it->second;  // 缓存命中
    }

    // 创建新 VAO
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    // 设置顶点属性
    GLVertexBuffer* vb = mVertexBufferAllocator.handle_cast(info.vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, vb->gl.vbo);

    for (size_t i = 0; i < info.attributes.size(); i++) {
        const auto& attrib = info.attributes[i];
        if (attrib.type != ElementType::INVALID) {
            glVertexAttribPointer(
                i,                              // index
                getComponentCount(attrib.type), // size
                getGLType(attrib.type),         // type
                GL_FALSE,                       // normalized
                attrib.stride,                  // stride
                (void*)(uintptr_t)attrib.offset // offset
            );
            glEnableVertexAttribArray(i);
        }
    }

    mVAOCache[info] = vao;
    return vao;
}
```

---

## 渲染命令实现

### draw() 实现

```cpp
void OpenGLDriver::draw(PipelineState pipelineState, RenderPrimitive primitive) {
    // 1. 使用着色器程序
    GLProgram* program = mProgramAllocator.handle_cast(pipelineState.program);
    mStateCache.useProgram(program->gl.program);

    // 2. 设置光栅化状态
    applyRasterState(pipelineState.rasterState);

    // 3. 绑定 VAO
    GLVertexBufferInfo vbInfo{primitive.vertexBuffer, /* attributes */};
    GLuint vao = getOrCreateVAO(vbInfo);
    mStateCache.bindVertexArray(vao);

    // 4. 绑定索引缓冲
    if (primitive.indexBuffer) {
        GLIndexBuffer* ib = mIndexBufferAllocator.handle_cast(primitive.indexBuffer);
        mStateCache.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->gl.ibo);

        // 5. 绘制（有索引）
        glDrawElements(
            getGLPrimitiveType(primitive.type),  // GL_TRIANGLES
            primitive.count,                      // 索引数量
            ib->glType,                          // GL_UNSIGNED_INT
            (void*)(uintptr_t)(primitive.offset * getElementTypeSize(ib->elementType))
        );
    } else {
        // 5. 绘制（无索引）
        glDrawArrays(
            getGLPrimitiveType(primitive.type),
            primitive.offset,
            primitive.count
        );
    }
}

// 应用光栅化状态
void OpenGLDriver::applyRasterState(const RasterState& state) {
    // 面剔除
    if (state.culling != CullingMode::NONE) {
        glEnable(GL_CULL_FACE);
        glCullFace(state.culling == CullingMode::BACK ? GL_BACK : GL_FRONT);
    } else {
        glDisable(GL_CULL_FACE);
    }

    // 深度测试
    if (state.depthFunc != DepthFunc::ALWAYS) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(getGLCompareFunc(state.depthFunc));
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    // 深度写入
    glDepthMask(state.depthWrite ? GL_TRUE : GL_FALSE);

    // 颜色写入
    glColorMask(state.colorWrite, state.colorWrite,
                state.colorWrite, state.colorWrite);

    // 混合
    if (state.blending != BlendMode::OPAQUE) {
        glEnable(GL_BLEND);
        applyBlendFunction(state.blending);
    } else {
        glDisable(GL_BLEND);
    }
}
```

---

## 扩展支持和兼容性

### 扩展检测

```cpp
class OpenGLContext {
public:
    struct Extensions {
        bool EXT_texture_filter_anisotropic = false;
        bool EXT_texture_compression_s3tc = false;
        bool KHR_debug = false;
        bool ARB_seamless_cubemap_per_texture = false;
        // ...
    };

    void queryExtensions() {
        const char* extensions = (const char*)glGetString(GL_EXTENSIONS);

        ext.EXT_texture_filter_anisotropic =
            strstr(extensions, "GL_EXT_texture_filter_anisotropic") != nullptr;

        ext.KHR_debug =
            strstr(extensions, "GL_KHR_debug") != nullptr;

        // ...
    }

    Extensions ext;
};

// 使用扩展
void OpenGLDriver::setTextureAnisotropy(GLTexture* texture, float anisotropy) {
    if (mContext.ext.EXT_texture_filter_anisotropic) {
        glTexParameterf(texture->gl.target,
                       GL_TEXTURE_MAX_ANISOTROPY_EXT,
                       anisotropy);
    } else {
        // 不支持各向异性过滤，忽略
    }
}
```

### OpenGL ES vs Core Profile

```cpp
// TextureFormat 转换（ES 和 Core 不同）
GLenum getGLInternalFormat(TextureFormat format) {
    #if defined(GL_ES_VERSION_3_0)
        // OpenGL ES 3.0
        switch (format) {
            case TextureFormat::RGBA8:
                return GL_RGBA8;
            case TextureFormat::RGB16F:
                return GL_RGB16F;
            // ...
        }
    #else
        // OpenGL Core 4.1+
        switch (format) {
            case TextureFormat::RGBA8:
                return GL_RGBA8;
            case TextureFormat::RGB16F:
                return GL_RGB16F;
            // ...
        }
    #endif
}

// 某些功能只在 Core Profile 可用
void OpenGLDriver::enableSeamlessCubemap() {
    #if !defined(GL_ES_VERSION_3_0)
        if (mContext.ext.ARB_seamless_cubemap_per_texture) {
            glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        }
    #endif
    // OpenGL ES 不需要手动启用，默认开启
}
```

---

## 错误处理和调试

### OpenGL 错误检查

```cpp
#if FILAMENT_DEBUG
#define CHECK_GL_ERROR() checkGLError(__FILE__, __LINE__)

void checkGLError(const char* file, int line) {
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        const char* errorStr = nullptr;
        switch (error) {
            case GL_INVALID_ENUM:
                errorStr = "GL_INVALID_ENUM";
                break;
            case GL_INVALID_VALUE:
                errorStr = "GL_INVALID_VALUE";
                break;
            case GL_INVALID_OPERATION:
                errorStr = "GL_INVALID_OPERATION";
                break;
            case GL_OUT_OF_MEMORY:
                errorStr = "GL_OUT_OF_MEMORY";
                break;
            default:
                errorStr = "Unknown error";
        }
        LOG(ERROR) << "OpenGL error " << errorStr
                   << " at " << file << ":" << line;
    }
}
#else
#define CHECK_GL_ERROR() ((void)0)
#endif

// 使用
void OpenGLDriver::createTexture(...) {
    glGenTextures(1, &texture);
    CHECK_GL_ERROR();

    glBindTexture(GL_TEXTURE_2D, texture);
    CHECK_GL_ERROR();

    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 512, 512);
    CHECK_GL_ERROR();
}
```

### KHR_debug 支持

```cpp
void OpenGLDriver::setupDebugCallback() {
    if (mContext.ext.KHR_debug) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(debugCallback, nullptr);
    }
}

void APIENTRY debugCallback(
    GLenum source,
    GLenum type,
    GLuint id,
    GLenum severity,
    GLsizei length,
    const GLchar* message,
    const void* userParam
) {
    if (severity == GL_DEBUG_SEVERITY_HIGH) {
        LOG(ERROR) << "OpenGL Error: " << message;
    } else if (severity == GL_DEBUG_SEVERITY_MEDIUM) {
        LOG(WARNING) << "OpenGL Warning: " << message;
    } else {
        LOG(INFO) << "OpenGL Info: " << message;
    }
}
```

---

## 性能优化技巧

### 1. 批量状态设置

```cpp
// ❌ 低效：每个操作单独调用
glEnable(GL_DEPTH_TEST);
glEnable(GL_CULL_FACE);
glEnable(GL_BLEND);

// ✅ 高效：批量设置
glEnable(GL_DEPTH_TEST);
glEnable(GL_CULL_FACE);
glEnable(GL_BLEND);
// OpenGL 驱动会批处理这些操作
```

### 2. 避免冗余绑定

```cpp
// 通过 StateCache 自动避免
mStateCache.bindTexture(0, GL_TEXTURE_2D, tex1);
mStateCache.bindTexture(0, GL_TEXTURE_2D, tex1);  // 跳过
```

### 3. VAO 重用

```cpp
// 使用 VAO 缓存，相同配置重用同一个 VAO
GLuint vao = getOrCreateVAO(vbInfo);
```

### 4. 延迟资源删除

```cpp
// 不立即删除，延迟到安全时机
void OpenGLDriver::destroyTexture(Handle<HwTexture> th) {
    mPendingDeletion.push_back({th, mCurrentFrame + 2});
}
```

---

## 相关文档

- **[02-driver-abstraction.md](02-driver-abstraction.md)**: Driver 抽象层
- **[06-vulkan-backend.md](06-vulkan-backend.md)**: Vulkan 实现对比
- **[09-backend-comparison.md](09-backend-comparison.md)**: 后端对比

**理论基础**:
- `../graphics/03-gpu-pipeline.md`: GPU 管线
- `../graphics/04-shader-programming.md`: Shader 编程

---

## 总结

OpenGLDriver 通过**状态缓存**、**VAO 管理**、**扩展兼容**，成功地在 OpenGL/OpenGL ES 平台上实现了 Filament 的 Driver 抽象，同时保持了高性能和广泛的平台兼容性。

**核心技术**:
- ✅ 状态缓存：避免冗余 API 调用
- ✅ VAO 缓存：重用顶点属性配置
- ✅ 扩展检测：兼容不同驱动
- ✅ ES/Core 兼容：统一的代码路径
