# Filament 渲染管线架构

## 📖 概述

渲染管线（Rendering Pipeline）是现代图形引擎的核心，它定义了从场景数据到最终像素的完整转换流程。Filament 采用高度优化的渲染管线设计，结合 Frame Graph、Pipeline State 缓存、Material 变体系统等先进技术，实现了高性能、可扩展的渲染架构。

**核心概念**：
- **RenderPass**: 渲染管线的基本执行单元（如阴影Pass、主渲染Pass、后处理Pass）
- **Pipeline State**: GPU管线状态（着色器、光栅化、深度测试、混合模式等）
- **Frame Graph**: 声明式渲染管线组织，自动管理资源和依赖
- **Material System**: 材质编译和变体生成，运行时动态选择

**关键挑战**：
1. **性能优化**：最小化状态切换、DrawCall、GPU停顿
2. **资源管理**：临时纹理、缓冲的自动分配和复用
3. **灵活性**：支持延迟渲染、前向渲染、混合方案
4. **可维护性**：声明式API，易于扩展新的渲染效果

本文档将深入讲解 Filament 渲染管线的设计原理、数据结构、关键算法，并通过完整代码示例展示如何构建高性能渲染应用。

---

## 1. 渲染管线概览

### 1.1 典型渲染帧流程

一个完整的渲染帧包含多个 RenderPass，每个 Pass 负责特定的渲染任务：

```
Frame Start
    │
    ├─> 1. Visibility Determination（CPU）
    │     - 视锥剔除
    │     - 遮挡剔除
    │     - LOD 选择
    │
    ├─> 2. Shadow Pass（GPU）
    │     - Directional Light Shadow Map
    │     - Spot Light Shadow Map
    │     - Point Light Shadow Cube Map
    │
    ├─> 3. Depth Prepass（GPU，可选）
    │     - 仅写深度，建立 Early-Z
    │     - 减少主Pass的overdraw
    │
    ├─> 4. GBuffer Pass（延迟渲染）或 Opaque Pass（前向渲染）
    │     - 渲染不透明物体
    │     - 输出 Albedo、Normal、Metallic、Roughness等
    │
    ├─> 5. Lighting Pass（延迟渲染）
    │     - 使用 GBuffer 计算光照
    │     - 支持大量动态光源
    │
    ├─> 6. Transparent Pass
    │     - 后向前渲染透明物体
    │     - Alpha Blending
    │
    ├─> 7. Post-Processing
    │     - Bloom
    │     - Tone Mapping
    │     - FXAA/TAA
    │     - Color Grading
    │
    └─> Frame End
          - Present to Screen
```

### 1.2 Filament 渲染循环

```cpp
// filament/src/Renderer.cpp (简化示例)

void Renderer::render(View* view) {
    // 1. 执行视锥剔除
    JobSystem::Job* cullJob = mEngine->getJobSystem().createJob(nullptr, [view](JobSystem&, JobSystem::Job*) {
        view->prepareCamera();
        view->cullRenderables();
    });
    mEngine->getJobSystem().run(cullJob);
    
    // 2. 等待剔除完成
    mEngine->getJobSystem().waitAndRelease(cullJob);
    
    // 3. 构建渲染命令
    RenderPass shadowPass = prepareShadowPass(view);
    RenderPass colorPass = prepareColorPass(view);
    RenderPass postProcessPass = preparePostProcessPass(view);
    
    // 4. 提交到 Backend Thread
    mDriver->beginFrame();
    
    executeShadowPass(shadowPass);
    executeColorPass(colorPass);
    executePostProcessPass(postProcessPass);
    
    mDriver->endFrame();
    
    // 5. Present
    mDriver->commit(mSwapChain);
}
```

---

## 2. Pipeline State 管理

### 2.1 Pipeline State 定义

Pipeline State 封装了 GPU 渲染所需的所有状态：

```cpp
// filament/backend/include/backend/PipelineState.h

namespace filament::backend {

struct PipelineState {
    // 着色器程序
    Handle<HwProgram> program;
    
    // 光栅化状态
    struct RasterState {
        CullingMode culling;        // NONE, FRONT, BACK
        bool depthWrite;
        CompareFunc depthFunc;      // LESS, LEQUAL, GREATER, etc.
        bool colorWrite;
        float polygonOffset;
    } rasterState;
    
    // 顶点输入
    struct VertexInputState {
        struct Attribute {
            uint32_t location;
            VertexAttribute::Type type;
            uint32_t offset;
            uint8_t stride;
        };
        std::array<Attribute, MAX_VERTEX_ATTRIBUTES> attributes;
        uint8_t attributeCount;
    } vertexInputState;
    
    // 混合状态
    struct BlendState {
        bool enabled;
        BlendFunction srcColorBlendFunction;
        BlendFunction dstColorBlendFunction;
        BlendFunction srcAlphaBlendFunction;
        BlendFunction dstAlphaBlendFunction;
        BlendEquation colorBlendEquation;
        BlendEquation alphaBlendEquation;
    } blendState;
    
    // 模板测试（可选）
    struct StencilState {
        bool enabled;
        CompareFunc func;
        StencilOp failOp;
        StencilOp depthFailOp;
        StencilOp passOp;
        uint8_t ref;
        uint8_t readMask;
        uint8_t writeMask;
    } stencilState;
};

}  // namespace filament::backend
```

