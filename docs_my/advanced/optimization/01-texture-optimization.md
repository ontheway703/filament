# 纹理优化

## 📖 概述

纹理是图形应用中占用内存和带宽最多的资源之一。合理的纹理优化可以显著提升性能、降低内存占用和加快加载速度。本文档介绍全面的纹理优化技术，从格式选择到运行时管理。

**本文涵盖**:
- 纹理压缩格式选择
- 纹理尺寸优化
- Mipmap 生成和优化
- 纹理图集 (Atlas)
- 流式纹理加载
- 纹理内存管理

**优化目标**:
- 减少纹理内存占用 50-80%
- 降低内存带宽消耗
- 加快纹理加载速度
- 提升渲染性能

---

## 1. 纹理压缩

### 1.1 压缩格式选择

```cpp
// TextureCompressionSelector.h
#pragma once

#include <filament/Texture.h>
#include <string>

using namespace filament;

/**
 * 纹理压缩格式选择器
 *
 * 根据平台和内容自动选择最佳压缩格式
 */
class TextureCompressionSelector {
public:
    /**
     * 平台类型
     */
    enum class Platform {
        ANDROID,
        IOS,
        WINDOWS,
        MACOS,
        LINUX,
        WEB
    };

    /**
     * 内容类型
     */
    enum class ContentType {
        ALBEDO,          // 反照率贴图
        NORMAL,          // 法线贴图
        ROUGHNESS,       // 粗糙度
        METALLIC,        // 金属度
        EMISSIVE,        // 自发光
        UI,              // UI 纹理
        HDR              // HDR 纹理
    };

    /**
     * 压缩配置
     */
    struct CompressionConfig {
        Texture::InternalFormat format;
        bool generateMipmaps;
        int maxSize;
        std::string description;
    };

public:
    /**
     * 选择最佳压缩格式
     */
    static CompressionConfig selectFormat(Platform platform, ContentType content, bool hasAlpha);

    /**
     * 获取压缩比
     */
    static float getCompressionRatio(Texture::InternalFormat format);

    /**
     * 检查格式支持
     */
    static bool isFormatSupported(Texture::InternalFormat format, Platform platform);

private:
    static CompressionConfig selectAndroidFormat(ContentType content, bool hasAlpha);
    static CompressionConfig selectIOSFormat(ContentType content, bool hasAlpha);
    static CompressionConfig selectDesktopFormat(ContentType content, bool hasAlpha);
};
```

