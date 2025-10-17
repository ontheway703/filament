/**
 * @file AssetLoaderExt.cpp
 * @brief 资产加载器实现 - 模块化加载glTF资源的入口
 *
 * ========== 核心设计 ==========
 *
 * AssetLoaderExt提供4个独立的加载函数：
 * 1. loadSkeleton(): 加载骨骼层级（从glTF skin）
 * 2. loadMesh(): 加载蒙皮网格（从glTF meshes）
 * 3. loadAnimation(): 加载所有动画（从glTF animations[*]）
 * 4. destroyXxx(): 销毁对应资源
 *
 * 每个函数的工作流程：
 * 1. 使用cgltf解析glTF数据（cgltf_parse）
 * 2. 加载缓冲区数据（cgltf_load_buffers）
 * 3. 创建对应的资产对象（FSkeletonAsset/FMeshAsset等）
 * 4. 调用资产对象的loadFromGltfXxx()方法
 * 5. 释放cgltf数据（cgltf_free）
 * 6. 返回资产对象
 *
 * ========== 设计理由 ==========
 *
 * Q: 为什么每次加载都解析整个glTF文件？
 * A: glTF是自包含格式，skeleton/mesh/animation都可能引用：
 *    - buffers/bufferViews：顶点数据、关键帧数据
 *    - nodes：骨骼层级、场景图
 *    - accessors：数据访问器
 *    独立解析虽然有重复，但简化了资源管理（无需缓存cgltf_data）。
 *    性能影响：解析很快（< 1ms），主要开销在加载buffers。
 *
 * Q: 为什么不返回智能指针？
 * A: Filament风格：裸指针 + 显式destroy函数
 *    - 清晰的所有权（调用者负责销毁）
 *    - 与Filament其他API一致（Engine, MaterialInstance等）
 *
 * Q: 为什么loadAnimation()加载所有动画而非单个？
 * A: glTF本身支持多个动画（idle/walk/run等）：
 *    - 一次性加载避免重复解析glTF数据
 *    - "多个"是"单个"的超集（N=1时等同于单个动画）
 *    - AnimationAsset本身就是容器，通过索引访问各个动画
 */

#include <gltfio/AssetLoaderExt.h>
#include "GltfioExtInternal.h"
#include "FSkeletonAsset.h"
#include "FMeshAsset.h"
#include "FAnimationAsset.h"

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
    GLTFIO_EXT_LOG("========== loadMesh START ==========");
    auto* self = static_cast<FAssetLoaderExt*>(this);

    // 1. 解析glTF
    GLTFIO_EXT_LOG("Parsing glTF...");
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse(&options, bytes, nbytes, &data);
    GLTFIO_EXT_LOG("Parse result: " << result);

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

    // 3. 创建MeshAsset
    FMeshAsset* meshAsset = new FMeshAsset();
    meshAsset->mEngine = self->mEngine;
    meshAsset->mRenderableManager = &self->mEngine->getRenderableManager();
    meshAsset->mMaterialProvider = self->mMaterials;
    meshAsset->mGltfData = data;  // 保持对cgltf_data的引用（纹理信息需要）

    // 4. 加载所有skinned primitives
    GLTFIO_EXT_LOG("Calling loadFromGltfData...");
    bool success = meshAsset->loadFromGltfData(data);
    GLTFIO_EXT_LOG("loadFromGltfData returned: " << (success ? "success" : "failure"));

    // 注意：不释放cgltf数据，由MeshAsset在析构时释放（纹理信息需要保持引用）

    if (!success) {
        cgltf_free(data);  // 失败时才释放
        delete meshAsset;
        return nullptr;
    }

    return meshAsset;
}

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

    // 4. 创建AnimationAsset并加载所有动画
    FAnimationAsset* animAsset = new FAnimationAsset();
    bool success = animAsset->loadFromGltfData(data);

    cgltf_free(data);

    if (!success) {
        delete animAsset;
        return nullptr;
    }

    return animAsset;
}

void AssetLoaderExt::destroySkeleton(SkeletonAsset* skeleton) {
    delete static_cast<FSkeletonAsset*>(skeleton);
}

void AssetLoaderExt::destroyMesh(MeshAsset* mesh) {
    delete static_cast<FMeshAsset*>(mesh);
}

void AssetLoaderExt::destroyAnimation(AnimationAsset* animation) {
    delete static_cast<FAnimationAsset*>(animation);
}

} // namespace filament::gltfio
