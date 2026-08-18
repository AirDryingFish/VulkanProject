#include "TriangleApplication.hpp"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/gtc/packing.hpp>

namespace
{
constexpr float PI = 3.14159265358979323846f;
constexpr VkFormat skyboxFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

constexpr std::array<std::array<unsigned char, 4>, 6> fallbackSkyboxColors = {
    std::array<unsigned char, 4>{70, 110, 180, 255},
    std::array<unsigned char, 4>{65, 100, 165, 255},
    std::array<unsigned char, 4>{115, 155, 215, 255},
    std::array<unsigned char, 4>{35, 45, 65, 255},
    std::array<unsigned char, 4>{80, 125, 190, 255},
    std::array<unsigned char, 4>{50, 80, 140, 255},
};

struct SkyboxPixels
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::uint16_t> pixels;

    bool empty() const
    {
        return pixels.empty();
    }

    VkDeviceSize layerSize() const
    {
        return static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4 * sizeof(std::uint16_t);
    }

    VkDeviceSize imageSize() const
    {
        return layerSize() * 6;
    }
};

float srgbToLinear(float value)
{
    if (value <= 0.04045f)
    {
        return value / 12.92f;
    }

    return std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float inverseReinhard(float value)
{
    constexpr float maxValue = 64.0f;
    value = std::clamp(value, 0.0f, 0.999f);
    return std::min(value / std::max(1.0f - value, 0.001f), maxValue);
}

std::uint16_t packHalf(float value)
{
    return static_cast<std::uint16_t>(glm::packHalf1x16(std::max(value, 0.0f)));
}

glm::vec3 cubeFaceDirection(uint32_t face, float u, float v)
{
    const float x = 2.0f * u - 1.0f;
    const float y = 2.0f * v - 1.0f;

    switch (face)
    {
    case 0:
        return glm::normalize(glm::vec3(1.0f, -y, -x)); // +X
    case 1:
        return glm::normalize(glm::vec3(-1.0f, -y, x)); // -X
    case 2:
        return glm::normalize(glm::vec3(x, 1.0f, y)); // +Y
    case 3:
        return glm::normalize(glm::vec3(x, -1.0f, -y)); // -Y
    case 4:
        return glm::normalize(glm::vec3(x, -y, 1.0f)); // +Z
    default:
        return glm::normalize(glm::vec3(-x, -y, -1.0f)); // -Z
    }
}

void sampleEquirectangularHdr(
    const float *hdrPixels,
    int width,
    int height,
    const glm::vec3 &direction,
    float outColor[4])
{
    const float longitude = std::atan2(direction.z, direction.x);
    const float latitude = std::asin(std::clamp(direction.y, -1.0f, 1.0f));

    float sourceX = (longitude / (2.0f * PI) + 0.5f) * static_cast<float>(width) - 0.5f;
    float sourceY = (0.5f - latitude / PI) * static_cast<float>(height) - 0.5f;

    sourceX = std::fmod(sourceX, static_cast<float>(width));
    if (sourceX < 0.0f)
    {
        sourceX += static_cast<float>(width);
    }
    sourceY = std::clamp(sourceY, 0.0f, static_cast<float>(height - 1));

    const float xFloor = std::floor(sourceX);
    const float yFloor = std::floor(sourceY);
    const int x0 = static_cast<int>(xFloor);
    const int y0 = static_cast<int>(yFloor);
    const int x1 = (x0 + 1) % width;
    const int y1 = std::min(y0 + 1, height - 1);
    const float tx = sourceX - xFloor;
    const float ty = sourceY - yFloor;

    const float *p00 = hdrPixels + (static_cast<size_t>(y0) * width + x0) * 4;
    const float *p10 = hdrPixels + (static_cast<size_t>(y0) * width + x1) * 4;
    const float *p01 = hdrPixels + (static_cast<size_t>(y1) * width + x0) * 4;
    const float *p11 = hdrPixels + (static_cast<size_t>(y1) * width + x1) * 4;

    for (int channel = 0; channel < 4; channel++)
    {
        const float top = p00[channel] * (1.0f - tx) + p10[channel] * tx;
        const float bottom = p01[channel] * (1.0f - tx) + p11[channel] * tx;
        outColor[channel] = top * (1.0f - ty) + bottom * ty;
    }
    outColor[3] = 1.0f;
}

SkyboxPixels loadHdrSkybox(const std::string &path)
{
    int hdrWidth = 0;
    int hdrHeight = 0;
    int channels = 0;
    float *hdrPixels = stbi_loadf(path.c_str(), &hdrWidth, &hdrHeight, &channels, STBI_rgb_alpha);
    if (hdrPixels == nullptr || hdrWidth <= 0 || hdrHeight <= 0)
    {
        stbi_image_free(hdrPixels);
        return {};
    }

    SkyboxPixels skybox{};
    skybox.width = static_cast<uint32_t>(std::max(1, hdrWidth / 4));
    skybox.height = skybox.width;
    skybox.pixels.resize(static_cast<size_t>(skybox.width) * skybox.height * 6 * 4);

    for (uint32_t face = 0; face < 6; face++)
    {
        for (uint32_t y = 0; y < skybox.height; y++)
        {
            for (uint32_t x = 0; x < skybox.width; x++)
            {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(skybox.width);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(skybox.height);
                const glm::vec3 direction = cubeFaceDirection(face, u, v);

                float color[4]{};
                sampleEquirectangularHdr(hdrPixels, hdrWidth, hdrHeight, direction, color);

                const size_t pixelOffset = ((static_cast<size_t>(face) * skybox.height + y) * skybox.width + x) * 4;
                skybox.pixels[pixelOffset + 0] = packHalf(color[0]);
                skybox.pixels[pixelOffset + 1] = packHalf(color[1]);
                skybox.pixels[pixelOffset + 2] = packHalf(color[2]);
                skybox.pixels[pixelOffset + 3] = packHalf(1.0f);
            }
        }
    }

    stbi_image_free(hdrPixels);
    return skybox;
}

void writeLdrPixelAsHdr(std::uint16_t *dst, const unsigned char *src)
{
    for (int channel = 0; channel < 3; channel++)
    {
        const float srgb = static_cast<float>(src[channel]) / 255.0f;
        dst[channel] = packHalf(inverseReinhard(srgbToLinear(srgb)));
    }
    dst[3] = packHalf(1.0f);
}

SkyboxPixels loadLdrFaceSkybox()
{
    int width = 0;
    int height = 0;
    std::array<stbi_uc *, 6> loadedPixels{};
    bool useFallback = false;

    for (size_t i = 0; i < SKYBOX_FACE_PATHS.size(); i++)
    {
        int faceWidth = 0;
        int faceHeight = 0;
        int channels = 0;
        loadedPixels[i] = stbi_load(SKYBOX_FACE_PATHS[i].c_str(), &faceWidth, &faceHeight, &channels, STBI_rgb_alpha);
        if (loadedPixels[i] == nullptr || faceWidth != faceHeight)
        {
            useFallback = true;
            break;
        }

        if (i == 0)
        {
            width = faceWidth;
            height = faceHeight;
        }
        else if (faceWidth != width || faceHeight != height)
        {
            useFallback = true;
            break;
        }
    }

    if (useFallback)
    {
        for (stbi_uc *pixels : loadedPixels)
        {
            stbi_image_free(pixels);
        }
        width = 1;
        height = 1;
    }

    SkyboxPixels skybox{};
    skybox.width = static_cast<uint32_t>(width);
    skybox.height = static_cast<uint32_t>(height);
    skybox.pixels.resize(static_cast<size_t>(skybox.width) * skybox.height * 6 * 4);

    for (size_t face = 0; face < SKYBOX_FACE_PATHS.size(); face++)
    {
        for (uint32_t y = 0; y < skybox.height; y++)
        {
            for (uint32_t x = 0; x < skybox.width; x++)
            {
                const unsigned char *src = nullptr;
                if (useFallback)
                {
                    src = fallbackSkyboxColors[face].data();
                }
                else
                {
                    src = loadedPixels[face] + (static_cast<size_t>(y) * skybox.width + x) * 4;
                }

                const size_t pixelOffset = ((face * skybox.height + y) * skybox.width + x) * 4;
                writeLdrPixelAsHdr(skybox.pixels.data() + pixelOffset, src);
            }
        }

        if (!useFallback)
        {
            stbi_image_free(loadedPixels[face]);
        }
    }

    return skybox;
}
}

