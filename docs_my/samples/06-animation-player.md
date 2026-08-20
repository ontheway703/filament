# 06 - Animation Player (骨骼动画播放器)

## 概述

本示例展示如何在 Filament 中加载和播放 glTF 骨骼动画。我们将使用 `gltfio` 库加载包含骨骼动画的 glTF 模型，并通过 `Animator` 类控制动画播放、混合和状态管理。

### 功能特性

- **glTF 动画加载** - 从 glTF 2.0 文件加载骨骼动画数据
- **动画播放控制** - 播放、暂停、循环和速度控制
- **多动画混合** - 在多个动画间平滑过渡
- **骨骼变换** - 实时更新骨骼节点的变换矩阵
- **动画状态机** - 管理复杂的动画状态转换
- **性能监控** - 监控动画更新性能

### 适用场景

- 角色动画播放（走、跑、跳等）
- 机械装置动画
- 游戏角色控制器
- 动画预览工具
- 虚拟角色应用

### 技术要点

- 使用 `gltfio::FilamentAsset` 管理动画资源
- 使用 `gltfio::Animator` 更新动画状态
- 理解骨骼层级和变换传播
- 掌握动画混合权重控制
- 优化动画更新性能

---

## 完整实现 (C++)

### 1. 主程序实现

**文件：`samples/animation_player.cpp`**

