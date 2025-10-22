# 08 - UI Rendering (2D UI 渲染系统)

## 概述

本示例展示如何在 Filament 3D 场景之上渲染 2D UI 元素。我们将实现 ImGui 集成、2D 纹理渲染、文本渲染和 UI 布局系统,适用于游戏 HUD、调试界面、工具窗口等场景。

### 功能特性

- **ImGui 集成** - 即时模式 GUI 库集成
- **2D 纹理渲染** - Sprite/Icon 渲染系统
- **文本渲染** - 字体加载和文本绘制
- **UI 布局** - 自动布局和对齐系统
- **输入处理** - 鼠标/触摸事件处理
- **HUD 系统** - 游戏 UI 覆盖层
- **批量渲染** - UI 元素批处理优化

### 适用场景

- 游戏 HUD 界面
- 调试工具面板
- 3D 应用菜单
- 数据可视化标签
- VR/AR UI 覆盖层

### 技术要点

- 使用正交投影渲染 2D 内容
- 理解 UI 坐标系统(屏幕空间)
- 掌握文本渲染管线
- 实现 UI 深度分层
- 优化大量 UI 元素性能

---

## 完整实现 (C++)

### 1. UI 渲染器基础

**文件：`samples/ui_rendering/UIRenderer.h`**

```cpp
/*
 * UI 渲染器 - 2D UI 元素渲染
 *
 * 功能：
 * - 2D 纹理/Sprite 渲染
 * - 文本渲染
 * - 基本形状（矩形、圆形、线）
 * - 批量渲染优化
 */

#pragma once

#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/Material.h>
#include <filament/Texture.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/RenderableManager.h>

#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>
#include <math/mat4.h>

#include <vector>
#include <string>

using namespace filament;
using namespace filament::math;

// UI 顶点格式
struct UIVertex {
    float2 position;   // 屏幕空间坐标
    float2 texCoord;   // 纹理坐标
    uint32_t color;    // RGBA 颜色 (packed)
};

// UI 绘制命令
struct UIDrawCommand {
    size_t vertexOffset;
    size_t indexOffset;
    size_t indexCount;
    Texture* texture;
    int layer;  // 深度层级
};

// UI 矩形
struct UIRect {
    float2 position;
    float2 size;
    float4 color;
    Texture* texture = nullptr;
    float2 uvMin = {0, 0};
    float2 uvMax = {1, 1};
    int layer = 0;
};

class UIRenderer {
public:
    UIRenderer(Engine* engine, Scene* scene, View* view)
        : mEngine(engine)
        , mScene(scene)
        , mView(view)
    {
        initialize();
    }

    ~UIRenderer() {
        cleanup();
    }

    // 开始 UI 帧
    void beginFrame(uint32_t screenWidth, uint32_t screenHeight) {
        mScreenWidth = screenWidth;
        mScreenHeight = screenHeight;
        mVertices.clear();
        mIndices.clear();
        mDrawCommands.clear();
        mCurrentVertexCount = 0;
    }

    // 绘制矩形
    void drawRect(const UIRect& rect) {
        size_t baseVertex = mVertices.size();

        // 转换到 NDC (-1 到 1)
        float2 posNDC = screenToNDC(rect.position);
        float2 sizeNDC = float2{
            rect.size.x / mScreenWidth * 2.0f,
            rect.size.y / mScreenHeight * 2.0f
        };

        // 打包颜色
        uint32_t color = packColor(rect.color);

        // 添加顶点 (4 个顶点组成矩形)
        mVertices.push_back({
            posNDC,
            rect.uvMin,
            color
        });
        mVertices.push_back({
            {posNDC.x + sizeNDC.x, posNDC.y},
            {rect.uvMax.x, rect.uvMin.y},
            color
        });
        mVertices.push_back({
            {posNDC.x + sizeNDC.x, posNDC.y + sizeNDC.y},
            rect.uvMax,
            color
        });
        mVertices.push_back({
            {posNDC.x, posNDC.y + sizeNDC.y},
            {rect.uvMin.x, rect.uvMax.y},
            color
        });

        // 添加索引 (2 个三角形)
        size_t baseIndex = mIndices.size();
        mIndices.push_back(baseVertex + 0);
        mIndices.push_back(baseVertex + 1);
        mIndices.push_back(baseVertex + 2);
        mIndices.push_back(baseVertex + 0);
        mIndices.push_back(baseVertex + 2);
        mIndices.push_back(baseVertex + 3);

        // 记录绘制命令
        mDrawCommands.push_back({
            baseVertex,
            baseIndex,
            6,
            rect.texture,
            rect.layer
        });

        mCurrentVertexCount += 4;
    }

    // 绘制文本（简化版 - 实际需要字体系统）
    void drawText(const std::string& text, float2 position,
                 float fontSize, float4 color) {
        // 简化实现：每个字符作为一个矩形
        float charWidth = fontSize * 0.6f;  // 假设字符宽度
        float2 currentPos = position;

        for (char c : text) {
            if (c == ' ') {
                currentPos.x += charWidth;
                continue;
            }

            // 这里应该从字体纹理集中获取字符的 UV
            // 简化版本：使用占位符
            UIRect charRect;
            charRect.position = currentPos;
            charRect.size = {charWidth, fontSize};
            charRect.color = color;
            charRect.texture = mFontTexture;  // 字体纹理集
            // charRect.uvMin/uvMax 应该根据字符在纹理集中的位置设置

            drawRect(charRect);
            currentPos.x += charWidth;
        }
    }

    // 结束 UI 帧并提交渲染
    void endFrame() {
        if (mVertices.empty()) return;

        // 按层级排序绘制命令
        std::sort(mDrawCommands.begin(), mDrawCommands.end(),
            [](const UIDrawCommand& a, const UIDrawCommand& b) {
                return a.layer < b.layer;
            });

        // 更新缓冲区
        updateBuffers();

        // 执行渲染
        render();
    }

    // 设置字体纹理
    void setFontTexture(Texture* texture) {
        mFontTexture = texture;
    }

private:
    void initialize() {
        // 创建 UI 材质（无光照、支持透明度）
        createUIMaterial();

        // 创建默认白色纹理
        createDefaultTexture();

        // 创建相机（正交投影）
        setupUICamera();
    }

    void createUIMaterial() {
        // 简化版本：使用 unlit 材质
        // 实际应用中应该使用自定义着色器

        static const char* UI_MATERIAL_SHADER = R"(
            material {
                name : UIMaterial,
                shadingModel : unlit,
                vertexDomain : device,
                blending : transparent,
                depthWrite : false,
                doubleSided : true,

                parameters : [
                    { type : sampler2d, name : baseColor }
                ],

                requires : [
                    uv0,
                    color
                ]
            }

            fragment {
                void material(inout MaterialInputs material) {
                    vec4 texColor = texture(materialParams_baseColor, getUV0());
                    material.baseColor = texColor * getColor();
                }
            }
        )";

        // 注意：实际使用中需要用 matc 编译这个材质
        // 这里假设已经有编译好的材质文件
    }

    void createDefaultTexture() {
        // 创建 1x1 白色纹理
        uint32_t white = 0xFFFFFFFF;
        mDefaultTexture = Texture::Builder()
            .width(1)
            .height(1)
            .levels(1)
            .format(Texture::InternalFormat::RGBA8)
            .build(*mEngine);

        Texture::PixelBufferDescriptor buffer(
            &white, sizeof(white),
            Texture::Format::RGBA, Texture::Type::UBYTE);

        mDefaultTexture->setImage(*mEngine, 0, std::move(buffer));
    }

    void setupUICamera() {
        // UI 使用独立的相机和视图
        mUICamera = mEngine->createCamera(EntityManager::get().create());

        // 正交投影 (-1, 1) 到 (1, -1)
        // Y 轴向下,符合 UI 习惯
        mUICamera->setProjection(Camera::Projection::ORTHO,
            -1.0, 1.0,   // left, right
            1.0, -1.0,   // bottom, top
            0.0, 1.0);   // near, far
    }

    void updateBuffers() {
        // 创建或更新顶点缓冲区
        if (!mVertexBuffer || mVertices.size() * sizeof(UIVertex) > mVertexBufferSize) {
            if (mVertexBuffer) {
                mEngine->destroy(mVertexBuffer);
            }

            mVertexBufferSize = mVertices.size() * sizeof(UIVertex) * 2;  // 预留空间

            mVertexBuffer = VertexBuffer::Builder()
                .vertexCount(mVertices.size())
                .bufferCount(1)
                .attribute(VertexAttribute::POSITION, 0,
                          VertexBuffer::AttributeType::FLOAT2, 0, sizeof(UIVertex))
                .attribute(VertexAttribute::UV0, 0,
                          VertexBuffer::AttributeType::FLOAT2,
                          offsetof(UIVertex, texCoord), sizeof(UIVertex))
                .attribute(VertexAttribute::COLOR, 0,
                          VertexBuffer::AttributeType::UBYTE4,
                          offsetof(UIVertex, color), sizeof(UIVertex))
                .normalized(VertexAttribute::COLOR)
                .build(*mEngine);
        }

        // 上传顶点数据
        VertexBuffer::BufferDescriptor vertexData(
            mVertices.data(),
            mVertices.size() * sizeof(UIVertex));

        mVertexBuffer->setBufferAt(*mEngine, 0, std::move(vertexData));

        // 创建或更新索引缓冲区
        if (!mIndexBuffer || mIndices.size() * sizeof(uint32_t) > mIndexBufferSize) {
            if (mIndexBuffer) {
                mEngine->destroy(mIndexBuffer);
            }

            mIndexBufferSize = mIndices.size() * sizeof(uint32_t) * 2;

            mIndexBuffer = IndexBuffer::Builder()
                .indexCount(mIndices.size())
                .bufferType(IndexBuffer::IndexType::UINT)
                .build(*mEngine);
        }

        // 上传索引数据
        IndexBuffer::BufferDescriptor indexData(
            mIndices.data(),
            mIndices.size() * sizeof(uint32_t));

        mIndexBuffer->setBufferAt(*mEngine, 0, std::move(indexData));
    }

    void render() {
        // 在 Filament 中渲染 UI
        // 实际实现需要创建 Renderable 实体
        // 这里展示基本思路

        if (!mUIEntity) {
            mUIEntity = EntityManager::get().create();
        }

        // 构建 Renderable
        RenderableManager::Builder builder(1);
        builder.boundingBox({{0, 0, 0}, {1, 1, 1}})
               .layerMask(0xFF, 0xFF)
               .priority(7)  // 高优先级,最后渲染
               .culling(false)
               .receiveShadows(false)
               .castShadows(false)
               .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,
                        mVertexBuffer, mIndexBuffer, 0, mIndices.size());

        // 设置材质（简化 - 实际需要为每个纹理设置）
        // builder.material(0, mUIMaterial);

        builder.build(*mEngine, mUIEntity);

        // 添加到场景
        mScene->addEntity(mUIEntity);
    }

    // 辅助函数：屏幕坐标转 NDC
    float2 screenToNDC(float2 screenPos) const {
        return float2{
            (screenPos.x / mScreenWidth) * 2.0f - 1.0f,
            -((screenPos.y / mScreenHeight) * 2.0f - 1.0f)  // Y 翻转
        };
    }

    // 辅助函数：打包颜色
    uint32_t packColor(float4 color) const {
        return ((uint32_t)(color.r * 255) << 0) |
               ((uint32_t)(color.g * 255) << 8) |
               ((uint32_t)(color.b * 255) << 16) |
               ((uint32_t)(color.a * 255) << 24);
    }

    void cleanup() {
        if (mVertexBuffer) mEngine->destroy(mVertexBuffer);
        if (mIndexBuffer) mEngine->destroy(mIndexBuffer);
        if (mDefaultTexture) mEngine->destroy(mDefaultTexture);
        if (mUICamera) mEngine->destroy(mUICamera);
        if (mUIEntity) {
            mScene->removeEntity(mUIEntity);
            EntityManager::get().destroy(mUIEntity);
        }
    }

private:
    Engine* mEngine;
    Scene* mScene;
    View* mView;

    // 缓冲区
    VertexBuffer* mVertexBuffer = nullptr;
    IndexBuffer* mIndexBuffer = nullptr;
    size_t mVertexBufferSize = 0;
    size_t mIndexBufferSize = 0;

    // 材质和纹理
    Material* mUIMaterial = nullptr;
    Texture* mDefaultTexture = nullptr;
    Texture* mFontTexture = nullptr;

    // 相机和实体
    Camera* mUICamera = nullptr;
    Entity mUIEntity;

    // 帧数据
    std::vector<UIVertex> mVertices;
    std::vector<uint32_t> mIndices;
    std::vector<UIDrawCommand> mDrawCommands;
    size_t mCurrentVertexCount = 0;

    uint32_t mScreenWidth = 0;
    uint32_t mScreenHeight = 0;
};
```

