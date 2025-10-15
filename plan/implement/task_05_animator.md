# 任务5: StandaloneAnimator实现

**预计时间**: 3小时
**前置依赖**: 任务2, 任务4完成
**完成标志**: 能播放动画并更新骨骼变换

---

## 目标

实现StandaloneAnimator核心功能：
1. 绑定SkeletonAsset
2. 播放多个AnimationAsset（支持权重）
3. 名称到索引的映射缓存（性能优化）
4. 每帧更新骨骼变换
5. 计算最终骨骼矩阵

---

## 1. 完善 FStandaloneAnimator.h

**文件**: `libs/gltfio_ext/src/FStandaloneAnimator.h`

```cpp
#ifndef GLTFIO_EXT_FSTANDALONEANIMATOR_H
#define GLTFIO_EXT_FSTANDALONEANIMATOR_H

#include <gltfio/StandaloneAnimator.h>
#include <filament/TransformManager.h>
#include <math/mat4.h>
#include <vector>

namespace filament {
    class Engine;
}

namespace filament::gltfio {

class SkeletonAsset;
class AnimationAsset;
struct Sampler;
struct Channel;

// 绑定后的通道（运行时使用索引）
struct BoundChannel {
    const Sampler* sampler;
    size_t boneIndex;  // ⭐ 缓存索引，避免每帧查找
    enum { TRANSLATION, ROTATION, SCALE, WEIGHTS } transformType;
};

// 动画播放状态
struct AnimationState {
    AnimationAsset* animation;
    float time = 0.0f;
    float weight = 1.0f;
    bool loop = true;
    bool playing = true;
    int id;

    // 绑定后的通道列表
    std::vector<BoundChannel> boundChannels;
};

struct AnimatorImpl {
    filament::Engine* engine = nullptr;
    filament::TransformManager* transformManager = nullptr;

    // 绑定的骨骼
    SkeletonAsset* skeleton = nullptr;

    // 播放中的动画
    std::vector<AnimationState> animations;
    int nextAnimationId = 1;

    // 骨骼变换缓存
    std::vector<filament::math::mat4f> boneMatrices;

    // 更新逻辑
    void applyAnimation(const AnimationState& state);
    void computeBoneMatrices();
};

} // namespace filament::gltfio

#endif // GLTFIO_EXT_FSTANDALONEANIMATOR_H
```

---

## 2. 实现 StandaloneAnimator.cpp

**文件**: `libs/gltfio_ext/src/StandaloneAnimator.cpp`

