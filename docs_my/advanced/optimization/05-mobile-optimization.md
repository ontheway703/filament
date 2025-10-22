# 移动端优化

## 📖 概述

移动设备有严格的性能和功耗限制。本文档提供针对 Android 和 iOS 平台的优化技术。

**优化目标**:
- 达到目标帧率 (30/60 FPS)
- 降低功耗
- 减少内存使用
- 优化电池寿命

---

## 1. 自适应质量

```cpp
// QualityManager.h
class QualityManager {
public:
    enum class QualityLevel {
        LOW, MEDIUM, HIGH
    };

    void adjustQuality(float currentFPS, float targetFPS) {
        if (currentFPS < targetFPS * 0.9f) {
            lowerQuality();
        } else if (currentFPS > targetFPS * 1.1f) {
            raiseQuality();
        }
    }

private:
    QualityLevel mCurrentQuality = QualityLevel::MEDIUM;
};
```

---

## 2. 纹理压缩

```cpp
// 使用 ASTC 或 ETC2
Texture::InternalFormat format;
#ifdef __ANDROID__
    format = Texture::InternalFormat::RGBA_ASTC_4x4;
#elif defined(__APPLE__)
    format = Texture::InternalFormat::RGBA_ASTC_4x4;
#endif
```

---

## 3. 总结

移动端优化关键点:
1. 使用纹理压缩 (ASTC/ETC2)
2. 实现 LOD 系统
3. 降低分辨率
4. 简化着色器
5. 降低帧率（省电模式）
