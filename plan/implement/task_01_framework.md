# 任务1: 框架搭建

**预计时间**: 30分钟
**前置依赖**: 无
**完成标志**: 编译通过，目录结构完整

---

## 目标

创建gltfio_ext库的基础框架：
1. 目录结构
2. CMakeLists.txt配置
3. 所有公开API头文件定义
4. 错误处理宏

---

## 1. 创建目录结构

```bash
mkdir -p libs/gltfio_ext/include/gltfio
mkdir -p libs/gltfio_ext/src
mkdir -p libs/gltfio_ext/tests
```

---

## 2. 创建 CMakeLists.txt

**文件**: `libs/gltfio_ext/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.19)
project(gltfio_ext)

set(TARGET gltfio_ext)
set(PUBLIC_HDR_DIR include)

# 公开头文件
set(PUBLIC_HDRS
    include/gltfio/SkeletonAsset.h
    include/gltfio/MeshAsset.h
    include/gltfio/AnimationAsset.h
    include/gltfio/AnimationPack.h
    include/gltfio/StandaloneAnimator.h
    include/gltfio/AssetLoaderExt.h
)

# 内部实现文件
set(SRCS
    src/FSkeletonAsset.h
    src/FMeshAsset.h
    src/FAnimationAsset.h
    src/FAnimationPack.h
    src/FStandaloneAnimator.h
    src/AssetLoaderExt.cpp
    src/SkeletonAsset.cpp
    src/MeshAsset.cpp
    src/AnimationAsset.cpp
    src/AnimationPack.cpp
    src/StandaloneAnimator.cpp
)

# 构建静态库
add_library(${TARGET} STATIC ${PUBLIC_HDRS} ${SRCS})

# 包含目录
target_include_directories(${TARGET} PUBLIC ${PUBLIC_HDR_DIR})
target_include_directories(${TARGET} PRIVATE src)

# 链接依赖 (只链接gltfio_core，不链接完整gltfio)
target_link_libraries(${TARGET} PUBLIC
    filament
    gltfio_core
    math
    utils
)

# 编译选项
set(GLTFIO_EXT_WARNINGS -Wall -Werror)
if (NOT MSVC)
    target_compile_options(${TARGET} PRIVATE ${GLTFIO_EXT_WARNINGS})
endif()

# 安装规则
install(TARGETS ${TARGET} ARCHIVE DESTINATION lib/${DIST_DIR})
install(DIRECTORY ${PUBLIC_HDR_DIR}/gltfio DESTINATION include)

# 设置文件夹
set_target_properties(${TARGET} PROPERTIES FOLDER Libs)
```

---

## 3. 修改顶层CMakeLists.txt

**文件**: `CMakeLists.txt` (在filament根目录)

找到gltfio的位置，在其后添加：

```cmake
# 在 add_subdirectory(libs/gltfio) 之后添加
add_subdirectory(libs/gltfio_ext)
```

---

## 4. 创建公开API头文件框架

### 4.1 SkeletonAsset.h

**文件**: `libs/gltfio_ext/include/gltfio/SkeletonAsset.h`

```cpp
#ifndef GLTFIO_SKELETONASSET_H
#define GLTFIO_SKELETONASSET_H

#include <utils/compiler.h>
#include <utils/Entity.h>
#include <math/mat4.h>

#include <stddef.h>
#include <stdint.h>

namespace filament::gltfio {

class UTILS_PUBLIC SkeletonAsset {
public:
    size_t getBoneCount() const noexcept;
    int getBoneIndex(const char* boneName) const noexcept;
    const char* getBoneName(size_t boneIndex) const noexcept;
    utils::Entity getBoneEntity(size_t boneIndex) const noexcept;
    const filament::math::mat4f* getInverseBindMatrices() const noexcept;
    utils::Entity getRoot() const noexcept;
    bool validateBoneIndices(const uint16_t* indices, size_t count) const noexcept;

protected:
    SkeletonAsset() noexcept = default;
    ~SkeletonAsset() = default;

public:
    SkeletonAsset(SkeletonAsset const&) = delete;
    SkeletonAsset(SkeletonAsset&&) = delete;
    SkeletonAsset& operator=(SkeletonAsset const&) = delete;
    SkeletonAsset& operator=(SkeletonAsset&&) = delete;
};

} // namespace filament::gltfio

#endif // GLTFIO_SKELETONASSET_H
```

### 4.2 MeshAsset.h

**文件**: `libs/gltfio_ext/include/gltfio/MeshAsset.h`

