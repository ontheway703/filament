# Complete Game Example - 完整游戏示例项目

## 概述

本示例展示如何使用 Filament 渲染引擎构建一个完整的 3D 游戏项目。该示例整合了前面所有章节介绍的技术：

- **动画系统**（06-animation-player）：角色动画播放和状态机
- **场景管理**（07-scene-manager）：场景图和资源管理
- **UI 渲染**（08-ui-rendering）：游戏界面和 HUD
- **后处理**（09-post-processing）：视觉效果增强

我们将创建一个简单的第三人称动作游戏，包含角色控制、相机系统、游戏逻辑、输入处理等完整功能。

## 功能特性

### 核心系统
- **游戏循环**：固定时间步长的主循环
- **输入系统**：键盘/鼠标/触屏输入处理
- **状态管理**：游戏状态机（菜单/游戏/暂停/结束）
- **相机系统**：第三人称跟随相机
- **物理系统**：简单的碰撞检测和物理模拟

### 渲染功能
- **PBR 材质**：物理基于渲染的材质系统
- **动态光照**：方向光、点光源、聚光灯
- **阴影系统**：级联阴影贴图
- **后处理链**：Bloom、SSAO、色调映射
- **天空盒**：IBL 环境光照

### 游戏玩法
- **角色控制**：WASD 移动、空格跳跃
- **动画系统**：待机、行走、跑步、跳跃动画
- **收集系统**：收集场景中的道具
- **得分系统**：分数统计和显示
- **UI 系统**：主菜单、HUD、暂停菜单

## 完整 C++ 实现

### 主游戏类定义

```cpp
// CompleteGameApp.h
#ifndef COMPLETE_GAME_APP_H
#define COMPLETE_GAME_APP_H

#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Renderer.h>
#include <filament/Camera.h>
#include <filament/Skybox.h>
#include <filament/IndirectLight.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <filament/LightManager.h>
#include <utils/EntityManager.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/Animator.h>
#include <math/vec3.h>
#include <math/mat4.h>
#include <math/quat.h>

#include <vector>
#include <memory>
#include <unordered_map>
#include <chrono>

using namespace filament;
using namespace filament::math;

/**
 * 游戏状态枚举
 */
enum class GameState {
    MAIN_MENU,      // 主菜单
    PLAYING,        // 游戏中
    PAUSED,         // 暂停
    GAME_OVER       // 游戏结束
};

/**
 * 输入状态结构
 */
struct InputState {
    bool moveForward = false;
    bool moveBackward = false;
    bool moveLeft = false;
    bool moveRight = false;
    bool jump = false;
    bool run = false;

    float2 mouseDelta = {0.0f, 0.0f};
    float2 touchPosition = {0.0f, 0.0f};
    bool touchActive = false;
};

/**
 * 角色控制器
 * 处理角色移动、跳跃、动画等
 */
class CharacterController {
public:
    CharacterController(Engine* engine, Scene* scene);
    ~CharacterController();

    /**
     * 加载角色模型
     */
    bool loadCharacter(const std::string& modelPath);

    /**
     * 更新角色状态
     */
    void update(float deltaTime, const InputState& input);

    /**
     * 获取角色位置
     */
    float3 getPosition() const { return mPosition; }

    /**
     * 获取角色朝向
     */
    float3 getForward() const;

    /**
     * 设置角色位置
     */
    void setPosition(const float3& position);

    /**
     * 角色跳跃
     */
    void jump();

    /**
     * 检查角色是否在地面
     */
    bool isGrounded() const { return mIsGrounded; }

private:
    /**
     * 更新移动
     */
    void updateMovement(float deltaTime, const InputState& input);

    /**
     * 更新动画
     */
    void updateAnimation(float deltaTime);

    /**
     * 更新物理
     */
    void updatePhysics(float deltaTime);

    /**
     * 更新变换矩阵
     */
    void updateTransform();

private:
    Engine* mEngine;
    Scene* mScene;

    // 角色资源
    gltfio::FilamentAsset* mAsset = nullptr;
    gltfio::Animator* mAnimator = nullptr;

    // 变换
    float3 mPosition = {0.0f, 0.0f, 0.0f};
    float3 mVelocity = {0.0f, 0.0f, 0.0f};
    float mRotationY = 0.0f; // 角色 Y 轴旋转角度

    // 物理参数
    float mMoveSpeed = 5.0f;        // 行走速度
    float mRunSpeed = 10.0f;        // 奔跑速度
    float mJumpForce = 8.0f;        // 跳跃力
    float mGravity = -20.0f;        // 重力加速度
    bool mIsGrounded = true;        // 是否在地面

    // 动画状态
    enum class AnimState {
        IDLE,
        WALK,
        RUN,
        JUMP
    };
    AnimState mCurrentAnimState = AnimState::IDLE;
    size_t mIdleAnimIndex = 0;
    size_t mWalkAnimIndex = 1;
    size_t mRunAnimIndex = 2;
    size_t mJumpAnimIndex = 3;
};

/**
 * 相机控制器
 * 实现第三人称跟随相机
 */
class CameraController {
public:
    CameraController(Camera* camera);

    /**
     * 更新相机
     */
    void update(float deltaTime, const float3& targetPosition, const InputState& input);

    /**
     * 设置相机参数
     */
    void setDistance(float distance) { mDistance = distance; }
    void setHeight(float height) { mHeight = height; }
    void setSensitivity(float sensitivity) { mSensitivity = sensitivity; }

    /**
     * 获取相机前方向（水平）
     */
    float3 getForwardDirection() const;

    /**
     * 获取相机右方向
     */
    float3 getRightDirection() const;

private:
    Camera* mCamera;

    // 相机参数
    float mDistance = 8.0f;         // 与目标的距离
    float mHeight = 3.0f;           // 相机高度偏移
    float mPitch = -20.0f;          // 俯仰角（度）
    float mYaw = 0.0f;              // 偏航角（度）
    float mSensitivity = 0.2f;      // 鼠标灵敏度

    // 相机约束
    float mMinPitch = -60.0f;
    float mMaxPitch = 20.0f;

    // 平滑参数
    float mSmoothSpeed = 10.0f;
    float3 mCurrentPosition = {0.0f, 0.0f, 0.0f};
};

/**
 * 收集物品类
 */
class Collectible {
public:
    Collectible(utils::Entity entity, const float3& position, float value)
        : mEntity(entity), mPosition(position), mValue(value), mActive(true) {}

    utils::Entity getEntity() const { return mEntity; }
    float3 getPosition() const { return mPosition; }
    float getValue() const { return mValue; }
    bool isActive() const { return mActive; }

    void collect() { mActive = false; }

    /**
     * 更新旋转动画
     */
    void update(float deltaTime) {
        mRotation += deltaTime * 2.0f; // 每秒旋转 2 弧度
    }

    float getRotation() const { return mRotation; }

private:
    utils::Entity mEntity;
    float3 mPosition;
    float mValue;
    bool mActive;
    float mRotation = 0.0f;
};

/**
 * 游戏管理器
 * 管理游戏逻辑、分数、收集物等
 */
class GameManager {
public:
    GameManager();

    /**
     * 初始化游戏
     */
    void initialize();

    /**
     * 重置游戏
     */
    void reset();

    /**
     * 更新游戏逻辑
     */
    void update(float deltaTime, CharacterController* character);

    /**
     * 添加收集物
     */
    void addCollectible(std::unique_ptr<Collectible> collectible);

    /**
     * 获取分数
     */
    float getScore() const { return mScore; }

    /**
     * 获取活跃的收集物
     */
    const std::vector<std::unique_ptr<Collectible>>& getCollectibles() const {
        return mCollectibles;
    }

    /**
     * 获取游戏时间
     */
    float getGameTime() const { return mGameTime; }

private:
    /**
     * 检测收集物碰撞
     */
    void checkCollectibles(CharacterController* character);

private:
    float mScore = 0.0f;
    float mGameTime = 0.0f;
    float mCollectionRadius = 1.5f; // 收集半径

    std::vector<std::unique_ptr<Collectible>> mCollectibles;
};

/**
 * UI 管理器
 * 管理游戏 UI 显示
 */
class UIManager {
public:
    UIManager();
    ~UIManager();

    /**
     * 初始化 UI
     */
    void initialize(Engine* engine, Scene* scene);

    /**
     * 渲染 UI
     */
    void render(GameState gameState, float score, float gameTime);

    /**
     * 渲染主菜单
     */
    void renderMainMenu();

    /**
     * 渲染 HUD
     */
    void renderHUD(float score, float gameTime);

    /**
     * 渲染暂停菜单
     */
    void renderPauseMenu();

    /**
     * 渲染游戏结束界面
     */
    void renderGameOver(float finalScore);

private:
    Engine* mEngine = nullptr;
    Scene* mScene = nullptr;

    // UI 实体
    utils::Entity mUIEntity;
};

/**
 * 后处理管理器
 */
class PostProcessManager {
public:
    PostProcessManager(Engine* engine);
    ~PostProcessManager();

    /**
     * 初始化后处理
     */
    void initialize(uint32_t width, uint32_t height);

    /**
     * 应用后处理效果
     */
    void apply(View* view);

    /**
     * 启用/禁用效果
     */
    void setBloomEnabled(bool enabled) { mBloomEnabled = enabled; }
    void setSSAOEnabled(bool enabled) { mSSAOEnabled = enabled; }
    void setTonemappingEnabled(bool enabled) { mTonemappingEnabled = enabled; }

private:
    Engine* mEngine;

    bool mBloomEnabled = true;
    bool mSSAOEnabled = true;
    bool mTonemappingEnabled = true;

    // 后处理纹理
    Texture* mColorTexture = nullptr;
    Texture* mDepthTexture = nullptr;
};

/**
 * 完整游戏应用类
 * 整合所有系统
 */
class CompleteGameApp {
public:
    CompleteGameApp();
    ~CompleteGameApp();

    /**
     * 初始化应用
     */
    bool initialize(void* nativeWindow, uint32_t width, uint32_t height);

    /**
     * 清理资源
     */
    void cleanup();

    /**
     * 主循环更新
     */
    void update(float deltaTime);

    /**
     * 渲染帧
     */
    void render();

    /**
     * 处理输入
     */
    void handleInput(const InputState& input);

    /**
     * 窗口尺寸改变
     */
    void resize(uint32_t width, uint32_t height);

    /**
     * 状态控制
     */
    void startGame();
    void pauseGame();
    void resumeGame();
    void quitGame();

private:
    /**
     * 创建渲染资源
     */
    bool createRenderResources();

    /**
     * 加载场景资源
     */
    bool loadSceneAssets();

    /**
     * 设置光照
     */
    void setupLighting();

    /**
     * 设置天空盒
     */
    void setupSkybox();

    /**
     * 创建地面
     */
    void createGround();

    /**
     * 生成收集物
     */
    void spawnCollectibles();

    /**
     * 更新游戏逻辑
     */
    void updateGameLogic(float deltaTime);

    /**
     * 更新渲染
     */
    void updateRendering(float deltaTime);

private:
    // Filament 核心对象
    Engine* mEngine = nullptr;
    Renderer* mRenderer = nullptr;
    Scene* mScene = nullptr;
    View* mView = nullptr;
    Camera* mCamera = nullptr;
    SwapChain* mSwapChain = nullptr;

    // 资源加载器
    gltfio::AssetLoader* mAssetLoader = nullptr;
    gltfio::ResourceLoader* mResourceLoader = nullptr;

    // 天空盒和环境光
    Skybox* mSkybox = nullptr;
    IndirectLight* mIndirectLight = nullptr;

    // 游戏系统
    std::unique_ptr<CharacterController> mCharacter;
    std::unique_ptr<CameraController> mCameraController;
    std::unique_ptr<GameManager> mGameManager;
    std::unique_ptr<UIManager> mUIManager;
    std::unique_ptr<PostProcessManager> mPostProcessManager;

    // 输入状态
    InputState mInputState;

    // 游戏状态
    GameState mGameState = GameState::MAIN_MENU;

    // 窗口尺寸
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;

    // 场景实体
    utils::Entity mGroundEntity;
    std::vector<utils::Entity> mSceneEntities;

    // 光源实体
    utils::Entity mSunLight;
    std::vector<utils::Entity> mPointLights;
};

#endif // COMPLETE_GAME_APP_H
```