```cpp
// TextureCompressionSelector.cpp
#include "TextureCompressionSelector.h"

TextureCompressionSelector::CompressionConfig 
TextureCompressionSelector::selectFormat(Platform platform, ContentType content, bool hasAlpha) {
    
    switch (platform) {
        case Platform::ANDROID:
            return selectAndroidFormat(content, hasAlpha);
            
        case Platform::IOS:
        case Platform::MACOS:
            return selectIOSFormat(content, hasAlpha);
            
        case Platform::WINDOWS:
        case Platform::LINUX:
            return selectDesktopFormat(content, hasAlpha);
            
        case Platform::WEB:
            // WebGL 通常支持 ETC2 和 ASTC
            return selectAndroidFormat(content, hasAlpha);
            
        default:
            return selectDesktopFormat(content, hasAlpha);
    }
}

TextureCompressionSelector::CompressionConfig 
TextureCompressionSelector::selectAndroidFormat(ContentType content, bool hasAlpha) {
    
    CompressionConfig config;
    config.generateMipmaps = true;
    config.maxSize = 2048;  // 移动端限制
    
    switch (content) {
        case ContentType::ALBEDO:
            if (hasAlpha) {
                // ASTC 提供最佳质量和压缩比
                config.format = Texture::InternalFormat::RGBA_ASTC_4x4;
                config.description = "ASTC 4x4 for albedo with alpha (8bpp, high quality)";
            } else {
                config.format = Texture::InternalFormat::RGB_ASTC_4x4;
                config.description = "ASTC 4x4 for albedo (8bpp, high quality)";
            }
            break;
            
        case ContentType::NORMAL:
            // 法线贴图不需要 alpha 通道
            config.format = Texture::InternalFormat::RGB_ASTC_4x4;
            config.description = "ASTC 4x4 for normal maps (8bpp)";
            // 注: 法线贴图应该标记为线性空间
            break;
            
        case ContentType::ROUGHNESS:
        case ContentType::METALLIC:
            // 单通道数据可以使用更高压缩率
            config.format = Texture::InternalFormat::RGBA_ASTC_8x8;
            config.description = "ASTC 8x8 for roughness/metallic (2bpp)";
            break;
            
        case ContentType::EMISSIVE:
            config.format = Texture::InternalFormat::RGB_ASTC_6x6;
            config.description = "ASTC 6x6 for emissive (3.56bpp)";
            break;
            
        case ContentType::UI:
            // UI 需要较高质量
            if (hasAlpha) {
                config.format = Texture::InternalFormat::RGBA_ASTC_4x4;
            } else {
                config.format = Texture::InternalFormat::RGB_ASTC_4x4;
            }
            config.description = "ASTC 4x4 for UI (8bpp, high quality)";
            config.generateMipmaps = false;  // UI 通常不需要 mipmap
            break;
            
        case ContentType::HDR:
            // HDR 需要高精度
            config.format = Texture::InternalFormat::RGBA16F;
            config.description = "RGBA16F for HDR (64bpp, uncompressed)";
            break;
    }
    
    return config;
}

TextureCompressionSelector::CompressionConfig 
TextureCompressionSelector::selectIOSFormat(ContentType content, bool hasAlpha) {
    
    CompressionConfig config;
    config.generateMipmaps = true;
    config.maxSize = 2048;
    
    // iOS 优先使用 ASTC (A8+ 芯片)
    // 对于老设备可以回退到 PVRTC
    
    switch (content) {
        case ContentType::ALBEDO:
            if (hasAlpha) {
                config.format = Texture::InternalFormat::RGBA_ASTC_4x4;
                config.description = "ASTC 4x4 for albedo with alpha";
            } else {
                config.format = Texture::InternalFormat::RGB_ASTC_4x4;
                config.description = "ASTC 4x4 for albedo";
            }
            break;
            
        case ContentType::NORMAL:
            config.format = Texture::InternalFormat::RGB_ASTC_4x4;
            config.description = "ASTC 4x4 for normal maps";
            break;
            
        case ContentType::ROUGHNESS:
        case ContentType::METALLIC:
            config.format = Texture::InternalFormat::RGBA_ASTC_8x8;
            config.description = "ASTC 8x8 for metallic/roughness";
            break;
            
        case ContentType::EMISSIVE:
            config.format = Texture::InternalFormat::RGB_ASTC_6x6;
            config.description = "ASTC 6x6 for emissive";
            break;
            
        case ContentType::UI:
            if (hasAlpha) {
                config.format = Texture::InternalFormat::RGBA_ASTC_4x4;
            } else {
                config.format = Texture::InternalFormat::RGB_ASTC_4x4;
            }
            config.generateMipmaps = false;
            break;
            
        case ContentType::HDR:
            config.format = Texture::InternalFormat::RGBA16F;
            config.description = "RGBA16F for HDR";
            break;
    }
    
    return config;
}

TextureCompressionSelector::CompressionConfig 
TextureCompressionSelector::selectDesktopFormat(ContentType content, bool hasAlpha) {
    
    CompressionConfig config;
    config.generateMipmaps = true;
    config.maxSize = 4096;  // 桌面端可以使用更大纹理
    
    // 桌面端使用 BC (DXT/S3TC) 格式
    
    switch (content) {
        case ContentType::ALBEDO:
            if (hasAlpha) {
                // BC3 (DXT5) 支持 alpha
                config.format = Texture::InternalFormat::RGBA_S3TC_DXT5;
                config.description = "BC3/DXT5 for albedo with alpha (8bpp)";
            } else {
                // BC1 (DXT1) 最佳压缩比
                config.format = Texture::InternalFormat::RGB_S3TC_DXT1;
                config.description = "BC1/DXT1 for albedo (4bpp)";
            }
            break;
            
        case ContentType::NORMAL:
            // BC5 专为法线贴图优化 (存储 XY, 重建 Z)
            config.format = Texture::InternalFormat::RG_RGTC2;
            config.description = "BC5/RGTC2 for normal maps (8bpp)";
            break;
            
        case ContentType::ROUGHNESS:
        case ContentType::METALLIC:
            // BC4 单通道压缩
            config.format = Texture::InternalFormat::R_RGTC1;
            config.description = "BC4/RGTC1 for roughness/metallic (4bpp)";
            break;
            
        case ContentType::EMISSIVE:
            config.format = Texture::InternalFormat::RGB_S3TC_DXT1;
            config.description = "BC1/DXT1 for emissive (4bpp)";
            break;
            
        case ContentType::UI:
            if (hasAlpha) {
                config.format = Texture::InternalFormat::RGBA_S3TC_DXT5;
            } else {
                config.format = Texture::InternalFormat::RGB_S3TC_DXT1;
            }
            config.generateMipmaps = false;
            break;
            
        case ContentType::HDR:
            // BC6H 专为 HDR 设计
            config.format = Texture::InternalFormat::RGB_BC6H;
            config.description = "BC6H for HDR (8bpp)";
            break;
    }
    
    return config;
}

float TextureCompressionSelector::getCompressionRatio(Texture::InternalFormat format) {
    // 相对于 RGBA8 (32bpp) 的压缩比
    
    switch (format) {
        // BC/DXT formats
        case Texture::InternalFormat::RGB_S3TC_DXT1:
            return 32.0f / 4.0f;  // 8:1
            
        case Texture::InternalFormat::RGBA_S3TC_DXT5:
        case Texture::InternalFormat::RG_RGTC2:
            return 32.0f / 8.0f;  // 4:1
            
        case Texture::InternalFormat::R_RGTC1:
            return 32.0f / 4.0f;  // 8:1
            
        case Texture::InternalFormat::RGB_BC6H:
            return 128.0f / 8.0f;  // 16:1 (vs RGBA16F)
            
        // ASTC formats
        case Texture::InternalFormat::RGBA_ASTC_4x4:
        case Texture::InternalFormat::RGB_ASTC_4x4:
            return 32.0f / 8.0f;  // 4:1
            
        case Texture::InternalFormat::RGBA_ASTC_6x6:
            return 32.0f / 3.56f;  // ~9:1
            
        case Texture::InternalFormat::RGBA_ASTC_8x8:
            return 32.0f / 2.0f;  // 16:1
            
        // ETC2 formats
        case Texture::InternalFormat::RGB8_ETC2:
            return 32.0f / 4.0f;  // 8:1 (vs RGB8)
            
        case Texture::InternalFormat::RGBA8_ETC2_EAC:
            return 32.0f / 8.0f;  // 4:1
            
        default:
            return 1.0f;  // 无压缩
    }
}

bool TextureCompressionSelector::isFormatSupported(Texture::InternalFormat format, Platform platform) {
    // 简化的支持检测
    // 实际应该查询 GPU 能力
    
    switch (platform) {
        case Platform::ANDROID:
            // 现代 Android 设备支持 ETC2 (OpenGL ES 3.0+) 和 ASTC
            return true;
            
        case Platform::IOS:
        case Platform::MACOS:
            // iOS/macOS 支持 ASTC 和 PVRTC
            return true;
            
        case Platform::WINDOWS:
        case Platform::LINUX:
            // 桌面 GPU 支持 BC formats
            return true;
            
        default:
            return false;
    }
}
```

