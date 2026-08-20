# Filament iOS 平台架构

## 架构概览

Filament 在 iOS 平台上采用分层架构设计，通过平台抽象层和后端驱动系统实现跨平台兼容性，同时充分利用 iOS 平台的原生图形能力。

```
┌─────────────────────────────────────┐
│           iOS 应用层                │
│    (UIViewController, UIView)       │
├─────────────────────────────────────┤
│        Objective-C++ 桥接层         │
│     (FilamentView, Platform)        │
├─────────────────────────────────────┤
│         Filament 引擎核心           │
│   (Engine, Renderer, Scene)         │
├─────────────────────────────────────┤
│        平台抽象层 (Backend)         │
│    (Driver, Platform, SwapChain)    │
├─────────────────────────────────────┤
│          图形后端驱动               │
│      Metal Driver / OpenGL Driver   │
├─────────────────────────────────────┤
│          系统图形框架               │
│        Metal / OpenGL ES            │
└─────────────────────────────────────┘
```

## 核心组件

### 1. 平台抽象层 (Platform Layer)

#### Platform 基类
**位置**: `filament/backend/include/backend/Platform.h`

```cpp
class Platform {
public:
    // 创建图形驱动
    virtual Driver* createDriver(void* sharedContext,
                               const DriverConfig& driverConfig) noexcept = 0;

    // 平台特定配置
    virtual void setPresentationTime(int64_t presentationTimeInNanosecond) noexcept {}
    virtual uint64_t getOSVersion() const noexcept = 0;

    // 外部图像支持
    virtual void createExternalImageTexture(void* externalImage) noexcept {}
};
```

#### iOS 平台实现
**Metal 平台**: `filament/backend/src/metal/PlatformMetal.mm`
**OpenGL 平台**: `filament/backend/src/opengl/platforms/PlatformCocoaTouchGL.mm`

### 2. 图形后端支持

#### Metal 后端
- **支持版本**: iOS 11.0+
- **特性**: 完整的现代图形 API 支持
- **优势**: 最佳性能，Apple 官方推荐

#### OpenGL ES 后端
- **支持版本**: iOS 7.0+（实际测试 iOS 11.0+）
- **特性**: 兼容性较好的传统 API
- **用途**: 设备兼容性后备方案

### 3. 设备兼容性

#### Metal 支持的设备
```
iPhone:
- iPhone 5s 及更新机型 (A7 芯片+)
- 支持 Metal 1.0/2.0/3.0

iPad:
- iPad Air 及更新机型
- iPad mini 2 及更新机型

iPod touch:
- 第 6 代及更新机型
```

#### iOS 模拟器支持
- **Intel Mac**: 完整的 Metal 和 OpenGL 支持
- **Apple Silicon Mac**: 原生 Metal 支持，性能优异

## 渲染管线架构

### 1. 初始化流程

```objc
// 1. 创建引擎实例
Engine* engine = Engine::create(Engine::Backend::METAL);

// 2. 创建渲染器
Renderer* renderer = engine->createRenderer();

// 3. 创建交换链 (SwapChain)
SwapChain* swapChain = engine->createSwapChain(nativeWindow);

// 4. 创建视图和场景
View* view = engine->createView();
Scene* scene = engine->createScene();
```

### 2. 渲染循环

```objc
// 典型的渲染循环 (在 CADisplayLink 回调中)
- (void)render {
    // 1. 开始帧
    if (renderer->beginFrame(swapChain)) {

        // 2. 渲染场景
        renderer->render(view);

        // 3. 结束帧并呈现
        renderer->endFrame();
    }
}
```

### 3. 资源管理

#### 纹理管理
```cpp
// Metal 纹理创建
class MetalTexture : public HwTexture {
    id<MTLTexture> metalTexture;
    MTLPixelFormat pixelFormat;

public:
    void updateImage(const PixelBufferDescriptor& data) override;
    void generateMipmaps() override;
};
```

#### 缓冲区管理
```cpp
// Metal 缓冲区实现
class MetalBuffer : public HwBufferObject {
    id<MTLBuffer> metalBuffer;

public:
    void updateBuffer(const BufferDescriptor& data) override;
    void* map() override;
    void unmap() override;
};
```

## 内存管理策略

### 1. 自动引用计数 (ARC)

#### Objective-C++ 对象
```objc
@interface FilamentView : UIView
@property (nonatomic, strong) CAMetalLayer* metalLayer;
@property (nonatomic, assign) Engine* engine;  // C++ 对象不使用 strong
@end
```

