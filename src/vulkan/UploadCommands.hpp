#pragma once

#include "vulkan/VulkanHeaders.hpp"
#include <cstdint>

namespace upload
{
    void recordBufferCopy(
        VkCommandBuffer commandBuffer,
        VkBuffer source,
        VkBuffer destination,
        VkDeviceSize size
    );

    // 用于“此前 gpu 尚未访问”的新建颜色图像。
    // mipLevels、layerCount 必须大于 0；所属 Device 必须已启用 synchronization2。
    void recordPrepareImageForUpload(
        VkCommandBuffer commandBuffer,
        VkImage image,
        uint32_t mipLevels,
        uint32_t layerCount,
        PFN_vkCmdPipelineBarrier2 pipelineBarrier2
    );

    void recordImageTransition(
        VkCommandBuffer commandBuffer,
        VkImage image,
        VkFormat format,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        uint32_t mipLevels,
        uint32_t layerCount = 1,
        PFN_vkCmdPipelineBarrier2 pipelineBarrier2 = nullptr
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
        uint32_t layerCount = 1,
        PFN_vkCmdPipelineBarrier2 pipelineBarrier2 = nullptr
    );
}