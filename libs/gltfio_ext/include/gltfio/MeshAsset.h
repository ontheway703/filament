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
#include <filament/Texture.h>
#include <filament/TextureSampler.h>

#include <vector>
#include <cstdint>

namespace filament::gltfio {

class SkeletonAsset;
class StandaloneAnimator;
class MeshInstance;

/**
 * Sampler wrap modes (aligned with glTF specification)
 */
enum class SamplerWrapMode : uint32_t {
    CLAMP_TO_EDGE = 33071,
    MIRRORED_REPEAT = 33648,
    REPEAT = 10497
};

/**
 * Sampler minification filter modes (aligned with glTF specification)
 */
enum class SamplerMinFilter : uint32_t {
    NEAREST = 9728,
    LINEAR = 9729,
    NEAREST_MIPMAP_NEAREST = 9984,
    LINEAR_MIPMAP_NEAREST = 9985,
    NEAREST_MIPMAP_LINEAR = 9986,
    LINEAR_MIPMAP_LINEAR = 9987
};

/**
 * Sampler magnification filter modes (aligned with glTF specification)
 */
enum class SamplerMagFilter : uint32_t {
    NEAREST = 9728,
    LINEAR = 9729
};

/**
 * @struct TextureInfo
 * @brief 纹理信息 - 描述一个primitive需要的纹理
 *
 * 包含纹理的所有必要信息（URI或嵌入数据、采样器参数等）
 */
struct TextureInfo {
    size_t primitiveIndex;        ///< 所属primitive索引
    const char* slot;             ///< 纹理槽位名称（如"baseColorMap"）
    const char* uri;              ///< 纹理URI（如果是外部文件，否则为nullptr）
    const uint8_t* data;          ///< Embedded数据指针（如果嵌入，否则为nullptr）
    size_t dataSize;              ///< Embedded数据大小（字节）
    const char* mimeType;         ///< MIME类型（"image/png", "image/jpeg"等）

    // Sampler参数
    SamplerWrapMode wrapS;        ///< S方向wrap模式
    SamplerWrapMode wrapT;        ///< T方向wrap模式
    SamplerMinFilter minFilter;   ///< 缩小过滤器
    SamplerMagFilter magFilter;   ///< 放大过滤器
};

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
     */
    void uploadResources() noexcept;

    //=== 纹理加载API ===

    /**
     * 获取所有需要的纹理信息
     * @return 纹理信息列表
     * @note 在uploadResources()之前调用，用于查询需要加载的纹理
     *
     * 使用示例：
     * @code
     *   auto textures = mesh->getRequiredTextures();
     *   for (const auto& texInfo : textures) {
     *       Texture* tex = myLoadTexture(texInfo);
     *       mesh->bindTexture(texInfo.primitiveIndex, texInfo.slot, tex, sampler);
     *   }
     * @endcode
     */
    std::vector<TextureInfo> getRequiredTextures() const noexcept;

    /**
     * 绑定纹理到指定primitive的指定槽位
     * @param primitiveIndex primitive索引
     * @param slot 槽位名称（"baseColorMap", "normalMap", "metallicRoughnessMap", "occlusionMap", "emissiveMap"）
     * @param texture Filament Texture对象
     * @param sampler Filament TextureSampler对象
     * @note 在uploadResources()之前调用
     */
    void bindTexture(size_t primitiveIndex, const char* slot,
                     filament::Texture* texture,
                     const filament::TextureSampler& sampler) noexcept;

    /**
     * 辅助函数：从TextureInfo创建Filament TextureSampler
     * @param info 纹理信息
     * @return Filament TextureSampler
     */
    static filament::TextureSampler createSampler(const TextureInfo& info) noexcept;

    //=== 实例化API（共享GPU资源） ===

    /**
     * 创建新实例（共享VertexBuffer/IndexBuffer）
     * @return 新的MeshInstance指针
     *
     * 说明：
     * - 新实例共享GPU资源（VertexBuffer/IndexBuffer）
     * - 新实例有独立的Entity和MaterialInstance
     * - MeshAsset拥有实例的生命周期
     *
     * 使用场景：实例化场景（如100个相同的敌人）
     * @code
     *   MeshAsset* enemyAsset = loader->loadMesh(enemyData, enemySize);
     *   for (int i = 0; i < 100; i++) {
     *       MeshInstance* instance = enemyAsset->createInstance();
     *       scene->addEntity(instance->getRenderableEntity());
     *   }
     * @endcode
     */
    MeshInstance* createInstance() noexcept;

    /**
     * 销毁实例
     * @param instance 要销毁的实例
     *
     * 说明：
     * - 释放Entity和MaterialInstance
     * - 不影响其他实例
     * - 不影响MeshAsset自己的资源
     */
    void destroyInstance(MeshInstance* instance) noexcept;

    /**
     * 获取所有实例
     * @return 实例数组指针
     */
    MeshInstance* const* getInstances() const noexcept;

    /**
     * 获取实例数量
     * @return 实例数量
     */
    size_t getInstanceCount() const noexcept;

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