```cpp
/*
 * Filament 示例 06 - 骨骼动画播放器
 *
 * 本示例展示如何加载和播放 glTF 骨骼动画，包括：
 * - 加载带有骨骼动画的 glTF 模型
 * - 使用 Animator 控制动画播放
 * - 实现多动画混合和状态机
 * - 性能监控和优化
 */

#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/Renderer.h>
#include <filament/SwapChain.h>
#include <filament/IndexBuffer.h>
#include <filament/VertexBuffer.h>
#include <filament/Material.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <filament/LightManager.h>

#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/Animator.h>

#include <filamentapp/FilamentApp.h>
#include <filamentapp/Config.h>

#include <utils/EntityManager.h>
#include <utils/Path.h>

#include <math/vec3.h>
#include <math/mat4.h>
#include <math/norm.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <memory>
#include <chrono>

using namespace filament;
using namespace filament::math;
using namespace utils;

// 动画状态枚举
enum class AnimationState {
    IDLE,
    WALK,
    RUN,
    JUMP,
    ATTACK,
    COUNT
};

// 动画状态转换配置
struct AnimationTransition {
    AnimationState from;
    AnimationState to;
    float blendDuration;  // 混合时长（秒）
};

// 动画播放器配置
struct AnimationConfig {
    size_t animationIndex;      // 动画索引
    float playbackSpeed;         // 播放速度倍率
    bool loop;                   // 是否循环
    float fadeInDuration;        // 淡入时长
    float fadeOutDuration;       // 淡出时长
};

// 动画混合器
class AnimationBlender {
public:
    struct BlendChannel {
        size_t animationIndex;
        float weight;
        float targetWeight;
        float blendSpeed;
    };

    std::vector<BlendChannel> channels;

    void addChannel(size_t index, float weight = 0.0f) {
        channels.push_back({index, weight, weight, 1.0f});
    }

    void setTargetWeight(size_t channelIndex, float target, float duration) {
        if (channelIndex < channels.size()) {
            channels[channelIndex].targetWeight = target;
            channels[channelIndex].blendSpeed = (duration > 0) ?
                (1.0f / duration) : 1000.0f;
        }
    }

    void update(float deltaTime) {
        for (auto& channel : channels) {
            if (std::abs(channel.weight - channel.targetWeight) > 0.001f) {
                float delta = channel.blendSpeed * deltaTime;
                if (channel.weight < channel.targetWeight) {
                    channel.weight = std::min(channel.weight + delta,
                                             channel.targetWeight);
                } else {
                    channel.weight = std::max(channel.weight - delta,
                                             channel.targetWeight);
                }
            }
        }
    }

    void normalizeWeights() {
        float totalWeight = 0.0f;
        for (const auto& channel : channels) {
            totalWeight += channel.weight;
        }
        if (totalWeight > 0.0f) {
            for (auto& channel : channels) {
                channel.weight /= totalWeight;
            }
        }
    }
};

// 动画状态机
class AnimationStateMachine {
public:
    AnimationStateMachine() : currentState(AnimationState::IDLE) {}

    void addTransition(AnimationState from, AnimationState to, float duration) {
        transitions.push_back({from, to, duration});
    }

    bool canTransition(AnimationState from, AnimationState to) const {
        for (const auto& trans : transitions) {
            if (trans.from == from && trans.to == to) {
                return true;
            }
        }
        return false;
    }

    float getTransitionDuration(AnimationState from, AnimationState to) const {
        for (const auto& trans : transitions) {
            if (trans.from == from && trans.to == to) {
                return trans.blendDuration;
            }
        }
        return 0.3f;  // 默认混合时长
    }

    AnimationState getCurrentState() const { return currentState; }
    void setCurrentState(AnimationState state) { currentState = state; }

private:
    AnimationState currentState;
    std::vector<AnimationTransition> transitions;
};

// 主应用类
class AnimationPlayerApp {
public:
    AnimationPlayerApp() = default;
    ~AnimationPlayerApp() { cleanup(); }

    bool initialize(Engine* engine, View* view, Scene* scene);
    void render(Engine* engine, View* view, Renderer* renderer,
                SwapChain* swapChain, uint64_t frameTimeNanos);
    void animate(double deltaTime);
    void cleanup();

    // 动画控制接口
    void playAnimation(size_t index, bool loop = true);
    void pauseAnimation();
    void resumeAnimation();
    void stopAnimation();
    void setPlaybackSpeed(float speed);
    void transitionToState(AnimationState newState);

private:
    // Filament 组件
    Engine* mEngine = nullptr;
    Scene* mScene = nullptr;

    // glTF 资源
    gltfio::AssetLoader* mAssetLoader = nullptr;
    gltfio::ResourceLoader* mResourceLoader = nullptr;
    gltfio::FilamentAsset* mAsset = nullptr;
    gltfio::Animator* mAnimator = nullptr;

    // 动画控制
    size_t mCurrentAnimationIndex = 0;
    float mAnimationTime = 0.0f;
    float mPlaybackSpeed = 1.0f;
    bool mIsPlaying = true;
    bool mIsLooping = true;

    // 动画状态机
    AnimationStateMachine mStateMachine;
    AnimationBlender mBlender;
    std::map<AnimationState, AnimationConfig> mStateConfigs;

    // 性能监控
    std::chrono::high_resolution_clock::time_point mLastFrameTime;
    float mAverageFrameTime = 0.0f;
    size_t mFrameCount = 0;

    // 辅助方法
    bool loadGLTFAsset(const std::string& path);
    void setupCamera(View* view);
    void setupLighting();
    void setupAnimationStates();
    void updateAnimationBlending(float deltaTime);
    void printAnimationInfo();
};

bool AnimationPlayerApp::initialize(Engine* engine, View* view, Scene* scene) {
    mEngine = engine;
    mScene = scene;

    // 创建 glTF 加载器
    mAssetLoader = gltfio::AssetLoader::create({mEngine});
    mResourceLoader = new gltfio::ResourceLoader({
        .engine = mEngine,
        .normalizeSkinningWeights = true,  // 规范化蒙皮权重
        .recomputeBoundingBoxes = false
    });

    // 加载 glTF 模型（假设在 assets 目录下）
    std::string modelPath = "assets/models/animated_character.glb";
    if (!loadGLTFAsset(modelPath)) {
        std::cerr << "Failed to load glTF asset: " << modelPath << std::endl;
        return false;
    }

    // 设置相机
    setupCamera(view);

    // 设置光照
    setupLighting();

    // 配置动画状态机
    setupAnimationStates();

    // 打印动画信息
    printAnimationInfo();

    // 初始化时间
    mLastFrameTime = std::chrono::high_resolution_clock::now();

    std::cout << "Animation Player initialized successfully" << std::endl;
    return true;
}

bool AnimationPlayerApp::loadGLTFAsset(const std::string& path) {
    // 读取 glTF 文件
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Cannot open file: " << path << std::endl;
        return false;
    }

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(fileSize);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();

    // 加载资产
    mAsset = mAssetLoader->createAsset(buffer.data(), buffer.size());
    if (!mAsset) {
        std::cerr << "Failed to create asset" << std::endl;
        return false;
    }

    // 异步加载资源（纹理、缓冲区等）
    mResourceLoader->asyncBeginLoad(mAsset);

    // 等待资源加载完成
    mResourceLoader->asyncUpdateLoad();
    while (mResourceLoader->asyncGetLoadProgress() < 1.0f) {
        mResourceLoader->asyncUpdateLoad();
    }

    // 创建动画控制器
    if (mAsset->getAnimationCount() > 0) {
        mAnimator = mAsset->getInstance()->getAnimator();
        if (!mAnimator) {
            std::cerr << "Failed to create animator" << std::endl;
            return false;
        }
    }

    // 添加到场景
    mScene->addEntities(mAsset->getEntities(), mAsset->getEntityCount());

    std::cout << "Loaded glTF asset with " << mAsset->getAnimationCount()
              << " animations" << std::endl;

    return true;
}

void AnimationPlayerApp::setupCamera(View* view) {
    auto& tcm = mEngine->getTransformManager();
    Camera* camera = &view->getCamera();

    // 设置相机位置和朝向
    camera->setProjection(45.0, 16.0f/9.0f, 0.1, 100.0);

    // 相机位置：稍微偏上偏后，适合观察角色
    float3 eye(0, 1.6f, 4.0f);
    float3 center(0, 1.0f, 0);
    float3 up(0, 1, 0);

    camera->lookAt(eye, center, up);
}

void AnimationPlayerApp::setupLighting() {
    // 创建主光源（定向光）
    Entity sunLight = EntityManager::get().create();
    LightManager::Builder(LightManager::Type::SUN)
        .color(Color::toLinear<ACCURATE>({1.0f, 0.95f, 0.9f}))
        .intensity(100000.0f)
        .direction({0.6f, -1.0f, -0.8f})
        .castShadows(true)
        .sunAngularRadius(1.9f)
        .sunHaloSize(10.0f)
        .sunHaloFalloff(80.0f)
        .build(*mEngine, sunLight);

    mScene->addEntity(sunLight);

    // 添加环境光（IBL）
    // 注意：实际应用中应该加载 IBL 纹理
    // 这里使用默认的环境光
}

void AnimationPlayerApp::setupAnimationStates() {
    // 假设模型包含以下动画（索引需根据实际模型调整）
    // 0: Idle
    // 1: Walk
    // 2: Run
    // 3: Jump
    // 4: Attack

    // 配置各状态的动画
    mStateConfigs[AnimationState::IDLE] = {0, 1.0f, true, 0.3f, 0.3f};
    mStateConfigs[AnimationState::WALK] = {1, 1.0f, true, 0.2f, 0.2f};
    mStateConfigs[AnimationState::RUN] = {2, 1.2f, true, 0.2f, 0.2f};
    mStateConfigs[AnimationState::JUMP] = {3, 1.0f, false, 0.1f, 0.1f};
    mStateConfigs[AnimationState::ATTACK] = {4, 1.5f, false, 0.1f, 0.2f};

    // 添加状态转换
    // 从 IDLE 可以转换到任何状态
    mStateMachine.addTransition(AnimationState::IDLE, AnimationState::WALK, 0.2f);
    mStateMachine.addTransition(AnimationState::IDLE, AnimationState::RUN, 0.2f);
    mStateMachine.addTransition(AnimationState::IDLE, AnimationState::JUMP, 0.1f);
    mStateMachine.addTransition(AnimationState::IDLE, AnimationState::ATTACK, 0.1f);

    // WALK 转换
    mStateMachine.addTransition(AnimationState::WALK, AnimationState::IDLE, 0.3f);
    mStateMachine.addTransition(AnimationState::WALK, AnimationState::RUN, 0.2f);
    mStateMachine.addTransition(AnimationState::WALK, AnimationState::JUMP, 0.1f);

    // RUN 转换
    mStateMachine.addTransition(AnimationState::RUN, AnimationState::WALK, 0.2f);
    mStateMachine.addTransition(AnimationState::RUN, AnimationState::IDLE, 0.3f);
    mStateMachine.addTransition(AnimationState::RUN, AnimationState::JUMP, 0.1f);

    // JUMP 和 ATTACK 完成后回到 IDLE
    mStateMachine.addTransition(AnimationState::JUMP, AnimationState::IDLE, 0.2f);
    mStateMachine.addTransition(AnimationState::ATTACK, AnimationState::IDLE, 0.2f);

    // 初始化混合通道
    for (size_t i = 0; i < static_cast<size_t>(AnimationState::COUNT); ++i) {
        mBlender.addChannel(i, i == 0 ? 1.0f : 0.0f);
    }
}

void AnimationPlayerApp::printAnimationInfo() {
    if (!mAsset || !mAnimator) return;

    std::cout << "\n=== Animation Information ===" << std::endl;
    std::cout << "Total animations: " << mAsset->getAnimationCount() << std::endl;

    for (size_t i = 0; i < mAsset->getAnimationCount(); ++i) {
        const char* name = mAsset->getAnimationName(i);
        float duration = mAnimator->getAnimationDuration(i);

        std::cout << "Animation " << i << ": "
                  << (name ? name : "Unnamed")
                  << " (Duration: " << duration << "s)" << std::endl;
    }
    std::cout << "============================\n" << std::endl;
}

void AnimationPlayerApp::animate(double deltaTime) {
    if (!mAnimator || !mIsPlaying) return;

    // 更新动画时间
    mAnimationTime += deltaTime * mPlaybackSpeed;

    // 获取当前动画时长
    float duration = mAnimator->getAnimationDuration(mCurrentAnimationIndex);

    // 处理循环
    if (mIsLooping) {
        while (mAnimationTime > duration) {
            mAnimationTime -= duration;
        }
    } else {
        if (mAnimationTime > duration) {
            mAnimationTime = duration;
            mIsPlaying = false;
        }
    }

    // 更新混合器
    updateAnimationBlending(deltaTime);

    // 应用动画（简单版本 - 单个动画）
    mAnimator->applyAnimation(mCurrentAnimationIndex, mAnimationTime);

    // 更新骨骼变换
    mAnimator->updateBoneMatrices();

    // 性能监控
    mFrameCount++;
    if (mFrameCount % 60 == 0) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            currentTime - mLastFrameTime).count();
        mAverageFrameTime = elapsed / 60.0f / 1000.0f;  // ms
        mLastFrameTime = currentTime;
    }
}

void AnimationPlayerApp::updateAnimationBlending(float deltaTime) {
    // 更新混合权重
    mBlender.update(deltaTime);

    // 应用混合动画（高级版本 - 多动画混合）
    // 注意：这需要手动累加骨骼变换
    /*
    for (const auto& channel : mBlender.channels) {
        if (channel.weight > 0.001f) {
            mAnimator->applyAnimation(channel.animationIndex,
                                     mAnimationTime,
                                     channel.weight);
        }
    }
    */
}

void AnimationPlayerApp::playAnimation(size_t index, bool loop) {
    if (!mAnimator || index >= mAsset->getAnimationCount()) return;

    mCurrentAnimationIndex = index;
    mAnimationTime = 0.0f;
    mIsLooping = loop;
    mIsPlaying = true;

    std::cout << "Playing animation " << index
              << " (" << (loop ? "looped" : "once") << ")" << std::endl;
}

void AnimationPlayerApp::pauseAnimation() {
    mIsPlaying = false;
    std::cout << "Animation paused at " << mAnimationTime << "s" << std::endl;
}

void AnimationPlayerApp::resumeAnimation() {
    mIsPlaying = true;
    std::cout << "Animation resumed" << std::endl;
}

void AnimationPlayerApp::stopAnimation() {
    mIsPlaying = false;
    mAnimationTime = 0.0f;
    std::cout << "Animation stopped" << std::endl;
}

void AnimationPlayerApp::setPlaybackSpeed(float speed) {
    mPlaybackSpeed = std::max(0.1f, std::min(speed, 5.0f));
    std::cout << "Playback speed set to " << mPlaybackSpeed << "x" << std::endl;
}

void AnimationPlayerApp::transitionToState(AnimationState newState) {
    AnimationState currentState = mStateMachine.getCurrentState();

    if (currentState == newState) return;

    if (!mStateMachine.canTransition(currentState, newState)) {
        std::cout << "Cannot transition from state " << (int)currentState
                  << " to " << (int)newState << std::endl;
        return;
    }

    float blendDuration = mStateMachine.getTransitionDuration(currentState, newState);

    // 淡出当前状态
    mBlender.setTargetWeight((size_t)currentState, 0.0f, blendDuration);

    // 淡入新状态
    mBlender.setTargetWeight((size_t)newState, 1.0f, blendDuration);

    // 更新状态机
    mStateMachine.setCurrentState(newState);

    // 应用新状态的配置
    const auto& config = mStateConfigs[newState];
    mCurrentAnimationIndex = config.animationIndex;
    mPlaybackSpeed = config.playbackSpeed;
    mIsLooping = config.loop;
    mAnimationTime = 0.0f;

    std::cout << "Transitioning to state " << (int)newState
              << " with blend duration " << blendDuration << "s" << std::endl;
}

void AnimationPlayerApp::render(Engine* engine, View* view, Renderer* renderer,
                                SwapChain* swapChain, uint64_t frameTimeNanos) {
    // 渲染由 FilamentApp 框架处理
    if (renderer->beginFrame(swapChain)) {
        renderer->render(view);
        renderer->endFrame();
    }
}

void AnimationPlayerApp::cleanup() {
    if (mAsset) {
        mAssetLoader->destroyAsset(mAsset);
        mAsset = nullptr;
    }

    if (mResourceLoader) {
        delete mResourceLoader;
        mResourceLoader = nullptr;
    }

    if (mAssetLoader) {
        gltfio::AssetLoader::destroy(&mAssetLoader);
    }

    std::cout << "Animation Player cleaned up" << std::endl;
}

// FilamentApp 回调函数
static AnimationPlayerApp* g_app = nullptr;

void setup(Engine* engine, View* view, Scene* scene) {
    g_app = new AnimationPlayerApp();
    if (!g_app->initialize(engine, view, scene)) {
        std::cerr << "Failed to initialize application" << std::endl;
        delete g_app;
        g_app = nullptr;
    }
}

void animate(Engine* engine, View* view, double now) {
    static double lastTime = 0.0;
    double deltaTime = now - lastTime;
    lastTime = now;

    if (g_app) {
        g_app->animate(deltaTime);
    }
}

void cleanup(Engine* engine, View* view, Scene* scene) {
    if (g_app) {
        g_app->cleanup();
        delete g_app;
        g_app = nullptr;
    }
}

// 主入口
int main(int argc, char** argv) {
    Config config;
    config.title = "Filament Animation Player";
    config.backend = Engine::Backend::DEFAULT;

    FilamentApp::get().run(config, setup, cleanup, nullptr, animate);

    return 0;
}
```

