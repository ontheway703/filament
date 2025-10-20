# 光照系统

## 概述

Filament 实现了基于物理的光照系统（Physically Based Rendering, PBR），支持多种光源类型和基于图像的光照（IBL）。光照系统通过 LightManager 组件管理，与 Scene 集成，提供真实感的渲染效果。

### 光照架构

```
+------------------+
|      Scene       |
+------------------+
         |
         | contains
         v
+------------------+       +------------------+
|  Light Entities  |------>|  LightManager    |
+------------------+       +------------------+
         |                          |
         |                          | manages
         v                          v
+------------------+       +------------------+
|  IndirectLight   |       | - Directional    |
|  (IBL/Skybox)    |       | - Point          |
+------------------+       | - Spot           |
                           | - Focused Spot   |
                           +------------------+
```

### 光照类型概览

| 光源类型          | 用途                | 阴影支持 | 性能开销 |
|-------------------|---------------------|----------|----------|
| Directional       | 太阳光/月光         | ✓        | 低       |
| Point             | 灯泡/火光           | ✗        | 中       |
| Spot              | 聚光灯              | ✓        | 中       |
| Focused Spot      | 投影仪/舞台灯       | ✓        | 中       |
| IBL (Indirect)    | 环境光/天空盒       | ✗        | 低       |

---

## 直接光照 (Direct Lighting)

### 1. 方向光 (Directional Light)

模拟无限远的平行光源（如太阳）：

```cpp
#include <filament/LightManager.h>

// 创建方向光实体
Entity sunEntity = EntityManager::get().create();

// 构建方向光
LightManager::Builder(LightManager::Type::DIRECTIONAL)
    .color({1.0f, 0.95f, 0.9f})        // 暖白色
    .intensity(100000.0f)               // 强度（lux）
    .direction({0.6, -1.0, -0.8})      // 光照方向
    .castShadows(true)                  // 启用阴影
    .sunAngularRadius(0.545f)          // 太阳角半径（度）
    .sunHaloSize(10.0f)                // 日晕大小
    .sunHaloFalloff(80.0f)             // 日晕衰减
    .build(*engine, sunEntity);

// 添加到场景
scene->addEntity(sunEntity);
```

**参数详解：**

- **intensity（强度）**：单位为 lux（勒克斯）
  - 晴天太阳：100,000 lux
  - 阴天：10,000 lux
  - 室内：300-500 lux
  - 月光：0.1 lux

- **direction（方向）**：光线传播方向（非从光源指向物体）
  ```cpp
  // 从上方照射（顶光）
  .direction({0, -1, 0})

  // 从侧面45度照射
  .direction({0.707, -0.707, 0})
  ```

- **sunAngularRadius（太阳角半径）**：
  - 真实太阳：0.545°
  - 更大的值产生更柔和的阴影边缘
  - 影响 PCF 阴影的柔和度

**时间/季节模拟：**

```cpp
class SunController {
    float timeOfDay = 12.0f;  // 0-24 小时

    void updateSun(Entity sunEntity, float time) {
        timeOfDay = fmod(time, 24.0f);

        // 计算太阳角度
        float sunAngle = (timeOfDay - 6.0f) / 12.0f * M_PI;
        float3 sunDir = {
            0.0f,
            -sin(sunAngle),    // 高度
            -cos(sunAngle)     // 方向
        };

        // 计算颜色（日出/日落偏红）
        float3 sunColor;
        if (timeOfDay < 6.0f || timeOfDay > 18.0f) {
            sunColor = {0.1f, 0.1f, 0.2f};  // 夜晚蓝色
        } else if (timeOfDay < 7.0f || timeOfDay > 17.0f) {
            sunColor = {1.0f, 0.6f, 0.3f};  // 日出/日落橙红色
        } else {
            sunColor = {1.0f, 0.95f, 0.9f}; // 白天暖白色
        }

        // 计算强度
        float intensity = max(0.0f, sin(sunAngle)) * 100000.0f;

        // 更新光源
        auto& lcm = engine->getLightManager();
        lcm.setDirection(lcm.getInstance(sunEntity), sunDir);
        lcm.setColor(lcm.getInstance(sunEntity), sunColor);
        lcm.setIntensity(lcm.getInstance(sunEntity), intensity);
    }
};
```

### 2. 点光源 (Point Light)

全方向发光的点状光源（如灯泡）：

