# Camera 和 View 系统

## 概述

Filament 中的 Camera 和 View 是渲染管线中的核心组件，它们共同决定了"从哪里看"和"如何渲染"。Camera 定义了观察点的位置、方向和投影方式，View 则配置了渲染参数、后处理效果和渲染目标。

### Camera vs View

```
+-----------------+          +-----------------+
|     Camera      |          |      View       |
+-----------------+          +-----------------+
| - Position      |          | - Camera        |
| - Direction     |   uses   | - Scene         |
| - Projection    |--------->| - Viewport      |
| - FOV/Ortho     |          | - RenderTarget  |
| - Near/Far      |          | - PostProcess   |
+-----------------+          | - AA/AO/Bloom   |
                             +-----------------+
```

**关键区别：**
- **Camera**：纯数学对象，定义观察变换和投影变换
- **View**：渲染配置对象，关联 Camera、Scene，配置渲染效果

一个 Camera 可以被多个 View 使用，实现同一视角的不同渲染效果。

---

## Camera 系统

### Camera 实体

Camera 也是基于 ECS 的组件：

```cpp
#include <filament/Camera.h>

// 创建 Camera 实体
Entity cameraEntity = EntityManager::get().create();
Camera* camera = engine->createCamera(cameraEntity);

// 设置投影
camera->setProjection(45.0, 16.0/9.0, 0.1, 100.0);

// 设置位置和朝向
camera->lookAt({0, 2, 5}, {0, 0, 0}, {0, 1, 0});
```

**完整 Camera API：**

```cpp
class Camera {
public:
    // 透视投影
    void setProjection(double fov,        // 垂直 FOV (度)
                      double aspect,      // 宽高比
                      double near,        // 近裁剪面
                      double far,         // 远裁剪面
                      Fov direction = Fov::VERTICAL);

    // 自定义透视投影矩阵
    void setCustomProjection(mat4 const& projection,
                            double near, double far);

    // 正交投影
    void setProjection(Projection projection,
                      double left, double right,
                      double bottom, double top,
                      double near, double far);

    // LookAt 变换
    void lookAt(math::float3 const& eye,      // 相机位置
               math::float3 const& center,    // 目标点
               math::float3 const& up);       // 上方向

    // 直接设置模型矩阵（世界空间位置）
    void setModelMatrix(mat4 const& modelMatrix);

    // 获取矩阵
    mat4 getProjectionMatrix() const;
    mat4 getModelMatrix() const;
    mat4 getViewMatrix() const;  // modelMatrix 的逆

    // 曝光设置（EV100）
    void setExposure(float aperture,      // 光圈 (f-stop)
                    float shutterSpeed,   // 快门速度 (1/s)
                    float sensitivity);   // ISO
    void setExposure(float ev100);        // 直接设置 EV100
};
```

---

## 投影类型

### 1. 透视投影 (Perspective)

模拟人眼/相机透视效果，远处物体看起来更小：

```cpp
// 标准透视投影
camera->setProjection(
    45.0,      // FOV: 45度垂直视野
    16.0/9.0,  // Aspect: 宽高比
    0.1,       // Near: 0.1米
    100.0      // Far: 100米
);

// 水平 FOV（对于超宽屏）
camera->setProjection(
    90.0, 21.0/9.0, 0.1, 100.0,
    Camera::Fov::HORIZONTAL
);
```

**投影矩阵（OpenGL风格）：**

```
       [ f/aspect   0         0              0        ]
P =    [   0        f         0              0        ]
       [   0        0    -(f+n)/(f-n)  -2fn/(f-n)     ]
       [   0        0        -1              0        ]

其中 f = 1/tan(fov/2), n = near, f = far
```

**应用场景：**
- 第一人称/第三人称游戏
- 建筑可视化
- 任何需要真实深度感的场景

### 2. 正交投影 (Orthographic)

无透视变形，平行线保持平行：

```cpp
camera->setProjection(
    Camera::Projection::ORTHO,
    -10.0, 10.0,   // left, right
    -10.0, 10.0,   // bottom, top
    0.0, 100.0     // near, far
);
```

**投影矩阵：**

