# Filament Web JavaScript 绑定机制

## 1. Embind 绑定框架

Filament Web 使用 Emscripten 的 Embind 技术将 C++ API 暴露给 JavaScript。这种绑定机制提供了类型安全、自动内存管理和高性能的 C++/JavaScript 互操作能力。

### 1.1 Embind 基础概念

```cpp
// jsbindings.cpp - C++ 绑定代码示例
#include <emscripten/bind.h>
#include <filament/Engine.h>
#include <filament/Renderer.h>

using namespace emscripten;
using namespace filament;

EMSCRIPTEN_BINDINGS(filament) {
    // 引擎类绑定
    class_<Engine>("Engine")
        .class_function("create", &Engine::create,
            allow_raw_pointers())
        .function("destroy", &Engine::destroy)
        .function("createRenderer", &Engine::createRenderer,
            allow_raw_pointers())
        .function("createScene", &Engine::createScene,
            allow_raw_pointers());

    // 渲染器类绑定
    class_<Renderer>("Renderer")
        .function("beginFrame", &Renderer::beginFrame)
        .function("endFrame", &Renderer::endFrame)
        .function("render", &Renderer::render);
}
```

### 1.2 嵌套类命名约定

Filament Web 使用 `$` 符号来表示 C++ 嵌套类：

```cpp
// C++ 嵌套类
class Texture {
public:
    class Builder {
    public:
        Builder& width(uint32_t width);
        Builder& height(uint32_t height);
        Texture* build(Engine& engine);
    };
};

// JavaScript 中的对应调用
EMSCRIPTEN_BINDINGS(texture) {
    class_<Texture>("Texture")
        .function("delete", &Texture::~Texture);

    class_<Texture::Builder>("Texture$Builder")
        .constructor<Engine&>()
        .function("width", &Texture::Builder::width)
        .function("height", &Texture::Builder::height)
        .function("build", &Texture::Builder::build,
            allow_raw_pointers());
}
```

```javascript
// JavaScript 使用方式
const textureBuilder = new Filament.Texture$Builder(engine);
const texture = textureBuilder
    .width(512)
    .height(512)
    .build();
```

## 2. 类型系统绑定

### 2.1 基础类型映射

```cpp
// 基础类型绑定
EMSCRIPTEN_BINDINGS(types) {
    // 枚举类型
    enum_<Backend>("Backend")
        .value("DEFAULT", Backend::DEFAULT)
        .value("OPENGL", Backend::OPENGL)
        .value("VULKAN", Backend::VULKAN)
        .value("METAL", Backend::METAL);

    // 数值类型
    register_vector<float>("FloatVector");
    register_vector<uint32_t>("Uint32Vector");

    // 结构体类型
    value_object<math::float3>("float3")
        .field("x", &math::float3::x)
        .field("y", &math::float3::y)
        .field("z", &math::float3::z);

    value_object<math::float4>("float4")
        .field("x", &math::float4::x)
        .field("y", &math::float4::y)
        .field("z", &math::float4::z)
        .field("w", &math::float4::w);
}
```

### 2.2 复杂类型绑定

```cpp
// 矩阵类型绑定
EMSCRIPTEN_BINDINGS(math) {
    value_array<math::mat4f>("mat4")
        .element(index<0>())
        .element(index<1>())
        .element(index<2>())
        .element(index<3>());

    // 四元数绑定
    value_object<math::quatf>("quat")
        .field("x", &math::quatf::x)
        .field("y", &math::quatf::y)
        .field("z", &math::quatf::z)
        .field("w", &math::quatf::w);

    // 包围盒绑定
    value_object<Box>("Box")
        .field("center", &Box::center)
        .field("halfExtent", &Box::halfExtent);
}
```

```javascript
// JavaScript 中的使用
const transform = new Filament.mat4();
const position = [1.0, 2.0, 3.0];
const rotation = new Filament.quat(0, 0, 0, 1);

// 矩阵运算
const translationMatrix = Filament.mat4.translation(position);
const rotationMatrix = Filament.mat4.fromQuat(rotation);
const finalTransform = Filament.mat4.multiply(translationMatrix, rotationMatrix);
```

## 3. 内存管理绑定

### 3.1 自动内存管理

```cpp
// 智能指针包装
template<typename T>
class ManagedPtr {
private:
    std::unique_ptr<T> ptr_;

public:
    ManagedPtr(T* p) : ptr_(p) {}
    T* get() { return ptr_.get(); }
    void reset() { ptr_.reset(); }
};

EMSCRIPTEN_BINDINGS(memory) {
    class_<ManagedPtr<Texture>>("ManagedTexture")
        .constructor<Texture*>()
        .function("get", &ManagedPtr<Texture>::get,
            allow_raw_pointers())
        .function("reset", &ManagedPtr<Texture>::reset);
}
```

