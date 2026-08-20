# 基准测试

## 📖 概述

基准测试是性能优化的基础,通过建立可重复的性能测试,你可以量化优化效果、检测性能回归,并确保应用在不同平台和场景下都能达到性能目标。

**本文涵盖**:
- 基准测试设计原则
- 自动化基准测试框架
- 性能回归检测
- 跨平台性能对比
- 持续集成中的基准测试

**目标**:
- 建立可重复的性能基准
- 自动化性能测试流程
- 及早发现性能回归
- 为优化提供量化依据

---

## 1. 基准测试设计原则

### 1.1 SMART 原则

```cpp
/**
 * 基准测试设计原则
 *
 * S - Specific    (具体的): 明确测试目标
 * M - Measurable  (可测量): 有明确的指标
 * A - Achievable  (可实现): 目标可达成
 * R - Relevant    (相关的): 与实际场景相关
 * T - Time-bound  (有时限): 有明确的时间范围
 */
class BenchmarkDesignPrinciples {
public:
    /**
     * 原则 1: 隔离变量
     * 每次只测试一个变量
     */
    static void testSingleVariable() {
        // 好的示例: 只测试纹理分辨率的影响
        runBenchmark("Texture_512x512");
        runBenchmark("Texture_1024x1024");
        runBenchmark("Texture_2048x2048");
    }

    /**
     * 原则 2: 可重复性
     * 确保测试结果可重复
     */
    static void ensureRepeatability() {
        // 固定随机种子
        srand(42);

        // 固定时间步长
        constexpr double fixedDeltaTime = 1.0 / 60.0;

        // 预热阶段
        for (int i = 0; i < 60; ++i) {
            warmupFrame();
        }

        // 正式测试
        for (int i = 0; i < 300; ++i) {
            benchmarkFrame(fixedDeltaTime);
        }
    }

    /**
     * 原则 3: 统计显著性
     * 运行足够多的样本
     */
    static void ensureStatisticalSignificance() {
        constexpr int MIN_SAMPLES = 100;
        constexpr int WARMUP_FRAMES = 60;

        // 预热
        for (int i = 0; i < WARMUP_FRAMES; ++i) {
            renderFrame();
        }

        // 收集样本
        std::vector<double> samples;
        samples.reserve(MIN_SAMPLES);

        for (int i = 0; i < MIN_SAMPLES; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            renderFrame();
            auto end = std::chrono::high_resolution_clock::now();

            double frameTime = std::chrono::duration<double, std::milli>(end - start).count();
            samples.push_back(frameTime);
        }

        // 计算统计数据
        analyzeStatistics(samples);
    }

private:
    static void runBenchmark(const char* name);
    static void warmupFrame();
    static void benchmarkFrame(double deltaTime);
    static void renderFrame();
    static void analyzeStatistics(const std::vector<double>& samples);
};
```

### 1.2 基准测试场景设计

```cpp
// BenchmarkScenario.h
#pragma once

#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <string>
#include <functional>

using namespace filament;

/**
 * 基准测试场景
 */
class BenchmarkScenario {
public:
    struct Config {
        std::string name;
        std::string description;
        int warmupFrames = 60;
        int testFrames = 300;
        bool fixedTimestep = true;
        double timestep = 1.0 / 60.0;
    };

    struct Result {
        std::string scenarioName;
        double avgFPS;
        double minFPS;
        double maxFPS;
        double avgFrameTime;  // ms
        double p95FrameTime;  // 95th percentile
        double p99FrameTime;  // 99th percentile
        size_t avgDrawCalls;
        size_t avgTriangles;
        size_t avgMemoryUsage;  // bytes
    };

public:
    BenchmarkScenario(const Config& config);
    virtual ~BenchmarkScenario() = default;

    /**
     * 初始化场景
     */
    virtual void setup(Engine* engine, Scene* scene) = 0;

    /**
     * 清理场景
     */
    virtual void teardown(Engine* engine, Scene* scene) = 0;

    /**
     * 更新场景 (每帧调用)
     */
    virtual void update(double deltaTime) = 0;

    /**
     * 运行基准测试
     */
    Result run(Engine* engine, Renderer* renderer, Scene* scene, View* view);

    /**
     * 获取配置
     */
    const Config& getConfig() const { return mConfig; }

protected:
    Config mConfig;

private:
    void warmup(Engine* engine, Renderer* renderer, View* view);
    void benchmark(Engine* engine, Renderer* renderer, View* view, Result& result);
};
```