```
       [ 2/(r-l)     0          0       -(r+l)/(r-l) ]
P =    [   0      2/(t-b)       0       -(t+b)/(t-b) ]
       [   0         0      -2/(f-n)    -(f+n)/(f-n) ]
       [   0         0          0             1      ]
```

**应用场景：**
- 2D 游戏/UI
- CAD/技术绘图
- 小地图/顶视图
- 阴影贴图渲染

---

## Camera 定位和朝向

### lookAt 方法

最常用的相机定位方式：

```cpp
// eye: 相机位置
// center: 观察目标
// up: 上方向（通常是 {0, 1, 0}）
camera->lookAt({5, 3, 5}, {0, 0, 0}, {0, 1, 0});
```

**内部计算：**

```cpp
// View 矩阵推导
float3 forward = normalize(eye - center);  // -Z 方向
float3 right = normalize(cross(up, forward));
float3 newUp = cross(forward, right);

mat4 view = mat4(
    right.x,   newUp.x,   forward.x,   0,
    right.y,   newUp.y,   forward.y,   0,
    right.z,   newUp.z,   forward.z,   0,
    -dot(right, eye), -dot(newUp, eye), -dot(forward, eye), 1
);
```

### 直接设置模型矩阵

对于复杂的相机控制（如第一人称控制器）：

```cpp
// 构建自定义变换矩阵
mat4 cameraTransform = mat4::translation(position) *
                       mat4::rotation(quaternion);
camera->setModelMatrix(cameraTransform);

// View 矩阵 = inverse(ModelMatrix)
mat4 viewMatrix = camera->getViewMatrix();
```

**第一人称相机示例：**

```cpp
class FPSCameraController {
    float3 position = {0, 1.6, 0};  // 眼睛高度
    float yaw = 0.0f;                // 水平旋转
    float pitch = 0.0f;              // 垂直旋转

    void update(Camera* camera, float deltaTime) {
        // 处理输入
        if (keyPressed(KEY_W)) position += getForward() * speed * deltaTime;
        if (keyPressed(KEY_S)) position -= getForward() * speed * deltaTime;
        if (keyPressed(KEY_A)) position -= getRight() * speed * deltaTime;
        if (keyPressed(KEY_D)) position += getRight() * speed * deltaTime;

        // 鼠标旋转
        yaw += mouseDeltaX * sensitivity;
        pitch = clamp(pitch + mouseDeltaY * sensitivity, -89.0f, 89.0f);

        // 更新相机
        float3 target = position + getForward();
        camera->lookAt(position, target, {0, 1, 0});
    }

    float3 getForward() const {
        return {
            cos(radians(pitch)) * sin(radians(yaw)),
            sin(radians(pitch)),
            cos(radians(pitch)) * cos(radians(yaw))
        };
    }

    float3 getRight() const {
        return normalize(cross(getForward(), {0, 1, 0}));
    }
};
```

---

## View 系统

### View 创建和配置

```cpp
#include <filament/View.h>

View* view = engine->createView();

// 关联 Scene 和 Camera
view->setScene(scene);
view->setCamera(camera);

// 设置视口
view->setViewport({0, 0, 1920, 1080});

// 渲染
renderer->render(view);
```

### Viewport 和 Scissor

**Viewport（视口）：**定义渲染输出的区域

```cpp
// 全屏渲染
view->setViewport({0, 0, width, height});

// 分屏渲染（左半屏）
view->setViewport({0, 0, width/2, height});

// 小窗口（右上角）
view->setViewport({width - 320, height - 240, 320, 240});
```

**Scissor（裁剪区）：**进一步限制渲染区域

```cpp
// 启用裁剪测试
view->setScissor({100, 100, 800, 600});

// 禁用裁剪
view->setScissor({0, 0, width, height});
```

**Viewport vs Scissor：**

```
+------------------------+  Screen
|                        |
|  +------------------+  |  Viewport (NDC 映射到这里)
|  |                  |  |
|  |  +----------+    |  |  Scissor (像素被丢弃)
|  |  |          |    |  |
|  |  |  Render  |    |  |
|  |  |   Area   |    |  |
|  |  +----------+    |  |
|  +------------------+  |
+------------------------+
```

