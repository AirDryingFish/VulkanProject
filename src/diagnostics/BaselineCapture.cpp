#include "diagnostics/BaselineCapture.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

void BaselineCapture::start(std::uint64_t firstId, const FrameTimingConfig& capturedConfig, std::string capturedContext)
{
    // 新采集不继承旧样本/消息/CPU 缓存；调用方必须在 start 后再 rememberCpu。
    *this = BaselineCapture{};

    firstSubmissionId = firstId;
    config = capturedConfig;
    contextText = std::move(capturedContext);

    samples.reserve(targetSamples);
    running = true;
}

void BaselineCapture::cancel(const char* reason)
{
    if (!running)
    {
        return;
    }

    running = false;
    complete = false;
    message = reason;
}

void BaselineCapture::rememberCpu(const CpuFrameTimingSnapshot& cpu)
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
    saved.milliseconds = {cpu.renderer.fenceWaitMs, cpu.renderer.acquireMs, cpu.updateMs, cpu.recordMs,
                          cpu.renderer.submitMs,    cpu.renderer.presentMs};

    saved.draws = cpu.renderer.draws;
}

void BaselineCapture::consume(const GpuTimingSnapshot& gpu)
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

    for (double milliseconds : cpu.milliseconds)
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

    // 六项 CPU 指标和当前配置预计执行的 GPU 区间全部有效才接收整行。
    // NotReady/NotExecuted 不用 0 代替；真实的 0ms 则允许保留。
    const std::uint32_t expectedMask = expectedGpuPassMask(config);

    for (std::uint32_t i = 0; i < gpuPassCount; ++i)
    {
        const std::uint32_t bit = std::uint32_t{1} << i;
        const GpuPassTiming& timing = gpu.passes[i];

        if ((expectedMask & bit) == 0)
        {
            if (timing.state != GpuTimingState::NotExecuted)
            {
                ++droppedSamples;
                return;
            }

            continue;
        }

        if (timing.state != GpuTimingState::Ready || !std::isfinite(timing.milliseconds) || timing.milliseconds < 0.0)
        {
            ++droppedSamples;
            return;
        }

        sample.gpuPassMask |= bit;
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

bool BaselineCapture::matchesConfig(const FrameTimingConfig& other) const
{
    return config.sceneId == other.sceneId && config.extent.width == other.extent.width &&
           config.extent.height == other.extent.height && config.sceneSamples == other.sceneSamples &&
           config.renderPath == other.renderPath && config.lightCullingMode == other.lightCullingMode &&
           config.gBufferGeometry == other.gBufferGeometry && config.gBufferView == other.gBufferView &&
           config.shadowEnabled == other.shadowEnabled && config.shadowMode == other.shadowMode &&
           config.shadowPcfEnabled == other.shadowPcfEnabled && config.shadowConstantBias == other.shadowConstantBias &&
           config.shadowSlopeBias == other.shadowSlopeBias &&
           config.shadowReceiverBiasNormalized == other.shadowReceiverBiasNormalized &&
           config.shadowReceiverBiasWorld == other.shadowReceiverBiasWorld &&
           config.shadowBlendFraction == other.shadowBlendFraction && config.shadowPreview == other.shadowPreview &&
           config.uiDrawn == other.uiDrawn && config.bypassToneMapping == other.bypassToneMapping &&
           config.exposureEv == other.exposureEv && config.toneMapper == other.toneMapper &&
           config.debugView == other.debugView && config.debugDepthNear == other.debugDepthNear &&
           config.debugDepthFar == other.debugDepthFar && config.shadowPreviewLayer == other.shadowPreviewLayer &&
           config.shadowDistance == other.shadowDistance && config.shadowSplitLambda == other.shadowSplitLambda &&
           config.shadowDepthMargin == other.shadowDepthMargin &&
           config.shadowCsmLightNear == other.shadowCsmLightNear &&
           glm::all(glm::equal(config.shadowSingleCenter, other.shadowSingleCenter)) &&
           config.shadowSingleHalfExtent == other.shadowSingleHalfExtent &&
           config.shadowFrame.activeCascadeCount == other.shadowFrame.activeCascadeCount &&
           config.shadowFrame.cameraNearDistance == other.shadowFrame.cameraNearDistance &&
           config.shadowFrame.receiverFarDistance == other.shadowFrame.receiverFarDistance &&
           config.shadowFrame.cameraFovDegrees == other.shadowFrame.cameraFovDegrees &&
           glm::all(glm::equal(config.shadowFrame.receiverBiasNormalized, other.shadowFrame.receiverBiasNormalized)) &&
           config.shadowTarget.extent.width == other.shadowTarget.extent.width &&
           config.shadowTarget.extent.height == other.shadowTarget.extent.height &&
           config.shadowTarget.format == other.shadowTarget.format &&
           config.shadowTarget.arrayLayers == other.shadowTarget.arrayLayers;
}