```cpp
// BenchmarkScenario.cpp
#include "BenchmarkScenario.h"
#include <chrono>
#include <algorithm>
#include <numeric>

BenchmarkScenario::BenchmarkScenario(const Config& config)
    : mConfig(config) {
}

BenchmarkScenario::Result BenchmarkScenario::run(
    Engine* engine,
    Renderer* renderer,
    Scene* scene,
    View* view) {

    Result result;
    result.scenarioName = mConfig.name;

    // 预热
    warmup(engine, renderer, view);

    // 基准测试
    benchmark(engine, renderer, view, result);

    return result;
}

void BenchmarkScenario::warmup(Engine* engine, Renderer* renderer, View* view) {
    for (int i = 0; i < mConfig.warmupFrames; ++i) {
        double deltaTime = mConfig.fixedTimestep ? mConfig.timestep : 0.016;
        update(deltaTime);

        if (renderer->beginFrame(mSwapChain)) {
            renderer->render(view);
            renderer->endFrame();
        }
    }
}

void BenchmarkScenario::benchmark(Engine* engine, Renderer* renderer, View* view, Result& result) {
    using Clock = std::chrono::high_resolution_clock;

    std::vector<double> frameTimes;
    frameTimes.reserve(mConfig.testFrames);

    size_t totalDrawCalls = 0;
    size_t totalTriangles = 0;

    for (int i = 0; i < mConfig.testFrames; ++i) {
        double deltaTime = mConfig.fixedTimestep ? mConfig.timestep : 0.016;

        auto startTime = Clock::now();

        update(deltaTime);

        if (renderer->beginFrame(mSwapChain)) {
            renderer->render(view);
            renderer->endFrame();
        }

        auto endTime = Clock::now();

        double frameTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        frameTimes.push_back(frameTime);

        // 收集统计信息 (如果可用)
        // totalDrawCalls += getDrawCallCount();
        // totalTriangles += getTriangleCount();
    }

    // 计算统计数据
    std::vector<double> sortedTimes = frameTimes;
    std::sort(sortedTimes.begin(), sortedTimes.end());

    double sum = std::accumulate(frameTimes.begin(), frameTimes.end(), 0.0);
    result.avgFrameTime = sum / frameTimes.size();
    result.avgFPS = 1000.0 / result.avgFrameTime;

    result.minFPS = 1000.0 / sortedTimes.back();
    result.maxFPS = 1000.0 / sortedTimes.front();

    size_t p95Index = static_cast<size_t>(sortedTimes.size() * 0.95);
    size_t p99Index = static_cast<size_t>(sortedTimes.size() * 0.99);
    result.p95FrameTime = sortedTimes[p95Index];
    result.p99FrameTime = sortedTimes[p99Index];

    result.avgDrawCalls = totalDrawCalls / mConfig.testFrames;
    result.avgTriangles = totalTriangles / mConfig.testFrames;
}
```

---

## 2. 典型基准测试场景

### 2.1 静态场景测试

```cpp
// StaticSceneBenchmark.cpp
#include "BenchmarkScenario.h"

/**
 * 静态场景基准测试
 *
 * 测试目标: 测试渲染管线的基础性能
 * 场景特点: 静止的相机和物体
 * 适用于: 测试 GPU 渲染性能
 */
class StaticSceneBenchmark : public BenchmarkScenario {
public:
    StaticSceneBenchmark() : BenchmarkScenario({
        .name = "StaticScene",
        .description = "Static scene with fixed camera",
        .warmupFrames = 60,
        .testFrames = 300,
        .fixedTimestep = true
    }) {}

    void setup(Engine* engine, Scene* scene) override {
        // 加载静态场景
        loadModels(engine, scene);
        setupLighting(scene);
        setupCamera();
    }

    void teardown(Engine* engine, Scene* scene) override {
        // 清理资源
        cleanupModels(engine, scene);
    }

    void update(double deltaTime) override {
        // 静态场景不需要更新
    }

private:
    void loadModels(Engine* engine, Scene* scene);
    void setupLighting(Scene* scene);
    void setupCamera();
    void cleanupModels(Engine* engine, Scene* scene);
};
```

### 2.2 动态场景测试

