# Metal Backend 实现详解

本文档详细讲解 Filament MetalDriver 的实现，包括 Metal 命令编码器、资源管理、同步机制，以及 Metal 特有的优化技术。

---

## Metal Backend 概述

**文件位置**: `filament/backend/src/metal/MetalDriver.h`

Metal 是 Apple 平台（iOS/macOS）的原生图形 API，设计目标类似 Vulkan，但更高层、更易用：
- **现代设计**: 低开销、显式控制
- **Apple 优化**: 针对 Apple GPU 深度优化
- **Objective-C++**: 使用 Objective-C 对象模型
- **统一内存**: iOS 使用统一内存架构（UMA）

### 设计目标

1. **高性能**: 充分利用 Metal 的硬件特性
2. **Apple 生态**: 深度集成 iOS/macOS 平台
3. **资源管理**: 利用 Metal 的自动资源追踪
4. **易用性**: Metal 比 Vulkan 更简洁

---

## MetalDriver 类结构

### 类定义

```objc
// 注意：Metal Backend 使用 Objective-C++
class MetalDriver final : public Driver {
public:
    explicit MetalDriver(Platform* platform) noexcept;
    ~MetalDriver() noexcept override;

    // Backend 能力
    ShaderModel getShaderModel() const noexcept override;
    ShaderLanguage getShaderLanguage() const noexcept override;

    // 资源创建
    Handle<HwVertexBuffer> createVertexBuffer(...) override;
    Handle<HwIndexBuffer> createIndexBuffer(...) override;
    Handle<HwTexture> createTexture(...) override;
    Handle<HwProgram> createProgram(...) override;

    // 渲染命令
    void beginFrame(...) override;
    void endFrame(...) override;
    void draw(PipelineState state, RenderPrimitive primitive) override;

private:
    // Metal 核心对象（Objective-C 对象）
    id<MTLDevice> mDevice;                    // GPU 设备
    id<MTLCommandQueue> mCommandQueue;        // 命令队列

    // 当前帧资源
    id<MTLCommandBuffer> mCurrentCommandBuffer;
    id<MTLRenderCommandEncoder> mCurrentRenderEncoder;

    // 管线状态缓存
    NSMutableDictionary* mPipelineStateCache;

    // 采样器缓存
    NSMutableDictionary* mSamplerStateCache;

    // 资源分配器
    HandleAllocator<MetalVertexBuffer> mVertexBufferAllocator;
    HandleAllocator<MetalIndexBuffer> mIndexBufferAllocator;
    HandleAllocator<MetalTexture> mTextureAllocator;
    HandleAllocator<MetalProgram> mProgramAllocator;

    // 辅助方法
    id<MTLRenderPipelineState> getOrCreatePipelineState(const PipelineKey& key);
    id<MTLSamplerState> getOrCreateSamplerState(const SamplerParams& params);
};
```

---

## Metal 资源结构

### MetalVertexBuffer

```objc
struct MetalVertexBuffer {
    id<MTLBuffer> buffer;         // Metal 缓冲对象

    uint8_t bufferCount;
    uint32_t vertexCount;
    AttributeArray attributes;

    MetalVertexBuffer(uint8_t bufferCount, uint32_t vertexCount,
                      AttributeArray attributes)
        : buffer(nil)
        , bufferCount(bufferCount)
        , vertexCount(vertexCount)
        , attributes(attributes)
    {}

    ~MetalVertexBuffer() {
        // Objective-C ARC 自动管理 buffer 的释放
        buffer = nil;
    }
};

// 创建实现
Handle<HwVertexBuffer> MetalDriver::createVertexBuffer(
    uint8_t bufferCount,
    uint8_t attributeCount,
    uint32_t vertexCount,
    AttributeArray attributes
) {
    MetalVertexBuffer* vb = new MetalVertexBuffer(
        bufferCount, vertexCount, attributes
    );

    // 计算缓冲区大小
    NSUInteger bufferSize = calculateVertexBufferSize(vertexCount, attributes);

    // 创建 Metal 缓冲
    vb->buffer = [mDevice newBufferWithLength:bufferSize
                                      options:MTLResourceStorageModePrivate];

    // 设置调试标签
    #if FILAMENT_DEBUG
    vb->buffer.label = @"VertexBuffer";
    #endif

    return mVertexBufferAllocator.allocate<HwVertexBuffer>(vb);
}
```

