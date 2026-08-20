# 平台抽象层

本文档从**设计理念**角度讲解 Filament Platform 接口,重点阐述跨平台渲染的核心挑战、SwapChain 概念,以及架构设计思想。

---

## 一、为什么需要平台抽象层?

### 问题背景

在跨平台渲染引擎开发中,面临的核心挑战:

1. **窗口系统差异巨大**:
   - Windows: Win32 API + HWND
   - macOS: Cocoa + NSView/NSWindow
   - Linux: X11/Wayland + Window
   - Android: ANativeWindow
   - iOS: UIView

2. **图形 API 与平台绑定**:
   - OpenGL 需要平台特定的上下文创建(EGL/GLX/WGL/CGL)
   - Vulkan 需要平台特定的 Surface(VkWin32Surface/VkXlibSurface)
   - Metal 只能在 Apple 平台使用

3. **呈现机制不统一**:
   - OpenGL: 隐式 SwapBuffers
   - Vulkan: 显式 SwapChain 管理
   - Metal: CAMetalLayer + Drawable

### 设计目标

Platform 抽象层通过**统一接口**隐藏这些差异:

```
┌─────────────────────────────────────────────────────────┐
│              Filament Engine (平台无关)                  │
├─────────────────────────────────────────────────────────┤
│              Platform 接口 (统一抽象)                     │
├──────────┬──────────┬──────────┬──────────┬─────────────┤
│ OpenGL   │ Vulkan   │  Metal   │  D3D12   │   WebGL    │
│ Platform │ Platform │ Platform │ Platform │  Platform  │
├──────────┴──────────┴──────────┴──────────┴─────────────┤
│  Windows  │  macOS  │  Linux  │  Android  │    iOS     │
└───────────┴─────────┴─────────┴───────────┴────────────┘
```

**设计原则**:
1. **上层代码平台无关**: Engine 层不关心操作系统
2. **一致的生命周期管理**: 统一的创建/销毁流程
3. **灵活扩展**: 添加新平台只需实现 Platform 接口

---

## 二、SwapChain 深度解析

### 什么是 SwapChain?

**SwapChain(交换链)** 是现代图形系统中最核心的概念之一,但也是最容易让人困惑的。

#### 用通俗比喻理解

想象你在画动画:
- 你有**多张画布**轮流使用
- 在**画布1**上画画时,观众看的是**画布2**
- 画完后**交换**:把画布1挂到墙上,在画布2上继续画
- 观众看到的永远是"完整的画",不会看到你正在画的半成品

这就是 SwapChain 的本质:**缓冲区轮换机制**。

#### SwapChain 解决的核心问题

**问题**: 如果直接在屏幕上渲染,用户会看到:
```
帧 N 渲染过程:
[=====               ] 渲染进度 25% ← 用户看到撕裂、闪烁
[==========          ] 渲染进度 50% ← 画面不完整
[====================] 渲染进度 100% ← 完整画面
```

**解决**: 使用双缓冲 SwapChain:
```
┌───────────────────────────────────────────────────────┐
│                    双缓冲 SwapChain                     │
│                                                        │
│   Back Buffer              Front Buffer               │
│   (渲染目标)                (屏幕显示)                  │
│                                                        │
│  ┌──────────┐              ┌──────────┐               │
│  │ 渲染中... │              │ 显示中... │               │
│  │ [====    ]│              │ [完整帧] │               │
│  └──────────┘              └──────────┘               │
│       ↓                         ↑                      │
│       └────── Present() ────────┘                      │
│              (交换缓冲区)                               │
└───────────────────────────────────────────────────────┘

时间轴:
帧 N:   后台渲染 Buffer A → Present → Buffer A 显示
帧 N+1: 后台渲染 Buffer B → Present → Buffer B 显示 (Buffer A 被复用)
帧 N+2: 后台渲染 Buffer A → Present → Buffer A 显示 (Buffer B 被复用)
```

### 双缓冲 vs 三缓冲

