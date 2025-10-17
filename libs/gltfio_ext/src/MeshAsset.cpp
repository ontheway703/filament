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
#include <gltfio/MeshInstance.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/StandaloneAnimator.h>
#include <gltfio/MaterialProvider.h>
#include "FMeshAsset.h"
#include "FMeshInstance.h"
#include "GltfioExtInternal.h"

#include <filament/Engine.h>
#include <filament/RenderableManager.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <utils/EntityManager.h>
#include <math/vec3.h>
#include <math/quat.h>
#include <math/mat3.h>
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
    size_t vertexCount = accessor->count;

    // Read bone indices using cgltf_accessor_read_uint (returns uint32)
    for (size_t v = 0; v < vertexCount; ++v) {
        uint32_t indices[4];
        cgltf_accessor_read_uint(accessor, v, indices, 4);

        for (int j = 0; j < 4; ++j) {
            if (indices[j] < 65535 && indices[j] > maxIndex) {  // Skip invalid indices
                maxIndex = static_cast<uint16_t>(indices[j]);
            }
        }
    }

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
 * 从 TBN 向量构建 quaternion（参考 gltfio/AssetLoader.cpp）
 *
 * @param tangent 切线向量（已归一化）
 * @param bitangent 副切线向量（已归一化）
 * @param normal 法线向量（已归一化）
 * @return quaternion 表示的 TBN 矩阵
 */
static quatf buildTbnQuaternion(const float3& tangent, const float3& bitangent, const float3& normal) {
    // 1. 构建 TBN 矩阵（列向量）
    mat3f tbn;
    tbn[0] = tangent;      // 第 1 列
    tbn[1] = bitangent;    // 第 2 列
    tbn[2] = normal;       // 第 3 列

    // 2. 转换为 quaternion
    quatf q = mat3f::packTangentFrame(tbn);

    return q;
}

/**
 * 从 Normal 生成简化的 Tangent（当 glTF 没有提供 TANGENT 属性时）
 *
 * 注意：这是简化实现，假设 tangent 垂直于 normal
 * 完整实现应使用 MikkTSpace 算法
 */