### 2. ImGui 集成

**文件：`samples/ui_rendering/ImGuiIntegration.h`**

```cpp
/*
 * ImGui 集成 - 即时模式 GUI
 *
 * 将 ImGui 集成到 Filament 渲染管线
 */

#pragma once

#include <imgui.h>
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/View.h>

class ImGuiRenderer {
public:
    ImGuiRenderer(Engine* engine, Scene* scene, View* view)
        : mEngine(engine), mScene(scene), mView(view)
    {
        // 初始化 ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();

        // 设置样式
        ImGui::StyleColorsDark();

        // 创建字体纹理
        createFontTexture();

        // 创建 ImGui 材质和缓冲区
        createImGuiResources();
    }

    ~ImGuiRenderer() {
        ImGui::DestroyContext();
        cleanup();
    }

    // 新建帧
    void newFrame(float deltaTime, uint32_t width, uint32_t height) {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(width, height);
        io.DeltaTime = deltaTime;

        ImGui::NewFrame();
    }

    // 渲染 ImGui
    void render() {
        ImGui::Render();
        renderDrawData(ImGui::GetDrawData());
    }

    // 处理输入事件
    void handleMouseButton(int button, bool pressed, float x, float y) {
        ImGuiIO& io = ImGui::GetIO();
        io.MousePos = ImVec2(x, y);
        if (button >= 0 && button < 5) {
            io.MouseDown[button] = pressed;
        }
    }

    void handleMouseWheel(float delta) {
        ImGuiIO& io = ImGui::GetIO();
        io.MouseWheel += delta;
    }

    void handleKeyEvent(int key, bool pressed) {
        ImGuiIO& io = ImGui::GetIO();
        // 映射键盘事件到 ImGui
        // 简化实现
    }

private:
    void createFontTexture() {
        ImGuiIO& io = ImGui::GetIO();

        // 获取字体纹理数据
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        // 创建 Filament 纹理
        mFontTexture = Texture::Builder()
            .width(width)
            .height(height)
            .levels(1)
            .format(Texture::InternalFormat::RGBA8)
            .build(*mEngine);

        Texture::PixelBufferDescriptor buffer(
            pixels, width * height * 4,
            Texture::Format::RGBA,
            Texture::Type::UBYTE);

        mFontTexture->setImage(*mEngine, 0, std::move(buffer));

        // 告诉 ImGui 纹理 ID
        io.Fonts->SetTexID((ImTextureID)(uintptr_t)mFontTexture);
    }

    void createImGuiResources() {
        // 创建动态顶点/索引缓冲区
        // 实现类似 UIRenderer
    }

    void renderDrawData(ImDrawData* drawData) {
        if (!drawData || drawData->TotalVtxCount == 0) return;

        // 转换 ImGui 绘制命令到 Filament
        for (int n = 0; n < drawData->CmdListsCount; n++) {
            const ImDrawList* cmdList = drawData->CmdLists[n];
            const ImDrawVert* vtxBuffer = cmdList->VtxBuffer.Data;
            const ImDrawIdx* idxBuffer = cmdList->IdxBuffer.Data;

            for (int cmdIdx = 0; cmdIdx < cmdList->CmdBuffer.Size; cmdIdx++) {
                const ImDrawCmd* cmd = &cmdList->CmdBuffer[cmdIdx];

                // 设置裁剪矩形
                // 绑定纹理
                // 提交绘制调用

                // 实际实现需要更新缓冲区并调用 Filament API
            }
        }
    }

    void cleanup() {
        if (mFontTexture) {
            mEngine->destroy(mFontTexture);
        }
    }

private:
    Engine* mEngine;
    Scene* mScene;
    View* mView;
    Texture* mFontTexture = nullptr;
};
```

