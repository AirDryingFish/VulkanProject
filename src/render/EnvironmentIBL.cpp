#include "render/Environment.hpp"
#include "render/Renderer.hpp"
#include "vulkan/VulkanContext.hpp"
#include "vulkan/VulkanCheck.hpp"
#include "core/AppConfig.hpp"
#include "core/FileUtils.hpp"

#include <algorithm>
#include <array>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>

namespace
{
    constexpr uint32_t irradianceDimension = 32;
    constexpr VkFormat irradianceFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    // constexpr uint32_t prefilterWidth = 128;
    // constexpr uint32_t prefilterHeight = 128;
    constexpr uint32_t prefilterDimension = 128;
    constexpr VkFormat prefilterFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    constexpr uint32_t brdfLUTDimension = 512;
    constexpr VkFormat brdfLUTFormat = VK_FORMAT_R16G16_SFLOAT;

    struct PrefilterPushConstants
    {
        glm::mat4 viewProjection;
        float roughness;
    };

    struct SingleColorRenderPassDesc{
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkImageLayout initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkSubpassDependency dependency{};
    };

    struct OffscreenPipelineDesc{
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkShaderModule vertexShader = VK_NULL_HANDLE;
        VkShaderModule fragmentShader = VK_NULL_HANDLE;
        const char* debugName = "vkCreateGraphicsPipelines(offscreen)";
    };

    VkImageView createIrradianceFaceImageView(
        VkDevice device,
        VkImage image,
        uint32_t faceIndex)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = irradianceFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = faceIndex;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView = VK_NULL_HANDLE;
        VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &imageView));
        return imageView;
    }

    VkImageView createPrefilterFaceImageView(
        VkDevice device,
        VkImage image,
        uint32_t mipIndex,
        uint32_t faceIndex)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = prefilterFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = mipIndex;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = faceIndex;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView = VK_NULL_HANDLE;
        VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &imageView));
        return imageView;
    }

    VkRenderPass createSingleColorRenderPass(VkDevice device, const SingleColorRenderPassDesc& desc)
    {
        if (device == VK_NULL_HANDLE)
        {
            throw std::invalid_argument("render pass device must not be null");
        }

        if (desc.format == VK_FORMAT_UNDEFINED)
        {
            throw std::invalid_argument("render pass format must be defined");
        }

        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = desc.format;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = desc.loadOp;
        colorAttachment.storeOp = desc.storeOp;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = desc.initialLayout;
        colorAttachment.finalLayout = desc.finalLayout;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &desc.dependency;

        VkRenderPass renderPass = VK_NULL_HANDLE;
        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass))
        {
            throw std::runtime_error("failed to create single color render pass");
        }

        return renderPass;
    }

    VkFramebuffer createSingleAttachmentFramebuffer(
        VkDevice device,
        VkRenderPass renderPass,
        VkImageView attachment,
        VkExtent2D extent,
        uint32_t layers = 1
    )
    {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &attachment;
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = layers;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer))
        {
            throw std::runtime_error("failed to create single attchment framebuffer");
        }
        return framebuffer;
    }

    VkPipeline createNoVertexInputPipeline(VkDevice device, const OffscreenPipelineDesc& desc)
    {
        if (device == VK_NULL_HANDLE)
        {
            throw std::invalid_argument("pipeline device must not be null");
        }

        if (desc.layout == VK_NULL_HANDLE ||
            desc.renderPass == VK_NULL_HANDLE ||
            desc.vertexShader == VK_NULL_HANDLE ||
            desc.fragmentShader == VK_NULL_HANDLE
        )
        {
            throw std::invalid_argument("offscreen pipeline handles must not be null");
        }

        VkPipelineShaderStageCreateInfo vertexStage{};
        vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertexStage.module = desc.vertexShader;
        vertexStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragmentStage{};
        fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragmentStage.module = desc.fragmentShader;
        fragmentStage.pName = "main";

        const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertexStage, fragmentStage};
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.sampleShadingEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.blendEnable = VK_FALSE;
        blendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &blendAttachment;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // 告诉vulkan: viewport/scissor 不属于 pipeline 固定配置，之后 command buffer 会提供。
        const std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = desc.layout;
        pipelineInfo.renderPass = desc.renderPass;
        pipelineInfo.subpass = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;

        const VkResult result = vkCreateGraphicsPipelines(
            device,
            VK_NULL_HANDLE,
            1,
            &pipelineInfo,
            nullptr,
            &pipeline
        );

        if (result != VK_SUCCESS && pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
        VK_CHECK_RESULT(result, desc.debugName);

        return pipeline;
    }
}