### 1.2 纹理压缩工具集成

```cpp
// TextureCompressor.h
#pragma once

#include <string>
#include <vector>

/**
 * 纹理压缩器
 *
 * 集成外部压缩工具进行离线压缩
 */
class TextureCompressor {
public:
    struct CompressOptions {
        std::string inputPath;
        std::string outputPath;
        std::string format;  // "astc", "bc7", "etc2"
        int quality;         // 0-100
        bool generateMipmaps;
        int maxSize;
    };

    struct CompressResult {
        bool success;
        std::string errorMessage;
        size_t originalSize;
        size_t compressedSize;
        float compressionRatio;
        float compressionTime;  // seconds
    };

public:
    /**
     * 压缩单个纹理
     */
    static CompressResult compressTexture(const CompressOptions& options);

    /**
     * 批量压缩
     */
    static std::vector<CompressResult> compressBatch(const std::vector<CompressOptions>& batch);

    /**
     * 使用 astcenc 压缩
     */
    static CompressResult compressWithASTCEnc(const CompressOptions& options);

    /**
     * 使用 compressonator 压缩
     */
    static CompressResult compressWithCompressonator(const CompressOptions& options);

    /**
     * 使用 etc2comp 压缩
     */
    static CompressResult compressWithETC2Comp(const CompressOptions& options);
};
```