### 3.2 资源池管理

```javascript
// JavaScript 资源池实现
class ResourcePool {
    constructor() {
        this.textures = new Map();
        this.materials = new Map();
        this.meshes = new Map();
        this.cleanupTasks = new Set();
    }

    createTexture(engine, width, height, format) {
        const key = `${width}x${height}_${format}`;

        if (this.textures.has(key)) {
            return this.textures.get(key);
        }

        const texture = Filament.Texture$Builder(engine)
            .width(width)
            .height(height)
            .format(format)
            .build();

        this.textures.set(key, texture);

        // 注册清理任务
        this.cleanupTasks.add(() => texture.delete());

        return texture;
    }

    cleanup() {
        for (const task of this.cleanupTasks) {
            task();
        }
        this.cleanupTasks.clear();
        this.textures.clear();
        this.materials.clear();
        this.meshes.clear();
    }
}
```

## 4. 回调函数绑定

### 4.1 C++ 回调接口

```cpp
// 回调接口定义
class RenderCallback {
public:
    virtual ~RenderCallback() = default;
    virtual void onPreRender() = 0;
    virtual void onPostRender() = 0;
};

// 绑定回调接口
EMSCRIPTEN_BINDINGS(callbacks) {
    class_<RenderCallback>("RenderCallback")
        .function("onPreRender", &RenderCallback::onPreRender,
            pure_virtual())
        .function("onPostRender", &RenderCallback::onPostRender,
            pure_virtual())
        .allow_subclass<RenderCallbackWrapper>("RenderCallbackWrapper");
}

// C++ 包装类
struct RenderCallbackWrapper : public wrapper<RenderCallback> {
    EMSCRIPTEN_WRAPPER(RenderCallbackWrapper);

    void onPreRender() override {
        return call<void>("onPreRender");
    }

    void onPostRender() override {
        return call<void>("onPostRender");
    }
};
```

### 4.2 JavaScript 回调实现

```javascript
// JavaScript 回调实现
class MyRenderCallback extends Filament.RenderCallback {
    onPreRender() {
        console.log('开始渲染帧');
        this.updateAnimations();
        this.updateLighting();
    }

    onPostRender() {
        console.log('完成渲染帧');
        this.collectStats();
    }

    updateAnimations() {
        // 更新动画状态
        const currentTime = performance.now() / 1000;
        this.animationSystem.update(currentTime);
    }

    updateLighting() {
        // 更新光照参数
        const sunDirection = this.calculateSunDirection();
        this.scene.setDirectionalLight(sunDirection);
    }

    collectStats() {
        // 收集渲染统计信息
        this.frameCount++;
        if (this.frameCount % 60 === 0) {
            this.reportPerformance();
        }
    }
}
```

## 5. 数组和容器绑定

### 5.1 固定大小数组

```cpp
// 固定大小数组绑定
EMSCRIPTEN_BINDINGS(arrays) {
    // 顶点属性数组
    register_vector<VertexAttribute>("VertexAttributeVector");

    // 索引数组
    register_vector<uint16_t>("Uint16Vector");
    register_vector<uint32_t>("Uint32Vector");

    // 浮点数组
    register_vector<float>("FloatVector");

    // 材质参数数组
    register_vector<MaterialParameter>("MaterialParameterVector");
}
```

### 5.2 动态容器绑定

```javascript
// JavaScript 中的数组操作
class MeshBuilder {
    constructor() {
        this.vertices = new Filament.FloatVector();
        this.indices = new Filament.Uint16Vector();
        this.attributes = new Filament.VertexAttributeVector();
    }

    addVertex(x, y, z, nx, ny, nz, u, v) {
        // 添加位置
        this.vertices.push_back(x);
        this.vertices.push_back(y);
        this.vertices.push_back(z);

        // 添加法向量
        this.vertices.push_back(nx);
        this.vertices.push_back(ny);
        this.vertices.push_back(nz);

        // 添加纹理坐标
        this.vertices.push_back(u);
        this.vertices.push_back(v);
    }

    addTriangle(i0, i1, i2) {
        this.indices.push_back(i0);
        this.indices.push_back(i1);
        this.indices.push_back(i2);
    }

    build(engine) {
        // 创建顶点缓冲区
        const vertexBuffer = Filament.VertexBuffer$Builder()
            .vertexCount(this.vertices.size() / 8)
            .bufferCount(1)
            .attribute(Filament.VertexAttribute.POSITION, 0,
                Filament.VertexBuffer$AttributeType.FLOAT3, 0, 32)
            .attribute(Filament.VertexAttribute.NORMAL, 0,
                Filament.VertexBuffer$AttributeType.FLOAT3, 12, 32)
            .attribute(Filament.VertexAttribute.UV0, 0,
                Filament.VertexBuffer$AttributeType.FLOAT2, 24, 32)
            .build(engine);

        // 创建索引缓冲区
        const indexBuffer = Filament.IndexBuffer$Builder()
            .indexCount(this.indices.size())
            .bufferType(Filament.IndexBuffer$IndexType.USHORT)
            .build(engine);

        return { vertexBuffer, indexBuffer };
    }

    cleanup() {
        this.vertices.delete();
        this.indices.delete();
        this.attributes.delete();
    }
}
```

