# 性能分析方法

## 📖 概述

性能分析是图形应用开发中至关重要的环节。本文档介绍系统化的性能分析方法,帮助你识别和解决性能瓶颈,确保应用在目标平台上流畅运行。

**本文涵盖**:
- 性能指标定义和测量
- CPU/GPU 性能分析
- 内存性能分析
- 帧时间分析
- 瓶颈识别方法
- 性能优化策略

**目标**:
- 达到目标帧率 (60 FPS / 90 FPS / 120 FPS)
- 降低功耗
- 减少内存占用
- 提升用户体验

---

## 1. 性能指标体系

### 1.1 核心性能指标

```cpp
// PerformanceMetrics.h
#pragma once

#include <chrono>
#include <vector>
#include <string>
#include <unordered_map>

/**
 * 性能指标收集器
 *
 * 功能:
 * - 帧时间测量
 * - FPS 计算
 * - CPU/GPU 时间分析
 * - 内存使用跟踪
 */
class PerformanceMetrics {
public:
    /**
     * 帧时间统计
     */
    struct FrameStats {
        double frameTime;      // 总帧时间 (ms)
        double cpuTime;        // CPU 时间 (ms)
        double gpuTime;        // GPU 时间 (ms)
        double renderTime;     // 渲染时间 (ms)
        double updateTime;     // 更新时间 (ms)

        size_t drawCalls;      // Draw Call 数量
        size_t triangles;      // 三角形数量
        size_t vertices;       // 顶点数量

        size_t memoryUsed;     // 内存使用 (bytes)
        size_t textureMemory;  // 纹理内存 (bytes)
        size_t bufferMemory;   // 缓冲区内存 (bytes)
    };

    /**
     * 性能统计摘要
     */
    struct PerformanceSummary {
        double avgFPS;
        double minFPS;
        double maxFPS;

        double avgFrameTime;
        double minFrameTime;
        double maxFrameTime;

        double percentile95;   // 95% 帧时间
        double percentile99;   // 99% 帧时间

        size_t totalFrames;
        size_t droppedFrames;  // 掉帧数
    };

public:
    PerformanceMetrics();

    /**
     * 开始帧测量
     */
    void beginFrame();

    /**
     * 结束帧测量
     */
    void endFrame();

    /**
     * 记录事件开始
     */
    void beginEvent(const std::string& name);

    /**
     * 记录事件结束
     */
    void endEvent(const std::string& name);

    /**
     * 记录 Draw Call 统计
     */
    void recordDrawCall(size_t triangles, size_t vertices);

    /**
     * 记录内存使用
     */
    void recordMemoryUsage(size_t total, size_t textures, size_t buffers);

    /**
     * 获取当前帧统计
     */
    const FrameStats& getCurrentFrameStats() const { return mCurrentFrame; }

    /**
     * 获取性能摘要
     */
    PerformanceSummary getSummary() const;

    /**
     * 重置统计
     */
    void reset();

    /**
     * 导出数据为 CSV
     */
    void exportToCSV(const std::string& filename) const;

    /**
     * 生成性能报告
     */
    std::string generateReport() const;

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    struct EventTiming {
        TimePoint startTime;
        double duration;
    };

    FrameStats mCurrentFrame;
    std::vector<FrameStats> mFrameHistory;

    TimePoint mFrameStartTime;
    std::unordered_map<std::string, EventTiming> mActiveEvents;
    std::unordered_map<std::string, std::vector<double>> mEventHistory;

    static constexpr size_t MAX_HISTORY_FRAMES = 1000;

    double getElapsedMilliseconds(TimePoint start, TimePoint end) const;
};
```

