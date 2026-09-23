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
    VkFormat format,
    GltfMaterialUpload &upload)
{
    GltfImageCache& cache = upload.imageCache;
    const std::string assetPath = imported.sourcePath.generic_u8string();
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
    const auto cached = cache.find(key);
    if (cached != cache.end())
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
    cache.insert_or_assign(key, std::weak_ptr<ImageResource>{gpuImage});
    ++upload.uploadedImageCount;
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

    // 将分开的metallic和roughness贴图打包到一张贴图中
    const DecodedImageData metallic = decodeTextureImageFromFileOrFallback(
        PBR_METALLIC_PATH,
        {0, 0, 0, 255}
    );
    const DecodedImageData roughness = decodeTextureImageFromFileOrFallback(
        PBR_ROUGHNESS_PATH,
        {255, 255, 255, 255}
    );
    const bool metallicConstant = metallic.width == 1 && metallic.height == 1;
    const bool roughnessConstant = roughness.width == 1 && roughness.height == 1;
    if (!metallicConstant && !roughnessConstant &&
        (metallic.width != roughness.width ||
        metallic.height != roughness.height))
    {
        throw std::runtime_error("Rusted Iron: metallic and roughness dimensions differ");
    }
    DecodedImageData packed{};
    packed.name = "Rusted Iron Metallic-Roughness";
    packed.width = metallicConstant ? roughness.width : metallic.width;
    packed.height = metallicConstant ? roughness.height : metallic.height;
    const std::size_t pixelCount = static_cast<std::size_t>(packed.width) * static_cast<std::size_t>(packed.height);
    packed.rgba8.resize(pixelCount * 4u);
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
    {
        const std::size_t destination = pixel * 4u;
        const std::size_t metallicOffset = metallicConstant ? 0 : destination;
        const std::size_t roughnessOffset = roughnessConstant ? 0 : destination;

        packed.rgba8[destination + 0] = 255;
        packed.rgba8[destination + 1] = roughness.rgba8[roughnessOffset];
        packed.rgba8[destination + 2] = metallic.rgba8[metallicOffset];
        packed.rgba8[destination + 3] = 255;
    }

    ImageHandle rustedMetallicRoughnessTexture = std::make_shared<ImageResource>();
    rustedMetallicRoughnessTexture->name = packed.name;
    rustedMetallicRoughnessTexture->image = uploadTexture2D(
        packed, VK_FORMAT_R8G8B8A8_UNORM, packed.name
    );
    textureLibrary.push_back(rustedMetallicRoughnessTexture);

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

    defaultMetallicRoughnessTexture =
        createImageResource(
            "Default MetallicRoughness",
            std::string(),
            VK_FORMAT_R8G8B8A8_UNORM,
            {255, 255, 255, 255}); // 默认白色

    // 表示缝隙等位置的环境遮蔽，主要影响间接光照
    defaultAoTexture =
        createImageResource(
            "Default AO",
            std::string(),
            VK_FORMAT_R8G8B8A8_UNORM,
            {255, 255, 255, 255});

    // 表示便面自身发出的颜色，不依赖灯光照亮
    defaultEmissiveTexture =
        createImageResource(
            "Default Emissive",
            std::string(),
            VK_FORMAT_R8G8B8A8_SRGB,
            {255, 255, 255, 255});

    defaultMaterial = std::make_shared<Material>();
    defaultMaterial->name = "Rusted Iron";
    defaultMaterial->baseColorTexture = {
        rustedBaseColorTexture, defaultTextureSampler, 0u};
    defaultMaterial->normalTexture = {
        rustedNormalTexture, defaultTextureSampler, 0u};
    defaultMaterial->metallicRoughnessTexture = {
        rustedMetallicRoughnessTexture, defaultTextureSampler, 0u};
    defaultMaterial->aoTexture = {
        rustedAoTexture, defaultTextureSampler, 0u};
    defaultMaterial->emissiveTexture = {
        defaultEmissiveTexture, defaultTextureSampler, 0u};

    MaterialHandle variantMaterial = std::make_shared<Material>();
    variantMaterial->name = "Rusted Iron Variant";
    variantMaterial->baseColorTexture = defaultMaterial->baseColorTexture;
    variantMaterial->normalTexture = defaultMaterial->normalTexture;
    variantMaterial->metallicRoughnessTexture = defaultMaterial->metallicRoughnessTexture;
    variantMaterial->aoTexture = defaultMaterial->aoTexture;
    variantMaterial->emissiveTexture = defaultMaterial->emissiveTexture;
    variantMaterial->baseColorFactor = glm::vec4(0.25f, 1.0f, 0.25f, 1.0f);
    variantMaterial->metallicFactor = 0.25f;
    variantMaterial->roughnessFactor = 0.30f;
    variantMaterial->occlusionStrength = 1.0f;

    materialLibrary.push_back(defaultMaterial);
    materialLibrary.push_back(variantMaterial);

    // gltf 没有指定 material 时使用的默认材质
    defaultGltfMaterial = std::make_shared<Material>();
    defaultGltfMaterial->name = "glTF Default";
    defaultGltfMaterial->baseColorTexture = {
        defaultBaseColorTexture, defaultTextureSampler, 0u};
    defaultGltfMaterial->normalTexture = {
        defaultNormalTexture, defaultTextureSampler, 0u};
    defaultGltfMaterial->metallicRoughnessTexture = {
        defaultMetallicRoughnessTexture, defaultTextureSampler, 0u};
    defaultGltfMaterial->aoTexture = {
        defaultAoTexture, defaultTextureSampler, 0u};
    defaultGltfMaterial->emissiveTexture = {
        defaultEmissiveTexture, defaultTextureSampler, 0u};

    materialLibrary.push_back(defaultGltfMaterial);

    mipLevels = defaultMaterial->baseColorTexture.image->image.mipLevels();
}

