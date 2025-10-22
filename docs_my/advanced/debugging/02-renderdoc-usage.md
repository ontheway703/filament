# RenderDoc 深度使用指南

## 概述

RenderDoc 是最受欢迎的开源图形调试工具之一，支持 OpenGL、Vulkan、Direct3D 11/12 等多种图形 API。它的强大之处在于能够捕获完整的渲染帧，并允许你逐步分析每个 Draw Call、查看所有渲染状态、检查纹理和缓冲区内容、调试着色器代码。

本文档提供 RenderDoc 的深度使用指南，从基础安装到高级分析技巧，帮助你充分发挥这个工具的威力。

### 为什么选择 RenderDoc

**优势**:
- ✅ **免费开源**: 完全免费，源代码开放
- ✅ **跨平台**: 支持 Windows、Linux、Android
- ✅ **易于使用**: 直观的 UI，学习曲线平缓
- ✅ **功能强大**: 完整的帧分析、资源查看、着色器调试
- ✅ **活跃维护**: 社区活跃，更新频繁
- ✅ **轻量级**: 对性能影响小，易于集成

**适用场景**:
- 分析 Draw Call 和渲染顺序
- 查看渲染状态和管线配置
- 检查纹理、缓冲区内容
- 调试着色器代码
- 性能分析（Draw Call 数量、纹理大小等）
- 学习其他项目的渲染技术

---

## 安装和配置

### Windows/Linux 安装

```bash
# Windows
# 从官网下载安装包: https://renderdoc.org/builds

# Linux (Ubuntu/Debian)
sudo apt install renderdoc

# 或从源码编译
git clone https://github.com/baldurk/renderdoc.git
cd renderdoc
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### Android 配置

```bash
# 1. 在 PC 上安装 RenderDoc

# 2. 通过 adb 连接 Android 设备
adb devices

# 3. 在 RenderDoc 中启用 Android 调试
# File → Inject into Process → Remote Android Device

# 4. 配置包名和 Activity
Package: com.example.filament.app
Activity: MainActivity
```

### 集成到应用

#### 可选：代码集成

虽然 RenderDoc 可以注入到任何应用，但代码集成可以更精确地控制捕获时机：

```cpp
// RenderDocIntegration.h
#ifndef RENDERDOC_INTEGRATION_H
#define RENDERDOC_INTEGRATION_H

#ifdef RENDERDOC_ENABLED
#include "renderdoc_app.h"
#endif

class RenderDocIntegration {
public:
    static bool initialize();
    static bool isAvailable();

    static void startFrameCapture();
    static void endFrameCapture();
    static void triggerCapture();

    static void setOverlayEnabled(bool enabled);
    static void setCaptureKeys(int key);

private:
#ifdef RENDERDOC_ENABLED
    static RENDERDOC_API_1_1_2* sAPI;
#endif
    static bool sInitialized;
};

#endif // RENDERDOC_INTEGRATION_H
```

```cpp
// RenderDocIntegration.cpp
#include "RenderDocIntegration.h"
#include <iostream>

#ifdef RENDERDOC_ENABLED
RENDERDOC_API_1_1_2* RenderDocIntegration::sAPI = nullptr;
#endif
bool RenderDocIntegration::sInitialized = false;

bool RenderDocIntegration::initialize() {
#ifdef RENDERDOC_ENABLED
    if (sInitialized) return true;

    // 加载 RenderDoc 动态库
#ifdef _WIN32
    HMODULE mod = GetModuleHandleA("renderdoc.dll");
    if (!mod) {
        std::cerr << "RenderDoc not found" << std::endl;
        return false;
    }

    pRENDERDOC_GetAPI RENDERDOC_GetAPI =
        (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
#else
    void* mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
    if (!mod) {
        std::cerr << "RenderDoc not found" << std::endl;
        return false;
    }

    pRENDERDOC_GetAPI RENDERDOC_GetAPI =
        (pRENDERDOC_GetAPI)dlsym(mod, "RENDERDOC_GetAPI");
#endif

    if (!RENDERDOC_GetAPI) {
        std::cerr << "Failed to get RenderDoc API" << std::endl;
        return false;
    }

    int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void**)&sAPI);
    if (ret != 1) {
        std::cerr << "Failed to initialize RenderDoc API" << std::endl;
        return false;
    }

    sInitialized = true;
    std::cout << "RenderDoc initialized successfully" << std::endl;

    // 配置默认选项
    sAPI->SetCaptureOptionU32(eRENDERDOC_Option_AllowVSync, 1);
    sAPI->SetCaptureOptionU32(eRENDERDOC_Option_AllowFullscreen, 1);
    sAPI->SetCaptureOptionU32(eRENDERDOC_Option_APIValidation, 1);
    sAPI->SetCaptureOptionU32(eRENDERDOC_Option_DebugOutputMute, 0);

    return true;
