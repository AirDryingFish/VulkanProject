#include "core/AppConfig.hpp"
#include "render/Renderer.hpp"
#include "vulkan/Swapchain.hpp"
#include "vulkan/VulkanCheck.hpp"
#include "vulkan/VulkanContext.hpp"
#include "render/DeferredMath.hpp"

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

void Renderer::createDeferredResources()
{
    if (context_ == nullptr || swapchain_ == nullptr)
    {
        throw std::logic_error("Deferred lighting requires a context and swapchain");
    }

    if (!gBufferGeometryAvailable())
    {
        for (DeferredTarget &target : deferredTargets_)
        {
            target.hdrColor.reset();
            target.sampledLayout = false;
        }
        return;
    }

    if (!gBufferPreviewSampler_ || !postSampler_ ||
        postDescriptorSetLayout_ == VK_NULL_HANDLE)
    {
        throw std::logic_error("Deferred lighting requires G-buffer and post samplers");
    }

    const VkDevice device = context_->device();
    const VkExtent2D extent = swapchain_->extent();
    const auto count = static_cast<std::uint32_t>(deferredTargets_.size());

    // Called at startup or after waiting for all GPU work during resize.
    // Build every new image first, so a failed allocation releases its
    // temporary images without partly replacing the previous target array.
    std::array<GpuImage, MAX_FRAMES_IN_FLIGHT> pending{};
    for (std::size_t slot = 0; slot < pending.size(); ++slot)
    {
        const std::string name =
            "Deferred lighting HDR[" + std::to_string(slot) + "]";

        ImageDesc desc{};
        desc.extent = {extent.width, extent.height, 1};
        desc.format = hdrFormat_;
        desc.samples = VK_SAMPLE_COUNT_1_BIT;
        desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                     VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.mipLevels = 1;
        desc.arrayLayers = 1;
        desc.tiling = VK_IMAGE_TILING_OPTIMAL;
        desc.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        desc.debugName = name.c_str();

        pending[slot] = context_->createImage(desc);
        pending[slot].setView(context_->createImageView(
            pending[slot].get(), hdrFormat_, 1, VK_IMAGE_ASPECT_COLOR_BIT));
    }

    for (std::size_t slot = 0; slot < deferredTargets_.size(); ++slot)
    {
        DeferredTarget &target = deferredTargets_[slot];
        target.hdrColor = std::move(pending[slot]);
        target.sampledLayout = false;

        // Buffer size does not depend on the framebuffer extent.
        if (!target.uniforms)
        {
            BufferDesc desc{};
            desc.size = sizeof(DeferredFrameUniforms);
            desc.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            desc.requiredMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            desc.debugName = "Deferred frame uniforms";
            target.uniforms = context_->createBuffer(desc);
            VK_CHECK(target.uniforms.map(&target.mapped));
        }
    }

    createTileBuffers();

    if (deferredSetLayout_ == VK_NULL_HANDLE)
    {
        std::array<VkDescriptorSetLayoutBinding, 8> bindings{};
        for (std::uint32_t i = 0; i < bindings.size(); ++i)
        {
            bindings[i].binding = i;
            bindings[i].descriptorType = i < 5 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : (i == 5 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            if (i >= 5)
            {
                bindings[i].stageFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
            }
        }

        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = static_cast<std::uint32_t>(bindings.size());
        info.pBindings = bindings.data();
        VK_CHECK(vkCreateDescriptorSetLayout(
            device, &info, nullptr, &deferredSetLayout_));
    }

    if (deferredPool_ == VK_NULL_HANDLE)
    {
        const std::array<VkDescriptorPoolSize, 3> poolSizes{
            {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, count},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count * 6},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, count * 2}
        }};

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = count * 2;
        poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        VK_CHECK(vkCreateDescriptorPool(
            device, &poolInfo, nullptr, &deferredPool_));

        std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts{};
        std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> sets{};
        layouts.fill(deferredSetLayout_);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = deferredPool_;
        allocInfo.descriptorSetCount = count;
        allocInfo.pSetLayouts = layouts.data();
        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, sets.data()));
        for (std::size_t slot = 0; slot < sets.size(); ++slot)
        {
            deferredTargets_[slot].inputSet = sets[slot];
        }

        layouts.fill(postDescriptorSetLayout_);
        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, sets.data()));
        for (std::size_t slot = 0; slot < sets.size(); ++slot)
        {
            deferredTargets_[slot].postSet = sets[slot];
        }
    }

    for (std::size_t slot = 0; slot < deferredTargets_.size(); ++slot)
    {
        const DeferredTarget &target = deferredTargets_[slot];
        const GBufferTarget &gBuffer = gBufferTargets_[slot];
        // 保存 6 份图片引用信息，每份包含 sampler imageview
        std::array<VkDescriptorImageInfo, 6> images{};
        // 保存 7 条 descriptor 更新指令，每条说明 “把什么资源写到哪个 set 的哪个 binding”
        // 0: Normal, 引用 images[0]
        // 1: Base color, 引用 images[1]
        // 2: Material, 引用 images[2]
        // 3: Emissive, 引用 images[3]
        // 4: Depth, 引用 images[4]
        // 5: Deferred UBO, 单独的 bufferInfo
        // 6: Lighting HDR, 引用 images[5]
        std::array<VkWriteDescriptorSet, 9> writes{};

        for (std::uint32_t i = 0; i < 5; ++i)
        {
            const bool depth = i == 4;
            images[i].sampler = gBufferPreviewSampler_.get();
            images[i].imageView = depth
                                      ? gBuffer.depth.view()
                                      : gBuffer.colors[i].view();
            images[i].imageLayout = depth
                                        ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = target.inputSet;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType =
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &images[i];
        }

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = target.uniforms.get();
        bufferInfo.range = sizeof(DeferredFrameUniforms);

        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = target.inputSet;
        writes[5].dstBinding = 5;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[5].pBufferInfo = &bufferInfo;

        // post
        images[5].sampler = postSampler_.get();
        images[5].imageView = target.hdrColor.view();
        images[5].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = target.postSet;
        writes[6].dstBinding = 0;
        writes[6].descriptorCount = 1;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[6].pImageInfo = &images[5];

        const std::array<VkDescriptorBufferInfo, 2> tileBuffers{{
            {target.tileHeaders.get(), 0, target.tileHeaders.size()},
            {target.tileIndices.get(), 0, target.tileIndices.size()}
        }};
        for (std::uint32_t i = 0; i < tileBuffers.size(); ++i)
        {
            auto& write = writes[i + 7];
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = target.inputSet;
            write.dstBinding = 6 + i;

            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = &tileBuffers[i];
        }

        vkUpdateDescriptorSets(
            device,
            static_cast<std::uint32_t>(writes.size()),
            writes.data(),
            0,
            nullptr);
    }

    if (deferredPipelineLayout_ == VK_NULL_HANDLE)
    {
        const std::array<VkDescriptorSetLayout, 3> layouts{
            frameDescriptorSetLayout_,
            deferredSetLayout_,
            skyboxDescriptorSetLayout_};

        VkPushConstantRange push{};
        push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        push.size = static_cast<std::uint32_t>(sizeof(glm::vec4));

        VkPipelineLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        info.setLayoutCount = static_cast<std::uint32_t>(layouts.size());
        info.pSetLayouts = layouts.data();
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = &push;
        VK_CHECK(vkCreatePipelineLayout(
            device, &info, nullptr, &deferredPipelineLayout_));
    }

    if (deferredPipeline_ == VK_NULL_HANDLE)
    {
        GraphicsPipelineConfig config{};
        config.vertShaderPath = FULLSCREEN_VERTEX_SHADER_PATH;
        config.fragShaderPath = DEFERRED_LIGHTING_FRAGMENT_SHADER_PATH;
        config.layout = deferredPipelineLayout_;
        config.useDynamicRendering = true;
        config.colorAttachmentFormats = {hdrFormat_};
        config.samples = VK_SAMPLE_COUNT_1_BIT;
        config.useVertexInput = false;
        config.cullMode = VK_CULL_MODE_NONE;
        config.depthTest = false;
        config.depthWrite = false;
        deferredPipeline_ = createGraphicsPipelineFromConfig(config);
    }

    createTilePipeline();
}