void Environment::createIrradianceResources()
{
    ImageDesc irradianceDesc{};
    irradianceDesc.extent = {
        irradianceDimension,
        irradianceDimension,
        1
    };
    irradianceDesc.mipLevels = 1;
    irradianceDesc.arrayLayers = 6;
    irradianceDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    irradianceDesc.format = irradianceFormat;
    irradianceDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
    irradianceDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    irradianceDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    irradianceDesc.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    irradianceDesc.debugName = "irradiance cubemap";

    irradianceImage_ = context_->createImage(irradianceDesc);

    irradianceImage_.setView(context_->createImageView(
        irradianceImage_.get(),
        irradianceFormat,
        1,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_VIEW_TYPE_CUBE,
        6));

    for (uint32_t face = 0; face < irradianceFaceImageViews_.size(); face++)
    {
        irradianceFaceImageViews_[face] = createIrradianceFaceImageView(context_->device(), irradianceImage_.get(), face);

    }

    SamplerDesc samplerDesc{};
    samplerDesc.magFilter = VK_FILTER_LINEAR;
    samplerDesc.minFilter = VK_FILTER_LINEAR;
    samplerDesc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.anisotropyEnable = VK_TRUE;
    samplerDesc.maxAnisotropy = 16.0f;
    samplerDesc.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerDesc.unnormalizedCoordinates = VK_FALSE;
    samplerDesc.compareEnable = VK_FALSE;
    samplerDesc.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerDesc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerDesc.mipLodBias = 0.0f;
    samplerDesc.minLod = 0.0f;
    samplerDesc.maxLod = 0.0f;
    samplerDesc.debugName = "irradiance sampler";
    irradianceSampler_ = context_->createSampler(samplerDesc);

    VkSubpassDependency irradianceDependency{};
    irradianceDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    irradianceDependency.dstSubpass = 0;
    irradianceDependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    irradianceDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    irradianceDependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    irradianceDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    SingleColorRenderPassDesc irradianceRenderPassDesc{};
    irradianceRenderPassDesc.format = irradianceFormat;
    irradianceRenderPassDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    irradianceRenderPassDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    irradianceRenderPassDesc.dependency = irradianceDependency;

    irradianceRenderPass_ = createSingleColorRenderPass(context_->device(), irradianceRenderPassDesc);

    for (uint32_t face = 0; face < irradianceFramebuffers_.size(); face++)
    {
        irradianceFramebuffers_[face] = createSingleAttachmentFramebuffer(
            context_->device(),
            irradianceRenderPass_,
            irradianceFaceImageViews_[face],
            {
                irradianceDimension,
                irradianceDimension
            }
        );

    }

    VkDescriptorSetLayoutBinding environmentMapBinding{};
    environmentMapBinding.binding = 0;
    environmentMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    environmentMapBinding.descriptorCount = 1;
    environmentMapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{};
    descriptorLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayoutInfo.bindingCount = 1;
    descriptorLayoutInfo.pBindings = &environmentMapBinding;

    VK_CHECK(vkCreateDescriptorSetLayout(context_->device(), &descriptorLayoutInfo, nullptr, &irradianceDescriptorSetLayout_));

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = 1;
    descriptorPoolInfo.pPoolSizes = &poolSize;
    descriptorPoolInfo.maxSets = 1;

    VK_CHECK(vkCreateDescriptorPool(context_->device(), &descriptorPoolInfo, nullptr, &irradianceDescriptorPool_));

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = irradianceDescriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &irradianceDescriptorSetLayout_;

    VK_CHECK(vkAllocateDescriptorSets(context_->device(), &allocInfo, &irradianceDescriptorSet_));

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = skyboxSampler_.get();
    imageInfo.imageView = skyboxImage_.view();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = irradianceDescriptorSet_;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(context_->device(), 1, &descriptorWrite, 0, nullptr);

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PrefilterPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &irradianceDescriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK(vkCreatePipelineLayout(context_->device(), &pipelineLayoutInfo, nullptr, &irradiancePipelineLayout_));

    auto vertShaderCode = readBinaryFile(IRRADIANCE_VERTEX_SHADER_PATH);
    auto fragShaderCode = readBinaryFile(IRRADIANCE_FRAGMENT_SHADER_PATH);

    UniqueShaderModule vertShaderModule = context_->createShaderModule(vertShaderCode);
    UniqueShaderModule fragShaderModule = context_->createShaderModule(fragShaderCode);

    OffscreenPipelineDesc pipelineDesc{};
    pipelineDesc.layout = irradiancePipelineLayout_;
    pipelineDesc.renderPass = irradianceRenderPass_;
    pipelineDesc.vertexShader = vertShaderModule.get();
    pipelineDesc.fragmentShader = fragShaderModule.get();
    pipelineDesc.debugName = "vkCreateGraphicsPipelines(irradiance)";

    irradiancePipeline_ = createNoVertexInputPipeline(context_->device(), pipelineDesc);

    transitionImageLayout(
        irradianceImage_.get(),
        irradianceFormat,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        1,
        6);

    renderIrradianceCubemap();

    transitionImageLayout(
        irradianceImage_.get(),
        irradianceFormat,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        1,
        6);
}