```cpp
#ifndef GLTFIO_MESHASSET_H
#define GLTFIO_MESHASSET_H

#include <utils/compiler.h>
#include <utils/Entity.h>
#include <filament/Box.h>
#include <filament/RenderableManager.h>

namespace filament::gltfio {

class SkeletonAsset;
class StandaloneAnimator;

class UTILS_PUBLIC MeshAsset {
public:
    bool bindSkeleton(SkeletonAsset* skeleton) noexcept;
    SkeletonAsset* getBoundSkeleton() const noexcept;
    void updateSkinning(const StandaloneAnimator* animator) noexcept;
    utils::Entity getRenderableEntity() const noexcept;
    filament::RenderableManager::Instance getRenderableInstance() const noexcept;
    filament::Aabb getBoundingBox() const noexcept;

protected:
    MeshAsset() noexcept = default;
    ~MeshAsset() = default;

public:
    MeshAsset(MeshAsset const&) = delete;
    MeshAsset(MeshAsset&&) = delete;
    MeshAsset& operator=(MeshAsset const&) = delete;
    MeshAsset& operator=(MeshAsset&&) = delete;
};

} // namespace filament::gltfio

#endif // GLTFIO_MESHASSET_H
```

### 4.3 AnimationAsset.h

**文件**: `libs/gltfio_ext/include/gltfio/AnimationAsset.h`

```cpp
#ifndef GLTFIO_ANIMATIONASSET_H
#define GLTFIO_ANIMATIONASSET_H

#include <utils/compiler.h>
#include <stddef.h>

namespace filament::gltfio {

class UTILS_PUBLIC AnimationAsset {
public:
    enum class ChannelType { TRANSLATION, ROTATION, SCALE, WEIGHTS };

    const char* getName() const noexcept;
    float getDuration() const noexcept;
    size_t getChannelCount() const noexcept;
    const char* getChannelTargetBone(size_t channelIndex) const noexcept;
    ChannelType getChannelType(size_t channelIndex) const noexcept;

protected:
    AnimationAsset() noexcept = default;
    ~AnimationAsset() = default;

public:
    AnimationAsset(AnimationAsset const&) = delete;
    AnimationAsset(AnimationAsset&&) = delete;
    AnimationAsset& operator=(AnimationAsset const&) = delete;
    AnimationAsset& operator=(AnimationAsset&&) = delete;
};

} // namespace filament::gltfio

#endif // GLTFIO_ANIMATIONASSET_H
```

### 4.4 AnimationPack.h

**文件**: `libs/gltfio_ext/include/gltfio/AnimationPack.h`

```cpp
#ifndef GLTFIO_ANIMATIONPACK_H
#define GLTFIO_ANIMATIONPACK_H

#include <utils/compiler.h>
#include <stddef.h>

namespace filament::gltfio {

class AnimationAsset;

class UTILS_PUBLIC AnimationPack {
public:
    size_t getAnimationCount() const noexcept;
    AnimationAsset* getAnimation(size_t index) noexcept;
    AnimationAsset* findAnimation(const char* name) noexcept;
    const char* const* getAnimationNames() const noexcept;

protected:
    AnimationPack() noexcept = default;
    ~AnimationPack() = default;

public:
    AnimationPack(AnimationPack const&) = delete;
    AnimationPack(AnimationPack&&) = delete;
    AnimationPack& operator=(AnimationPack const&) = delete;
    AnimationPack& operator=(AnimationPack&&) = delete;
};

} // namespace filament::gltfio

#endif // GLTFIO_ANIMATIONPACK_H
```

### 4.5 StandaloneAnimator.h

**文件**: `libs/gltfio_ext/include/gltfio/StandaloneAnimator.h`

