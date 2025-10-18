# glTF 动画系统详解

本文档详细说明 Filament 中 glTF 动画系统的使用，包括 Animator API、骨骼动画、动画混合等。

---

## 概述

Filament 的 `Animator` 类提供两大功能：
1. **Transform 动画**: 更新节点的平移、旋转、缩放
2. **骨骼动画**: 更新骨骼矩阵用于蒙皮变形

---

## Animator API

### 获取 Animator

```cpp
FilamentAsset* asset = loader->createAsset(data.data(), data.size());

// 从资产获取 Animator
Animator* animator = asset->getInstance()->getAnimator();

// 检查是否有动画
if (animator && animator->getAnimationCount() > 0) {
    // 有动画可播放
}
```

### 基本播放

```cpp
// 获取动画信息
size_t animCount = animator->getAnimationCount();
for (size_t i = 0; i < animCount; ++i) {
    const char* name = animator->getAnimationName(i);
    float duration = animator->getAnimationDuration(i);
    slog.i << "Animation " << i << ": " << name 
           << " (" << duration << "s)" << io::endl;
}

// 播放动画
float time = 0.0f;
const float dt = 0.016f;  // 60 FPS

while (rendering) {
    time += dt;
    
    // 应用动画（更新 TransformManager）
    animator->applyAnimation(0, time);
    
    // 更新骨骼矩阵（如果有蒙皮）
    animator->updateBoneMatrices();
    
    // 渲染
    renderer->render(view);
}
```

### 循环播放

```cpp
float time = 0.0f;
const size_t animIndex = 0;
const float duration = animator->getAnimationDuration(animIndex);

while (rendering) {
    time += dt;
    
    // 循环
    if (time > duration) {
        time = fmod(time, duration);
    }
    
    animator->applyAnimation(animIndex, time);
    animator->updateBoneMatrices();
    
    renderer->render(view);
}
```

---

## 骨骼动画

### 骨骼蒙皮流程

```
1. applyAnimation() 
   → 更新节点变换（TransformManager）
   
2. updateBoneMatrices()
   → 计算世界矩阵
   → 乘以逆绑定矩阵
   → 上传到 RenderableManager
   
3. Vertex Shader
   → 使用骨骼矩阵变换顶点
```

### 示例：角色动画

```cpp
// 加载带骨骼的角色模型
FilamentAsset* character = loader->createAsset(data.data(), data.size());
Animator* animator = character->getInstance()->getAnimator();

// 添加到场景
scene->addEntities(character->getEntities(), character->getEntityCount());

// 渲染循环
float animTime = 0.0f;
while (rendering) {
    animTime += 0.016f;
    
    // 1. 应用动画到骨骼节点
    animator->applyAnimation(0, animTime);  // "Walk" 动画
    
    // 2. 更新骨骼矩阵（关键！）
    animator->updateBoneMatrices();
    
    // 3. 渲染
    renderer->render(view);
}
```

### 重置为 T-Pose

```cpp
// 重置到绑定姿态（T-Pose）
animator->resetBoneMatrices();
```

---

## 动画混合

### 交叉淡入淡出

```cpp
// 从动画 0 淡入到动画 1
float blendTime = 0.0f;
const float blendDuration = 0.5f;  // 0.5 秒过渡

float anim0Time = 10.0f;  // 当前动画的时间
float anim1Time = 0.0f;   // 新动画的时间

while (blendTime < blendDuration) {
    blendTime += dt;
    float alpha = blendTime / blendDuration;  // [0, 1]
    
    // 先应用新动画
    animator->applyAnimation(1, anim1Time);
    
    // 然后混合旧动画
    animator->applyCrossFade(0, anim0Time, 1.0f - alpha);
    
    // 更新骨骼
    animator->updateBoneMatrices();
    
    anim0Time += dt;
    anim1Time += dt;
    
    renderer->render(view);
}

// 混合完成，继续播放新动画
while (rendering) {
    anim1Time += dt;
    animator->applyAnimation(1, anim1Time);
    animator->updateBoneMatrices();
    renderer->render(view);
}
```

### 多动画混合

```cpp
// 手动实现多动画混合
void blendAnimations(Animator* animator, 
                     const std::vector<size_t>& anims,
                     const std::vector<float>& times,
                     const std::vector<float>& weights) {
    
    // 第一个动画
    animator->applyAnimation(anims[0], times[0]);
    
    // 混合其他动画
    for (size_t i = 1; i < anims.size(); ++i) {
        animator->applyCrossFade(anims[i], times[i], weights[i]);
    }
    
    animator->updateBoneMatrices();
}

// 使用示例：混合 idle 和 walk
blendAnimations(animator,
    {0, 1},              // idle, walk
    {idleTime, walkTime},
    {1.0f - walkBlend, walkBlend}  // walkBlend = [0, 1]
);
```