### 2. 高级动画混合实现

**文件：`samples/advanced_animation_blending.cpp`**

```cpp
/*
 * 高级动画混合示例
 *
 * 展示如何实现复杂的多层动画混合：
 * - 上下身分离（upper/lower body blending）
 * - 叠加动画（additive animations）
 * - IK（Inverse Kinematics）集成
 */

#include <gltfio/Animator.h>
#include <math/quat.h>
#include <math/mat4.h>
#include <vector>

using namespace filament::math;

// 骨骼遮罩 - 用于部分身体动画
class BoneMask {
public:
    std::vector<float> weights;  // 每个骨骼的权重（0-1）

    BoneMask(size_t boneCount, float defaultWeight = 1.0f)
        : weights(boneCount, defaultWeight) {}

    void setUpperBodyMask() {
        // 假设骨骼索引：
        // 0-4: 下身和腿
        // 5-10: 上身和手臂
        for (size_t i = 0; i < weights.size(); ++i) {
            weights[i] = (i >= 5) ? 1.0f : 0.0f;
        }
    }

    void setLowerBodyMask() {
        for (size_t i = 0; i < weights.size(); ++i) {
            weights[i] = (i < 5) ? 1.0f : 0.0f;
        }
    }

    float getWeight(size_t boneIndex) const {
        return (boneIndex < weights.size()) ? weights[boneIndex] : 0.0f;
    }
};

// 叠加动画混合器
class AdditiveBlender {
public:
    struct BoneTransform {
        quat rotation;
        float3 translation;
        float3 scale;
    };

    // 混合两个骨骼变换
    static BoneTransform blend(const BoneTransform& a, const BoneTransform& b,
                              float weight, bool additive = false) {
        BoneTransform result;

        if (additive) {
            // 叠加模式：在 a 的基础上添加 b 的变换
            result.rotation = normalize(a.rotation * slerp(quat(), b.rotation, weight));
            result.translation = a.translation + b.translation * weight;
            result.scale = a.scale + (b.scale - float3(1.0f)) * weight;
        } else {
            // 普通混合模式
            result.rotation = slerp(a.rotation, b.rotation, weight);
            result.translation = lerp(a.translation, b.translation, weight);
            result.scale = lerp(a.scale, b.scale, weight);
        }

        return result;
    }

    // 应用骨骼遮罩的混合
    static std::vector<BoneTransform> blendWithMask(
        const std::vector<BoneTransform>& base,
        const std::vector<BoneTransform>& overlay,
        const BoneMask& mask,
        float globalWeight)
    {
        std::vector<BoneTransform> result(base.size());

        for (size_t i = 0; i < base.size(); ++i) {
            float weight = mask.getWeight(i) * globalWeight;
            result[i] = blend(base[i], overlay[i], weight);
        }

        return result;
    }
};

// 多层动画系统
class LayeredAnimationSystem {
public:
    struct AnimationLayer {
        std::string name;
        size_t animationIndex;
        float weight;
        float playbackSpeed;
        bool additive;
        BoneMask mask;

        AnimationLayer(const std::string& n, size_t idx, size_t boneCount)
            : name(n), animationIndex(idx), weight(1.0f),
              playbackSpeed(1.0f), additive(false), mask(boneCount) {}
    };

    std::vector<AnimationLayer> layers;
    gltfio::Animator* animator;

    LayeredAnimationSystem(gltfio::Animator* anim) : animator(anim) {}

    void addLayer(const std::string& name, size_t animIndex,
                 size_t boneCount, bool isAdditive = false) {
        AnimationLayer layer(name, animIndex, boneCount);
        layer.additive = isAdditive;
        layers.push_back(layer);
    }

    void update(float deltaTime) {
        // 实际实现需要：
        // 1. 对每个层应用动画到临时骨骼变换
        // 2. 根据权重和遮罩混合所有层
        // 3. 应用最终结果到 Animator

        // 伪代码示例：
        /*
        std::vector<BoneTransform> finalTransforms = baseTransforms;

        for (const auto& layer : layers) {
            if (layer.weight > 0.001f) {
                auto layerTransforms = applyAnimation(layer.animationIndex, time);
                finalTransforms = AdditiveBlender::blendWithMask(
                    finalTransforms, layerTransforms, layer.mask, layer.weight);
            }
        }

        applyTransformsToAnimator(finalTransforms);
        */
    }

    AnimationLayer* getLayer(const std::string& name) {
        for (auto& layer : layers) {
            if (layer.name == name) return &layer;
        }
        return nullptr;
    }
};

// 使用示例
void setupLayeredAnimation(gltfio::Animator* animator, size_t boneCount) {
    LayeredAnimationSystem layerSystem(animator);

    // 基础层：全身动作（走路）
    layerSystem.addLayer("BaseLocomotion", 0, boneCount, false);

    // 上身层：持枪姿势
    layerSystem.addLayer("UpperBodyPose", 1, boneCount, false);
    auto* upperLayer = layerSystem.getLayer("UpperBodyPose");
    if (upperLayer) {
        upperLayer->mask.setUpperBodyMask();
        upperLayer->weight = 1.0f;
    }

    // 叠加层：呼吸动画
    layerSystem.addLayer("Breathing", 2, boneCount, true);
    auto* breathLayer = layerSystem.getLayer("Breathing");
    if (breathLayer) {
        breathLayer->weight = 0.3f;
        breathLayer->playbackSpeed = 0.5f;
    }

    // 更新循环
    float time = 0.0f;
    float deltaTime = 1.0f / 60.0f;
    while (true) {
        layerSystem.update(deltaTime);
        time += deltaTime;
        // break; // 实际使用中这里是游戏循环
    }
}
```