SamplerHandle TriangleApplication::getOrCreateGltfSampler(const GltfSamplerData &source, const std::string &debugName, std::vector<SamplerHandle> &library)
{
    const GltfFilter magFilter = source.magFilter.value_or(GltfFilter::Linear);
    const GltfFilter minFilter = source.minFilter.value_or(GltfFilter::LinearMipmapLinear);

    const SamplerKey key{
        static_cast<std::uint16_t>(magFilter),
        static_cast<std::uint16_t>(minFilter),
        static_cast<std::uint16_t>(source.wrapS),
        static_cast<std::uint16_t>(source.wrapT)
    };

    for (const SamplerHandle &resource : library)
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
    library.push_back(resource);
    return resource;
}

void TriangleApplication::createTextureSampler()
{
    defaultTextureSampler = getOrCreateGltfSampler(GltfSamplerData{}, "Default texture sampler", samplerLibrary);
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

// 把解析出来的 gltf 材质描述，转换成 engine 绘制时使用的材质对象
// imported: cpu中的图像
// source: cpu侧的材质对象
MaterialHandle TriangleApplication::createGltfMaterial(
    const GltfImportData &imported,
    const GltfMaterialData &source,
    const std::string &debugName,
    GltfMaterialUpload& upload
)
{
    if (source.alphaMode == GltfAlphaMode::Blend)
    {
        throw std::runtime_error(debugName + ": BLEND materials are not supported");
    }

    // 把 gltf 里的一个材质纹理引用，转换为 Engine 自己的 MaterialTextureSlot
    // 使用 [&] 捕获，所以它可以直接访问 upload 参数
    auto makeSlot = [&](
        const std::optional<GltfMaterialTextureSlot>& reference,
        const ImageHandle& fallback,
        VkFormat format,
        const std::string& semantic
    ) -> MaterialTextureSlot
    {
        if (!reference)
        {
            return {fallback, defaultTextureSampler, 0u};
        }

        const std::string context = debugName + ": " + semantic;
        if (reference->textureIndex >= imported.textures.size())
        {
            throw std::runtime_error(context + ": texture index is out of range");
        }
        if (reference->texCoord > 1u)
        {
            throw std::runtime_error(context + ": unsupported UV set");
        }

        const GltfTextureRef& texture = imported.textures[reference->textureIndex];
        SamplerHandle sampler = defaultTextureSampler;

        if (texture.samplerIndex)
        {
            const std::size_t index = *texture.samplerIndex;
            if (index >= imported.samplers.size())
            {
                throw std::runtime_error(context + ": sampler index is out of range");
            }
            sampler = getOrCreateGltfSampler(
                imported.samplers[index],
                imported.sourcePath.u8string() + ": sampler[" + std::to_string(index) + "]",
                upload.samplers
            );
        }

        ImageHandle image = getOrCreateGltfImage(
            imported, texture.imageIndex, format, upload
        );

        return {image, sampler, reference->texCoord};
    };

    // Material 为完整的材质，包含多个 Material slot
    // 一个 Material slot 对应一个 Texture
    MaterialHandle material = std::make_shared<Material>();
    material->name = source.name.empty() ? debugName : source.name;
    material->baseColorFactor = source.baseColorFactor;
    material->metallicFactor = source.metallicFactor;
    material->roughnessFactor = source.roughnessFactor;
    material->emissiveFactor = source.emissiveFactor;
    material->normalScale = source.normalScale;
    material->occlusionStrength = source.occlusionStrength;
    material->doubleSided = source.doubleSided;
    material->alphaMode = source.alphaMode == GltfAlphaMode::Mask ? MaterialAlphaMode::Mask : MaterialAlphaMode::Opaque;
    material->alphaCutoff = source.alphaCutoff;

    material->baseColorTexture = makeSlot(
        source.baseColorTexture,
        defaultBaseColorTexture,
        VK_FORMAT_R8G8B8A8_SRGB,
        "baseColorTexture"
    );
    material->normalTexture = makeSlot(
        source.normalTexture,
        defaultNormalTexture,
        VK_FORMAT_R8G8B8A8_UNORM,
        "normalTexture"
    );
    material->metallicRoughnessTexture = makeSlot(
        source.metallicRoughnessTexture,
        defaultMetallicRoughnessTexture,
        VK_FORMAT_R8G8B8A8_UNORM,
        "metallicRoughnessTexture"
    );
    material->aoTexture = makeSlot(
        source.occlusionTexture,
        defaultAoTexture,
        VK_FORMAT_R8G8B8A8_UNORM,
        "occlusionTexture"
    );
    material->emissiveTexture = makeSlot(
        source.emissiveTexture,
        defaultEmissiveTexture,
        VK_FORMAT_R8G8B8A8_SRGB,
        "emissiveTexture");
    return material;
}
