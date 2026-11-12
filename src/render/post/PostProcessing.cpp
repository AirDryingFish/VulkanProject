#include "core/AppConfig.hpp"
#include "render/Renderer.hpp"
#include "vulkan/context/VulkanContext.hpp"
#include "vulkan/presentation/Swapchain.hpp"
#include "vulkan/VulkanCheck.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

// HDR 采样、tone mapping 及目标重建后的描述符重写。
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
    if (context_ == nullptr || swapchain_ == nullptr || swapchain_->format() == VK_FORMAT_UNDEFINED ||
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
    config.colorAttachmentFormats = {swapchain_->format()};

    config.samples = VK_SAMPLE_COUNT_1_BIT;
    config.useVertexInput = false;
    config.cullMode = VK_CULL_MODE_NONE;
    config.depthTest = false;
    config.depthWrite = false;

    tonemapPipeline_ = createGraphicsPipelineFromConfig(config);

    std::cout << "Tone mapping pipeline prepared!\n";
}

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

void Renderer::recordToneMapping(const FrameToken& token, const RenderFrameData& data, bool useDeferred)
{
    vkCmdBindPipeline(token.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemapPipeline_);
    // 绑定 descriptor set
    const VkDescriptorSet postSet =
        useDeferred ? deferredTargets_.at(token.frameIndex).postSet : postDescriptorSets_.at(token.frameIndex);
    vkCmdBindDescriptorSets(token.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipelineLayout_, 0, 1, &postSet,
                            0, nullptr);

    // params.x: exposure EV
    // params.y/z: 固定亮度显示范围：log2(Y) ∈ [-8, +8]
    // modes.x: ToneMapper
    // modes.y: bypass exposure and tone mapping
    PostPushConstants post{};
    post.params.x = data.exposureEv;
    post.params.y = -8.0f;
    post.params.z = 8.0f;
    post.modes.x = static_cast<std::uint32_t>(data.toneMapper);
    post.modes.y = data.bypassToneMapping ? 1 : 0;
    post.modes.z = static_cast<std::int32_t>(data.debugView);
    vkCmdPushConstants(token.commandBuffer, postPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       static_cast<uint32_t>(sizeof(PostPushConstants)), &post);
    vkCmdDraw(token.commandBuffer, 3, 1, 0, 0);
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
    createGBufferTargets();
    createGBufferPipelines();
    createGBufferPreviewResources();
    createDeferredResources();
}