### MetalTexture

```objc
struct MetalTexture {
    id<MTLTexture> texture;       // Metal 纹理对象
    id<MTLSamplerState> sampler;  // 采样器状态（可选）

    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint8_t levels;
    MTLPixelFormat format;

    MetalTexture()
        : texture(nil)
        , sampler(nil)
    {}

    ~MetalTexture() {
        texture = nil;
        sampler = nil;
    }
};

// 创建实现
Handle<HwTexture> MetalDriver::createTexture(
    SamplerType target,
    uint8_t levels,
    TextureFormat format,
    uint8_t samples,
    uint32_t width, uint32_t height, uint32_t depth,
    TextureUsage usage
) {
    MetalTexture* tex = new MetalTexture();

    // 创建纹理描述符
    MTLTextureDescriptor* descriptor = [[MTLTextureDescriptor alloc] init];
    descriptor.textureType = getMetalTextureType(target);
    descriptor.pixelFormat = getMetalPixelFormat(format);
    descriptor.width = width;
    descriptor.height = height;
    descriptor.depth = depth;
    descriptor.mipmapLevelCount = levels;
    descriptor.sampleCount = samples;
    descriptor.usage = getMetalTextureUsage(usage);
    descriptor.storageMode = MTLStorageModePrivate;

    if (target == SamplerType::SAMPLER_CUBEMAP) {
        descriptor.arrayLength = 6;
    }

    // 创建纹理
    tex->texture = [mDevice newTextureWithDescriptor:descriptor];

    #if FILAMENT_DEBUG
    tex->texture.label = @"Texture";
    #endif

    tex->width = width;
    tex->height = height;
    tex->depth = depth;
    tex->levels = levels;
    tex->format = getMetalPixelFormat(format);

    return mTextureAllocator.allocate<HwTexture>(tex);
}

// Metal 纹理类型转换
MTLTextureType getMetalTextureType(SamplerType type) {
    switch (type) {
        case SamplerType::SAMPLER_2D:
            return MTLTextureType2D;
        case SamplerType::SAMPLER_2D_ARRAY:
            return MTLTextureType2DArray;
        case SamplerType::SAMPLER_CUBEMAP:
            return MTLTextureTypeCube;
        case SamplerType::SAMPLER_3D:
            return MTLTextureType3D;
        default:
            return MTLTextureType2D;
    }
}

// Metal 像素格式转换
MTLPixelFormat getMetalPixelFormat(TextureFormat format) {
    switch (format) {
        case TextureFormat::RGBA8:
            return MTLPixelFormatRGBA8Unorm;
        case TextureFormat::RGB16F:
            return MTLPixelFormatRGBA16Float;  // Metal 不支持 RGB16F
        case TextureFormat::DEPTH24:
            return MTLPixelFormatDepth32Float;  // Metal 不支持 24位深度
        case TextureFormat::RGBA16F:
            return MTLPixelFormatRGBA16Float;
        case TextureFormat::R8:
            return MTLPixelFormatR8Unorm;
        // ...
        default:
            return MTLPixelFormatRGBA8Unorm;
    }
}
```

### MetalProgram

