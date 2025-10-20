# Vulkan Backend 实现详解

本文档详细讲解 Filament VulkanDriver 的实现，包括 Vulkan 命令缓冲管理、描述符集系统、同步机制、以及内存管理。

---

## Vulkan Backend 概述

**文件位置**: `filament/backend/src/vulkan/VulkanDriver.h`

Vulkan 是现代低开销的图形 API，给予开发者更多控制权，但也带来更多复杂性：
- **显式命令缓冲**: 需要手动录制和提交命令
- **显式同步**: 需要手动管理 GPU-CPU 和 GPU-GPU 同步
- **显式内存管理**: 需要手动分配和绑定内存
- **描述符集**: 复杂的资源绑定机制

###设计目标

1. **高性能**: 充分利用 Vulkan 的低开销特性
2. **简化抽象**: 隐藏 Vulkan 的复杂性，提供简洁的 Driver 接口
3. **多线程**: 利用 Vulkan 的多线程命令录制能力
4. **内存高效**: 智能的内存分配和管理策略

---

## VulkanDriver 类结构

### 类定义

```cpp
class VulkanDriver final : public Driver {
public:
    explicit VulkanDriver(Platform* platform) noexcept;
    ~VulkanDriver() noexcept override;

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
    // Vulkan 核心对象
    VkInstance mInstance;
    VkPhysicalDevice mPhysicalDevice;
    VkDevice mDevice;
    VkQueue mGraphicsQueue;
    VkQueue mPresentQueue;

    // 命令管理
    VulkanCommandManager mCommandManager;

    // 描述符管理
    VulkanDescriptorSetManager mDescriptorSetManager;

    // 内存管理
    VulkanMemoryAllocator mMemoryAllocator;

    // 管线缓存
    VkPipelineCache mPipelineCache;

    // 资源分配器
    HandleAllocator<VulkanVertexBuffer> mVertexBufferAllocator;
    HandleAllocator<VulkanIndexBuffer> mIndexBufferAllocator;
    HandleAllocator<VulkanTexture> mTextureAllocator;
    HandleAllocator<VulkanProgram> mProgramAllocator;
};
```

---

## Vulkan 资源结构

### VulkanVertexBuffer

```cpp
struct VulkanVertexBuffer {
    VkBuffer buffer;              // Vulkan 缓冲对象
    VkDeviceMemory memory;        // 设备内存
    VkDeviceSize size;            // 大小

    uint8_t bufferCount;
    uint32_t vertexCount;
    AttributeArray attributes;

    VulkanVertexBuffer(uint8_t bufferCount, uint32_t vertexCount,
                       AttributeArray attributes)
        : buffer(VK_NULL_HANDLE)
        , memory(VK_NULL_HANDLE)
        , size(0)
        , bufferCount(bufferCount)
        , vertexCount(vertexCount)
        , attributes(attributes)
    {}
};

// 创建实现
Handle<HwVertexBuffer> VulkanDriver::createVertexBuffer(
    uint8_t bufferCount,
    uint8_t attributeCount,
    uint32_t vertexCount,
    AttributeArray attributes
) {
    VulkanVertexBuffer* vb = new VulkanVertexBuffer(
        bufferCount, vertexCount, attributes
    );

    // 计算缓冲区大小
    VkDeviceSize bufferSize = calculateVertexBufferSize(vertexCount, attributes);

    // 创建 Vulkan 缓冲
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(mDevice, &bufferInfo, nullptr, &vb->buffer);

    // 分配内存
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(mDevice, vb->buffer, &memRequirements);

    vb->memory = mMemoryAllocator.allocate(
        memRequirements.size,
        memRequirements.alignment,
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT  // GPU 内存
    );

    // 绑定内存
    vkBindBufferMemory(mDevice, vb->buffer, vb->memory, 0);

    vb->size = bufferSize;

    return mVertexBufferAllocator.allocate<HwVertexBuffer>(vb);
}
```

### VulkanTexture