```cpp
// PerformanceMetrics.cpp
#include "PerformanceMetrics.h"
#include <algorithm>
#include <numeric>
#include <fstream>
#include <sstream>
#include <iomanip>

PerformanceMetrics::PerformanceMetrics() {
    mFrameHistory.reserve(MAX_HISTORY_FRAMES);
    reset();
}

void PerformanceMetrics::beginFrame() {
    mFrameStartTime = Clock::now();
    mCurrentFrame = FrameStats{};
}

void PerformanceMetrics::endFrame() {
    TimePoint now = Clock::now();
    mCurrentFrame.frameTime = getElapsedMilliseconds(mFrameStartTime, now);

    // 记录帧历史
    if (mFrameHistory.size() >= MAX_HISTORY_FRAMES) {
        mFrameHistory.erase(mFrameHistory.begin());
    }
    mFrameHistory.push_back(mCurrentFrame);
}

void PerformanceMetrics::beginEvent(const std::string& name) {
    EventTiming timing;
    timing.startTime = Clock::now();
    mActiveEvents[name] = timing;
}

void PerformanceMetrics::endEvent(const std::string& name) {
    auto it = mActiveEvents.find(name);
    if (it != mActiveEvents.end()) {
        TimePoint now = Clock::now();
        double duration = getElapsedMilliseconds(it->second.startTime, now);

        // 记录到历史
        mEventHistory[name].push_back(duration);

        // 更新当前帧统计
        if (name == "Update") {
            mCurrentFrame.updateTime = duration;
        } else if (name == "Render") {
            mCurrentFrame.renderTime = duration;
        } else if (name == "CPU") {
            mCurrentFrame.cpuTime = duration;
        } else if (name == "GPU") {
            mCurrentFrame.gpuTime = duration;
        }

        mActiveEvents.erase(it);
    }
}

void PerformanceMetrics::recordDrawCall(size_t triangles, size_t vertices) {
    mCurrentFrame.drawCalls++;
    mCurrentFrame.triangles += triangles;
    mCurrentFrame.vertices += vertices;
}

void PerformanceMetrics::recordMemoryUsage(size_t total, size_t textures, size_t buffers) {
    mCurrentFrame.memoryUsed = total;
    mCurrentFrame.textureMemory = textures;
    mCurrentFrame.bufferMemory = buffers;
}

PerformanceMetrics::PerformanceSummary PerformanceMetrics::getSummary() const {
    PerformanceSummary summary{};

    if (mFrameHistory.empty()) {
        return summary;
    }

    // 收集帧时间数据
    std::vector<double> frameTimes;
    frameTimes.reserve(mFrameHistory.size());
    for (const auto& frame : mFrameHistory) {
        frameTimes.push_back(frame.frameTime);
    }

    // 排序以计算百分位数
    std::vector<double> sortedFrameTimes = frameTimes;
    std::sort(sortedFrameTimes.begin(), sortedFrameTimes.end());

    // 计算基本统计
    double sum = std::accumulate(frameTimes.begin(), frameTimes.end(), 0.0);
    summary.avgFrameTime = sum / frameTimes.size();
    summary.minFrameTime = sortedFrameTimes.front();
    summary.maxFrameTime = sortedFrameTimes.back();

    // 计算 FPS
    summary.avgFPS = 1000.0 / summary.avgFrameTime;
    summary.minFPS = 1000.0 / summary.maxFrameTime;
    summary.maxFPS = 1000.0 / summary.minFrameTime;

    // 计算百分位数
    size_t p95Index = static_cast<size_t>(sortedFrameTimes.size() * 0.95);
    size_t p99Index = static_cast<size_t>(sortedFrameTimes.size() * 0.99);
    summary.percentile95 = sortedFrameTimes[p95Index];
    summary.percentile99 = sortedFrameTimes[p99Index];

    // 统计总帧数和掉帧
    summary.totalFrames = mFrameHistory.size();
    summary.droppedFrames = 0;

    // 假设目标是 60 FPS (16.67 ms)
    constexpr double targetFrameTime = 16.67;
    for (double frameTime : frameTimes) {
        if (frameTime > targetFrameTime) {
            summary.droppedFrames++;
        }
    }

    return summary;
}

void PerformanceMetrics::reset() {
    mFrameHistory.clear();
    mEventHistory.clear();
    mActiveEvents.clear();
    mCurrentFrame = FrameStats{};
}

void PerformanceMetrics::exportToCSV(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        return;
    }

    // 写入表头
    file << "FrameIndex,FrameTime,CPUTime,GPUTime,RenderTime,UpdateTime,"
         << "DrawCalls,Triangles,Vertices,MemoryUsed,TextureMemory,BufferMemory\n";

    // 写入数据
    for (size_t i = 0; i < mFrameHistory.size(); ++i) {
        const auto& frame = mFrameHistory[i];
        file << i << ","
             << frame.frameTime << ","
             << frame.cpuTime << ","
             << frame.gpuTime << ","
             << frame.renderTime << ","
             << frame.updateTime << ","
             << frame.drawCalls << ","
             << frame.triangles << ","
             << frame.vertices << ","
             << frame.memoryUsed << ","
             << frame.textureMemory << ","
             << frame.bufferMemory << "\n";
    }

    file.close();
}

std::string PerformanceMetrics::generateReport() const {
    std::ostringstream report;
    auto summary = getSummary();

    report << "=== Performance Report ===\n\n";

    report << "Frame Statistics:\n";
    report << "  Total Frames: " << summary.totalFrames << "\n";
    report << "  Dropped Frames: " << summary.droppedFrames << " ("
           << std::fixed << std::setprecision(2)
           << (100.0 * summary.droppedFrames / summary.totalFrames) << "%)\n\n";

    report << "FPS:\n";
    report << "  Average: " << std::fixed << std::setprecision(2) << summary.avgFPS << "\n";
    report << "  Min: " << summary.minFPS << "\n";
    report << "  Max: " << summary.maxFPS << "\n\n";

    report << "Frame Time (ms):\n";
    report << "  Average: " << std::fixed << std::setprecision(3) << summary.avgFrameTime << "\n";
    report << "  Min: " << summary.minFrameTime << "\n";
    report << "  Max: " << summary.maxFrameTime << "\n";
    report << "  95th Percentile: " << summary.percentile95 << "\n";
    report << "  99th Percentile: " << summary.percentile99 << "\n\n";

    // 事件统计
    report << "Event Timings (Average, ms):\n";
    for (const auto& [name, timings] : mEventHistory) {
        double avg = std::accumulate(timings.begin(), timings.end(), 0.0) / timings.size();
        report << "  " << name << ": " << std::fixed << std::setprecision(3) << avg << "\n";
    }

    return report.str();
}

double PerformanceMetrics::getElapsedMilliseconds(TimePoint start, TimePoint end) const {
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    return duration.count() / 1000.0;
}
```

