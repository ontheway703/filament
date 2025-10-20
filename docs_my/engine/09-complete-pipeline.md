# 完整渲染管线：从应用到GPU

## 概述

本文档整合前面所有章节的知识，呈现 Filament 从应用层代码到 GPU 执行的完整渲染管线。理解这个端到端的流程，能帮助您优化性能、调试问题，并深入掌握 Filament 架构。

### 管线概览

```
应用层 (C++ API)
    ↓
Engine 层 (Engine, Scene, View, Camera)
    ↓
FEngine (内部引擎实现)
    ↓
Renderer (剔除、排序、批处理)
    ↓
RenderPass (渲染通道管理)
    ↓
Backend API (Driver 接口)
    ↓
CommandStream (命令录制)
    ↓
Graphics Driver (OpenGL/Vulkan/Metal)
    ↓
GPU 执行
```

---

## 第一阶段：应用层设置

### 1.1 资源创建

应用程序首先创建渲染所需的所有资源：

```cpp
// === 引擎初始化 ===
Engine* engine = Engine::create(Engine::Backend::VULKAN);

// === 场景设置 ===
Scene* scene = engine->createScene();

// === 几何资源 ===
VertexBuffer* vb = VertexBuffer::Builder()
    .vertexCount(vertexCount)
    .bufferCount(1)
    .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3)
    .attribute(VertexAttribute::NORMAL, 0, VertexBuffer::AttributeType::FLOAT3)
    .build(*engine);

IndexBuffer* ib = IndexBuffer::Builder()
    .indexCount(indexCount)
    .bufferType(IndexBuffer::IndexType::UINT16)
    .build(*engine);

// 上传数据
vb->setBufferAt(*engine, 0,
    VertexBuffer::BufferDescriptor(vertices, vertexDataSize));
ib->setBuffer(*engine,
    IndexBuffer::BufferDescriptor(indices, indexDataSize));

// === 材质资源 ===
Material* material = Material::Builder()
    .package(materialData, materialSize)
    .build(*engine);

MaterialInstance* matInstance = material->createInstance();
matInstance->setParameter("baseColor", float4{1, 0, 0, 1});

Texture* texture = Texture::Builder()
    .width(512).height(512)
    .format(Texture::InternalFormat::RGBA8)
    .build(*engine);

matInstance->setParameter("albedo", texture, sampler);

// === 渲染实体 ===
Entity entity = EntityManager::get().create();

RenderableManager::Builder(1)
    .boundingBox({{-1, -1, -1}, {1, 1, 1}})
    .geometry(0, PrimitiveType::TRIANGLES, vb, ib)
    .material(0, matInstance)
    .build(*engine, entity);

scene->addEntity(entity);

// === 光照 ===
Entity sun = EntityManager::get().create();
LightManager::Builder(LightManager::Type::DIRECTIONAL)
    .direction({0, -1, 0})
    .intensity(100000)
    .build(*engine, sun);
scene->addEntity(sun);

// === 相机和视图 ===
Entity cameraEntity = EntityManager::get().create();
Camera* camera = engine->createCamera(cameraEntity);
camera->setProjection(45.0, 16.0/9.0, 0.1, 100.0);
camera->lookAt({0, 2, 5}, {0, 0, 0}, {0, 1, 0});

View* view = engine->createView();
view->setScene(scene);
view->setCamera(camera);
view->setViewport({0, 0, 1920, 1080});
```

**此阶段在 Engine 层发生的事情：**

1. **资源分配**：Engine 为每个资源分配 `Handle<HwXXX>`
2. **命令录制**：资源创建命令进入 CommandStream
3. **延迟执行**：命令将在渲染线程执行

```cpp
// 内部流程示例（简化）
Handle<HwVertexBuffer> Engine::createVertexBuffer(...) {
    Handle<HwVertexBuffer> handle = mResourceAllocator.alloc<HwVertexBuffer>();
    mCommandStream.createVertexBuffer(handle, ...);
    return handle;
}
```

---

