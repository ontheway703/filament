# Filament JNI 桥接层实现

## JNI 桥接层概述

Filament 的 JNI 桥接层负责在 Java/Kotlin 应用层和 C++ 引擎之间建立通信桥梁，实现高效的跨语言调用和资源管理。

## 整体架构设计

```
┌─────────────────────────────────────────────────────────┐
│                 Java/Kotlin Layer                      │
│  Engine.java, Material.java, VertexBuffer.java...     │
├─────────────────────────────────────────────────────────┤
│                  JNI Interface                         │
│          native method declarations                    │
├─────────────────────────────────────────────────────────┤
│                JNI Implementation                      │
│   Engine.cpp, Material.cpp, VertexBuffer.cpp...       │
├─────────────────────────────────────────────────────────┤
│               Native Filament API                      │
│            C++ Engine Core                             │
└─────────────────────────────────────────────────────────┘
```

## 核心 JNI 实现

### 1. 引擎管理 JNI

#### Java 层接口定义
```java
// Engine.java
public class Engine {
    private long mNativeObject;

    // 原生方法声明
    private static native long nCreateBuilder();
    private static native void nDestroyBuilder(long nativeBuilder);
    private static native long nBuilderBuild(long nativeBuilder);
    private static native void nDestroyEngine(long nativeEngine);

    // SwapChain 管理
    private static native long nCreateSwapChain(long nativeEngine, Object surface, long flags);
    private static native boolean nDestroySwapChain(long nativeEngine, long nativeSwapChain);

    // 资源创建
    private static native long nCreateRenderer(long nativeEngine);
    private static native long nCreateView(long nativeEngine);
    private static native long nCreateScene(long nativeEngine);
    private static native long nCreateCamera(long nativeEngine, int entity);
}
```

#### C++ JNI 实现
```cpp
// Engine.cpp
extern "C" JNIEXPORT void JNICALL
Java_com_google_android_filament_Engine_nDestroyEngine(JNIEnv*, jclass, jlong nativeEngine) {
    Engine* engine = (Engine*) nativeEngine;
    Engine::destroy(&engine);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_google_android_filament_Engine_nCreateSwapChain(JNIEnv* env,
        jclass klass, jlong nativeEngine, jobject surface, jlong flags) {
    Engine* engine = (Engine*) nativeEngine;
    void* win = getNativeWindow(env, klass, surface);
    return (jlong) engine->createSwapChain(win, (uint64_t) flags);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_google_android_filament_Engine_nCreateRenderer(JNIEnv*, jclass,
        jlong nativeEngine) {
    Engine* engine = (Engine*) nativeEngine;
    return (jlong) engine->createRenderer();
}

// 引擎构建器
extern "C" JNIEXPORT jlong JNICALL
Java_com_google_android_filament_Engine_nCreateBuilder(JNIEnv*, jclass) {
    Engine::Builder* builder = new Engine::Builder{};
    return (jlong) builder;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_google_android_filament_Engine_nBuilderBuild(JNIEnv*, jclass, jlong nativeBuilder) {
    Engine::Builder* builder = (Engine::Builder*) nativeBuilder;
    return (jlong) builder->build();
}
```

### 2. 材质系统 JNI

#### Java 层材质接口
```java
// Material.java
public class Material {
    private long mNativeObject;

    private static native long nCreateMaterial(long nativeEngine, ByteBuffer buffer, int size);
    private static native void nDestroyMaterial(long nativeEngine, long nativeMaterial);
    private static native long nGetDefaultInstance(long nativeMaterial);
    private static native String nGetName(long nativeMaterial);
    private static native void nCompile(long nativeMaterial, int priority,
                                       int userVariantFilter, Handler handler,
                                       CompilerCallback callback);

    public static class Builder {
        private ByteBuffer mBuffer;

        public Builder payload(ByteBuffer buffer, int size) {
            mBuffer = buffer;
            return this;
        }

        public Material build(Engine engine) {
            long nativeMaterial = nCreateMaterial(engine.getNativeObject(),
                                                 mBuffer, mBuffer.remaining());
            return new Material(nativeMaterial);
        }
    }
}
```

