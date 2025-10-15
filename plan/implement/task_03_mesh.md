# 任务3: MeshAsset实现

**预计时间**: 2小时
**前置依赖**: 任务1, 任务2完成
**完成标志**: 能加载网格并绑定到骨骼

---

## 目标

实现MeshAsset的完整功能：
1. 从glTF加载网格几何数据
2. 创建VertexBuffer和IndexBuffer
3. 验证BONE_INDICES和BONE_WEIGHTS
4. 绑定骨骼
5. 更新蒙皮矩阵

---

## 1. 完善 FMeshAsset.h

**文件**: `libs/gltfio_ext/src/FMeshAsset.h`

```cpp
#ifndef GLTFIO_EXT_FMESHASSET_H
#define GLTFIO_EXT_FMESHASSET_H

#include <gltfio/MeshAsset.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Box.h>
#include <utils/Entity.h>

struct cgltf_primitive;
struct cgltf_data;

namespace filament {
    class Engine;
}

namespace filament::gltfio {

class SkeletonAsset;

struct FMeshAsset : public MeshAsset {
    filament::Engine* mEngine = nullptr;
    filament::RenderableManager* mRenderableManager = nullptr;

    // 几何数据
    filament::VertexBuffer* mVertexBuffer = nullptr;
    filament::IndexBuffer* mIndexBuffer = nullptr;
    filament::MaterialInstance* mMaterialInstance = nullptr;

    // 渲染实体
    utils::Entity mRenderableEntity;
    filament::RenderableManager::Instance mRenderableInstance;

    // 蒙皮绑定
    SkeletonAsset* mBoundSkeleton = nullptr;

    // 边界盒
    filament::Aabb mBoundingBox;

    // 骨骼索引范围（用于验证）
    uint16_t mMaxBoneIndex = 0;
    bool mHasSkinning = false;

    // 从cgltf_primitive加载
    bool loadFromGltfPrimitive(const cgltf_primitive* prim, const cgltf_data* data,
                               filament::MaterialInstance* defaultMaterial);

    ~FMeshAsset();
};

} // namespace filament::gltfio

#endif // GLTFIO_EXT_FMESHASSET_H
```

---

## 2. 实现 MeshAsset.cpp

**文件**: `libs/gltfio_ext/src/MeshAsset.cpp`