## 第二阶段：帧开始（beginFrame）

### 2.1 应用层调用

```cpp
Renderer* renderer = engine->createRenderer();

// 帧开始
if (renderer->beginFrame(swapChain)) {
    // ... 渲染操作
    renderer->endFrame();
}
```

### 2.2 内部执行

```cpp
bool Renderer::beginFrame(SwapChain* swapChain) {
    // 1. 等待 GPU 完成前一帧（如果使用 vsync）
    driver.beginFrame(...);

    // 2. 获取 SwapChain 的下一个可用 RenderTarget
    Handle<HwRenderTarget> rt = swapChain->getRenderTarget();

    // 3. 重置命令流
    commandStream.reset();

    // 4. 更新时间戳
    frameId++;
    frameTime = getCurrentTime();

    return true;  // 准备好渲染
}
```

**Backend Driver 执行（Vulkan 示例）：**

```cpp
void VulkanDriver::beginFrame(...) {
    // 1. 等待栅栏（fence）
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    // 2. 获取 SwapChain 图像
    uint32_t imageIndex;
    vkAcquireNextImageKHR(device, swapChain, UINT64_MAX,
                         imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    // 3. 重置命令缓冲区
    vkResetCommandBuffer(commandBuffers[currentFrame], 0);
    vkBeginCommandBuffer(commandBuffers[currentFrame], &beginInfo);

    // 4. 重置栅栏
    vkResetFences(device, 1, &inFlightFences[currentFrame]);
}
```

---

## 第三阶段：渲染（render）

### 3.1 应用层调用

```cpp
renderer->render(view);
```

这是整个管线中最复杂的部分，涉及多个子系统协作。

### 3.2 场景剔除 (Culling)

**FView::prepare()** 执行可见性剔除：

```cpp
void FView::prepare(...) {
    // 1. 获取相机视锥体
    Frustum frustum = camera->getFrustum(projection, viewMatrix);

    // 2. 遍历场景中所有 Renderable
    auto& rcm = engine->getRenderableManager();
    const auto& renderables = scene->getRenderables();

    visibleRenderables.clear();
    for (Entity entity : renderables) {
        auto instance = rcm.getInstance(entity);

        // 获取世界空间包围盒
        Box worldBounds = getWorldBoundingBox(entity);

        // 视锥剔除
        if (frustum.intersects(worldBounds)) {
            visibleRenderables.push_back(instance);
        }
    }

    // 3. 同样剔除光源
    visibleLights = cullLights(scene->getLights(), frustum);
}
```

**性能统计：**
- 输入：1000 个实体
- 视锥剔除后：300 个可见实体
- 遮挡剔除后（可选）：250 个实体

### 3.3 排序 (Sorting)

对可见物体排序以优化渲染：

```cpp
void FView::sort(std::vector<RenderPrimitive>& primitives) {
    // 1. 分离不透明和透明物体
    auto partition = std::partition(primitives.begin(), primitives.end(),
        [](const RenderPrimitive& prim) {
            return prim.material->isOpaque();
        }
    );

    // 2. 不透明物体：前向后排序（深度优先，减少 overdraw）
    std::sort(primitives.begin(), partition,
        [](const RenderPrimitive& a, const RenderPrimitive& b) {
            return a.depth < b.depth;  // 近到远
        }
    );

    // 3. 透明物体：后向前排序（正确混合）
    std::sort(partition, primitives.end(),
        [](const RenderPrimitive& a, const RenderPrimitive& b) {
            return a.depth > b.depth;  // 远到近
        }
    );

    // 4. 材质排序（减少状态切换）
    std::stable_sort(primitives.begin(), partition,
        [](const RenderPrimitive& a, const RenderPrimitive& b) {
            if (a.depth == b.depth) {
                return a.material->getId() < b.material->getId();
            }
            return a.depth < b.depth;
        }
    );
}
```

### 3.4 渲染通道 (Render Passes)

Filament 使用多通道渲染策略：

