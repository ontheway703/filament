# 完整示例代码

本文档提供 gltfio 从初始化到渲染的完整示例代码。

---

## 基础示例: 加载并渲染 glTF 模型

```cpp
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Camera.h>

#include <gltfio/AssetLoader.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/FilamentInstance.h>
#include <gltfio/Animator.h>

#include <gltfio/materials/uberarchive.h>
#include <image/KtxUtility.h>
#include <stb_image.h>

using namespace filament;
using namespace gltfio;

int main() {
    // ============================================
    // 1. 初始化 Filament
    // ============================================

    Engine* engine = Engine::create();
    SwapChain* swapChain = engine->createSwapChain(window);
    Renderer* renderer = engine->createRenderer();
    Scene* scene = engine->createScene();
    View* view = engine->createView();

    // 配置 View
    view->setScene(scene);

    // 创建相机
    Camera* camera = engine->createCamera(EntityManager::get().create());
    camera->setProjection(45.0, aspect, 0.1, 1000.0);
    camera->lookAt({0, 2, 10}, {0, 0, 0});
    view->setCamera(camera);

    // ============================================
    // 2. 创建材质提供者
    // ============================================

    // 使用内置的 uber shader
    MaterialProvider* materials = createUbershaderProvider(engine, UBERARCHIVE_DEFAULT_DATA,
                                                          UBERARCHIVE_DEFAULT_SIZE);

    // 或者使用 JIT shader (推荐)
    // MaterialProvider* materials = createJitShaderProvider(engine);

    // ============================================
    // 3. 创建纹理提供者
    // ============================================

    // STB 图像解码器 (PNG, JPEG, BMP, etc.)
    auto stbDecoder = createStbProvider(engine);

    // KTX2 解码器 (GPU 压缩纹理)
    auto ktx2Decoder = createKtx2Provider(engine);

    // ============================================
    // 4. 创建 AssetLoader
    // ============================================

    AssetLoader* assetLoader = AssetLoader::create({
        .engine = engine,
        .materials = materials,
        .names = nullptr  // 可选: 使用 NameComponentManager
    });

    // ============================================
    // 5. 加载 glTF 文件
    // ============================================

    // 从文件读取
    std::ifstream file("model.glb", std::ios::binary);
    std::vector<uint8_t> glbData((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());

    // 创建 Asset
    FilamentAsset* asset = assetLoader->createAsset(glbData.data(), glbData.size());

    if (!asset) {
        std::cerr << "Failed to load glTF asset" << std::endl;
        return -1;
    }

    // 释放 CPU 内存 (可选,但推荐)
    glbData.clear();

    // ============================================
    // 6. 加载资源
    // ============================================

    ResourceLoader resourceLoader({
        .engine = engine,
        .gltfPath = ".",           // glTF 文件所在目录
        .asyncCache = true         // 启用异步缓存
    });

    // 注册纹理解码器
    resourceLoader.addTextureProvider("image/png", stbDecoder);
    resourceLoader.addTextureProvider("image/jpeg", stbDecoder);
    resourceLoader.addTextureProvider("image/ktx2", ktx2Decoder);

    // 同步加载资源
    if (!resourceLoader.loadResources(asset)) {
        std::cerr << "Failed to load resources" << std::endl;
        return -1;
    }

    // ============================================
    // 7. 添加到场景
    // ============================================

    scene->addEntities(asset->getEntities(), asset->getEntityCount());

    // 可选: 设置资产位置
    auto& transformManager = engine->getTransformManager();
    auto rootTransform = transformManager.getInstance(asset->getRoot());
    transformManager.setTransform(rootTransform,
        filament::math::mat4f::translation({0, 0, 0}));

    // ============================================
    // 8. 获取 Animator
    // ============================================

    FilamentInstance* instance = asset->getInstance();
    Animator* animator = instance->getAnimator();

    if (animator && animator->getAnimationCount() > 0) {
        std::cout << "Animation count: " << animator->getAnimationCount() << std::endl;
        for (size_t i = 0; i < animator->getAnimationCount(); i++) {
            std::cout << "  [" << i << "] " << animator->getAnimationName(i)
                     << " (" << animator->getAnimationDuration(i) << "s)" << std::endl;
        }
    }

    // ============================================
    // 9. 渲染循环
    // ============================================

    float time = 0.0f;
    float deltaTime = 1.0f / 60.0f;

    while (!shouldQuit()) {
        // 更新动画
        if (animator) {
            animator->applyAnimation(0, time);  // 播放第 0 个动画
            animator->updateBoneMatrices();
            time += deltaTime;

            // 循环播放
            if (animator->getAnimationCount() > 0) {
                float duration = animator->getAnimationDuration(0);
                if (time > duration) {
                    time = 0.0f;
                }
            }
        }

        // 渲染
        if (renderer->beginFrame(swapChain)) {
            renderer->render(view);
            renderer->endFrame();
        }
    }

    // ============================================
    // 10. 清理
    // ============================================

    scene->removeEntities(asset->getEntities(), asset->getEntityCount());

    assetLoader->destroyAsset(asset);
    materials->destroyMaterials();

    delete materials;
    delete stbDecoder;
    delete ktx2Decoder;
    AssetLoader::destroy(&assetLoader);

    engine->destroy(scene);
    engine->destroy(view);
    engine->destroy(camera);
    engine->destroy(renderer);
    engine->destroy(swapChain);
    Engine::destroy(&engine);

    return 0;
}
```

