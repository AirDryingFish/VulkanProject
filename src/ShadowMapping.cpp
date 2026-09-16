#include "Renderer.hpp"
#include "VulkanContext.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

void Renderer::createShadowTargets()
{
    // -- 从 gpu 支持的深度格式里，选一个既能当 depth attachment，又能被 shader 采样的格式，作为 shadow map 的格式--
    const std::array<VkFormat, 2> candidates{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D16_UNORM
    };

    constexpr VkFormatFeatureFlags requiredFeatures =
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    shadowDepthFormat_ = VK_FORMAT_UNDEFINED;

    for (VkFormat format : candidates)
    {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(
            context_->physicalDevice(),
            format,
            &properties
        );

        if ((properties.optimalTilingFeatures & requiredFeatures) == requiredFeatures)
        {
            shadowDepthFormat_ = format;
            break;
        }
    }

    if (shadowDepthFormat_ == VK_FORMAT_UNDEFINED)
    {
        throw std::runtime_error("No supported sampled shadow depth format");
    }
    // ----

    // -- 创建每帧图片 --
    for (std::size_t index = 0; index < shadowTargets_.size(); ++index)
    {
        const std::string name = "Directional shadow depth[" + std::to_string(index) + "]";
        ImageDesc desc{};
        desc.extent = {directionalShadowResolution, directionalShadowResolution, 1};
        desc.format = shadowDepthFormat_;
        desc.mipLevels = 1;
        desc.arrayLayers = 1;
        desc.samples = VK_SAMPLE_COUNT_1_BIT; // 阴影图暂时不用主画面的 MSAA
        desc.tiling = VK_IMAGE_TILING_OPTIMAL;
        desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        desc.debugName = name.c_str();

        ShadowTarget& target = shadowTargets_[index];
        target.depth = context_->createImage(desc);
        target.depth.setView(
            context_->createImageView(
                target.depth.get(),
                shadowDepthFormat_,
                1,
                VK_IMAGE_ASPECT_DEPTH_BIT
            )
        );
    }
    // ----

    // 创建两个 sampler
    SamplerDesc samplerDesc{};
    samplerDesc.magFilter = VK_FILTER_NEAREST;
    samplerDesc.minFilter = VK_FILTER_NEAREST;
    samplerDesc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    samplerDesc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerDesc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerDesc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;

    samplerDesc.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerDesc.anisotropyEnable = VK_FALSE;
    samplerDesc.maxAnisotropy = 1.0f;

    samplerDesc.minLod = 0.0f;
    samplerDesc.maxLod = 0.0f;

    samplerDesc.compareEnable = VK_TRUE;
    // LESS_OR_EQUAL: 后续查询时，表面的参考深度小于等于图片中保存的最近深度，表示没有被更近的表面挡住
    samplerDesc.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    samplerDesc.debugName = "Directional shadow comparison sampler";
    shadowCompareSampler_ = context_->createSampler(samplerDesc);

    samplerDesc.compareEnable = VK_FALSE;
    samplerDesc.debugName = "Directional shadow preview sampler";
    shadowPreviewSampler_ = context_->createSampler(samplerDesc);

    std::cout
        << "Shadow targets created: "
        << shadowTargets_.size()
        << " x "
        << directionalShadowResolution
        << "x"
        << directionalShadowResolution
        << '\n';
}
void Renderer::destroyShadowTargets() noexcept
{
    for (ShadowTarget& target : shadowTargets_)
    {
        target.depth.reset();
    }
    shadowCompareSampler_.reset();
    shadowPreviewSampler_.reset();
    shadowDepthFormat_ = VK_FORMAT_UNDEFINED;
}