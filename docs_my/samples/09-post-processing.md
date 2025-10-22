# 09 - Post Processing (后处理效果实现)

## 概述

本示例展示如何在 Filament 中实现后处理效果。后处理是在 3D 场景渲染完成后,对最终图像进行处理以增强视觉效果的技术。我们将实现 Bloom 泛光、SSAO 环境光遮蔽、色调映射、景深、运动模糊等常见效果。

### 功能特性

- **Bloom 泛光** - 高亮区域发光效果
- **SSAO** - 屏幕空间环境光遮蔽
- **色调映射** - HDR 到 LDR 的映射
- **景深 (DOF)** - 模拟相机焦距效果
- **运动模糊** - 快速移动物体模糊
- **色彩分级** - 调整图像色调和对比度
- **抗锯齿 (TAA)** - 时域抗锯齿
- **自定义后处理** - 实现自定义着色器效果

### 适用场景

- AAA 游戏视觉效果
- 电影级渲染
- 建筑可视化
- 产品展示
- 艺术创作

### 技术要点

- 理解渲染管线和 RenderTarget
- 掌握全屏四边形渲染
- 实现多 Pass 后处理链
- 优化性能和内存使用
- 处理 HDR 和 LDR 转换

---

## 完整实现 (C++)

### 1. 后处理基础框架

**文件：`samples/post_processing/PostProcessPipeline.h`**

```cpp
/*
 * 后处理管线
 *
 * 管理多个后处理效果的执行顺序和中间纹理
 */

#pragma once

#include <filament/Engine.h>
#include <filament/View.h>
#include <filament/Scene.h>
#include <filament/RenderTarget.h>
#include <filament/Texture.h>
#include <filament/Material.h>
#include <filament/Camera.h>

#include <vector>
#include <memory>

using namespace filament;

// 后处理效果基类
class PostProcessEffect {
public:
    virtual ~PostProcessEffect() = default;

    // 初始化效果所需资源
    virtual void initialize(Engine* engine, uint32_t width, uint32_t height) = 0;

    // 执行后处理
    // inputTexture: 输入纹理
    // outputTarget: 输出渲染目标
    virtual void render(Engine* engine, View* view,
                       Texture* inputTexture,
                       RenderTarget* outputTarget) = 0;

    // 窗口大小改变时调用
    virtual void resize(uint32_t width, uint32_t height) = 0;

    // 清理资源
    virtual void cleanup(Engine* engine) = 0;

    // 效果开关
    void setEnabled(bool enabled) { mEnabled = enabled; }
    bool isEnabled() const { return mEnabled; }

protected:
    bool mEnabled = true;
};

// 后处理管线
class PostProcessPipeline {
public:
    PostProcessPipeline(Engine* engine, uint32_t width, uint32_t height)
        : mEngine(engine)
        , mWidth(width)
        , mHeight(height)
    {
        initialize();
    }

    ~PostProcessPipeline() {
        cleanup();
    }

    // 添加后处理效果
    void addEffect(std::unique_ptr<PostProcessEffect> effect) {
        effect->initialize(mEngine, mWidth, mHeight);
        mEffects.push_back(std::move(effect));
    }

    // 执行后处理管线
    void render(View* view, Texture* sceneTexture) {
        if (mEffects.empty()) return;

        Texture* currentInput = sceneTexture;
        RenderTarget* currentOutput = nullptr;

        for (size_t i = 0; i < mEffects.size(); ++i) {
            auto& effect = mEffects[i];

            if (!effect->isEnabled()) continue;

            // 最后一个效果输出到默认帧缓冲
            if (i == mEffects.size() - 1) {
                currentOutput = nullptr;  // 默认帧缓冲
            } else {
                // 使用 ping-pong 缓冲
                currentOutput = (i % 2 == 0) ? mPingTarget : mPongTarget;
            }

            // 执行效果
            effect->render(mEngine, view, currentInput, currentOutput);

            // 下一个效果的输入是当前效果的输出
            if (currentOutput == mPingTarget) {
                currentInput = mPingTexture;
            } else if (currentOutput == mPongTarget) {
                currentInput = mPongTexture;
            }
        }
    }

    // 窗口大小改变
    void resize(uint32_t width, uint32_t height) {
        mWidth = width;
        mHeight = height;

        // 重新创建中间纹理
        cleanup();
        initialize();

        // 通知所有效果
        for (auto& effect : mEffects) {
            effect->resize(width, height);
        }
    }

private:
    void initialize() {
        // 创建 ping-pong 纹理用于多 Pass 渲染
        mPingTexture = Texture::Builder()
            .width(mWidth)
            .height(mHeight)
            .levels(1)
            .format(Texture::InternalFormat::RGBA16F)  // HDR 格式
            .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)
            .build(*mEngine);

        mPongTexture = Texture::Builder()
            .width(mWidth)
            .height(mHeight)
            .levels(1)
            .format(Texture::InternalFormat::RGBA16F)
            .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)
            .build(*mEngine);

        // 创建渲染目标
        mPingTarget = RenderTarget::Builder()
            .texture(RenderTarget::AttachmentPoint::COLOR, mPingTexture)
            .build(*mEngine);

        mPongTarget = RenderTarget::Builder()
            .texture(RenderTarget::AttachmentPoint::COLOR, mPongTexture)
            .build(*mEngine);
    }

    void cleanup() {
        if (mPingTarget) mEngine->destroy(mPingTarget);
        if (mPongTarget) mEngine->destroy(mPongTarget);
        if (mPingTexture) mEngine->destroy(mPingTexture);
        if (mPongTexture) mEngine->destroy(mPongTexture);

        for (auto& effect : mEffects) {
            effect->cleanup(mEngine);
        }
    }

private:
    Engine* mEngine;
    uint32_t mWidth;
    uint32_t mHeight;

    // Ping-pong 缓冲
    Texture* mPingTexture = nullptr;
    Texture* mPongTexture = nullptr;
    RenderTarget* mPingTarget = nullptr;
    RenderTarget* mPongTarget = nullptr;

    // 后处理效果列表
    std::vector<std::unique_ptr<PostProcessEffect>> mEffects;
};
```

