# 任务4: AnimationAsset + AnimationPack实现

**预计时间**: 3小时
**前置依赖**: 任务1完成
**完成标志**: 能从glTF加载动画并查询动画信息

---

## 目标

实现AnimationAsset和AnimationPack：
1. 从glTF加载动画数据（samplers和channels）
2. 使用名称延迟绑定（不依赖Entity）
3. 批量加载多个动画（AnimationPack）
4. 实现所有查询接口

---

## 1. 完善 FAnimationAsset.h

**文件**: `libs/gltfio_ext/src/FAnimationAsset.h`

```cpp
#ifndef GLTFIO_EXT_FANIMATIONASSET_H
#define GLTFIO_EXT_FANIMATIONASSET_H

#include <gltfio/AnimationAsset.h>
#include <utils/CString.h>
#include <map>
#include <vector>
#include <string>

struct cgltf_animation;

namespace filament::gltfio {

// 采样器数据
struct Sampler {
    std::map<float, size_t> times;  // 时间 -> 索引
    std::vector<float> values;       // 关键帧值
    enum { LINEAR, STEP, CUBIC } interpolation;
};

// 通道数据（使用名称延迟绑定）
struct Channel {
    const Sampler* sampler;
    std::string targetBoneName;  // ⭐ 使用名称而非Entity
    enum { TRANSLATION, ROTATION, SCALE, WEIGHTS } transformType;
};

struct FAnimationAsset : public AnimationAsset {
    utils::CString mName;
    float mDuration = 0.0f;

    // 动画数据（自主存储）
    std::vector<Sampler> mSamplers;
    std::vector<Channel> mChannels;

    // 从cgltf_animation加载
    bool loadFromGltfAnimation(const cgltf_animation* anim);

    ~FAnimationAsset() = default;
};

} // namespace filament::gltfio

#endif // GLTFIO_EXT_FANIMATIONASSET_H
```

---

## 2. 实现 AnimationAsset.cpp

**文件**: `libs/gltfio_ext/src/AnimationAsset.cpp`