static float3 computeSimpleTangent(const float3& normal) {
    // 找一个与 normal 不平行的向量
    float3 c1 = cross(normal, float3{0.0f, 0.0f, 1.0f});
    float3 c2 = cross(normal, float3{0.0f, 1.0f, 0.0f});

    // 选择长度更大的（更稳定）
    float3 tangent = length2(c1) > length2(c2) ? c1 : c2;
    return normalize(tangent);
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

            // 查找所有顶点属性
            const cgltf_accessor* positionAccessor = nullptr;
            const cgltf_accessor* normalAccessor = nullptr;
            const cgltf_accessor* tangentAccessor = nullptr;  // 新增：TANGENT 属性
            const cgltf_accessor* texcoord0Accessor = nullptr;
            const cgltf_accessor* texcoord1Accessor = nullptr;
            const cgltf_accessor* colorAccessor = nullptr;
            const cgltf_accessor* boneIndicesAccessor = nullptr;
            const cgltf_accessor* boneWeightsAccessor = nullptr;

            for (size_t i = 0; i < prim->attributes_count; ++i) {
                const cgltf_attribute& attr = prim->attributes[i];
                switch (attr.type) {
                    case cgltf_attribute_type_position:
                        positionAccessor = attr.data;
                        break;
                    case cgltf_attribute_type_normal:
                        normalAccessor = attr.data;
                        break;
                    case cgltf_attribute_type_tangent:  // 新增：读取 TANGENT
                        tangentAccessor = attr.data;
                        break;
                    case cgltf_attribute_type_texcoord:
                        if (attr.index == 0) texcoord0Accessor = attr.data;
                        else if (attr.index == 1) texcoord1Accessor = attr.data;
                        break;
                    case cgltf_attribute_type_color:
                        colorAccessor = attr.data;
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

            // 创建VertexBuffer（添加所有可用属性，包括骨骼）
            const size_t vertexCount = positionAccessor->count;

            // 计算每个顶点的stride（所有属性的总大小）
            // ALWAYS allocate space for all attributes (even if not present in glTF)
            // Materials may require them, so we provide dummy data (zeros) if missing
            size_t stride = 0;
            size_t positionOffset = stride; stride += sizeof(float) * 3;  // POSITION: vec3
            size_t normalOffset = stride; stride += sizeof(float) * 4;  // TANGENTS: vec4 (always include)
            size_t uv0Offset = stride; stride += sizeof(float) * 2;  // UV0: vec2 (always include)
            size_t uv1Offset = stride; stride += sizeof(float) * 2;  // UV1: vec2 (always include)
            size_t colorOffset = stride; stride += sizeof(float) * 4;  // COLOR: vec4 (always include)
            // CRITICAL: Add bone attributes for skinned mesh rendering
            size_t boneIndicesOffset = stride; stride += sizeof(uint16_t) * 4;  // BONE_INDICES: uvec4 (4 bone indices per vertex)
            size_t boneWeightsOffset = stride; stride += sizeof(float) * 4;     // BONE_WEIGHTS: vec4 (4 weights per vertex)

            VertexBuffer::Builder vbb;
            vbb.vertexCount(vertexCount)
               .bufferCount(1)
               .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3, positionOffset, stride)
               // ALWAYS include TANGENTS, UV0, UV1, COLOR (materials may require them)
               .attribute(VertexAttribute::TANGENTS, 0, VertexBuffer::AttributeType::FLOAT4, normalOffset, stride)
               .attribute(VertexAttribute::UV0, 0, VertexBuffer::AttributeType::FLOAT2, uv0Offset, stride)
               .attribute(VertexAttribute::UV1, 0, VertexBuffer::AttributeType::FLOAT2, uv1Offset, stride)
               .attribute(VertexAttribute::COLOR, 0, VertexBuffer::AttributeType::FLOAT4, colorOffset, stride)
               // Add bone attributes (REQUIRED for skinned mesh)
               .attribute(VertexAttribute::BONE_INDICES, 0, VertexBuffer::AttributeType::USHORT4, boneIndicesOffset, stride)
               .attribute(VertexAttribute::BONE_WEIGHTS, 0, VertexBuffer::AttributeType::FLOAT4, boneWeightsOffset, stride);

            VertexBuffer* vertexBuffer = vbb.build(*mEngine);

            // 填充所有顶点数据到交错格式的CPU内存
            uint8_t* vertexData = new uint8_t[vertexCount * stride];
            memset(vertexData, 0, vertexCount * stride);  // Initialize to zero

            // POSITION
            for (size_t v = 0; v < vertexCount; ++v) {
                float* dst = (float*)(vertexData + v * stride + positionOffset);
                cgltf_accessor_read_float(positionAccessor, v, dst, 3);
            }

            // TANGENTS (TBN quaternion for normal mapping)
            if (normalAccessor) {
                for (size_t v = 0; v < vertexCount; ++v) {
                    // 1. Read normal
                    float normalData[3];
                    cgltf_accessor_read_float(normalAccessor, v, normalData, 3);
                    float3 normal = normalize(float3(normalData[0], normalData[1], normalData[2]));

                    // 2. Get tangent (from glTF or compute)
                    float3 tangent;
                    float handedness = 1.0f;  // Default handedness

                    if (tangentAccessor) {
                        // glTF provides TANGENT attribute (vec4: xyz=tangent, w=handedness)
                        float tangentData[4];
                        cgltf_accessor_read_float(tangentAccessor, v, tangentData, 4);
                        tangent = normalize(float3(tangentData[0], tangentData[1], tangentData[2]));
                        handedness = tangentData[3];
                    } else {
                        // No TANGENT in glTF, compute from normal
                        tangent = computeSimpleTangent(normal);
                        handedness = 1.0f;
                    }

                    // 3. Compute bitangent
                    float3 bitangent = normalize(cross(normal, tangent) * handedness);

                    // 4. Build TBN quaternion
                    quatf q = buildTbnQuaternion(tangent, bitangent, normal);

                    // 5. Store quaternion in TANGENTS attribute
                    float* dst = (float*)(vertexData + v * stride + normalOffset);
                    dst[0] = q.x;
                    dst[1] = q.y;
                    dst[2] = q.z;
                    dst[3] = q.w;
                }
            }

            // UV0
            if (texcoord0Accessor) {
                for (size_t v = 0; v < vertexCount; ++v) {
                    float* dst = (float*)(vertexData + v * stride + uv0Offset);
                    cgltf_accessor_read_float(texcoord0Accessor, v, dst, 2);
                }
            }

            // UV1
            if (texcoord1Accessor) {
                for (size_t v = 0; v < vertexCount; ++v) {
                    float* dst = (float*)(vertexData + v * stride + uv1Offset);
                    cgltf_accessor_read_float(texcoord1Accessor, v, dst, 2);
                }
            }

            // COLOR
            if (colorAccessor) {
                for (size_t v = 0; v < vertexCount; ++v) {
                    float* dst = (float*)(vertexData + v * stride + colorOffset);
                    cgltf_accessor_read_float(colorAccessor, v, dst, 4);
                }
            }

            // BONE_INDICES (CRITICAL for skinning)
            for (size_t v = 0; v < vertexCount; ++v) {
                uint16_t* dst = (uint16_t*)(vertexData + v * stride + boneIndicesOffset);
                // Read as uint32 first, then convert to uint16
                uint32_t temp[4];
                cgltf_accessor_read_uint(boneIndicesAccessor, v, temp, 4);
                dst[0] = static_cast<uint16_t>(temp[0]);
                dst[1] = static_cast<uint16_t>(temp[1]);
                dst[2] = static_cast<uint16_t>(temp[2]);
                dst[3] = static_cast<uint16_t>(temp[3]);
            }

            // BONE_WEIGHTS (CRITICAL for skinning)
            for (size_t v = 0; v < vertexCount; ++v) {
                float* dst = (float*)(vertexData + v * stride + boneWeightsOffset);
                cgltf_accessor_read_float(boneWeightsAccessor, v, dst, 4);
            }

            // 保存待上传数据，不立即上传
            PendingBufferData pending = {};
            pending.data = vertexData;
            pending.size = vertexCount * stride;
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
                        delete[] static_cast<uint8_t*>(pending.data);
                        mEngine->destroy(pending.vertexBuffer);
                    } else if (pending.indexBuffer) {
                        free(pending.data);
                        mEngine->destroy(pending.indexBuffer);
                    }
                    mPendingUploads.pop_back();
                }
                continue;
            }

            // Set material parameters from glTF (fixes black model issue)
            const cgltf_material* mat = prim->material ? prim->material : &kDefaultMat;
            auto mrConfig = mat->pbr_metallic_roughness;

            // baseColorFactor (RGBA)
            const float* c = mrConfig.base_color_factor;
            materialInstance->setParameter("baseColorFactor", float4(c[0], c[1], c[2], c[3]));

            // metallicFactor and roughnessFactor
            materialInstance->setParameter("metallicFactor", mrConfig.metallic_factor);
            materialInstance->setParameter("roughnessFactor", mrConfig.roughness_factor);

            // emissiveFactor (RGB with optional strength multiplier)
            const float* emissive = &mat->emissive_factor[0];
            float3 emissiveFactor(emissive[0], emissive[1], emissive[2]);
            if (mat->has_emissive_strength) {
                emissiveFactor *= mat->emissive_strength.emissive_strength;
            }
            materialInstance->setParameter("emissiveFactor", emissiveFactor);

            // alphaMask threshold (if MASK mode)
            if (mat->alpha_mode == cgltf_alpha_mode_mask) {
                materialInstance->setMaskThreshold(mat->alpha_cutoff);
            }

            // 收集纹理信息（用于getRequiredTextures）
            PrimitiveTextureInfo primTexInfo;
            primTexInfo.primitiveIndex = primitives.size();

            // Base Color Texture
            if (mrConfig.base_color_texture.texture) {
                primTexInfo.slots.push_back({
                    "baseColorMap",
                    mrConfig.base_color_texture.texture,
                    (int)mrConfig.base_color_texture.texcoord
                });
            }

            // Normal Texture
            if (mat->normal_texture.texture) {
                primTexInfo.slots.push_back({
                    "normalMap",
                    mat->normal_texture.texture,
                    (int)mat->normal_texture.texcoord
                });
            }

            // Metallic Roughness Texture
            if (mrConfig.metallic_roughness_texture.texture) {
                primTexInfo.slots.push_back({
                    "metallicRoughnessMap",
                    mrConfig.metallic_roughness_texture.texture,
                    (int)mrConfig.metallic_roughness_texture.texcoord
                });
            }

            // Occlusion Texture
            if (mat->occlusion_texture.texture) {
                primTexInfo.slots.push_back({
                    "occlusionMap",
                    mat->occlusion_texture.texture,
                    (int)mat->occlusion_texture.texcoord
                });
            }

            // Emissive Texture
            if (mat->emissive_texture.texture) {
                primTexInfo.slots.push_back({
                    "emissiveMap",
                    mat->emissive_texture.texture,
                    (int)mat->emissive_texture.texcoord
                });
            }

            if (!primTexInfo.slots.empty()) {
                mTextureInfos.push_back(primTexInfo);
            }

            // 计算AABB和maxBoneIndex
            Aabb aabb = computeAABB(prim);
            uint16_t maxBoneIndex = findMaxBoneIndex(boneIndicesAccessor);

            // Debug: log bone index info for each primitive
            if (primitives.size() < 5 || primitives.size() % 50 == 0) {
                GLTFIO_EXT_LOG("  Primitive " << primitives.size() << ": "
                             << "vertexCount=" << vertexCount << ", "
                             << "maxBoneIndex=" << maxBoneIndex << ", "
                             << "materialName=" << (prim->material && prim->material->name ? prim->material->name : "default"));
            }

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
    GLTFIO_EXT_LOG("Max bone index found: " << mMaxBoneIndex << " (will need skeleton with at least " << (mMaxBoneIndex + 1) << " bones)");

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
                [](void* buffer, size_t, void*) { delete[] static_cast<uint8_t*>(buffer); }
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
                [](void* buffer, size_t, void*) { delete[] static_cast<uint8_t*>(buffer); }
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
    // 1. 首先销毁所有实例（在销毁共享资源之前）
    GLTFIO_EXT_LOG("Destroying MeshAsset with " << mInstances.size() << " instances");
    for (auto* instance : mInstances) {
        delete static_cast<FMeshInstance*>(instance);
    }
    mInstances.clear();

    // 2. 释放未上传的pending数据
    for (auto& pending : mPendingUploads) {
        if (pending.data) {
            if (pending.vertexBuffer) {
                delete[] static_cast<uint8_t*>(pending.data);
            } else if (pending.indexBuffer) {
                free(pending.data);
            }
        }
    }
    mPendingUploads.clear();

    if (mEngine) {
        // CRITICAL: Destroy Renderable component FIRST (before MaterialInstances)
        // Otherwise MaterialInstances will still be referenced by the Renderable
        if (mRenderableEntity) {
            mEngine->destroy(mRenderableEntity);  // Destroys all components
            EntityManager::get().destroy(mRenderableEntity);  // Destroys entity
        }

        // Now safe to destroy MaterialInstances (Renderable no longer references them)
        for (auto* mi : mMaterialInstances) {
            if (mi) mEngine->destroy(mi);
        }

        // Destroy VertexBuffers and IndexBuffers (shared resources)
        for (auto* vb : mVertexBuffers) {
            if (vb) mEngine->destroy(vb);
        }
        for (auto* ib : mIndexBuffers) {
            if (ib) mEngine->destroy(ib);
        }
    }

    // 释放 cgltf_data（包含纹理信息）
    if (mGltfData) {
        cgltf_free(const_cast<cgltf_data*>(mGltfData));
        mGltfData = nullptr;
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
        GLTFIO_EXT_WARN("Bone index out of range: maxBoneIndex=" << self->mMaxBoneIndex
                       << ", skeletonBoneCount=" << skeleton->getBoneCount());
        return false;
    }

    GLTFIO_EXT_LOG("Skeleton bound successfully: " << skeleton->getBoneCount() << " bones, maxBoneIndex=" << self->mMaxBoneIndex);

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

        // Debug: log first update
        static bool logged = false;
        if (!logged) {
            GLTFIO_EXT_LOG("updateSkinning: Set " << boneCount << " bone matrices for "
                         << self->mMaterialInstances.size() << " primitives");
            logged = true;
        }
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

