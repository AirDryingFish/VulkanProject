#include "TriangleApplication.hpp"
#include "VulkanResources.hpp"
#include <stb_image.h>

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

GpuImage TriangleApplication::createTextureImageFromFile(
    const std::string &path,
    VkFormat format,
    const std::array<unsigned char, 4> &fallbackPixel)
{
    int texWidth = 0;
    int texHeight = 0;
    int texChannels = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> loadedPixels(
        stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha),
        &stbi_image_free
    );
    const stbi_uc* pixels = loadedPixels.get();

    std::vector<unsigned char> fallbackPixels;
    if (!pixels)
    {
        texWidth = 1;
        texHeight = 1;
        fallbackPixels.assign(fallbackPixel.begin(), fallbackPixel.end());
        pixels = fallbackPixels.data();
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(texWidth) * static_cast<VkDeviceSize>(texHeight) * 4;
    const uint32_t imageMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

    const std::string stagingDebugName = path + " staging buffer";
    BufferDesc stagingDesc{};
    stagingDesc.size = imageSize;
    stagingDesc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    stagingDesc.debugName = stagingDebugName.c_str();

    GpuBuffer stagingBuffer = context.createBuffer(stagingDesc);

    void *data = nullptr;
    VK_CHECK(stagingBuffer.map(&data));
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    stagingBuffer.unmap();

    const std::string imageDebugName = path + " image";
    ImageDesc imageDesc{};
    imageDesc.extent = {
        static_cast<uint32_t>(texWidth),
        static_cast<uint32_t>(texHeight),
        1
    };
    imageDesc.mipLevels = imageMipLevels;
    imageDesc.arrayLayers = 1;
    imageDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    imageDesc.format = format;
    imageDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageDesc.usage =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;
    imageDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    imageDesc.flags = 0;
    imageDesc.debugName = imageDebugName.c_str();

    GpuImage image = context.createImage(imageDesc);

    transitionImageLayout(image.get(), format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, imageMipLevels);
    copyBufferToImage(stagingBuffer.get(), image.get(), static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
    generateMipmaps(image.get(), format, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), imageMipLevels);
    return image;
}

void TriangleApplication::createTextureImage()
{
    textureImage = createTextureImageFromFile(PBR_ALBEDO_PATH, VK_FORMAT_R8G8B8A8_SRGB, {255, 255, 255, 255});
    normalImage = createTextureImageFromFile(PBR_NORMAL_PATH, VK_FORMAT_R8G8B8A8_UNORM, {128, 128, 255, 255});
    metallicImage = createTextureImageFromFile(PBR_METALLIC_PATH, VK_FORMAT_R8G8B8A8_UNORM, {0, 0, 0, 255});
    roughnessImage = createTextureImageFromFile(PBR_ROUGHNESS_PATH, VK_FORMAT_R8G8B8A8_UNORM, {128, 128, 128, 255});
    aoImage = createTextureImageFromFile(PBR_AO_PATH, VK_FORMAT_R8G8B8A8_UNORM, {255, 255, 255, 255});
    mipLevels = textureImage.mipLevels();
}

void TriangleApplication::generateMipmaps(VkImage image, VkFormat imageFormat, uint32_t texWidth, uint32_t texHeight, uint32_t mipLevels, uint32_t layerCount)
{
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(context.physicalDevice(), imageFormat, &formatProperties);

    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        throw std::runtime_error("texture image format does not support linear blitting!");
    }

    renderer.immediateSubmit([&](VkCommandBuffer commandBuffer)
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

        int32_t mipWidth = texWidth;
        int32_t mipHeight = texHeight;

        for (uint32_t i = 1; i < mipLevels; i++)
        {
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier);

            VkImageBlit blit{};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = layerCount;
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {
                mipWidth > 1 ? mipWidth / 2 : 1,
                mipHeight > 1 ? mipHeight / 2 : 1,
                1};
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = layerCount;

            vkCmdBlitImage(
                commandBuffer,
                image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit,
                VK_FILTER_LINEAR);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier);

            if (mipWidth > 1)
            {
                mipWidth /= 2;
            }
            if (mipHeight > 1)
            {
                mipHeight /= 2;
            }
        }

        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier); });
}