```cpp
#include <gltfio/AnimationAsset.h>
#include "FAnimationAsset.h"
#include "GltfioExtInternal.h"

#include <cgltf.h>
#include <algorithm>

namespace filament::gltfio {

// 辅助函数：创建Sampler
static void createSampler(const cgltf_animation_sampler& src, Sampler& dst) {
    // 1. 复制时间轴到map
    const cgltf_accessor* timelineAccessor = src.input;
    const uint8_t* timelineBlob = nullptr;
    const float* timelineFloats = nullptr;

    if (timelineAccessor->buffer_view->has_meshopt_compression) {
        timelineBlob = (const uint8_t*)timelineAccessor->buffer_view->data;
        timelineFloats = (const float*)(timelineBlob + timelineAccessor->offset);
    } else {
        timelineBlob = (const uint8_t*)timelineAccessor->buffer_view->buffer->data;
        timelineFloats = (const float*)(timelineBlob + timelineAccessor->offset +
                                        timelineAccessor->buffer_view->offset);
    }

    for (size_t i = 0; i < timelineAccessor->count; ++i) {
        dst.times[timelineFloats[i]] = i;
    }

    // 2. 解包值数据
    const cgltf_accessor* valuesAccessor = src.output;
    size_t componentCount = 0;

    switch (valuesAccessor->type) {
        case cgltf_type_scalar:
            componentCount = 1;
            break;
        case cgltf_type_vec3:
            componentCount = 3;
            break;
        case cgltf_type_vec4:
            componentCount = 4;
            break;
        default:
            GLTFIO_EXT_WARN("Unknown animation value type");
            return;
    }

    dst.values.resize(valuesAccessor->count * componentCount);
    cgltf_accessor_unpack_floats(src.output, dst.values.data(), dst.values.size());

    // 3. 设置插值类型
    switch (src.interpolation) {
        case cgltf_interpolation_type_linear:
            dst.interpolation = Sampler::LINEAR;
            break;
        case cgltf_interpolation_type_step:
            dst.interpolation = Sampler::STEP;
            break;
        case cgltf_interpolation_type_cubic_spline:
            dst.interpolation = Sampler::CUBIC;
            break;
        default:
            dst.interpolation = Sampler::LINEAR;
            break;
    }
}

bool FAnimationAsset::loadFromGltfAnimation(const cgltf_animation* anim) {
    if (!anim) {
        return false;
    }

    // 1. 存储名称
    mName = anim->name ? anim->name : "";
    mDuration = 0.0f;

    // 2. 加载Samplers
    mSamplers.resize(anim->samplers_count);
    for (size_t i = 0; i < anim->samplers_count; ++i) {
        const cgltf_animation_sampler& srcSampler = anim->samplers[i];
        Sampler& dstSampler = mSamplers[i];
        createSampler(srcSampler, dstSampler);

        // 更新duration
        if (!dstSampler.times.empty()) {
            float maxTime = dstSampler.times.rbegin()->first;
            mDuration = std::max(mDuration, maxTime);
        }
    }

    // 3. 加载Channels（使用骨骼名称）
    for (size_t i = 0; i < anim->channels_count; ++i) {
        const cgltf_animation_channel& srcChannel = anim->channels[i];

        // 跳过无效channel
        if (!srcChannel.target_node || !srcChannel.sampler) {
            continue;
        }

        Channel dstChannel;
        dstChannel.sampler = &mSamplers[srcChannel.sampler - anim->samplers];

        // ⭐ 关键：存储骨骼名称而非Entity
        if (srcChannel.target_node->name) {
            dstChannel.targetBoneName = srcChannel.target_node->name;
        } else {
            // 跳过匿名节点
            GLTFIO_EXT_WARN("Skipping animation channel with unnamed target node");
            continue;
        }

        // 设置变换类型
        switch (srcChannel.target_path) {
            case cgltf_animation_path_type_translation:
                dstChannel.transformType = Channel::TRANSLATION;
                break;
            case cgltf_animation_path_type_rotation:
                dstChannel.transformType = Channel::ROTATION;
                break;
            case cgltf_animation_path_type_scale:
                dstChannel.transformType = Channel::SCALE;
                break;
            case cgltf_animation_path_type_weights:
                dstChannel.transformType = Channel::WEIGHTS;
                break;
            default:
                GLTFIO_EXT_WARN("Unsupported animation path type");
                continue;
        }

        mChannels.push_back(dstChannel);
    }

    if (mChannels.empty()) {
        GLTFIO_EXT_WARN("Animation has no valid channels");
        return false;
    }

    return true;
}

// 公开API实现
const char* AnimationAsset::getName() const noexcept {
    auto* self = static_cast<const FAnimationAsset*>(this);
    return self->mName.c_str();
}

float AnimationAsset::getDuration() const noexcept {
    auto* self = static_cast<const FAnimationAsset*>(this);
    return self->mDuration;
}

size_t AnimationAsset::getChannelCount() const noexcept {
    auto* self = static_cast<const FAnimationAsset*>(this);
    return self->mChannels.size();
}

const char* AnimationAsset::getChannelTargetBone(size_t channelIndex) const noexcept {
    auto* self = static_cast<const FAnimationAsset*>(this);
    if (channelIndex >= self->mChannels.size()) {
        return nullptr;
    }
    return self->mChannels[channelIndex].targetBoneName.c_str();
}

AnimationAsset::ChannelType AnimationAsset::getChannelType(size_t channelIndex) const noexcept {
    auto* self = static_cast<const FAnimationAsset*>(this);
    if (channelIndex >= self->mChannels.size()) {
        return ChannelType::TRANSLATION;
    }

    switch (self->mChannels[channelIndex].transformType) {
        case Channel::TRANSLATION: return ChannelType::TRANSLATION;
        case Channel::ROTATION: return ChannelType::ROTATION;
        case Channel::SCALE: return ChannelType::SCALE;
        case Channel::WEIGHTS: return ChannelType::WEIGHTS;
        default: return ChannelType::TRANSLATION;
    }
}

} // namespace filament::gltfio
```

---

## 3. 完善 FAnimationPack.h

**文件**: `libs/gltfio_ext/src/FAnimationPack.h`

```cpp
#ifndef GLTFIO_EXT_FANIMATIONPACK_H
#define GLTFIO_EXT_FANIMATIONPACK_H

#include <gltfio/AnimationPack.h>
#include <gltfio/AnimationAsset.h>
#include <vector>
#include <string>

namespace filament::gltfio {

struct FAnimationPack : public AnimationPack {
    std::vector<AnimationAsset*> mAnimations;
    std::vector<const char*> mAnimationNames;  // 指向mAnimations中的名称

    ~FAnimationPack();
};

} // namespace filament::gltfio

#endif // GLTFIO_EXT_FANIMATIONPACK_H
```