### 多视图渲染

```cpp
// 分屏多人游戏
Camera* camera1 = engine->createCamera(entity1);
Camera* camera2 = engine->createCamera(entity2);

View* view1 = engine->createView();
view1->setCamera(camera1);
view1->setViewport({0, 0, width/2, height});

View* view2 = engine->createView();
view2->setCamera(camera2);
view2->setViewport({width/2, 0, width/2, height});

// 渲染两个视图
renderer->render(view1);
renderer->render(view2);
```

---

## 后处理配置

### View 后处理选项

```cpp
#include <filament/View.h>

// 获取后处理选项
View::DepthOfFieldOptions& dof = view->getDepthOfFieldOptions();
View::BloomOptions& bloom = view->getBloomOptions();
View::AmbientOcclusionOptions& ao = view->getAmbientOcclusionOptions();
View::TemporalAntiAliasingOptions& taa = view->getTemporalAntiAliasingOptions();

// 启用/禁用效果
view->setPostProcessingEnabled(true);
view->setAntiAliasing(View::AntiAliasing::FXAA);
view->setDithering(View::Dithering::TEMPORAL);
```

### 1. 抗锯齿 (Anti-Aliasing)

```cpp
// FXAA - 快速近似抗锯齿
view->setAntiAliasing(View::AntiAliasing::FXAA);

// TAA - 时间抗锯齿（更高质量但需要稳定帧率）
view->setAntiAliasing(View::AntiAliasing::TAA);
auto& taa = view->getTemporalAntiAliasingOptions();
taa.filterWidth = 1.0f;      // 滤波器宽度
taa.feedback = 0.12f;        // 历史帧反馈
taa.enabled = true;

// MSAA - 多重采样（仅部分后端支持）
view->setSampleCount(4);  // 4x MSAA
```

**效果对比：**

| 方法 | 质量 | 性能开销 | 运动伪影 | 移动端适用 |
|------|------|----------|----------|------------|
| 无   | 低   | 0%       | 无       | ✓          |
| FXAA | 中   | 5-10%    | 无       | ✓          |
| TAA  | 高   | 10-15%   | 可能有   | △          |
| MSAA | 高   | 20-40%   | 无       | ✗          |

### 2. 环境光遮蔽 (AO)

```cpp
auto& ao = view->getAmbientOcclusionOptions();
ao.enabled = true;
ao.radius = 0.3f;        // 采样半径（世界空间）
ao.power = 1.0f;         // AO 强度
ao.bias = 0.0005f;       // 深度偏移
ao.resolution = 0.5f;    // 分辨率（0.5 = 半分辨率）
ao.intensity = 1.0f;     // 最终强度
ao.quality = View::QualityLevel::HIGH;
```

**性能优化：**

```cpp
// 移动端低质量设置
ao.quality = View::QualityLevel::LOW;
ao.resolution = 0.5f;  // 半分辨率
ao.upsampling = View::QualityLevel::LOW;

// 桌面端高质量设置
ao.quality = View::QualityLevel::ULTRA;
ao.resolution = 1.0f;  // 全分辨率
ao.upsampling = View::QualityLevel::HIGH;
```

### 3. Bloom (辉光)

```cpp
auto& bloom = view->getBloomOptions();
bloom.enabled = true;
bloom.strength = 0.10f;       // 强度 (0-1)
bloom.resolution = 360;       // 分辨率（像素）
bloom.levels = 6;             // Mipmap 层数
bloom.blendMode = BloomOptions::BlendMode::ADD;
bloom.threshold = true;       // 启用阈值
bloom.highlight = 1000.0f;    // 高光限制
```

**Bloom 链：**

```
原始图像 -> 亮度提取 (threshold) -> Downsample (6 levels)
                                           ↓
最终图像 <- Upsample + Blend       <- Gaussian Blur
```

### 4. 景深 (Depth of Field)

```cpp
auto& dof = view->getDepthOfFieldOptions();
dof.enabled = true;
dof.focusDistance = 10.0f;    // 对焦距离（米）
dof.blurScale = 1.0f;         // 模糊强度
dof.maxApertureDiameter = 0.01f;  // 最大光圈直径
dof.filter = View::DepthOfFieldOptions::Filter::MEDIAN;
```