### 3. 动画事件系统

**文件：`samples/animation_events.cpp`**

```cpp
/*
 * 动画事件系统
 *
 * 在动画的特定时间点触发事件：
 * - 脚步声
 * - 武器攻击判定
 * - 粒子特效触发
 */

#include <functional>
#include <map>
#include <vector>
#include <string>

// 动画事件
struct AnimationEvent {
    float time;                              // 触发时间（秒）
    std::string eventName;                   // 事件名称
    std::map<std::string, std::string> parameters;  // 事件参数

    AnimationEvent(float t, const std::string& name)
        : time(t), eventName(name) {}
};

// 动画事件轨道
class AnimationEventTrack {
public:
    using EventCallback = std::function<void(const AnimationEvent&)>;

    void addEvent(float time, const std::string& name) {
        events.push_back(AnimationEvent(time, name));
        // 按时间排序
        std::sort(events.begin(), events.end(),
            [](const AnimationEvent& a, const AnimationEvent& b) {
                return a.time < b.time;
            });
    }

    void setEventCallback(const std::string& eventName, EventCallback callback) {
        callbacks[eventName] = callback;
    }

    void update(float lastTime, float currentTime) {
        for (const auto& event : events) {
            // 检查事件是否在 lastTime 和 currentTime 之间
            bool triggered = false;

            if (lastTime < currentTime) {
                // 正向播放
                triggered = (event.time >= lastTime && event.time < currentTime);
            } else {
                // 反向播放或循环
                triggered = (event.time >= currentTime && event.time < lastTime);
            }

            if (triggered) {
                triggerEvent(event);
            }
        }
    }

private:
    std::vector<AnimationEvent> events;
    std::map<std::string, EventCallback> callbacks;

    void triggerEvent(const AnimationEvent& event) {
        auto it = callbacks.find(event.eventName);
        if (it != callbacks.end()) {
            it->second(event);
        }
    }
};

// 使用示例
void setupAnimationEvents(AnimationEventTrack& track) {
    // 添加脚步事件
    track.addEvent(0.3f, "footstep_left");
    track.addEvent(0.8f, "footstep_right");

    // 添加攻击判定
    track.addEvent(0.5f, "attack_hit");

    // 注册事件回调
    track.setEventCallback("footstep_left", [](const AnimationEvent& e) {
        std::cout << "Play footstep sound (left)" << std::endl;
        // 播放脚步音效
    });

    track.setEventCallback("footstep_right", [](const AnimationEvent& e) {
        std::cout << "Play footstep sound (right)" << std::endl;
    });

    track.setEventCallback("attack_hit", [](const AnimationEvent& e) {
        std::cout << "Check attack collision" << std::endl;
        // 执行攻击判定
    });
}
```