### 1.2 使用性能指标收集器

```cpp
// Application.cpp
#include "PerformanceMetrics.h"

class Application {
public:
    void initialize() {
        mMetrics = std::make_unique<PerformanceMetrics>();
        // 初始化 Filament...
    }

    void run() {
        while (!shouldQuit()) {
            mMetrics->beginFrame();

            // 更新
            mMetrics->beginEvent("Update");
            update(deltaTime);
            mMetrics->endEvent("Update");

            // 渲染
            mMetrics->beginEvent("Render");
            render();
            mMetrics->endEvent("Render");

            mMetrics->endFrame();

            // 每 5 秒输出一次报告
            if (++mFrameCount % 300 == 0) {
                printPerformanceReport();
            }
        }
    }

private:
    void printPerformanceReport() {
        auto report = mMetrics->generateReport();
        std::cout << report << std::endl;

        // 导出 CSV
        mMetrics->exportToCSV("performance_log.csv");
    }

    std::unique_ptr<PerformanceMetrics> mMetrics;
    uint64_t mFrameCount = 0;
};
```

---

## 2. CPU 性能分析

### 2.1 CPU 时间分析

```cpp
// CPUProfiler.h
#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>

/**
 * CPU 性能分析器
 *
 * 功能:
 * - 函数调用计时
 * - 调用堆栈分析
 * - 热点函数识别
 */
class CPUProfiler {
public:
    /**
     * 作用域计时器
     */
    class ScopedTimer {
    public:
        ScopedTimer(const char* name, CPUProfiler* profiler)
            : mName(name), mProfiler(profiler) {
            mProfiler->beginSection(mName);
        }

        ~ScopedTimer() {
            mProfiler->endSection(mName);
        }

    private:
        const char* mName;
        CPUProfiler* mProfiler;
    };

    /**
     * 性能数据
     */
    struct ProfileData {
        std::string name;
        size_t callCount;
        double totalTime;      // ms
        double averageTime;    // ms
        double minTime;        // ms
        double maxTime;        // ms
        double percentage;     // 占总时间的百分比
    };

public:
    CPUProfiler();

    void beginSection(const std::string& name);
    void endSection(const std::string& name);

    void beginFrame();
    void endFrame();

    std::vector<ProfileData> getTopFunctions(size_t count = 10) const;
    std::string generateReport() const;

    void reset();

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    struct SectionData {
        TimePoint startTime;
        std::vector<double> samples;
        size_t callCount = 0;
    };

    std::unordered_map<std::string, SectionData> mSections;
    std::unordered_map<std::string, TimePoint> mActiveTimers;

    double mTotalFrameTime = 0.0;

    double getElapsedMilliseconds(TimePoint start, TimePoint end) const;
};

// 便捷宏
#define CPU_PROFILE_SCOPE(name) \
    CPUProfiler::ScopedTimer __cpu_timer__(name, &gCPUProfiler)

// 全局分析器实例
extern CPUProfiler gCPUProfiler;
```