```objc
struct MetalProgram {
    id<MTLLibrary> library;           // Shader 库
    id<MTLFunction> vertexFunction;   // 顶点函数
    id<MTLFunction> fragmentFunction; // 片段函数

    UniformBlockInfo uniformBlocks;
    SamplerGroupInfo samplerGroups;

    MetalProgram()
        : library(nil)
        , vertexFunction(nil)
        , fragmentFunction(nil)
    {}

    ~MetalProgram() {
        library = nil;
        vertexFunction = nil;
        fragmentFunction = nil;
    }
};

// 创建实现
Handle<HwProgram> MetalDriver::createProgram(Program&& program) {
    MetalProgram* metalProgram = new MetalProgram();

    NSError* error = nil;

    // 从 MSL 源码创建库
    NSString* source = [NSString stringWithUTF8String:program.vertexShader.source];

    MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
    options.fastMathEnabled = YES;

    metalProgram->library = [mDevice newLibraryWithSource:source
                                                  options:options
                                                    error:&error];

    if (error) {
        LOG(ERROR) << "Failed to compile Metal shader: "
                   << [[error localizedDescription] UTF8String];
    }

    // 获取函数
    metalProgram->vertexFunction = [metalProgram->library
                                    newFunctionWithName:@"vertexMain"];
    metalProgram->fragmentFunction = [metalProgram->library
                                      newFunctionWithName:@"fragmentMain"];

    if (!metalProgram->vertexFunction || !metalProgram->fragmentFunction) {
        LOG(ERROR) << "Failed to find shader functions";
    }

    metalProgram->uniformBlocks = std::move(program.uniformBlocks);
    metalProgram->samplerGroups = std::move(program.samplerGroups);

    return mProgramAllocator.allocate<HwProgram>(metalProgram);
}
```

---

## Metal 命令编码

### 命令缓冲和编码器

```objc
void MetalDriver::beginFrame(int64_t monotonic_clock_ns, uint32_t frameId) {
    // 创建新的命令缓冲
    mCurrentCommandBuffer = [mCommandQueue commandBuffer];

    #if FILAMENT_DEBUG
    mCurrentCommandBuffer.label = [NSString stringWithFormat:@"Frame %u", frameId];
    #endif
}

void MetalDriver::endFrame(uint32_t frameId) {
    // 结束当前的渲染编码器（如果有）
    if (mCurrentRenderEncoder) {
        [mCurrentRenderEncoder endEncoding];
        mCurrentRenderEncoder = nil;
    }

    // 提交命令缓冲
    [mCurrentCommandBuffer commit];
    mCurrentCommandBuffer = nil;
}

void MetalDriver::setRenderTarget(Handle<HwRenderTarget> rth) {
    // 结束之前的编码器
    if (mCurrentRenderEncoder) {
        [mCurrentRenderEncoder endEncoding];
        mCurrentRenderEncoder = nil;
    }

    // 获取渲染目标
    MetalRenderTarget* rt = mRenderTargetAllocator.handle_cast(rth);

    // 创建渲染通道描述符
    MTLRenderPassDescriptor* passDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];

    // 配置颜色附件
    passDescriptor.colorAttachments[0].texture = rt->colorTexture;
    passDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    passDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    passDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);

    // 配置深度附件（如果有）
    if (rt->depthTexture) {
        passDescriptor.depthAttachment.texture = rt->depthTexture;
        passDescriptor.depthAttachment.loadAction = MTLLoadActionClear;
        passDescriptor.depthAttachment.storeAction = MTLStoreActionStore;
        passDescriptor.depthAttachment.clearDepth = 1.0;
    }

    // 创建渲染命令编码器
    mCurrentRenderEncoder = [mCurrentCommandBuffer
                             renderCommandEncoderWithDescriptor:passDescriptor];

    #if FILAMENT_DEBUG
    mCurrentRenderEncoder.label = @"RenderEncoder";
    #endif
}
```

---

## Metal 渲染管线状态

### Pipeline State Object (PSO)

