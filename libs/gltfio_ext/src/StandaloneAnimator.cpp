/**
 * @file StandaloneAnimator.cpp
 * @brief 独立动画播放器实现 - 支持动画混合和TRS分解/组合
 *
 * ========== 核心设计 ==========
 *
 * 1. TRS分解/组合修复（解决单通道动画bug）：
 *    问题：glTF动画可能只包含部分通道（如只有ROTATION通道）
 *          如果直接覆盖Transform，会丢失其他分量（T和S被置零）
 *
 *    解决方案：TRS分解→修改→重组
 *    a) decomposeTRS(): 从当前Transform分解出T/R/S三个分量
 *    b) 应用动画数据：只修改当前通道对应的分量（如只修改R）
 *    c) composeTRS(): 重新组合T/R/S为新Transform矩阵
 *
 *    示例：如果动画只有ROTATION通道
 *    - 分解: Transform → (T=currentT, R=currentR, S=currentS)
 *    - 修改: R = animationR （T和S保持不变）
 *    - 组合: (T, animationR, S) → newTransform
 *
 * 2. 动画混合（支持多动画同时播放）：
 *    - 每个动画有独立的权重（0.0 ~ 1.0）
 *    - 应用顺序：按播放顺序依次应用到骨骼
 *    - 混合方式：lerp（线性插值）用于T/S，nlerp（归一化线性插值）用于R
 *
 *    混合公式：
 *    - Translation: result = lerp(current, target, weight)
 *    - Rotation:    result = nlerp(current, target, weight)
 *    - Scale:       result = lerp(current, scale, weight)
 *
 *    示例：idle(权重0.5) + walk(权重0.5)
 *    - 先应用idle：骨骼Transform = lerp(bindPose, idle, 0.5)
 *    - 再应用walk：骨骼Transform = lerp(上一步结果, walk, 0.5)
 *
 * 3. 通道绑定（Animation → Skeleton映射）：
 *    - AnimationAsset存储骨骼名称（如"LeftArm"）
 *    - 播放时查询SkeletonAsset::getBoneIndex()建立映射
 *    - BoundChannel存储骨骼索引（避免每帧查找）
 *
 * ========== 设计理由 ==========
 *
 * Q: 为什么需要TRS分解/组合？
 * A: glTF动画可能只包含部分通道（T/R/S中的一个或多个），常见情况：
 *    - 角色动画：只有ROTATION（骨骼不缩放不平移）
 *    - 相机动画：只有TRANSLATION + ROTATION（相机不缩放）
 *    如果直接setTransform(animationTransform)，未定义的分量会被覆盖为默认值：
 *    - 只有R通道 → T和S被置零 → 骨骼消失
 *    正确做法是保留当前Transform的其他分量，只修改动画通道对应的分量。
 *
 * Q: 为什么用lerp而非additive blending？
 * A: 两种混合方式对比：
 *    Lerp blending:     result = lerp(current, target, weight)
 *    Additive blending: result = current + (target - basePose) * weight
 *
 *    当前实现使用lerp是因为：
 *    - 实现简单（不需要basePose）
 *    - 适合大多数场景（状态切换、crossfade）
 *    - 权重归一化后结果直观（0.5+0.5 = 平均混合）
 *
 *    Additive适合：
 *    - 叠加动画（如呼吸+瞄准）
 *    - 局部修正（如头部追踪）
 *    可以在未来扩展支持additive模式。
 *
 * Q: 为什么用nlerp而非slerp插值四元数？
 * A: 性能考虑：
 *    - nlerp: 线性插值 + 归一化（快）
 *    - slerp: 球面插值（慢，需要三角函数）
 *    差异：只有在旋转角度很大（>90度）时才明显
 *    通常动画的相邻关键帧角度很小，nlerp足够准确。
 */

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
#include <math/mat3.h>
#include <algorithm>
#include <cmath>

using namespace filament;
using namespace filament::math;
using namespace utils;

