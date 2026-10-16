#pragma once

#include "RenderTypes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <iomanip>
#include <locale>


struct BaselineGpuSample
{
    // 这份 gpu 耗时对应哪次提交
    std::uint64_t submissionId = 0;
    // 使用哪个帧资源槽
    std::uint32_t frameSlot = 0;
    // 每个 gpu pass 的耗时
    std::array<double, gpuPassCount> milliseconds{};
};

struct GpuBaselineCapture
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

    GpuTimingConfig config{};
    std::vector<BaselineGpuSample> samples;
    std::string message;

    void start(std::uint64_t firstId, const GpuTimingConfig& capturedConfig)
    {
        // 用一个全新对象覆盖当前对象
        *this = GpuBaselineCapture{};
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
    bool matchesConfig(const GpuTimingConfig& other) const
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

        BaselineGpuSample sample{};
        sample.submissionId = gpu.submissionId;
        sample.frameSlot = gpu.frameSlot;

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
        file << "submission_id,frame_slot,shadow_ms,scene_ms,post_ui_ms\n";
        for (const BaselineGpuSample& sample : samples)
        {
            file
                << sample.submissionId << ','
                << sample.frameSlot << ','
                << sample.milliseconds[0] << ','
                << sample.milliseconds[1] << ','
                << sample.milliseconds[2] << '\n';
        }

        file.flush();
        return file.good();
    }
};