```objc
struct PipelineKey {
    id<MTLFunction> vertexFunction;
    id<MTLFunction> fragmentFunction;
    MTLPixelFormat colorFormat;
    MTLPixelFormat depthFormat;
    RasterState rasterState;

    // 哈希和比较
    size_t hash() const;
    bool operator==(const PipelineKey& other) const;
};

id<MTLRenderPipelineState> MetalDriver::getOrCreatePipelineState(
    const PipelineKey& key
) {
    // 使用 NSValue 包装 C++ 结构体作为 key
    NSValue* keyValue = [NSValue valueWithBytes:&key
                                       objCType:@encode(PipelineKey)];

    // 查找缓存
    id<MTLRenderPipelineState> cached = mPipelineStateCache[keyValue];
    if (cached) {
        return cached;
    }

    // 创建新的管线状态
    MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];

    // 着色器函数
    descriptor.vertexFunction = key.vertexFunction;
    descriptor.fragmentFunction = key.fragmentFunction;

    // 颜色附件
    descriptor.colorAttachments[0].pixelFormat = key.colorFormat;

    // 深度附件
    descriptor.depthAttachmentPixelFormat = key.depthFormat;

    // 顶点描述符
    descriptor.vertexDescriptor = createVertexDescriptor(/* attributes */);

    // 光栅化状态
    if (key.rasterState.blending != BlendMode::OPAQUE) {
        descriptor.colorAttachments[0].blendingEnabled = YES;
        configureBlending(descriptor.colorAttachments[0], key.rasterState.blending);
    }

    // 编译管线状态
    NSError* error = nil;
    id<MTLRenderPipelineState> pipelineState =
        [mDevice newRenderPipelineStateWithDescriptor:descriptor
                                                error:&error];

    if (error) {
        LOG(ERROR) << "Failed to create pipeline state: "
                   << [[error localizedDescription] UTF8String];
    }

    // 缓存
    mPipelineStateCache[keyValue] = pipelineState;

    return pipelineState;
}

// 创建顶点描述符
MTLVertexDescriptor* createVertexDescriptor(const AttributeArray& attributes) {
    MTLVertexDescriptor* descriptor = [[MTLVertexDescriptor alloc] init];

    for (size_t i = 0; i < attributes.size(); i++) {
        const auto& attrib = attributes[i];
        if (attrib.type == ElementType::INVALID) {
            continue;
        }

        // 属性描述
        descriptor.attributes[i].format = getMetalVertexFormat(attrib.type);
        descriptor.attributes[i].offset = attrib.offset;
        descriptor.attributes[i].bufferIndex = attrib.buffer;

        // 缓冲布局
        descriptor.layouts[attrib.buffer].stride = attrib.stride;
        descriptor.layouts[attrib.buffer].stepFunction = MTLVertexStepFunctionPerVertex;
    }

    return descriptor;
}

// Metal 顶点格式转换
MTLVertexFormat getMetalVertexFormat(ElementType type) {
    switch (type) {
        case ElementType::FLOAT:
            return MTLVertexFormatFloat;
        case ElementType::FLOAT2:
            return MTLVertexFormatFloat2;
        case ElementType::FLOAT3:
            return MTLVertexFormatFloat3;
        case ElementType::FLOAT4:
            return MTLVertexFormatFloat4;
        case ElementType::UBYTE4:
            return MTLVertexFormatUChar4;
        // ...
        default:
            return MTLVertexFormatInvalid;
    }
}
```

---

## Metal 深度/模板状态