void Renderer::recordDeferredLightingPass(
    const FrameToken &token,
    const RenderFrameData &data)
{
    DeferredTarget &target = deferredTargets_.at(token.frameIndex);
    const VkExtent2D extent = swapchain_->extent();
    const auto &functions = context_->deviceFunctions();


    const auto mode = frames_.at(token.frameIndex).recordedTimingConfig.lightCullingMode;
    DeferredFrameUniforms uniforms = data.deferredUniforms;
    uniforms.tileParams = glm::uvec4(deferred::tileSize, deferred::maxLightsPerTile, static_cast<std::uint32_t>(mode), 0u);

    // beginFrame has already waited for this frame slot's fence.
    std::memcpy(target.mapped, &uniforms, sizeof(uniforms));
    VK_CHECK(target.uniforms.flush(0, sizeof(uniforms)));
    if (mode == LightCullingMode::Tiled)
    {
        recordTileLightLists(token, data);
    }

    const auto transition = [&](bool toAttachment)
    {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = target.hdrColor.get();
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        if (toAttachment)
        {
            barrier.oldLayout = target.sampledLayout
                                    ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                    : VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            if (target.sampledLayout)
            {
                barrier.srcStageMask =
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                barrier.srcAccessMask =
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            }
            barrier.dstStageMask =
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        }
        else
        {
            barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcStageMask =
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        }

        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        functions.cmdPipelineBarrier2(token.commandBuffer, &dependency);
    };

    transition(true);

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = target.hdrColor.view();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = extent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    functions.cmdBeginRendering(token.commandBuffer, &rendering);

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(token.commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = extent;
    vkCmdSetScissor(token.commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(
        token.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, deferredPipeline_);

    // 这里决定 inputSet 写入 set = 1
    const std::array<VkDescriptorSet, 3> sets{
        data.frameDescriptorSet,
        target.inputSet,
        data.skyboxDescriptorSet};

    vkCmdBindDescriptorSets(
        token.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        deferredPipelineLayout_,
        0,
        static_cast<std::uint32_t>(sets.size()),
        sets.data(),
        0,
        nullptr);

    vkCmdPushConstants(
        token.commandBuffer,
        deferredPipelineLayout_,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        static_cast<std::uint32_t>(sizeof(glm::vec4)),
        &data.clearColor);

    vkCmdDraw(token.commandBuffer, 3, 1, 0, 0);
    functions.cmdEndRendering(token.commandBuffer);

    transition(false);
    target.sampledLayout = true;
}

void Renderer::destroyDeferredResources() noexcept
{
    destroyTileResources();
    const VkDevice device = context_->device();
    if (deferredPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, deferredPipeline_, nullptr);
        deferredPipeline_ = VK_NULL_HANDLE;
    }
    if (deferredPipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, deferredPipelineLayout_, nullptr);
        deferredPipelineLayout_ = VK_NULL_HANDLE;
    }
    if (deferredPool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, deferredPool_, nullptr);
        deferredPool_ = VK_NULL_HANDLE;
    }
    if (deferredSetLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, deferredSetLayout_, nullptr);
        deferredSetLayout_ = VK_NULL_HANDLE;
    }

    for (DeferredTarget &target : deferredTargets_)
    {
        target.hdrColor.reset();
        target.uniforms.reset();
        target.mapped = nullptr;
        target.inputSet = VK_NULL_HANDLE;
        target.postSet = VK_NULL_HANDLE;
        target.sampledLayout = false;
    }
}

bool Renderer::deferredLightingAvailable() const noexcept
{
    if (!gBufferGeometryAvailable() || deferredPipeline_ == VK_NULL_HANDLE)
    {
        return false;
    }
    for (const DeferredTarget &target : deferredTargets_)
    {
        if (!target.tileHeaders || !target.tileIndices || !target.hdrColor || target.hdrColor.view() == VK_NULL_HANDLE ||
            !target.uniforms || target.mapped == nullptr ||
            target.inputSet == VK_NULL_HANDLE || target.postSet == VK_NULL_HANDLE)
        {
            return false;
        }
    }
    return true;
}
