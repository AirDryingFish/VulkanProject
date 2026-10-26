#include "core/AppConfig.hpp"
#include "render/Renderer.hpp"
#include "vulkan/VulkanContext.hpp"
#include "vulkan/Swapchain.hpp"
#include "vulkan/VulkanCheck.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <optional>

namespace
{
    // 集中填写图像和子资源范围，让每条 barrier 的同步含义更容易阅读
    VkImageMemoryBarrier2 makeSceneImageBarrier(const GpuImage& image, VkImageAspectFlags aspect)
    {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.get();

        barrier.subresourceRange.aspectMask = aspect;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        return barrier;
    }

    // 图像格式 -> 每个样本的理论字节数
    std::optional<std::uint64_t> nominalBytesPerSample(VkFormat format) noexcept
    {
        switch (format)
        {
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return 8;

        case VK_FORMAT_D32_SFLOAT:
            return 4;

        case VK_FORMAT_D16_UNORM:
            return 2;

        case VK_FORMAT_D24_UNORM_S8_UINT:
            return 4;

        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return 5;

        default:
            return std::nullopt;
        }
    }

    std::optional<std::uint64_t> estimateImagePayload(const GpuImage& image) noexcept
    {
        if (!image)
        {
            return std::uint64_t{0};
        }

        const VkExtent3D extent = image.extent();

        // 限定当前渲染目标使用的形态
        if (image.mipLevels() != 1 || image.arrayLayers() != 1 || extent.depth != 1)
        {
            return std::nullopt;
        }

        const auto bytesPerSample = nominalBytesPerSample(image.format());

        if (!bytesPerSample)
        {
            return std::nullopt;
        }

        return static_cast<std::uint64_t>(extent.width) * extent.height *
                static_cast<std::uint64_t>(image.samples()) *
                *bytesPerSample;
    }

}


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