## 6. 异步操作绑定

### 6.1 Promise 基础绑定

```cpp
// 异步操作绑定
EMSCRIPTEN_BINDINGS(async) {
    // 资源加载器
    class_<AssetLoader>("AssetLoader")
        .constructor<Engine&>()
        .function("loadGltfAsync", &AssetLoader::loadGltfAsync)
        .function("loadTextureAsync", &AssetLoader::loadTextureAsync)
        .function("loadMaterialAsync", &AssetLoader::loadMaterialAsync);
}

// C++ 异步实现
class AssetLoader {
public:
    emscripten::val loadGltfAsync(const std::string& url) {
        return emscripten::val::global("Promise").new_(
            emscripten::val::module_property("_loadGltfAsync")
                .call<emscripten::val>("bind", emscripten::val::null(),
                    emscripten::val(url))
        );
    }
};
```

### 6.2 JavaScript Promise 实现

```javascript
// 扩展异步功能
Filament.AssetLoader.prototype.loadModelAsync = async function(url) {
    const response = await fetch(url);
    if (!response.ok) {
        throw new Error(`加载模型失败: ${response.statusText}`);
    }

    const arrayBuffer = await response.arrayBuffer();
    const uint8Array = new Uint8Array(arrayBuffer);

    // 将数据传递给 C++
    const dataPtr = Filament._malloc(uint8Array.length);
    Filament.HEAPU8.set(uint8Array, dataPtr);

    try {
        const asset = this.loadGltfFromMemory(dataPtr, uint8Array.length);
        return asset;
    } finally {
        Filament._free(dataPtr);
    }
};

// 批量加载资源
Filament.AssetLoader.prototype.loadAssetsAsync = async function(urls) {
    const loadPromises = urls.map(url => {
        if (url.endsWith('.gltf') || url.endsWith('.glb')) {
            return this.loadModelAsync(url);
        } else if (url.endsWith('.ktx') || url.endsWith('.ktx2')) {
            return this.loadTextureAsync(url);
        } else {
            throw new Error(`不支持的文件格式: ${url}`);
        }
    });

    const results = await Promise.allSettled(loadPromises);

    const assets = [];
    const errors = [];

    results.forEach((result, index) => {
        if (result.status === 'fulfilled') {
            assets.push(result.value);
        } else {
            errors.push({ url: urls[index], error: result.reason });
        }
    });

    if (errors.length > 0) {
        console.warn('部分资源加载失败:', errors);
    }

    return assets;
};
```

## 7. 扩展函数绑定

### 7.1 工具函数扩展

```javascript
// extensions.js - JavaScript 扩展函数

// 数学工具扩展
Filament.math = {
    // 向量运算
    vec3: {
        create: (x = 0, y = 0, z = 0) => ({ x, y, z }),

        add: (a, b) => ({
            x: a.x + b.x,
            y: a.y + b.y,
            z: a.z + b.z
        }),

        subtract: (a, b) => ({
            x: a.x - b.x,
            y: a.y - b.y,
            z: a.z - b.z
        }),

        multiply: (v, scalar) => ({
            x: v.x * scalar,
            y: v.y * scalar,
            z: v.z * scalar
        }),

        normalize: (v) => {
            const length = Math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (length === 0) return { x: 0, y: 0, z: 0 };
            return {
                x: v.x / length,
                y: v.y / length,
                z: v.z / length
            };
        },

        cross: (a, b) => ({
            x: a.y * b.z - a.z * b.y,
            y: a.z * b.x - a.x * b.z,
            z: a.x * b.y - a.y * b.x
        }),

        dot: (a, b) => a.x * b.x + a.y * b.y + a.z * b.z
    },

    // 矩阵工具
    mat4: {
        identity: () => new Float32Array([
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        ]),

        translation: (x, y, z) => new Float32Array([
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            x, y, z, 1
        ]),

        scale: (x, y, z) => new Float32Array([
            x, 0, 0, 0,
            0, y, 0, 0,
            0, 0, z, 0,
            0, 0, 0, 1
        ]),

        perspective: (fovy, aspect, near, far) => {
            const f = 1.0 / Math.tan(fovy / 2);
            const rangeInv = 1.0 / (near - far);

            return new Float32Array([
                f / aspect, 0, 0, 0,
                0, f, 0, 0,
                0, 0, (near + far) * rangeInv, -1,
                0, 0, near * far * rangeInv * 2, 0
            ]);
        }
    }
};
```

