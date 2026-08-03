#include "TriangleApplication.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <stdexcept>

void TriangleApplication::createPresentSemaphores()
{
    renderFinishedSemaphores.resize(swapChainImages.size());

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;


    for (VkSemaphore &renderFinished : renderFinishedSemaphores)
    {
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinished));
        swapChainDeletionQueue.pushFunction([this, renderFinished]() {
            vkDestroySemaphore(device, renderFinished, nullptr);
        });
    }
}

void TriangleApplication::drawFrame()
{
    if (!rendererReady || frameInProgress)
    {
        return;
    }

    FrameContext& frame = frames[currentFrame];

    frameInProgress = true;
    auto finishFrame = [this]() {
        frameInProgress = false;
    };

    const float now = static_cast<float>(glfwGetTime());
    float deltaTime = now - lastFrameTime;
    lastFrameTime = now;
    deltaTime = std::min(deltaTime, 0.05f);

    VK_CHECK(vkWaitForFences(device, 1, &frame.renderFence, VK_TRUE, UINT64_MAX));
    frame.deletionQueue.flush();

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width == 0 || height == 0)
    {
        finishFrame();
        return;
    }

    if (framebufferResized)
    {
        framebufferResized = false;
        recreateSwapChain();
        finishFrame();
        return;
    }

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        framebufferResized = false;
        recreateSwapChain();
        finishFrame();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        finishFrame();
        VK_CHECK_RESULT(result, "vkAcquireNextImageKHR");
    }


    processCameraInput(deltaTime);
    updateUniformBuffer(currentFrame, deltaTime);

    VK_CHECK(vkResetCommandPool(device, frame.commandPool, 0));

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    drawImGui();
    processModelPicking();
    ImGui::Render();

    recordCommandBuffer(frame.commandBuffer, imageIndex);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {frame.imageAvailable};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer;

    // Presentation completion is not covered by the per-frame submit fence.
    // Indexing this semaphore by acquired image makes reuse safe: reacquiring
    // that image guarantees the previous presentation wait has completed.
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[imageIndex]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VK_CHECK(vkResetFences(device, 1, &frame.renderFence));


    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, frame.renderFence));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized)
    {
        framebufferResized = false;
        recreateSwapChain();
    }
    else if (result != VK_SUCCESS)
    {
        finishFrame();
        VK_CHECK_RESULT(result, "vkQueuePresentKHR");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    finishFrame();
}