### 2. Bloom 泛光效果

**文件：`samples/post_processing/BloomEffect.h`**

```cpp
/*
 * Bloom 泛光效果
 *
 * 实现步骤：
 * 1. 提取亮区 (Bright Pass)
 * 2. 高斯模糊 (Gaussian Blur)
 * 3. 与原图混合
 */

#pragma once

#include "PostProcessPipeline.h"
#include <filament/MaterialInstance.h>

class BloomEffect : public PostProcessEffect {
public:
    BloomEffect(float threshold = 1.0f, float intensity = 1.0f)
        : mThreshold(threshold)
        , mIntensity(intensity)
    {}

    void initialize(Engine* engine, uint32_t width, uint32_t height) override {
        mEngine = engine;
        mWidth = width;
        mHeight = height;

        // 创建下采样链（每级缩小一半）
        const int numLevels = 5;
        for (int i = 0; i < numLevels; ++i) {
            uint32_t levelWidth = width >> (i + 1);
            uint32_t levelHeight = height >> (i + 1);

            // 创建纹理
            auto* texture = Texture::Builder()
                .width(levelWidth)
                .height(levelHeight)
                .levels(1)
                .format(Texture::InternalFormat::RGBA16F)
                .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)
                .build(*engine);

            mDownsampleTextures.push_back(texture);

            // 创建渲染目标
            auto* target = RenderTarget::Builder()
                .texture(RenderTarget::AttachmentPoint::COLOR, texture)
                .build(*engine);

            mDownsampleTargets.push_back(target);
        }

        // 加载材质
        loadMaterials(engine);

        // 创建全屏四边形
        createFullscreenQuad(engine);
    }

    void render(Engine* engine, View* view,
               Texture* inputTexture,
               RenderTarget* outputTarget) override {

        // 1. 提取亮区并下采样
        renderBrightPass(inputTexture, mDownsampleTargets[0]);

        // 2. 下采样金字塔
        for (size_t i = 1; i < mDownsampleTextures.size(); ++i) {
            renderDownsample(mDownsampleTextures[i-1], mDownsampleTargets[i]);
        }

        // 3. 上采样并模糊
        for (int i = mDownsampleTextures.size() - 2; i >= 0; --i) {
            renderUpsampleAndBlur(mDownsampleTextures[i+1],
                                 mDownsampleTargets[i]);
        }

        // 4. 与原图混合
        renderComposite(inputTexture, mDownsampleTextures[0], outputTarget);
    }

    void resize(uint32_t width, uint32_t height) override {
        mWidth = width;
        mHeight = height;

        // 清理旧资源
        cleanup(mEngine);

        // 重新初始化
        initialize(mEngine, width, height);
    }

    void cleanup(Engine* engine) override {
        for (auto* texture : mDownsampleTextures) {
            engine->destroy(texture);
        }
        for (auto* target : mDownsampleTargets) {
            engine->destroy(target);
        }

        mDownsampleTextures.clear();
        mDownsampleTargets.clear();

        // 清理材质和几何体
        if (mBrightPassMaterial) engine->destroy(mBrightPassMaterial);
        if (mBlurMaterial) engine->destroy(mBlurMaterial);
        if (mCompositeMaterial) engine->destroy(mCompositeMaterial);
        if (mFullscreenQuad) engine->destroy(mFullscreenQuad);
    }

    // 设置参数
    void setThreshold(float threshold) { mThreshold = threshold; }
    void setIntensity(float intensity) { mIntensity = intensity; }

private:
    void loadMaterials(Engine* engine) {
        // 加载预编译的材质
        // 实际应用中这些材质应该用 matc 编译

        // 亮度提取材质
        // mBrightPassMaterial = loadMaterial("bright_pass.filamat");

        // 模糊材质
        // mBlurMaterial = loadMaterial("gaussian_blur.filamat");

        // 合成材质
        // mCompositeMaterial = loadMaterial("bloom_composite.filamat");
    }

    void createFullscreenQuad(Engine* engine) {
        // 创建全屏四边形用于后处理
        // 简化实现 - 实际应该创建 VertexBuffer 和 IndexBuffer
    }

    void renderBrightPass(Texture* input, RenderTarget* output) {
        // 渲染亮度提取 Pass
        // 着色器伪代码：
        // vec3 color = texture(input, uv).rgb;
        // float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
        // if (brightness > threshold) {
        //     fragColor = vec4(color, 1.0);
        // } else {
        //     fragColor = vec4(0.0);
        // }
    }

    void renderDownsample(Texture* input, RenderTarget* output) {
        // 下采样并应用模糊
    }

    void renderUpsampleAndBlur(Texture* input, RenderTarget* output) {
        // 上采样并模糊
    }

    void renderComposite(Texture* sceneTexture, Texture* bloomTexture,
                        RenderTarget* output) {
        // 混合原图和 bloom
        // fragColor = sceneColor + bloomColor * intensity;
    }

private:
    Engine* mEngine = nullptr;
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;

    float mThreshold;
    float mIntensity;

    std::vector<Texture*> mDownsampleTextures;
    std::vector<RenderTarget*> mDownsampleTargets;

    Material* mBrightPassMaterial = nullptr;
    Material* mBlurMaterial = nullptr;
    Material* mCompositeMaterial = nullptr;

    Entity mFullscreenQuad;
};
```

### 3. SSAO 环境光遮蔽

**文件：`samples/post_processing/SSAOEffect.h`**

