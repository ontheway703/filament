/**
 * @file MeshInstance.cpp
 * @brief MeshInstance实现 - 共享GPU资源的独立渲染实例
 *
 * 核心设计：
 * - 共享GPU资源（VertexBuffer/IndexBuffer）
 * - 独立CPU状态（Entity/MaterialInstance）
 * - 支持独立的骨骼绑定和蒙皮
 *
 * 参考：Filament FilamentAsset/FilamentInstance模式
 */

#include <gltfio/MeshInstance.h>
#include <gltfio/MeshAsset.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/StandaloneAnimator.h>
#include "FMeshInstance.h"
#include "FMeshAsset.h"
#include "GltfioExtInternal.h"

#include <filament/Engine.h>
#include <filament/RenderableManager.h>
#include <filament/MaterialInstance.h>
#include <filament/Material.h>
#include <filament/Box.h>
#include <utils/EntityManager.h>
#include <math/vec3.h>
#include <math/vec4.h>

using namespace filament;
using namespace filament::math;
using namespace utils;

namespace filament::gltfio {

/**
 * 辅助函数：复制MaterialInstance的参数
 *
 * 注意：Filament的MaterialInstance不提供参数枚举或get API
 * 目前的策略：只复制材质引用，参数不复制
 * 用户可以在创建实例后手动设置参数
 *
 * TODO: 考虑在MeshAsset中保存参数列表，用于实例复制
 */
static void copyMaterialParameters(MaterialInstance* src, MaterialInstance* dst) {
    if (!src || !dst) return;

    // NOTE: Filament不提供MaterialInstance参数的get API
    // 因此无法自动复制参数
    // 实例的材质参数目前需要用户手动设置

    // 纹理会通过材质的默认值或用户显式绑定来设置
}

/**
 * FMeshInstance构造函数
 *
 * 核心流程：
 * 1. 创建独立的Entity
 * 2. Clone MaterialInstances（从MeshAsset复制）
 * 3. 创建RenderableManager组件（共享VertexBuffer/IndexBuffer）
 */
FMeshInstance::FMeshInstance(MeshAsset* asset, Engine* engine)
    : mEngine(engine), mAsset(asset) {

    auto* meshAsset = static_cast<FMeshAsset*>(asset);

    // 1. 创建独立的Entity
    mRenderableEntity = EntityManager::get().create();

    // 2. Clone MaterialInstances
    size_t primCount = meshAsset->mMaterialInstances.size();
    mMaterialInstances.reserve(primCount);

    for (size_t i = 0; i < primCount; i++) {
        MaterialInstance* original = meshAsset->mMaterialInstances[i];
        if (!original) {
            GLTFIO_EXT_WARN("Original MaterialInstance is null at index " << i);
            mMaterialInstances.push_back(nullptr);
            continue;
        }

        // 创建新的MaterialInstance（使用相同的Material）
        Material* mat = const_cast<Material*>(original->getMaterial());
        MaterialInstance* cloned = mat->createInstance();

        // 复制参数
        copyMaterialParameters(original, cloned);

        mMaterialInstances.push_back(cloned);
    }

    // 3. 创建RenderableManager组件（共享VertexBuffer/IndexBuffer）
    RenderableManager::Builder builder(primCount);

    for (size_t i = 0; i < primCount; i++) {
        // 共享VertexBuffer和IndexBuffer（从MeshAsset）
        builder.geometry(i,
                        RenderableManager::PrimitiveType::TRIANGLES,
                        meshAsset->mVertexBuffers[i],
                        meshAsset->mIndexBuffers[i]);

        // 使用clone的MaterialInstance
        builder.material(i, mMaterialInstances[i]);
    }

    // 设置包围盒和其他属性
    Box box;
    box.set(meshAsset->mBoundingBox.min, meshAsset->mBoundingBox.max);

    builder.boundingBox(box)
           .culling(true)
           .receiveShadows(true)
           .castShadows(true)
           .skinning(512);  // 支持512个骨骼

    builder.build(*mEngine, mRenderableEntity);

    mRenderableInstance = mEngine->getRenderableManager().getInstance(mRenderableEntity);

    GLTFIO_EXT_LOG("Created MeshInstance with " << primCount << " primitives");
}

/**
 * FMeshInstance析构函数
 *
 * 释放独立资源，不影响共享资源
 */
FMeshInstance::~FMeshInstance() {
    if (mEngine) {
        // 1. 销毁RenderableManager组件（在销毁Entity之前）
        if (mRenderableEntity) {
            mEngine->destroy(mRenderableEntity);
            EntityManager::get().destroy(mRenderableEntity);
        }

        // 2. 销毁MaterialInstances（独立资源）
        for (auto* mi : mMaterialInstances) {
            if (mi) {
                mEngine->destroy(mi);
            }
        }
    }

    mMaterialInstances.clear();

    // 注意：不销毁VertexBuffer/IndexBuffer（共享资源，由MeshAsset拥有）
}

// 公开API实现

MeshAsset const* MeshInstance::getAsset() const noexcept {
    auto* self = static_cast<const FMeshInstance*>(this);
    return self->mAsset;
}

bool MeshInstance::bindSkeleton(SkeletonAsset* skeleton) noexcept {
    auto* self = static_cast<FMeshInstance*>(this);
    auto* meshAsset = static_cast<FMeshAsset*>(self->mAsset);

    if (!meshAsset->mHasSkinning) {
        GLTFIO_EXT_WARN("Mesh has no skinning data");
        return false;
    }

    if (!skeleton) {
        GLTFIO_EXT_WARN("Null skeleton");
        return false;
    }

    // 验证骨骼索引范围
    if (meshAsset->mMaxBoneIndex >= skeleton->getBoneCount()) {
        GLTFIO_EXT_WARN("Bone index out of range: maxBoneIndex=" << meshAsset->mMaxBoneIndex
                       << ", skeletonBoneCount=" << skeleton->getBoneCount());
        return false;
    }

    GLTFIO_EXT_LOG("Skeleton bound successfully to instance: " << skeleton->getBoneCount() << " bones");

    self->mBoundSkeleton = skeleton;
    return true;
}

SkeletonAsset* MeshInstance::getBoundSkeleton() const noexcept {
    auto* self = static_cast<const FMeshInstance*>(this);
    return self->mBoundSkeleton;
}

void MeshInstance::updateSkinning(const StandaloneAnimator* animator) noexcept {
    auto* self = static_cast<FMeshInstance*>(this);

    if (!self->mBoundSkeleton || !animator) {
        return;
    }

    const auto* boneMatrices = animator->getBoneMatrices();
    size_t boneCount = animator->getBoneCount();

    if (boneMatrices && boneCount > 0 && self->mRenderableInstance) {
        self->mEngine->getRenderableManager().setBones(self->mRenderableInstance,
                                                       boneMatrices, boneCount);
    }
}

Entity MeshInstance::getRenderableEntity() const noexcept {
    auto* self = static_cast<const FMeshInstance*>(this);
    return self->mRenderableEntity;
}

RenderableManager::Instance MeshInstance::getRenderableInstance() const noexcept {
    auto* self = static_cast<const FMeshInstance*>(this);
    return self->mRenderableInstance;
}

Aabb MeshInstance::getBoundingBox() const noexcept {
    auto* self = static_cast<const FMeshInstance*>(this);
    auto* meshAsset = static_cast<const FMeshAsset*>(self->mAsset);
    return meshAsset->mBoundingBox;
}

MaterialInstance* MeshInstance::getMaterialInstance(size_t primitiveIndex) noexcept {
    auto* self = static_cast<FMeshInstance*>(this);

    if (primitiveIndex >= self->mMaterialInstances.size()) {
        GLTFIO_EXT_WARN("Invalid primitive index: " << primitiveIndex);
        return nullptr;
    }

    return self->mMaterialInstances[primitiveIndex];
}

size_t MeshInstance::getPrimitiveCount() const noexcept {
    auto* self = static_cast<const FMeshInstance*>(this);
    return self->mMaterialInstances.size();
}

} // namespace filament::gltfio