### 2.2 Pipeline State 哈希缓存

为避免重复创建相同的 Pipeline 对象（OpenGL 的 Program、Vulkan 的 VkPipeline），Filament 使用哈希表缓存：

```cpp
// filament/backend/src/PipelineStateCache.h

class PipelineStateCache {
public:
    // 获取或创建 Pipeline
    Handle<HwRenderPrimitive> getOrCreate(const PipelineState& state) {
        // 计算哈希
        uint64_t hash = computeHash(state);
        
        // 查找缓存
        auto it = mCache.find(hash);
        if (it != mCache.end()) {
            // 缓存命中
            mStats.hits++;
            return it->second;
        }
        
        // 缓存未命中，创建新 Pipeline
        mStats.misses++;
        Handle<HwRenderPrimitive> handle = createPipeline(state);
        mCache[hash] = handle;
        
        return handle;
    }
    
    struct Stats {
        size_t hits = 0;
        size_t misses = 0;
        size_t evictions = 0;
    };
    
    const Stats& getStats() const { return mStats; }
    
private:
    // 计算 Pipeline State 的哈希值
    uint64_t computeHash(const PipelineState& state) {
        utils::hash::MurmurHashFn<uint64_t> hasher;
        
        hasher(state.program.getId());
        hasher(state.rasterState.culling);
        hasher(state.rasterState.depthWrite);
        hasher(state.rasterState.depthFunc);
        hasher(state.blendState.enabled);
        // ... 其他字段
        
        return hasher.get();
    }
    
    // 创建底层 Pipeline 对象
    Handle<HwRenderPrimitive> createPipeline(const PipelineState& state) {
        // 调用 Driver 创建实际的 GPU Pipeline
        // Vulkan: vkCreateGraphicsPipelines
        // Metal: MTLRenderPipelineDescriptor
        // OpenGL: glUseProgram + glEnable/glDisable
        return mDriver->createRenderPrimitive(state);
    }
    
    std::unordered_map<uint64_t, Handle<HwRenderPrimitive>> mCache;
    Driver* mDriver;
    Stats mStats;
};
```

### 2.3 状态排序优化

通过对 Renderables 按 Pipeline State 排序，可以最小化状态切换次数：

```cpp
// filament/src/RenderPass.cpp

void RenderPass::sortRenderables() {
    // 按 Pipeline State Hash 排序
    std::sort(mRenderables.begin(), mRenderables.end(), 
              [](const RenderableInfo& a, const RenderableInfo& b) {
        // 主键：Pipeline State Hash
        if (a.pipelineStateHash != b.pipelineStateHash) {
            return a.pipelineStateHash < b.pipelineStateHash;
        }
        
        // 次键：Material Instance ID（减少 Uniform 更新）
        if (a.materialInstanceId != b.materialInstanceId) {
            return a.materialInstanceId < b.materialInstanceId;
        }
        
        // 三级键：距离（前向后排序，利用 Early-Z）
        return a.distanceToCamera < b.distanceToCamera;
    });
    
    // 统计状态切换次数
    size_t stateChanges = 0;
    uint64_t lastStateHash = 0;
    
    for (const auto& renderable : mRenderables) {
        if (renderable.pipelineStateHash != lastStateHash) {
            stateChanges++;
            lastStateHash = renderable.pipelineStateHash;
        }
    }
    
    LOG_DEBUG("RenderPass: %zu renderables, %zu state changes", 
             mRenderables.size(), stateChanges);
}
```

**优化效果**：
- 未排序：1000 个物体可能产生 1000 次状态切换
- 排序后：1000 个物体可能只需 50-100 次状态切换（共享相同材质）
- **性能提升**：减少 CPU 时间 20-40%，提升帧率

---

## 3. RenderPass 架构

### 3.1 RenderPass 类设计

```cpp
// filament/src/details/RenderPass.h

namespace filament {

class RenderPass {
public:
    enum class Type {
        Shadow,
        Depth,
        Color,
        PostProcess
    };
    
    struct Config {
        Type type;
        const char* name;
        
        // RenderTarget 配置
        Handle<HwRenderTarget> renderTarget;
        Viewport viewport;
        
        // Clear 操作
        bool clearColor;
        bool clearDepth;
        bool clearStencil;
        math::float4 clearColorValue;
        float clearDepthValue;
        uint8_t clearStencilValue;
        
        // 剔除配置
        bool frustumCulling;
        bool occlusionCulling;
    };
    
    RenderPass(Engine& engine, const Config& config);
    ~RenderPass();
    
    // 添加 Renderable
    void addRenderable(const RenderableInfo& info);
    
    // 执行 Pass
    void execute(Driver& driver);
    
    // 性能统计
    struct Stats {
        size_t visibleRenderables;
        size_t drawCalls;
        size_t triangles;
        size_t stateChanges;
        double cpuTime;     // CPU 时间（毫秒）
        double gpuTime;     // GPU 时间（毫秒）
    };
    const Stats& getStats() const { return mStats; }
    
private:
    void preparePipelineStates();
    void issueDrawCalls(Driver& driver);
    
    Engine& mEngine;
    Config mConfig;
    
    std::vector<RenderableInfo> mRenderables;
    PipelineStateCache mPipelineCache;
    
    Stats mStats;
};

}  // namespace filament
```

