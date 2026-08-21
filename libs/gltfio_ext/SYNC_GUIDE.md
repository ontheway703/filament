# gltfio_ext Synchronization Guide

This document provides step-by-step instructions for synchronizing `gltfio_ext` with upstream `gltfio` updates.

---

## Table of Contents

- [Overview](#overview)
- [Synchronization Strategy](#synchronization-strategy)
- [Pre-Sync Checklist](#pre-sync-checklist)
- [Step-by-Step Process](#step-by-step-process)
- [Conflict Resolution](#conflict-resolution)
- [Post-Sync Validation](#post-sync-validation)
- [Rollback Procedure](#rollback-procedure)

---

## Overview

`gltfio_ext` is a **fork** of Filament's `gltfio` library with the following relationship:

```
libs/gltfio (upstream)
    ↓
libs/gltfio_ext (fork with enhancements)
```

**Fork characteristics**:
- Same namespace structure: `filament::gltfio` → `filament::gltfio_ext`
- Full backward compatibility: All gltfio APIs remain functional
- Enhanced features: External animation loading, cache system, multi-instance support
- Manual synchronization: Updates from gltfio must be manually applied

**Goals**:
1. ✅ Keep core rendering and animation logic synchronized with gltfio
2. ✅ Maintain backward compatibility with gltfio APIs
3. ✅ Preserve gltfio_ext enhancements (cache, external animations)
4. ✅ Pass all existing tests after synchronization

---

## Synchronization Strategy

### What to Sync

Apply these changes from `gltfio` to `gltfio_ext`:

| Component | Sync Strategy | Files |
|-----------|--------------|-------|
| **Core Animation** | Full sync | `Animator.cpp`, animation playback logic |
| **glTF Parsing** | Full sync | `AssetLoader.cpp`, FFilamentAsset.cpp |
| **Rendering** | Full sync | Material/texture handling |
| **Bug Fixes** | Full sync | All bug fixes from gltfio |
| **Performance** | Full sync | Optimization improvements |

### What NOT to Change

**Preserve these gltfio_ext additions** (do NOT overwrite with gltfio):

| Component | Location | Description |
|-----------|----------|-------------|
| **AnimationAsset** | `include/AnimationAsset.h`, `src/AnimationAsset.cpp` | External animation container |
| **AnimationBinding** | `include/AnimationBinding.h`, `src/AnimationBinding.cpp` | Bone name matching |
| **Animation Cache** | `src/AnimationCache.h`, `src/AnimationCache.cpp` | LRU cache system |
| **Cache APIs** | `Animator.h` lines 193-313, `Animator.cpp` cache methods | `loadAnimationsFromSource()`, etc. |
| **Multi-Instance** | Instance animator creation logic | Independent animator per instance |
| **Test Suite** | `test/*.cpp` | All gltfio_ext tests |
| **Documentation** | `README.md`, `SYNC_GUIDE.md` | gltfio_ext documentation |

### Key Principles

1. **Namespace Replacement**: All `filament::gltfio` → `filament::gltfio_ext`
2. **API Preservation**: Never remove gltfio APIs (backward compatibility requirement)
3. **Additive Changes**: gltfio_ext enhancements supplement, not replace, gltfio features
4. **Test-Driven**: Classify every test as passed, failed, or skipped; a missing dependency is a failure, not a pass

### Current Sync Baseline

- Upstream baseline: official Filament tag `v1.75.0` (`0e58877c09afb1aacd09ff640f74d2adcd2a7e80`)
- Previous upstream baseline: official Filament tag `v1.71.4` (`a0ecdbbeba5f1005bbad0a4c8b2fe6955788cdee`)
- Fixture source: deterministic generators under `test/fixtures/`, verified by SHA-256
- Expected host result: 129 tests total, 124 passed, 5 explicitly skipped, 0 failed

The five skips exercise bone-matrix updates on the NOOP backend. They are not evidence that the
327- or 512-bone path works on a real GPU; release validation still requires Metal and Android GPU
smoke tests.

---

## Pre-Sync Checklist

Before starting synchronization:

- [ ] **Backup current state**: Create a git branch for rollback
  ```bash
  git checkout -b pre-sync-backup
  git checkout animation_cache  # Return to working branch
  ```

- [ ] **Run baseline tests**: Verify all tests pass before sync
  ```bash
  ./libs/gltfio_ext/run_tests.sh
  # Expected: 129 tests (124 passing + 5 explicitly skipped)
  ```

- [ ] **Review gltfio changes**: Inspect upstream commits
  ```bash
  cd libs/gltfio
  git log --oneline --since="2024-01-01" -- .
  git diff <last-sync-commit> HEAD -- .
  ```

- [ ] **Identify conflict areas**: Look for changes in:
  - `Animator.cpp` (most likely conflict zone)
  - `AssetLoader.cpp` (loadAnimationAsset additions)
  - `FilamentAsset.h` / `FilamentInstance.h` (multi-instance support)

- [ ] **Allocate time**: Plan 2-4 hours for sync + testing

---

## Step-by-Step Process

### Step 1: Create Sync Branch

```bash
cd /path/to/filament
git checkout animation_cache
git checkout -b sync-gltfio-$(date +%Y%m%d)
```

### Step 2: Review Upstream Changes

```bash
cd libs/gltfio
git log --oneline --graph --decorate --since="2024-01-01" -- .
```

**Identify change categories**:
- 🐛 Bug fixes (high priority)
- ⚡ Performance improvements (high priority)
- ✨ New features (evaluate case-by-case)
- 📝 Documentation (optional)

### Step 3: Apply Changes File-by-File

For each modified file in gltfio:

#### 3a. Core Files (Full Sync)

**Example: Animator.cpp bug fix**

```bash
# View gltfio change
cd libs/gltfio
git show <commit-hash> -- src/Animator.cpp

# Apply to gltfio_ext (manual merge)
cd ../gltfio_ext
# Edit src/Animator.cpp
# - Apply the bug fix from gltfio
# - Keep cache-related code intact
# - Replace namespace: gltfio → gltfio_ext
```

**Critical sections in Animator.cpp**:

| Line Range | Content | Sync Strategy |
|------------|---------|--------------|
| 1-400 | Core animation structures | Full sync |
| 401-1500 | Animation playback logic | Full sync, preserve cache calls |
| 1501-1800 | Cache system implementation | **DO NOT OVERWRITE** |
| 1801-1900 | Multi-instance support | **DO NOT OVERWRITE** |
| 1901-1957 | gltfio compatibility APIs | **DO NOT OVERWRITE** |

#### 3b. Preserved Files (No Sync)

**Skip these files entirely** (gltfio_ext exclusive):

```bash
# DO NOT sync these files
include/gltfio_ext/AnimationAsset.h
include/gltfio_ext/AnimationBinding.h
src/AnimationAsset.cpp
src/AnimationBinding.cpp
src/AnimationCache.h
src/AnimationCache.cpp
test/*.cpp
README.md
SYNC_GUIDE.md
```

#### 3c. Header Files (Careful Merge)

**Example: Animator.h**

```diff
// gltfio change (example):
+ void newFeature();  // New method in gltfio

// Merge into gltfio_ext Animator.h:
// 1. Add new method before cache API section
// 2. Keep cache APIs (lines 193-313) unchanged
// 3. Keep gltfio compatibility APIs (lines 90-154) unchanged
```

### Step 4: Namespace Replacement

After applying changes, verify namespace consistency:

```bash
cd libs/gltfio_ext

# Check for accidental gltfio namespace usage
grep -r "namespace filament::gltfio[^_]" include/ src/
grep -r "using namespace.*gltfio[^_]" include/ src/

# Should find zero matches (all should be gltfio_ext)
```

### Step 5: Build Verification

```bash
cd /path/to/filament
./build.sh debug

# Check for compilation errors
# Fix any namespace issues or missing includes
```

---

## Conflict Resolution

### Common Conflicts

#### Conflict 1: Animator.cpp - Animation Playback Logic

**Scenario**: gltfio modifies `applyAnimation(size_t, float)` implementation

**Resolution**:
```cpp
// gltfio_ext has TWO applyAnimation methods:
// 1. void applyAnimation(size_t index, float time) const;  // Legacy API
// 2. bool applyAnimation(const char* sourceId, const char* animName, float time) const;  // Cache API

// If gltfio updates method #1:
// - Apply the fix to gltfio_ext's implementation at line ~1910
// - Ensure it only accesses mImpl->animations[0...mInternalAnimCount-1]
// - Do NOT modify method #2 (cache API)
```

#### Conflict 2: AssetLoader - loadAnimationAsset()

**Scenario**: gltfio adds a new loading method

**Resolution**:
- gltfio_ext already has `loadAnimationAsset()`
- If gltfio adds similar functionality, compare implementations
- Prefer gltfio_ext version (already returns AnimationAsset)
- If gltfio version is superior, adapt to return AnimationAsset

#### Conflict 3: Multi-Instance Animator Creation

**Scenario**: gltfio changes FilamentInstance initialization

**Resolution**:
```cpp
// gltfio_ext creates Animator per instance:
// FilamentInstance.cpp line ~X:
mAnimator = new Animator(mAsset, this);  // gltfio_ext enhancement

// If gltfio changes initialization:
// - Apply changes but preserve per-instance Animator creation
// - Verify multi-instance tests still pass
```

### Conflict Resolution Checklist

- [ ] Compare gltfio change intent vs gltfio_ext implementation
- [ ] Preserve gltfio_ext cache-related code
- [ ] Maintain backward compatibility (don't remove old APIs)
- [ ] Update both implementations if method is overloaded
- [ ] Test affected functionality after merge

---

## Post-Sync Validation

### Phase 1: Compilation

```bash
./build.sh debug
# Expected: Clean build with no errors
```

### Phase 2: Unit Tests

```bash
cd out/cmake-debug/libs/gltfio_ext
./run_tests.sh
```

**Expected results**:
```
========================================
gltfio_ext 单元测试套件
========================================

✓ test_animation_asset: 22 tests passed
✓ test_gltfio_ext: 10 tests passed
✓ test_asset_loader: 12 tests passed
✓ test_animation_binding: 9 tests passed
✓ test_instance_lifecycle: 5 tests passed
⊘ test_bone_matrices: 5 tests skipped (NOOP backend)
✓ test_animator_lifecycle: 9 tests passed
✓ test_animator_playback: 14 tests passed  # Including compatibility tests
✓ test_animator_cache: 3 tests passed
✓ test_animator_crossfade: 7 tests passed
✓ test_animation_cache: 33 tests passed

========================================
测试总结
========================================
总测试数: 129
通过: 124
跳过: 5
失败: 0

所有可执行测试通过！ (5 个测试因环境限制跳过)
```

### Phase 3: Compatibility Validation

**Run gltfio compatibility tests explicitly**:

```bash
cd out/cmake-debug/libs/gltfio_ext
./test_animator_playback --gtest_filter="*Compatibility*"
```

**Expected**:
```
[ RUN      ] AnimatorTest.GltfioCompatibilityIndexBasedAPI
[       OK ] AnimatorTest.GltfioCompatibilityIndexBasedAPI
[ RUN      ] AnimatorTest.GltfioCompatibilityInternalVsExternal
[       OK ] AnimatorTest.GltfioCompatibilityInternalVsExternal
```

### Phase 4: Sample Application Testing

```bash
# Test external animation sample
cd /path/to/filament
./out/cmake-debug/samples/animation/external_animation

# Expected: No crashes, smooth animation playback
```

### Phase 5: Documentation Review

- [ ] Update README.md if new APIs were added
- [ ] Update SYNC_GUIDE.md with any new conflict patterns discovered
- [ ] Document gltfio version/commit synced from:
  ```markdown
  <!-- Add to README.md -->
  ## Version History
  - **2025-01-XX**: Synced with gltfio commit `abc123` - Added feature X
  ```

---

## Rollback Procedure

If synchronization introduces regressions:

### Quick Rollback

```bash
# Return to backup branch
git checkout pre-sync-backup

# Verify tests pass
cd out/cmake-debug/libs/gltfio_ext
./run_tests.sh

# Delete failed sync branch
git branch -D sync-gltfio-20250102
```

### Partial Rollback (Cherry-pick)

```bash
# If only specific files are problematic
git checkout sync-gltfio-20250102
git checkout pre-sync-backup -- libs/gltfio_ext/src/Animator.cpp

# Rebuild and test
./build.sh debug
cd out/cmake-debug/libs/gltfio_ext
./run_tests.sh
```

---

## Best Practices

### Sync Frequency

- **Quarterly reviews**: Check gltfio changes every 3 months
- **Critical fixes**: Sync immediately for security/crash fixes
- **Major releases**: Sync before major Filament version updates

### Documentation

After each sync, document in commit message:

```bash
git commit -m "Sync with gltfio: <brief description>

Synced from gltfio commit: <commit-hash>
Changes applied:
- Bug fix: Animation timing precision (Animator.cpp)
- Performance: Optimized bone matrix updates (Animator.cpp)

Preserved gltfio_ext features:
- Animation cache system
- Multi-instance animator support
- External animation loading

Tests: 124 passed, 5 explicitly skipped, 0 failed (129 total)
"
```

### Testing Protocol

Always run full test suite:
```bash
# Before sync
./libs/gltfio_ext/run_tests.sh > /tmp/before_sync.log

# After sync
./libs/gltfio_ext/run_tests.sh > /tmp/after_sync.log

# Compare
diff /tmp/before_sync.log /tmp/after_sync.log
```

---

## Contact & Support

**Questions about synchronization?**
- Review this guide first
- Check git history for previous sync examples: `git log --grep="Sync with gltfio"`
- Consult gltfio source: `libs/gltfio`

**Sync went wrong?**
- Use rollback procedure above
- Review conflict resolution section
- Test incrementally (sync one file at a time)

---

## Appendix: File Change Tracking

### Files to Monitor in gltfio

**High priority** (sync immediately):
- `src/Animator.cpp` - Core animation logic
- `src/AssetLoader.cpp` - Asset loading
- `include/gltfio/Animator.h` - Public API

**Medium priority** (sync quarterly):
- `src/FFilamentAsset.cpp` - Asset management
- `src/FFilamentInstance.cpp` - Instance handling
- `src/ResourceLoader.cpp` - Resource loading

**Low priority** (optional):
- Documentation files
- Sample applications
- Build configuration

### gltfio_ext Exclusive Files (Never Sync)

```
libs/gltfio_ext/
├── include/gltfio_ext/
│   ├── AnimationAsset.h          ❌ Never sync
│   └── AnimationBinding.h        ❌ Never sync
├── src/
│   ├── AnimationAsset.cpp        ❌ Never sync
│   ├── AnimationBinding.cpp      ❌ Never sync
│   ├── AnimationCache.h          ❌ Never sync
│   └── AnimationCache.cpp        ❌ Never sync
├── test/
│   └── *.cpp                     ❌ Never sync (all tests)
├── README.md                     ❌ Never sync
└── SYNC_GUIDE.md                 ❌ Never sync (this file)
```

---

**Last Updated**: 2026-08-20
**gltfio Version Tracking**: Check `libs/gltfio` git history for reference
