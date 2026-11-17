#include "core/AppConfig.hpp"
#include "render/Renderer.hpp"
#include "vulkan/VulkanCheck.hpp"
#include "vulkan/context/VulkanContext.hpp"
#include "vulkan/presentation/Swapchain.hpp"

#include <array>
#include <iostream>
#include <string>
#include <utility>

void Renderer::createTemporalMotionResources()
{
    temporalMotionAvailable_ = false;

    const auto reject = [&](const std::string& reason){
        destroyTemporalMotionResources();
        std::cout << "Temporal motion unavailable: " << reason << '\n';
    };

    if (!deferredLightingAvailable() || temporalGeometryPipelineLayout_ == VK_NULL_HANDLE)
    {
        reject("Deferred lighting or temporal geometry data layout is unavailable");
        return;
    }

    const VkExtent2D extent = swapchain_->extent();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context_->physicalDevice(), &properties);
    const auto& limits = properties.limits;
    constexpr std::size_t colorCount = gBufferColorFormats_.size() + 2;
    if (limits.maxColorAttachments < colorCount || limits.maxFragmentOutputAttachments < colorCount)
    {
        reject("Six color attachments and fragment outputs are required");
        return;
    }
    if (extent.width == 0 || extent.height == 0 || extent.width > limits.maxFramebufferWidth || extent.height > limits.maxFramebufferHeight)
    {
        reject("Framebuffer extent is zero or exceeds the device limit");
        return;
    }

    constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    constexpr VkFormatFeatureFlags features = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

    // 用来检查指定格式能否创建 “当前尺寸、单采样、既可以作为颜色附件写入又可供 shader 采样” 的二维图像
    const auto supportsImage = [&](VkFormat format, const char* label){
        VkFormatProperties formatProperties{};
        vkGetPhysicalDeviceFormatProperties(context_->physicalDevice(), format, &formatProperties);
        if ((formatProperties.optimalTilingFeatures & features) != features)
        {
            reject(std::string(label) + " lacks color attachment or sampled image support");
            return false;
        }

        VkImageFormatProperties imageProperties{};
        const VkResult result = vkGetPhysicalDeviceImageFormatProperties(context_->physicalDevice(), format, VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL, usage, 0, &imageProperties
        );

        if (result == VK_ERROR_FORMAT_NOT_SUPPORTED)
        {
            reject(std::string(label) + " does not support combined color attachment and sampled usage");
            return false;
        }
        VK_CHECK_RESULT(result, "Temporal motion image format query");
        if (extent.width > imageProperties.maxExtent.width || extent.height > imageProperties.maxExtent.height ||
            imageProperties.maxExtent.depth < 1 || imageProperties.maxMipLevels < 1 ||
            imageProperties.maxArrayLayers < 1 || (imageProperties.sampleCounts & VK_SAMPLE_COUNT_1_BIT) == 0)
        {
            reject(std::string(label) + " does not support the requested single-sample extent");
            return false;
        }

        return true;
    };

    if (!supportsImage(temporalMotionFormat_, "RGBA16F motion") ||
        !supportsImage(temporalSurfaceKeyFormat_, "RGBA32_UINT surface key"))
    {
        return;
    }

    const auto createImage = [&](VkFormat format, const std::string& name){
        ImageDesc desc{};
        desc.extent = {extent.width, extent.height, 1};
        desc.format = format;
        desc.usage = usage;
        desc.samples = VK_SAMPLE_COUNT_1_BIT;
        desc.mipLevels = 1;
        desc.arrayLayers = 1;
        desc.tiling = VK_IMAGE_TILING_OPTIMAL;
        desc.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        desc.debugName = name.c_str();

        GpuImage image = context_->createImage(desc);
        image.setView(context_->createImageView(image.get(), format, 1, VK_IMAGE_ASPECT_COLOR_BIT));
        return image;
    };

    std::array<std::array<GpuImage, 2>, MAX_FRAMES_IN_FLIGHT> pending{};
    for (std::size_t slot = 0; slot < pending.size(); ++slot)
    {
        const std::string suffix = " [" + std::to_string(slot) + "]";
        pending[slot][0] = createImage(temporalMotionFormat_, "Temporal motion" + suffix);
        pending[slot][1] = createImage(temporalSurfaceKeyFormat_, "Temporal surface key" + suffix);
    }

    GraphicsPipelineConfig config{};
    config.vertShaderPath = TEMPORAL_GEOMETRY_VERTEX_SHADER_PATH;
    config.fragShaderPath = TEMPORAL_GEOMETRY_FRAGMENT_SHADER_PATH;
    config.layout = temporalGeometryPipelineLayout_;
    config.useDynamicRendering = true;
    config.colorAttachmentFormats.assign(gBufferColorFormats_.begin(), gBufferColorFormats_.end());
    config.colorAttachmentFormats.push_back(temporalMotionFormat_);
    config.colorAttachmentFormats.push_back(temporalSurfaceKeyFormat_);
    config.depthAttachmentFormat = gBufferDepthFormat_;
    config.samples = VK_SAMPLE_COUNT_1_BIT;
    config.cullMode = VK_CULL_MODE_BACK_BIT;
    if (temporalMotionPipeline_ == VK_NULL_HANDLE)
    {
        temporalMotionPipeline_ = createGraphicsPipelineFromConfig(config);
    }
    config.cullMode = VK_CULL_MODE_NONE;
    if (temporalMotionDoubleSidedPipeline_ == VK_NULL_HANDLE)
    {
        temporalMotionDoubleSidedPipeline_ = createGraphicsPipelineFromConfig(config);
    }

    for (std::size_t slot = 0; slot < pending.size(); ++slot)
    {
        auto& frame = temporalGeometryFrames_[slot];
        frame.motion = std::move(pending[slot][0]);
        frame.surfaceKey = std::move(pending[slot][1]);
        frame.motionSampledLayout = false;
    }

    temporalMotionAvailable_ = true;

    std::cout << "Temporal motion targets prepared: " << pending.size() << " frames, " << extent.width << 'x'
              << extent.height << ", RGBA16F motion + RGBA32_UINT surface key, six MRT outputs\n";
}

void Renderer::destroyTemporalMotionResources() noexcept
{
    const VkDevice device = context_->device();

    if (temporalMotionPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, temporalMotionPipeline_, nullptr);
        temporalMotionPipeline_ = VK_NULL_HANDLE;
    }

    if (temporalMotionDoubleSidedPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, temporalMotionDoubleSidedPipeline_, nullptr);
        temporalMotionDoubleSidedPipeline_ = VK_NULL_HANDLE;
    }

    for (auto& frame : temporalGeometryFrames_)
    {
        frame.motion.reset();
        frame.surfaceKey.reset();
        frame.motionSampledLayout = false;
    }

    temporalMotionAvailable_ = false;
}

bool Renderer::temporalMotionAvailable() const noexcept
{
    return temporalMotionAvailable_;
}