### 3. HUD 系统

**文件：`samples/ui_rendering/HUDSystem.h`**

```cpp
/*
 * HUD 系统 - 游戏抬头显示
 *
 * 管理游戏 UI 元素：血量、地图、提示等
 */

#pragma once

#include "UIRenderer.h"
#include <map>
#include <functional>

class HUDSystem {
public:
    HUDSystem(UIRenderer* uiRenderer)
        : mUIRenderer(uiRenderer) {}

    // UI 元素类型
    enum class ElementType {
        HEALTH_BAR,
        MINIMAP,
        AMMO_COUNTER,
        SCORE,
        NOTIFICATION,
        CROSSHAIR
    };

    // UI 元素
    struct UIElement {
        std::string id;
        ElementType type;
        float2 position;
        float2 size;
        bool visible;
        std::function<void(UIRenderer*)> renderCallback;
    };

    // 添加 UI 元素
    void addElement(const std::string& id, ElementType type,
                   float2 position, float2 size) {
        UIElement element;
        element.id = id;
        element.type = type;
        element.position = position;
        element.size = size;
        element.visible = true;

        // 根据类型设置渲染回调
        switch (type) {
            case ElementType::HEALTH_BAR:
                element.renderCallback = [this, position, size](UIRenderer* r) {
                    renderHealthBar(r, position, size);
                };
                break;
            case ElementType::CROSSHAIR:
                element.renderCallback = [this, position, size](UIRenderer* r) {
                    renderCrosshair(r, position, size);
                };
                break;
            // 其他类型...
        }

        mElements[id] = element;
    }

    // 更新和渲染
    void update(float deltaTime) {
        // 更新动画、过渡等
    }

    void render(uint32_t screenWidth, uint32_t screenHeight) {
        mUIRenderer->beginFrame(screenWidth, screenHeight);

        for (auto& [id, element] : mElements) {
            if (element.visible && element.renderCallback) {
                element.renderCallback(mUIRenderer);
            }
        }

        mUIRenderer->endFrame();
    }

    // 设置元素可见性
    void setVisible(const std::string& id, bool visible) {
        auto it = mElements.find(id);
        if (it != mElements.end()) {
            it->second.visible = visible;
        }
    }

    // 更新数据
    void setHealth(float health) { mHealth = health; }
    void setMaxHealth(float maxHealth) { mMaxHealth = maxHealth; }
    void setAmmo(int ammo) { mAmmo = ammo; }
    void setScore(int score) { mScore = score; }

private:
    // 渲染各种 UI 元素
    void renderHealthBar(UIRenderer* r, float2 pos, float2 size) {
        // 背景
        UIRect bg;
        bg.position = pos;
        bg.size = size;
        bg.color = {0.2f, 0.2f, 0.2f, 0.8f};
        bg.layer = 0;
        r->drawRect(bg);

        // 血量条
        float healthPercent = mHealth / mMaxHealth;
        UIRect bar;
        bar.position = pos + float2{2, 2};
        bar.size = {(size.x - 4) * healthPercent, size.y - 4};
        bar.color = {1.0f - healthPercent, healthPercent, 0.0f, 1.0f};
        bar.layer = 1;
        r->drawRect(bar);

        // 文本
        std::string text = std::to_string((int)mHealth) + " / " +
                          std::to_string((int)mMaxHealth);
        r->drawText(text, pos + float2{5, 5}, 16.0f, {1, 1, 1, 1});
    }

    void renderCrosshair(UIRenderer* r, float2 center, float2 size) {
        // 十字准星
        float thickness = 2.0f;
        float length = size.x;

        // 水平线
        UIRect hLine;
        hLine.position = {center.x - length/2, center.y - thickness/2};
        hLine.size = {length, thickness};
        hLine.color = {1, 1, 1, 0.8f};
        hLine.layer = 10;
        r->drawRect(hLine);

        // 垂直线
        UIRect vLine;
        vLine.position = {center.x - thickness/2, center.y - length/2};
        vLine.size = {thickness, length};
        vLine.color = {1, 1, 1, 0.8f};
        vLine.layer = 10;
        r->drawRect(vLine);
    }

    void renderMinimap(UIRenderer* r, float2 pos, float2 size) {
        // 小地图背景
        UIRect bg;
        bg.position = pos;
        bg.size = size;
        bg.color = {0.1f, 0.1f, 0.1f, 0.7f};
        bg.layer = 0;
        r->drawRect(bg);

        // 玩家位置（中心点）
        UIRect player;
        player.position = pos + size * 0.5f - float2{4, 4};
        player.size = {8, 8};
        player.color = {0, 1, 0, 1};
        player.layer = 1;
        r->drawRect(player);
    }

private:
    UIRenderer* mUIRenderer;
    std::map<std::string, UIElement> mElements;

    // 游戏数据
    float mHealth = 100.0f;
    float mMaxHealth = 100.0f;
    int mAmmo = 30;
    int mScore = 0;
};
```