#else
    return false;
#endif
}

bool RenderDocIntegration::isAvailable() {
#ifdef RENDERDOC_ENABLED
    return sInitialized && sAPI != nullptr;
#else
    return false;
#endif
}

void RenderDocIntegration::startFrameCapture() {
#ifdef RENDERDOC_ENABLED
    if (!isAvailable()) return;
    sAPI->StartFrameCapture(nullptr, nullptr);
    std::cout << "RenderDoc: Frame capture started" << std::endl;
#endif
}

void RenderDocIntegration::endFrameCapture() {
#ifdef RENDERDOC_ENABLED
    if (!isAvailable()) return;
    sAPI->EndFrameCapture(nullptr, nullptr);
    std::cout << "RenderDoc: Frame capture ended" << std::endl;
#endif
}

void RenderDocIntegration::triggerCapture() {
#ifdef RENDERDOC_ENABLED
    if (!isAvailable()) return;
    sAPI->TriggerCapture();
    std::cout << "RenderDoc: Capture triggered" << std::endl;
#endif
}

void RenderDocIntegration::setOverlayEnabled(bool enabled) {
#ifdef RENDERDOC_ENABLED
    if (!isAvailable()) return;
    sAPI->SetOverlayEnabled(enabled);
#endif
}

void RenderDocIntegration::setCaptureKeys(int key) {
#ifdef RENDERDOC_ENABLED
    if (!isAvailable()) return;
    sAPI->SetCaptureKeys(&key, 1);
#endif
}
```

#### 使用示例

```cpp
// main.cpp
#include "RenderDocIntegration.h"

int main() {
    // 初始化 RenderDoc
    RenderDocIntegration::initialize();

    // 设置 F12 为捕获键
    RenderDocIntegration::setCaptureKeys(SDLK_F12);

    // 启用 overlay
    RenderDocIntegration::setOverlayEnabled(true);

    // 主循环
    while (running) {
        // 可选：程序化捕获特定帧
        if (shouldCapture) {
            RenderDocIntegration::startFrameCapture();
        }

        // 渲染
        render();

        if (shouldCapture) {
            RenderDocIntegration::endFrameCapture();
            shouldCapture = false;
        }
    }

    return 0;
}
```

---

## 基础使用流程

### 1. 捕获帧

#### 方法 A: 通过 UI 启动应用

```
1. 打开 RenderDoc
2. File → Launch Application
3. 配置:
   - Executable Path: 你的应用路径
   - Working Directory: 工作目录
   - Command-line Arguments: 命令行参数
   - Capture Options: 捕获选项
4. 点击 "Launch" 启动应用
5. 按 F12（或配置的快捷键）捕获帧
```

#### 方法 B: 注入到运行中的进程

```
1. File → Attach to Running Instance
2. 选择目标进程
3. 点击 "Inject" 注入 RenderDoc
4. 按 F12 捕获帧
```

#### 方法 C: 程序化捕获

使用前面集成的代码：

```cpp
// 捕获特定帧
if (frameNumber == 100) {
    RenderDocIntegration::startFrameCapture();
}

render();