### 3.2 RenderPass 执行

```cpp
// filament/src/details/RenderPass.cpp

void RenderPass::execute(Driver& driver) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // 1. 排序 Renderables
    sortRenderables();
    
    // 2. 设置 RenderTarget
    driver.beginRenderPass(mConfig.renderTarget, {
        .clearColor = mConfig.clearColor,
        .clearDepth = mConfig.clearDepth,
        .clearStencil = mConfig.clearStencil,
        .clearColorValue = mConfig.clearColorValue,
        .clearDepthValue = mConfig.clearDepthValue,
        .clearStencilValue = mConfig.clearStencilValue
    });
    
    // 3. 设置 Viewport
    driver.setViewport(mConfig.viewport);
    
    // 4. 遍历 Renderables，发出 Draw Calls
    uint64_t currentStateHash = 0;
    Handle<HwRenderPrimitive> currentPipeline;
    
    for (const auto& renderable : mRenderables) {
        // 状态切换（仅当必要时）
        if (renderable.pipelineStateHash != currentStateHash) {
            currentStateHash = renderable.pipelineStateHash;
            currentPipeline = mPipelineCache.getOrCreate(renderable.pipelineState);
            
            driver.bindPipeline(currentPipeline);
            mStats.stateChanges++;
        }
        
        // 绑定资源
        driver.bindUniformBuffer(0, renderable.perObjectUBO);
        driver.bindSamplers(renderable.samplerGroup);
        
        // 发出 Draw Call
        driver.draw(
            renderable.primitiveType,
            renderable.indexBuffer,
            renderable.vertexBuffer,
            renderable.indexCount,
            renderable.indexOffset
        );
        
        mStats.drawCalls++;
        mStats.triangles += renderable.indexCount / 3;
    }
    
    // 5. 结束 RenderPass
    driver.endRenderPass();
    
    // 6. 统计 CPU 时间
    auto endTime = std::chrono::high_resolution_clock::now();
    mStats.cpuTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    
    LOG_DEBUG("RenderPass '%s': %zu draw calls, %zu triangles, %.2f ms",
             mConfig.name, mStats.drawCalls, mStats.triangles, mStats.cpuTime);
}
```

---

## 4. Frame Graph 深入

### 4.1 Frame Graph 概念

Frame Graph 是一种**声明式**的渲染管线组织方式，开发者只需声明：
- **Pass**：要执行的渲染任务
- **Resources**：Pass 需要的输入/输出资源
- **Dependencies**：Pass 之间的依赖关系

Frame Graph 自动完成：
- 资源生命周期管理（分配、复用、释放）
- Pass 执行顺序优化
- 未使用 Pass 的剔除

### 4.2 Frame Graph 数据结构