```cpp
#include <gltfio/StandaloneAnimator.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/AnimationAsset.h>
#include "FStandaloneAnimator.h"
#include "FAnimationAsset.h"
#include "FSkeletonAsset.h"
#include "GltfioExtInternal.h"

#include <filament/Engine.h>
#include <filament/TransformManager.h>
#include <math/quat.h>
#include <math/vec3.h>
#include <math/vec4.h>
#include <math/scalar.h>
#include <algorithm>

using namespace filament;
using namespace filament::math;

namespace filament::gltfio {

// 辅助函数：插值计算
static float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static float3 lerp(const float3& a, const float3& b, float t) {
    return a + (b - a) * t;
}

static float4 lerp(const float4& a, const float4& b, float t) {
    return a + (b - a) * t;
}

static quatf slerp(const quatf& a, const quatf& b, float t) {
    return normalize(nlerp(a, b, t));
}

// 三次样条插值
template<typename T>
static T cubicSpline(const T& v0, const T& tangent0, const T& v1, const T& tangent1, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return (2.0f * t3 - 3.0f * t2 + 1.0f) * v0 +
           (t3 - 2.0f * t2 + t) * tangent0 +
           (-2.0f * t3 + 3.0f * t2) * v1 +
           (t3 - t2) * tangent1;
}

StandaloneAnimator* StandaloneAnimator::create(Engine& engine) {
    return new StandaloneAnimator(engine);
}

void StandaloneAnimator::destroy(StandaloneAnimator** animator) {
    delete *animator;
    *animator = nullptr;
}

StandaloneAnimator::StandaloneAnimator(Engine& engine) {
    mImpl = new AnimatorImpl();
    mImpl->engine = &engine;
    mImpl->transformManager = &engine.getTransformManager();
}

StandaloneAnimator::~StandaloneAnimator() {
    delete mImpl;
}

void StandaloneAnimator::bindSkeleton(SkeletonAsset* skeleton) noexcept {
    mImpl->skeleton = skeleton;
    mImpl->boneMatrices.resize(skeleton->getBoneCount());

    // 重新绑定所有动画的通道（建立名称→索引映射）
    for (auto& state : mImpl->animations) {
        auto* animAsset = static_cast<FAnimationAsset*>(state.animation);

        state.boundChannels.clear();
        state.boundChannels.reserve(animAsset->mChannels.size());

        for (const auto& srcChannel : animAsset->mChannels) {
            int boneIndex = skeleton->getBoneIndex(srcChannel.targetBoneName.c_str());
            if (boneIndex >= 0) {
                BoundChannel bound;
                bound.sampler = srcChannel.sampler;
                bound.boneIndex = static_cast<size_t>(boneIndex);

                switch (srcChannel.transformType) {
                    case Channel::TRANSLATION:
                        bound.transformType = BoundChannel::TRANSLATION;
                        break;
                    case Channel::ROTATION:
                        bound.transformType = BoundChannel::ROTATION;
                        break;
                    case Channel::SCALE:
                        bound.transformType = BoundChannel::SCALE;
                        break;
                    case Channel::WEIGHTS:
                        bound.transformType = BoundChannel::WEIGHTS;
                        break;
                }

                state.boundChannels.push_back(bound);
            } else {
                if (GLTFIO_EXT_VERBOSE) {
                    GLTFIO_EXT_WARN("Bone not found for animation channel: " << srcChannel.targetBoneName);
                }
            }
        }
    }
}

int StandaloneAnimator::playAnimation(AnimationAsset* animation, float weight, bool loop) noexcept {
    if (!mImpl->skeleton) {
        GLTFIO_EXT_WARN("No skeleton bound to animator");
        return -1;
    }

    AnimationState state;
    state.animation = animation;
    state.time = 0.0f;
    state.weight = weight;
    state.loop = loop;
    state.playing = true;
    state.id = mImpl->nextAnimationId++;

    // 绑定通道（名称→索引映射）
    auto* animAsset = static_cast<FAnimationAsset*>(animation);
    state.boundChannels.reserve(animAsset->mChannels.size());

    for (const auto& srcChannel : animAsset->mChannels) {
        int boneIndex = mImpl->skeleton->getBoneIndex(srcChannel.targetBoneName.c_str());
        if (boneIndex >= 0) {
            BoundChannel bound;
            bound.sampler = srcChannel.sampler;
            bound.boneIndex = static_cast<size_t>(boneIndex);

            switch (srcChannel.transformType) {
                case Channel::TRANSLATION:
                    bound.transformType = BoundChannel::TRANSLATION;
                    break;
                case Channel::ROTATION:
                    bound.transformType = BoundChannel::ROTATION;
                    break;
                case Channel::SCALE:
                    bound.transformType = BoundChannel::SCALE;
                    break;
                case Channel::WEIGHTS:
                    bound.transformType = BoundChannel::WEIGHTS;
                    break;
            }

            state.boundChannels.push_back(bound);
        }
    }

    mImpl->animations.push_back(state);
    return state.id;
}

void StandaloneAnimator::stopAnimation(int animationId) noexcept {
    auto it = std::remove_if(mImpl->animations.begin(), mImpl->animations.end(),
                             [animationId](const AnimationState& state) {
                                 return state.id == animationId;
                             });
    mImpl->animations.erase(it, mImpl->animations.end());
}

void StandaloneAnimator::setAnimationWeight(int animationId, float weight) noexcept {
    for (auto& state : mImpl->animations) {
        if (state.id == animationId) {
            state.weight = weight;
            break;
        }
    }
}

void StandaloneAnimator::update(float deltaTime) noexcept {
    if (!mImpl->skeleton) {
        return;
    }

    // 1. 更新所有动画时间
    for (auto& state : mImpl->animations) {
        if (!state.playing) {
            continue;
        }

        state.time += deltaTime;

        float duration = state.animation->getDuration();
        if (state.loop && state.time > duration) {
            state.time = fmod(state.time, duration);
        }
    }

    // 2. 应用动画到骨骼变换
    mImpl->transformManager->openLocalTransformTransaction();

    for (const auto& state : mImpl->animations) {
        if (state.playing && state.weight > 0.0f) {
            mImpl->applyAnimation(state);
        }
    }

    mImpl->transformManager->commitLocalTransformTransaction();

    // 3. 计算最终骨骼矩阵
    mImpl->computeBoneMatrices();
}

const mat4f* StandaloneAnimator::getBoneMatrices() const noexcept {
    return mImpl->boneMatrices.empty() ? nullptr : mImpl->boneMatrices.data();
}

size_t StandaloneAnimator::getBoneCount() const noexcept {
    return mImpl->boneMatrices.size();
}

void StandaloneAnimator::resetToBindPose() noexcept {
    // 停止所有动画
    mImpl->animations.clear();

    if (!mImpl->skeleton) {
        return;
    }

    // 重置所有骨骼变换为单位矩阵
    mImpl->transformManager->openLocalTransformTransaction();

    for (size_t i = 0; i < mImpl->skeleton->getBoneCount(); ++i) {
        Entity boneEntity = mImpl->skeleton->getBoneEntity(i);
        TransformManager::Instance ti = mImpl->transformManager->getInstance(boneEntity);
        if (ti) {
            mImpl->transformManager->setTransform(ti, mat4f());
        }
    }

    mImpl->transformManager->commitLocalTransformTransaction();

    // 计算骨骼矩阵
    mImpl->computeBoneMatrices();
}

// ========== AnimatorImpl 实现 ==========

void AnimatorImpl::applyAnimation(const AnimationState& state) {
    float time = state.time;

    for (const auto& channel : state.boundChannels) {
        const Sampler* sampler = channel.sampler;
        if (sampler->times.size() < 2) {
            continue;
        }

        // 查找关键帧
        auto iter = sampler->times.lower_bound(time);

        float t = 0.0f;
        size_t nextIndex, prevIndex;

        if (iter == sampler->times.end()) {
            nextIndex = sampler->times.size() - 1;
            prevIndex = nextIndex;
        } else if (iter == sampler->times.begin()) {
            nextIndex = 0;
            prevIndex = 0;
        } else {
            auto prev = iter;
            --prev;
            nextIndex = iter->second;
            prevIndex = prev->second;
            float nextTime = iter->first;
            float prevTime = prev->first;
            float deltaTime = nextTime - prevTime;
            if (deltaTime > 0) {
                t = (time - prevTime) / deltaTime;
            }
        }

        if (sampler->interpolation == Sampler::STEP) {
            t = 0.0f;
        }

        // 应用变换
        Entity boneEntity = skeleton->getBoneEntity(channel.boneIndex);
        TransformManager::Instance ti = transformManager->getInstance(boneEntity);

        if (!ti) {
            continue;
        }

        switch (channel.transformType) {
            case BoundChannel::TRANSLATION: {
                const float3* values = reinterpret_cast<const float3*>(sampler->values.data());
                float3 translation;

                if (sampler->interpolation == Sampler::CUBIC) {
                    float3 v0 = values[prevIndex * 3 + 1];
                    float3 t0 = values[prevIndex * 3 + 2];
                    float3 t1 = values[nextIndex * 3];
                    float3 v1 = values[nextIndex * 3 + 1];
                    translation = cubicSpline(v0, t0, v1, t1, t);
                } else {
                    translation = lerp(values[prevIndex], values[nextIndex], t);
                }

                mat4f currentTransform = transformManager->getTransform(ti);
                float3 currentTranslation, currentScale;
                quatf currentRotation;
                decomposeMatrix(currentTransform, &currentTranslation, &currentRotation, &currentScale);

                // 混合权重
                currentTranslation = lerp(currentTranslation, translation, state.weight);
                transformManager->setTransform(ti, composeMatrix(currentTranslation, currentRotation, currentScale));
                break;
            }

            case BoundChannel::ROTATION: {
                const quatf* values = reinterpret_cast<const quatf*>(sampler->values.data());
                quatf rotation;

                if (sampler->interpolation == Sampler::CUBIC) {
                    quatf v0 = values[prevIndex * 3 + 1];
                    quatf t0 = values[prevIndex * 3 + 2];
                    quatf t1 = values[nextIndex * 3];
                    quatf v1 = values[nextIndex * 3 + 1];
                    rotation = normalize(cubicSpline(v0, t0, v1, t1, t));
                } else {
                    rotation = slerp(values[prevIndex], values[nextIndex], t);
                }

                mat4f currentTransform = transformManager->getTransform(ti);
                float3 currentTranslation, currentScale;
                quatf currentRotation;
                decomposeMatrix(currentTransform, &currentTranslation, &currentRotation, &currentScale);

                currentRotation = slerp(currentRotation, rotation, state.weight);
                transformManager->setTransform(ti, composeMatrix(currentTranslation, currentRotation, currentScale));
                break;
            }

            case BoundChannel::SCALE: {
                const float3* values = reinterpret_cast<const float3*>(sampler->values.data());
                float3 scale;

                if (sampler->interpolation == Sampler::CUBIC) {
                    float3 v0 = values[prevIndex * 3 + 1];
                    float3 t0 = values[prevIndex * 3 + 2];
                    float3 t1 = values[nextIndex * 3];
                    float3 v1 = values[nextIndex * 3 + 1];
                    scale = cubicSpline(v0, t0, v1, t1, t);
                } else {
                    scale = lerp(values[prevIndex], values[nextIndex], t);
                }

                mat4f currentTransform = transformManager->getTransform(ti);
                float3 currentTranslation, currentScale;
                quatf currentRotation;
                decomposeMatrix(currentTransform, &currentTranslation, &currentRotation, &currentScale);

                currentScale = lerp(currentScale, scale, state.weight);
                transformManager->setTransform(ti, composeMatrix(currentTranslation, currentRotation, currentScale));
                break;
            }

            case BoundChannel::WEIGHTS:
                // Morph targets支持（暂不实现）
                break;
        }
    }
}

void AnimatorImpl::computeBoneMatrices() {
    size_t boneCount = skeleton->getBoneCount();
    const mat4f* invBindMatrices = skeleton->getInverseBindMatrices();

    for (size_t i = 0; i < boneCount; ++i) {
        Entity boneEntity = skeleton->getBoneEntity(i);
        TransformManager::Instance ti = transformManager->getInstance(boneEntity);

        // 骨骼矩阵 = 世界变换 × 逆绑定矩阵
        mat4f worldTransform = transformManager->getWorldTransform(ti);
        boneMatrices[i] = worldTransform * invBindMatrices[i];
    }
}

} // namespace filament::gltfio
```

