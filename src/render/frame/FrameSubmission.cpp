#include "render/Renderer.hpp"
#include "vulkan/context/VulkanContext.hpp"
#include "vulkan/presentation/Swapchain.hpp"
#include "vulkan/VulkanCheck.hpp"
#include "diagnostics/CpuTiming.hpp"

#include <cassert>
#include <stdexcept>
#include <imgui_impl_vulkan.h>

// 帧槽等待、绘制路径编排与提交；pass 自身负责附件布局和命令录制。
void Renderer::createFrameContexts()
{
    VulkanContext& context = *context_;
    QueueFamilyIndices indices = context.queueFamilies();

    for (FrameContext& frame : frames_)
    {
        // 创建 frame.commandPool
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        // TRANSIENT_BIT 表示该 command pool 中的 command buffer 会频繁分配和释放
        // 而不是 RESET_COMMAND_BUFFER_BIT 表示该 command pool 中的 command buffer 可以单独 reset
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

        VK_CHECK(vkCreateCommandPool(context.device(), &poolInfo, nullptr, &frame.commandPool));

        // 从 frame.commandPool 分配 frame.commandBuffer

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = frame.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VK_CHECK(vkAllocateCommandBuffers(context.device(), &allocInfo, &frame.commandBuffer));

        // 创建 frame.imageAvailable 信号量
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VK_CHECK(vkCreateSemaphore(context.device(), &semaphoreInfo, nullptr, &frame.imageAvailable));

        // 首次复用槽位时还没有 GPU 提交，因此 fence 初始为 signaled，避免首帧永久等待。
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VK_CHECK(vkCreateFence(context.device(), &fenceInfo, nullptr, &frame.renderFence));
    }
}

void Renderer::destroyFrameContexts() noexcept
{
    const VkDevice device = context_->device();
    for (FrameContext& frame : frames_)
    {
        frame.retiredBuffers.clear(); // 析构时自动触发资源释放

        if (frame.timestampQueryPool != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(device, frame.timestampQueryPool, nullptr);
            frame.timestampQueryPool = VK_NULL_HANDLE;
        }
        frame.timestampQueriesPending = false;
        frame.recordedGpuPassMask = 0;
        frame.recordedTimingConfig = {};
        frame.submittedGpuPassMask = 0;
        frame.submittedGpuSubmissionId = 0;
        frame.submittedTimingConfig = {};
        frame.recordedDraws = {};

        if (frame.renderFence != VK_NULL_HANDLE)
        {
            vkDestroyFence(device, frame.renderFence, nullptr);
        }

        if (frame.imageAvailable != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, frame.imageAvailable, nullptr);
        }

        if (frame.commandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device, frame.commandPool, nullptr);
        }
        frame.renderFence = VK_NULL_HANDLE;
        frame.imageAvailable = VK_NULL_HANDLE;
        frame.commandPool = VK_NULL_HANDLE;
        frame.commandBuffer = VK_NULL_HANDLE;
    }
}

void Renderer::waitForAllFrames()
{
    if (!initialized_)
    {
        return;
    }

    std::array<VkFence, MAX_FRAMES_IN_FLIGHT> fences{};

    for (std::size_t i = 0; i < frames_.size(); i++)
    {
        fences[i] = frames_[i].renderFence;
    }

    VK_CHECK(
        vkWaitForFences(context_->device(), static_cast<uint32_t>(fences.size()), fences.data(), VK_TRUE, UINT64_MAX));
}