```cpp
Entity lightEntity = EntityManager::get().create();

LightManager::Builder(LightManager::Type::POINT)
    .color({1.0f, 0.8f, 0.6f})         // 暖黄色
    .intensity(100000.0f)               // 强度（candela）
    .position({0, 2, 0})               // 位置
    .falloff(10.0f)                    // 衰减半径
    .build(*engine, lightEntity);

scene->addEntity(lightEntity);
```

**强度单位：**
- 点光源/聚光灯使用 **candela（坎德拉，cd）** 或 **lumen（流明，lm）**
- Filament 使用 candela 作为默认单位
- 转换：lumen = candela × 4π（对于均匀发光的球体）

**常见光源强度：**

| 光源类型         | 强度 (lumen) | 强度 (candela) |
|------------------|--------------|----------------|
| 蜡烛             | 12           | ~1             |
| 60W 白炽灯       | 800          | ~64            |
| 100W 白炽灯      | 1600         | ~127           |
| LED 灯泡         | 800-1600     | ~64-127        |
| 汽车大灯         | 1500-3000    | ~120-240       |

**衰减模型：**

Filament 使用物理正确的平方反比衰减 + 截止半径：

```cpp
// 光照强度计算
float distance = length(lightPosition - fragmentPosition);
float attenuation;

if (distance > falloffRadius) {
    attenuation = 0.0;  // 截止
} else {
    float distanceSquared = distance * distance;
    float falloffSquared = falloffRadius * falloffRadius;

    // 平方反比衰减，在边界处平滑过渡到 0
    float num = saturate(1.0 - pow(distance / falloffRadius, 4));
    attenuation = (num * num) / (distanceSquared + 1.0);
}
```

**动态点光源示例：**

```cpp
// 闪烁的火炬
class FlickeringTorch {
    Entity entity;
    float baseIntensity = 50000.0f;

    void update(float time) {
        // 使用 Perlin 噪声模拟闪烁
        float flicker = perlinNoise(time * 3.0f) * 0.3f + 0.7f;
        float intensity = baseIntensity * flicker;

        // 颜色也随之变化（橙红色）
        float3 color = {
            1.0f,
            0.5f + flicker * 0.3f,
            0.1f + flicker * 0.2f
        };

        auto& lcm = engine->getLightManager();
        auto instance = lcm.getInstance(entity);
        lcm.setIntensity(instance, intensity);
        lcm.setColor(instance, color);
    }
};
```

### 3. 聚光灯 (Spot Light)

锥形光束的光源：

```cpp
Entity spotEntity = EntityManager::get().create();

LightManager::Builder(LightManager::Type::SPOT)
    .color({1.0f, 1.0f, 1.0f})
    .intensity(100000.0f)
    .position({0, 5, 0})
    .direction({0, -1, 0})             // 照射方向
    .falloff(10.0f)                    // 距离衰减
    .spotLightCone(15.0f, 20.0f)       // 内角、外角（度）
    .castShadows(true)
    .build(*engine, spotEntity);

scene->addEntity(spotEntity);
```

**锥形参数：**

```
                Light Position
                      *
                     /|\
                    / | \
      innerAngle   /  |  \   outerAngle
                  /   |   \
                 /    |    \
                /_____|_____\

               [ Full ][ Fade ]
               Intensity   to 0
```

- **innerAngle（内锥角）**：全强度区域
- **outerAngle（外锥角）**：衰减到 0 的边界
- 两者之间平滑插值

**聚光灯衰减计算：**

```cpp
// 角度衰减
float cosOuter = cos(radians(outerAngle));
float cosInner = cos(radians(innerAngle));
float cosTheta = dot(normalize(lightDir), normalize(lightToFragment));

float angularAttenuation = saturate((cosTheta - cosOuter) / (cosInner - cosOuter));
angularAttenuation = angularAttenuation * angularAttenuation;

// 总衰减 = 距离衰减 × 角度衰减
float totalAttenuation = distanceAttenuation * angularAttenuation;
```

### 4. 聚焦聚光灯 (Focused Spot Light)

带有 IES 配置文件或纹理投影的聚光灯：

