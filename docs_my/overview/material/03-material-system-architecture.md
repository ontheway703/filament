# Material System Architecture

## Overview

Filament's material system architecture is built around a separation of material definitions (templates) and material instances (parameterized variants). This design enables efficient memory usage, fast parameter updates, and optimal GPU resource management across multiple rendering contexts.

## Core Architecture Components

### Material vs MaterialInstance Relationship

```cpp
// filament/include/filament/Material.h
class Material : public FilamentAPI {
public:
    // Material is immutable after creation
    MaterialInstance* createInstance(const char* name = nullptr) const noexcept;

    // Introspection API
    size_t getParameterCount() const noexcept;
    bool hasParameter(const char* name) const noexcept;
    Shading getShading() const noexcept;
    BlendingMode getBlendingMode() const noexcept;

    // Default instance for shared parameters
    MaterialInstance* getDefaultInstance() noexcept;
};

// filament/include/filament/MaterialInstance.h
class MaterialInstance : public FilamentAPI {
public:
    // Parameter setting API
    template<typename T>
    void setParameter(const char* name, T value) noexcept;

    void setParameter(const char* name, Texture const* texture,
                     TextureSampler const& sampler) noexcept;

    // State management
    Material const* getMaterial() const noexcept;
    const char* getName() const noexcept;
};
```

### Relationship Model

```
┌─────────────────┐    creates     ┌──────────────────────┐
│    Material     │──────────────→ │  MaterialInstance    │
│                 │                │                      │
│ • Shader Code   │                │ • Parameter Values   │
│ • Properties    │                │ • Texture Bindings   │
│ • Metadata      │   1        *   │ • Uniform Buffer     │
│ • Immutable     │◆──────────────→│ • Mutable State      │
└─────────────────┘                └──────────────────────┘
```

## Package Format (filaflat)

### Binary Layout Structure

```cpp
// libs/filaflat/include/filaflat/MaterialChunk.h
class ChunkContainer {
    struct ChunkRecord {
        uint32_t type;           // Chunk type identifier
        uint32_t size;           // Size in bytes
        uint32_t offset;         // Offset from file start
    };

    // Chunk types for material data
    enum MaterialChunkType : uint32_t {
        MaterialGlsl     = 0x00000001,  // GLSL shader source
        MaterialSpirv    = 0x00000002,  // SPIRV bytecode
        MaterialMsl      = 0x00000003,  // Metal shading language
        MaterialSamplers = 0x00000004,  // Sampler definitions
        MaterialUib      = 0x00000005,  // Uniform interface block
        MaterialConstants= 0x00000006,  // Specialization constants
        MaterialDict     = 0x00000007,  // Parameter dictionary
        MaterialPostProcess = 0x00000008 // Post-processing info
    };
};
```

### Efficient Loading Strategy

```cpp
// Lazy loading of shader variants
class MaterialParser {
    struct VariantKey {
        uint8_t userVariant;     // User-configurable flags
        uint8_t systemVariant;   // System-generated flags
        uint8_t primitiveType;   // Points, lines, triangles
        uint8_t shaderModel;     // Target shader model
    };

    // Only load shaders when needed
    Program* getProgram(const VariantKey& key) {
        auto it = mProgramCache.find(key);
        if (it == mProgramCache.end()) {
            // Load and compile on-demand
            return loadProgramVariant(key);
        }
        return it->second;
    }
};
```

## Parameter System Architecture

### Uniform Interface Block (UiB)

Filament uses structured uniform buffers for efficient parameter management:

```cpp
// Generated UiB structure for material parameters
struct MaterialParams {
    alignas(16) float4 baseColor;       // 16-byte aligned
    alignas(16) float4 emissive;        // 16-byte aligned
    alignas(4)  float  roughness;       // 4-byte aligned
    alignas(4)  float  metallic;        // 4-byte aligned
    alignas(4)  float  reflectance;     // 4-byte aligned
    alignas(4)  uint32_t albedoMap;     // Texture handle
    // Automatic padding for alignment
};
```

### Parameter Binding Strategy

