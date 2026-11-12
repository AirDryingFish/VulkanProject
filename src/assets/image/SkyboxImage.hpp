#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// 拥有线性 RGBA16F 像素，按 +X、-X、+Y、-Y、+Z、-Z 六面连续存储。
struct SkyboxPixels
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::uint16_t> pixels;

    bool empty() const
    {
        return pixels.empty();
    }

    std::uint64_t layerSize() const
    {
        return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4 * sizeof(std::uint16_t);
    }

    std::uint64_t imageSize() const
    {
        return layerSize() * 6;
    }
};

// 优先重采样等距柱状 HDR；失败则读取六张 LDR，缺失或尺寸不一致时沿用内建颜色。
// 路径仅在调用期间借用，返回像素不依赖 Vulkan 或渲染器。
[[nodiscard]]
SkyboxPixels loadSkyboxImage(const std::string& hdrPath, const std::array<std::string, 6>& facePaths);
