#include "TriangleApplication.hpp"

#include <array>
#include <stdexcept>

namespace
{
    constexpr uint32_t irradianceDimension = 32;
    constexpr VkFormat irradianceFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    // constexpr uint32_t prefilterWidth = 128;
    // constexpr uint32_t prefilterHeight = 128;
    constexpr uint32_t prefilterDimension = 128;
    constexpr VkFormat prefilterFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    struct PrefilterPushConstants
    {
        glm::mat4 viewProjection;
        float roughness;
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
        if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create irradiance face image view!");
        }
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
        if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create irradiance face image view!");
        }
        return imageView;
    }
}

void TriangleApplication::createIrradianceResources()
{
    irradianceImage = createImage(
        irradianceDimension,
        irradianceDimension,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        irradianceFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        6,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);

    irradianceImage.imageView = createImageView(
        irradianceImage.image,
        irradianceFormat,
        1,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_VIEW_TYPE_CUBE,
        6);

    mainDeletionQueue.pushFunction([this, image = irradianceImage]() mutable
                                   { destroyImage(image); });

    for (uint32_t face = 0; face < irradianceFaceImageViews.size(); face++)
    {
        irradianceFaceImageViews[face] = createIrradianceFaceImageView(device, irradianceImage.image, face);
        mainDeletionQueue.pushFunction([this, imageView = irradianceFaceImageViews[face]]()
                                       { vkDestroyImageView(device, imageView, nullptr); });
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &irradianceSampler) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create irradiance sampler!");
    }
    mainDeletionQueue.pushFunction([this, sampler = irradianceSampler]()
                                   { vkDestroySampler(device, sampler, nullptr); });

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = irradianceFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &irradianceRenderPass) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create irradiance render pass!");
    }
    mainDeletionQueue.pushFunction([this, renderPass = irradianceRenderPass]()
                                   { vkDestroyRenderPass(device, renderPass, nullptr); });

    for (uint32_t face = 0; face < irradianceFramebuffers.size(); face++)
    {
        VkImageView attachment = irradianceFaceImageViews[face];
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = irradianceRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &attachment;
        framebufferInfo.width = irradianceDimension;
        framebufferInfo.height = irradianceDimension;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &irradianceFramebuffers[face]) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create irradiance framebuffer!");
        }
        mainDeletionQueue.pushFunction([this, framebuffer = irradianceFramebuffers[face]]()
                                       { vkDestroyFramebuffer(device, framebuffer, nullptr); });
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

    if (vkCreateDescriptorSetLayout(device, &descriptorLayoutInfo, nullptr, &irradianceDescriptorSetLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create irradiance descriptor set layout!");
    }
    mainDeletionQueue.pushFunction([this, layout = irradianceDescriptorSetLayout]()
                                   { vkDestroyDescriptorSetLayout(device, layout, nullptr); });

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = 1;
    descriptorPoolInfo.pPoolSizes = &poolSize;
    descriptorPoolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &irradianceDescriptorPool) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create irradiance descriptor pool!");
    }
    mainDeletionQueue.pushFunction([this, pool = irradianceDescriptorPool]()
                                   { vkDestroyDescriptorPool(device, pool, nullptr); });

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = irradianceDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &irradianceDescriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &irradianceDescriptorSet) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to allocate irradiance descriptor set!");
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = skyboxSampler;
    imageInfo.imageView = skyboxImage.imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = irradianceDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PrefilterPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &irradianceDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &irradiancePipelineLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create irradiance pipeline layout!");
    }
    mainDeletionQueue.pushFunction([this, layout = irradiancePipelineLayout]()
                                   { vkDestroyPipelineLayout(device, layout, nullptr); });

    auto vertShaderCode = readFile(IRRADIANCE_VERTEX_SHADER_PATH);
    auto fragShaderCode = readFile(IRRADIANCE_FRAGMENT_SHADER_PATH);

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertShaderStageInfo, fragShaderStageInfo};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
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
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = irradiancePipelineLayout;
    pipelineInfo.renderPass = irradianceRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &irradiancePipeline) != VK_SUCCESS)
    {
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        throw std::runtime_error("failed to create irradiance pipeline!");
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);

    mainDeletionQueue.pushFunction([this, pipeline = irradiancePipeline]()
                                   { vkDestroyPipeline(device, pipeline, nullptr); });

    transitionImageLayout(
        irradianceImage.image,
        irradianceFormat,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        1,
        6);

    renderIrradianceCubemap();

    transitionImageLayout(
        irradianceImage.image,
        irradianceFormat,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        1,
        6);
}

