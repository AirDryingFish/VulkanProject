#include "Renderer.hpp"
#include "VulkanContext.hpp"
#include "Swapchain.hpp"
#include "VulkanCheck.hpp"
#include "UploadCommands.hpp"

#include <cassert>
#include <stdexcept>
#include <utility>
#include <imgui_impl_vulkan.h>
#include <cstring>

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

        createDescriptorSetLayouts();
        createPipelineLayouts();

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
        // -- 初始化阴影渲染管线 --
        createShadowTargets();
        createShadowRenderPass();
        createShadowFramebuffers();
        createShadowPipeline();
        // -- hdr 资源--
        createHdrTargets();
        createSceneRenderPass();
        createHdrFramebuffers();

        createPostDescriptors();
        createPresentRenderPass();
        createPostPipelineLayout();

        // -- 创建 pipeline --
        createGraphicsPipeline();
        createSkyboxPipeline();
        createShadowPreviewPipeline();
        createTonemapPipeline();

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
            if (tonemapPipeline_ != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, tonemapPipeline_, nullptr);
                tonemapPipeline_ = VK_NULL_HANDLE;
            }

            if (postPipelineLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(device, postPipelineLayout_, nullptr);
                postPipelineLayout_ = VK_NULL_HANDLE;
            }

            if (presentRenderPass_ != VK_NULL_HANDLE)
            {
                vkDestroyRenderPass(device, presentRenderPass_, nullptr);
                presentRenderPass_ = VK_NULL_HANDLE;
            }

            destroyPostDescriptors();
            destroyHdrTargets();
            if (sceneRenderPass_ != VK_NULL_HANDLE)
            {
                vkDestroyRenderPass(
                    device,
                    sceneRenderPass_,
                    nullptr);
                sceneRenderPass_ = VK_NULL_HANDLE;
            }
            destroyShadowTargets();

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
                frame.commandBuffer = VK_NULL_HANDLE;
            }

            if (uploadContext_.fence != VK_NULL_HANDLE)
            {
                vkDestroyFence(device, uploadContext_.fence, nullptr);
            }
            if (uploadContext_.commandPool != VK_NULL_HANDLE)
            {
                vkDestroyCommandPool(device, uploadContext_.commandPool, nullptr);
            }

            if (skyboxPipeline_ != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, skyboxPipeline_, nullptr);
            }

            if (graphicsPipeline_ != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, graphicsPipeline_, nullptr);
            }

            if (skyboxPipelineLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(device, skyboxPipelineLayout_, nullptr);
            }

            if (scenePipelineLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(device, scenePipelineLayout_, nullptr);
            }

            if (skyboxDescriptorSetLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device, skyboxDescriptorSetLayout_, nullptr);
            }

            if (materialDescriptorSetLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device, materialDescriptorSetLayout_, nullptr);
            }

            if (frameDescriptorSetLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device, frameDescriptorSetLayout_, nullptr);
            }
        }
    }

    uploadContext_.fence = VK_NULL_HANDLE;
    uploadContext_.commandBuffer = VK_NULL_HANDLE;
    uploadContext_.commandPool = VK_NULL_HANDLE;

    skyboxPipeline_ = VK_NULL_HANDLE;
    graphicsPipeline_ = VK_NULL_HANDLE;
    scenePipelineLayout_ = VK_NULL_HANDLE;
    skyboxPipelineLayout_ = VK_NULL_HANDLE;
    materialDescriptorSetLayout_ = VK_NULL_HANDLE;
    frameDescriptorSetLayout_ = VK_NULL_HANDLE;
    skyboxDescriptorSetLayout_ = VK_NULL_HANDLE;
    sceneRenderPass_ = VK_NULL_HANDLE;
    hdrFormat_ = VK_FORMAT_UNDEFINED;
    sceneDepthFormat_ = VK_FORMAT_UNDEFINED;
    sceneSamples_ = VK_SAMPLE_COUNT_1_BIT;

    currentFrame_ = 0;
    hasActiveFrame_ = false;
    hasRecordedFrame_ = false;
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

void Renderer::retireBuffer(GpuBuffer &&buffer)
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

bool Renderer::hasActiveFrame() const noexcept
{
    return hasActiveFrame_;
}


VkDescriptorSetLayout Renderer::frameDescriptorSetLayout() const noexcept
{
    return frameDescriptorSetLayout_;
}

VkDescriptorSetLayout Renderer::skyboxDescriptorSetLayout() const noexcept
{
    return skyboxDescriptorSetLayout_;
}

VkDescriptorSetLayout Renderer::materialDescriptorSetLayout() const noexcept
{
    return materialDescriptorSetLayout_;
}

VkRenderPass Renderer::sceneRenderPass() const noexcept
{
    return sceneRenderPass_;
}
VkRenderPass Renderer::presentRenderPass() const noexcept
{
    return presentRenderPass_;
}
VkSampleCountFlagBits Renderer::sceneSamples() const noexcept
{
    return sceneSamples_;
}

