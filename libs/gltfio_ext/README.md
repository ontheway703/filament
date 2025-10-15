# gltfio_ext - Filament glTF Separate Loading Extension

Extends Filament's gltfio with support for separate loading of skeletons, meshes, and animations.

## Features

- **Separate Loading**: Load skeleton, mesh, and animation from different glTF files
- **Flexible Binding**: One skeleton can drive multiple meshes (equipment system)
- **Runtime Animation Switching**: Load and play animations independently
- **Batch Loading**: Load multiple animations in one go with AnimationPack
- **Name-based Binding**: Use bone names for delayed binding (no Entity dependency)
- **Performance Optimized**: Name-to-index mapping cache

## Quick Example

```cpp
#include <gltfio/AssetLoaderExt.h>

// Create loader
AssetConfigurationExt config = { .engine = engine, .materials = materials };
AssetLoaderExt* loader = AssetLoaderExt::create(config);

// Load assets separately
SkeletonAsset* skeleton = loader->loadSkeleton(skelBytes, skelSize);
MeshAsset* mesh = loader->loadMesh(meshBytes, meshSize);
AnimationAsset* anim = loader->loadAnimation(animBytes, animSize);

// Bind and animate
mesh->bindSkeleton(skeleton);
StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
animator->bindSkeleton(skeleton);
animator->playAnimation(anim);

// Update loop
while (rendering) {
    animator->update(deltaTime);
    mesh->updateSkinning(animator);
    renderer->render(view);
}
```

## Building

```bash
cd out
cmake -G Ninja ../..
ninja gltfio_ext
```

## Testing

```bash
# Build all tests
ninja test_skeleton test_mesh test_animation test_animator test_integration

# Run individual tests
./libs/gltfio_ext/test_skeleton
./libs/gltfio_ext/test_mesh
./libs/gltfio_ext/test_animation
./libs/gltfio_ext/test_animator
./libs/gltfio_ext/test_integration
```

## Architecture

### Core Components

- **SkeletonAsset**: Bone hierarchy + inverse bind matrices
  - `getBoneCount()` - Number of bones
  - `getBoneIndex(name)` - Name to index lookup
  - `getBoneEntity(index)` - Get bone entity
  - `getInverseBindMatrices()` - Get inverse bind matrix array

- **MeshAsset**: Geometry + skinning data
  - `bindSkeleton(skeleton)` - Bind to skeleton
  - `updateSkinning(animator)` - Update from animator
  - `getRenderableEntity()` - Get Filament entity

- **AnimationAsset**: Animation channels (name-based)
  - `getName()` - Animation name
  - `getDuration()` - Animation duration
  - `getChannelCount()` - Number of channels
  - `getChannelTargetBone(index)` - Target bone name

- **AnimationPack**: Batch-loaded animations
  - `getAnimationCount()` - Number of animations
  - `getAnimation(index)` - Get by index
  - `findAnimation(name)` - Find by name

- **StandaloneAnimator**: Independent animation playback
  - `bindSkeleton(skeleton)` - Bind skeleton
  - `playAnimation(anim, weight, loop)` - Play animation
  - `update(deltaTime)` - Update per frame
  - `getBoneMatrices()` - Get final bone matrices

- **AssetLoaderExt**: Unified loader interface
  - `loadSkeleton(bytes, size)` - Load skeleton
  - `loadMesh(bytes, size)` - Load mesh
  - `loadAnimation(bytes, size)` - Load single animation
  - `loadAnimationPack(bytes, size)` - Load all animations

## Use Cases

### Equipment System

```cpp
// Load character skeleton once
SkeletonAsset* skeleton = loader->loadSkeleton(characterData, size);

// Load multiple equipment meshes
MeshAsset* body = loader->loadMesh(bodyData, bodySize);
MeshAsset* armor = loader->loadMesh(armorData, armorSize);
MeshAsset* weapon = loader->loadMesh(weaponData, weaponSize);

// All share the same skeleton
body->bindSkeleton(skeleton);
armor->bindSkeleton(skeleton);
weapon->bindSkeleton(skeleton);

// One animator drives all
StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
animator->bindSkeleton(skeleton);
animator->playAnimation(walkAnim);

// Update all meshes
animator->update(dt);
body->updateSkinning(animator);
armor->updateSkinning(animator);
weapon->updateSkinning(animator);
```

### Runtime Animation Loading

```cpp
// Load skeleton and mesh once
SkeletonAsset* skeleton = loader->loadSkeleton(data, size);
MeshAsset* mesh = loader->loadMesh(data, size);
mesh->bindSkeleton(skeleton);

// Create animator
StandaloneAnimator* animator = StandaloneAnimator::create(*engine);
animator->bindSkeleton(skeleton);

// Load and play animations on demand
AnimationAsset* idle = loader->loadAnimation(idleData, idleSize);
int idleId = animator->playAnimation(idle, 1.0f, true);

// Later: switch to different animation
animator->stopAnimation(idleId);
AnimationAsset* run = loader->loadAnimation(runData, runSize);
animator->playAnimation(run, 1.0f, true);
```

## Dependencies

- Filament (gltfio_core only, no filamat dependency)
- cgltf (via gltfio_core)
- math, utils

## Design Decisions

1. **Name-based Binding**: Uses bone names instead of Entity pointers
   - Enables cross-file loading
   - Delays binding until runtime
   - Caches name→index mapping for performance

2. **No filamat Dependency**: Links only gltfio_core
   - Smaller binary size
   - Faster compilation
   - Cleaner separation

3. **Independent Components**: Each asset type is self-contained
   - Skeleton doesn't depend on Mesh
   - Animation doesn't depend on Skeleton
   - Flexible composition

4. **Manual Resource Management**: Explicit destroy methods
   - Clear ownership semantics
   - No hidden allocations
   - Compatible with Filament's design

## Performance Characteristics

- **Name Lookup**: O(1) after initial binding (cached indices)
- **Animation Update**: O(channels) per frame
- **Bone Matrix Calculation**: O(bones) per frame
- **Memory**: ~1KB per bone + animation data

## Limitations

- Simplified skinning vertex attributes (POSITION only in example)
- No morph target support (WEIGHTS channels ignored)
- Single skeleton per mesh (standard glTF limitation)
- Manual glTF parsing (uses cgltf directly)

## License

Same as Filament (Apache 2.0)
