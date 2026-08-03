#include "TriangleApplication.hpp"

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
void TriangleApplication::createRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChainImageFormat; // swap chain图像的格式
    // colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT; // 不使用多重采样
    colorAttachment.samples = msaaSamples;

    // LoadOp和storeOp决定如何处理attachment中的数据
    // LoadOp:
    // * VK_ATTACHMENT_LOAD_OP_LOAD: 保留attachment现有内容
    // * VK_ATTACHMENT_LOAD_OP_CLEAR: 在渲染开始时把attachment清空为一个常量值（clear color）
    // * VK_ATTACHMENT_LOAD_OP_DONT_CARE: 现有内容未定义，不关心attachment开始时是什么
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // StoreOp:
    // * VK_ATTACHMENT_STORE_OP_STORE: 渲染后的内容存储在内存中，稍后可以读取
    // * VK_ATTACHMENT_STORE_OP_DONT_CARE: 渲染后的内容未定义，不关心attachment结束时是什么
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;   // 不使用模板缓冲区，所以不关心它的内容
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // 不使用模板缓冲区，所以不关心它的内容

    // vulkan中的texture和framebuffer由具体特定像素格式的VkImage对象表示，内存中像素的布局可以根据对图像的操作而改变
    // * VK_IMAGE_LAYOUT_UNDEFINED: render pass 开始时不关心图像开始时的布局，渲染开始时图像中的数据会被丢弃
    // * VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: 渲染过程中图像的最佳布局
    // * VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: Render pass结束时，自动把图像转换成适合呈现到屏幕的布局。这样Render pass结束后图像就可以直接被显示器使用了。
    // initialLayout和finalLayout的转换由vulkan自动完成，我们只需要告诉vulkan我们不关心初始布局是什么，渲染结束后要把它转换成适合显示的布局就行了。
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // 这张 image 即将被呈现到屏幕
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // 这张 image 还要继续被当作颜色附件使用

    // 一个 render pass 里可以有多个 subpass，每个 subpass 是一个渲染步骤，后面的 subpass 可以读取前面 subpass 的输出结果。
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;                                    // 指定我们要使用第一个attachment
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // subpass使用attachment时的布局

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = findDepthFormat();
    // depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.samples = msaaSamples;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription colorAttachmentResolve{};
    colorAttachmentResolve.format = swapChainImageFormat;
    colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentResolveRef{};
    colorAttachmentResolveRef.attachment = 2;
    colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    // pColorAttachments是关键部分。传入的是一个数组，数组的索引直接对应fragment shader里的layout(location = 0) out vec4 outColor;中的location。
    // 比如如果传入两个attachment，索引分别是0和1，那么fragment shader里就可以有两个输出变量，分别是layout(location = 0) out vec4 outColor0;和layout(location = 1) out vec4 outColor1;。
    // 如果fragment shader里只有一个输出变量，直接写layout(location = 0) out vec4 outColor;就行了，vulkan会自动把它绑定到索引为0的attachment上。
    // 这就是Multiple Render Targets (MRT)技术，可以在一个subpass里同时输出到多张图像上，性能更好。
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;
    subpass.pResolveAttachments = &colorAttachmentResolveRef;

    std::array<VkAttachmentDescription, 3> attachments = {colorAttachment, depthAttachment, colorAttachmentResolve};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    // Render pass开始时会做布局转换(比如把图像从“未定义”转成“可以写颜色”的布局)
    // vulkan默认在管线最开头做这个转换，但是那时候图像可能还没从swap chain中那到。往一个没准备好的图像上做布局转换，会出问题
    // 解决思路：告诉vulkan，布局转换等到真正要写颜色的阶段再做
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL; // 外部，renderpass之间的操作
    dependency.dstSubpass = 0;                   // 我们的子通道

    /*
        顶点输入 (VERTEX_INPUT)
            ↓
        顶点着色 (VERTEX_SHADER)
            ↓
        几何着色 (GEOMETRY_SHADER)
            ↓
        光栅化早期 (EARLY_FRAGMENT_TESTS)  ← 早期深度测试
            ↓
        片段着色 (FRAGMENT_SHADER)
            ↓
        光栅化晚期 (LATE_FRAGMENT_TESTS)
            ↓
        颜色附件输出 (COLOR_ATTACHMENT_OUTPUT)  ← fragment 写到 framebuffer
            ↓
        传输 (TRANSFER)  ← 单独的阶段, vkCmdCopyImage 等在这运行
            ↓
        计算着色 (COMPUTE_SHADER)
    */

    // src 在 <某stage做的某种access> 完成， dst端的 <某stage做的某种access> 才能开始
    // => "上一次的 color/depth 写入(包括 LATE 测试阶段)必须真正完成、cache 都 flush 干净,这一次的 color/depth 写入(从 EARLY 测试阶段开始)才能动手。"
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
    mainDeletionQueue.pushFunction([this, capturedRenderPass = renderPass]() mutable{
        vkDestroyRenderPass(device, capturedRenderPass, nullptr);
    });
}