| 缓冲模式 | 缓冲区数量 | 优点 | 缺点 | 适用场景 |
|---------|-----------|------|------|---------|
| **单缓冲** | 1个 | 内存占用少 | 严重撕裂和闪烁 | 几乎不使用 |
| **双缓冲** | 2个 | 无撕裂,内存适中 | 可能有输入延迟 | 通用场景(默认) |
| **三缓冲** | 3个 | 低延迟,高帧率 | 内存占用高 | 竞技游戏 |

**三缓冲工作流程**:
```
时刻 T0: 渲染 Buffer C | 准备 Buffer B | 显示 Buffer A
         ↓ Present
时刻 T1: 渲染 Buffer A | 准备 Buffer C | 显示 Buffer B
         ↓ Present
时刻 T2: 渲染 Buffer B | 准备 Buffer A | 显示 Buffer C
```

### SwapChain 与相关概念的关系

```
┌─────────────────────────────────────────────────────────┐
│                     Platform Layer                       │
│                                                          │
│  ┌────────────┐       ┌──────────────┐                  │
│  │   Window   │──────▶│   Surface    │                  │
│  │ (操作系统)  │       │ (渲染目标抽象) │                  │
│  └────────────┘       └──────┬───────┘                  │
│                              │                           │
│                              ▼                           │
│                       ┌──────────────┐                   │
│                       │  SwapChain   │                   │
│                       │ (缓冲区管理)  │                   │
│                       └──────┬───────┘                   │
│                              │                           │
│                    ┌─────────┴─────────┐                 │
│                    ▼                   ▼                 │
│            ┌──────────────┐    ┌──────────────┐         │
│            │ Back Buffer  │    │ Front Buffer │         │
│            │ (RenderTarget)│    │  (Display)   │         │
│            └──────────────┘    └──────────────┘         │
└─────────────────────────────────────────────────────────┘

关系说明:
- Window: 操作系统窗口句柄 (HWND/NSView/ANativeWindow)
- Surface: 图形API的绘制目标 (EGLSurface/VkSurfaceKHR/CAMetalLayer)
- SwapChain: 管理多个缓冲区的轮换逻辑
- Buffer: 实际的帧缓冲 (Texture/Image)
```

### 不同图形 API 的 SwapChain 实现

#### OpenGL: 隐式 SwapChain

```cpp
// OpenGL 的 SwapChain 是隐式的,由窗口系统管理
GLFWwindow* window = glfwCreateWindow(...);

// 渲染循环
while (running) {
    // 渲染到默认帧缓冲 (Back Buffer)
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    // ... 渲染命令 ...

    // 交换前后缓冲 (SwapChain 操作被隐藏)
    glfwSwapBuffers(window);  // ← 内部管理了 SwapChain
}
```

**特点**:
- 开发者**不需要**显式创建 SwapChain
- 平台 API 自动管理(EGL/GLX/WGL)
- 简单但不灵活

#### Vulkan: 显式 SwapChain

```cpp
// Vulkan 要求显式管理 SwapChain
VkSwapchainKHR swapchain;
VkSwapchainCreateInfoKHR createInfo = {
    .imageCount = 3,          // 三缓冲
    .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,  // 低延迟模式
    // ...
};
vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain);

// 渲染循环
while (running) {
    // 获取下一个可用图像索引
    uint32_t imageIndex;
    vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                         semaphore, nullptr, &imageIndex);

    // 渲染到 swapchain images[imageIndex]
    // ...

    // 提交并呈现
    VkPresentInfoKHR presentInfo = { .pImageIndices = &imageIndex };
    vkQueuePresentKHR(queue, &presentInfo);
}
```

**特点**:
- 完全**显式控制**
- 可精确配置缓冲数量、呈现模式
- 复杂但灵活

#### Metal: CAMetalLayer

```objc
// Metal 使用 CAMetalLayer 管理 SwapChain
CAMetalLayer* metalLayer = (CAMetalLayer*)view.layer;
metalLayer.device = device;

// 渲染循环
while (running) {
    // 获取下一个 Drawable (类似 SwapChain 的 Buffer)
    id<CAMetalDrawable> drawable = [metalLayer nextDrawable];

    // 渲染到 drawable.texture
    // ...

    // 呈现
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}
```

**特点**:
- 使用 Core Animation 层管理
- 自动处理双缓冲/三缓冲
- Apple 平台独有

### 平台 SwapChain 实现对比

