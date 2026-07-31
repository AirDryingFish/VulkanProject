#include "TriangleApplication.hpp"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <array>
#include <stdexcept>


void TriangleApplication::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChainExtent;
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapChainExtent.width);
    viewport.height = static_cast<float>(swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapChainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &skyboxDescriptorSets[currentFrame], 0, nullptr);
    vkCmdDraw(commandBuffer, 36, 1, 0, 0);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);

    VkDeviceSize offsets[] = {0};
    for (const SceneObject &object : sceneObjects)
    {
        if (object.indexCount == 0 || object.vertexBuffer.buffer == VK_NULL_HANDLE || object.indexBuffer.buffer == VK_NULL_HANDLE)
        {
            continue;
        }

        const glm::mat4 model = getObjectMatrix(object);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &model);

        VkBuffer vertexBuffers[] = {object.vertexBuffer.buffer};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, object.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, object.indexCount, 1, 0, 0, 0);
    }

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void TriangleApplication::immediateSubmit(std::function<void(VkCommandBuffer cmd)> &&function)
{
    // VkCommandBufferAllocateInfo allocInfo{};
    // allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    // allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    // allocInfo.commandPool = commandPool;
    // allocInfo.commandBufferCount = 1;

    // VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    // vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    // VkCommandBufferBeginInfo beginInfo{};
    // beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    // beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    // vkBeginCommandBuffer(commandBuffer, &beginInfo);
    // function(commandBuffer);
    // vkEndCommandBuffer(commandBuffer);

    // VkSubmitInfo submitInfo{};
    // submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    // submitInfo.commandBufferCount = 1;
    // submitInfo.pCommandBuffers = &commandBuffer;

    // vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    // vkQueueWaitIdle(graphicsQueue);

    // vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);


    // 确认上一次使用的UploadContext已经完成
    if (vkWaitForFences(device, 1, &uploadContext.fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to wait for upload fence!");
    }

    // Fence必须回到 unsignaled 才能交给下一次 submit
    if (vkResetFences(device, 1, &uploadContext.fence) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to reset upload fence!");
    }

    // Fence 已经证明上一轮 command buffer 不再 pending，因此可以安全reset command pool
    if (vkResetCommandPool(device, uploadContext.commandPool, 0) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to reset upload command pool!");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(uploadContext.commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to begin upload command buffer!");
    }

    function(uploadContext.commandBuffer);

    if (vkEndCommandBuffer(uploadContext.commandBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to end upload command buffer!");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &uploadContext.commandBuffer;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, uploadContext.fence) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to submit upload command buffer!");
    }

    if (vkWaitForFences(device, 1, &uploadContext.fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to finish upload commands!");
    }

}

void TriangleApplication::createUploadContext()
{
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

    if(vkCreateCommandPool(device, &poolInfo, nullptr, &uploadContext.commandPool) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create upload command pool!");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = uploadContext.commandPool;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device, &allocInfo, &uploadContext.commandBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to allocate upload command buffer!");
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if(vkCreateFence(device, &fenceInfo, nullptr, &uploadContext.fence) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create upload fence!");
    }

    mainDeletionQueue.pushFunction([this]() {
        vkDestroyFence(device, uploadContext.fence, nullptr);

        vkDestroyCommandPool(device, uploadContext.commandPool, nullptr);
    });
}

void TriangleApplication::createFrameContexts()
{
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    
    for (FrameContext& frame : frames)
    {
        // 创建 frame.commandPool

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        // TRANSIENT_BIT 表示该 command pool 中的 command buffer 会频繁分配和释放
        // 而不是 RESET_COMMAND_BUFFER_BIT 表示该 command pool 中的 command buffer 可以单独 reset
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

        if (vkCreateCommandPool(device, &poolInfo, nullptr, &frame.commandPool) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create frame command pool!");
        }

        // 从 frame.commandPool 分配 frame.commandBuffer

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = frame.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(device, &allocInfo, &frame.commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate frame command buffer!");
        }

        // 创建 frame.imageAvailable 信号量
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.imageAvailable) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create frame image available semaphore!");
        }

        // 创建带 SIGNALED_BIT 的 frame.renderFence
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateFence(device, &fenceInfo, nullptr, &frame.renderFence) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create frame render fence!");
        }

        const VkCommandPool commandPool = frame.commandPool;
        // const VkCommandBuffer commandBuffer = frame.commandBuffer;
        const VkFence renderFence = frame.renderFence;
        const VkSemaphore imageAvailable = frame.imageAvailable;

        mainDeletionQueue.pushFunction([this, commandPool, renderFence, imageAvailable]() {

            vkDestroyFence(device, renderFence, nullptr);
            vkDestroySemaphore(device, imageAvailable, nullptr);
            vkDestroyCommandPool(device, commandPool, nullptr);
        });
    }
}