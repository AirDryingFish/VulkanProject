#pragma once

#include "vulkan/VulkanHeaders.hpp"

#include <cstddef>
#include <vector>

class VulkanContext;
struct GLFWwindow;

enum class SwapchainBuildStatus
{
    Ready,
    Deferred,
    WindowClosed,
};

// 拥有交换链及其 view、呈现信号量和 legacy framebuffer；借用窗口与上下文。
class Swapchain final
{
private:
    VulkanContext* context_ = nullptr;
    GLFWwindow* window_ = nullptr;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;

    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR colorSpace_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D extent_{};

    uint32_t minImageCount_ = 0;

    std::vector<VkFramebuffer> framebuffers_;

    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& presentMode) const;
    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    void createImageViews();
    void createPresentSemaphores();

public:
    Swapchain() noexcept = default;
    ~Swapchain() noexcept;

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = delete;
    Swapchain& operator=(Swapchain&&) = delete;

    // 核心生命周期：context 与 window 必须比交换链活得更久。
    void initializeCore(VulkanContext& context, GLFWwindow* window);
    // 窗口可绘制时等待设备空闲并重建；最小化/关闭时保留现有资源并返回状态。
    SwapchainBuildStatus rebuildCore();
    // 调用方先保证 GPU 与呈现操作不再使用这些资源，可重复调用。
    void shutdown() noexcept;
    SwapchainBuildStatus buildStatus() const noexcept;

    // Legacy 呈现附件：framebuffer 必须先于所依赖的 render pass 释放。
    void createFramebuffers(VkRenderPass renderPass);
    void destroyFramebuffers() noexcept;
    VkFramebuffer framebuffer(std::size_t index) const;

    // 借用当前交换链资源，句柄在下一次重建或 shutdown 时失效。
    VkSwapchainKHR get() const noexcept;
    VkFormat format() const noexcept;
    VkExtent2D extent() const noexcept;
    std::size_t imageCount() const noexcept;
    VkImage image(std::size_t index) const;
    VkImageView imageView(std::size_t index) const;
    // 按交换链图像而非飞行帧槽索引，供对应 present 等待。
    VkSemaphore renderFinishedSemaphore(std::size_t index) const;
    VkColorSpaceKHR colorSpace() const noexcept;
    uint32_t minImageCount() const noexcept;
};
