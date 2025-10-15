/**
 * @file FMeshAsset.h
 * @brief 蒙皮网格资产内部实现 - 支持两阶段加载的数据结构
 *
 * 设计说明：
 * - FMeshAsset是MeshAsset的具体实现（F前缀表示Filament内部类型）
 * - 支持多个primitives（使用RenderableManager的多geometry slots）
 * - 实现两阶段加载（PendingBufferData存储待上传的CPU数据）
 *
 * 核心数据结构：
 * 1. 几何数据：
 *    - mVertexBuffers/mIndexBuffers: 每个primitive一个buffer
 *    - mMaterialInstances: 每个primitive一个材质（从glTF材质定义创建）
 *    - mRenderableEntity: 单个Entity，多个geometry slots
 *
 * 2. 两阶段加载：
 *    - mPendingUploads: 待上传的CPU数据（PendingBufferData）
 *    - mResourcesLoaded: 标记是否已上传完成
 *
 * 3. 蒙皮绑定：
 *    - mBoundSkeleton: 绑定的骨骼资产
 *    - mMaxBoneIndex: 验证骨骼索引范围
 */

#ifndef GLTFIO_EXT_FMESHASSET_H
#define GLTFIO_EXT_FMESHASSET_H

#include <gltfio/MeshAsset.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Box.h>
#include <utils/Entity.h>
#include <vector>

struct cgltf_primitive;
struct cgltf_data;

namespace filament {
    class Engine;
}

namespace filament::gltfio {

class SkeletonAsset;
class MaterialProvider;

/**
 * 待上传的GPU数据（两阶段加载的核心数据结构）
 *
 * 设计说明：
 * - loadFromGltfData()阶段：创建Buffer对象，数据保留在CPU（此结构）
 * - uploadResources()阶段：上传到GPU，释放CPU数据
 *
 * 数据所有权：
 * - data: 指向malloc或new[]分配的内存（需要在上传后释放）
 * - vertexBuffer/indexBuffer: Buffer对象的指针（Engine拥有所有权）
 */
struct PendingBufferData {
    void* data;
    size_t size;
    filament::VertexBuffer* vertexBuffer;
    filament::IndexBuffer* indexBuffer;
    int bufferIndex;  // for VertexBuffer::setBufferAt()
};

struct FMeshAsset : public MeshAsset {
    filament::Engine* mEngine = nullptr;
    filament::RenderableManager* mRenderableManager = nullptr;
    MaterialProvider* mMaterialProvider = nullptr;

    // 多个primitives的几何数据（使用RenderableManager的多geometry slots）
    std::vector<filament::VertexBuffer*> mVertexBuffers;
    std::vector<filament::IndexBuffer*> mIndexBuffers;
    std::vector<filament::MaterialInstance*> mMaterialInstances;

    // 单个渲染实体（包含多个geometry slots）
    utils::Entity mRenderableEntity;
    filament::RenderableManager::Instance mRenderableInstance;

    // 蒙皮绑定
    SkeletonAsset* mBoundSkeleton = nullptr;

    // 边界盒
    filament::Aabb mBoundingBox;

    // 骨骼索引范围（用于验证）
    uint16_t mMaxBoneIndex = 0;
    bool mHasSkinning = false;

    // 待上传的Buffer数据
    std::vector<PendingBufferData> mPendingUploads;
    bool mResourcesLoaded = false;

    // 从cgltf_data加载所有skinned primitives（只创建对象，不上传数据）
    bool loadFromGltfData(const cgltf_data* data);

    // 上传所有GPU数据
    void uploadResources();

    // 分批上传（返回本次上传的数量）
    size_t uploadNextBatch(size_t maxCount);

    ~FMeshAsset();
};

} // namespace filament::gltfio

#endif // GLTFIO_EXT_FMESHASSET_H