---

## Android 实现 (Kotlin)

**文件：`android/app/src/main/java/com/example/AnimationPlayerActivity.kt`**

```kotlin
package com.example.filament.samples

import android.app.Activity
import android.os.Bundle
import android.view.Choreographer
import android.view.SurfaceView
import android.view.MotionEvent
import android.widget.Button
import android.widget.SeekBar
import android.widget.TextView
import com.google.android.filament.*
import com.google.android.filament.gltfio.AssetLoader
import com.google.android.filament.gltfio.FilamentAsset
import com.google.android.filament.gltfio.ResourceLoader
import com.google.android.filament.gltfio.Animator
import com.google.android.filament.utils.*
import java.nio.ByteBuffer

/**
 * Filament 动画播放器示例 - Android 实现
 *
 * 功能：
 * - 加载和播放 glTF 骨骼动画
 * - 动画控制（播放、暂停、速度调节）
 * - 多动画切换
 * - UI 控制界面
 */
class AnimationPlayerActivity : Activity() {

    // Filament 组件
    private lateinit var surfaceView: SurfaceView
    private lateinit var choreographer: Choreographer
    private lateinit var modelViewer: ModelViewer

    // UI 组件
    private lateinit var playButton: Button
    private lateinit var pauseButton: Button
    private lateinit var speedSeekBar: SeekBar
    private lateinit var animationSpinner: android.widget.Spinner
    private lateinit var infoTextView: TextView

    // glTF 资源
    private var assetLoader: AssetLoader? = null
    private var resourceLoader: ResourceLoader? = null
    private var asset: FilamentAsset? = null
    private var animator: Animator? = null

    // 动画状态
    private var currentAnimationIndex = 0
    private var animationTime = 0.0f
    private var playbackSpeed = 1.0f
    private var isPlaying = true
    private var lastFrameTimeNanos = 0L

    companion object {
        init {
            Utils.init()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_animation_player)

        // 初始化 UI
        surfaceView = findViewById(R.id.surfaceView)
        playButton = findViewById(R.id.playButton)
        pauseButton = findViewById(R.id.pauseButton)
        speedSeekBar = findViewById(R.id.speedSeekBar)
        animationSpinner = findViewById(R.id.animationSpinner)
        infoTextView = findViewById(R.id.infoTextView)

        // 初始化 ModelViewer
        choreographer = Choreographer.getInstance()
        modelViewer = ModelViewer(surfaceView)

        // 设置相机
        setupCamera()

        // 加载模型
        loadGLTFAsset()

        // 设置 UI 监听器
        setupUIListeners()

        // 启动渲染循环
        choreographer.postFrameCallback(frameCallback)
    }

    private fun setupCamera() {
        modelViewer.scene.skybox = Skybox.Builder().build(modelViewer.engine)
        modelViewer.scene.indirectLight = IndirectLight.Builder()
            .intensity(30000.0f)
            .build(modelViewer.engine)

        // 设置相机位置
        modelViewer.view.camera.apply {
            setProjection(45.0,
                          surfaceView.width.toDouble() / surfaceView.height,
                          0.1, 100.0, Camera.Fov.VERTICAL)
            lookAt(
                0.0, 1.6, 4.0,    // eye
                0.0, 1.0, 0.0,    // center
                0.0, 1.0, 0.0     // up
            )
        }
    }

    private fun loadGLTFAsset() {
        // 从 assets 读取 glTF 文件
        val buffer = readAsset("models/animated_character.glb")

        // 创建加载器
        assetLoader = AssetLoader(modelViewer.engine,
                                 MaterialProvider(modelViewer.engine),
                                 EntityManager.get())

        resourceLoader = ResourceLoader(modelViewer.engine,
                                       normalizeSkinningWeights = true)

        // 加载资产
        asset = assetLoader?.createAsset(buffer)
        asset?.let { asset ->
            // 加载资源
            resourceLoader?.loadResources(asset)

            // 等待资源加载完成（实际应用中应该异步处理）
            resourceLoader?.asyncBeginLoad(asset)

            // 添加到场景
            modelViewer.scene.addEntities(asset.entities)

            // 创建动画控制器
            if (asset.animationCount > 0) {
                animator = asset.getInstance()?.animator

                // 更新 UI
                updateAnimationList()
                updateInfoText()
            }
        }
    }

    private fun readAsset(assetName: String): ByteBuffer {
        val input = assets.open(assetName)
        val bytes = ByteArray(input.available())
        input.read(bytes)
        input.close()
        return ByteBuffer.wrap(bytes)
    }

    private fun setupUIListeners() {
        // 播放按钮
        playButton.setOnClickListener {
            isPlaying = true
        }

        // 暂停按钮
        pauseButton.setOnClickListener {
            isPlaying = false
        }

        // 速度滑块（0.1x - 3.0x）
        speedSeekBar.max = 290  // (3.0 - 0.1) * 100
        speedSeekBar.progress = 90  // 默认 1.0x
        speedSeekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                playbackSpeed = 0.1f + progress / 100.0f
                updateInfoText()
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        // 动画选择器
        animationSpinner.onItemSelectedListener = object :
            android.widget.AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: android.widget.AdapterView<*>?,
                                      view: android.view.View?,
                                      position: Int, id: Long) {
                playAnimation(position)
            }
            override fun onNothingSelected(parent: android.widget.AdapterView<*>?) {}
        }
    }

    private fun updateAnimationList() {
        asset?.let { asset ->
            val animationNames = mutableListOf<String>()
            for (i in 0 until asset.animationCount) {
                val name = asset.getAnimationName(i) ?: "Animation $i"
                animationNames.add(name)
            }

            val adapter = android.widget.ArrayAdapter(
                this,
                android.R.layout.simple_spinner_item,
                animationNames
            )
            adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
            animationSpinner.adapter = adapter
        }
    }

    private fun updateInfoText() {
        animator?.let { anim ->
            val duration = anim.getAnimationDuration(currentAnimationIndex)
            val info = """
                Animation: ${asset?.getAnimationName(currentAnimationIndex) ?: "N/A"}
                Time: ${"%.2f".format(animationTime)} / ${"%.2f".format(duration)}s
                Speed: ${"%.1f".format(playbackSpeed)}x
                Status: ${if (isPlaying) "Playing" else "Paused"}
            """.trimIndent()

            infoTextView.text = info
        }
    }

    private fun playAnimation(index: Int) {
        animator?.let {
            currentAnimationIndex = index
            animationTime = 0.0f
            updateInfoText()
        }
    }

    private val frameCallback = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            // 计算 delta time
            val deltaTime = if (lastFrameTimeNanos != 0L) {
                (frameTimeNanos - lastFrameTimeNanos) / 1_000_000_000.0f
            } else {
                0.0f
            }
            lastFrameTimeNanos = frameTimeNanos

            // 更新动画
            updateAnimation(deltaTime)

            // 渲染
            modelViewer.render(frameTimeNanos)

            // 下一帧
            choreographer.postFrameCallback(this)
        }
    }

    private fun updateAnimation(deltaTime: Float) {
        if (!isPlaying) return

        animator?.let { anim ->
            // 更新动画时间
            animationTime += deltaTime * playbackSpeed

            // 获取当前动画时长
            val duration = anim.getAnimationDuration(currentAnimationIndex)

            // 循环处理
            if (animationTime > duration) {
                animationTime -= duration
            }

            // 应用动画
            anim.applyAnimation(currentAnimationIndex, animationTime)
            anim.updateBoneMatrices()

            // 更新 UI（每 10 帧更新一次）
            if (frameTimeNanos % 10 == 0L) {
                runOnUiThread { updateInfoText() }
            }
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

        // 清理资源
        choreographer.removeFrameCallback(frameCallback)

        asset?.let { assetLoader?.destroyAsset(it) }
        assetLoader?.destroy()
        resourceLoader?.destroy()

        modelViewer.destroy()
    }
}
```

