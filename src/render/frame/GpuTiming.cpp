#include "render/Renderer.hpp"
#include "vulkan/context/VulkanContext.hpp"
#include "vulkan/presentation/Swapchain.hpp"
#include "vulkan/VulkanCheck.hpp"

#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

// 逐帧时间戳查询、实际配置快照与完成结果读回。
void Renderer::createTimestampQueryPools()
{
    if (context_ == nullptr)
    {
        throw std::logic_error("Renderer requires a VulkanContex");
    }

    const VkPhysicalDevice physicalDevice = context_->physicalDevice();

    // -- 1. 查询这个 gpu 一共有多少个 queue family --
    std::uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    // ----

    // -- 2. 真正把每个 queue family 的信息填入进去 --
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
    // ----

    // graphics queue 属于哪个 queue family
    const std::uint32_t graphicsFamily = context_->queueFamilies().graphicsFamily.value();
    timestampValidBits_ = queueFamilies.at(graphicsFamily).timestampValidBits;

    // 获取整个物理 GPU 的通用属性
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    // gpu timestamp 每增加 1 tick，相当于多少纳秒
    timestampPeriodNs_ = static_cast<double>(properties.limits.timestampPeriod);
    if (timestampValidBits_ == 0)
    {
        std::cout << "GPU timing unavailable: graphics queue does not support timestamps.\n";
        return;
    }

    VkQueryPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    poolInfo.queryCount = gpuTimestampQueryCount;
    for (FrameContext& frame : frames_)
    {
        VkQueryPool queryPool = VK_NULL_HANDLE;
        VK_CHECK(vkCreateQueryPool(context_->device(), &poolInfo, nullptr, &queryPool));
        frame.timestampQueryPool = queryPool;
        frame.timestampQueriesPending = false;
    }

    gpuTimingSupported_ = true;

    std::cout << "GPU timing prepared: graphics family=" << graphicsFamily << ", valid bits=" << timestampValidBits_
              << ", period=" << timestampPeriodNs_ << " ns/tick"
              << ", pools=" << frames_.size() << ", queries per pool=" << gpuTimestampQueryCount << "\n";
}

void Renderer::captureFrameTimingConfig(const FrameToken& token, const RenderFrameData& data, bool useDeferred)
{
    FrameContext& frame = frames_[token.frameIndex];
    frame.recordedGpuPassMask = 0;
    frame.recordedTimingConfig.sceneId = data.sceneId;
    frame.recordedTimingConfig.extent = swapchain_->extent();

    frame.recordedTimingConfig.renderPath = useDeferred ? RenderPath::Deferred : RenderPath::Forward;
    frame.recordedTimingConfig.lightCullingMode =
        useDeferred && data.lightCullingMode == LightCullingMode::Tiled && tiledLightingAvailable()
            ? LightCullingMode::Tiled
            : LightCullingMode::None;
    frame.recordedTimingConfig.sceneSamples = useDeferred ? VK_SAMPLE_COUNT_1_BIT : sceneSamples_;
    frame.recordedTimingConfig.gBufferGeometry = (useDeferred || data.buildGBuffer) && gBufferGeometryAvailable();
    frame.recordedTimingConfig.gBufferView =
        frame.recordedTimingConfig.gBufferGeometry && gBufferPreviewPipeline_ != VK_NULL_HANDLE ? data.gBufferView
                                                                                                : GBufferView::Off;

    frame.recordedTimingConfig.shadowEnabled = data.directionalShadowsEnabled;
    frame.recordedTimingConfig.shadowPreview = data.showShadowDepth;
    frame.recordedTimingConfig.shadowPreviewLayer = data.shadowPreviewLayer;

    frame.recordedTimingConfig.shadowMode = data.shadowMode;
    frame.recordedTimingConfig.shadowBlendFraction = data.shadowBlendFraction;
    frame.recordedTimingConfig.shadowPcfEnabled = data.shadowPcfEnabled;
    frame.recordedTimingConfig.shadowConstantBias = data.shadowConstantBias;
    frame.recordedTimingConfig.shadowSlopeBias = data.shadowSlopeBias;
    frame.recordedTimingConfig.shadowReceiverBiasNormalized = data.shadowReceiverBiasNormalized;
    frame.recordedTimingConfig.shadowReceiverBiasWorld = data.shadowReceiverBiasWorld;

    frame.recordedTimingConfig.shadowDistance = data.shadowDistance;
    frame.recordedTimingConfig.shadowSplitLambda = data.shadowSplitLambda;
    frame.recordedTimingConfig.shadowDepthMargin = data.shadowDepthMargin;
    frame.recordedTimingConfig.shadowCsmLightNear = data.shadowCsmLightNear;
    frame.recordedTimingConfig.shadowSingleCenter = data.shadowSingleCenter;
    frame.recordedTimingConfig.shadowSingleHalfExtent = data.shadowSingleHalfExtent;

    frame.recordedTimingConfig.shadowFrame = data.shadowFrame;
    const GpuImage& shadowDepth = shadowTargets_.at(token.frameIndex).depth;
    const VkExtent3D shadowExtent = shadowDepth.extent();
    frame.recordedTimingConfig.shadowTarget = {
        {shadowExtent.width, shadowExtent.height}, shadowDepth.format(), shadowDepth.arrayLayers()};

    frame.recordedTimingConfig.uiDrawn = data.imguiDrawData != nullptr;
    frame.recordedTimingConfig.bypassToneMapping = data.bypassToneMapping;
    frame.recordedDraws = {};
    frame.recordedTimingConfig.exposureEv = data.exposureEv;
    frame.recordedTimingConfig.toneMapper = data.toneMapper;
    frame.recordedTimingConfig.debugView = data.debugView;
    frame.recordedTimingConfig.debugDepthNear = data.debugDepthNear;
    frame.recordedTimingConfig.debugDepthFar = data.debugDepthFar;
}

