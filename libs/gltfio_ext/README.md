# gltfio_ext - Extended glTF Animation Loading

**[Overview](#overview)** | **[Quick Start](#quick-start)** | **[Core Concepts](#core-concepts)** | **[API Reference](#api-reference)** | **[Common Issues](#common-issues)** | **[Testing](#testing)**

> Decoupled animation loading for Filament's glTF pipeline

---

## Table of Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
  - [Prerequisites](#prerequisites)
  - [Basic Workflow](#basic-workflow)
- [Core Concepts](#core-concepts)
  - [AnimationAsset](#animationasset)
  - [Animation Cache](#animation-cache)
  - [Bone Mapping](#bone-mapping)
- [API Reference](#api-reference)
  - [AssetLoader](#assetloader)
  - [Animator](#animator)
  - [AnimationAsset API](#animationasset-api)
- [Common Issues](#common-issues)
- [Best Practices](#best-practices)
- [Testing](#testing)

---

## Overview

`gltfio_ext` extends Filament's glTF loader to support **decoupled mesh and animation assets**, enabling efficient animation reuse across multiple characters.

### Problem It Solves

Traditional glTF workflow bundles meshes and animations in a single file, leading to:
- **Duplication**: Reusing animations across characters requires duplicating animation data
- **Inefficiency**: Loading full assets just to access animation clips wastes resources
- **Inflexibility**: Difficult to manage large animation libraries separately from character models

### Key Features

- **Separate Asset Management**: Store meshes and animations in different GLB files
- **Animation Reuse**: Apply multiple animation sets (idle, walk, run) to a single mesh
- **Lightweight Loading**: Load only animation data (skeleton + keyframes), not geometry or materials
- **Multi-Animation Support**: Single GLB file can contain multiple animation clips
- **Smart Caching**: LRU cache system with source-based organization for efficient playback
- **Automatic Memory Management**: Uses `std::unique_ptr` for safe resource handling

### Typical Workflow

```
1. Blender Export → Mesh GLB (geometry + materials + skeleton)
2. Blender Export → Animation GLB (export_meshes=False, skeleton + animations only)
3. Load mesh → Load animations → Bind via bone names → Play animations
```

---

## Quick Start

### Prerequisites

⚠️ **Critical**: External animations require bone name matching. You **MUST** enable the name component manager:

```cpp
#include <gltfio_ext/AssetLoader.h>
#include <utils/NameComponentManager.h>

using namespace filament::gltfio_ext;

// Create NameComponentManager
auto* nameManager = new NameComponentManager(EntityManager::get());

// Configure AssetLoader (critical: must set names)
AssetConfiguration config;
config.engine = engine;
config.materials = materialProvider;
config.names = nameManager;  // ⚠️ Required for bone matching

AssetLoader* assetLoader = AssetLoader::create(config);
```

**Without setting `names`**:
- Bone matching will fail (0% match rate)
- `loadAnimationsFromSource()` will return 0
- Error log: "AnimationBinding: Bone mapping failed with 0% match rate"

### Basic Workflow

```cpp
#include <gltfio_ext/AssetLoader.h>
#include <gltfio_ext/Animator.h>

using namespace filament::gltfio_ext;

// 1. Load mesh asset
FilamentAsset* meshAsset = assetLoader->createAsset(meshData, meshDataSize);
resourceLoader->asyncBeginLoad(meshAsset);
while (!resourceLoader->asyncUpdateLoad()) { /* wait */ }
scene->addEntities(meshAsset->getEntities(), meshAsset->getEntityCount());

// 2. Load animation asset (returns unique_ptr, auto-managed)
auto animAsset = assetLoader->loadAnimationAsset(animData, animDataSize);
if (!animAsset) {
    utils::slog.e << "Failed to load animation asset" << utils::io::endl;
    return;
}

// 3. Load animations into cache
FilamentInstance* instance = meshAsset->getInstance();
Animator* animator = instance->getAnimator();

size_t loadedCount = animator->loadAnimationsFromSource("character_anims", animAsset.get());
if (loadedCount == 0) {
    utils::slog.e << "Failed to load animations (bone mismatch?)" << utils::io::endl;
    return;
}

// 4. Play animations in render loop
float currentTime = 0.0f;
while (running) {
    currentTime += deltaTime;

    // Play by name (convenient)
    animator->applyAnimationByName("walk", currentTime);

    // Update bone matrices
    animator->updateBoneMatrices();

    // Render...
}

// 5. Cleanup (correct order)
animator->unloadAnimationsFromSource("character_anims");  // 1. Unload first
// animAsset auto-destructs (unique_ptr)                  // 2. Auto-cleanup
assetLoader->destroyAsset(meshAsset);                     // 3. Destroy mesh last
```

---

## Core Concepts

### AnimationAsset

Lightweight container for animation data without mesh geometry.

**Key properties**:
- Shared skeleton hierarchy across all animation clips
- Supports multiple animations per file
- Automatic memory management via `std::unique_ptr`
- Query animations by index or name

**Example**:
```cpp
auto animAsset = assetLoader->loadAnimationAsset(data, size);

// Inspect animations
size_t count = animAsset->getAnimationCount();
for (size_t i = 0; i < count; ++i) {
    const auto& anim = animAsset->getAnimation(i);
    std::cout << anim.name << ": " << anim.getDuration() << "s\n";
}

// Find by name
const auto* walkAnim = animAsset->getAnimationByName("walk");
```

### Animation Cache

LRU (Least Recently Used) cache system for efficient animation management.

**Features**:
- **Source-based organization**: Group animations by source ID (e.g., "combat", "locomotion")
- **Automatic eviction**: Removes least recently used animations when cache is full
- **Name-based lookup**: Play animations by name without tracking indices
- **Statistics**: Query cache hit rate, loaded animations, and capacity

**Default settings**:
- Capacity: 100 animations (~10MB for typical characters)
- Threshold: 90% bone match rate required

**Example**:
```cpp
// Load from multiple sources
animator->loadAnimationsFromSource("combat", combatAnims.get());
animator->loadAnimationsFromSource("locomotion", locoAnims.get());

// Query cached animations
auto combatList = animator->getAnimationsInSource("combat");
auto stats = animator->getAnimationCacheStats();
std::cout << "Cache: " << stats.cachedCount << "/" << stats.maxSize << "\n";

// Unload specific source
animator->unloadAnimationsFromSource("combat");
```

### Bone Mapping

Establishes correspondence between animation skeleton and mesh entities via name matching.

**How it works**:
1. Animation GLB contains bone hierarchy with names
2. Mesh GLB contains entities with corresponding names (via NameComponentManager)
3. `AnimationBinding` matches bones by name and creates transform instance mapping
4. 90% match rate threshold ensures compatibility

**Validation**:
```cpp
// Check match rate
float matchRate = binding.getMatchRate();
if (matchRate < 0.9f) {
    utils::slog.w << "Low match rate: " << (matchRate * 100) << "%\n";

    // Inspect unmatched bones
    const auto& unmatched = binding.getUnmatchedBones();
    for (size_t nodeIdx : unmatched) {
        utils::slog.w << "Unmatched: " << animAsset->nodes[nodeIdx].name << "\n";
    }
}
```

---

## API Reference

### AssetLoader

#### loadAnimationAsset()

```cpp
std::unique_ptr<AnimationAsset> loadAnimationAsset(
    const uint8_t* bytes,
    uint32_t nbytes
);
```

Loads animation data from GLB file without creating mesh geometry.

**Parameters**:
- `bytes`: GLB file data
- `nbytes`: Data size in bytes

**Returns**: `unique_ptr<AnimationAsset>` (auto-managed), `nullptr` on failure

**Example**:
```cpp
auto animAsset = assetLoader->loadAnimationAsset(data, size);
if (!animAsset) {
    // Handle error
}
// No manual cleanup needed - automatic destruction
```

---

### Animator

#### loadAnimationsFromSource()

```cpp
size_t loadAnimationsFromSource(
    const char* sourceId,
    const AnimationAsset* animAsset
);
```

Loads all animations from an AnimationAsset into the cache with a source identifier.

**Parameters**:
- `sourceId`: Source identifier for grouping (e.g., "combat_anims")
- `animAsset`: Animation asset to load (must remain valid while cached)

**Returns**: Number of animations loaded, `0` on failure (bone mismatch < 90%)

**Notes**:
- Replaces previous animations from the same `sourceId`
- Performs bone name matching; requires NameComponentManager
- Does not take ownership of `animAsset`

---

#### applyAnimationByName()

```cpp
bool applyAnimationByName(const char* animName, float time) const;
```

Plays an animation by name, using LRU to resolve conflicts.

**Parameters**:
- `animName`: Animation name
- `time`: Playback time in seconds

**Returns**: `true` if animation found and applied, `false` otherwise

**Example**:
```cpp
if (!animator->applyAnimationByName("walk", currentTime)) {
    utils::slog.w << "Animation 'walk' not found\n";
}
```

---

#### applyAnimation()

```cpp
bool applyAnimation(
    const char* sourceId,
    const char* animName,
    float time
) const;
```

Plays an animation from a specific source (precise control).

**Parameters**:
- `sourceId`: Source identifier
- `animName`: Animation name
- `time`: Playback time in seconds

**Returns**: `true` if animation found and applied, `false` otherwise

---

#### unloadAnimationsFromSource()

```cpp
void unloadAnimationsFromSource(const char* sourceId);
```

Removes all animations from a specific source from the cache.

**Important**: Call this before destroying the associated `AnimationAsset`.

---

#### getAnimationsInSource()

```cpp
std::vector<std::string> getAnimationsInSource(const char* sourceId) const;
```

Retrieves list of animation names in a source.

**Returns**: Vector of animation names, empty if source not found

---

#### getAnimationCacheStats()

```cpp
AnimationCacheStats getAnimationCacheStats() const;

struct AnimationCacheStats {
    size_t cachedCount;   // Current animations in cache
    size_t maxSize;       // Cache capacity
    size_t sourceCount;   // Number of loaded sources
    uint64_t hitCount;    // Cache hits
    uint64_t missCount;   // Cache misses
    float hitRate;        // Hit rate percentage
};
```

Returns cache statistics for diagnostics and optimization.

---

#### setAnimationCacheSize()

```cpp
void setAnimationCacheSize(size_t maxAnimations);
```

Adjusts cache capacity (default: 100).

**Note**: If new size < current cached count, least recently used animations are evicted.

---

### AnimationAsset API

```cpp
class AnimationAsset {
public:
    // Multi-animation access
    size_t getAnimationCount() const;
    const Animation& getAnimation(size_t index) const;
    const Animation* getAnimationByName(const std::string& name) const;

    // Node queries
    int findNodeByName(const std::string& name) const;

    // Data members
    std::vector<AnimationNode> nodes;                           // Skeleton hierarchy
    std::vector<Animation> animations;                          // Animation clips
    std::vector<filament::math::mat4f> inverseBindMatrices;    // Skin data
};

struct Animation {
    std::string name;
    std::vector<AnimationChannel> channels;
    std::vector<AnimationSampler> samplers;

    float getDuration() const;
    bool validate() const;
};
```

---

## Common Issues

### 1. Bone Matching Failures

| Symptom | Cause | Solution |
|---------|-------|----------|
| `Bone mapping failed with 0% match rate` | NameComponentManager not set | Set `config.names = nameManager` in AssetLoader |
| `Bone mapping failed with X% match rate (< 90%)` | Mesh and animation from different models | Ensure same source file; check Blender export settings |
| Some bones not matched | Bone names modified during export | Use `binding.getUnmatchedBones()` to inspect; verify Blender settings |

**Diagnostics**:
```cpp
// Get unmatched bone list
const auto& unmatchedBones = binding.getUnmatchedBones();
for (size_t nodeIndex : unmatchedBones) {
    const auto& node = animAsset->nodes[nodeIndex];
    utils::slog.w << "Unmatched bone: " << node.name << "\n";
}
```

---

### 2. Lifecycle Errors

**Crash when playing animations after unload**:

```cpp
// ❌ Wrong order
// animAsset auto-destructs
animator->unloadAnimationsFromSource("source");  // Too late! Crashes

// ✅ Correct order
animator->unloadAnimationsFromSource("source");  // 1. Unload first
// animAsset auto-destructs safely                // 2. Then destroy
```

**Check before playback**:
```cpp
if (animator->hasSource("combat")) {
    animator->applyAnimation("combat", "slash", time);
} else {
    utils::slog.w << "Source 'combat' not loaded\n";
}
```

---

### 3. Invalid Animation Index

**Legacy index-based API** requires careful tracking:

```cpp
size_t internalCount = animator->getAnimationCount();  // e.g., 3
animator->loadAnimationsFromSource("external", animAsset.get());  // +2 animations
size_t totalCount = animator->getAnimationCount();     // Now 5

// ✅ Correct usage (name-based API recommended)
animator->applyAnimationByName("walk", time);

// ❌ Risky: manual index calculation
// animator->applyAnimation(3, time);  // Fragile if internal count changes
```

---

## Best Practices

### Error Handling

Always check return values:

```cpp
// Check asset loading
auto animAsset = assetLoader->loadAnimationAsset(data, size);
if (!animAsset) {
    utils::slog.e << "Failed to load animation asset\n";
    return;
}

// Check cache loading
size_t loaded = animator->loadAnimationsFromSource("source", animAsset.get());
if (loaded == 0) {
    utils::slog.e << "Failed to load animations (bone mismatch?)\n";
    return;
}
```

### Memory Management

Use `unique_ptr` correctly:

```cpp
// ✅ Automatic cleanup
{
    auto animAsset = assetLoader->loadAnimationAsset(data, size);
    animator->loadAnimationsFromSource("temp", animAsset.get());
    // ...
    animator->unloadAnimationsFromSource("temp");
}  // animAsset auto-destructs here

// ✅ Explicit reset
auto animAsset = assetLoader->loadAnimationAsset(data, size);
// ... use ...
animator->unloadAnimationsFromSource("source");
animAsset.reset();  // Explicit destruction
```

### Performance Tips

1. **Preload common animations** at startup to avoid runtime hitches
2. **Use source IDs** to organize animations logically ("locomotion", "combat", "emotes")
3. **Monitor cache stats** to optimize capacity:
   ```cpp
   auto stats = animator->getAnimationCacheStats();
   if (stats.hitRate < 80.0f) {
       animator->setAnimationCacheSize(stats.maxSize * 2);  // Increase capacity
   }
   ```
4. **Prefer name-based API** over index-based for maintainability

---

## Testing

Run the comprehensive test suite to verify your setup:

```bash
cd out/cmake-debug/libs/gltfio_ext

# Run all tests
./run_tests.sh

# Or run individually
./test_asset_loader       # Asset loading and validation
./test_animation_binding  # Bone mapping and matching
./test_animator           # Animation playback and lifecycle
./test_animation_cache    # Cache system and LRU eviction
```

**Test Coverage**:
- **Unit tests**: Data structures, validation, error handling
- **Integration tests**: Complete workflows, multi-instance scenarios
- **Lifecycle tests**: Destruction order, memory safety
- **Edge cases**: Invalid inputs, bone mismatches, cache eviction

**Test Assets**: Uses real-world ecorche model (327 bones, 3 animations, 100% match rate)

For detailed test documentation, see [`test/README.md`](test/README.md).

---

## Additional Resources

- **API Documentation**: See header docstrings in `include/gltfio_ext/*.h`
- **glTF Specification**: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
- **Filament Documentation**: https://google.github.io/filament/
- **Material Providers**: See Filament's gltfio documentation for MaterialProvider and TextureProvider setup
