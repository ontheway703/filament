# Web 集成详解

## 概述

Filament 通过 **WebAssembly** 和 **WebGL 2.0** 将高质量 PBR 渲染带到 Web 平台。本文档详细介绍如何使用 Emscripten 编译 Filament 到 WebAssembly,并集成到网页应用中。

**主要内容**:
- ✅ Emscripten 环境配置
- ✅ Filament WebAssembly 编译
- ✅ HTML Canvas 集成
- ✅ JavaScript/TypeScript API 使用
- ✅ Web Workers 多线程支持
- ✅ 资源加载和优化
- ✅ 性能优化和调试技巧

**浏览器支持**:
- Chrome 57+ (推荐 Chrome 90+)
- Firefox 51+ (推荐 Firefox 78+)
- Safari 15+ (macOS/iOS)
- Edge 79+ (Chromium 版本)

**前置知识**:
- JavaScript/TypeScript 基础
- WebGL 基础 (可选)
- CMake 和 C++ 构建系统 (可选)

## 环境配置

### 安装 Emscripten SDK

```bash
# 克隆 emsdk
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk

# 安装最新版本
./emsdk install latest

# 激活最新版本
./emsdk activate latest

# 设置环境变量 (每次新终端都需要)
source ./emsdk_env.sh

# 或永久添加到 ~/.bashrc 或 ~/.zshrc
echo 'source "/path/to/emsdk/emsdk_env.sh"' >> ~/.bashrc

# 验证安装
emcc --version
# 输出: emcc (Emscripten gcc/clang-like replacement) 3.1.50
```

### 安装构建工具

```bash
# macOS
brew install cmake ninja

# Ubuntu/Debian
sudo apt install cmake ninja-build

# 验证
cmake --version
ninja --version
```

## 编译 Filament to WebAssembly

### 方式 1: 使用 build.sh (推荐)

```bash
# 克隆 Filament
git clone https://github.com/google/filament.git
cd filament

# 激活 Emscripten 环境
source /path/to/emsdk/emsdk_env.sh

# 编译 WebAssembly 版本
./build.sh -p webgl release

# 编译产物
# out/cmake-webgl-release/web/filament-js/
#   ├── filament.js          # JavaScript 绑定
#   ├── filament.wasm        # WebAssembly 二进制
#   ├── filament.d.ts        # TypeScript 类型定义
#   └── ...
```

### 方式 2: 手动 CMake 构建

```bash
mkdir out/cmake-webgl-release
cd out/cmake-webgl-release

# 配置 CMake
emcmake cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DFILAMENT_ENABLE_JAVA=OFF \
    -DFILAMENT_SKIP_SAMPLES=ON \
    ../..

# 构建
ninja

# 构建产物在 web/filament-js/
```

### 编译选项

```bash
# 最小化构建 (减小 WASM 文件大小)
./build.sh -p webgl release

# 优化大小
emcmake cmake \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DFILAMENT_ENABLE_LTO=ON \
    ..

# 优化性能
emcmake cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DFILAMENT_ENABLE_LTO=ON \
    ..
```

## HTML Canvas 集成

### 基本 HTML 结构

**index.html:**

```html
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Filament WebGL Demo</title>
    <style>
        body {
            margin: 0;
            padding: 0;
            overflow: hidden;
        }
        #canvas {
            width: 100vw;
            height: 100vh;
            display: block;
        }
    </style>
</head>
<body>
    <canvas id="canvas"></canvas>
    <script src="filament.js"></script>
    <script src="app.js"></script>
</body>
</html>
```

### JavaScript 应用

**app.js:**

```javascript
// 等待 Filament 模块加载
Filament.init(['filament.wasm'], () => {
    // 获取 Canvas
    const canvas = document.getElementById('canvas');

    // 创建 Filament 引擎
    const engine = Filament.Engine.create(canvas);

    // 创建 SwapChain
    const swapChain = engine.createSwapChain();

    // 创建 Renderer
    const renderer = engine.createRenderer();

    // 创建 Scene
    const scene = engine.createScene();

    // 创建 View
    const view = engine.createView();
    view.setScene(scene);

    // 创建 Camera
    const camera = engine.createCamera(Filament.EntityManager.get().create());
    view.setCamera(camera);

    // 设置相机位置
    const eye = [0, 0, 4];
    const center = [0, 0, 0];
    const up = [0, 1, 0];
    camera.lookAt(eye, center, up);

    // 设置 Viewport
    const resize = () => {
        const dpr = window.devicePixelRatio;
        const width = canvas.width = window.innerWidth * dpr;
        const height = canvas.height = window.innerHeight * dpr;
        canvas.style.width = window.innerWidth + 'px';
        canvas.style.height = window.innerHeight + 'px';

        view.setViewport([0, 0, width, height]);

        const aspect = width / height;
        const Projection = Filament.Camera$Projection;
        const Fov = Filament.Camera$Fov;
        camera.setProjection(Projection.PERSPECTIVE, 45, aspect, 0.1, 100, Fov.VERTICAL);
    };

    window.addEventListener('resize', resize);
    resize();

    // 渲染循环
    const render = () => {
        renderer.beginFrame(swapChain);
        renderer.render(view);
        renderer.endFrame();

        requestAnimationFrame(render);
    };

    render();
});
```