```cpp
struct VulkanTexture {
    VkImage image;                // Vulkan 图像对象
    VkImageView imageView;        // 图像视图
    VkDeviceMemory memory;        // 设备内存
    VkSampler sampler;            // 采样器（可选）

    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint8_t levels;
    VkFormat format;
    VkImageLayout currentLayout;  // 当前布局

    VulkanTexture()
        : image(VK_NULL_HANDLE)
        , imageView(VK_NULL_HANDLE)
        , memory(VK_NULL_HANDLE)
        , sampler(VK_NULL_HANDLE)
        , currentLayout(VK_IMAGE_LAYOUT_UNDEFINED)
    {}
};

// 创建实现
Handle<HwTexture> VulkanDriver::createTexture(
    SamplerType target,
    uint8_t levels,
    TextureFormat format,
    uint8_t samples,
    uint32_t width, uint32_t height, uint32_t depth,
    TextureUsage usage
) {
    VulkanTexture* texture = new VulkanTexture();

    // 创建 Vulkan 图像
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = getVkImageType(target);
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = depth;
    imageInfo.mipLevels = levels;
    imageInfo.arrayLayers = getArrayLayers(target);  // 1 or 6 (cubemap)
    imageInfo.format = getVkFormat(format);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = getVkImageUsage(usage);
    imageInfo.samples = (VkSampleCountFlagBits)samples;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (target == SamplerType::SAMPLER_CUBEMAP) {
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    vkCreateImage(mDevice, &imageInfo, nullptr, &texture->image);

    // 分配内存
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(mDevice, texture->image, &memRequirements);

    texture->memory = mMemoryAllocator.allocate(
        memRequirements.size,
        memRequirements.alignment,
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    vkBindImageMemory(mDevice, texture->image, texture->memory, 0);

    // 创建图像视图
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture->image;
    viewInfo.viewType = getVkImageViewType(target);
    viewInfo.format = getVkFormat(format);
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = levels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = getArrayLayers(target);

    vkCreateImageView(mDevice, &viewInfo, nullptr, &texture->imageView);

    texture->width = width;
    texture->height = height;
    texture->depth = depth;
    texture->levels = levels;
    texture->format = getVkFormat(format);

    return mTextureAllocator.allocate<HwTexture>(texture);
}
```

### VulkanProgram

```cpp
struct VulkanProgram {
    VkShaderModule vertexShader;
    VkShaderModule fragmentShader;
    VkPipelineLayout pipelineLayout;
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts;

    UniformBlockInfo uniformBlocks;
    SamplerGroupInfo samplerGroups;

    VulkanProgram()
        : vertexShader(VK_NULL_HANDLE)
        , fragmentShader(VK_NULL_HANDLE)
        , pipelineLayout(VK_NULL_HANDLE)
    {}
};

// 创建实现
Handle<HwProgram> VulkanDriver::createProgram(Program&& program) {
    VulkanProgram* vkProgram = new VulkanProgram();

    // 创建顶点着色器模块（SPIRV）
    VkShaderModuleCreateInfo shaderModuleInfo = {};
    shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleInfo.codeSize = program.vertexShader.sourceLength;
    shaderModuleInfo.pCode = reinterpret_cast<const uint32_t*>(
        program.vertexShader.source
    );

    vkCreateShaderModule(mDevice, &shaderModuleInfo, nullptr,
                         &vkProgram->vertexShader);

    // 创建片段着色器模块
    shaderModuleInfo.codeSize = program.fragmentShader.sourceLength;
    shaderModuleInfo.pCode = reinterpret_cast<const uint32_t*>(
        program.fragmentShader.source
    );

    vkCreateShaderModule(mDevice, &shaderModuleInfo, nullptr,
                         &vkProgram->fragmentShader);

    // 创建描述符集布局
    vkProgram->descriptorSetLayouts = createDescriptorSetLayouts(
        program.uniformBlocks,
        program.samplerGroups
    );

    // 创建管线布局
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = vkProgram->descriptorSetLayouts.size();
    pipelineLayoutInfo.pSetLayouts = vkProgram->descriptorSetLayouts.data();

    vkCreatePipelineLayout(mDevice, &pipelineLayoutInfo, nullptr,
                          &vkProgram->pipelineLayout);

    vkProgram->uniformBlocks = std::move(program.uniformBlocks);
    vkProgram->samplerGroups = std::move(program.samplerGroups);

    return mProgramAllocator.allocate<HwProgram>(vkProgram);
}
```

---

## Vulkan 命令缓冲管理

### VulkanCommandManager