**模拟相机景深：**

```cpp
// 根据相机参数计算
float aperture = 2.8f;        // f-stop
float focalLength = 0.05f;    // 50mm
float focusDistance = 5.0f;   // 对焦 5 米处

dof.focusDistance = focusDistance;
dof.maxApertureDiameter = focalLength / aperture;
dof.enabled = true;
```

### 5. 色调映射和颜色分级

```cpp
#include <filament/ColorGrading.h>

// 创建色调映射配置
ColorGrading* colorGrading = ColorGrading::Builder()
    .toneMapping(ColorGrading::ToneMapping::ACES)
    .exposure(0.0f)         // EV 偏移
    .contrast(1.0f)         // 对比度
    .saturation(1.0f)       // 饱和度
    .vibrance(1.0f)         // 自然饱和度
    .temperature(0.0f)      // 色温 (-1 到 1)
    .tint(0.0f)            // 色调
    .build(*engine);

view->setColorGrading(colorGrading);
```

**色调映射算法：**

| 算法         | 特点                     | 适用场景      |
|--------------|--------------------------|---------------|
| LINEAR       | 无映射，直接截断         | 调试          |
| ACES         | 电影级，高对比度         | 游戏/电影     |
| FILMIC       | 柔和，保留高光细节       | 室外场景      |
| GENERIC      | 可调参数                 | 自定义        |

---

## 曝光控制

### EV100 曝光系统

Filament 使用 EV100（ISO 100 的曝光值）系统：

```cpp
// 方法 1: 通过相机参数
camera->setExposure(
    16.0f,      // 光圈 f/16
    1.0/125.0,  // 快门速度 1/125s
    100.0f      // ISO 100
);

// 方法 2: 直接设置 EV100
camera->setExposure(15.0f);  // EV100 = 15

// EV100 计算公式
float ev100 = log2((aperture * aperture) / shutterSpeed * 100.0f / iso);
```

**常见场景 EV 值：**

| 场景                 | EV100 |
|----------------------|-------|
| 星空                 | -5    |
| 夜间街道             | 5     |
| 室内                 | 8     |
| 阴天室外             | 12    |
| 晴天阴影             | 14    |
| 晴天阳光直射         | 16    |

### 自动曝光

```cpp
view->setDynamicResolutionOptions({
    .enabled = true,
    .homogeneousScaling = true,
    .minScale = 0.5f,
    .maxScale = 1.0f,
    .quality = View::QualityLevel::MEDIUM
});

// 自动曝光（通过 ColorGrading）
ColorGrading* grading = ColorGrading::Builder()
    .exposure(autoExposureValue)  // 动态调整
    .build(*engine);
```

---

## RenderTarget 和离屏渲染

### 渲染到纹理

```cpp
#include <filament/RenderTarget.h>

// 1. 创建颜色纹理
Texture* colorTexture = Texture::Builder()
    .width(1024).height(1024)
    .levels(1)
    .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)
    .format(Texture::InternalFormat::RGBA8)
    .build(*engine);

// 2. 创建深度纹理
Texture* depthTexture = Texture::Builder()
    .width(1024).height(1024)
    .levels(1)
    .usage(Texture::Usage::DEPTH_ATTACHMENT)
    .format(Texture::InternalFormat::DEPTH24)
    .build(*engine);

// 3. 创建 RenderTarget
RenderTarget* rt = RenderTarget::Builder()
    .texture(RenderTarget::AttachmentPoint::COLOR, colorTexture)
    .texture(RenderTarget::AttachmentPoint::DEPTH, depthTexture)
    .build(*engine);

// 4. 关联到 View
view->setRenderTarget(rt);
view->setViewport({0, 0, 1024, 1024});

// 5. 渲染
renderer->render(view);

// 6. 使用渲染结果（作为纹理）
material->setParameter("reflectionMap", colorTexture, sampler);
```

### 多渲染目标 (MRT)

