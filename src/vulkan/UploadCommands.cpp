#include "vulkan/UploadCommands.hpp"

#include <stdexcept>

namespace upload{

void recordBufferCopy(VkCommandBuffer commandBuffer, VkBuffer source, VkBuffer destination, VkDeviceSize size)
{
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;
    vkCmdCopyBuffer(
        commandBuffer,
        source,
        destination,
        1,
        &copyRegion
    );
}

// 向命令缓冲区记录一条 barrier，把新图像的全部 mip，全部指定 layer 转为传输写入布局，为上传和生成 mipmap 做准备
void recordPrepareImageForUpload(VkCommandBuffer commandBuffer, VkImage image, uint32_t mipLevels, uint32_t layerCount, PFN_vkCmdPipelineBarrier2 pipelineBarrier2)
{
    if (pipelineBarrier2 == nullptr)
    {
        throw std::logic_error("Upload preparation requires vkCmdPipelineBarrier2");
    }

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;

    // 新图像没有此前需要等待的 gpu 访问
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.srcAccessMask = VK_ACCESS_2_NONE;

    // 后续 copy 写 mip 0，blit 写其他 mip
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;  // 后续相关操作是 copy blit 等传输操作
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;       // 后续会写入图像

    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    // 当前上传，没有跨 queue family 转移所有权。
    // 表示：这条 barrier 不负责把图像从一个队列族交给另一个队列族
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;

    // subresourceRange 指定转换图像的哪些部分
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layerCount;

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;

    pipelineBarrier2(commandBuffer, &dependency);
}

void recordImageTransition(VkCommandBuffer commandBuffer, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t layerCount)
{
    static_cast<void>(format);

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;

    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; // 这个 barrier 作用于颜色部分
    barrier.subresourceRange.baseMipLevel = 0; // 从 0 级 mip 开始
    barrier.subresourceRange.levelCount = mipLevels; // 一共影响多少级 mip
    barrier.subresourceRange.baseArrayLayer = 0; // 从第 0 个 image layer 开始
    barrier.subresourceRange.layerCount = layerCount; // 一共影响多少个 layer (2D texture 通常有 1 个，cubemap有 6 个)

    // 准备把数据copy到image：上传纹理
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0; // 不关心 image 原来的内容
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // 接下来 transfer 阶段准备往 image 写数据
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    // 新创建了一个 image, 不上传图片而是把它当作 render target (当作颜色附件)
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    // 纹理上传完成后，例如加载 roughness.png albedo.png
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    // render结束后
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        throw std::invalid_argument("unsupported layout transition");
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage,
        destinationStage,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
}

void recordBufferToImageCopy(VkCommandBuffer commandBuffer, VkBuffer source, VkImage destination, VkExtent3D extent, uint32_t layerCount)
{
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = layerCount;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = extent;

    vkCmdCopyBufferToImage(commandBuffer, source, destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void recordGenerateMipmaps(VkCommandBuffer commandBuffer, VkImage image, VkExtent2D baseExtent, uint32_t mipLevels, uint32_t layerCount, PFN_vkCmdPipelineBarrier2 pipelineBarrier2)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layerCount;
    barrier.subresourceRange.levelCount = 1;

    if (mipLevels == 0 || layerCount == 0 || baseExtent.width == 0 || baseExtent.height == 0)
    {
        throw std::invalid_argument("Mipmap dimensions and ranges must be nonzero");
    }

    const auto recordMipTransition = [&](
        uint32_t mip,
        VkImageLayout oldLayout,
        VkImageLayout newLayout
    ){
        const bool wasBlitSource = oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        const bool becomesBlitSource = newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = mip;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = layerCount;

        if (pipelineBarrier2 != nullptr)
        {
            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;

            barrier.srcStageMask = wasBlitSource ? VK_PIPELINE_STAGE_2_BLIT_BIT : VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
            barrier.srcAccessMask = wasBlitSource ? VK_ACCESS_2_TRANSFER_READ_BIT : VK_ACCESS_2_TRANSFER_WRITE_BIT;

            barrier.dstStageMask = becomesBlitSource ? VK_PIPELINE_STAGE_2_BLIT_BIT : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.dstAccessMask = becomesBlitSource ? VK_ACCESS_2_TRANSFER_READ_BIT : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange = range;

            VkDependencyInfo dependency{};
            dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers = &barrier;

            pipelineBarrier2(commandBuffer, &dependency);
            return;
        }

        // legacy 不使用 barrier2 的路径
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

        barrier.srcAccessMask = wasBlitSource
                                    ? VK_ACCESS_TRANSFER_READ_BIT
                                    : VK_ACCESS_TRANSFER_WRITE_BIT;

        barrier.dstAccessMask = becomesBlitSource
                                    ? VK_ACCESS_TRANSFER_READ_BIT
                                    : VK_ACCESS_SHADER_READ_BIT;

        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = range;

        const VkPipelineStageFlags destinationStage = becomesBlitSource
                                                          ? VK_PIPELINE_STAGE_TRANSFER_BIT
                                                          : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            destinationStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

    };

    int32_t mipWidth = static_cast<int32_t>(baseExtent.width);
    int32_t mipHeight = static_cast<int32_t>(baseExtent.height);

    // Mip 0:
    // TRANSFER_DST → TRANSFER_SRC
    //         ↓
    // blit Mip 0 → Mip 1
    //         ↓
    // Mip 0:
    // TRANSFER_SRC → SHADER_READ_ONLY
    // 此时：
    // Mip 0 = SHADER_READ_ONLY
    // Mip 1 = TRANSFER_DST
    // Mip 2 = TRANSFER_DST
    // Mip 3 = TRANSFER_DST
    for (uint32_t mip = 1; mip < mipLevels; ++mip)
    {
        recordMipTransition(mip - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = mip - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = layerCount;

        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {
            mipWidth > 1 ? mipWidth / 2 : 1,
            mipHeight > 1 ? mipHeight / 2 : 1,
            1
        };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = mip;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = layerCount;

        // 同一个image的不同mip层
        vkCmdBlitImage(
            commandBuffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &blit,
            VK_FILTER_LINEAR
        );

        recordMipTransition(mip - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        if (mipWidth > 1)
        {
            mipWidth /= 2;
        }
        if (mipHeight > 1)
        {
            mipHeight /= 2;
        }
    }

    recordMipTransition(mipLevels - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}


}