| 平台/API | SwapChain 类型 | 缓冲区管理 | 创建方式 | 呈现方式 |
|---------|---------------|-----------|---------|---------|
| **OpenGL/EGL** | `EGLSurface` | 隐式,由 EGL 管理 | `eglCreateWindowSurface()` | `eglSwapBuffers()` |
| **OpenGL/GLX** | `GLXWindow` | 隐式,由 GLX 管理 | `glXCreateWindow()` | `glXSwapBuffers()` |
| **OpenGL/WGL** | `HDC` | 隐式,由 GDI 管理 | `GetDC(hwnd)` | `SwapBuffers(hdc)` |
| **Vulkan** | `VkSwapchainKHR` | 显式,开发者控制 | `vkCreateSwapchainKHR()` | `vkQueuePresentKHR()` |
| **Metal** | `CAMetalLayer` | 半自动,Layer 管理 | `-[CAMetalLayer init]` | `presentDrawable:` |
| **D3D12** | `IDXGISwapChain` | 显式,DXGI 管理 | `CreateSwapChainForHwnd()` | `Present()` |

---

## 三、Platform 接口设计

### 核心接口定义

**文件位置**: `filament/backend/include/backend/Platform.h`

```cpp
class Platform {
public:
    // === SwapChain 生命周期 ===

    // 从窗口创建 SwapChain
    virtual SwapChain* createSwapChain(
        void* nativeWindow,    // 平台窗口句柄
        uint64_t flags = 0
    ) noexcept = 0;

    // 离屏 SwapChain (无窗口渲染)
    virtual SwapChain* createSwapChain(
        uint32_t width, uint32_t height,
        uint64_t flags = 0
    ) noexcept = 0;

    virtual void destroySwapChain(SwapChain*) noexcept = 0;

    // === Driver 创建 ===
    virtual Driver* createDriver(
        void* sharedContext,
        const DriverConfig& config
    ) noexcept = 0;

    // === 帧呈现 ===
    virtual void commit(SwapChain*) noexcept;  // Vulkan/Metal 使用
    virtual void swapBuffers(SwapChain*) noexcept;  // OpenGL 使用
};
```

### 设计模式分析

#### 1. 工厂模式 (Factory Pattern)

Platform 作为工厂,创建平台特定对象:

```
┌─────────────────────────────────────────┐
│         Platform (抽象工厂)              │
├─────────────────────────────────────────┤
│ + createSwapChain() → SwapChain*        │
│ + createDriver() → Driver*              │
└───────────┬─────────────────────────────┘
            │
    ┌───────┴────────┬────────────────┐
    ▼                ▼                ▼
┌─────────┐    ┌──────────┐    ┌──────────┐
│ OpenGL  │    │  Vulkan  │    │  Metal   │
│ Platform│    │ Platform │    │ Platform │
├─────────┤    ├──────────┤    ├──────────┤
│创建 EGL │    │创建 Vk   │    │创建 Metal│
│SwapChain│    │SwapChain │    │SwapChain │
└─────────┘    └──────────┘    └──────────┘
```

**优点**: 上层代码不关心具体平台,通过工厂获取对象。

#### 2. 桥接模式 (Bridge Pattern)

Platform 作为桥梁,连接 Engine 和操作系统:

```
Engine (抽象层)
   │
   │ 使用
   ▼
Platform 接口 ──────┐
   │                │ 桥接
   │ 实现            │
   ▼                ▼
具体 Platform ──→ OS API
(PlatformEGL)    (eglSwapBuffers)
```

#### 3. 策略模式 (Strategy Pattern)

不同 Platform 实现不同策略:

```cpp
// 策略1: OpenGL Platform - 隐式缓冲管理
void OpenGLPlatform::commit(SwapChain* sc) {
    makeCurrent(sc);
    swapBuffers(sc);  // 平台 API 处理 SwapChain
}

// 策略2: Vulkan Platform - 显式缓冲管理
void VulkanPlatform::commit(SwapChain* sc) {
    uint32_t imageIndex;
    vkAcquireNextImageKHR(..., &imageIndex);  // 手动获取缓冲
    vkQueuePresentKHR(...);  // 手动呈现
}
```

---

