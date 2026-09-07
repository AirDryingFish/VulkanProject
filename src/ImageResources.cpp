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
#include <memory>
#include <utility>

namespace{
VkSamplerAddressMode toSamplerAddressMode(
    GltfWrap wrap,
    const std::string& debugName
)
{
    switch (wrap)
    {
    case GltfWrap::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case GltfWrap::MirroredRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case GltfWrap::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    throw std::runtime_error(debugName + ": unsupported sampler wrap mode");
}
}

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

ImageHandle TriangleApplication::getOrCreateGltfImage(
    const GltfImportData &imported,
    std::size_t imageIndex,
    VkFormat format)
{
    const std::string assetPath = imported.sourcePath.generic_string();
    const std::string debugName = assetPath + ": image[" + std::to_string(imageIndex) + "]";
    if (imageIndex >= imported.images.size())
    {
        throw std::runtime_error(debugName + ": image index is out of range");
    }
    if (format != VK_FORMAT_R8G8B8A8_SRGB && format != VK_FORMAT_R8G8B8A8_UNORM)
    {
        throw std::invalid_argument(debugName + ": unsupported RGBA8 upload format");
    }

    const GltfImageKey key{assetPath, imageIndex, format};
    const auto cached = gltfImageCache.find(key);
    if (cached != gltfImageCache.end())
    {
        if (ImageHandle gpuImage = cached->second.lock())
        {
            return gpuImage;
        }
    }

    const DecodedImageData& decoded = imported.images[imageIndex];
    ImageHandle gpuImage = std::make_shared<ImageResource>();
    gpuImage->name = debugName + (format == VK_FORMAT_R8G8B8A8_SRGB ? " SRGB" : " UNORM");
    gpuImage->image = uploadTexture2D(decoded, format, gpuImage->name);
    gltfImageCache.insert_or_assign(key, std::weak_ptr<ImageResource>{gpuImage});
    return gpuImage;
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

ImageHandle TriangleApplication::createImageResource(
    const std::string& name,
    const std::string& path,
    VkFormat format,
    const std::array<unsigned char, 4>& fallbackPixels
)
{
    const DecodedImageData decoded = decodeTextureImageFromFileOrFallback(path, fallbackPixels);

    ImageHandle texture = std::make_shared<ImageResource>();
    texture->name = name;
    texture->image = uploadTexture2D(decoded, format, name);

    textureLibrary.push_back(texture);

    return texture;
}

void TriangleApplication::createMaterialResources()
{
    const ImageHandle rustedBaseColorTexture = createImageResource(
        "Rusted Iron Base Color",
        PBR_ALBEDO_PATH,
        VK_FORMAT_R8G8B8A8_SRGB,
        {255, 255, 255, 255}
    );

    const ImageHandle rustedNormalTexture =
        createImageResource(
            "Rusted Iron Normal",
            PBR_NORMAL_PATH,
            VK_FORMAT_R8G8B8A8_UNORM,
            {128, 128, 255, 255});

    const ImageHandle rustedMetallicTexture =
        createImageResource(
            "Rusted Iron Metallic",
            PBR_METALLIC_PATH,
            VK_FORMAT_R8G8B8A8_UNORM,
            {0, 0, 0, 255});

    const ImageHandle rustedRoughnessTexture =
        createImageResource(
            "Rusted Iron Roughness",
            PBR_ROUGHNESS_PATH,
            VK_FORMAT_R8G8B8A8_UNORM,
            {255, 255, 255, 255});

    const ImageHandle rustedAoTexture =
        createImageResource(
            "Rusted Iron AO",
            PBR_AO_PATH,
            VK_FORMAT_R8G8B8A8_UNORM,
            {255, 255, 255, 255});

    defaultBaseColorTexture = createImageResource(
        "Default Base Color",
        std::string(),
        VK_FORMAT_R8G8B8A8_SRGB,
        {255, 255, 255, 255}
    );

    defaultNormalTexture =
        createImageResource(
            "Default Normal",
            std::string(),
            VK_FORMAT_R8G8B8A8_UNORM,
            {128, 128, 255, 255});

    defaultMetallicTexture =
        createImageResource(
            "Default Metallic",
            std::string(),
            VK_FORMAT_R8G8B8A8_UNORM,
            {0, 0, 0, 255});

    defaultRoughnessTexture =
        createImageResource(
            "Default Roughness",
            std::string(),
            VK_FORMAT_R8G8B8A8_UNORM,
            {255, 255, 255, 255});

    defaultAoTexture =
        createImageResource(
            "Default AO",
            std::string(),
            VK_FORMAT_R8G8B8A8_UNORM,
            {255, 255, 255, 255});

    defaultEmissiveTexture =
        createImageResource(
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

SamplerHandle TriangleApplication::getOrCreateGltfSampler(const GltfSamplerData &source, const std::string &debugName)
{
    const GltfFilter magFilter = source.magFilter.value_or(GltfFilter::Linear);
    const GltfFilter minFilter = source.minFilter.value_or(GltfFilter::LinearMipmapLinear);

    const SamplerKey key{
        static_cast<std::uint16_t>(magFilter),
        static_cast<std::uint16_t>(minFilter),
        static_cast<std::uint16_t>(source.wrapS),
        static_cast<std::uint16_t>(source.wrapT)
    };

    for (const SamplerHandle& resource: samplerLibrary)
    {
        if (resource->key == key)
        {
            return resource;
        }
    }

    // 第一次遇到这个类型的 sampler，就创建对应的 gpusampler
    SamplerDesc desc{};
    desc.addressModeU = toSamplerAddressMode(source.wrapS, debugName);
    desc.addressModeV = toSamplerAddressMode(source.wrapT, debugName);
    desc.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    desc.anisotropyEnable = VK_FALSE;
    desc.maxAnisotropy = 1.0f;
    desc.minLod = 0.0f;
    desc.maxLod = VK_LOD_CLAMP_NONE;
    switch (magFilter)
    {
    case GltfFilter::Nearest:
        desc.magFilter = VK_FILTER_NEAREST;
        break;
    case GltfFilter::Linear:
        desc.magFilter = VK_FILTER_LINEAR;
        break;
    default:
        throw std::runtime_error(debugName + ": invalid magnification filter");
    }
    // LinearMipmapNearest 表示：每层内部线性过滤，mip 层之间选择最近一层
    // 普通 Nearest/Linear 不使用 mipmap。使用 NEAREST mipmapmode 和 maxLod = 0.25f，既限定在第 0 层，又保留缩小过滤的选择
    // 未指定过滤时项目默认使用 Linear + LinearMipmapLinear。
    switch (minFilter)
    {
    case GltfFilter::Nearest:
        desc.minFilter = VK_FILTER_NEAREST;
        desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        desc.maxLod = 0.25f;
        break;
    case GltfFilter::Linear:
        desc.minFilter = VK_FILTER_LINEAR;
        desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        desc.maxLod = 0.25f;
        break;
    case GltfFilter::NearestMipmapNearest:
        desc.minFilter = VK_FILTER_NEAREST;
        desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case GltfFilter::NearestMipmapLinear:
        desc.minFilter = VK_FILTER_NEAREST;
        desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        break;
    case GltfFilter::LinearMipmapNearest:
        desc.minFilter = VK_FILTER_LINEAR;
        desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case GltfFilter::LinearMipmapLinear:
        desc.minFilter = VK_FILTER_LINEAR;
        desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        break;

    default:
        throw std::runtime_error(debugName + ": invalid minification filter");
    }

    SamplerHandle resource = std::make_shared<SamplerResource>();
    resource->name = debugName;
    resource->key = key;
    resource->sampler = context.createSampler(desc);
    samplerLibrary.push_back(resource);
    return resource;
}

void TriangleApplication::createTextureSampler()
{
    defaultTextureSampler = getOrCreateGltfSampler(GltfSamplerData{}, "Default texture sampler");
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
