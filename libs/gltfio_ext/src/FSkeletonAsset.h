/**
 * @file FSkeletonAsset.h
 * @brief 骨骼资产内部实现 - 存储骨骼层级和绑定姿态数据
 *
 * 设计说明：
 * - FSkeletonAsset是SkeletonAsset的具体实现（F前缀表示Filament内部类型）
 * - 使用FixedCapacityVector存储骨骼数据（避免动态扩容）
 * - 使用unordered_map建立名称→索引快速查找
 * - 使用Entity + TransformManager存储骨骼层级（利用Filament场景图）
 *
 * 核心数据结构：
 * - mBoneEntities: 骨骼Entity数组（与skin.joints一一对应）
 * - mBoneNames: 骨骼名称（用于动画通道绑定）
 * - mBoneNameToIndex: 名称→索引映射（加速查找）
 * - mInverseBindMatrices: 逆绑定矩阵（蒙皮计算所需）
 * - mRoot: 骨骼层级的根Entity
 */

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

/**
 * 骨骼资产内部实现
 */
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