```cpp
// TextureCompressor.cpp
#include "TextureCompressor.h"
#include <cstdlib>
#include <sstream>
#include <chrono>

TextureCompressor::CompressResult 
TextureCompressor::compressTexture(const CompressOptions& options) {
    
    if (options.format == "astc") {
        return compressWithASTCEnc(options);
    } else if (options.format == "bc7" || options.format == "bc6h") {
        return compressWithCompressonator(options);
    } else if (options.format == "etc2") {
        return compressWithETC2Comp(options);
    } else {
        CompressResult result;
        result.success = false;
        result.errorMessage = "Unknown format: " + options.format;
        return result;
    }
}

TextureCompressor::CompressResult 
TextureCompressor::compressWithASTCEnc(const CompressOptions& options) {
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // 构建 astcenc 命令
    // astcenc -cl input.png output.astc 4x4 -medium
    
    std::ostringstream command;
    command << "astcenc -cl ";
    command << "\"" << options.inputPath << "\" ";
    command << "\"" << options.outputPath << "\" ";
    
    // 块大小映射到质量
    if (options.quality >= 80) {
        command << "4x4 ";  // 最高质量 (8bpp)
    } else if (options.quality >= 60) {
        command << "6x6 ";  // 中等质量 (3.56bpp)
    } else {
        command << "8x8 ";  // 较低质量 (2bpp)
    }
    
    // 质量预设
    if (options.quality >= 90) {
        command << "-thorough ";
    } else if (options.quality >= 70) {
        command << "-medium ";
    } else {
        command << "-fast ";
    }
    
    // 执行压缩
    int exitCode = std::system(command.str().c_str());
    
    auto endTime = std::chrono::high_resolution_clock::now();
    float elapsedTime = std::chrono::duration<float>(endTime - startTime).count();
    
    CompressResult result;
    result.success = (exitCode == 0);
    result.compressionTime = elapsedTime;
    
    if (!result.success) {
        result.errorMessage = "astcenc failed with exit code " + std::to_string(exitCode);
    }
    
    // TODO: 计算文件大小和压缩比
    
    return result;
}

TextureCompressor::CompressResult 
TextureCompressor::compressWithCompressonator(const CompressOptions& options) {
    
    // 使用 AMD Compressonator CLI
    // CompressonatorCLI -fd BC7 input.png output.dds
    
    std::ostringstream command;
    command << "CompressonatorCLI ";
    command << "-fd " << options.format << " ";
    command << "\"" << options.inputPath << "\" ";
    command << "\"" << options.outputPath << "\"";
    
    if (options.generateMipmaps) {
        command << " -miplevels 0";  // 生成所有 mipmap
    }
    
    int exitCode = std::system(command.str().c_str());
    
    CompressResult result;
    result.success = (exitCode == 0);
    
    if (!result.success) {
        result.errorMessage = "Compressonator failed";
    }
    
    return result;
}

TextureCompressor::CompressResult 
TextureCompressor::compressWithETC2Comp(const CompressOptions& options) {
    
    // 使用 Google etc2comp
    // EtcTool input.png -output output.ktx -format RGB8 -effort 100
    
    std::ostringstream command;
    command << "EtcTool ";
    command << "\"" << options.inputPath << "\" ";
    command << "-output \"" << options.outputPath << "\" ";
    command << "-format RGB8 ";
    command << "-effort " << options.quality;
    
    int exitCode = std::system(command.str().c_str());
    
    CompressResult result;
    result.success = (exitCode == 0);
    
    return result;
}

std::vector<TextureCompressor::CompressResult> 
TextureCompressor::compressBatch(const std::vector<CompressOptions>& batch) {
    
    std::vector<CompressResult> results;
    results.reserve(batch.size());
    
    for (const auto& options : batch) {
        results.push_back(compressTexture(options));
    }
    
    return results;
}
```

---

## 2. Mipmap 优化

### 2.1 Mipmap 生成器

```cpp
// MipmapGenerator.h
#pragma once

#include <filament/Texture.h>
#include <vector>

using namespace filament;

/**
 * Mipmap 生成器
 *
 * 生成高质量 mipmap 链
 */
class MipmapGenerator {
public:
    /**
     * 过滤器类型
     */
    enum class FilterType {
        BOX,           // 盒式滤波 (最快)
        TRIANGLE,      // 三角滤波
        LANCZOS,       // Lanczos 滤波 (最高质量)
        KAISER         // Kaiser 滤波
    };

    /**
     * Mipmap 配置
     */
    struct MipmapConfig {
        FilterType filter = FilterType::LANCZOS;
        bool sharpen = false;       // 锐化
        float sharpenAmount = 0.5f;
        bool normalizeNormals = false;  // 法线贴图专用
        int maxLevels = -1;        // -1 = 生成所有级别
    };

public:
    /**
     * 生成 mipmap 链
     */
    static std::vector<std::vector<uint8_t>> generateMipmaps(
        const uint8_t* data,
        int width,
        int height,
        int channels,
        const MipmapConfig& config
    );

    /**
     * 计算 mipmap 级别数
     */
    static int calculateMipmapLevels(int width, int height);

    /**
     * 上传 mipmap 到 Filament 纹理
     */
    static void uploadMipmaps(
        Texture* texture,
        const std::vector<std::vector<uint8_t>>& mipmaps,
        int width,
        int height
    );

private:
    static std::vector<uint8_t> downsample(
        const uint8_t* data,
        int srcWidth,
        int srcHeight,
        int channels,
        FilterType filter
    );

    static void sharpenMipmap(
        uint8_t* data,
        int width,
        int height,
        int channels,
        float amount
    );

    static void normalizeNormalMap(
        uint8_t* data,
        int width,
        int height
    );
};
```

