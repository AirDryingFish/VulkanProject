#include "assets/image/SkyboxImage.hpp"

#include <stb_image.h>
#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>

namespace
{
constexpr float PI = 3.14159265358979323846f;

constexpr std::array<std::array<unsigned char, 4>, 6> fallbackSkyboxColors = {
    std::array<unsigned char, 4>{70, 110, 180, 255},  std::array<unsigned char, 4>{65, 100, 165, 255},
    std::array<unsigned char, 4>{115, 155, 215, 255}, std::array<unsigned char, 4>{35, 45, 65, 255},
    std::array<unsigned char, 4>{80, 125, 190, 255},  std::array<unsigned char, 4>{50, 80, 140, 255},
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

void sampleEquirectangularHdr(const float* hdrPixels, int width, int height, const glm::vec3& direction,
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

    const float* p00 = hdrPixels + (static_cast<size_t>(y0) * width + x0) * 4;
    const float* p10 = hdrPixels + (static_cast<size_t>(y0) * width + x1) * 4;
    const float* p01 = hdrPixels + (static_cast<size_t>(y1) * width + x0) * 4;
    const float* p11 = hdrPixels + (static_cast<size_t>(y1) * width + x1) * 4;

    for (int channel = 0; channel < 4; channel++)
    {
        const float top = p00[channel] * (1.0f - tx) + p10[channel] * tx;
        const float bottom = p01[channel] * (1.0f - tx) + p11[channel] * tx;
        outColor[channel] = top * (1.0f - ty) + bottom * ty;
    }
    outColor[3] = 1.0f;
}

SkyboxPixels loadHdrSkybox(const std::string& path)
{
    int hdrWidth = 0;
    int hdrHeight = 0;
    int channels = 0;
    float* hdrPixels = stbi_loadf(path.c_str(), &hdrWidth, &hdrHeight, &channels, STBI_rgb_alpha);
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

void writeLdrPixelAsHdr(std::uint16_t* dst, const unsigned char* src)
{
    for (int channel = 0; channel < 3; channel++)
    {
        const float srgb = static_cast<float>(src[channel]) / 255.0f;
        dst[channel] = packHalf(inverseReinhard(srgbToLinear(srgb)));
    }
    dst[3] = packHalf(1.0f);
}

SkyboxPixels loadLdrFaceSkybox(const std::array<std::string, 6>& facePaths)
{
    int width = 0;
    int height = 0;
    std::array<stbi_uc*, 6> loadedPixels{};
    bool useFallback = false;

    for (size_t i = 0; i < facePaths.size(); i++)
    {
        int faceWidth = 0;
        int faceHeight = 0;
        int channels = 0;
        loadedPixels[i] = stbi_load(facePaths[i].c_str(), &faceWidth, &faceHeight, &channels, STBI_rgb_alpha);
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
        for (stbi_uc* pixels : loadedPixels)
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

    for (size_t face = 0; face < facePaths.size(); face++)
    {
        for (uint32_t y = 0; y < skybox.height; y++)
        {
            for (uint32_t x = 0; x < skybox.width; x++)
            {
                const unsigned char* src = nullptr;
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
} // namespace

SkyboxPixels loadSkyboxImage(const std::string& hdrPath, const std::array<std::string, 6>& facePaths)
{
    SkyboxPixels pixels = loadHdrSkybox(hdrPath);
    if (pixels.empty())
    {
        pixels = loadLdrFaceSkybox(facePaths);
    }
    return pixels;
}
