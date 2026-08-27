#include "Renderer.hpp"
#include "VulkanContext.hpp"
#include "Swapchain.hpp"
#include "VulkanCheck.hpp"
#include "VulkanTypes.hpp"
#include "FileUtils.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_map>

void Renderer::createDescriptorSetLayouts()
{
    // UBO, vertex和fragement shader都可见
    std::array<VkDescriptorSetLayoutBinding, pbrImageDescriptorCount + 1> sceneBindings{};
    sceneBindings[0].binding = 0;
    sceneBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sceneBindings[0].descriptorCount = 1;
    sceneBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // VkDescriptorSetLayoutBinding uboLayoutBinding{};
    // uboLayoutBinding.binding = 0;
    // uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    // // binding 0上只有1个descriptor
    // uboLayoutBinding.descriptorCount = 1;
    // uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    // uboLayoutBinding.pImmutableSamplers = nullptr;

    // std::array<VkDescriptorSetLayoutBinding, pbrImageDescriptorCount + 1> bindings{};
    // bindings[0] = uboLayoutBinding;
    for (uint32_t binding = 1; binding < static_cast<uint32_t>(sceneBindings.size()); binding++)
    {
        sceneBindings[binding].binding = binding;
        sceneBindings[binding].descriptorCount = 1;
        sceneBindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sceneBindings[binding].pImmutableSamplers = nullptr;
        sceneBindings[binding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(sceneBindings.size());
    layoutInfo.pBindings = sceneBindings.data();

    VK_CHECK(vkCreateDescriptorSetLayout(context_->device(), &layoutInfo, nullptr, &sceneDescriptorSetLayout_));
    // mainDeletionQueue.pushFunction([this, layout = descriptorSetLayout]() mutable
    //                                { vkDestroyDescriptorSetLayout(context.device(), layout, nullptr); });

    std::array<VkDescriptorSetLayoutBinding, materialImageDescriptorCount> materialBindings{};
    for (uint32_t binding = 0; binding < static_cast<uint32_t>(materialBindings.size()); binding++)
    {
        materialBindings[binding].binding = binding;
        materialBindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        materialBindings[binding].descriptorCount = 1;
        materialBindings[binding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo materialLayoutInfo{};
    materialLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    materialLayoutInfo.bindingCount = static_cast<uint32_t>(materialBindings.size());
    materialLayoutInfo.pBindings = materialBindings.data();
    VK_CHECK(vkCreateDescriptorSetLayout(context_->device(), &materialLayoutInfo, nullptr, &materialDescriptorSetLayout_));
    
    std::array<VkDescriptorSetLayoutBinding, 2> skyboxBindings{};
    skyboxBindings[0].binding = 0;
    skyboxBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    skyboxBindings[0].descriptorCount = 1;
    skyboxBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    skyboxBindings[1].binding = 1;
    skyboxBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    skyboxBindings[1].descriptorCount = 1;
    skyboxBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo skyboxLayoutInfo{};
    skyboxLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    skyboxLayoutInfo.bindingCount = static_cast<uint32_t>(skyboxBindings.size());
    skyboxLayoutInfo.pBindings = skyboxBindings.data();

    VK_CHECK(vkCreateDescriptorSetLayout(context_->device(), &skyboxLayoutInfo, nullptr, &skyboxDescriptorSetLayout_));
}

void Renderer::createPipelineLayouts()
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context_->physicalDevice(), &properties);
    if (sizeof(DrawPushConstants) > properties.limits.maxPushConstantsSize)
    {
        throw std::runtime_error("DrawPushConstants exceeds maxPushConstantSize");
    }


    VkPushConstantRange drawPushConstant{};
    drawPushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    drawPushConstant.offset = 0;
    drawPushConstant.size = sizeof(DrawPushConstants);

    VkPipelineLayoutCreateInfo sceneLayoutInfo{};
    sceneLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    const std::array<VkDescriptorSetLayout, 2> sceneLayouts{
        sceneDescriptorSetLayout_,   // set 0
        materialDescriptorSetLayout_ // set 1
    };
    sceneLayoutInfo.setLayoutCount = static_cast<uint32_t>(sceneLayouts.size());
    sceneLayoutInfo.pSetLayouts = sceneLayouts.data();
    sceneLayoutInfo.pushConstantRangeCount = 1;
    sceneLayoutInfo.pPushConstantRanges = &drawPushConstant;

    VK_CHECK(vkCreatePipelineLayout(context_->device(), &sceneLayoutInfo, nullptr, &scenePipelineLayout_));

    VkPipelineLayoutCreateInfo skyboxLayoutInfo{};
    skyboxLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    skyboxLayoutInfo.setLayoutCount = 1;
    skyboxLayoutInfo.pSetLayouts = &skyboxDescriptorSetLayout_;

    VK_CHECK(vkCreatePipelineLayout(context_->device(), &skyboxLayoutInfo, nullptr, &skyboxPipelineLayout_));
}

void Renderer::createGraphicsPipeline()
{
    GraphicsPipelineConfig config{};
    config.vertShaderPath = MAIN_VERTEX_SHADER_PATH;
    config.fragShaderPath = MAIN_FRAGMENT_SHADER_PATH;
    config.layout = scenePipelineLayout_;
    config.useVertexInput = true;
    config.cullMode = VK_CULL_MODE_BACK_BIT;
    config.depthTest = true;
    config.depthWrite = true;
    config.depthCompareOp = VK_COMPARE_OP_LESS;

    graphicsPipeline_ = createGraphicsPipelineFromConfig(config);
    // mainDeletionQueue.pushFunction([this, pipeline = graphicsPipeline]() mutable
    //                                { vkDestroyPipeline(context.device(), pipeline, nullptr); });
}

void Renderer::createSkyboxPipeline()
{
    GraphicsPipelineConfig config{};
    config.vertShaderPath = SKYBOX_VERTEX_SHADER_PATH;
    config.fragShaderPath = SKYBOX_FRAGMENT_SHADER_PATH;
    config.layout = skyboxPipelineLayout_;
    config.useVertexInput = false;
    config.cullMode = VK_CULL_MODE_NONE;
    config.depthTest = true;
    config.depthWrite = false;
    config.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    skyboxPipeline_ = createGraphicsPipelineFromConfig(config);
    // mainDeletionQueue.pushFunction([this, pipeline = skyboxPipeline]() mutable
    //                                { vkDestroyPipeline(context.device(), pipeline, nullptr); });
}

VkPipeline Renderer::createGraphicsPipelineFromConfig(const GraphicsPipelineConfig &config)
{
    if (context_ == nullptr || renderPass_ == VK_NULL_HANDLE)
    {
        throw std::logic_error("graphics pipeline requires an initialized renderer");
    }

    if (config.layout == VK_NULL_HANDLE)
    {
        throw std::invalid_argument("graphics pipeline requires an explicit pipeline layout");
    }

    auto vertShaderCode = readBinaryFile(config.vertShaderPath);
    auto fragShaderCode = readBinaryFile(config.fragShaderPath);

    UniqueShaderModule vertShaderModule = context_->createShaderModule(vertShaderCode);
    UniqueShaderModule fragShaderModule = context_->createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule.get();
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule.get();
    fragShaderStageInfo.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertShaderStageInfo, fragShaderStageInfo};

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (config.useVertexInput)
    {
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &bindingDescription;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInput.pVertexAttributeDescriptions = attributeDescriptions.data();
    }

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
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = config.cullMode;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = context_->msaaSamples();

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = config.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = config.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = config.depthCompareOp;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

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
    pipelineInfo.layout = config.layout;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult pipelineResult = vkCreateGraphicsPipelines(context_->device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    VK_CHECK_RESULT(pipelineResult, "vkCreateGraphicsPipelines");

    return pipeline;
}