**布局文件：`res/layout/activity_animation_player.xml`**

```xml
<?xml version="1.0" encoding="utf-8"?>
<LinearLayout xmlns:android="http://schemas.android.com/apk/res/android"
    android:layout_width="match_parent"
    android:layout_height="match_parent"
    android:orientation="vertical">

    <!-- 渲染视图 -->
    <SurfaceView
        android:id="@+id/surfaceView"
        android:layout_width="match_parent"
        android:layout_height="0dp"
        android:layout_weight="1" />

    <!-- 控制面板 -->
    <LinearLayout
        android:layout_width="match_parent"
        android:layout_height="wrap_content"
        android:orientation="vertical"
        android:padding="16dp"
        android:background="#f0f0f0">

        <!-- 信息显示 -->
        <TextView
            android:id="@+id/infoTextView"
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:text="Animation Info"
            android:textSize="12sp"
            android:fontFamily="monospace"
            android:layout_marginBottom="8dp" />

        <!-- 动画选择 -->
        <Spinner
            android:id="@+id/animationSpinner"
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:layout_marginBottom="8dp" />

        <!-- 播放控制按钮 -->
        <LinearLayout
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:orientation="horizontal"
            android:layout_marginBottom="8dp">

            <Button
                android:id="@+id/playButton"
                android:layout_width="0dp"
                android:layout_height="wrap_content"
                android:layout_weight="1"
                android:text="Play"
                android:layout_marginEnd="4dp" />

            <Button
                android:id="@+id/pauseButton"
                android:layout_width="0dp"
                android:layout_height="wrap_content"
                android:layout_weight="1"
                android:text="Pause"
                android:layout_marginStart="4dp" />
        </LinearLayout>

        <!-- 速度控制 -->
        <TextView
            android:layout_width="wrap_content"
            android:layout_height="wrap_content"
            android:text="Playback Speed"
            android:textSize="14sp"
            android:layout_marginBottom="4dp" />

        <SeekBar
            android:id="@+id/speedSeekBar"
            android:layout_width="match_parent"
            android:layout_height="wrap_content" />
    </LinearLayout>
</LinearLayout>
```

