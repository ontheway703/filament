# Filament OpenGL ES 后端实现

## OpenGL ES 后端概述

Filament OpenGL ES 后端为 iOS 平台提供了兼容性更好的图形渲染方案，支持从 iOS 7.0+ 的设备，是 Metal 后端的重要补充。

```
OpenGL ES 后端架构:
Filament API → OpenGL Driver → EAGL → OpenGL ES → GPU
     ↓              ↓            ↓         ↓        ↓
   引擎层         驱动抽象      上下文管理   系统API   硬件
```

## 核心组件

### 1. iOS 平台适配层

**位置**: `filament/backend/src/opengl/platforms/PlatformCocoaTouchGL.mm`

```objc
class PlatformCocoaTouchGL final : public Platform {
private:
    struct PlatformCocoaTouchGLImpl* pImpl;

public:
    PlatformCocoaTouchGL();
    ~PlatformCocoaTouchGL() noexcept override;

    Driver* createDriver(void* sharedGLContext,
                        const Platform::DriverConfig& driverConfig) noexcept override;

    // SwapChain 管理
    void createSwapChain(void* nativeWindow, uint64_t flags) noexcept override;
    void createSwapChain(uint32_t width, uint32_t height, uint64_t flags) noexcept override;

    // 缓冲区操作
    void makeCurrent(SwapChain* drawSwapChain, SwapChain* readSwapChain) noexcept override;
    void commit(SwapChain* swapChain) noexcept override;

    // 外部纹理支持
    Platform::ExternalTexture* createExternalTextureStorage() noexcept override;
    void destroyExternalTexture(Platform::ExternalTexture* texture) noexcept override;
};
```

### 2. EAGL 上下文管理

```objc
struct PlatformCocoaTouchGLImpl {
    EAGLContext* mGLContext = nullptr;              // OpenGL ES 上下文
    CAEAGLLayer* mCurrentGlLayer = nullptr;         // 当前渲染层
    std::vector<CAEAGLLayer*> mHeadlessGlLayers;    // 离屏渲染层
    std::vector<EAGLContext*> mAdditionalContexts;  // 额外上下文

    // 帧缓冲区
    GLuint mDefaultFramebuffer = 0;
    GLuint mDefaultColorbuffer = 0;
    GLuint mDefaultDepthbuffer = 0;

    // 外部纹理支持
    CVOpenGLESTextureCacheRef mTextureCache = nullptr;
    CocoaTouchExternalImage::SharedGl* mExternalImageSharedGl = nullptr;

    // 外部图像结构
    struct ExternalImageCocoaTouchGL : public Platform::ExternalImage {
        CVPixelBufferRef cvBuffer;
    protected:
        ~ExternalImageCocoaTouchGL() noexcept final;
    };
};
```

### 3. 上下文创建和配置

```objc
Driver* PlatformCocoaTouchGL::createDriver(void* sharedGLContext,
                                         const Platform::DriverConfig& driverConfig) noexcept {

    // 获取共享组
    EAGLSharegroup* sharegroup = (__bridge EAGLSharegroup*) sharedGLContext;

    // 创建 OpenGL ES 3.0 上下文
    EAGLContext *context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3
                                                 sharegroup:sharegroup];

    FILAMENT_CHECK_POSTCONDITION(context) << "Unable to create OpenGL ES context.";

    // 设置当前上下文
    [EAGLContext setCurrentContext:context];
    pImpl->mGLContext = context;

    // 创建默认帧缓冲区
    [self setupDefaultFramebuffer];

    // 创建纹理缓存
    CVReturn result = CVOpenGLESTextureCacheCreate(
        kCFAllocatorDefault,
        nullptr,
        context,
        nullptr,
        &pImpl->mTextureCache);

    FILAMENT_CHECK_POSTCONDITION(result == kCVReturnSuccess)
        << "Unable to create texture cache.";

    return filament::backend::create_driver(driverConfig,
                                           static_cast<Platform*>(this));
}
```

## SwapChain 管理

### 1. CAEAGLLayer 集成