## TypeScript 集成

### 类型定义

**app.ts:**

```typescript
/// <reference types="filament" />

async function init() {
    // 初始化 Filament
    await new Promise<void>((resolve) => {
        Filament.init(['filament.wasm'], () => resolve());
    });

    const canvas = document.getElementById('canvas') as HTMLCanvasElement;
    const engine = Filament.Engine.create(canvas);

    const swapChain = engine.createSwapChain();
    const renderer = engine.createRenderer();
    const scene = engine.createScene();
    const view = engine.createView();

    view.setScene(scene);

    // 创建相机
    const cameraEntity = Filament.EntityManager.get().create();
    const camera = engine.createCamera(cameraEntity);
    view.setCamera(camera);

    // 设置相机
    camera.lookAt(
        [0, 0, 4],  // eye
        [0, 0, 0],  // center
        [0, 1, 0]   // up
    );

    // 设置视口
    const resize = () => {
        const dpr = window.devicePixelRatio;
        const width = canvas.width = window.innerWidth * dpr;
        const height = canvas.height = window.innerHeight * dpr;

        view.setViewport([0, 0, width, height]);
        camera.setProjection(
            Filament.Camera$Projection.PERSPECTIVE,
            45,
            width / height,
            0.1,
            100,
            Filament.Camera$Fov.VERTICAL
        );
    };

    window.addEventListener('resize', resize);
    resize();

    // 渲染循环
    function render() {
        renderer.beginFrame(swapChain);
        renderer.render(view);
        renderer.endFrame();

        requestAnimationFrame(render);
    }

    render();
}

init().catch(console.error);
```

### tsconfig.json

```json
{
    "compilerOptions": {
        "target": "ES2020",
        "module": "ES2020",
        "moduleResolution": "node",
        "lib": ["ES2020", "DOM"],
        "types": ["filament"],
        "strict": true,
        "esModuleInterop": true,
        "skipLibCheck": true
    },
    "include": ["src/**/*"],
    "exclude": ["node_modules"]
}
```

## 加载 glTF 模型

```javascript
Filament.init(['filament.wasm', 'filament.wasm'], async () => {
    const engine = Filament.Engine.create(canvas);
    const scene = engine.createScene();

    // 加载 glTF
    const assetLoader = engine.createAssetLoader();

    // 获取 glTF 文件
    const response = await fetch('model.glb');
    const arrayBuffer = await response.arrayBuffer();
    const uint8Array = new Uint8Array(arrayBuffer);

    // 解析 glTF
    const asset = assetLoader.createAsset(uint8Array.buffer);

    // 加载外部资源
    const resourceLoader = engine.createResourceLoader();

    // 等待资源加载完成
    while (!resourceLoader.asyncDone()) {
        await new Promise(resolve => setTimeout(resolve, 10));
    }

    // 添加到场景
    const entities = asset.getEntities();
    for (let i = 0; i < entities.length; i++) {
        scene.addEntity(entities[i]);
    }

    // 设置相机看向模型
    const aabb = asset.getBoundingBox();
    const center = aabb.center;
    const halfExtent = aabb.halfExtent;
    const maxExtent = Math.max(halfExtent[0], halfExtent[1], halfExtent[2]);

    camera.lookAt(
        [center[0], center[1], center[2] + maxExtent * 3],
        center,
        [0, 1, 0]
    );
});
```

## 资源加载

### 异步加载资源

```javascript
async function loadResources(engine) {
    const [modelData, matData, iblData] = await Promise.all([
        fetch('model.glb').then(r => r.arrayBuffer()),
        fetch('material.filamat').then(r => r.arrayBuffer()),
        fetch('ibl.ktx').then(r => r.arrayBuffer())
    ]);

    // 创建材质
    const material = engine.createMaterial(new Uint8Array(matData).buffer);

    // 加载 IBL
    const ibl = engine.createIblFromKtx(new Uint8Array(iblData).buffer);
    scene.setIndirectLight(ibl);

    return { material, ibl };
}
```

### 进度显示