```cpp
// filament/backend/include/backend/FrameGraph.h

namespace filament::backend {

class FrameGraph {
public:
    // 资源句柄
    struct ResourceHandle {
        uint32_t id;
        bool operator==(const ResourceHandle& other) const { return id == other.id; }
    };
    
    // 资源描述符
    struct ResourceDescriptor {
        enum class Type { Texture, Buffer };
        
        Type type;
        union {
            struct {
                uint32_t width;
                uint32_t height;
                TextureFormat format;
                uint8_t levels;
            } texture;
            
            struct {
                uint32_t size;
                BufferUsage usage;
            } buffer;
        };
    };
    
    // Pass 节点
    struct PassNode {
        std::string name;
        std::function<void(Driver&, const PassResources&)> execute;
        
        std::vector<ResourceHandle> reads;   // 输入资源
        std::vector<ResourceHandle> writes;  // 输出资源
        
        bool culled = false;  // 是否被剔除
    };
    
    // 资源节点
    struct ResourceNode {
        ResourceDescriptor descriptor;
        
        PassNode* writer = nullptr;          // 写入该资源的 Pass
        std::vector<PassNode*> readers;      // 读取该资源的 Passes
        
        uint32_t firstUse = UINT32_MAX;      // 首次使用的 Pass 索引
        uint32_t lastUse = 0;                // 最后使用的 Pass 索引
        
        Handle<HwTexture> physicalResource;  // 实际分配的 GPU 资源
    };
    
    // 添加 Pass
    template<typename Setup, typename Execute>
    void addPass(const char* name, Setup&& setup, Execute&& execute) {
        PassNode pass;
        pass.name = name;
        
        PassBuilder builder(*this, pass);
        setup(builder);  // 用户声明资源依赖
        
        pass.execute = std::forward<Execute>(execute);
        mPasses.push_back(std::move(pass));
    }
    
    // 编译 Frame Graph
    void compile() {
        // 1. 计算资源生命周期
        computeResourceLifetimes();
        
        // 2. 剔除未使用的 Passes
        cullUnusedPasses();
        
        // 3. 分配物理资源
        allocateResources();
        
        // 4. 拓扑排序 Passes
        topologicalSort();
    }
    
    // 执行 Frame Graph
    void execute(Driver& driver) {
        for (auto& pass : mPasses) {
            if (!pass.culled) {
                PassResources resources = preparePassResources(pass);
                pass.execute(driver, resources);
            }
        }
    }
    
    // 重置（下一帧）
    void reset() {
        mPasses.clear();
        mResources.clear();
    }
    
private:
    void computeResourceLifetimes() {
        for (size_t passIndex = 0; passIndex < mPasses.size(); ++passIndex) {
            const auto& pass = mPasses[passIndex];
            
            // 更新输入资源的生命周期
            for (auto resourceHandle : pass.reads) {
                auto& resource = mResources[resourceHandle.id];
                resource.firstUse = std::min(resource.firstUse, static_cast<uint32_t>(passIndex));
                resource.lastUse = std::max(resource.lastUse, static_cast<uint32_t>(passIndex));
            }
            
            // 更新输出资源的生命周期
            for (auto resourceHandle : pass.writes) {
                auto& resource = mResources[resourceHandle.id];
                resource.writer = &mPasses[passIndex];
                resource.firstUse = std::min(resource.firstUse, static_cast<uint32_t>(passIndex));
                resource.lastUse = std::max(resource.lastUse, static_cast<uint32_t>(passIndex));
            }
        }
    }
    
    void cullUnusedPasses() {
        // 从后向前遍历，标记未被使用的 Passes
        for (int i = mPasses.size() - 1; i >= 0; --i) {
            auto& pass = mPasses[i];
            
            // 如果该 Pass 的输出没有被任何后续 Pass 读取，则剔除
            bool hasReaders = false;
            for (auto resourceHandle : pass.writes) {
                auto& resource = mResources[resourceHandle.id];
                if (!resource.readers.empty()) {
                    hasReaders = true;
                    break;
                }
            }
            
            if (!hasReaders && !pass.writes.empty()) {
                pass.culled = true;
                LOG_INFO("FrameGraph: Culled unused pass '%s'", pass.name.c_str());
            }
        }
    }
    
    void allocateResources() {
        // 使用贪心算法复用资源
        std::vector<ResourceNode*> freePool;
        
        for (size_t passIndex = 0; passIndex < mPasses.size(); ++passIndex) {
            // 释放不再使用的资源
            for (auto it = freePool.begin(); it != freePool.end(); ) {
                if ((*it)->lastUse < passIndex) {
                    it = freePool.erase(it);
                } else {
                    ++it;
                }
            }
            
            // 为当前 Pass 的输出分配资源
            for (auto resourceHandle : mPasses[passIndex].writes) {
                auto& resource = mResources[resourceHandle.id];
                
                // 尝试从 freePool 复用
                bool reused = false;
                for (auto* freeResource : freePool) {
                    if (isCompatible(resource.descriptor, freeResource->descriptor)) {
                        resource.physicalResource = freeResource->physicalResource;
                        reused = true;
                        LOG_DEBUG("FrameGraph: Reused resource for '%s'", 
                                 mPasses[passIndex].name.c_str());
                        break;
                    }
                }
                
                // 未能复用，创建新资源
                if (!reused) {
                    resource.physicalResource = createPhysicalResource(resource.descriptor);
                    LOG_DEBUG("FrameGraph: Allocated new resource for '%s'",
                             mPasses[passIndex].name.c_str());
                }
                
                freePool.push_back(&resource);
            }
        }
    }
    
    void topologicalSort() {
        // Kahn 算法拓扑排序
        // （确保依赖的 Pass 先执行）
        // 实现省略...
    }
    
    bool isCompatible(const ResourceDescriptor& a, const ResourceDescriptor& b) {
        if (a.type != b.type) return false;
        
        if (a.type == ResourceDescriptor::Type::Texture) {
            return a.texture.width == b.texture.width &&
                   a.texture.height == b.texture.height &&
                   a.texture.format == b.texture.format;
        } else {
            return a.buffer.size == b.buffer.size;
        }
    }
    
    Handle<HwTexture> createPhysicalResource(const ResourceDescriptor& desc) {
        // 调用 Driver 创建实际的 GPU 资源
        // 实现省略...
        return {};
    }
    
    std::vector<PassNode> mPasses;
    std::vector<ResourceNode> mResources;
    Driver* mDriver;
};

}  // namespace filament::backend
```

### 4.3 Frame Graph 使用示例

```cpp
// 构建 Frame Graph
FrameGraph fg;

// 1. Shadow Pass
auto shadowMap = fg.addPass("ShadowPass",
    [&](FrameGraph::Builder& builder) {
        // 声明输出：Shadow Map
        builder.write(shadowMap, {
            .type = ResourceDescriptor::Type::Texture,
            .texture = {2048, 2048, TextureFormat::DEPTH16, 1}
        });
    },
    [=](Driver& driver, const PassResources& resources) {
        // 执行阴影渲染
        auto shadowRT = resources.getRenderTarget(shadowMap);
        driver.beginRenderPass(shadowRT, {.clearDepth = true});
        // ... 渲染阴影 ...
        driver.endRenderPass();
    }
);

// 2. Color Pass
auto colorBuffer = fg.addPass("ColorPass",
    [&](FrameGraph::Builder& builder) {
        // 声明输入：Shadow Map
        builder.read(shadowMap);
        
        // 声明输出：Color Buffer
        builder.write(colorBuffer, {
            .type = ResourceDescriptor::Type::Texture,
            .texture = {1920, 1080, TextureFormat::RGBA16F, 1}
        });
    },
    [=](Driver& driver, const PassResources& resources) {
        // 执行主渲染
        auto colorRT = resources.getRenderTarget(colorBuffer);
        auto shadowTex = resources.getTexture(shadowMap);
        
        driver.beginRenderPass(colorRT, {.clearColor = true});
        // ... 渲染场景，使用 shadowTex ...
        driver.endRenderPass();
    }
);

// 3. Post-Process Pass
fg.addPass("BloomPass",
    [&](FrameGraph::Builder& builder) {
        builder.read(colorBuffer);
        builder.write(finalBuffer, {
            .type = ResourceDescriptor::Type::Texture,
            .texture = {1920, 1080, TextureFormat::RGBA8, 1}
        });
    },
    [=](Driver& driver, const PassResources& resources) {
        // Bloom 后处理
        // ...
    }
);

// 编译并执行
fg.compile();
fg.execute(driver);

// 下一帧
fg.reset();
```

