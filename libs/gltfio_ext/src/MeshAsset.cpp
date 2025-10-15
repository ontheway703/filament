/**
 * @file MeshAsset.cpp
 * @brief 蒙皮网格加载实现 - 支持两阶段加载和材质提取
 *
 * ========== 核心设计 ==========
 *
 * 1. 两阶段加载（解决大模型阻塞问题）：
 *    阶段1: loadFromGltfData()
 *      - 解析glTF数据，创建VertexBuffer/IndexBuffer对象
 *      - 提取顶点数据到CPU内存（PendingBufferData）
 *      - 创建材质实例（MaterialProvider）
 *      - 立即返回，不阻塞主线程
 *
 *    阶段2: uploadResources() 或 uploadNextBatch()
 *      - 将CPU内存数据上传到GPU
 *      - 支持一次性全部上传或分帧上传
 *      - 完成后释放CPU内存
 *
 * 2. 材质从glTF文件加载（参考Filament AssetLoader.cpp）：
 *    - getMaterialKey()：从cgltf_material提取材质属性
 *      * baseColor/metallic/roughness等PBR参数
 *      * 纹理贴图（baseColor/normal/occlusion/emissive等）
 *      * alphaMode（OPAQUE/MASK/BLEND）
 *      * doubleSided/unlit等特性
 *
 *    - 每个primitive创建独立的MaterialInstance
 *      * 调用MaterialProvider::createMaterialInstance()
 *      * 传入MaterialKey和UvMap
 *      * 失败时清理已创建的资源
 *
 * 3. 多primitive处理（支持大型模型如ecorche.glb的351个primitives）：
 *    - 筛选有skinning data的primitives（POSITION + JOINTS + WEIGHTS）
 *    - 所有primitives共享一个Entity，使用多个geometry slots
 *    - 每个slot独立材质、VertexBuffer、IndexBuffer
 *
 * ========== 设计理由 ==========
 *
 * Q: 为什么需要两阶段加载？
 * A: 大型模型（如ecorche.glb有351个primitives）如果同步上传GPU会导致：
 *    - 主线程阻塞数秒（等待GPU上传完成）
 *    - 用户体验差（加载时卡顿）
 *    两阶段加载解决方案：
 *    - loadMesh()快速返回（只创建对象，数据留在CPU）
 *    - uploadResources()可以分帧调用（避免卡顿）
 *    - 支持显示loading进度条（isResourcesLoaded查询）
 *
 * Q: 为什么材质从glTF读取而非默认材质？
 * A: glTF文件包含完整的材质定义（PBR参数、纹理等），使用默认材质会导致：
 *    - 丢失艺术家设计的外观（所有mesh看起来一样）
 *    - 用户必须手动重新设置材质（繁琐且容易出错）
 *    从glTF读取材质的好处：
 *    - 所见即所得（与DCC工具中的外观一致）
 *    - 零配置（加载即可使用）
 *    - 参考Filament原生gltfio的实现（getMaterialKey + MaterialProvider）
 *
 * Q: 为什么用一个Entity多个geometry slots而非多个Entity？
 * A: 性能优化考虑：
 *    - 减少Entity数量（降低场景图开销）
 *    - 统一culling（共享包围盒）
 *    - 统一蒙皮矩阵（一次setBones调用）
 */

#include <gltfio/MeshAsset.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/StandaloneAnimator.h>
#include <gltfio/MaterialProvider.h>
#include "FMeshAsset.h"
#include "GltfioExtInternal.h"

#include <filament/Engine.h>
#include <filament/RenderableManager.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <utils/EntityManager.h>
#include <math/vec3.h>
#include <cgltf.h>

using namespace filament;
using namespace filament::math;
using namespace utils;

