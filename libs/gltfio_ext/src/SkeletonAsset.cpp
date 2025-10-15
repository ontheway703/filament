/**
 * @file SkeletonAsset.cpp
 * @brief 骨骼资产实现 - 从glTF skin加载骨骼层级
 *
 * ========== 核心设计 ==========
 *
 * 骨骼加载流程（loadFromGltfSkin）：
 * 1. 创建骨骼Entity：
 *    - 遍历skin.joints数组
 *    - 为每个joint创建Entity（EntityManager::create）
 *    - 添加TransformManager组件
 *
 * 2. 设置Transform：
 *    - 从cgltf_node提取变换（matrix或TRS）
 *    - 设置为局部变换（TransformManager::setTransform）
 *
 * 3. 建立父子关系：
 *    - 查找joint.parent在joints数组中的索引
 *    - 设置TransformManager父子关系
 *
 * 4. 提取逆绑定矩阵：
 *    - 从skin.inverse_bind_matrices accessor解包mat4f数据
 *    - 用于蒙皮计算：boneMatrix = worldTransform * inverseBindMatrix
 *
 * 5. 建立名称索引：
 *    - 存储骨骼名称（joint.name）
 *    - 建立name→index映射（用于动画通道绑定）
 *
 * ========== 设计理由 ==========
 *
 * Q: 为什么用Entity而非纯数据结构？
 * A: 利用Filament的TransformManager：
 *    - 自动计算世界变换（考虑父子关系）
 *    - 高效的层级变换更新
 *    - 与Filament场景图集成
 *
 * Q: 为什么需要逆绑定矩阵？
 * A: 蒙皮变换公式：finalMatrix = boneWorldTransform * inverseBindMatrix
 *    - inverseBindMatrix：将顶点从模型空间转到骨骼空间
 *    - boneWorldTransform：将顶点从骨骼空间转到世界空间
 *    - 结果：顶点跟随骨骼移动
 */

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

// 辅助函数：从cgltf_node提取变换矩阵
static mat4f extractNodeTransform(const cgltf_node* node) {
    if (node->has_matrix) {
        // 直接使用matrix
        mat4f matrix;
        memcpy(&matrix, node->matrix, sizeof(mat4f));
        return matrix;
    } else {
        // 从TRS构建矩阵
        float3 translation(0, 0, 0);
        quatf rotation(1, 0, 0, 0);
        float3 scale(1, 1, 1);

        if (node->has_translation) {
            translation = float3(node->translation[0], node->translation[1], node->translation[2]);
        }

        if (node->has_rotation) {
            rotation = quatf(node->rotation[3], node->rotation[0],
                           node->rotation[1], node->rotation[2]);
        }

        if (node->has_scale) {
            scale = float3(node->scale[0], node->scale[1], node->scale[2]);
        }

        // 构建变换矩阵: T * R * S
        mat4f T = mat4f::translation(translation);
        mat4f R = mat4f(mat3f(rotation));  // quaternion to mat3f, then to mat4f
        mat4f S = mat4f::scaling(scale);
        return T * R * S;
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
        mat4f localTransform = extractNodeTransform(joint);

        // 设置TransformManager
        TransformManager::Instance ti = mTransformManager->getInstance(entity);
        if (!ti) {
            mTransformManager->create(entity);
            ti = mTransformManager->getInstance(entity);
        }

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