std::vector<TextureInfo> MeshAsset::getRequiredTextures() const noexcept {
    auto* self = static_cast<const FMeshAsset*>(this);
    std::vector<TextureInfo> result;

    if (!self->mGltfData) {
        return result;
    }

    // 遍历所有primitives的纹理信息
    for (const auto& primTexInfo : self->mTextureInfos) {
        for (const auto& slot : primTexInfo.slots) {
            TextureInfo info = {};
            info.primitiveIndex = primTexInfo.primitiveIndex;
            info.slot = slot.slotName;

            cgltf_texture* tex = slot.gltfTexture;
            if (!tex || !tex->image) {
                continue;  // 跳过无效纹理
            }

            cgltf_image* img = tex->image;

            // 1. 提取 URI 或 embedded 数据
            if (img->uri) {
                info.uri = img->uri;
                info.data = nullptr;
                info.dataSize = 0;
            } else if (img->buffer_view) {
                // Embedded 数据
                info.uri = nullptr;
                info.data = (const uint8_t*)img->buffer_view->buffer->data
                          + img->buffer_view->offset;
                info.dataSize = img->buffer_view->size;
            } else {
                continue;  // 无数据源，跳过
            }

            // 2. MIME 类型
            info.mimeType = img->mime_type ? img->mime_type : "image/png";

            // 3. Sampler 参数
            cgltf_sampler* sampler = tex->sampler;
            if (sampler) {
                info.wrapS = static_cast<SamplerWrapMode>(sampler->wrap_s);
                info.wrapT = static_cast<SamplerWrapMode>(sampler->wrap_t);
                info.minFilter = static_cast<SamplerMinFilter>(sampler->min_filter);
                info.magFilter = static_cast<SamplerMagFilter>(sampler->mag_filter);
            } else {
                // 默认值（glTF规范默认）
                info.wrapS = SamplerWrapMode::REPEAT;
                info.wrapT = SamplerWrapMode::REPEAT;
                info.minFilter = SamplerMinFilter::LINEAR;
                info.magFilter = SamplerMagFilter::LINEAR;
            }

            result.push_back(info);
        }
    }

    return result;
}