```objc
id<MTLDepthStencilState> MetalDriver::getOrCreateDepthStencilState(
    const RasterState& state
) {
    // 创建深度模板描述符
    MTLDepthStencilDescriptor* descriptor = [[MTLDepthStencilDescriptor alloc] init];

    if (state.depthFunc != DepthFunc::ALWAYS) {
        descriptor.depthCompareFunction = getMetalCompareFunction(state.depthFunc);
        descriptor.depthWriteEnabled = state.depthWrite;
    } else {
        descriptor.depthCompareFunction = MTLCompareFunctionAlways;
        descriptor.depthWriteEnabled = NO;
    }

    return [mDevice newDepthStencilStateWithDescriptor:descriptor];
}

// Metal 比较函数转换
MTLCompareFunction getMetalCompareFunction(DepthFunc func) {
    switch (func) {
        case DepthFunc::LESS:
            return MTLCompareFunctionLess;
        case DepthFunc::LESS_EQUAL:
            return MTLCompareFunctionLessEqual;
        case DepthFunc::GREATER:
            return MTLCompareFunctionGreater;
        case DepthFunc::GREATER_EQUAL:
            return MTLCompareFunctionGreaterEqual;
        case DepthFunc::EQUAL:
            return MTLCompareFunctionEqual;
        case DepthFunc::NOT_EQUAL:
            return MTLCompareFunctionNotEqual;
        case DepthFunc::ALWAYS:
            return MTLCompareFunctionAlways;
        case DepthFunc::NEVER:
            return MTLCompareFunctionNever;
        default:
            return MTLCompareFunctionLess;
    }
}
```

---

## Metal 采样器状态

```objc
id<MTLSamplerState> MetalDriver::getOrCreateSamplerState(
    const SamplerParams& params
) {
    // 创建采样器描述符
    MTLSamplerDescriptor* descriptor = [[MTLSamplerDescriptor alloc] init];

    // 过滤模式
    descriptor.minFilter = getMetalSamplerMinFilter(params.filterMin);
    descriptor.magFilter = getMetalSamplerMagFilter(params.filterMag);
    descriptor.mipFilter = getMetalSamplerMipFilter(params.filterMin);

    // 包裹模式
    descriptor.sAddressMode = getMetalSamplerAddressMode(params.wrapS);
    descriptor.tAddressMode = getMetalSamplerAddressMode(params.wrapT);
    descriptor.rAddressMode = getMetalSamplerAddressMode(params.wrapR);

    // 各向异性过滤
    if (params.anisotropy > 1.0f) {
        descriptor.maxAnisotropy = params.anisotropy;
    }

    // 比较模式（用于阴影贴图）
    if (params.compareMode != SamplerCompareMode::NONE) {
        descriptor.compareFunction = getMetalCompareFunction(params.compareFunc);
    }

    return [mDevice newSamplerStateWithDescriptor:descriptor];
}

// Metal 过滤模式转换
MTLSamplerMinMagFilter getMetalSamplerMinFilter(SamplerMinFilter filter) {
    switch (filter) {
        case SamplerMinFilter::NEAREST:
        case SamplerMinFilter::NEAREST_MIPMAP_NEAREST:
        case SamplerMinFilter::NEAREST_MIPMAP_LINEAR:
            return MTLSamplerMinMagFilterNearest;
        case SamplerMinFilter::LINEAR:
        case SamplerMinFilter::LINEAR_MIPMAP_NEAREST:
        case SamplerMinFilter::LINEAR_MIPMAP_LINEAR:
            return MTLSamplerMinMagFilterLinear;
        default:
            return MTLSamplerMinMagFilterLinear;
    }
}

// Metal 包裹模式转换
MTLSamplerAddressMode getMetalSamplerAddressMode(SamplerWrapMode mode) {
    switch (mode) {
        case SamplerWrapMode::CLAMP_TO_EDGE:
            return MTLSamplerAddressModeClampToEdge;
        case SamplerWrapMode::REPEAT:
            return MTLSamplerAddressModeRepeat;
        case SamplerWrapMode::MIRRORED_REPEAT:
            return MTLSamplerAddressModeMirrorRepeat;
        default:
            return MTLSamplerAddressModeClampToEdge;
    }
}
```

---

## 绘制命令