```javascript
async function loadWithProgress(url, onProgress) {
    const response = await fetch(url);
    const reader = response.body.getReader();
    const contentLength = +response.headers.get('Content-Length');

    let receivedLength = 0;
    const chunks = [];

    while (true) {
        const { done, value } = await reader.read();

        if (done) break;

        chunks.push(value);
        receivedLength += value.length;

        const progress = receivedLength / contentLength;
        onProgress(progress);
    }

    const chunksAll = new Uint8Array(receivedLength);
    let position = 0;
    for (const chunk of chunks) {
        chunksAll.set(chunk, position);
        position += chunk.length;
    }

    return chunksAll.buffer;
}

// 使用
const modelData = await loadWithProgress('large-model.glb', (progress) => {
    console.log(`加载进度: ${(progress * 100).toFixed(2)}%`);
    document.getElementById('progress').style.width = (progress * 100) + '%';
});
```

## 性能优化

### WASM 文件大小优化

```bash
# 使用 wasm-opt 优化
wasm-opt -Oz filament.wasm -o filament.opt.wasm

# 启用 LTO (Link Time Optimization)
emcmake cmake \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DFILAMENT_ENABLE_LTO=ON \
    ..

# 移除调试信息
emcmake cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-g0" \
    ..
```

### Gzip/Brotli 压缩

**服务器配置 (Nginx):**

```nginx
# nginx.conf
gzip on;
gzip_types application/wasm application/javascript;
gzip_min_length 1000;

# Brotli (推荐)
brotli on;
brotli_types application/wasm application/javascript;
brotli_comp_level 6;
```

**文件大小对比:**

```
filament.wasm          : 5.2 MB
filament.wasm.gz       : 1.8 MB (gzip)
filament.wasm.br       : 1.5 MB (brotli)
```

### 延迟加载

```javascript
// 仅在需要时加载 Filament
async function initWhenNeeded() {
    const startButton = document.getElementById('start');

    startButton.addEventListener('click', async () => {
        // 显示加载提示
        document.getElementById('loading').style.display = 'block';

        // 动态加载 Filament
        await loadScript('filament.js');

        // 初始化
        Filament.init(['filament.wasm'], () => {
            document.getElementById('loading').style.display = 'none';
            initApp();
        });
    });
}

function loadScript(url) {
    return new Promise((resolve, reject) => {
        const script = document.createElement('script');
        script.src = url;
        script.onload = resolve;
        script.onerror = reject;
        document.body.appendChild(script);
    });
}
```

### 帧率优化

```javascript
let lastTime = 0;
const targetFPS = 60;
const frameTime = 1000 / targetFPS;

function render(currentTime) {
    const deltaTime = currentTime - lastTime;

    if (deltaTime >= frameTime) {
        renderer.beginFrame(swapChain);
        renderer.render(view);
        renderer.endFrame();

        lastTime = currentTime - (deltaTime % frameTime);
    }

    requestAnimationFrame(render);
}

requestAnimationFrame(render);
```

## Web Workers 支持

### 主线程

**main.js:**

```javascript
const worker = new Worker('worker.js');

worker.postMessage({
    type: 'init',
    canvas: canvas.transferControlToOffscreen()
}, [canvas]);

worker.onmessage = (e) => {
    if (e.data.type === 'ready') {
        console.log('Worker ready');
    }
};
```

### Worker 线程

**worker.js:**

```javascript
let engine, renderer, view;

self.onmessage = async (e) => {
    if (e.data.type === 'init') {
        const canvas = e.data.canvas;

        // 初始化 Filament (Worker 中)
        await new Promise(resolve => {
            Filament.init(['filament.wasm'], resolve);
        });

        engine = Filament.Engine.create(canvas);
        renderer = engine.createRenderer();
        view = engine.createView();

        // 开始渲染循环
        render();

        self.postMessage({ type: 'ready' });
    }
};

function render() {
    renderer.beginFrame(swapChain);
    renderer.render(view);
    renderer.endFrame();

    requestAnimationFrame(render);
}
```

## 调试技巧

### 浏览器开发者工具

```javascript
// 启用性能监控
const stats = {
    fps: 0,
    frameTime: 0,
    memory: 0
};

let frameCount = 0;
let lastFpsUpdate = performance.now();

function render(currentTime) {
    // 渲染
    renderer.beginFrame(swapChain);
    renderer.render(view);
    renderer.endFrame();

    // 统计
    frameCount++;
    const elapsed = currentTime - lastFpsUpdate;

    if (elapsed >= 1000) {
        stats.fps = (frameCount / elapsed) * 1000;
        stats.frameTime = elapsed / frameCount;
        stats.memory = performance.memory?.usedJSHeapSize / (1024 * 1024);

        console.log(`FPS: ${stats.fps.toFixed(1)}, ` +
                    `Frame: ${stats.frameTime.toFixed(2)}ms, ` +
                    `Memory: ${stats.memory.toFixed(2)}MB`);

        frameCount = 0;
        lastFpsUpdate = currentTime;
    }

    requestAnimationFrame(render);
}
```