```cpp
/*
 * SSAO (Screen Space Ambient Occlusion)
 *
 * 屏幕空间环境光遮蔽效果
 */

#pragma once

#include "PostProcessPipeline.h"
#include <math/vec3.h>
#include <random>

using namespace filament::math;

class SSAOEffect : public PostProcessEffect {
public:
    SSAOEffect(int kernelSize = 64, float radius = 0.5f)
        : mKernelSize(kernelSize)
        , mRadius(radius)
    {}

    void initialize(Engine* engine, uint32_t width, uint32_t height) override {
        mEngine = engine;
        mWidth = width;
        mHeight = height;

        // 生成采样核心
        generateKernel();

        // 生成噪声纹理
        generateNoiseTexture(engine);

        // 创建 AO 纹理
        mAOTexture = Texture::Builder()
            .width(width)
            .height(height)
            .levels(1)
            .format(Texture::InternalFormat::R8)  // 单通道
            .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)
            .build(*engine);

        mAOTarget = RenderTarget::Builder()
            .texture(RenderTarget::AttachmentPoint::COLOR, mAOTexture)
            .build(*engine);

        // 创建模糊后的纹理（可选）
        mBlurredAOTexture = Texture::Builder()
            .width(width)
            .height(height)
            .levels(1)
            .format(Texture::InternalFormat::R8)
            .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)
            .build(*engine);

        mBlurTarget = RenderTarget::Builder()
            .texture(RenderTarget::AttachmentPoint::COLOR, mBlurredAOTexture)
            .build(*engine);

        // 加载材质
        // mSSAOMaterial = loadMaterial("ssao.filamat");
        // mBlurMaterial = loadMaterial("ssao_blur.filamat");
        // mApplyMaterial = loadMaterial("ssao_apply.filamat");
    }

    void render(Engine* engine, View* view,
               Texture* inputTexture,
               RenderTarget* outputTarget) override {

        // 需要深度缓冲和法线缓冲
        // Texture* depthTexture = getDepthTexture(view);
        // Texture* normalTexture = getNormalTexture(view);

        // 1. 计算 AO
        renderSSAO(/* depthTexture, normalTexture, */ mAOTarget);

        // 2. 模糊 AO（减少噪声）
        renderBlur(mAOTexture, mBlurTarget);

        // 3. 应用 AO 到场景
        renderApply(inputTexture, mBlurredAOTexture, outputTarget);
    }

    void resize(uint32_t width, uint32_t height) override {
        mWidth = width;
        mHeight = height;
        cleanup(mEngine);
        initialize(mEngine, width, height);
    }

    void cleanup(Engine* engine) override {
        if (mAOTexture) engine->destroy(mAOTexture);
        if (mAOTarget) engine->destroy(mAOTarget);
        if (mBlurredAOTexture) engine->destroy(mBlurredAOTexture);
        if (mBlurTarget) engine->destroy(mBlurTarget);
        if (mNoiseTexture) engine->destroy(mNoiseTexture);
    }

    // 参数设置
    void setRadius(float radius) { mRadius = radius; }
    void setBias(float bias) { mBias = bias; }
    void setPower(float power) { mPower = power; }

private:
    void generateKernel() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);

        mKernel.clear();
        for (int i = 0; i < mKernelSize; ++i) {
            float3 sample(
                randomFloats(gen) * 2.0f - 1.0f,
                randomFloats(gen) * 2.0f - 1.0f,
                randomFloats(gen)  // 半球
            );

            sample = normalize(sample);
            sample *= randomFloats(gen);

            // 让样本更集中在原点附近
            float scale = (float)i / (float)mKernelSize;
            scale = 0.1f + scale * scale * 0.9f;  // 插值
            sample *= scale;

            mKernel.push_back(sample);
        }
    }

    void generateNoiseTexture(Engine* engine) {
        // 4x4 噪声纹理用于旋转采样核心
        const int noiseSize = 4;
        std::vector<float3> noise;

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);

        for (int i = 0; i < noiseSize * noiseSize; ++i) {
            float3 n(
                randomFloats(gen) * 2.0f - 1.0f,
                randomFloats(gen) * 2.0f - 1.0f,
                0.0f  // Z 轴旋转
            );
            noise.push_back(n);
        }

        mNoiseTexture = Texture::Builder()
            .width(noiseSize)
            .height(noiseSize)
            .levels(1)
            .format(Texture::InternalFormat::RGB8)
            .build(*engine);

        Texture::PixelBufferDescriptor buffer(
            noise.data(), noise.size() * sizeof(float3),
            Texture::Format::RGB, Texture::Type::FLOAT);

        mNoiseTexture->setImage(*engine, 0, std::move(buffer));
    }

    void renderSSAO(RenderTarget* output) {
        // SSAO 着色器伪代码：
        /*
        // 获取片段位置和法线（视图空间）
        vec3 fragPos = getViewSpacePosition(uv, depth);
        vec3 normal = getViewSpaceNormal(uv);

        // 获取噪声用于旋转采样核心
        vec3 randomVec = texture(noiseTexture, uv * noiseScale).xyz;

        // 构建 TBN 矩阵
        vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
        vec3 bitangent = cross(normal, tangent);
        mat3 TBN = mat3(tangent, bitangent, normal);

        // 采样周围点
        float occlusion = 0.0;
        for(int i = 0; i < kernelSize; ++i) {
            vec3 samplePos = TBN * kernel[i];
            samplePos = fragPos + samplePos * radius;

            // 投影到屏幕空间
            vec4 offset = projection * vec4(samplePos, 1.0);
            offset.xyz /= offset.w;
            offset.xyz = offset.xyz * 0.5 + 0.5;

            // 获取该位置的深度
            float sampleDepth = getDepth(offset.xy);

            // 范围检查和遮蔽计算
            float rangeCheck = smoothstep(0.0, 1.0,
                radius / abs(fragPos.z - sampleDepth));
            occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
        }

        occlusion = 1.0 - (occlusion / kernelSize);
        fragColor = pow(occlusion, power);
        */
    }

    void renderBlur(Texture* input, RenderTarget* output) {
        // 双边模糊或高斯模糊
    }

    void renderApply(Texture* sceneTexture, Texture* aoTexture,
                    RenderTarget* output) {
        // fragColor = sceneColor * ao;
    }

private:
    Engine* mEngine = nullptr;
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;

    int mKernelSize;
    float mRadius;
    float mBias = 0.025f;
    float mPower = 1.0f;

    std::vector<float3> mKernel;
    Texture* mNoiseTexture = nullptr;

    Texture* mAOTexture = nullptr;
    RenderTarget* mAOTarget = nullptr;

    Texture* mBlurredAOTexture = nullptr;
    RenderTarget* mBlurTarget = nullptr;

    Material* mSSAOMaterial = nullptr;
    Material* mBlurMaterial = nullptr;
    Material* mApplyMaterial = nullptr;
};
```

