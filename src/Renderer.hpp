#pragma once

#include "AppConfig.hpp"
#include "RenderTypes.hpp"
#include "VulkanResources.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

class VulkanContext;
class Swapchain;

class Renderer
{
private:
    struct FrameContext
    {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence renderFence = VK_NULL_HANDLE;

        std::vector<AllocatedBuffer> retiredBuffers;
    };
    
    struct UploadContext
    {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
    };

    VulkanContext* context_ = nullptr;
    Swapchain* swapchain_ = nullptr;

    std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> frames_{};

    UploadContext uploadContext_{};

    uint32_t currentFrame_ = 0;
    bool frameInProgress_ = false;
    bool initialized_ = false;

public:
    Renderer() noexcept = default;
    ~Renderer() noexcept;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    void initialize(VulkanContext& context, Swapchain& swapchain);
    void shutdown() noexcept;

    // 让 FrameContext 保持为 Renderer 的私有实现，同时给 TriangleApplication 提供完成一帧所需的最少信息
    BeginFrameResult beginFrame();
    FrameStatus endFrame(const FrameToken& token);

    void waitForAllFrames();
    void retireBuffer(AllocatedBuffer&& buffer);

    uint32_t currentFrameIndex() const noexcept;

    bool frameInProgress() const noexcept;



};