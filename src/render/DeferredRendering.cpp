#include "core/AppConfig.hpp"
#include "render/Renderer.hpp"
#include "vulkan/Swapchain.hpp"
#include "vulkan/VulkanCheck.hpp"
#include "vulkan/VulkanContext.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

void Renderer::createGBufferTargets()
{
    if (context_ == nullptr || swapchain_ == nullptr)
    {
        throw std::logic_error("G-buffer requires a context and swapchain");
    }

    gBufferAvailable_ = false;
    gBufferUnavailableReason_ = "G-buffer creation has not completed";


    const auto reject = [&](const std::string& reason){
        destroyGBufferTargets();
        gBufferUnavailableReason_ = reason;

        std::cout << "G-buffer unavailable: " << reason << '\n';
    };

    if (!useDynamicRenderingForScene())
    {
        reject("Dynamic Rendering and Synchronization2 are required");
        return;
    }

    const VkExtent2D extent = swapchain_->extent();
    if (extent.width == 0 || extent.height == 0)
    {
        reject("Framebuffer extent is zero");
        return;
    }

    const VkPhysicalDevice physicalDevice = context_->physicalDevice();
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    const auto& limits = properties.limits;
    if (limits.maxColorAttachments < gBufferColorFormats_.size() ||
        limits.maxFragmentOutputAttachments < gBufferColorFormats_.size() ||
        extent.width > limits.maxFramebufferWidth ||
        extent.height > limits.maxFramebufferHeight)
    {
        reject("Four MRT outputs or framebuffer extent are unsupported");
        return;
    }

    constexpr VkImageUsageFlags colorUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;

    constexpr VkImageUsageFlags depthUsage =
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;

    // 在创建 Gbuffer 图片之前，检查 gpu 是否支持要求的图片格式、用途、尺寸和单采样配置
    const auto supportImage = [&](
        VkFormat format,
        VkImageUsageFlags usage,
        VkFormatFeatureFlags features
    ){
        const std::string label = "format=" + std::to_string(static_cast<int>(format));

        VkFormatProperties formatProperties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProperties);
        if ((formatProperties.optimalTilingFeatures & features) != features)
        {
            reject("Required format features are missing: " + label);
            return false;
        }

        VkImageFormatProperties imageProperties{};
        const VkResult result = vkGetPhysicalDeviceImageFormatProperties(
            physicalDevice,
            format,
            VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL,
            usage,
            0,
            &imageProperties
        );

        if (result == VK_ERROR_FORMAT_NOT_SUPPORTED)
        {
            reject("Image usage combination is unsupported: " + label);
            return false;
        }
        VK_CHECK_RESULT(result, "G-buffer image format query");

        if (extent.width > imageProperties.maxExtent.width ||
            extent.height > imageProperties.maxExtent.height ||
            imageProperties.maxExtent.depth < 1 ||
            imageProperties.maxMipLevels < 1 ||
            imageProperties.maxArrayLayers < 1 ||
            (imageProperties.sampleCounts &
             VK_SAMPLE_COUNT_1_BIT) == 0)
        {
            reject(
                "Single-sample image extent is unsupported: " +
                label);
            return false;
        }

        return true;
    };

    for (VkFormat format : gBufferColorFormats_)
    {
        if (!supportImage(format, colorUsage, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        {
            return;
        }
    }

    if (!supportImage(gBufferDepthFormat_, depthUsage, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
    {
        return;
    }

    const auto createImage = [&](
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageAspectFlags aspect,
        const std::string& name
    ){
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
        image.setView(context_->createImageView(image.get(), format, 1, aspect));

        return image;
    };

    std::array<GBufferTarget, MAX_FRAMES_IN_FLIGHT> pending{};
    const std::array<const char *, 4> names{
        "G-buffer normal",
        "G-buffer base color",
        "G-buffer material",
        "G-buffer emissive"
    };

    for (std::size_t frame = 0; frame < pending.size(); ++frame)
    {
        const std::string suffix = "[" + std::to_string(frame) + "]";

        for (std::size_t attachment = 0; attachment < gBufferColorFormats_.size(); ++attachment)
        {
            pending[frame].colors[attachment] = createImage(
                gBufferColorFormats_[attachment],
                colorUsage,
                VK_IMAGE_ASPECT_COLOR_BIT,
                std::string(names[attachment])
            );
        }

        pending[frame].depth = createImage(
            gBufferDepthFormat_,
            depthUsage,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            "G-buffer depth" + suffix
        );
    }

    // 调用位置必须保证旧图像已不再被 gpu 使用
    gBufferTargets_ = std::move(pending);
    gBufferAvailable_ = true;
    gBufferUnavailableReason_.clear();

    std::cout
        << "G-buffer targets prepared: "
        << gBufferTargets_.size() << " frames, "
        << extent.width << "x" << extent.height
        << ", 4 colors + D32 depth, samples=1\n";
}

void Renderer::destroyGBufferTargets() noexcept
{
    for (GBufferTarget& target : gBufferTargets_)
    {
        for (GpuImage& color : target.colors)
        {
            color.reset();
        }

        target.depth.reset();
        target.sampledLayout = false;
    }

    gBufferAvailable_ = false;
    gBufferUnavailableReason_.clear();
}

void Renderer::createGBufferPipelines()
{
    if (!gBufferAvailable_)
    {
        return;
    }

    GraphicsPipelineConfig config{};
    config.vertShaderPath = MAIN_VERTEX_SHADER_PATH;
    config.fragShaderPath = GBUFFER_FRAGMENT_SHADER_PATH;
    config.layout = scenePipelineLayout_;
    config.useDynamicRendering = true;
    config.colorAttachmentFormats.assign(gBufferColorFormats_.begin(), gBufferColorFormats_.end());
    config.depthAttachmentFormat = gBufferDepthFormat_;
    config.samples = VK_SAMPLE_COUNT_1_BIT;
    config.cullMode = VK_CULL_MODE_BACK_BIT;
    if (gBufferPipeline_ == VK_NULL_HANDLE)
    {
        gBufferPipeline_ = createGraphicsPipelineFromConfig(config);
    }

    config.cullMode = VK_CULL_MODE_NONE;
    if (gBufferDoubleSidedPipeline_ == VK_NULL_HANDLE)
    {
        gBufferDoubleSidedPipeline_ = createGraphicsPipelineFromConfig(config);
    }
}

void Renderer::recordGBufferPass(const FrameToken &token, const RenderFrameData &data)
{
    GBufferTarget& target = gBufferTargets_.at(token.frameIndex);

    const auto& functions = context_->deviceFunctions();
    const VkExtent2D extent = swapchain_->extent();

    // 用来给 5 张 gbuffer 图片统一记录 同步barrier，让它们在“附件写入”和“shader 采样读取”两种用途之间切换
    const auto transition = [&](bool toAttachment)
    {
        std::array<VkImageMemoryBarrier2, 5> barriers{};

        for (std::size_t i = 0; i < barriers.size(); ++i)
        {
            const bool depth = i == 4;

            const GpuImage& image = depth ? target.depth : target.colors[i];

            // 这张图被作为附件使用的时候，GPU 在哪些管线阶段访问它
            const VkPipelineStageFlags2 stages =
                depth ?
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                :
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

            const VkAccessFlags2 writes =
                depth ?
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                :
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

            const VkImageLayout attachmentLayout =
                depth ?
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                :
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            const VkImageLayout sampledLayout =
                depth ?
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                :
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            auto& barrier = barriers[i];
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image.get();
            barrier.subresourceRange.aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;

            if (toAttachment)
            {
                // false -> 图片刚创建，尚未使用
                // true -> 上一次 Geometry 后已经转换为采样读取
                // 两种情况都要转换到 newLayout 去
                barrier.oldLayout = target.sampledLayout ? sampledLayout : VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = attachmentLayout;

                if (target.sampledLayout)
                {
                    barrier.srcStageMask = stages | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                    barrier.srcAccessMask = writes | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
                }
                barrier.dstStageMask = stages;
                barrier.dstAccessMask = writes;

                if (depth)
                {
                    barrier.dstAccessMask |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                }
            }

            else
            {
                barrier.oldLayout = attachmentLayout;
                barrier.newLayout = sampledLayout;
                barrier.srcStageMask = stages;
                barrier.srcAccessMask = writes;
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            }
        }

        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
        dependency.pImageMemoryBarriers = barriers.data();
        functions.cmdPipelineBarrier2(token.commandBuffer, &dependency);
    };

    transition(true);
    std::array<VkRenderingAttachmentInfo, 4> colors{};
    for (std::size_t i = 0; i < colors.size(); ++i)
    {
        colors[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colors[i].imageView = target.colors[i].view();
        colors[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colors[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colors[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }

    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = target.depth.view();
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = extent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = static_cast<std::uint32_t>(colors.size());
    rendering.pColorAttachments = colors.data();
    rendering.pDepthAttachment = &depth;

    functions.cmdBeginRendering(token.commandBuffer, &rendering);


    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(token.commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(token.commandBuffer, 0, 1, &scissor);

    vkCmdBindDescriptorSets(
        token.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        scenePipelineLayout_,
        0,
        1,
        &data.frameDescriptorSet,
        0,
        nullptr
    );

    if (data.objects != nullptr)
    {
        FrameContext& frame = frames_.at(token.frameIndex);
        const VkDeviceSize offset = 0;

        for (const RenderObjectView& object : *data.objects)
        {
            if (object.indexCount == 0 ||
                object.vertexBuffer == VK_NULL_HANDLE ||
                object.indexBuffer == VK_NULL_HANDLE ||
                object.materialDescriptorSet == VK_NULL_HANDLE)
            {
                continue;
            }

            vkCmdBindPipeline(
                token.commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                object.doubleSided ? gBufferDoubleSidedPipeline_ : gBufferPipeline_
            );

            vkCmdBindDescriptorSets(
                token.commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                scenePipelineLayout_,
                1, 1,
                &object.materialDescriptorSet,
                0, nullptr
            );

            vkCmdPushConstants(
                token.commandBuffer,
                scenePipelineLayout_,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(DrawPushConstants),
                &object.pushConstants
            );

            vkCmdBindVertexBuffers(
                token.commandBuffer,
                0,
                1,
                &object.vertexBuffer,
                &offset
            );

            vkCmdBindIndexBuffer(
                token.commandBuffer,
                object.indexBuffer,
                0,
                VK_INDEX_TYPE_UINT32
            );

            vkCmdDrawIndexed(
                token.commandBuffer,
                object.indexCount,
                1,
                0,
                0,
                0
            );

            ++frame.recordedDraws.sceneDraws;
            frame.recordedDraws.sceneTriangles += object.indexCount / 3;
        }
    }

    functions.cmdEndRendering(token.commandBuffer);

    transition(false);
    target.sampledLayout = true;
}