### 角色控制器实现

```cpp
// CharacterController.cpp
#include "CompleteGameApp.h"
#include <iostream>

CharacterController::CharacterController(Engine* engine, Scene* scene)
    : mEngine(engine), mScene(scene) {
}

CharacterController::~CharacterController() {
    if (mAsset) {
        mScene->removeEntities(mAsset->getEntities(), mAsset->getEntityCount());
        mAsset->releaseSourceData();
        delete mAnimator;
        delete mAsset;
    }
}

bool CharacterController::loadCharacter(const std::string& modelPath) {
    // 创建资源加载器
    gltfio::AssetLoader* assetLoader = gltfio::AssetLoader::create({mEngine});

    // 加载 glTF 模型
    // 实际项目中需要从文件读取数据
    // 这里假设有加载函数
    // mAsset = assetLoader->createAssetFromFile(modelPath);

    if (!mAsset) {
        std::cerr << "Failed to load character model: " << modelPath << std::endl;
        delete assetLoader;
        return false;
    }

    // 创建动画器
    mAnimator = new gltfio::Animator(mAsset);

    // 添加到场景
    mScene->addEntities(mAsset->getEntities(), mAsset->getEntityCount());

    delete assetLoader;
    return true;
}

void CharacterController::update(float deltaTime, const InputState& input) {
    updateMovement(deltaTime, input);
    updatePhysics(deltaTime);
    updateAnimation(deltaTime);
    updateTransform();
}

void CharacterController::updateMovement(float deltaTime, const InputState& input) {
    // 计算移动方向（相对于相机）
    float3 moveDirection = {0.0f, 0.0f, 0.0f};

    if (input.moveForward) {
        moveDirection.z -= 1.0f;
    }
    if (input.moveBackward) {
        moveDirection.z += 1.0f;
    }
    if (input.moveLeft) {
        moveDirection.x -= 1.0f;
    }
    if (input.moveRight) {
        moveDirection.x += 1.0f;
    }

    // 标准化移动方向
    float length = std::sqrt(moveDirection.x * moveDirection.x +
                            moveDirection.z * moveDirection.z);
    if (length > 0.0f) {
        moveDirection.x /= length;
        moveDirection.z /= length;

        // 更新角色朝向
        mRotationY = std::atan2(moveDirection.x, moveDirection.z);
    }

    // 应用移动速度
    float currentSpeed = input.run ? mRunSpeed : mMoveSpeed;
    mVelocity.x = moveDirection.x * currentSpeed;
    mVelocity.z = moveDirection.z * currentSpeed;
}

void CharacterController::updatePhysics(float deltaTime) {
    // 应用重力
    if (!mIsGrounded) {
        mVelocity.y += mGravity * deltaTime;
    }

    // 更新位置
    mPosition += mVelocity * deltaTime;

    // 简单的地面检测（假设地面在 y = 0）
    if (mPosition.y <= 0.0f) {
        mPosition.y = 0.0f;
        mVelocity.y = 0.0f;
        mIsGrounded = true;
    } else {
        mIsGrounded = false;
    }
}

void CharacterController::updateAnimation(float deltaTime) {
    if (!mAnimator) return;

    // 根据速度选择动画
    AnimState newState = AnimState::IDLE;

    float horizontalSpeed = std::sqrt(mVelocity.x * mVelocity.x +
                                     mVelocity.z * mVelocity.z);

    if (!mIsGrounded) {
        newState = AnimState::JUMP;
    } else if (horizontalSpeed > mRunSpeed * 0.8f) {
        newState = AnimState::RUN;
    } else if (horizontalSpeed > 0.1f) {
        newState = AnimState::WALK;
    } else {
        newState = AnimState::IDLE;
    }

    // 切换动画
    if (newState != mCurrentAnimState) {
        mCurrentAnimState = newState;

        size_t animIndex;
        switch (mCurrentAnimState) {
            case AnimState::IDLE: animIndex = mIdleAnimIndex; break;
            case AnimState::WALK: animIndex = mWalkAnimIndex; break;
            case AnimState::RUN: animIndex = mRunAnimIndex; break;
            case AnimState::JUMP: animIndex = mJumpAnimIndex; break;
        }

        if (animIndex < mAnimator->getAnimationCount()) {
            mAnimator->applyAnimation(animIndex);
        }
    }

    // 更新动画时间
    mAnimator->updateBoneMatrices();
}

void CharacterController::updateTransform() {
    if (!mAsset) return;

    auto& tcm = mEngine->getTransformManager();

    // 创建变换矩阵
    mat4f translation = mat4f::translation(mPosition);
    mat4f rotation = mat4f::rotation(mRotationY, float3{0.0f, 1.0f, 0.0f});
    mat4f transform = translation * rotation;

    // 应用到根实体
    auto instance = tcm.getInstance(mAsset->getRoot());
    tcm.setTransform(instance, transform);
}

float3 CharacterController::getForward() const {
    return float3{
        std::sin(mRotationY),
        0.0f,
        std::cos(mRotationY)
    };
}

void CharacterController::setPosition(const float3& position) {
    mPosition = position;
}

void CharacterController::jump() {
    if (mIsGrounded) {
        mVelocity.y = mJumpForce;
        mIsGrounded = false;
    }
}
```

