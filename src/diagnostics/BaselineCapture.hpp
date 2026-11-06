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
#include <limits>
#include <utility>

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

    void start(std::uint64_t firstId, const FrameTimingConfig& capturedConfig, std::string capturedContext = {})
    {
        // 新采集不继承旧样本/消息/CPU 缓存；调用方必须在 start 后再 rememberCpu。
        *this = BaselineCapture{};

        firstSubmissionId = firstId;
        config = capturedConfig;
        contextText = std::move(capturedContext);

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

    // 检查已记录的渲染配置是否改变；相机姿态、灯光和材质编辑不在这份配置中，
    // 采集时仍需由使用者保持它们固定。
    bool matchesConfig(const FrameTimingConfig& other) const
    {
        return config.sceneId == other.sceneId &&
               config.extent.width == other.extent.width &&
               config.extent.height == other.extent.height &&
               config.sceneSamples == other.sceneSamples &&
               config.gBufferGeometry == other.gBufferGeometry &&
               config.gBufferView == other.gBufferView &&
               config.shadowEnabled == other.shadowEnabled &&
               config.shadowMode == other.shadowMode &&
               config.shadowPcfEnabled == other.shadowPcfEnabled &&
               config.shadowConstantBias == other.shadowConstantBias &&
               config.shadowSlopeBias == other.shadowSlopeBias &&
               config.shadowReceiverBiasNormalized ==
                   other.shadowReceiverBiasNormalized &&
               config.shadowReceiverBiasWorld ==
                   other.shadowReceiverBiasWorld &&
               config.shadowBlendFraction == other.shadowBlendFraction &&
               config.shadowPreview == other.shadowPreview &&
               config.uiDrawn == other.uiDrawn &&
               config.bypassToneMapping == other.bypassToneMapping &&
               config.exposureEv == other.exposureEv &&
               config.toneMapper == other.toneMapper &&
               config.debugView == other.debugView &&
               config.debugDepthNear == other.debugDepthNear &&
               config.debugDepthFar == other.debugDepthFar &&
               config.shadowPreviewLayer == other.shadowPreviewLayer &&
               config.shadowDistance == other.shadowDistance &&
               config.shadowSplitLambda == other.shadowSplitLambda &&
               config.shadowDepthMargin == other.shadowDepthMargin &&
               config.shadowCsmLightNear == other.shadowCsmLightNear &&
               glm::all(glm::equal(
                   config.shadowSingleCenter,
                   other.shadowSingleCenter)) &&
               config.shadowSingleHalfExtent == other.shadowSingleHalfExtent &&
               config.shadowFrame.activeCascadeCount ==
                   other.shadowFrame.activeCascadeCount &&
               config.shadowFrame.cameraNearDistance ==
                   other.shadowFrame.cameraNearDistance &&
               config.shadowFrame.receiverFarDistance ==
                   other.shadowFrame.receiverFarDistance &&
               config.shadowFrame.cameraFovDegrees ==
                   other.shadowFrame.cameraFovDegrees &&
               glm::all(glm::equal(
                   config.shadowFrame.receiverBiasNormalized,
                   other.shadowFrame.receiverBiasNormalized)) &&
               config.shadowTarget.extent.width ==
                   other.shadowTarget.extent.width &&
               config.shadowTarget.extent.height ==
                   other.shadowTarget.extent.height &&
               config.shadowTarget.format ==
                   other.shadowTarget.format &&
               config.shadowTarget.arrayLayers ==
                   other.shadowTarget.arrayLayers;
    }

    // 接收延迟返回的 GPU 快照，通常在 beginFrame() 后、覆盖该槽 CPU 缓存前调用。
    void consume(const GpuTimingSnapshot& gpu)
    {
        if (!running || !gpu.hasSample || gpu.submissionId <= lastSeenSubmissionId)
        {
            return;
        }

        // UI/渲染器可能连续保留同一快照；每个提交只处理一次，drop 也不能重复计数。
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
        // 预热按提交 ID 计算，不按有效样本数计算；被丢弃的正式样本也不会重新预热。
        if (offset < warmupSubmissions)
        {
            return;
        }

        if (gpu.frameSlot >= cpuByFrameSlot.size())
        {
            ++droppedSamples;
            return;
        }

        // 槽号相同还不够：它可能已经被后续帧复用，必须再次核对 submissionId。
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

        // 六项 CPU 指标和三个 GPU pass 全部有效才接收整行。
        // NotReady/NotExecuted 不用 0 代替；真实的 0ms 则允许保留。
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

        // 固定小数点格式，避免系统 locale 使用逗号后破坏 CSV 列结构。
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
    bool saveMetadata(const char *path) const
    {
        if (!complete || samples.empty() ||
            path == nullptr || path[0] == '\0')
        {
            return false;
        }

        std::ofstream file(path);
        if (!file)
        {
            return false;
        }

        file.imbue(std::locale::classic());
        file << std::boolalpha
             << std::setprecision(
                    std::numeric_limits<float>::max_digits10);

        // 普通标量：key=value
        const auto write = [&](const char *key, const auto &value)
        {
            file << key << '=' << value << '\n';
        };

        // 三维向量：key=x y z
        const auto writeVec3 =
            [&](const char *key, const glm::vec3 &value)
        {
            file << key << '='
                 << value.x << ' '
                 << value.y << ' '
                 << value.z << '\n';
        };

        // 四维向量：key=x y z w
        const auto writeVec4 =
            [&](const char *key, const glm::vec4 &value)
        {
            file << key << '='
                 << value.x << ' '
                 << value.y << ' '
                 << value.z << ' '
                 << value.w << '\n';
        };

        // 采集身份和样本数量。
        write("schema_version", 2);
        write("warmup_first_submission_id", firstSubmissionId);
        write("first_measured_submission_id",
              samples.front().submissionId);
        write("last_measured_submission_id",
              samples.back().submissionId);
        write("warmup_submissions", warmupSubmissions);
        write("target_samples", targetSamples);
        write("actual_samples", samples.size());
        write("dropped_samples", droppedSamples);

        // 场景与实际渲染目标配置。
        write("scene_id", std::quoted(config.sceneId));
        write("width", config.extent.width);
        write("height", config.extent.height);
        write("scene_samples",
              static_cast<std::uint32_t>(config.sceneSamples));

        write("gbuffer_geometry", config.gBufferGeometry);
        write("gbuffer_samples", config.gBufferGeometry ? 1 : 0);
        write("gbuffer_view", static_cast<std::int32_t>(config.gBufferView));

        // 阴影模式及输入参数。
        const char *modeName = "Unknown";
        if (config.shadowMode == ShadowMode::Single)
        {
            modeName = "Single";
        }
        else if (config.shadowMode == ShadowMode::Cascaded)
        {
            modeName = "Cascaded";
        }

        write("shadow_mode", modeName);
        write("shadow_mode_value",
              static_cast<std::uint32_t>(config.shadowMode));
        write("shadow_enabled", config.shadowEnabled);
        write("shadow_preview", config.shadowPreview);
        write("shadow_preview_layer", config.shadowPreviewLayer);
        write("shadow_pcf_enabled", config.shadowPcfEnabled);
        write("shadow_blend_fraction", config.shadowBlendFraction);
        write("shadow_constant_bias", config.shadowConstantBias);
        write("shadow_slope_bias", config.shadowSlopeBias);
        write("single_receiver_bias",
              config.shadowReceiverBiasNormalized);
        write("csm_receiver_bias_world",
              config.shadowReceiverBiasWorld);
        write("shadow_distance", config.shadowDistance);
        write("shadow_split_lambda", config.shadowSplitLambda);
        write("shadow_depth_margin", config.shadowDepthMargin);
        write("shadow_csm_light_near", config.shadowCsmLightNear);
        writeVec3("single_center", config.shadowSingleCenter);
        write("single_half_extent", config.shadowSingleHalfExtent);

        // 本次 UBO 使用的实际结果。
        write("active_cascades",
              config.shadowFrame.activeCascadeCount);
        write("camera_near",
              config.shadowFrame.cameraNearDistance);
        write("receiver_far",
              config.shadowFrame.receiverFarDistance);
        write("camera_fov_degrees",
              config.shadowFrame.cameraFovDegrees);
        writeVec4("cascade_receiver_bias",
                  config.shadowFrame.receiverBiasNormalized);

        // 实际分配的阴影图形状。
        write("shadow_width", config.shadowTarget.extent.width);
        write("shadow_height", config.shadowTarget.extent.height);
        write("allocated_shadow_layers",
              config.shadowTarget.arrayLayers);
        write("shadow_format_value",
              static_cast<std::int32_t>(config.shadowTarget.format));

        const char *formatName = "Unknown";
        if (config.shadowTarget.format == VK_FORMAT_D32_SFLOAT)
        {
            formatName = "D32_SFLOAT";
        }
        else if (config.shadowTarget.format == VK_FORMAT_D16_UNORM)
        {
            formatName = "D16_UNORM";
        }
        write("shadow_format", formatName);

        // 显示设置也会影响对照条件。
        write("ui_drawn", config.uiDrawn);
        write("bypass_tone_mapping", config.bypassToneMapping);
        write("exposure_ev", config.exposureEv);
        write("tone_mapper_value",
              static_cast<std::uint32_t>(config.toneMapper));
        write("debug_view_value",
              static_cast<std::int32_t>(config.debugView));
        write("debug_depth_near", config.debugDepthNear);
        write("debug_depth_far", config.debugDepthFar);

        write("context_available", !contextText.empty());

        file << "\n[capture_context]\n";
        file << contextText;

        file.flush();
        if (!file.good())
        {
            return false;
        }

        file.close();
        return !file.fail();
    }

    bool saveBundle(const char *csvPath) const
    {
        if (!complete || samples.empty() ||
            csvPath == nullptr || csvPath[0] == '\0')
        {
            return false;
        }

        if (!saveCsv(csvPath))
        {
            return false;
        }

        const std::string metadataPath =
            std::string(csvPath) + ".config.txt";

        return saveMetadata(metadataPath.c_str());
    }

    void rememberCpu(const CpuFrameTimingSnapshot& cpu)
    {
        if (!running || !cpu.renderer.valid || cpu.renderer.frameSlot >= cpuByFrameSlot.size())
        {
            return;
        }

        // 覆盖前必须先 consume 该槽上一次提交的 GPU 快照，顺序由 Application 保证。
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
