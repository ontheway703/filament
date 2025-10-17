# Filament Metal 后端实现

## Metal 后端架构

Filament Metal 后端是 iOS 和 macOS 平台上的主要图形驱动，提供了现代化的图形 API 接口和最佳性能表现。

```
Metal 后端架构:
Filament API → Metal Driver → Metal Framework → GPU
     ↓             ↓              ↓            ↓
   引擎层        驱动抽象层      系统框架      硬件加速
```

## 核心组件

### 1. Metal 驱动主类

**位置**: `filament/backend/src/metal/MetalDriver.mm`

```objc
class MetalDriver final : public Driver {
private:
    id<MTLDevice> mDevice;
    id<MTLCommandQueue> mCommandQueue;
    MetalContext* mContext;
    MetalBufferPool* mBufferPool;
    MetalSamplerGroup mSamplerGroup;

public:
    // 核心接口实现
    void tick(int) override;
    void beginFrame(int64_t monotonic_clock_ns,
                   int64_t refreshPeriodNs,
                   uint32_t frameId) override;
    void endFrame(uint32_t frameId) override;

    // 资源管理
    Handle<HwVertexBuffer> createVertexBuffer(uint8_t bufferCount,
                                            uint8_t attributeCount,
                                            uint32_t elementCount,
                                            AttributeArray attributes) override;

    Handle<HwTexture> createTexture(SamplerType target,
                                  uint8_t levels,
                                  TextureFormat format,
                                  uint8_t samples,
                                  uint32_t w, uint32_t h, uint32_t depth,
                                  TextureUsage usage) override;
};
```

### 2. Metal 上下文管理

**位置**: `filament/backend/src/metal/MetalContext.mm`

```objc
class MetalContext {
private:
    id<MTLDevice> device;
    id<MTLCommandQueue> commandQueue;
    id<MTLCommandBuffer> currentCommandBuffer;
    id<MTLRenderCommandEncoder> currentRenderEncoder;

public:
    // 命令缓冲区管理
    id<MTLCommandBuffer> beginFrame() {
        currentCommandBuffer = [commandQueue commandBuffer];
        currentCommandBuffer.label = @"Filament Frame";
        return currentCommandBuffer;
    }

    void endFrame() {
        [currentCommandBuffer commit];
        currentCommandBuffer = nil;
    }

    // 渲染编码器管理
    id<MTLRenderCommandEncoder> beginRenderPass(MTLRenderPassDescriptor* descriptor) {
        currentRenderEncoder = [currentCommandBuffer renderCommandEncoderWithDescriptor:descriptor];
        return currentRenderEncoder;
    }

    void endRenderPass() {
        [currentRenderEncoder endEncoding];
        currentRenderEncoder = nil;
    }
};
```

## 资源管理系统

### 1. 纹理管理

**位置**: `filament/backend/src/metal/MetalHandles.mm`

```objc
struct MetalTexture : public HwTexture {
    id<MTLTexture> texture;
    MTLPixelFormat format;
    uint32_t width, height, depth;
    uint8_t levels;

    MetalTexture(id<MTLDevice> device, SamplerType target,
                uint8_t levels, TextureFormat format,
                uint32_t w, uint32_t h, uint32_t d) {

        // 转换 Filament 格式到 Metal 格式
        this->format = getMetalFormat(format);

        // 创建纹理描述符
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
        descriptor.pixelFormat = this->format;
        descriptor.width = w;
        descriptor.height = h;
        descriptor.depth = d;
        descriptor.mipmapLevelCount = levels;
        descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;

        // 设置纹理类型
        switch (target) {
            case SamplerType::SAMPLER_2D:
                descriptor.textureType = MTLTextureType2D;
                break;
            case SamplerType::SAMPLER_CUBEMAP:
                descriptor.textureType = MTLTextureTypeCube;
                break;
            case SamplerType::SAMPLER_2D_ARRAY:
                descriptor.textureType = MTLTextureType2DArray;
                break;
        }

        // 创建 Metal 纹理
        texture = [device newTextureWithDescriptor:descriptor];
    }

    void updateImage(const PixelBufferDescriptor& data, uint32_t level) {
        MTLRegion region = MTLRegionMake2D(0, 0, width >> level, height >> level);

        [texture replaceRegion:region
                   mipmapLevel:level
                         slice:0
                     withBytes:data.buffer
                   bytesPerRow:data.stride];
    }
};
```

### 2. 缓冲区管理