### 7.2 便利性扩展

```javascript
// 渲染器扩展
Filament.Renderer.prototype.renderWithTiming = function(view) {
    const startTime = performance.now();
    this.render(view);
    const endTime = performance.now();

    return {
        renderTime: endTime - startTime,
        timestamp: startTime
    };
};

// 场景管理扩展
Filament.Scene.prototype.addMeshInstance = function(mesh, material, transform) {
    const entity = Filament.EntityManager.get().create();

    // 添加渲染组件
    this.getRenderableManager().Builder(1)
        .boundingBox(mesh.boundingBox)
        .material(0, material)
        .geometry(0, mesh.primitiveType, mesh.vertexBuffer, mesh.indexBuffer)
        .build(this.getEngine(), entity);

    // 添加变换组件
    if (transform) {
        this.getTransformManager().setTransform(
            this.getTransformManager().getInstance(entity),
            transform
        );
    }

    this.addEntity(entity);
    return entity;
};

// 材质便利函数
Filament.Material.prototype.setParameters = function(parameters) {
    for (const [name, value] of Object.entries(parameters)) {
        if (typeof value === 'number') {
            this.setParameter(name, value);
        } else if (Array.isArray(value)) {
            if (value.length === 3) {
                this.setParameter(name, value[0], value[1], value[2]);
            } else if (value.length === 4) {
                this.setParameter(name, value[0], value[1], value[2], value[3]);
            }
        } else if (value.constructor.name.includes('Texture')) {
            this.setParameter(name, value);
        }
    }
    return this;
};
```

## 8. 调试和性能监控绑定

### 8.1 调试信息绑定

```cpp
// 调试功能绑定
EMSCRIPTEN_BINDINGS(debug) {
    class_<DebugRegistry>("DebugRegistry")
        .class_function("get", &DebugRegistry::get,
            allow_raw_pointers())
        .function("setProperty", select_overload<void(const char*, bool)>
            (&DebugRegistry::setProperty))
        .function("setProperty", select_overload<void(const char*, int)>
            (&DebugRegistry::setProperty))
        .function("setProperty", select_overload<void(const char*, float)>
            (&DebugRegistry::setProperty));
}
```

### 8.2 性能监控扩展

```javascript
// 性能监控包装器
class PerformanceProfiler {
    constructor(engine) {
        this.engine = engine;
        this.metrics = new Map();
        this.isEnabled = false;
    }

    enable() {
        this.isEnabled = true;
        this.startTime = performance.now();
    }

    disable() {
        this.isEnabled = false;
    }

    profileFunction(name, fn) {
        if (!this.isEnabled) {
            return fn();
        }

        const startTime = performance.now();
        const result = fn();
        const endTime = performance.now();

        const duration = endTime - startTime;
        if (!this.metrics.has(name)) {
            this.metrics.set(name, []);
        }
        this.metrics.get(name).push(duration);

        return result;
    }

    getReport() {
        const report = {};
        for (const [name, times] of this.metrics.entries()) {
            const average = times.reduce((a, b) => a + b, 0) / times.length;
            const min = Math.min(...times);
            const max = Math.max(...times);

            report[name] = {
                average: average.toFixed(3),
                min: min.toFixed(3),
                max: max.toFixed(3),
                samples: times.length
            };
        }
        return report;
    }

    clear() {
        this.metrics.clear();
    }
}
```

## 9. 总结

Filament Web 的 JavaScript 绑定机制通过以下几个关键技术实现了高效的 C++/JavaScript 互操作：

1. **Embind 框架**: 提供类型安全的 API 绑定
2. **内存管理**: 双重内存模型和自动清理机制
3. **类型映射**: 完整的 C++ 类型到 JavaScript 的映射
4. **异步支持**: Promise 基础的异步操作绑定
5. **扩展机制**: JavaScript 层面的便利性扩展
6. **调试支持**: 完善的调试和性能监控功能

这种绑定机制使得开发者可以在 JavaScript 中享受到接近原生 C++ API 的开发体验，同时保持了良好的性能和类型安全性。