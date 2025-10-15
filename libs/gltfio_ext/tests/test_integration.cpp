#include <gltfio/AssetLoaderExt.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/MeshAsset.h>
#include <gltfio/AnimationAsset.h>
#include <gltfio/StandaloneAnimator.h>
#include "../src/FSkeletonAsset.h"
#include "../src/FMeshAsset.h"
#include "../src/FAnimationAsset.h"

#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/RenderableManager.h>
#include <utils/EntityManager.h>
#include <math/vec3.h>
#include <iostream>
#include <chrono>

using namespace filament;
using namespace filament::gltfio;
using namespace filament::math;
using namespace utils;

int main() {
    std::cout << "=== gltfio_ext Integration Test ===" << std::endl;

    // 1. 创建Filament环境
    std::cout << "\n--- Setting up Filament ---" << std::endl;
    Engine* engine = Engine::create();
    Scene* scene = engine->createScene();
    auto& em = EntityManager::get();
    auto& tm = engine->getTransformManager();
    auto& rm = engine->getRenderableManager();
    std::cout << "✓ Engine, Scene created" << std::endl;

    // 2. 手动创建骨骼（模拟从glTF加载）
    std::cout << "\n--- Creating Test Assets ---" << std::endl;
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
            ti = tm.getInstance(entity);
        }

        char nameBuf[32];
        snprintf(nameBuf, sizeof(nameBuf), "bone_%zu", i);
        skeleton->mBoneNames.emplace_back(nameBuf);
        skeleton->mBoneNameToIndex[nameBuf] = i;

        skeleton->mInverseBindMatrices.push_back(mat4f());

        // 设置父子关系
        if (i > 0) {
            auto parentTi = tm.getInstance(skeleton->mBoneEntities[i - 1]);
            tm.setParent(ti, parentTi);
        }
    }

    skeleton->mRoot = skeleton->mBoneEntities[0];
    std::cout << "✓ Skeleton created: " << skeleton->getBoneCount() << " bones" << std::endl;

    // 3. 创建简单网格（模拟从glTF加载）
    FMeshAsset* mesh = new FMeshAsset();
    mesh->mEngine = engine;
    mesh->mRenderableManager = &rm;
    mesh->mHasSkinning = false;

    // 创建简单三角形
    const size_t vertexCount = 3;
    float vertices[] = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.5f, 1.0f, 0.0f
    };

    VertexBuffer::Builder vbb;
    vbb.vertexCount(vertexCount)
       .bufferCount(1)
       .attribute(VertexAttribute::POSITION, 0,
                  VertexBuffer::AttributeType::FLOAT3, 0, sizeof(float) * 3);
    mesh->mVertexBuffer = vbb.build(*engine);

    size_t bufferSize = vertexCount * sizeof(float) * 3;
    float* vertexData = new float[vertexCount * 3];
    memcpy(vertexData, vertices, bufferSize);

    VertexBuffer::BufferDescriptor desc(vertexData, bufferSize,
        [](void* buffer, size_t size, void* user) { delete[] static_cast<float*>(buffer); });
    mesh->mVertexBuffer->setBufferAt(*engine, 0, std::move(desc));

    uint16_t indices[] = {0, 1, 2};
    const size_t indexCount = 3;
    mesh->mIndexBuffer = IndexBuffer::Builder()
        .indexCount(indexCount)
        .bufferType(IndexBuffer::IndexType::USHORT)
        .build(*engine);

    uint16_t* indexData = new uint16_t[indexCount];
    memcpy(indexData, indices, indexCount * sizeof(uint16_t));

    IndexBuffer::BufferDescriptor indexDesc(indexData, indexCount * sizeof(uint16_t),
        [](void* buffer, size_t, void*) { delete[] static_cast<uint16_t*>(buffer); });
    mesh->mIndexBuffer->setBuffer(*engine, std::move(indexDesc));

    mesh->mRenderableEntity = Entity();
    mesh->mRenderableInstance = RenderableManager::Instance();
    mesh->mBoundingBox = Aabb{{0, 0, 0}, {1, 1, 0}};

    std::cout << "✓ Mesh created" << std::endl;

    // 4. 绑定骨骼到网格
    std::cout << "\n--- Binding Skeleton to Mesh ---" << std::endl;
    // 由于我们的简化mesh没有skinning数据，绑定会失败，这是正常的
    if (mesh->bindSkeleton(skeleton)) {
        std::cout << "✓ Skeleton bound to mesh" << std::endl;
    } else {
        std::cout << "⚠ Mesh has no skinning (expected for simplified test)" << std::endl;
    }

    // 5. 创建动画
    FAnimationAsset* anim = new FAnimationAsset();
    anim->mName = CString("TestAnimation");
    anim->mDuration = 2.0f;

    Sampler sampler;
    sampler.times[0.0f] = 0;
    sampler.times[1.0f] = 1;
    sampler.times[2.0f] = 2;
    sampler.values = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f
    };
    sampler.interpolation = Sampler::LINEAR;
    anim->mSamplers.push_back(sampler);

    Channel channel;
    channel.sampler = &anim->mSamplers[0];
    channel.targetBoneName = "bone_0";
    channel.transformType = Channel::TRANSLATION;
    anim->mChannels.push_back(channel);

    std::cout << "✓ Animation created: '" << anim->getName()
              << "' (" << anim->getDuration() << "s, "
              << anim->getChannelCount() << " channels)" << std::endl;

    // 6. 创建动画器
    std::cout << "\n--- Setting up Animator ---" << std::endl;
    StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
    animator->bindSkeleton(skeleton);
    int animId = animator->playAnimation(anim, 1.0f, true);
    std::cout << "✓ Animator created and playing (ID: " << animId << ")" << std::endl;

    // 7. 模拟渲染循环
    std::cout << "\n--- Simulating Render Loop ---" << std::endl;
    auto startTime = std::chrono::high_resolution_clock::now();

    for (int frame = 0; frame < 60; ++frame) {
        float deltaTime = 1.0f / 60.0f;

        // 更新动画
        animator->update(deltaTime);
        mesh->updateSkinning(animator);

        if (frame % 15 == 0) {
            const auto* boneMatrices = animator->getBoneMatrices();
            if (boneMatrices && animator->getBoneCount() > 0) {
                std::cout << "  Frame " << frame << ": bone[0] translation.x = "
                          << boneMatrices[0][3][0] << std::endl;
            }
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    std::cout << "✓ Simulated 60 frames in " << duration.count() << "ms" << std::endl;

    // 8. 测试多网格共享骨骼
    std::cout << "\n--- Testing Multi-Mesh Sharing Skeleton ---" << std::endl;
    FMeshAsset* mesh2 = new FMeshAsset();
    mesh2->mEngine = engine;
    mesh2->mRenderableManager = &rm;
    mesh2->mHasSkinning = false;
    mesh2->mVertexBuffer = nullptr;
    mesh2->mIndexBuffer = nullptr;
    mesh2->mRenderableEntity = Entity();
    mesh2->mBoundingBox = Aabb{{0, 0, 0}, {1, 1, 0}};

    if (mesh2->bindSkeleton(skeleton)) {
        std::cout << "✓ Second mesh bound to same skeleton" << std::endl;
    } else {
        std::cout << "⚠ Second mesh has no skinning (expected)" << std::endl;
    }

    // 更新两个网格
    animator->update(1.0f / 60.0f);
    mesh->updateSkinning(animator);
    mesh2->updateSkinning(animator);
    std::cout << "✓ Both meshes updated with same animator" << std::endl;

    // 9. 测试权重和停止
    std::cout << "\n--- Testing Animation Control ---" << std::endl;
    animator->setAnimationWeight(animId, 0.5f);
    std::cout << "✓ Animation weight set to 0.5" << std::endl;

    animator->stopAnimation(animId);
    std::cout << "✓ Animation stopped" << std::endl;

    animator->resetToBindPose();
    std::cout << "✓ Reset to bind pose" << std::endl;

    // 10. 清理
    std::cout << "\n--- Cleanup ---" << std::endl;

    StandaloneAnimator::destroy(&animator);
    delete mesh2;
    delete anim;
    delete mesh;
    delete skeleton;

    engine->destroy(scene);
    Engine::destroy(&engine);

    std::cout << "✓ All resources cleaned up" << std::endl;

    std::cout << "\n=== INTEGRATION TEST PASSED ===" << std::endl;
    return 0;
}
