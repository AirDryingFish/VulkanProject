#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace deferred
{
    inline constexpr std::uint32_t tileSize = 16;
    inline constexpr std::uint32_t maxLightsPerTile = 128;

    struct TileGrid
    {
        std::uint32_t tilesX = 0;
        std::uint32_t tilesY = 0;
        std::uint64_t count = 0;
    };

    // 像素中心 -> NDC XY
    glm::vec2 pixelCenterNdc(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height);

    // NDC XY + depth -> 世界坐标
    glm::vec3 reconstructWorldPosition(const glm::mat4& inverseViewProjection, const glm::vec2& ndcXY, float depth);

    // 分辨率 -> tile 数量
    TileGrid makeTileGrid(std::uint32_t width, std::uint32_t height);

    // 像素 -> 行优先 tile 索引
    std::uint64_t tileIndexForPixel(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height);

}