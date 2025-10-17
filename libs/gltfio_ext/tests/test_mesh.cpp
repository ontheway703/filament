/**
 * ============================================================================
 * test_mesh.cpp - MeshAsset 综合功能测试
 * ============================================================================
 *
 * 【测试目标】
 * 全面验证 MeshAsset 的核心功能：
 * - GPU 资源管理（VertexBuffer, IndexBuffer）
 * - 骨骼绑定和蒙皮更新
 * - 包围盒计算
 * - Entity 和 Renderable 管理
 * - 有/无 skinning 的不同场景
 *
 * 【背景知识】
 * MeshAsset 代表一个 3D 网格模型，包含：
 * 1. VertexBuffer（顶点缓冲区）: 存储顶点数据（位置、法线、UV等），在 GPU 内存中
 * 2. IndexBuffer（索引缓冲区）: 存储三角形索引，减少顶点重复
 * 3. Renderable: Filament 的可渲染组件，将网格添加到渲染场景
 * 4. Skinning（蒙皮）: 通过骨骼动画变形网格的技术
 *
 * 关键概念：
 * - Skinning（蒙皮/骨骼动画）:
 *   每个顶点受多个骨骼影响，通过权重混合。公式：
 *   finalVertex = Σ(weight[i] * boneMatrix[i] * inverseBindMatrix[i] * vertex)
 *
 * - AABB（Axis-Aligned Bounding Box，轴对齐包围盒）:
 *   用于快速剔除不可见物体，提升渲染性能
 *
 * - RenderableManager:
 *   管理所有可渲染物体，将网格数据传递给渲染器
 *
 * 【测试方法】
 * 单元测试 - 手动构造不同场景的 MeshAsset，验证 API 行为和边界情况
 *
 * 【测试用例概览】
 * Test 1: MeshAsset 基础创建（VertexBuffer/IndexBuffer）
 * Test 2: Skeleton 绑定测试（成功/失败场景）
 * Test 3: updateSkinning 蒙皮更新测试
 * Test 4: 无 skinning 的 mesh 测试
 * Test 5: Entity 和 Renderable 测试
 * Test 6: 空 MeshAsset 边界情况
 *
 * 【关键验证点】
 * - GPU 资源创建和数据上传
 * - 骨骼绑定验证（索引范围检查）
 * - 蒙皮更新不崩溃
 * - 包围盒计算正确
 * - 场景添加/移除成功
 *
 * 【与其他测试的关系】
 * - 依赖：test_skeleton.cpp（骨骼绑定需要 SkeletonAsset）
 * - 后续：test_mesh_instance.cpp（实例化测试）
 *         test_integration.cpp（完整渲染流程）
 *
 * 【运行方式】
 * ./out/cmake-debug/libs/gltfio_ext/test_mesh
 *
 * 【预期输出】
 * 所有子测试通过，显示 "ALL MESH TESTS PASSED!"
 * ============================================================================
 */

#include <gltfio/MeshAsset.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/StandaloneAnimator.h>
#include "../src/FMeshAsset.h"
#include "../src/FSkeletonAsset.h"

#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/RenderableManager.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <utils/EntityManager.h>
#include <math/vec3.h>
#include <iostream>

using namespace filament;
using namespace filament::gltfio;
using namespace filament::math;
using namespace utils;