namespace filament::gltfio {

// 辅助函数：插值计算
static float3 lerp(const float3& a, const float3& b, float t) {
    return a + (b - a) * t;
}

/**
 * TRS分解：将mat4分解为Translation, Rotation, Scale
 *
 * 算法步骤：
 * 1. Translation: 提取第4列的前3个元素（matrix[3].xyz）
 * 2. Scale: 计算前3列向量的长度（|col0|, |col1|, |col2|）
 * 3. Rotation: 归一化前3列向量得到旋转矩阵，转换为quaternion
 *
 * Quaternion转换算法：Shoemake's algorithm
 * - 计算trace（对角线元素之和）
 * - 根据trace选择最稳定的分支（避免数值误差）
 *
 * @param matrix 输入变换矩阵（列主序）
 * @param outTranslation [输出] 平移分量
 * @param outRotation [输出] 旋转分量（quaternion）
 * @param outScale [输出] 缩放分量
 */
static void decomposeTRS(const mat4f& matrix, float3& outTranslation,
                         quatf& outRotation, float3& outScale) {
    // 1. 提取Translation（第4列的前3个元素）
    outTranslation = float3(matrix[3][0], matrix[3][1], matrix[3][2]);

    // 2. 提取Scale（各列向量的长度）
    float3 col0(matrix[0][0], matrix[0][1], matrix[0][2]);
    float3 col1(matrix[1][0], matrix[1][1], matrix[1][2]);
    float3 col2(matrix[2][0], matrix[2][1], matrix[2][2]);

    outScale.x = length(col0);
    outScale.y = length(col1);
    outScale.z = length(col2);

    // 3. 提取Rotation（归一化旋转矩阵部分，转换为quaternion）
    mat3f rotMat;
    if (outScale.x > 0.0001f) rotMat[0] = col0 / outScale.x; else rotMat[0] = float3(1, 0, 0);
    if (outScale.y > 0.0001f) rotMat[1] = col1 / outScale.y; else rotMat[1] = float3(0, 1, 0);
    if (outScale.z > 0.0001f) rotMat[2] = col2 / outScale.z; else rotMat[2] = float3(0, 0, 1);

    // 4. 从旋转矩阵构造quaternion (Shoemake算法)
    float trace = rotMat[0][0] + rotMat[1][1] + rotMat[2][2];

    if (trace > 0.0f) {
        float s = std::sqrt(trace + 1.0f);
        outRotation.w = s * 0.5f;
        s = 0.5f / s;
        outRotation.x = (rotMat[2][1] - rotMat[1][2]) * s;
        outRotation.y = (rotMat[0][2] - rotMat[2][0]) * s;
        outRotation.z = (rotMat[1][0] - rotMat[0][1]) * s;
    } else if (rotMat[0][0] > rotMat[1][1] && rotMat[0][0] > rotMat[2][2]) {
        float s = std::sqrt(1.0f + rotMat[0][0] - rotMat[1][1] - rotMat[2][2]);
        outRotation.x = s * 0.5f;
        s = 0.5f / s;
        outRotation.y = (rotMat[0][1] + rotMat[1][0]) * s;
        outRotation.z = (rotMat[0][2] + rotMat[2][0]) * s;
        outRotation.w = (rotMat[2][1] - rotMat[1][2]) * s;
    } else if (rotMat[1][1] > rotMat[2][2]) {
        float s = std::sqrt(1.0f + rotMat[1][1] - rotMat[0][0] - rotMat[2][2]);
        outRotation.y = s * 0.5f;
        s = 0.5f / s;
        outRotation.x = (rotMat[0][1] + rotMat[1][0]) * s;
        outRotation.z = (rotMat[1][2] + rotMat[2][1]) * s;
        outRotation.w = (rotMat[0][2] - rotMat[2][0]) * s;
    } else {
        float s = std::sqrt(1.0f + rotMat[2][2] - rotMat[0][0] - rotMat[1][1]);
        outRotation.z = s * 0.5f;
        s = 0.5f / s;
        outRotation.x = (rotMat[0][2] + rotMat[2][0]) * s;
        outRotation.y = (rotMat[1][2] + rotMat[2][1]) * s;
        outRotation.w = (rotMat[1][0] - rotMat[0][1]) * s;
    }
}

/**
 * TRS合成：将Translation, Rotation, Scale合成为mat4
 *
 * 变换顺序：T * R * S （先缩放，再旋转，最后平移）
 * 这是标准的3D变换顺序，确保：
 * - 缩放在局部坐标系
 * - 旋转在缩放后的坐标系
 * - 平移在世界坐标系
 *
 * @param translation 平移分量
 * @param rotation 旋转分量（quaternion）
 * @param scale 缩放分量
 * @return 合成的变换矩阵
 */
static mat4f composeTRS(const float3& translation, const quatf& rotation,
                        const float3& scale) {
    // 先Scale，再Rotation，最后Translation: T * R * S
    mat4f T = mat4f::translation(translation);
    mat4f R = mat4f(mat3f(rotation));
    mat4f S = mat4f::scaling(scale);
    return T * R * S;
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
        const auto& singleAnim = animAsset->mAnimations[state.animIndex];

        state.boundChannels.clear();
        state.boundChannels.reserve(singleAnim.mChannels.size());

        for (const auto& srcChannel : singleAnim.mChannels) {
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
            }
        }
    }
}