void Environment::renderIrradianceCubemap()
{
    glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    // captureProjection[1][1] *= -1.0f;

    const std::array<glm::mat4, 6> captureViews = {
        glm::lookAt(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
    };

    renderer_->immediateSubmit([&](VkCommandBuffer commandBuffer)
                             {
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(irradianceDimension);
        viewport.height = static_cast<float>(irradianceDimension);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {irradianceDimension, irradianceDimension};

        VkClearValue clearValue{};
        clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        for (uint32_t face = 0; face < irradianceFramebuffers_.size(); face++)
        {
            VkRenderPassBeginInfo renderPassInfo{};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = irradianceRenderPass_;
            renderPassInfo.framebuffer = irradianceFramebuffers_[face];
            renderPassInfo.renderArea.offset = {0, 0};
            renderPassInfo.renderArea.extent = {irradianceDimension, irradianceDimension};
            renderPassInfo.clearValueCount = 1;
            renderPassInfo.pClearValues = &clearValue;

            vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, irradiancePipeline_);
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                irradiancePipelineLayout_,
                0,
                1,
                &irradianceDescriptorSet_,
                0,
                nullptr);

            const glm::mat4 viewProjection = captureProjection * captureViews[face];
            vkCmdPushConstants(
                commandBuffer,
                irradiancePipelineLayout_,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(glm::mat4),
                &viewProjection);

            vkCmdDraw(commandBuffer, 36, 1, 0, 0);
            vkCmdEndRenderPass(commandBuffer);
        } });
}

