# XMuscle Filament Fork

This repository is the XMuscle fork of Google Filament.

The upstream project is:

```text
https://github.com/google/filament.git
```

XMuscle Engine consumes this fork as a git submodule at:

```text
xmuscle-engine/third_party/filament
```

## Branch Policy

Use these branches consistently:

| Branch or tag | Purpose |
| --- | --- |
| `main` | Mirror of upstream Filament. Do not put XMuscle-specific changes here. |
| `custom-modifications` | Long-lived XMuscle branch used by `xmuscle-engine`. |
| `xmuscle/sync-vX.Y.Z` | Temporary branch for syncing an official Filament release. |
| `xmuscle-before-...` | Backup tag before a sync or risky maintenance operation. |

`xmuscle-engine` should pin the submodule to a verified commit from
`custom-modifications`, not to `main` or `upstream/main`.

## XMuscle Changes

The XMuscle branch currently carries project-specific changes including:

- Increased bone / UBO limits for 512-bone skeletal animation.
- `libs/gltfio_ext`, an extended glTF loading and animation library used by XMuscle.
- iOS / Apple Silicon simulator build script adjustments.

The current synchronization baseline is the official Filament `v1.75.0` tag. Do not use
`upstream/main` as a release baseline.

Keep these changes isolated from `main` so that the fork always has a clean upstream baseline.

## Sync Strategy

Do not update XMuscle directly from `upstream/main` for production use. Prefer official Filament
release tags, for example `v1.75.0`, because they provide a stable and reproducible integration
target.

Recommended flow:

```bash
git fetch origin
git fetch upstream --tags

git checkout custom-modifications
git tag xmuscle-before-filament-vX.Y.Z-sync
git push origin xmuscle-before-filament-vX.Y.Z-sync

git checkout -b xmuscle/sync-vX.Y.Z
git merge vX.Y.Z
```

Resolve conflicts on the sync branch, validate the result, then merge the sync branch back into
`custom-modifications` and push it to the fork.

After the fork is updated and validated, update the submodule pointer in `xmuscle-engine`.

## gltfio_ext

`libs/gltfio_ext` is derived from upstream `libs/gltfio`, but it is not updated automatically when
upstream Filament changes.

When upstream `libs/gltfio` receives relevant fixes or behavior changes, manually port the needed
changes into `libs/gltfio_ext` while preserving XMuscle features such as external animation loading,
animation cache behavior, and multi-instance support.

Follow the dedicated guide:

```text
libs/gltfio_ext/SYNC_GUIDE.md
```

## Validation

A Filament sync is not complete until the fork and XMuscle integration have both been verified.
Use the smallest useful validation set for the change, but major syncs should cover:

- Filament release build.
- `libs/gltfio_ext` tests.
- `xmuscle-engine` macOS / CMake build.
- `xmuscle-engine` iOS build.
- `xmuscle-engine` Android build.
- iOS and Android app integration checks when engine behavior or platform libraries change.

## Useful Git Settings

For repeated upstream syncs, enable recorded conflict resolution:

```bash
git config rerere.enabled true
```

This helps Git reuse conflict resolutions across future Filament updates.
