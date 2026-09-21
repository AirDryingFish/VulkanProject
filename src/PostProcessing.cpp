#include "Renderer.hpp"
#include "VulkanContext.hpp"
#include "Swapchain.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

void Renderer::selectHdrConfiguration()    // 选择支持的格式和采样数
{
    if (context_ == nullptr || swapchain_ == nullptr)
    {
        throw std::logic_error("HDR configuration requires a context and swapchain");
    }

    const VkExtent2D extent = swapchain_->extent();
    if (extent.width == 0 || extent.height == 0)
    {
        throw std::runtime_error("Cannot create HDR targets with a zero extent");
    }

    hdrFormat_ = VK_FORMAT_R16G16B16A16_SFLOAT;
    sceneDepthFormat_ = context_->findDepthFormat();

    // 1. VkFormatProperties: 这个 “像素格式” 本身支持什么功能
    const VkPhysicalDevice physicalDevice = context_->physicalDevice();
    VkFormatProperties formatProperties{};
    // Gpu 对 VK_FORMAT_R16G16B16A16_SFLOAT 这个格式支持哪些操作
    vkGetPhysicalDeviceFormatProperties(physicalDevice, hdrFormat_, &formatProperties);
    // 至少同时支持 1. 作为 Color Attachment + 2. 作为纹理被采样 + 3. 纹理采样时支持线性过滤
    constexpr VkFormatFeatureFlags requiredFeatures =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((formatProperties.optimalTilingFeatures & requiredFeatures) != requiredFeatures)
    {
        throw std::runtime_error("RBGA16F requires color attachment, sampled image and linear filtering support");
    }

    // 2. VkImageFormatProperties: 这个 format + usage 的具体 image 能不能创建
    const auto queryImageSupport = [&](VkFormat format, VkImageUsageFlags usage)
    {
        VkImageFormatProperties properties{};
        const VkResult result = vkGetPhysicalDeviceImageFormatProperties(
            physicalDevice,
            format,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL,
            usage,
            0,
            &properties
        );

        const std::string description = "format=" + std::to_string(static_cast<int>(format)) + ", usage=" + std::to_string(usage);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("HDR image query failed: " + description + ", result=" + std::to_string(static_cast<int>(result)));
        }
        if (extent.width > properties.maxExtent.width || extent.height > properties.maxExtent.height ||
            properties.maxExtent.depth < 1 || properties.maxMipLevels < 1 || properties.maxArrayLayers < 1)
        {
            throw std::runtime_error("HDR image dimension unsupported: " + description);
        }
        return properties;
    };

    const auto hdrProperties = queryImageSupport(hdrFormat_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    if ((hdrProperties.sampleCounts & VK_SAMPLE_COUNT_1_BIT) == 0)
    {
        throw std::runtime_error("Single-sample RGBA16F HDR target is unsupported");
    }

    const auto colorProperties = queryImageSupport(hdrFormat_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    const auto depthProperties = queryImageSupport(sceneDepthFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);

    // MSAA color + MSAA depth 必须使用相同 sample count
    // color       = 1111
    // depth       = 0111
    // fb color    = 1111
    // fb depth    = 0011
    // ----------------
    // intersection= 0011
    // 最后支持 1x 2x
    const VkSampleCountFlags supportedSamples =
        colorProperties.sampleCounts &
        depthProperties.sampleCounts &
        deviceProperties.limits.framebufferColorSampleCounts &
        deviceProperties.limits.framebufferDepthSampleCounts;
    // 实际使用的 msaa sample count
    const std::array<VkSampleCountFlagBits, 3> candidates{VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_2_BIT, VK_SAMPLE_COUNT_1_BIT};
    for (const VkSampleCountFlagBits samples : candidates)
    {
        if (samples <= context_->msaaSamples() && (supportedSamples & samples) != 0)
        {
            sceneSamples_ = samples;
            return;
        }
    }

    throw std::runtime_error("No compatible sample count for HDR color and scene depth");
}

// 创建每帧图像及 image view
void Renderer::createHdrTargets()
{
    selectHdrConfiguration();

    const VkExtent2D extent = swapchain_->extent();

    const auto createTargetImage = [&](
        VkFormat format,                // 像素格式
        VkSampleCountFlagBits samples,  // 每像素采样次数
        VkImageUsageFlags usage,        // 这张图拿来干什么
        VkImageAspectFlags aspect,      // ImageView 看图像的哪部分
        const std::string& name)
    {
        ImageDesc desc{};
        desc.extent = {extent.width, extent.height, 1};
        desc.format = format;
        desc.samples = samples;
        desc.usage = usage;
        desc.mipLevels = 1;
        desc.arrayLayers = 1;
        desc.tiling = VK_IMAGE_TILING_OPTIMAL;
        desc.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        desc.debugName = name.c_str();

        GpuImage image = context_->createImage(desc);

        image.setView(context_->createImageView(image.get(), format, 1, aspect));
        return image;
    };

    for (std::size_t index = 0; index < hdrTargets_.size(); ++index)
    {
        HdrFrameTarget& target = hdrTargets_.at(index);
        const std::string suffix = "[" + std::to_string(index) + "]";
        target.hdrColor = createTargetImage(
            hdrFormat_,
            VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            "HDR Color" + suffix
        );

        if (sceneSamples_ != VK_SAMPLE_COUNT_1_BIT)
        {
            target.msaaColor = createTargetImage(
                hdrFormat_,
                sceneSamples_,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                "HDR MSAA Color" + suffix
            );
        }

        target.depth = createTargetImage(
            sceneDepthFormat_,
            sceneSamples_,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            "HDR scene depth" + suffix
        );
    }
    std::cout
        << "HDR targets prepared: "
        << hdrTargets_.size() << " frames, "
        << extent.width << "x" << extent.height
        << ", RGBA16F, scene samples="
        << static_cast<unsigned int>(sceneSamples_)
        << '\n';
}
void Renderer::destroyHdrTargets() noexcept // 释放图像
{
    for (HdrFrameTarget& target : hdrTargets_)
    {
        target.depth.reset();
        target.msaaColor.reset();
        target.hdrColor.reset();
    }

    hdrFormat_ = VK_FORMAT_UNDEFINED;
    sceneDepthFormat_ = VK_FORMAT_UNDEFINED;
    sceneSamples_ = VK_SAMPLE_COUNT_1_BIT;
}