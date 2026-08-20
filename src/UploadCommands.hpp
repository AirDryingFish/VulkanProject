#pragma once

#include "VulkanHeaders.hpp"
#include <cstdint>

namespace upload
{
    void recordBufferCopy(
        VkCommandBuffer commandBuffer,
        VkBuffer source,
        VkBuffer destination,
        VkDeviceSize size
    );

    void recordImageTransition(
        VkCommandBuffer commandBuffer,
        VkImage image,
        VkFormat format,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        uint32_t mipLevels,
        uint32_t layerCount = 1
    );

    void recordBufferToImageCopy(
        VkCommandBuffer commandBuffer,
        VkBuffer source,
        VkImage destination,
        VkExtent3D extent,
        uint32_t layerCount = 1
    );

    void recordGenerateMipmaps(
        VkCommandBuffer commandBuffer,
        VkImage image,
        VkExtent2D baseExtent,
        uint32_t mipLevels,
        uint32_t layerCount = 1
    );
}