```cpp
// MipmapGenerator.cpp
#include "MipmapGenerator.h"
#include <cmath>
#include <algorithm>

int MipmapGenerator::calculateMipmapLevels(int width, int height) {
    int maxDim = std::max(width, height);
    return static_cast<int>(std::floor(std::log2(maxDim))) + 1;
}

std::vector<std::vector<uint8_t>> MipmapGenerator::generateMipmaps(
    const uint8_t* data,
    int width,
    int height,
    int channels,
    const MipmapConfig& config) {

    std::vector<std::vector<uint8_t>> mipmaps;

    // 级别 0 (原始数据)
    size_t level0Size = width * height * channels;
    mipmaps.push_back(std::vector<uint8_t>(data, data + level0Size));

    // 计算级别数
    int maxLevels = calculateMipmapLevels(width, height);
    if (config.maxLevels > 0) {
        maxLevels = std::min(maxLevels, config.maxLevels);
    }

    // 生成每一级
    int currentWidth = width;
    int currentHeight = height;
    const uint8_t* currentData = data;

    for (int level = 1; level < maxLevels; ++level) {
        int nextWidth = std::max(1, currentWidth / 2);
        int nextHeight = std::max(1, currentHeight / 2);

        // 降采样
        auto downsampled = downsample(
            currentData,
            currentWidth,
            currentHeight,
            channels,
            config.filter
        );

        // 可选: 锐化
        if (config.sharpen && level < maxLevels - 1) {
            sharpenMipmap(
                downsampled.data(),
                nextWidth,
                nextHeight,
                channels,
                config.sharpenAmount
            );
        }

        // 可选: 法线归一化
        if (config.normalizeNormals && channels >= 3) {
            normalizeNormalMap(
                downsampled.data(),
                nextWidth,
                nextHeight
            );
        }

        mipmaps.push_back(std::move(downsampled));

        currentWidth = nextWidth;
        currentHeight = nextHeight;
        currentData = mipmaps.back().data();
    }

    return mipmaps;
}

std::vector<uint8_t> MipmapGenerator::downsample(
    const uint8_t* data,
    int srcWidth,
    int srcHeight,
    int channels,
    FilterType filter) {

    int dstWidth = std::max(1, srcWidth / 2);
    int dstHeight = std::max(1, srcHeight / 2);

    std::vector<uint8_t> result(dstWidth * dstHeight * channels);

    // 简单的盒式滤波 (2x2 平均)
    for (int y = 0; y < dstHeight; ++y) {
        for (int x = 0; x < dstWidth; ++x) {
            int srcX = x * 2;
            int srcY = y * 2;

            for (int c = 0; c < channels; ++c) {
                // 获取 2x2 区域的 4 个像素
                int sum = 0;
                int count = 0;

                for (int dy = 0; dy < 2 && (srcY + dy) < srcHeight; ++dy) {
                    for (int dx = 0; dx < 2 && (srcX + dx) < srcWidth; ++dx) {
                        int srcIdx = ((srcY + dy) * srcWidth + (srcX + dx)) * channels + c;
                        sum += data[srcIdx];
                        count++;
                    }
                }

                int dstIdx = (y * dstWidth + x) * channels + c;
                result[dstIdx] = static_cast<uint8_t>(sum / count);
            }
        }
    }

    return result;
}

void MipmapGenerator::sharpenMipmap(
    uint8_t* data,
    int width,
    int height,
    int channels,
    float amount) {

    // 简单的锐化滤波器
    // TODO: 实现更高质量的锐化算法
}

void MipmapGenerator::normalizeNormalMap(
    uint8_t* data,
    int width,
    int height) {

    // 归一化法线向量
    for (int i = 0; i < width * height; ++i) {
        int idx = i * 3;

        // 从 [0, 255] 映射到 [-1, 1]
        float x = (data[idx + 0] / 255.0f) * 2.0f - 1.0f;
        float y = (data[idx + 1] / 255.0f) * 2.0f - 1.0f;
        float z = (data[idx + 2] / 255.0f) * 2.0f - 1.0f;

        // 归一化
        float length = std::sqrt(x * x + y * y + z * z);
        if (length > 0.0f) {
            x /= length;
            y /= length;
            z /= length;
        }

        // 映射回 [0, 255]
        data[idx + 0] = static_cast<uint8_t>((x * 0.5f + 0.5f) * 255.0f);
        data[idx + 1] = static_cast<uint8_t>((y * 0.5f + 0.5f) * 255.0f);
        data[idx + 2] = static_cast<uint8_t>((z * 0.5f + 0.5f) * 255.0f);
    }
}

void MipmapGenerator::uploadMipmaps(
    Texture* texture,
    const std::vector<std::vector<uint8_t>>& mipmaps,
    int width,
    int height) {

    int currentWidth = width;
    int currentHeight = height;

    for (size_t level = 0; level < mipmaps.size(); ++level) {
        Texture::PixelBufferDescriptor buffer(
            mipmaps[level].data(),
            mipmaps[level].size(),
            Texture::Format::RGB,
            Texture::Type::UBYTE
        );

        texture->setImage(
            *Engine::getInstance(),
            level,
            std::move(buffer)
        );

        currentWidth = std::max(1, currentWidth / 2);
        currentHeight = std::max(1, currentHeight / 2);
    }
}
```

---

## 3. 纹理图集 (Texture Atlas)

### 3.1 图集打包器

