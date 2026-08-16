#include "Renderer.hpp"
#include "VulkanContext.hpp"
#include "Swapchain.hpp"
#include "VulkanCheck.hpp"

#include <cassert>
#include <stdexcept>
#include <utility>

Renderer::~Renderer() noexcept
{
    shutdown();
}

// 创建 framecontext 所需要的资源
void Renderer::initialize(VulkanContext &context, Swapchain &swapchain)
{
    if (initialized_)
    {
        throw std::logic_error("Renderer is already initialized");
    }
    try
    {
        context_ = &context;
        swapchain_ = &swapchain;
        QueueFamilyIndices indices = context.queueFamilies();

        for (FrameContext &frame : frames_)
        {
            // 创建 frame.commandPool

            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            // TRANSIENT_BIT 表示该 command pool 中的 command buffer 会频繁分配和释放
            // 而不是 RESET_COMMAND_BUFFER_BIT 表示该 command pool 中的 command buffer 可以单独 reset
            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

            VK_CHECK(vkCreateCommandPool(context.device(), &poolInfo, nullptr, &frame.commandPool));

            // const auto commandPool = frame.commandPool;
            // mainDeletionQueue.pushFunction([this, commandPool]() {
            //     vkDestroyCommandPool(context.device(), commandPool, nullptr);
            // });

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

            // const auto imageAvailable = frame.imageAvailable;
            // mainDeletionQueue.pushFunction([this, imageAvailable]() {
            //     vkDestroySemaphore(context.device(), imageAvailable, nullptr);
            // });

            // 创建带 SIGNALED_BIT 的 frame.renderFence
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            VK_CHECK(vkCreateFence(context.device(), &fenceInfo, nullptr, &frame.renderFence));

            // const VkFence renderFence = frame.renderFence;
            // mainDeletionQueue.pushFunction([this, renderFence]() {
            //     vkDestroyFence(context.device(), renderFence, nullptr);
            // });
        }

        createUploadContext();

        initialized_ = true;
    }
    catch (...)
    {
        shutdown();
        throw;
    }
}

void Renderer::createUploadContext()
{
    if (context_ == nullptr)
    {
        throw std::logic_error("Renderer requires a VulkanContext");
    }

    const QueueFamilyIndices &indices = context_->queueFamilies();
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

    VK_CHECK(vkCreateCommandPool(context_->device(), &poolInfo, nullptr, &uploadContext_.commandPool));
    const VkCommandPool commandPool = uploadContext_.commandPool;

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = uploadContext_.commandPool;
    allocInfo.commandBufferCount = 1;

    VK_CHECK(vkAllocateCommandBuffers(context_->device(), &allocInfo, &uploadContext_.commandBuffer));

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VK_CHECK(vkCreateFence(context_->device(), &fenceInfo, nullptr, &uploadContext_.fence));
}

void Renderer::shutdown() noexcept
{
    if (context_ != nullptr)
    {
        const VkDevice device = context_->device();

        if (device != VK_NULL_HANDLE)
        {
            for (FrameContext &frame : frames_)
            {
                frame.retiredBuffers.clear(); // 析构时自动触发资源释放

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

                if (uploadContext_.fence != VK_NULL_HANDLE)
                {
                    vkDestroyFence(device, uploadContext_.fence, nullptr);
                }
                if (uploadContext_.commandPool != VK_NULL_HANDLE)
                {
                    vkDestroyCommandPool(device, uploadContext_.commandPool, nullptr);
                }
            }
        }
    }

    uploadContext_.fence = VK_NULL_HANDLE;
    uploadContext_.commandBuffer = VK_NULL_HANDLE;
    uploadContext_.commandPool = VK_NULL_HANDLE;

    currentFrame_ = 0;
    frameInProgress_ = false;
    initialized_ = false;

    swapchain_ = nullptr;
    context_ = nullptr;
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

    VK_CHECK(vkWaitForFences(context_->device(), static_cast<uint32_t>(fences.size()), fences.data(), VK_TRUE, UINT64_MAX));
}

void Renderer::retireBuffer(AllocatedBuffer &&buffer)
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

uint32_t Renderer::currentFrameIndex() const noexcept
{
    return currentFrame_;
}

bool Renderer::frameInProgress() const noexcept
{
    return frameInProgress_;
}