void MeshAsset::bindTexture(size_t primitiveIndex, const char* slot,
                            Texture* texture, const TextureSampler& sampler) noexcept {
    auto* self = static_cast<FMeshAsset*>(this);

    if (primitiveIndex >= self->mMaterialInstances.size()) {
        GLTFIO_EXT_WARN("Invalid primitive index: " << primitiveIndex
                       << " (total: " << self->mMaterialInstances.size() << ")");
        return;
    }

    if (!texture) {
        GLTFIO_EXT_WARN("Cannot bind null texture to slot: " << (slot ? slot : "null"));
        return;
    }

    MaterialInstance* mi = self->mMaterialInstances[primitiveIndex];
    if (!mi) {
        GLTFIO_EXT_WARN("MaterialInstance is null for primitive " << primitiveIndex);
        return;
    }

    // 绑定纹理到MaterialInstance
    mi->setParameter(slot, texture, sampler);

    // Debug log for first few bindings
    static int bindCount = 0;
    if (bindCount < 5) {
        GLTFIO_EXT_LOG("Bound texture to primitive " << primitiveIndex
                     << ", slot: " << slot);
        bindCount++;
    }
}

TextureSampler MeshAsset::createSampler(const TextureInfo& info) noexcept {
    TextureSampler sampler;

    // Wrap modes
    auto convertWrapMode = [](SamplerWrapMode mode) -> TextureSampler::WrapMode {
        switch (mode) {
            case SamplerWrapMode::CLAMP_TO_EDGE:
                return TextureSampler::WrapMode::CLAMP_TO_EDGE;
            case SamplerWrapMode::MIRRORED_REPEAT:
                return TextureSampler::WrapMode::MIRRORED_REPEAT;
            case SamplerWrapMode::REPEAT:
            default:
                return TextureSampler::WrapMode::REPEAT;
        }
    };

    sampler.setWrapModeS(convertWrapMode(info.wrapS));
    sampler.setWrapModeT(convertWrapMode(info.wrapT));

    // Min filter
    auto convertMinFilter = [](SamplerMinFilter filter) -> TextureSampler::MinFilter {
        switch (filter) {
            case SamplerMinFilter::NEAREST:
                return TextureSampler::MinFilter::NEAREST;
            case SamplerMinFilter::LINEAR:
                return TextureSampler::MinFilter::LINEAR;
            case SamplerMinFilter::NEAREST_MIPMAP_NEAREST:
                return TextureSampler::MinFilter::NEAREST_MIPMAP_NEAREST;
            case SamplerMinFilter::LINEAR_MIPMAP_NEAREST:
                return TextureSampler::MinFilter::LINEAR_MIPMAP_NEAREST;
            case SamplerMinFilter::NEAREST_MIPMAP_LINEAR:
                return TextureSampler::MinFilter::NEAREST_MIPMAP_LINEAR;
            case SamplerMinFilter::LINEAR_MIPMAP_LINEAR:
            default:
                return TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR;
        }
    };

    sampler.setMinFilter(convertMinFilter(info.minFilter));

    // Mag filter
    auto convertMagFilter = [](SamplerMagFilter filter) -> TextureSampler::MagFilter {
        switch (filter) {
            case SamplerMagFilter::NEAREST:
                return TextureSampler::MagFilter::NEAREST;
            case SamplerMagFilter::LINEAR:
            default:
                return TextureSampler::MagFilter::LINEAR;
        }
    };

    sampler.setMagFilter(convertMagFilter(info.magFilter));

    return sampler;
}

