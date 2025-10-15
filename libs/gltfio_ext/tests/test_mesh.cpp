#include <gltfio/AssetLoaderExt.h>
#include <gltfio/SkeletonAsset.h>
#include <gltfio/MeshAsset.h>
#include "../src/FMeshAsset.h"

#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/RenderableManager.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <utils/EntityManager.h>
#include <iostream>

using namespace filament;
using namespace filament::gltfio;
using namespace utils;

int main() {
    std::cout << "=== MeshAsset Test ===" << std::endl;

    // 创建Engine
    Engine* engine = Engine::create();
    auto& em = EntityManager::get();
    auto& rm = engine->getRenderableManager();

    // 手动创建一个简单的MeshAsset用于测试
    FMeshAsset* mesh = new FMeshAsset();
    mesh->mEngine = engine;
    mesh->mRenderableManager = &rm;
    mesh->mHasSkinning = false;

    // 创建简单的三角形几何体
    const size_t vertexCount = 3;
    float vertices[] = {
        0.0f, 0.0f, 0.0f,    // vertex 0
        1.0f, 0.0f, 0.0f,    // vertex 1
        0.5f, 1.0f, 0.0f     // vertex 2
    };

    // 创建VertexBuffer
    VertexBuffer::Builder vbb;
    vbb.vertexCount(vertexCount)
       .bufferCount(1)
       .attribute(VertexAttribute::POSITION, 0,
                  VertexBuffer::AttributeType::FLOAT3, 0, sizeof(float) * 3);

    mesh->mVertexBuffer = vbb.build(*engine);

    // 填充顶点数据
    size_t bufferSize = vertexCount * sizeof(float) * 3;
    float* vertexData = new float[vertexCount * 3];
    memcpy(vertexData, vertices, bufferSize);

    VertexBuffer::BufferDescriptor desc(vertexData, bufferSize,
        [](void* buffer, size_t size, void* user) { delete[] static_cast<float*>(buffer); });
    mesh->mVertexBuffer->setBufferAt(*engine, 0, std::move(desc));

    // 创建IndexBuffer
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

    // 注意：在这个简化测试中，我们不创建完整的Renderable
    // 因为需要有效的MaterialProvider，这将在集成测试中验证
    mesh->mRenderableEntity = Entity();  // 空实体
    mesh->mRenderableInstance = RenderableManager::Instance();
    mesh->mMaterialInstance = nullptr;
    mesh->mBoundingBox = Aabb{{0, 0, 0}, {1, 1, 0}};

    // 测试API
    std::cout << "\n--- Testing MeshAsset API ---" << std::endl;

    Entity renderableEntity = mesh->getRenderableEntity();
    std::cout << "Renderable entity ID: " << renderableEntity.getId() << std::endl;
    // 在简化测试中，实体ID为0是正常的
    std::cout << "Entity is null (expected in simplified test): " << (renderableEntity.getId() == 0 ? "YES" : "NO") << std::endl;

    Aabb bbox = mesh->getBoundingBox();
    std::cout << "Bounding box min: (" << bbox.min.x << ", " << bbox.min.y << ", " << bbox.min.z << ")" << std::endl;
    std::cout << "Bounding box max: (" << bbox.max.x << ", " << bbox.max.y << ", " << bbox.max.z << ")" << std::endl;
    assert(bbox.min.x == 0.0f && bbox.min.y == 0.0f && bbox.min.z == 0.0f);
    assert(bbox.max.x == 1.0f && bbox.max.y == 1.0f && bbox.max.z == 0.0f);

    // 测试骨骼绑定（应该失败，因为没有蒙皮数据）
    std::cout << "\n--- Testing Skeleton Binding ---" << std::endl;
    bool bindResult = mesh->bindSkeleton(nullptr);
    std::cout << "Bind null skeleton: " << (bindResult ? "SUCCESS (unexpected)" : "FAILED (expected)") << std::endl;
    assert(!bindResult);

    SkeletonAsset* boundSkeleton = mesh->getBoundSkeleton();
    std::cout << "Bound skeleton: " << (boundSkeleton ? "EXISTS" : "NULL (expected)") << std::endl;
    assert(boundSkeleton == nullptr);

    // 测试VertexBuffer和IndexBuffer
    std::cout << "\n--- Testing Geometry Buffers ---" << std::endl;
    std::cout << "VertexBuffer created: " << (mesh->mVertexBuffer != nullptr ? "YES" : "NO") << std::endl;
    assert(mesh->mVertexBuffer != nullptr);
    std::cout << "IndexBuffer created: " << (mesh->mIndexBuffer != nullptr ? "YES" : "NO") << std::endl;
    assert(mesh->mIndexBuffer != nullptr);

    // 清理
    delete mesh;
    Engine::destroy(&engine);

    std::cout << "\n=== MeshAsset Test PASSED ===" << std::endl;
    return 0;
}