void Renderer::collectGpuTimings(FrameContext& frame, std::uint32_t frameSlot)
{
    if (!gpuTimingSupported_ || !frame.timestampQueriesPending)
    {
        return;
    }

    GpuTimingSnapshot snapshot{};
    snapshot.supported = true;
    snapshot.hasSample = true;
    snapshot.submissionId = frame.submittedGpuSubmissionId;
    snapshot.frameSlot = frameSlot;
    snapshot.config = frame.submittedTimingConfig;

    for (std::uint32_t i = 0; i < gpuPassCount; ++i)
    {
        GpuPassTiming& timing = snapshot.passes[i];
        const std::uint32_t passBit = 1u << i;
        if ((frame.submittedGpuPassMask & passBit) == 0)
        {
            timing.state = GpuTimingState::NotExecuted;
            continue;
        }

        // 已由槽位 fence 等待完成，不再加 WAIT_BIT；只读提交 mask 中确实写过的一对 query。
        std::uint64_t ticks[2]{};
        const VkResult result =
            vkGetQueryPoolResults(context_->device(), frame.timestampQueryPool, i * 2, 2, sizeof(ticks), ticks,
                                  sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);
        if (result == VK_NOT_READY)
        {
            // fence 后仍不可用属于异常样本：保留状态/日志，不用未定义 ticks 计算，也不忙等。
            timing.state = GpuTimingState::NotReady;
            std::cerr << "GPU timestamp not ready: submission=" << snapshot.submissionId << ", frame slot=" << frameSlot
                      << ", pass=" << i << "\n";

            continue;
        }

        VK_CHECK_RESULT(result, "vkGetQueryPoolResults");

        std::uint64_t deltaTicks = ticks[1] - ticks[0];
        // GPU 时间戳可能只有 timestampValidBits_(N) 个有效位，计数值达到 2^N - 1 后会重新从 0 开始
        if (timestampValidBits_ < 64)
        {
            // 差值在有效位范围内取模；64 位单独跳过，避免执行未定义的 1ULL << 64。
            const std::uint64_t mask = (std::uint64_t{1} << timestampValidBits_) - 1;
            deltaTicks &= mask;
        }
        timing.milliseconds = static_cast<double>(deltaTicks) * timestampPeriodNs_ / 1'000'000.0;
        timing.state = GpuTimingState::Ready;
    }

    latestGpuTiming_ = std::move(snapshot);
    // 一份提交只消费一次；acquire 随后失败或 UI 多次刷新都不能重复累计这个样本。
    frame.timestampQueriesPending = false;
}

const GpuTimingSnapshot& Renderer::gpuTimingSnapshot() const noexcept
{
    return latestGpuTiming_;
};