void Environment::createPrefilterResources()
{
    ImageDesc prefilterDesc{};
    prefilterDesc.extent = {
        prefilterDimension,
        prefilterDimension,
        1
    };
    prefilterDesc.mipLevels = prefilterMipLevels; // 表示不同 roughness
    prefilterDesc.arrayLayers = 6;
    prefilterDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    prefilterDesc.format = prefilterFormat;
    prefilterDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
    prefilterDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    prefilterDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    prefilterDesc.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    prefilterDesc.debugName = "prefilter cubemap";

    prefilterImage_ = context_->createImage(prefilterDesc);

    prefilterImage_.setView(context_->createImageView(
        prefilterImage_.get(),
        prefilterFormat,
        prefilterMipLevels,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_VIEW_TYPE_CUBE,
        6));

    for (uint32_t mip = 0; mip < prefilterMipLevels; mip++)
    {
        for (uint32_t face = 0; face < prefilterFaceImageViews_[mip].size(); face++)
        {
            prefilterFaceImageViews_[mip][face] = createPrefilterFaceImageView(context_->device(), prefilterImage_.get(), mip, face);

        }
    }

    SamplerDesc samplerDesc{};
    samplerDesc.magFilter = VK_FILTER_LINEAR;
    samplerDesc.minFilter = VK_FILTER_LINEAR;
    samplerDesc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.anisotropyEnable = VK_FALSE;
    samplerDesc.maxAnisotropy = 16.0f;
    samplerDesc.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerDesc.unnormalizedCoordinates = VK_FALSE;
    samplerDesc.compareEnable = VK_FALSE;
    samplerDesc.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerDesc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerDesc.mipLodBias = 0.0f;
    samplerDesc.minLod = 0.0f;
    samplerDesc.maxLod = float(prefilterMipLevels - 1);
    samplerDesc.debugName = "prefilter sampler";

    prefilterSampler_ = context_->createSampler(samplerDesc);

    VkSubpassDependency prefilterDependency{};
    prefilterDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    prefilterDependency.dstSubpass = 0;
    prefilterDependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    prefilterDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    prefilterDependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    prefilterDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    SingleColorRenderPassDesc prefilterRenderPassDesc{};
    prefilterRenderPassDesc.format = prefilterFormat;
    prefilterRenderPassDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    prefilterRenderPassDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    prefilterRenderPassDesc.dependency = prefilterDependency;

    prefilterRenderpass_ = createSingleColorRenderPass(context_->device(), prefilterRenderPassDesc);

    for (uint32_t mip = 0; mip < prefilterMipLevels; mip++)
    {
        for (uint32_t face = 0; face < prefilterFramebuffers_[mip].size(); face++)
        {
            const uint32_t mipDimension = prefilterDimension >> mip;
            prefilterFramebuffers_[mip][face] = createSingleAttachmentFramebuffer(
                context_->device(),
                prefilterRenderpass_,
                prefilterFaceImageViews_[mip][face],
                {
                    mipDimension,
                    mipDimension
                }
            );

        }
    }

    VkDescriptorSetLayoutBinding environmentMapBinding{};
    environmentMapBinding.binding = 0;
    environmentMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    environmentMapBinding.descriptorCount = 1;
    environmentMapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{};
    descriptorLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayoutInfo.bindingCount = 1;
    descriptorLayoutInfo.pBindings = &environmentMapBinding;

    VK_CHECK(vkCreateDescriptorSetLayout(context_->device(), &descriptorLayoutInfo, nullptr, &prefilterDescriptorSetLayout_));

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = 1;
    descriptorPoolInfo.pPoolSizes = &poolSize;
    descriptorPoolInfo.maxSets = 1;

    VK_CHECK(vkCreateDescriptorPool(context_->device(), &descriptorPoolInfo, nullptr, &prefilterDescriptorPool_));

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = prefilterDescriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &prefilterDescriptorSetLayout_;

    VK_CHECK(vkAllocateDescriptorSets(context_->device(), &allocInfo, &prefilterDescriptorSet_));

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = skyboxSampler_.get();
    imageInfo.imageView = skyboxImage_.view();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = prefilterDescriptorSet_;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(context_->device(), 1, &descriptorWrite, 0, nullptr);

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PrefilterPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &prefilterDescriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK(vkCreatePipelineLayout(context_->device(), &pipelineLayoutInfo, nullptr, &prefilterPipelineLayout_));

    auto vertShaderCode = readBinaryFile(PREFILTER_VERTEX_SHADER_PATH);
    auto fragShaderCode = readBinaryFile(PREFILTER_FRAGMENT_SHADER_PATH);

    UniqueShaderModule vertShaderModule = context_->createShaderModule(vertShaderCode);
    UniqueShaderModule fragShaderModule = context_->createShaderModule(fragShaderCode);

    OffscreenPipelineDesc pipelineDesc{};
    pipelineDesc.layout = prefilterPipelineLayout_;
    pipelineDesc.renderPass = prefilterRenderpass_;
    pipelineDesc.vertexShader = vertShaderModule.get();
    pipelineDesc.fragmentShader = fragShaderModule.get();
    pipelineDesc.debugName = "vkCreateGraphicsPipelines(prefilter)";

    prefilterPipeline_ = createNoVertexInputPipeline(context_->device(), pipelineDesc);

    transitionImageLayout(
        prefilterImage_.get(),
        prefilterFormat,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        prefilterMipLevels,
        6);

    renderPrefilterCubemap();

    transitionImageLayout(
        prefilterImage_.get(),
        prefilterFormat,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        prefilterMipLevels,
        6);
}