int StandaloneAnimator::playAnimation(AnimationAsset* animation, size_t animIndex, float weight, bool loop) noexcept {
    if (!mImpl->skeleton) {
        GLTFIO_EXT_WARN("No skeleton bound to animator");
        return -1;
    }

    // 验证animIndex有效性
    auto* animAsset = static_cast<FAnimationAsset*>(animation);
    if (animIndex >= animAsset->mAnimations.size()) {
        GLTFIO_EXT_WARN("Invalid animation index: " << animIndex << " (count: " << animAsset->mAnimations.size() << ")");
        return -1;
    }

    AnimationState state;
    state.animation = animation;
    state.animIndex = animIndex;
    state.time = 0.0f;
    state.weight = weight;
    state.loop = loop;
    state.playing = true;
    state.id = mImpl->nextAnimationId++;

    // 绑定通道（名称→索引映射）
    const auto& singleAnim = animAsset->mAnimations[animIndex];
    state.boundChannels.reserve(singleAnim.mChannels.size());

    for (const auto& srcChannel : singleAnim.mChannels) {
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

        float duration = state.animation->getAnimationDuration(state.animIndex);
        if (state.loop && state.time > duration) {
            state.time = std::fmod(state.time, duration);
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

/**
 * 应用动画到骨骼变换（核心动画混合逻辑）
 *
 * 算法流程：
 * 1. 查找关键帧：
 *    - 使用lower_bound查找当前时间对应的关键帧
 *    - 计算插值因子t（当前时间在prev和next之间的比例）
 *
 * 2. 插值计算：
 *    - TRANSLATION: lerp(prev, next, t)
 *    - ROTATION: nlerp(prev, next, t)
 *    - SCALE: lerp(prev, next, t)
 *
 * 3. TRS分解→修改→组合（关键步骤，避免单通道覆盖bug）：
 *    a) 获取当前骨骼Transform并分解为T/R/S
 *    b) 只修改当前通道对应的分量（如ROTATION通道只修改R）
 *    c) 应用权重混合：component = lerp(current, target, weight)
 *    d) 重新组合T/R/S为新Transform
 *    e) 设置回骨骼
 *
 * 权重混合示例：
 * - weight=1.0: 完全使用动画值（result = target）
 * - weight=0.5: 50%混合（result = 0.5*current + 0.5*target）
 * - weight=0.0: 不应用（result = current）
 *
 * @param state 动画状态（包含时间、权重、通道绑定）
 */
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

        // 获取当前变换并分解为TRS
        mat4f currentTransform = transformManager->getTransform(ti);
        float3 translation, scale;
        quatf rotation;
        decomposeTRS(currentTransform, translation, rotation, scale);

        // 根据通道类型修改对应分量
        switch (channel.transformType) {
            case BoundChannel::TRANSLATION: {
                const float3* values = reinterpret_cast<const float3*>(sampler->values.data());
                float3 targetTranslation = lerp(values[prevIndex], values[nextIndex], t);
                // 应用权重混合
                translation = lerp(translation, targetTranslation, state.weight);
                break;
            }

            case BoundChannel::ROTATION: {
                const quatf* values = reinterpret_cast<const quatf*>(sampler->values.data());
                quatf targetRotation = nlerp(values[prevIndex], values[nextIndex], t);
                // 应用权重混合
                rotation = nlerp(rotation, targetRotation, state.weight);
                break;
            }

            case BoundChannel::SCALE: {
                const float3* values = reinterpret_cast<const float3*>(sampler->values.data());
                float3 targetScale = lerp(values[prevIndex], values[nextIndex], t);
                // 应用权重混合
                scale = lerp(scale, targetScale, state.weight);
                break;
            }

            case BoundChannel::WEIGHTS:
                // Morph targets支持（暂不实现）
                break;
        }

        // 重新合成并设置变换
        mat4f newTransform = composeTRS(translation, rotation, scale);
        transformManager->setTransform(ti, newTransform);
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