```objc
struct MetalBuffer : public HwBufferObject {
    id<MTLBuffer> buffer;
    uint32_t size;
    BufferObjectBinding bindingType;

    MetalBuffer(id<MTLDevice> device, uint32_t byteCount, BufferObjectBinding bindingType)
        : size(byteCount), bindingType(bindingType) {

        // 根据绑定类型选择存储模式
        MTLResourceOptions options;
        switch (bindingType) {
            case BufferObjectBinding::VERTEX:
            case BufferObjectBinding::INDEX:
                options = MTLResourceStorageModeShared;
                break;
            case BufferObjectBinding::UNIFORM:
                options = MTLResourceStorageModeShared;
                break;
            case BufferObjectBinding::SHADER_STORAGE:
                options = MTLResourceStorageModePrivate;
                break;
        }

        buffer = [device newBufferWithLength:byteCount options:options];
    }

    void updateBuffer(const BufferDescriptor& data, uint32_t byteOffset) {
        // 更新缓冲区数据
        memcpy((uint8_t*)[buffer contents] + byteOffset, data.buffer, data.size);

        // 如果是共享内存，需要同步到 GPU
        if ([buffer storageMode] == MTLStorageModeManaged) {
            [buffer didModifyRange:NSMakeRange(byteOffset, data.size)];
        }
    }
};
```

### 3. 缓冲区池优化

**位置**: `filament/backend/src/metal/MetalBufferPool.mm`

```objc
class MetalBufferPool {
private:
    id<MTLDevice> device;
    std::vector<id<MTLBuffer>> availableBuffers[BufferPoolEntry::CAPACITY];
    std::vector<id<MTLBuffer>> usedBuffers;
    uint32_t frameIndex;

public:
    id<MTLBuffer> acquireBuffer(uint32_t size) {
        // 查找合适大小的缓冲区
        size_t poolIndex = bufferPoolSizeToIndex(size);

        auto& pool = availableBuffers[poolIndex];
        if (!pool.empty()) {
            id<MTLBuffer> buffer = pool.back();
            pool.pop_back();
            usedBuffers.push_back(buffer);
            return buffer;
        }

        // 创建新缓冲区
        uint32_t actualSize = bufferPoolIndexToSize(poolIndex);
        id<MTLBuffer> buffer = [device newBufferWithLength:actualSize
                                                   options:MTLResourceStorageModeShared];
        buffer.label = [NSString stringWithFormat:@"PoolBuffer_%u", actualSize];

        usedBuffers.push_back(buffer);
        return buffer;
    }

    void reset() {
        // 帧结束时将使用的缓冲区回收到池中
        for (id<MTLBuffer> buffer in usedBuffers) {
            size_t poolIndex = bufferPoolSizeToIndex([buffer length]);
            availableBuffers[poolIndex].push_back(buffer);
        }
        usedBuffers.clear();
    }
};
```

## 渲染管线实现

### 1. 着色器编译

**位置**: `filament/backend/src/metal/MetalShaderCompiler.mm`

```objc
class MetalShaderCompiler {
public:
    static NSString* compileMSL(const Program& program, Variant variant) {
        // 从 SPIR-V 转换到 MSL
        spirv_cross::CompilerMSL mslCompiler(program.getSpirvShader(variant));

        // 设置 MSL 编译选项
        spirv_cross::CompilerMSL::Options options;
        options.platform = spirv_cross::CompilerMSL::Options::Platform::iOS;
        options.ios_support_base_vertex_instance = true;
        options.enable_decoration_binding = true;

        mslCompiler.set_msl_options(options);

        // 编译为 MSL 源码
        std::string mslSource = mslCompiler.compile();
        return [NSString stringWithUTF8String:mslSource.c_str()];
    }

    static id<MTLLibrary> createLibrary(id<MTLDevice> device, NSString* mslSource) {
        NSError* error = nil;
        MTLCompileOptions* options = [MTLCompileOptions new];
        options.languageVersion = MTLLanguageVersion2_0;

        id<MTLLibrary> library = [device newLibraryWithSource:mslSource
                                                      options:options
                                                        error:&error];

        if (error) {
            utils::slog.e << "Metal shader compilation failed: "
                         << [error.localizedDescription UTF8String];
            return nil;
        }

        return library;
    }
};
```

### 2. 渲染状态管理

