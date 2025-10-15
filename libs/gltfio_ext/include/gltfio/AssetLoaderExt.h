/**
 * @file AssetLoaderExt.h
 * @brief gltfio_ext扩展资产加载器 - 支持模块化加载骨骼动画资源
 *
 * AssetLoaderExt提供了与Filament原生gltfio不同的加载方式：
 * - 分离加载：可以独立加载skeleton、animation、mesh
 * - 资源复用：同一个skeleton可以被多个mesh复用
 * - 按需加载：支持运行时动态加载/卸载动画
 * - 换装系统：mesh和skeleton独立，支持runtime换装
 *
 * 设计理由：
 * Filament原生gltfio是整体加载，所有skeleton/animation/mesh绑定在一个Asset中。
 * gltfio_ext设计为模块化加载，适合需要动态资源管理的应用场景。
 */

#ifndef GLTFIO_ASSETLOADEREXT_H
#define GLTFIO_ASSETLOADEREXT_H

#include <gltfio/MaterialProvider.h>
#include <utils/compiler.h>
#include <stdint.h>

namespace filament {
    class Engine;
}

namespace utils {
    class EntityManager;
    class NameComponentManager;
}

namespace filament::gltfio {

class SkeletonAsset;
class MeshAsset;
class AnimationAsset;
class AnimationPack;

/**
 * AssetLoaderExt配置结构
 */
struct AssetConfigurationExt {
    filament::Engine* engine;           ///< Filament引擎实例（必需）
    MaterialProvider* materials;        ///< 材质提供器（必需，用于创建mesh材质）
    utils::NameComponentManager* names = nullptr;  ///< 可选：实体名称管理器
    utils::EntityManager* entities = nullptr;      ///< 可选：自定义实体管理器
};

/**
 * @class AssetLoaderExt
 * @brief 扩展资产加载器 - 从glTF文件中分离加载骨骼动画资源
 *
 * 核心功能：
 * - loadSkeleton()：加载骨骼层级结构和绑定姿态
 * - loadAnimation()：加载单个动画数据
 * - loadAnimationPack()：加载文件中的所有动画
 * - loadMesh()：加载蒙皮网格（需要uploadResources上传GPU）
 *
 * 典型用法：
 * @code
 *   AssetLoaderExt* loader = AssetLoaderExt::create(config);
 *   SkeletonAsset* skeleton = loader->loadSkeleton(data, size);
 *   AnimationAsset* anim = loader->loadAnimation(data, size);
 *   MeshAsset* mesh = loader->loadMesh(data, size);
 *   mesh->uploadResources();  // 上传GPU资源
 *   mesh->bindSkeleton(skeleton);
 * @endcode
 */
class UTILS_PUBLIC AssetLoaderExt {
public:
    /**
     * 创建资产加载器
     * @param config 配置参数（必须提供engine和materials）
     * @return 加载器实例
     */
    static AssetLoaderExt* create(const AssetConfigurationExt& config);

    /**
     * 销毁资产加载器
     * @param loader 加载器指针的指针，销毁后会被置为nullptr
     */
    static void destroy(AssetLoaderExt** loader);

    /**
     * 从glTF数据中加载骨骼层级结构
     * @param bytes glTF文件数据（.glb或.gltf+.bin）
     * @param nbytes 数据大小
     * @return 骨骼资产，失败返回nullptr
     * @note 从第一个skin加载，包含骨骼层级和逆绑定矩阵
     */
    SkeletonAsset* loadSkeleton(const uint8_t* bytes, uint32_t nbytes);

    /**
     * 从glTF数据中加载网格（包含蒙皮数据）
     * @param bytes glTF文件数据
     * @param nbytes 数据大小
     * @return 网格资产，失败返回nullptr
     * @note 只加载有JOINTS+WEIGHTS的skinned primitives
     * @note 加载后需调用uploadResources()上传GPU数据
     */
    MeshAsset* loadMesh(const uint8_t* bytes, uint32_t nbytes);

    /**
     * 从glTF数据中加载第一个动画
     * @param bytes glTF文件数据
     * @param nbytes 数据大小
     * @return 动画资产，失败返回nullptr
     */
    AnimationAsset* loadAnimation(const uint8_t* bytes, uint32_t nbytes);

    /**
     * 从glTF数据中加载所有动画（打包）
     * @param bytes glTF文件数据
     * @param nbytes 数据大小
     * @return 动画包，失败返回nullptr
     * @note 适合需要管理多个动画的场景（如角色动画库）
     */
    AnimationPack* loadAnimationPack(const uint8_t* bytes, uint32_t nbytes);

    void destroySkeleton(SkeletonAsset* skeleton);
    void destroyMesh(MeshAsset* mesh);
    void destroyAnimation(AnimationAsset* animation);
    void destroyAnimationPack(AnimationPack* pack);

protected:
    AssetLoaderExt() noexcept = default;
    ~AssetLoaderExt() = default;

public:
    AssetLoaderExt(AssetLoaderExt const&) = delete;
    AssetLoaderExt(AssetLoaderExt&&) = delete;
    AssetLoaderExt& operator=(AssetLoaderExt const&) = delete;
    AssetLoaderExt& operator=(AssetLoaderExt&&) = delete;
};

} // namespace filament::gltfio

#endif // GLTFIO_ASSETLOADEREXT_H