void TriangleApplication::renderIrradianceCubemap()
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

    immediateSubmit([&](VkCommandBuffer commandBuffer)
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

        for (uint32_t face = 0; face < irradianceFramebuffers.size(); face++)
        {
            VkRenderPassBeginInfo renderPassInfo{};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = irradianceRenderPass;
            renderPassInfo.framebuffer = irradianceFramebuffers[face];
            renderPassInfo.renderArea.offset = {0, 0};
            renderPassInfo.renderArea.extent = {irradianceDimension, irradianceDimension};
            renderPassInfo.clearValueCount = 1;
            renderPassInfo.pClearValues = &clearValue;

            vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, irradiancePipeline);
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                irradiancePipelineLayout,
                0,
                1,
                &irradianceDescriptorSet,
                0,
                nullptr);

            const glm::mat4 viewProjection = captureProjection * captureViews[face];
            vkCmdPushConstants(
                commandBuffer,
                irradiancePipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(glm::mat4),
                &viewProjection);

            vkCmdDraw(commandBuffer, 36, 1, 0, 0);
            vkCmdEndRenderPass(commandBuffer);
        } });
}

void TriangleApplication::createPrefilterResources()
{
    prefilterImage = createImage(
        prefilterDimension,
        prefilterDimension,
        prefilterMipLevels,
        VK_SAMPLE_COUNT_1_BIT,
        prefilterFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        6,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);

    prefilterImage.imageView = createImageView(
        prefilterImage.image,
        prefilterFormat,
        prefilterMipLevels,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_VIEW_TYPE_CUBE,
        6);

    mainDeletionQueue.pushFunction([this, image = prefilterImage]() mutable
                                   { destroyImage(image); });

    for (uint32_t mip = 0; mip < prefilterMipLevels; mip++)
    {
        for (uint32_t face = 0; face < prefilterFaceImageViews[mip].size(); face++)
        {
            prefilterFaceImageViews[mip][face] = createPrefilterFaceImageView(device, prefilterImage.image, mip, face);
            mainDeletionQueue.pushFunction([this, imageView = prefilterFaceImageViews[mip][face]]()
                                           { vkDestroyImageView(device, imageView, nullptr); });
        }
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = float(prefilterMipLevels - 1);

    if (vkCreateSampler(device, &samplerInfo, nullptr, &prefilterSampler) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create irradiance sampler!");
    }
    mainDeletionQueue.pushFunction([this, sampler = prefilterSampler]()
                                   { vkDestroySampler(device, sampler, nullptr); });

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = prefilterFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &prefilterRenderpass) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create irradiance render pass!");
    }
    mainDeletionQueue.pushFunction([this, renderPass = prefilterRenderpass]()
                                   { vkDestroyRenderPass(device, renderPass, nullptr); });

    for (uint32_t mip = 0; mip < prefilterMipLevels; mip++)
    {
        for (uint32_t face = 0; face < prefilterFramebuffers[mip].size(); face++)
        {
            VkImageView attachment = prefilterFaceImageViews[mip][face];
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = prefilterRenderpass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &attachment;
            framebufferInfo.width = prefilterDimension >> mip;
            framebufferInfo.height = prefilterDimension >> mip;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &prefilterFramebuffers[mip][face]) != VK_SUCCESS)
            {
                throw std::runtime_error("failed to create irradiance framebuffer!");
            }
            mainDeletionQueue.pushFunction([this, framebuffer = prefilterFramebuffers[mip][face]]()
                                           { vkDestroyFramebuffer(device, framebuffer, nullptr); });
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

    if (vkCreateDescriptorSetLayout(device, &descriptorLayoutInfo, nullptr, &prefilterDescriptorSetLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create irradiance descriptor set layout!");
    }
    mainDeletionQueue.pushFunction([this, layout = prefilterDescriptorSetLayout]()
                                   { vkDestroyDescriptorSetLayout(device, layout, nullptr); });

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = 1;
    descriptorPoolInfo.pPoolSizes = &poolSize;
    descriptorPoolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &prefilterDescriptorPool) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create irradiance descriptor pool!");
    }
    mainDeletionQueue.pushFunction([this, pool = prefilterDescriptorPool]()
                                   { vkDestroyDescriptorPool(device, pool, nullptr); });

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = prefilterDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &prefilterDescriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &prefilterDescriptorSet) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to allocate irradiance descriptor set!");
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = skyboxSampler;
    imageInfo.imageView = skyboxImage.imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = prefilterDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PrefilterPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &prefilterDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &prefilterPipelineLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create irradiance pipeline layout!");
    }
    mainDeletionQueue.pushFunction([this, layout = prefilterPipelineLayout]()
                                   { vkDestroyPipelineLayout(device, layout, nullptr); });

    auto vertShaderCode = readFile(PREFILTER_VERTEX_SHADER_PATH);
    auto fragShaderCode = readFile(PREFILTER_FRAGMENT_SHADER_PATH);

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertShaderStageInfo, fragShaderStageInfo};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
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
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = prefilterPipelineLayout;
    pipelineInfo.renderPass = prefilterRenderpass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &prefilterPipeline) != VK_SUCCESS)
    {
        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
        throw std::runtime_error("failed to create irradiance pipeline!");
    }

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);

    mainDeletionQueue.pushFunction([this, pipeline = prefilterPipeline]()
                                   { vkDestroyPipeline(device, pipeline, nullptr); });

    transitionImageLayout(
        prefilterImage.image,
        prefilterFormat,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        prefilterMipLevels,
        6);

    renderPrefilterCubemap();

    transitionImageLayout(
        prefilterImage.image,
        prefilterFormat,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        prefilterMipLevels,
        6);
}