```cpp
// TextureAtlasPacker.h
#pragma once

#include <vector>
#include <string>

/**
 * 纹理图集打包器
 *
 * 将多个小纹理打包成一个大纹理
 */
class TextureAtlasPacker {
public:
    /**
     * 矩形
     */
    struct Rect {
        int x, y;
        int width, height;
    };

    /**
     * 纹理条目
     */
    struct TextureEntry {
        std::string name;
        int width, height;
        std::vector<uint8_t> data;
        Rect packedRect;  // 打包后的位置
    };

    /**
     * 图集配置
     */
    struct AtlasConfig {
        int maxWidth = 4096;
        int maxHeight = 4096;
        int padding = 2;      // 纹理间距 (避免渗色)
        bool powerOfTwo = true;
        bool allowRotation = false;
    };

    /**
     * 打包结果
     */
    struct PackResult {
        bool success;
        int atlasWidth;
        int atlasHeight;
        std::vector<uint8_t> atlasData;
        std::vector<TextureEntry> entries;
    };

public:
    /**
     * 打包纹理
     */
    static PackResult packTextures(
        const std::vector<TextureEntry>& textures,
        const AtlasConfig& config
    );

    /**
     * 生成 UV 映射
     */
    static void generateUVMapping(
        const PackResult& result,
        const std::string& outputPath
    );

private:
    static bool packRectangles(
        std::vector<TextureEntry>& textures,
        int atlasWidth,
        int atlasHeight,
        int padding
    );

    static void copyToAtlas(
        uint8_t* atlas,
        int atlasWidth,
        const TextureEntry& entry
    );
};
```

```cpp
// TextureAtlasPacker.cpp
#include "TextureAtlasPacker.h"
#include <algorithm>
#include <cmath>

TextureAtlasPacker::PackResult TextureAtlasPacker::packTextures(
    const std::vector<TextureEntry>& textures,
    const AtlasConfig& config) {

    PackResult result;
    result.success = false;

    if (textures.empty()) {
        return result;
    }

    // 复制并排序纹理 (按高度降序)
    auto sortedTextures = textures;
    std::sort(sortedTextures.begin(), sortedTextures.end(),
        [](const TextureEntry& a, const TextureEntry& b) {
            return a.height > b.height;
        });

    // 尝试不同的图集尺寸
    int atlasWidth = 256;
    int atlasHeight = 256;

    while (atlasWidth <= config.maxWidth && atlasHeight <= config.maxHeight) {
        // 尝试打包
        auto testTextures = sortedTextures;
        if (packRectangles(testTextures, atlasWidth, atlasHeight, config.padding)) {
            // 打包成功,创建图集
            result.success = true;
            result.atlasWidth = atlasWidth;
            result.atlasHeight = atlasHeight;
            result.entries = testTextures;

            // 创建图集数据 (假设 RGBA)
            int channels = 4;
            result.atlasData.resize(atlasWidth * atlasHeight * channels, 0);

            // 复制每个纹理到图集
            for (const auto& entry : testTextures) {
                copyToAtlas(result.atlasData.data(), atlasWidth, entry);
            }

            break;
        }

        // 增大图集尺寸
        if (atlasWidth == atlasHeight) {
            atlasWidth *= 2;
        } else {
            atlasHeight *= 2;
        }
    }

    return result;
}

bool TextureAtlasPacker::packRectangles(
    std::vector<TextureEntry>& textures,
    int atlasWidth,
    int atlasHeight,
    int padding) {

    // 简单的货架打包算法 (Shelf Packing)
    int currentX = padding;
    int currentY = padding;
    int shelfHeight = 0;

    for (auto& texture : textures) {
        int texWidth = texture.width + padding * 2;
        int texHeight = texture.height + padding * 2;

        // 检查当前行是否有空间
        if (currentX + texWidth > atlasWidth) {
            // 换行
            currentX = padding;
            currentY += shelfHeight + padding;
            shelfHeight = 0;

            // 检查是否超出高度
            if (currentY + texHeight > atlasHeight) {
                return false;  // 无法打包
            }
        }

        // 放置纹理
        texture.packedRect.x = currentX;
        texture.packedRect.y = currentY;
        texture.packedRect.width = texture.width;
        texture.packedRect.height = texture.height;

        currentX += texWidth;
        shelfHeight = std::max(shelfHeight, texHeight);
    }

    return true;
}

void TextureAtlasPacker::copyToAtlas(
    uint8_t* atlas,
    int atlasWidth,
    const TextureEntry& entry) {

    int channels = 4;  // 假设 RGBA

    const Rect& rect = entry.packedRect;

    for (int y = 0; y < rect.height; ++y) {
        for (int x = 0; x < rect.width; ++x) {
            int srcIdx = (y * entry.width + x) * channels;
            int dstIdx = ((rect.y + y) * atlasWidth + (rect.x + x)) * channels;

            for (int c = 0; c < channels; ++c) {
                atlas[dstIdx + c] = entry.data[srcIdx + c];
            }
        }
    }
}

void TextureAtlasPacker::generateUVMapping(
    const PackResult& result,
    const std::string& outputPath) {

    // 生成 JSON 或其他格式的 UV 映射文件
    // 包含每个纹理在图集中的位置和 UV 坐标

    // TODO: 实现 UV 映射导出
}
```

---

## 4. 流式纹理加载

### 4.1 流式加载管理器