### 相机控制器实现

```cpp
// CameraController.cpp
#include "CompleteGameApp.h"

CameraController::CameraController(Camera* camera)
    : mCamera(camera) {
}

void CameraController::update(float deltaTime, const float3& targetPosition,
                              const InputState& input) {
    // 更新旋转角度
    mYaw += input.mouseDelta.x * mSensitivity;
    mPitch -= input.mouseDelta.y * mSensitivity;

    // 限制俯仰角
    mPitch = std::max(mMinPitch, std::min(mMaxPitch, mPitch));

    // 计算相机位置
    float yawRad = mYaw * 3.14159f / 180.0f;
    float pitchRad = mPitch * 3.14159f / 180.0f;

    float3 offset;
    offset.x = mDistance * std::cos(pitchRad) * std::sin(yawRad);
    offset.y = mHeight + mDistance * std::sin(pitchRad);
    offset.z = mDistance * std::cos(pitchRad) * std::cos(yawRad);

    float3 desiredPosition = targetPosition + offset;

    // 平滑插值
    mCurrentPosition = mCurrentPosition +
        (desiredPosition - mCurrentPosition) * mSmoothSpeed * deltaTime;

    // 更新相机变换
    mCamera->lookAt(mCurrentPosition, targetPosition, {0.0f, 1.0f, 0.0f});
}

float3 CameraController::getForwardDirection() const {
    float yawRad = mYaw * 3.14159f / 180.0f;
    return float3{
        std::sin(yawRad),
        0.0f,
        std::cos(yawRad)
    };
}

float3 CameraController::getRightDirection() const {
    float yawRad = mYaw * 3.14159f / 180.0f;
    return float3{
        std::cos(yawRad),
        0.0f,
        -std::sin(yawRad)
    };
}
```

### 游戏管理器实现

```cpp
// GameManager.cpp
#include "CompleteGameApp.h"

GameManager::GameManager() {
}

void GameManager::initialize() {
    reset();
}

void GameManager::reset() {
    mScore = 0.0f;
    mGameTime = 0.0f;
    mCollectibles.clear();
}

void GameManager::update(float deltaTime, CharacterController* character) {
    mGameTime += deltaTime;

    // 更新收集物
    for (auto& collectible : mCollectibles) {
        if (collectible->isActive()) {
            collectible->update(deltaTime);
        }
    }

    // 检测收集
    checkCollectibles(character);
}

void GameManager::checkCollectibles(CharacterController* character) {
    float3 charPos = character->getPosition();

    for (auto& collectible : mCollectibles) {
        if (!collectible->isActive()) continue;

        float3 itemPos = collectible->getPosition();
        float distance = length(charPos - itemPos);

        if (distance < mCollectionRadius) {
            mScore += collectible->getValue();
            collectible->collect();
        }
    }
}

void GameManager::addCollectible(std::unique_ptr<Collectible> collectible) {
    mCollectibles.push_back(std::move(collectible));
}
```

### 主游戏类实现