```objc
struct MetalRenderPipeline {
    id<MTLRenderPipelineState> pipelineState;
    id<MTLDepthStencilState> depthStencilState;

    static MetalRenderPipeline create(id<MTLDevice> device,
                                    const RenderPipelineDescriptor& desc) {

        // 创建渲染管线描述符
        MTLRenderPipelineDescriptor* pipelineDesc = [MTLRenderPipelineDescriptor new];

        // 设置着色器函数
        pipelineDesc.vertexFunction = desc.vertexFunction;
        pipelineDesc.fragmentFunction = desc.fragmentFunction;

        // 配置顶点描述符
        MTLVertexDescriptor* vertexDesc = [MTLVertexDescriptor new];
        for (size_t i = 0; i < desc.vertexAttributes.size(); ++i) {
            const auto& attr = desc.vertexAttributes[i];

            vertexDesc.attributes[i].format = getMetalVertexFormat(attr.format);
            vertexDesc.attributes[i].offset = attr.offset;
            vertexDesc.attributes[i].bufferIndex = attr.bufferIndex;
        }
        pipelineDesc.vertexDescriptor = vertexDesc;

        // 配置渲染目标
        for (size_t i = 0; i < desc.colorAttachments.size(); ++i) {
            const auto& attachment = desc.colorAttachments[i];

            pipelineDesc.colorAttachments[i].pixelFormat = attachment.format;
            pipelineDesc.colorAttachments[i].blendingEnabled = attachment.blendEnabled;

            if (attachment.blendEnabled) {
                pipelineDesc.colorAttachments[i].sourceRGBBlendFactor = attachment.srcRGB;
                pipelineDesc.colorAttachments[i].destinationRGBBlendFactor = attachment.dstRGB;
                pipelineDesc.colorAttachments[i].rgbBlendOperation = attachment.rgbOp;
            }
        }

        // 创建管线状态
        NSError* error = nil;
        MetalRenderPipeline pipeline;
        pipeline.pipelineState = [device newRenderPipelineStateWithDescriptor:pipelineDesc
                                                                         error:&error];

        if (error) {
            utils::slog.e << "Failed to create Metal pipeline: "
                         << [error.localizedDescription UTF8String];
        }

        // 创建深度模板状态
        pipeline.depthStencilState = createDepthStencilState(device, desc.depthStencil);

        return pipeline;
    }
};
```

## 高级特性实现

### 1. 计算着色器支持

```objc
class MetalComputePipeline {
private:
    id<MTLComputePipelineState> pipelineState;
    MTLSize threadgroupSize;

public:
    MetalComputePipeline(id<MTLDevice> device, id<MTLFunction> computeFunction) {
        NSError* error = nil;
        pipelineState = [device newComputePipelineStateWithFunction:computeFunction
                                                              error:&error];

        if (error) {
            utils::slog.e << "Failed to create compute pipeline: "
                         << [error.localizedDescription UTF8String];
        }

        // 设置线程组大小
        NSUInteger maxThreadsPerGroup = pipelineState.maxTotalThreadsPerThreadgroup;
        threadgroupSize = MTLSizeMake(16, 16, 1);  // 通常为 16x16
    }

    void dispatch(id<MTLComputeCommandEncoder> encoder,
                 uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) {

        [encoder setComputePipelineState:pipelineState];

        MTLSize gridSize = MTLSizeMake(groupsX, groupsY, groupsZ);
        [encoder dispatchThreadgroups:gridSize threadsPerThreadgroup:threadgroupSize];
    }
};
```

### 2. 多重采样抗锯齿 (MSAA)

```objc
id<MTLTexture> createMSAATexture(id<MTLDevice> device,
                               uint32_t width, uint32_t height,
                               MTLPixelFormat format, uint32_t samples) {

    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor new];
    descriptor.textureType = MTLTextureType2DMultisample;
    descriptor.pixelFormat = format;
    descriptor.width = width;
    descriptor.height = height;
    descriptor.sampleCount = samples;
    descriptor.usage = MTLTextureUsageRenderTarget;
    descriptor.storageMode = MTLStorageModePrivate;

    return [device newTextureWithDescriptor:descriptor];
}

void resolveMSAA(id<MTLRenderCommandEncoder> encoder,
                id<MTLTexture> msaaTexture,
                id<MTLTexture> resolveTexture) {

    // Metal 会自动在渲染通道结束时解析 MSAA
    // 只需要在渲染通道描述符中设置 resolveTexture
}
```

### 3. 间接绘制

```objc
struct IndirectDrawCommand {
    uint32_t vertexCount;
    uint32_t instanceCount;
    uint32_t vertexStart;
    uint32_t baseInstance;
};

void executeIndirectDraw(id<MTLRenderCommandEncoder> encoder,
                        id<MTLBuffer> indirectBuffer,
                        uint32_t commandCount) {

    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
            indirectBuffer:indirectBuffer
      indirectBufferOffset:0];

    // 或者使用索引绘制
    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexType:MTLIndexTypeUInt16
                      indexBuffer:indexBuffer
                indexBufferOffset:0
                   indirectBuffer:indirectBuffer
             indirectBufferOffset:0];
}
```

## 性能优化技术

### 1. 资源绑定优化