```cpp
// 纹理投影（例如窗户投影）
Texture* cookieTexture = loadTexture("window_cookie.png");

LightManager::Builder(LightManager::Type::FOCUSED_SPOT)
    .color({1.0f, 1.0f, 1.0f})
    .intensity(100000.0f)
    .position({0, 5, 0})
    .direction({0, -1, 0})
    .falloff(15.0f)
    .spotLightCone(30.0f, 35.0f)
    .castShadows(true)
    // 投影纹理
    .build(*engine, spotEntity);

// 通过材质参数设置（需要自定义材质）
// material->setParameter("lightCookie", cookieTexture, sampler);
```

**应用场景：**
- 窗户光斑投影
- 舞台灯光图案
- 幻灯机效果
- IES 光度学配置

---

## 间接光照 (Indirect Lighting / IBL)

### IndirectLight 概念

基于图像的光照（IBL）模拟环境光和反射：

```cpp
#include <filament/IndirectLight.h>

// 从预处理的 IBL 资源创建
IndirectLight* ibl = IndirectLight::Builder()
    .reflections(reflectionMap)       // 镜面反射贴图
    .irradiance(3, irradianceSH)      // 漫反射球谐系数
    .intensity(30000.0f)              // 强度（lux）
    .rotation(rotationMatrix)         // 旋转环境
    .build(*engine);

scene->setIndirectLight(ibl);
```

### IBL 资源生成

使用 `cmgen` 工具预处理环境贴图：

```bash
# 从 HDR 环境贴图生成 IBL
cmgen -x . --format=ktx --size=256 --extract-blur=0.1 environment.exr

# 输出文件：
# - environment_ibl.ktx       (镜面反射 cubemap)
# - environment_skybox.ktx    (天空盒 cubemap)
# - environment.txt           (漫反射球谐系数)
```

**加载 IBL 资源：**

```cpp
#include <ktxreader/Ktx1Reader.h>
#include <ktxreader/Ktx2Reader.h>

class IBLLoader {
public:
    struct IBLData {
        Texture* reflectionMap = nullptr;
        Texture* skyboxMap = nullptr;
        std::array<float3, 9> irradianceSH;
    };

    static IBLData loadIBL(Engine* engine, const char* prefix) {
        IBLData data;

        // 1. 加载反射贴图
        std::string reflPath = std::string(prefix) + "_ibl.ktx";
        data.reflectionMap = loadKtxCubemap(engine, reflPath.c_str());

        // 2. 加载天空盒
        std::string skyPath = std::string(prefix) + "_skybox.ktx";
        data.skyboxMap = loadKtxCubemap(engine, skyPath.c_str());

        // 3. 加载球谐系数
        std::string shPath = std::string(prefix) + ".txt";
        data.irradianceSH = loadSphericalHarmonics(shPath.c_str());

        return data;
    }

private:
    static Texture* loadKtxCubemap(Engine* engine, const char* path) {
        std::ifstream file(path, std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());

        // KTX1/KTX2 解析
        using namespace image;
        LinearImage image = ktx::createTexture(
            data.data(), data.size(),
            ktx::TextureFormat::COMPRESSED
        );

        // 创建 Filament Texture
        Texture* texture = Texture::Builder()
            .width(image.getWidth())
            .height(image.getHeight())
            .levels(image.getLevels())
            .format(Texture::InternalFormat::RGBA8)
            .sampler(Texture::Sampler::SAMPLER_CUBEMAP)
            .build(*engine);

        // 上传数据
        for (uint32_t level = 0; level < image.getLevels(); ++level) {
            for (uint32_t face = 0; face < 6; ++face) {
                Texture::FaceOffsets offsets(face);
                texture->setImage(*engine, level, std::move(offsets),
                                 image.getData(level, face));
            }
        }

        return texture;
    }

    static std::array<float3, 9> loadSphericalHarmonics(const char* path) {
        std::array<float3, 9> sh;
        std::ifstream file(path);

        // 读取 cmgen 生成的 SH 系数文件
        for (int i = 0; i < 9; ++i) {
            file >> sh[i].x >> sh[i].y >> sh[i].z;
        }

        return sh;
    }
};

// 使用
IBLLoader::IBLData iblData = IBLLoader::loadIBL(engine, "environment");

IndirectLight* ibl = IndirectLight::Builder()
    .reflections(iblData.reflectionMap)
    .irradiance(3, iblData.irradianceSH.data())
    .intensity(30000.0f)
    .build(*engine);

scene->setIndirectLight(ibl);
```

### 天空盒 (Skybox)

天空盒提供背景渲染：