#### C++ 材质 JNI 实现
```cpp
// Material.cpp
extern "C" JNIEXPORT jlong JNICALL
Java_com_google_android_filament_Material_nCreateMaterial(JNIEnv* env, jclass,
        jlong nativeEngine, jobject buffer_, jint size) {
    Engine* engine = (Engine*) nativeEngine;

    AutoBuffer buffer(env, buffer_, size);
    return (jlong) Material::Builder()
        .package(buffer.getData(), buffer.getSize())
        .build(*engine);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_google_android_filament_Material_nGetDefaultInstance(JNIEnv*, jclass,
        jlong nativeMaterial) {
    Material* material = (Material*) nativeMaterial;
    return (jlong) material->getDefaultInstance();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_google_android_filament_Material_nGetName(JNIEnv* env, jclass,
        jlong nativeMaterial) {
    Material* material = (Material*) nativeMaterial;
    return env->NewStringUTF(material->getName().c_str());
}
```

### 3. 几何体 JNI

#### VertexBuffer JNI
```java
// VertexBuffer.java
public class VertexBuffer {
    public enum VertexAttribute {
        POSITION(0),
        TANGENTS(1),
        COLOR(2),
        UV0(3),
        UV1(4),
        BONE_INDICES(5),
        BONE_WEIGHTS(6);

        private final int mValue;
        VertexAttribute(int value) { mValue = value; }
        int getValue() { return mValue; }
    }

    public static class Builder {
        private static native long nCreateBuilder();
        private static native void nDestroyBuilder(long nativeBuilder);
        private static native void nBuilderVertexCount(long nativeBuilder, int vertexCount);
        private static native void nBuilderBufferCount(long nativeBuilder, int bufferCount);
        private static native void nBuilderAttribute(long nativeBuilder, int attribute,
                int bufferIndex, int attributeType, int byteOffset, int byteStride);
        private static native long nBuilderBuild(long nativeBuilder, long nativeEngine);
    }
}
```

```cpp
// VertexBuffer.cpp
extern "C" JNIEXPORT jlong JNICALL
Java_com_google_android_filament_VertexBuffer_00024Builder_nCreateBuilder(JNIEnv*, jclass) {
    return (jlong) new VertexBuffer::Builder();
}

extern "C" JNIEXPORT void JNICALL
Java_com_google_android_filament_VertexBuffer_00024Builder_nBuilderAttribute(JNIEnv*, jclass,
        jlong nativeBuilder, jint attribute, jint bufferIndex, jint attributeType,
        jint byteOffset, jint byteStride) {
    VertexBuffer::Builder* builder = (VertexBuffer::Builder*) nativeBuilder;
    builder->attribute((VertexAttribute) attribute,
                      (uint8_t) bufferIndex,
                      (VertexBuffer::AttributeType) attributeType,
                      (uint32_t) byteOffset,
                      (uint8_t) byteStride);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_google_android_filament_VertexBuffer_00024Builder_nBuilderBuild(JNIEnv*, jclass,
        jlong nativeBuilder, jlong nativeEngine) {
    VertexBuffer::Builder* builder = (VertexBuffer::Builder*) nativeBuilder;
    Engine* engine = (Engine*) nativeEngine;
    return (jlong) builder->build(*engine);
}
```

## 内存管理机制

### 1. 原生对象生命周期管理

#### Java 对象包装器
```java
// 基础原生对象包装类
public abstract class NativeObject {
    private long mNativeObject;

    protected NativeObject(long nativeObject) {
        mNativeObject = nativeObject;
    }

    public long getNativeObject() {
        if (mNativeObject == 0) {
            throw new IllegalStateException("Calling method on destroyed object");
        }
        return mNativeObject;
    }

    protected void clearNativeObject() {
        mNativeObject = 0;
    }
}
```

#### 资源销毁追踪
```java
// Engine.java - 资源销毁管理
public class Engine extends NativeObject {
    private final Set<Long> mNativeObjects = Collections.synchronizedSet(new HashSet<>());

    void trackNativeObject(long nativeObject) {
        mNativeObjects.add(nativeObject);
    }

    void untrackNativeObject(long nativeObject) {
        mNativeObjects.remove(nativeObject);
    }

    public void destroy() {
        // 检查是否有未销毁的资源
        if (!mNativeObjects.isEmpty()) {
            Log.w("Filament", "Engine destroyed with " + mNativeObjects.size() + " live objects");
        }
        nDestroyEngine(getNativeObject());
        clearNativeObject();
    }
}
```

### 2. ByteBuffer 内存管理