// 执行一次性的GPU操作，比如复制Buffer、复制纹理、生成MIPMAP、IBL预计算
void Renderer::immediateSubmit(std::function<void(VkCommandBuffer)> &&function)
{

    // 确认上一次使用的UploadContext已经完成
    VK_CHECK(vkWaitForFences(context_->device(), 1, &uploadContext_.fence, VK_TRUE, UINT64_MAX));

    // Fence必须回到 unsignaled 才能交给下一次 submit
    VK_CHECK(vkResetFences(context_->device(), 1, &uploadContext_.fence));

    // Fence 已经证明上一轮 command buffer 不再 pending，因此可以安全reset command pool
    VK_CHECK(vkResetCommandPool(context_->device(), uploadContext_.commandPool, 0));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(uploadContext_.commandBuffer, &beginInfo));

    function(uploadContext_.commandBuffer);

    VK_CHECK(vkEndCommandBuffer(uploadContext_.commandBuffer));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &uploadContext_.commandBuffer;

    VK_CHECK(vkQueueSubmit(context_->graphicsQueue(), 1, &submitInfo, uploadContext_.fence));

    // 提交后立即等待
    VK_CHECK(vkWaitForFences(context_->device(), 1, &uploadContext_.fence, VK_TRUE, UINT64_MAX));
}

BeginFrameResult Renderer::beginFrame()
{
    // framInProgress 为 true 代表beginFrame已经成功开始了一帧，但还没有调用endFrame来完成提交和呈现。
    if (!initialized_ || context_ == nullptr || swapchain_ == nullptr || frameInProgress_)
    {
        return {
            FrameStatus::Skip, {}};
    }

    FrameContext &frame = frames_[currentFrame_];
    // 1. 等待当前帧fence
    VK_CHECK(vkWaitForFences(context_->device(), 1, &frame.renderFence, VK_TRUE, UINT64_MAX));

    // 2. clear掉帧的buffer，fence已经完成，GPU不再使用这些旧的buffer
    frame.retiredBuffers.clear();

    // 3. vcAcquireNextImageKHR()
    uint32_t imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(
        context_->device(),
        swapchain_->get(),
        UINT64_MAX,
        frame.imageAvailable,
        VK_NULL_HANDLE,
        &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return {FrameStatus::RecreateSwapchain, {}};
    }

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
    {
        VK_CHECK_RESULT(acquireResult, "vkAcquireNextImageKHR");
    }

    // 4. 重置当前帧 command pool
    VK_CHECK(vkResetCommandPool(context_->device(), frame.commandPool, 0));

    // 5. 返回 FrameToken
    frameInProgress_ = true;
    FrameToken token{};
    token.frameIndex = currentFrame_;
    token.imageIndex = imageIndex;
    token.commandBuffer = frame.commandBuffer;

    return {FrameStatus::Ready, token};
}

// CPU 已经录完 commandBuffer
//         ↓
// 等待 imageAvailable semaphore
//         ↓
// GPU 执行 frame.commandBuffer
//         ↓
// 执行完成
//         ↓
// signal renderFinished semaphore
//         ↓
// Present 等 renderFinished
//         ↓
// vkQueuePresentKHR()
FrameStatus Renderer::endFrame(const FrameToken &token)
{
    if (!initialized_ || !frameInProgress_)
    {
        throw std::logic_error("Renderer has no active frame");
    }

    if (token.frameIndex != currentFrame_)
    {
        throw std::logic_error("FrameToken dose not match current frame");
    }

    FrameContext &frame = frames_[currentFrame_];

    if (token.commandBuffer != frame.commandBuffer)
    {
        throw std::logic_error("FrameToken contains invalid command buffer");
    }

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

    // 1. 重置 fence。
    VK_CHECK(vkResetFences(context_->device(), 1, &frame.renderFence));
    // 2. vkQueueSubmit()。
    VK_CHECK(vkQueueSubmit(context_->graphicsQueue(), 1, &submitInfo, frame.renderFence));
    // 3. vkQueuePresentKHR()。
    VkSwapchainKHR swapchianHandle = swapchain_->get();
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchianHandle;
    presentInfo.pImageIndices = &token.imageIndex;
    const VkResult presentResult = vkQueuePresentKHR(context_->presentQueue(), &presentInfo);

    // 4. 更新 currentFrame_。
    currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;

    // 5. 设置 frameInProgress_ = false。
    frameInProgress_ = false;

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
    {
        return FrameStatus::RecreateSwapchain;
    }

    if (presentResult != VK_SUCCESS)
    {
        VK_CHECK_RESULT(presentResult, "vkQueuePresentKHR");
    }

    return FrameStatus::Ready;
}