**Frame Graph 优势**：
- **自动资源管理**：无需手动管理临时纹理
- **内存复用**：Shadow Map、Bloom Temp 等可复用
- **自动剔除**：未使用的 Debug Pass 自动跳过
- **易于扩展**：添加新 Pass 不影响现有代码

---

## 5. Material 变体系统

### 5.1 变体生成策略

Filament 的 Material 编译器（`matc`）根据场景需求生成多个**变体（Variants）**：

```
Base Material
    │
    ├─> Variant 1: No Skinning, No Shadow
    ├─> Variant 2: No Skinning, With Shadow
    ├─> Variant 3: With Skinning, No Shadow
    ├─> Variant 4: With Skinning, With Shadow
    ├─> Variant 5: With Skinning, With Shadow, With Vertex Color
    └─> ... (2^N 种组合)
```

**变体维度**：
- **Skinning**: 是否需要骨骼动画
- **Shadow**: 是否接收阴影
- **Vertex Color**: 是否使用顶点颜色
- **Instancing**: 是否使用 GPU Instancing
- **Morphing**: 是否支持 Blend Shapes

**编译时优化**：
- Dead Code Elimination：未使用的代码路径被移除
- Constant Folding：编译时常量计算

### 5.2 运行时变体选择

```cpp
// filament/src/details/Material.cpp

namespace filament {

class Material {
public:
    // 选择合适的变体
    uint32_t selectVariant(const RenderableInfo& renderable, const View& view) const {
        uint32_t variant = 0;
        
        // Bit 0: Skinning
        if (renderable.hasSkinning) {
            variant |= VARIANT_HAS_SKINNING;
        }
        
        // Bit 1: Shadow Receiver
        if (view.hasShadows() && renderable.receiveShadows) {
            variant |= VARIANT_HAS_SHADOW_RECEIVER;
        }
        
        // Bit 2: Vertex Color
        if (renderable.hasVertexColor) {
            variant |= VARIANT_HAS_VERTEX_COLOR;
        }
        
        // Bit 3: Instancing
        if (renderable.instanceCount > 1) {
            variant |= VARIANT_HAS_INSTANCING;
        }
        
        // 检查变体是否存在
        if (mVariants.find(variant) == mVariants.end()) {
            // 回退到基础变体
            LOG_WARNING("Material variant %u not found, using default", variant);
            variant = 0;
        }
        
        return variant;
    }
    
    Handle<HwProgram> getProgram(uint32_t variant) const {
        auto it = mVariants.find(variant);
        if (it != mVariants.end()) {
            return it->second.program;
        }
        return mVariants.at(0).program;  // 默认变体
    }
    
private:
    struct Variant {
        Handle<HwProgram> program;
        PipelineState pipelineState;
    };
    
    std::unordered_map<uint32_t, Variant> mVariants;
    
    static constexpr uint32_t VARIANT_HAS_SKINNING         = 0x01;
    static constexpr uint32_t VARIANT_HAS_SHADOW_RECEIVER  = 0x02;
    static constexpr uint32_t VARIANT_HAS_VERTEX_COLOR     = 0x04;
    static constexpr uint32_t VARIANT_HAS_INSTANCING       = 0x08;
};

}  // namespace filament
```

### 5.3 Uber Shader vs 多变体对比

**Uber Shader（单一着色器 + 分支）**：
```glsl
// Uber Shader 示例
void main() {
    vec4 color = texture(baseColorMap, uv);
    
    #ifdef HAS_VERTEX_COLOR
        color *= vertexColor;
    #endif
    
    #ifdef HAS_SHADOW
        float shadow = computeShadow();
        color *= shadow;
    #endif
    
    fragColor = color;
}
```

**优点**：
- 编译时间短（只编译一次）
- 磁盘占用小

**缺点**：
- 运行时分支影响性能
- GPU 寄存器占用高
- 难以优化

**多变体（为每种配置编译独立着色器）**：
```glsl
// Variant 1: No Shadow
void main() {
    vec4 color = texture(baseColorMap, uv);
    fragColor = color;
}

// Variant 2: With Shadow
void main() {
    vec4 color = texture(baseColorMap, uv);
    float shadow = computeShadow();
    color *= shadow;
    fragColor = color;
}
```

**优点**：
- 无运行时分支，性能最优
- 编译器可充分优化
- 寄存器占用低

**缺点**：
- 编译时间长（指数级增长）
- 磁盘占用大