### 4. 主应用示例

**文件：`samples/ui_rendering_demo.cpp`**

```cpp
/*
 * UI 渲染示例应用
 */

#include "UIRenderer.h"
#include "ImGuiIntegration.h"
#include "HUDSystem.h"

#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/Renderer.h>
#include <filament/SwapChain.h>

#include <filamentapp/FilamentApp.h>
#include <filamentapp/Config.h>

#include <iostream>

using namespace filament;
using namespace filament::math;

class UIRenderingApp {
public:
    bool initialize(Engine* engine, View* view, Scene* scene) {
        mEngine = engine;
        mView = view;
        mScene = scene;

        // 创建 UI 渲染器
        mUIRenderer = std::make_unique<UIRenderer>(engine, scene, view);

        // 创建 ImGui 渲染器
        mImGuiRenderer = std::make_unique<ImGuiRenderer>(engine, scene, view);

        // 创建 HUD 系统
        mHUDSystem = std::make_unique<HUDSystem>(mUIRenderer.get());
        setupHUD();

        // 设置 3D 场景相机
        setup3DScene();

        std::cout << "UI Rendering App initialized" << std::endl;
        return true;
    }

    void update(float deltaTime, uint32_t width, uint32_t height) {
        mScreenWidth = width;
        mScreenHeight = height;

        // 更新游戏逻辑
        updateGameLogic(deltaTime);

        // 渲染 HUD
        mHUDSystem->render(width, height);

        // 渲染 ImGui 调试界面
        renderImGuiDebug(deltaTime, width, height);
    }

    void handleInput(/* input events */) {
        // 处理输入并传递给 ImGui
        // mImGuiRenderer->handleMouseButton(...);
    }

    void cleanup() {
        mHUDSystem.reset();
        mImGuiRenderer.reset();
        mUIRenderer.reset();
    }

private:
    void setupHUD() {
        // 添加血量条
        mHUDSystem->addElement("health", HUDSystem::ElementType::HEALTH_BAR,
                              {20, 20}, {200, 30});

        // 添加准星
        // 位置会在 render 时根据屏幕中心计算
        mHUDSystem->addElement("crosshair", HUDSystem::ElementType::CROSSHAIR,
                              {0, 0}, {20, 20});

        // 添加小地图
        mHUDSystem->addElement("minimap", HUDSystem::ElementType::MINIMAP,
                              {0, 0}, {150, 150});  // 右上角,在 render 时计算
    }

    void setup3DScene() {
        Camera& camera = mView->getCamera();
        camera.setProjection(45.0, 16.0f/9.0f, 0.1, 100.0);

        float3 eye(0, 2, 5);
        float3 center(0, 0, 0);
        float3 up(0, 1, 0);

        camera.lookAt(eye, center, up);

        // 添加一些 3D 对象用于背景
        // ...
    }

    void updateGameLogic(float deltaTime) {
        // 模拟游戏逻辑
        static float time = 0.0f;
        time += deltaTime;

        // 动态更新血量（示例）
        float health = 50.0f + 50.0f * std::sin(time);
        mHUDSystem->setHealth(health);
        mHUDSystem->setMaxHealth(100.0f);
    }

    void renderImGuiDebug(float deltaTime, uint32_t width, uint32_t height) {
        mImGuiRenderer->newFrame(deltaTime, width, height);

        // 调试窗口
        ImGui::Begin("Debug Info");

        ImGui::Text("FPS: %.1f", 1.0f / deltaTime);
        ImGui::Text("Screen: %dx%d", width, height);

        ImGui::Separator();

        ImGui::Text("Game Stats:");
        static float health = 100.0f;
        if (ImGui::SliderFloat("Health", &health, 0.0f, 100.0f)) {
            mHUDSystem->setHealth(health);
        }

        static int ammo = 30;
        if (ImGui::SliderInt("Ammo", &ammo, 0, 100)) {
            mHUDSystem->setAmmo(ammo);
        }

        ImGui::Separator();

        ImGui::Text("UI Elements:");
        bool healthVisible = true;
        if (ImGui::Checkbox("Show Health Bar", &healthVisible)) {
            mHUDSystem->setVisible("health", healthVisible);
        }

        bool crosshairVisible = true;
        if (ImGui::Checkbox("Show Crosshair", &crosshairVisible)) {
            mHUDSystem->setVisible("crosshair", crosshairVisible);
        }

        ImGui::End();

        mImGuiRenderer->render();
    }

private:
    Engine* mEngine = nullptr;
    View* mView = nullptr;
    Scene* mScene = nullptr;

    std::unique_ptr<UIRenderer> mUIRenderer;
    std::unique_ptr<ImGuiRenderer> mImGuiRenderer;
    std::unique_ptr<HUDSystem> mHUDSystem;

    uint32_t mScreenWidth = 0;
    uint32_t mScreenHeight = 0;
};

// 全局应用实例
static UIRenderingApp* g_app = nullptr;

void setup(Engine* engine, View* view, Scene* scene) {
    g_app = new UIRenderingApp();
    g_app->initialize(engine, view, scene);
}

void animate(Engine* engine, View* view, double now) {
    static double lastTime = 0.0;
    double deltaTime = now - lastTime;
    lastTime = now;

    if (g_app) {
        // 获取屏幕尺寸（简化 - 实际从窗口获取）
        g_app->update(deltaTime, 1920, 1080);
    }
}

void cleanup(Engine* engine, View* view, Scene* scene) {
    if (g_app) {
        g_app->cleanup();
        delete g_app;
        g_app = nullptr;
    }
}

int main(int argc, char** argv) {
    Config config;
    config.title = "Filament UI Rendering Demo";
    config.backend = Engine::Backend::DEFAULT;

    FilamentApp::get().run(config, setup, cleanup, nullptr, animate);

    return 0;
}
```