#### NioUtils 工具类
```java
// NioUtils.java
public final class NioUtils {
    private static native long nGetDirectBufferAddress(ByteBuffer buffer);
    private static native long nGetDirectBufferCapacity(ByteBuffer buffer);

    public static long getDirectBufferAddress(ByteBuffer buffer) {
        if (!buffer.isDirect()) {
            throw new IllegalArgumentException("Buffer must be direct");
        }
        return nGetDirectBufferAddress(buffer);
    }
}
```

```cpp
// NioUtils.cpp
extern "C" JNIEXPORT jlong JNICALL
Java_com_google_android_filament_NioUtils_nGetDirectBufferAddress(JNIEnv* env, jclass,
        jobject buffer) {
    return (jlong) env->GetDirectBufferAddress(buffer);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_google_android_filament_NioUtils_nGetDirectBufferCapacity(JNIEnv* env, jclass,
        jobject buffer) {
    return (jlong) env->GetDirectBufferCapacity(buffer);
}
```

#### 自动缓冲区管理
```cpp
// CallbackUtils.h
class AutoBuffer {
private:
    JNIEnv* mEnv;
    jobject mBuffer;
    void* mData;
    jint mSize;
    bool mOwnsData;

public:
    AutoBuffer(JNIEnv* env, jobject buffer, jint size) :
        mEnv(env), mBuffer(buffer), mSize(size), mOwnsData(false) {

        if (buffer) {
            mData = env->GetDirectBufferAddress(buffer);
            if (!mData) {
                // 如果不是 direct buffer，复制数据
                mData = malloc(size);
                mOwnsData = true;
                // 复制数据逻辑...
            }
        } else {
            mData = nullptr;
        }
    }

    ~AutoBuffer() {
        if (mOwnsData && mData) {
            free(mData);
        }
    }

    void* getData() const { return mData; }
    jint getSize() const { return mSize; }
};
```

## 异步回调机制

### 1. 材质编译回调

#### Java 回调接口
```java
// Material.java
public interface CompilerCallback {
    void onMaterialCompiled(Material material);
}

public class Material {
    public void compile(CompilerPriorityQueue priority, int userVariantFilter,
                       Handler handler, CompilerCallback callback) {
        nCompile(getNativeObject(), priority.ordinal(), userVariantFilter, handler, callback);
    }
}
```

#### C++ 回调实现
```cpp
// Material.cpp
struct CallbackData {
    JavaVM* jvm;
    jobject handler;
    jobject callback;
    jmethodID postMethod;
    jmethodID onCompiledMethod;
};

extern "C" JNIEXPORT void JNICALL
Java_com_google_android_filament_Material_nCompile(JNIEnv* env, jclass clazz,
        jlong nativeMaterial, jint priority, jint userVariantFilter,
        jobject handler, jobject callback) {

    Material* material = (Material*) nativeMaterial;

    // 创建回调数据
    CallbackData* data = new CallbackData();
    env->GetJavaVM(&data->jvm);
    data->handler = env->NewGlobalRef(handler);
    data->callback = env->NewGlobalRef(callback);

    // 获取方法 ID
    jclass handlerClass = env->FindClass("android/os/Handler");
    data->postMethod = env->GetMethodID(handlerClass, "post", "(Ljava/lang/Runnable;)Z");

    // 异步编译
    material->compile((Material::CompilerPriorityQueue)priority,
                     (Material::UserVariantFilterBit)userVariantFilter,
                     [data, material](bool success) {
                         // 在 UI 线程中执行回调
                         postToUIThread(data, material, success);
                     });
}

void postToUIThread(CallbackData* data, Material* material, bool success) {
    JNIEnv* env;
    data->jvm->AttachCurrentThread(&env, nullptr);

    // 创建 Runnable
    jclass runnableClass = env->FindClass("java/lang/Runnable");
    // 执行回调...

    data->jvm->DetachCurrentThread();
}
```

### 2. 线程安全的回调处理

```cpp
// CallbackUtils.cpp
class CallbackDispatcher {
public:
    static void dispatchCallback(JavaVM* jvm, jobject handler, std::function<void(JNIEnv*)> callback) {
        JNIEnv* env;
        bool isAttached = (jvm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK);

        if (!isAttached) {
            jvm->AttachCurrentThread(&env, nullptr);
        }

        // 创建 Java Runnable 对象
        jclass runnableClass = env->FindClass("java/lang/Runnable");
        jmethodID constructor = env->GetMethodID(runnableClass, "<init>", "()V");
        jobject runnable = env->NewObject(runnableClass, constructor);

        // 存储 C++ 回调
        CallbackWrapper* wrapper = new CallbackWrapper(callback);
        setNativeCallback(env, runnable, (jlong)wrapper);

        // 提交到 Handler
        jclass handlerClass = env->GetObjectClass(handler);
        jmethodID postMethod = env->GetMethodID(handlerClass, "post", "(Ljava/lang/Runnable;)Z");
        env->CallBooleanMethod(handler, postMethod, runnable);

        if (!isAttached) {
            jvm->DetachCurrentThread();
        }
    }
};
```

