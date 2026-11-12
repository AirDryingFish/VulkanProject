#include "render/Renderer.hpp"
#include "vulkan/context/VulkanContext.hpp"
#include "vulkan/presentation/Swapchain.hpp"
#include "vulkan/VulkanCheck.hpp"

#include <stdexcept>

// 交换链附件只在此处进入呈现 pass，并在结束后转回 PRESENT 布局。
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

void Renderer::recordPresentImageTransition(const FrameToken& token, PresentImageTransition transition)
{
    if (!initialized_ || context_ == nullptr || swapchain_ == nullptr)
    {
        throw std::logic_error("Present image transition requires an initialized renderer");
    }

    const auto cmdPipelineBarrier2 = context_->deviceFunctions().cmdPipelineBarrier2;
    if (!context_->capabilities().synchronization2Enabled || cmdPipelineBarrier2 == nullptr)
    {
        throw std::logic_error("Present image transition requires Synchronization2");
    }

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = swapchain_->image(token.imageIndex);

    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    if (transition == PresentImageTransition::ToColorAttachment)
    {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_NONE;

        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

        // 本帧会 clear 主动丢弃旧内容，这不代表可以省略 acquire 等待
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    else
    {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;

        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_NONE;

        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;

    cmdPipelineBarrier2(token.commandBuffer, &dependency);
}

void Renderer::beginPresentPass(const FrameToken& token, const VkClearValue& value)
{
    if (!useDynamicRenderingForPresent())
    {
        VkRenderPassBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        beginInfo.renderPass = presentRenderPass_;
        beginInfo.framebuffer = swapchain_->framebuffer(token.imageIndex);
        beginInfo.renderArea.offset = {0, 0};
        beginInfo.renderArea.extent = swapchain_->extent();
        beginInfo.clearValueCount = 1;
        beginInfo.pClearValues = &value;

        vkCmdBeginRenderPass(token.commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
        return;
    }

    const auto cmdBeginRendering = context_->deviceFunctions().cmdBeginRendering;

    if (cmdBeginRendering == nullptr)
    {
        throw std::logic_error("Dynamic present requires vkCmdBeginRendering");
    }

    // 显示完成进入附件布局的转换
    recordPresentImageTransition(token, PresentImageTransition::ToColorAttachment);

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = swapchain_->imageView(token.imageIndex);
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;   // 开始时清空附件，丢弃旧内容
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // 保存结果供随后呈现
    color.clearValue = value;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = swapchain_->extent();
    renderingInfo.layerCount = 1;
    renderingInfo.viewMask = 0;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &color;

    cmdBeginRendering(token.commandBuffer, &renderingInfo);
}

void Renderer::endPresentPass(const FrameToken& token)
{
    if (!useDynamicRenderingForPresent())
    {
        vkCmdEndRenderPass(token.commandBuffer);
        return;
    }

    const auto cmdEndRendering = context_->deviceFunctions().cmdEndRendering;
    if (cmdEndRendering == nullptr)
    {
        throw std::logic_error("Dynamic present requires vkCmdEndRendering");
    }

    cmdEndRendering(token.commandBuffer);
    // 完成附件写入后，显式转换为可呈现布局
    recordPresentImageTransition(token, PresentImageTransition::ToPresent);
}

bool Renderer::useDynamicRenderingForPresent() const noexcept
{
    if (context_ == nullptr)
    {
        return false;
    }
    const auto& capabilities = context_->capabilities();

    return capabilities.dynamicRenderingEnabled && capabilities.synchronization2Enabled;
}

VkRenderPass Renderer::presentRenderPass() const noexcept
{
    return presentRenderPass_;
}
