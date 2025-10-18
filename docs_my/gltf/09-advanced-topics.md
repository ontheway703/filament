# 高级主题

本文档涵盖 glTF 在 Filament 中的高级应用场景，包括多场景管理、自定义扩展、系统集成和疑难问题解决。

## 目录

1. [多场景管理](#多场景管理)
2. [自定义扩展](#自定义扩展)
3. [物理引擎集成](#物理引擎集成)
4. [音频系统集成](#音频系统集成)
5. [程序化生成](#程序化生成)
6. [调试与验证](#调试与验证)
7. [生产流程集成](#生产流程集成)
8. [常见问题与解决](#常见问题与解决)

---

## 多场景管理

glTF 文件可以包含多个场景（Scenes），每个场景是独立的节点树。

### 1. 场景结构

**glTF 多场景示例：**
```json
{
  "scenes": [
    {
      "name": "MainMenu",
      "nodes": [0, 1, 2]
    },
    {
      "name": "Level1",
      "nodes": [3, 4, 5]
    },
    {
      "name": "Level2",
      "nodes": [6, 7, 8]
    }
  ],
  "scene": 0,  // 默认场景索引
  "nodes": [...]
}
```

### 2. 场景切换

**Filament 场景管理：**
```cpp
#include <gltfio/FilamentAsset.h>

class MultiSceneManager {
public:
    MultiSceneManager(Engine* engine, AssetLoader* loader, ResourceLoader* resLoader)
        : mEngine(engine), mAssetLoader(loader), mResourceLoader(resLoader) {
        mFilamentScene = engine->createScene();
    }

    bool loadGltfFile(const std::string& path) {
        auto data = readFile(path);
        mAsset = mAssetLoader->createAsset(data.data(), data.size());

        if (!mAsset) {
            return false;
        }

        mResourceLoader->loadResources(mAsset);

        // 获取场景数量
        size_t sceneCount = mAsset->getSceneCount();
        utils::slog.i << "Loaded " << sceneCount << " scenes" << utils::io::endl;

        // 打印所有场景名称
        for (size_t i = 0; i < sceneCount; i++) {
            const char* name = mAsset->getSceneName(i);
            utils::slog.i << "  Scene " << i << ": "
                          << (name ? name : "<unnamed>") << utils::io::endl;
        }

        // 默认显示第一个场景
        if (sceneCount > 0) {
            activateScene(0);
        }

        return true;
    }

    void activateScene(size_t index) {
        size_t sceneCount = mAsset->getSceneCount();
        if (index >= sceneCount) {
            utils::slog.e << "Invalid scene index: " << index << utils::io::endl;
            return;
        }

        // 移除当前场景的所有实体
        if (mCurrentSceneIndex != SIZE_MAX) {
            auto entities = mAsset->getEntities();
            auto count = mAsset->getEntityCount();
            mFilamentScene->removeEntities(entities, count);
        }

        // 添加新场景的实体
        // 注意：glTF 场景是节点子集，我们需要手动过滤
        mCurrentSceneIndex = index;
        const char* name = mAsset->getSceneName(index);

        // TODO: Filament 的 gltfio 目前没有直接 API 获取单个场景的实体
        // 需要解析场景节点层级
        activateSceneNodes(index);

        utils::slog.i << "Activated scene: "
                      << (name ? name : "<unnamed>") << utils::io::endl;
    }

    size_t getCurrentScene() const { return mCurrentSceneIndex; }
    size_t getSceneCount() const { return mAsset ? mAsset->getSceneCount() : 0; }

private:
    void activateSceneNodes(size_t sceneIndex) {
        // 获取场景的根节点列表
        // 注意：这需要访问内部 glTF 数据结构
        // 实际使用中可能需要使用 cgltf 直接解析

        // 简化实现：激活所有实体（如果只有一个场景）
        auto entities = mAsset->getEntities();
        auto count = mAsset->getEntityCount();
        mFilamentScene->addEntities(entities, count);
    }

    Engine* mEngine;
    AssetLoader* mAssetLoader;
    ResourceLoader* mResourceLoader;
    FilamentAsset* mAsset = nullptr;
    Scene* mFilamentScene;
    size_t mCurrentSceneIndex = SIZE_MAX;
};

// 使用示例
MultiSceneManager sceneManager(engine, assetLoader, resourceLoader);
sceneManager.loadGltfFile("multi_scene.glb");

// 切换场景
sceneManager.activateScene(1);  // Level1
sceneManager.activateScene(2);  // Level2
```

### 3. 高级场景过滤

**基于 cgltf 的精确场景控制：**
```cpp
#include <cgltf/cgltf.h>

class PreciseSceneLoader {
public:
    struct SceneInfo {
        std::string name;
        std::vector<Entity> entities;
    };

    bool loadGltfWithScenes(const std::string& path) {
        auto data = readFile(path);

        // 1. 使用 cgltf 解析
        cgltf_options options = {};
        cgltf_data* gltfData = nullptr;
        cgltf_result result = cgltf_parse(&options, data.data(), data.size(), &gltfData);

        if (result != cgltf_result_success) {
            return false;
        }

        result = cgltf_load_buffers(&options, gltfData, path.c_str());
        if (result != cgltf_result_success) {
            cgltf_free(gltfData);
            return false;
        }

        // 2. 解析每个场景
        for (size_t i = 0; i < gltfData->scenes_count; i++) {
            cgltf_scene* scene = &gltfData->scenes[i];

            SceneInfo info;
            info.name = scene->name ? scene->name : "";

            // 遍历场景的根节点
            for (size_t j = 0; j < scene->nodes_count; j++) {
                cgltf_node* node = scene->nodes[j];
                collectSceneEntities(node, info.entities);
            }

            mScenes.push_back(info);
        }

        // 3. 使用 Filament 加载完整资产
        mAsset = mAssetLoader->createAsset(data.data(), data.size());
        mResourceLoader->loadResources(mAsset);

        cgltf_free(gltfData);
        return true;
    }

    void activateScene(size_t index) {
        if (index >= mScenes.size()) return;

        // 移除当前场景
        if (mCurrentScene != SIZE_MAX) {
            const auto& entities = mScenes[mCurrentScene].entities;
            mFilamentScene->removeEntities(entities.data(), entities.size());
        }

        // 添加新场景
        const auto& entities = mScenes[index].entities;
        mFilamentScene->addEntities(entities.data(), entities.size());
        mCurrentScene = index;
    }

private:
    void collectSceneEntities(cgltf_node* node, std::vector<Entity>& entities) {
        // 根据节点索引映射到 Filament Entity
        // 这需要维护 cgltf_node* 到 Entity 的映射

        // 递归处理子节点
        for (size_t i = 0; i < node->children_count; i++) {
            collectSceneEntities(node->children[i], entities);
        }
    }

    std::vector<SceneInfo> mScenes;
    size_t mCurrentScene = SIZE_MAX;
    FilamentAsset* mAsset = nullptr;
    AssetLoader* mAssetLoader;
    ResourceLoader* mResourceLoader;
    Scene* mFilamentScene;
};
```

---

## 自定义扩展

glTF 支持自定义扩展来添加应用特定的数据。

### 1. 扩展命名规范

```
KHR_*        - Khronos 官方扩展（需要批准）
EXT_*        - 多厂商扩展（需要社区审查）
VENDOR_*     - 厂商特定扩展
MYAPP_*      - 应用自定义扩展
```

### 2. 定义自定义扩展

**glTF 文件中的自定义扩展：**
```json
{
  "extensionsUsed": ["MYAPP_gameplay_data"],
  "extensionsRequired": [],  // 如果不是必需的，留空

  "nodes": [
    {
      "name": "Checkpoint1",
      "translation": [10.0, 0.0, 5.0],
      "extensions": {
        "MYAPP_gameplay_data": {
          "type": "checkpoint",
          "checkpointId": 1,
          "respawnPoint": true,
          "saveGame": true
        }
      }
    },
    {
      "name": "Enemy1",
      "translation": [20.0, 0.0, 10.0],
      "extensions": {
        "MYAPP_gameplay_data": {
          "type": "enemy",
          "enemyType": "goblin",
          "health": 100,
          "attackPower": 15,
          "patrolPath": [0, 1, 2, 3]  // 引用其他节点
        }
      }
    }
  ]
}
```

### 3. 解析自定义扩展

**使用 cgltf 读取扩展数据：**
```cpp
#include <cgltf/cgltf.h>
#include <json/json.hpp>  // nlohmann/json

using json = nlohmann::json;

struct GameplayData {
    std::string type;
    int id;
    std::map<std::string, std::string> properties;
};

class CustomExtensionParser {
public:
    std::map<Entity, GameplayData> parseGameplayExtensions(const std::string& path) {
        std::map<Entity, GameplayData> gameplayEntities;

        // 加载 glTF
        auto fileData = readFile(path);
        cgltf_options options = {};
        cgltf_data* data = nullptr;

        if (cgltf_parse(&options, fileData.data(), fileData.size(), &data) != cgltf_result_success) {
            return gameplayEntities;
        }

        cgltf_load_buffers(&options, data, path.c_str());

        // 遍历节点
        for (size_t i = 0; i < data->nodes_count; i++) {
            cgltf_node* node = &data->nodes[i];

            // 检查扩展
            if (!node->extensions) continue;

            // 查找自定义扩展
            for (size_t j = 0; j < node->extensions_count; j++) {
                cgltf_extension* ext = &node->extensions[j];

                if (strcmp(ext->name, "MYAPP_gameplay_data") == 0) {
                    // 解析 JSON 数据
                    json extData = json::parse(ext->data);

                    GameplayData gameplay;
                    gameplay.type = extData.value("type", "");

                    if (gameplay.type == "checkpoint") {
                        gameplay.id = extData.value("checkpointId", 0);
                        gameplay.properties["respawnPoint"] = extData.value("respawnPoint", false) ? "true" : "false";
                    }
                    else if (gameplay.type == "enemy") {
                        gameplay.properties["enemyType"] = extData.value("enemyType", "");
                        gameplay.properties["health"] = std::to_string(extData.value("health", 100));
                        gameplay.properties["attackPower"] = std::to_string(extData.value("attackPower", 10));
                    }

                    // 映射到 Filament Entity（需要维护索引映射）
                    Entity entity = getEntityForNode(i);
                    gameplayEntities[entity] = gameplay;
                }
            }
        }

        cgltf_free(data);
        return gameplayEntities;
    }

private:
    Entity getEntityForNode(size_t nodeIndex) {
        // 需要维护 cgltf 节点索引到 Filament Entity 的映射
        // 这通常在资产加载时建立
        return mNodeToEntityMap[nodeIndex];
    }

    std::map<size_t, Entity> mNodeToEntityMap;
};

// 使用示例
CustomExtensionParser parser;
auto gameplayData = parser.parseGameplayExtensions("level.glb");

for (const auto& [entity, data] : gameplayData) {
    if (data.type == "checkpoint") {
        utils::slog.i << "Found checkpoint " << data.id << utils::io::endl;
        registerCheckpoint(entity, data.id);
    }
    else if (data.type == "enemy") {
        utils::slog.i << "Found enemy: " << data.properties["enemyType"] << utils::io::endl;
        spawnEnemy(entity, data.properties);
    }
}
```

### 4. Blender 中导出自定义扩展

**Blender Python 导出插件：**
```python
import bpy
import json

def export_with_gameplay_data(filepath):
    """导出 glTF 并添加自定义游戏数据"""

    # 1. 首先使用标准导出器
    bpy.ops.export_scene.gltf(
        filepath=filepath,
        export_format='GLB'
    )

    # 2. 读取生成的 GLB
    with open(filepath, 'rb') as f:
        glb_data = f.read()

    # 解析 GLB 结构（JSON chunk）
    import struct

    magic = struct.unpack('<I', glb_data[0:4])[0]
    version = struct.unpack('<I', glb_data[4:8])[0]
    length = struct.unpack('<I', glb_data[8:12])[0]

    # 读取 JSON chunk
    json_chunk_length = struct.unpack('<I', glb_data[12:16])[0]
    json_chunk_type = struct.unpack('<I', glb_data[16:20])[0]
    json_data = glb_data[20:20 + json_chunk_length].decode('utf-8')

    # 解析 JSON
    gltf = json.loads(json_data)

    # 3. 添加自定义扩展
    if 'extensionsUsed' not in gltf:
        gltf['extensionsUsed'] = []
    gltf['extensionsUsed'].append('MYAPP_gameplay_data')

    # 为标记的对象添加扩展数据
    for obj in bpy.context.scene.objects:
        # 检查对象的自定义属性
        if 'gameplay_type' in obj:
            node_index = find_node_index(gltf, obj.name)
            if node_index is not None:
                node = gltf['nodes'][node_index]

                if 'extensions' not in node:
                    node['extensions'] = {}

                # 添加游戏数据
                node['extensions']['MYAPP_gameplay_data'] = {
                    'type': obj['gameplay_type'],
                    'id': obj.get('gameplay_id', 0)
                }

                # 根据类型添加特定属性
                if obj['gameplay_type'] == 'checkpoint':
                    node['extensions']['MYAPP_gameplay_data']['respawnPoint'] = obj.get('respawn_point', False)

    # 4. 重新打包 GLB
    # （这部分需要完整的 GLB 重写逻辑）
    # 简化版：只保存 glTF（非 GLB）
    gltf_json = json.dumps(gltf, indent=2)
    with open(filepath.replace('.glb', '.gltf'), 'w') as f:
        f.write(gltf_json)

    print("Exported with custom extensions")

def find_node_index(gltf, object_name):
    """查找节点索引"""
    for i, node in enumerate(gltf.get('nodes', [])):
        if node.get('name') == object_name:
            return i
    return None

# 使用示例：
# 1. 在 Blender 中为对象添加自定义属性
obj = bpy.context.active_object
obj['gameplay_type'] = 'checkpoint'
obj['gameplay_id'] = 1
obj['respawn_point'] = True

# 2. 导出
export_with_gameplay_data("/path/to/level.glb")
```

---

## 物理引擎集成

将 glTF 视觉模型与物理碰撞体结合。

### 1. Bullet Physics 集成

**碰撞体生成：**
```cpp
#include <btBulletDynamicsCommon.h>
#include <filament/RenderableManager.h>

class PhysicsIntegration {
public:
    PhysicsIntegration() {
        // 初始化 Bullet
        mCollisionConfiguration = new btDefaultCollisionConfiguration();
        mDispatcher = new btCollisionDispatcher(mCollisionConfiguration);
        mBroadphase = new btDbvtBroadphase();
        mSolver = new btSequentialImpulseConstraintSolver();
        mDynamicsWorld = new btDiscreteDynamicsWorld(
            mDispatcher, mBroadphase, mSolver, mCollisionConfiguration);
        mDynamicsWorld->setGravity(btVector3(0, -9.81, 0));
    }

    void addRigidBodyFromGltf(FilamentAsset* asset, float mass = 1.0f) {
        auto& rcm = mEngine->getRenderableManager();
        auto& tcm = mEngine->getTransformManager();

        // 遍历所有 Renderable 实体
        for (size_t i = 0; i < asset->getRenderableEntityCount(); i++) {
            Entity entity = asset->getRenderableEntities()[i];
            auto ri = rcm.getInstance(entity);

            // 获取 AABB
            filament::Box aabb = rcm.getAxisAlignedBoundingBox(ri);
            math::float3 halfExtents = aabb.halfExtent;

            // 创建 Bullet 碰撞盒
            btCollisionShape* shape = new btBoxShape(
                btVector3(halfExtents.x, halfExtents.y, halfExtents.z));

            // 获取初始变换
            auto ti = tcm.getInstance(entity);
            math::mat4f transform = tcm.getWorldTransform(ti);
            math::float3 position = transform[3].xyz;

            btTransform startTransform;
            startTransform.setIdentity();
            startTransform.setOrigin(btVector3(position.x, position.y, position.z));

            // 创建刚体
            btVector3 localInertia(0, 0, 0);
            if (mass > 0.0f) {
                shape->calculateLocalInertia(mass, localInertia);
            }

            btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);
            btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, motionState, shape, localInertia);
            btRigidBody* body = new btRigidBody(rbInfo);

            mDynamicsWorld->addRigidBody(body);

            // 保存映射
            mEntityToBody[entity] = body;
            mBodyToEntity[body] = entity;
        }
    }

    void update(float deltaTime) {
        // 步进物理模拟
        mDynamicsWorld->stepSimulation(deltaTime, 10);

        // 同步物理变换到渲染
        auto& tcm = mEngine->getTransformManager();

        for (auto& [entity, body] : mEntityToBody) {
            btTransform trans;
            body->getMotionState()->getWorldTransform(trans);

            btVector3 origin = trans.getOrigin();
            btQuaternion rotation = trans.getRotation();

            // 更新 Filament 变换
            auto ti = tcm.getInstance(entity);
            math::mat4f newTransform = math::mat4f::translation(
                math::float3{origin.x(), origin.y(), origin.z()}
            ) * math::mat4f(math::quatf{rotation.w(), rotation.x(), rotation.y(), rotation.z()});

            tcm.setTransform(ti, newTransform);
        }
    }

private:
    btDefaultCollisionConfiguration* mCollisionConfiguration;
    btCollisionDispatcher* mDispatcher;
    btBroadphaseInterface* mBroadphase;
    btSequentialImpulseConstraintSolver* mSolver;
    btDiscreteDynamicsWorld* mDynamicsWorld;

    std::map<Entity, btRigidBody*> mEntityToBody;
    std::map<btRigidBody*, Entity> mBodyToEntity;
    Engine* mEngine;
};

// 使用示例
PhysicsIntegration physics;
physics.addRigidBodyFromGltf(asset, 10.0f);  // 10kg

// 游戏循环
while (running) {
    float deltaTime = calculateDeltaTime();
    physics.update(deltaTime);
    renderer->render(view);
}
```

### 2. 自定义碰撞网格

使用简化的碰撞网格代替视觉网格。

**Blender 导出碰撞体：**
```python
import bpy

def export_with_collision_meshes(filepath):
    """导出视觉网格 + 简化碰撞网格"""

    # 1. 为每个对象创建碰撞代理
    for obj in bpy.context.scene.objects:
        if obj.type != 'MESH' or obj.name.endswith('_collision'):
            continue

        # 复制对象作为碰撞网格
        collision_obj = obj.copy()
        collision_obj.data = obj.data.copy()
        collision_obj.name = f"{obj.name}_collision"
        bpy.context.collection.objects.link(collision_obj)

        # 简化碰撞网格
        bpy.context.view_layer.objects.active = collision_obj
        mod = collision_obj.modifiers.new(name="Decimate", type='DECIMATE')
        mod.ratio = 0.1  # 简化到 10%
        bpy.ops.object.modifier_apply(modifier="Decimate")

        # 隐藏碰撞网格（不渲染）
        collision_obj.hide_render = True

    # 2. 导出
    bpy.ops.export_scene.gltf(filepath=filepath, export_format='GLB')

    print("Exported with collision meshes")

export_with_collision_meshes("/path/to/model.glb")
```

---

## 音频系统集成

将 3D 音源附加到 glTF 节点。

### 1. 空间音频

**使用 OpenAL：**
```cpp
#include <AL/al.h>
#include <AL/alc.h>

class SpatialAudio {
public:
    void attachSoundToNode(Entity entity, const std::string& audioFile) {
        // 加载音频文件
        ALuint buffer = loadAudioFile(audioFile);
        ALuint source;

        alGenSources(1, &source);
        alSourcei(source, AL_BUFFER, buffer);

        // 3D 音频属性
        alSourcef(source, AL_PITCH, 1.0f);
        alSourcef(source, AL_GAIN, 1.0f);
        alSource3f(source, AL_POSITION, 0, 0, 0);
        alSource3f(source, AL_VELOCITY, 0, 0, 0);
        alSourcei(source, AL_LOOPING, AL_TRUE);

        mEntityToSource[entity] = source;
        alSourcePlay(source);
    }

    void updateListener(const math::float3& position, const math::float3& forward, const math::float3& up) {
        // 更新听者（通常是相机）位置
        alListener3f(AL_POSITION, position.x, position.y, position.z);

        float orientation[6] = {
            forward.x, forward.y, forward.z,
            up.x, up.y, up.z
        };
        alListenerfv(AL_ORIENTATION, orientation);
    }

    void updateSources(Engine* engine) {
        auto& tcm = engine->getTransformManager();

        for (auto& [entity, source] : mEntityToSource) {
            auto ti = tcm.getInstance(entity);
            math::float3 pos = tcm.getWorldTransform(ti)[3].xyz;

            alSource3f(source, AL_POSITION, pos.x, pos.y, pos.z);
        }
    }

private:
    ALuint loadAudioFile(const std::string& path) {
        // 使用 libsndfile 或其他库加载音频
        // ...
        return 0;  // 占位
    }

    std::map<Entity, ALuint> mEntityToSource;
};

// 使用示例
SpatialAudio audio;

// 为 glTF 节点附加音效
Entity fireEntity = findEntityByName(asset, "Campfire");
audio.attachSoundToNode(fireEntity, "fire_loop.wav");

// 更新循环
while (running) {
    math::float3 cameraPos = getCameraPosition();
    math::float3 forward = getCameraForward();
    math::float3 up = getCameraUp();

    audio.updateListener(cameraPos, forward, up);
    audio.updateSources(engine);
}
```

---

## 程序化生成

在运行时修改 glTF 数据或生成变体。

### 1. 运行时网格修改

```cpp
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>

class ProceduralMeshModifier {
public:
    void deformMesh(Entity entity, std::function<math::float3(math::float3)> deformFunc) {
        auto& rcm = mEngine->getRenderableManager();
        auto ri = rcm.getInstance(entity);

        // 获取原始顶点数据
        // 注意：Filament 不直接支持修改 VertexBuffer，需要重新创建

        // 1. 读取原始数据（需要保存副本）
        std::vector<math::float3> positions = mOriginalPositions[entity];

        // 2. 应用变形
        std::vector<math::float3> deformedPositions;
        for (const auto& pos : positions) {
            deformedPositions.push_back(deformFunc(pos));
        }

        // 3. 创建新的 VertexBuffer
        VertexBuffer* vb = VertexBuffer::Builder()
            .vertexCount(deformedPositions.size())
            .bufferCount(1)
            .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3)
            .build(*mEngine);

        vb->setBufferAt(*mEngine, 0,
            VertexBuffer::BufferDescriptor(
                deformedPositions.data(),
                deformedPositions.size() * sizeof(math::float3)
            )
        );

        // 4. 更新 RenderableManager（需要重建 Renderable）
        // 这比较复杂，通常建议使用 Morph Targets 或 Vertex Shader
    }

private:
    std::map<Entity, std::vector<math::float3>> mOriginalPositions;
    Engine* mEngine;
};
```

### 2. 材质变体

```cpp
class MaterialVariantGenerator {
public:
    void createVariant(FilamentAsset* asset, const math::float4& newColor) {
        auto materialInstances = asset->getMaterialInstances();
        size_t count = asset->getMaterialInstanceCount();

        for (size_t i = 0; i < count; i++) {
            MaterialInstance* mi = materialInstances[i];

            // 修改材质参数
            mi->setParameter("baseColorFactor", newColor);

            // 或创建新材质实例
            MaterialInstance* variant = mi->getMaterial()->createInstance();
            variant->setParameter("baseColorFactor", newColor);

            // 应用到不同实例
            // ...
        }
    }
};

// 使用示例：生成不同颜色的角色变体
MaterialVariantGenerator generator;
generator.createVariant(characterAsset, math::float4{1, 0, 0, 1});  // 红色
generator.createVariant(characterAsset, math::float4{0, 1, 0, 1});  // 绿色
generator.createVariant(characterAsset, math::float4{0, 0, 1, 1});  // 蓝色
```

---

## 调试与验证

### 1. glTF Validator

**命令行验证：**
```bash
# 安装
npm install -g gltf-validator

# 验证文件
gltf-validator model.glb

# 详细报告
gltf-validator model.glb -r report.json -a

# 查看报告
cat report.json | jq .

# 常见错误
cat report.json | jq '.issues.messages[] | select(.severity > 0)'
```

### 2. Filament 调试工具

**启用详细日志：**
```cpp
#include <utils/Log.h>

// 设置日志级别
utils::slog.setLevel(utils::slog.Level::DEBUG);

// AssetLoader 调试
FilamentAsset* asset = assetLoader->createAsset(data, size);
if (!asset) {
    utils::slog.e << "Failed to load asset" << utils::io::endl;
} else {
    utils::slog.i << "Loaded asset: "
                  << asset->getEntityCount() << " entities, "
                  << asset->getMaterialInstanceCount() << " materials"
                  << utils::io::endl;
}

// 打印所有实体名称
auto& ncm = engine->getNameComponentManager();
for (size_t i = 0; i < asset->getEntityCount(); i++) {
    Entity e = asset->getEntities()[i];
    if (ncm.hasComponent(e)) {
        auto ni = ncm.getInstance(e);
        const char* name = ncm.getName(ni);
        utils::slog.i << "Entity " << i << ": " << (name ? name : "<unnamed>") << utils::io::endl;
    }
}
```

### 3. 可视化调试

**渲染边界框：**
```cpp
#include <filament/DebugRegistry.h>

class DebugRenderer {
public:
    void drawBoundingBoxes(FilamentAsset* asset, Scene* scene) {
        auto& rcm = mEngine->getRenderableManager();
        auto& tcm = mEngine->getTransformManager();

        for (size_t i = 0; i < asset->getRenderableEntityCount(); i++) {
            Entity entity = asset->getRenderableEntities()[i];
            auto ri = rcm.getInstance(entity);
            auto ti = tcm.getInstance(entity);

            // 获取 AABB
            filament::Box aabb = rcm.getAxisAlignedBoundingBox(ri);
            math::mat4f transform = tcm.getWorldTransform(ti);

            // 绘制线框盒子
            drawWireframeBox(aabb, transform);
        }
    }

private:
    void drawWireframeBox(const filament::Box& box, const math::mat4f& transform) {
        // 使用 DebugRegistry 或自定义线框渲染
        // ...
    }

    Engine* mEngine;
};
```

---

## 生产流程集成

### 1. CI/CD 自动化

**GitHub Actions 示例：**
```yaml
name: glTF Asset Pipeline

on:
  push:
    paths:
      - 'assets/models/**/*.blend'

jobs:
  export-and-optimize:
    runs-on: ubuntu-latest

    steps:
      - uses: actions/checkout@v2

      - name: Install Blender
        run: |
          sudo snap install blender --classic

      - name: Install gltfpack
        run: |
          wget https://github.com/zeux/meshoptimizer/releases/download/v0.18/gltfpack
          chmod +x gltfpack
          sudo mv gltfpack /usr/local/bin/

      - name: Export glTF from Blender
        run: |
          for f in assets/models/**/*.blend; do
            blender "$f" --background --python scripts/export_gltf.py
          done

      - name: Optimize glTF files
        run: |
          for f in assets/models/**/*.glb; do
            gltfpack -i "$f" -o "${f%.glb}_optimized.glb" -cc -tc -mi
          done

      - name: Validate glTF
        run: |
          npm install -g gltf-validator
          for f in assets/models/**/*_optimized.glb; do
            gltf-validator "$f" || exit 1
          done

      - name: Upload artifacts
        uses: actions/upload-artifact@v2
        with:
          name: optimized-models
          path: assets/models/**/*_optimized.glb
```

### 2. 资产版本管理

**Git LFS 配置：**
```bash
# 安装 Git LFS
git lfs install

# 跟踪 glTF 文件
git lfs track "*.glb"
git lfs track "*.gltf"
git lfs track "*.bin"
git lfs track "*.ktx2"

# 提交 .gitattributes
git add .gitattributes
git commit -m "Track glTF assets with Git LFS"
```

### 3. 资产数据库

**资产元数据管理：**
```cpp
#include <sqlite3.h>

class AssetDatabase {
public:
    struct AssetMetadata {
        std::string path;
        std::string name;
        size_t fileSize;
        int triangleCount;
        int materialCount;
        std::string lastModified;
    };

    void indexAsset(const std::string& path, FilamentAsset* asset) {
        AssetMetadata meta;
        meta.path = path;
        meta.name = extractFileName(path);
        meta.fileSize = getFileSize(path);
        meta.triangleCount = calculateTriangleCount(asset);
        meta.materialCount = asset->getMaterialInstanceCount();
        meta.lastModified = getCurrentTimestamp();

        // 插入数据库
        const char* sql = "INSERT INTO assets (path, name, file_size, triangle_count, material_count, last_modified) "
                          "VALUES (?, ?, ?, ?, ?, ?)";

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(mDb, sql, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, meta.path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, meta.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, meta.fileSize);
        sqlite3_bind_int(stmt, 4, meta.triangleCount);
        sqlite3_bind_int(stmt, 5, meta.materialCount);
        sqlite3_bind_text(stmt, 6, meta.lastModified.c_str(), -1, SQLITE_TRANSIENT);

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    std::vector<AssetMetadata> searchAssets(const std::string& query) {
        // 搜索资产
        // ...
        return {};
    }

private:
    sqlite3* mDb;

    int calculateTriangleCount(FilamentAsset* asset) {
        // 计算总三角形数
        int total = 0;
        // ... 实现
        return total;
    }
};
```

---

## 常见问题与解决

### 1. 加载失败

**问题：** `AssetLoader::createAsset()` 返回 `nullptr`

**原因与解决：**
```cpp
// 1. 检查文件数据
if (data.empty()) {
    utils::slog.e << "Empty file data" << utils::io::endl;
}

// 2. 验证 glTF 格式
// 使用 gltf-validator 检查文件

// 3. 检查扩展支持
// 如果文件使用了 Filament 不支持的扩展且标记为 required

// 4. 检查 MaterialProvider
if (!materialProvider) {
    utils::slog.e << "MaterialProvider is null" << utils::io::endl;
}
```

### 2. 纹理丢失

**问题：** 模型加载成功但纹理全黑或全白

**解决：**
```cpp
// 1. 检查资源加载
resourceLoader->loadResources(asset);

// 等待加载完成
while (resourceLoader->asyncGetLoadProgress() < 1.0f) {
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
}

// 2. 检查纹理路径（对于 glTF，不是 GLB）
// 确保相对路径正确

// 3. 检查纹理格式支持
// KTX2 需要 ktxreader 库

// 4. 手动加载纹理
auto materialInstances = asset->getMaterialInstances();
for (size_t i = 0; i < asset->getMaterialInstanceCount(); i++) {
    MaterialInstance* mi = materialInstances[i];
    // 检查纹理参数
    if (mi->getMaterial()->hasParameter("baseColorMap")) {
        // 手动加载纹理
        // ...
    }
}
```

### 3. 动画不播放

**问题：** 动画加载成功但模型不动

**解决：**
```cpp
// 1. 检查 Animator
Animator* animator = asset->getAnimator();
if (!animator) {
    utils::slog.e << "No animator in asset" << utils::io::endl;
}

// 2. 检查动画数量
size_t animCount = animator->getAnimationCount();
if (animCount == 0) {
    utils::slog.e << "No animations found" << utils::io::endl;
}

// 3. 应用动画
animator->applyAnimation(0, time);

// 4. 必须调用 updateBoneMatrices()
animator->updateBoneMatrices();  // ← 经常忘记这一步！

// 5. 检查蒙皮是否正确导出
// 在 Blender 中确保：
// - Armature 修改器已应用到网格
// - 顶点权重正确
// - 导出时启用 export_skins=True
```

### 4. 性能问题

**问题：** 帧率低，渲染卡顿

**诊断与优化：**
```cpp
// 1. 检查三角形数量
size_t totalTriangles = 0;
auto& rcm = engine->getRenderableManager();
for (size_t i = 0; i < asset->getRenderableEntityCount(); i++) {
    auto ri = rcm.getInstance(asset->getRenderableEntities()[i]);
    // 统计三角形
}
utils::slog.i << "Total triangles: " << totalTriangles << utils::io::endl;

// 2. 检查 draw calls
// 使用 Filament 的性能分析工具

// 3. 优化建议
// - 使用 LOD
// - 启用视锥剔除
// - 合并材质
// - 使用实例化
// - 压缩纹理和几何
```

### 5. 内存泄漏

**问题：** 内存占用持续增长

**解决：**
```cpp
// 1. 正确销毁资产
assetLoader->destroyAsset(asset);  // ← 不要忘记！

// 2. 销毁实例
for (auto* instance : instances) {
    assetLoader->destroyInstance(instance);
}

// 3. 释放源数据
asset->releaseSourceData();

// 4. 销毁 loader
AssetLoader::destroy(&assetLoader);
delete resourceLoader;
delete materialProvider;

// 5. 检查循环引用
// 避免保存 FilamentAsset* 指针而不释放
```

---

## 总结

本文档涵盖了 glTF 在 Filament 中的高级应用：

- **多场景管理**: 切换和管理多个 glTF 场景
- **自定义扩展**: 添加应用特定的游戏数据
- **物理集成**: 与 Bullet 等物理引擎结合
- **音频集成**: 空间音频绑定到 3D 节点
- **程序化生成**: 运行时修改和生成变体
- **调试工具**: 验证、日志和可视化调试
- **生产流程**: CI/CD、版本管理、资产数据库
- **问题诊断**: 常见问题和解决方案

结合前面的文档，你现在应该能够：
1. 完整掌握 glTF 2.0 格式和扩展
2. 在 Blender 中正确导出和优化资产
3. 使用 Filament 高效加载和渲染
4. 集成到复杂的游戏或应用系统
5. 调试和解决各种问题
6. 建立生产级的资产管道

继续探索 Filament 和 glTF 的强大功能，创造出色的 3D 应用！