if (frameNumber == 100) {
    RenderDocIntegration::endFrameCapture();
}
```

### 2. 加载捕获文件

捕获的帧会自动出现在 RenderDoc 的 "Captures" 列表中，双击打开。

捕获文件格式：`.rdc` (RenderDoc Capture)

---

## 界面布局

RenderDoc 的主界面分为几个关键区域：

```
┌─────────────────────────────────────────────────────────┐
│  Menu Bar & Toolbar                                     │
├──────────────┬──────────────────────────────────────────┤
│              │                                          │
│  Event       │  Resource Inspector                      │
│  Browser     │  (Textures, Buffers, Shaders)           │
│              │                                          │
│  (Draw Call  │  ┌────────────────────────────────────┐ │
│   List)      │  │ Texture Viewer                      │ │
│              │  │                                      │ │
│              │  └────────────────────────────────────┘ │
├──────────────┼──────────────────────────────────────────┤
│              │                                          │
│  Pipeline    │  Mesh Viewer                             │
│  State       │                                          │
│              │                                          │
└──────────────┴──────────────────────────────────────────┘
```

### 关键窗口说明

**Event Browser (事件浏览器)**:
- 列出帧中的所有事件（Draw Call、Dispatch、Clear 等）
- 树形结构显示事件层级
- 可以跳转到任意事件查看状态

**Texture Viewer (纹理查看器)**:
- 显示纹理内容
- 支持多种可视化模式（RGB、Alpha、Depth等）
- 可以查看 Mipmap 层级

**Pipeline State (管线状态)**:
- 显示完整的图形管线状态
- 顶点输入、光栅化、输出合并等所有阶段
- 可以跳转到绑定的资源

**Mesh Viewer (网格查看器)**:
- 3D 可视化顶点数据
- 支持查看输入和输出数据
- 可以逐顶点检查

---

## 核心功能详解

### 1. Event Browser 使用

Event Browser 是 RenderDoc 的核心，显示帧中的所有图形事件。

#### 事件类型

```cpp
/**
 * RenderDoc 中的事件类型
 */
enum class EventType {
    // 渲染事件
    Draw,           // 绘制调用
    DrawIndexed,    // 索引绘制
    DrawInstanced,  // 实例化绘制
    Dispatch,       // 计算着色器调度

    // 资源操作
    Clear,          // 清除
    Copy,           // 复制
    Resolve,        // MSAA 解析

    // 状态变更
    SetViewport,    // 设置视口
    SetScissor,     // 设置裁剪矩形
    BindPipeline,   // 绑定管线

    // 标记
    PushMarker,     // 推入调试标记
    PopMarker,      // 弹出调试标记
    SetMarker       // 设置标记
};
```

#### 导航技巧

- **搜索**: `Ctrl+F` 搜索事件名称
- **书签**: 右键 → Add Bookmark 标记重要事件
- **过滤**: 底部的过滤器可以隐藏特定类型的事件
- **跳转**: 双击事件跳转到该事件的状态

#### 添加调试标记

在代码中添加标记，方便在 RenderDoc 中识别：

```cpp
/**
 * OpenGL 调试标记
 */
void renderShadowPass() {
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Shadow Pass");

    // 阴影渲染代码
    for (const auto& light : lights) {
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1,
                        ("Light " + std::to_string(light.id)).c_str());
        renderShadowMap(light);
        glPopDebugGroup();
    }

    glPopDebugGroup();
}

void renderMainPass() {
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Main Pass");

    // G-Buffer Pass
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "G-Buffer");
    renderGBuffer();
    glPopDebugGroup();

    // Lighting Pass
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Lighting");
    renderLighting();
    glPopDebugGroup();

    // Post-processing
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, "Post-Processing");
    renderPostProcessing();
    glPopDebugGroup();

    glPopDebugGroup();
}
```

**Vulkan 调试标记**:

```cpp
void beginDebugLabel(VkCommandBuffer cmd, const char* name,
                    float r, float g, float b) {
    VkDebugUtilsLabelEXT label = {};
    label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    label.pLabelName = name;
    label.color[0] = r;
    label.color[1] = g;
    label.color[2] = b;
    label.color[3] = 1.0f;

    vkCmdBeginDebugUtilsLabelEXT(cmd, &label);
}

void endDebugLabel(VkCommandBuffer cmd) {
    vkCmdEndDebugUtilsLabelEXT(cmd);
}