```cpp
class VulkanCommandManager {
public:
    VulkanCommandManager(VkDevice device, VkQueue queue,
                        uint32_t queueFamilyIndex);

    // 获取当前命令缓冲
    VkCommandBuffer getCurrentCommandBuffer();

    // 开始新帧
    void beginFrame();

    // 结束帧并提交
    void endFrame();

    // 立即提交命令（用于资源上传等）
    void submitImmediate(std::function<void(VkCommandBuffer)> func);

private:
    struct FrameResources {
        VkCommandPool commandPool;
        VkCommandBuffer commandBuffer;
        VkFence fence;
        bool inUse;
    };

    VkDevice mDevice;
    VkQueue mQueue;
    uint32_t mQueueFamilyIndex;

    // 多帧资源（triple buffering）
    std::array<FrameResources, 3> mFrameResources;
    uint32_t mCurrentFrame = 0;

    void createFrameResources();
    void waitForFence(VkFence fence);
};

// 实现
VkCommandBuffer VulkanCommandManager::getCurrentCommandBuffer() {
    return mFrameResources[mCurrentFrame].commandBuffer;
}

void VulkanCommandManager::beginFrame() {
    auto& frame = mFrameResources[mCurrentFrame];

    // 等待该帧的命令执行完成
    if (frame.inUse) {
        waitForFence(frame.fence);
        frame.inUse = false;
    }

    // 重置命令池
    vkResetCommandPool(mDevice, frame.commandPool, 0);

    // 开始录制命令
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(frame.commandBuffer, &beginInfo);
}

void VulkanCommandManager::endFrame() {
    auto& frame = mFrameResources[mCurrentFrame];

    // 结束命令录制
    vkEndCommandBuffer(frame.commandBuffer);

    // 提交到队列
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer;

    vkQueueSubmit(mQueue, 1, &submitInfo, frame.fence);
    frame.inUse = true;

    // 切换到下一帧
    mCurrentFrame = (mCurrentFrame + 1) % mFrameResources.size();
}

void VulkanCommandManager::submitImmediate(
    std::function<void(VkCommandBuffer)> func
) {
    // 创建临时命令缓冲
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = mQueueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    VkCommandPool tempPool;
    vkCreateCommandPool(mDevice, &poolInfo, nullptr, &tempPool);

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = tempPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(mDevice, &allocInfo, &cmdBuffer);

    // 录制命令
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    func(cmdBuffer);  // 执行用户命令
    vkEndCommandBuffer(cmdBuffer);

    // 提交并等待
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(mQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(mQueue);

    // 清理
    vkDestroyCommandPool(mDevice, tempPool, nullptr);
}
```

---

## 描述符集管理

### VulkanDescriptorSetManager

描述符集是 Vulkan 绑定资源（Uniform、Sampler）的机制：

```cpp
class VulkanDescriptorSetManager {
public:
    VulkanDescriptorSetManager(VkDevice device);

    // 分配描述符集
    VkDescriptorSet allocate(VkDescriptorSetLayout layout);

    // 更新描述符集
    void updateUniformBuffer(VkDescriptorSet descriptorSet,
                            uint32_t binding,
                            VkBuffer buffer,
                            VkDeviceSize offset,
                            VkDeviceSize range);

    void updateSampler(VkDescriptorSet descriptorSet,
                      uint32_t binding,
                      VkImageView imageView,
                      VkSampler sampler);

    // 重置所有描述符池
    void reset();

private:
    VkDevice mDevice;
    std::vector<VkDescriptorPool> mPools;
    VkDescriptorPool mCurrentPool;
    size_t mCurrentPoolIndex = 0;

    VkDescriptorPool createPool();
};

// 创建描述符池
VkDescriptorPool VulkanDescriptorSetManager::createPool() {
    // 定义池大小
    std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 }
    }};

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = poolSizes.size();
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1000;

    VkDescriptorPool pool;
    vkCreateDescriptorPool(mDevice, &poolInfo, nullptr, &pool);

    return pool;
}

// 分配描述符集
VkDescriptorSet VulkanDescriptorSetManager::allocate(
    VkDescriptorSetLayout layout
) {
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mCurrentPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet;
    VkResult result = vkAllocateDescriptorSets(mDevice, &allocInfo,
                                               &descriptorSet);

    if (result == VK_ERROR_OUT_OF_POOL_MEMORY) {
        // 当前池用完，创建新池
        mCurrentPool = createPool();
        mPools.push_back(mCurrentPool);

        allocInfo.descriptorPool = mCurrentPool;
        vkAllocateDescriptorSets(mDevice, &allocInfo, &descriptorSet);
    }

    return descriptorSet;
}

// 更新 Uniform Buffer
void VulkanDescriptorSetManager::updateUniformBuffer(
    VkDescriptorSet descriptorSet,
    uint32_t binding,
    VkBuffer buffer,
    VkDeviceSize offset,
    VkDeviceSize range
) {
    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = offset;
    bufferInfo.range = range;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(mDevice, 1, &write, 0, nullptr);
}

// 更新采样器
void VulkanDescriptorSetManager::updateSampler(
    VkDescriptorSet descriptorSet,
    uint32_t binding,
    VkImageView imageView,
    VkSampler sampler
) {
    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(mDevice, 1, &write, 0, nullptr);
}
```