**Filament 策略**：混合方案
- 高频变化的特性（如 Shadow）使用变体
- 低频变化的特性（如材质参数）使用 Uniform
- 控制变体数量（< 100 个）

---

## 6. 延迟渲染管线实现

### 6.1 延迟渲染概述

延迟渲染（Deferred Rendering）将渲染分为两个阶段：

1. **GBuffer Pass**：渲染几何信息到多个纹理（Albedo、Normal、Depth、Metallic/Roughness）
2. **Lighting Pass**：使用 GBuffer 计算光照（全屏 quad）

**优势**：
- 支持大量动态光源（O(光源数) 而非 O(光源数 × 物体数)）
- 延迟计算，减少 overdraw

**劣势**：
- 内存带宽高（GBuffer 读写）
- 不支持硬件 MSAA（需要自定义 AA）
- 透明物体需单独渲染

### 6.2 GBuffer 布局

```cpp
struct GBuffer {
    // RT0: Albedo (RGB) + Occlusion (A)
    Texture* albedoOcclusion;  // RGBA8
    
    // RT1: Normal (XY in octahedral encoding) + Roughness (Z) + Metallic (W)
    Texture* normalRoughMetal;  // RGBA8
    
    // RT2: Emissive (RGB) + AO (A)
    Texture* emissive;  // RGBA16F
    
    // Depth/Stencil
    Texture* depthStencil;  // DEPTH24_STENCIL8
};
```

### 6.3 完整延迟渲染管线

```cpp
class DeferredPipeline {
public:
    void render(View& view, Scene& scene) {
        // 1. GBuffer Pass
        renderGBuffer(view, scene);
        
        // 2. Lighting Pass
        renderLighting(view);
        
        // 3. Forward Pass（透明物体）
        renderTransparent(view, scene);
        
        // 4. Post-Processing
        renderPostProcess(view);
    }
    
private:
    void renderGBuffer(View& view, Scene& scene) {
        // 设置 MRT（Multiple Render Targets）
        driver->beginRenderPass(mGBufferRT, {
            .clearColor = true,
            .clearDepth = true
        });
        
        // 渲染所有不透明物体
        for (auto& renderable : scene.getOpaqueRenderables()) {
            // 使用 GBuffer Material
            auto* material = renderable.getMaterial();
            auto variant = material->selectVariant(renderable, view);
            auto program = material->getProgram(variant | VARIANT_DEFERRED_GBUFFER);
            
            driver->bindPipeline(program);
            driver->draw(renderable.primitiveType, ...);
        }
        
        driver->endRenderPass();
    }
    
    void renderLighting(View& view) {
        // 绑定 GBuffer 纹理作为输入
        driver->bindTexture(0, mGBuffer.albedoOcclusion);
        driver->bindTexture(1, mGBuffer.normalRoughMetal);
        driver->bindTexture(2, mGBuffer.emissive);
        driver->bindTexture(3, mGBuffer.depthStencil);
        
        // 设置输出 RT
        driver->beginRenderPass(mLightingRT, {.clearColor = false});
        
        // 全屏 quad，执行光照计算
        driver->bindPipeline(mDeferredLightingProgram);
        driver->draw(PrimitiveType::TRIANGLES, mFullscreenQuad, 6);
        
        driver->endRenderPass();
    }
    
    GBuffer mGBuffer;
    Handle<HwRenderTarget> mGBufferRT;
    Handle<HwRenderTarget> mLightingRT;
    Handle<HwProgram> mDeferredLightingProgram;
    Handle<HwVertexBuffer> mFullscreenQuad;
    Driver* driver;
};
```

**Deferred Lighting Shader**：
```glsl
// deferred_lighting.fs
uniform sampler2D albedoOcclusion;
uniform sampler2D normalRoughMetal;
uniform sampler2D emissive;
uniform sampler2D depthStencil;

uniform vec3 lightPositions[MAX_LIGHTS];
uniform vec3 lightColors[MAX_LIGHTS];
uniform int lightCount;

void main() {
    // 从 GBuffer 重建世界坐标
    vec2 uv = gl_FragCoord.xy / screenSize;
    float depth = texture(depthStencil, uv).r;
    vec3 worldPos = reconstructWorldPos(uv, depth);
    
    // 读取材质属性
    vec4 albedoOcc = texture(albedoOcclusion, uv);
    vec4 normRoughMetal = texture(normalRoughMetal, uv);
    
    vec3 albedo = albedoOcc.rgb;
    vec3 normal = decodeNormal(normRoughMetal.xy);
    float roughness = normRoughMetal.z;
    float metallic = normRoughMetal.w;
    
    // 计算光照
    vec3 color = vec3(0.0);
    for (int i = 0; i < lightCount; ++i) {
        vec3 L = normalize(lightPositions[i] - worldPos);
        vec3 V = normalize(cameraPos - worldPos);
        vec3 H = normalize(L + V);
        
        // PBR BRDF
        vec3 F0 = mix(vec3(0.04), albedo, metallic);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        float NDF = distributionGGX(normal, H, roughness);
        float G = geometrySmith(normal, V, L, roughness);
        
        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(normal, V), 0.0) * max(dot(normal, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;
        
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        vec3 radiance = lightColors[i] * max(dot(normal, L), 0.0);
        
        color += (kD * albedo / PI + specular) * radiance;
    }
    
    // 添加 Emissive
    color += texture(emissive, uv).rgb;
    
    fragColor = vec4(color, 1.0);
}
```