```cpp
// CompleteGameApp.cpp
#include "CompleteGameApp.h"
#include <filament/Material.h>
#include <filament/Viewport.h>
#include <backend/PixelBufferDescriptor.h>
#include <iostream>

CompleteGameApp::CompleteGameApp() {
}

CompleteGameApp::~CompleteGameApp() {
    cleanup();
}

bool CompleteGameApp::initialize(void* nativeWindow, uint32_t width, uint32_t height) {
    mWidth = width;
    mHeight = height;

    // 创建 Filament 引擎
    mEngine = Engine::create();
    if (!mEngine) {
        std::cerr << "Failed to create Filament engine" << std::endl;
        return false;
    }

    // 创建交换链
    mSwapChain = mEngine->createSwapChain(nativeWindow);

    // 创建渲染器
    mRenderer = mEngine->createRenderer();

    // 创建场景和视图
    mScene = mEngine->createScene();
    mView = mEngine->createView();
    mView->setScene(mScene);

    // 创建相机
    auto& entityManager = utils::EntityManager::get();
    mCamera = mEngine->createCamera(entityManager.create());

    // 设置投影矩阵
    float aspect = (float)width / (float)height;
    mCamera->setProjection(45.0, aspect, 0.1, 100.0, Camera::Fov::VERTICAL);

    mView->setCamera(mCamera);
    mView->setViewport({0, 0, width, height});

    // 创建渲染资源
    if (!createRenderResources()) {
        return false;
    }

    // 加载场景资源
    if (!loadSceneAssets()) {
        return false;
    }

    // 设置光照和天空盒
    setupLighting();
    setupSkybox();

    // 创建地面
    createGround();

    // 初始化游戏系统
    mCharacter = std::make_unique<CharacterController>(mEngine, mScene);
    mCharacter->setPosition({0.0f, 0.0f, 0.0f});

    mCameraController = std::make_unique<CameraController>(mCamera);

    mGameManager = std::make_unique<GameManager>();
    mGameManager->initialize();

    mUIManager = std::make_unique<UIManager>();
    mUIManager->initialize(mEngine, mScene);

    mPostProcessManager = std::make_unique<PostProcessManager>(mEngine);
    mPostProcessManager->initialize(width, height);

    return true;
}

bool CompleteGameApp::createRenderResources() {
    // 创建资源加载器
    mAssetLoader = gltfio::AssetLoader::create({mEngine});
    if (!mAssetLoader) {
        std::cerr << "Failed to create asset loader" << std::endl;
        return false;
    }

    mResourceLoader = new gltfio::ResourceLoader({mEngine});

    return true;
}

bool CompleteGameApp::loadSceneAssets() {
    // 加载角色模型
    if (!mCharacter->loadCharacter("assets/models/character.glb")) {
        std::cerr << "Failed to load character model" << std::endl;
        return false;
    }

    return true;
}

void CompleteGameApp::setupLighting() {
    auto& entityManager = utils::EntityManager::get();

    // 创建太阳光（方向光）
    mSunLight = entityManager.create();

    LightManager::Builder(LightManager::Type::SUN)
        .color(Color::toLinear<ACCURATE>({1.0f, 0.95f, 0.9f}))
        .intensity(100000.0f)
        .direction({0.6f, -1.0f, -0.8f})
        .castShadows(true)
        .build(*mEngine, mSunLight);

    mScene->addEntity(mSunLight);

    // 添加几个点光源
    for (int i = 0; i < 3; i++) {
        utils::Entity light = entityManager.create();

        float3 position;
        float3 color;

        switch (i) {
            case 0:
                position = {5.0f, 2.0f, 5.0f};
                color = {1.0f, 0.8f, 0.6f};
                break;
            case 1:
                position = {-5.0f, 2.0f, 5.0f};
                color = {0.6f, 0.8f, 1.0f};
                break;
            case 2:
                position = {0.0f, 2.0f, -5.0f};
                color = {0.8f, 1.0f, 0.8f};
                break;
        }

        LightManager::Builder(LightManager::Type::POINT)
            .color(Color::toLinear<ACCURATE>(color))
            .intensity(100.0f)
            .position(position)
            .falloff(10.0f)
            .build(*mEngine, light);

        mScene->addEntity(light);
        mPointLights.push_back(light);
    }
}

void CompleteGameApp::setupSkybox() {
    // 创建简单的颜色天空盒
    // 实际项目中应该加载 HDR 环境贴图

    // 这里使用纯色天空盒作为示例
    Skybox::Builder skyboxBuilder;
    skyboxBuilder.color({0.5f, 0.7f, 1.0f, 1.0f});
    mSkybox = skyboxBuilder.build(*mEngine);
    mScene->setSkybox(mSkybox);

    // 创建环境光
    // 实际项目中应该从 IBL 贴图生成
    IndirectLight::Builder indirectLightBuilder;
    indirectLightBuilder.intensity(30000.0f);
    mIndirectLight = indirectLightBuilder.build(*mEngine);
    mScene->setIndirectLight(mIndirectLight);
}

void CompleteGameApp::createGround() {
    // 创建简单的地面平面
    // 实际项目中应该加载真实的地形模型

    auto& entityManager = utils::EntityManager::get();
    mGroundEntity = entityManager.create();

    // 这里简化处理，实际需要创建网格和材质
    // 创建 50x50 的地面平面

    mScene->addEntity(mGroundEntity);
}

void CompleteGameApp::spawnCollectibles() {
    // 生成随机分布的收集物
    auto& entityManager = utils::EntityManager::get();

    for (int i = 0; i < 10; i++) {
        // 随机位置
        float x = (rand() % 40) - 20.0f;
        float z = (rand() % 40) - 20.0f;
        float3 position = {x, 1.0f, z};

        // 创建实体
        utils::Entity entity = entityManager.create();

        // 创建收集物（这里简化，实际应该加载模型）
        auto collectible = std::make_unique<Collectible>(entity, position, 10.0f);
        mGameManager->addCollectible(std::move(collectible));

        mScene->addEntity(entity);
    }
}

void CompleteGameApp::update(float deltaTime) {
    // 限制最大时间步长，避免物理不稳定
    deltaTime = std::min(deltaTime, 0.033f); // 最大 33ms (30 FPS)

    switch (mGameState) {
        case GameState::MAIN_MENU:
            // 主菜单状态：只更新 UI
            break;

        case GameState::PLAYING:
            updateGameLogic(deltaTime);
            updateRendering(deltaTime);
            break;

        case GameState::PAUSED:
            // 暂停状态：不更新游戏逻辑
            break;

        case GameState::GAME_OVER:
            // 游戏结束：显示最终分数
            break;
    }
}

void CompleteGameApp::updateGameLogic(float deltaTime) {
    // 更新角色
    if (mCharacter) {
        mCharacter->update(deltaTime, mInputState);

        // 检查跳跃输入
        if (mInputState.jump) {
            mCharacter->jump();
            mInputState.jump = false; // 清除跳跃输入
        }
    }

    // 更新相机
    if (mCameraController && mCharacter) {
        mCameraController->update(deltaTime, mCharacter->getPosition(), mInputState);
    }

    // 更新游戏管理器
    if (mGameManager) {
        mGameManager->update(deltaTime, mCharacter.get());
    }

    // 清除每帧的鼠标增量
    mInputState.mouseDelta = {0.0f, 0.0f};
}

void CompleteGameApp::updateRendering(float deltaTime) {
    // 更新收集物的变换（旋转动画）
    auto& tcm = mEngine->getTransformManager();

    for (const auto& collectible : mGameManager->getCollectibles()) {
        if (collectible->isActive()) {
            auto instance = tcm.getInstance(collectible->getEntity());
            if (instance.isValid()) {
                float3 pos = collectible->getPosition();
                float rotation = collectible->getRotation();

                mat4f translation = mat4f::translation(pos);
                mat4f rotationMat = mat4f::rotation(rotation, float3{0.0f, 1.0f, 0.0f});
                tcm.setTransform(instance, translation * rotationMat);
            }
        }
    }
}

void CompleteGameApp::render() {
    // 开始帧
    if (mRenderer->beginFrame(mSwapChain)) {
        // 应用后处理
        if (mPostProcessManager) {
            mPostProcessManager->apply(mView);
        }

        // 渲染场景
        mRenderer->render(mView);

        // 渲染 UI
        if (mUIManager) {
            mUIManager->render(mGameState,
                              mGameManager->getScore(),
                              mGameManager->getGameTime());
        }

        // 结束帧
        mRenderer->endFrame();
    }
}

void CompleteGameApp::handleInput(const InputState& input) {
    mInputState = input;

    // 处理状态转换
    if (mGameState == GameState::MAIN_MENU) {
        // 按空格键开始游戏
        if (input.jump) {
            startGame();
        }
    } else if (mGameState == GameState::PLAYING) {
        // ESC 键暂停
        // 这里需要额外的按键检测逻辑
    }
}

void CompleteGameApp::resize(uint32_t width, uint32_t height) {
    mWidth = width;
    mHeight = height;

    // 更新视口
    mView->setViewport({0, 0, width, height});

    // 更新相机投影
    float aspect = (float)width / (float)height;
    mCamera->setProjection(45.0, aspect, 0.1, 100.0, Camera::Fov::VERTICAL);

    // 重新初始化后处理
    if (mPostProcessManager) {
        mPostProcessManager->initialize(width, height);
    }
}

void CompleteGameApp::startGame() {
    mGameState = GameState::PLAYING;
    mGameManager->reset();

    // 重置角色位置
    mCharacter->setPosition({0.0f, 0.0f, 0.0f});

    // 生成收集物
    spawnCollectibles();
}

void CompleteGameApp::pauseGame() {
    if (mGameState == GameState::PLAYING) {
        mGameState = GameState::PAUSED;
    }
}

void CompleteGameApp::resumeGame() {
    if (mGameState == GameState::PAUSED) {
        mGameState = GameState::PLAYING;
    }
}

void CompleteGameApp::quitGame() {
    mGameState = GameState::MAIN_MENU;
}

void CompleteGameApp::cleanup() {
    // 清理游戏系统
    mCharacter.reset();
    mCameraController.reset();
    mGameManager.reset();
    mUIManager.reset();
    mPostProcessManager.reset();

    // 清理场景实体
    auto& entityManager = utils::EntityManager::get();

    if (mGroundEntity) {
        mScene->remove(mGroundEntity);
        entityManager.destroy(mGroundEntity);
    }

    for (auto entity : mSceneEntities) {
        mScene->remove(entity);
        entityManager.destroy(entity);
    }

    for (auto light : mPointLights) {
        mEngine->destroy(light);
    }

    if (mSunLight) {
        mEngine->destroy(mSunLight);
    }

    // 清理 Filament 资源
    if (mIndirectLight) mEngine->destroy(mIndirectLight);
    if (mSkybox) mEngine->destroy(mSkybox);

    if (mResourceLoader) delete mResourceLoader;
    if (mAssetLoader) gltfio::AssetLoader::destroy(&mAssetLoader);

    if (mCamera) mEngine->destroyCameraComponent(mCamera->getEntity());
    if (mView) mEngine->destroy(mView);
    if (mScene) mEngine->destroy(mScene);
    if (mRenderer) mEngine->destroy(mRenderer);
    if (mSwapChain) mEngine->destroy(mSwapChain);
    if (mEngine) Engine::destroy(&mEngine);
}
```

### 主程序入口