// 使用示例
void recordCommandBuffer(VkCommandBuffer cmd) {
    beginDebugLabel(cmd, "Shadow Pass", 1.0f, 0.5f, 0.0f);
    // 阴影渲染命令
    endDebugLabel(cmd);

    beginDebugLabel(cmd, "Main Pass", 0.0f, 1.0f, 0.5f);
    // 主渲染命令
    endDebugLabel(cmd);
}
```

### 2. Texture Viewer 使用

Texture Viewer 用于查看纹理内容，是调试渲染问题的关键工具。

#### 可视化模式

```
RGB/RGBA:  显示颜色通道
R/G/B/A:   单独显示某个通道
Depth:     深度缓冲可视化
Stencil:   模板缓冲可视化
```

#### 查看技巧

**查看特定像素值**:
- 鼠标悬停在纹理上，底部显示精确的像素值
- `Ctrl+Click` 锁定特定像素，可以在缩放时持续显示

**调整可视化范围**:
- Range Adapt: 自动调整显示范围到实际数据范围
- Custom Range: 自定义最小/最大值
- 对于 HDR 纹理特别有用

**Mipmap 查看**:
- 右侧的 Mip Level 滑块可以查看不同 Mipmap 层级
- 验证 Mipmap 是否正确生成

**对比纹理**:
- Locked Tabs 功能可以锁定多个纹理视图
- 方便对比不同 Pass 的输出

#### 常见调试场景

**场景 1: 检查 Shadow Map**

```
1. 找到阴影渲染的 Draw Call
2. 在 Pipeline State → Outputs 中点击 Depth Target
3. 选择 Depth 可视化模式
4. 检查:
   - 阴影是否正确渲染
   - 深度范围是否合理
   - 是否有 Z-fighting
```

**场景 2: 检查 G-Buffer**

```
1. 找到 G-Buffer Pass 的最后一个 Draw Call
2. 查看多个 Render Target:
   - RT0: Albedo (Base Color)
   - RT1: Normal
   - RT2: Roughness/Metallic
   - RT3: Emissive
3. 验证每个通道的数据是否正确
```

**场景 3: 检查后处理**

```
1. 找到 Bloom Pass
2. 查看降采样的中间纹理
3. 验证模糊效果
4. 检查最终合成结果
```

### 3. Pipeline State 查看

Pipeline State 窗口显示选定事件时的完整图形管线状态。

#### Vertex Input (顶点输入)

显示:
- Vertex Buffer 绑定
- 顶点属性配置
- 输入布局

**调试示例**:

```cpp
// 检查顶点数据是否正确
void debugVertexInput() {
    /*
    在 RenderDoc 中:
    1. 选择一个 Draw Call
    2. Pipeline State → Vertex Input
    3. 查看:
       - Buffer 0: Position (3 floats)
       - Buffer 0: Normal (3 floats)
       - Buffer 0: UV (2 floats)
    4. 点击 "Go to Buffer Viewer" 查看实际数据
    */
}
```

#### Vertex Shader (顶点着色器)

显示:
- 着色器代码
- Uniform Buffer 绑定
- 纹理绑定

**调试技巧**:
- 点击 "Edit" 可以编辑着色器并实时查看效果
- 点击 "Debug" 可以逐步调试着色器执行

#### Rasterizer (光栅化)

显示:
- 视口配置
- 裁剪矩形
- 剔除模式
- 多边形模式
- 深度偏移

**常见问题排查**:

```cpp
// 背面剔除问题
// 如果模型显示不完整，检查:
// Cull Mode: BACK/FRONT/NONE
// Front Face: CCW/CW

// 深度测试问题
// 如果深度有问题，检查:
// Depth Test: Enabled/Disabled
// Depth Function: LESS/LEQUAL/GREATER/etc.
// Depth Write: Enabled/Disabled
```

#### Fragment Shader (片段着色器)

显示:
- 着色器代码
- Uniform Buffer 绑定
- 纹理采样器绑定

**调试技巧**:
- 点击纹理可以跳转到 Texture Viewer
- 查看 Uniform Buffer 的实际值

#### Output Merger (输出合并)

显示:
- Render Target 绑定
- Blend State 配置
- Depth/Stencil 配置

**调试透明度问题**:

```
检查 Blend State:
- Blend Enable: true/false
- Src Blend: SRC_ALPHA
- Dst Blend: ONE_MINUS_SRC_ALPHA
- Blend Op: ADD

检查 Depth Write:
- 透明物体通常应该禁用深度写入
- Depth Write Enabled: false
```

### 4. Mesh Viewer 使用

Mesh Viewer 以 3D 形式显示顶点数据，非常直观。

#### 显示模式

```
Solid:      实体显示
Wireframe:  线框显示
Points:     点显示
```

#### 查看顶点属性

- **Input**: 显示输入的顶点数据（从 Vertex Buffer）
- **VS Output**: 显示顶点着色器输出
- **GS/TS Output**: 显示几何/曲面细分着色器输出

#### 逐顶点调试

```
1. 选择一个 Draw Call
2. 打开 Mesh Viewer
3. 点击场景中的顶点
4. 底部显示该顶点的所有属性值
5. 可以点击 "Debug Vertex" 逐步调试顶点着色器
```

#### 常见用途

**检查顶点变换**:

```
问题：模型显示异常