---

## 性能优化

### 1. 按需更新骨骼

```cpp
// 只有当角色在视野内时才更新动画
if (camera->frustum.contains(character->getBoundingBox())) {
    animator->applyAnimation(animIndex, time);
    animator->updateBoneMatrices();
}
```

### 2. 动画 LOD

```cpp
// 根据距离选择更新频率
float distance = length(cameraPos - characterPos);

if (distance < 10.0f) {
    // 近距离：每帧更新
    animator->applyAnimation(animIndex, time);
    animator->updateBoneMatrices();
} else if (distance < 50.0f) {
    // 中距离：每 3 帧更新一次
    if (frameCount % 3 == 0) {
        animator->applyAnimation(animIndex, time);
        animator->updateBoneMatrices();
    }
} else {
    // 远距离：不更新（静止姿态）
}
```

### 3. 骨骼数量优化

```cpp
// Filament 支持最多 512 个骨骼（32KB UBO）
// 但移动设备建议 < 100 个骨骼

// 在 Blender 中简化骨骼层级
// 或使用 LOD 模型（远距离用更少骨骼）
```

---

## 完整示例

```cpp
#include <filament/Engine.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/Animator.h>

void playAnimation(const char* modelPath) {
    // 创建引擎和加载器
    Engine* engine = Engine::create();
    Scene* scene = engine->createScene();
    MaterialProvider* materials = createJitShaderProvider(engine);
    AssetLoader* loader = AssetLoader::create({engine, materials});
    
    // 加载模型
    std::vector<uint8_t> data = readFile(modelPath);
    FilamentAsset* asset = loader->createAsset(data.data(), data.size());
    
    // 加载资源
    TextureProvider* texProvider = createStbProvider(engine);
    ResourceLoader* resLoader = new ResourceLoader({engine, modelPath});
    resLoader->addTextureProvider("image/png", texProvider);
    resLoader->loadResources(asset);
    
    // 添加到场景
    scene->addEntities(asset->getEntities(), asset->getEntityCount());
    
    // 获取 Animator
    Animator* animator = asset->getInstance()->getAnimator();
    
    if (!animator || animator->getAnimationCount() == 0) {
        slog.w << "No animations found" << io::endl;
        return;
    }
    
    // 打印动画信息
    slog.i << "Found " << animator->getAnimationCount() << " animations:" << io::endl;
    for (size_t i = 0; i < animator->getAnimationCount(); ++i) {
        slog.i << "  [" << i << "] " << animator->getAnimationName(i)
               << " (" << animator->getAnimationDuration(i) << "s)" << io::endl;
    }
    
    // 播放第一个动画
    const size_t animIndex = 0;
    const float duration = animator->getAnimationDuration(animIndex);
    float time = 0.0f;
    const float dt = 0.016f;
    
    while (rendering) {
        time += dt;
        if (time > duration) {
            time = fmod(time, duration);  // 循环
        }
        
        // 应用动画
        animator->applyAnimation(animIndex, time);
        animator->updateBoneMatrices();
        
        // 渲染
        if (renderer->beginFrame(swapChain)) {
            renderer->render(view);
            renderer->endFrame();
        }
    }
    
    // 清理
    scene->removeEntities(asset->getEntities(), asset->getEntityCount());
    loader->destroyAsset(asset);
    materials->destroyMaterials();
    delete materials;
    delete texProvider;
    delete resLoader;
    AssetLoader::destroy(&loader);
    Engine::destroy(&engine);
}
```

---

## 总结

**Animator 关键要点**:
1. `applyAnimation()` - 更新 TransformManager
2. `updateBoneMatrices()` - 更新骨骼矩阵（必需）
3. `applyCrossFade()` - 动画混合
4. `resetBoneMatrices()` - 重置到 T-Pose

**性能优化**:
- 视锥剔除（只更新可见对象）
- 动画 LOD（距离-based 更新频率）
- 限制骨骼数量（< 100 for mobile）

**常见错误**:
- 忘记调用 `updateBoneMatrices()`（骨骼动画不生效）
- 时间未循环（动画播放一次后停止）
- 混合权重未归一化（导致错误的姿态）