---

## Android 实现 (Kotlin)

**文件：`android/app/src/main/java/com/example/UIRenderingActivity.kt`**

```kotlin
package com.example.filament.samples

import android.app.Activity
import android.os.Bundle
import android.view.Choreographer
import android.view.SurfaceView
import android.view.MotionEvent
import android.widget.TextView
import com.google.android.filament.*
import com.google.android.filament.utils.*
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Color as AndroidColor
import android.view.View

/**
 * UI 渲染示例 - Android 实现
 *
 * 展示如何在 Filament 3D 场景上渲染 2D UI
 */
class UIRenderingActivity : Activity() {

    private lateinit var surfaceView: SurfaceView
    private lateinit var overlayView: UIOverlayView
    private lateinit var choreographer: Choreographer
    private lateinit var modelViewer: ModelViewer

    // 游戏数据
    private var health = 100f
    private var maxHealth = 100f
    private var score = 0

    companion object {
        init {
            Utils.init()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_ui_rendering)

        surfaceView = findViewById(R.id.surfaceView)
        overlayView = findViewById(R.id.overlayView)

        choreographer = Choreographer.getInstance()
        modelViewer = ModelViewer(surfaceView)

        setupCamera()
        setupLighting()

        // 设置 UI 回调
        overlayView.setDataProvider {
            UIData(health, maxHealth, score)
        }

        choreographer.postFrameCallback(frameCallback)
    }

    private fun setupCamera() {
        modelViewer.view.camera.apply {
            setProjection(45.0,
                         surfaceView.width.toDouble() / surfaceView.height,
                         0.1, 100.0, Camera.Fov.VERTICAL)
            lookAt(
                0.0, 2.0, 5.0,
                0.0, 0.0, 0.0,
                0.0, 1.0, 0.0
            )
        }
    }

    private fun setupLighting() {
        val sun = EntityManager.get().create()
        LightManager.Builder(LightManager.Type.SUN)
            .color(1.0f, 1.0f, 1.0f)
            .intensity(100000.0f)
            .direction(0.6f, -1.0f, -0.8f)
            .build(modelViewer.engine, sun)

        modelViewer.scene.addEntity(sun)
    }

    private val frameCallback = object : Choreographer.FrameCallback {
        private var lastFrameTime = 0L

        override fun doFrame(frameTimeNanos: Long) {
            val deltaTime = if (lastFrameTime != 0L) {
                (frameTimeNanos - lastFrameTime) / 1_000_000_000.0f
            } else {
                0f
            }
            lastFrameTime = frameTimeNanos

            // 更新游戏逻辑
            updateGame(deltaTime)

            // 渲染 3D 场景
            modelViewer.render(frameTimeNanos)

            // 刷新 UI 覆盖层
            overlayView.invalidate()

            choreographer.postFrameCallback(this)
        }
    }

    private fun updateGame(deltaTime: Float) {
        // 模拟血量变化
        val time = System.currentTimeMillis() / 1000.0
        health = (50 + 50 * Math.sin(time)).toFloat()
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        // 处理触摸事件
        when (event.action) {
            MotionEvent.ACTION_DOWN -> {
                score += 10
            }
        }
        return true
    }

    override fun onResume() {
        super.onResume()
        choreographer.postFrameCallback(frameCallback)
    }

    override fun onPause() {
        super.onPause()
        choreographer.removeFrameCallback(frameCallback)
    }

    override fun onDestroy() {
        super.onDestroy()
        choreographer.removeFrameCallback(frameCallback)
        modelViewer.destroy()
    }
}

/**
 * UI 数据类
 */
data class UIData(
    val health: Float,
    val maxHealth: Float,
    val score: Int
)

/**
 * 自定义 UI 覆盖层 View
 */
class UIOverlayView(context: android.content.Context, attrs: android.util.AttributeSet) :
    View(context, attrs) {

    private var dataProvider: (() -> UIData)? = null

    private val paint = Paint().apply {
        isAntiAlias = true
    }

    fun setDataProvider(provider: () -> UIData) {
        dataProvider = provider
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)

        val data = dataProvider?.invoke() ?: return

        // 绘制血量条
        drawHealthBar(canvas, data.health, data.maxHealth)

        // 绘制分数
        drawScore(canvas, data.score)

        // 绘制准星
        drawCrosshair(canvas)
    }

    private fun drawHealthBar(canvas: Canvas, health: Float, maxHealth: Float) {
        val x = 20f
        val y = 20f
        val width = 200f
        val height = 30f

        // 背景
        paint.color = AndroidColor.argb(200, 50, 50, 50)
        paint.style = Paint.Style.FILL
        canvas.drawRect(x, y, x + width, y + height, paint)

        // 血量条
        val healthPercent = health / maxHealth
        val healthColor = AndroidColor.rgb(
            ((1 - healthPercent) * 255).toInt(),
            (healthPercent * 255).toInt(),
            0
        )

        paint.color = healthColor
        canvas.drawRect(x + 2, y + 2,
                       x + 2 + (width - 4) * healthPercent,
                       y + height - 2, paint)

        // 文本
        paint.color = AndroidColor.WHITE
        paint.textSize = 16f
        paint.style = Paint.Style.FILL
        val text = "${health.toInt()} / ${maxHealth.toInt()}"
        canvas.drawText(text, x + 5, y + 20, paint)
    }

    private fun drawScore(canvas: Canvas, score: Int) {
        paint.color = AndroidColor.WHITE
        paint.textSize = 24f
        paint.style = Paint.Style.FILL
        paint.textAlign = Paint.Align.RIGHT

        val text = "Score: $score"
        canvas.drawText(text, width - 20f, 50f, paint)
    }

    private fun drawCrosshair(canvas: Canvas) {
        val centerX = width / 2f
        val centerY = height / 2f
        val size = 20f
        val thickness = 2f

        paint.color = AndroidColor.argb(200, 255, 255, 255)
        paint.strokeWidth = thickness
        paint.style = Paint.Style.STROKE

        // 水平线
        canvas.drawLine(centerX - size, centerY,
                       centerX + size, centerY, paint)

        // 垂直线
        canvas.drawLine(centerX, centerY - size,
                       centerX, centerY + size, paint)
    }
}
```