void TriangleApplication::createTextureImageView()
{
    // textureImage.imageView = createImageView(textureImage.image, VK_FORMAT_R8G8B8A8_SRGB, textureImage.mipLevels);
    // normalImage.imageView = createImageView(normalImage.image, VK_FORMAT_R8G8B8A8_UNORM, normalImage.mipLevels);
    // metallicImage.imageView = createImageView(metallicImage.image, VK_FORMAT_R8G8B8A8_UNORM, metallicImage.mipLevels);
    // roughnessImage.imageView = createImageView(roughnessImage.image, VK_FORMAT_R8G8B8A8_UNORM, roughnessImage.mipLevels);
    // aoImage.imageView = createImageView(aoImage.image, VK_FORMAT_R8G8B8A8_UNORM, aoImage.mipLevels);

    // mainDeletionQueue.pushFunction([this, image = textureImage]() mutable
    //                                { destroyImage(image); });
    // mainDeletionQueue.pushFunction([this, image = normalImage]() mutable
    //                                { destroyImage(image); });
    // mainDeletionQueue.pushFunction([this, image = metallicImage]() mutable
    //                                { destroyImage(image); });
    // mainDeletionQueue.pushFunction([this, image = roughnessImage]() mutable
    //                                { destroyImage(image); });
    // mainDeletionQueue.pushFunction([this, image = aoImage]() mutable
    //                                { destroyImage(image); });
    textureImage.setView(context.createImageView(textureImage.get(), VK_FORMAT_R8G8B8A8_SRGB, textureImage.mipLevels()));
    normalImage.setView(context.createImageView(normalImage.get(), VK_FORMAT_R8G8B8A8_UNORM, normalImage.mipLevels()));
    metallicImage.setView(context.createImageView(metallicImage.get(), VK_FORMAT_R8G8B8A8_UNORM, metallicImage.mipLevels()));
    roughnessImage.setView(context.createImageView(roughnessImage.get(), VK_FORMAT_R8G8B8A8_UNORM, roughnessImage.mipLevels()));
    aoImage.setView(context.createImageView(aoImage.get(), VK_FORMAT_R8G8B8A8_UNORM, aoImage.mipLevels()));
}

void TriangleApplication::createTextureSampler()
{
    SamplerDesc samplerDesc{};
    samplerDesc.magFilter = VK_FILTER_LINEAR;
    samplerDesc.minFilter = VK_FILTER_LINEAR;
    samplerDesc.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerDesc.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerDesc.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerDesc.anisotropyEnable = VK_TRUE;
    samplerDesc.maxAnisotropy = 16.0f;
    samplerDesc.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerDesc.unnormalizedCoordinates = VK_FALSE;
    samplerDesc.compareEnable = VK_FALSE;
    samplerDesc.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerDesc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerDesc.minLod = 0.0f;
    samplerDesc.maxLod = VK_LOD_CLAMP_NONE;
    samplerDesc.debugName = "texture sampler";

    textureSampler = context.createSampler(samplerDesc);
}

void TriangleApplication::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t layerCount)
{
    renderer.immediateSubmit([&](VkCommandBuffer commandBuffer)
                             {
        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else
        {
            throw std::invalid_argument("unsupported layout transition!");
        }

        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = layerCount;

        vkCmdPipelineBarrier(
            commandBuffer,
            sourceStage,
            destinationStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier); });
}

void TriangleApplication::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
    renderer.immediateSubmit([&](VkCommandBuffer commandBuffer)
                             {
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {
            width,
            height,
            1};

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region); });
}


bool TriangleApplication::hasStencilComponent(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}
