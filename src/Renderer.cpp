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

        createRenderPass();
        createDescriptorSetLayout();
        createGraphicsPipeline();
        createSkyboxPipeline();

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

void Renderer::createRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchain_->format(); // swap chain图像的格式
    // colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT; // 不使用多重采样
    colorAttachment.samples = context_->msaaSamples();

    // LoadOp和storeOp决定如何处理attachment中的数据
    // LoadOp:
    // * VK_ATTACHMENT_LOAD_OP_LOAD: 保留attachment现有内容
    // * VK_ATTACHMENT_LOAD_OP_CLEAR: 在渲染开始时把attachment清空为一个常量值（clear color）
    // * VK_ATTACHMENT_LOAD_OP_DONT_CARE: 现有内容未定义，不关心attachment开始时是什么
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // StoreOp:
    // * VK_ATTACHMENT_STORE_OP_STORE: 渲染后的内容存储在内存中，稍后可以读取
    // * VK_ATTACHMENT_STORE_OP_DONT_CARE: 渲染后的内容未定义，不关心attachment结束时是什么
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;   // 不使用模板缓冲区，所以不关心它的内容
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // 不使用模板缓冲区，所以不关心它的内容

    // vulkan中的texture和framebuffer由具体特定像素格式的VkImage对象表示，内存中像素的布局可以根据对图像的操作而改变
    // * VK_IMAGE_LAYOUT_UNDEFINED: render pass 开始时不关心图像开始时的布局，渲染开始时图像中的数据会被丢弃
    // * VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: 渲染过程中图像的最佳布局
    // * VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: Render pass结束时，自动把图像转换成适合呈现到屏幕的布局。这样Render pass结束后图像就可以直接被显示器使用了。
    // initialLayout和finalLayout的转换由vulkan自动完成，我们只需要告诉vulkan我们不关心初始布局是什么，渲染结束后要把它转换成适合显示的布局就行了。
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // 这张 image 即将被呈现到屏幕
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // 这张 image 还要继续被当作颜色附件使用

    // 一个 render pass 里可以有多个 subpass，每个 subpass 是一个渲染步骤，后面的 subpass 可以读取前面 subpass 的输出结果。
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;                                    // 指定我们要使用第一个attachment
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // subpass使用attachment时的布局

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = context_->findDepthFormat();
    // depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.samples = context_->msaaSamples();
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription colorAttachmentResolve{};
    colorAttachmentResolve.format = swapchain_->format();
    colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentResolveRef{};
    colorAttachmentResolveRef.attachment = 2;
    colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    // pColorAttachments是关键部分。传入的是一个数组，数组的索引直接对应fragment shader里的layout(location = 0) out vec4 outColor;中的location。
    // 比如如果传入两个attachment，索引分别是0和1，那么fragment shader里就可以有两个输出变量，分别是layout(location = 0) out vec4 outColor0;和layout(location = 1) out vec4 outColor1;。
    // 如果fragment shader里只有一个输出变量，直接写layout(location = 0) out vec4 outColor;就行了，vulkan会自动把它绑定到索引为0的attachment上。
    // 这就是Multiple Render Targets (MRT)技术，可以在一个subpass里同时输出到多张图像上，性能更好。
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;
    subpass.pResolveAttachments = &colorAttachmentResolveRef;

    std::array<VkAttachmentDescription, 3> attachments = {colorAttachment, depthAttachment, colorAttachmentResolve};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    // Render pass开始时会做布局转换(比如把图像从“未定义”转成“可以写颜色”的布局)
    // vulkan默认在管线最开头做这个转换，但是那时候图像可能还没从swap chain中那到。往一个没准备好的图像上做布局转换，会出问题
    // 解决思路：告诉vulkan，布局转换等到真正要写颜色的阶段再做
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL; // 外部，renderpass之间的操作
    dependency.dstSubpass = 0;                   // 我们的子通道

    /*
        顶点输入 (VERTEX_INPUT)
            ↓
        顶点着色 (VERTEX_SHADER)
            ↓
        几何着色 (GEOMETRY_SHADER)
            ↓
        光栅化早期 (EARLY_FRAGMENT_TESTS)  ← 早期深度测试
            ↓
        片段着色 (FRAGMENT_SHADER)
            ↓
        光栅化晚期 (LATE_FRAGMENT_TESTS)
            ↓
        颜色附件输出 (COLOR_ATTACHMENT_OUTPUT)  ← fragment 写到 framebuffer
            ↓
        传输 (TRANSFER)  ← 单独的阶段, vkCmdCopyImage 等在这运行
            ↓
        计算着色 (COMPUTE_SHADER)
    */

    // src 在 <某stage做的某种access> 完成， dst端的 <某stage做的某种access> 才能开始
    // => "上一次的 color/depth 写入(包括 LATE 测试阶段)必须真正完成、cache 都 flush 干净,这一次的 color/depth 写入(从 EARLY 测试阶段开始)才能动手。"
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VK_CHECK(vkCreateRenderPass(context_->device(), &renderPassInfo, nullptr, &renderPass_));
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

            if (pipelineLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
            }

            if (descriptorSetLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr);
            }

            if (renderPass_ != VK_NULL_HANDLE)
            {
                vkDestroyRenderPass(device, renderPass_, nullptr);
            }
        }
    }

    uploadContext_.fence = VK_NULL_HANDLE;
    uploadContext_.commandBuffer = VK_NULL_HANDLE;
    uploadContext_.commandPool = VK_NULL_HANDLE;

    skyboxPipeline_ = VK_NULL_HANDLE;
    graphicsPipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    descriptorSetLayout_ = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE;

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

VkRenderPass Renderer::renderPass() const noexcept
{
    return renderPass_;
}

VkDescriptorSetLayout Renderer::descriptorSetLayout() const noexcept
{
    return descriptorSetLayout_;
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
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = swapchain_->framebuffer(token.imageIndex);
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

    // 绘制 skybox
    vkCmdBindPipeline(token.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline_);

    vkCmdBindDescriptorSets(
        token.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout_,
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

    vkCmdBindPipeline(token.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);

    vkCmdBindDescriptorSets(
        token.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout_,
        0,
        1,
        &data.sceneDescriptorSet,
        0,
        nullptr);

    if (data.objects != nullptr)
    {
        const VkDeviceSize offset = 0;
        for (const RenderObjectView &object : *data.objects)
        {
            if (object.indexCount == 0 || object.vertexBuffer == VK_NULL_HANDLE || object.indexBuffer == VK_NULL_HANDLE)
            {
                continue;
            }
            vkCmdPushConstants(
                token.commandBuffer,
                pipelineLayout_,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(glm::mat4),
                &object.model);
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
