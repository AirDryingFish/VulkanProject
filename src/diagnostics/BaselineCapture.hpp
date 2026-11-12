#pragma once

#include "diagnostics/FrameTiming.hpp"
#include "core/FrameConfig.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

inline constexpr std::size_t baselineCpuMetricCount = 6;

struct BaselineCpuSample
{
    bool valid = false;
    std::uint64_t submissionId = 0;

    // Wait -> Acquire -> Update -> Record -> Submit -> Present
    std::array<double, baselineCpuMetricCount> milliseconds{};

    DrawStatistics draws{};
};

struct BaselineSample
{
    // 一行样本的 CPU/GPU 必须来自同一次提交；帧槽编号会循环复用，不能单独作为身份。
    std::uint64_t submissionId = 0;
    // 使用哪个帧资源槽
    std::uint32_t frameSlot = 0;
    std::uint32_t gpuPassMask = 0;
    // 每个 gpu pass 的耗时
    std::array<double, gpuPassCount> milliseconds{};
    BaselineCpuSample cpu{};
};

struct BaselineCapture
{
    // 按成功提交的 ID 跳过前 120 次预热，再收集 300 行完整有效的样本。
    static constexpr std::uint32_t warmupSubmissions = 120;
    static constexpr std::uint32_t targetSamples = 300;

    bool running = false;
    bool complete = false;

    std::uint64_t firstSubmissionId = 0;
    std::uint64_t lastSeenSubmissionId = 0;

    // warmup 进度
    std::uint32_t warmupDone = 0;
    std::uint32_t droppedSamples = 0;

    FrameTimingConfig config{};
    std::string contextText;

    std::vector<BaselineSample> samples;
    // 采集状态与导出反馈独立保存，导出成功不应覆盖 Capture complete。
    std::string message;
    std::string exportMessage;

    // CPU 数据在提交后立即可得，GPU 数据要等对应 fence 完成；按帧槽暂存以便稍后配对。
    std::array<BaselineCpuSample, MAX_FRAMES_IN_FLIGHT> cpuByFrameSlot{};

    // 采集生命周期
    // 重置样本与 CPU 缓存；调用方在 start 后按提交顺序交付快照。
    void start(std::uint64_t firstId, const FrameTimingConfig& capturedConfig, std::string capturedContext = {});
    void cancel(const char* reason);

    // CPU/GPU 样本配对
    // 同一帧槽复用前先 consume 旧 GPU 快照，再 rememberCpu 新提交。
    void rememberCpu(const CpuFrameTimingSnapshot& cpu);
    void consume(const GpuTimingSnapshot& gpu);

    // 只核对记录在 FrameTimingConfig 的配置；采集期间相机、灯光和材质仍须保持固定。
    bool matchesConfig(const FrameTimingConfig& other) const;

    // 文件导出
    // 仅导出完整采集，写入失败返回 false；bundle 先写 CSV，再写 .config.txt 元数据。
    bool saveCsv(const char* path) const;
    bool saveMetadata(const char* path) const;
    bool saveBundle(const char* csvPath) const;
};