---

## 7. 性能优化技术

### 7.1 Early-Z 优化

**原理**：
- GPU 在执行 Fragment Shader 前会进行深度测试（Early-Z）
- 如果 fragment 被遮挡，直接丢弃，不执行昂贵的 Fragment Shader

**实现方式**：

**方法 1：Depth Prepass**
```cpp
void renderWithDepthPrepass(View& view, Scene& scene) {
    // 1. Depth Prepass（只写深度，不写颜色）
    driver->beginRenderPass(mDepthRT, {.clearDepth = true});
    
    for (auto& renderable : scene.getOpaqueRenderables()) {
        // 使用简单的 Depth-Only Shader
        driver->bindPipeline(mDepthOnlyProgram);
        driver->setColorWrite(false);  // 禁用颜色写入
        driver->draw(...);
    }
    
    driver->endRenderPass();
    
    // 2. Color Pass（复用深度缓冲，利用 Early-Z）
    driver->beginRenderPass(mColorRT, {
        .clearColor = true,
        .clearDepth = false  // 不清除深度
    });
    
    for (auto& renderable : scene.getOpaqueRenderables()) {
        // 使用完整 Shader，但大部分 fragment 会被 Early-Z 剔除
        driver->bindPipeline(renderable.material->getProgram());
        driver->setDepthFunc(CompareFunc::EQUAL);  // 只渲染深度相等的 fragment
        driver->draw(...);
    }
    
    driver->endRenderPass();
}
```

**效果**：
- 场景复杂度高（如室内场景）时，性能提升 20-50%
- Fragment Shader 越昂贵，提升越明显

**方法 2：前向后排序**
```cpp
void sortFrontToBack(std::vector<RenderableInfo>& renderables, const vec3& cameraPos) {
    std::sort(renderables.begin(), renderables.end(), 
              [&cameraPos](const RenderableInfo& a, const RenderableInfo& b) {
        float distA = distance(a.worldPosition, cameraPos);
        float distB = distance(b.worldPosition, cameraPos);
        return distA < distB;  // 近的先渲染
    });
}
```

### 7.2 GPU Instancing

**原理**：一次 Draw Call 渲染多个相同几何体的实例。

**实现**：
```cpp
// 准备 Instance 数据
struct InstanceData {
    mat4 modelMatrix;
    vec4 color;
};

std::vector<InstanceData> instances;
for (int i = 0; i < 1000; ++i) {
    instances.push_back({
        .modelMatrix = translate(vec3(i * 2.0f, 0, 0)),
        .color = vec4(random(), random(), random(), 1.0f)
    });
}

// 创建 Instance Buffer
Handle<HwUniformBuffer> instanceBuffer = driver->createUniformBuffer(
    instances.size() * sizeof(InstanceData)
);
driver->updateUniformBuffer(instanceBuffer, instances.data(), instances.size() * sizeof(InstanceData));

// 绑定并发出 Instanced Draw Call
driver->bindUniformBuffer(1, instanceBuffer);
driver->drawInstanced(PrimitiveType::TRIANGLES, vertexBuffer, indexBuffer, indexCount, instances.size());
```

**Vertex Shader**：
```glsl
// instanced.vs
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

// Instance 数据
layout(std140) uniform InstanceBlock {
    mat4 modelMatrices[1000];
    vec4 colors[1000];
};

out vec4 vColor;

void main() {
    int instanceID = gl_InstanceID;
    
    mat4 model = modelMatrices[instanceID];
    vColor = colors[instanceID];
    
    gl_Position = projection * view * model * vec4(position, 1.0);
}
```

**性能对比**：
- 传统方式：1000 个物体 = 1000 个 Draw Call
- Instancing：1000 个物体 = 1 个 Draw Call
- **性能提升**：10-100 倍（取决于物体复杂度）

### 7.3 DrawCall 批处理

**合并条件**：
- 相同的 Material
- 相同的 Pipeline State
- 顶点格式相同

**实现**：
```cpp
class DrawCallBatcher {
public:
    void addRenderable(const RenderableInfo& renderable) {
        uint64_t batchKey = computeBatchKey(renderable);
        mBatches[batchKey].push_back(renderable);
    }
    
    void flush(Driver& driver) {
        for (auto& [key, renderables] : mBatches) {
            if (renderables.size() == 1) {
                // 单个物体，直接渲染
                draw(driver, renderables[0]);
            } else {
                // 多个物体，合并成一个 Draw Call
                drawBatched(driver, renderables);
            }
        }
        
        mBatches.clear();
    }
    
private:
    uint64_t computeBatchKey(const RenderableInfo& renderable) {
        utils::hash::MurmurHashFn<uint64_t> hasher;
        hasher(renderable.materialId);
        hasher(renderable.pipelineStateHash);
        return hasher.get();
    }
    
    void drawBatched(Driver& driver, const std::vector<RenderableInfo>& renderables) {
        // 合并 Vertex/Index Buffers
        // 或使用 Multi-Draw Indirect
        driver->multiDrawElementsIndirect(...);
    }
    
    std::unordered_map<uint64_t, std::vector<RenderableInfo>> mBatches;
};
```