```cpp
// main.cpp
#include "CompleteGameApp.h"
#include <SDL2/SDL.h>
#include <iostream>

/**
 * 主程序入口
 */
int main(int argc, char* argv[]) {
    // 初始化 SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
        return -1;
    }

    // 创建窗口
    const uint32_t width = 1280;
    const uint32_t height = 720;

    SDL_Window* window = SDL_CreateWindow(
        "Filament Complete Game Example",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    // 创建游戏应用
    CompleteGameApp app;

    // 获取原生窗口句柄
    void* nativeWindow = nullptr;
#ifdef __APPLE__
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    SDL_GetWindowWMInfo(window, &wmi);
    nativeWindow = wmi.info.cocoa.window;
#elif defined(_WIN32)
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    SDL_GetWindowWMInfo(window, &wmi);
    nativeWindow = wmi.info.win.window;
#else
    // Linux
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    SDL_GetWindowWMInfo(window, &wmi);
    nativeWindow = (void*)(uintptr_t)wmi.info.x11.window;
#endif

    // 初始化应用
    if (!app.initialize(nativeWindow, width, height)) {
        std::cerr << "Failed to initialize app" << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    // 主循环
    bool running = true;
    auto lastTime = std::chrono::high_resolution_clock::now();

    InputState inputState;

    while (running) {
        // 计算时间步长
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        // 处理事件
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;

                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                        app.resize(event.window.data1, event.window.data2);
                    }
                    break;

                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym) {
                        case SDLK_w: inputState.moveForward = true; break;
                        case SDLK_s: inputState.moveBackward = true; break;
                        case SDLK_a: inputState.moveLeft = true; break;
                        case SDLK_d: inputState.moveRight = true; break;
                        case SDLK_SPACE: inputState.jump = true; break;
                        case SDLK_LSHIFT: inputState.run = true; break;
                        case SDLK_ESCAPE: running = false; break;
                    }
                    break;

                case SDL_KEYUP:
                    switch (event.key.keysym.sym) {
                        case SDLK_w: inputState.moveForward = false; break;
                        case SDLK_s: inputState.moveBackward = false; break;
                        case SDLK_a: inputState.moveLeft = false; break;
                        case SDLK_d: inputState.moveRight = false; break;
                        case SDLK_LSHIFT: inputState.run = false; break;
                    }
                    break;

                case SDL_MOUSEMOTION:
                    if (SDL_GetRelativeMouseMode()) {
                        inputState.mouseDelta.x = event.motion.xrel;
                        inputState.mouseDelta.y = event.motion.yrel;
                    }
                    break;

                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                    }
                    break;
            }
        }

        // 更新
        app.handleInput(inputState);
        app.update(deltaTime);

        // 渲染
        app.render();
    }

    // 清理
    app.cleanup();
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
```

## Android Kotlin 实现

### MainActivity.kt