```cpp
#include <filament/Skybox.h>

Skybox* skybox = Skybox::Builder()
    .environment(iblData.skyboxMap)  // 使用 IBL 的天空盒贴图
    .showSun(true)                   // 绘制太阳
    .build(*engine);

scene->setSkybox(skybox);
```

**程序化天空盒：**

```cpp
// 使用颜色创建简单天空盒
Skybox* gradientSky = Skybox::Builder()
    .color({0.5f, 0.7f, 1.0f, 1.0f})  // 天蓝色
    .build(*engine);

scene->setSkybox(gradientSky);
```

---

## 球谐光照 (Spherical Harmonics)

### SH 原理

球谐函数（Spherical Harmonics, SH）是球面上的一组正交基函数，用于高效表示漫反射环境光照。

**SH 系数：**

```cpp
// Filament 使用 3 阶 SH（9 个系数）
struct IrradianceSH {
    float3 sh[9];  // RGB × 9 系数
};

// 计算漫反射光照
float3 evaluateSH(float3 normal, const float3 sh[9]) {
    // SH 基函数（前 3 阶）
    float Y[9];
    Y[0] = 0.282095f;  // band 0
    Y[1] = 0.488603f * normal.y;
    Y[2] = 0.488603f * normal.z;
    Y[3] = 0.488603f * normal.x;
    Y[4] = 1.092548f * normal.x * normal.y;
    Y[5] = 1.092548f * normal.y * normal.z;
    Y[6] = 0.315392f * (3.0f * normal.z * normal.z - 1.0f);
    Y[7] = 1.092548f * normal.x * normal.z;
    Y[8] = 0.546274f * (normal.x * normal.x - normal.y * normal.y);

    // 重建光照
    float3 irradiance = float3(0);
    for (int i = 0; i < 9; ++i) {
        irradiance += sh[i] * Y[i];
    }
    return max(irradiance, 0.0f);
}
```

**优势：**
- 高效：仅 9 个 float3 系数（108 字节）
- 实时计算：每像素仅需几次乘加
- 低频精度高：适合漫反射

---

## 阴影系统

### 阴影配置

```cpp
// 1. 光源启用阴影
LightManager::Builder(LightManager::Type::DIRECTIONAL)
    .castShadows(true)
    .shadowOptions(shadowOptions)  // 可选自定义
    .build(*engine, sunEntity);

// 2. View 配置阴影
view->setShadowType(View::ShadowType::PCF);  // PCF/VSM

// 3. Renderable 配置
RenderableManager::Builder(1)
    .castShadows(true)         // 投射阴影
    .receiveShadows(true)      // 接收阴影
    .build(*engine, entity);
```

### 阴影选项

```cpp
#include <filament/LightManager.h>

LightManager::ShadowOptions shadowOpts;

// CSM 级联设置（方向光）
shadowOpts.mapSize = 2048;             // 阴影贴图分辨率
shadowOpts.cascades = 4;               // 级联数量
shadowOpts.splitPositions[0] = 0.05f;  // 级联分割位置
shadowOpts.splitPositions[1] = 0.15f;
shadowOpts.splitPositions[2] = 0.50f;
shadowOpts.splitPositions[3] = 1.00f;

// VSM 特定选项
shadowOpts.vsm.msaaSamples = 4;
shadowOpts.vsm.anisotropy = 8;
shadowOpts.vsm.blurWidth = 11;

// Bias 设置
shadowOpts.constantBias = 0.001f;      // 常量偏移
shadowOpts.normalBias = 1.0f;          // 法线偏移

LightManager::Builder(LightManager::Type::DIRECTIONAL)
    .castShadows(true)
    .shadowOptions(shadowOpts)
    .build(*engine, sunEntity);
```

### 级联阴影贴图 (CSM)

方向光使用 CSM 技术提供大范围高质量阴影：

```
        Camera Frustum
         /|        |\
        / |        | \
       /  |        |  \
      /   |        |   \
     /----|--------|----\
    [  0  |   1    | 2  ] 3    <- Cascades

    近处高分辨率 → 远处低分辨率
```

**CSM 调试可视化：**

```cpp
// 启用级联可视化（调试）
view->setDebugCamera(camera);
view->setShadowType(View::ShadowType::PCF);

// 材质中可以访问级联索引（需自定义材质）
// uint cascade = getCascadeIndex(fragCoord);
// 用不同颜色显示不同级联
```