void Environment::renderPrefilterCubemap()
{
    glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    // captureProjection[1][1] *= -1.0f;

    const std::array<glm::mat4, 6> captureViews = {
        glm::lookAt(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
    };

    renderer_->immediateSubmit([&](VkCommandBuffer commandBuffer)
                             {
                        VkViewport viewport{};
                        viewport.x = 0.0f;
                        viewport.y = 0.0f;
                        // viewport.width = static_cast<float>(prefilterDimension);
                        // viewport.height = static_cast<float>(prefilterDimension);
                        viewport.minDepth = 0.0f;
                        viewport.maxDepth = 1.0f;

                        VkRect2D scissor{};
                        scissor.offset = {0, 0};
                        // scissor.extent = {prefilterDimension, prefilterDimension};

                        VkClearValue clearValue{};
                        clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

                        for (uint32_t mip = 0; mip < prefilterMipLevels; mip++)
                        {
                            uint32_t mipSize = prefilterDimension >> mip;
                            viewport.width = static_cast<float>(mipSize);
                            viewport.height = static_cast<float>(mipSize);
                            scissor.extent = {mipSize, mipSize};

                            for (uint32_t face = 0; face < prefilterFramebuffers_[mip].size(); face++)
                            {
                                VkRenderPassBeginInfo renderPassInfo{};
                                renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                                renderPassInfo.renderPass = prefilterRenderpass_;
                                renderPassInfo.framebuffer = prefilterFramebuffers_[mip][face];
                                renderPassInfo.renderArea.offset = {0, 0};
                                renderPassInfo.renderArea.extent = {mipSize, mipSize};
                                renderPassInfo.clearValueCount = 1;
                                renderPassInfo.pClearValues = &clearValue;

                                vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
                                vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
                                vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
                                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, prefilterPipeline_);
                                vkCmdBindDescriptorSets(
                                    commandBuffer,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    prefilterPipelineLayout_,
                                    0,
                                    1,
                                    &prefilterDescriptorSet_,
                                    0,
                                    nullptr);

                                // const glm::mat4 viewProjection = captureProjection * captureViews[face];
                                PrefilterPushConstants pushConstants{};
                                pushConstants.viewProjection = captureProjection * captureViews[face];
                                pushConstants.roughness = static_cast<float>(mip) / static_cast<float>(prefilterMipLevels - 1);
                                vkCmdPushConstants(
                                    commandBuffer,
                                    prefilterPipelineLayout_,
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                    0,
                                    sizeof(PrefilterPushConstants),
                                    &pushConstants);

                                vkCmdDraw(commandBuffer, 36, 1, 0, 0);
                                vkCmdEndRenderPass(commandBuffer);
                            }
                        } });
}

