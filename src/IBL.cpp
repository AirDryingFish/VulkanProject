#include "TriangleApplication.hpp"
#include "FileUtils.hpp"

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

        VK_CHECK_RESULT(result, desc.debugName);

        return pipeline;
    }
}

void TriangleApplication::createIrradianceResources()
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

    irradianceImage = context.createImage(irradianceDesc);

    irradianceImage.setView(context.createImageView(
        irradianceImage.get(),
        irradianceFormat,
        1,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_VIEW_TYPE_CUBE,
        6));

    for (uint32_t face = 0; face < irradianceFaceImageViews.size(); face++)
    {
        irradianceFaceImageViews[face] = createIrradianceFaceImageView(context.device(), irradianceImage.get(), face);
        mainDeletionQueue.pushFunction([this, imageView = irradianceFaceImageViews[face]]()
                                       { vkDestroyImageView(context.device(), imageView, nullptr); });
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
    irradianceSampler = context.createSampler(samplerDesc);

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

    irradianceRenderPass = createSingleColorRenderPass(context.device(), irradianceRenderPassDesc);

    const VkDevice device = context.device();
    const VkRenderPass renderPass = irradianceRenderPass;

    mainDeletionQueue.pushFunction([device, renderPass]() noexcept{
        vkDestroyRenderPass(device, renderPass, nullptr);
    });

    for (uint32_t face = 0; face < irradianceFramebuffers.size(); face++)
    {
        irradianceFramebuffers[face] = createSingleAttachmentFramebuffer(
            context.device(), 
            irradianceRenderPass, 
            irradianceFaceImageViews[face], 
            {
                irradianceDimension, 
                irradianceDimension
            }
        );
        
        VkFramebuffer framebuffer = irradianceFramebuffers[face];

        mainDeletionQueue.pushFunction([device, framebuffer]() noexcept {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        });
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

    VK_CHECK(vkCreateDescriptorSetLayout(context.device(), &descriptorLayoutInfo, nullptr, &irradianceDescriptorSetLayout));
    mainDeletionQueue.pushFunction([this, layout = irradianceDescriptorSetLayout]()
                                   { vkDestroyDescriptorSetLayout(context.device(), layout, nullptr); });

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = 1;
    descriptorPoolInfo.pPoolSizes = &poolSize;
    descriptorPoolInfo.maxSets = 1;

    VK_CHECK(vkCreateDescriptorPool(context.device(), &descriptorPoolInfo, nullptr, &irradianceDescriptorPool));
    mainDeletionQueue.pushFunction([this, pool = irradianceDescriptorPool]()
                                   { vkDestroyDescriptorPool(context.device(), pool, nullptr); });

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = irradianceDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &irradianceDescriptorSetLayout;

    VK_CHECK(vkAllocateDescriptorSets(context.device(), &allocInfo, &irradianceDescriptorSet));

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = skyboxSampler.get();
    imageInfo.imageView = skyboxImage.view();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = irradianceDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(context.device(), 1, &descriptorWrite, 0, nullptr);

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

    VK_CHECK(vkCreatePipelineLayout(context.device(), &pipelineLayoutInfo, nullptr, &irradiancePipelineLayout));
    mainDeletionQueue.pushFunction([this, layout = irradiancePipelineLayout]()
                                   { vkDestroyPipelineLayout(context.device(), layout, nullptr); });

    auto vertShaderCode = readBinaryFile(IRRADIANCE_VERTEX_SHADER_PATH);
    auto fragShaderCode = readBinaryFile(IRRADIANCE_FRAGMENT_SHADER_PATH);

    UniqueShaderModule vertShaderModule = context.createShaderModule(vertShaderCode);
    UniqueShaderModule fragShaderModule = context.createShaderModule(fragShaderCode);

    OffscreenPipelineDesc pipelineDesc{};
    pipelineDesc.layout = irradiancePipelineLayout;
    pipelineDesc.renderPass = irradianceRenderPass;
    pipelineDesc.vertexShader = vertShaderModule.get();
    pipelineDesc.fragmentShader = fragShaderModule.get();
    pipelineDesc.debugName = "vkCreateGraphicsPipelines(irradiance)";

    irradiancePipeline = createNoVertexInputPipeline(context.device(), pipelineDesc);

    mainDeletionQueue.pushFunction([this, pipeline = irradiancePipeline]()
                                   { vkDestroyPipeline(context.device(), pipeline, nullptr); });

    transitionImageLayout(
        irradianceImage.get(),
        irradianceFormat,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        1,
        6);

    renderIrradianceCubemap();

    transitionImageLayout(
        irradianceImage.get(),
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

    renderer.immediateSubmit([&](VkCommandBuffer commandBuffer)
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

    prefilterImage = context.createImage(prefilterDesc);

    prefilterImage.setView(context.createImageView(
        prefilterImage.get(),
        prefilterFormat,
        prefilterMipLevels,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_VIEW_TYPE_CUBE,
        6));

    for (uint32_t mip = 0; mip < prefilterMipLevels; mip++)
    {
        for (uint32_t face = 0; face < prefilterFaceImageViews[mip].size(); face++)
        {
            prefilterFaceImageViews[mip][face] = createPrefilterFaceImageView(context.device(), prefilterImage.get(), mip, face);
            mainDeletionQueue.pushFunction([this, imageView = prefilterFaceImageViews[mip][face]]()
                                           { vkDestroyImageView(context.device(), imageView, nullptr); });
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

    prefilterSampler = context.createSampler(samplerDesc);

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

    prefilterRenderpass = createSingleColorRenderPass(context.device(), prefilterRenderPassDesc);

    mainDeletionQueue.pushFunction([this, renderPass = prefilterRenderpass]()
                                   { vkDestroyRenderPass(context.device(), renderPass, nullptr); });

    for (uint32_t mip = 0; mip < prefilterMipLevels; mip++)
    {
        for (uint32_t face = 0; face < prefilterFramebuffers[mip].size(); face++)
        {
            const uint32_t mipDimension = prefilterDimension >> mip;
            prefilterFramebuffers[mip][face] = createSingleAttachmentFramebuffer(
                context.device(),
                prefilterRenderpass,
                prefilterFaceImageViews[mip][face],
                {
                    mipDimension,
                    mipDimension
                }
            );
            
            mainDeletionQueue.pushFunction([this, framebuffer = prefilterFramebuffers[mip][face]]()
                                           { vkDestroyFramebuffer(context.device(), framebuffer, nullptr); });
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

    VK_CHECK(vkCreateDescriptorSetLayout(context.device(), &descriptorLayoutInfo, nullptr, &prefilterDescriptorSetLayout));
    mainDeletionQueue.pushFunction([this, layout = prefilterDescriptorSetLayout]()
                                   { vkDestroyDescriptorSetLayout(context.device(), layout, nullptr); });

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = 1;
    descriptorPoolInfo.pPoolSizes = &poolSize;
    descriptorPoolInfo.maxSets = 1;

    VK_CHECK(vkCreateDescriptorPool(context.device(), &descriptorPoolInfo, nullptr, &prefilterDescriptorPool));
    mainDeletionQueue.pushFunction([this, pool = prefilterDescriptorPool]()
                                   { vkDestroyDescriptorPool(context.device(), pool, nullptr); });

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = prefilterDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &prefilterDescriptorSetLayout;

    VK_CHECK(vkAllocateDescriptorSets(context.device(), &allocInfo, &prefilterDescriptorSet));

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = skyboxSampler.get();
    imageInfo.imageView = skyboxImage.view();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = prefilterDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(context.device(), 1, &descriptorWrite, 0, nullptr);

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

    VK_CHECK(vkCreatePipelineLayout(context.device(), &pipelineLayoutInfo, nullptr, &prefilterPipelineLayout));
    mainDeletionQueue.pushFunction([this, layout = prefilterPipelineLayout]()
                                   { vkDestroyPipelineLayout(context.device(), layout, nullptr); });

    auto vertShaderCode = readBinaryFile(PREFILTER_VERTEX_SHADER_PATH);
    auto fragShaderCode = readBinaryFile(PREFILTER_FRAGMENT_SHADER_PATH);

    UniqueShaderModule vertShaderModule = context.createShaderModule(vertShaderCode);
    UniqueShaderModule fragShaderModule = context.createShaderModule(fragShaderCode);

    OffscreenPipelineDesc pipelineDesc{};
    pipelineDesc.layout = prefilterPipelineLayout;
    pipelineDesc.renderPass = prefilterRenderpass;
    pipelineDesc.vertexShader = vertShaderModule.get();
    pipelineDesc.fragmentShader = fragShaderModule.get();
    pipelineDesc.debugName = "vkCreateGraphicsPipelines(prefilter)";

    prefilterPipeline = createNoVertexInputPipeline(context.device(), pipelineDesc);
    mainDeletionQueue.pushFunction([this, pipeline = prefilterPipeline]()
                                   { vkDestroyPipeline(context.device(), pipeline, nullptr); });

    transitionImageLayout(
        prefilterImage.get(),
        prefilterFormat,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        prefilterMipLevels,
        6);

    renderPrefilterCubemap();

    transitionImageLayout(
        prefilterImage.get(),
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

    renderer.immediateSubmit([&](VkCommandBuffer commandBuffer)
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

void TriangleApplication::createBRDFLUTResources()
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
    brdfLUTImage = context.createImage(brdfLUTDesc);

    brdfLUTImage.setView(context.createImageView(
        brdfLUTImage.get(),
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

    brdfLUTSampler = context.createSampler(samplerDesc);

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

    brdfLUTRenderPass = createSingleColorRenderPass(
        context.device(),
        brdfRenderpassDesc
    );
    
    mainDeletionQueue.pushFunction([this, renderPass = brdfLUTRenderPass]()
                                   { vkDestroyRenderPass(context.device(), renderPass, nullptr); });

    // 创建帧缓冲：这个帧缓冲会使用哪些附件、渲染通道、尺寸
    brdfLUTFramebuffer = createSingleAttachmentFramebuffer(
        context.device(),
        brdfLUTRenderPass,
        brdfLUTImage.view(),
        {
            brdfLUTDimension,
            brdfLUTDimension
        }
    );
    mainDeletionQueue.pushFunction([this, framebuffer = brdfLUTFramebuffer]()
                                   { vkDestroyFramebuffer(context.device(), framebuffer, nullptr); });

    // 创建管线布局：这个管线布局会使用哪些描述符集、推送常量
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VK_CHECK(vkCreatePipelineLayout(context.device(), &pipelineLayoutInfo, nullptr, &brdfLUTPipelineLayout));
    mainDeletionQueue.pushFunction([this, layout = brdfLUTPipelineLayout]()
                                   { vkDestroyPipelineLayout(context.device(), layout, nullptr); });

    // 创建着色器
    const auto vertexCode = readBinaryFile(BRDF_LUT_VERTEX_SHADER_PATH);
    const auto fragmentCode = readBinaryFile(BRDF_LUT_FRAGMENT_SHADER_PATH);
    UniqueShaderModule vertexShaderModule = context.createShaderModule(vertexCode);
    UniqueShaderModule fragmentShaderModule = context.createShaderModule(fragmentCode);

    OffscreenPipelineDesc pipelineDesc{};
    pipelineDesc.layout = brdfLUTPipelineLayout;
    pipelineDesc.renderPass = brdfLUTRenderPass;
    pipelineDesc.vertexShader = vertexShaderModule.get();
    pipelineDesc.fragmentShader = fragmentShaderModule.get();
    pipelineDesc.debugName = "vkCreateGraphicsPipelines(brdf LUT)";

    brdfLUTPipeline = createNoVertexInputPipeline(context.device(), pipelineDesc);
    mainDeletionQueue.pushFunction([this, pipeline = brdfLUTPipeline]()
                                   { vkDestroyPipeline(context.device(), pipeline, nullptr); });

    // 上面创建好了pipeline，接下来把fragment的结果写入附件，先设置image的格式可以作为颜色附件写入
    transitionImageLayout(
        brdfLUTImage.get(),
        brdfLUTFormat,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        1);
    renderBRDFLUT();

    // 切换成 Shader 只读布局，方便后续 PBR Shader 采样 LUT
    transitionImageLayout(
        brdfLUTImage.get(),
        brdfLUTFormat,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        1);
}

void TriangleApplication::renderBRDFLUT()
{
    renderer.immediateSubmit(
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
            renderPassInfo.renderPass = brdfLUTRenderPass;
            renderPassInfo.framebuffer = brdfLUTFramebuffer;
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
                brdfLUTPipeline);

            vkCmdDraw(commandBuffer, 3, 1, 0, 0);
            vkCmdEndRenderPass(commandBuffer);
        }

    );
}