### 阴影技术对比

| 技术     | 质量 | 性能 | 内存  | 特点                          |
|----------|------|------|-------|-------------------------------|
| PCF      | 中   | 快   | 低    | 硬阴影边缘，适合移动端        |
| VSM      | 高   | 中   | 高    | 柔和阴影，可预过滤            |
| PCSS     | 极高 | 慢   | 中    | 接触硬化阴影（未来可能支持）  |

---

## 光照管理和优化

### 动态光源管理

```cpp
class LightManager {
    std::vector<Entity> lights;
    Engine* engine;

    Entity addPointLight(float3 position, float3 color, float intensity) {
        Entity light = EntityManager::get().create();

        filament::LightManager::Builder(filament::LightManager::Type::POINT)
            .color(color)
            .intensity(intensity)
            .position(position)
            .falloff(10.0f)
            .build(*engine, light);

        scene->addEntity(light);
        lights.push_back(light);
        return light;
    }

    void removeLight(Entity light) {
        scene->remove(light);
        engine->destroy(light);
        lights.erase(std::remove(lights.begin(), lights.end(), light));
    }

    void updateLightPosition(Entity light, float3 newPosition) {
        auto& lcm = engine->getLightManager();
        auto instance = lcm.getInstance(light);
        lcm.setPosition(instance, newPosition);
    }
};
```

### 光源裁剪

Filament 自动执行视锥裁剪和距离裁剪：

```cpp
// 光源自动裁剪基于：
// 1. 视锥可见性
// 2. falloff 半径
// 3. 对最终图像的贡献

// 强制限制光源数量（性能优化）
const uint32_t MAX_LIGHTS = 256;  // Filament 默认上限
```

### 性能考量

**光源数量建议：**

| 平台       | 方向光 | 点光源 + 聚光灯 | 备注              |
|------------|--------|-----------------|-------------------|
| 移动端     | 1      | 4-8             | 依赖 GPU 性能     |
| 桌面端     | 1-2    | 16-32           | 现代 GPU          |
| 高端 PC    | 2-4    | 64-256          | 延迟渲染更高效    |

**优化建议：**

```cpp
// 1. 使用 falloff 限制光源范围
pointLight.falloff(5.0f);  // 而非 100.0f

// 2. 动态调整光源数量
void updateLights(float cameraDistance) {
    for (auto& light : distantLights) {
        if (distance(light.position, cameraPos) > threshold) {
            scene->remove(light.entity);  // 暂时移除
        }
    }
}

// 3. LOD 光源
// 远处使用 IBL 近似
// 近处使用详细点光源
```

---

## 完整示例：光照场景设置