```cpp
// CPUProfiler.cpp
#include "CPUProfiler.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

CPUProfiler gCPUProfiler;

CPUProfiler::CPUProfiler() = default;

void CPUProfiler::beginSection(const std::string& name) {
    mActiveTimers[name] = Clock::now();
}

void CPUProfiler::endSection(const std::string& name) {
    auto it = mActiveTimers.find(name);
    if (it != mActiveTimers.end()) {
        TimePoint now = Clock::now();
        double elapsed = getElapsedMilliseconds(it->second, now);

        auto& section = mSections[name];
        section.samples.push_back(elapsed);
        section.callCount++;

        mActiveTimers.erase(it);
    }
}

void CPUProfiler::beginFrame() {
    // 清除上一帧的计时器
    mActiveTimers.clear();
}

void CPUProfiler::endFrame() {
    // 计算总帧时间
    mTotalFrameTime = 0.0;
    for (const auto& [name, section] : mSections) {
        if (!section.samples.empty()) {
            mTotalFrameTime += section.samples.back();
        }
    }
}

std::vector<CPUProfiler::ProfileData> CPUProfiler::getTopFunctions(size_t count) const {
    std::vector<ProfileData> results;

    for (const auto& [name, section] : mSections) {
        if (section.samples.empty()) continue;

        ProfileData data;
        data.name = name;
        data.callCount = section.callCount;

        double sum = 0.0;
        double min = section.samples[0];
        double max = section.samples[0];

        for (double sample : section.samples) {
            sum += sample;
            min = std::min(min, sample);
            max = std::max(max, sample);
        }

        data.totalTime = sum;
        data.averageTime = sum / section.samples.size();
        data.minTime = min;
        data.maxTime = max;
        data.percentage = (sum / mTotalFrameTime) * 100.0;

        results.push_back(data);
    }

    // 按总时间排序
    std::sort(results.begin(), results.end(), [](const ProfileData& a, const ProfileData& b) {
        return a.totalTime > b.totalTime;
    });

    // 返回前 N 个
    if (results.size() > count) {
        results.resize(count);
    }

    return results;
}

std::string CPUProfiler::generateReport() const {
    std::ostringstream report;
    auto topFunctions = getTopFunctions(20);

    report << "=== CPU Profile Report ===\n\n";
    report << std::left << std::setw(30) << "Function"
           << std::right << std::setw(10) << "Calls"
           << std::setw(12) << "Total (ms)"
           << std::setw(12) << "Avg (ms)"
           << std::setw(12) << "Min (ms)"
           << std::setw(12) << "Max (ms)"
           << std::setw(10) << "% Time\n";
    report << std::string(98, '-') << "\n";

    for (const auto& data : topFunctions) {
        report << std::left << std::setw(30) << data.name
               << std::right << std::setw(10) << data.callCount
               << std::fixed << std::setprecision(3)
               << std::setw(12) << data.totalTime
               << std::setw(12) << data.averageTime
               << std::setw(12) << data.minTime
               << std::setw(12) << data.maxTime
               << std::setprecision(1)
               << std::setw(9) << data.percentage << "%\n";
    }

    return report.str();
}

void CPUProfiler::reset() {
    mSections.clear();
    mActiveTimers.clear();
    mTotalFrameTime = 0.0;
}

double CPUProfiler::getElapsedMilliseconds(TimePoint start, TimePoint end) const {
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    return duration.count() / 1000.0;
}
```

### 2.2 使用 CPU 分析器

```cpp
// Scene.cpp
#include "CPUProfiler.h"

class Scene {
public:
    void update(double deltaTime) {
        CPU_PROFILE_SCOPE("Scene::update");

        {
            CPU_PROFILE_SCOPE("UpdateTransforms");
            updateTransforms(deltaTime);
        }

        {
            CPU_PROFILE_SCOPE("UpdateAnimations");
            updateAnimations(deltaTime);
        }

        {
            CPU_PROFILE_SCOPE("UpdatePhysics");
            updatePhysics(deltaTime);
        }

        {
            CPU_PROFILE_SCOPE("UpdateCulling");
            performCulling();
        }
    }

private:
    void updateTransforms(double deltaTime) {
        CPU_PROFILE_SCOPE("TransformHierarchy");
        // 更新变换层次结构
    }

    void updateAnimations(double deltaTime) {
        CPU_PROFILE_SCOPE("SkinningUpdate");
        // 更新骨骼动画

        CPU_PROFILE_SCOPE("MorphTargets");
        // 更新变形目标
    }

    void updatePhysics(double deltaTime) {
        // 物理模拟
    }

    void performCulling() {
        CPU_PROFILE_SCOPE("FrustumCulling");
        // 视锥裁剪

        CPU_PROFILE_SCOPE("OcclusionCulling");
        // 遮挡剔除
    }
};
```

---

## 3. GPU 性能分析

### 3.1 GPU 时间查询