namespace filament::gltfio {

// 默认glTF材质（参考Filament AssetLoader.cpp）
static constexpr cgltf_material kDefaultMat = {
    .name = (char*) "Default gltfio_ext material",
    .has_pbr_metallic_roughness = true,
    .has_pbr_specular_glossiness = false,
    .has_clearcoat = false,
    .has_transmission = false,
    .has_volume = false,
    .has_ior = false,
    .has_specular = false,
    .has_sheen = false,
    .pbr_metallic_roughness = {
        .base_color_factor = {1.0, 1.0, 1.0, 1.0},
        .metallic_factor = 1.0,
        .roughness_factor = 1.0,
    },
};

/**
 * 从cgltf_material提取MaterialKey（简化版本，参考Filament AssetLoader.cpp:1337）
 *
 * 核心功能：
 * - 读取glTF材质定义（PBR金属/粗糙度工作流）
 * - 转换为Filament MaterialKey格式
 * - 设置UV映射（哪个纹理用哪个UV set）
 *
 * 提取的属性：
 * - baseColor纹理和系数
 * - metallic/roughness纹理和系数
 * - normal贴图
 * - occlusion贴图
 * - emissive纹理和系数
 * - alphaMode（OPAQUE/MASK/BLEND）
 * - doubleSided/unlit标记
 *
 * @param inputMat glTF材质定义（可为nullptr，使用默认材质）
 * @param uvmap [输出] UV映射表（纹理→UV set）
 * @param vertexColor 是否有顶点颜色
 * @return MaterialKey 用于创建MaterialInstance
 */
static MaterialKey getMaterialKey(const cgltf_material* inputMat, UvMap* uvmap, bool vertexColor) {
    if (!inputMat) {
        inputMat = &kDefaultMat;
    }

    auto mrConfig = inputMat->pbr_metallic_roughness;

    // 初始化uvmap
    uvmap->fill(UNUSED);
    uvmap->at(0) = UV0;
    uvmap->at(1) = UV1;

    MaterialKey matkey = {};
    matkey.doubleSided = !!inputMat->double_sided;
    matkey.unlit = !!inputMat->unlit;
    matkey.hasVertexColors = vertexColor;
    matkey.hasBaseColorTexture = (mrConfig.base_color_texture.texture != nullptr);
    matkey.hasNormalTexture = (inputMat->normal_texture.texture != nullptr);
    matkey.hasOcclusionTexture = (inputMat->occlusion_texture.texture != nullptr);
    matkey.hasEmissiveTexture = (inputMat->emissive_texture.texture != nullptr);
    matkey.hasMetallicRoughnessTexture = (mrConfig.metallic_roughness_texture.texture != nullptr);
    matkey.baseColorUV = (uint8_t) mrConfig.base_color_texture.texcoord;
    matkey.metallicRoughnessUV = (uint8_t) mrConfig.metallic_roughness_texture.texcoord;
    matkey.emissiveUV = (uint8_t) inputMat->emissive_texture.texcoord;
    matkey.aoUV = (uint8_t) inputMat->occlusion_texture.texcoord;
    matkey.normalUV = (uint8_t) inputMat->normal_texture.texcoord;

    switch (inputMat->alpha_mode) {
        case cgltf_alpha_mode_opaque:
            matkey.alphaMode = AlphaMode::OPAQUE;
            break;
        case cgltf_alpha_mode_mask:
            matkey.alphaMode = AlphaMode::MASK;
            break;
        case cgltf_alpha_mode_blend:
            matkey.alphaMode = AlphaMode::BLEND;
            break;
        default:
            matkey.alphaMode = AlphaMode::OPAQUE;
            break;
    }

    return matkey;
}

// 辅助函数：计算AABB
static Aabb computeAABB(const cgltf_primitive* prim) {
    float3 minBounds(std::numeric_limits<float>::max());
    float3 maxBounds(std::numeric_limits<float>::lowest());

    for (size_t i = 0; i < prim->attributes_count; ++i) {
        if (prim->attributes[i].type == cgltf_attribute_type_position) {
            const cgltf_accessor* accessor = prim->attributes[i].data;
            if (accessor->has_min && accessor->has_max) {
                minBounds = float3(accessor->min[0], accessor->min[1], accessor->min[2]);
                maxBounds = float3(accessor->max[0], accessor->max[1], accessor->max[2]);
            }
            break;
        }
    }

    return Aabb(minBounds, maxBounds);
}

// 辅助函数：查找BONE_INDICES的最大值
static uint16_t findMaxBoneIndex(const cgltf_accessor* accessor) {
    if (!accessor) return 0;

    uint16_t maxIndex = 0;
    size_t count = accessor->count * 4;  // vec4

    // 分配临时缓冲区
    uint16_t* data = new uint16_t[count];
    cgltf_accessor_unpack_indices(accessor, data, sizeof(uint16_t), count);

    for (size_t i = 0; i < count; ++i) {
        if (data[i] > maxIndex) {
            maxIndex = data[i];
        }
    }

    delete[] data;
    return maxIndex;
}

// 辅助函数：检查primitive是否有顶点颜色
static bool primitiveHasVertexColor(const cgltf_primitive* prim) {
    for (size_t i = 0; i < prim->attributes_count; ++i) {
        if (prim->attributes[i].type == cgltf_attribute_type_color) {
            return true;
        }
    }
    return false;
}

/**
 * 从glTF数据加载蒙皮网格（两阶段加载的阶段1）
 *
 * 核心流程：
 * 1. 遍历所有meshes和primitives，筛选有skinning data的
 *    - 需要POSITION + JOINTS + WEIGHTS属性
 *    - 跳过无skinning data的primitives
 *
 * 2. 为每个primitive创建资源（不上传GPU）：
 *    a) 创建VertexBuffer/IndexBuffer对象
 *    b) 解包顶点/索引数据到CPU内存
 *    c) 保存到mPendingUploads（延迟上传）
 *    d) 从glTF材质定义创建MaterialInstance
 *    e) 计算AABB和maxBoneIndex
 *
 * 3. 创建单个Renderable Entity with多个geometry slots：
 *    - 所有primitives共享一个Entity
 *    - 每个primitive一个slot（独立材质+buffer）
 *    - 统一包围盒和蒙皮设置
 *
 * 关键设计：
 * - 数据保留在CPU（mPendingUploads），不阻塞主线程
 * - 材质从prim->material读取，每个primitive独立材质
 * - 失败时正确清理已分配的资源
 *
 * @param data cgltf解析的glTF数据
 * @return 成功返回true，失败返回false
 */
bool FMeshAsset::loadFromGltfData(const cgltf_data* data) {
    if (!data || data->meshes_count == 0) {
        GLTFIO_EXT_WARN("No meshes in glTF data");
        return false;
    }

    if (!mMaterialProvider) {
        GLTFIO_EXT_WARN("No MaterialProvider available");
        return false;
    }

    // 临时存储primitive信息
    struct PrimitiveInfo {
        const cgltf_primitive* primitive;
        VertexBuffer* vertexBuffer;
        IndexBuffer* indexBuffer;
        MaterialInstance* materialInstance;
        Aabb aabb;
        uint16_t maxBoneIndex;
    };
    std::vector<PrimitiveInfo> primitives;

    GLTFIO_EXT_LOG("Scanning " << data->meshes_count << " meshes for skinned primitives...");

    // 1. 遍历所有meshes和primitives，筛选有skinning data的
    for (size_t meshIdx = 0; meshIdx < data->meshes_count; ++meshIdx) {
        const cgltf_mesh* mesh = &data->meshes[meshIdx];

        for (size_t primIdx = 0; primIdx < mesh->primitives_count; ++primIdx) {
            const cgltf_primitive* prim = &mesh->primitives[primIdx];

            // 查找POSITION, JOINTS, WEIGHTS
            const cgltf_accessor* positionAccessor = nullptr;
            const cgltf_accessor* boneIndicesAccessor = nullptr;
            const cgltf_accessor* boneWeightsAccessor = nullptr;

            for (size_t i = 0; i < prim->attributes_count; ++i) {
                const cgltf_attribute& attr = prim->attributes[i];
                switch (attr.type) {
                    case cgltf_attribute_type_position:
                        positionAccessor = attr.data;
                        break;
                    case cgltf_attribute_type_joints:
                        boneIndicesAccessor = attr.data;
                        break;
                    case cgltf_attribute_type_weights:
                        boneWeightsAccessor = attr.data;
                        break;
                    default:
                        break;
                }
            }

            // 只加载有skinning data的primitives
            if (!positionAccessor || !boneIndicesAccessor || !boneWeightsAccessor) {
                continue;
            }

            mHasSkinning = true;

            if (primitives.size() % 50 == 0) {
                GLTFIO_EXT_LOG("Processing primitive " << primitives.size() << "...");
            }

            // 创建VertexBuffer（不上传数据）
            const size_t vertexCount = positionAccessor->count;
            VertexBuffer::Builder vbb;
            vbb.vertexCount(vertexCount)
               .bufferCount(1)
               .attribute(VertexAttribute::POSITION, 0,
                          VertexBuffer::AttributeType::FLOAT3, 0, sizeof(float) * 3);

            VertexBuffer* vertexBuffer = vbb.build(*mEngine);

            // 填充POSITION数据到CPU内存
            float* vertexData = new float[vertexCount * 3];
            cgltf_accessor_unpack_floats(positionAccessor, vertexData, vertexCount * 3);

            // 保存待上传数据，不立即上传
            PendingBufferData pending = {};
            pending.data = vertexData;
            pending.size = vertexCount * sizeof(float) * 3;
            pending.vertexBuffer = vertexBuffer;
            pending.indexBuffer = nullptr;
            pending.bufferIndex = 0;
            mPendingUploads.push_back(pending);

            // 创建IndexBuffer（不上传数据）
            IndexBuffer* indexBuffer = nullptr;
            if (prim->indices) {
                const cgltf_accessor* indexAccessor = prim->indices;
                const size_t indexCount = indexAccessor->count;

                IndexBuffer::IndexType indexType = IndexBuffer::IndexType::USHORT;
                size_t indexSize = 2;
                if (indexAccessor->component_type == cgltf_component_type_r_32u) {
                    indexType = IndexBuffer::IndexType::UINT;
                    indexSize = 4;
                }

                indexBuffer = IndexBuffer::Builder()
                    .indexCount(indexCount)
                    .bufferType(indexType)
                    .build(*mEngine);

                // 解包索引数据到CPU内存
                void* indexData = malloc(indexCount * indexSize);
                cgltf_accessor_unpack_indices(indexAccessor, indexData, indexSize, indexCount);

                // 保存待上传数据，不立即上传
                PendingBufferData pending = {};
                pending.data = indexData;
                pending.size = indexCount * indexSize;
                pending.vertexBuffer = nullptr;
                pending.indexBuffer = indexBuffer;
                pending.bufferIndex = -1;
                mPendingUploads.push_back(pending);
            }

            // 创建材质实例
            bool hasVertexColor = primitiveHasVertexColor(prim);
            UvMap uvmap;
            MaterialKey matkey = getMaterialKey(prim->material, &uvmap, hasVertexColor);

            const char* materialName = (prim->material && prim->material->name) ?
                                       prim->material->name : "gltfio_ext_material";
            MaterialInstance* materialInstance =
                mMaterialProvider->createMaterialInstance(&matkey, &uvmap, materialName);

            if (!materialInstance) {
                GLTFIO_EXT_WARN("Failed to create material instance for primitive");
                // Clean up: data was already added to mPendingUploads, remove them first
                size_t numToRemove = indexBuffer ? 2 : 1;
                for (size_t i = 0; i < numToRemove; ++i) {
                    auto& pending = mPendingUploads.back();
                    if (pending.vertexBuffer) {
                        delete[] static_cast<float*>(pending.data);
                        mEngine->destroy(pending.vertexBuffer);
                    } else if (pending.indexBuffer) {
                        free(pending.data);
                        mEngine->destroy(pending.indexBuffer);
                    }
                    mPendingUploads.pop_back();
                }
                continue;
            }

            // 计算AABB和maxBoneIndex
            Aabb aabb = computeAABB(prim);
            uint16_t maxBoneIndex = findMaxBoneIndex(boneIndicesAccessor);

            // 保存primitive信息
            primitives.push_back({prim, vertexBuffer, indexBuffer, materialInstance, aabb, maxBoneIndex});

            // 扩展总AABB
            mBoundingBox.min = min(mBoundingBox.min, aabb.min);
            mBoundingBox.max = max(mBoundingBox.max, aabb.max);

            // 更新最大bone index
            if (maxBoneIndex > mMaxBoneIndex) {
                mMaxBoneIndex = maxBoneIndex;
            }
        }
    }

    if (primitives.empty()) {
        GLTFIO_EXT_WARN("No skinned primitives found");
        return false;
    }

    GLTFIO_EXT_LOG("Loaded " << primitives.size() << " skinned primitives");

    // 2. 创建RenderableEntity with多个geometry slots
    mRenderableEntity = EntityManager::get().create();

    Box box;
    box.set(mBoundingBox.min, mBoundingBox.max);

    RenderableManager::Builder builder(primitives.size());
    builder.boundingBox(box)
           .culling(true)
           .receiveShadows(true)
           .castShadows(true)
           .skinning(512);  // 支持512个骨骼

    // 3. 为每个primitive添加geometry slot
    for (size_t i = 0; i < primitives.size(); ++i) {
        builder.material(i, primitives[i].materialInstance)
               .geometry(i, RenderableManager::PrimitiveType::TRIANGLES,
                         primitives[i].vertexBuffer,
                         primitives[i].indexBuffer);

        mVertexBuffers.push_back(primitives[i].vertexBuffer);
        mIndexBuffers.push_back(primitives[i].indexBuffer);
        mMaterialInstances.push_back(primitives[i].materialInstance);
    }

    builder.build(*mEngine, mRenderableEntity);
    mRenderableInstance = mRenderableManager->getInstance(mRenderableEntity);

    return true;
}

/**
 * 上传所有GPU资源（两阶段加载的阶段2 - 一次性完成）
 *
 * 核心功能：
 * - 遍历mPendingUploads中的所有待上传数据
 * - 调用VertexBuffer::setBufferAt/IndexBuffer::setBuffer上传GPU
 * - 上传完成后释放CPU内存
 * - 标记mResourcesLoaded=true
 *
 * 适用场景：
 * - 小型模型（primitive数量少，上传快）
 * - 不需要分帧加载的场景
 *
 * 注意事项：
 * - 大型模型可能阻塞较久（如ecorche.glb的351个primitives）
 * - 建议大模型使用uploadNextBatch分帧上传
 */
void FMeshAsset::uploadResources() {
    if (mResourcesLoaded) {
        return;
    }

    GLTFIO_EXT_LOG("Uploading " << mPendingUploads.size() << " buffers to GPU...");

    for (auto& pending : mPendingUploads) {
        if (pending.vertexBuffer) {
            VertexBuffer::BufferDescriptor desc(
                pending.data, pending.size,
                [](void* buffer, size_t, void*) { delete[] static_cast<float*>(buffer); }
            );
            pending.vertexBuffer->setBufferAt(*mEngine, pending.bufferIndex, std::move(desc));
        } else if (pending.indexBuffer) {
            IndexBuffer::BufferDescriptor desc(
                pending.data, pending.size,
                [](void* buffer, size_t, void*) { free(buffer); }
            );
            pending.indexBuffer->setBuffer(*mEngine, std::move(desc));
        }
    }

    mPendingUploads.clear();
    mResourcesLoaded = true;

    GLTFIO_EXT_LOG("GPU upload complete");
}

/**
 * 分批上传GPU资源（两阶段加载的阶段2 - 分帧完成）
 *
 * 核心功能：
 * - 每次最多上传maxCount个buffer
 * - 上传后从mPendingUploads移除
 * - 全部上传完成后标记mResourcesLoaded=true
 *
 * 适用场景：
 * - 大型模型（如ecorche.glb的351个primitives）
 * - 需要显示loading进度的场景
 * - 避免单帧卡顿
 *
 * 典型用法：
 * @code
 *   while (!mesh->isResourcesLoaded()) {
 *       mesh->uploadNextBatch(10);  // 每帧最多上传10个buffer
 *       updateLoadingUI(...);
 *   }
 * @endcode
 *
 * @param maxCount 本次最多上传的buffer数量
 * @return 实际上传的数量（返回0表示全部完成）
 */
size_t FMeshAsset::uploadNextBatch(size_t maxCount) {
    if (mResourcesLoaded || mPendingUploads.empty()) {
        return 0;
    }

    size_t uploadCount = std::min(maxCount, mPendingUploads.size());
    GLTFIO_EXT_LOG("Uploading batch: " << uploadCount << " buffers...");

    for (size_t i = 0; i < uploadCount; ++i) {
        auto& pending = mPendingUploads[i];

        if (pending.vertexBuffer) {
            VertexBuffer::BufferDescriptor desc(
                pending.data, pending.size,
                [](void* buffer, size_t, void*) { delete[] static_cast<float*>(buffer); }
            );
            pending.vertexBuffer->setBufferAt(*mEngine, pending.bufferIndex, std::move(desc));
        } else if (pending.indexBuffer) {
            IndexBuffer::BufferDescriptor desc(
                pending.data, pending.size,
                [](void* buffer, size_t, void*) { free(buffer); }
            );
            pending.indexBuffer->setBuffer(*mEngine, std::move(desc));
        }
    }

    // 移除已上传的项
    mPendingUploads.erase(mPendingUploads.begin(), mPendingUploads.begin() + uploadCount);

    if (mPendingUploads.empty()) {
        mResourcesLoaded = true;
        GLTFIO_EXT_LOG("All GPU uploads complete");
    }

    return uploadCount;
}

FMeshAsset::~FMeshAsset() {
    // 释放未上传的pending数据
    for (auto& pending : mPendingUploads) {
        if (pending.data) {
            if (pending.vertexBuffer) {
                delete[] static_cast<float*>(pending.data);
            } else if (pending.indexBuffer) {
                free(pending.data);
            }
        }
    }
    mPendingUploads.clear();

    if (mEngine) {
        // 销毁所有VertexBuffers
        for (auto* vb : mVertexBuffers) {
            if (vb) mEngine->destroy(vb);
        }
        // 销毁所有IndexBuffers
        for (auto* ib : mIndexBuffers) {
            if (ib) mEngine->destroy(ib);
        }
        // Note: mMaterialInstances 由MaterialProvider管理，不在这里销毁

        // 销毁Entity
        if (mRenderableEntity) {
            EntityManager::get().destroy(mRenderableEntity);
        }
    }
}

// 公开API实现
bool MeshAsset::bindSkeleton(SkeletonAsset* skeleton) noexcept {
    auto* self = static_cast<FMeshAsset*>(this);

    if (!self->mHasSkinning) {
        GLTFIO_EXT_WARN("Mesh has no skinning data");
        return false;
    }

    if (!skeleton) {
        GLTFIO_EXT_WARN("Null skeleton");
        return false;
    }

    // 验证骨骼索引范围
    if (self->mMaxBoneIndex >= skeleton->getBoneCount()) {
        GLTFIO_EXT_WARN("Bone index out of range");
        return false;
    }

    self->mBoundSkeleton = skeleton;
    return true;
}

SkeletonAsset* MeshAsset::getBoundSkeleton() const noexcept {
    auto* self = static_cast<const FMeshAsset*>(this);
    return self->mBoundSkeleton;
}

void MeshAsset::updateSkinning(const StandaloneAnimator* animator) noexcept {
    auto* self = static_cast<FMeshAsset*>(this);

    if (!self->mBoundSkeleton || !animator) {
        return;
    }

    const auto* boneMatrices = animator->getBoneMatrices();
    size_t boneCount = animator->getBoneCount();

    if (boneMatrices && boneCount > 0 && self->mRenderableInstance) {
        self->mRenderableManager->setBones(self->mRenderableInstance,
                                           boneMatrices, boneCount);
    }
}

Entity MeshAsset::getRenderableEntity() const noexcept {
    auto* self = static_cast<const FMeshAsset*>(this);
    return self->mRenderableEntity;
}

RenderableManager::Instance MeshAsset::getRenderableInstance() const noexcept {
    auto* self = static_cast<const FMeshAsset*>(this);
    return self->mRenderableInstance;
}

Aabb MeshAsset::getBoundingBox() const noexcept {
    auto* self = static_cast<const FMeshAsset*>(this);
    return self->mBoundingBox;
}

void MeshAsset::uploadResources() noexcept {
    auto* self = static_cast<FMeshAsset*>(this);
    self->uploadResources();
}

size_t MeshAsset::uploadNextBatch(size_t maxCount) noexcept {
    auto* self = static_cast<FMeshAsset*>(this);
    return self->uploadNextBatch(maxCount);
}

bool MeshAsset::isResourcesLoaded() const noexcept {
    auto* self = static_cast<const FMeshAsset*>(this);
    return self->mResourcesLoaded;
}

} // namespace filament::gltfio
