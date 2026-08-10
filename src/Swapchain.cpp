#include "Swapchain.hpp"

void Swapchain::initializeCore(VulkanContext &context, GLFWwindow *window)
{
}

void Swapchain::shutdown() noexcept
{
}

VkSwapchainKHR Swapchain::get() const noexcept
{
    return VkSwapchainKHR();
}

VkFormat Swapchain::format() const noexcept
{
    return VkFormat();
}

VkExtent2D Swapchain::extenet() const noexcept
{
    return VkExtent2D();
}

std::size_t Swapchain::imageCount() const noexcept
{
    return std::size_t();
}

VkImage Swapchain::image(std::size_t index) const
{
    return VkImage();
}

VkImageView Swapchain::imageView(std::size_t index) const
{
    return VkImageView();
}

VkSemaphore Swapchain::renderFinishedSemaphore(std::size_t index) const
{
    return VkSemaphore();
}

VkSurfaceFormatKHR Swapchain::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &formats) const
{
    return VkSurfaceFormatKHR();
}

VkPresentModeKHR Swapchain::choosePresentMode(const std::vector<VkPresentModeKHR> &presentMode) const
{
    return VkPresentModeKHR();
}

VkExtent2D Swapchain::chooseExtent(const VkSurfaceCapabilitiesKHR &capibilities) const
{
    return VkExtent2D();
}

void Swapchain::createImageViews()
{
}

void Swapchain::createPresentSemaphores()
{
}