---

## 8. 调试和性能分析

### 8.1 Pipeline State Viewer

```cpp
class PipelineDebugger {
public:
    void visualizePipelineStates(const std::vector<RenderableInfo>& renderables) {
        // 统计 Pipeline State 分布
        std::unordered_map<uint64_t, int> stateHistogram;
        
        for (const auto& renderable : renderables) {
            stateHistogram[renderable.pipelineStateHash]++;
        }
        
        // 输出统计
        LOG_INFO("=== Pipeline State Statistics ===");
        LOG_INFO("Total renderables: %zu", renderables.size());
        LOG_INFO("Unique states: %zu", stateHistogram.size());
        
        // 按使用频率排序
        std::vector<std::pair<uint64_t, int>> sorted(stateHistogram.begin(), stateHistogram.end());
        std::sort(sorted.begin(), sorted.end(), 
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        
        LOG_INFO("Top 10 most used states:");
        for (int i = 0; i < std::min(10, (int)sorted.size()); ++i) {
            LOG_INFO("  State 0x%016llx: %d renderables", sorted[i].first, sorted[i].second);
        }
    }
};
```

### 8.2 RenderDoc 集成

```cpp
#if defined(ENABLE_RENDERDOC)
    #include <renderdoc_app.h>
    
    static RENDERDOC_API_1_1_2* sRenderDocAPI = nullptr;
    
    void initRenderDoc() {
        #if defined(_WIN32)
            if (HMODULE mod = GetModuleHandleA("renderdoc.dll")) {
                pRENDERDOC_GetAPI RENDERDOC_GetAPI = 
                    (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
                RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void**)&sRenderDocAPI);
            }
        #endif
    }
    
    void captureFrame() {
        if (sRenderDocAPI) {
            sRenderDocAPI->TriggerCapture();
        }
    }
#endif
```

---

## 9. 最佳实践总结

1. **最小化状态切换**
   - 按材质 ID 排序 Renderables
   - 使用 Pipeline State Cache
   - 合并相同材质的 Draw Calls

2. **利用 Early-Z**
   - Depth Prepass（复杂场景）
   - 前向后排序（简单场景）
   - 避免 Alpha Test（使用 Alpha Blend）

3. **使用 GPU Instancing**
   - 重复几何体（树木、草地、粒子）
   - Instance Buffer 存储变换和颜色

4. **Frame Graph 管理资源**
   - 声明式 API，易于维护
   - 自动资源复用，减少内存
   - 自动剔除未使用 Pass

5. **控制 Material 变体数量**
   - 高频特性使用变体（Shadow、Skinning）
   - 低频特性使用 Uniform（颜色、粗糙度）
   - 限制在 64-128 个变体

6. **性能监控**
   - 使用 GPU Profiler 分析 Pass 耗时
   - RenderDoc/Nsight 查看 Pipeline State
   - 统计 Draw Call、State Change 次数

---

## 10. 常见问题

### Q1: 如何减少 Pipeline State 切换次数？

**A**: 
- 按材质 ID 排序 Renderables
- 使用 Material Instance 共享基础材质
- 合并相同材质的物体到一个 Draw Call

### Q2: Depth Prepass 何时有性能提升？

**A**: 
- 场景复杂，overdraw 严重（> 3x）
- Fragment Shader 昂贵（PBR、复杂光照）
- 不适合简单场景（增加 Draw Call 开销）

### Q3: Frame Graph 的内存节省有多少？

**A**: 
- 典型场景：节省 30-50% 临时纹理内存
- 复杂管线（多个后处理）：节省 50-70%

### Q4: Uber Shader 还是多变体？

**A**: 
- 少量配置（< 8）：Uber Shader
- 中等配置（8-64）：多变体
- 大量配置（> 64）：混合策略

---

## 11. 相关文档

- [01-data-flow-analysis.md](./01-data-flow-analysis.md) - 数据流架构
- [02-multithreading-design.md](./02-multithreading-design.md) - 多线程设计
- [04-ecs-best-practices.md](./04-ecs-best-practices.md) - ECS 最佳实践
- [../optimization/03-drawcall-reduction.md](../optimization/03-drawcall-reduction.md) - DrawCall 优化
- [../shaders/01-material-lang-reference.md](../shaders/01-material-lang-reference.md) - Material 语言

---

## 12. 总结

Filament 的渲染管线架构体现了现代渲染引擎的设计精髓：

1. **高效的 Pipeline State 管理**
   - Hash 缓存避免重复创建
   - 排序优化减少状态切换

2. **灵活的 RenderPass 组织**
   - 支持延迟、前向、混合渲染
   - Frame Graph 声明式管线

3. **智能的 Material 变体系统**
   - 编译时优化，运行时选择
   - 平衡性能和磁盘占用

4. **完善的性能优化**
   - Early-Z、Instancing、批处理
   - 调试工具和性能分析

5. **可扩展性**
   - 易于添加新的 RenderPass
   - 自定义 Material 和后处理

通过理解和应用这些架构设计，您可以构建高性能、可维护的 Filament 渲染应用，充分发挥现代 GPU 的能力。

渲染管线不仅是技术实现，更是艺术：在性能、质量、可维护性之间找到最佳平衡点。