---

## 高级示例: 多实例和材质变体

```cpp
#include <gltfio/AssetLoader.h>
#include <gltfio/ResourceLoader.h>

// 1. 加载资产
AssetLoader* loader = AssetLoader::create({engine, materials});
FilamentAsset* asset = loader->createAsset(glbData.data(), glbData.size());

ResourceLoader resLoader({engine, ".", true});
resLoader.addTextureProvider("image/png", stbDecoder);
resLoader.loadResources(asset);

// 2. 创建多个实例
FilamentInstance* instance1 = asset->getInstance();  // 默认实例
FilamentInstance* instance2 = loader->createInstance(asset);
FilamentInstance* instance3 = loader->createInstance(asset);

// 3. 设置不同位置
auto& tm = engine->getTransformManager();
tm.setTransform(tm.getInstance(instance1->getRoot()),
    mat4f::translation({0, 0, 0}));
tm.setTransform(tm.getInstance(instance2->getRoot()),
    mat4f::translation({5, 0, 0}));
tm.setTransform(tm.getInstance(instance3->getRoot()),
    mat4f::translation({-5, 0, 0}));

// 4. 应用不同的材质变体
if (instance2->getMaterialVariantCount() > 0) {
    std::cout << "Material variants:" << std::endl;
    for (size_t i = 0; i < instance2->getMaterialVariantCount(); i++) {
        std::cout << "  [" << i << "] " << instance2->getMaterialVariantName(i) << std::endl;
    }

    instance2->applyMaterialVariant(1);  // 应用第 1 个变体
}

// 5. 添加所有实例到场景
scene->addEntities(instance1->getEntities(), instance1->getEntityCount());
scene->addEntities(instance2->getEntities(), instance2->getEntityCount());
scene->addEntities(instance3->getEntities(), instance3->getEntityCount());

// 6. 独立动画控制
Animator* anim1 = instance1->getAnimator();
Animator* anim2 = instance2->getAnimator();
Animator* anim3 = instance3->getAnimator();

// 渲染循环
float time = 0.0f;
while (!quit) {
    anim1->applyAnimation(0, time);      // 动画 0
    anim2->applyAnimation(1, time * 2);  // 动画 1 (双速)
    anim3->applyAnimation(0, time * 0.5);// 动画 0 (半速)

    anim1->updateBoneMatrices();
    anim2->updateBoneMatrices();
    anim3->updateBoneMatrices();

    time += deltaTime;

    // 渲染
    renderer->beginFrame(swapChain);
    renderer->render(view);
    renderer->endFrame();
}
```

