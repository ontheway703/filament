# Filament Web 平台架构

## 1. 整体架构概览

Filament Web 平台采用了基于 WebAssembly (WASM) 的架构设计，将原生 C++ 引擎编译为 Web 平台可执行的字节码。整体架构包含以下几个核心层次：

```
┌─────────────────────────────────────────┐
│           Web 应用层 (HTML/CSS/JS)        │
├─────────────────────────────────────────┤
│        JavaScript 绑定层 (Embind)        │
├─────────────────────────────────────────┤
│         WebAssembly 运行时             │
├─────────────────────────────────────────┤
│         Filament 核心引擎              │
├─────────────────────────────────────────┤
│          WebGL 2.0 后端               │
├─────────────────────────────────────────┤
│           浏览器平台层                  │
└─────────────────────────────────────────┘
```

### 1.1 核心组件架构

**1. Filament 核心引擎**
- 与原生版本共享相同的 C++ 代码库
- 包含渲染器、材质系统、光照系统等
- 通过条件编译适配 Web 平台特性

**2. WebAssembly 运行时**
- 由 Emscripten 编译生成的 WASM 模块
- 提供接近原生的性能表现
- 支持内存动态增长和垃圾回收

**3. JavaScript 绑定层**
- 使用 Embind 技术实现 C++ 与 JavaScript 的双向绑定
- 提供类型安全的 API 调用接口
- 处理内存管理和对象生命周期

**4. WebGL 2.0 后端**
- 提供跨平台的图形 API 抽象
- 支持现代渲染特性（如 Uniform Buffer Objects）
- 兼容不同厂商的 WebGL 实现

## 2. 模块化设计

### 2.1 核心模块结构

```javascript
// 模块加载示例
import { Filament } from './filament.js';

Filament().then((Module) => {
    // 引擎核心模块
    const engine = Module.Engine.create();

    // 渲染器模块
    const renderer = Module.Renderer.create(engine);

    // 场景管理模块
    const scene = Module.Scene.create(engine);

    // 相机系统模块
    const camera = Module.Camera.create(engine);
});
```

### 2.2 主要模块分类

**渲染核心模块**
- `Engine`: 引擎实例管理
- `Renderer`: 渲染器和渲染管道
- `Scene`: 场景图管理
- `View`: 视口和相机配置

**资源管理模块**
- `VertexBuffer`: 顶点缓冲区
- `IndexBuffer`: 索引缓冲区
- `Material`: 材质系统
- `Texture`: 纹理管理

**几何和数学模块**
- `EntityManager`: 实体组件系统
- `TransformManager`: 变换组件
- `RenderableManager`: 可渲染组件

**工具和扩展模块**
- `gltfio`: glTF 文件加载器
- `viewer`: 内置查看器组件
- `ktxreader`: KTX 纹理加载器

## 3. 内存管理架构

### 3.1 双重内存模型

Filament Web 采用了 JavaScript 堆 + WebAssembly 线性内存的双重模型：

```javascript
// JavaScript 对象 (垃圾回收)
const jsTexture = new Image();
jsTexture.src = 'texture.png';

// WebAssembly 对象 (手动管理)
const wasmTexture = Filament.Texture$Builder(engine)
    .width(512)
    .height(512)
    .levels(1)
    .build();

// 需要显式释放 WASM 对象
wasmTexture.delete();
```

### 3.2 自动内存管理

```javascript
// 使用 RAII 风格的包装器
class AutoManagedTexture {
    constructor(engine, width, height) {
        this.texture = Filament.Texture$Builder(engine)
            .width(width)
            .height(height)
            .build();
    }

    // 析构函数模拟
    dispose() {
        if (this.texture) {
            this.texture.delete();
            this.texture = null;
        }
    }
}

// 使用 WeakRef 实现自动清理
const registry = new FinalizationRegistry((texture) => {
    texture.delete();
});
```

## 4. 渲染管道架构

### 4.1 渲染流程

```javascript
function renderFrame(engine, renderer, scene, view, camera) {
    // 1. 更新场景状态
    engine.transformManager.setTransform(
        engine.transformManager.getInstance(entity),
        transform
    );

    // 2. 准备渲染
    renderer.beginFrame(swapChain);

    // 3. 执行渲染
    renderer.render(view);

    // 4. 完成帧渲染
    renderer.endFrame();
}
```

### 4.2 多通道渲染支持

```javascript
// 阴影通道渲染
const shadowMapView = engine.createView();
shadowMapView.setScene(scene);
shadowMapView.setCamera(shadowCamera);
shadowMapView.setViewport([0, 0, shadowMapSize, shadowMapSize]);
shadowMapView.setRenderTarget(shadowMapTarget);

// 主通道渲染
const mainView = engine.createView();
mainView.setScene(scene);
mainView.setCamera(mainCamera);
mainView.setViewport([0, 0, canvas.width, canvas.height]);
```

## 5. 并发和异步架构

### 5.1 单线程限制

由于 Web 平台的单线程特性，Filament Web 采用了以下策略：

