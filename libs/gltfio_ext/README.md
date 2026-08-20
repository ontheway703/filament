# gltfio_ext - Extended glTF Animation Loading

**[Overview](#overview)** | **[Compatibility](#gltfio-compatibility)** | **[Quick Start](#quick-start)** | **[Core Concepts](#core-concepts)** | **[API Reference](#api-reference)** | **[Best Practices](#best-practices)** | **[Testing](#testing)**

> Decoupled animation loading for Filament's glTF pipeline - with multi-instance support and smart caching

---

## Table of Contents

- [Overview](#overview)
- [gltfio Compatibility](#gltfio-compatibility)
  - [Migration from gltfio](#migration-from-gltfio)
  - [Enhanced Features](#enhanced-features)
  - [Internal vs External Animations](#internal-vs-external-animations)
  - [Deprecated APIs](#deprecated-apis)
  - [Synchronization with gltfio](#synchronization-with-gltfio)
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
  - [AnimationBinding](#animationbinding)
  - [AnimationAsset API](#animationasset-api)
- [Common Issues](#common-issues)
- [Best Practices](#best-practices)
  - [Error Handling](#error-handling)
  - [Memory Management](#memory-management)
  - [Performance Tips](#performance-tips)
  - [Multi-Instance Scenarios](#multi-instance-scenarios)
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
- **Multi-Instance Support**: Share mesh data across multiple characters with independent animations
- **Lightweight Loading**: Load only animation data (skeleton + keyframes), not geometry or materials
- **Multi-Animation Support**: Single GLB file can contain multiple animation clips
- **Smart Caching**: LRU cache system with source-based organization for efficient playback
- **Automatic Memory Management**: Uses `std::unique_ptr` for safe resource handling
- **Bone Matrix Updates**: Inherited updateBoneMatrices() and resetBoneMatrices() from gltfio

### Typical Workflow

```
1. Blender Export → Mesh GLB (geometry + materials + skeleton)
2. Blender Export → Animation GLB (export_meshes=False, skeleton + animations only)
3. Load mesh → Load animations → Bind via bone names → Play animations
```

---

## gltfio Compatibility

`gltfio_ext` is **fully backward compatible** with Filament's original `gltfio` library. You can drop in `gltfio_ext` as a replacement by simply changing the namespace.

### Migration from gltfio

**Simple namespace change**:
```cpp
// Before (gltfio)
using namespace filament::gltfio;

// After (gltfio_ext)
using namespace filament::gltfio_ext;
```

**All existing APIs work identically**:
```cpp
// Legacy index-based API (for internal animations)
size_t count = animator->getAnimationCount();           // Returns internal animation count
const char* name = animator->getAnimationName(0);       // Get animation name by index
float duration = animator->getAnimationDuration(0);     // Get animation duration
animator->applyAnimation(0, time);                      // Play animation by index

// Bone matrix updates (inherited from gltfio)
animator->updateBoneMatrices();
animator->resetBoneMatrices();
```

### Enhanced Features

While maintaining full backward compatibility, `gltfio_ext` adds:

- **External animation loading**: Load animations from separate GLB files
- **Animation cache**: LRU cache with source-based organization
- **Name-based playback**: Play animations by name without tracking indices
- **Multi-instance support**: Independent animations for characters sharing mesh data

### Internal vs External Animations

`gltfio_ext` distinguishes between two types of animations:

| Type | Source | Access Method | Use Case |
|------|--------|---------------|----------|
| **Internal** | Embedded in mesh GLB | Index-based API (`applyAnimation(index, time)`) | Legacy gltfio workflow |
| **External** | Loaded via cache | Name-based API (`applyAnimationByName(name, time)`) | New decoupled workflow |

**Example**:
```cpp
// Load mesh with 3 embedded animations
FilamentAsset* meshAsset = loader->createAsset(meshData, meshSize);
Animator* animator = meshAsset->getInstance()->getAnimator();

// Legacy API: works with internal animations
size_t internalCount = animator->getAnimationCount();  // Returns 3
animator->applyAnimation(0, time);                     // Play "Walk"
animator->applyAnimation(1, time);                     // Play "Run"

// Load external animations into cache
auto externalAnims = loader->loadAnimationAsset(animData, animSize);
animator->loadAnimationsFromSource("combat", externalAnims.get());

// New API: works with both internal and external animations
animator->applyAnimationByName("Walk", time);           // Internal animation
animator->applyAnimation("combat", "Slash", time);      // External animation

// Internal count unchanged - external animations stored separately
size_t stillThree = animator->getAnimationCount();     // Still returns 3
```

### Deprecated APIs

The following methods are **deprecated but fully functional** for backward compatibility:

```cpp
void applyAnimation(size_t animationIndex, float time) const;
size_t getAnimationCount() const;
float getAnimationDuration(size_t animationIndex) const;
const char* getAnimationName(size_t animationIndex) const;
```

**Recommendation**: For new code, prefer name-based APIs (`applyAnimationByName()`, `applyAnimation(sourceId, name, time)`).

### Synchronization with gltfio

`gltfio_ext` is a fork of `gltfio` and can be manually synchronized when gltfio receives updates:

- **Compatible**: Core animation and rendering APIs remain stable
- **Manual sync**: Apply gltfio changes by namespace replacement and conflict resolution
- **Independent development**: gltfio_ext features (cache, external animations) are separate additions

See `SYNC_GUIDE.md` for detailed synchronization procedures.

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

**Multi-Instance Support**:
- `createForAsset()`: Bind animations to the base FilamentAsset (simple scenario)
- `createForInstance()`: Bind animations to a specific FilamentInstance (multi-character scenario)

**Usage Example**:
```cpp
// Single instance
auto binding = AnimationBinding::createForAsset(animAsset, meshAsset, engine);

// Multiple instances (share mesh, independent transforms)
FilamentInstance* character1 = meshAsset->getInstance();  // Base instance
FilamentInstance* character2 = loader->createInstance(meshAsset);

auto binding1 = AnimationBinding::createForInstance(animAsset, character1, engine);
auto binding2 = AnimationBinding::createForInstance(animAsset, character2, engine);
```

**Validation**:
```cpp
// Check match rate
float matchRate = binding->getMatchRate();
if (matchRate < 0.9f) {
    utils::slog.w << "Low match rate: " << (matchRate * 100) << "%\n";

    // Inspect unmatched bones
    const auto& unmatched = binding->getUnmatchedBones();
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

#### updateBoneMatrices()

```cpp
void updateBoneMatrices();
```

Updates bone matrices for skinned meshes, applying the current animation pose to renderable geometry.

**Usage**:
```cpp
// In render loop
animator->applyAnimationByName("walk", currentTime);
animator->updateBoneMatrices();  // Apply transforms to GPU
```

**Notes**:
- Inherited from gltfio's Animator
- Must be called after applyAnimation() to see visual changes
- Sends bone transforms to RenderableManager
- Required for skinned mesh rendering

---

#### resetBoneMatrices()

```cpp
void resetBoneMatrices();
```

Resets bone matrices to their bind pose (T-pose), removing all animation transforms.

**Usage**:
```cpp
// Stop animation and return to bind pose
animator->resetBoneMatrices();
```

**Notes**:
- Inherited from gltfio's Animator
- Useful for debugging or returning to default pose
- Does not affect animation cache or loaded animations

---

### AnimationBinding

#### createForAsset()

```cpp
static std::unique_ptr<AnimationBinding> createForAsset(
    const AnimationAsset* externalAnim,
    const FilamentAsset* meshAsset,
    filament::Engine* engine
);
```

Creates an AnimationBinding for a FilamentAsset (simple scenario).

**Parameters**:
- `externalAnim`: External animation asset (must remain valid during binding lifetime)
- `meshAsset`: Mesh asset that provides both entities and names
- `engine`: Filament engine instance

**Returns**: `unique_ptr<AnimationBinding>` (auto-managed), `nullptr` on failure

**Usage**:
```cpp
auto binding = AnimationBinding::createForAsset(animAsset, meshAsset, engine);
if (binding && binding->getMatchRate() >= 0.9f) {
    // Binding successful, use with Animator
}
```

---

#### createForInstance()

```cpp
static std::unique_ptr<AnimationBinding> createForInstance(
    const AnimationAsset* externalAnim,
    const FilamentInstance* instance,
    filament::Engine* engine
);
```

Creates an AnimationBinding for a FilamentInstance (multi-instance scenario).

**Parameters**:
- `externalAnim`: External animation asset (must remain valid during binding lifetime)
- `instance`: Instance that provides entities (automatically uses owner asset for names)
- `engine`: Filament engine instance

**Returns**: `unique_ptr<AnimationBinding>` (auto-managed), `nullptr` on failure

**Usage**:
```cpp
// Create multiple instances with independent animations
FilamentInstance* npc1 = loader->createInstance(baseAsset);
FilamentInstance* npc2 = loader->createInstance(baseAsset);

auto binding1 = AnimationBinding::createForInstance(idleAnim, npc1, engine);
auto binding2 = AnimationBinding::createForInstance(walkAnim, npc2, engine);
```

**Notes**:
- Ideal for scenarios with multiple characters sharing the same mesh
- Each instance has independent transform state
- Bindings can use different animations for different instances

---

#### getMatchRate()

```cpp
float getMatchRate() const;
```

Returns the percentage of animation bones successfully matched to mesh entities (0.0 to 1.0).

**Threshold**: 0.9 (90%) is the minimum for reliable animation playback.

---

#### getUnmatchedBones()

```cpp
const std::vector<size_t>& getUnmatchedBones() const;
```

Returns indices of animation nodes that failed to match mesh entities.

**Usage**:
```cpp
if (binding->getMatchRate() < 0.9f) {
    for (size_t idx : binding->getUnmatchedBones()) {
        utils::slog.w << "Unmatched: " << animAsset->nodes[idx].name << "\n";
    }
}
```

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

### Multi-Instance Scenarios

Use `createInstance()` to share mesh data across multiple characters:

```cpp
// Load base mesh once
FilamentAsset* baseMesh = loader->createAsset(meshData, meshSize);
resourceLoader->loadResources(baseMesh);

// Create multiple instances (share geometry, independent transforms)
FilamentInstance* npc1 = baseMesh->getInstance();           // Base instance
FilamentInstance* npc2 = loader->createInstance(baseMesh);  // Additional instance
FilamentInstance* npc3 = loader->createInstance(baseMesh);

// Load animation sets
auto idleAnim = loader->loadAnimationAsset(idleData, idleSize);
auto walkAnim = loader->loadAnimationAsset(walkData, walkSize);

// Each instance can have different animations
Animator* animator1 = npc1->getAnimator();
Animator* animator2 = npc2->getAnimator();
Animator* animator3 = npc3->getAnimator();

animator1->loadAnimationsFromSource("npc1_anims", idleAnim.get());
animator2->loadAnimationsFromSource("npc2_anims", walkAnim.get());
animator3->loadAnimationsFromSource("npc3_anims", idleAnim.get());

// In render loop - each character animates independently
animator1->applyAnimationByName("idle", time);
animator2->applyAnimationByName("walk", time);
animator3->applyAnimationByName("idle", time + 0.5f);  // Phase offset

animator1->updateBoneMatrices();
animator2->updateBoneMatrices();
animator3->updateBoneMatrices();
```

**Benefits**:
- ✅ Shared geometry saves GPU memory (one vertex buffer for all instances)
- ✅ Independent transforms allow different positions/rotations
- ✅ Independent animations enable diverse character behaviors
- ✅ Efficient for crowd rendering or multiple NPCs

---

## Testing

Run the comprehensive test suite to verify your setup:

```bash
cd out/cmake-debug/libs/gltfio_ext

# Run all tests (recommended)
./run_tests.sh

# Or run individually
./test_animation_asset          # AnimationAsset data structures (21 tests)
./test_gltfio_ext               # Basic glTF asset loading (3 tests)
./test_asset_loader             # loadAnimationAsset() API (12 tests)
./test_animation_binding        # Bone mapping & multi-instance (9 tests)
./test_bone_matrices            # Bone matrix updates (5 tests, skipped on NOOP backend)
./test_animator_lifecycle       # Resource lifecycle and memory safety (9 tests)
./test_animator_playback        # Source management and playback (11 tests)
./test_animator_cache           # Cache integration (3 tests)
./test_animator_crossfade       # Animation blending (7 tests)
./test_animation_cache          # LRU cache system (34 tests)
```

### Test Statistics

**Total**: 117 test cases across 10 test files (112 passing + 5 skipped)

| Test File | Tests | Status | Coverage |
|-----------|-------|--------|----------|
| `test_animation_asset` | 21 | ✅ Pass | AnimationAsset validation, multi-animation support |
| `test_gltfio_ext` | 3 | ✅ Pass | FilamentAsset loading, materials, transforms |
| `test_asset_loader` | 12 | ✅ Pass | Animation-only GLB loading, skin data extraction |
| `test_animation_binding` | 9 | ✅ Pass | **Bone name mapping, createForInstance() multi-instance** |
| `test_bone_matrices` | 5 | ⏭️ Skip | updateBoneMatrices() (NOOP backend limitation) |
| `test_animator_lifecycle` | 9 | ✅ Pass | Safe destruction, load/unload cycles |
| `test_animator_playback` | 13 | ✅ Pass | Source loading, playback, **gltfio compatibility** |
| `test_animator_cache` | 3 | ✅ Pass | Cache integration with Animator |
| `test_animator_crossfade` | 7 | ✅ Pass | Cross-fade, alpha blending, multi-animator sync |
| `test_animation_cache` | 34 | ✅ Pass | **LRU eviction, hit rate, cache statistics** |

### Test Architecture

Tests are organized by functional layers:

**Core Layer** (AnimationAsset, AssetLoader):
- Data validation, multi-animation support, error handling
- Animation-only GLB loading (export_meshes=False in Blender)
- Skin data extraction (joints, inverseBindMatrices)

**Integration Layer** (AnimationBinding):
- Bone name mapping with 90% match rate threshold
- **Multi-instance support**: `createForAsset()` and `createForInstance()`
- Entity mapping validation, unmatched bone diagnostics

**Playback Layer** (Animator, AnimationCache):
- External animation loading and source management
- Name-based animation playback
- LRU cache eviction, hit rate optimization
- Cross-fade and alpha blending

**Benefits**:
- ✅ **Comprehensive coverage**: 115 tests covering all gltfio_ext features
- ✅ **Real-world validation**: Uses ecorche model (327 bones, 3 animations, 100% match rate)
- ✅ **Multi-instance testing**: Verifies independent animation bindings
- ✅ **Cache performance**: Validates LRU eviction and hit rate tracking
- ✅ **Memory safety**: Lifecycle tests ensure proper resource cleanup

---

## Additional Resources

- **API Documentation**: See header docstrings in `include/gltfio_ext/*.h`
- **glTF Specification**: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
- **Filament Documentation**: https://google.github.io/filament/
- **Material Providers**: See Filament's gltfio documentation for MaterialProvider and TextureProvider setup