---

## 4. 实现 AnimationPack.cpp

**文件**: `libs/gltfio_ext/src/AnimationPack.cpp`

```cpp
#include <gltfio/AnimationPack.h>
#include "FAnimationPack.h"
#include <cstring>

namespace filament::gltfio {

FAnimationPack::~FAnimationPack() {
    // AnimationPack拥有所有AnimationAsset的所有权
    for (auto* anim : mAnimations) {
        delete anim;
    }
}

size_t AnimationPack::getAnimationCount() const noexcept {
    auto* self = static_cast<const FAnimationPack*>(this);
    return self->mAnimations.size();
}

AnimationAsset* AnimationPack::getAnimation(size_t index) noexcept {
    auto* self = static_cast<FAnimationPack*>(this);
    if (index >= self->mAnimations.size()) {
        return nullptr;
    }
    return self->mAnimations[index];
}

AnimationAsset* AnimationPack::findAnimation(const char* name) noexcept {
    auto* self = static_cast<FAnimationPack*>(this);
    if (!name) {
        return nullptr;
    }

    for (auto* anim : self->mAnimations) {
        if (strcmp(anim->getName(), name) == 0) {
            return anim;
        }
    }

    return nullptr;
}

const char* const* AnimationPack::getAnimationNames() const noexcept {
    auto* self = static_cast<const FAnimationPack*>(this);
    return self->mAnimationNames.empty() ? nullptr : self->mAnimationNames.data();
}

} // namespace filament::gltfio
```

---

## 5. 实现 AssetLoaderExt::loadAnimation() 和 loadAnimationPack()

**文件**: `libs/gltfio_ext/src/AssetLoaderExt.cpp` (更新)

```cpp
#include "FAnimationAsset.h"
#include "FAnimationPack.h"

AnimationAsset* AssetLoaderExt::loadAnimation(const uint8_t* bytes, uint32_t nbytes) {
    // 1. 解析glTF
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse(&options, bytes, nbytes, &data);

    if (result != cgltf_result_success) {
        GLTFIO_EXT_WARN("Failed to parse glTF data");
        return nullptr;
    }

    // 2. 加载缓冲区
    result = cgltf_load_buffers(&options, data, nullptr);
    if (result != cgltf_result_success) {
        GLTFIO_EXT_WARN("Failed to load glTF buffers");
        cgltf_free(data);
        return nullptr;
    }

    // 3. 检查动画
    if (data->animations_count == 0) {
        GLTFIO_EXT_WARN("No animations found in glTF file");
        cgltf_free(data);
        return nullptr;
    }

    // 4. 加载第一个动画
    const cgltf_animation* anim = &data->animations[0];
    FAnimationAsset* animAsset = new FAnimationAsset();
    bool success = animAsset->loadFromGltfAnimation(anim);

    cgltf_free(data);

    if (!success) {
        delete animAsset;
        return nullptr;
    }

    return animAsset;
}

AnimationPack* AssetLoaderExt::loadAnimationPack(const uint8_t* bytes, uint32_t nbytes) {
    // 1. 解析glTF
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse(&options, bytes, nbytes, &data);

    if (result != cgltf_result_success) {
        GLTFIO_EXT_WARN("Failed to parse glTF data");
        return nullptr;
    }

    // 2. 加载缓冲区
    result = cgltf_load_buffers(&options, data, nullptr);
    if (result != cgltf_result_success) {
        GLTFIO_EXT_WARN("Failed to load glTF buffers");
        cgltf_free(data);
        return nullptr;
    }

    // 3. 检查动画
    if (data->animations_count == 0) {
        GLTFIO_EXT_WARN("No animations found in glTF file");
        cgltf_free(data);
        return nullptr;
    }

    // 4. 创建AnimationPack
    FAnimationPack* pack = new FAnimationPack();
    pack->mAnimations.reserve(data->animations_count);
    pack->mAnimationNames.reserve(data->animations_count);

    // 5. 加载所有动画
    for (size_t i = 0; i < data->animations_count; ++i) {
        const cgltf_animation* anim = &data->animations[i];
        FAnimationAsset* animAsset = new FAnimationAsset();

        if (animAsset->loadFromGltfAnimation(anim)) {
            pack->mAnimations.push_back(animAsset);
            pack->mAnimationNames.push_back(animAsset->getName());
        } else {
            GLTFIO_EXT_WARN("Failed to load animation, skipping");
            delete animAsset;
        }
    }

    cgltf_free(data);

    if (pack->mAnimations.empty()) {
        GLTFIO_EXT_WARN("No valid animations loaded");
        delete pack;
        return nullptr;
    }

    return pack;
}

void AssetLoaderExt::destroyAnimation(AnimationAsset* animation) {
    delete static_cast<FAnimationAsset*>(animation);
}

void AssetLoaderExt::destroyAnimationPack(AnimationPack* pack) {
    delete static_cast<FAnimationPack*>(pack);
}
```

