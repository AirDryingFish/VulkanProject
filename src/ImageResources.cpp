#include "TriangleApplication.hpp"
#include "VulkanResources.hpp"
#include "UploadCommands.hpp"
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

    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(context.physicalDevice(), image.format(), &formatProperties);

    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        throw std::runtime_error("texture image format does not support linear blitting");
    }

    const VkExtent3D imageExtent = image.extent();

    // 连续录制
    renderer.immediateSubmit([&](VkCommandBuffer commandBuffer){
        upload::recordImageTransition(
            commandBuffer,
            image.get(),
            image.format(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            image.mipLevels(),
            image.arrayLayers()
        );

        upload::recordBufferToImageCopy(
            commandBuffer,
            stagingBuffer.get(),
            image.get(),
            imageExtent,
            image.arrayLayers()
        );
        upload::recordGenerateMipmaps(
            commandBuffer,
            image.get(),
            VkExtent2D{
                imageExtent.width,
                imageExtent.height
            },
            image.mipLevels(),
            image.arrayLayers()
        );
    });
    

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

void TriangleApplication::createTextureImageView()
{
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
        upload::recordImageTransition(
            commandBuffer,
            image,
            format,
            oldLayout,
            newLayout,
            mipLevels,
            layerCount
        );
    });
}


bool TriangleApplication::hasStencilComponent(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}