void TriangleApplication::renderPrefilterCubemap()
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

    immediateSubmit([&](VkCommandBuffer commandBuffer)
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

                            for (uint32_t face = 0; face < prefilterFramebuffers[mip].size(); face++)
                            {
                                VkRenderPassBeginInfo renderPassInfo{};
                                renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                                renderPassInfo.renderPass = prefilterRenderpass;
                                renderPassInfo.framebuffer = prefilterFramebuffers[mip][face];
                                renderPassInfo.renderArea.offset = {0, 0};
                                renderPassInfo.renderArea.extent = {mipSize, mipSize};
                                renderPassInfo.clearValueCount = 1;
                                renderPassInfo.pClearValues = &clearValue;

                                vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
                                vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
                                vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
                                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, prefilterPipeline);
                                vkCmdBindDescriptorSets(
                                    commandBuffer,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    prefilterPipelineLayout,
                                    0,
                                    1,
                                    &prefilterDescriptorSet,
                                    0,
                                    nullptr);

                                // const glm::mat4 viewProjection = captureProjection * captureViews[face];
                                PrefilterPushConstants pushConstants{};
                                pushConstants.viewProjection = captureProjection * captureViews[face];
                                pushConstants.roughness = static_cast<float>(mip) / static_cast<float>(prefilterMipLevels - 1);
                                vkCmdPushConstants(
                                    commandBuffer,
                                    prefilterPipelineLayout,
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                    0,
                                    sizeof(PrefilterPushConstants),
                                    &pushConstants);

                                vkCmdDraw(commandBuffer, 36, 1, 0, 0);
                                vkCmdEndRenderPass(commandBuffer);
                            }
                        } });
}