```cpp
// GPUProfiler.h
#pragma once

#include <filament/Engine.h>
#include <string>
#include <vector>
#include <queue>

using namespace filament;

/**
 * GPU 性能分析器
 *
 * 使用 GPU 时间查询测量 GPU 耗时
 */
class GPUProfiler {
public:
    /**
     * GPU 时间查询
     */
    struct GPUQuery {
        std::string name;
        backend::TimerQueryHandle beginQuery;
        backend::TimerQueryHandle endQuery;
        double duration = 0.0;  // ms
        bool completed = false;
    };

    /**
     * GPU 性能数据
     */
    struct GPUProfileData {
        std::string name;
        double averageTime;  // ms
        double minTime;
        double maxTime;
        size_t sampleCount;
    };

public:
    explicit GPUProfiler(Engine* engine);
    ~GPUProfiler();

    /**
     * 开始 GPU 时间查询
     */
    void beginGPUEvent(const std::string& name);

    /**
     * 结束 GPU 时间查询
     */
    void endGPUEvent(const std::string& name);

    /**
     * 收集查询结果
     */
    void collectResults();

    /**
     * 获取性能数据
     */
    std::vector<GPUProfileData> getProfileData() const;

    /**
     * 生成报告
     */
    std::string generateReport() const;

    /**
     * 重置
     */
    void reset();

private:
    Engine* mEngine;
    std::vector<GPUQuery> mActiveQueries;
    std::vector<GPUQuery> mCompletedQueries;

    struct QueryHistory {
        std::vector<double> samples;
    };

    std::unordered_map<std::string, QueryHistory> mHistory;

    static constexpr size_t MAX_HISTORY_SAMPLES = 100;
};
```

```cpp
// GPUProfiler.cpp
#include "GPUProfiler.h"
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>

GPUProfiler::GPUProfiler(Engine* engine)
    : mEngine(engine) {
}

GPUProfiler::~GPUProfiler() {
    // 清理查询对象
}

void GPUProfiler::beginGPUEvent(const std::string& name) {
    GPUQuery query;
    query.name = name;

    // 创建开始查询
    // 注意: Filament 的 backend API 可能不直接暴露 Timer Query
    // 这里展示概念,实际实现需要通过 backend 扩展

    // query.beginQuery = driver->createTimerQuery();
    // driver->beginTimerQuery(query.beginQuery);

    mActiveQueries.push_back(query);
}

void GPUProfiler::endGPUEvent(const std::string& name) {
    // 查找对应的开始查询
    for (auto& query : mActiveQueries) {
        if (query.name == name && !query.completed) {
            // query.endQuery = driver->createTimerQuery();
            // driver->endTimerQuery(query.endQuery);
            query.completed = true;
            break;
        }
    }
}

void GPUProfiler::collectResults() {
    // 收集完成的查询结果
    for (auto it = mActiveQueries.begin(); it != mActiveQueries.end();) {
        if (it->completed) {
            // 获取查询结果
            // uint64_t nanoseconds = driver->getTimerQueryValue(it->beginQuery, it->endQuery);
            // it->duration = nanoseconds / 1000000.0; // 转换为毫秒

            // 添加到历史
            auto& history = mHistory[it->name];
            history.samples.push_back(it->duration);

            if (history.samples.size() > MAX_HISTORY_SAMPLES) {
                history.samples.erase(history.samples.begin());
            }

            // 移动到已完成列表
            mCompletedQueries.push_back(*it);
            it = mActiveQueries.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<GPUProfiler::GPUProfileData> GPUProfiler::getProfileData() const {
    std::vector<GPUProfileData> results;

    for (const auto& [name, history] : mHistory) {
        if (history.samples.empty()) continue;

        GPUProfileData data;
        data.name = name;
        data.sampleCount = history.samples.size();

        double sum = std::accumulate(history.samples.begin(), history.samples.end(), 0.0);
        data.averageTime = sum / history.samples.size();

        auto [minIt, maxIt] = std::minmax_element(history.samples.begin(), history.samples.end());
        data.minTime = *minIt;
        data.maxTime = *maxIt;

        results.push_back(data);
    }

    // 按平均时间排序
    std::sort(results.begin(), results.end(), [](const GPUProfileData& a, const GPUProfileData& b) {
        return a.averageTime > b.averageTime;
    });

    return results;
}

std::string GPUProfiler::generateReport() const {
    std::ostringstream report;
    auto profileData = getProfileData();

    report << "=== GPU Profile Report ===\n\n";
    report << std::left << std::setw(30) << "GPU Event"
           << std::right << std::setw(12) << "Avg (ms)"
           << std::setw(12) << "Min (ms)"
           << std::setw(12) << "Max (ms)"
           << std::setw(12) << "Samples\n";
    report << std::string(78, '-') << "\n";

    for (const auto& data : profileData) {
        report << std::left << std::setw(30) << data.name
               << std::right << std::fixed << std::setprecision(3)
               << std::setw(12) << data.averageTime
               << std::setw(12) << data.minTime
               << std::setw(12) << data.maxTime
               << std::setw(12) << data.sampleCount << "\n";
    }

    return report.str();
}

void GPUProfiler::reset() {
    mActiveQueries.clear();
    mCompletedQueries.clear();
    mHistory.clear();
}
```