```objc
void PlatformCocoaTouchGL::createSwapChain(void* nativeWindow, uint64_t flags) noexcept {
    CAEAGLLayer* glLayer = (__bridge CAEAGLLayer*) nativeWindow;

    FILAMENT_CHECK_PRECONDITION(glLayer) << "nativeWindow must be a CAEAGLLayer.";

    // 配置 EAGL 层属性
    glLayer.opaque = YES;
    glLayer.drawableProperties = @{
        kEAGLDrawablePropertyRetainedBacking: @NO,
        kEAGLDrawablePropertyColorFormat: kEAGLColorFormatRGBA8
    };

    pImpl->mCurrentGlLayer = glLayer;
    [self setupLayerBuffers:glLayer];
}

- (void)setupLayerBuffers:(CAEAGLLayer*)layer {
    // 绑定渲染缓冲区到层
    GLuint renderbuffer;
    glGenRenderbuffers(1, &renderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);

    BOOL success = [pImpl->mGLContext renderbufferStorage:GL_RENDERBUFFER
                                             fromDrawable:layer];
    FILAMENT_CHECK_POSTCONDITION(success) << "Failed to create renderbuffer from drawable.";

    // 获取缓冲区尺寸
    GLint width, height;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);

    // 创建深度缓冲区
    GLuint depthbuffer;
    glGenRenderbuffers(1, &depthbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);

    // 创建帧缓冲区
    GLuint framebuffer;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    // 附加渲染缓冲区
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, renderbuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                             GL_RENDERBUFFER, depthbuffer);

    // 验证帧缓冲区完整性
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    FILAMENT_CHECK_POSTCONDITION(status == GL_FRAMEBUFFER_COMPLETE)
        << "Incomplete framebuffer.";

    pImpl->mDefaultFramebuffer = framebuffer;
    pImpl->mDefaultColorbuffer = renderbuffer;
    pImpl->mDefaultDepthbuffer = depthbuffer;
}
```

### 2. 缓冲区交换

```objc
void PlatformCocoaTouchGL::commit(SwapChain* swapChain) noexcept {
    if (pImpl->mCurrentGlLayer) {
        // 绑定渲染缓冲区
        glBindRenderbuffer(GL_RENDERBUFFER, pImpl->mDefaultColorbuffer);

        // 呈现渲染结果
        BOOL success = [pImpl->mGLContext presentRenderbuffer:GL_RENDERBUFFER];
        if (!success) {
            utils::slog.w << "Failed to present renderbuffer.";
        }
    }
}

void PlatformCocoaTouchGL::makeCurrent(SwapChain* drawSwapChain,
                                     SwapChain* readSwapChain) noexcept {
    // 设置当前上下文
    [EAGLContext setCurrentContext:pImpl->mGLContext];

    // 绑定默认帧缓冲区
    glBindFramebuffer(GL_FRAMEBUFFER, pImpl->mDefaultFramebuffer);

    // 设置视口
    GLint width, height;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);
    glViewport(0, 0, width, height);
}
```

## 外部纹理支持

### 1. CoreVideo 集成

**位置**: `filament/backend/src/opengl/platforms/CocoaTouchExternalImage.mm`

```objc
class CocoaTouchExternalImage {
public:
    struct SharedGl {
        CVOpenGLESTextureCacheRef textureCache;
        EAGLContext* glContext;
    };

    static void create(SharedGl* sharedGl,
                      CVPixelBufferRef pixelBuffer,
                      GLuint* outTexture,
                      GLenum* outTarget) {

        *outTarget = GL_TEXTURE_2D;

        CVOpenGLESTextureRef cvTexture = nullptr;

        // 从像素缓冲区创建 OpenGL ES 纹理
        CVReturn result = CVOpenGLESTextureCacheCreateTextureFromImage(
            kCFAllocatorDefault,
            sharedGl->textureCache,
            pixelBuffer,
            nullptr,
            GL_TEXTURE_2D,
            GL_RGBA,
            static_cast<GLsizei>(CVPixelBufferGetWidth(pixelBuffer)),
            static_cast<GLsizei>(CVPixelBufferGetHeight(pixelBuffer)),
            GL_BGRA,
            GL_UNSIGNED_BYTE,
            0,
            &cvTexture);

        if (result != kCVReturnSuccess) {
            utils::slog.e << "Failed to create texture from CVPixelBuffer: " << result;
            *outTexture = 0;
            return;
        }

        // 获取 OpenGL 纹理名称
        *outTexture = CVOpenGLESTextureGetName(cvTexture);

        // 配置纹理参数
        glBindTexture(GL_TEXTURE_2D, *outTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // 释放 CV 纹理引用
        CFRelease(cvTexture);
    }
};
```