```objc
class MetalResourceBinder {
private:
    // 缓存当前绑定状态以避免冗余操作
    id<MTLBuffer> currentVertexBuffers[MAX_VERTEX_BUFFER_COUNT];
    id<MTLTexture> currentTextures[MAX_TEXTURE_COUNT];
    id<MTLSamplerState> currentSamplers[MAX_SAMPLER_COUNT];

public:
    void bindVertexBuffer(id<MTLRenderCommandEncoder> encoder,
                         id<MTLBuffer> buffer, uint32_t index) {
        if (currentVertexBuffers[index] != buffer) {
            [encoder setVertexBuffer:buffer offset:0 atIndex:index];
            currentVertexBuffers[index] = buffer;
        }
    }

    void bindTexture(id<MTLRenderCommandEncoder> encoder,
                    id<MTLTexture> texture, uint32_t index) {
        if (currentTextures[index] != texture) {
            [encoder setFragmentTexture:texture atIndex:index];
            currentTextures[index] = texture;
        }
    }

    void reset() {
        // 帧开始时重置绑定状态
        memset(currentVertexBuffers, 0, sizeof(currentVertexBuffers));
        memset(currentTextures, 0, sizeof(currentTextures));
        memset(currentSamplers, 0, sizeof(currentSamplers));
    }
};
```

### 2. 命令缓冲区复用

```objc
class MetalCommandBufferPool {
private:
    id<MTLCommandQueue> commandQueue;
    std::vector<id<MTLCommandBuffer>> availableBuffers;
    std::vector<id<MTLCommandBuffer>> pendingBuffers;

public:
    id<MTLCommandBuffer> acquireCommandBuffer() {
        if (!availableBuffers.empty()) {
            id<MTLCommandBuffer> buffer = availableBuffers.back();
            availableBuffers.pop_back();
            pendingBuffers.push_back(buffer);
            return buffer;
        }

        id<MTLCommandBuffer> buffer = [commandQueue commandBuffer];
        pendingBuffers.push_back(buffer);

        // 添加完成回调以回收缓冲区
        [buffer addCompletedHandler:^(id<MTLCommandBuffer> completedBuffer) {
            [self recycleCommandBuffer:completedBuffer];
        }];

        return buffer;
    }

private:
    void recycleCommandBuffer(id<MTLCommandBuffer> buffer) {
        auto it = std::find(pendingBuffers.begin(), pendingBuffers.end(), buffer);
        if (it != pendingBuffers.end()) {
            pendingBuffers.erase(it);
            availableBuffers.push_back(buffer);
        }
    }
};
```

### 3. GPU 同步优化

```objc
class MetalFenceManager {
private:
    std::vector<id<MTLFence>> availableFences;

public:
    id<MTLFence> acquireFence(id<MTLDevice> device) {
        if (!availableFences.empty()) {
            id<MTLFence> fence = availableFences.back();
            availableFences.pop_back();
            return fence;
        }

        return [device newFence];
    }

    void waitForFence(id<MTLRenderCommandEncoder> encoder, id<MTLFence> fence) {
        [encoder waitForFence:fence beforeStages:MTLRenderStageVertex];
    }

    void signalFence(id<MTLRenderCommandEncoder> encoder, id<MTLFence> fence) {
        [encoder updateFence:fence afterStages:MTLRenderStageFragment];
    }

    void recycleFence(id<MTLFence> fence) {
        availableFences.push_back(fence);
    }
};
```

## 调试和分析工具

### 1. Metal 调试层

```objc
#if DEBUG
void enableMetalDebugging() {
    // 启用 Metal 验证层
    setenv("MTL_SHADER_VALIDATION", "1", 1);
    setenv("MTL_DEBUG_LAYER", "1", 1);

    // 启用 API 验证
    setenv("MTL_DEBUG_LAYER_VALIDATE_LOAD_ACTIONS", "1", 1);
    setenv("MTL_DEBUG_LAYER_VALIDATE_STORE_ACTIONS", "1", 1);
}
#endif
```

### 2. 性能统计

```objc
class MetalPerformanceCounter {
private:
    CFTimeInterval frameStartTime;
    uint32_t drawCallCount;
    uint32_t triangleCount;

public:
    void beginFrame() {
        frameStartTime = CACurrentMediaTime();
        drawCallCount = 0;
        triangleCount = 0;
    }

    void recordDrawCall(uint32_t triangles) {
        drawCallCount++;
        triangleCount += triangles;
    }

    void endFrame() {
        CFTimeInterval frameTime = CACurrentMediaTime() - frameStartTime;
        double fps = 1.0 / frameTime;

        utils::slog.d << "Frame stats - FPS: " << fps
                     << ", Draw calls: " << drawCallCount
                     << ", Triangles: " << triangleCount;
    }
};
```

这个 Metal 后端实现为 Filament 在 iOS 平台上提供了高性能的图形渲染能力。