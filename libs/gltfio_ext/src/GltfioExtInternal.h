/**
 * @file GltfioExtInternal.h
 * @brief 内部日志宏定义
 *
 * 设计说明：
 * - 统一的日志前缀："gltfio_ext: "
 * - 使用Filament的utils::slog系统
 * - 支持调试时启用/禁用详细日志
 *
 * 日志宏：
 * - GLTFIO_EXT_WARN: 警告日志（slog.w）
 *   用于非致命错误（如跳过无效数据）
 *
 * - GLTFIO_EXT_LOG: 信息日志（slog.i）
 *   用于调试信息（如加载进度）
 *
 * 使用方式：
 * @code
 *   GLTFIO_EXT_WARN("Failed to load animation");
 *   GLTFIO_EXT_LOG("Loaded " << count << " primitives");
 * @endcode
 *
 * 注意：
 * - 生产版本应关闭VERBOSE（改为0）减少日志输出
 * - 日志使用流式语法（<< 操作符）
 */

#ifndef GLTFIO_EXT_INTERNAL_H
#define GLTFIO_EXT_INTERNAL_H

#include <utils/Log.h>

// 临时启用所有日志用于调试
#define GLTFIO_EXT_VERBOSE 1
#define GLTFIO_EXT_WARN(msg) utils::slog.w << "gltfio_ext: " << msg << utils::io::endl
#define GLTFIO_EXT_LOG(msg) utils::slog.i << "gltfio_ext: " << msg << utils::io::endl

#endif // GLTFIO_EXT_INTERNAL_H