```cpp
// filament/src/details/MaterialInstance.h
class FMaterialInstance {
    struct UniformBuffer {
        void* data;                      // CPU-side parameter storage
        size_t size;                     // Buffer size in bytes
        bool dirty;                      // Needs GPU update
        backend::BufferObjectHandle handle; // GPU buffer handle
    };

    // Efficient parameter updates
    template<typename T>
    void setParameter(const char* name, T value) {
        auto offset = mMaterial->getParameterOffset(name);
        if (offset != Material::INVALID_OFFSET) {
            // Direct memory write to uniform buffer
            *reinterpret_cast<T*>(mUniformBuffer.data + offset) = value;
            mUniformBuffer.dirty = true;
        }
    }
};
```

## Texture Management

### Sampler Handle System

Efficient texture binding using handle-based approach:

```cpp
// Texture sampler representation
struct TextureHandle {
    uint32_t index;          // Texture unit index
    Texture const* texture;  // Texture resource pointer
    TextureSampler sampler;  // Sampling parameters
};

// Material parameter for textures
union MaterialTextureParam {
    uint32_t handle;         // 0 = no texture bound
    struct {
        uint32_t index : 16; // Texture unit
        uint32_t flags : 16; // Sampling flags
    };
};
```

### Texture Binding Architecture

```cpp
// Texture binding in material instance
void FMaterialInstance::setParameter(const char* name,
                                   Texture const* texture,
                                   TextureSampler const& sampler) {
    auto paramInfo = mMaterial->getParameterInfo(name);
    if (paramInfo.isSampler) {
        // Register texture with handle
        uint32_t handle = mTextureManager.registerTexture(texture, sampler);

        // Store handle in uniform buffer
        setParameter(name, handle);

        // Track for resource management
        mBoundTextures[paramInfo.binding] = {texture, sampler};
    }
}
```

## Runtime Material Management

### Material Loading Pipeline

```cpp
// Material creation from package data
class MaterialLoader {
public:
    static Material* loadMaterial(Engine& engine, const void* data, size_t size) {
        // Parse package format
        filaflat::ChunkContainer container(data, size);

        // Extract material information
        auto materialInfo = container.getMaterialInfo();
        auto shaderChunks = container.getShaderChunks();

        // Create material with builder
        Material::Builder builder;
        builder.package(data, size);

        return builder.build(engine);
    }
};
```

### Asynchronous Compilation

```cpp
// Background shader compilation
void Material::compile(CompilerPriorityQueue priority,
                      UserVariantFilterMask variants,
                      backend::CallbackHandler* handler,
                      utils::Invocable<void(Material*)>&& callback) {

    // Queue compilation job
    auto job = [this, variants, callback = std::move(callback)]() {
        // Compile requested variants
        for (auto variant : variants.value) {
            compileVariant(variant);
        }

        // Notify completion on main thread
        mEngine.getMainThreadHandler().post([=]() {
            callback(this);
        });
    };

    // Submit to appropriate queue
    auto& scheduler = mEngine.getJobSystem();
    if (priority == CompilerPriorityQueue::HIGH) {
        scheduler.runAndWait(job);
    } else {
        scheduler.run(job);
    }
}
```

## Memory Management Strategy

### Resource Ownership Model

```cpp
class Engine {
    // Material lifecycle management
    std::unordered_set<Material*> mMaterials;
    std::unordered_set<MaterialInstance*> mMaterialInstances;

public:
    void destroy(Material const* material) {
        // Clean up all instances first
        auto instances = getMaterialInstances(material);
        for (auto instance : instances) {
            destroy(instance);
        }

        // Release GPU resources
        material->terminate(mBackend);

        // Remove from tracking
        mMaterials.erase(const_cast<Material*>(material));
        delete material;
    }

    void destroy(MaterialInstance const* instance) {
        // Release uniform buffers
        instance->terminate(mBackend);

        // Remove from tracking
        mMaterialInstances.erase(const_cast<MaterialInstance*>(instance));
        delete instance;
    }
};
```

### GPU Resource Lifecycle