```objc
void MetalDriver::draw(PipelineState pipelineState, RenderPrimitive primitive) {
    if (!mCurrentRenderEncoder) {
        LOG(ERROR) << "No active render encoder";
        return;
    }

    // 1. 获取或创建管线状态
    PipelineKey key = makePipelineKey(pipelineState);
    id<MTLRenderPipelineState> pso = getOrCreatePipelineState(key);

    [mCurrentRenderEncoder setRenderPipelineState:pso];

    // 2. 设置深度模板状态
    id<MTLDepthStencilState> depthState =
        getOrCreateDepthStencilState(pipelineState.rasterState);

    [mCurrentRenderEncoder setDepthStencilState:depthState];

    // 3. 设置面剔除
    MTLCullMode cullMode = getMetalCullMode(pipelineState.rasterState.culling);
    [mCurrentRenderEncoder setCullMode:cullMode];

    // 4. 绑定顶点缓冲
    MetalVertexBuffer* vb = mVertexBufferAllocator.handle_cast(primitive.vertexBuffer);
    [mCurrentRenderEncoder setVertexBuffer:vb->buffer
                                    offset:0
                                   atIndex:0];

    // 5. 绑定 Uniform 缓冲（简化示例）
    if (mCurrentUniformBuffer) {
        [mCurrentRenderEncoder setVertexBuffer:mCurrentUniformBuffer
                                        offset:0
                                       atIndex:1];
        [mCurrentRenderEncoder setFragmentBuffer:mCurrentUniformBuffer
                                          offset:0
                                         atIndex:0];
    }

    // 6. 绑定纹理和采样器
    for (size_t i = 0; i < mCurrentTextures.size(); i++) {
        if (mCurrentTextures[i]) {
            MetalTexture* tex = mTextureAllocator.handle_cast(mCurrentTextures[i]);
            [mCurrentRenderEncoder setFragmentTexture:tex->texture
                                              atIndex:i];
            if (tex->sampler) {
                [mCurrentRenderEncoder setFragmentSamplerState:tex->sampler
                                                       atIndex:i];
            }
        }
    }

    // 7. 绘制
    MTLPrimitiveType primitiveType = getMetalPrimitiveType(primitive.type);

    if (primitive.indexBuffer) {
        // 有索引绘制
        MetalIndexBuffer* ib = mIndexBufferAllocator.handle_cast(primitive.indexBuffer);

        MTLIndexType indexType = (ib->elementType == ElementType::UINT) ?
            MTLIndexTypeUInt32 : MTLIndexTypeUInt16;

        NSUInteger indexBufferOffset = primitive.offset *
            getElementTypeSize(ib->elementType);

        [mCurrentRenderEncoder drawIndexedPrimitives:primitiveType
                                          indexCount:primitive.count
                                           indexType:indexType
                                         indexBuffer:ib->buffer
                                   indexBufferOffset:indexBufferOffset];
    } else {
        // 无索引绘制
        [mCurrentRenderEncoder drawPrimitives:primitiveType
                                  vertexStart:primitive.offset
                                  vertexCount:primitive.count];
    }
}

// Metal 图元类型转换
MTLPrimitiveType getMetalPrimitiveType(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::TRIANGLES:
            return MTLPrimitiveTypeTriangle;
        case PrimitiveType::TRIANGLE_STRIP:
            return MTLPrimitiveTypeTriangleStrip;
        case PrimitiveType::LINES:
            return MTLPrimitiveTypeLine;
        case PrimitiveType::LINE_STRIP:
            return MTLPrimitiveTypeLineStrip;
        case PrimitiveType::POINTS:
            return MTLPrimitiveTypePoint;
        default:
            return MTLPrimitiveTypeTriangle;
    }
}

// Metal 剔除模式转换
MTLCullMode getMetalCullMode(CullingMode mode) {
    switch (mode) {
        case CullingMode::BACK:
            return MTLCullModeBack;
        case CullingMode::FRONT:
            return MTLCullModeFront;
        case CullingMode::NONE:
            return MTLCullModeNone;
        default:
            return MTLCullModeBack;
    }
}
```