**布局文件：`res/layout/activity_ui_rendering.xml`**

```xml
<?xml version="1.0" encoding="utf-8"?>
<FrameLayout xmlns:android="http://schemas.android.com/apk/res/android"
    android:layout_width="match_parent"
    android:layout_height="match_parent">

    <!-- 3D 渲染表面 -->
    <SurfaceView
        android:id="@+id/surfaceView"
        android:layout_width="match_parent"
        android:layout_height="match_parent" />

    <!-- UI 覆盖层 -->
    <com.example.filament.samples.UIOverlayView
        android:id="@+id/overlayView"
        android:layout_width="match_parent"
        android:layout_height="match_parent" />
</FrameLayout>
```

---

## 性能优化

### 1. UI 批量渲染

```cpp
/*
 * 批量处理 UI 绘制调用
 * 合并使用相同纹理的元素
 */

class UIBatcher {
public:
    struct Batch {
        Texture* texture;
        std::vector<UIVertex> vertices;
        std::vector<uint32_t> indices;
    };

    void addRect(const UIRect& rect) {
        // 查找或创建批次
        Batch* batch = findOrCreateBatch(rect.texture);

        // 添加顶点和索引
        size_t baseVertex = batch->vertices.size();

        // ... 添加顶点数据 ...

        batch->indices.push_back(baseVertex + 0);
        batch->indices.push_back(baseVertex + 1);
        batch->indices.push_back(baseVertex + 2);
        // ...
    }

    void flush(Engine* engine) {
        // 为每个批次创建一个绘制调用
        for (auto& batch : mBatches) {
            // 上传数据并渲染
            renderBatch(batch, engine);
        }
        clear();
    }

private:
    Batch* findOrCreateBatch(Texture* texture) {
        for (auto& batch : mBatches) {
            if (batch.texture == texture) {
                return &batch;
            }
        }
        mBatches.push_back({texture, {}, {}});
        return &mBatches.back();
    }

    void clear() {
        mBatches.clear();
    }

    std::vector<Batch> mBatches;
};
```