```cpp
#include <gltfio/MeshAsset.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/StandaloneAnimator.h>
#include "FMeshAsset.h"
#include "GltfioExtInternal.h"

#include <filament/Engine.h>
#include <filament/RenderableManager.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <utils/EntityManager.h>
#include <cgltf.h>

using namespace filament;
using namespace utils;

namespace filament::gltfio {

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
    uint16_t maxIndex = 0;
    size_t count = accessor->count;

    // 假设BONE_INDICES是USHORT4或UBYTE4
    if (accessor->type == cgltf_type_vec4) {
        if (accessor->component_type == cgltf_component_type_r_16u) {
            // USHORT4
            uint16_t* data = new uint16_t[count * 4];
            cgltf_accessor_unpack_indices(accessor, data, count * 4);
            for (size_t i = 0; i < count * 4; ++i) {
                if (data[i] > maxIndex) {
                    maxIndex = data[i];
                }
            }
            delete[] data;
        } else if (accessor->component_type == cgltf_component_type_r_8u) {
            // UBYTE4
            uint8_t* data = new uint8_t[count * 4];
            cgltf_accessor_unpack_indices(accessor, data, count * 4);
            for (size_t i = 0; i < count * 4; ++i) {
                if (data[i] > maxIndex) {
                    maxIndex = data[i];
                }
            }
            delete[] data;
        }
    }

    return maxIndex;
}

bool FMeshAsset::loadFromGltfPrimitive(const cgltf_primitive* prim, const cgltf_data* data,
                                       MaterialInstance* defaultMaterial) {
    // 1. 检查必需的属性
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

    if (!positionAccessor) {
        GLTFIO_EXT_WARN("No POSITION attribute found");
        return false;
    }

    const size_t vertexCount = positionAccessor->count;

    // 2. 检查是否有蒙皮数据
    mHasSkinning = (boneIndicesAccessor && boneWeightsAccessor);
    if (mHasSkinning) {
        mMaxBoneIndex = findMaxBoneIndex(boneIndicesAccessor);
    }

    // 3. 创建VertexBuffer
    VertexBuffer::Builder vbb;
    vbb.vertexCount(vertexCount);
    vbb.bufferCount(1);

    // 添加POSITION
    vbb.attribute(VertexAttribute::POSITION, 0,
                  VertexBuffer::AttributeType::FLOAT3, 0, 0);

    size_t stride = 12; // POSITION: 3 floats

    // 添加BONE_INDICES和BONE_WEIGHTS（如果有）
    if (mHasSkinning) {
        vbb.attribute(VertexAttribute::BONE_INDICES, 0,
                      VertexBuffer::AttributeType::USHORT4, stride, 0);
        stride += 8; // USHORT4: 8 bytes

        vbb.attribute(VertexAttribute::BONE_WEIGHTS, 0,
                      VertexBuffer::AttributeType::FLOAT4, stride, 0);
        stride += 16; // FLOAT4: 16 bytes
    }

    mVertexBuffer = vbb.build(*mEngine);

    // 4. 填充顶点数据
    size_t bufferSize = vertexCount * stride;
    uint8_t* vertexData = new uint8_t[bufferSize];
    memset(vertexData, 0, bufferSize);

    // 解包POSITION
    float* positions = reinterpret_cast<float*>(vertexData);
    cgltf_accessor_unpack_floats(positionAccessor, positions, vertexCount * 3);

    // 解包BONE_INDICES和BONE_WEIGHTS
    if (mHasSkinning) {
        uint16_t* indices = reinterpret_cast<uint16_t*>(vertexData + 12);
        float* weights = reinterpret_cast<float*>(vertexData + 12 + 8);

        for (size_t i = 0; i < vertexCount; ++i) {
            // BONE_INDICES
            cgltf_size indexCount = 4;
            uint16_t tempIndices[4];
            cgltf_accessor_unpack_indices(boneIndicesAccessor, tempIndices, indexCount);
            memcpy(indices + i * 4, tempIndices, 8);

            // BONE_WEIGHTS
            float tempWeights[4];
            cgltf_accessor_unpack_floats(boneWeightsAccessor, tempWeights, 4);
            memcpy(weights + i * 4, tempWeights, 16);
        }
    }

    // 上传到GPU
    VertexBuffer::BufferDescriptor desc(vertexData, bufferSize,
        [](void* buffer, size_t size, void* user) { delete[] static_cast<uint8_t*>(buffer); });
    mVertexBuffer->setBufferAt(*mEngine, 0, std::move(desc));

    // 5. 创建IndexBuffer
    if (prim->indices) {
        const cgltf_accessor* indexAccessor = prim->indices;
        const size_t indexCount = indexAccessor->count;

        IndexBuffer::IndexType indexType = IndexBuffer::IndexType::USHORT;
        if (indexAccessor->component_type == cgltf_component_type_r_32u) {
            indexType = IndexBuffer::IndexType::UINT;
        }

        mIndexBuffer = IndexBuffer::Builder()
            .indexCount(indexCount)
            .bufferType(indexType)
            .build(*mEngine);

        // 解包索引数据
        size_t indexSize = (indexType == IndexBuffer::IndexType::UINT) ? 4 : 2;
        void* indexData = malloc(indexCount * indexSize);
        cgltf_accessor_unpack_indices(indexAccessor, indexData, indexCount);

        IndexBuffer::BufferDescriptor indexDesc(indexData, indexCount * indexSize);
        mIndexBuffer->setBuffer(*mEngine, std::move(indexDesc));
    }

    // 6. 创建Renderable
    mRenderableEntity = EntityManager::get().create();
    mMaterialInstance = defaultMaterial;

    size_t maxBoneCount = mHasSkinning ? 256 : 0;

    RenderableManager::Builder builder(1);
    builder.boundingBox(computeAABB(prim))
           .material(0, mMaterialInstance)
           .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,
                     mVertexBuffer, mIndexBuffer)
           .culling(true)
           .receiveShadows(true)
           .castShadows(true);

    if (mHasSkinning) {
        builder.skinning(maxBoneCount)
               .enableSkinningBuffers(false);
    }

    builder.build(*mEngine, mRenderableEntity);

    mRenderableInstance = mRenderableManager->getInstance(mRenderableEntity);
    mBoundingBox = computeAABB(prim);

    return true;
}

FMeshAsset::~FMeshAsset() {
    if (mEngine) {
        if (mVertexBuffer) mEngine->destroy(mVertexBuffer);
        if (mIndexBuffer) mEngine->destroy(mIndexBuffer);
        // Note: mMaterialInstance 由MaterialProvider管理，不在这里销毁
        if (mRenderableEntity) mEngine->destroy(mRenderableEntity);
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

    if (boneMatrices && boneCount > 0) {
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

} // namespace filament::gltfio
```

---

## 3. 实现 AssetLoaderExt::loadMesh()

**文件**: `libs/gltfio_ext/src/AssetLoaderExt.cpp` (更新)

在AssetLoaderExt.cpp中添加：