BeginFrameResult Renderer::beginFrame()
{
    // 同一 Renderer 只允许一个活动 token；上一帧必须先完成录制与提交。
    if (!initialized_ || context_ == nullptr || swapchain_ == nullptr || hasActiveFrame_)
    {
        return {FrameStatus::Skip, {}};
    }

    FrameContext& frame = frames_[currentFrame_];

    currentCpuTiming_ = {};
    currentCpuTiming_.frameSlot = currentFrame_;
    // wait 表示主机等待这个 frame slot 可以复用
    const auto waitBegin = CpuClock::now();

    // 1. 等待当前帧fence
    const VkResult waitResult = vkWaitForFences(context_->device(), 1, &frame.renderFence, VK_TRUE, UINT64_MAX);
    currentCpuTiming_.fenceWaitMs = cpuElapsedMs(waitBegin, CpuClock::now());
    VK_CHECK_RESULT(waitResult, "vkWaitForFences");

    // fence 已证明旧提交完成：先读旧 query，再 acquire，避免 out-of-date 时漏读或重复发布。
    collectGpuTimings(frame, currentFrame_);

    // 2. clear掉帧的buffer，fence已经完成，GPU不再使用这些旧的buffer
    frame.retiredBuffers.clear();

    // 3. vcAcquireNextImageKHR()
    uint32_t imageIndex = 0;

    // acquire 表示主机取得交换链图像的调用时间
    const auto acquireBegin = CpuClock::now();

    const VkResult acquireResult = vkAcquireNextImageKHR(context_->device(), swapchain_->get(), UINT64_MAX,
                                                         frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);

    currentCpuTiming_.acquireMs = cpuElapsedMs(acquireBegin, CpuClock::now());

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return {FrameStatus::RecreateSwapchain, {}};
    }

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
    {
        VK_CHECK_RESULT(acquireResult, "vkAcquireNextImageKHR");
    }

    // 此时旧提交已结束，才能重置槽内命令。这里仍不 reset fence：acquire 失败时没有新提交来 signal。
    VK_CHECK(vkResetCommandPool(context_->device(), frame.commandPool, 0));

    // 5. 返回 FrameToken
    hasActiveFrame_ = true;
    hasRecordedFrame_ = false;
    FrameToken token{};
    token.frameIndex = currentFrame_;
    token.imageIndex = imageIndex;
    token.commandBuffer = frame.commandBuffer;

    return {FrameStatus::Ready, token};
}

