#pragma once

#include "vulkan/VulkanHeaders.hpp"
#include <cstdint>

// 仅录制上传命令，不负责提交或等待；调用方保持资源存活并组织跨提交依赖。
namespace upload
{
// 复制范围从源与目标的 offset 0 开始；目标供后续读取的 barrier 由调用方组织。
void recordBufferCopy(VkCommandBuffer commandBuffer, VkBuffer source, VkBuffer destination, VkDeviceSize size);

// 用于“此前 gpu 尚未访问”的新建颜色图像。
// mipLevels、layerCount 必须大于 0；所属 Device 必须已启用 synchronization2。
void recordPrepareImageForUpload(VkCommandBuffer commandBuffer, VkImage image, uint32_t mipLevels, uint32_t layerCount,
                                 PFN_vkCmdPipelineBarrier2 pipelineBarrier2);

// 转换受支持的颜色图像布局；未提供 barrier2 时使用 legacy 同步路径。
void recordImageTransition(VkCommandBuffer commandBuffer, VkImage image, VkFormat format, VkImageLayout oldLayout,
                           VkImageLayout newLayout, uint32_t mipLevels, uint32_t layerCount = 1,
                           PFN_vkCmdPipelineBarrier2 pipelineBarrier2 = nullptr);

// 紧密排列的数据上传至 mip 0；目标图像须已处于 TRANSFER_DST_OPTIMAL。
void recordBufferToImageCopy(VkCommandBuffer commandBuffer, VkBuffer source, VkImage destination, VkExtent3D extent,
                             uint32_t layerCount = 1);

// 所有 mip 须处于 TRANSFER_DST_OPTIMAL，mip 0 已有内容；结束时全部可供片元采样。
// 调用方须先验证图像格式支持线性 blit。
void recordGenerateMipmaps(VkCommandBuffer commandBuffer, VkImage image, VkExtent2D baseExtent, uint32_t mipLevels,
                           uint32_t layerCount = 1, PFN_vkCmdPipelineBarrier2 pipelineBarrier2 = nullptr);
} // namespace upload