#### Metal 资源管理
```objc
// 自动释放 Metal 对象
id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
// texture 会自动释放，无需手动 release
```

### 2. Filament 对象生命周期

```cpp
class FilamentResourceManager {
    // 确保按正确顺序销毁
    ~FilamentResourceManager() {
        // 1. 销毁渲染器
        engine->destroy(renderer);

        // 2. 销毁场景和视图
        engine->destroy(scene);
        engine->destroy(view);

        // 3. 销毁交换链
        engine->destroy(swapChain);

        // 4. 最后销毁引擎
        Engine::destroy(engine);
    }
};
```

## 线程模型

### 1. 主线程职责
- UI 更新和事件处理
- Filament 命令提交
- 渲染循环控制

### 2. 渲染线程
- Metal 命令编码
- GPU 资源创建和更新
- 着色器编译

### 3. 线程同步

```objc
// 确保在主线程操作 Filament
dispatch_async(dispatch_get_main_queue(), ^{
    // Filament 渲染调用
    renderer->render(view);
});
```

## 图形状态管理

### 1. Metal 渲染状态

```objc
// Metal 管线状态对象 (PSO)
class MetalRenderPipeline {
    id<MTLRenderPipelineState> pipelineState;
    id<MTLDepthStencilState> depthStencilState;

    void bind(id<MTLRenderCommandEncoder> encoder) {
        [encoder setRenderPipelineState:pipelineState];
        [encoder setDepthStencilState:depthStencilState];
    }
};
```

### 2. 状态缓存

```cpp
// 状态对象缓存以避免重复创建
class StateCache {
    std::unordered_map<StateHash, id<MTLRenderPipelineState>> pipelineCache;
    std::unordered_map<StateHash, id<MTLDepthStencilState>> depthStencilCache;

public:
    id<MTLRenderPipelineState> getPipelineState(const PipelineDescriptor& desc);
    id<MTLDepthStencilState> getDepthStencilState(const DepthStencilDescriptor& desc);
};
```

## 性能优化架构

### 1. 命令缓冲区优化

```objc
// 重用命令缓冲区
@property (nonatomic, strong) id<MTLCommandQueue> commandQueue;

- (void)render {
    id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];

    // 批量提交渲染命令
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
        // 命令执行完成回调
    }];

    [commandBuffer commit];
}
```

### 2. 资源池管理

```cpp
// 缓冲区池以减少内存分配
class BufferPool {
    std::vector<id<MTLBuffer>> availableBuffers;
    std::vector<id<MTLBuffer>> usedBuffers;

public:
    id<MTLBuffer> acquireBuffer(size_t size);
    void releaseBuffer(id<MTLBuffer> buffer);
    void reset(); // 帧结束时重置
};
```

## 错误处理和调试

### 1. Metal 验证层

```objc
// 开发时启用 Metal 验证
#if DEBUG
setenv("MTL_SHADER_VALIDATION", "1", 1);
setenv("MTL_DEBUG_LAYER", "1", 1);
#endif
```

### 2. 错误检查

```cpp
// Filament 错误检查机制
class MetalDriver : public Driver {
    bool hasError() const override {
        return [commandQueue.device hasUnifiedMemory] == NO && lastError != nil;
    }

    void checkError(const char* operation) {
        if (lastError) {
            utils::slog.e << "Metal error in " << operation
                         << ": " << [lastError localizedDescription];
        }
    }
};
```

## 平台特定特性

### 1. iOS 设备特性检测

```objc
// 检测设备能力
- (void)detectDeviceCapabilities {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();

    // 检查 Metal 功能集
    if ([device supportsFeatureSet:MTLFeatureSet_iOS_GPUFamily3_v1]) {
        // 支持高级 Metal 特性
    }

    // 检查纹理压缩支持
    if ([device supportsFeatureSet:MTLFeatureSet_iOS_GPUFamily2_v1]) {
        // 支持 ASTC 纹理压缩
    }
}
```

### 2. 内存警告处理

```objc
// 响应内存警告
- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];

    // 清理 Filament 缓存
    engine->flushAndWait();

    // 释放非必要资源
    [self cleanupTextures];
}
```

### 3. 应用状态管理

```objc
// 后台/前台状态处理
- (void)applicationDidEnterBackground {
    // 暂停渲染循环
    displayLink.paused = YES;

    // 完成当前渲染操作
    engine->flushAndWait();
}

- (void)applicationWillEnterForeground {
    // 恢复渲染循环
    displayLink.paused = NO;
}
```

这个架构设计确保了 Filament 在 iOS 平台上的高性能表现和良好的用户体验。