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

DecodedImageData TriangleApplication::decodeTextureImageFromFileOrFallback(const std::string &path, const std::array<unsigned char, 4> &fallbackPixel)
{
    DecodedImageData result{};
    result.name = path.empty() ? "fallback texture" : path;
    int width = 0;
    int height = 0;
    int channels = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        path.empty() ? nullptr : stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha),
        &stbi_image_free
    );
    if (!pixels)
    {
        result.width = 1;
        result.height = 1;
        result.rgba8.assign(fallbackPixel.begin(), fallbackPixel.end());
        return result;
    }

    if (width <= 0 || height <= 0)
    {
        throw std::runtime_error(path + ": decoded image has invalid dimensions");
    }

    const std::size_t byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    result.width = width;
    result.height = height;
    result.rgba8.assign(pixels.get(), pixels.get() + byteCount);
    return result;
}

GpuImage TriangleApplication::uploadTexture2D(const DecodedImageData &decoded, VkFormat format, const std::string &debugName)
{
    if (decoded.width <= 0 || decoded.height <= 0)
    {
        throw std::invalid_argument(debugName + ": image dimensions must be positive");
    }
    const int texWidth = decoded.width;
    const int texHeight = decoded.height;
    const std::size_t expectedByteCount = static_cast<std::size_t>(texWidth) * static_cast<std::size_t>(texHeight) * 4u;
    if (decoded.rgba8.size() != expectedByteCount)
    {
        throw std::invalid_argument(debugName + ": RGBA8 byte count does not match image dimensions");
    }

    std::string path = decoded.name;
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(expectedByteCount);
    const uint32_t imageMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

    const std::string stagingDebugName = debugName + " staging buffer";
    BufferDesc stagingDesc{};
    stagingDesc.size = imageSize;
    stagingDesc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    stagingDesc.debugName = stagingDebugName.c_str();

    GpuBuffer stagingBuffer = context.createBuffer(stagingDesc);

    void *data = nullptr;
    VK_CHECK(stagingBuffer.map(&data));
    memcpy(data, decoded.rgba8.data(), expectedByteCount);
    stagingBuffer.unmap();

    const std::string imageDebugName = debugName + " image";
    ImageDesc imageDesc{};
    imageDesc.extent = {
        static_cast<uint32_t>(texWidth),
        static_cast<uint32_t>(texHeight),
        1};
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
    renderer.immediateSubmit([&](VkCommandBuffer commandBuffer)
                             {
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
        ); });

    image.setView(context.createImageView(image.get(), image.format(), image.mipLevels()));

    return image;
}

TextureHandle TriangleApplication::createTextureResource(
    const std::string& name,
    const std::string& path,
    VkFormat format,
    const std::array<unsigned char, 4>& fallbackPixels
)
{
    const DecodedImageData decoded = decodeTextureImageFromFileOrFallback(path, fallbackPixels);

    TextureHandle texture = std::make_shared<TextureResource>();
    texture->name = name;
    texture->image = uploadTexture2D(decoded, format, name);

    textureLibrary.push_back(texture);

    return texture;
}

void TriangleApplication::createMaterialResources()
{
    const TextureHandle rustedBaseColorTexture = createTextureResource(
        "Rusted Iron Base Color",
        PBR_ALBEDO_PATH,
        VK_FORMAT_R8G8B8A8_SRGB,
        {255, 255, 255, 255}
    );

    const TextureHandle rustedNormalTexture =
        createTextureResource(
            "Rusted Iron Normal",
            PBR_NORMAL_PATH,
            VK_FORMAT_R8G8B8A8_UNORM,
            {128, 128, 255, 255});

    const TextureHandle rustedMetallicTexture =
        createTextureResource(
            "Rusted Iron Metallic",
            PBR_METALLIC_PATH,
            VK_FORMAT_R8G8B8A8_UNORM,
            {0, 0, 0, 255});

    const TextureHandle rustedRoughnessTexture =
        createTextureResource(
            "Rusted Iron Roughness",
            PBR_ROUGHNESS_PATH,
            VK_FORMAT_R8G8B8A8_UNORM,
            {255, 255, 255, 255});

    const TextureHandle rustedAoTexture =
        createTextureResource(
            "Rusted Iron AO",
            PBR_AO_PATH,
            VK_FORMAT_R8G8B8A8_UNORM,
            {255, 255, 255, 255});

    defaultBaseColorTexture = createTextureResource(
        "Default Base Color",
        std::string(),
        VK_FORMAT_R8G8B8A8_SRGB,
        {255, 255, 255, 255}
    );

    defaultNormalTexture =
        createTextureResource(
            "Default Normal",
            std::string(),
            VK_FORMAT_R8G8B8A8_UNORM,
            {128, 128, 255, 255});

    defaultMetallicTexture =
        createTextureResource(
            "Default Metallic",
            std::string(),
            VK_FORMAT_R8G8B8A8_UNORM,
            {0, 0, 0, 255});

    defaultRoughnessTexture =
        createTextureResource(
            "Default Roughness",
            std::string(),
            VK_FORMAT_R8G8B8A8_UNORM,
            {255, 255, 255, 255});

    defaultAoTexture =
        createTextureResource(
            "Default AO",
            std::string(),
            VK_FORMAT_R8G8B8A8_UNORM,
            {255, 255, 255, 255});

    defaultEmissiveTexture =
        createTextureResource(
            "Default Emissive",
            std::string(),
            VK_FORMAT_R8G8B8A8_SRGB,
            {0, 0, 0, 255});

    defaultMaterial = std::make_shared<Material>();
    defaultMaterial->name = "Rusted Iron";
    defaultMaterial->baseColorTexture = rustedBaseColorTexture;
    defaultMaterial->normalTexture = rustedNormalTexture;
    defaultMaterial->metallicTexture = rustedMetallicTexture;
    defaultMaterial->roughnessTexture = rustedRoughnessTexture;
    defaultMaterial->aoTexture = rustedAoTexture;
    defaultMaterial->emissiveTexture = defaultEmissiveTexture;

    MaterialHandle variantMaterial = std::make_shared<Material>();
    variantMaterial->name = "Rusted Iron Variant";
    variantMaterial->baseColorTexture = rustedBaseColorTexture;
    variantMaterial->normalTexture = rustedNormalTexture;
    variantMaterial->metallicTexture = rustedMetallicTexture;
    variantMaterial->roughnessTexture = rustedRoughnessTexture;
    variantMaterial->aoTexture = rustedAoTexture;
    variantMaterial->emissiveTexture = defaultEmissiveTexture;
    variantMaterial->baseColorFactor = glm::vec4(0.25f, 1.0f, 0.25f, 1.0f);
    variantMaterial->metallicFactor = 0.25f;
    variantMaterial->roughnessFactor = 0.30f;
    variantMaterial->aoFactor = 1.0f;

    materialLibrary.push_back(defaultMaterial);
    materialLibrary.push_back(variantMaterial);

    mipLevels = defaultMaterial->baseColorTexture->image.mipLevels();
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
