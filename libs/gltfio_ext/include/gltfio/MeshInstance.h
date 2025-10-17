/**
 * @file MeshInstance.h
 * @brief Mesh实例 - 共享GPU资源的独立渲染对象
 *
 * MeshInstance表示MeshAsset的一个实例：
 * - 共享GPU资源（VertexBuffer/IndexBuffer）
 * - 独立的Entity和MaterialInstance
 * - 可以独立绑定骨骼和更新蒙皮
 *
 * 设计理由：
 * 实例化场景（如100个相同的敌人）中：
 * - 当前方案：每次loadMesh()重复解析和上传GPU资源 → 100x内存占用
 * - 新方案：loadMesh()一次，createInstance() 100次 → 1x GPU资源 + 100x Entity
 *
 * 与Filament FilamentAsset/FilamentInstance模式对齐：
 * - MeshAsset = 共享资源容器（VertexBuffer/IndexBuffer）
 * - MeshInstance = 独立场景对象（Entity/MaterialInstance）
 *
 * 典型用法：
 * @code
 *   MeshAsset* enemyAsset = loader->loadMesh(enemyData, enemySize);
 *   enemyAsset->uploadResources();
 *
 *   // 创建100个实例（共享GPU资源）
 *   for (int i = 0; i < 100; i++) {
 *       MeshInstance* instance = enemyAsset->createInstance();
 *       instance->bindSkeleton(skeleton);
 *       scene->addEntity(instance->getRenderableEntity());
 *   }
 * @endcode
 */

#ifndef GLTFIO_MESHINSTANCE_H
#define GLTFIO_MESHINSTANCE_H

#include <utils/compiler.h>
#include <utils/Entity.h>
#include <filament/Box.h>
#include <filament/RenderableManager.h>
#include <cstdint>

namespace filament {
    class MaterialInstance;
}

namespace filament::gltfio {

class MeshAsset;
class SkeletonAsset;
class StandaloneAnimator;

/**
 * @class MeshInstance
 * @brief MeshAsset的实例 - 共享GPU资源的独立渲染对象
 *
 * 核心特点：
 * - 共享GPU资源（VertexBuffer/IndexBuffer）
 * - 独立的Entity和MaterialInstance（可以有不同的材质参数）
 * - 独立的骨骼绑定和蒙皮状态
 * - 独立的Transform（位置/旋转/缩放）
 *
 * 生命周期管理：
 * - 由MeshAsset::createInstance()创建
 * - 由MeshAsset::destroyInstance()销毁
 * - 不能直接new/delete
 */
class UTILS_PUBLIC MeshInstance {
public:
    /**
     * 获取所属的MeshAsset
     * @return MeshAsset指针
     */
    MeshAsset const* getAsset() const noexcept;

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
     * @return AABB包围盒（从MeshAsset继承）
     */
    filament::Aabb getBoundingBox() const noexcept;

    /**
     * 获取指定primitive的MaterialInstance
     * @param primitiveIndex primitive索引
     * @return MaterialInstance指针
     * @note 用于修改材质参数（如颜色、纹理）
     */
    filament::MaterialInstance* getMaterialInstance(size_t primitiveIndex) noexcept;

    /**
     * 获取primitive数量
     * @return primitive数量
     */
    size_t getPrimitiveCount() const noexcept;

protected:
    MeshInstance() noexcept = default;
    ~MeshInstance() = default;

public:
    MeshInstance(MeshInstance const&) = delete;
    MeshInstance(MeshInstance&&) = delete;
    MeshInstance& operator=(MeshInstance const&) = delete;
    MeshInstance& operator=(MeshInstance&&) = delete;
};

} // namespace filament::gltfio

#endif // GLTFIO_MESHINSTANCE_H
