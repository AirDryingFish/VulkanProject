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



void TriangleApplication::waitForAllFrames()
{
    std::array<VkFence, MAX_FRAMES_IN_FLIGHT> fences{};
    for (size_t i = 0; i < frames.size(); i++)
    {
        fences[i] = frames[i].renderFence;
    }

    VK_CHECK(vkWaitForFences(context.device(), static_cast<uint32_t>(fences.size()), fences.data(), VK_TRUE, UINT64_MAX));
}

void TriangleApplication::recreateSwapChain()
{
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);

    while (width == 0 || height == 0)
    {
        if (glfwWindowShouldClose(window))
        {
            return;
        }
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    waitForAllFrames();
    // Frame Fence 只能证明 Graphics Submission完成
    // Present Queue 可能还在使用旧的 SwapChain Image，必须等待 Present Queue 空闲后才能销毁旧的 SwapChain
    VK_CHECK(vkQueueWaitIdle(context.presentQueue()));

    const VkFormat oldFormat = swapchain.format();

    swapchain.shutdown();
    swapchain.initializeCore(context, window);

    if (oldFormat != VK_FORMAT_UNDEFINED && oldFormat != swapchain.format())
    {
        throw std::runtime_error(
            "swapchain format changed; renderer "
            "resources must be rebuilt");
    }

    swapchain.createFramebuffers(renderPass);
}

void TriangleApplication::framebufferRizeCallback(GLFWwindow *window, int width, int height)
{
    auto app = reinterpret_cast<TriangleApplication *>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
}

void TriangleApplication::windowRefreshCallback(GLFWwindow *window)
{
    auto app = reinterpret_cast<TriangleApplication *>(glfwGetWindowUserPointer(window));
    if (app != nullptr && app->rendererReady && !app->frameInProgress)
    {
        app->drawFrame();
    }
}