## 错误处理机制

### 1. 异常转换

```cpp
// 错误处理宏
#define FILAMENT_JNI_TRY(env) \
    try {

#define FILAMENT_JNI_CATCH(env) \
    } catch (const std::exception& e) { \
        jclass exceptionClass = env->FindClass("java/lang/RuntimeException"); \
        env->ThrowNew(exceptionClass, e.what()); \
    } catch (...) { \
        jclass exceptionClass = env->FindClass("java/lang/RuntimeException"); \
        env->ThrowNew(exceptionClass, "Unknown native exception"); \
    }

// 使用示例
extern "C" JNIEXPORT jlong JNICALL
Java_com_google_android_filament_Engine_nCreateRenderer(JNIEnv* env, jclass,
        jlong nativeEngine) {
    FILAMENT_JNI_TRY(env)
        Engine* engine = (Engine*) nativeEngine;
        return (jlong) engine->createRenderer();
    FILAMENT_JNI_CATCH(env)
    return 0;
}
```

### 2. 参数验证

```cpp
// 参数验证工具
inline bool validateEngine(JNIEnv* env, jlong nativeEngine) {
    if (nativeEngine == 0) {
        jclass exceptionClass = env->FindClass("java/lang/IllegalStateException");
        env->ThrowNew(exceptionClass, "Engine has been destroyed");
        return false;
    }
    return true;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_google_android_filament_Engine_nCreateView(JNIEnv* env, jclass,
        jlong nativeEngine) {
    if (!validateEngine(env, nativeEngine)) {
        return 0;
    }

    Engine* engine = (Engine*) nativeEngine;
    return (jlong) engine->createView();
}
```

## 性能优化策略

### 1. JNI 调用优化

```cpp
// 批量操作减少 JNI 调用
extern "C" JNIEXPORT void JNICALL
Java_com_google_android_filament_VertexBuffer_nSetBufferAtBatch(JNIEnv* env, jclass,
        jlong nativeVertexBuffer, jlong nativeEngine, jintArray bufferIndices,
        jobjectArray buffers) {

    VertexBuffer* vertexBuffer = (VertexBuffer*) nativeVertexBuffer;
    Engine* engine = (Engine*) nativeEngine;

    jint* indices = env->GetIntArrayElements(bufferIndices, nullptr);
    jsize count = env->GetArrayLength(bufferIndices);

    for (jsize i = 0; i < count; i++) {
        jobject buffer = env->GetObjectArrayElement(buffers, i);
        void* data = env->GetDirectBufferAddress(buffer);
        jlong size = env->GetDirectBufferCapacity(buffer);

        VertexBuffer::BufferDescriptor desc(data, size);
        vertexBuffer->setBufferAt(*engine, indices[i], std::move(desc));
    }

    env->ReleaseIntArrayElements(bufferIndices, indices, JNI_ABORT);
}
```

### 2. 内存拷贝优化

```cpp
// 零拷贝 ByteBuffer 传输
class DirectBufferWrapper {
private:
    void* mData;
    size_t mSize;
    bool mNeedsCopy;

public:
    DirectBufferWrapper(JNIEnv* env, jobject buffer) {
        mData = env->GetDirectBufferAddress(buffer);
        mSize = env->GetDirectBufferCapacity(buffer);
        mNeedsCopy = (mData == nullptr);

        if (mNeedsCopy) {
            // 回退到数组复制
            jbyteArray array = (jbyteArray)buffer;
            mSize = env->GetArrayLength(array);
            mData = malloc(mSize);
            env->GetByteArrayRegion(array, 0, mSize, (jbyte*)mData);
        }
    }

    ~DirectBufferWrapper() {
        if (mNeedsCopy && mData) {
            free(mData);
        }
    }
};
```

这个 JNI 桥接层设计确保了 Java/Kotlin 应用与 C++ 引擎之间的高效通信，同时保持了类型安全和内存管理的正确性。