```cpp
#include "FMeshAsset.h"

MeshAsset* AssetLoaderExt::loadMesh(const uint8_t* bytes, uint32_t nbytes) {
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

    // 3. 检查是否有mesh
    if (data->meshes_count == 0) {
        GLTFIO_EXT_WARN("No meshes found in glTF file");
        cgltf_free(data);
        return nullptr;
    }

    // 4. 加载第一个mesh的第一个primitive
    const cgltf_mesh* mesh = &data->meshes[0];
    if (mesh->primitives_count == 0) {
        GLTFIO_EXT_WARN("No primitives in mesh");
        cgltf_free(data);
        return nullptr;
    }

    const cgltf_primitive* prim = &mesh->primitives[0];

    // 5. 创建MeshAsset
    FMeshAsset* meshAsset = new FMeshAsset();
    meshAsset->mEngine = self->mEngine;
    meshAsset->mRenderableManager = &self->mEngine->getRenderableManager();

    // 6. 获取默认材质
    MaterialInstance* defaultMaterial = nullptr;
    if (self->mMaterials) {
        const Material* const* materials = self->mMaterials->getMaterials();
        if (materials && self->mMaterials->getMaterialsCount() > 0) {
            defaultMaterial = materials[0]->getDefaultInstance();
        }
    }

    if (!defaultMaterial) {
        GLTFIO_EXT_WARN("No default material available");
        delete meshAsset;
        cgltf_free(data);
        return nullptr;
    }

    // 7. 加载primitive
    bool success = meshAsset->loadFromGltfPrimitive(prim, data, defaultMaterial);

    // 8. 释放cgltf数据
    cgltf_free(data);

    if (!success) {
        delete meshAsset;
        return nullptr;
    }

    return meshAsset;
}

void AssetLoaderExt::destroyMesh(MeshAsset* mesh) {
    delete static_cast<FMeshAsset*>(mesh);
}
```

---

## 4. 创建测试程序

**文件**: `libs/gltfio_ext/tests/test_mesh.cpp`

```cpp
#include <gltfio/AssetLoaderExt.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/MeshAsset.h>
#include <gltfio/MaterialProvider.h>

#include <filament/Engine.h>
#include <filament/Scene.h>
#include <iostream>
#include <fstream>
#include <vector>

using namespace filament;
using namespace filament::gltfio;

static std::vector<uint8_t> readFile(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }
    size_t size = file.tellg();
    std::vector<uint8_t> buffer(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}

int main() {
    Engine* engine = Engine::create();
    Scene* scene = engine->createScene();

    MaterialProvider* materials = createUbershaderProvider(engine);
    AssetConfigurationExt config = {
        .engine = engine,
        .materials = materials
    };
    AssetLoaderExt* loader = AssetLoaderExt::create(config);

    // 1. 加载骨骼
    std::vector<uint8_t> data = readFile("third_party/models/AnimatedMorphCube/AnimatedMorphCube.glb");
    SkeletonAsset* skeleton = loader->loadSkeleton(data.data(), data.size());
    if (!skeleton) {
        std::cerr << "Failed to load skeleton" << std::endl;
        return 1;
    }

    // 2. 加载网格
    MeshAsset* mesh = loader->loadMesh(data.data(), data.size());
    if (!mesh) {
        std::cerr << "Failed to load mesh" << std::endl;
        return 1;
    }

    std::cout << "=== Mesh Info ===" << std::endl;
    std::cout << "Renderable entity: " << mesh->getRenderableEntity().getId() << std::endl;

    // 3. 绑定骨骼
    if (mesh->bindSkeleton(skeleton)) {
        std::cout << "Skeleton bound successfully" << std::endl;
        std::cout << "Bound skeleton has " << mesh->getBoundSkeleton()->getBoneCount() << " bones" << std::endl;
    } else {
        std::cout << "Skeleton binding failed (mesh may not have skinning)" << std::endl;
    }

    // 4. 测试添加到场景
    scene->addEntity(mesh->getRenderableEntity());
    std::cout << "Mesh added to scene" << std::endl;

    // 5. 清理
    scene->remove(mesh->getRenderableEntity());
    loader->destroyMesh(mesh);
    loader->destroySkeleton(skeleton);
    AssetLoaderExt::destroy(&loader);
    engine->destroy(scene);
    materials->destroyMaterials();
    delete materials;
    Engine::destroy(&engine);

    std::cout << "\n=== Test PASSED ===" << std::endl;
    return 0;
}
```

---

## 5. 更新CMakeLists.txt

**文件**: `libs/gltfio_ext/CMakeLists.txt` (追加)

```cmake
if (NOT WEBGL AND NOT ANDROID AND NOT IOS)
    add_executable(test_mesh tests/test_mesh.cpp)
    target_link_libraries(test_mesh PRIVATE gltfio_ext)
    set_target_properties(test_mesh PROPERTIES FOLDER Tests)
endif()
```

---

## 6. 验证步骤

```bash
# 1. 编译
cd out
ninja gltfio_ext test_mesh

# 2. 运行测试
./libs/test_mesh

# 预期输出:
# === Mesh Info ===
# Renderable entity: <id>
# Skeleton bound successfully
# Bound skeleton has N bones
# Mesh added to scene
# === Test PASSED ===
```

---

## 完成标志

- [ ] FMeshAsset::loadFromGltfPrimitive() 实现完成
- [ ] AssetLoaderExt::loadMesh() 实现完成
- [ ] bindSkeleton() 实现并验证骨骼索引
- [ ] updateSkinning() 实现完成
- [ ] 测试程序运行成功

完成后进入 **任务4: AnimationAsset + AnimationPack实现**