---

## 3. 创建测试程序

**文件**: `libs/gltfio_ext/tests/test_animator.cpp`

```cpp
#include <gltfio/AssetLoaderExt.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/AnimationAsset.h>
#include <gltfio/StandaloneAnimator.h>
#include <gltfio/MaterialProvider.h>

#include <filament/Engine.h>
#include <iostream>
#include <fstream>
#include <vector>

using namespace filament;
using namespace filament::gltfio;

static std::vector<uint8_t> readFile(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }
    size_t size = file.tellg();
    std::vector<uint8_t> buffer(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}

int main() {
    Engine* engine = Engine::create();

    MaterialProvider* materials = createUbershaderProvider(engine);
    AssetConfigurationExt config = {
        .engine = engine,
        .materials = materials
    };
    AssetLoaderExt* loader = AssetLoaderExt::create(config);

    std::vector<uint8_t> data = readFile("third_party/models/AnimatedMorphCube/AnimatedMorphCube.glb");

    // 1. 加载骨骼和动画
    SkeletonAsset* skeleton = loader->loadSkeleton(data.data(), data.size());
    AnimationAsset* anim = loader->loadAnimation(data.data(), data.size());

    if (!skeleton || !anim) {
        std::cerr << "Failed to load assets" << std::endl;
        return 1;
    }

    std::cout << "=== Animator Test ===" << std::endl;
    std::cout << "Skeleton bones: " << skeleton->getBoneCount() << std::endl;
    std::cout << "Animation duration: " << anim->getDuration() << "s" << std::endl;

    // 2. 创建动画器
    StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
    animator->bindSkeleton(skeleton);

    // 3. 播放动画
    int animId = animator->playAnimation(anim, 1.0f, true);
    std::cout << "Playing animation ID: " << animId << std::endl;

    // 4. 模拟更新
    std::cout << "\nSimulating 5 frames..." << std::endl;
    for (int i = 0; i < 5; ++i) {
        animator->update(1.0f / 60.0f);

        const auto* boneMatrices = animator->getBoneMatrices();
        if (boneMatrices) {
            std::cout << "Frame " << i << ": bone[0] matrix[0][0] = "
                      << boneMatrices[0][0][0] << std::endl;
        }
    }

    // 5. 测试权重
    std::cout << "\nTesting weight adjustment..." << std::endl;
    animator->setAnimationWeight(animId, 0.5f);
    animator->update(1.0f / 60.0f);
    std::cout << "Weight set to 0.5" << std::endl;

    // 6. 测试停止
    animator->stopAnimation(animId);
    std::cout << "Animation stopped" << std::endl;

    // 7. 测试重置
    animator->resetToBindPose();
    std::cout << "Reset to bind pose" << std::endl;

    // 清理
    StandaloneAnimator::destroy(&animator);
    loader->destroyAnimation(anim);
    loader->destroySkeleton(skeleton);
    AssetLoaderExt::destroy(&loader);
    materials->destroyMaterials();
    delete materials;
    Engine::destroy(&engine);

    std::cout << "\n=== Test PASSED ===" << std::endl;
    return 0;
}
```

---

## 4. 更新CMakeLists.txt

**文件**: `libs/gltfio_ext/CMakeLists.txt` (追加)

```cmake
if (NOT WEBGL AND NOT ANDROID AND NOT IOS)
    add_executable(test_animator tests/test_animator.cpp)
    target_link_libraries(test_animator PRIVATE gltfio_ext)
    set_target_properties(test_animator PROPERTIES FOLDER Tests)
endif()
```

---

## 5. 验证步骤

```bash
cd out
ninja gltfio_ext test_animator
./libs/test_animator

# 预期输出:
# === Animator Test ===
# Skeleton bones: N
# Animation duration: Xs
# Playing animation ID: 1
# Simulating 5 frames...
# Frame 0: bone[0] matrix[0][0] = <value>
# ...
# Testing weight adjustment...
# Weight set to 0.5
# Animation stopped
# Reset to bind pose
# === Test PASSED ===
```

---

## 完成标志

- [ ] 名称到索引映射缓存实现
- [ ] playAnimation() 支持权重
- [ ] update() 正确更新骨骼变换
- [ ] computeBoneMatrices() 计算正确
- [ ] 测试程序运行成功

完成后进入 **任务6: 集成测试 + 示例程序**