---

## 异步加载示例

```cpp
#include <gltfio/ResourceLoader.h>

// 1. 开始异步加载
ResourceLoader resourceLoader({engine, ".", true});
resourceLoader.addTextureProvider("image/png", stbDecoder);
resourceLoader.asyncBeginLoad(asset);

// 2. 渲染循环中更新加载进度
bool resourcesReady = false;

while (!quit) {
    if (!resourcesReady) {
        // 更新异步加载
        resourceLoader.asyncUpdateLoad();

        // 获取进度
        float progress = resourceLoader.asyncGetLoadProgress();
        std::cout << "Loading: " << (progress * 100) << "%" << std::endl;

        // 渐进式显示
        utils::Entity entity;
        while ((entity = asset->popRenderable())) {
            scene->addEntity(entity);  // 逐个添加准备好的实体
        }

        if (progress >= 1.0f) {
            resourcesReady = true;
            std::cout << "Resources loaded!" << std::endl;

            // 创建 Animator
            FilamentInstance* instance = asset->getInstance();
            Animator* animator = instance->getAnimator();
        }
    }

    // 渲染
    renderer->beginFrame(swapChain);
    renderer->render(view);
    renderer->endFrame();
}
```

---

## 动画混合示例

```cpp
// 交叉淡入淡出 (Cross-Fade)
float fadeTime = 0.0f;
float fadeDuration = 0.5f;
size_t currentAnim = 0;
size_t nextAnim = 1;
float currentTime = 0.0f;

while (!quit) {
    if (fadeTime < fadeDuration) {
        // 混合两个动画
        float alpha = fadeTime / fadeDuration;

        animator->applyAnimation(currentAnim, currentTime);
        animator->applyCrossFade(nextAnim, 0.0f, alpha);

        fadeTime += deltaTime;
    } else {
        // 淡入完成,播放新动画
        animator->applyAnimation(nextAnim, currentTime);
    }

    animator->updateBoneMatrices();
    currentTime += deltaTime;

    // 渲染
    renderer->beginFrame(swapChain);
    renderer->render(view);
    renderer->endFrame();
}
```

---

## 蒙皮动态绑定示例

```cpp
// 动态附加蒙皮
FilamentInstance* instance = asset->getInstance();

// 获取蒙皮信息
size_t skinCount = instance->getSkinCount();
for (size_t i = 0; i < skinCount; i++) {
    const char* skinName = instance->getSkinNameAt(i);
    size_t jointCount = instance->getJointCountAt(i);
    const utils::Entity* joints = instance->getJointsAt(i);

    std::cout << "Skin [" << i << "] " << skinName
             << " (" << jointCount << " joints)" << std::endl;
}

// 找到需要蒙皮的网格
utils::Entity meshEntity = asset->getFirstEntityByName("Body");

// 附加蒙皮
instance->attachSkin(0, meshEntity);

// 渲染循环
while (!quit) {
    animator->applyAnimation(0, time);
    animator->updateBoneMatrices();  // 计算蒙皮矩阵

    // 渲染
    renderer->beginFrame(swapChain);
    renderer->render(view);
    renderer->endFrame();

    time += deltaTime;
}

// 分离蒙皮
instance->detachSkin(0, meshEntity);
```

---

## 材质参数动态修改示例

```cpp
// 获取材质实例
FilamentInstance* instance = asset->getInstance();
MaterialInstance* const* materials = instance->getMaterialInstances();
size_t materialCount = instance->getMaterialInstanceCount();

// 修改第一个材质的参数
if (materialCount > 0) {
    MaterialInstance* mi = materials[0];

    // 修改基础颜色
    mi->setParameter("baseColorFactor",
        filament::math::float4{1.0f, 0.0f, 0.0f, 1.0f});  // 红色

    // 修改金属度和粗糙度
    mi->setParameter("metallicFactor", 1.0f);
    mi->setParameter("roughnessFactor", 0.3f);

    // 修改自发光
    mi->setParameter("emissiveFactor",
        filament::math::float3{1.0f, 1.0f, 1.0f});
}

// 动态修改 (渲染循环)
float time = 0.0f;
while (!quit) {
    // 脉动自发光
    float emissive = (std::sin(time * 2.0f) + 1.0f) * 0.5f;
    materials[0]->setParameter("emissiveFactor",
        filament::math::float3{emissive, emissive, emissive});

    time += deltaTime;

    // 渲染
    renderer->beginFrame(swapChain);
    renderer->render(view);
    renderer->endFrame();
}
```

