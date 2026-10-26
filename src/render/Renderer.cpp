#include "render/Renderer.hpp"
#include "vulkan/VulkanContext.hpp"
#include "vulkan/Swapchain.hpp"
#include "vulkan/VulkanCheck.hpp"
#include "vulkan/UploadCommands.hpp"
#include "diagnostics/CpuTiming.hpp"

#include <cassert>
#include <stdexcept>
#include <utility>
#include <imgui_impl_vulkan.h>
#include <cstring>
#include <iostream>

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

            // 首次复用槽位时还没有 GPU 提交，因此 fence 初始为 signaled，避免首帧永久等待。
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            VK_CHECK(vkCreateFence(context.device(), &fenceInfo, nullptr, &frame.renderFence));

            // const VkFence renderFence = frame.renderFence;
            // mainDeletionQueue.pushFunction([this, renderFence]() {
            //     vkDestroyFence(context.device(), renderFence, nullptr);
            // });
        }

        // -- gpu 计时 --
        createTimestampQueryPools();
        latestGpuTiming_ = {};
        latestGpuTiming_.supported = gpuTimingSupported_;
        for (GpuPassTiming& timing : latestGpuTiming_.passes)
        {
            timing.state = gpuTimingSupported_ ? GpuTimingState::Pending : GpuTimingState::Unavailable;
        }

        // -- cpu timing --
        currentCpuTiming_ = {};
        latestCpuTiming_ = {};

        createUploadContext();
        // -- 初始化阴影渲染管线 --
        createShadowTargets();
        if (!useDynamicRenderingForShadow())
        {
            createShadowRenderPass();
            createShadowFramebuffers();
        }
        createShadowPipeline();
        // -- hdr 资源--
        createHdrTargets();
        if (!useDynamicRenderingForScene())
        {
            createSceneRenderPass();
            createHdrFramebuffers();
        }

        createPostDescriptors();
        if (!useDynamicRenderingForPresent())
        {
            createPresentRenderPass();
        }
        createPostPipelineLayout();

        // -- 创建 pipeline --
        createGraphicsPipeline();
        createSkyboxPipeline();
        createShadowPreviewPipeline();
        createTonemapPipeline();

        std::cout << "Present path: " << (useDynamicRenderingForPresent() ? "Dynamic Rendering" : "RenderPass") << "\n";
        std::cout << "Scene path: "
                  << (useDynamicRenderingForScene()
                          ? "Dynamic Rendering"
                          : "RenderPass")
                  << ", samples="
                  << static_cast<unsigned int>(sceneSamples_)
                  << "\n";
        std::cout << "Shadow path: "
                  << (useDynamicRenderingForShadow()
                          ? "Dynamic Rendering"
                          : "RenderPass")
                  << "\n";
        initialized_ = true;
    }
    catch (...)
    {
        shutdown();
        throw;
    }
}

// 能力查询和资源创建
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

    std::cout << "GPU timing prepared: graphics family=" << graphicsFamily
              << ", valid bits=" << timestampValidBits_
              << ", period=" << timestampPeriodNs_ << " ns/tick"
              << ", pools=" << frames_.size()
              << ", queries per pool=" << gpuTimestampQueryCount
              << "\n";
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

                if(frame.timestampQueryPool != VK_NULL_HANDLE)
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

            if (graphicsDoubleSidedPipeline_ != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, graphicsDoubleSidedPipeline_, nullptr);
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

    gpuSubmissionCounter_ = 0;
    latestGpuTiming_ = {};
    currentCpuTiming_ = {};
    latestCpuTiming_ = {};

    uploadContext_.fence = VK_NULL_HANDLE;
    uploadContext_.commandBuffer = VK_NULL_HANDLE;
    uploadContext_.commandPool = VK_NULL_HANDLE;

    skyboxPipeline_ = VK_NULL_HANDLE;
    graphicsPipeline_ = VK_NULL_HANDLE;
    graphicsDoubleSidedPipeline_ = VK_NULL_HANDLE;
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
    gpuTimingSupported_ = false;
    timestampValidBits_ = 0;
    timestampPeriodNs_ = 0.0;

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

bool Renderer::hasActiveFrame() const noexcept
{
    return hasActiveFrame_;
}

bool Renderer::useDynamicRenderingForPresent() const noexcept
{
    if (context_ == nullptr)
    {
        return false;
    }
    const auto& capabilities = context_->capabilities();

    return capabilities.dynamicRenderingEnabled && capabilities.synchronization2Enabled;
}

