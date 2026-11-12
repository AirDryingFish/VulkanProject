#pragma once

#include "vulkan/resources/VulkanResources.hpp"

#include <functional>
#include <vector>

class VulkanContext;

// data 只在同步上传期间借用；返回的目标 buffer 由调用方持有。
struct BufferUploadRequest
{
    const void* data = nullptr;
    VkDeviceSize size = 0;
    VkBufferUsageFlags destinationUsage = 0;
    const char* debugName = nullptr;
};

// 复用独立命令池和 fence 的同步上传服务；不依赖 Renderer 的帧槽或渲染路径。
// VulkanContext 必须活得更久；调用方串行使用，并在关闭设备前释放此服务。
class UploadContext final
{
public:
    UploadContext() noexcept = default;
    ~UploadContext() noexcept;

    UploadContext(const UploadContext&) = delete;
    UploadContext& operator=(const UploadContext&) = delete;
    UploadContext(UploadContext&&) = delete;
    UploadContext& operator=(UploadContext&&) = delete;

    // 仅在空状态初始化；创建失败时 shutdown/destructor 负责释放部分资源。
    void initialize(VulkanContext& context);
    // 调用方须已处理提交失败后的 GPU 状态；此处释放资源，不额外等待设备。
    void shutdown() noexcept;

    // 成功返回时命令已执行完毕，因此回调使用的 staging 可以立即释放。
    void immediateSubmit(std::function<void(VkCommandBuffer)>&& function);
    // 校验请求并批量复制；每批只提交和等待一次，所有目标 buffer 交给调用方。
    std::vector<GpuBuffer> uploadBuffers(const std::vector<BufferUploadRequest>& requests);

private:
    VulkanContext* context_ = nullptr;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
};