```kotlin
// MainActivity.kt
package com.example.filament.completegame

import android.annotation.SuppressLint
import android.os.Bundle
import android.view.Choreographer
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.SurfaceView
import androidx.appcompat.app.AppCompatActivity
import com.google.android.filament.*
import com.google.android.filament.android.DisplayHelper
import com.google.android.filament.android.UiHelper
import com.google.android.filament.gltfio.AssetLoader
import com.google.android.filament.gltfio.FilamentAsset
import com.google.android.filament.gltfio.ResourceLoader
import com.google.android.filament.gltfio.Animator
import com.google.android.filament.utils.*
import java.nio.ByteBuffer
import kotlin.math.*

/**
 * 完整游戏示例 - Android Activity
 */
class MainActivity : AppCompatActivity() {

    // Filament 组件
    private lateinit var engine: Engine
    private lateinit var renderer: Renderer
    private lateinit var scene: Scene
    private lateinit var view: View
    private lateinit var camera: Camera

    // UI 辅助类
    private lateinit var uiHelper: UiHelper
    private lateinit var displayHelper: DisplayHelper
    private lateinit var surfaceView: SurfaceView

    // 资源加载
    private lateinit var assetLoader: AssetLoader
    private lateinit var resourceLoader: ResourceLoader

    // 游戏组件
    private var characterAsset: FilamentAsset? = null
    private var characterAnimator: Animator? = null

    // 相机控制
    private var cameraDistance = 8.0f
    private var cameraHeight = 3.0f
    private var cameraYaw = 0.0f
    private var cameraPitch = -20.0f

    // 角色状态
    private val characterPosition = FloatArray(3) // x, y, z
    private val characterVelocity = FloatArray(3)
    private var characterRotation = 0.0f
    private var isGrounded = true

    // 输入状态
    private val inputState = InputState()

    // 游戏状态
    private var gameState = GameState.PLAYING
    private var score = 0.0f
    private var gameTime = 0.0f

    // 收集物列表
    private val collectibles = mutableListOf<Collectible>()

    // 手势检测
    private lateinit var gestureDetector: GestureDetector

    // 帧回调
    private val frameScheduler = FrameCallback()

    /**
     * 输入状态
     */
    data class InputState(
        var moveX: Float = 0.0f,
        var moveY: Float = 0.0f,
        var jump: Boolean = false,
        var run: Boolean = false
    )

    /**
     * 游戏状态
     */
    enum class GameState {
        MAIN_MENU,
        PLAYING,
        PAUSED,
        GAME_OVER
    }

    /**
     * 收集物
     */
    data class Collectible(
        @Entity val entity: Int,
        val position: FloatArray,
        val value: Float,
        var active: Boolean = true,
        var rotation: Float = 0.0f
    )

    @SuppressLint("ClickableViewAccessibility")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        surfaceView = SurfaceView(this)
        setContentView(surfaceView)

        // 初始化 Filament
        setupFilament()

        // 设置手势检测
        setupGestureDetector()

        surfaceView.setOnTouchListener { _, event ->
            gestureDetector.onTouchEvent(event)
            handleTouchInput(event)
            true
        }
    }

    /**
     * 初始化 Filament
     */
    private fun setupFilament() {
        // 创建引擎
        engine = Engine.create()
        renderer = engine.createRenderer()
        scene = engine.createScene()
        view = engine.createView()
        camera = engine.createCamera(engine.entityManager.create())

        view.scene = scene
        view.camera = camera

        // 创建 UI 辅助
        uiHelper = UiHelper(UiHelper.ContextErrorPolicy.DONT_CHECK)
        uiHelper.renderCallback = SurfaceCallback()
        uiHelper.attachTo(surfaceView)

        displayHelper = DisplayHelper(this)

        // 创建资源加载器
        assetLoader = AssetLoader(engine, MaterialProvider(engine), EntityManager.get())
        resourceLoader = ResourceLoader(engine)

        // 设置相机
        camera.setProjection(45.0, 16.0 / 9.0, 0.1, 100.0, Camera.Fov.VERTICAL)

        // 加载场景
        loadScene()

        // 设置光照
        setupLighting()

        // 生成收集物
        spawnCollectibles()

        // 开始渲染循环
        frameScheduler.start()
    }

    /**
     * 加载场景资源
     */
    private fun loadScene() {
        // 加载角色模型
        assets.open("models/character.glb").use { input ->
            val bytes = ByteArray(input.available())
            input.read(bytes)
            val buffer = ByteBuffer.wrap(bytes)

            characterAsset = assetLoader.createAsset(buffer)
            characterAsset?.let { asset ->
                resourceLoader.loadResources(asset)
                scene.addEntities(asset.entities)

                characterAnimator = asset.instance.animator
            }
        }

        // 创建地面
        createGround()
    }

    /**
     * 创建地面
     */
    private fun createGround() {
        val vertices = floatArrayOf(
            -50f, 0f, -50f,
            50f, 0f, -50f,
            50f, 0f, 50f,
            -50f, 0f, 50f
        )

        val indices = shortArrayOf(0, 1, 2, 0, 2, 3)

        val vertexBuffer = VertexBuffer.Builder()
            .vertexCount(4)
            .bufferCount(1)
            .attribute(VertexBuffer.VertexAttribute.POSITION, 0,
                VertexBuffer.AttributeType.FLOAT3, 0, 12)
            .build(engine)

        vertexBuffer.setBufferAt(engine, 0,
            FloatBuffer.allocate(vertices.size).put(vertices))

        val indexBuffer = IndexBuffer.Builder()
            .indexCount(6)
            .bufferType(IndexBuffer.Builder.IndexType.USHORT)
            .build(engine)

        indexBuffer.setBuffer(engine,
            ShortBuffer.allocate(indices.size).put(indices))

        // 创建材质（简化，实际应该加载材质文件）
        val material = Material.Builder()
            .package(engine, ByteBuffer.allocate(0))
            .build(engine)

        val groundEntity = EntityManager.get().create()

        RenderableManager.Builder(1)
            .boundingBox(Box(-50f, 0f, -50f, 50f, 0f, 50f))
            .geometry(0, RenderableManager.PrimitiveType.TRIANGLES,
                vertexBuffer, indexBuffer, 0, 6)
            .material(0, material.defaultInstance)
            .build(engine, groundEntity)

        scene.addEntity(groundEntity)
    }

    /**
     * 设置光照
     */
    private fun setupLighting() {
        // 太阳光
        val sunEntity = EntityManager.get().create()
        LightManager.Builder(LightManager.Type.SUN)
            .color(1.0f, 0.95f, 0.9f)
            .intensity(100000.0f)
            .direction(0.6f, -1.0f, -0.8f)
            .castShadows(true)
            .build(engine, sunEntity)
        scene.addEntity(sunEntity)

        // 天空盒
        val skybox = Skybox.Builder()
            .color(0.5f, 0.7f, 1.0f, 1.0f)
            .build(engine)
        scene.skybox = skybox

        // 环境光
        val ibl = IndirectLight.Builder()
            .intensity(30000.0f)
            .build(engine)
        scene.indirectLight = ibl
    }

    /**
     * 生成收集物
     */
    private fun spawnCollectibles() {
        for (i in 0 until 10) {
            val x = (Math.random() * 40 - 20).toFloat()
            val z = (Math.random() * 40 - 20).toFloat()
            val position = floatArrayOf(x, 1.0f, z)

            val entity = EntityManager.get().create()

            // 创建简单的立方体作为收集物（实际应该加载模型）
            // 这里简化处理

            val collectible = Collectible(entity, position, 10.0f)
            collectibles.add(collectible)

            scene.addEntity(entity)
        }
    }

    /**
     * 设置手势检测
     */
    private fun setupGestureDetector() {
        gestureDetector = GestureDetector(this,
            object : GestureDetector.SimpleOnGestureListener() {
                override fun onScroll(
                    e1: MotionEvent?,
                    e2: MotionEvent,
                    distanceX: Float,
                    distanceY: Float
                ): Boolean {
                    // 处理相机旋转
                    cameraYaw -= distanceX * 0.2f
                    cameraPitch -= distanceY * 0.2f
                    cameraPitch = cameraPitch.coerceIn(-60f, 20f)
                    return true
                }

                override fun onDoubleTap(e: MotionEvent): Boolean {
                    // 双击跳跃
                    inputState.jump = true
                    return true
                }
            })
    }

    /**
     * 处理触摸输入
     */
    private fun handleTouchInput(event: MotionEvent) {
        when (event.action) {
            MotionEvent.ACTION_DOWN -> {
                // 使用触摸位置控制移动
                val x = (event.x / surfaceView.width - 0.5f) * 2
                val y = (event.y / surfaceView.height - 0.5f) * 2

                inputState.moveX = x
                inputState.moveY = -y
            }
            MotionEvent.ACTION_MOVE -> {
                val x = (event.x / surfaceView.width - 0.5f) * 2
                val y = (event.y / surfaceView.height - 0.5f) * 2

                inputState.moveX = x
                inputState.moveY = -y
            }
            MotionEvent.ACTION_UP -> {
                inputState.moveX = 0.0f
                inputState.moveY = 0.0f
            }
        }
    }

    /**
     * 更新游戏
     */
    private fun update(deltaTime: Float) {
        if (gameState != GameState.PLAYING) return

        // 更新游戏时间
        gameTime += deltaTime

        // 更新角色
        updateCharacter(deltaTime)

        // 更新相机
        updateCamera(deltaTime)

        // 更新收集物
        updateCollectibles(deltaTime)

        // 检查收集
        checkCollections()
    }

    /**
     * 更新角色
     */
    private fun updateCharacter(deltaTime: Float) {
        // 移动
        val moveSpeed = if (inputState.run) 10.0f else 5.0f

        characterVelocity[0] = inputState.moveX * moveSpeed
        characterVelocity[2] = inputState.moveY * moveSpeed

        // 重力
        if (!isGrounded) {
            characterVelocity[1] += -20.0f * deltaTime
        }

        // 跳跃
        if (inputState.jump && isGrounded) {
            characterVelocity[1] = 8.0f
            isGrounded = false
            inputState.jump = false
        }

        // 更新位置
        for (i in 0..2) {
            characterPosition[i] += characterVelocity[i] * deltaTime
        }

        // 地面检测
        if (characterPosition[1] <= 0.0f) {
            characterPosition[1] = 0.0f
            characterVelocity[1] = 0.0f
            isGrounded = true
        } else {
            isGrounded = false
        }

        // 更新朝向
        if (inputState.moveX != 0.0f || inputState.moveY != 0.0f) {
            characterRotation = atan2(inputState.moveX, inputState.moveY)
        }

        // 更新变换
        characterAsset?.let { asset ->
            val transform = FloatArray(16)
            Mat4.identity(transform)
            Mat4.translate(transform, characterPosition[0],
                characterPosition[1], characterPosition[2])
            Mat4.rotate(transform, characterRotation, 0f, 1f, 0f)

            val tcm = engine.transformManager
            val instance = tcm.getInstance(asset.root)
            tcm.setTransform(instance, transform)
        }

        // 更新动画
        characterAnimator?.apply {
            updateBoneMatrices()
        }
    }

    /**
     * 更新相机
     */
    private fun updateCamera(deltaTime: Float) {
        val yawRad = Math.toRadians(cameraYaw.toDouble())
        val pitchRad = Math.toRadians(cameraPitch.toDouble())

        val offsetX = (cameraDistance * cos(pitchRad) * sin(yawRad)).toFloat()
        val offsetY = (cameraHeight + cameraDistance * sin(pitchRad)).toFloat()
        val offsetZ = (cameraDistance * cos(pitchRad) * cos(yawRad)).toFloat()

        val eye = floatArrayOf(
            characterPosition[0] + offsetX,
            characterPosition[1] + offsetY,
            characterPosition[2] + offsetZ
        )

        val center = characterPosition
        val up = floatArrayOf(0f, 1f, 0f)

        camera.lookAt(
            eye[0].toDouble(), eye[1].toDouble(), eye[2].toDouble(),
            center[0].toDouble(), center[1].toDouble(), center[2].toDouble(),
            up[0].toDouble(), up[1].toDouble(), up[2].toDouble()
        )
    }

    /**
     * 更新收集物
     */
    private fun updateCollectibles(deltaTime: Float) {
        val tcm = engine.transformManager

        for (collectible in collectibles) {
            if (!collectible.active) continue

            // 旋转动画
            collectible.rotation += deltaTime * 2.0f

            val transform = FloatArray(16)
            Mat4.identity(transform)
            Mat4.translate(transform, collectible.position[0],
                collectible.position[1], collectible.position[2])
            Mat4.rotate(transform, collectible.rotation, 0f, 1f, 0f)

            val instance = tcm.getInstance(collectible.entity)
            if (instance.isValid) {
                tcm.setTransform(instance, transform)
            }
        }
    }

    /**
     * 检查收集
     */
    private fun checkCollections() {
        for (collectible in collectibles) {
            if (!collectible.active) continue

            val dx = characterPosition[0] - collectible.position[0]
            val dy = characterPosition[1] - collectible.position[1]
            val dz = characterPosition[2] - collectible.position[2]
            val distance = sqrt(dx * dx + dy * dy + dz * dz)

            if (distance < 1.5f) {
                score += collectible.value
                collectible.active = false

                // 从场景移除
                scene.remove(collectible.entity)
            }
        }
    }

    /**
     * 表面回调
     */
    inner class SurfaceCallback : UiHelper.RendererCallback {
        override fun onNativeWindowChanged(surface: Surface) {
            val swapChain = engine.createSwapChain(surface)
            renderer.setSwapChain(swapChain)
        }

        override fun onDetachedFromSurface() {
            renderer.clearSwapChain()
        }

        override fun onResized(width: Int, height: Int) {
            view.viewport = Viewport(0, 0, width, height)

            val aspect = width.toDouble() / height.toDouble()
            camera.setProjection(45.0, aspect, 0.1, 100.0, Camera.Fov.VERTICAL)
        }
    }

    /**
     * 帧回调
     */
    inner class FrameCallback : Choreographer.FrameCallback {
        private var lastTime = System.nanoTime()

        override fun doFrame(frameTimeNanos: Long) {
            val deltaTime = (frameTimeNanos - lastTime) / 1_000_000_000.0f
            lastTime = frameTimeNanos

            // 限制时间步长
            val clampedDeltaTime = min(deltaTime, 0.033f)

            // 更新游戏
            update(clampedDeltaTime)

            // 渲染
            if (uiHelper.isReadyToRender) {
                if (renderer.beginFrame(renderer.swapChain!!, frameTimeNanos)) {
                    renderer.render(view)
                    renderer.endFrame()
                }
            }

            // 继续下一帧
            Choreographer.getInstance().postFrameCallback(this)
        }

        fun start() {
            Choreographer.getInstance().postFrameCallback(this)
        }
    }

    override fun onDestroy() {
        super.onDestroy()

        // 清理资源
        characterAnimator = null
        characterAsset?.let {
            assetLoader.destroyAsset(it)
        }

        engine.destroyRenderer(renderer)
        engine.destroyView(view)
        engine.destroyScene(scene)
        engine.destroyCameraComponent(camera.entity)

        assetLoader.destroy()
        resourceLoader.destroy()

        engine.destroy()
    }
}
```