### 2. 外部纹理生命周期

```objc
Platform::ExternalTexture* PlatformCocoaTouchGL::createExternalTextureStorage() noexcept {
    return new PlatformCocoaTouchGLImpl::ExternalImageCocoaTouchGL();
}

void PlatformCocoaTouchGL::destroyExternalTexture(Platform::ExternalTexture* texture) noexcept {
    auto* externalImage = static_cast<PlatformCocoaTouchGLImpl::ExternalImageCocoaTouchGL*>(texture);

    // 释放像素缓冲区
    if (externalImage->cvBuffer) {
        CVPixelBufferRelease(externalImage->cvBuffer);
        externalImage->cvBuffer = nullptr;
    }

    delete externalImage;
}

PlatformCocoaTouchGLImpl::ExternalImageCocoaTouchGL::~ExternalImageCocoaTouchGL() noexcept {
    if (cvBuffer) {
        CVPixelBufferRelease(cvBuffer);
    }
}
```

## OpenGL ES 优化技术

### 1. 纹理格式选择

```objc
GLenum getOptimalTextureFormat(TextureFormat format) {
    switch (format) {
        case TextureFormat::RGBA8:
            // iOS 优化：使用 BGRA 格式可能更快
            return GL_BGRA;

        case TextureFormat::RGB8:
            // iOS 不推荐 RGB，使用 RGBA 代替
            return GL_RGBA;

        case TextureFormat::DEPTH16:
            return GL_DEPTH_COMPONENT16;

        case TextureFormat::DEPTH24:
            // 检查设备支持
            if (hasExtension("GL_OES_depth24")) {
                return GL_DEPTH_COMPONENT24;
            }
            return GL_DEPTH_COMPONENT16;

        case TextureFormat::DEPTH32F:
            if (hasExtension("GL_OES_depth32")) {
                return GL_DEPTH_COMPONENT32F;
            }
            return GL_DEPTH_COMPONENT24;

        default:
            return GL_RGBA;
    }
}
```

### 2. 缓冲区对象管理

```objc
class OpenGLESBufferManager {
private:
    std::vector<GLuint> bufferPool;
    std::unordered_map<GLuint, size_t> bufferSizes;

public:
    GLuint acquireBuffer(size_t size, GLenum usage) {
        // 查找合适大小的缓冲区
        for (auto it = bufferPool.begin(); it != bufferPool.end(); ++it) {
            if (bufferSizes[*it] >= size) {
                GLuint buffer = *it;
                bufferPool.erase(it);
                return buffer;
            }
        }

        // 创建新缓冲区
        GLuint buffer;
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferData(GL_ARRAY_BUFFER, size, nullptr, usage);

        bufferSizes[buffer] = size;
        return buffer;
    }

    void releaseBuffer(GLuint buffer) {
        bufferPool.push_back(buffer);
    }

    void cleanup() {
        if (!bufferPool.empty()) {
            glDeleteBuffers(static_cast<GLsizei>(bufferPool.size()), bufferPool.data());
            bufferPool.clear();
        }
        bufferSizes.clear();
    }
};
```

### 3. 扩展功能检测

```objc
class OpenGLESExtensions {
private:
    std::unordered_set<std::string> supportedExtensions;
    bool extensionsLoaded = false;

public:
    void loadExtensions() {
        if (extensionsLoaded) return;

        // 获取扩展字符串
        const char* extensionString = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        if (extensionString) {
            std::string extensions(extensionString);
            std::istringstream iss(extensions);
            std::string extension;

            while (iss >> extension) {
                supportedExtensions.insert(extension);
            }
        }

        extensionsLoaded = true;
    }

    bool hasExtension(const std::string& extension) {
        loadExtensions();
        return supportedExtensions.find(extension) != supportedExtensions.end();
    }

    // 检查关键功能支持
    bool supportsInstancing() {
        return hasExtension("GL_EXT_instanced_arrays");
    }

    bool supportsTextureLOD() {
        return hasExtension("GL_EXT_shader_texture_lod");
    }

    bool supportsDepth24() {
        return hasExtension("GL_OES_depth24");
    }

    bool supportsSRGB() {
        return hasExtension("GL_EXT_sRGB");
    }
};
```

