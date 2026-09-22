#include "Swapchain.hpp"

#include "VulkanCheck.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <array>

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

    try
    {
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
        colorSpace_ = surfaceFormat.colorSpace;
        presentMode_ = presentMode;
        minImageCount_ = swapChainSupport.capabilities.minImageCount;

        createImageViews();
        createPresentSemaphores();
    }
    catch (...)
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
            destroyFramebuffers();

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
    minImageCount_ = 0;
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

VkColorSpaceKHR Swapchain::colorSpace() const noexcept
{
    return colorSpace_;
}
uint32_t Swapchain::minImageCount() const noexcept
{
    return minImageCount_;
}

void Swapchain::createFramebuffers(VkRenderPass renderPass)
{
    if (context_ == nullptr || swapchain_ == VK_NULL_HANDLE)
    {
        throw std::logic_error("Swapchain is not initialized");
    }

    if (renderPass == VK_NULL_HANDLE)
    {
        throw std::invalid_argument("createFramebuffers requires a valid render pass");
    }

    try
    {
        // 1 个swap chain图像对应1个framebuffer，所以framebuffer的数量和swap chain图像的数量一样多
        framebuffers_.assign(images_.size(), VK_NULL_HANDLE);
        // 遍历 ImageView，给每个ImageView创建一个对应的framebuffer
        for (size_t i = 0; i < imageViews_.size(); i++)
        {
            // 现在每个 swapchain 只有一张 imageView，没有depth 和 resolve
            const VkImageView attachments = imageViews_.at(i);
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass; // framebuffer要兼容哪个render pass
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &attachments;       // framebuffer要绑定哪些图像作为附件
            framebufferInfo.width = extent_.width;             // framebuffer的宽高必须和render pass里定义的视口大小一致
            framebufferInfo.height = extent_.height;           // framebuffer的宽高必须和render pass里定义的视口大小一致
            framebufferInfo.layers = 1;                        // 只有一层

            VK_CHECK(vkCreateFramebuffer(context_->device(), &framebufferInfo, nullptr, &framebuffers_[i]));
        }
    }
    catch (...)
    {
        destroyFramebuffers();
        throw;
    }
}

void Swapchain::destroyFramebuffers() noexcept
{
    if (context_ != nullptr)
    {
        const VkDevice device = context_->device();
        if (device != VK_NULL_HANDLE)
        {
            for (VkFramebuffer framebuffer : framebuffers_)
            {
                if (framebuffer != VK_NULL_HANDLE)
                {
                    vkDestroyFramebuffer(device, framebuffer, nullptr);
                }
            }
        }
    }
    framebuffers_.clear();
}

VkFramebuffer Swapchain::framebuffer(std::size_t index) const
{
    return framebuffers_.at(index);
}

SwapchainBuildStatus Swapchain::buildStatus() const noexcept
{
    if (window_ == nullptr || glfwWindowShouldClose(window_))
    {
        return SwapchainBuildStatus::WindowClosed;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);

    if (width == 0 || height == 0)
    {
        return SwapchainBuildStatus::Deferred;
    }

    return SwapchainBuildStatus::Ready;
}

SwapchainBuildStatus Swapchain::rebuildCore()
{
    const SwapchainBuildStatus status = buildStatus();

    if (status != SwapchainBuildStatus::Ready)
    {
        return status;
    }

    if (context_ == nullptr || window_ == nullptr)
    {
        throw std::logic_error("Swapchain is not initialized");
    }

    VulkanContext *context = context_;
    GLFWwindow *window = window_;

    VK_CHECK(context_->waitIdle());

    shutdown();
    initializeCore(*context, window);
    return SwapchainBuildStatus::Ready;
}

// 后处理 shader 输出线性颜色，使用 _SRGB 附件完成编码，不再让任意 UNORM fallback 改变颜色解释
VkSurfaceFormatKHR Swapchain::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &formats) const
{
    const std::array<VkFormat, 2> preferredFormats{VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB};
    for (VkFormat preferred : preferredFormats)
    {
        for (const VkSurfaceFormatKHR& available : formats)
        {
            if (available.format == preferred && available.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return available;
            }
        }
    }
    throw std::runtime_error("HDR presentation requires an sRGB swapchain format with SRGB_NONLINEAR color space");
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
    }
}

Swapchain::~Swapchain() noexcept
{
    shutdown();
}