### 4. 色调映射

**文件：`samples/post_processing/ToneMappingEffect.h`**

```cpp
/*
 * 色调映射效果
 *
 * 将 HDR 图像映射到 LDR 显示器
 */

#pragma once

#include "PostProcessPipeline.h"

class ToneMappingEffect : public PostProcessEffect {
public:
    enum class Operator {
        REINHARD,        // Reinhard
        FILMIC,          // Filmic (Uncharted 2)
        ACES,            // ACES
        LINEAR,          // 线性
        EXPOSURE         // 曝光调整
    };

    ToneMappingEffect(Operator op = Operator::ACES, float exposure = 1.0f)
        : mOperator(op)
        , mExposure(exposure)
    {}

    void initialize(Engine* engine, uint32_t width, uint32_t height) override {
        mEngine = engine;

        // 加载色调映射材质
        // mToneMappingMaterial = loadMaterial("tone_mapping.filamat");
    }

    void render(Engine* engine, View* view,
               Texture* inputTexture,
               RenderTarget* outputTarget) override {

        // 设置参数
        // setMaterialParameter("exposure", mExposure);
        // setMaterialParameter("operator", (int)mOperator);

        // 渲染全屏四边形
        // renderFullscreenQuad(inputTexture, outputTarget);
    }

    void resize(uint32_t width, uint32_t height) override {
        // 色调映射不需要中间纹理
    }

    void cleanup(Engine* engine) override {
        if (mToneMappingMaterial) {
            engine->destroy(mToneMappingMaterial);
        }
    }

    // 参数设置
    void setOperator(Operator op) { mOperator = op; }
    void setExposure(float exposure) { mExposure = exposure; }
    void setGamma(float gamma) { mGamma = gamma; }

    // 着色器代码（参考）
    static const char* getShaderCode() {
        return R"(
            // Reinhard 色调映射
            vec3 reinhardToneMapping(vec3 color) {
                return color / (color + vec3(1.0));
            }

            // Filmic 色调映射 (Uncharted 2)
            vec3 filmicToneMapping(vec3 color) {
                float A = 0.15;
                float B = 0.50;
                float C = 0.10;
                float D = 0.20;
                float E = 0.02;
                float F = 0.30;

                color = ((color * (A * color + C * B) + D * E) /
                        (color * (A * color + B) + D * F)) - E / F;
                return color;
            }

            // ACES 色调映射
            vec3 acesToneMapping(vec3 color) {
                float a = 2.51;
                float b = 0.03;
                float c = 2.43;
                float d = 0.59;
                float e = 0.14;
                return clamp((color * (a * color + b)) /
                            (color * (c * color + d) + e), 0.0, 1.0);
            }

            // 主函数
            void material(inout MaterialInputs material) {
                vec3 hdrColor = texture(materialParams_inputTexture, getUV0()).rgb;

                // 应用曝光
                hdrColor *= materialParams.exposure;

                // 应用色调映射
                vec3 ldrColor;
                if (materialParams.operator == 0) {
                    ldrColor = reinhardToneMapping(hdrColor);
                } else if (materialParams.operator == 1) {
                    ldrColor = filmicToneMapping(hdrColor);
                } else if (materialParams.operator == 2) {
                    ldrColor = acesToneMapping(hdrColor);
                } else {
                    ldrColor = hdrColor;
                }

                // Gamma 校正
                ldrColor = pow(ldrColor, vec3(1.0 / materialParams.gamma));

                material.baseColor = vec4(ldrColor, 1.0);
            }
        )";
    }

private:
    Engine* mEngine = nullptr;
    Operator mOperator;
    float mExposure;
    float mGamma = 2.2f;

    Material* mToneMappingMaterial = nullptr;
};
```

### 5. 景深效果

**文件：`samples/post_processing/DOFEffect.h`**