void TriangleApplication::createSkyboxImage()
{
    
    SkyboxPixels pixels = loadHdrSkybox(SKYBOX_HDR_PATH);
    if (pixels.empty())
    {
        pixels = loadLdrFaceSkybox();
    }
    const uint32_t skyboxMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(pixels.width, pixels.height)))) + 1;
    
    std::string stagingDebugName = SKYBOX_HDR_PATH + " skybox";
    BufferDesc stagingDesc{};
    stagingDesc.size = pixels.imageSize();
    stagingDesc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    stagingDesc.debugName = stagingDebugName.c_str();
    
    GpuBuffer stagingBuffer = context.createBuffer(stagingDesc);

    void *data = nullptr;
    VK_CHECK(stagingBuffer.map(&data));
    std::memcpy(data, pixels.pixels.data(), static_cast<size_t>(pixels.imageSize()));
    stagingBuffer.unmap();

    const std::string imageDebugName = SKYBOX_HDR_PATH + " image";
    ImageDesc skyboxDecs{};
    skyboxDecs.extent = {
        pixels.width,
        pixels.height,
        1
    };
    skyboxDecs.mipLevels = skyboxMipLevels;
    skyboxDecs.arrayLayers = 6;
    skyboxDecs.samples = VK_SAMPLE_COUNT_1_BIT;
    skyboxDecs.format = skyboxFormat;
    skyboxDecs.tiling = VK_IMAGE_TILING_OPTIMAL;
    skyboxDecs.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    skyboxDecs.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    skyboxDecs.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    skyboxDecs.debugName = imageDebugName.c_str();

    skyboxImage = context.createImage(skyboxDecs);

    transitionImageLayout(
        skyboxImage.get(),
        skyboxFormat,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        skyboxMipLevels,
        6);

    renderer.immediateSubmit([&](VkCommandBuffer commandBuffer) {
        std::array<VkBufferImageCopy, 6> regions{};
        for (uint32_t i = 0; i < regions.size(); i++)
        {
            regions[i].bufferOffset = pixels.layerSize() * i;
            regions[i].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            regions[i].imageSubresource.mipLevel = 0;
            regions[i].imageSubresource.baseArrayLayer = i;
            regions[i].imageSubresource.layerCount = 1;
            regions[i].imageExtent = {
                pixels.width,
                pixels.height,
                1};
        }

        vkCmdCopyBufferToImage(
            commandBuffer,
            stagingBuffer.get(),
            skyboxImage.get(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<uint32_t>(regions.size()),
            regions.data());
    });

    // transitionImageLayout(
    //     skyboxImage.image,
    //     skyboxFormat,
    //     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    //     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    //     skyboxMipLevels,
    //     6);
    generateMipmaps(
        skyboxImage.get(),
        skyboxFormat,
        pixels.width,
        pixels.height,
        skyboxMipLevels, 
        6
    );

    skyboxImage.setView(context.createImageView(
        skyboxImage.get(),
        skyboxFormat,
        skyboxMipLevels,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_VIEW_TYPE_CUBE,
        6));
}

void TriangleApplication::createSkyboxSampler()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_TRUE;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context.physicalDevice(), &properties);
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;

    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(skyboxImage.mipLevels() - 1);

    VK_CHECK(vkCreateSampler(context.device(), &samplerInfo, nullptr, &skyboxSampler));

    mainDeletionQueue.pushFunction([this, sampler = skyboxSampler]() {
        vkDestroySampler(context.device(), sampler, nullptr);
    });
}
