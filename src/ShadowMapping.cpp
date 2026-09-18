#include "Renderer.hpp"
#include "VulkanContext.hpp"
#include "VulkanCheck.hpp"
#include "VulkanTypes.hpp"
#include "FileUtils.hpp"

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

void Renderer::createShadowPipeline()
{
    // set 0 复用每帧 descriptor (因为 shader 里面用了ubo), push constant 只需要 push model
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &frameDescriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VK_CHECK(vkCreatePipelineLayout(
        context_->device(),
        &layoutInfo,
        nullptr,
        &shadowPipelineLayout_
    ));

    const auto shaderCode = readBinaryFile(SHADOW_VERTEX_SHADER_PATH);
    auto shaderModule = context_->createShaderModule(shaderCode);

    VkPipelineShaderStageCreateInfo shaderStage{};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStage.module = shaderModule.get();
    shaderStage.pName = "main";

    // buffer 仍存放完整 vertex, 但此 pipeline 只读取 position
    const auto binding = Vertex::getBindingDescription();
    const auto positionAttribute = Vertex::getAttributeDescriptions()[0];
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &positionAttribute;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.depthBiasEnable = VK_TRUE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 0;

    const std::array<VkDynamicState, 3> dynamicStates{
        VkDynamicState::VK_DYNAMIC_STATE_VIEWPORT,
        VkDynamicState::VK_DYNAMIC_STATE_SCISSOR,
        VkDynamicState::VK_DYNAMIC_STATE_DEPTH_BIAS};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 1;
    pipelineInfo.pStages = &shaderStage;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = shadowPipelineLayout_;
    pipelineInfo.renderPass = shadowRenderPass_;
    pipelineInfo.subpass = 0; // 运行在 renderPass 的第 0 个 subpass 里

    VK_CHECK(vkCreateGraphicsPipelines(
        context_->device(),
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &shadowPipeline_
    ));

}

void Renderer::recordShadowPass(const FrameToken &token, const RenderFrameData &data)
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
    // -- 在 shadow pass 内绘制模型 --
    vkCmdBindPipeline(
        token.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        shadowPipeline_
    );

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(directionalShadowResolution);
    viewport.height = static_cast<float>(directionalShadowResolution);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0;
    vkCmdSetViewport(token.commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {directionalShadowResolution, directionalShadowResolution};
    vkCmdSetScissor(token.commandBuffer, 0, 1, &scissor);
    // 先验证原始深度：接入阴影采样后再调节偏移
    vkCmdSetDepthBias(token.commandBuffer, 0.0f, 0.0f, 0.0f);
    vkCmdBindDescriptorSets(
        token.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        shadowPipelineLayout_,
        0,
        1,
        &data.frameDescriptorSet,
        0,
        nullptr
    );
    // 依次画各个物体的深度
    if (data.objects != nullptr)
    {
        const VkDeviceSize offset = 0;
        for (const RenderObjectView& object : *data.objects)
        {
            if (object.indexCount == 0 || object.vertexBuffer == VK_NULL_HANDLE || object.indexBuffer == VK_NULL_HANDLE)
            {
                continue;
            }

            vkCmdPushConstants(
                token.commandBuffer,
                shadowPipelineLayout_,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(glm::mat4), // 只传入模型的 model 矩阵
                &object.pushConstants.model
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
                offset,
                VkIndexType::VK_INDEX_TYPE_UINT32
            );

            vkCmdDrawIndexed(
                token.commandBuffer,
                object.indexCount,
                1,
                0,
                0,
                0
            );
        }
    }
    // ----

    vkCmdEndRenderPass(token.commandBuffer);
}

void Renderer::destroyShadowTargets() noexcept
{
    const VkDevice device = context_->device();

    if (shadowPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, shadowPipeline_, nullptr);
        shadowPipeline_ = VK_NULL_HANDLE;
    }

    if (shadowPipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, shadowPipelineLayout_, nullptr);
        shadowPipelineLayout_ = VK_NULL_HANDLE;
    }


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