/**
 * @file FMeshInstance.h
 * @brief MeshInstance内部实现 - 共享GPU资源的独立渲染实例
 *
 * 设计说明：
 * - FMeshInstance是MeshInstance的具体实现（F前缀表示Filament内部类型）
 * - 共享MeshAsset的VertexBuffer/IndexBuffer（不复制GPU资源）
 * - 拥有独立的Entity和MaterialInstance（可以有不同的材质参数）
 * - 拥有独立的骨骼绑定和蒙皮状态
 *
 * 核心数据结构：
 * 1. 共享资源（指向MeshAsset）：
 *    - mAsset: 指向所属的MeshAsset
 *    - VertexBuffer/IndexBuffer通过mAsset访问
 *
 * 2. 独立资源（实例拥有）：
 *    - mRenderableEntity: 独立的Entity
 *    - mMaterialInstances: 独立的MaterialInstance数组（clone自MeshAsset）
 *    - mBoundSkeleton: 独立的骨骼绑定
 *
 * 生命周期管理：
 * - 由MeshAsset::createInstance()创建
 * - 由MeshAsset::destroyInstance()销毁
 * - 析构函数释放独立资源，不影响共享资源
 */

#ifndef GLTFIO_EXT_FMESHINSTANCE_H
#define GLTFIO_EXT_FMESHINSTANCE_H

#include <gltfio/MeshInstance.h>
#include <filament/RenderableManager.h>
#include <utils/Entity.h>
#include <vector>

namespace filament {
    class Engine;
    class MaterialInstance;
}

namespace filament::gltfio {

class MeshAsset;
class SkeletonAsset;

/**
 * FMeshInstance: MeshInstance的内部实现
 *
 * 实例化模式：
 * - 共享GPU资源（VertexBuffer/IndexBuffer）
 * - 独立CPU状态（Entity/MaterialInstance/Transform）
 * - 独立骨骼绑定和蒙皮状态
 */
struct FMeshInstance : public MeshInstance {
    filament::Engine* mEngine = nullptr;
    MeshAsset* mAsset = nullptr;  // 指向所属的MeshAsset（共享资源的来源）

    // 独立资源（实例拥有）
    utils::Entity mRenderableEntity;
    filament::RenderableManager::Instance mRenderableInstance;
    std::vector<filament::MaterialInstance*> mMaterialInstances;  // clone自MeshAsset

    // 独立状态
    SkeletonAsset* mBoundSkeleton = nullptr;

    // 构造函数：从MeshAsset创建实例
    // 参数：
    //   asset: 所属的MeshAsset（提供共享的VertexBuffer/IndexBuffer）
    //   engine: Filament引擎
    FMeshInstance(MeshAsset* asset, filament::Engine* engine);

    // 析构函数：释放独立资源
    // 注意：不释放共享资源（VertexBuffer/IndexBuffer仍由MeshAsset拥有）
    ~FMeshInstance();
};

} // namespace filament::gltfio

#endif // GLTFIO_EXT_FMESHINSTANCE_H