//=== 实例化API实现 ===

MeshInstance* MeshAsset::createInstance() noexcept {
    auto* self = static_cast<FMeshAsset*>(this);

    // 创建新实例（共享VertexBuffer/IndexBuffer）
    FMeshInstance* instance = new FMeshInstance(this, self->mEngine);

    // 添加到实例列表
    self->mInstances.push_back(instance);

    GLTFIO_EXT_LOG("Created instance " << self->mInstances.size()
                 << " of MeshAsset (shared GPU resources)");

    return instance;
}

void MeshAsset::destroyInstance(MeshInstance* instance) noexcept {
    auto* self = static_cast<FMeshAsset*>(this);

    if (!instance) {
        GLTFIO_EXT_WARN("Cannot destroy null instance");
        return;
    }

    // 从列表中移除
    auto it = std::find(self->mInstances.begin(), self->mInstances.end(), instance);
    if (it != self->mInstances.end()) {
        self->mInstances.erase(it);
        delete static_cast<FMeshInstance*>(instance);

        GLTFIO_EXT_LOG("Destroyed instance, remaining: " << self->mInstances.size());
    } else {
        GLTFIO_EXT_WARN("Instance not found in MeshAsset");
    }
}

MeshInstance* const* MeshAsset::getInstances() const noexcept {
    auto* self = static_cast<const FMeshAsset*>(this);
    return self->mInstances.data();
}

size_t MeshAsset::getInstanceCount() const noexcept {
    auto* self = static_cast<const FMeshAsset*>(this);
    return self->mInstances.size();
}

} // namespace filament::gltfio