void Environment::createBRDFLUTResources()
{
    ImageDesc brdfLUTDesc{};
    brdfLUTDesc.extent = {
        brdfLUTDimension,
        brdfLUTDimension,
        1
    };
    brdfLUTDesc.mipLevels = 1;
    brdfLUTDesc.arrayLayers = 1;
    brdfLUTDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    brdfLUTDesc.format = brdfLUTFormat;
    brdfLUTDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
    brdfLUTDesc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    brdfLUTDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    brdfLUTDesc.flags = 0;
    brdfLUTDesc.debugName = "BRDF LUT";
    // 创建BRDF LUT纹理
    brdfLUTImage_ = context_->createImage(brdfLUTDesc);

    brdfLUTImage_.setView(context_->createImageView(
        brdfLUTImage_.get(),
        brdfLUTFormat,
        1));

    SamplerDesc samplerDesc{};
    samplerDesc.magFilter = VK_FILTER_LINEAR;
    samplerDesc.minFilter = VK_FILTER_LINEAR;
    samplerDesc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerDesc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.minLod = 0.0f;
    samplerDesc.maxLod = 0.0f;
    samplerDesc.debugName = "BRDF LUT sampler";

    brdfLUTSampler_ = context_->createSampler(samplerDesc);

    // 创建子通道依赖：这个子通道的执行依赖于哪些阶段、访问权限
    VkSubpassDependency brdfDependency{};
    brdfDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    brdfDependency.dstSubpass = 0;
    brdfDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    brdfDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    brdfDependency.srcAccessMask = 0;
    brdfDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    SingleColorRenderPassDesc brdfRenderpassDesc{};
    brdfRenderpassDesc.format = brdfLUTFormat;
    brdfRenderpassDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    brdfRenderpassDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    brdfRenderpassDesc.dependency = brdfDependency;

    brdfLUTRenderPass_ = createSingleColorRenderPass(
        context_->device(),
        brdfRenderpassDesc
    );

    // 创建帧缓冲：这个帧缓冲会使用哪些附件、渲染通道、尺寸
    brdfLUTFramebuffer_ = createSingleAttachmentFramebuffer(
        context_->device(),
        brdfLUTRenderPass_,
        brdfLUTImage_.view(),
        {
            brdfLUTDimension,
            brdfLUTDimension
        }
    );

    // 创建管线布局：这个管线布局会使用哪些描述符集、推送常量
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VK_CHECK(vkCreatePipelineLayout(context_->device(), &pipelineLayoutInfo, nullptr, &brdfLUTPipelineLayout_));

    // 创建着色器
    const auto vertexCode = readBinaryFile(BRDF_LUT_VERTEX_SHADER_PATH);
    const auto fragmentCode = readBinaryFile(BRDF_LUT_FRAGMENT_SHADER_PATH);
    UniqueShaderModule vertexShaderModule = context_->createShaderModule(vertexCode);
    UniqueShaderModule fragmentShaderModule = context_->createShaderModule(fragmentCode);

    OffscreenPipelineDesc pipelineDesc{};
    pipelineDesc.layout = brdfLUTPipelineLayout_;
    pipelineDesc.renderPass = brdfLUTRenderPass_;
    pipelineDesc.vertexShader = vertexShaderModule.get();
    pipelineDesc.fragmentShader = fragmentShaderModule.get();
    pipelineDesc.debugName = "vkCreateGraphicsPipelines(brdf LUT)";

    brdfLUTPipeline_ = createNoVertexInputPipeline(context_->device(), pipelineDesc);

    // 上面创建好了pipeline，接下来把fragment的结果写入附件，先设置image的格式可以作为颜色附件写入
    transitionImageLayout(
        brdfLUTImage_.get(),
        brdfLUTFormat,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        1);
    renderBRDFLUT();

    // 切换成 Shader 只读布局，方便后续 PBR Shader 采样 LUT
    transitionImageLayout(
        brdfLUTImage_.get(),
        brdfLUTFormat,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        1);
}

void Environment::renderBRDFLUT()
{
    renderer_->immediateSubmit(
        [&](VkCommandBuffer commandBuffer)
        {
            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(brdfLUTDimension);
            viewport.height = static_cast<float>(brdfLUTDimension);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = {
                brdfLUTDimension,
                brdfLUTDimension,
            };

            VkClearValue clearValue{};
            clearValue.color = {
                {0.0f, 0.0f, 0.0f, 1.0f}};

            VkRenderPassBeginInfo renderPassInfo{};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = brdfLUTRenderPass_;
            renderPassInfo.framebuffer = brdfLUTFramebuffer_;
            renderPassInfo.renderArea.offset = {0, 0};
            renderPassInfo.renderArea.extent = {
                brdfLUTDimension,
                brdfLUTDimension,
            };
            renderPassInfo.clearValueCount = 1;
            renderPassInfo.pClearValues = &clearValue;

            vkCmdBeginRenderPass(
                commandBuffer,
                &renderPassInfo,
                VK_SUBPASS_CONTENTS_INLINE);

            vkCmdSetViewport(
                commandBuffer,
                0,
                1,
                &viewport);

            vkCmdSetScissor(
                commandBuffer,
                0,
                1,
                &scissor);

            vkCmdBindPipeline(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                brdfLUTPipeline_);

            vkCmdDraw(commandBuffer, 3, 1, 0, 0);
            vkCmdEndRenderPass(commandBuffer);
        }

    );
}