void Renderer::recordFrame(const FrameToken& token, const RenderFrameData& data)
{
    // 绘制主线：Shadow 写光源深度 -> Scene 写 HDR 颜色 -> Post 采样 HDR 写交换链并叠加 UI。
    // 此处只把这些操作录入 command buffer，真正提交由 endFrame() 完成。

    if (!initialized_ || !hasActiveFrame_)
    {
        throw std::logic_error("Renderer has no active frame to record");
    }

    if (token.frameIndex != currentFrame_)
    {
        throw std::logic_error("FrameToken does not match current frame");
    }

    if (hasRecordedFrame_)
    {
        throw std::logic_error("Renderer frame has already been recorded");
    }

    FrameContext& frame = frames_[currentFrame_];

    if (token.commandBuffer != frame.commandBuffer)
    {
        throw std::logic_error("FrameToken contains invalid command buffer");
    }

    if (token.imageIndex >= swapchain_->imageCount())
    {
        throw std::out_of_range("FrameToken contains invalid swapchain image index");
    }

    const bool useTimestamp2 = gpuTimingSupported_ && context_->capabilities().synchronization2Enabled;
    const auto cmdWriteTimestamp2 = useTimestamp2 ? context_->deviceFunctions().cmdWriteTimestamp2 : nullptr;
    if (useTimestamp2 && cmdWriteTimestamp2 == nullptr)
    {
        throw std::logic_error("GPU timing requires vkCmdWriteTimestamp2");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VK_CHECK(vkBeginCommandBuffer(token.commandBuffer, &beginInfo));

    const bool useDeferred = data.renderPath == RenderPath::Deferred && deferredLightingAvailable();
    captureFrameTimingConfig(token, data, useDeferred);

    if (gpuTimingSupported_)
    {
        // 重置 command pool 不会重置 query；query reset 必须单独录制在 RenderPass 外。
        vkCmdResetQueryPool(token.commandBuffer, frame.timestampQueryPool, 0, gpuTimestampQueryCount);
    }

    const auto beginGpuPass = [&](GpuPass pass)
    {
        if (!gpuTimingSupported_)
        {
            return;
        }

        const std::uint32_t passIndex = static_cast<std::uint32_t>(pass);
        const std::uint32_t query = passIndex * 2;
        if (useTimestamp2)
        {
            cmdWriteTimestamp2(token.commandBuffer, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, frame.timestampQueryPool,
                               query);
        }
        else
        {
            vkCmdWriteTimestamp(token.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame.timestampQueryPool,
                                query // 每个 Pass 占两个连续 query：开始 2*i，结束 2*i+1。
            );
        }
    };

    const auto endGpuPass = [&](GpuPass pass)
    {
        if (!gpuTimingSupported_)
        {
            return;
        }

        const std::uint32_t passIndex = static_cast<uint32_t>(pass);
        const std::uint32_t query = passIndex * 2 + 1;

        if (useTimestamp2)
        {
            cmdWriteTimestamp2(token.commandBuffer, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, frame.timestampQueryPool,
                               query);
        }
        else
        {
            vkCmdWriteTimestamp(token.commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame.timestampQueryPool,
                                query // 每个 Pass 占两个连续 query：开始 2*i，结束 2*i+1。
            );
        }

        frame.recordedGpuPassMask |= (1u << passIndex);
    };

    beginGpuPass(GpuPass::FrameGpu);

    beginGpuPass(GpuPass::Shadow);
    recordShadowPass(token, data);
    endGpuPass(GpuPass::Shadow);

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{
        data.clearColor.r,
        data.clearColor.g,
        data.clearColor.b,
        data.clearColor.a,
    }};
    clearValues[1].depthStencil = {1.0f, 0};

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchain_->extent().width);
    viewport.height = static_cast<float>(swapchain_->extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(token.commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_->extent();
    vkCmdSetScissor(token.commandBuffer, 0, 1, &scissor);

    if (useDeferred)
    {
        prepareDeferredFrame(token, data);
    }

    if (frame.recordedTimingConfig.gBufferGeometry)
    {
        beginGpuPass(GpuPass::Geometry);
        recordGBufferPass(token, data);
        endGpuPass(GpuPass::Geometry);
    }

    if (useDeferred)
    {
        if (frame.recordedTimingConfig.lightCullingMode == LightCullingMode::Tiled)
        {
            beginGpuPass(GpuPass::LightCull);
            recordTileLightLists(token, data);
            endGpuPass(GpuPass::LightCull);
        }

        beginGpuPass(GpuPass::Lighting);
        recordDeferredLightingPass(token, data);
        endGpuPass(GpuPass::Lighting);
    }
    else
    {
        beginGpuPass(GpuPass::Scene);
        recordForwardPass(token, data, clearValues);
        endGpuPass(GpuPass::Scene);
    }

    VkClearValue presentClear{};
    presentClear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    beginGpuPass(GpuPass::PostAndUi);
    beginPresentPass(token, presentClear);

    // 使用前面已设置好的全窗口 viewport/scissor
    vkCmdSetViewport(token.commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(token.commandBuffer, 0, 1, &scissor);

    if (frame.recordedTimingConfig.gBufferView != GBufferView::Off)
    {
        recordGBufferPreview(token, data);
    }
    else
    {
        recordToneMapping(token, data, useDeferred);
    }

    // ----

    if (data.showShadowDepth)
    {
        recordShadowPreview(token, data);
    }

    if (data.imguiDrawData != nullptr)
    {
        ImGui_ImplVulkan_RenderDrawData(data.imguiDrawData, token.commandBuffer);
    }
    // vkCmdEndRenderPass(token.commandBuffer);

    endPresentPass(token);
    endGpuPass(GpuPass::PostAndUi);

    endGpuPass(GpuPass::FrameGpu);
    // ----

    VK_CHECK(vkEndCommandBuffer(token.commandBuffer));

    hasRecordedFrame_ = true;
}

// endFrame 负责提交与请求呈现，返回不代表 GPU 或屏幕显示已经完成。
// imageAvailable/renderFinished 协调 GPU 与呈现；renderFence 供 CPU 下次安全复用帧槽。
FrameStatus Renderer::endFrame(const FrameToken& token)
{

    if (!initialized_ || !hasActiveFrame_)
    {
        throw std::logic_error("Renderer has no active frame");
    }

    if (!hasRecordedFrame_)
    {
        throw std::logic_error("Renderer active frame has not been recorded");
    }

    if (token.frameIndex != currentFrame_)
    {
        throw std::logic_error("FrameToken does not match current frame");
    }

    FrameContext& frame = frames_[currentFrame_];

    if (token.commandBuffer != frame.commandBuffer)
    {
        throw std::logic_error("FrameToken contains invalid command buffer");
    }

    const bool useSync2 = context_->capabilities().synchronization2Enabled;
    const auto queueSubmit2 = useSync2 ? context_->deviceFunctions().queueSubmit2 : nullptr;
    if (useSync2 && queueSubmit2 == nullptr)
    {
        throw std::logic_error("Immediate submit requires vkQueueSubmit2");
    }

    // 呈现用的完成信号量按交换链图像管理；不能与 frameIndex 下的帧槽资源混用索引。
    const VkSemaphore renderFinished = swapchain_->renderFinishedSemaphore(token.imageIndex);

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinished;

    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = frame.imageAvailable;
    waitInfo.value = 0;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT; // 之后受等待的 stage
    waitInfo.deviceIndex = 0;

    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = renderFinished;
    signalInfo.value = 0;
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; // 之前必须完成的stage
    signalInfo.deviceIndex = 0;

    VkCommandBufferSubmitInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandInfo.commandBuffer = frame.commandBuffer;
    commandInfo.deviceMask = 1;

    VkSubmitInfo2 submitInfo2{};
    submitInfo2.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo2.waitSemaphoreInfoCount = 1;
    submitInfo2.pWaitSemaphoreInfos = &waitInfo;
    submitInfo2.signalSemaphoreInfoCount = 1;
    submitInfo2.pSignalSemaphoreInfos = &signalInfo;
    submitInfo2.commandBufferInfoCount = 1;
    submitInfo2.pCommandBufferInfos = &commandInfo;

    // 紧邻提交才 reset fence；若 submit 失败，异常路径退出，不能继续等待这个无人 signal 的 fence。
    VK_CHECK(vkResetFences(context_->device(), 1, &frame.renderFence));

    const auto submitBegin = CpuClock::now();

    // 2. vkQueueSubmit()。
    VkResult submitResult = useSync2 ? queueSubmit2(context_->graphicsQueue(), 1, &submitInfo2, frame.renderFence)
                                     : vkQueueSubmit(context_->graphicsQueue(), 1, &submitInfo, frame.renderFence);

    currentCpuTiming_.submitMs = cpuElapsedMs(submitBegin, CpuClock::now());
    currentCpuTiming_.draws = frame.recordedDraws;
    VK_CHECK_RESULT(submitResult, useSync2 ? "vkQueueSubmit2" : "vkQueueSubmit");

    // submit 成功才生成 ID/发布 pending；即使随后 Present 要求重建，这份 GPU 工作仍真实存在。
    const std::uint64_t submissionId = ++gpuSubmissionCounter_;

    currentCpuTiming_.submissionId = submissionId;
    currentCpuTiming_.config = frame.recordedTimingConfig;

    if (gpuTimingSupported_)
    {
        frame.submittedGpuPassMask = frame.recordedGpuPassMask;
        frame.submittedGpuSubmissionId = submissionId;
        frame.submittedTimingConfig = frame.recordedTimingConfig;
        frame.timestampQueriesPending = true;
    }

    // 3. vkQueuePresentKHR()。
    VkSwapchainKHR swapchainHandle = swapchain_->get();
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchainHandle;
    presentInfo.pImageIndices = &token.imageIndex;

    const auto presentBegin = CpuClock::now();
    const VkResult presentResult = vkQueuePresentKHR(context_->presentQueue(), &presentInfo);
    currentCpuTiming_.presentMs = cpuElapsedMs(presentBegin, CpuClock::now());

    // 4. 更新 currentFrame_。
    currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;

    // 5. 设置 hasActiveFrame_ = false。
    hasActiveFrame_ = false;
    hasRecordedFrame_ = false;

    const bool needsRecreate = presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR;

    if (presentResult != VK_SUCCESS && !needsRecreate)
    {
        VK_CHECK_RESULT(presentResult, "vkQueuePresentKHR");
    }
    currentCpuTiming_.valid = true;
    latestCpuTiming_ = std::move(currentCpuTiming_);

    return needsRecreate ? FrameStatus::RecreateSwapchain : FrameStatus::Ready;
}

void Renderer::retireBuffer(GpuBuffer&& buffer)
{
    if (!buffer)
    {
        return;
    }

    if (!initialized_)
    {
        buffer.reset();
        return;
    }

    frames_[currentFrame_].retiredBuffers.push_back(std::move(buffer));
}

bool Renderer::hasActiveFrame() const noexcept
{
    return hasActiveFrame_;
}

const RendererCpuTimingSnapshot& Renderer::cpuTimingSnapshot() const noexcept
{
    return latestCpuTiming_;
};