```cpp
/*
 * 景深 (Depth of Field) 效果
 *
 * 模拟相机焦距,实现景深模糊
 */

#pragma once

#include "PostProcessPipeline.h"

class DOFEffect : public PostProcessEffect {
public:
    DOFEffect(float focusDistance = 10.0f, float focalLength = 50.0f,
             float aperture = 2.8f)
        : mFocusDistance(focusDistance)
        , mFocalLength(focalLength)
        , mAperture(aperture)
    {}

    void initialize(Engine* engine, uint32_t width, uint32_t height) override {
        mEngine = engine;
        mWidth = width;
        mHeight = height;

        // 创建 CoC (Circle of Confusion) 纹理
        mCoCTexture = Texture::Builder()
            .width(width)
            .height(height)
            .levels(1)
            .format(Texture::InternalFormat::R16F)
            .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)
            .build(*engine);

        mCoCTarget = RenderTarget::Builder()
            .texture(RenderTarget::AttachmentPoint::COLOR, mCoCTexture)
            .build(*engine);

        // 创建模糊纹理
        mBlurTexture = Texture::Builder()
            .width(width)
            .height(height)
            .levels(1)
            .format(Texture::InternalFormat::RGBA16F)
            .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)
            .build(*engine);

        mBlurTarget = RenderTarget::Builder()
            .texture(RenderTarget::AttachmentPoint::COLOR, mBlurTexture)
            .build(*engine);

        // 加载材质
        // mCoCMaterial = loadMaterial("dof_coc.filamat");
        // mBokehBlurMaterial = loadMaterial("dof_bokeh.filamat");
        // mCompositeMaterial = loadMaterial("dof_composite.filamat");
    }

    void render(Engine* engine, View* view,
               Texture* inputTexture,
               RenderTarget* outputTarget) override {

        // 需要深度缓冲
        // Texture* depthTexture = getDepthTexture(view);

        // 1. 计算 CoC (模糊圆)
        renderCoC(/* depthTexture, */ mCoCTarget);

        // 2. 散景模糊
        renderBokehBlur(inputTexture, mCoCTexture, mBlurTarget);

        // 3. 合成
        renderComposite(inputTexture, mBlurTexture, mCoCTexture, outputTarget);
    }

    void resize(uint32_t width, uint32_t height) override {
        mWidth = width;
        mHeight = height;
        cleanup(mEngine);
        initialize(mEngine, width, height);
    }

    void cleanup(Engine* engine) override {
        if (mCoCTexture) engine->destroy(mCoCTexture);
        if (mCoCTarget) engine->destroy(mCoCTarget);
        if (mBlurTexture) engine->destroy(mBlurTexture);
        if (mBlurTarget) engine->destroy(mBlurTarget);
    }

    // 参数设置
    void setFocusDistance(float distance) { mFocusDistance = distance; }
    void setFocalLength(float length) { mFocalLength = length; }
    void setAperture(float aperture) { mAperture = aperture; }

private:
    void renderCoC(RenderTarget* output) {
        // 计算模糊圆大小
        // 着色器伪代码：
        /*
        float depth = texture(depthTexture, uv).r;
        float viewDepth = linearizeDepth(depth);

        // 薄透镜方程
        float coc = abs(aperture * (focalLength * (focusDistance - viewDepth)) /
                       (viewDepth * (focusDistance - focalLength)));

        // 归一化
        coc = clamp(coc / maxCoC, 0.0, 1.0);

        fragColor = vec4(coc);
        */
    }

    void renderBokehBlur(Texture* input, Texture* cocTexture,
                        RenderTarget* output) {
        // 散景模糊（六边形/圆形采样）
        /*
        vec4 color = vec4(0.0);
        float totalWeight = 0.0;

        float coc = texture(cocTexture, uv).r;
        int samples = int(coc * maxSamples);

        for (int i = 0; i < samples; ++i) {
            vec2 offset = poissonDisk[i] * coc * blurRadius;
            vec4 sampleColor = texture(input, uv + offset);
            float sampleCoC = texture(cocTexture, uv + offset).r;

            float weight = 1.0;
            color += sampleColor * weight;
            totalWeight += weight;
        }

        fragColor = color / totalWeight;
        */
    }

    void renderComposite(Texture* sharp, Texture* blurred,
                        Texture* cocTexture, RenderTarget* output) {
        // 根据 CoC 混合清晰和模糊图像
        /*
        vec3 sharpColor = texture(sharp, uv).rgb;
        vec3 blurredColor = texture(blurred, uv).rgb;
        float coc = texture(cocTexture, uv).r;

        vec3 finalColor = mix(sharpColor, blurredColor, coc);
        fragColor = vec4(finalColor, 1.0);
        */
    }

private:
    Engine* mEngine = nullptr;
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;

    float mFocusDistance;
    float mFocalLength;
    float mAperture;

    Texture* mCoCTexture = nullptr;
    RenderTarget* mCoCTarget = nullptr;

    Texture* mBlurTexture = nullptr;
    RenderTarget* mBlurTarget = nullptr;

    Material* mCoCMaterial = nullptr;
    Material* mBokehBlurMaterial = nullptr;
    Material* mCompositeMaterial = nullptr;
};
```

### 6. 主应用示例

**文件：`samples/post_processing_demo.cpp`**