```cpp
#ifndef GLTFIO_STANDALONEANIMATOR_H
#define GLTFIO_STANDALONEANIMATOR_H

#include <utils/compiler.h>
#include <math/mat4.h>
#include <stddef.h>

namespace filament {
    class Engine;
}

namespace filament::gltfio {

class SkeletonAsset;
class AnimationAsset;
struct AnimatorImpl;

class UTILS_PUBLIC StandaloneAnimator {
public:
    static StandaloneAnimator* create(filament::Engine& engine);
    static void destroy(StandaloneAnimator** animator);

    void bindSkeleton(SkeletonAsset* skeleton) noexcept;
    int playAnimation(AnimationAsset* animation, float weight = 1.0f, bool loop = true) noexcept;
    void stopAnimation(int animationId) noexcept;
    void setAnimationWeight(int animationId, float weight) noexcept;
    void update(float deltaTime) noexcept;
    const filament::math::mat4f* getBoneMatrices() const noexcept;
    size_t getBoneCount() const noexcept;
    void resetToBindPose() noexcept;

private:
    StandaloneAnimator(filament::Engine& engine);
    ~StandaloneAnimator();

    StandaloneAnimator(const StandaloneAnimator&) = delete;
    StandaloneAnimator(StandaloneAnimator&&) = delete;
    StandaloneAnimator& operator=(const StandaloneAnimator&) = delete;

    AnimatorImpl* mImpl;
};

} // namespace filament::gltfio

#endif // GLTFIO_STANDALONEANIMATOR_H
```

### 4.6 AssetLoaderExt.h

**文件**: `libs/gltfio_ext/include/gltfio/AssetLoaderExt.h`

```cpp
#ifndef GLTFIO_ASSETLOADEREXT_H
#define GLTFIO_ASSETLOADEREXT_H

#include <gltfio/MaterialProvider.h>
#include <utils/compiler.h>
#include <stdint.h>

namespace filament {
    class Engine;
}

namespace utils {
    class EntityManager;
    class NameComponentManager;
}

namespace filament::gltfio {

class SkeletonAsset;
class MeshAsset;
class AnimationAsset;
class AnimationPack;

struct AssetConfigurationExt {
    filament::Engine* engine;
    MaterialProvider* materials;
    utils::NameComponentManager* names = nullptr;
    utils::EntityManager* entities = nullptr;
};

class UTILS_PUBLIC AssetLoaderExt {
public:
    static AssetLoaderExt* create(const AssetConfigurationExt& config);
    static void destroy(AssetLoaderExt** loader);

    SkeletonAsset* loadSkeleton(const uint8_t* bytes, uint32_t nbytes);
    MeshAsset* loadMesh(const uint8_t* bytes, uint32_t nbytes);
    AnimationAsset* loadAnimation(const uint8_t* bytes, uint32_t nbytes);
    AnimationPack* loadAnimationPack(const uint8_t* bytes, uint32_t nbytes);

    void destroySkeleton(SkeletonAsset* skeleton);
    void destroyMesh(MeshAsset* mesh);
    void destroyAnimation(AnimationAsset* animation);
    void destroyAnimationPack(AnimationPack* pack);

protected:
    AssetLoaderExt() noexcept = default;
    ~AssetLoaderExt() = default;

public:
    AssetLoaderExt(AssetLoaderExt const&) = delete;
    AssetLoaderExt(AssetLoaderExt&&) = delete;
    AssetLoaderExt& operator=(AssetLoaderExt const&) = delete;
    AssetLoaderExt& operator=(AssetLoaderExt&&) = delete;
};

} // namespace filament::gltfio

#endif // GLTFIO_ASSETLOADEREXT_H
```

---

## 5. 创建内部实现头文件框架

### 5.1 错误处理宏

**文件**: `libs/gltfio_ext/src/GltfioExtInternal.h`

```cpp
#ifndef GLTFIO_EXT_INTERNAL_H
#define GLTFIO_EXT_INTERNAL_H

#include <utils/Log.h>

#ifdef NDEBUG
#define GLTFIO_EXT_VERBOSE 0
#define GLTFIO_EXT_WARN(msg)
#else
#define GLTFIO_EXT_VERBOSE 1
#define GLTFIO_EXT_WARN(msg) utils::slog.w << "gltfio_ext: " << msg << utils::io::endl
#endif

#endif // GLTFIO_EXT_INTERNAL_H
```

### 5.2 内部实现类（空框架）

**文件**: `libs/gltfio_ext/src/FSkeletonAsset.h`

```cpp
#ifndef GLTFIO_EXT_FSKELETONASSET_H
#define GLTFIO_EXT_FSKELETONASSET_H

#include <gltfio/SkeletonAsset.h>
#include <utils/Entity.h>
#include <utils/FixedCapacityVector.h>
#include <utils/CString.h>
#include <math/mat4.h>
#include <map>

namespace filament {
    class Engine;
    class TransformManager;
}

namespace utils {
    class EntityManager;
}

namespace filament::gltfio {

struct FSkeletonAsset : public SkeletonAsset {
    filament::Engine* mEngine = nullptr;
    utils::EntityManager* mEntityManager = nullptr;
    filament::TransformManager* mTransformManager = nullptr;

    utils::FixedCapacityVector<utils::Entity> mBoneEntities;
    utils::FixedCapacityVector<utils::CString> mBoneNames;
    std::map<std::string, size_t> mBoneNameToIndex;
    utils::FixedCapacityVector<filament::math::mat4f> mInverseBindMatrices;
    utils::Entity mRoot;

    ~FSkeletonAsset();
};

} // namespace filament::gltfio

#endif // GLTFIO_EXT_FSKELETONASSET_H
```