```cpp
/**
 * 动态场景基准测试
 *
 * 测试目标: 测试场景更新和动画性能
 * 场景特点: 移动的相机和物体、动画
 * 适用于: 测试 CPU 场景更新性能
 */
class DynamicSceneBenchmark : public BenchmarkScenario {
public:
    DynamicSceneBenchmark() : BenchmarkScenario({
        .name = "DynamicScene",
        .description = "Dynamic scene with animations",
        .warmupFrames = 60,
        .testFrames = 300,
        .fixedTimestep = true
    }) {}

    void setup(Engine* engine, Scene* scene) override {
        loadAnimatedModels(engine, scene);
        setupLighting(scene);
        mTime = 0.0;
    }

    void teardown(Engine* engine, Scene* scene) override {
        cleanupModels(engine, scene);
    }

    void update(double deltaTime) override {
        mTime += deltaTime;

        // 更新相机位置 (圆周运动)
        float radius = 10.0f;
        float cameraX = radius * cos(mTime);
        float cameraZ = radius * sin(mTime);
        updateCamera(cameraX, 5.0f, cameraZ);

        // 更新动画
        for (auto* animator : mAnimators) {
            animator->applyAnimation(0, mTime);
            animator->updateBoneMatrices();
        }
    }

private:
    double mTime;
    std::vector<gltfio::Animator*> mAnimators;

    void loadAnimatedModels(Engine* engine, Scene* scene);
    void setupLighting(Scene* scene);
    void updateCamera(float x, float y, float z);
    void cleanupModels(Engine* engine, Scene* scene);
};
```

### 2.3 压力测试

```cpp
/**
 * 压力测试基准
 *
 * 测试目标: 测试极限场景下的性能
 * 场景特点: 大量物体、高复杂度
 * 适用于: 发现性能极限和稳定性问题
 */
class StressTestBenchmark : public BenchmarkScenario {
public:
    StressTestBenchmark(int objectCount)
        : BenchmarkScenario({
            .name = "StressTest_" + std::to_string(objectCount),
            .description = "Stress test with " + std::to_string(objectCount) + " objects",
            .warmupFrames = 60,
            .testFrames = 300,
            .fixedTimestep = true
          }),
          mObjectCount(objectCount) {}

    void setup(Engine* engine, Scene* scene) override {
        // 创建大量物体
        mEntities.reserve(mObjectCount);

        for (int i = 0; i < mObjectCount; ++i) {
            Entity entity = EntityManager::get().create();

            // 随机位置
            float x = (rand() % 100 - 50) / 10.0f;
            float y = (rand() % 100 - 50) / 10.0f;
            float z = (rand() % 100 - 50) / 10.0f;

            // 创建渲染组件
            RenderableManager::Builder(1)
                .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,
                         mVertexBuffer, mIndexBuffer)
                .material(0, mMaterialInstance)
                .build(*engine, entity);

            // 设置变换
            auto& tcm = engine->getTransformManager();
            tcm.setTransform(tcm.getInstance(entity),
                           mat4f::translation(float3{x, y, z}));

            scene->addEntity(entity);
            mEntities.push_back(entity);
        }
    }

    void teardown(Engine* engine, Scene* scene) override {
        for (Entity entity : mEntities) {
            scene->remove(entity);
            engine->destroy(entity);
        }
        mEntities.clear();
    }

    void update(double deltaTime) override {
        mTime += deltaTime;

        // 旋转所有物体
        auto& tcm = mEngine->getTransformManager();
        for (Entity entity : mEntities) {
            auto instance = tcm.getInstance(entity);
            mat4f rotation = mat4f::rotation(mTime, float3{0, 1, 0});
            mat4f transform = tcm.getTransform(instance);
            tcm.setTransform(instance, transform * rotation);
        }
    }

private:
    int mObjectCount;
    double mTime = 0.0;
    std::vector<Entity> mEntities;

    Engine* mEngine;
    VertexBuffer* mVertexBuffer;
    IndexBuffer* mIndexBuffer;
    MaterialInstance* mMaterialInstance;
};
```

---

## 3. 基准测试框架

### 3.1 基准测试管理器