---

## 6. 创建测试程序

**文件**: `libs/gltfio_ext/tests/test_animation.cpp`

```cpp
#include <gltfio/AssetLoaderExt.h>
#include <gltfio/AnimationAsset.h>
#include <gltfio/AnimationPack.h>
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

    // 测试1: 加载单个动画
    std::cout << "=== Test 1: Load Single Animation ===" << std::endl;
    AnimationAsset* anim = loader->loadAnimation(data.data(), data.size());
    if (anim) {
        std::cout << "Animation name: " << (anim->getName()[0] ? anim->getName() : "(unnamed)") << std::endl;
        std::cout << "Duration: " << anim->getDuration() << "s" << std::endl;
        std::cout << "Channels: " << anim->getChannelCount() << std::endl;

        for (size_t i = 0; i < anim->getChannelCount(); ++i) {
            const char* bone = anim->getChannelTargetBone(i);
            std::cout << "  [" << i << "] target: " << (bone ? bone : "(none)") << std::endl;
        }

        loader->destroyAnimation(anim);
    }

    // 测试2: 加载动画包
    std::cout << "\n=== Test 2: Load Animation Pack ===" << std::endl;
    AnimationPack* pack = loader->loadAnimationPack(data.data(), data.size());
    if (pack) {
        std::cout << "Animation count: " << pack->getAnimationCount() << std::endl;

        const char* const* names = pack->getAnimationNames();
        for (size_t i = 0; i < pack->getAnimationCount(); ++i) {
            std::cout << "  [" << i << "] " << (names[i][0] ? names[i] : "(unnamed)") << std::endl;
        }

        // 测试通过名称查找
        if (pack->getAnimationCount() > 0) {
            const char* firstName = names[0];
            AnimationAsset* found = pack->findAnimation(firstName);
            if (found) {
                std::cout << "\nName lookup test: found '" << firstName << "'" << std::endl;
            }
        }

        loader->destroyAnimationPack(pack);
    }

    AssetLoaderExt::destroy(&loader);
    materials->destroyMaterials();
    delete materials;
    Engine::destroy(&engine);

    std::cout << "\n=== Test PASSED ===" << std::endl;
    return 0;
}
```

---

## 7. 更新CMakeLists.txt

**文件**: `libs/gltfio_ext/CMakeLists.txt` (追加)

```cmake
if (NOT WEBGL AND NOT ANDROID AND NOT IOS)
    add_executable(test_animation tests/test_animation.cpp)
    target_link_libraries(test_animation PRIVATE gltfio_ext)
    set_target_properties(test_animation PROPERTIES FOLDER Tests)
endif()
```

---

## 8. 验证步骤

```bash
cd out
ninja gltfio_ext test_animation
./libs/test_animation

# 预期输出:
# === Test 1: Load Single Animation ===
# Animation name: <name>
# Duration: <seconds>s
# Channels: N
#   [0] target: bone_name
# === Test 2: Load Animation Pack ===
# Animation count: N
#   [0] anim_name_0
# Name lookup test: found 'anim_name_0'
# === Test PASSED ===
```

---

## 完成标志

- [ ] FAnimationAsset::loadFromGltfAnimation() 实现完成
- [ ] AnimationPack实现完成
- [ ] 使用名称延迟绑定（不依赖Entity）
- [ ] 测试程序运行成功

完成后进入 **任务5: StandaloneAnimator实现**