```cpp
/*
 * 后处理示例应用
 */

#include "PostProcessPipeline.h"
#include "BloomEffect.h"
#include "SSAOEffect.h"
#include "ToneMappingEffect.h"
#include "DOFEffect.h"

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

class PostProcessingApp {
public:
    bool initialize(Engine* engine, View* view, Scene* scene) {
        mEngine = engine;
        mView = view;
        mScene = scene;

        // 设置 3D 场景
        setup3DScene();

        // 创建后处理管线
        mPostProcessPipeline = std::make_unique<PostProcessPipeline>(
            engine, 1920, 1080);

        // 添加后处理效果
        setupPostProcessEffects();

        std::cout << "Post Processing App initialized" << std::endl;
        return true;
    }

    void update(float deltaTime) {
        // 更新场景动画
        updateScene(deltaTime);
    }

    void render(Renderer* renderer, SwapChain* swapChain) {
        // 1. 渲染 3D 场景到纹理
        // Texture* sceneTexture = renderSceneToTexture();

        // 2. 执行后处理管线
        // mPostProcessPipeline->render(mView, sceneTexture);

        // 3. 渲染到屏幕
        if (renderer->beginFrame(swapChain)) {
            renderer->render(mView);
            renderer->endFrame();
        }
    }

    void resize(uint32_t width, uint32_t height) {
        mPostProcessPipeline->resize(width, height);
    }

    void cleanup() {
        mPostProcessPipeline.reset();
    }

    // UI 控制
    void toggleBloom() {
        if (mBloomEffect) {
            mBloomEffect->setEnabled(!mBloomEffect->isEnabled());
        }
    }

    void toggleSSAO() {
        if (mSSAOEffect) {
            mSSAOEffect->setEnabled(!mSSAOEffect->isEnabled());
        }
    }

    void toggleDOF() {
        if (mDOFEffect) {
            mDOFEffect->setEnabled(!mDOFEffect->isEnabled());
        }
    }

    void setBloomIntensity(float intensity) {
        if (mBloomEffect) {
            mBloomEffect->setIntensity(intensity);
        }
    }

    void setExposure(float exposure) {
        if (mToneMappingEffect) {
            mToneMappingEffect->setExposure(exposure);
        }
    }

private:
    void setup3DScene() {
        // 设置相机
        Camera& camera = mView->getCamera();
        camera.setProjection(45.0, 16.0f/9.0f, 0.1, 100.0);

        float3 eye(0, 5, 10);
        float3 center(0, 0, 0);
        float3 up(0, 1, 0);

        camera.lookAt(eye, center, up);

        // 添加光源
        setupLighting();

        // 加载场景模型
        // loadSceneModels();
    }

    void setupLighting() {
        // 太阳光
        Entity sun = EntityManager::get().create();
        LightManager::Builder(LightManager::Type::SUN)
            .color(Color::toLinear<ACCURATE>({1.0f, 1.0f, 1.0f}))
            .intensity(100000.0f)
            .direction({0.6f, -1.0f, -0.8f})
            .castShadows(true)
            .build(*mEngine, sun);

        mScene->addEntity(sun);
    }

    void setupPostProcessEffects() {
        // 1. SSAO（需要深度和法线）
        auto ssao = std::make_unique<SSAOEffect>(64, 0.5f);
        mSSAOEffect = ssao.get();
        mPostProcessPipeline->addEffect(std::move(ssao));

        // 2. Bloom
        auto bloom = std::make_unique<BloomEffect>(1.0f, 0.3f);
        mBloomEffect = bloom.get();
        mPostProcessPipeline->addEffect(std::move(bloom));

        // 3. 景深
        auto dof = std::make_unique<DOFEffect>(10.0f, 50.0f, 2.8f);
        mDOFEffect = dof.get();
        mPostProcessPipeline->addEffect(std::move(dof));

        // 4. 色调映射（应该最后执行）
        auto toneMapping = std::make_unique<ToneMappingEffect>(
            ToneMappingEffect::Operator::ACES, 1.0f);
        mToneMappingEffect = toneMapping.get();
        mPostProcessPipeline->addEffect(std::move(toneMapping));
    }

    void updateScene(float deltaTime) {
        // 旋转场景对象等
        static float angle = 0.0f;
        angle += deltaTime;

        // 更新对象变换...
    }

private:
    Engine* mEngine = nullptr;
    View* mView = nullptr;
    Scene* mScene = nullptr;

    std::unique_ptr<PostProcessPipeline> mPostProcessPipeline;

    // 效果指针（用于运行时控制）
    BloomEffect* mBloomEffect = nullptr;
    SSAOEffect* mSSAOEffect = nullptr;
    ToneMappingEffect* mToneMappingEffect = nullptr;
    DOFEffect* mDOFEffect = nullptr;
};

// 全局应用实例
static PostProcessingApp* g_app = nullptr;

void setup(Engine* engine, View* view, Scene* scene) {
    g_app = new PostProcessingApp();
    g_app->initialize(engine, view, scene);
}

void animate(Engine* engine, View* view, double now) {
    static double lastTime = 0.0;
    double deltaTime = now - lastTime;
    lastTime = now;

    if (g_app) {
        g_app->update(deltaTime);
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
    config.title = "Filament Post Processing Demo";
    config.backend = Engine::Backend::DEFAULT;

    FilamentApp::get().run(config, setup, cleanup, nullptr, animate);

    return 0;
}
```

---

## Android 实现 (Kotlin)

**文件：`android/app/src/main/java/com/example/PostProcessingActivity.kt`**

```kotlin
package com.example.filament.samples

import android.app.Activity
import android.os.Bundle
import android.view.Choreographer
import android.view.SurfaceView
import android.widget.SeekBar
import android.widget.Switch
import android.widget.TextView
import com.google.android.filament.*
import com.google.android.filament.utils.*

/**
 * 后处理示例 - Android 实现
 */
class PostProcessingActivity : Activity() {

    private lateinit var surfaceView: SurfaceView
    private lateinit var choreographer: Choreographer
    private lateinit var modelViewer: ModelViewer

    // UI 控件
    private lateinit var bloomSwitch: Switch
    private lateinit var bloomIntensitySeekBar: SeekBar
    private lateinit var exposureSeekBar: SeekBar
    private lateinit var infoTextView: TextView

    // 后处理参数
    private var bloomEnabled = true
    private var bloomIntensity = 0.3f
    private var exposure = 1.0f

    companion object {
        init {
            Utils.init()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_post_processing)

        surfaceView = findViewById(R.id.surfaceView)
        bloomSwitch = findViewById(R.id.bloomSwitch)
        bloomIntensitySeekBar = findViewById(R.id.bloomIntensitySeekBar)
        exposureSeekBar = findViewById(R.id.exposureSeekBar)
        infoTextView = findViewById(R.id.infoTextView)

        choreographer = Choreographer.getInstance()
        modelViewer = ModelViewer(surfaceView)

        setupCamera()
        setupLighting()
        setupUI()

        // 启用 Filament 内置后处理
        setupPostProcessing()

        choreographer.postFrameCallback(frameCallback)
    }

    private fun setupCamera() {
        modelViewer.view.camera.apply {
            setProjection(45.0,
                         surfaceView.width.toDouble() / surfaceView.height,
                         0.1, 100.0, Camera.Fov.VERTICAL)
            lookAt(
                0.0, 5.0, 10.0,
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
            .castShadows(true)
            .build(modelViewer.engine, sun)

        modelViewer.scene.addEntity(sun)

        // IBL
        modelViewer.scene.indirectLight = IndirectLight.Builder()
            .intensity(30000.0f)
            .build(modelViewer.engine)
    }

    private fun setupPostProcessing() {
        // Filament 内置后处理选项
        modelViewer.view.apply {
            // 启用 Bloom
            isBloomEnabled = bloomEnabled
            bloomOptions = bloomOptions.apply {
                this.strength = bloomIntensity
                this.threshold = true
            }

            // 色调映射
            colorGrading = ColorGrading.Builder()
                .toneMapping(ColorGrading.ToneMapping.ACES)
                .exposure(exposure)
                .build(modelViewer.engine)

            // 抗锯齿
            antiAliasing = View.AntiAliasing.FXAA
            isTemporalAntiAliasingEnabled = true

            // SSAO
            ambientOcclusionOptions = ambientOcclusionOptions.apply {
                this.enabled = true
                this.radius = 0.5f
                this.power = 1.0f
            }

            // 景深
            depthOfFieldOptions = depthOfFieldOptions.apply {
                this.enabled = false  // 默认关闭
                this.focusDistance = 10.0f
                this.blurScale = 1.0f
            }
        }
    }

    private fun setupUI() {
        // Bloom 开关
        bloomSwitch.isChecked = bloomEnabled
        bloomSwitch.setOnCheckedChangeListener { _, isChecked ->
            bloomEnabled = isChecked
            modelViewer.view.isBloomEnabled = isChecked
        }

        // Bloom 强度
        bloomIntensitySeekBar.max = 100
        bloomIntensitySeekBar.progress = (bloomIntensity * 100).toInt()
        bloomIntensitySeekBar.setOnSeekBarChangeListener(object :
            SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int,
                                          fromUser: Boolean) {
                bloomIntensity = progress / 100.0f
                modelViewer.view.bloomOptions = modelViewer.view.bloomOptions.apply {
                    this.strength = bloomIntensity
                }
                updateInfo()
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        // 曝光
        exposureSeekBar.max = 200
        exposureSeekBar.progress = 100  // 默认 1.0
        exposureSeekBar.setOnSeekBarChangeListener(object :
            SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int,
                                          fromUser: Boolean) {
                exposure = progress / 100.0f
                modelViewer.view.colorGrading = ColorGrading.Builder()
                    .toneMapping(ColorGrading.ToneMapping.ACES)
                    .exposure(exposure)
                    .build(modelViewer.engine)
                updateInfo()
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        updateInfo()
    }

    private fun updateInfo() {
        val info = """
            Bloom: ${if (bloomEnabled) "ON" else "OFF"}
            Bloom Intensity: ${"%.2f".format(bloomIntensity)}
            Exposure: ${"%.2f".format(exposure)}
            Anti-aliasing: FXAA + TAA
            SSAO: ON
        """.trimIndent()

        runOnUiThread {
            infoTextView.text = info
        }
    }

    private val frameCallback = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            modelViewer.render(frameTimeNanos)
            choreographer.postFrameCallback(this)
        }
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
```

