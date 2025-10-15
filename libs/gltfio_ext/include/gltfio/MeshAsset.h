/**
 * @file MeshAsset.h
 * @brief 网格资产 - 蒙皮网格的容器（支持两阶段加载）
 *
 * MeshAsset表示可以进行骨骼蒙皮的网格（skinned mesh）：
 * - 包含顶点数据（POSITION, JOINTS, WEIGHTS等）
 * - 包含材质信息（从glTF材质定义创建）
 * - 支持绑定到SkeletonAsset进行蒙皮变换
 *
 * 设计理由 - 两阶段加载：
 * 大型模型（如ecorche.glb有351个primitives）如果同步上传GPU会导致：
 * - 主线程阻塞数秒（等待GPU上传完成）
 * - 用户体验差（加载时卡顿）
 *
 * 解决方案：
 * 阶段1: loadMesh() - 只创建VertexBuffer/IndexBuffer对象，数据保存在CPU内存
 * 阶段2: uploadResources() - 批量或分帧上传GPU数据
 *
 * 这样可以：
 * - 快速返回控制权（loadMesh不阻塞）
 * - 分帧上传避免卡顿（uploadNextBatch）
 * - 支持loading界面（isResourcesLoaded查询进度）
 */

#ifndef GLTFIO_MESHASSET_H
#define GLTFIO_MESHASSET_H

#include <utils/compiler.h>
#include <utils/Entity.h>
#include <filament/Box.h>
#include <filament/RenderableManager.h>

namespace filament::gltfio {

class SkeletonAsset;
class StandaloneAnimator;

/**
 * @class MeshAsset
 * @brief 蒙皮网格资产 - 支持骨骼动画的3D模型
 *
 * 核心功能：
 * - 两阶段加载：loadMesh() → uploadResources()
 * - 骨骼绑定：bindSkeleton()绑定到骨骼层级
 * - 蒙皮更新：updateSkinning()每帧更新骨骼矩阵
 *
 * 典型用法：
 * @code
 *   MeshAsset* mesh = loader->loadMesh(data, size);
 *   mesh->uploadResources();  // 上传GPU（可以分帧调用uploadNextBatch）
 *   mesh->bindSkeleton(skeleton);
 *   scene->addEntity(mesh->getRenderableEntity());
 *
 *   // 每帧更新
 *   animator->update(deltaTime);
 *   mesh->updateSkinning(animator);
 * @endcode
 */
class UTILS_PUBLIC MeshAsset {
public:
    /**
     * 绑定骨骼
     * @param skeleton 骨骼资产
     * @return 绑定成功返回true
     * @note 会验证mesh的骨骼索引是否在skeleton范围内
     */
    bool bindSkeleton(SkeletonAsset* skeleton) noexcept;

    /**
     * 获取已绑定的骨骼
     * @return 骨骼资产，未绑定返回nullptr
     */
    SkeletonAsset* getBoundSkeleton() const noexcept;

    /**
     * 更新蒙皮变换（每帧调用）
     * @param animator 动画器（提供当前骨骼变换矩阵）
     * @note 从animator获取骨骼矩阵并设置到RenderableManager
     */
    void updateSkinning(const StandaloneAnimator* animator) noexcept;

    /**
     * 获取可渲染实体
     * @return Filament实体（用于添加到Scene）
     */
    utils::Entity getRenderableEntity() const noexcept;

    /**
     * 获取RenderableManager实例
     * @return Renderable组件实例
     */
    filament::RenderableManager::Instance getRenderableInstance() const noexcept;

    /**
     * 获取包围盒
     * @return AABB包围盒（所有primitives的合并包围盒）
     */
    filament::Aabb getBoundingBox() const noexcept;

    //=== 资源加载API（两阶段加载） ===

    /**
     * 上传所有GPU资源（一次性上传所有pending数据）
     * @note 对于大模型可能阻塞较久，建议使用uploadNextBatch分帧上传
     */
    void uploadResources() noexcept;

    /**
     * 分批上传GPU资源（用于分帧加载）
     * @param maxCount 本次最多上传的buffer数量
     * @return 实际上传的数量
     * @note 返回0表示全部上传完成
     */
    size_t uploadNextBatch(size_t maxCount) noexcept;

    /**
     * 查询资源是否已全部上传
     * @return 已上传返回true
     */
    bool isResourcesLoaded() const noexcept;

protected:
    MeshAsset() noexcept = default;
    ~MeshAsset() = default;

public:
    MeshAsset(MeshAsset const&) = delete;
    MeshAsset(MeshAsset&&) = delete;
    MeshAsset& operator=(MeshAsset const&) = delete;
    MeshAsset& operator=(MeshAsset&&) = delete;
};

} // namespace filament::gltfio

#endif // GLTFIO_MESHASSET_H
