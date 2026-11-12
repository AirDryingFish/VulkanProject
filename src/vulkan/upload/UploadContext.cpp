#include "vulkan/upload/UploadContext.hpp"
#include "vulkan/upload/UploadCommands.hpp"
#include "vulkan/VulkanCheck.hpp"
#include "vulkan/context/VulkanContext.hpp"

#include <cassert>
#include <cstring>
#include <stdexcept>
#include <utility>

UploadContext::~UploadContext() noexcept
{
    shutdown();
}

void UploadContext::initialize(VulkanContext& context)
{
    assert(context_ == nullptr);
    context_ = &context;

    const QueueFamilyIndices& indices = context_->queueFamilies();
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

    VK_CHECK(vkCreateCommandPool(context_->device(), &poolInfo, nullptr, &commandPool_));

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool_;
    allocInfo.commandBufferCount = 1;

    VK_CHECK(vkAllocateCommandBuffers(context_->device(), &allocInfo, &commandBuffer_));

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VK_CHECK(vkCreateFence(context_->device(), &fenceInfo, nullptr, &fence_));
}

void UploadContext::shutdown() noexcept
{
    if (context_ != nullptr)
    {
        const VkDevice device = context_->device();
        if (device != VK_NULL_HANDLE)
        {
            if (fence_ != VK_NULL_HANDLE)
            {
                vkDestroyFence(device, fence_, nullptr);
            }
            if (commandPool_ != VK_NULL_HANDLE)
            {
                vkDestroyCommandPool(device, commandPool_, nullptr);
            }
        }
    }

    fence_ = VK_NULL_HANDLE;
    commandBuffer_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    context_ = nullptr;
}

void UploadContext::immediateSubmit(std::function<void(VkCommandBuffer)>&& function)
{
    if (context_ == nullptr || commandPool_ == VK_NULL_HANDLE || commandBuffer_ == VK_NULL_HANDLE ||
        fence_ == VK_NULL_HANDLE)
    {
        throw std::logic_error("UploadContext is not initialized");
    }

    const bool useSync2 = context_->capabilities().synchronization2Enabled;
    const auto queueSubmit2 = useSync2 ? context_->deviceFunctions().queueSubmit2 : nullptr;
    if (useSync2 && queueSubmit2 == nullptr)
    {
        throw std::logic_error("Immediate submit requires vkQueueSubmit2");
    }

    // 确认上一次使用的UploadContext已经完成
    VK_CHECK(vkWaitForFences(context_->device(), 1, &fence_, VK_TRUE, UINT64_MAX));

    // Fence 已经证明上一轮 command buffer 不再 pending，因此可以安全reset command pool
    VK_CHECK(vkResetCommandPool(context_->device(), commandPool_, 0));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer_, &beginInfo));

    function(commandBuffer_);

    VK_CHECK(vkEndCommandBuffer(commandBuffer_));

    if (useSync2)
    {
        VkCommandBufferSubmitInfo commandInfo{};
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandInfo.commandBuffer = commandBuffer_;
        commandInfo.deviceMask = 1;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandInfo;

        VK_CHECK(vkResetFences(context_->device(), 1, &fence_));
        VK_CHECK(queueSubmit2(context_->graphicsQueue(), 1, &submitInfo, fence_));
    }
    else
    {
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer_;

        // Fence必须回到 unsignaled 才能交给下一次 submit
        VK_CHECK(vkResetFences(context_->device(), 1, &fence_));
        VK_CHECK(vkQueueSubmit(context_->graphicsQueue(), 1, &submitInfo, fence_));
    }

    // 同步提交刻意等到命令执行完成；调用方的临时 staging 不能在 GPU 仍读取时析构。
    VK_CHECK(vkWaitForFences(context_->device(), 1, &fence_, VK_TRUE, UINT64_MAX));
}

std::vector<GpuBuffer> UploadContext::uploadBuffers(const std::vector<BufferUploadRequest>& requests)
{

    // 1. 检查 renderer/context 初始化
    if (context_ == nullptr)
    {
        throw std::logic_error("cannot upload buffers before UploadContext initialization");
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
        // staging 必须可映射；写入后 flush，使非 HOST_COHERENT 内存对 GPU 可见。
        stagingDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        stagingDesc.debugName = "synchronous upload staging buffer";

        GpuBuffer stagingBuffer = context_->createBuffer(stagingDesc);

        void* mappedData = nullptr;
        VK_CHECK(stagingBuffer.map(&mappedData));
        std::memcpy(mappedData, request.data, static_cast<size_t>(request.size));
        VK_CHECK(stagingBuffer.flush(0, request.size));

        stagingBuffer.unmap();
        stagingBuffers.push_back(std::move(stagingBuffer));
    }

    // 创建 gpu 本地的目标 buffers
    for (const BufferUploadRequest& request : requests)
    {
        BufferDesc destinationDesc{};
        destinationDesc.size = request.size;
        destinationDesc.usage = request.destinationUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        destinationDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        destinationDesc.debugName = request.debugName;

        destinationBuffers.push_back(context_->createBuffer(destinationDesc));
    }

    // 一批复制共用一次提交/等待；成功返回后 staging 可销毁，目标 buffer 移交给调用方。
    immediateSubmit(
        [&](VkCommandBuffer commandBuffer)
        {
            // 7. callback 中循环调用 upload::recoredBufferCopy()
            for (std::size_t index = 0; index < requests.size(); ++index)
            {
                upload::recordBufferCopy(commandBuffer, stagingBuffers[index].get(), destinationBuffers[index].get(),
                                         requests[index].size);
            }
        });

    // 8. 返回 destinationBuffers
    return destinationBuffers;
}