**布局文件：`res/layout/activity_post_processing.xml`**

```xml
<?xml version="1.0" encoding="utf-8"?>
<LinearLayout xmlns:android="http://schemas.android.com/apk/res/android"
    android:layout_width="match_parent"
    android:layout_height="match_parent"
    android:orientation="vertical">

    <SurfaceView
        android:id="@+id/surfaceView"
        android:layout_width="match_parent"
        android:layout_height="0dp"
        android:layout_weight="1" />

    <ScrollView
        android:layout_width="match_parent"
        android:layout_height="wrap_content"
        android:background="#f0f0f0">

        <LinearLayout
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:orientation="vertical"
            android:padding="16dp">

            <TextView
                android:id="@+id/infoTextView"
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:text="Post Processing Info"
                android:textSize="12sp"
                android:fontFamily="monospace"
                android:layout_marginBottom="16dp" />

            <Switch
                android:id="@+id/bloomSwitch"
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:text="Enable Bloom"
                android:layout_marginBottom="8dp" />

            <TextView
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:text="Bloom Intensity"
                android:textSize="14sp" />

            <SeekBar
                android:id="@+id/bloomIntensitySeekBar"
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:layout_marginBottom="16dp" />

            <TextView
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:text="Exposure"
                android:textSize="14sp" />

            <SeekBar
                android:id="@+id/exposureSeekBar"
                android:layout_width="match_parent"
                android:layout_height="wrap_content" />
        </LinearLayout>
    </ScrollView>
</LinearLayout>
```

---

## 性能优化

### 1. 降采样优化

```cpp
// 使用 Mipmap 链降采样
class MipmapDownsampler {
public:
    void downsample(Texture* input, std::vector<Texture*>& mipChain) {
        // 生成 mipmap 链而不是手动降采样
        // 更高效且硬件加速

        for (size_t i = 0; i < mipChain.size(); ++i) {
            // 使用 Filament 的 Mipmap 生成
            // 或者使用计算着色器
        }
    }
};
```

### 2. 半分辨率后处理

```cpp
// 对性能敏感的效果使用半分辨率
class HalfResolutionEffect : public PostProcessEffect {
public:
    void initialize(Engine* engine, uint32_t width, uint32_t height) override {
        // 使用一半分辨率
        mHalfWidth = width / 2;
        mHalfHeight = height / 2;

        // 创建半分辨率纹理
        mHalfResTexture = Texture::Builder()
            .width(mHalfWidth)
            .height(mHalfHeight)
            // ...
            .build(*engine);
    }

    void render(Engine* engine, View* view,
               Texture* inputTexture,
               RenderTarget* outputTarget) override {
        // 1. 降采样到半分辨率
        downsample(inputTexture, mHalfResTarget);

        // 2. 在半分辨率执行效果
        applyEffect(mHalfResTexture, mHalfResTarget);

        // 3. 上采样回全分辨率
        upsample(mHalfResTexture, outputTarget);
    }
};
```

### 3. 计算着色器优化

```cpp
// 使用计算着色器加速某些效果
class ComputeShaderBlur {
public:
    void blur(Texture* input, Texture* output) {
        // 使用计算着色器并行处理
        // 比传统全屏四边形更高效

        /*
        layout (local_size_x = 16, local_size_y = 16) in;

        void main() {
            ivec2 pixelCoord = ivec2(gl_GlobalInvocationID.xy);

            // 执行模糊
            vec4 color = vec4(0.0);
            for (int y = -radius; y <= radius; ++y) {
                for (int x = -radius; x <= radius; ++x) {
                    ivec2 sampleCoord = pixelCoord + ivec2(x, y);
                    color += imageLoad(inputImage, sampleCoord) * kernel[y+radius][x+radius];
                }
            }

            imageStore(outputImage, pixelCoord, color);
        }
        */
    }
};
```