---

## 构建和运行

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.19)
project(animation_player)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Filament 路径
set(FILAMENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Filament root directory")

# 查找 Filament 包
find_package(filament REQUIRED CONFIG PATHS ${FILAMENT_DIR}/out/cmake-release)

# 添加可执行文件
add_executable(animation_player
    samples/animation_player.cpp
)

# 链接 Filament 库
target_link_libraries(animation_player PRIVATE
    filament
    filamat
    gltfio_core
    gltfio
    utils
    filamentapp
)

# 包含目录
target_include_directories(animation_player PRIVATE
    ${FILAMENT_DIR}/filament/include
    ${FILAMENT_DIR}/libs/gltfio/include
    ${FILAMENT_DIR}/libs/utils/include
    ${FILAMENT_DIR}/libs/math/include
    ${FILAMENT_DIR}/libs/filamentapp/include
)

# 复制资源文件
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# 安装
install(TARGETS animation_player DESTINATION bin)
```

### 编译和运行

```bash
# 创建构建目录
mkdir build && cd build

# 配置 CMake
cmake -DCMAKE_BUILD_TYPE=Release \
      -DFILAMENT_DIR=/path/to/filament ..

# 编译
make -j8

# 运行
./animation_player

# 或者指定模型路径
./animation_player --model=/path/to/model.glb
```

### Android 构建

```bash
# 在 Android Studio 中打开项目
# 或使用 Gradle 命令行

cd android
./gradlew assembleDebug

# 安装到设备
adb install app/build/outputs/apk/debug/app-debug.apk

# 运行
adb shell am start -n com.example.filament.samples/.AnimationPlayerActivity
```

---

## 性能优化

### 1. 骨骼更新优化

```cpp
// 仅更新可见的骨骼
class OptimizedAnimator {
public:
    void updateVisibleBones(const std::vector<bool>& visibilityMask) {
        // 仅更新可见骨骼的变换矩阵
        for (size_t i = 0; i < bones.size(); ++i) {
            if (visibilityMask[i]) {
                updateBone(i);
            }
        }
    }

    // LOD based animation update
    void updateWithLOD(float distanceToCamera) {
        if (distanceToCamera < 10.0f) {
            // 高质量：每帧更新
            updateAllBones();
        } else if (distanceToCamera < 50.0f) {
            // 中等质量：每 2 帧更新
            if (frameCount % 2 == 0) {
                updateAllBones();
            }
        } else {
            // 低质量：每 4 帧更新
            if (frameCount % 4 == 0) {
                updateAllBones();
            }
        }
        frameCount++;
    }

private:
    std::vector<Bone> bones;
    size_t frameCount = 0;
};
```

### 2. 动画数据缓存

```cpp
// 缓存常用动画姿势
class AnimationCache {
public:
    struct CachedPose {
        std::vector<mat4> boneMatrices;
        float timestamp;
    };

    std::map<size_t, std::vector<CachedPose>> cache;

    const std::vector<mat4>* getCachedPose(size_t animIndex, float time) {
        auto it = cache.find(animIndex);
        if (it != cache.end()) {
            // 查找最接近的缓存姿势
            for (const auto& pose : it->second) {
                if (std::abs(pose.timestamp - time) < 0.01f) {
                    return &pose.boneMatrices;
                }
            }
        }
        return nullptr;
    }

    void cachePose(size_t animIndex, float time,
                   const std::vector<mat4>& matrices) {
        cache[animIndex].push_back({matrices, time});
    }
};
```

### 3. 多线程动画更新

```cpp
#include <thread>
#include <future>

class ParallelAnimator {
public:
    void updateAnimationsParallel(std::vector<gltfio::Animator*>& animators,
                                  float deltaTime) {
        std::vector<std::future<void>> futures;

        for (auto* animator : animators) {
            futures.push_back(std::async(std::launch::async, [animator, deltaTime]() {
                // 在独立线程中更新动画
                animator->applyAnimation(0, deltaTime);
                animator->updateBoneMatrices();
            }));
        }

        // 等待所有任务完成
        for (auto& future : futures) {
            future.wait();
        }
    }
};
```

### 4. 内存优化

```cpp
// 使用对象池管理动画资源
template<typename T>
class ObjectPool {
public:
    T* acquire() {
        if (available.empty()) {
            return new T();
        }
        T* obj = available.back();
        available.pop_back();
        return obj;
    }

    void release(T* obj) {
        available.push_back(obj);
    }

    ~ObjectPool() {
        for (auto* obj : available) {
            delete obj;
        }
    }

private:
    std::vector<T*> available;
};

// 使用示例
ObjectPool<AnimationBlender> blenderPool;
auto* blender = blenderPool.acquire();
// 使用 blender...
blenderPool.release(blender);
```

---

## 常见问题

### Q1: 动画播放速度不正确？

**A:** 检查以下几点：

```cpp
// 1. 确保 deltaTime 单位正确（秒）
float deltaTime = frameDuration / 1000.0f;  // 如果 frameDuration 是毫秒

// 2. 检查动画时长单位
float duration = animator->getAnimationDuration(index);  // 返回秒

// 3. 验证 applyAnimation 调用
animator->applyAnimation(index, time);  // time 单位是秒

// 4. 确认播放速度倍率
float adjustedDelta = deltaTime * playbackSpeed;
```

### Q2: 动画混合出现跳变？

**A:** 使用平滑过渡：

```cpp
// 错误：直接切换权重
weight = targetWeight;  // 会导致跳变

// 正确：平滑插值
float blendSpeed = 1.0f / blendDuration;
weight = std::lerp(weight, targetWeight, blendSpeed * deltaTime);

// 或使用 smoothstep
float t = clamp((elapsedTime / blendDuration), 0.0f, 1.0f);
float smoothT = t * t * (3.0f - 2.0f * t);  // smoothstep
weight = std::lerp(startWeight, targetWeight, smoothT);
```

### Q3: 骨骼变换不正确？

**A:** 检查变换顺序和空间：

```cpp
// 1. 确保在正确的空间计算
// 骨骼变换通常在局部空间

// 2. 检查变换矩阵顺序
// Filament 使用列主序矩阵
mat4 boneMatrix = parentMatrix * localMatrix;

// 3. 验证骨骼层级更新顺序
// 必须先更新父骨骼，再更新子骨骼
void updateBoneHierarchy(BoneNode* node, const mat4& parentTransform) {
    mat4 localTransform = getLocalTransform(node);
    mat4 globalTransform = parentTransform * localTransform;

    for (auto* child : node->children) {
        updateBoneHierarchy(child, globalTransform);
    }
}
```

### Q4: 如何实现动画循环无缝衔接？

**A:** 使用循环动画技术：

```cpp
// 1. 使用模运算确保时间循环
float loopTime(float time, float duration) {
    return fmod(time, duration);
}

// 2. 对于非循环动画，添加过渡
if (time >= duration && !isLooping) {
    // 过渡回初始姿势
    float blendTime = 0.3f;
    float t = (time - duration) / blendTime;

    if (t < 1.0f) {
        // 混合当前姿势和初始姿势
        blendPoses(currentPose, initialPose, t);
    } else {
        // 完全回到初始姿势
        currentPose = initialPose;
        time = 0.0f;
    }
}

// 3. 使用动画设计器创建循环友好的动画
// 确保首尾帧相同或接近
```

### Q5: 多个模型实例如何共享动画数据？

**A:** 使用实例化技术：

```cpp
// 共享动画数据，独立动画状态
class SharedAnimationData {
public:
    gltfio::FilamentAsset* sharedAsset;  // 共享的资产

    struct Instance {
        gltfio::FilamentInstance* instance;
        gltfio::Animator* animator;
        float currentTime;
        size_t currentAnimation;
    };

    std::vector<Instance> instances;

    void createInstance() {
        auto* inst = sharedAsset->createInstance();
        auto* anim = inst->getAnimator();
        instances.push_back({inst, anim, 0.0f, 0});
    }

    void updateAll(float deltaTime) {
        for (auto& inst : instances) {
            inst.currentTime += deltaTime;
            inst.animator->applyAnimation(inst.currentAnimation, inst.currentTime);
            inst.animator->updateBoneMatrices();
        }
    }
};
```

### Q6: 如何调试动画问题？

**A:** 使用可视化调试工具：

```cpp
// 1. 渲染骨骼调试线
void renderBoneSkeleton(gltfio::Animator* animator) {
    auto& tcm = engine->getTransformManager();

    for (size_t i = 0; i < boneCount; ++i) {
        mat4 boneMatrix = animator->getBoneMatrix(i);
        float3 bonePos = boneMatrix[3].xyz;

        // 渲染骨骼位置（球体）
        renderDebugSphere(bonePos, 0.05f);

        // 渲染骨骼连接（线段）
        int parentIndex = getBoneParent(i);
        if (parentIndex >= 0) {
            mat4 parentMatrix = animator->getBoneMatrix(parentIndex);
            float3 parentPos = parentMatrix[3].xyz;
            renderDebugLine(bonePos, parentPos);
        }
    }
}

// 2. 打印动画信息
void printAnimatorDebugInfo(gltfio::Animator* animator, size_t animIndex) {
    std::cout << "Animation: " << animIndex << std::endl;
    std::cout << "Duration: " << animator->getAnimationDuration(animIndex) << "s" << std::endl;
    std::cout << "Bone count: " << animator->getBoneCount() << std::endl;

    // 打印每个骨骼的当前变换
    for (size_t i = 0; i < animator->getBoneCount(); ++i) {
        mat4 m = animator->getBoneMatrix(i);
        std::cout << "Bone " << i << ": pos=" << m[3].xyz << std::endl;
    }
}
```

---

## 相关文档

- [03-pbr-model.md](./03-pbr-model.md) - PBR 模型渲染基础
- [04-gltf-viewer.md](./04-gltf-viewer.md) - glTF 查看器实现
- [07-scene-manager.md](./07-scene-manager.md) - 场景和资源管理
- [../tools/matc.md](../tools/matc.md) - 材质编译器使用
- [../platforms/android.md](../platforms/android.md) - Android 平台集成

---

## 总结

本示例展示了 Filament 中骨骼动画系统的完整实现，包括：

1. **基础动画播放** - 使用 gltfio::Animator 控制动画
2. **动画混合** - 实现多动画平滑过渡
3. **状态机** - 管理复杂的动画状态转换
4. **性能优化** - LOD、缓存、多线程等技术
5. **跨平台实现** - Desktop (C++) 和 Android (Kotlin)

通过这个示例，你应该能够：
- 加载和播放 glTF 骨骼动画
- 实现动画控制系统
- 优化动画性能
- 解决常见的动画问题

下一步可以学习 [07-scene-manager.md](./07-scene-manager.md) 了解场景管理系统。