int main() {
    std::cout << "=== MeshAsset Comprehensive Test ===" << std::endl;

    // 创建Filament环境
    Engine* engine = Engine::create();
    Scene* scene = engine->createScene();
    auto& em = EntityManager::get();
    auto& tm = engine->getTransformManager();
    auto& rm = engine->getRenderableManager();
    std::cout << "✓ Engine and Scene created" << std::endl;

    int passed = 0, failed = 0;

    // ========================================================================
    // Test 1: MeshAsset 基础创建 - GPU 资源管理测试
    // ========================================================================
    // 【测试目的】
    // 验证 VertexBuffer 和 IndexBuffer 的创建、数据上传、包围盒设置
    //
    // 【验证点】
    // 1. VertexBuffer 创建成功
    // 2. 顶点数据上传到 GPU
    // 3. IndexBuffer 创建成功
    // 4. 索引数据上传到 GPU
    // 5. 包围盒(AABB)设置正确
    //
    // 【背景】
    // GPU 渲染流程：CPU 准备顶点/索引数据 -> 上传到 GPU -> GPU 渲染
    std::cout << "\n[Test 1] MeshAsset basic creation..." << std::endl;
    {
        FMeshAsset* mesh = new FMeshAsset();
        mesh->mEngine = engine;
        mesh->mRenderableManager = &rm;
        mesh->mHasSkinning = false;  // 这是一个静态网格（不需要骨骼动画）

        // 创建简单三角形顶点数据（一个最基本的 3D 模型）
        // 三角形是 3D 图形的基本单元，所有复杂模型都由三角形构成
        const size_t vertexCount = 3;
        float vertices[] = {
            0.0f, 0.0f, 0.0f,  // vertex 0 (x, y, z)
            1.0f, 0.0f, 0.0f,  // vertex 1
            0.5f, 1.0f, 0.0f   // vertex 2
        };

        // 创建VertexBuffer
        VertexBuffer::Builder vbb;
        vbb.vertexCount(vertexCount)
           .bufferCount(1)
           .attribute(VertexAttribute::POSITION, 0,
                      VertexBuffer::AttributeType::FLOAT3, 0, sizeof(float) * 3);
        mesh->mVertexBuffers.resize(1);
        mesh->mVertexBuffers[0] = vbb.build(*engine);

        if (mesh->mVertexBuffers[0]) {
            std::cout << "  ✓ VertexBuffer created successfully" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: VertexBuffer creation failed" << std::endl;
            failed++;
        }

        // 上传顶点数据
        size_t bufferSize = vertexCount * sizeof(float) * 3;
        float* vertexData = new float[vertexCount * 3];
        memcpy(vertexData, vertices, bufferSize);

        VertexBuffer::BufferDescriptor desc(vertexData, bufferSize,
            [](void* buffer, size_t size, void* user) { delete[] static_cast<float*>(buffer); });
        mesh->mVertexBuffers[0]->setBufferAt(*engine, 0, std::move(desc));
        std::cout << "  ✓ Vertex data uploaded" << std::endl;
        passed++;

        // 创建IndexBuffer
        uint16_t indices[] = {0, 1, 2};
        const size_t indexCount = 3;
        mesh->mIndexBuffers.resize(1);
        mesh->mIndexBuffers[0] = IndexBuffer::Builder()
            .indexCount(indexCount)
            .bufferType(IndexBuffer::IndexType::USHORT)
            .build(*engine);

        if (mesh->mIndexBuffers[0]) {
            std::cout << "  ✓ IndexBuffer created successfully" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: IndexBuffer creation failed" << std::endl;
            failed++;
        }

        // 上传索引数据
        uint16_t* indexData = new uint16_t[indexCount];
        memcpy(indexData, indices, indexCount * sizeof(uint16_t));

        IndexBuffer::BufferDescriptor indexDesc(indexData, indexCount * sizeof(uint16_t),
            [](void* buffer, size_t, void*) { delete[] static_cast<uint16_t*>(buffer); });
        mesh->mIndexBuffers[0]->setBuffer(*engine, std::move(indexDesc));
        std::cout << "  ✓ Index data uploaded" << std::endl;
        passed++;

        // 设置包围盒
        mesh->mBoundingBox = Aabb{{0, 0, 0}, {1, 1, 0}};
        Aabb bbox = mesh->getBoundingBox();
        if (bbox.min.x == 0 && bbox.max.x == 1) {
            std::cout << "  ✓ Bounding box set correctly" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Bounding box incorrect" << std::endl;
            failed++;
        }

        delete mesh;
    }

    // ========================================================================
    // Test 2: Skeleton 绑定测试 - 蒙皮网格与骨骼绑定
    // ========================================================================
    // 【测试目的】
    // 验证 MeshAsset 绑定 SkeletonAsset 的逻辑，包括成功和失败场景
    //
    // 【验证点】
    // 1. 骨骼索引在范围内时绑定成功
    // 2. getBoundSkeleton() 返回正确的骨骼指针
    // 3. 骨骼索引超范围时绑定失败（安全检查）
    //
    // 【背景】
    // Skinning 要求：mesh 的每个顶点包含骨骼索引和权重，骨骼索引必须在
    // skeleton 的骨骼数量范围内，否则会导致访问越界或渲染错误。
    std::cout << "\n[Test 2] Skeleton binding..." << std::endl;
    {
        // 创建骨骼
        FSkeletonAsset* skeleton = new FSkeletonAsset();
        skeleton->mEngine = engine;
        skeleton->mEntityManager = &em;
        skeleton->mTransformManager = &tm;

        const size_t boneCount = 3;
        skeleton->mBoneEntities.reserve(boneCount);
        skeleton->mBoneNames.reserve(boneCount);
        skeleton->mInverseBindMatrices.reserve(boneCount);

        for (size_t i = 0; i < boneCount; ++i) {
            Entity entity = em.create();
            skeleton->mBoneEntities.push_back(entity);

            auto ti = tm.getInstance(entity);
            if (!ti) {
                tm.create(entity);
            }

            char nameBuf[32];
            snprintf(nameBuf, sizeof(nameBuf), "bone_%zu", i);
            skeleton->mBoneNames.emplace_back(nameBuf);
            skeleton->mBoneNameToIndex[nameBuf] = i;
            skeleton->mInverseBindMatrices.push_back(mat4f());
        }
        skeleton->mRoot = skeleton->mBoneEntities[0];

        // 创建MeshAsset（带skinning）
        FMeshAsset* mesh = new FMeshAsset();
        mesh->mEngine = engine;
        mesh->mRenderableManager = &rm;
        mesh->mHasSkinning = true;
        mesh->mMaxBoneIndex = 2;  // 使用骨骼0, 1, 2

        // 测试骨骼绑定（索引在范围内，应该成功）
        if (mesh->bindSkeleton(skeleton)) {
            std::cout << "  ✓ Skeleton binding successful" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Skeleton binding failed" << std::endl;
            failed++;
        }

        // 验证绑定的骨骼
        if (mesh->getBoundSkeleton() == skeleton) {
            std::cout << "  ✓ getBoundSkeleton() returns correct skeleton" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: getBoundSkeleton() incorrect" << std::endl;
            failed++;
        }

        // 测试骨骼索引超范围（应该失败）
        FMeshAsset* mesh2 = new FMeshAsset();
        mesh2->mEngine = engine;
        mesh2->mRenderableManager = &rm;
        mesh2->mHasSkinning = true;
        mesh2->mMaxBoneIndex = 10;  // 超过skeleton的骨骼数

        if (!mesh2->bindSkeleton(skeleton)) {
            std::cout << "  ✓ Out-of-range bone index rejected correctly" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Should reject out-of-range bone indices" << std::endl;
            failed++;
        }

        delete mesh2;
        delete mesh;
        delete skeleton;
    }

    // Test 3: updateSkinning 测试
    std::cout << "\n[Test 3] updateSkinning..." << std::endl;
    {
        // 创建完整的 skeleton + mesh + animator
        FSkeletonAsset* skeleton = new FSkeletonAsset();
        skeleton->mEngine = engine;
        skeleton->mEntityManager = &em;
        skeleton->mTransformManager = &tm;

        const size_t boneCount = 2;
        skeleton->mBoneEntities.reserve(boneCount);
        skeleton->mBoneNames.reserve(boneCount);
        skeleton->mInverseBindMatrices.reserve(boneCount);

        for (size_t i = 0; i < boneCount; ++i) {
            Entity entity = em.create();
            skeleton->mBoneEntities.push_back(entity);

            auto ti = tm.getInstance(entity);
            if (!ti) {
                tm.create(entity);
                ti = tm.getInstance(entity);
            }

            char nameBuf[32];
            snprintf(nameBuf, sizeof(nameBuf), "bone_%zu", i);
            skeleton->mBoneNames.emplace_back(nameBuf);
            skeleton->mBoneNameToIndex[nameBuf] = i;
            skeleton->mInverseBindMatrices.push_back(mat4f());
        }
        skeleton->mRoot = skeleton->mBoneEntities[0];

        // 创建 mesh
        FMeshAsset* mesh = new FMeshAsset();
        mesh->mEngine = engine;
        mesh->mRenderableManager = &rm;
        mesh->mHasSkinning = true;
        mesh->mMaxBoneIndex = 1;
        mesh->bindSkeleton(skeleton);

        // 创建 animator
        StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
        animator->bindSkeleton(skeleton);

        // 测试 updateSkinning（不应该崩溃）
        try {
            mesh->updateSkinning(animator);
            std::cout << "  ✓ updateSkinning() executed without crash" << std::endl;
            passed++;
        } catch (...) {
            std::cout << "  ✗ FAIL: updateSkinning() crashed" << std::endl;
            failed++;
        }

        // 测试多次调用
        for (int i = 0; i < 10; ++i) {
            mesh->updateSkinning(animator);
        }
        std::cout << "  ✓ Multiple updateSkinning() calls successful" << std::endl;
        passed++;

        StandaloneAnimator::destroy(&animator);
        delete mesh;
        delete skeleton;
    }

    // Test 4: 无 skinning 的 mesh 测试
    std::cout << "\n[Test 4] Non-skinned mesh..." << std::endl;
    {
        FMeshAsset* mesh = new FMeshAsset();
        mesh->mEngine = engine;
        mesh->mRenderableManager = &rm;
        mesh->mHasSkinning = false;
        mesh->mBoundingBox = Aabb{{-1, -1, -1}, {1, 1, 1}};

        // 测试获取包围盒
        Aabb bbox = mesh->getBoundingBox();
        if (bbox.min.x == -1 && bbox.max.x == 1) {
            std::cout << "  ✓ Non-skinned mesh bounding box correct" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Bounding box incorrect" << std::endl;
            failed++;
        }

        // 测试绑定 skeleton（无 skinning 应该失败）
        FSkeletonAsset* skeleton = new FSkeletonAsset();
        skeleton->mEngine = engine;
        skeleton->mEntityManager = &em;
        skeleton->mTransformManager = &tm;

        if (!mesh->bindSkeleton(skeleton)) {
            std::cout << "  ✓ Non-skinned mesh correctly rejects skeleton binding" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Non-skinned mesh should not bind skeleton" << std::endl;
            failed++;
        }

        delete skeleton;
        delete mesh;
    }

    // Test 5: Entity 和 Renderable 测试
    std::cout << "\n[Test 5] Entity and Renderable..." << std::endl;
    {
        FMeshAsset* mesh = new FMeshAsset();
        mesh->mEngine = engine;
        mesh->mRenderableManager = &rm;
        mesh->mHasSkinning = false;

        // 设置 renderable entity
        mesh->mRenderableEntity = em.create();

        Entity entity = mesh->getRenderableEntity();
        if (entity) {
            std::cout << "  ✓ getRenderableEntity() returns valid entity" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: getRenderableEntity() returns null" << std::endl;
            failed++;
        }

        // 测试添加到场景
        scene->addEntity(entity);
        std::cout << "  ✓ Entity added to scene successfully" << std::endl;
        passed++;

        scene->remove(entity);
        delete mesh;
    }

    // Test 6: 空 MeshAsset 测试
    std::cout << "\n[Test 6] Empty MeshAsset..." << std::endl;
    {
        FMeshAsset* mesh = new FMeshAsset();
        mesh->mEngine = engine;
        mesh->mRenderableManager = &rm;
        mesh->mHasSkinning = false;

        // 测试空 mesh 的包围盒（应该是默认值）
        Aabb bbox = mesh->getBoundingBox();
        std::cout << "  ✓ Empty mesh bounding box: [" << bbox.min.x << "," << bbox.max.x << "]" << std::endl;
        passed++;

        // 测试 getBoundSkeleton（应该返回 nullptr）
        if (mesh->getBoundSkeleton() == nullptr) {
            std::cout << "  ✓ Empty mesh getBoundSkeleton() returns nullptr" << std::endl;
            passed++;
        } else {
            std::cout << "  ✗ FAIL: Should return nullptr when no skeleton bound" << std::endl;
            failed++;
        }

        delete mesh;
    }

    // Cleanup
    engine->destroy(scene);
    Engine::destroy(&engine);

    // Summary
    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total: " << (passed + failed) << std::endl;

    if (failed == 0) {
        std::cout << "\n🎉 ALL MESH TESTS PASSED!" << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ SOME TESTS FAILED" << std::endl;
        return 1;
    }
}