```javascript
// 异步资源加载
async function loadAssets() {
    const [gltfData, iblData, textureData] = await Promise.all([
        fetch('model.gltf').then(r => r.arrayBuffer()),
        fetch('environment.ktx').then(r => r.arrayBuffer()),
        fetch('texture.png').then(r => r.blob())
    ]);

    // 批量创建资源
    const gltf = gltfLoader.createAssetFromBinary(gltfData);
    const ibl = ktxLoader.load(iblData);
    const texture = createTextureFromBlob(textureData);

    return { gltf, ibl, texture };
}
```

### 5.2 任务分片处理

```javascript
// 将耗时操作分片处理
class TaskScheduler {
    constructor() {
        this.tasks = [];
        this.isRunning = false;
    }

    addTask(task) {
        this.tasks.push(task);
        this.scheduleExecution();
    }

    scheduleExecution() {
        if (this.isRunning) return;
        this.isRunning = true;

        requestIdleCallback((deadline) => {
            while (deadline.timeRemaining() > 0 && this.tasks.length > 0) {
                const task = this.tasks.shift();
                task();
            }

            this.isRunning = false;
            if (this.tasks.length > 0) {
                this.scheduleExecution();
            }
        });
    }
}
```

## 6. 平台适配架构

### 6.1 特性检测

```javascript
// WebGL 特性检测
class WebGLCapabilities {
    constructor(gl) {
        this.maxTextureSize = gl.getParameter(gl.MAX_TEXTURE_SIZE);
        this.maxRenderBufferSize = gl.getParameter(gl.MAX_RENDERBUFFER_SIZE);
        this.maxVaryingVectors = gl.getParameter(gl.MAX_VARYING_VECTORS);

        // 扩展检测
        this.hasInstancedArrays = !!gl.getExtension('ANGLE_instanced_arrays');
        this.hasVertexArrayObjects = !!gl.getExtension('OES_vertex_array_object');
        this.hasFloatTextures = !!gl.getExtension('OES_texture_float');
    }

    configureBestSettings() {
        const config = {
            shadowMapSize: Math.min(2048, this.maxTextureSize / 4),
            maxLights: this.maxVaryingVectors > 16 ? 8 : 4,
            useInstancing: this.hasInstancedArrays
        };
        return config;
    }
}
```

### 6.2 回退机制

```javascript
// 渲染质量回退
class QualityManager {
    constructor(capabilities) {
        this.capabilities = capabilities;
        this.currentLevel = this.detectOptimalQuality();
    }

    detectOptimalQuality() {
        if (this.capabilities.maxTextureSize >= 4096) {
            return 'high';
        } else if (this.capabilities.maxTextureSize >= 2048) {
            return 'medium';
        } else {
            return 'low';
        }
    }

    getSettings(level) {
        const settings = {
            'high': {
                shadowMapSize: 2048,
                maxLights: 8,
                useSSAO: true,
                bloomEnabled: true
            },
            'medium': {
                shadowMapSize: 1024,
                maxLights: 4,
                useSSAO: true,
                bloomEnabled: false
            },
            'low': {
                shadowMapSize: 512,
                maxLights: 2,
                useSSAO: false,
                bloomEnabled: false
            }
        };
        return settings[level];
    }
}
```

## 7. 调试和开发工具架构

### 7.1 调试信息输出

```javascript
// 调试模式配置
const debugConfig = {
    enableStats: true,
    showFrameGraph: true,
    logRenderCalls: true,
    enableGPUTiming: true
};

// 性能统计
class PerformanceMonitor {
    constructor(engine) {
        this.engine = engine;
        this.frameCount = 0;
        this.lastTime = performance.now();
    }

    update() {
        this.frameCount++;
        const currentTime = performance.now();

        if (currentTime - this.lastTime >= 1000) {
            const fps = this.frameCount / ((currentTime - this.lastTime) / 1000);
            console.log(`FPS: ${fps.toFixed(1)}`);

            this.frameCount = 0;
            this.lastTime = currentTime;
        }
    }
}
```

### 7.2 错误处理架构

```javascript
// 全局错误处理
class ErrorHandler {
    constructor() {
        window.addEventListener('unhandledrejection', this.handlePromiseRejection);
        window.addEventListener('error', this.handleError);
    }

    handlePromiseRejection(event) {
        console.error('未处理的 Promise 拒绝:', event.reason);
        this.reportError('promise_rejection', event.reason);
    }

    handleError(event) {
        console.error('JavaScript 错误:', event.error);
        this.reportError('javascript_error', event.error);
    }

    reportError(type, error) {
        // 发送错误报告到分析服务
        fetch('/api/error-report', {
            method: 'POST',
            body: JSON.stringify({
                type,
                message: error.message,
                stack: error.stack,
                userAgent: navigator.userAgent,
                timestamp: Date.now()
            })
        });
    }
}
```

## 8. 总结

Filament Web 的架构设计体现了以下几个核心原则：

1. **模块化设计**: 清晰的模块边界和职责分离
2. **内存安全**: 双重内存模型和自动管理机制
3. **性能优化**: 异步加载和任务分片处理
4. **平台适配**: 特性检测和渐进式降级
5. **开发友好**: 完善的调试工具和错误处理

这种架构设计使得 Filament 能够在 Web 平台上提供接近原生性能的 3D 渲染能力，同时保持良好的开发体验和跨浏览器兼容性。