```cpp
// StreamingTextureManager.h
#pragma once

#include <filament/Engine.h>
#include <filament/Texture.h>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <thread>

using namespace filament;

/**
 * 流式纹理管理器
 *
 * 按需加载和卸载纹理
 */
class StreamingTextureManager {
public:
    /**
     * 纹理优先级
     */
    enum class Priority {
        CRITICAL,   // 立即需要
        HIGH,       // 高优先级
        NORMAL,     // 普通
        LOW         // 低优先级
    };

    /**
     * 纹理状态
     */
    enum class State {
        UNLOADED,   // 未加载
        LOADING,    // 加载中
        LOADED,     // 已加载
        EVICTED     // 已驱逐
    };

    /**
     * 纹理描述
     */
    struct TextureDesc {
        std::string path;
        Texture* texture;
        State state;
        Priority priority;
        size_t memorySize;
        double lastAccessTime;
        int currentMipLevel;  // 当前加载的 mip 级别
        int targetMipLevel;   // 目标 mip 级别
    };

public:
    explicit StreamingTextureManager(Engine* engine);
    ~StreamingTextureManager();

    /**
     * 请求纹理
     */
    Texture* requestTexture(const std::string& path, Priority priority);

    /**
     * 更新 (每帧调用)
     */
    void update();

    /**
     * 设置内存预算
     */
    void setMemoryBudget(size_t bytes);

    /**
     * 获取内存使用情况
     */
    size_t getCurrentMemoryUsage() const { return mCurrentMemoryUsage; }

    /**
     * 预加载纹理
     */
    void preloadTexture(const std::string& path);

    /**
     * 卸载纹理
     */
    void unloadTexture(const std::string& path);

private:
    void loadingThread();
    void loadTexture(TextureDesc& desc);
    void evictLRU();
    bool shouldEvict() const;

    Engine* mEngine;
    std::unordered_map<std::string, TextureDesc> mTextures;

    size_t mMemoryBudget = 512 * 1024 * 1024;  // 512 MB
    size_t mCurrentMemoryUsage = 0;

    std::queue<std::string> mLoadQueue;
    std::mutex mQueueMutex;

    std::thread mLoadingThread;
    bool mRunning = true;
};
```

```cpp
// StreamingTextureManager.cpp
#include "StreamingTextureManager.h"
#include <algorithm>
#include <chrono>

StreamingTextureManager::StreamingTextureManager(Engine* engine)
    : mEngine(engine) {

    mLoadingThread = std::thread(&StreamingTextureManager::loadingThread, this);
}

StreamingTextureManager::~StreamingTextureManager() {
    mRunning = false;
    if (mLoadingThread.joinable()) {
        mLoadingThread.join();
    }

    // 清理所有纹理
    for (auto& [path, desc] : mTextures) {
        if (desc.texture) {
            mEngine->destroy(desc.texture);
        }
    }
}

Texture* StreamingTextureManager::requestTexture(const std::string& path, Priority priority) {
    auto it = mTextures.find(path);

    if (it == mTextures.end()) {
        // 创建新纹理描述
        TextureDesc desc;
        desc.path = path;
        desc.texture = nullptr;
        desc.state = State::UNLOADED;
        desc.priority = priority;
        desc.currentMipLevel = -1;
        desc.targetMipLevel = 0;

        mTextures[path] = desc;
        it = mTextures.find(path);

        // 加入加载队列
        {
            std::lock_guard<std::mutex> lock(mQueueMutex);
            mLoadQueue.push(path);
        }
    }

    // 更新访问时间
    auto now = std::chrono::system_clock::now();
    it->second.lastAccessTime = std::chrono::duration<double>(now.time_since_epoch()).count();

    // 更新优先级
    if (priority > it->second.priority) {
        it->second.priority = priority;
    }

    return it->second.texture;
}

void StreamingTextureManager::update() {
    // 检查是否需要驱逐纹理
    while (shouldEvict()) {
        evictLRU();
    }

    // TODO: 更新渐进式加载 (逐步加载更高 mip 级别)
}

void StreamingTextureManager::loadingThread() {
    while (mRunning) {
        std::string path;

        {
            std::lock_guard<std::mutex> lock(mQueueMutex);
            if (!mLoadQueue.empty()) {
                path = mLoadQueue.front();
                mLoadQueue.pop();
            }
        }

        if (!path.empty()) {
            auto it = mTextures.find(path);
            if (it != mTextures.end()) {
                loadTexture(it->second);
            }
        } else {
            // 队列为空,休眠
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void StreamingTextureManager::loadTexture(TextureDesc& desc) {
    desc.state = State::LOADING;

    // TODO: 实际加载纹理数据
    // 1. 从文件读取
    // 2. 解码
    // 3. 创建 Filament Texture
    // 4. 上传数据

    // 示例:
    // desc.texture = Texture::Builder()
    //     .width(width)
    //     .height(height)
    //     .format(format)
    //     .build(*mEngine);

    desc.state = State::LOADED;
    mCurrentMemoryUsage += desc.memorySize;
}

void StreamingTextureManager::evictLRU() {
    // 找到最久未使用的纹理
    TextureDesc* lruTexture = nullptr;
    double oldestTime = std::numeric_limits<double>::max();

    for (auto& [path, desc] : mTextures) {
        if (desc.state == State::LOADED &&
            desc.priority != Priority::CRITICAL &&
            desc.lastAccessTime < oldestTime) {

            lruTexture = &desc;
            oldestTime = desc.lastAccessTime;
        }
    }

    if (lruTexture) {
        // 驱逐纹理
        if (lruTexture->texture) {
            mEngine->destroy(lruTexture->texture);
            lruTexture->texture = nullptr;
        }

        mCurrentMemoryUsage -= lruTexture->memorySize;
        lruTexture->state = State::EVICTED;

        utils::slog.i << "Evicted texture: " << lruTexture->path << utils::io::endl;
    }
}

bool StreamingTextureManager::shouldEvict() const {
    return mCurrentMemoryUsage > mMemoryBudget;
}

void StreamingTextureManager::setMemoryBudget(size_t bytes) {
    mMemoryBudget = bytes;
}

void StreamingTextureManager::preloadTexture(const std::string& path) {
    requestTexture(path, Priority::NORMAL);
}

void StreamingTextureManager::unloadTexture(const std::string& path) {
    auto it = mTextures.find(path);
    if (it != mTextures.end()) {
        if (it->second.texture) {
            mEngine->destroy(it->second.texture);
            mCurrentMemoryUsage -= it->second.memorySize;
        }
        mTextures.erase(it);
    }
}
```

