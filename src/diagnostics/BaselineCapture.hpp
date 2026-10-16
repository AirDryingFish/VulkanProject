#pragma once

#include "diagnostics/FrameTiming.hpp"
#include "core/FrameConfig.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <iomanip>
#include <locale>
#include <cstddef>

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
    // 这份 gpu 耗时对应哪次提交
    std::uint64_t submissionId = 0;
    // 使用哪个帧资源槽
    std::uint32_t frameSlot = 0;
    // 每个 gpu pass 的耗时
    std::array<double, gpuPassCount> milliseconds{};
    BaselineCpuSample cpu{};
};

struct BaselineCapture
{
    // warmup 提交数
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
    std::vector<BaselineSample> samples;
    std::string message;
    std::string exportMessage;

    std::array<BaselineCpuSample, MAX_FRAMES_IN_FLIGHT> cpuByFrameSlot{};

    void start(std::uint64_t firstId, const FrameTimingConfig& capturedConfig)
    {
        // 用一个全新对象覆盖当前对象
        *this = BaselineCapture{};
        firstSubmissionId = firstId;
        config = capturedConfig;
        samples.reserve(targetSamples);
        running = true;
    }

    void cancel(const char* reason)
    {
        if (!running)
        {
            return;
        }

        running = false;
        complete = false;
        message = reason;
    }

    // 保证测试条件一致
    bool matchesConfig(const FrameTimingConfig& other) const
    {
        return config.sceneId == other.sceneId &&
               config.extent.width == other.extent.width &&
               config.extent.height == other.extent.height &&
               config.sceneSamples == other.sceneSamples &&
               config.shadowEnabled == other.shadowEnabled &&
               config.shadowPreview == other.shadowPreview &&
               config.uiDrawn == other.uiDrawn &&
               config.bypassToneMapping == other.bypassToneMapping &&
               config.exposureEv == other.exposureEv &&
               config.toneMapper == other.toneMapper &&
               config.debugView == other.debugView &&
               config.debugDepthNear == other.debugDepthNear &&
               config.debugDepthFar == other.debugDepthFar;
    }

    // 处理一份返回的 gpu 计时
    void consume(const GpuTimingSnapshot& gpu)
    {
        if (!running || !gpu.hasSample || gpu.submissionId <= lastSeenSubmissionId)
        {
            return;
        }

        lastSeenSubmissionId = gpu.submissionId;
        if (gpu.submissionId < firstSubmissionId)
        {
            return;
        }

        if (!matchesConfig(gpu.config))
        {
            cancel("Capture cancelled: render config changed.");
            return;
        }

        const std::uint64_t offset = gpu.submissionId - firstSubmissionId;
        warmupDone = static_cast<std::uint32_t>(std::min(offset + 1, static_cast<std::uint64_t>(warmupSubmissions)));
        // 在预热阶段跳过
        if (offset < warmupSubmissions)
        {
            return;
        }

        if (gpu.frameSlot >= cpuByFrameSlot.size())
        {
            ++droppedSamples;
            return;
        }

        const BaselineCpuSample& cpu = cpuByFrameSlot[gpu.frameSlot];
        if (!cpu.valid || cpu.submissionId != gpu.submissionId)
        {
            ++droppedSamples;
            return;
        }

        for(double milliseconds : cpu.milliseconds)
        {
            if (!std::isfinite(milliseconds) || milliseconds < 0.0)
            {
                ++droppedSamples;
                return;
            }
        }

        BaselineSample sample{};
        sample.submissionId = gpu.submissionId;
        sample.frameSlot = gpu.frameSlot;
        sample.cpu = cpu;

        for (std::uint32_t i = 0; i < gpuPassCount; ++i)
        {
            const GpuPassTiming& timing = gpu.passes[i];
            if (timing.state != GpuTimingState::Ready ||
                !std::isfinite(timing.milliseconds) ||
                timing.milliseconds < 0.0)
            {
                ++droppedSamples;
                return;
            }
            sample.milliseconds[i] = timing.milliseconds;
        }

        samples.push_back(sample);
        if (samples.size() == targetSamples)
        {
            running = false;
            complete = true;
            message = "Capture complete.";
        }
    }

    bool saveCsv(const char* path) const
    {
        if (!complete)
        {
            return false;
        }

        std::ofstream file(path);

        if (!file)
        {
            return false;
        }

        file.imbue(std::locale::classic());
        file << std::setprecision(9);
        file
            << "submission_id,frame_slot,shadow_ms,scene_ms,post_ui_ms,"
               "cpu_submission_id,"
               "cpu_wait_ms,cpu_acquire_ms,cpu_update_ms,"
               "cpu_record_ms,cpu_submit_ms,cpu_present_ms,"
               "scene_draws,scene_triangles,"
               "shadow_draws,shadow_triangles\n";
        for (const BaselineSample& sample : samples)
        {
            file
                << sample.submissionId << ','
                << sample.frameSlot << ','
                << sample.milliseconds[0] << ','
                << sample.milliseconds[1] << ','
                << sample.milliseconds[2] << ','
                << sample.cpu.submissionId;

            for (double milliseconds : sample.cpu.milliseconds)
            {
                file << ',' << milliseconds;
            }

            const DrawStatistics& draws = sample.cpu.draws;
            file
                << ',' << draws.sceneDraws
                << ',' << draws.sceneTriangles
                << ',' << draws.shadowDraws
                << ',' << draws.shadowTriangles
                << '\n';
        }

        file.flush();
        return file.good();
    }

    void rememberCpu(const CpuFrameTimingSnapshot& cpu)
    {
        if (!running || !cpu.renderer.valid || cpu.renderer.frameSlot >= cpuByFrameSlot.size())
        {
            return;
        }

        BaselineCpuSample& saved = cpuByFrameSlot[cpu.renderer.frameSlot];
        saved = {};
        saved.valid = true;
        saved.submissionId = cpu.renderer.submissionId;
        saved.milliseconds = {
            cpu.renderer.fenceWaitMs,
            cpu.renderer.acquireMs,
            cpu.updateMs,
            cpu.recordMs,
            cpu.renderer.submitMs,
            cpu.renderer.presentMs
        };

        saved.draws = cpu.renderer.draws;
    }
};