```cpp
// BenchmarkManager.h
#pragma once

#include "BenchmarkScenario.h"
#include <vector>
#include <memory>

/**
 * 基准测试管理器
 *
 * 负责运行和管理所有基准测试
 */
class BenchmarkManager {
public:
    struct Report {
        std::vector<BenchmarkScenario::Result> results;
        std::string timestamp;
        std::string platform;
        std::string gpuModel;
        std::string driverVersion;
    };

public:
    BenchmarkManager(Engine* engine, Renderer* renderer);

    /**
     * 注册基准测试
     */
    void registerBenchmark(std::unique_ptr<BenchmarkScenario> scenario);

    /**
     * 运行所有基准测试
     */
    Report runAll();

    /**
     * 运行指定基准测试
     */
    BenchmarkScenario::Result runBenchmark(const std::string& name);

    /**
     * 导出报告
     */
    void exportReportToJSON(const Report& report, const std::string& filename);
    void exportReportToCSV(const Report& report, const std::string& filename);
    void exportReportToHTML(const Report& report, const std::string& filename);

    /**
     * 对比报告
     */
    static void compareReports(const Report& baseline, const Report& current);

private:
    Engine* mEngine;
    Renderer* mRenderer;
    Scene* mScene;
    View* mView;
    SwapChain* mSwapChain;

    std::vector<std::unique_ptr<BenchmarkScenario>> mScenarios;

    void collectSystemInfo(Report& report);
};
```

```cpp
// BenchmarkManager.cpp
#include "BenchmarkManager.h"
#include <utils/Log.h>
#include <fstream>
#include <iomanip>
#include <ctime>

BenchmarkManager::BenchmarkManager(Engine* engine, Renderer* renderer)
    : mEngine(engine), mRenderer(renderer) {

    // 创建场景、视图等
    mScene = mEngine->createScene();
    mView = mEngine->createView();
    mView->setScene(mScene);

    // 设置视口等
}

void BenchmarkManager::registerBenchmark(std::unique_ptr<BenchmarkScenario> scenario) {
    mScenarios.push_back(std::move(scenario));
}

BenchmarkManager::Report BenchmarkManager::runAll() {
    Report report;

    // 收集系统信息
    collectSystemInfo(report);

    utils::slog.i << "Running " << mScenarios.size() << " benchmarks..." << utils::io::endl;

    for (auto& scenario : mScenarios) {
        utils::slog.i << "Running: " << scenario->getConfig().name << utils::io::endl;

        // 设置场景
        scenario->setup(mEngine, mScene);

        // 运行测试
        auto result = scenario->run(mEngine, mRenderer, mScene, mView);

        // 清理场景
        scenario->teardown(mEngine, mScene);

        // 保存结果
        report.results.push_back(result);

        utils::slog.i << "  Avg FPS: " << result.avgFPS << utils::io::endl;
        utils::slog.i << "  P95: " << result.p95FrameTime << " ms" << utils::io::endl;
    }

    return report;
}

BenchmarkScenario::Result BenchmarkManager::runBenchmark(const std::string& name) {
    for (auto& scenario : mScenarios) {
        if (scenario->getConfig().name == name) {
            scenario->setup(mEngine, mScene);
            auto result = scenario->run(mEngine, mRenderer, mScene, mView);
            scenario->teardown(mEngine, mScene);
            return result;
        }
    }

    throw std::runtime_error("Benchmark not found: " + name);
}

void BenchmarkManager::exportReportToJSON(const Report& report, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        utils::slog.e << "Failed to open file: " << filename << utils::io::endl;
        return;
    }

    file << "{\n";
    file << "  \"timestamp\": \"" << report.timestamp << "\",\n";
    file << "  \"platform\": \"" << report.platform << "\",\n";
    file << "  \"gpu\": \"" << report.gpuModel << "\",\n";
    file << "  \"driver\": \"" << report.driverVersion << "\",\n";
    file << "  \"results\": [\n";

    for (size_t i = 0; i < report.results.size(); ++i) {
        const auto& result = report.results[i];

        file << "    {\n";
        file << "      \"name\": \"" << result.scenarioName << "\",\n";
        file << "      \"avgFPS\": " << result.avgFPS << ",\n";
        file << "      \"minFPS\": " << result.minFPS << ",\n";
        file << "      \"maxFPS\": " << result.maxFPS << ",\n";
        file << "      \"avgFrameTime\": " << result.avgFrameTime << ",\n";
        file << "      \"p95FrameTime\": " << result.p95FrameTime << ",\n";
        file << "      \"p99FrameTime\": " << result.p99FrameTime << ",\n";
        file << "      \"avgDrawCalls\": " << result.avgDrawCalls << ",\n";
        file << "      \"avgTriangles\": " << result.avgTriangles << ",\n";
        file << "      \"avgMemoryUsage\": " << result.avgMemoryUsage << "\n";
        file << "    }";

        if (i < report.results.size() - 1) {
            file << ",";
        }
        file << "\n";
    }

    file << "  ]\n";
    file << "}\n";

    file.close();
}

void BenchmarkManager::compareReports(const Report& baseline, const Report& current) {
    utils::slog.i << "=== Benchmark Comparison ===" << utils::io::endl;
    utils::slog.i << "Baseline: " << baseline.timestamp << utils::io::endl;
    utils::slog.i << "Current:  " << current.timestamp << utils::io::endl;
    utils::slog.i << utils::io::endl;

    for (const auto& currentResult : current.results) {
        // 查找对应的基准结果
        auto it = std::find_if(baseline.results.begin(), baseline.results.end(),
            [&](const BenchmarkScenario::Result& r) {
                return r.scenarioName == currentResult.scenarioName;
            });

        if (it == baseline.results.end()) {
            utils::slog.w << "No baseline found for: " << currentResult.scenarioName << utils::io::endl;
            continue;
        }

        const auto& baselineResult = *it;

        double fpsDiff = ((currentResult.avgFPS - baselineResult.avgFPS) / baselineResult.avgFPS) * 100.0;

        utils::slog.i << currentResult.scenarioName << ":" << utils::io::endl;
        utils::slog.i << "  Baseline FPS: " << baselineResult.avgFPS << utils::io::endl;
        utils::slog.i << "  Current FPS:  " << currentResult.avgFPS << utils::io::endl;
        utils::slog.i << "  Difference:   " << std::fixed << std::setprecision(2)
                      << fpsDiff << "%" << utils::io::endl;

        if (fpsDiff < -5.0) {
            utils::slog.w << "  WARNING: Performance regression detected!" << utils::io::endl;
        } else if (fpsDiff > 5.0) {
            utils::slog.i << "  IMPROVEMENT: Performance improved!" << utils::io::endl;
        }

        utils::slog.i << utils::io::endl;
    }
}

void BenchmarkManager::collectSystemInfo(Report& report) {
    // 时间戳
    auto now = std::time(nullptr);
    char buffer[100];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    report.timestamp = buffer;

    // 平台信息
#ifdef __ANDROID__
    report.platform = "Android";
#elif defined(__APPLE__)
    report.platform = "iOS/macOS";
#elif defined(_WIN32)
    report.platform = "Windows";
#else
    report.platform = "Linux";
#endif

    // GPU 信息 (需要从 backend 获取)
    report.gpuModel = "Unknown";
    report.driverVersion = "Unknown";
}
```

