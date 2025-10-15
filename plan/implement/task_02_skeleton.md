# 任务2: SkeletonAsset实现

**预计时间**: 2小时
**前置依赖**: 任务1完成
**完成标志**: 能从glTF加载骨骼并查询骨骼信息

---

## 目标

实现SkeletonAsset的完整功能：
1. 从cgltf_skin加载骨骼层次
2. 提取逆绑定矩阵
3. 建立骨骼名称索引
4. 实现所有查询接口

---

## 1. 完善 FSkeletonAsset.h

**文件**: `libs/gltfio_ext/src/FSkeletonAsset.h`

```cpp
#ifndef GLTFIO_EXT_FSKELETONASSET_H
#define GLTFIO_EXT_FSKELETONASSET_H

#include <gltfio/SkeletonAsset.h>
#include <utils/Entity.h>
#include <utils/FixedCapacityVector.h>
#include <utils/CString.h>
#include <math/mat4.h>
#include <unordered_map>
#include <string>

struct cgltf_skin;
struct cgltf_data;

namespace filament {
    class Engine;
    class TransformManager;
}

namespace utils {
    class EntityManager;
}

namespace filament::gltfio {

struct FSkeletonAsset : public SkeletonAsset {
    filament::Engine* mEngine = nullptr;
    utils::EntityManager* mEntityManager = nullptr;
    filament::TransformManager* mTransformManager = nullptr;

    // 骨骼层次结构
    utils::FixedCapacityVector<utils::Entity> mBoneEntities;
    utils::FixedCapacityVector<utils::CString> mBoneNames;
    std::unordered_map<std::string, size_t> mBoneNameToIndex;

    // 绑定姿态数据
    utils::FixedCapacityVector<filament::math::mat4f> mInverseBindMatrices;

    // 根节点
    utils::Entity mRoot;

    // 从cgltf_skin加载
    bool loadFromGltfSkin(const cgltf_skin* skin, const cgltf_data* data);

    ~FSkeletonAsset();
};

} // namespace filament::gltfio

#endif // GLTFIO_EXT_FSKELETONASSET_H
```

---

## 2. 实现 SkeletonAsset.cpp

**文件**: `libs/gltfio_ext/src/SkeletonAsset.cpp`