## 四、跨平台挑战与解决方案

### 挑战 1: 窗口句柄类型不统一

**问题**: 每个平台的窗口句柄类型完全不同:

| 平台 | 窗口句柄类型 | 示例 |
|------|-------------|------|
| Windows | `HWND` | `HWND hwnd = CreateWindow(...)` |
| macOS | `NSView*` | `NSView* view = [[NSView alloc] init]` |
| Linux X11 | `Window` (unsigned long) | `Window xwin = XCreateWindow(...)` |
| Android | `ANativeWindow*` | `ANativeWindow* win = ...` |

**解决方案**: 使用 `void*` 作为**通用指针**:

```cpp
SwapChain* createSwapChain(void* nativeWindow, uint64_t flags) {
    #if defined(__ANDROID__)
        ANativeWindow* window = reinterpret_cast<ANativeWindow*>(nativeWindow);
    #elif defined(_WIN32)
        HWND hwnd = reinterpret_cast<HWND>(nativeWindow);
    #elif defined(__linux__)
        Window xwindow = reinterpret_cast<Window>(nativeWindow);
    #endif
    // ...
}
```

### 挑战 2: SwapChain 生命周期不同

| API | 创建时机 | 销毁责任 | 特殊性 |
|-----|---------|---------|--------|
| **OpenGL** | 创建上下文时自动创建 | 销毁上下文时自动销毁 | 无需手动管理 |
| **Vulkan** | 必须显式创建 | 必须显式销毁 | 窗口 resize 需重建 |
| **Metal** | Layer 创建时存在 | Layer 销毁时清理 | 半自动管理 |

**解决方案**: Platform 统一管理生命周期:

```cpp
// 统一的创建/销毁接口
SwapChain* sc = platform->createSwapChain(window);
// ... 使用 ...
platform->destroySwapChain(sc);  // 内部处理平台差异
```

### 挑战 3: VSync 控制方式不同

**问题**: 不同 API 的 VSync 控制方式完全不同:

```cpp
// OpenGL (GLX)
glXSwapIntervalEXT(display, drawable, 1);  // 1 = VSync

// Vulkan
VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;  // FIFO = VSync

// Metal
metalLayer.displaySyncEnabled = YES;  // 属性控制
```

**解决方案**: 在 SwapChain 创建时统一配置:

```cpp
// 统一的 flags 控制
enum SwapChainFlags : uint64_t {
    CONFIG_VSYNC         = 0x1,
    CONFIG_TRIPLE_BUFFER = 0x2,
    CONFIG_HDR           = 0x4,
};

SwapChain* sc = platform->createSwapChain(window,
    CONFIG_VSYNC | CONFIG_TRIPLE_BUFFER);
```

---

## 五、模块协作关系

### Platform 在整体架构中的位置

```
┌──────────────────────────────────────────────────────────┐
│                    Filament Engine                       │
│  ┌────────────┐  ┌──────────┐  ┌───────────┐             │
│  │  Renderer  │→ │ View     │→ │ SwapChain │             │
│  └────────────┘  └──────────┘  └─────┬─────┘             │
└────────────────────────────────────┬─┴───────────────────┘
                                     │
                    调用             │
                                     ▼
         ┌───────────────────────────────────────────┐
         │           Platform 接口                    │
         │  createSwapChain() / commit()             │
         └───────────────┬───────────────────────────┘
                         │
            ┌────────────┼────────────┐
            ▼            ▼            ▼
    ┌──────────┐  ┌──────────┐  ┌──────────┐
    │ OpenGL   │  │ Vulkan   │  │  Metal   │
    │ Platform │  │ Platform │  │ Platform │
    └─────┬────┘  └─────┬────┘  └─────┬────┘
          │             │              │
          ▼             ▼              ▼
    ┌──────────┐  ┌──────────┐  ┌──────────┐
    │   EGL    │  │ Vulkan   │  │ Core     │
    │   API    │  │   API    │  │Animation │
    └──────────┘  └──────────┘  └──────────┘
```

### 典型渲染流程中的协作