---

## 4. 持续集成中的基准测试

### 4.1 自动化测试脚本

```bash
#!/bin/bash
# run_benchmarks.sh

set -e

# 配置
BENCHMARK_EXECUTABLE="./build/benchmark_runner"
BASELINE_REPORT="benchmarks/baseline_report.json"
CURRENT_REPORT="benchmarks/current_report.json"
THRESHOLD=-5.0  # Performance regression threshold (%)

echo "=== Running Filament Benchmarks ==="

# 运行基准测试
${BENCHMARK_EXECUTABLE} --output ${CURRENT_REPORT}

# 对比基准
if [ -f ${BASELINE_REPORT} ]; then
    echo "Comparing with baseline..."
    ${BENCHMARK_EXECUTABLE} --compare ${BASELINE_REPORT} ${CURRENT_REPORT} --threshold ${THRESHOLD}

    EXIT_CODE=$?
    if [ $EXIT_CODE -ne 0 ]; then
        echo "ERROR: Performance regression detected!"
        exit 1
    fi
else
    echo "No baseline found. Saving current report as baseline."
    cp ${CURRENT_REPORT} ${BASELINE_REPORT}
fi

echo "Benchmarks completed successfully!"
```

### 4.2 CI 配置 (GitHub Actions)

```yaml
# .github/workflows/benchmarks.yml
name: Performance Benchmarks

on:
  pull_request:
    branches: [ main ]
  push:
    branches: [ main ]

jobs:
  benchmark:
    runs-on: ubuntu-latest

    steps:
      - uses: actions/checkout@v3

      - name: Setup Dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake ninja-build

      - name: Build Benchmarks
        run: |
          mkdir build
          cd build
          cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
          ninja benchmark_runner

      - name: Run Benchmarks
        run: |
          ./build/benchmark_runner --output current_report.json

      - name: Download Baseline
        uses: actions/download-artifact@v3
        with:
          name: baseline-report
          path: .
        continue-on-error: true

      - name: Compare Performance
        run: |
          if [ -f baseline_report.json ]; then
            ./build/benchmark_runner --compare baseline_report.json current_report.json --threshold -5.0
          else
            echo "No baseline found, skipping comparison"
          fi

      - name: Upload Current Report
        uses: actions/upload-artifact@v3
        with:
          name: current-report
          path: current_report.json

      - name: Update Baseline (on main branch)
        if: github.ref == 'refs/heads/main'
        uses: actions/upload-artifact@v3
        with:
          name: baseline-report
          path: current_report.json
```