bool Renderer::useDynamicRenderingForScene() const noexcept
{
    if (context_ == nullptr)
    {
        return false;
    }

    const auto& capabilities = context_->capabilities();

    return capabilities.dynamicRenderingEnabled && capabilities.synchronization2Enabled;
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

VkRenderPass Renderer::presentRenderPass() const noexcept
{
    return presentRenderPass_;
}
VkSampleCountFlagBits Renderer::sceneSamples() const noexcept
{
    return sceneSamples_;
}

const GpuTimingSnapshot& Renderer::gpuTimingSnapshot() const noexcept
{
    return latestGpuTiming_;
};

const RendererCpuTimingSnapshot& Renderer::cpuTimingSnapshot() const noexcept
{
    return latestCpuTiming_;
};

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

    const bool useSync2 = context_->capabilities().synchronization2Enabled;
    const auto queueSubmit2 = useSync2 ? context_->deviceFunctions().queueSubmit2 : nullptr;
    if (useSync2 && queueSubmit2 == nullptr)
    {
        throw std::logic_error("Immediate submit requires vkQueueSubmit2");
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

    if (useSync2)
    {
        VkCommandBufferSubmitInfo commandInfo{};
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandInfo.commandBuffer = uploadContext_.commandBuffer;
        commandInfo.deviceMask = 1;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandInfo;

        VK_CHECK(vkResetFences(
            context_->device(),
            1,
            &uploadContext_.fence
        ));
        VK_CHECK(queueSubmit2(
            context_->graphicsQueue(),
            1,
            &submitInfo,
            uploadContext_.fence
        ));
    }
    else
    {
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &uploadContext_.commandBuffer;

        // Fence必须回到 unsignaled 才能交给下一次 submit
        VK_CHECK(vkResetFences(context_->device(), 1, &uploadContext_.fence));
        VK_CHECK(vkQueueSubmit(context_->graphicsQueue(), 1, &submitInfo, uploadContext_.fence));
    }

    // 同步提交刻意等到命令执行完成；调用方的临时 staging 不能在 GPU 仍读取时析构。
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

    // 一批复制共用一次提交/等待；成功返回后 staging 可销毁，目标 buffer 移交给调用方。
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
    // 同一 Renderer 只允许一个活动 token；上一帧必须先完成录制与提交。
    if (!initialized_ || context_ == nullptr || swapchain_ == nullptr || hasActiveFrame_)
    {
        return {
            FrameStatus::Skip, {}};
    }

    FrameContext &frame = frames_[currentFrame_];

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

    const VkResult acquireResult = vkAcquireNextImageKHR(
        context_->device(),
        swapchain_->get(),
        UINT64_MAX,
        frame.imageAvailable,
        VK_NULL_HANDLE,
        &imageIndex);

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

void Renderer::recordFrame(const FrameToken &token, const RenderFrameData &data)
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

    FrameContext &frame = frames_[currentFrame_];

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

    // 把录制时的值复制下来，方便之后解释计时结果
    frame.recordedGpuPassMask = 0;
    frame.recordedTimingConfig.sceneId = data.sceneId;
    frame.recordedTimingConfig.extent = swapchain_->extent();
    frame.recordedTimingConfig.sceneSamples = sceneSamples_;
    frame.recordedTimingConfig.shadowEnabled = data.directionalShadowsEnabled;
    frame.recordedTimingConfig.shadowPreview = data.showShadowDepth;
    frame.recordedTimingConfig.uiDrawn = data.imguiDrawData != nullptr;
    frame.recordedTimingConfig.bypassToneMapping = data.bypassToneMapping;
    frame.recordedDraws = {};
    frame.recordedTimingConfig.exposureEv = data.exposureEv;
    frame.recordedTimingConfig.toneMapper = data.toneMapper;
    frame.recordedTimingConfig.debugView = data.debugView;
    frame.recordedTimingConfig.debugDepthNear = data.debugDepthNear;
    frame.recordedTimingConfig.debugDepthFar = data.debugDepthFar;

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
            cmdWriteTimestamp2(
                token.commandBuffer,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                frame.timestampQueryPool,
                query
            );
        }
        else{
            vkCmdWriteTimestamp(
                token.commandBuffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                frame.timestampQueryPool,
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
            cmdWriteTimestamp2(
                token.commandBuffer,
                VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                frame.timestampQueryPool,
                query);
        }
        else
        {
            vkCmdWriteTimestamp(
                token.commandBuffer,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                frame.timestampQueryPool,
                query // 每个 Pass 占两个连续 query：开始 2*i，结束 2*i+1。
            );
        }

        frame.recordedGpuPassMask |= (1u << passIndex);
    };

    // -- 开启 shadow pass --
    beginGpuPass(GpuPass::Shadow);
    recordShadowPass(token, data);
    endGpuPass(GpuPass::Shadow);
    // ----

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{
        data.clearColor.r,
        data.clearColor.g,
        data.clearColor.b,
        data.clearColor.a,
    }};
    clearValues[1].depthStencil = {1.0f, 0};

    beginGpuPass(GpuPass::Scene);
    beginScenePass(token, clearValues);

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

    // -- 绘制 skybox (表面数据视图不绘制) --
    const bool drawSkybox = data.debugView == DebugView::Lit || data.debugView == DebugView::HdrLuminance;
    if (drawSkybox)
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

            const VkPipeline pipeline = object.doubleSided ? graphicsDoubleSidedPipeline_ : graphicsPipeline_;
            vkCmdBindPipeline(token.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

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

            ++frame.recordedDraws.sceneDraws;
            frame.recordedDraws.sceneTriangles += static_cast<std::uint64_t>(object.indexCount / 3);
        }
    }
    // ----

    // -- 结束 HDR scene pass --
    // hdrColor 的最终布局及输出依赖由 sceneRenderPass_ 处理
    endScenePass(token);
    endGpuPass(GpuPass::Scene);

    VkClearValue presentClear{};
    presentClear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    // VkRenderPassBeginInfo presentInfo{};
    // presentInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    // presentInfo.renderPass = presentRenderPass_;
    // presentInfo.framebuffer = swapchain_->framebuffer(token.imageIndex);
    // presentInfo.renderArea.offset = {0, 0};
    // presentInfo.renderArea.extent = swapchain_->extent();
    // presentInfo.clearValueCount = 1;
    // presentInfo.pClearValues = &presentClear;

    // // -- 开启 present 并打点 --
    // beginGpuPass(GpuPass::PostAndUi);
    // vkCmdBeginRenderPass(token.commandBuffer, &presentInfo, VK_SUBPASS_CONTENTS_INLINE);

    beginGpuPass(GpuPass::PostAndUi);
    beginPresentPass(token, presentClear);

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

    // params.x: exposure EV
    // params.y/z: 固定亮度显示范围：log2(Y) ∈ [-8, +8]
    // modes.x: ToneMapper
    // modes.y: bypass exposure and tone mapping
    PostPushConstants post{};
    post.params.x = data.exposureEv;
    post.params.y = -8.0f;
    post.params.z = 8.0f;
    post.modes.x = static_cast<std::uint32_t>(data.toneMapper);
    post.modes.y = data.bypassToneMapping ? 1 : 0;
    post.modes.z = static_cast<std::int32_t>(data.debugView);
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
    // vkCmdEndRenderPass(token.commandBuffer);

    endPresentPass(token);
    endGpuPass(GpuPass::PostAndUi);
    // ----

    VK_CHECK(vkEndCommandBuffer(token.commandBuffer));

    hasRecordedFrame_ = true;
}