```cpp
#include <gltfio/SkeletonAsset.h>
#include "FSkeletonAsset.h"
#include "GltfioExtInternal.h"

#include <filament/TransformManager.h>
#include <utils/EntityManager.h>
#include <math/quat.h>
#include <cgltf.h>

using namespace filament;
using namespace filament::math;
using namespace utils;

namespace filament::gltfio {

// 辅助函数：从cgltf_node提取TRS
static void extractNodeTransform(const cgltf_node* node, float3& translation,
                                  quatf& rotation, float3& scale) {
    if (node->has_matrix) {
        // 如果有matrix，先分解
        mat4f matrix;
        memcpy(&matrix, node->matrix, sizeof(mat4f));
        decomposeMatrix(matrix, &translation, &rotation, &scale);
    } else {
        // 否则使用TRS
        if (node->has_translation) {
            translation = float3(node->translation[0], node->translation[1], node->translation[2]);
        } else {
            translation = float3(0, 0, 0);
        }

        if (node->has_rotation) {
            rotation = quatf(node->rotation[3], node->rotation[0],
                           node->rotation[1], node->rotation[2]);
        } else {
            rotation = quatf(1, 0, 0, 0);
        }

        if (node->has_scale) {
            scale = float3(node->scale[0], node->scale[1], node->scale[2]);
        } else {
            scale = float3(1, 1, 1);
        }
    }
}

bool FSkeletonAsset::loadFromGltfSkin(const cgltf_skin* skin, const cgltf_data* data) {
    if (!skin || skin->joints_count == 0) {
        GLTFIO_EXT_WARN("Invalid skin or no joints");
        return false;
    }

    const size_t jointCount = skin->joints_count;
    mBoneEntities.reserve(jointCount);
    mBoneNames.reserve(jointCount);
    mInverseBindMatrices.reserve(jointCount);

    // 1. 创建骨骼实体并设置变换
    for (size_t i = 0; i < jointCount; ++i) {
        const cgltf_node* joint = skin->joints[i];

        // 创建Entity
        Entity entity = mEntityManager->create();
        mBoneEntities.push_back(entity);

        // 提取变换
        float3 translation, scale;
        quatf rotation;
        extractNodeTransform(joint, translation, rotation, scale);

        // 设置TransformManager
        TransformManager::Instance ti = mTransformManager->getInstance(entity);
        if (!ti) {
            mTransformManager->create(entity);
            ti = mTransformManager->getInstance(entity);
        }

        mat4f localTransform = composeMatrix(translation, rotation, scale);
        mTransformManager->setTransform(ti, localTransform);

        // 存储名称
        if (joint->name) {
            mBoneNames.emplace_back(joint->name);
            mBoneNameToIndex[joint->name] = i;
        } else {
            // 匿名骨骼
            char nameBuf[32];
            snprintf(nameBuf, sizeof(nameBuf), "bone_%zu", i);
            mBoneNames.emplace_back(nameBuf);
            mBoneNameToIndex[nameBuf] = i;
        }

        // 设置父子关系
        if (joint->parent && joint->parent >= data->nodes &&
            joint->parent < data->nodes + data->nodes_count) {

            // 查找父节点在骨骼列表中的索引
            for (size_t j = 0; j < jointCount; ++j) {
                if (skin->joints[j] == joint->parent) {
                    Entity parentEntity = mBoneEntities[j];
                    TransformManager::Instance parentTi = mTransformManager->getInstance(parentEntity);
                    mTransformManager->setParent(ti, parentTi);
                    break;
                }
            }
        }
    }

    // 2. 提取逆绑定矩阵
    if (skin->inverse_bind_matrices) {
        const cgltf_accessor* ibmAccessor = skin->inverse_bind_matrices;

        if (ibmAccessor->count != jointCount) {
            GLTFIO_EXT_WARN("Inverse bind matrices count mismatch");
            return false;
        }

        mInverseBindMatrices.resize(jointCount);

        // 解包浮点数据
        cgltf_size floatCount = ibmAccessor->count * 16;
        float* floatData = new float[floatCount];
        cgltf_accessor_unpack_floats(ibmAccessor, floatData, floatCount);

        // 复制到mat4f
        for (size_t i = 0; i < jointCount; ++i) {
            memcpy(&mInverseBindMatrices[i], floatData + i * 16, sizeof(mat4f));
        }

        delete[] floatData;
    } else {
        // 没有逆绑定矩阵，使用单位矩阵
        GLTFIO_EXT_WARN("No inverse bind matrices, using identity");
        mInverseBindMatrices.resize(jointCount);
        for (size_t i = 0; i < jointCount; ++i) {
            mInverseBindMatrices[i] = mat4f();
        }
    }

    // 3. 确定根节点（没有父节点的第一个关节）
    mRoot = mBoneEntities[0];
    for (size_t i = 0; i < jointCount; ++i) {
        TransformManager::Instance ti = mTransformManager->getInstance(mBoneEntities[i]);
        if (!mTransformManager->getParent(ti)) {
            mRoot = mBoneEntities[i];
            break;
        }
    }

    return true;
}

FSkeletonAsset::~FSkeletonAsset() {
    if (mEntityManager) {
        for (Entity entity : mBoneEntities) {
            mEntityManager->destroy(entity);
        }
    }
}

// 公开API实现
size_t SkeletonAsset::getBoneCount() const noexcept {
    auto* self = static_cast<const FSkeletonAsset*>(this);
    return self->mBoneEntities.size();
}

int SkeletonAsset::getBoneIndex(const char* boneName) const noexcept {
    auto* self = static_cast<const FSkeletonAsset*>(this);
    auto it = self->mBoneNameToIndex.find(boneName);
    return (it != self->mBoneNameToIndex.end()) ? static_cast<int>(it->second) : -1;
}

const char* SkeletonAsset::getBoneName(size_t boneIndex) const noexcept {
    auto* self = static_cast<const FSkeletonAsset*>(this);
    if (boneIndex >= self->mBoneNames.size()) {
        return nullptr;
    }
    return self->mBoneNames[boneIndex].c_str();
}

Entity SkeletonAsset::getBoneEntity(size_t boneIndex) const noexcept {
    auto* self = static_cast<const FSkeletonAsset*>(this);
    if (boneIndex >= self->mBoneEntities.size()) {
        return Entity();
    }
    return self->mBoneEntities[boneIndex];
}

const mat4f* SkeletonAsset::getInverseBindMatrices() const noexcept {
    auto* self = static_cast<const FSkeletonAsset*>(this);
    return self->mInverseBindMatrices.empty() ? nullptr : self->mInverseBindMatrices.data();
}

Entity SkeletonAsset::getRoot() const noexcept {
    auto* self = static_cast<const FSkeletonAsset*>(this);
    return self->mRoot;
}

bool SkeletonAsset::validateBoneIndices(const uint16_t* indices, size_t count) const noexcept {
    auto* self = static_cast<const FSkeletonAsset*>(this);
    const size_t boneCount = self->mBoneEntities.size();

    for (size_t i = 0; i < count; ++i) {
        if (indices[i] >= boneCount) {
            return false;
        }
    }
    return true;
}

} // namespace filament::gltfio
```

