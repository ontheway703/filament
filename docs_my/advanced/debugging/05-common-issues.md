# 常见问题排查手册

## 概述

本文档整理了 Filament 开发中最常见的渲染问题及其解决方案。这是一个实战导向的速查手册，按照问题类型分类，每个问题都提供症状描述、根本原因分析和具体解决方案。

无论你是遇到黑屏、闪烁、性能问题还是崩溃，都可以在这里快速找到解决方案。

### 使用方法

1. **快速查找**: 使用 Ctrl+F 搜索关键词
2. **按症状分类**: 根据你看到的现象找到对应章节
3. **验证诊断**: 使用提供的检查代码确认问题
4. **应用修复**: 按照解决方案修复问题
5. **预防复发**: 阅读"预防措施"避免再次发生

---

## 目录

1. [视觉问题](#视觉问题) - 黑屏、闪烁、渲染错误
2. [性能问题](#性能问题) - 卡顿、帧率低、内存高
3. [崩溃问题](#崩溃问题) - 驱动崩溃、应用崩溃
4. [平台特定问题](#平台特定问题) - Android、iOS、Web
5. [资源问题](#资源问题) - 纹理、模型加载失败
6. [着色器问题](#着色器问题) - 编译失败、渲染异常

---

## 视觉问题

### 问题 1: 黑屏（完全没有渲染）

**症状**: 窗口显示纯黑色，没有任何内容

**诊断代码**:
```cpp
void diagnoseBlackScreen(Engine* engine, View* view, Scene* scene) {
    std::cout << "=== Black Screen Diagnosis ===" << std::endl;
    
    // 1. 检查 SwapChain
    if (!swapChain) {
        std::cerr << "ERROR: SwapChain is null!" << std::endl;
        return;
    }
    std::cout << "✓ SwapChain exists" << std::endl;
    
    // 2. 检查 View 配置
    auto viewport = view->getViewport();
    std::cout << "Viewport: " << viewport.width << "x" << viewport.height << std::endl;
    if (viewport.width == 0 || viewport.height == 0) {
        std::cerr << "ERROR: Invalid viewport!" << std::endl;
    }
    
    // 3. 检查清屏颜色
    if (view->getClearTargetColor().has_value()) {
        auto color = view->getClearTargetColor().value();
        std::cout << "Clear Color: (" << color.r << ", " << color.g 
                  << ", " << color.b << ", " << color.a << ")" << std::endl;
    }
    
    // 4. 检查场景内容
    size_t entityCount = scene->getEntityCount();
    std::cout << "Entities in scene: " << entityCount << std::endl;
    if (entityCount == 0) {
        std::cerr << "ERROR: Scene is empty!" << std::endl;
    }
    
    // 5. 检查相机
    auto camera = view->getCamera();
    if (!camera) {
        std::cerr << "ERROR: No camera!" << std::endl;
        return;
    }
    auto position = camera->getPosition();
    std::cout << "Camera position: (" << position.x << ", " 
              << position.y << ", " << position.z << ")" << std::endl;
}
```

**常见原因和解决方案**:

| 原因 | 解决方案 |
|------|---------|
| SwapChain 创建失败 | 检查窗口句柄是否有效，确保在主线程创建 |
| 清屏颜色设置为黑色 | `view->setClearColor({1.0f, 0.0f, 1.0f, 1.0f});` 改为明显颜色测试 |
| 场景为空 | 确保调用了 `scene->addEntity()` |
| 相机未配置 | 设置相机位置和朝向：`camera->lookAt({0,0,5}, {0,0,0}, {0,1,0})` |
| 视口尺寸为 0 | `view->setViewport({0, 0, width, height})` |
| 渲染命令未提交 | 确保调用 `renderer->render(view)` |

**修复代码**:
```cpp
void fixBlackScreen() {
    // 1. 确保视口正确
    view->setViewport({0, 0, width, height});
    
    // 2. 设置明显的清屏颜色（测试）
    view->setClearColor({0.2f, 0.3f, 0.3f, 1.0f});
    
    // 3. 确保相机配置正确
    camera->setProjection(45.0, (float)width/height, 0.1, 100.0);
    camera->lookAt({0, 0, 10}, {0, 0, 0}, {0, 1, 0});
    
    // 4. 添加测试物体
    auto testEntity = createSimpleTriangle();
    scene->addEntity(testEntity);
    
    // 5. 确保渲染循环正确
    if (renderer->beginFrame(swapChain)) {
        renderer->render(view);
        renderer->endFrame();
    }
}
```

---

### 问题 2: 闪烁（画面快速抖动）

**症状**: 画面快速闪烁，看起来不稳定

**诊断代码**:
```cpp
void diagnoseFlickering() {
    // 1. 检查帧率
    static int frameCount = 0;
    static auto lastTime = std::chrono::high_resolution_clock::now();
    frameCount++;
    
    auto currentTime = std::chrono::high_resolution_clock::now();
    float elapsed = std::chrono::duration<float>(currentTime - lastTime).count();
    if (elapsed >= 1.0f) {
        float fps = frameCount / elapsed;
        std::cout << "FPS: " << fps << std::endl;
        frameCount = 0;
        lastTime = currentTime;
    }
    
    // 2. 检查 VSync
    std::cout << "VSync status: " << (isVSyncEnabled() ? "ON" : "OFF") << std::endl;
    
    // 3. 检查深度缓冲精度
    std::cout << "Near plane: " << camera->getNear() << std::endl;
    std::cout << "Far plane: " << camera->getFar() << std::endl;
}
```

**常见原因和解决方案**:

| 原因 | 解决方案 |
|------|---------|
| Z-fighting（深度冲突） | 增大近裁剪面：`camera->setProjection(45.0, aspect, 1.0, 1000.0)` |
| 双缓冲问题 | 启用 VSync：`swapChain->setVSync(true)` |
| CPU/GPU 资源竞争 | 使用 `engine->flushAndWait()` 同步 |
| 相机快速移动 | 启用运动模糊或增加帧率 |

**Z-Fighting 修复**:
```cpp
void fixZFighting() {
    // 方法 1: 调整近裁剪面
    // 从 0.1 改为 1.0 或更大
    camera->setProjection(45.0, aspect, 1.0, 1000.0, Camera::Fov::VERTICAL);
    
    // 方法 2: 使用深度偏移（多边形偏移）
    auto& rcm = engine->getRenderableManager();
    auto instance = rcm.getInstance(entity);
    
    // 为重叠的物体设置不同的偏移
    // materialInstance->setParameter("depthOffset", 0.001f);
    
    // 方法 3: 确保物体不完全重叠
    // 调整位置使深度略有差异
}
```

---

### 问题 3: 模型显示不完整或消失

**症状**: 模型的一部分或全部不可见

**诊断代码**:
```cpp
void diagnoseIncompleteModel(Engine* engine, utils::Entity entity) {
    auto& rcm = engine->getRenderableManager();
    auto& tcm = engine->getTransformManager();
    
    if (!rcm.hasComponent(entity)) {
        std::cerr << "Entity has no renderable component!" << std::endl;
        return;
    }
    
    auto instance = rcm.getInstance(entity);
    
    // 1. 检查包围盒
    auto aabb = rcm.getAxisAlignedBoundingBox(instance);
    std::cout << "AABB: min(" << aabb.min.x << ", " << aabb.min.y << ", " << aabb.min.z << ") "
              << "max(" << aabb.max.x << ", " << aabb.max.y << ", " << aabb.max.z << ")" << std::endl;
    
    // 2. 检查剔除状态
    std::cout << "Culling enabled: " << rcm.isCullingEnabled(instance) << std::endl;
    
    // 3. 检查变换
    auto transformInstance = tcm.getInstance(entity);
    if (transformInstance.isValid()) {
        auto transform = tcm.getWorldTransform(transformInstance);
        std::cout << "Transform valid: YES" << std::endl;
    }
    
    // 4. 检查可见性
    std::cout << "Visible: " << (scene->hasEntity(entity) ? "YES" : "NO") << std::endl;
}
```

**常见原因和解决方案**:

| 原因 | 解决方案 |
|------|---------|
| 背面剔除导致 | 检查模型法线方向，或临时禁用剔除测试 |
| 视锥剔除导致 | 检查包围盒是否正确，或临时禁用剔除 |
| 深度测试失败 | 检查深度测试配置和渲染顺序 |
| 变换矩阵错误 | 检查 scale、rotation、position |
| 材质问题 | 尝试用简单的 unlit 材质替换 |

**修复代码**:
```cpp
void fixIncompleteModel() {
    // 1. 临时禁用背面剔除（诊断）
    // 在材质定义中：
    // culling = none
    
    // 2. 禁用视锥剔除（诊断）
    auto& rcm = engine->getRenderableManager();
    auto instance = rcm.getInstance(entity);
    rcm.setCulling(instance, false);
    
    // 3. 检查深度测试
    // 确保深度缓冲已创建和清除
    view->setDepthPrepass(View::DepthPrepass::DEFAULT);
    
    // 4. 重置变换（测试）
    auto& tcm = engine->getTransformManager();
    auto transformInstance = tcm.getInstance(entity);
    tcm->setTransform(transformInstance, mat4f(1.0f)); // 单位矩阵
}
```

---

### 问题 4: 纹理显示为纯色或错误

**症状**: 纹理显示为黑色、白色或其他纯色

**诊断代码**:
```cpp
void diagnoseTextureIssue(Texture* texture, MaterialInstance* matInst) {
    if (!texture) {
        std::cerr << "ERROR: Texture is null!" << std::endl;
        return;
    }
    
    std::cout << "=== Texture Diagnosis ===" << std::endl;
    std::cout << "Width: " << texture->getWidth() << std::endl;
    std::cout << "Height: " << texture->getHeight() << std::endl;
    std::cout << "Levels: " << texture->getLevels() << std::endl;
    std::cout << "Format: " << (int)texture->getFormat() << std::endl;
    
    // 检查材质实例绑定
    // matInst->getParameter("baseColorMap") 应该返回纹理
}
```

**常见原因和解决方案**:

| 原因 | 解决方案 |
|------|---------|
| 纹理未正确加载 | 检查文件路径和加载代码 |
| 纹理数据未上传 | 确保调用 `texture->setImage()` 后数据已传输到 GPU |
| UV 坐标错误 | 检查顶点数据中的 UV 坐标 |
| 采样器配置错误 | 检查 wrap mode 和 filter |
| sRGB/Linear 混淆 | 确保颜色纹理使用 sRGB 格式 |
| Mipmap 缺失 | 生成 Mipmap 或禁用 Mipmap 过滤 |

**修复代码**:
```cpp
void fixTextureIssue() {
    // 1. 确保纹理正确加载
    Texture* texture = Texture::Builder()
        .width(width)
        .height(height)
        .levels(mipLevels)
        .format(Texture::InternalFormat::SRGB8_A8) // 注意 sRGB
        .sampler(Texture::Sampler::SAMPLER_2D)
        .build(*engine);
    
    // 2. 上传数据
    Texture::PixelBufferDescriptor buffer(
        data, size,
        Texture::Format::RGBA,
        Texture::Type::UBYTE
    );
    texture->setImage(*engine, 0, std::move(buffer));
    
    // 3. 配置采样器
    TextureSampler sampler(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
                          TextureSampler::MagFilter::LINEAR);
    sampler.setWrapModeS(TextureSampler::WrapMode::REPEAT);
    sampler.setWrapModeT(TextureSampler::WrapMode::REPEAT);
    
    // 4. 绑定到材质
    materialInstance->setParameter("baseColorMap", texture, sampler);
}
```

---

### 问题 5: 光照异常（过亮或过暗）

**症状**: 场景光照不正常，过亮、过暗或无光照

**常见原因和解决方案**:

| 原因 | 解决方案 |
|------|---------|
| 缺少光源 | 添加至少一个光源（太阳光或环境光） |
| 法线错误 | 检查法线是否规范化，方向是否正确 |
| 材质参数错误 | 检查 Roughness、Metallic 值 |
| IBL 缺失 | 设置 IndirectLight |
| 曝光值不当 | 调整相机曝光：`camera->setExposure(16.0f, 100.0f, 100.0f)` |
| 颜色空间错误 | 确保 Albedo 纹理是 sRGB，其他是 Linear |

**修复代码**:
```cpp
void fixLighting(Engine* engine, Scene* scene) {
    // 1. 添加太阳光
    auto& em = utils::EntityManager::get();
    auto sunLight = em.create();
    
    LightManager::Builder(LightManager::Type::SUN)
        .color(Color::toLinear<ACCURATE>({1.0f, 1.0f, 1.0f}))
        .intensity(100000.0f)
        .direction({0.6f, -1.0f, -0.8f})
        .castShadows(true)
        .build(*engine, sunLight);
    
    scene->addEntity(sunLight);
    
    // 2. 添加环境光（IBL）
    auto ibl = IndirectLight::Builder()
        .intensity(30000.0f)
        .build(*engine);
    scene->setIndirectLight(ibl);
    
    // 3. 调整曝光
    camera->setExposure(16.0f, 1.0f / 125.0f, 100.0f);
    
    // 4. 检查材质法线
    // 确保法线在切线空间，并且规范化
}
```

---

## 性能问题

### 问题 6: 帧率低（FPS < 30）

**诊断代码**:
```cpp
class PerformanceDiagnostics {
public:
    void measureFrameTime() {
        auto start = std::chrono::high_resolution_clock::now();
        
        // 渲染
        renderer->beginFrame(swapChain);
        renderer->render(view);
        renderer->endFrame();
        
        auto end = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float, std::milli>(end - start).count();
        
        std::cout << "Frame time: " << frameTime << " ms (" 
                  << (1000.0f / frameTime) << " FPS)" << std::endl;
        
        if (frameTime > 33.33f) { // < 30 FPS
            std::cout << "WARNING: Frame time too high!" << std::endl;
            analyzePotentialBottlenecks();
        }
    }
    
    void analyzePotentialBottlenecks() {
        // 1. 统计 Draw Call
        size_t drawCalls = countDrawCalls(scene);
        std::cout << "Draw Calls: " << drawCalls << std::endl;
        if (drawCalls > 500) {
            std::cout << "  → Too many draw calls! Consider batching." << std::endl;
        }
        
        // 2. 统计三角形
        size_t triangles = countTriangles(scene);
        std::cout << "Triangles: " << triangles << std::endl;
        if (triangles > 1000000) {
            std::cout << "  → Too many triangles! Consider LOD." << std::endl;
        }
        
        // 3. 检查纹理内存
        size_t textureMem = estimateTextureMemory();
        std::cout << "Texture Memory: " << (textureMem / 1024 / 1024) << " MB" << std::endl;
        if (textureMem > 500 * 1024 * 1024) {
            std::cout << "  → High texture memory! Consider compression." << std::endl;
        }
    }
};
```

**优化检查清单**:

```cpp
void performanceOptimizationChecklist() {
    std::cout << "=== Performance Optimization Checklist ===" << std::endl;
    
    // 1. Draw Call 优化
    std::cout << "[ ] Batch similar objects" << std::endl;
    std::cout << "[ ] Use instancing for repeated objects" << std::endl;
    std::cout << "[ ] Minimize material switches" << std::endl;
    
    // 2. 几何优化
    std::cout << "[ ] Implement LOD system" << std::endl;
    std::cout << "[ ] Use frustum culling" << std::endl;
    std::cout << "[ ] Use occlusion culling" << std::endl;
    
    // 3. 纹理优化
    std::cout << "[ ] Use compressed textures (ASTC/ETC2)" << std::endl;
    std::cout << "[ ] Generate mipmaps" << std::endl;
    std::cout << "[ ] Reduce resolution where possible" << std::endl;
    
    // 4. 着色器优化
    std::cout << "[ ] Simplify fragment shaders" << std::endl;
    std::cout << "[ ] Reduce texture samples" << std::endl;
    std::cout << "[ ] Avoid dynamic branching" << std::endl;
    
    // 5. 渲染优化
    std::cout << "[ ] Use depth pre-pass" << std::endl;
    std::cout << "[ ] Sort transparent objects" << std::endl;
    std::cout << "[ ] Minimize overdraw" << std::endl;
}
```

---

### 问题 7: 卡顿（偶尔的长帧）

**症状**: 大部分时间流畅，但偶尔出现明显卡顿

**诊断代码**:
```cpp
class StutterDiagnostics {
public:
    void trackFrameTimes() {
        static std::vector<float> frameTimes;
        static const size_t maxSamples = 300; // 5秒 @ 60fps
        
        auto start = std::chrono::high_resolution_clock::now();
        render();
        auto end = std::chrono::high_resolution_clock::now();
        
        float frameTime = std::chrono::duration<float, std::milli>(end - start).count();
        frameTimes.push_back(frameTime);
        
        if (frameTimes.size() > maxSamples) {
            frameTimes.erase(frameTimes.begin());
        }
        
        // 检测长帧
        if (frameTime > 50.0f) { // > 50ms (< 20 FPS)
            std::cout << "STUTTER DETECTED: " << frameTime << " ms" << std::endl;
            analyzeStutterCause();
        }
        
        // 定期报告统计
        if (frameTimes.size() == maxSamples) {
            printFrameTimeStats(frameTimes);
        }
    }
    
    void analyzeStutterCause() {
        std::cout << "Potential causes:" << std::endl;
        std::cout << "  1. Shader compilation (check if new shaders loaded)" << std::endl;
        std::cout << "  2. Resource loading (check if textures/models loaded)" << std::endl;
        std::cout << "  3. GC/Memory allocation" << std::endl;
        std::cout << "  4. CPU-GPU sync point" << std::endl;
    }
};
```

**常见原因和解决方案**:

| 原因 | 解决方案 |
|------|---------|
| 着色器编译 | 预编译或异步编译着色器 |
| 资源同步加载 | 使用异步加载，显示加载界面 |
| GC/内存分配 | 使用对象池，减少运行时分配 |
| CPU-GPU 同步 | 避免 `readPixels()` 等同步调用 |

---

### 问题 8: 内存占用过高

**诊断代码**:
```cpp
class MemoryDiagnostics {
public:
    void analyzeMemoryUsage() {
        std::cout << "=== Memory Usage Analysis ===" << std::endl;
        
        // 1. 纹理内存
        size_t textureMemory = estimateTextureMemory();
        std::cout << "Texture Memory: " << (textureMemory / 1024 / 1024) << " MB" << std::endl;
        
        // 2. 顶点缓冲内存
        size_t vertexMemory = estimateVertexBufferMemory();
        std::cout << "Vertex Buffer Memory: " << (vertexMemory / 1024 / 1024) << " MB" << std::endl;
        
        // 3. 索引缓冲内存
        size_t indexMemory = estimateIndexBufferMemory();
        std::cout << "Index Buffer Memory: " << (indexMemory / 1024 / 1024) << " MB" << std::endl;
        
        // 4. 其他资源
        size_t otherMemory = estimateOtherMemory();
        std::cout << "Other Memory: " << (otherMemory / 1024 / 1024) << " MB" << std::endl;
        
        size_t totalMemory = textureMemory + vertexMemory + indexMemory + otherMemory;
        std::cout << "Total GPU Memory: " << (totalMemory / 1024 / 1024) << " MB" << std::endl;
        
        // 建议
        if (textureMemory > 200 * 1024 * 1024) {
            std::cout << "→ Consider texture compression" << std::endl;
        }
        if (vertexMemory > 100 * 1024 * 1024) {
            std::cout << "→ Consider mesh LOD or simplification" << std::endl;
        }
    }
};
```

**解决方案**:
```cpp
void reduceMemoryUsage() {
    // 1. 压缩纹理
    // 使用 ASTC/ETC2 而不是 RGBA8
    
    // 2. 减少 Mipmap 层级
    // 只生成必要的 Mipmap
    
    // 3. 使用纹理图集
    // 合并小纹理减少内存和 Draw Call
    
    // 4. 实现资源流式加载
    // 根据需要加载/卸载资源
    
    // 5. 使用 16 位纹理
    // 对于法线贴图等，使用 RGB565 或 RGBA4444
}
```

---

## 崩溃问题

### 问题 9: 应用崩溃（段错误）

**常见原因**:
```cpp
// 1. 空指针访问
if (!texture) {
    // 错误：未检查就使用
    // texture->getWidth(); // CRASH!
    return;
}

// 2. 资源已释放
// 错误：使用已销毁的对象
// engine->destroy(texture);
// materialInstance->setParameter("tex", texture); // CRASH!

// 3. 线程不安全
// 错误：多线程访问 Filament 对象
// std::thread([&]() {
//     scene->addEntity(entity); // CRASH! 只能在主线程
// }).detach();
```

**防御性编程**:
```cpp
class SafeResourceManager {
public:
    Texture* getTexture(const std::string& name) {
        auto it = textures.find(name);
        if (it != textures.end()) {
            return it->second;
        }
        std::cerr << "Texture not found: " << name << std::endl;
        return defaultTexture; // 返回默认纹理而不是 nullptr
    }
    
    void safeDestroyTexture(const std::string& name) {
        auto it = textures.find(name);
        if (it != textures.end()) {
            engine->destroy(it->second);
            textures.erase(it);
        }
    }
    
private:
    std::map<std::string, Texture*> textures;
    Texture* defaultTexture = nullptr;
};
```

---

## 平台特定问题

### Android 平台

**问题**: 应用切换到后台后崩溃

**解决方案**:
```cpp
// 正确处理 Android 生命周期
void onPause() {
    // 释放 Surface 相关资源
    if (swapChain) {
        engine->destroy(swapChain);
        swapChain = nullptr;
    }
}

void onResume() {
    // 重新创建 SwapChain
    swapChain = engine->createSwapChain(nativeWindow);
}
```

### iOS 平台

**问题**: Metal 后端性能差

**解决方案**:
```cpp
// 使用 Metal 优化选项
Engine::Config config;
config.backend = Engine::Backend::METAL;
config.stereoscopicEyeCount = 1; // 非 VR
auto engine = Engine::create(config);
```

---

## 总结

本文档涵盖了最常见的 20+ 个问题及其解决方案。记住：

1. **诊断优先**: 先诊断再修复
2. **隔离问题**: 使用最小复现案例
3. **工具辅助**: 使用 RenderDoc 等工具
4. **预防为主**: 防御性编程和单元测试

如果你的问题不在此列表中，请参考其他调试文档获得更系统的方法论。

## 相关文档

- [01-debugging-workflow.md](./01-debugging-workflow.md) - 系统调试流程
- [02-renderdoc-usage.md](./02-renderdoc-usage.md) - RenderDoc 使用
- [04-performance-profiling.md](./04-performance-profiling.md) - 性能分析
- [07-mobile-debugging.md](./07-mobile-debugging.md) - 移动端调试