---

## 5. CMakeLists.txt 配置

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.19)
project(FilamentBenchmarks)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件
set(BENCHMARK_SOURCES
    src/BenchmarkScenario.cpp
    src/BenchmarkManager.cpp
    src/StaticSceneBenchmark.cpp
    src/DynamicSceneBenchmark.cpp
    src/StressTestBenchmark.cpp
)

# 基准测试库
add_library(benchmark_lib STATIC ${BENCHMARK_SOURCES})

target_link_libraries(benchmark_lib PUBLIC
    filament
    gltfio_core
    utils
)

target_include_directories(benchmark_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 基准测试可执行文件
add_executable(benchmark_runner
    src/main.cpp
)

target_link_libraries(benchmark_runner PRIVATE
    benchmark_lib
    filament
    filamentapp
)

# 安装
install(TARGETS benchmark_runner
    RUNTIME DESTINATION bin
)
```

---

## 6. 常见问题

### Q1: 如何确保基准测试的可重复性?

**A**: 关键要素:

```cpp
// 1. 固定随机种子
srand(42);

// 2. 使用固定时间步长
constexpr double FIXED_TIMESTEP = 1.0 / 60.0;

// 3. 预热阶段
for (int i = 0; i < 60; ++i) {
    renderFrame();
}

// 4. 禁用 VSync (避免帧率上限)
SwapChain::CONFIG_HAS_STENCIL_BUFFER

// 5. 固定相机和光照
setupFixedCamera();
setupFixedLighting();
```

### Q2: 多少样本数是合适的?

**A**: 建议:

```
- 最少: 100 帧
- 标准: 300 帧 (5 秒 @ 60 FPS)
- 严格: 1000+ 帧

同时考虑:
- 预热帧: 60 帧 (1 秒)
- 丢弃异常值: 前 10% 和后 10%
- 统计显著性: 使用 t-test 验证
```

### Q3: 如何检测性能回归?

**A**: 回归检测策略:

```cpp
// 1. 设置阈值
constexpr double REGRESSION_THRESHOLD = -5.0;  // -5%

// 2. 对比 FPS
double percentChange = ((currentFPS - baselineFPS) / baselineFPS) * 100.0;

if (percentChange < REGRESSION_THRESHOLD) {
    reportRegression();
}

// 3. 对比 P95/P99 (更稳定的指标)
if (currentP95 > baselineP95 * 1.1) {  // 10% 增长
    reportRegression();
}
```

---

## 7. 相关文档

- [debugging/04-performance-profiling.md](./04-performance-profiling.md) - 性能分析方法
- [optimization/01-texture-optimization.md](../optimization/01-texture-optimization.md) - 纹理优化
- [optimization/09-performance-case-studies.md](../optimization/09-performance-case-studies.md) - 性能优化案例

---

## 8. 总结

建立完善的基准测试体系是性能优化的基础:

1. **设计原则**: 遵循 SMART 原则,确保测试有效性
2. **场景覆盖**: 包含静态、动态、压力等多种场景
3. **自动化**: 集成到 CI/CD 流程中
4. **回归检测**: 及早发现性能问题
5. **持续改进**: 定期更新基准测试

通过系统化的基准测试,你可以量化优化效果,确保应用性能持续提升。