```
初始化阶段:
  Engine::create()
    └→ Platform::createDriver()  // 创建后端 Driver
    └→ Platform::createSwapChain(window)  // 创建交换链

渲染帧:
  Renderer::render(View, Scene)
    ├→ Driver::beginFrame()
    ├→ Driver::draw(...)  // 渲染到 SwapChain 的 Back Buffer
    ├→ Driver::endFrame()
    └→ Platform::commit(swapChain)  // 呈现 Back Buffer
           ├→ [OpenGL] swapBuffers()
           ├→ [Vulkan] vkQueuePresentKHR()
           └→ [Metal] presentDrawable:

窗口 Resize:
  onWindowResize(newWidth, newHeight)
    └→ Platform::destroySwapChain(oldSwapChain)
    └→ Platform::createSwapChain(window, newFlags)
    └→ Driver::updateRenderTarget(newSwapChain)

清理阶段:
  Engine::destroy()
    └→ Platform::destroySwapChain(swapChain)
    └→ Platform::destroyDriver(driver)
```

### Platform 与 Driver 的职责划分

| 组件 | 职责 | 示例 |
|------|------|------|
| **Platform** | 窗口系统集成、SwapChain 管理 | 创建 EGLSurface、管理缓冲区轮换 |
| **Driver** | 图形命令执行、资源管理 | 编译 Shader、绘制三角形 |

**关键交互**:
```cpp
// Platform 创建 SwapChain
SwapChain* sc = platform->createSwapChain(window);

// Driver 将 SwapChain 包装为 RenderTarget
Handle<HwRenderTarget> rt = driver->createRenderTarget(
    TargetBufferFlags::COLOR,
    sc->width, sc->height,
    sc  // ← 关联 SwapChain
);

// 渲染到 RenderTarget (间接渲染到 SwapChain)
driver->beginRenderPass(rt, ...);
driver->draw(...);
driver->endRenderPass();

// Platform 负责呈现
platform->commit(sc);
```

---

## 六、实际使用场景

### 场景 1: 创建窗口并渲染

```cpp
// 1. 获取平台特定的 Platform 实例
Platform* platform = Platform::create(Backend::OPENGL);

// 2. 从原生窗口创建 SwapChain
void* nativeWindow = /* 平台窗口句柄 */;
SwapChain* swapChain = platform->createSwapChain(nativeWindow);

// 3. 创建 Driver
Driver* driver = platform->createDriver(nullptr, {});

// 4. 渲染循环
while (running) {
    driver->beginFrame(timestamp, frameId);

    // 渲染到 SwapChain
    driver->makeCurrent(swapChain, swapChain);
    driver->draw(...);

    driver->endFrame(frameId);

    // 呈现
    platform->commit(swapChain);
}

// 5. 清理
platform->destroySwapChain(swapChain);
delete platform;
```

### 场景 2: 离屏渲染 (无窗口)

```cpp
// 创建离屏 SwapChain
SwapChain* offscreenSwapChain = platform->createSwapChain(
    1920, 1080,  // 分辨率
    0  // 无需 VSync
);

// 渲染到离屏缓冲
driver->beginFrame(...);
driver->makeCurrent(offscreenSwapChain, offscreenSwapChain);
driver->draw(...);
driver->endFrame(...);

// 读取像素数据
PixelBufferDescriptor pixels(...);
driver->readPixels(offscreenSwapChain, 0, 0, 1920, 1080, std::move(pixels));
```

### 场景 3: 处理窗口 Resize

```cpp
void onWindowResize(Platform* platform, SwapChain* oldSwapChain,
                   void* window, uint32_t newWidth, uint32_t newHeight) {
    // 1. 销毁旧 SwapChain
    platform->destroySwapChain(oldSwapChain);

    // 2. 重新创建 SwapChain
    uint64_t flags = CONFIG_VSYNC | CONFIG_TRIPLE_BUFFER;
    SwapChain* newSwapChain = platform->createSwapChain(window, flags);

    // 3. 更新 Engine 的渲染目标
    engine->updateSwapChain(newSwapChain);
}
```

### 常见误区

❌ **错误 1: 混淆 Window 和 SwapChain**
```cpp
// 错误: 认为 Window 就是 SwapChain
void* window = createWindow();
draw(window);  // ✗ 不能直接渲染到 Window
```

