#include "Renderer.hpp"
#include "VulkanContext.hpp"
#include "Swapchain.hpp"
#include "VulkanCheck.hpp"

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

    hdrFormat_ = VK_FORMAT_UNDEFINED;
    sceneDepthFormat_ = VK_FORMAT_UNDEFINED;
    sceneSamples_ = VK_SAMPLE_COUNT_1_BIT;
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
    if (context_ == nullptr || presentRenderPass_ == VK_NULL_HANDLE || postPipelineLayout_ == VK_NULL_HANDLE)
    {
        throw std::logic_error("Tone mapping pipeline requires a present pass and post pipeline layout");
    }

    GraphicsPipelineConfig config{};
    config.vertShaderPath = FULLSCREEN_VERTEX_SHADER_PATH;
    config.fragShaderPath = TONEMAP_FRAGMENT_SHADER_PATH;
    config.layout = postPipelineLayout_;
    config.renderPass = presentRenderPass_;
    config.samples = VK_SAMPLE_COUNT_1_BIT;
    config.useVertexInput = false;
    config.cullMode = VK_CULL_MODE_NONE;
    config.depthTest = false;
    config.depthWrite = false;
    tonemapPipeline_ = createGraphicsPipelineFromConfig(config);
    std::cout << "Tone mapping pipeline prepared!\n";
}