# Filament Web 开发指南

这个目录包含 Filament 在 Web 平台上的全面开发文档，涵盖 WebAssembly、JavaScript 绑定、性能优化和部署等所有方面。

## 📖 文档目录

### 核心架构
- **[01-Web平台架构](./01-Web平台架构.md)** - WebAssembly 架构和 Emscripten 集成
- **[02-JavaScript绑定机制](./02-JavaScript绑定机制.md)** - Embind 桥接层和 API 设计
- **[03-构建系统与工具链](./03-构建系统与工具链.md)** - Emscripten 工具链和 CMake 配置

### 实际应用
- **[04-示例应用详解](./04-示例应用详解.md)** - 11个 Web 示例深度分析
- **[05-与原生平台差异](./05-与原生平台差异.md)** - Web 平台特有限制和差异
- **[06-性能优化指南](./06-性能优化指南.md)** - WASM 性能优化和最佳实践

### 开发实践
- **[07-开发实战指南](./07-开发实战指南.md)** - 完整的 Web 开发流程和部署

## 🌐 快速开始

### 环境要求

- **Emscripten SDK**: 最新稳定版本
- **Node.js**: 16+ (用于开发工具)
- **Python**: 3.6+ (Emscripten 依赖)
- **Web 服务器**: 支持 WASM 的现代浏览器

### 构建 Filament Web

1. **安装 Emscripten**
```bash
# 下载和安装 Emscripten SDK
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

2. **构建 Web 版本**
```bash
# 构建 WebGL 版本
./build.sh -p webgl release

# 构建结果位于
# out/cmake-webgl-release/web/filament-js/
```

3. **运行示例**
```bash
# 启动本地服务器（WASM 需要服务器环境）
cd out/cmake-webgl-release/web/filament-js
python -m http.server 8000

# 访问 http://localhost:8000
```

### 核心组件概览

#### 📦 模块结构
```
web/
├── filament-js/              # JavaScript 绑定库
│   ├── filament.js          # 主要 WASM 模块
│   ├── filament.wasm        # WebAssembly 二进制
│   ├── filament.d.ts        # TypeScript 类型定义
│   └── utilities.js         # 工具函数
├── samples/                 # 示例应用
│   ├── triangle.html        # 基础三角形
│   ├── suzanne.html         # 3D 模型加载
│   ├── helmet.html          # glTF 模型查看器
│   └── ...                  # 其他示例
└── docs/                    # 教程文档
```

#### 🔧 关键特性

- **WebAssembly 引擎**: 接近原生性能的渲染
- **WebGL 2.0 支持**: 现代 Web 图形 API
- **TypeScript 支持**: 完整的类型定义
- **模块化加载**: 按需加载资源和功能
- **CDN 友好**: 支持 unpkg 和 jsdelivr

#### 🌍 浏览器支持

- **桌面浏览器**: Chrome 61+, Firefox 60+, Safari 11+, Edge 79+
- **移动浏览器**: iOS Safari 11+, Chrome Mobile 61+
- **必需特性**: WebGL 2.0, WebAssembly

### API 使用示例

#### 基础渲染设置

```html
<!DOCTYPE html>
<html>
<head>
    <title>Filament Web App</title>
    <style>
        canvas { width: 100%; height: 100vh; }
    </style>
</head>
<body>
    <canvas id="canvas"></canvas>
    <script src="filament.js"></script>
    <script>
        Filament.init(['material.filamat'], () => {
            const app = new FilamentApp();
            app.run();
        });
    </script>
</body>
</html>
```

#### JavaScript 应用类

```javascript
class FilamentApp {
    constructor() {
        const canvas = document.getElementById('canvas');

        // 创建引擎实例
        this.engine = Filament.Engine.create(canvas);
        this.scene = this.engine.createScene();
        this.renderer = this.engine.createRenderer();
        this.camera = this.engine.createCamera(Filament.EntityManager.get().create());
        this.view = this.engine.createView();

        this.setupView();
        this.createGeometry();
    }

    setupView() {
        this.view.setCamera(this.camera);
        this.view.setScene(this.scene);
        this.view.setSampleCount(4); // 4x MSAA

        // 设置清除选项
        this.renderer.setClearOptions({
            clearColor: [0.1, 0.125, 0.25, 1.0],
            clear: true
        });
    }