```cpp
void Renderer::render(View* view) {
    // === Pass 1: 阴影贴图 ===
    for (Light light : view->getShadowCasters()) {
        renderShadowMap(light, view->getVisibleRenderables());
    }

    // === Pass 2: Z-Prepass (可选) ===
    if (view->hasZPrepass()) {
        renderDepthPrepass(view);
    }

    // === Pass 3: SSAO (可选) ===
    if (view->hasSSAO()) {
        renderSSAO(view, depthTexture);
    }

    // === Pass 4: 主渲染通道 ===
    renderMainPass(view);

    // === Pass 5: 天空盒 ===
    if (view->hasSkybox()) {
        renderSkybox(view);
    }

    // === Pass 6: 透明物体 ===
    renderTransparentPass(view);

    // === Pass 7: 后处理 ===
    if (view->hasPostProcessing()) {
        renderPostProcess(view);
    }
}
```

**主渲染通道详细流程：**

```cpp
void Renderer::renderMainPass(View* view) {
    // 1. 设置 RenderTarget
    driver.beginRenderPass(view->getRenderTarget(), renderPassParams);

    // 2. 清除缓冲区
    driver.clearRenderTarget(COLOR | DEPTH | STENCIL);

    // 3. 设置全局状态
    driver.setViewport(view->getViewport());
    driver.setScissor(view->getScissor());

    // 4. 渲染所有可见物体
    for (RenderPrimitive& prim : view->getVisiblePrimitives()) {
        renderPrimitive(prim, view);
    }

    // 5. 结束渲染通道
    driver.endRenderPass();
}

void Renderer::renderPrimitive(const RenderPrimitive& prim, View* view) {
    // 1. 绑定材质（设置管线状态）
    Handle<HwProgram> program = prim.material->getProgram();
    driver.useProgram(program);

    // 2. 设置 Uniform 参数
    // - 变换矩阵
    mat4 modelMatrix = getWorldTransform(prim.entity);
    mat4 mvpMatrix = view->getProjectionMatrix() *
                     view->getViewMatrix() *
                     modelMatrix;
    driver.setUniform(uniformMVP, mvpMatrix);

    // - 材质参数
    driver.setUniform(uniformBaseColor, prim.material->getBaseColor());
    driver.setUniform(uniformMetallic, prim.material->getMetallic());
    driver.setUniform(uniformRoughness, prim.material->getRoughness());

    // - 光照参数
    driver.setUniform(uniformLightDir, view->getDirectionalLight().direction);
    driver.setUniform(uniformLightColor, view->getDirectionalLight().color);

    // 3. 绑定纹理
    driver.bindTexture(0, prim.material->getAlbedoTexture());
    driver.bindTexture(1, prim.material->getNormalTexture());
    driver.bindTexture(2, view->getIBLReflectionTexture());

    // 4. 绑定几何数据
    driver.bindVertexBuffer(prim.vertexBuffer);
    driver.bindIndexBuffer(prim.indexBuffer);

    // 5. 设置渲染状态
    driver.setRasterState(prim.material->getRasterState());
    driver.setDepthState(prim.material->getDepthState());
    driver.setBlendState(prim.material->getBlendState());

    // 6. 发起绘制调用
    driver.draw(PrimitiveType::TRIANGLES,
               prim.indexOffset,
               prim.indexCount);
}
```

---

## 第四阶段：命令流转换

### 4.1 Driver 接口调用转换为 CommandStream

所有 `driver.xxx()` 调用不会立即执行，而是录制到命令流：

```cpp
// 应用层调用
driver.draw(PrimitiveType::TRIANGLES, 0, 1000);

// 内部实现
void Driver::draw(PrimitiveType type, uint32_t offset, uint32_t count) {
    mCommandStream.draw(type, offset, count);
}

// CommandStream 录制
void CommandStream::draw(PrimitiveType type, uint32_t offset, uint32_t count) {
    void* cmd = allocate(sizeof(DrawCommand));
    new (cmd) DrawCommand{type, offset, count};
}
```

**命令流内存布局：**

