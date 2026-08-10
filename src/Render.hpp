#pragma once

#include "RenderTypes.hpp"
#include "VulkanResources.hpp"

#include <vector>

struct FrameContext
{
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkFence renderFence = VK_NULL_HANDLE;

    std::vector<AllocatedBuffer> retiredBuffers;
};

class Render
{
private:


public:


};