---

## 常见问题

### Q1: 后处理导致性能下降?

**A:** 优化策略:

```cpp
// 1. 使用降采样
// 在低分辨率执行昂贵的效果

// 2. 合并 Pass
// 将多个简单效果合并到一个着色器

// 3. 异步计算
// 使用计算队列并行处理

// 4. LOD
// 根据性能动态调整效果质量
class AdaptivePostProcessing {
    void update(float fps) {
        if (fps < 30) {
            // 降低质量
            bloomEffect->setQuality(Quality::LOW);
            ssaoEffect->setEnabled(false);
        } else if (fps > 55) {
            // 提高质量
            bloomEffect->setQuality(Quality::HIGH);
            ssaoEffect->setEnabled(true);
        }
    }
};
```

### Q2: Bloom 效果不明显?

**A:** 调整参数:

```cpp
// 1. 降低阈值
bloomEffect->setThreshold(0.8f);  // 从 1.0 降到 0.8

// 2. 增加强度
bloomEffect->setIntensity(1.5f);  // 从 1.0 增到 1.5

// 3. 增加模糊半径
bloomEffect->setBlurRadius(5.0f);

// 4. 确保场景有高亮区域
// HDR 渲染很重要
light->setIntensity(200000.0f);  // 高强度光源
```

### Q3: SSAO 有噪点?

**A:** 使用降噪技术:

```cpp
// 1. 增加采样数
ssaoEffect->setKernelSize(128);  // 从 64 增到 128

// 2. 应用模糊
ssaoEffect->setBlurRadius(4.0f);

// 3. 使用时域降噪(TAA)
view->setTemporalAntiAliasingEnabled(true);

// 4. 使用更好的噪声纹理
// 改用蓝噪声而不是白噪声
```

### Q4: 色调映射后颜色失真?

**A:** 调整色彩空间和 Gamma:

```cpp
// 1. 确保使用线性颜色空间
// 在着色器中：
// color = pow(color, vec3(2.2));  // sRGB to Linear

// 2. 选择合适的色调映射算子
toneMappingEffect->setOperator(ToneMappingEffect::Operator::ACES);

// 3. 调整曝光
toneMappingEffect->setExposure(1.2f);

// 4. 应用色彩分级
ColorGrading::Builder()
    .contrast(1.1f)
    .saturation(1.05f)
    .vibrance(1.0f)
    .build(*engine);
```

### Q5: 景深效果边缘锯齿?

**A:** 改进采样和滤波:

```cpp
// 1. 增加散景采样数
dofEffect->setSampleCount(64);

// 2. 使用更好的采样模式
// 泊松圆盘采样

// 3. 应用抗锯齿到 CoC
// 模糊 CoC 纹理

// 4. 使用更高质量的双边滤波
void bilateralFilter(Texture* input, Texture* output) {
    // 考虑空间距离和颜色差异
}
```

### Q6: 如何调试后处理效果?

**A:** 可视化中间结果:

```cpp
class PostProcessDebugger {
public:
    enum class DebugMode {
        FINAL_OUTPUT,
        BLOOM_ONLY,
        AO_ONLY,
        COC_MAP,
        DEPTH_BUFFER,
        NORMAL_BUFFER
    };

    void setDebugMode(DebugMode mode) {
        mDebugMode = mode;
    }

    void render(...) {
        switch (mDebugMode) {
            case DebugMode::BLOOM_ONLY:
                // 只显示 Bloom
                outputColor = bloomColor;
                break;
            case DebugMode::AO_ONLY:
                // 只显示 AO
                outputColor = vec3(ao);
                break;
            case DebugMode::COC_MAP:
                // 可视化模糊圆
                outputColor = vec3(coc);
                break;
            // ...
        }
    }
};
```

---

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.19)
project(post_processing)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FILAMENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Filament root")

find_package(filament REQUIRED CONFIG PATHS ${FILAMENT_DIR}/out/cmake-release)

add_executable(post_processing_demo
    samples/post_processing/PostProcessPipeline.h
    samples/post_processing/BloomEffect.h
    samples/post_processing/SSAOEffect.h
    samples/post_processing/ToneMappingEffect.h
    samples/post_processing/DOFEffect.h
    samples/post_processing_demo.cpp
)

target_link_libraries(post_processing_demo PRIVATE
    filament
    filamat
    utils
    filamentapp
)

target_include_directories(post_processing_demo PRIVATE
    ${FILAMENT_DIR}/filament/include
    ${FILAMENT_DIR}/libs/utils/include
    ${FILAMENT_DIR}/libs/math/include
    ${FILAMENT_DIR}/libs/filamentapp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/samples
)

install(TARGETS post_processing_demo DESTINATION bin)
```

---

## 相关文档

- [08-ui-rendering.md](./08-ui-rendering.md) - UI 渲染系统
- [10-complete-game.md](./10-complete-game.md) - 完整游戏示例
- [../tools/matc.md](../tools/matc.md) - 材质编译器
- [../platforms/android.md](../platforms/android.md) - Android 平台

---

## 总结

本示例展示了 Filament 中后处理效果的完整实现:

1. **后处理管线** - 多 Pass 渲染框架
2. **Bloom 泛光** - 高亮区域发光
3. **SSAO** - 环境光遮蔽
4. **色调映射** - HDR 到 LDR 转换
5. **景深** - 模拟相机焦距

关键收获:
- 理解后处理管线架构
- 掌握常见后处理效果原理
- 实现高性能后处理
- 调试和优化技巧

下一步学习 [10-complete-game.md](./10-complete-game.md) 综合所有技术。