```
CommandStream 缓冲区 (4MB):
+-------------------+
| CreateVertexBuffer|  <- 初始化时的资源创建命令
+-------------------+
| CreateTexture     |
+-------------------+
| ...               |
+-------------------+
| UseProgram        |  <- 渲染帧的命令
+-------------------+
| SetUniform        |
+-------------------+
| BindTexture       |
+-------------------+
| Draw              |
+-------------------+
| ...               |
+-------------------+
```

### 4.2 命令批处理优化

```cpp
class CommandStream {
    // 合并相同材质的绘制调用
    void optimizeBatch() {
        std::vector<DrawCommand> batch;

        for (Command& cmd : commands) {
            if (cmd.type == CommandType::DRAW) {
                DrawCommand* draw = static_cast<DrawCommand*>(&cmd);

                // 如果材质相同且几何连续，合并
                if (!batch.empty() &&
                    canMergeDraw(batch.back(), *draw)) {
                    batch.back().count += draw->count;
                } else {
                    batch.push_back(*draw);
                }
            }
        }

        // 替换原命令
        replaceDrawCommands(batch);
    }
};
```

---

## 第五阶段：命令执行

### 5.1 渲染线程执行

```cpp
// Engine 主循环（渲染线程）
void FEngine::loop() {
    while (!shutdown) {
        // 1. 等待命令流准备好
        mCommandBufferQueue.waitForCommands();

        // 2. 获取命令缓冲区
        CommandBuffer* cmds = mCommandBufferQueue.getCommandBuffer();

        // 3. 执行命令
        driver->execute(cmds);

        // 4. 回收命令缓冲区
        mCommandBufferQueue.releaseCommandBuffer(cmds);
    }
}
```

### 5.2 Driver 执行命令

以 Vulkan 为例：

```cpp
void VulkanDriver::execute(CommandBuffer* cmds) {
    CommandStream stream(cmds);

    while (Command* cmd = stream.next()) {
        switch (cmd->type) {
            case CommandType::USE_PROGRAM: {
                UseProgramCommand* c = static_cast<UseProgramCommand*>(cmd);
                Handle<HwProgram> handle = c->program;

                VulkanProgram* program = handle_cast<VulkanProgram>(handle);
                vkCmdBindPipeline(commandBuffer,
                                 VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 program->pipeline);
                break;
            }

            case CommandType::SET_UNIFORM: {
                SetUniformCommand* c = static_cast<SetUniformCommand*>(cmd);
                // 更新 Uniform Buffer
                memcpy(uniformBufferMapped + c->offset, c->data, c->size);
                break;
            }

            case CommandType::BIND_TEXTURE: {
                BindTextureCommand* c = static_cast<BindTextureCommand*>(cmd);
                VulkanTexture* texture = handle_cast<VulkanTexture>(c->texture);

                // 更新描述符集
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageView = texture->imageView;
                imageInfo.sampler = texture->sampler;
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkWriteDescriptorSet descriptorWrite{};
                descriptorWrite.dstSet = descriptorSets[currentFrame];
                descriptorWrite.dstBinding = c->binding;
                descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descriptorWrite.pImageInfo = &imageInfo;

                vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
                break;
            }

            case CommandType::DRAW: {
                DrawCommand* c = static_cast<DrawCommand*>(cmd);

                // 绑定描述符集
                vkCmdBindDescriptorSets(commandBuffer,
                                       VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       pipelineLayout, 0, 1,
                                       &descriptorSets[currentFrame],
                                       0, nullptr);

                // 绑定顶点缓冲区
                VkBuffer vertexBuffers[] = {currentVertexBuffer};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

                // 绑定索引缓冲区
                vkCmdBindIndexBuffer(commandBuffer, currentIndexBuffer, 0,
                                    VK_INDEX_TYPE_UINT16);

                // 绘制
                vkCmdDrawIndexed(commandBuffer,
                                c->count,     // indexCount
                                1,            // instanceCount
                                c->offset,    // firstIndex
                                0,            // vertexOffset
                                0);           // firstInstance
                break;
            }

            // ... 其他命令类型
        }
    }
}
```