### Chrome DevTools Performance

```bash
# 1. 打开 Chrome DevTools (F12)
# 2. 切换到 Performance 标签
# 3. 点击 Record 开始录制
# 4. 操作应用
# 5. 点击 Stop 停止录制
# 6. 分析火焰图,查找性能瓶颈
```

### WebGL 调试

```javascript
// 启用 WebGL 错误检查
const canvas = document.getElementById('canvas');
const gl = canvas.getContext('webgl2', {
    alpha: false,
    antialias: true,
    depth: true,
    failIfMajorPerformanceCaveat: true
});

// 检查 WebGL 2.0 支持
if (!gl) {
    console.error('WebGL 2.0 not supported');
    alert('您的浏览器不支持 WebGL 2.0');
}

// 检查扩展
const extensions = gl.getSupportedExtensions();
console.log('Supported extensions:', extensions);
```

## 常见问题

### Q1: WASM 加载失败

**问题:**
```
failed to asynchronously prepare wasm
```

**解决方案:**

```javascript
// 确保 MIME 类型正确
// 服务器配置 (Apache .htaccess):
AddType application/wasm .wasm

// 或使用 fetch API 预加载
Filament.init([
    fetch('filament.wasm').then(r => r.arrayBuffer())
], () => {
    // 初始化成功
});
```

### Q2: CORS 错误

**问题:**
```
Cross-Origin Request Blocked
```

**解决方案:**

```javascript
// 开发环境使用本地服务器
// 使用 Python:
python3 -m http.server 8000

// 使用 Node.js http-server:
npx http-server -p 8000

// 生产环境配置 CORS
// Nginx:
add_header Access-Control-Allow-Origin *;
```

### Q3: 性能低下

**优化检查清单:**

```javascript
// 1. 降低分辨率
const dpr = Math.min(window.devicePixelRatio, 2);

// 2. 禁用抗锯齿
const engine = Filament.Engine.create(canvas, {
    antialias: false
});

// 3. 减少材质复杂度
// 4. 使用 LOD
// 5. 启用 Frustum Culling
view.setFrustumCullingEnabled(true);
```

### Q4: 内存泄漏

**解决方案:**

```javascript
// 正确销毁资源
function cleanup() {
    engine.destroyView(view);
    engine.destroyScene(scene);
    engine.destroyRenderer(renderer);
    engine.destroySwapChain(swapChain);

    // 销毁所有实体
    const entities = scene.getEntities();
    for (let i = 0; i < entities.length; i++) {
        engine.destroyEntity(entities[i]);
    }

    Filament.Engine.destroy(engine);
}

// 监听页面卸载
window.addEventListener('beforeunload', cleanup);
```

## 最佳实践

### 1. 资源预加载

```javascript
async function preload() {
    const resources = [
        'filament.wasm',
        'model.glb',
        'material.filamat',
        'environment.ktx'
    ];

    const promises = resources.map(url =>
        fetch(url).then(r => r.arrayBuffer())
    );

    return Promise.all(promises);
}
```

### 2. 错误处理

```javascript
Filament.init(['filament.wasm'], init, (error) => {
    console.error('Filament initialization failed:', error);
    document.getElementById('error').textContent = '加载失败,请刷新页面重试';
});
```

### 3. 响应式设计

```javascript
const resize = () => {
    const dpr = window.devicePixelRatio;
    const width = window.innerWidth * dpr;
    const height = window.innerHeight * dpr;

    canvas.width = width;
    canvas.height = height;
    canvas.style.width = window.innerWidth + 'px';
    canvas.style.height = window.innerHeight + 'px';

    view.setViewport([0, 0, width, height]);
    camera.setProjection(
        Filament.Camera$Projection.PERSPECTIVE,
        45,
        width / height,
        0.1,
        100,
        Filament.Camera$Fov.VERTICAL
    );
};

window.addEventListener('resize', resize);
resize();
```

### 4. 移动端优化

```javascript
// 检测移动设备
const isMobile = /Android|webOS|iPhone|iPad|iPod/i.test(navigator.userAgent);

if (isMobile) {
    // 降低质量
    const dpr = 1;  // 固定为 1x
    // 禁用抗锯齿
    // 减少阴影质量
}
```

## 相关文档

- [01-platform-overview.md](./01-platform-overview.md) - 平台支持概览
- [08-swapchain-创建.md](./08-swapchain-创建.md) - SwapChain 详解
- [../backend/04-webgl-backend.md](../backend/04-webgl-backend.md) - WebGL Backend 详解
- [../tools/06-gltf-tools.md](../tools/06-gltf-tools.md) - glTF 工具链