### AndroidManifest.xml

```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.example.filament.completegame">

    <application
        android:allowBackup="true"
        android:icon="@mipmap/ic_launcher"
        android:label="@string/app_name"
        android:theme="@style/Theme.AppCompat.NoActionBar">

        <activity
            android:name=".MainActivity"
            android:exported="true"
            android:screenOrientation="landscape"
            android:configChanges="orientation|screenSize">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
```

### build.gradle (Module)

```gradle
plugins {
    id 'com.android.application'
    id 'org.jetbrains.kotlin.android'
}

android {
    namespace 'com.example.filament.completegame'
    compileSdk 34

    defaultConfig {
        applicationId "com.example.filament.completegame"
        minSdk 24
        targetSdk 34
        versionCode 1
        versionName "1.0"

        ndk {
            abiFilters 'arm64-v8a', 'armeabi-v7a', 'x86_64'
        }
    }

    buildTypes {
        release {
            minifyEnabled false
            proguardFiles getDefaultProguardFile('proguard-android-optimize.txt'),
                         'proguard-rules.pro'
        }
    }

    compileOptions {
        sourceCompatibility JavaVersion.VERSION_1_8
        targetCompatibility JavaVersion.VERSION_1_8
    }

    kotlinOptions {
        jvmTarget = '1.8'
    }
}

dependencies {
    implementation 'androidx.core:core-ktx:1.12.0'
    implementation 'androidx.appcompat:appcompat:1.6.1'

    // Filament
    implementation 'com.google.android.filament:filament-android:1.51.0'
    implementation 'com.google.android.filament:filament-utils-android:1.51.0'
    implementation 'com.google.android.filament:gltfio-android:1.51.0'
}
```

## 性能优化

### 1. 渲染优化

```cpp
/**
 * 实现 LOD (Level of Detail) 系统
 */
class LODSystem {
public:
    void update(const float3& cameraPosition) {
        for (auto& entity : mEntities) {
            float distance = length(entity.position - cameraPosition);

            // 根据距离选择 LOD 级别
            int lodLevel = 0;
            if (distance > 50.0f) lodLevel = 3;
            else if (distance > 30.0f) lodLevel = 2;
            else if (distance > 15.0f) lodLevel = 1;

            // 更新 Renderable 组件
            updateLOD(entity.entity, lodLevel);
        }
    }

private:
    struct LODEntity {
        utils::Entity entity;
        float3 position;
    };

    std::vector<LODEntity> mEntities;
};
```

### 2. 物理优化

```cpp
/**
 * 使用空间分区加速碰撞检测
 */
class SpatialGrid {
public:
    SpatialGrid(float cellSize) : mCellSize(cellSize) {}

    void insert(utils::Entity entity, const float3& position) {
        int2 cell = getCell(position);
        mGrid[cell].push_back({entity, position});
    }

    std::vector<utils::Entity> queryNearby(const float3& position, float radius) {
        std::vector<utils::Entity> result;

        int2 minCell = getCell(position - float3{radius, 0, radius});
        int2 maxCell = getCell(position + float3{radius, 0, radius});

        for (int x = minCell.x; x <= maxCell.x; x++) {
            for (int y = minCell.y; y <= maxCell.y; y++) {
                auto it = mGrid.find({x, y});
                if (it != mGrid.end()) {
                    for (auto& item : it->second) {
                        if (length(item.position - position) <= radius) {
                            result.push_back(item.entity);
                        }
                    }
                }
            }
        }

        return result;
    }

private:
    struct GridItem {
        utils::Entity entity;
        float3 position;
    };

    int2 getCell(const float3& position) {
        return {
            static_cast<int>(std::floor(position.x / mCellSize)),
            static_cast<int>(std::floor(position.z / mCellSize))
        };
    }

    float mCellSize;
    std::unordered_map<int2, std::vector<GridItem>> mGrid;
};
```

### 3. 内存优化

```cpp
/**
 * 对象池减少内存分配
 */
template<typename T>
class ObjectPool {
public:
    ObjectPool(size_t initialSize = 64) {
        mPool.reserve(initialSize);
        for (size_t i = 0; i < initialSize; i++) {
            mPool.push_back(new T());
        }
    }

    ~ObjectPool() {
        for (auto obj : mPool) {
            delete obj;
        }
        for (auto obj : mActive) {
            delete obj;
        }
    }

    T* acquire() {
        if (mPool.empty()) {
            return new T();
        }

        T* obj = mPool.back();
        mPool.pop_back();
        mActive.push_back(obj);
        return obj;
    }

    void release(T* obj) {
        auto it = std::find(mActive.begin(), mActive.end(), obj);
        if (it != mActive.end()) {
            mActive.erase(it);
            mPool.push_back(obj);
        }
    }

private:
    std::vector<T*> mPool;
    std::vector<T*> mActive;
};
```

### 4. 异步资源加载

```cpp
/**
 * 后台线程加载资源
 */
class AsyncResourceLoader {
public:
    void loadAssetAsync(const std::string& path,
                       std::function<void(gltfio::FilamentAsset*)> callback) {
        std::thread([this, path, callback]() {
            // 在后台线程加载
            gltfio::FilamentAsset* asset = loadAssetFromFile(path);

            // 在主线程回调
            mMainThreadCallbacks.push([callback, asset]() {
                callback(asset);
            });
        }).detach();
    }

    void processCallbacks() {
        std::function<void()> callback;
        while (mMainThreadCallbacks.try_pop(callback)) {
            callback();
        }
    }

private:
    concurrent_queue<std::function<void()>> mMainThreadCallbacks;
};
```

## 常见问题

### Q1: 如何处理不同平台的输入系统？

**A:** 使用输入抽象层统一处理：

```cpp
class InputManager {
public:
    virtual ~InputManager() = default;

    virtual void update() = 0;
    virtual bool isKeyDown(int keyCode) const = 0;
    virtual float2 getMouseDelta() const = 0;

    InputState getInputState() const { return mInputState; }

protected:
    InputState mInputState;
};

class SDLInputManager : public InputManager {
public:
    void update() override {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            handleEvent(event);
        }
    }

    bool isKeyDown(int keyCode) const override {
        const Uint8* state = SDL_GetKeyboardState(nullptr);
        return state[SDL_GetScancodeFromKey(keyCode)];
    }
};

class AndroidInputManager : public InputManager {
    // Android 触摸输入实现
};
```

### Q2: 如何实现游戏存档系统？

**A:** 使用 JSON 序列化保存游戏状态：

```cpp
#include <nlohmann/json.hpp>

class SaveSystem {
public:
    void saveGame(const std::string& filename) {
        nlohmann::json saveData;

        // 保存玩家数据
        saveData["player"]["position"] = {
            mCharacter->getPosition().x,
            mCharacter->getPosition().y,
            mCharacter->getPosition().z
        };

        // 保存游戏状态
        saveData["score"] = mGameManager->getScore();
        saveData["time"] = mGameManager->getGameTime();

        // 保存收集物状态
        auto& collectibles = saveData["collectibles"];
        for (const auto& item : mGameManager->getCollectibles()) {
            if (!item->isActive()) {
                collectibles.push_back(/* item ID */);
            }
        }

        // 写入文件
        std::ofstream file(filename);
        file << saveData.dump(4);
    }

    void loadGame(const std::string& filename) {
        std::ifstream file(filename);
        nlohmann::json saveData = nlohmann::json::parse(file);

        // 恢复玩家位置
        auto pos = saveData["player"]["position"];
        mCharacter->setPosition({pos[0], pos[1], pos[2]});

        // 恢复游戏状态
        // ...
    }
};
```

