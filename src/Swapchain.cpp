#include "Swapchain.hpp"

#include "VulkanCheck.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>

void Swapchain::initializeCore(VulkanContext &context, GLFWwindow *window)
{
    if (window == nullptr)
    {
        throw std::invalid_argument("Swapchain requires a window");
    }

    if (context_ != nullptr || swapchain_ != VK_NULL_HANDLE)
    {
        throw std::logic_error("Swapchain is already initialized");
    }
    context_ = &context;
    window_ = window;

    try{
        SwapChainSupportDetails swapChainSupport = context.querySwapchainSupport();
        VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR presentMode = choosePresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = chooseExtent(swapChainSupport.capabilities);

        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;

        if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount)
        {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = context.surface();
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        QueueFamilyIndices indices = context.queueFamilies();
        uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

        if (indices.graphicsFamily != indices.presentFamily)
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;
            createInfo.pQueueFamilyIndices = nullptr;
        }

        createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        VK_CHECK(vkCreateSwapchainKHR(context_->device(), &createInfo, nullptr, &swapchain_));

        VK_CHECK(vkGetSwapchainImagesKHR(context_->device(), swapchain_, &imageCount, nullptr));
        images_.resize(imageCount);
        VK_CHECK(vkGetSwapchainImagesKHR(context_->device(), swapchain_, &imageCount, images_.data()));

        format_ = surfaceFormat.format;
        extent_ = extent;
        presentMode_ = presentMode;
        extent_ = extent;

        createImageViews();
        createPresentSemaphores();
    }
    catch(...)
    {
        shutdown();
        throw;
    }

}

void Swapchain::shutdown() noexcept
{
    if (context_ != nullptr)
    {
        const VkDevice device = context_->device();
        if (device != VK_NULL_HANDLE)
        {
            for (VkImageView imageView : imageViews_)
            {
                if (imageView != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(device, imageView, nullptr);
                }
            }

            for (VkSemaphore semaphore : renderFinishedSemaphores_)
            {
                if (semaphore != VK_NULL_HANDLE)
                {
                    vkDestroySemaphore(device, semaphore, nullptr);
                }
            }

            if (swapchain_ != VK_NULL_HANDLE)
            {
                vkDestroySwapchainKHR(device, swapchain_, nullptr);
            }
        }
    }
    imageViews_.clear();
    renderFinishedSemaphores_.clear();
    images_.clear();
    swapchain_ = VK_NULL_HANDLE;
    format_ = VK_FORMAT_UNDEFINED;
    colorSpace_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    extent_ = {};

    window_ = nullptr;
    context_ = nullptr;
}

VkSwapchainKHR Swapchain::get() const noexcept
{
    return swapchain_;
}

VkFormat Swapchain::format() const noexcept
{
    return format_;
}

VkExtent2D Swapchain::extent() const noexcept
{
    return extent_;
}

std::size_t Swapchain::imageCount() const noexcept
{
    return images_.size();
}

VkImage Swapchain::image(std::size_t index) const
{
    return images_.at(index);
}

VkImageView Swapchain::imageView(std::size_t index) const
{
    return imageViews_.at(index);
}

VkSemaphore Swapchain::renderFinishedSemaphore(std::size_t index) const
{
    return renderFinishedSemaphores_.at(index);
}

VkSurfaceFormatKHR Swapchain::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &formats) const
{
    for (const auto &availableForat : formats)
    {
        if (availableForat.format == VK_FORMAT_B8G8R8A8_SRGB && availableForat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return availableForat;
        }
    }

    return formats.at(0);
}

VkPresentModeKHR Swapchain::choosePresentMode(const std::vector<VkPresentModeKHR> &presentMode) const
{
    for (const auto &availablePresentMode : presentMode)
    {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Swapchain::chooseExtent(const VkSurfaceCapabilitiesKHR &capabilities) const
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }

    int width, height;
    glfwGetFramebufferSize(window_, &width, &height);

    VkExtent2D actualExtent = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)};
    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actualExtent;
}

void Swapchain::createImageViews()
{
    assert(context_ != nullptr);
    assert(imageViews_.empty());

    // imageViews_.resize(images_.size());
    imageViews_.assign(images_.size(), VK_NULL_HANDLE);

    for (size_t i = 0; i < images_.size(); i++)
    {
        imageViews_[i] = context_->createImageView(images_[i], format_, 1);
    }
}

void Swapchain::createPresentSemaphores()
{
    renderFinishedSemaphores_.resize(images_.size());

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;


    for (VkSemaphore &renderFinished : renderFinishedSemaphores_)
    {
        VK_CHECK(vkCreateSemaphore(context_->device(), &semaphoreInfo, nullptr, &renderFinished));
        // swapChainDeletionQueue.pushFunction([this, renderFinished]() {
        //     vkDestroySemaphore(context.device(), renderFinished, nullptr);
        // });
    }
}

Swapchain::~Swapchain() noexcept
{
    shutdown();
}