创建类似的空框架文件：
- `libs/gltfio_ext/src/FMeshAsset.h`
- `libs/gltfio_ext/src/FAnimationAsset.h`
- `libs/gltfio_ext/src/FAnimationPack.h`
- `libs/gltfio_ext/src/FStandaloneAnimator.h`

---

## 6. 创建空实现文件

创建以下空cpp文件（只包含最小实现，避免编译错误）：

**文件**: `libs/gltfio_ext/src/AssetLoaderExt.cpp`

```cpp
#include <gltfio/AssetLoaderExt.h>
#include "GltfioExtInternal.h"

namespace filament::gltfio {

struct FAssetLoaderExt : public AssetLoaderExt {
    filament::Engine* mEngine;
    MaterialProvider* mMaterials;
    utils::EntityManager* mEntityManager;
    utils::NameComponentManager* mNameManager;

    FAssetLoaderExt(const AssetConfigurationExt& config)
        : mEngine(config.engine)
        , mMaterials(config.materials)
        , mEntityManager(config.entities)
        , mNameManager(config.names) {}
};

AssetLoaderExt* AssetLoaderExt::create(const AssetConfigurationExt& config) {
    return new FAssetLoaderExt(config);
}

void AssetLoaderExt::destroy(AssetLoaderExt** loader) {
    delete *loader;
    *loader = nullptr;
}

SkeletonAsset* AssetLoaderExt::loadSkeleton(const uint8_t* bytes, uint32_t nbytes) {
    GLTFIO_EXT_WARN("loadSkeleton not implemented yet");
    return nullptr;
}

MeshAsset* AssetLoaderExt::loadMesh(const uint8_t* bytes, uint32_t nbytes) {
    GLTFIO_EXT_WARN("loadMesh not implemented yet");
    return nullptr;
}

AnimationAsset* AssetLoaderExt::loadAnimation(const uint8_t* bytes, uint32_t nbytes) {
    GLTFIO_EXT_WARN("loadAnimation not implemented yet");
    return nullptr;
}

AnimationPack* AssetLoaderExt::loadAnimationPack(const uint8_t* bytes, uint32_t nbytes) {
    GLTFIO_EXT_WARN("loadAnimationPack not implemented yet");
    return nullptr;
}

void AssetLoaderExt::destroySkeleton(SkeletonAsset* skeleton) {
    delete skeleton;
}

void AssetLoaderExt::destroyMesh(MeshAsset* mesh) {
    delete mesh;
}

void AssetLoaderExt::destroyAnimation(AnimationAsset* animation) {
    delete animation;
}

void AssetLoaderExt::destroyAnimationPack(AnimationPack* pack) {
    delete pack;
}

} // namespace filament::gltfio
```

创建类似的空实现文件：
- `libs/gltfio_ext/src/SkeletonAsset.cpp`
- `libs/gltfio_ext/src/MeshAsset.cpp`
- `libs/gltfio_ext/src/AnimationAsset.cpp`
- `libs/gltfio_ext/src/AnimationPack.cpp`
- `libs/gltfio_ext/src/StandaloneAnimator.cpp`

---

## 7. 验证步骤

```bash
# 1. 配置CMake
cd out
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug ../..

# 2. 编译gltfio_ext
ninja gltfio_ext

# 3. 检查编译结果
ls -lh libs/libgltfio_ext.a

# 4. 验证符号导出
nm libs/libgltfio_ext.a | grep AssetLoaderExt
```

**预期结果**:
- 编译成功，无错误
- 生成 libgltfio_ext.a
- 符号表包含 AssetLoaderExt::create 等函数

---

## 完成标志

- [ ] 目录结构创建完成
- [ ] CMakeLists.txt配置正确
- [ ] 6个公开API头文件创建完成
- [ ] 5个内部实现头文件创建完成
- [ ] 6个cpp实现文件创建完成
- [ ] 编译通过
- [ ] libgltfio_ext.a生成成功

完成后进入 **任务2: SkeletonAsset实现**