// endFrame 负责提交与请求呈现，返回不代表 GPU 或屏幕显示已经完成。
// imageAvailable/renderFinished 协调 GPU 与呈现；renderFence 供 CPU 下次安全复用帧槽。
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
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;         // 之前必须完成的stage
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
    VkResult submitResult = useSync2 ?
        queueSubmit2(
            context_->graphicsQueue(),
            1,
            &submitInfo2,
            frame.renderFence
        )
        :
        vkQueueSubmit(
            context_->graphicsQueue(),
            1,
            &submitInfo,
            frame.renderFence
        );

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

// 构建一份新快照，填入各 pass 的耗时，再替换之前保存的快照
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
        const VkResult result = vkGetQueryPoolResults(
            context_->device(),
            frame.timestampQueryPool,
            i * 2,
            2,
            sizeof(ticks),
            ticks,
            sizeof(std::uint64_t),
            VK_QUERY_RESULT_64_BIT
        );
        if (result == VK_NOT_READY)
        {
            // fence 后仍不可用属于异常样本：保留状态/日志，不用未定义 ticks 计算，也不忙等。
            timing.state = GpuTimingState::NotReady;
            std::cerr << "GPU timestamp not ready: submission="
            << snapshot.submissionId
            << ", frame slot="
            << frameSlot
            << ", pass="
            << i << "\n";

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