## 性能调优策略

### 1. 渲染状态缓存

```objc
class OpenGLESStateCache {
private:
    // 当前状态缓存
    GLuint currentProgram = 0;
    GLuint currentArrayBuffer = 0;
    GLuint currentElementArrayBuffer = 0;
    GLuint currentTexture2D[MAX_TEXTURE_UNITS] = {0};
    GLuint currentFramebuffer = 0;

    // 混合状态
    bool blendEnabled = false;
    GLenum blendSrcRGB = GL_ONE;
    GLenum blendDstRGB = GL_ZERO;

public:
    void useProgram(GLuint program) {
        if (currentProgram != program) {
            glUseProgram(program);
            currentProgram = program;
        }
    }

    void bindBuffer(GLenum target, GLuint buffer) {
        GLuint* currentBuffer = nullptr;

        switch (target) {
            case GL_ARRAY_BUFFER:
                currentBuffer = &currentArrayBuffer;
                break;
            case GL_ELEMENT_ARRAY_BUFFER:
                currentBuffer = &currentElementArrayBuffer;
                break;
            default:
                glBindBuffer(target, buffer);
                return;
        }

        if (*currentBuffer != buffer) {
            glBindBuffer(target, buffer);
            *currentBuffer = buffer;
        }
    }

    void bindTexture(GLenum target, GLuint texture, GLuint unit) {
        if (target == GL_TEXTURE_2D && unit < MAX_TEXTURE_UNITS) {
            if (currentTexture2D[unit] != texture) {
                glActiveTexture(GL_TEXTURE0 + unit);
                glBindTexture(target, texture);
                currentTexture2D[unit] = texture;
            }
        } else {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(target, texture);
        }
    }

    void setBlendState(bool enabled, GLenum srcRGB, GLenum dstRGB) {
        if (blendEnabled != enabled) {
            if (enabled) {
                glEnable(GL_BLEND);
            } else {
                glDisable(GL_BLEND);
            }
            blendEnabled = enabled;
        }

        if (enabled && (blendSrcRGB != srcRGB || blendDstRGB != dstRGB)) {
            glBlendFunc(srcRGB, dstRGB);
            blendSrcRGB = srcRGB;
            blendDstRGB = dstRGB;
        }
    }
};
```

### 2. 批量渲染优化

```objc
class OpenGLESBatchRenderer {
private:
    struct DrawCommand {
        GLuint vao;
        GLuint program;
        GLuint texture;
        GLsizei indexCount;
        const void* indices;
    };

    std::vector<DrawCommand> pendingCommands;
    static const size_t MAX_BATCH_SIZE = 1000;

public:
    void addDrawCommand(GLuint vao, GLuint program, GLuint texture,
                       GLsizei indexCount, const void* indices) {

        pendingCommands.push_back({vao, program, texture, indexCount, indices});

        if (pendingCommands.size() >= MAX_BATCH_SIZE) {
            flush();
        }
    }

    void flush() {
        if (pendingCommands.empty()) return;

        // 按状态排序以减少状态切换
        std::sort(pendingCommands.begin(), pendingCommands.end(),
                 [](const DrawCommand& a, const DrawCommand& b) {
                     if (a.program != b.program) return a.program < b.program;
                     if (a.texture != b.texture) return a.texture < b.texture;
                     return a.vao < b.vao;
                 });

        // 批量执行绘制命令
        GLuint currentProgram = 0;
        GLuint currentTexture = 0;
        GLuint currentVAO = 0;

        for (const auto& cmd : pendingCommands) {
            if (cmd.program != currentProgram) {
                glUseProgram(cmd.program);
                currentProgram = cmd.program;
            }

            if (cmd.texture != currentTexture) {
                glBindTexture(GL_TEXTURE_2D, cmd.texture);
                currentTexture = cmd.texture;
            }

            if (cmd.vao != currentVAO) {
                glBindVertexArray(cmd.vao);
                currentVAO = cmd.vao;
            }

            glDrawElements(GL_TRIANGLES, cmd.indexCount, GL_UNSIGNED_SHORT, cmd.indices);
        }

        pendingCommands.clear();
    }
};
```