    createGeometry() {
        // 创建三角形几何体
        const positions = new Float32Array([
            0, 1, 0,
            -1, -1, 0,
            1, -1, 0
        ]);

        const colors = new Uint32Array([
            0xffff0000, // 红色
            0xff00ff00, // 绿色
            0xff0000ff  // 蓝色
        ]);

        // 创建顶点缓冲区
        this.vertexBuffer = Filament.VertexBuffer.Builder()
            .vertexCount(3)
            .bufferCount(2)
            .attribute(Filament.VertexAttribute.POSITION, 0,
                      Filament.VertexBuffer$AttributeType.FLOAT3, 0, 12)
            .attribute(Filament.VertexAttribute.COLOR, 1,
                      Filament.VertexBuffer$AttributeType.UBYTE4, 0, 4)
            .normalized(Filament.VertexAttribute.COLOR)
            .build(this.engine);

        this.vertexBuffer.setBufferAt(this.engine, 0, positions);
        this.vertexBuffer.setBufferAt(this.engine, 1, colors);

        // 创建索引缓冲区
        this.indexBuffer = Filament.IndexBuffer.Builder()
            .indexCount(3)
            .bufferType(Filament.IndexBuffer$IndexType.USHORT)
            .build(this.engine);

        this.indexBuffer.setBuffer(this.engine, new Uint16Array([0, 1, 2]));
    }

    run() {
        const render = () => {
            this.renderer.render(this.view);
            requestAnimationFrame(render);
        };
        requestAnimationFrame(render);
    }
}
```

#### 材质和纹理加载

```javascript
// 异步加载材质
async function loadMaterial(engine, url) {
    const response = await fetch(url);
    const arrayBuffer = await response.arrayBuffer();
    const uint8Array = new Uint8Array(arrayBuffer);

    return engine.createMaterial(uint8Array);
}

// 加载纹理
function loadTexture(engine, imageData) {
    return Filament.Texture.Builder()
        .width(imageData.width)
        .height(imageData.height)
        .levels(1)
        .format(Filament.Texture$InternalFormat.RGBA8)
        .build(engine);
}
```

## 📚 学习路径建议

### 初学者
1. 从 **[Web平台架构](./01-Web平台架构.md)** 开始了解 WASM 基础
2. 学习 **[示例应用详解](./04-示例应用详解.md)** 中的 triangle.html
3. 参考 **[开发实战指南](./07-开发实战指南.md)** 创建第一个应用

### 进阶开发者
1. 深入研究 **[JavaScript绑定机制](./02-JavaScript绑定机制.md)** 了解 Embind
2. 学习 **[构建系统与工具链](./03-构建系统与工具链.md)** 进行定制构建
3. 掌握 **[性能优化指南](./06-性能优化指南.md)** 提升性能

### 企业开发
1. 了解 **[与原生平台差异](./05-与原生平台差异.md)** 规避限制
2. 建立 CI/CD 流程和 CDN 部署
3. 实施性能监控和错误追踪

## 🛠️ 常见问题

### 构建问题
- **Emscripten 版本不匹配**: 使用项目推荐的 Emscripten 版本
- **内存不足**: 增加 Node.js 堆大小 `--max-old-space-size=8192`
- **链接错误**: 检查 CMake 配置和依赖库

### 运行时问题
- **WASM 加载失败**: 确保通过 HTTP 服务器访问，不能直接打开文件
- **WebGL 错误**: 检查浏览器 WebGL 2.0 支持
- **内存泄漏**: 确保正确调用 `.delete()` 方法清理 C++ 对象

### 性能问题
- **帧率低**: 使用浏览器开发者工具分析瓶颈
- **加载慢**: 启用 gzip 压缩和 CDN 缓存
- **内存占用高**: 优化纹理大小和几何体复杂度

## 🚀 部署选项

### CDN 部署
```html
<!-- 使用 unpkg CDN -->
<script src="https://unpkg.com/filament@latest/filament.js"></script>

<!-- 使用 jsdelivr CDN -->
<script src="https://cdn.jsdelivr.net/npm/filament@latest/filament.js"></script>
```

### npm 包管理
```bash
# 安装 Filament
npm install filament

# 在项目中使用
import * as Filament from 'filament';
```

### 自托管部署
```nginx
# Nginx 配置示例
location ~* \.(wasm)$ {
    add_header Content-Type application/wasm;
    add_header Cross-Origin-Embedder-Policy require-corp;
    add_header Cross-Origin-Opener-Policy same-origin;
}
```

## 📖 相关资源

- **[Filament 官方文档](https://google.github.io/filament/)**
- **[Emscripten 文档](https://emscripten.org/docs/)**
- **[WebAssembly 参考](https://webassembly.org/)**
- **[WebGL 2.0 规范](https://www.khronos.org/webgl/)**
- **[在线演示](https://google.github.io/filament/viewer/)**

---

**提示**: Web 平台的性能和功能会持续改进。建议定期查看最新版本的特性和优化。