```cpp
class SceneLighting {
public:
    SceneLighting(Engine* engine, Scene* scene)
        : engine(engine), scene(scene) {}

    void setupOutdoorLighting() {
        // 1. 太阳光（方向光）
        Entity sun = EntityManager::get().create();
        LightManager::Builder(LightManager::Type::DIRECTIONAL)
            .color({1.0f, 0.95f, 0.9f})
            .intensity(100000.0f)
            .direction({0.6, -1.0, -0.8})
            .castShadows(true)
            .sunAngularRadius(0.545f)
            .build(*engine, sun);
        scene->addEntity(sun);

        // 2. IBL（环境光）
        auto iblData = IBLLoader::loadIBL(engine, "outdoor");
        IndirectLight* ibl = IndirectLight::Builder()
            .reflections(iblData.reflectionMap)
            .irradiance(3, iblData.irradianceSH.data())
            .intensity(30000.0f)
            .build(*engine);
        scene->setIndirectLight(ibl);

        // 3. 天空盒
        Skybox* skybox = Skybox::Builder()
            .environment(iblData.skyboxMap)
            .showSun(true)
            .build(*engine);
        scene->setSkybox(skybox);
    }

    void setupIndoorLighting() {
        // 1. 主光源（窗户方向光，较弱）
        Entity windowLight = EntityManager::get().create();
        LightManager::Builder(LightManager::Type::DIRECTIONAL)
            .color({0.9f, 0.95f, 1.0f})  // 冷色调
            .intensity(5000.0f)          // 比室外弱
            .direction({1.0, -0.5, 0})
            .castShadows(true)
            .build(*engine, windowLight);
        scene->addEntity(windowLight);

        // 2. 室内 IBL（较暗）
        auto iblData = IBLLoader::loadIBL(engine, "indoor");
        IndirectLight* ibl = IndirectLight::Builder()
            .reflections(iblData.reflectionMap)
            .irradiance(3, iblData.irradianceSH.data())
            .intensity(3000.0f)  // 低强度
            .build(*engine);
        scene->setIndirectLight(ibl);

        // 3. 点光源（灯具）
        addCeilingLight({0, 3, 0});
        addCeilingLight({5, 3, 0});
        addCeilingLight({-5, 3, 0});

        // 4. 装饰光源
        addTableLamp({2, 1, 2});
    }

    void setupNightLighting() {
        // 1. 月光（方向光，冷色调）
        Entity moon = EntityManager::get().create();
        LightManager::Builder(LightManager::Type::DIRECTIONAL)
            .color({0.5f, 0.6f, 0.8f})
            .intensity(1000.0f)  // 很弱
            .direction({0.3, -0.8, 0.5})
            .castShadows(true)
            .build(*engine, moon);
        scene->addEntity(moon);

        // 2. 夜晚 IBL
        auto iblData = IBLLoader::loadIBL(engine, "night");
        IndirectLight* ibl = IndirectLight::Builder()
            .reflections(iblData.reflectionMap)
            .irradiance(3, iblData.irradianceSH.data())
            .intensity(500.0f)
            .build(*engine);
        scene->setIndirectLight(ibl);

        // 3. 人工光源
        addStreetLight({10, 5, 0});
        addStreetLight({-10, 5, 0});
    }

private:
    void addCeilingLight(float3 position) {
        Entity light = EntityManager::get().create();
        LightManager::Builder(LightManager::Type::POINT)
            .color({1.0f, 0.9f, 0.8f})
            .intensity(20000.0f)
            .position(position)
            .falloff(8.0f)
            .build(*engine, light);
        scene->addEntity(light);
    }

    void addTableLamp(float3 position) {
        Entity light = EntityManager::get().create();
        LightManager::Builder(LightManager::Type::SPOT)
            .color({1.0f, 0.85f, 0.7f})
            .intensity(10000.0f)
            .position(position)
            .direction({0, 1, 0})
            .falloff(5.0f)
            .spotLightCone(30.0f, 40.0f)
            .build(*engine, light);
        scene->addEntity(light);
    }

    void addStreetLight(float3 position) {
        Entity light = EntityManager::get().create();
        LightManager::Builder(LightManager::Type::SPOT)
            .color({1.0f, 0.8f, 0.5f})
            .intensity(50000.0f)
            .position(position)
            .direction({0, -1, 0})
            .falloff(15.0f)
            .spotLightCone(45.0f, 60.0f)
            .castShadows(true)
            .build(*engine, light);
        scene->addEntity(light);
    }

    Engine* engine;
    Scene* scene;
};

// 使用
SceneLighting lighting(engine, scene);
lighting.setupOutdoorLighting();
```

---

## 总结

### 光照流程

```
应用层：
    LightManager::Builder 创建光源
    Scene.addEntity(lightEntity)
    IndirectLight + Skybox 设置

渲染时：
    Filament 自动光源裁剪
    LightManager 提供光源数据
    Shader 执行 PBR 计算：
        - Direct Lighting (点/方向/聚光灯)
        - Indirect Lighting (IBL)
        - Shadows (CSM/VSM)
        ↓
    最终着色输出
```

### 关键要点

1. **物理单位**：使用真实光照单位（lux, candela）
2. **IBL 重要性**：基于图像的光照提供真实感
3. **阴影配置**：CSM 级联和偏移需仔细调整
4. **性能平衡**：光源数量 vs 视觉质量

### 相关文档

- **[01-core-concepts.md](./01-core-concepts.md)** - Scene 和 Entity 基础
- **[02-entity-component.md](./02-entity-component.md)** - LightManager 组件
- **[03-resource-management.md](./03-resource-management.md)** - Texture 和 IBL 资源
- **[../graphics/02-lighting-theory.md](../graphics/02-lighting-theory.md)** - PBR 光照理论
- **[../graphics/03-pbr-materials.md](../graphics/03-pbr-materials.md)** - 材质与光照交互

通过合理配置光照系统，您可以创建从白天到夜晚、从室内到室外的各种真实感场景。
