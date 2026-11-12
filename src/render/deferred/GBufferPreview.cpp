#include "core/AppConfig.hpp"
#include "render/Renderer.hpp"
#include "vulkan/context/VulkanContext.hpp"
#include "vulkan/presentation/Swapchain.hpp"
#include "vulkan/VulkanCheck.hpp"

#include <array>
#include <stdexcept>

// G-buffer 调试显示的独立采样描述符与全屏管线；不参与几何写入。
void Renderer::createGBufferPreviewResources()
{
    if (!gBufferGeometryAvailable())
    {
        return;
    }

    const VkDevice device = context_->device();
    if (!gBufferPreviewSampler_)
    {
        SamplerDesc desc{};
        desc.magFilter = VK_FILTER_NEAREST;
        desc.minFilter = VK_FILTER_NEAREST;
        desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        desc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        desc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        desc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        desc.compareEnable = VK_FALSE;
        desc.anisotropyEnable = VK_FALSE;
        desc.minLod = 0.0f;
        desc.maxLod = 0.0f;
        desc.debugName = "G-buffer preview sampler";

        gBufferPreviewSampler_ = context_->createSampler(desc);
    }

    if (gBufferPreviewSetLayout_ == VK_NULL_HANDLE)
    {
        std::array<VkDescriptorSetLayoutBinding, 5> bindings{};

        for (std::uint32_t i = 0; i < bindings.size(); ++i)
        {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = static_cast<std::uint32_t>(bindings.size());
        info.pBindings = bindings.data();

        VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &gBufferPreviewSetLayout_));
    }

    if (gBufferPreviewPool_ == VK_NULL_HANDLE)
    {
        const auto count = static_cast<std::uint32_t>(gBufferPreviewSets_.size());

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = count * 5;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = count;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &gBufferPreviewPool_));

        std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts{};
        layouts.fill(gBufferPreviewSetLayout_);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = gBufferPreviewPool_;
        allocInfo.descriptorSetCount = count;
        allocInfo.pSetLayouts = layouts.data();

        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, gBufferPreviewSets_.data()));
    }

    if (gBufferPreviewLayout_ == VK_NULL_HANDLE)
    {
        const std::array<VkDescriptorSetLayout, 2> layouts{frameDescriptorSetLayout_, gBufferPreviewSetLayout_};

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = static_cast<std::uint32_t>(sizeof(GBufferPreviewPushConstants));

        VkPipelineLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = &pushRange;
        info.setLayoutCount = static_cast<std::uint32_t>(layouts.size());
        info.pSetLayouts = layouts.data();

        VK_CHECK(vkCreatePipelineLayout(device, &info, nullptr, &gBufferPreviewLayout_));
    }

    if (gBufferPreviewPipeline_ == VK_NULL_HANDLE)
    {
        GraphicsPipelineConfig config{};
        config.vertShaderPath = FULLSCREEN_VERTEX_SHADER_PATH;
        config.fragShaderPath = GBUFFER_PREVIEW_FRAGMENT_SHADER_PATH;
        config.layout = gBufferPreviewLayout_;
        config.useDynamicRendering = useDynamicRenderingForPresent();
        config.renderPass = config.useDynamicRendering ? VK_NULL_HANDLE : presentRenderPass_;

        config.colorAttachmentFormats = {swapchain_->format()};
        config.samples = VK_SAMPLE_COUNT_1_BIT;
        config.useVertexInput = false;
        config.cullMode = VK_CULL_MODE_NONE;
        config.depthTest = false;
        config.depthWrite = false;

        gBufferPreviewPipeline_ = createGraphicsPipelineFromConfig(config);
    }

    writeGBufferPreviewDescriptors();
}

void Renderer::writeGBufferPreviewDescriptors()
{
    if (!gBufferGeometryAvailable())
    {
        return;
    }

    for (std::size_t slot = 0; slot < gBufferPreviewSets_.size(); ++slot)
    {
        if (!gBufferPreviewSampler_ || gBufferPreviewSets_[slot] == VK_NULL_HANDLE)
        {
            throw std::logic_error("G-buffer preview requires a sampler and descriptor set");
        }

        const GBufferTarget& target = gBufferTargets_.at(slot);
        std::array<VkDescriptorImageInfo, 5> images{};
        std::array<VkWriteDescriptorSet, 5> writes{};

        for (std::uint32_t i = 0; i < images.size(); ++i)
        {
            const bool depth = i == 4;
            const VkImageView view = depth ? target.depth.view() : target.colors[i].view();
            if (view == VK_NULL_HANDLE)
            {
                throw std::logic_error("G-buffer preview requires all five image views");
            }

            images[i].sampler = gBufferPreviewSampler_.get();
            images[i].imageView = view;
            images[i].imageLayout =
                depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = gBufferPreviewSets_[slot];
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &images[i];
        }

        vkUpdateDescriptorSets(context_->device(), static_cast<std::uint32_t>(writes.size()), writes.data(), 0,
                               nullptr);
    }
}

void Renderer::recordGBufferPreview(const FrameToken& token, const RenderFrameData& data)
{
    vkCmdBindPipeline(token.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gBufferPreviewPipeline_);

    // set 0: 现有 frameDescriptorSet 提供本帧投影矩阵
    // set 1: 新增 G-buffer，提供 5 张附件
    const std::array<VkDescriptorSet, 2> sets{data.frameDescriptorSet, gBufferPreviewSets_.at(token.frameIndex)};

    vkCmdBindDescriptorSets(token.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gBufferPreviewLayout_, 0,
                            static_cast<std::uint32_t>(sets.size()), sets.data(), 0, nullptr);

    GBufferPreviewPushConstants push{};
    push.params = {data.debugDepthNear, data.debugDepthFar, data.exposureEv, 0.0f};
    push.modes.x = static_cast<std::int32_t>(frames_[token.frameIndex].recordedTimingConfig.gBufferView);
    push.modes.y = static_cast<std::int32_t>(data.toneMapper);
    vkCmdPushConstants(token.commandBuffer, gBufferPreviewLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       static_cast<std::uint32_t>(sizeof(push)), &push);

    vkCmdDraw(token.commandBuffer, 3, 1, 0, 0);
}

void Renderer::destroyGBufferPreviewResources() noexcept
{
    const VkDevice device = context_->device();

    if (gBufferPreviewPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, gBufferPreviewPipeline_, nullptr);
        gBufferPreviewPipeline_ = VK_NULL_HANDLE;
    }

    if (gBufferPreviewLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, gBufferPreviewLayout_, nullptr);
        gBufferPreviewLayout_ = VK_NULL_HANDLE;
    }

    if (gBufferPreviewPool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, gBufferPreviewPool_, nullptr);
        gBufferPreviewPool_ = VK_NULL_HANDLE;
    }

    gBufferPreviewSets_.fill(VK_NULL_HANDLE);

    if (gBufferPreviewSetLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, gBufferPreviewSetLayout_, nullptr);

        gBufferPreviewSetLayout_ = VK_NULL_HANDLE;
    }

    gBufferPreviewSampler_.reset();
}