```cpp
// 创建多个颜色附件
Texture* albedoTexture = createTexture(RGBA8);
Texture* normalTexture = createTexture(RGBA16F);
Texture* materialTexture = createTexture(RGBA8);

RenderTarget* gBufferRT = RenderTarget::Builder()
    .texture(RenderTarget::AttachmentPoint::COLOR0, albedoTexture)
    .texture(RenderTarget::AttachmentPoint::COLOR1, normalTexture)
    .texture(RenderTarget::AttachmentPoint::COLOR2, materialTexture)
    .texture(RenderTarget::AttachmentPoint::DEPTH, depthTexture)
    .build(*engine);

// 在材质中输出到多个目标
// (材质代码)
// layout(location = 0) out vec4 outAlbedo;
// layout(location = 1) out vec4 outNormal;
// layout(location = 2) out vec4 outMaterial;
```

**应用场景：**
- 延迟渲染 G-Buffer
- 后处理效果链
- 阴影贴图生成
- 反射/折射探针

---

## 完整示例：可配置的渲染视图

```cpp
class RenderView {
public:
    RenderView(Engine* engine, Scene* scene)
        : engine(engine), scene(scene) {

        // 创建相机
        cameraEntity = EntityManager::get().create();
        camera = engine->createCamera(cameraEntity);
        camera->setProjection(45.0, 16.0/9.0, 0.1, 100.0);
        camera->lookAt({0, 2, 5}, {0, 0, 0}, {0, 1, 0});

        // 创建视图
        view = engine->createView();
        view->setScene(scene);
        view->setCamera(camera);
        view->setViewport({0, 0, 1920, 1080});

        // 配置后处理
        setupPostProcessing();
    }

    void setupPostProcessing() {
        view->setPostProcessingEnabled(true);

        // 抗锯齿
        view->setAntiAliasing(View::AntiAliasing::FXAA);

        // AO
        auto& ao = view->getAmbientOcclusionOptions();
        ao.enabled = true;
        ao.radius = 0.3f;
        ao.quality = View::QualityLevel::HIGH;

        // Bloom
        auto& bloom = view->getBloomOptions();
        bloom.enabled = true;
        bloom.strength = 0.10f;
        bloom.levels = 6;

        // 色调映射
        ColorGrading* grading = ColorGrading::Builder()
            .toneMapping(ColorGrading::ToneMapping::ACES)
            .exposure(0.0f)
            .build(*engine);
        view->setColorGrading(grading);
    }

    void resize(uint32_t width, uint32_t height) {
        view->setViewport({0, 0, width, height});
        camera->setProjection(45.0, double(width)/height, 0.1, 100.0);
    }

    void setQualityPreset(Quality quality) {
        switch (quality) {
            case Quality::LOW:
                view->setAntiAliasing(View::AntiAliasing::NONE);
                view->getAmbientOcclusionOptions().enabled = false;
                view->getBloomOptions().enabled = false;
                view->setSampleCount(1);
                break;

            case Quality::MEDIUM:
                view->setAntiAliasing(View::AntiAliasing::FXAA);
                view->getAmbientOcclusionOptions().enabled = true;
                view->getAmbientOcclusionOptions().quality = View::QualityLevel::MEDIUM;
                view->getBloomOptions().enabled = true;
                view->setSampleCount(1);
                break;

            case Quality::HIGH:
                view->setAntiAliasing(View::AntiAliasing::TAA);
                view->getAmbientOcclusionOptions().enabled = true;
                view->getAmbientOcclusionOptions().quality = View::QualityLevel::HIGH;
                view->getBloomOptions().enabled = true;
                view->getDepthOfFieldOptions().enabled = true;
                view->setSampleCount(1);
                break;

            case Quality::ULTRA:
                view->setAntiAliasing(View::AntiAliasing::TAA);
                view->getAmbientOcclusionOptions().enabled = true;
                view->getAmbientOcclusionOptions().quality = View::QualityLevel::ULTRA;
                view->getBloomOptions().enabled = true;
                view->getDepthOfFieldOptions().enabled = true;
                view->setSampleCount(4);  // MSAA
                break;
        }
    }

    void updateCamera(float deltaTime) {
        // 轨道相机示例
        static float angle = 0.0f;
        angle += deltaTime * 0.5f;

        float radius = 5.0f;
        float3 eye = {
            sin(angle) * radius,
            2.0f,
            cos(angle) * radius
        };

        camera->lookAt(eye, {0, 0, 0}, {0, 1, 0});
    }

    View* getView() const { return view; }
    Camera* getCamera() const { return camera; }

private:
    Engine* engine;
    Scene* scene;
    Entity cameraEntity;
    Camera* camera;
    View* view;

    enum class Quality { LOW, MEDIUM, HIGH, ULTRA };
};

// 使用
RenderView renderView(engine, scene);
renderView.setQualityPreset(RenderView::Quality::HIGH);

// 每帧更新
renderView.updateCamera(deltaTime);
renderer->render(renderView.getView());
```