---

## 第六阶段：帧结束（endFrame）

### 6.1 应用层调用

```cpp
renderer->endFrame();
```

### 6.2 内部执行

```cpp
void Renderer::endFrame() {
    // 1. 提交所有命令到渲染线程
    commandStream.flush();

    // 2. 通知 SwapChain 准备呈现
    driver.endFrame();

    // 3. 执行延迟销毁
    mResourceCleanupQueue.flush();
}
```

### 6.3 Driver 执行（Vulkan）

```cpp
void VulkanDriver::endFrame() {
    // 1. 结束命令缓冲区录制
    vkEndCommandBuffer(commandBuffers[currentFrame]);

    // 2. 提交命令缓冲区
    VkSubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]);

    // 3. 呈现到屏幕
    VkPresentInfoKHR presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(presentQueue, &presentInfo);

    // 4. 推进帧索引
    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}
```

---

## 第七阶段：GPU 执行

### 7.1 GPU 管线阶段

```
顶点着色器 (Vertex Shader)
    ↓
    输入：顶点数据 (位置、法线、UV 等)
    处理：变换到裁剪空间
    输出：gl_Position, varying 变量

    示例 GLSL:
    void main() {
        vec4 worldPos = modelMatrix * vec4(position, 1.0);
        gl_Position = projectionMatrix * viewMatrix * worldPos;
        fragNormal = mat3(normalMatrix) * normal;
        fragUV = uv;
    }

裁剪和光栅化 (Rasterization)
    ↓
    视锥裁剪
    透视除法（除以 w）
    视口变换
    三角形光栅化 -> 生成片段

片段着色器 (Fragment Shader)
    ↓
    输入：插值后的 varying 变量
    处理：PBR 光照计算
    输出：颜色

    示例 GLSL (简化 PBR):
    void main() {
        // 1. 采样纹理
        vec3 albedo = texture(albedoMap, fragUV).rgb;
        vec3 normal = normalize(texture(normalMap, fragUV).rgb * 2.0 - 1.0);
        float metallic = texture(metallicMap, fragUV).r;
        float roughness = texture(roughnessMap, fragUV).r;

        // 2. 光照计算
        vec3 N = normalize(fragNormal);
        vec3 V = normalize(cameraPos - fragWorldPos);
        vec3 L = normalize(lightDir);
        vec3 H = normalize(V + L);

        // 3. BRDF
        float NdotL = max(dot(N, L), 0.0);
        vec3 F0 = mix(vec3(0.04), albedo, metallic);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        float D = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);

        vec3 specular = (D * F * G) / (4.0 * max(dot(N, V), 0.0) * NdotL + 0.001);
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        vec3 diffuse = kD * albedo / PI;

        // 4. 直接光照
        vec3 directLighting = (diffuse + specular) * lightColor * NdotL;

        // 5. 间接光照 (IBL)
        vec3 kS = F;
        vec3 kD = 1.0 - kS;
        vec3 irradiance = texture(irradianceMap, N).rgb;
        vec3 diffuseIBL = irradiance * albedo;

        vec3 R = reflect(-V, N);
        vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * 5.0).rgb;
        vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
        vec3 specularIBL = prefilteredColor * (F * brdf.x + brdf.y);

        vec3 indirectLighting = (kD * diffuseIBL + specularIBL) * ao;

        // 6. 最终颜色
        vec3 color = directLighting + indirectLighting;

        fragColor = vec4(color, 1.0);
    }

深度/模板测试 (Depth/Stencil Test)
    ↓
    Early-Z（如果启用）
    片段深度 vs 深度缓冲区
    通过 -> 继续；失败 -> 丢弃

混合 (Blending)
    ↓
    不透明物体：覆盖写入
    透明物体：Alpha 混合
        finalColor = srcColor * srcAlpha + dstColor * (1 - srcAlpha)

帧缓冲区写入
    ↓
    写入颜色附件
    写入深度附件
```