---

## 3. 实现 AssetLoaderExt::loadSkeleton()

**文件**: `libs/gltfio_ext/src/AssetLoaderExt.cpp` (更新)

```cpp
#include <gltfio/AssetLoaderExt.h>
#include "FSkeletonAsset.h"
#include "GltfioExtInternal.h"

#include <filament/Engine.h>
#include <filament/TransformManager.h>
#include <utils/EntityManager.h>
#include <cgltf.h>

namespace filament::gltfio {

struct FAssetLoaderExt : public AssetLoaderExt {
    filament::Engine* mEngine;
    MaterialProvider* mMaterials;
    utils::EntityManager* mEntityManager;
    utils::NameComponentManager* mNameManager;

    FAssetLoaderExt(const AssetConfigurationExt& config)
        : mEngine(config.engine)
        , mMaterials(config.materials)
        , mEntityManager(config.entities ? config.entities : &utils::EntityManager::get())
        , mNameManager(config.names) {}
};

AssetLoaderExt* AssetLoaderExt::create(const AssetConfigurationExt& config) {
    return new FAssetLoaderExt(config);
}

void AssetLoaderExt::destroy(AssetLoaderExt** loader) {
    delete *loader;
    *loader = nullptr;
}

SkeletonAsset* AssetLoaderExt::loadSkeleton(const uint8_t* bytes, uint32_t nbytes) {
    auto* self = static_cast<FAssetLoaderExt*>(this);

    // 1. 解析glTF
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse(&options, bytes, nbytes, &data);

    if (result != cgltf_result_success) {
        GLTFIO_EXT_WARN("Failed to parse glTF data");
        return nullptr;
    }

    // 2. 加载缓冲区数据
    result = cgltf_load_buffers(&options, data, nullptr);
    if (result != cgltf_result_success) {
        GLTFIO_EXT_WARN("Failed to load glTF buffers");
        cgltf_free(data);
        return nullptr;
    }

    // 3. 检查是否有skin
    if (data->skins_count == 0) {
        GLTFIO_EXT_WARN("No skins found in glTF file");
        cgltf_free(data);
        return nullptr;
    }

    // 4. 创建SkeletonAsset
    FSkeletonAsset* skeleton = new FSkeletonAsset();
    skeleton->mEngine = self->mEngine;
    skeleton->mEntityManager = self->mEntityManager;
    skeleton->mTransformManager = &self->mEngine->getTransformManager();

    // 5. 加载第一个skin
    const cgltf_skin* skin = &data->skins[0];
    bool success = skeleton->loadFromGltfSkin(skin, data);

    // 6. 释放cgltf数据（已经复制到SkeletonAsset）
    cgltf_free(data);

    if (!success) {
        delete skeleton;
        return nullptr;
    }

    return skeleton;
}

MeshAsset* AssetLoaderExt::loadMesh(const uint8_t* bytes, uint32_t nbytes) {
    GLTFIO_EXT_WARN("loadMesh not implemented yet");
    return nullptr;
}

AnimationAsset* AssetLoaderExt::loadAnimation(const uint8_t* bytes, uint32_t nbytes) {
    GLTFIO_EXT_WARN("loadAnimation not implemented yet");
    return nullptr;
}

AnimationPack* AssetLoaderExt::loadAnimationPack(const uint8_t* bytes, uint32_t nbytes) {
    GLTFIO_EXT_WARN("loadAnimationPack not implemented yet");
    return nullptr;
}

void AssetLoaderExt::destroySkeleton(SkeletonAsset* skeleton) {
    delete static_cast<FSkeletonAsset*>(skeleton);
}

void AssetLoaderExt::destroyMesh(MeshAsset* mesh) {
    delete mesh;
}

void AssetLoaderExt::destroyAnimation(AnimationAsset* animation) {
    delete animation;
}

void AssetLoaderExt::destroyAnimationPack(AnimationPack* pack) {
    delete pack;
}

} // namespace filament::gltfio
```