### 3.2 GPU 性能分析实践

```cpp
// Renderer.cpp
#include "GPUProfiler.h"

class Renderer {
public:
    void initialize(Engine* engine) {
        mGPUProfiler = std::make_unique<GPUProfiler>(engine);
    }

    void renderFrame(View* view) {
        // Shadow Pass
        mGPUProfiler->beginGPUEvent("ShadowPass");
        renderShadows(view);
        mGPUProfiler->endGPUEvent("ShadowPass");

        // GBuffer Pass
        mGPUProfiler->beginGPUEvent("GBufferPass");
        renderGBuffer(view);
        mGPUProfiler->endGPUEvent("GBufferPass");

        // Lighting Pass
        mGPUProfiler->beginGPUEvent("LightingPass");
        renderLighting(view);
        mGPUProfiler->endGPUEvent("LightingPass");

        // Post Processing
        mGPUProfiler->beginGPUEvent("PostProcessing");
        renderPostProcessing(view);
        mGPUProfiler->endGPUEvent("PostProcessing");

        // 收集结果
        mGPUProfiler->collectResults();

        // 定期输出报告
        if (++mFrameCount % 300 == 0) {
            auto report = mGPUProfiler->generateReport();
            utils::slog.i << report << utils::io::endl;
        }
    }

private:
    std::unique_ptr<GPUProfiler> mGPUProfiler;
    uint64_t mFrameCount = 0;
};
```

---

## 4. 瓶颈识别

### 4.1 CPU vs GPU Bound 检测

```cpp
// BottleneckDetector.h
#pragma once

#include <string>

/**
 * 性能瓶颈检测器
 */
class BottleneckDetector {
public:
    enum class BottleneckType {
        CPU_BOUND,           // CPU 限制
        GPU_BOUND,           // GPU 限制
        MEMORY_BANDWIDTH,    // 内存带宽限制
        FILL_RATE,           // 填充率限制
        VERTEX_PROCESSING,   // 顶点处理限制
        BALANCED             // 平衡
    };

    struct BottleneckAnalysis {
        BottleneckType type;
        double confidence;  // 0.0 - 1.0
        std::string description;
        std::vector<std::string> recommendations;
    };

public:
    /**
     * 分析当前瓶颈
     */
    static BottleneckAnalysis analyze(
        double cpuTime,
        double gpuTime,
        size_t drawCalls,
        size_t triangles,
        size_t textureMemory);

    /**
     * CPU Bound 测试
     *
     * 通过降低渲染分辨率测试,如果性能没有明显提升,说明是 CPU Bound
     */
    static bool testCPUBound(
        double baselineFPS,
        double lowResFPS);

    /**
     * GPU Bound 测试
     *
     * 通过降低渲染分辨率测试,如果性能显著提升,说明是 GPU Bound
     */
    static bool testGPUBound(
        double baselineFPS,
        double lowResFPS);

    /**
     * Fill Rate 测试
     *
     * 通过减少 Overdraw 测试填充率限制
     */
    static bool testFillRate(
        double baselineFPS,
        double noOverdrawFPS);
};
```