```cpp
// GPU resource management in material
class FMaterial {
    struct ProgramEntry {
        backend::ProgramHandle program;
        std::atomic<bool> compiled{false};
    };

    std::array<ProgramEntry, VARIANT_COUNT> mPrograms;
    backend::UniformBufferHandle mDefaultUbh;

public:
    void terminate(backend::DriverApi& driver) noexcept {
        // Clean up shader programs
        for (auto& entry : mPrograms) {
            if (entry.program) {
                driver.destroyProgram(entry.program);
            }
        }

        // Clean up uniform buffers
        if (mDefaultUbh) {
            driver.destroyUniformBuffer(mDefaultUbh);
        }
    }
};
```

## Performance Optimization Architecture

### Material Instance Pooling

```cpp
// Efficient instance management
class MaterialInstancePool {
    std::vector<std::unique_ptr<MaterialInstance>> mPool;
    std::queue<MaterialInstance*> mAvailable;

public:
    MaterialInstance* acquire(Material const* material) {
        if (mAvailable.empty()) {
            // Create new instance
            auto instance = std::make_unique<MaterialInstance>(material);
            auto* ptr = instance.get();
            mPool.push_back(std::move(instance));
            return ptr;
        }

        auto* instance = mAvailable.front();
        mAvailable.pop();
        instance->reset(material);  // Reuse existing instance
        return instance;
    }

    void release(MaterialInstance* instance) {
        instance->resetParameters();  // Clear state
        mAvailable.push(instance);
    }
};
```

### Batch Parameter Updates

```cpp
// Batch uniform buffer updates
class UniformBufferManager {
    struct UpdateBatch {
        std::vector<MaterialInstance*> instances;
        size_t totalSize = 0;
    };

public:
    void flushUpdates() {
        for (auto& batch : mPendingBatches) {
            // Allocate staging buffer
            void* staging = allocateStaging(batch.totalSize);

            // Copy all dirty uniform buffers
            size_t offset = 0;
            for (auto* instance : batch.instances) {
                if (instance->isDirty()) {
                    memcpy(staging + offset, instance->getUniformBuffer(),
                           instance->getUniformBufferSize());
                    offset += instance->getUniformBufferSize();
                }
            }

            // Single GPU upload
            uploadToGPU(staging, batch.totalSize);
        }

        mPendingBatches.clear();
    }
};
```

### Shader Variant Management

```cpp
// Intelligent variant caching
class VariantCache {
    struct VariantStats {
        uint32_t useCount = 0;
        uint64_t lastUsed = 0;
        size_t memorySize = 0;
    };

    std::unordered_map<VariantKey, VariantStats> mVariantStats;
    size_t mTotalMemory = 0;
    static constexpr size_t MAX_CACHE_SIZE = 64 * 1024 * 1024; // 64MB

public:
    void evictLeastUsed() {
        if (mTotalMemory <= MAX_CACHE_SIZE) return;

        // Sort by usage frequency and recency
        std::vector<std::pair<VariantKey, VariantStats*>> candidates;
        for (auto& pair : mVariantStats) {
            candidates.emplace_back(pair.first, &pair.second);
        }

        std::sort(candidates.begin(), candidates.end(),
                 [](const auto& a, const auto& b) {
                     return a.second->useCount < b.second->useCount;
                 });

        // Evict until under limit
        for (auto& candidate : candidates) {
            evictVariant(candidate.first);
            mTotalMemory -= candidate.second->memorySize;

            if (mTotalMemory <= MAX_CACHE_SIZE * 0.8) break;
        }
    }
};
```

## Integration with Rendering Pipeline

### Material Sorting and Batching

```cpp
// Render queue organization
class RenderQueue {
    struct MaterialBatch {
        Material const* material;
        std::vector<RenderPrimitive> primitives;
    };

    std::vector<MaterialBatch> mOpaqueBatches;
    std::vector<MaterialBatch> mTransparentBatches;

public:
    void sort() {
        // Sort opaque by material (minimize state changes)
        std::sort(mOpaqueBatches.begin(), mOpaqueBatches.end(),
                 [](const auto& a, const auto& b) {
                     return a.material < b.material;
                 });

        // Sort transparent by depth (back-to-front)
        std::sort(mTransparentBatches.begin(), mTransparentBatches.end(),
                 [](const auto& a, const auto& b) {
                     return a.primitives[0].depth > b.primitives[0].depth;
                 });
    }
};
```

This architecture enables Filament to efficiently manage complex material systems while maintaining high performance and flexible parameter control across diverse rendering scenarios.