---

## 4. 创建测试程序

**文件**: `libs/gltfio_ext/tests/test_skeleton.cpp`

```cpp
#include <gltfio/AssetLoaderExt.h>
#include <gltfio/SkeletonAsset.h>
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
        std::cerr << "Failed to open: " << path << std::endl;
        return {};
    }
    size_t size = file.tellg();
    std::vector<uint8_t> buffer(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}

int main() {
    // 1. 创建Engine
    Engine* engine = Engine::create();

    // 2. 创建加载器
    MaterialProvider* materials = createUbershaderProvider(engine);
    AssetConfigurationExt config = {
        .engine = engine,
        .materials = materials
    };
    AssetLoaderExt* loader = AssetLoaderExt::create(config);

    // 3. 加载骨骼
    std::vector<uint8_t> data = readFile("third_party/models/AnimatedMorphCube/AnimatedMorphCube.glb");
    if (data.empty()) {
        std::cerr << "Failed to read glTF file" << std::endl;
        return 1;
    }

    SkeletonAsset* skeleton = loader->loadSkeleton(data.data(), data.size());
    if (!skeleton) {
        std::cerr << "Failed to load skeleton" << std::endl;
        return 1;
    }

    // 4. 查询骨骼信息
    std::cout << "=== Skeleton Info ===" << std::endl;
    std::cout << "Bone count: " << skeleton->getBoneCount() << std::endl;

    for (size_t i = 0; i < skeleton->getBoneCount(); ++i) {
        const char* name = skeleton->getBoneName(i);
        std::cout << "  [" << i << "] " << (name ? name : "(unnamed)") << std::endl;
    }

    // 5. 测试名称查找
    if (skeleton->getBoneCount() > 0) {
        const char* firstName = skeleton->getBoneName(0);
        if (firstName) {
            int index = skeleton->getBoneIndex(firstName);
            std::cout << "\nName lookup test: '" << firstName << "' -> index " << index << std::endl;
        }
    }

    // 6. 检查逆绑定矩阵
    const auto* ibm = skeleton->getInverseBindMatrices();
    if (ibm) {
        std::cout << "\nInverse bind matrices: OK" << std::endl;
    }

    // 7. 清理
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

## 5. 更新CMakeLists.txt添加测试

**文件**: `libs/gltfio_ext/CMakeLists.txt` (追加)

```cmake
# 测试程序
if (NOT WEBGL AND NOT ANDROID AND NOT IOS)
    add_executable(test_skeleton tests/test_skeleton.cpp)
    target_link_libraries(test_skeleton PRIVATE gltfio_ext)
    set_target_properties(test_skeleton PROPERTIES FOLDER Tests)
endif()
```

---

## 6. 验证步骤

```bash
# 1. 重新编译
cd out
ninja gltfio_ext

# 2. 编译测试
ninja test_skeleton

# 3. 运行测试
./libs/test_skeleton

# 预期输出:
# === Skeleton Info ===
# Bone count: N
#   [0] bone_name_0
#   [1] bone_name_1
#   ...
# Name lookup test: 'bone_name_0' -> index 0
# Inverse bind matrices: OK
# === Test PASSED ===
```

---

## 完成标志

- [ ] FSkeletonAsset::loadFromGltfSkin() 实现完成
- [ ] AssetLoaderExt::loadSkeleton() 实现完成
- [ ] 所有查询接口实现完成
- [ ] 测试程序编译通过
- [ ] 测试程序运行成功
- [ ] 能正确输出骨骼数量和名称

完成后进入 **任务3: MeshAsset实现**