---

## 图像布局转换

Vulkan 需要显式管理图像布局：

```cpp
void VulkanDriver::transitionImageLayout(
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout
) {
    VkCommandBuffer cmdBuffer = mCommandManager.getCurrentCommandBuffer();

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    // 确定访问掩码和管线阶段
    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        // 未定义 -> 传输目标
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        // 传输目标 -> Shader 只读
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    } else {
        // 其他布局转换...
    }

    vkCmdPipelineBarrier(
        cmdBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

// 更新纹理数据
void VulkanDriver::updateTexture(Handle<HwTexture> th,
                                PixelBufferDescriptor&& data) {
    VulkanTexture* texture = mTextureAllocator.handle_cast(th);

    // 1. 创建暂存缓冲
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    createStagingBuffer(data.size, &stagingBuffer, &stagingMemory);

    // 2. 拷贝数据到暂存缓冲
    void* mappedMemory;
    vkMapMemory(mDevice, stagingMemory, 0, data.size, 0, &mappedMemory);
    memcpy(mappedMemory, data.buffer, data.size);
    vkUnmapMemory(mDevice, stagingMemory);

    // 3. 转换图像布局为传输目标
    transitionImageLayout(
        texture->image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    // 4. 从暂存缓冲拷贝到图像
    VkCommandBuffer cmdBuffer = mCommandManager.getCurrentCommandBuffer();

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {texture->width, texture->height, 1};

    vkCmdCopyBufferToImage(
        cmdBuffer,
        stagingBuffer,
        texture->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    // 5. 转换图像布局为 Shader 只读
    transitionImageLayout(
        texture->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    texture->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // 6. 清理暂存缓冲（延迟到帧结束）
    mPendingDeletion.push_back({stagingBuffer, stagingMemory});
}
```

---

## 渲染管线管理

### Pipeline State Object (PSO)

Vulkan 使用 PSO 封装所有渲染状态：

```cpp
struct PipelineKey {
    VkPipelineLayout layout;
    VkRenderPass renderPass;
    RasterState rasterState;
    // ...

    bool operator==(const PipelineKey& other) const;
};

// PSO 缓存
std::unordered_map<PipelineKey, VkPipeline> mPipelineCache;

VkPipeline VulkanDriver::getOrCreatePipeline(const PipelineKey& key) {
    auto it = mPipelineCache.find(key);
    if (it != mPipelineCache.end()) {
        return it->second;
    }

    // 创建新管线
    VkPipeline pipeline = createGraphicsPipeline(key);
    mPipelineCache[key] = pipeline;
    return pipeline;
}

VkPipeline VulkanDriver::createGraphicsPipeline(const PipelineKey& key) {
    // 顶点输入状态
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    // ... 配置顶点属性

    // 输入装配
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 视口和裁剪
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // 光栅化
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.cullMode = getVkCullMode(key.rasterState.culling);
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;

    // 多重采样
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 深度/模板
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = key.rasterState.depthFunc != DepthFunc::ALWAYS;
    depthStencil.depthWriteEnable = key.rasterState.depthWrite;
    depthStencil.depthCompareOp = getVkCompareOp(key.rasterState.depthFunc);

    // 颜色混合
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = key.rasterState.colorWrite ?
        (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT) : 0;
    colorBlendAttachment.blendEnable = key.rasterState.blending != BlendMode::OPAQUE;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 动态状态（viewport, scissor）
    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = dynamicStates.size();
    dynamicState.pDynamicStates = dynamicStates.data();

    // 组装管线
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = key.layout;
    pipelineInfo.renderPass = key.renderPass;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline;
    vkCreateGraphicsPipelines(mDevice, mPipelineCache, 1, &pipelineInfo,
                             nullptr, &pipeline);

    return pipeline;
}
```