### 7.2 GPU 性能分析

**典型帧时间分解（1080p, 60 FPS 目标 = 16.67ms）：**

| 阶段                | 时间 (ms) | 百分比 |
|---------------------|-----------|--------|
| CPU 逻辑            | 2.0       | 12%    |
| 剔除和排序          | 1.5       | 9%     |
| 命令录制            | 1.0       | 6%     |
| GPU 等待            | 0.5       | 3%     |
| 阴影贴图渲染        | 2.5       | 15%    |
| Z-Prepass           | 1.0       | 6%     |
| 主渲染通道          | 5.0       | 30%    |
| 后处理 (SSAO+Bloom) | 2.0       | 12%    |
| 其他                | 1.2       | 7%     |
| **总计**            | **16.7**  | **100%** |

---

## 完整示例：端到端渲染

```cpp
class FilamentApp {
public:
    void init() {
        // === 1. 引擎初始化 ===
        engine = Engine::create(Engine::Backend::VULKAN);
        swapChain = engine->createSwapChain(nativeWindow);
        renderer = engine->createRenderer();

        // === 2. 场景设置 ===
        scene = engine->createScene();

        // === 3. 加载模型 ===
        loadModel("assets/model.glb");

        // === 4. 光照设置 ===
        setupLighting();

        // === 5. 相机和视图 ===
        setupCamera();
    }

    void loadModel(const char* path) {
        // glTF 加载
        std::vector<uint8_t> data = readFile(path);
        FilamentAsset* asset = assetLoader->createAsset(data.data(), data.size());
        resourceLoader->loadResources(asset);

        // 添加到场景
        scene->addEntities(asset->getEntities(), asset->getEntityCount());
    }

    void setupLighting() {
        // 太阳光
        Entity sun = EntityManager::get().create();
        LightManager::Builder(LightManager::Type::DIRECTIONAL)
            .direction({0.6, -1, -0.8})
            .intensity(100000)
            .castShadows(true)
            .build(*engine, sun);
        scene->addEntity(sun);

        // IBL
        auto iblData = IBLLoader::loadIBL(engine, "assets/env");
        IndirectLight* ibl = IndirectLight::Builder()
            .reflections(iblData.reflectionMap)
            .irradiance(3, iblData.irradianceSH.data())
            .intensity(30000)
            .build(*engine);
        scene->setIndirectLight(ibl);

        // 天空盒
        Skybox* skybox = Skybox::Builder()
            .environment(iblData.skyboxMap)
            .build(*engine);
        scene->setSkybox(skybox);
    }

    void setupCamera() {
        cameraEntity = EntityManager::get().create();
        camera = engine->createCamera(cameraEntity);
        camera->setProjection(45.0, float(width)/height, 0.1, 100.0);
        camera->lookAt({0, 2, 5}, {0, 0, 0}, {0, 1, 0});

        view = engine->createView();
        view->setScene(scene);
        view->setCamera(camera);
        view->setViewport({0, 0, width, height});

        // 后处理
        view->setPostProcessingEnabled(true);
        view->setAntiAliasing(View::AntiAliasing::FXAA);
        view->getAmbientOcclusionOptions().enabled = true;
        view->getBloomOptions().enabled = true;
    }

    void render() {
        // === 帧循环 ===
        if (renderer->beginFrame(swapChain)) {
            // 更新场景
            updateCamera(deltaTime);
            updateAnimations(deltaTime);

            // 渲染
            renderer->render(view);

            // 结束帧
            renderer->endFrame();
        }
    }

    void updateCamera(float dt) {
        static float angle = 0;
        angle += dt * 0.5f;

        float3 eye = {
            sin(angle) * 5.0f,
            2.0f,
            cos(angle) * 5.0f
        };
        camera->lookAt(eye, {0, 0, 0}, {0, 1, 0});
    }

    void updateAnimations(float dt) {
        if (animator) {
            animator->applyAnimation(0, currentTime);
            animator->updateBoneMatrices();
        }
    }

    ~FilamentApp() {
        engine->destroy(renderer);
        engine->destroy(view);
        engine->destroy(scene);
        engine->destroy(swapChain);
        Engine::destroy(&engine);
    }

private:
    Engine* engine;
    SwapChain* swapChain;
    Renderer* renderer;
    Scene* scene;
    View* view;
    Camera* camera;
    Entity cameraEntity;
    Animator* animator = nullptr;
};

// 主函数
int main() {
    FilamentApp app;
    app.init();

    while (!shouldQuit()) {
        app.render();
    }

    return 0;
}
```