---

## 性能考量

### 后处理性能开销

基于 1080p 分辨率的典型开销（移动端 GPU）：

| 效果            | 开销   | 优化建议                          |
|-----------------|--------|-----------------------------------|
| FXAA            | 0.5ms  | 默认启用                          |
| TAA             | 1.2ms  | 需要稳定帧率                      |
| SSAO (Low)      | 0.8ms  | 使用 0.5 分辨率                   |
| SSAO (High)     | 2.5ms  | 桌面端可用                        |
| Bloom           | 1.0ms  | 降低分辨率到 360p                 |
| DOF             | 1.5ms  | 可选效果                          |
| Color Grading   | 0.1ms  | 几乎无开销                        |

### 优化建议

```cpp
// 移动端优化配置
void setupMobileView(View* view) {
    // 禁用昂贵的后处理
    view->setPostProcessingEnabled(true);
    view->setAntiAliasing(View::AntiAliasing::FXAA);

    // 低质量 AO
    auto& ao = view->getAmbientOcclusionOptions();
    ao.enabled = true;
    ao.quality = View::QualityLevel::LOW;
    ao.resolution = 0.5f;  // 半分辨率

    // 简化 Bloom
    auto& bloom = view->getBloomOptions();
    bloom.enabled = true;
    bloom.resolution = 256;  // 降低分辨率
    bloom.levels = 5;        // 减少层数

    // 禁用 DOF
    view->getDepthOfFieldOptions().enabled = false;
}

// 桌面端高质量配置
void setupDesktopView(View* view) {
    view->setPostProcessingEnabled(true);
    view->setAntiAliasing(View::AntiAliasing::TAA);

    auto& ao = view->getAmbientOcclusionOptions();
    ao.enabled = true;
    ao.quality = View::QualityLevel::ULTRA;
    ao.resolution = 1.0f;

    auto& bloom = view->getBloomOptions();
    bloom.enabled = true;
    bloom.resolution = 512;
    bloom.levels = 6;

    auto& dof = view->getDepthOfFieldOptions();
    dof.enabled = true;
    dof.filter = View::DepthOfFieldOptions::Filter::MEDIAN;
}
```

---

## 总结

### Camera 和 View 职责

**Camera（数学变换）：**
- 投影矩阵（透视/正交）
- 视图矩阵（位置和朝向）
- 曝光参数（EV100）

**View（渲染配置）：**
- 关联 Camera 和 Scene
- Viewport/Scissor 设置
- 后处理效果配置
- RenderTarget 关联

### 关键流程

```
应用层：
    Camera.lookAt() / setProjection()
    View.setCamera() / setScene()
    View.配置后处理()

渲染时：
    Renderer.render(view)
        ↓
    View 提供：ViewMatrix, ProjectionMatrix
    Scene 提供：可见对象列表
    后处理效果应用
        ↓
    输出到 RenderTarget 或 SwapChain
```

### 相关文档

- **[01-core-concepts.md](./01-core-concepts.md)** - Engine/Scene/View 基础概念
- **[07-render-loop.md](./07-render-loop.md)** - 渲染循环中的 View 使用
- **[../graphics/02-lighting-theory.md](../graphics/02-lighting-theory.md)** - 曝光和色调映射理论
- **[../backend/08-platform-abstraction.md](../backend/08-platform-abstraction.md)** - RenderTarget 的后端实现

通过理解 Camera 和 View 系统，您可以完全控制 Filament 的渲染视角和效果配置，实现从简单的 3D 查看器到复杂的多视图渲染管线。