```cpp
// BottleneckDetector.cpp
#include "BottleneckDetector.h"

BottleneckDetector::BottleneckAnalysis BottleneckDetector::analyze(
    double cpuTime,
    double gpuTime,
    size_t drawCalls,
    size_t triangles,
    size_t textureMemory) {

    BottleneckAnalysis analysis;

    // 比较 CPU 和 GPU 时间
    double totalTime = cpuTime + gpuTime;
    double cpuPercent = cpuTime / totalTime;
    double gpuPercent = gpuTime / totalTime;

    if (cpuPercent > 0.7) {
        // CPU Bound
        analysis.type = BottleneckType::CPU_BOUND;
        analysis.confidence = cpuPercent;
        analysis.description = "Application is CPU-bound. CPU time significantly exceeds GPU time.";

        analysis.recommendations = {
            "Reduce draw calls by batching geometry",
            "Optimize scene update and culling algorithms",
            "Use instancing for repeated objects",
            "Implement multi-threaded rendering",
            "Reduce state changes between draw calls"
        };

    } else if (gpuPercent > 0.7) {
        // GPU Bound - 需要进一步分析具体原因

        // 高三角形数量 -> Vertex Processing Bound
        if (triangles > 10000000) { // 10M triangles
            analysis.type = BottleneckType::VERTEX_PROCESSING;
            analysis.confidence = 0.8;
            analysis.description = "GPU is vertex-processing bound. Very high triangle count.";

            analysis.recommendations = {
                "Implement LOD (Level of Detail) system",
                "Use aggressive culling (frustum, occlusion)",
                "Simplify mesh geometry",
                "Use tessellation for detailed meshes"
            };

        // 高纹理内存 -> Memory Bandwidth Bound
        } else if (textureMemory > 1024 * 1024 * 1024) { // > 1GB
            analysis.type = BottleneckType::MEMORY_BANDWIDTH;
            analysis.confidence = 0.75;
            analysis.description = "GPU is memory bandwidth bound. High texture memory usage.";

            analysis.recommendations = {
                "Use texture compression (ASTC, ETC2, BC7)",
                "Reduce texture resolution",
                "Use mipmaps to reduce bandwidth",
                "Optimize texture formats (RGB instead of RGBA where possible)"
            };

        // 默认: Fill Rate Bound
        } else {
            analysis.type = BottleneckType::FILL_RATE;
            analysis.confidence = gpuPercent;
            analysis.description = "GPU is fill-rate bound. Fragment shading or overdraw is expensive.";

            analysis.recommendations = {
                "Reduce fragment shader complexity",
                "Implement depth pre-pass to reduce overdraw",
                "Optimize rendering order (front-to-back for opaque)",
                "Use lower rendering resolution",
                "Reduce post-processing effects"
            };
        }

    } else {
        // Balanced
        analysis.type = BottleneckType::BALANCED;
        analysis.confidence = 1.0 - std::abs(cpuPercent - gpuPercent);
        analysis.description = "Performance is relatively balanced between CPU and GPU.";

        analysis.recommendations = {
            "Continue optimizing both CPU and GPU workloads",
            "Monitor performance as content scales",
            "Implement adaptive quality settings"
        };
    }

    return analysis;
}

bool BottleneckDetector::testCPUBound(double baselineFPS, double lowResFPS) {
    // 如果降低分辨率后 FPS 提升不明显 (< 10%),说明是 CPU Bound
    double improvement = (lowResFPS - baselineFPS) / baselineFPS;
    return improvement < 0.1;
}

bool BottleneckDetector::testGPUBound(double baselineFPS, double lowResFPS) {
    // 如果降低分辨率后 FPS 提升明显 (> 30%),说明是 GPU Bound
    double improvement = (lowResFPS - baselineFPS) / baselineFPS;
    return improvement > 0.3;
}

bool BottleneckDetector::testFillRate(double baselineFPS, double noOverdrawFPS) {
    // 如果减少 Overdraw 后 FPS 提升明显,说明是 Fill Rate Bound
    double improvement = (noOverdrawFPS - baselineFPS) / baselineFPS;
    return improvement > 0.2;
}
```

### 4.2 使用瓶颈检测器

```cpp
// PerformanceManager.cpp
#include "BottleneckDetector.h"
#include "PerformanceMetrics.h"

class PerformanceManager {
public:
    void analyzeBottleneck() {
        // 获取当前性能数据
        auto frameStats = mMetrics->getCurrentFrameStats();

        // 分析瓶颈
        auto analysis = BottleneckDetector::analyze(
            frameStats.cpuTime,
            frameStats.gpuTime,
            frameStats.drawCalls,
            frameStats.triangles,
            frameStats.textureMemory
        );

        // 输出分析结果
        utils::slog.i << "Bottleneck Analysis:" << utils::io::endl;
        utils::slog.i << "  Type: " << bottleneckTypeToString(analysis.type) << utils::io::endl;
        utils::slog.i << "  Confidence: " << (analysis.confidence * 100.0) << "%" << utils::io::endl;
        utils::slog.i << "  Description: " << analysis.description << utils::io::endl;
        utils::slog.i << "  Recommendations:" << utils::io::endl;

        for (const auto& recommendation : analysis.recommendations) {
            utils::slog.i << "    - " << recommendation << utils::io::endl;
        }

        // 自动应用优化
        applyOptimizations(analysis);
    }

private:
    void applyOptimizations(const BottleneckDetector::BottleneckAnalysis& analysis) {
        switch (analysis.type) {
            case BottleneckDetector::BottleneckType::CPU_BOUND:
                // 启用更积极的裁剪
                // 增加批处理
                // 减少状态切换
                break;

            case BottleneckDetector::BottleneckType::GPU_BOUND:
            case BottleneckDetector::BottleneckType::FILL_RATE:
                // 降低渲染分辨率
                // 简化着色器
                // 减少后处理
                break;

            case BottleneckDetector::BottleneckType::VERTEX_PROCESSING:
                // 启用 LOD
                // 增强裁剪
                break;

            case BottleneckDetector::BottleneckType::MEMORY_BANDWIDTH:
                // 使用纹理压缩
                // 降低纹理分辨率
                break;

            default:
                break;
        }
    }

    std::string bottleneckTypeToString(BottleneckDetector::BottleneckType type) {
        switch (type) {
            case BottleneckDetector::BottleneckType::CPU_BOUND: return "CPU Bound";
            case BottleneckDetector::BottleneckType::GPU_BOUND: return "GPU Bound";
            case BottleneckDetector::BottleneckType::MEMORY_BANDWIDTH: return "Memory Bandwidth";
            case BottleneckDetector::BottleneckType::FILL_RATE: return "Fill Rate";
            case BottleneckDetector::BottleneckType::VERTEX_PROCESSING: return "Vertex Processing";
            case BottleneckDetector::BottleneckType::BALANCED: return "Balanced";
            default: return "Unknown";
        }
    }

    PerformanceMetrics* mMetrics;
};
```