---

## 内存管理

### VulkanMemoryAllocator

Vulkan 需要手动管理 GPU 内存：

```cpp
class VulkanMemoryAllocator {
public:
    VulkanMemoryAllocator(VkDevice device, VkPhysicalDevice physicalDevice);

    // 分配内存
    VkDeviceMemory allocate(
        VkDeviceSize size,
        VkDeviceSize alignment,
        uint32_t memoryTypeBits,
        VkMemoryPropertyFlags properties
    );

    // 释放内存
    void free(VkDeviceMemory memory);

private:
    VkDevice mDevice;
    VkPhysicalDevice mPhysicalDevice;
    VkPhysicalDeviceMemoryProperties mMemoryProperties;

    uint32_t findMemoryType(uint32_t typeFilter,
                           VkMemoryPropertyFlags properties);
};

uint32_t VulkanMemoryAllocator::findMemoryType(
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties
) {
    for (uint32_t i = 0; i < mMemoryProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (mMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    LOG(FATAL) << "Failed to find suitable memory type!";
    return 0;
}

VkDeviceMemory VulkanMemoryAllocator::allocate(
    VkDeviceSize size,
    VkDeviceSize alignment,
    uint32_t memoryTypeBits,
    VkMemoryPropertyFlags properties
) {
    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = size;
    allocInfo.memoryTypeIndex = findMemoryType(memoryTypeBits, properties);

    VkDeviceMemory memory;
    VkResult result = vkAllocateMemory(mDevice, &allocInfo, nullptr, &memory);

    if (result != VK_SUCCESS) {
        LOG(ERROR) << "Failed to allocate " << size << " bytes";
        return VK_NULL_HANDLE;
    }

    return memory;
}
```

---

## 同步机制

### Fence 和 Semaphore

```cpp
// Fence: CPU-GPU 同步
VkFence fence;
VkFenceCreateInfo fenceInfo = {};
fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
vkCreateFence(mDevice, &fenceInfo, nullptr, &fence);

// 提交命令
vkQueueSubmit(mQueue, 1, &submitInfo, fence);

// 等待 GPU 完成
vkWaitForFences(mDevice, 1, &fence, VK_TRUE, UINT64_MAX);
vkResetFences(mDevice, 1, &fence);

// Semaphore: GPU-GPU 同步
VkSemaphore imageAvailableSemaphore;
VkSemaphore renderFinishedSemaphore;

VkSemaphoreCreateInfo semaphoreInfo = {};
semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
vkCreateSemaphore(mDevice, &semaphoreInfo, nullptr, &imageAvailableSemaphore);
vkCreateSemaphore(mDevice, &semaphoreInfo, nullptr, &renderFinishedSemaphore);

// 提交时指定信号量
VkSubmitInfo submitInfo = {};
submitInfo.waitSemaphoreCount = 1;
submitInfo.pWaitSemaphores = &imageAvailableSemaphore;
submitInfo.signalSemaphoreCount = 1;
submitInfo.pSignalSemaphores = &renderFinishedSemaphore;
```

---

## 相关文档

- **[05-opengl-backend.md](05-opengl-backend.md)**: OpenGL 实现对比
- **[07-metal-backend.md](07-metal-backend.md)**: Metal 实现对比
- **[09-backend-comparison.md](09-backend-comparison.md)**: 后端对比

**理论基础**:
- `../graphics/03-gpu-pipeline.md`: GPU 管线

---

## 总结

VulkanDriver 通过**显式命令缓冲**、**描述符集管理**、**管线缓存**、**智能内存分配**，成功地在 Vulkan 平台上实现了高性能的渲染抽象，充分利用了 Vulkan 的低开销特性。

**核心技术**:
- ✅ 命令缓冲管理：多帧并行，高效提交
- ✅ 描述符集：灵活的资源绑定
- ✅ PSO 缓存：避免重复创建管线
- ✅ 显式同步：精确控制 GPU 执行
- ✅ 内存管理：智能分配，减少碎片