### 2. 文本渲染缓存

```cpp
/*
 * 缓存静态文本的渲染结果
 */

class TextCache {
public:
    struct CachedText {
        std::string text;
        std::vector<UIVertex> vertices;
        std::vector<uint32_t> indices;
    };

    const CachedText* getText(const std::string& text,
                             float fontSize,
                             float4 color) {
        std::string key = text + "_" + std::to_string(fontSize);

        auto it = mCache.find(key);
        if (it != mCache.end()) {
            return &it->second;
        }

        // 生成文本几何体
        CachedText cached;
        cached.text = text;
        generateTextGeometry(text, fontSize, color,
                           cached.vertices, cached.indices);

        mCache[key] = cached;
        return &mCache[key];
    }

private:
    std::map<std::string, CachedText> mCache;
};
```

### 3. 脏区域更新

```cpp
/*
 * 只更新变化的 UI 区域
 */

class DirtyRegionTracker {
public:
    struct Region {
        float2 min;
        float2 max;
    };

    void markDirty(const Region& region) {
        mDirtyRegions.push_back(region);
    }

    bool isDirty(const Region& region) const {
        for (const auto& dirty : mDirtyRegions) {
            if (intersects(region, dirty)) {
                return true;
            }
        }
        return false;
    }

    void clear() {
        mDirtyRegions.clear();
    }

private:
    bool intersects(const Region& a, const Region& b) const {
        return !(a.max.x < b.min.x || a.min.x > b.max.x ||
                a.max.y < b.min.y || a.min.y > b.max.y);
    }

    std::vector<Region> mDirtyRegions;
};
```

---

## 常见问题

### Q1: UI 文本模糊怎么办?

**A:** 使用高分辨率字体纹理和SDF字体:

```cpp
// 使用 Signed Distance Field (SDF) 字体
// 1. 生成 SDF 字体纹理（使用 msdf-gen 等工具）

// 2. 使用特殊着色器渲染
fragment {
    void material(inout MaterialInputs material) {
        float dist = texture(materialParams_fontAtlas, getUV0()).r;
        float alpha = smoothstep(0.5 - 0.1, 0.5 + 0.1, dist);
        material.baseColor = vec4(getColor().rgb, alpha);
    }
}
```

### Q2: UI 深度排序错误?

**A:** 使用分层系统:

```cpp
// 为不同的 UI 层设置不同的深度
void UIRenderer::setLayerDepth(int layer, float depth) {
    // layer 0: 背景 (depth = 0.9)
    // layer 1: 主UI (depth = 0.5)
    // layer 2: 弹窗 (depth = 0.1)
    // layer 3: 提示 (depth = 0.0)

    mLayerDepths[layer] = depth;
}

// 在顶点着色器中应用深度
vertex {
    void materialVertex(inout MaterialVertexInputs material) {
        float depth = materialParams.layerDepths[material.layer];
        gl_Position.z = depth;
    }
}
```

### Q3: 如何实现 UI 动画?

**A:** 使用插值和缓动函数:

```cpp
class UIAnimation {
public:
    enum class EaseType {
        LINEAR,
        EASE_IN,
        EASE_OUT,
        EASE_IN_OUT
    };

    float interpolate(float from, float to, float t, EaseType ease) {
        float easedT = applyEase(t, ease);
        return from + (to - from) * easedT;
    }

private:
    float applyEase(float t, EaseType ease) {
        switch (ease) {
            case EaseType::LINEAR:
                return t;
            case EaseType::EASE_IN:
                return t * t;
            case EaseType::EASE_OUT:
                return t * (2.0f - t);
            case EaseType::EASE_IN_OUT:
                return t < 0.5f ? 2.0f * t * t :
                       -1.0f + (4.0f - 2.0f * t) * t;
        }
        return t;
    }
};

// 使用示例：淡入效果
class FadeInAnimation {
    float mTime = 0.0f;
    float mDuration = 1.0f;

    void update(float deltaTime, UIRect& rect) {
        mTime += deltaTime;
        float t = std::min(mTime / mDuration, 1.0f);

        rect.color.a = mAnimator.interpolate(0.0f, 1.0f, t,
                                            UIAnimation::EaseType::EASE_IN);
    }
};
```