---

## 5. CMakeLists.txt 配置

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.19)
project(FilamentPerformanceTools)

# C++17 支持
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件
set(SOURCES
    src/PerformanceMetrics.cpp
    src/CPUProfiler.cpp
    src/GPUProfiler.cpp
    src/BottleneckDetector.cpp
)

# 创建库
add_library(${PROJECT_NAME} STATIC ${SOURCES})

# Filament 依赖
target_link_libraries(${PROJECT_NAME} PUBLIC
    filament
    utils
)

# 包含目录
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 示例应用
add_executable(performance_demo examples/performance_demo.cpp)
target_link_libraries(performance_demo PRIVATE
    ${PROJECT_NAME}
    filament
    filamentapp
)
```

---

## 6. 常见问题

### Q1: 如何判断是 CPU Bound 还是 GPU Bound?

**A**: 使用分辨率测试法:

```cpp
// 1. 测试基准 FPS (全分辨率)
double baselineFPS = measureFPS(1920, 1080);

// 2. 测试低分辨率 FPS
double lowResFPS = measureFPS(960, 540);

// 3. 分析结果
if (lowResFPS / baselineFPS > 1.3) {
    // FPS 提升 > 30% -> GPU Bound (fragment 或 fill-rate bound)
    utils::slog.i << "GPU Bound" << utils::io::endl;
} else if (lowResFPS / baselineFPS < 1.1) {
    // FPS 提升 < 10% -> CPU Bound
    utils::slog.i << "CPU Bound" << utils::io::endl;
} else {
    utils::slog.i << "Balanced" << utils::io::endl;
}
```

### Q2: 如何测量 GPU 时间?

**A**: Filament 不直接暴露 Timer Query,可以通过外部工具:

```cpp
// 方式 1: 使用平台特定的工具
// - RenderDoc: 查看每个 Pass 的 GPU 时间
// - Nsight Graphics: GPU Trace 功能
// - Metal Debugger: Shader Profiler

// 方式 2: 使用 Filament 的内置统计
// 注意: 需要在编译时启用 DEBUG 模式
Engine::Config config;
config.stereoscopicEyeCount = 1;
// 创建 Engine 后,可以查询 backend 统计信息
```

### Q3: 什么是 Overdraw? 如何检测?

**A**: Overdraw 是指同一个像素被多次绘制:

```cpp
// 检测 Overdraw:
// 1. 使用 RenderDoc 的 Overdraw 视图
// 2. 使用自定义着色器可视化

// 可视化 Overdraw 的着色器
material {
    name : OverdrawVisualizer,
    shadingModel : unlit,
    blending : add,  // 累加混合

    fragment {
        void material(inout MaterialInputs material) {
            // 每次绘制添加固定颜色
            material.baseColor = vec4(0.1, 0.0, 0.0, 1.0);
        }
    }
}

// Overdraw 越严重,颜色越红
```

---

## 7. 相关文档

- [debugging/01-debugging-workflow.md](./01-debugging-workflow.md) - 调试工作流程
- [debugging/02-renderdoc-usage.md](./02-renderdoc-usage.md) - RenderDoc 使用
- [optimization/01-texture-optimization.md](../optimization/01-texture-optimization.md) - 纹理优化
- [optimization/02-mesh-optimization.md](../optimization/02-mesh-optimization.md) - 网格优化

---

## 8. 总结

性能分析是一个系统化的过程:

1. **建立基准**: 使用 PerformanceMetrics 收集基准数据
2. **识别瓶颈**: 使用 BottleneckDetector 识别性能瓶颈
3. **细化分析**: 使用 CPU/GPU Profiler 定位具体热点
4. **应用优化**: 根据分析结果应用针对性优化
5. **验证效果**: 再次测量并验证优化效果

记住:
- **先测量,再优化**: 避免过早优化
- **关注热点**: 优化占比最高的部分
- **持续监控**: 建立性能回归测试

通过系统化的性能分析方法,你可以有效地优化 Filament 应用的性能。