---

## 场景查询示例

```cpp
// 按名称查找实体
utils::Entity entity = asset->getFirstEntityByName("Head");
if (entity) {
    std::cout << "Found entity: Head" << std::endl;
}

// 获取所有同名实体
utils::Entity entities[10];
size_t count = asset->getEntitiesByName("Wheel", entities, 10);
std::cout << "Found " << count << " entities named 'Wheel'" << std::endl;

// 按前缀查找
utils::Entity prefixEntities[20];
count = asset->getEntitiesByPrefix("Bone_", prefixEntities, 20);
std::cout << "Found " << count << " bone entities" << std::endl;

// 获取实体名称
for (size_t i = 0; i < asset->getEntityCount(); i++) {
    utils::Entity e = asset->getEntities()[i];
    const char* name = asset->getName(e);
    if (name) {
        std::cout << "Entity [" << i << "]: " << name << std::endl;
    }
}

// 获取 extras 信息
utils::Entity e = asset->getFirstEntityByName("CustomObject");
const char* extras = asset->getExtras(e);
if (extras) {
    std::cout << "Extras: " << extras << std::endl;
}
```

---

## Morph Target (变形目标) 示例

```cpp
// 查询变形目标
utils::Entity entity = asset->getFirstEntityByName("Face");
size_t morphCount = asset->getMorphTargetCountAt(entity);

std::cout << "Morph targets: " << morphCount << std::endl;
for (size_t i = 0; i < morphCount; i++) {
    const char* name = asset->getMorphTargetNameAt(entity, i);
    std::cout << "  [" << i << "] " << name << std::endl;
}

// 动态修改权重
auto& rm = engine->getRenderableManager();
auto instance = rm.getInstance(entity);

float weights[morphCount];
float time = 0.0f;

while (!quit) {
    // 动画变形 (0: smile, 1: blink)
    weights[0] = (std::sin(time) + 1.0f) * 0.5f;       // smile
    weights[1] = (std::cos(time * 3) + 1.0f) * 0.5f;  // blink

    rm.setMorphWeights(instance, weights, morphCount);

    time += deltaTime;

    // 渲染
    renderer->beginFrame(swapChain);
    renderer->render(view);
    renderer->endFrame();
}
```

---

## 完整生产级示例