// 执行一次性的GPU操作，比如复制Buffer、复制纹理、生成MIPMAP、IBL预计算
void Renderer::immediateSubmit(std::function<void(VkCommandBuffer)> &&function)
{
    if (!initialized_ ||
        context_ == nullptr ||
        uploadContext_.commandPool == VK_NULL_HANDLE ||
        uploadContext_.commandBuffer == VK_NULL_HANDLE ||
        uploadContext_.fence == VK_NULL_HANDLE)
    {
        throw std::logic_error("Renderer upload context is not initialized");
    }

    // 确认上一次使用的UploadContext已经完成
    VK_CHECK(vkWaitForFences(context_->device(), 1, &uploadContext_.fence, VK_TRUE, UINT64_MAX));

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

    // Fence必须回到 unsignaled 才能交给下一次 submit
    VK_CHECK(vkResetFences(context_->device(), 1, &uploadContext_.fence));


    VK_CHECK(vkQueueSubmit(context_->graphicsQueue(), 1, &submitInfo, uploadContext_.fence));

    // 提交后立即等待
    VK_CHECK(vkWaitForFences(context_->device(), 1, &uploadContext_.fence, VK_TRUE, UINT64_MAX));
}

std::vector<GpuBuffer> Renderer::uploadBuffers(const std::vector<BufferUploadRequest> &requests)
{

    // 1. 检查 renderer/context 初始化
    if (!initialized_ || context_ == nullptr)
    {
        throw std::logic_error("cannot upload buffers before Renderer initialization");
    }

    if (requests.empty())
    {
        throw std::invalid_argument("buffer upload requests must not be empty");
    }

    // 2. 检查每个请求的 data/size/destinationUsage
    for (BufferUploadRequest request : requests)
    {
        if (request.data == nullptr)
        {
            throw std::invalid_argument("buffer upload data must not be null");
        }
        if (request.size == 0)
        {
            throw std::invalid_argument("buffer upload size must be greater than zero");
        }
        if (request.destinationUsage == 0)
        {
            throw std::invalid_argument("buffer destination usage must not be empty");
        }
    }

    // 3. 创建stagingBuffers vector
    std::vector<GpuBuffer> stagingBuffers;
    stagingBuffers.reserve(requests.size());

    // 4. 创建destinationBuffers vector
    std::vector<GpuBuffer> destinationBuffers;
    destinationBuffers.reserve(requests.size());

    // 5. 将每个 request.data memcpy 到对应 staging
    for (const BufferUploadRequest& request : requests)
    {
        BufferDesc stagingDesc{};
        stagingDesc.size = request.size;
        stagingDesc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        // GPU 内存可以映射到 CPU 地址空间，并且 CPU 写入后不需要手动 flush (不需要 vmaFlushAllocation，gpu 就能看到数据)
        stagingDesc.requiredMemoryProperties =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        stagingDesc.debugName = "synchronous upload staging buffer";

        GpuBuffer stagingBuffer = context_->createBuffer(stagingDesc);

        void* mappedData = nullptr;
        VK_CHECK(stagingBuffer.map(&mappedData));
        std::memcpy(mappedData, request.data, static_cast<size_t>(request.size));
        stagingBuffer.unmap();
        stagingBuffers.push_back(std::move(stagingBuffer));
    }

    // 创建 gpu 本地的目标 buffers
    for (const BufferUploadRequest& request : requests)
    {
        BufferDesc destinationDesc{};
        destinationDesc.size = request.size;
        destinationDesc.usage =
            request.destinationUsage |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        destinationDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        destinationDesc.debugName = request.debugName;

        destinationBuffers.push_back(context_->createBuffer(destinationDesc));
    }

    // 6. 一次 immediateSubmit
    immediateSubmit([&](VkCommandBuffer commandBuffer){
        // 7. callback 中循环调用 upload::recoredBufferCopy()
        for (std::size_t index = 0; index < requests.size(); ++index)
        {
            upload::recordBufferCopy(
                commandBuffer,
                stagingBuffers[index].get(),
                destinationBuffers[index].get(),
                requests[index].size
            );
        }
    });

    // 8. 返回 destinationBuffers
    return destinationBuffers;
}

BeginFrameResult Renderer::beginFrame()
{
    // framInProgress 为 true 代表beginFrame已经成功开始了一帧，但还没有调用endFrame来完成提交和呈现。
    if (!initialized_ || context_ == nullptr || swapchain_ == nullptr || hasActiveFrame_)
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
    hasActiveFrame_ = true;
    hasRecordedFrame_ = false;
    FrameToken token{};
    token.frameIndex = currentFrame_;
    token.imageIndex = imageIndex;
    token.commandBuffer = frame.commandBuffer;

    return {FrameStatus::Ready, token};
}