---

## Metal 内存管理

### 统一内存架构 (UMA)

iOS 设备使用统一内存架构，CPU 和 GPU 共享内存：

```objc
// iOS: 使用 Shared 内存（CPU 和 GPU 都可访问）
#if TARGET_OS_IOS
    MTLResourceOptions options = MTLResourceStorageModeShared;
#else
    // macOS: 使用 Managed 内存（需要手动同步）
    MTLResourceOptions options = MTLResourceStorageModeManaged;
#endif

id<MTLBuffer> buffer = [mDevice newBufferWithLength:size
                                             options:options];

// 写入数据（CPU）
void* ptr = [buffer contents];
memcpy(ptr, data, size);

#if !TARGET_OS_IOS
    // macOS: 需要手动同步到 GPU
    [buffer didModifyRange:NSMakeRange(0, size)];
#endif
```

### 资源追踪

Metal 自动追踪资源使用，无需手动管理：

```objc
void MetalDriver::updateTexture(Handle<HwTexture> th,
                                PixelBufferDescriptor&& data) {
    MetalTexture* texture = mTextureAllocator.handle_cast(th);

    // 创建临时缓冲
    id<MTLBuffer> stagingBuffer = [mDevice newBufferWithBytes:data.buffer
                                                       length:data.size
                                                      options:MTLResourceStorageModeShared];

    // 创建 Blit 编码器
    id<MTLBlitCommandEncoder> blitEncoder = [mCurrentCommandBuffer blitCommandEncoder];

    // 从缓冲拷贝到纹理
    [blitEncoder copyFromBuffer:stagingBuffer
                   sourceOffset:0
              sourceBytesPerRow:texture->width * 4
            sourceBytesPerImage:0
                     sourceSize:MTLSizeMake(texture->width, texture->height, 1)
                      toTexture:texture->texture
               destinationSlice:0
               destinationLevel:0
              destinationOrigin:MTLOriginMake(0, 0, 0)];

    [blitEncoder endEncoding];

    // Metal 自动追踪 stagingBuffer 的生命周期
    // 不需要手动延迟删除！
    // stagingBuffer 会在命令缓冲执行完后自动释放
}
```

---

## Metal 特有优化

### Tile-Based Deferred Rendering (TBDR)

Apple GPU 使用 TBDR 架构，可以利用 On-Chip 内存：

```objc
// 使用 Memoryless 纹理（仅存在于 Tile 内存）
MTLTextureDescriptor* descriptor = [[MTLTextureDescriptor alloc] init];
descriptor.textureType = MTLTextureType2D;
descriptor.pixelFormat = MTLPixelFormatDepth32Float;
descriptor.width = width;
descriptor.height = height;

#if TARGET_OS_IOS
    // iOS: 深度缓冲可以使用 Memoryless 存储
    // 不需要写回主内存，节省带宽
    descriptor.storageMode = MTLStorageModeMemoryless;
#else
    descriptor.storageMode = MTLStorageModePrivate;
#endif

id<MTLTexture> depthTexture = [mDevice newTextureWithDescriptor:descriptor];
```

### Metal Argument Buffers

使用参数缓冲绑定大量资源：

```objc
// 创建参数编码器
id<MTLArgumentEncoder> argumentEncoder =
    [function newArgumentEncoderWithBufferIndex:0];

// 分配参数缓冲
id<MTLBuffer> argumentBuffer =
    [mDevice newBufferWithLength:argumentEncoder.encodedLength
                         options:MTLResourceStorageModeShared];

// 编码参数
[argumentEncoder setArgumentBuffer:argumentBuffer offset:0];
[argumentEncoder setTexture:texture1 atIndex:0];
[argumentEncoder setTexture:texture2 atIndex:1];
[argumentEncoder setSamplerState:sampler atIndex:0];

// 绑定参数缓冲
[renderEncoder setFragmentBuffer:argumentBuffer offset:0 atIndex:0];
[renderEncoder useResource:texture1 usage:MTLResourceUsageRead];
[renderEncoder useResource:texture2 usage:MTLResourceUsageRead];
```