```cpp
class GLTFViewer {
public:
    GLTFViewer(Engine* engine) : mEngine(engine) {
        // 创建场景
        mScene = mEngine->createScene();
        mView = mEngine->createView();
        mCamera = mEngine->createCamera(EntityManager::get().create());
        mRenderer = mEngine->createRenderer();

        mView->setScene(mScene);
        mView->setCamera(mCamera);

        // 创建提供者
        mMaterials = createJitShaderProvider(mEngine);
        mStbDecoder = createStbProvider(mEngine);

        // 创建 AssetLoader
        mAssetLoader = AssetLoader::create({mEngine, mMaterials});
    }

    ~GLTFViewer() {
        if (mAsset) {
            mScene->removeEntities(mAsset->getEntities(), mAsset->getEntityCount());
            mAssetLoader->destroyAsset(mAsset);
        }

        mMaterials->destroyMaterials();
        delete mMaterials;
        delete mStbDecoder;
        AssetLoader::destroy(&mAssetLoader);

        mEngine->destroy(mScene);
        mEngine->destroy(mView);
        mEngine->destroy(mCamera);
        mEngine->destroy(mRenderer);
    }

    bool loadAsset(const char* path) {
        // 读取文件
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return false;
        }

        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());

        // 创建 Asset
        mAsset = mAssetLoader->createAsset(data.data(), data.size());
        if (!mAsset) {
            return false;
        }

        // 加载资源
        ResourceLoader loader({mEngine, getDirectory(path), true});
        loader.addTextureProvider("image/png", mStbDecoder);
        loader.addTextureProvider("image/jpeg", mStbDecoder);

        if (!loader.loadResources(mAsset)) {
            return false;
        }

        // 添加到场景
        mScene->addEntities(mAsset->getEntities(), mAsset->getEntityCount());

        // 居中模型
        centerAsset();

        // 获取 Animator
        mAnimator = mAsset->getInstance()->getAnimator();

        return true;
    }

    void setAnimation(size_t index) {
        mCurrentAnim = index;
        mAnimTime = 0.0f;
    }

    void update(float deltaTime) {
        if (mAnimator && mCurrentAnim < mAnimator->getAnimationCount()) {
            mAnimator->applyAnimation(mCurrentAnim, mAnimTime);
            mAnimator->updateBoneMatrices();

            mAnimTime += deltaTime;

            float duration = mAnimator->getAnimationDuration(mCurrentAnim);
            if (mAnimTime > duration) {
                mAnimTime = 0.0f;
            }
        }
    }

    void render(SwapChain* swapChain) {
        if (mRenderer->beginFrame(swapChain)) {
            mRenderer->render(mView);
            mRenderer->endFrame();
        }
    }

    Renderer* getRenderer() { return mRenderer; }
    View* getView() { return mView; }
    Camera* getCamera() { return mCamera; }
    FilamentAsset* getAsset() { return mAsset; }

private:
    void centerAsset() {
        auto& tm = mEngine->getTransformManager();
        Aabb aabb = mAsset->getBoundingBox();
        filament::math::float3 center = (aabb.min + aabb.max) * 0.5f;

        auto rootTransform = tm.getInstance(mAsset->getRoot());
        tm.setTransform(rootTransform, mat4f::translation(-center));
    }

    std::string getDirectory(const char* path) {
        std::string s(path);
        size_t pos = s.find_last_of("/\\");
        return (pos == std::string::npos) ? "." : s.substr(0, pos);
    }

    Engine* mEngine;
    Scene* mScene;
    View* mView;
    Camera* mCamera;
    Renderer* mRenderer;

    MaterialProvider* mMaterials;
    TextureProvider* mStbDecoder;
    AssetLoader* mAssetLoader;
    FilamentAsset* mAsset = nullptr;
    Animator* mAnimator = nullptr;

    size_t mCurrentAnim = 0;
    float mAnimTime = 0.0f;
};

// 使用
int main() {
    Engine* engine = Engine::create();
    SwapChain* swapChain = engine->createSwapChain(window);

    GLTFViewer viewer(engine);

    if (!viewer.loadAsset("model.glb")) {
        return -1;
    }

    // 设置相机
    viewer.getCamera()->lookAt({0, 2, 5}, {0, 0, 0});

    // 渲染循环
    float deltaTime = 1.0f / 60.0f;
    while (!quit) {
        viewer.update(deltaTime);
        viewer.render(swapChain);
    }

    engine->destroy(swapChain);
    Engine::destroy(&engine);
    return 0;
}
```

---

## 常见模式总结

### 1. 基本加载流程

```cpp
AssetLoader → createAsset → ResourceLoader::loadResources → addToScene
```

### 2. 多实例模式

```cpp
Asset → createInstance × N → 独立位置/动画
```

### 3. 异步加载模式

```cpp
asyncBeginLoad → asyncUpdateLoad (轮询) → popRenderable (渐进式)
```

### 4. 动画控制模式

```cpp
applyAnimation → updateBoneMatrices → render
```

### 5. 材质修改模式

```cpp
getMaterialInstances → setParameter → 动态效果
```