### Q4: 多语言文本如何处理?

**A:** 使用 Unicode 字体和本地化系统:

```cpp
class LocalizationSystem {
public:
    void loadLanguage(const std::string& langCode) {
        // 加载语言文件 (JSON/XML)
        mStrings = loadStringsFromFile(langCode + ".json");

        // 加载对应的字体
        mFontTexture = loadFontForLanguage(langCode);
    }

    std::u32string getString(const std::string& key) {
        auto it = mStrings.find(key);
        if (it != mStrings.end()) {
            return utf8ToUtf32(it->second);
        }
        return U"";
    }

private:
    std::map<std::string, std::string> mStrings;
    Texture* mFontTexture;
};

// 渲染 Unicode 文本
void renderUnicodeText(const std::u32string& text, ...) {
    for (char32_t codepoint : text) {
        // 从字体纹理集查找字符
        auto glyphInfo = mFontAtlas->getGlyph(codepoint);
        // 渲染字形...
    }
}
```

### Q5: 触摸事件如何传递给 UI?

**A:** 实现 UI 事件系统:

```cpp
class UIEventSystem {
public:
    struct TouchEvent {
        float2 position;
        bool pressed;
    };

    void handleTouch(const TouchEvent& event) {
        // 遍历 UI 元素,从上到下（高层到低层）
        for (auto it = mElements.rbegin(); it != mElements.rend(); ++it) {
            if (it->visible && contains(it->bounds, event.position)) {
                // 调用回调
                if (it->onTouch) {
                    it->onTouch(event);
                }
                // 阻止事件传播
                if (it->blockEvents) {
                    break;
                }
            }
        }
    }

private:
    bool contains(const UIRect& rect, float2 point) {
        return point.x >= rect.position.x &&
               point.x <= rect.position.x + rect.size.x &&
               point.y >= rect.position.y &&
               point.y <= rect.position.y + rect.size.y;
    }

    struct UIElement {
        UIRect bounds;
        bool visible;
        bool blockEvents;
        std::function<void(const TouchEvent&)> onTouch;
    };

    std::vector<UIElement> mElements;
};
```

### Q6: 如何实现 9-patch 图像?

**A:** 实现可拉伸 UI 元素:

```cpp
void draw9Patch(UIRenderer* r, Texture* texture,
               float2 position, float2 size,
               float4 borders) {  // left, top, right, bottom
    // 将图像分成9个区域渲染
    // 角落保持原始尺寸,边缘拉伸,中心同时拉伸

    float leftWidth = borders.x;
    float rightWidth = borders.z;
    float topHeight = borders.y;
    float bottomHeight = borders.w;

    float centerWidth = size.x - leftWidth - rightWidth;
    float centerHeight = size.y - topHeight - bottomHeight;

    // 左上角
    r->drawRect({position, {leftWidth, topHeight}, {1,1,1,1},
                texture, {0, 0}, {0.25f, 0.25f}});

    // 上边（拉伸）
    r->drawRect({{position.x + leftWidth, position.y},
                {centerWidth, topHeight}, {1,1,1,1},
                texture, {0.25f, 0}, {0.75f, 0.25f}});

    // 右上角
    r->drawRect({{position.x + leftWidth + centerWidth, position.y},
                {rightWidth, topHeight}, {1,1,1,1},
                texture, {0.75f, 0}, {1.0f, 0.25f}});

    // 继续渲染其他6个区域...
}
```

---

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.19)
project(ui_rendering)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FILAMENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Filament root")

find_package(filament REQUIRED CONFIG PATHS ${FILAMENT_DIR}/out/cmake-release)

# ImGui 源文件
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
)

add_executable(ui_rendering_demo
    samples/ui_rendering/UIRenderer.h
    samples/ui_rendering/ImGuiIntegration.h
    samples/ui_rendering/HUDSystem.h
    samples/ui_rendering_demo.cpp
    ${IMGUI_SOURCES}
)

target_link_libraries(ui_rendering_demo PRIVATE
    filament
    filamat
    utils
    filamentapp
)

target_include_directories(ui_rendering_demo PRIVATE
    ${FILAMENT_DIR}/filament/include
    ${FILAMENT_DIR}/libs/utils/include
    ${FILAMENT_DIR}/libs/math/include
    ${FILAMENT_DIR}/libs/filamentapp/include
    ${IMGUI_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/samples
)

install(TARGETS ui_rendering_demo DESTINATION bin)
```

---

## 相关文档

- [07-scene-manager.md](./07-scene-manager.md) - 场景管理系统
- [09-post-processing.md](./09-post-processing.md) - 后处理效果
- [../platforms/android.md](../platforms/android.md) - Android 平台
- [../tools/matc.md](../tools/matc.md) - 材质编译器

---

## 总结

本示例展示了在 Filament 中实现 2D UI 渲染的完整方案:

1. **基础 UI 渲染** - 矩形、文本、图片
2. **ImGui 集成** - 即时模式调试界面
3. **HUD 系统** - 游戏界面管理
4. **性能优化** - 批量渲染、缓存
5. **跨平台** - Desktop 和 Android 实现

关键收获:
- 理解 2D/3D 混合渲染
- 掌握 UI 坐标系统
- 实现高性能 UI 批处理
- 处理输入事件和交互

下一步学习 [09-post-processing.md](./09-post-processing.md) 了解后处理效果。
