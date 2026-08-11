#include "TriangleApplication.hpp"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <array>
#include <stdexcept>


void TriangleApplication::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchain.framebuffer(imageIndex);
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchain.extent();
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchain.extent().width);
    viewport.height = static_cast<float>(swapchain.extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain.extent();
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &skyboxDescriptorSets[currentFrame], 0, nullptr);
    vkCmdDraw(commandBuffer, 36, 1, 0, 0);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);

    VkDeviceSize offsets[] = {0};
    for (const SceneObject &object : sceneObjects)
    {
        if (object.indexCount == 0 || object.vertexBuffer.get() == VK_NULL_HANDLE || object.indexBuffer.get() == VK_NULL_HANDLE)
        {
            continue;
        }

        const glm::mat4 model = getObjectMatrix(object);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &model);

        VkBuffer vertexBuffers[] = {object.vertexBuffer.get()};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, object.indexBuffer.get(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, object.indexCount, 1, 0, 0, 0);
    }

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    VK_CHECK(vkEndCommandBuffer(commandBuffer));
}

void TriangleApplication::immediateSubmit(std::function<void(VkCommandBuffer cmd)> &&function)
{

    // 确认上一次使用的UploadContext已经完成
    VK_CHECK(vkWaitForFences(context.device(), 1, &uploadContext.fence, VK_TRUE, UINT64_MAX));

    // Fence必须回到 unsignaled 才能交给下一次 submit
    VK_CHECK(vkResetFences(context.device(), 1, &uploadContext.fence));

    // Fence 已经证明上一轮 command buffer 不再 pending，因此可以安全reset command pool
    VK_CHECK(vkResetCommandPool(context.device(), uploadContext.commandPool, 0));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(uploadContext.commandBuffer, &beginInfo));

    function(uploadContext.commandBuffer);

    VK_CHECK(vkEndCommandBuffer(uploadContext.commandBuffer));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &uploadContext.commandBuffer;

    VK_CHECK(vkQueueSubmit(context.graphicsQueue(), 1, &submitInfo, uploadContext.fence));

    VK_CHECK(vkWaitForFences(context.device(), 1, &uploadContext.fence, VK_TRUE, UINT64_MAX));

}

void TriangleApplication::createUploadContext()
{
    QueueFamilyIndices indices = context.queueFamilies();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

    VK_CHECK(vkCreateCommandPool(context.device(), &poolInfo, nullptr, &uploadContext.commandPool));

    const VkCommandPool commandPool = uploadContext.commandPool;
    mainDeletionQueue.pushFunction([this, commandPool]() noexcept {
        vkDestroyCommandPool(context.device(), commandPool, nullptr);
    });

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = uploadContext.commandPool;
    allocInfo.commandBufferCount = 1;

    VK_CHECK(vkAllocateCommandBuffers(context.device(), &allocInfo, &uploadContext.commandBuffer));

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VK_CHECK(vkCreateFence(context.device(), &fenceInfo, nullptr, &uploadContext.fence));

    const VkFence fence = uploadContext.fence;
    mainDeletionQueue.pushFunction([this, fence]() noexcept {
        vkDestroyFence(context.device(), fence, nullptr);
    });
}

void TriangleApplication::createFrameContexts()
{
    QueueFamilyIndices indices = context.queueFamilies();
    
    for (FrameContext& frame : frames)
    {
        // 创建 frame.commandPool

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        // TRANSIENT_BIT 表示该 command pool 中的 command buffer 会频繁分配和释放
        // 而不是 RESET_COMMAND_BUFFER_BIT 表示该 command pool 中的 command buffer 可以单独 reset
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

        VK_CHECK(vkCreateCommandPool(context.device(), &poolInfo, nullptr, &frame.commandPool));

        const auto commandPool = frame.commandPool;
        mainDeletionQueue.pushFunction([this, commandPool]() {
            vkDestroyCommandPool(context.device(), commandPool, nullptr);
        });

        // 从 frame.commandPool 分配 frame.commandBuffer

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = frame.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VK_CHECK(vkAllocateCommandBuffers(context.device(), &allocInfo, &frame.commandBuffer));

        // 创建 frame.imageAvailable 信号量
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VK_CHECK(vkCreateSemaphore(context.device(), &semaphoreInfo, nullptr, &frame.imageAvailable));

        const auto imageAvailable = frame.imageAvailable;
        mainDeletionQueue.pushFunction([this, imageAvailable]() {
            vkDestroySemaphore(context.device(), imageAvailable, nullptr);
        });

        // 创建带 SIGNALED_BIT 的 frame.renderFence
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VK_CHECK(vkCreateFence(context.device(), &fenceInfo, nullptr, &frame.renderFence));

        const VkFence renderFence = frame.renderFence;
        mainDeletionQueue.pushFunction([this, renderFence]() {
            vkDestroyFence(context.device(), renderFence, nullptr);
        });
    }
}