---

## 性能优化建议

### CPU 端优化

```cpp
// 1. 减少绘制调用（批处理）
// 不良：1000 个小物体 = 1000 次绘制
// 优化：合并为 10 个批次 = 10 次绘制

// 2. 使用实例化渲染
RenderableManager::Builder(1)
    .instances(100)  // 100 个实例，1 次绘制
    .build(*engine, entity);

// 3. 视锥剔除优化
// 使用更紧密的包围盒
.boundingBox(tightBounds);  // 而非保守的大包围盒

// 4. LOD (Level of Detail)
float distance = length(cameraPos - objectPos);
if (distance < 10.0f) {
    renderHighPolyModel();
} else if (distance < 50.0f) {
    renderMediumPolyModel();
} else {
    renderLowPolyModel();
}
```

### GPU 端优化

```cpp
// 1. 纹理压缩
Texture::Builder()
    .format(Texture::InternalFormat::ETC2_RGB8)  // 移动端
    .format(Texture::InternalFormat::BC7_RGBA)   // 桌面端
    .build(*engine);

// 2. Mipmap
Texture::Builder()
    .levels(log2(maxDimension) + 1)  // 完整 mipmap 链
    .build(*engine);

// 3. 减少 overdraw
// - Z-Prepass
// - 前向后排序不透明物体

// 4. 降低后处理分辨率
view->getAmbientOcclusionOptions().resolution = 0.5f;  // 半分辨率 SSAO
```

---

## 调试工具

### 1. Renderdoc 集成

```bash
# 使用 Renderdoc 捕获帧
renderdoc --capture ./your_app
```

### 2. Filament 内置调试

```cpp
// 启用调试标记
view->setDebugCamera(camera);

// 显示包围盒
view->setDebugMode(View::DebugMode::BOUNDING_BOXES);

// 显示级联阴影
view->setDebugMode(View::DebugMode::CASCADE_SPLITS);

// 打印统计信息
auto stats = renderer->getStatistics();
std::cout << "Draw calls: " << stats.drawCalls << std::endl;
std::cout << "Triangles: " << stats.triangles << std::endl;
```

---

## 总结

### 数据流向

```
应用代码
    ↓ (C++ API 调用)
Engine 公共接口
    ↓ (内部实现)
FEngine (剔除、排序、批处理)
    ↓ (Driver 接口调用)
CommandStream (命令录制)
    ↓ (跨线程传递)
渲染线程
    ↓ (Driver 执行)
Graphics API (Vulkan/Metal/OpenGL)
    ↓ (提交到 GPU)
GPU 硬件执行
    ↓
屏幕输出
```

### 关键要点

1. **异步设计**：主线程和渲染线程分离
2. **命令缓冲**：延迟执行，批量优化
3. **多通道渲染**：阴影、主通道、后处理
4. **数据驱动**：ECS 架构，组件化设计

### 相关文档

- **[01-core-concepts.md](./01-core-concepts.md)** - Engine/Scene/View 基础
- **[07-render-loop.md](./07-render-loop.md)** - 渲染循环详解
- **[../backend/03-command-stream.md](../backend/03-command-stream.md)** - 命令流机制
- **[../backend/06-vulkan-backend.md](../backend/06-vulkan-backend.md)** - Vulkan 后端实现

通过理解这个完整的端到端管线，您可以更好地调试问题、优化性能，并充分利用 Filament 的强大功能。
