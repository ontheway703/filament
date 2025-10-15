/**
 * @file SkeletonAsset.h
 * @brief 骨骼资产 - 表示骨骼层级结构和绑定姿态
 *
 * SkeletonAsset存储骨骼动画所需的骨骼层级信息：
 * - 骨骼层级：父子关系、Transform组件
 * - 绑定姿态：每个骨骼的初始位置/旋转/缩放
 * - 逆绑定矩阵：用于蒙皮变换的预计算矩阵
 *
 * 设计理由：
 * 骨骼独立于mesh和animation，可以被多个资源复用：
 * - 一个skeleton可以绑定多个mesh（LOD、换装）
 * - 一个skeleton可以播放多个animation（共享骨骼层级）
 */

#ifndef GLTFIO_SKELETONASSET_H
#define GLTFIO_SKELETONASSET_H

#include <utils/compiler.h>
#include <utils/Entity.h>
#include <math/mat4.h>

#include <stddef.h>
#include <stdint.h>

namespace filament::gltfio {

/**
 * @class SkeletonAsset
 * @brief 骨骼资产 - 骨骼层级结构的容器
 *
 * 核心数据：
 * - 骨骼实体：每个骨骼对应一个Filament Entity（带TransformManager组件）
 * - 骨骼名称：用于按名称查找骨骼（如"LeftHand"）
 * - 逆绑定矩阵：蒙皮计算所需（boneMatrix = worldTransform * inverseBindMatrix）
 *
 * 典型用法：
 * @code
 *   SkeletonAsset* skeleton = loader->loadSkeleton(data, size);
 *   size_t boneCount = skeleton->getBoneCount();
 *   int handIndex = skeleton->getBoneIndex("LeftHand");
 *   Entity handEntity = skeleton->getBoneEntity(handIndex);
 * @endcode
 */
class UTILS_PUBLIC SkeletonAsset {
public:
    /**
     * 获取骨骼数量
     * @return 骨骼数量（与skin.joints_count一致）
     */
    size_t getBoneCount() const noexcept;

    /**
     * 按名称查找骨骼索引
     * @param boneName 骨骼名称（glTF node名称）
     * @return 骨骼索引，未找到返回-1
     * @note 骨骼索引用于getBoneEntity()和getInverseBindMatrices()
     */
    int getBoneIndex(const char* boneName) const noexcept;

    /**
     * 获取骨骼名称
     * @param boneIndex 骨骼索引（0 ~ getBoneCount()-1）
     * @return 骨骼名称，无效索引返回nullptr
     */
    const char* getBoneName(size_t boneIndex) const noexcept;

    /**
     * 获取骨骼对应的Filament实体
     * @param boneIndex 骨骼索引
     * @return 骨骼实体（带TransformManager组件）
     * @note 可用于读取/修改骨骼transform
     */
    utils::Entity getBoneEntity(size_t boneIndex) const noexcept;

    /**
     * 获取逆绑定矩阵数组
     * @return 逆绑定矩阵数组（长度为getBoneCount()）
     * @note 用于蒙皮计算：finalMatrix[i] = boneTransform[i] * inverseBindMatrix[i]
     */
    const filament::math::mat4f* getInverseBindMatrices() const noexcept;

    /**
     * 获取骨骼层级的根实体
     * @return 根实体（所有骨骼的共同祖先）
     */
    utils::Entity getRoot() const noexcept;

    /**
     * 验证骨骼索引是否有效（用于网格绑定验证）
     * @param indices 骨骼索引数组（来自JOINTS_0属性）
     * @param count 索引数量
     * @return 所有索引都有效返回true，否则返回false
     */
    bool validateBoneIndices(const uint16_t* indices, size_t count) const noexcept;

protected:
    SkeletonAsset() noexcept = default;
    ~SkeletonAsset() = default;

public:
    SkeletonAsset(SkeletonAsset const&) = delete;
    SkeletonAsset(SkeletonAsset&&) = delete;
    SkeletonAsset& operator=(SkeletonAsset const&) = delete;
    SkeletonAsset& operator=(SkeletonAsset&&) = delete;
};

} // namespace filament::gltfio

#endif // GLTFIO_SKELETONASSET_H