### Indirect Command Buffers

使用间接命令缓冲进行 GPU 驱动渲染：

```objc
// 创建间接命令缓冲
MTLIndirectCommandBufferDescriptor* icbDescriptor =
    [[MTLIndirectCommandBufferDescriptor alloc] init];
icbDescriptor.commandTypes = MTLIndirectCommandTypeDraw;
icbDescriptor.inheritBuffers = NO;
icbDescriptor.maxVertexBufferBindCount = 2;

id<MTLIndirectCommandBuffer> icb =
    [mDevice newIndirectCommandBufferWithDescriptor:icbDescriptor
                                     maxCommandCount:1000
                                             options:0];

// GPU 可以直接修改和执行这些命令
```

---

## 同步机制

### 命令缓冲完成回调

```objc
[mCurrentCommandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
    // 命令缓冲执行完成后调用
    // 可以安全删除资源

    // 清理临时资源
    for (auto& resource : mPendingDeletion) {
        // 释放资源
    }
    mPendingDeletion.clear();
}];
```

### 事件（Event）

```objc
// 创建事件
id<MTLEvent> event = [mDevice newEvent];

// GPU 信号事件
[renderEncoder signalEvent:event value:1];

// GPU 等待事件
[blitEncoder waitForEvent:event value:1];
```

---

## 调试和性能分析

### Xcode Metal Debugger

```objc
#if FILAMENT_DEBUG
    // 设置捕获范围
    MTLCaptureManager* captureManager = [MTLCaptureManager sharedCaptureManager];
    MTLCaptureDescriptor* captureDescriptor = [[MTLCaptureDescriptor alloc] init];
    captureDescriptor.captureObject = mDevice;

    [captureManager startCaptureWithDescriptor:captureDescriptor error:nil];

    // 渲染代码...

    [captureManager stopCapture];
#endif
```

### Metal 性能统计

```objc
// 使用 GPU 计数器
if (@available(iOS 14.0, macOS 11.0, *)) {
    MTLCounterSampleBufferDescriptor* descriptor =
        [[MTLCounterSampleBufferDescriptor alloc] init];
    descriptor.counterSet = mDevice.counterSets[0];
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.sampleCount = 2;

    id<MTLCounterSampleBuffer> counterBuffer =
        [mDevice newCounterSampleBufferWithDescriptor:descriptor
                                                error:nil];

    // 采样开始
    [renderEncoder sampleCountersInBuffer:counterBuffer
                            atSampleIndex:0
                              withBarrier:YES];

    // 渲染...

    // 采样结束
    [renderEncoder sampleCountersInBuffer:counterBuffer
                            atSampleIndex:1
                              withBarrier:YES];
}
```

---

## 相关文档

- **[05-opengl-backend.md](05-opengl-backend.md)**: OpenGL 实现对比
- **[06-vulkan-backend.md](06-vulkan-backend.md)**: Vulkan 实现对比
- **[09-backend-comparison.md](09-backend-comparison.md)**: 后端对比

**理论基础**:
- `../graphics/03-gpu-pipeline.md`: GPU 管线

---

## 总结

MetalDriver 通过**命令编码器**、**管线状态缓存**、**自动资源追踪**、**TBDR 优化**，成功地在 Apple 平台上实现了高性能的渲染抽象，充分利用了 Metal 的现代特性和硬件优化。

**核心技术**:
- ✅ 命令编码器：简洁的命令录制API
- ✅ PSO 缓存：避免重复编译
- ✅ 自动追踪：无需手动管理资源生命周期
- ✅ TBDR 优化：利用 On-Chip 内存
- ✅ 统一内存：iOS 平台的零拷贝
- ✅ 现代特性：参数缓冲、间接命令等