调试步骤：
1. 查看 Input 数据（模型空间坐标）
2. 查看 VS Output 数据（裁剪空间坐标）
3. 对比变换是否正确
   - 位置是否在合理范围（-1到1）
   - 是否有 NaN 或 Inf
```

**检查法线**:

```
1. 启用 "Show Normals" 选项
2. 检查法线方向是否正确
3. 检查法线是否规范化
```

### 5. 着色器调试

RenderDoc 支持逐步调试着色器代码，这是其最强大的功能之一。

#### 开始调试

```
方法 1: 调试特定顶点
1. 在 Mesh Viewer 中选择一个顶点
2. 右键 → Debug Vertex

方法 2: 调试特定像素
1. 在 Texture Viewer 中点击一个像素
2. 右键 → Debug Pixel

方法 3: 调试计算着色器
1. 选择 Dispatch 事件
2. Pipeline State → Compute Shader → Debug
```

#### 调试界面

```
┌────────────────────────────────────────────┐
│  Shader Code (左侧)                        │
│  - 高亮当前执行行                          │
│  - 可以设置断点                            │
│                                            │
├────────────────────────────────────────────┤
│  Variables (右侧)                          │
│  - 输入变量                                │
│  - 局部变量                                │
│  - 输出变量                                │
└────────────────────────────────────────────┘

控制按钮：
- Step Over (F10): 执行下一行
- Step Into (F11): 进入函数
- Step Out (Shift+F11): 退出函数
- Continue (F5): 继续执行到断点
```

#### 调试示例

```glsl
// Fragment Shader 调试示例
vec4 fragmentShader() {
    vec3 albedo = texture(albedoMap, uv).rgb;  // ← 断点 1
    vec3 normal = normalize(vNormal);

    vec3 lighting = vec3(0.0);
    for (int i = 0; i < lightCount; i++) {    // ← 断点 2
        Light light = lights[i];
        vec3 L = normalize(light.position - vPosition);
        float NdotL = max(dot(normal, L), 0.0);
        lighting += light.color * NdotL;
    }

    vec3 finalColor = albedo * lighting;
    return vec4(finalColor, 1.0);              // ← 断点 3
}
```

**调试步骤**:

```
1. 在 Texture Viewer 中选择一个黑色像素（怀疑光照有问题）
2. 右键 → Debug Pixel
3. 设置断点 1，查看 albedo 值
   - 如果 albedo 是黑色，问题在纹理
4. 单步执行到断点 2，进入循环
5. 查看 NdotL 值
   - 如果是 0，说明法线或光照方向有问题
6. 继续执行，查看最终 finalColor
```

### 6. 性能分析

虽然 RenderDoc 主要用于功能调试，但也提供基础的性能分析。

#### 统计信息

在 Event Browser 底部，显示:
- **Draw Calls**: Draw Call 总数
- **Dispatches**: Compute Shader 调度次数
- **Vertices**: 顶点总数
- **Primitives**: 图元总数（三角形数量）

#### 资源使用统计

Window → Resource Inspector 显示:
- **Textures**: 纹理列表和大小
- **Buffers**: 缓冲区列表和大小
- **Pipelines**: 管线状态对象

#### 查找性能瓶颈

**识别冗余 Draw Call**:

```
1. 查看 Event Browser
2. 寻找:
   - 空 Draw Call（顶点数为 0）
   - 屏幕外的 Draw Call（可以剔除）
   - 重复的状态切换
```

**识别过大的纹理**:

```
1. Resource Inspector → Textures
2. 按大小排序
3. 检查:
   - 是否有不必要的高分辨率纹理
   - Mipmap 链是否完整
   - 是否使用了压缩格式
```

**识别过度绘制**:

```
1. 选择最终的 Draw Call
2. Texture Viewer → Overlay → Overdraw
3. 显示像素被绘制的次数:
   - 绿色: 1-2 次（正常）
   - 黄色: 3-4 次（注意）
   - 红色: 5+ 次（严重过度绘制）