std::vector<char> TriangleApplication::readFile(const std::string &filename)
{
    // ate: 从文件末尾开始读取，这样就可以直接得到文件大小，不需要先读一遍来计算大小了
    // binary: 以二进制模式打开文件，SPIR-V 是二进制数据。如果用文本模式，Windows 会把 \r\n 自动转成 \n，破坏二进制内容。
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        throw std::runtime_error("failed to open file!");
    }

    // 光标位置 = 文件字节数
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);

    file.close();
    return buffer;
}

VkShaderModule TriangleApplication::createShaderModule(const std::vector<char> &code)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    // SPIR-V的字节码大小(字节数)
    createInfo.codeSize = code.size();
    // 指向SPIR-V字节码的指针。SPIR-V字节码是一个uint32_t数组，所以需要把char*转换成uint32_t*。
    // 因为SPIR-V字节码的大小必须是4的倍数，所以code.size()一定是4的倍数，reinterpret_cast是安全的。
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule));
    return shaderModule;
}

void TriangleApplication::createDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    // binding 0上只有1个descriptor
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 9> bindings{};
    bindings[0] = uboLayoutBinding;
    for (uint32_t binding = 1; binding < static_cast<uint32_t>(bindings.size()); binding++)
    {
        bindings[binding].binding = binding;
        bindings[binding].descriptorCount = 1;
        bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[binding].pImmutableSamplers = nullptr;
        bindings[binding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout));
    mainDeletionQueue.pushFunction([this, layout = descriptorSetLayout]() mutable {
        vkDestroyDescriptorSetLayout(device, layout, nullptr);
    });
}

void TriangleApplication::createGraphicsPipeline()
{
    GraphicsPipelineConfig config{};
    config.vertShaderPath = MAIN_VERTEX_SHADER_PATH;
    config.fragShaderPath = MAIN_FRAGMENT_SHADER_PATH;
    config.useVertexInput = true;
    config.cullMode = VK_CULL_MODE_BACK_BIT;
    config.depthTest = true;
    config.depthWrite = true;
    config.depthCompareOp = VK_COMPARE_OP_LESS;

    graphicsPipeline = createGraphicsPipelineFromConfig(config);
    mainDeletionQueue.pushFunction([this, pipeline = graphicsPipeline]() mutable {
        vkDestroyPipeline(device, pipeline, nullptr);
    });
}

void TriangleApplication::createSkyboxPipeline()
{
    GraphicsPipelineConfig config{};
    config.vertShaderPath = SKYBOX_VERTEX_SHADER_PATH;
    config.fragShaderPath = SKYBOX_FRAGMENT_SHADER_PATH;
    config.useVertexInput = false;
    config.cullMode = VK_CULL_MODE_NONE;
    config.depthTest = true;
    config.depthWrite = false;
    config.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    skyboxPipeline = createGraphicsPipelineFromConfig(config);
    mainDeletionQueue.pushFunction([this, pipeline = skyboxPipeline]() mutable {
        vkDestroyPipeline(device, pipeline, nullptr);
    });
}

VkPipeline TriangleApplication::createGraphicsPipelineFromConfig(const GraphicsPipelineConfig &config)
{
    auto vertShaderCode = readFile(config.vertShaderPath);
    auto fragShaderCode = readFile(config.fragShaderPath);

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
    multisampling.rasterizationSamples = msaaSamples;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    if (pipelineLayout == VK_NULL_HANDLE)
    {
        VkPushConstantRange modelPushConstant{};
        modelPushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        modelPushConstant.offset = 0;
        modelPushConstant.size = sizeof(glm::mat4);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &modelPushConstant;

        VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

        mainDeletionQueue.pushFunction([this, layout = pipelineLayout]() mutable {
            vkDestroyPipelineLayout(device, layout, nullptr);
        });
    }

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
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult pipelineResult = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
    VK_CHECK_RESULT(pipelineResult, "vkCreateGraphicsPipelines");

    return pipeline;
}

void TriangleApplication::createFramebuffers()
{
    // 1 个swap chain图像对应1个framebuffer，所以framebuffer的数量和swap chain图像的数量一样多
    swapChainFramebuffers.resize(swapChainImageViews.size());
    // 遍历 ImageView，给每个ImageView创建一个对应的framebuffer
    for (size_t i = 0; i < swapChainImageViews.size(); i++)
    {

        // std::array<VkImageView, 3> attachments = {
        //     swapChainImageViews[i],
        //     depthImageView,
        //     colorImageView};
        std::array<VkImageView, 3> attachments = {
            colorImage.imageView,
            depthImage.imageView,
            swapChainImageViews[i]};
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass; // framebuffer要兼容哪个render pass
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data(); // framebuffer要绑定哪些图像作为附件
        framebufferInfo.width = swapChainExtent.width;     // framebuffer的宽高必须和render pass里定义的视口大小一致
        framebufferInfo.height = swapChainExtent.height;   // framebuffer的宽高必须和render pass里定义的视口大小一致
        framebufferInfo.layers = 1;                        // 只有一层

        VK_CHECK(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]));

        swapChainDeletionQueue.pushFunction([this, frameBuffer = swapChainFramebuffers[i]]() {
            vkDestroyFramebuffer(device, frameBuffer, nullptr);
        });
    }
}