---

## 5. CMakeLists.txt 配置

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.19)
project(FilamentTextureOptimization)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件
set(SOURCES
    src/TextureCompressionSelector.cpp
    src/TextureCompressor.cpp
    src/MipmapGenerator.cpp
    src/TextureAtlasPacker.cpp
    src/StreamingTextureManager.cpp
)

# 创建库
add_library(texture_optimization STATIC ${SOURCES})

target_link_libraries(texture_optimization PUBLIC
    filament
    utils
)

target_include_directories(texture_optimization PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 纹理压缩工具 (可选)
find_program(ASTCENC astcenc)
find_program(COMPRESSONATOR CompressonatorCLI)
find_program(ETC2COMP EtcTool)

if(ASTCENC)
    message(STATUS "Found astcenc: ${ASTCENC}")
    add_compile_definitions(HAVE_ASTCENC)
endif()

if(COMPRESSONATOR)
    message(STATUS "Found Compressonator: ${COMPRESSONATOR}")
    add_compile_definitions(HAVE_COMPRESSONATOR)
endif()

if(ETC2COMP)
    message(STATUS "Found EtcTool: ${ETC2COMP}")
    add_compile_definitions(HAVE_ETC2COMP)
endif()
```

---

## 6. 常见问题

### Q1: 如何选择纹理压缩格式?

**A**: 根据平台和内容:

```cpp
// Android
CompressionConfig config = TextureCompressionSelector::selectFormat(
    Platform::ANDROID,
    ContentType::ALBEDO,
    true  // has alpha
);
// 结果: ASTC 4x4 (8bpp, 4:1 压缩)

// Desktop
config = TextureCompressionSelector::selectFormat(
    Platform::WINDOWS,
    ContentType::ALBEDO,
    false  // no alpha
);
// 结果: BC1/DXT1 (4bpp, 8:1 压缩)
```

### Q2: 压缩会损失多少质量?

**A**: 取决于格式和质量设置:

```
高质量 (视觉无损):
- ASTC 4x4: ~8bpp, 4:1
- BC7: ~8bpp, 4:1

中等质量:
- ASTC 6x6: ~3.56bpp, ~9:1
- BC1: ~4bpp, 8:1

低质量 (移动设备省内存):
- ASTC 8x8: ~2bpp, 16:1
```

### Q3: Mipmap 有什么用?

**A**: 三大作用:

```
1. 减少走样 (Aliasing)
   - 远处物体使用低分辨率 mip
   - 避免纹理采样问题

2. 提升性能
   - 减少纹理带宽
   - 提高纹理缓存命中率

3. 减少闪烁
   - 平滑的 LOD 过渡
```

---

## 7. 相关文档

- [optimization/02-mesh-optimization.md](./02-mesh-optimization.md) - 网格优化
- [optimization/04-memory-management.md](./04-memory-management.md) - 内存管理
- [optimization/08-asset-pipeline-opt.md](./08-asset-pipeline-opt.md) - 资产管线优化

---

## 8. 总结

纹理优化关键技术:

1. **压缩**: 使用平台原生格式 (ASTC, BC, ETC2)
2. **Mipmap**: 生成高质量 mipmap 链
3. **图集**: 合并小纹理减少 draw call
4. **流式加载**: 按需加载和卸载纹理
5. **内存管理**: LRU 缓存和内存预算

通过系统化的纹理优化,可以实现:
- **内存减少 50-80%**
- **带宽减少 40-60%**
- **加载速度提升 2-3倍**

纹理优化是性能优化的基础,必须在项目早期就建立完善的纹理管线。