// 在 scene 绘制前，把图片准备成“渲染目标”
void Renderer::recordSceneAttachmentsBegin(const FrameToken &token)
{
    if (!initialized_ || context_ == nullptr)
    {
        throw std::logic_error("Scene barriers require an initialized renderer");
    }

    const auto cmdPipelineBarrier2 = context_->deviceFunctions().cmdPipelineBarrier2;
    if (!context_->capabilities().synchronization2Enabled || cmdPipelineBarrier2 == nullptr)
    {
        throw std::logic_error("Scene barriers require Synchronization2");
    }

    const HdrFrameTarget& target = hdrTargets_.at(token.frameIndex);

    std::array<VkImageMemoryBarrier2, 3> barriers{};
    std::uint32_t barrierCount = 0;

    // 1x 时直接绘制，MSAA 时接收 resolve
    // 两条路径最终都由 Post 采样 hdrColor
    auto& hdr = barriers[barrierCount++];
    hdr = makeSceneImageBarrier(target.hdrColor, VK_IMAGE_ASPECT_COLOR_BIT);
    hdr.srcStageMask =
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |           // post 读取 hdrColor
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;    // Scene 或 附件 resolve 写入 hdrColor
    hdr.srcAccessMask =
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    hdr.dstStageMask =
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    hdr.dstAccessMask =
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    hdr.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    hdr.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    if (sceneSamples_ != VK_SAMPLE_COUNT_1_BIT)
    {
        auto& msaa = barriers[barrierCount++];
        msaa = makeSceneImageBarrier(target.msaaColor, VK_IMAGE_ASPECT_COLOR_BIT);

        msaa.srcStageMask =
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        msaa.srcAccessMask =
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        msaa.dstStageMask =
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        msaa.dstAccessMask =
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

        msaa.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        msaa.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkImageAspectFlags depthAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    const VkFormat depthFormat = target.depth.format();
    if (depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
        depthFormat == VK_FORMAT_D24_UNORM_S8_UINT)
    {
        depthAspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    // 深度 模板附件会在哪些阶段被访问，以及会发生哪些读写
    const VkPipelineStageFlags2 depthStages =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |  // 提前做深度/模板测试，例如尽早剔除被遮挡的片元
        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;    // 在片元着色后做深度/模板测试及相关操作
    // 以普通深度测试为例，假设深度附件里某个位置保存着 0.6，新片元的深度是 0.3，比较方式是 LESS
    // READ: 读取已有深度 0.6
    // WRITE: 如果开启深度写入，把 0.3写回深度附件
    const VkAccessFlags2 depthAccess =
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    auto& depth = barriers[barrierCount++];
    depth = makeSceneImageBarrier(target.depth, depthAspect);

    depth.srcStageMask = depthStages;
    depth.srcAccessMask = depthAccess;
    depth.dstStageMask = depthStages;
    depth.dstAccessMask = depthAccess;
    depth.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // 接下来把图像作为深度/模板附件供深度测试、模板测试相关
    depth.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = barrierCount;
    dependency.pImageMemoryBarriers = barriers.data();

    cmdPipelineBarrier2(token.commandBuffer, &dependency);
}

// 在 scene 绘制后，把 HDR 颜色图准备成 “后处理输入纹理”
void Renderer::recordSceneColorForPost(const FrameToken &token)
{
    if (!initialized_ || context_ == nullptr)
    {
        throw std::logic_error("Scene barriers require an initialized renderer");
    }
    const auto cmdPipelineBarrier2 = context_->deviceFunctions().cmdPipelineBarrier2;
    if (!context_->capabilities().synchronization2Enabled || cmdPipelineBarrier2 == nullptr)
    {
        throw std::logic_error("Scene barriers require Synchronization2");
    }

    const HdrFrameTarget& target = hdrTargets_.at(token.frameIndex);
    auto barrier = makeSceneImageBarrier(target.hdrColor, VK_IMAGE_ASPECT_COLOR_BIT);

    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;

    cmdPipelineBarrier2(token.commandBuffer, &dependency);
}

void Renderer::beginScenePass(const FrameToken &token, const std::array<VkClearValue, 2> &clearValues)
{
    if (!useDynamicRenderingForScene())
    {
        VkRenderPassBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        beginInfo.renderPass = sceneRenderPass_;
        beginInfo.framebuffer = hdrTargets_.at(token.frameIndex).framebuffer;
        beginInfo.renderArea.offset = {0, 0};
        beginInfo.renderArea.extent = swapchain_->extent();
        beginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        beginInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(
            token.commandBuffer,
            &beginInfo,
            VK_SUBPASS_CONTENTS_INLINE
        );
        return;
    }

    const auto cmdBeginRendering = context_->deviceFunctions().cmdBeginRendering;
    if (cmdBeginRendering == nullptr)
    {
        throw std::logic_error("Dynamic scene requires vkCmdBeginRendering");
    }

    // -- 准备 scene 渲染使用的颜色附件 --
    recordSceneAttachmentsBegin(token);

    const HdrFrameTarget& target = hdrTargets_.at(token.frameIndex);
    const bool useMsaa = sceneSamples_ != VK_SAMPLE_COUNT_1_BIT;

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = useMsaa ? target.msaaColor.view() : target.hdrColor.view();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // 开始渲染时，清空成指定的颜色
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // 不开 msaa： 直接画在 hdrColor 上，post还要读取，必须保留
    // 开启 msaa： resolve 后使用的是 hdrColor，原始的 msaaColor 可以丢弃
    color.storeOp = useMsaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = clearValues[0];
    // 暂时不启用 Resolve
    color.resolveMode = VK_RESOLVE_MODE_NONE;

    if (useMsaa)
    {
        // 表示对一个像素中的多个采样颜色取平均值
        color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        color.resolveImageView = target.hdrColor.view();
        color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = target.depth.view();
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue = clearValues[1];

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = swapchain_->extent();
    renderingInfo.layerCount = 1;
    renderingInfo.viewMask = 0;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &color;
    renderingInfo.pDepthAttachment = &depth;
    renderingInfo.pStencilAttachment = nullptr;

    cmdBeginRendering(token.commandBuffer, &renderingInfo);
}

void Renderer::endScenePass(const FrameToken &token)
{
    if (!useDynamicRenderingForScene())
    {
        vkCmdEndRenderPass(token.commandBuffer);
        return;
    }

    const auto cmdEndRendering = context_->deviceFunctions().cmdEndRendering;
    if (cmdEndRendering == nullptr)
    {
        throw std::logic_error("Dynamic rendering requires vkCmdEndRendering");
    }

    cmdEndRendering(token.commandBuffer);
    recordSceneColorForPost(token);
}

// ---------开始 Scene RenderPass-----------------------
//         ↓
// Subpass 0
// ├─ Vertex Shader
// ├─ Rasterization
// ├─ Fragment Shader
// │
// ├─ Depth Test
// │    └─ 读写 4x depth
// │
// ├─ Color Output
// │    └─ 写 4x msaaColor
// │
// └─ Resolve
//      └─ 4x msaaColor
//           ↓
//         1x hdrColor

//         ↓
// 结束 Scene RenderPass
// ---------------------------------------------------
// ---------------- External subpass -----------------
// ---------------------------------------------------
// hdrColor 进入 SHADER_READ_ONLY_OPTIMAL
//         ↓
// PostProcess Pass
//         ↓
// Fragment Shader 采样 hdrColor
//         ↓
// Tone Mapping
//         ↓
// Swapchain
void Renderer::createSceneRenderPass()
{
    if (context_ == nullptr || hdrFormat_ == VK_FORMAT_UNDEFINED || sceneDepthFormat_ == VK_FORMAT_UNDEFINED)
    {
        throw std::logic_error("Scene render pass requires an HDR configuration");
    }

    // 不开 MSAA: 直接把场景画到最终 HDR 图里
    // 开 MSAA: 先画到 MSAA Color，再 resolve 到单采样 hdr 图
    const bool useMsaa = sceneSamples_ != VK_SAMPLE_COUNT_1_BIT;
    std::array<VkAttachmentDescription, 3> attachments{};

    // 附件 0: 直接绘制的颜色附件
    VkAttachmentDescription& color= attachments[0];
    color.format = hdrFormat_;
    color.samples = sceneSamples_;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // renderpass 结束后，这个 attachment 里的结果还要不要保存
    // 1. 不开启 msaa，render pass 结束后还要 tone mapping -> sample hdrColor，所以颜色结果必须保留下来
    // 2. 开启 msaa, attachments[0] 是 MSAA color image (Raster -> MSAA color -> single-sample hdrColor)，它只是中间结果在 resolve 完就没用了
    color.storeOp = useMsaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // 1. 不开 msaa，第 0 个 attachment 本身就是最终 HDR Color。下一步一般是 tone mapping pass，它会把 hdr image当纹理读，所以必须进入 SHADER_READ_ONLY_OPTIMAL
    // 2. 开启 msaa，第 0 个 attachment 是临时的，它结束后仍然可以作为 attachment，因为它下一帧大概率继续当 attachment
    color.finalLayout = useMsaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // 附件 1: 场景深度
    VkAttachmentDescription& depth = attachments[1];
    depth.format = sceneDepthFormat_;
    depth.samples = sceneSamples_;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // 附件 2: MSAA resolve 后的单采样 HDR 颜色 (1x 分支不会把这个附件交给 vulkan)
    VkAttachmentDescription& resolve = attachments[2];
    resolve.format = hdrFormat_;
    resolve.samples = VK_SAMPLE_COUNT_1_BIT;
    resolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    resolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    resolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    resolve.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthReference{};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference resolveReference{};
    resolveReference.attachment = 2;
    resolveReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;
    subpass.pResolveAttachments = useMsaa ? &resolveReference : nullptr;

    std::array<VkSubpassDependency, 2> dependencies{};
    // 定义一个 external -> subpass 0 的同步依赖：
    // 在进入这个 scene render pass 之前，先确保“之前对这些 image 的读写”已经完成，并且这些 image 现在可以安全地拿来当 color/depth attachment
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL; // VK_SUBPASS_EXTERNAL 不是 “另一个 subpass”，而是 “当前 render pass” 之外的操作。
    dependencies[0].dstSubpass = 0;
    // 1. srcStageMask: 我要等 “之前哪些 Pipeline”
    dependencies[0].srcStageMask =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |         // fragment shader 在读某张 image
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | // 之前可能有 color attachment write 比如上一帧 scene pass 写过它
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |    // depth attachment 的读写
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    // 2. srcAccessMask: 之前到底在访问什么，这个阶段对资源做了什么
    dependencies[0].srcAccessMask =
        // VK_ACCESS_SHADER_READ_BIT 通常与 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT一起看
        // 表示 fragment shader 之前在读取 image
        VK_ACCESS_SHADER_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |          // 之前在写 color attachment
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;   // 之前在写 depth stencil attachment
    // 3. dstStageMask: 当前要在哪些阶段开始使用，subpass 0 中 image 会被使用的阶段
    // 是在告诉 vulkan 这些阶段不能太早执行，要等 src 那边完成
    dependencies[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    // 4. dstAccessMask: 当前要怎么访问
    // 当前 subpass 里，color attachment 可能读/写，depth attachment 也可能读/写
    dependencies[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = 0;

    // 本次颜色写入/resolve -> 后处理 fragment shader 采样
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = 0;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = useMsaa ? 3u : 2u;
    createInfo.pAttachments = attachments.data();
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    createInfo.pDependencies = dependencies.data();

    VK_CHECK(vkCreateRenderPass(
        context_->device(),
        &createInfo,
        nullptr,
        &sceneRenderPass_
    ));
}

void Renderer::createPresentRenderPass()
{
    if (context_ == nullptr || swapchain_ == nullptr || swapchain_->format() == VK_FORMAT_UNDEFINED)
    {
        throw std::logic_error("Present render pass requires a valid swapchain");
    }

    // 唯一的附件：当前 swapchain 图片
    VkAttachmentDescription color{};
    // 输出附件必须匹配实际交换链格式
    color.format = swapchain_->format();
    // MSAA 已在 Scene Pass resolve，显示阶段使用单采样
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // 这里必须 store，因为后面还要 vkQueuePresentKHR 把这张 swapchain image 显示出去
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // 关心它进入这个 Render Pass 之前里面原来的内容，也不打算保留，所以可以把旧内容直接丢掉。
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    // 外部的 subpass 源 stage 是 COLOR_ATTACHMENT_OUTPUT
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    // 当前 subpass 的 COLOR_ATTACHMENT_OUTPUT
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    // 不需要等待某个具体的 “之前的内存读/写访问” 变得可见
    dependency.srcAccessMask = 0;
    // 当前 subpass 接下来要进行的是 color attachment write
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &color;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;

    VK_CHECK(vkCreateRenderPass(context_->device(), &createInfo, nullptr, &presentRenderPass_));
}

void Renderer::recordPresentImageTransition(
    const FrameToken& token,
    PresentImageTransition transition)
{
    if (!initialized_ || context_ == nullptr || swapchain_ == nullptr)
    {
        throw std::logic_error("Present image transition requires an initialized renderer");
    }

    const auto cmdPipelineBarrier2 = context_->deviceFunctions().cmdPipelineBarrier2;
    if (!context_->capabilities().synchronization2Enabled || cmdPipelineBarrier2 == nullptr)
    {
        throw std::logic_error("Present image transition requires Synchronization2");
    }

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapchain_->image(token.imageIndex);

    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    if (transition == PresentImageTransition::ToColorAttachment)
    {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_NONE;

        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

        // 本帧会 clear 主动丢弃旧内容，这不代表可以省略 acquire 等待
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    else
    {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

        barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask = VK_ACCESS_2_NONE;

        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;

    cmdPipelineBarrier2(token.commandBuffer, &dependency);
}

void Renderer::beginPresentPass(const FrameToken &token, const VkClearValue &value)
{
    if (!useDynamicRenderingForPresent())
    {
        VkRenderPassBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        beginInfo.renderPass = presentRenderPass_;
        beginInfo.framebuffer = swapchain_->framebuffer(token.imageIndex);
        beginInfo.renderArea.offset = {0, 0};
        beginInfo.renderArea.extent = swapchain_->extent();
        beginInfo.clearValueCount = 1;
        beginInfo.pClearValues = &value;

        vkCmdBeginRenderPass(token.commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
        return;
    }

    const auto cmdBeginRendering = context_->deviceFunctions().cmdBeginRendering;

    if (cmdBeginRendering == nullptr)
    {
        throw std::logic_error("Dynamic present requires vkCmdBeginRendering");
    }

    // 显示完成进入附件布局的转换
    recordPresentImageTransition(token, PresentImageTransition::ToColorAttachment);

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = swapchain_->imageView(token.imageIndex);
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;     // 开始时清空附件，丢弃旧内容
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;   // 保存结果供随后呈现
    color.clearValue = value;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = swapchain_->extent();
    renderingInfo.layerCount = 1;
    renderingInfo.viewMask = 0;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &color;

    cmdBeginRendering(token.commandBuffer, &renderingInfo);
}

void Renderer::endPresentPass(const FrameToken &token)
{
    if (!useDynamicRenderingForPresent())
    {
        vkCmdEndRenderPass(token.commandBuffer);
        return;
    }

    const auto cmdEndRendering = context_->deviceFunctions().cmdEndRendering;
    if (cmdEndRendering == nullptr)
    {
        throw std::logic_error("Dynamic present requires vkCmdEndRendering");
    }

    cmdEndRendering(token.commandBuffer);
    // 完成附件写入后，显式转换为可呈现布局
    recordPresentImageTransition(token, PresentImageTransition::ToPresent);
}

void Renderer::createHdrFramebuffers()
{
    if (context_ == nullptr || sceneRenderPass_ == VK_NULL_HANDLE)
    {
        throw std::logic_error("HDR framebuffers require a scene render pass");
    }

    const bool useMsaa = sceneSamples_ != VK_SAMPLE_COUNT_1_BIT;

    for (HdrFrameTarget& target : hdrTargets_)
    {
        std::array<VkImageView, 3> attachments;
        attachments[0] = useMsaa ? target.msaaColor.view() : target.hdrColor.view();
        attachments[1] = target.depth.view();
        if (useMsaa)
        {
            attachments[2] = target.hdrColor.view();
        }

        if (attachments[0] == VK_NULL_HANDLE || attachments[1] == VK_NULL_HANDLE ||
            (useMsaa && attachments[2] == VK_NULL_HANDLE))
        {
            throw std::logic_error("HDR framebuffer requires valid attachments views");
        }

        const VkExtent3D extent = target.hdrColor.extent();

        VkFramebufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = sceneRenderPass_;
        createInfo.attachmentCount = useMsaa ? 3u : 2u;
        createInfo.pAttachments = attachments.data();
        createInfo.width = extent.width;
        createInfo.height = extent.height;
        createInfo.layers = 1;
        VK_CHECK(vkCreateFramebuffer(
            context_->device(),
            &createInfo,
            nullptr,
            &target.framebuffer));
    }
    std::cout
        << "HDR framebuffers prepared: "
        << hdrTargets_.size()
        << ", attachments per framebuffer="
        << (useMsaa ? 3 : 2)
        << '\n';
}

void Renderer::destroyHdrTargets() noexcept // 释放图像
{
    for (HdrFrameTarget& target : hdrTargets_)
    {
        if (target.framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(
                context_->device(),
                target.framebuffer,
                nullptr
            );
            target.framebuffer = VK_NULL_HANDLE;
        }
        target.depth.reset();
        target.msaaColor.reset();
        target.hdrColor.reset();
    }
}

// 创建 sampler、layout、pool 和 sets
void Renderer::createPostDescriptors()
{
    const VkDevice device = context_->device();
    SamplerDesc samplerDesc{};
    samplerDesc.magFilter = VK_FILTER_LINEAR;
    samplerDesc.minFilter = VK_FILTER_LINEAR;
    samplerDesc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.compareEnable = VK_FALSE;
    samplerDesc.anisotropyEnable = VK_FALSE;
    samplerDesc.minLod = 0.0f;
    samplerDesc.maxLod = 0.0f;
    samplerDesc.debugName = "Post-process HDR sampler";

    postSampler_ = context_->createSampler(samplerDesc);

    // 一个 binding：供 fragment shader 读取 HDR 图片
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &postDescriptorSetLayout_));

    const uint32_t count = static_cast<uint32_t>(postDescriptorSets_.size()); // 有多少个 frames
    // 每个飞行帧一个 set，每个 set 一个 combined image sampler
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = count;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = count;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &postDescriptorPool_));

    std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts{};
    layouts.fill(postDescriptorSetLayout_);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = postDescriptorPool_;
    allocInfo.descriptorSetCount = count;
    allocInfo.pSetLayouts = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, postDescriptorSets_.data()));

    writePostDescriptors();
}

// 把每个飞行帧对应的 HDR 图像 + post-process sampler，真正写进对应的 VkDescriptorSet
// 这样后处理 fragment shader 才能采样 HDR 结果
void Renderer::writePostDescriptors()
{
    for (std::size_t index = 0; index < postDescriptorSets_.size(); ++index)
    {
        const VkImageView view = hdrTargets_[index].hdrColor.view();
        if (view == VK_NULL_HANDLE || postDescriptorSets_[index] == VK_NULL_HANDLE || !postSampler_)
        {
            throw std::logic_error("Post descriptor requires an HDR image, sampler and set");
        }

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = postSampler_.get();
        imageInfo.imageView = view;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = postDescriptorSets_[index];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(context_->device(), 1, &write, 0, nullptr);
    }
}

void Renderer::destroyPostDescriptors() noexcept
{
    const VkDevice device = context_->device();

    if (postDescriptorPool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, postDescriptorPool_, nullptr);
        postDescriptorPool_ = VK_NULL_HANDLE;
    }

    postDescriptorSets_.fill(VK_NULL_HANDLE);

    if (postDescriptorSetLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, postDescriptorSetLayout_, nullptr);
        postDescriptorSetLayout_ = VK_NULL_HANDLE;
    }

    postSampler_.reset();
}