✅ **正确做法**:
```cpp
void* window = createWindow();
SwapChain* sc = platform->createSwapChain(window);  // 从 Window 创建 SwapChain
draw(sc);  // ✓ 渲染到 SwapChain
```

❌ **错误 2: 忘记处理 SwapChain 重建**
```cpp
// 窗口 resize 后继续使用旧 SwapChain
onResize(newWidth, newHeight);
// ... 继续使用旧 SwapChain → 可能崩溃或显示错误
```

✅ **正确做法**:
```cpp
onResize(newWidth, newHeight) {
    platform->destroySwapChain(oldSc);
    newSc = platform->createSwapChain(window);
}
```

---

## 七、设计权衡

### 权衡 1: 统一接口 vs 平台特性

**问题**: 如何在统一接口和平台特性之间平衡?

**Filament 的选择**:
- **基础功能**: 完全统一(createSwapChain, commit)
- **高级特性**: 通过 `flags` 参数暴露(HDR, VRR)
- **极端特性**: 平台特定扩展

```cpp
// 统一接口
SwapChain* sc = platform->createSwapChain(window);

// 平台特性通过 flags
SwapChain* hdrSc = platform->createSwapChain(window, CONFIG_HDR);

// 极端情况: 类型转换访问平台特定功能
if (backend == Backend::VULKAN) {
    VulkanPlatform* vkPlatform = static_cast<VulkanPlatform*>(platform);
    vkPlatform->enableRayTracing();  // Vulkan 特有
}
```

### 权衡 2: 自动管理 vs 手动控制

**OpenGL 风格 (自动)**:
- ✅ 简单易用
- ❌ 缺乏控制,性能优化困难

**Vulkan 风格 (手动)**:
- ✅ 完全控制,性能可优化
- ❌ 复杂,易出错

**Filament 的选择**: **平台决定策略**
- OpenGL Platform: 自动管理(eglSwapBuffers)
- Vulkan Platform: 手动控制(vkAcquireNextImageKHR)
- 上层代码只调用统一的 `commit()`,内部策略不同

### 权衡 3: 多后端支持的代价

**成本**:
- 维护多个 Platform 实现
- 每个平台需要适配和测试
- 抽象可能限制某些平台的高级特性

**收益**:
- 一次编写,到处运行
- 降低上层开发复杂度
- 便于切换渲染后端(OpenGL → Vulkan)

**Filament 的取舍**: 优先支持主流平台(Windows/macOS/Linux/Android/iOS),次要平台通过社区贡献。

---

## 八、总结

### 核心要点

1. **Platform 抽象层的本质**:
   - 隐藏操作系统和窗口系统的差异
   - 提供统一的 SwapChain 管理接口
   - 作为 Engine 和 OS 之间的桥梁

2. **SwapChain 的核心作用**:
   - 解决画面撕裂和闪烁问题
   - 管理多个缓冲区的轮换
   - 不同 API 实现方式差异巨大(隐式 vs 显式)

3. **设计模式应用**:
   - 工厂模式: 创建平台特定对象
   - 桥接模式: 连接抽象层和实现层
   - 策略模式: 不同平台不同策略

4. **跨平台关键技术**:
   - 使用 `void*` 统一窗口句柄
   - 通过 `flags` 暴露平台特性
   - 统一生命周期管理接口

### SwapChain 理解要点

| 关键概念 | 核心理解 |
|---------|---------|
| **为什么需要** | 防止撕裂和闪烁 |
| **本质** | 缓冲区轮换机制 |
| **实现方式** | OpenGL 隐式、Vulkan 显式、Metal 半自动 |
| **与 Window 关系** | Window 是容器,SwapChain 是渲染目标 |
| **生命周期** | 随窗口 resize 需重建 |

---

## 相关文档

- **[01-architecture-overview.md](01-architecture-overview.md)**: Backend 架构总览
- **[05-opengl-backend.md](05-opengl-backend.md)**: OpenGL 后端实现
- **[06-vulkan-backend.md](06-vulkan-backend.md)**: Vulkan 后端实现
- **[07-metal-backend.md](07-metal-backend.md)**: Metal 后端实现

**上层使用**:
- `../engine/07-render-loop.md`: 渲染循环与 Platform 协作
