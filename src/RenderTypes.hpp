#pragma once

#include "VulkanHeaders.hpp"
#include <glm/mat4x4.hpp>

#include <cstdint>
#include <vector>

struct ImDrawData;

struct RenderObjectView
{
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
    glm::mat4 model{1.0f};
};

struct RenderFrameData
{
    const std::vector<RenderObjectView>* objects = nullptr;
    VkDescriptorSet sceneDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet skyboxDescriptorSet = VK_NULL_HANDLE;

    ImDrawData* imguiDrawData = nullptr;
};