// layout 明确了后处理 shader 的两个输入接口：
// 1. descriptor: HDR 图像和 sampler
// 2. push constants: 后处理参数
void Renderer::createPostPipelineLayout()
{
    if (context_ == nullptr || postDescriptorSetLayout_ == VK_NULL_HANDLE)
    {
        throw std::logic_error("Post pipeline layout requires a descriptor set layout");
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = static_cast<uint32_t>(sizeof(PostPushConstants));

    VkPipelineLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    createInfo.pushConstantRangeCount = 1;
    createInfo.pPushConstantRanges = &pushRange;
    createInfo.setLayoutCount = 1;
    createInfo.pSetLayouts = &postDescriptorSetLayout_;

    VK_CHECK(vkCreatePipelineLayout(context_->device(), &createInfo, nullptr, &postPipelineLayout_));
}

void Renderer::createTonemapPipeline()
{
    if (context_ == nullptr ||
        swapchain_ == nullptr ||
        swapchain_->format() == VK_FORMAT_UNDEFINED ||
        postPipelineLayout_ == VK_NULL_HANDLE)
    {
        throw std::logic_error("Tone mapping pipeline requires a present pass and post pipeline layout");
    }

    GraphicsPipelineConfig config{};
    config.vertShaderPath = FULLSCREEN_VERTEX_SHADER_PATH;
    config.fragShaderPath = TONEMAP_FRAGMENT_SHADER_PATH;
    config.layout = postPipelineLayout_;

    config.useDynamicRendering = useDynamicRenderingForPresent();
    config.renderPass = config.useDynamicRendering ? VK_NULL_HANDLE : presentRenderPass_;
    config.colorAttachmentFormat = swapchain_->format();

    config.samples = VK_SAMPLE_COUNT_1_BIT;
    config.useVertexInput = false;
    config.cullMode = VK_CULL_MODE_NONE;
    config.depthTest = false;
    config.depthWrite = false;

    tonemapPipeline_ = createGraphicsPipelineFromConfig(config);

    std::cout << "Tone mapping pipeline prepared!\n";
}

void Renderer::recreateHdrTargets()
{
    if (!initialized_ || context_ == nullptr || swapchain_ == nullptr || hasActiveFrame_)
    {
        throw std::logic_error("HDR resize requires an initialized renderer outside na activate");
    }

    // 调用方必须已经等待 gpu 空闲
    const VkFormat oldHdrFormat = hdrFormat_;
    const VkFormat oldDepthFormat = sceneDepthFormat_;
    const VkSampleCountFlagBits oldSamples = sceneSamples_;

    destroyHdrTargets();
    createHdrTargets();

    if (hdrFormat_ != oldHdrFormat || sceneDepthFormat_ != oldDepthFormat || sceneSamples_ != oldSamples)
    {
        throw std::runtime_error("HDR configuration changed; restart required");
    }

    if (!useDynamicRenderingForScene())
    {
        createHdrFramebuffers();
    }
    // 原来的 post set 指向旧 imageView。图像重建后需要让每帧的 set 指向新 view，而且更新时不能还有 GPU 提交在使用他们
    writePostDescriptors();
}

RenderTargetPayloadEstimate Renderer::renderTargetPayloadEstimate() const noexcept
{
    RenderTargetPayloadEstimate result{};

    if (!initialized_)
    {
        return result;
    }
    result.available = true;

    const auto addImage = [&](const GpuImage& image, std::uint64_t& destination) -> bool
    {
        const auto bytes = estimateImagePayload(image);
        if (!bytes)
        {
            return false;
        }
        destination += *bytes;
        return true;
    };

    for (const HdrFrameTarget& target : hdrTargets_)
    {
        if (!addImage(target.hdrColor, result.hdrColorBytes) ||
            !addImage(target.msaaColor, result.msaaColorBytes) ||
            !addImage(target.depth, result.sceneDepthBytes))
        {
            return result;
        }
    }

    for (const ShadowTarget& target : shadowTargets_)
    {
        if (!addImage(target.depth, result.shadowDepthBytes))
        {
            return result;
        }
    }

    result.totalBytes = result.hdrColorBytes + result.msaaColorBytes + result.sceneDepthBytes + result.shadowDepthBytes;
    result.complete = true;
    return result;
}