### Q3: 如何优化大量实体的渲染性能？

**A:** 使用实例化渲染：

```cpp
/**
 * 使用 GPU 实例化渲染大量相同对象
 */
void renderCollectiblesInstanced() {
    // 收集所有活跃收集物的变换矩阵
    std::vector<mat4f> transforms;
    for (const auto& collectible : mGameManager->getCollectibles()) {
        if (collectible->isActive()) {
            transforms.push_back(collectible->getTransform());
        }
    }

    if (transforms.empty()) return;

    // 创建实例化缓冲区
    VertexBuffer* instanceBuffer = VertexBuffer::Builder()
        .vertexCount(transforms.size())
        .bufferCount(1)
        .attribute(VertexBuffer.VertexAttribute::CUSTOM0, 0,
                  VertexBuffer::AttributeType::FLOAT4, 0, 64)
        .attribute(VertexBuffer.VertexAttribute::CUSTOM1, 0,
                  VertexBuffer::AttributeType::FLOAT4, 16, 64)
        .attribute(VertexBuffer.VertexAttribute::CUSTOM2, 0,
                  VertexBuffer::AttributeType::FLOAT4, 32, 64)
        .attribute(VertexBuffer.VertexAttribute::CUSTOM3, 0,
                  VertexBuffer::AttributeType::FLOAT4, 48, 64)
        .build(*mEngine);

    instanceBuffer->setBufferAt(*mEngine, 0,
        VertexBuffer::BufferDescriptor(transforms.data(),
            transforms.size() * sizeof(mat4f)));

    // 使用实例化渲染
    // 在材质中使用 CUSTOM0-3 属性访问变换矩阵
}
```

### Q4: 如何实现流畅的场景切换？

**A:** 使用异步加载和淡入淡出：

```cpp
class SceneTransition {
public:
    void transitionTo(const std::string& sceneName,
                     float fadeDuration = 1.0f) {
        mTransitioning = true;
        mFadeProgress = 0.0f;
        mFadeDuration = fadeDuration;
        mNextScene = sceneName;
        mFadingOut = true;

        // 开始异步加载下一个场景
        loadSceneAsync(sceneName);
    }

    void update(float deltaTime) {
        if (!mTransitioning) return;

        mFadeProgress += deltaTime / mFadeDuration;

        if (mFadeProgress >= 1.0f) {
            if (mFadingOut) {
                // 卸载当前场景
                unloadCurrentScene();

                // 等待加载完成
                if (mNextSceneLoaded) {
                    activateNextScene();
                    mFadingOut = false;
                    mFadeProgress = 0.0f;
                }
            } else {
                // 淡入完成
                mTransitioning = false;
            }
        }

        // 更新淡入淡出效果
        float alpha = mFadingOut ? mFadeProgress : (1.0f - mFadeProgress);
        applyFadeEffect(alpha);
    }

private:
    bool mTransitioning = false;
    bool mFadingOut = false;
    float mFadeProgress = 0.0f;
    float mFadeDuration = 1.0f;
    std::string mNextScene;
    bool mNextSceneLoaded = false;
};
```

### Q5: 如何调试 Filament 渲染问题？

**A:** 使用调试工具和辅助渲染：

```cpp
class RenderDebugger {
public:
    void enableDebugMode(bool enable) {
        mDebugEnabled = enable;
    }

    void renderDebugInfo(View* view) {
        if (!mDebugEnabled) return;

        // 显示包围盒
        renderBoundingBoxes();

        // 显示光源位置
        renderLightGizmos();

        // 显示相机视锥体
        renderCameraFrustum();

        // 显示帧率和性能统计
        renderPerformanceOverlay();
    }

    void renderBoundingBoxes() {
        auto& rcm = mEngine->getRenderableManager();
        auto& tcm = mEngine->getTransformManager();

        // 遍历所有可渲染实体
        for (auto entity : mScene->getEntities()) {
            auto instance = rcm.getInstance(entity);
            if (!instance.isValid()) continue;

            Box box = rcm.getAxisAlignedBoundingBox(instance);
            mat4f transform = tcm.getWorldTransform(
                tcm.getInstance(entity));

            // 绘制线框包围盒
            drawWireframeBox(box, transform, {1.0f, 1.0f, 0.0f});
        }
    }

    void renderPerformanceOverlay() {
        // 显示 FPS、drawcall 数量、三角形数量等
        std::string info =
            "FPS: " + std::to_string(mCurrentFPS) + "\n" +
            "DrawCalls: " + std::to_string(mDrawCallCount) + "\n" +
            "Triangles: " + std::to_string(mTriangleCount);

        renderText(info, {10, 10});
    }

private:
    bool mDebugEnabled = false;
    Engine* mEngine;
    Scene* mScene;
    float mCurrentFPS = 0.0f;
    int mDrawCallCount = 0;
    int mTriangleCount = 0;
};
```

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.19)
project(complete-game)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Filament 路径
set(FILAMENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../out/release/filament")

# 包含目录
include_directories(
    ${FILAMENT_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 链接目录
link_directories(
    ${FILAMENT_DIR}/lib/x86_64
)

# 源文件
set(SOURCES
    src/main.cpp
    src/CompleteGameApp.cpp
    src/CharacterController.cpp
    src/CameraController.cpp
    src/GameManager.cpp
    src/UIManager.cpp
    src/PostProcessManager.cpp
)

# 创建可执行文件
add_executable(complete-game ${SOURCES})

# 链接 Filament 库
target_link_libraries(complete-game
    filament
    backend
    filabridge
    filaflat
    utils
    geometry
    ibl
    image
    gltfio_core
    gltfio
    dracodec
    ktxreader
    stb
    tinyexr
    uberzlib
    z
    png
)

# 链接 SDL2
find_package(SDL2 REQUIRED)
target_include_directories(complete-game PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(complete-game ${SDL2_LIBRARIES})

# 平台特定链接
if(APPLE)
    target_link_libraries(complete-game
        "-framework Cocoa"
        "-framework Metal"
        "-framework QuartzCore"
    )
elseif(UNIX)
    target_link_libraries(complete-game
        GL
        pthread
        dl
    )
elseif(WIN32)
    target_link_libraries(complete-game
        opengl32
    )
endif()

# 复制资源文件
add_custom_command(TARGET complete-game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:complete-game>/assets
)

# 安装
install(TARGETS complete-game DESTINATION bin)
install(DIRECTORY assets DESTINATION bin)
```

## 构建说明

### Desktop (macOS/Linux/Windows)

```bash
# 创建构建目录
mkdir build && cd build

# 配置项目
cmake ..

# 编译
cmake --build . --config Release

# 运行
./complete-game
```

### Android

```bash
# 使用 Android Studio 打开项目
# 或者使用 Gradle 命令行

./gradlew assembleDebug
./gradlew installDebug
```

### iOS

```bash
# 使用 Xcode 打开项目
open CompleteGame.xcodeproj

# 或使用命令行构建
xcodebuild -scheme CompleteGame -configuration Release
```

## 相关文档

- [06-animation-player.md](./06-animation-player.md) - 骨骼动画播放器
- [07-scene-manager.md](./07-scene-manager.md) - 场景和资源管理
- [08-ui-rendering.md](./08-ui-rendering.md) - 2D UI 渲染系统
- [09-post-processing.md](./09-post-processing.md) - 后处理效果
- [../engine/Engine.md](../engine/Engine.md) - Filament 引擎核心
- [../material/Material.md](../material/Material.md) - 材质系统
- [../platforms/](../platforms/) - 平台集成指南

## 总结

本示例展示了如何使用 Filament 构建一个完整的 3D 游戏应用，整合了：

1. **核心系统**
   - 游戏循环和状态机
   - 输入处理系统
   - 角色控制和动画
   - 相机系统

2. **渲染技术**
   - PBR 材质和光照
   - 阴影系统
   - 天空盒和 IBL
   - 后处理效果

3. **游戏功能**
   - 角色移动和跳跃
   - 收集系统
   - 得分和计时
   - UI 显示

4. **跨平台支持**
   - Desktop (SDL2)
   - Android (Kotlin)
   - iOS (Swift)
   - Web (WebGL)

通过学习这个完整示例，你可以掌握使用 Filament 开发 3D 游戏的完整流程，并应用到自己的项目中。
