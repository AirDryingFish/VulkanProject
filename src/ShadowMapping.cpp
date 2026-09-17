#include "Renderer.hpp"
#include "VulkanContext.hpp"
#include "VulkanCheck.hpp"

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

// render pass 描述 “怎么使用附件”
void Renderer::createShadowRenderPass()
{
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = shadowDepthFormat_;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    // 每帧从一张空的深度图开始
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // pass 结束后保留深度，供主画面读取
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // 不保留上一帧的像素内容
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // pass 结束后准备好给 shader 采样
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthReference{};
    depthReference.attachment = 0;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthReference;

    // 两个 dependency 分别描述 “之前的采样 -> 本次深度写入” 和 “本次深度写入 -> 后续采样” 的同步关系
    // 1. shadow map 被 fragment shader 采样 -> dependencies[0]
    // 2. 本次 shadow pass: shadow map 作为 depth attachment 读 / 写 -> dependencies[1]
    // 3. shadow map 再给 fragment shader 采样
    std::array<VkSubpassDependency, 2> dependencies{};
    // render pass 外部 -> 当前 subpass 0
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    // 之前这张图可能正在被 fragment shader 当 shadow map 读
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    // 现在要把它作为 depth attachment，用于深度测试和深度写入
    dependencies[0].dstStageMask =
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    // 当前 shadow subpass -> render pass 外部
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask =
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &depthAttachment;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = static_cast<std::uint32_t>(dependencies.size());
    createInfo.pDependencies = dependencies.data();

    VK_CHECK(vkCreateRenderPass(
        context_->device(),
        &createInfo,
        nullptr,
        &shadowRenderPass_
    ));
}
// Framebuffer 指定 “使用哪张图”。所有帧可以共用一个 shadowRenderPass_，但每帧使用自己的 framebuffer和深度图
void Renderer::createShadowFramebuffers()
{
    for (ShadowTarget& target : shadowTargets_)
    {
        const VkImageView depthView = target.depth.view();
        VkFramebufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = shadowRenderPass_;
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &depthView;
        createInfo.width = directionalShadowResolution;
        createInfo.height = directionalShadowResolution;
        createInfo.layers = 1;

        VK_CHECK(vkCreateFramebuffer(
            context_->device(),
            &createInfo,
            nullptr,
            &target.framebuffer
        ));
    }
}
void Renderer::recordShadowPass(const FrameToken &token)
{
    const ShadowTarget& target = shadowTargets_.at(token.frameIndex);

    VkClearValue clearValue{};
    // 深度清成 1.0f，代表目前还没有记录到更近的遮挡物
    clearValue.depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass = shadowRenderPass_;
    beginInfo.framebuffer = target.framebuffer;
    beginInfo.renderArea.offset = {0, 0};
    beginInfo.renderArea.extent = {
        directionalShadowResolution,
        directionalShadowResolution};
    beginInfo.clearValueCount = 1;
    beginInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(
        token.commandBuffer,
        &beginInfo,
        VK_SUBPASS_CONTENTS_INLINE
    );
    // 这里暂时没有 draw，但不是无效操作。render pass 会执行附件清空，以及相应的 layout 转换
    // 不绘制就不需要绑定 graphics pipeline

    vkCmdEndRenderPass(token.commandBuffer);
}

void Renderer::destroyShadowTargets() noexcept
{
    const VkDevice device = context_->device();
    for (ShadowTarget& target : shadowTargets_)
    {
        if (target.framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(
                device,
                target.framebuffer,
                nullptr
            );
            target.framebuffer = VK_NULL_HANDLE;
        }
        target.depth.reset();
    }
    shadowCompareSampler_.reset();
    shadowPreviewSampler_.reset();
    if (shadowRenderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(
            device,
            shadowRenderPass_,
            nullptr
        );
        shadowRenderPass_ = VK_NULL_HANDLE;
    }

    shadowDepthFormat_ = VK_FORMAT_UNDEFINED;
}