## 错误处理和调试

### 1. OpenGL 错误检查

```objc
#if DEBUG
#define CHECK_GL_ERROR() checkGLError(__FILE__, __LINE__)

void checkGLError(const char* file, int line) {
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        const char* errorString = nullptr;

        switch (error) {
            case GL_INVALID_ENUM:
                errorString = "GL_INVALID_ENUM";
                break;
            case GL_INVALID_VALUE:
                errorString = "GL_INVALID_VALUE";
                break;
            case GL_INVALID_OPERATION:
                errorString = "GL_INVALID_OPERATION";
                break;
            case GL_OUT_OF_MEMORY:
                errorString = "GL_OUT_OF_MEMORY";
                break;
            case GL_INVALID_FRAMEBUFFER_OPERATION:
                errorString = "GL_INVALID_FRAMEBUFFER_OPERATION";
                break;
            default:
                errorString = "Unknown Error";
                break;
        }

        utils::slog.e << "OpenGL Error: " << errorString
                     << " at " << file << ":" << line;
    }
}
#else
#define CHECK_GL_ERROR()
#endif
```

### 2. 内存使用监控

```objc
class OpenGLESMemoryTracker {
private:
    size_t totalTextureMemory = 0;
    size_t totalBufferMemory = 0;
    std::unordered_map<GLuint, size_t> textureSizes;
    std::unordered_map<GLuint, size_t> bufferSizes;

public:
    void trackTexture(GLuint texture, GLsizei width, GLsizei height,
                     GLenum format, GLenum type) {

        size_t size = calculateTextureSize(width, height, format, type);
        textureSizes[texture] = size;
        totalTextureMemory += size;

        utils::slog.d << "Texture created: " << texture
                     << ", Size: " << size << " bytes"
                     << ", Total texture memory: " << totalTextureMemory;
    }

    void untrackTexture(GLuint texture) {
        auto it = textureSizes.find(texture);
        if (it != textureSizes.end()) {
            totalTextureMemory -= it->second;
            textureSizes.erase(it);
        }
    }

    void trackBuffer(GLuint buffer, GLsizeiptr size) {
        bufferSizes[buffer] = size;
        totalBufferMemory += size;
    }

    void untrackBuffer(GLuint buffer) {
        auto it = bufferSizes.find(buffer);
        if (it != bufferSizes.end()) {
            totalBufferMemory -= it->second;
            bufferSizes.erase(it);
        }
    }

    void printStats() {
        utils::slog.i << "OpenGL ES Memory Usage:"
                     << "\n  Textures: " << totalTextureMemory << " bytes"
                     << "\n  Buffers: " << totalBufferMemory << " bytes"
                     << "\n  Total: " << (totalTextureMemory + totalBufferMemory) << " bytes";
    }

private:
    size_t calculateTextureSize(GLsizei width, GLsizei height,
                              GLenum format, GLenum type) {
        size_t bytesPerPixel = 4; // 默认 RGBA

        switch (format) {
            case GL_RGB:
                bytesPerPixel = 3;
                break;
            case GL_RGBA:
                bytesPerPixel = 4;
                break;
            case GL_LUMINANCE:
                bytesPerPixel = 1;
                break;
            case GL_LUMINANCE_ALPHA:
                bytesPerPixel = 2;
                break;
        }

        if (type == GL_UNSIGNED_SHORT_5_6_5) {
            bytesPerPixel = 2;
        } else if (type == GL_UNSIGNED_SHORT_4_4_4_4) {
            bytesPerPixel = 2;
        }

        return width * height * bytesPerPixel;
    }
};
```

这个 OpenGL ES 后端实现为 Filament 在 iOS 平台上提供了稳定可靠的图形渲染能力，特别是在需要兼容较老设备时。