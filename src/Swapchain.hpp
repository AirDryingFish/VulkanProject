#pragma once

#include "VulkanContext.hpp"
#include "VulkanResources.hpp"

#include <cstddef>
#include <vector>

enum class SwapchainBuildStatus
{
    Ready,
    Deferred,
    WindowClosed,
};

class Swapchain final
{
private:
    VulkanContext *context_ = nullptr;
    GLFWwindow *window_ = nullptr;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;

    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR colorSpace_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D extent_{};

    AllocatedImage depthImage_;
    AllocatedImage colorImage_;

    std::vector<VkFramebuffer> framebuffers_;

    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &formats) const;
    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR> &presentMode) const;
    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR &capabilities) const;
    void createImageViews();
    void createPresentSemaphores();
    void createAttachments();

public:
    Swapchain() noexcept = default;
    ~Swapchain() noexcept;

    Swapchain(const Swapchain &) = delete;
    Swapchain &operator=(const Swapchain &) = delete;
    Swapchain(Swapchain &&) = delete;
    Swapchain &operator=(Swapchain &&) = delete;

    // 暴露的接口
    void initializeCore(VulkanContext &context, GLFWwindow *window);

    void shutdown() noexcept;

    VkSwapchainKHR get() const noexcept;
    VkFormat format() const noexcept;
    VkExtent2D extent() const noexcept;

    std::size_t imageCount() const noexcept;

    VkImage image(std::size_t index) const;
    VkImageView imageView(std::size_t index) const;
    VkSemaphore renderFinishedSemaphore(std::size_t index) const;

    void createFramebuffers(VkRenderPass renderPass);
    void destroyFramebuffersAndAttachments() noexcept;
    VkFramebuffer framebuffer(std::size_t index) const;

    SwapchainBuildStatus buildStatus() const noexcept;

    SwapchainBuildStatus rebuildCore();
};