void Renderer::recordFrame(const FrameToken &token, const RenderFrameData &data)
{

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

    FrameContext &frame = frames_[currentFrame_];

    if (token.commandBuffer != frame.commandBuffer)
    {
        throw std::logic_error("FrameToken contains invalid command buffer");
    }

    if (token.imageIndex >= swapchain_->imageCount())
    {
        throw std::out_of_range("FrameToken contains invalid swapchain image index");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VK_CHECK(vkBeginCommandBuffer(token.commandBuffer, &beginInfo));

    recordShadowPass(token, data);

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{
        data.clearColor.r,
        data.clearColor.g,
        data.clearColor.b,
        data.clearColor.a,
    }};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = sceneRenderPass_;
    renderPassInfo.framebuffer = hdrTargets_.at(token.frameIndex).framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchain_->extent();
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(token.commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

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

    // -- 绘制 skybox --
    if (!data.bypassToneMapping)
    {
        vkCmdBindPipeline(token.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline_);

        vkCmdBindDescriptorSets(
            token.commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            skyboxPipelineLayout_,
            0,
            1,
            &data.skyboxDescriptorSet,
            0,
            nullptr);
        vkCmdDraw(
            token.commandBuffer,
            36,
            1,
            0,
            0);
    }
    // ----

    vkCmdBindPipeline(token.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);

    vkCmdBindDescriptorSets(
        token.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        scenePipelineLayout_,
        0,
        1,
        &data.frameDescriptorSet,
        0,
        nullptr);

    if (data.objects != nullptr)
    {
        const VkDeviceSize offset = 0;
        for (const RenderObjectView &object : *data.objects)
        {
            if (object.indexCount == 0 || object.vertexBuffer == VK_NULL_HANDLE || object.indexBuffer == VK_NULL_HANDLE || object.materialDescriptorSet == VK_NULL_HANDLE)
            {
                continue;
            }
            vkCmdBindDescriptorSets(
                token.commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                scenePipelineLayout_,
                1, // 绑定 set 1
                1,
                &object.materialDescriptorSet,
                0,
                nullptr
            );
            vkCmdPushConstants(
                token.commandBuffer,
                scenePipelineLayout_,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(DrawPushConstants),
                &object.pushConstants);
            vkCmdBindVertexBuffers(
                token.commandBuffer,
                0,
                1,
                &object.vertexBuffer,
                &offset);

            vkCmdBindIndexBuffer(
                token.commandBuffer,
                object.indexBuffer,
                0,
                VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(
                token.commandBuffer,
                object.indexCount,
                1,
                0,
                0,
                0);
        }
    }

    // -- 结束 HDR scene pass --
    // hdrColor 的最终布局及输出依赖由 sceneRenderPass_ 处理
    vkCmdEndRenderPass(token.commandBuffer);

    VkClearValue presentClear{};
    presentClear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    presentInfo.renderPass = presentRenderPass_;
    presentInfo.framebuffer = swapchain_->framebuffer(token.imageIndex);
    presentInfo.renderArea.offset = {0, 0};
    presentInfo.renderArea.extent = swapchain_->extent();
    presentInfo.clearValueCount = 1;
    presentInfo.pClearValues = &presentClear;

    vkCmdBeginRenderPass(token.commandBuffer, &presentInfo, VK_SUBPASS_CONTENTS_INLINE);
    // 使用前面已设置好的全窗口 viewport/scissor
    vkCmdSetViewport(token.commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(token.commandBuffer, 0, 1, &scissor);
    vkCmdBindPipeline(token.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemapPipeline_);
    // 绑定 descriptor set
    const VkDescriptorSet postSet = postDescriptorSets_.at(token.frameIndex);
    vkCmdBindDescriptorSets(
        token.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        postPipelineLayout_,
        0,
        1,
        &postSet,
        0,
        nullptr
    );

    PostPushConstants post{};
    post.modes.y = data.bypassToneMapping ? 1 : 0;
    vkCmdPushConstants(token.commandBuffer, postPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, static_cast<uint32_t>(sizeof(PostPushConstants)), &post);
    vkCmdDraw(token.commandBuffer, 3, 1, 0, 0);
    // ----

    if (data.showShadowDepth)
    {
        recordShadowPreview(token, data);
    }

    if (data.imguiDrawData != nullptr)
    {
        ImGui_ImplVulkan_RenderDrawData(data.imguiDrawData, token.commandBuffer);
    }

    vkCmdEndRenderPass(token.commandBuffer);

    VK_CHECK(vkEndCommandBuffer(token.commandBuffer));

    hasRecordedFrame_ = true;
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

    // 5. 设置 hasActiveFrame_ = false。
    hasActiveFrame_ = false;
    hasRecordedFrame_ = false;

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