```

---

## 高级技巧

### 1. Python 脚本自动化

RenderDoc 支持 Python 脚本自动化分析。

```python
# analyze_capture.py
import renderdoc as rd

def analyze_draw_calls(controller):
    """统计 Draw Call 信息"""

    api = controller.GetAPIProperties()
    print(f"API: {api.pipelineType}")

    # 遍历所有事件
    events = controller.GetRootActions()

    total_draws = 0
    total_verts = 0
    total_prims = 0

    def process_action(action):
        nonlocal total_draws, total_verts, total_prims

        if action.flags & rd.ActionFlags.Drawcall:
            total_draws += 1
            total_verts += action.numIndices if action.numIndices > 0 else action.numVertices
            total_prims += action.numIndices // 3 if action.numIndices > 0 else action.numVertices // 3

        # 递归处理子事件
        for child in action.children:
            process_action(child)

    for action in events:
        process_action(action)

    print(f"Total Draw Calls: {total_draws}")
    print(f"Total Vertices: {total_verts}")
    print(f"Total Primitives: {total_prims}")

def find_large_textures(controller):
    """查找大纹理"""

    resources = controller.GetResources()

    large_textures = []
    threshold = 4 * 1024 * 1024  # 4MB

    for res in resources:
        if res.type == rd.ResourceType.Texture:
            # 估算纹理大小
            size = res.width * res.height * 4  # 假设 RGBA8
            if size > threshold:
                large_textures.append((res.name, size // 1024 // 1024))

    print("Large Textures (>4MB):")
    for name, size_mb in sorted(large_textures, key=lambda x: x[1], reverse=True):
        print(f"  {name}: {size_mb} MB")

# 使用方法：
# 1. 在 RenderDoc 中打开捕获
# 2. Tools → Python Shell
# 3. 执行脚本
```

### 2. 对比多帧

```python
# compare_frames.py
def compare_frames(cap1, cap2):
    """对比两个捕获的差异"""

    # 对比 Draw Call 数量
    draw_count1 = count_draw_calls(cap1)
    draw_count2 = count_draw_calls(cap2)

    print(f"Draw Calls: {draw_count1} vs {draw_count2}")
    print(f"Difference: {draw_count2 - draw_count1}")

    # 对比资源使用
    textures1 = get_textures(cap1)
    textures2 = get_textures(cap2)

    new_textures = set(textures2) - set(textures1)
    removed_textures = set(textures1) - set(textures2)

    if new_textures:
        print(f"New textures: {new_textures}")
    if removed_textures:
        print(f"Removed textures: {removed_textures}")
```

### 3. 自定义资源查看器

```python
# custom_viewer.py
def show_depth_histogram(controller, texture_id):
    """显示深度缓冲的直方图"""

    # 获取纹理数据
    texture = controller.GetTexture(texture_id)
    data = controller.GetTextureData(texture_id, rd.Subresource())

    # 转换为 numpy 数组
    import numpy as np
    depths = np.frombuffer(data, dtype=np.float32)

    # 绘制直方图
    import matplotlib.pyplot as plt
    plt.hist(depths, bins=100)
    plt.title('Depth Buffer Histogram')
    plt.xlabel('Depth Value')
    plt.ylabel('Pixel Count')
    plt.show()
```

### 4. 批量导出资源

```python
# export_resources.py
def export_all_textures(controller, output_dir):
    """导出所有纹理"""

    import os

    resources = controller.GetResources()

    for res in resources:
        if res.type == rd.ResourceType.Texture:
            # 构造输出路径
            filename = f"{res.name}.png"
            filepath = os.path.join(output_dir, filename)

            # 保存纹理
            controller.SaveTextureToFile(res.resourceId, filepath)
            print(f"Exported: {filepath}")
```

---

## 常见问题排查

### 问题 1: 黑屏

**RenderDoc 调试步骤**:

```
1. 捕获帧
2. 检查最终的输出纹理（通常是 Backbuffer）
   - 如果是纯黑色，继续
3. 回溯到清屏操作
   - 检查 Clear Color 是否是黑色
4. 检查是否有 Draw Call
   - 如果没有 → 问题在 CPU 端（没有提交渲染命令）
   - 如果有 → 继续
5. 选择一个 Draw Call，查看输出
   - 如果输出是空的 → 检查着色器
6. 检查顶点数据
   - Mesh Viewer 查看顶点是否在视锥内
7. 调试 Fragment Shader
   - 查看是否所有像素都被丢弃
```

### 问题 2: 模型显示不完整

**RenderDoc 调试步骤**:

```
1. 检查剔除模式（Cull Mode）
   Pipeline State → Rasterizer → Cull Mode
   - 尝试设置为 NONE

2. 检查深度测试
   Pipeline State → Output Merger → Depth Test
   - 检查 Depth Function 是否正确

3. 检查裁剪平面
   Pipeline State → Rasterizer → Viewport
   - 检查 Near/Far 平面

4. 在 Mesh Viewer 中查看几何体
   - 检查是否有缺失的三角形
```

### 问题 3: 纹理显示错误

**RenderDoc 调试步骤**:

```
1. 找到使用该纹理的 Draw Call
2. Pipeline State → Fragment Shader
3. 点击纹理槽位
4. 在 Texture Viewer 中查看纹理内容
   - 检查纹理是否正确加载
   - 检查 Mipmap 是否完整
5. 调试 Fragment Shader
   - 查看 UV 坐标是否正确
   - 查看采样结果
```

### 问题 4: 透明度问题

**RenderDoc 调试步骤**:

```
1. 检查 Blend State
   Pipeline State → Output Merger → Blend State
   - Blend Enable: true
   - Src Blend: SRC_ALPHA
   - Dst Blend: ONE_MINUS_SRC_ALPHA

2. 检查 Alpha 值
   - 在 Fragment Shader 中查看输出的 Alpha
   - 在 Texture Viewer 中查看 Alpha 通道

3. 检查渲染顺序
   - 透明物体应该后渲染
   - 检查 Event Browser 中的顺序
```

### 问题 5: 阴影错误

**RenderDoc 调试步骤**:

```
1. 找到 Shadow Pass
2. 查看 Shadow Map 纹理
   - Depth 可视化模式
   - 检查深度范围（Range Adapt）

3. 检查阴影矩阵
   - 在 Fragment Shader 中查看变换后的坐标
   - 检查是否在 [0,1] 范围内

4. 检查 PCF 采样
   - 调试 Shadow Sampling 代码
   - 查看采样结果
```

---

## 性能优化建议

### 基于 RenderDoc 的优化流程

```
1. 捕获代表性的帧（复杂场景）
2. 统计信息分析
   - Draw Call 数量过多？ → 考虑批处理
   - 顶点数量过多？ → 考虑 LOD
   - 纹理过大？ → 考虑压缩/降分辨率
3. 识别瓶颈
   - 过度绘制严重？ → 优化渲染顺序
   - 冗余状态切换？ → 优化状态管理
   - 大量小 Draw Call？ → 考虑实例化
4. 实施优化
5. 再次捕获，对比数据
```

### 优化检查清单

- [ ] Draw Call 数量 < 500（移动端 < 100）
- [ ] 过度绘制 < 2x
- [ ] 无空 Draw Call（0 顶点）
- [ ] 纹理使用压缩格式
- [ ] Mipmap 链完整
- [ ] 无不必要的高分辨率纹理
- [ ] 状态切换最小化
- [ ] 透明物体正确排序

---

## 总结

RenderDoc 是图形开发者的必备工具。掌握它的使用能大大提高调试效率：

**核心功能**:
- ✅ 完整的帧捕获和回放
- ✅ 逐事件的状态查看
- ✅ 纹理和缓冲区查看
- ✅ 着色器调试
- ✅ 3D 网格查看
- ✅ Python 脚本自动化

**最佳实践**:
- ✅ 添加调试标记（Debug Marker）
- ✅ 定期捕获帧检查渲染正确性
- ✅ 使用脚本自动化常规检查
- ✅ 保存关键帧作为回归测试基准

**进阶学习**:
- 学习 Python API 自动化分析
- 结合其他工具（Nsight、PIX）
- 参与 RenderDoc 社区

## 相关文档

- [01-debugging-workflow.md](./01-debugging-workflow.md) - 调试流程
- [03-platform-debuggers.md](./03-platform-debuggers.md) - 其他平台调试器
- [09-shader-debugging.md](./09-shader-debugging.md) - 着色器调试详解
- [RenderDoc 官方文档](https://renderdoc.org/docs/)
