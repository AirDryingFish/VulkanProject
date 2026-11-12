#include "render/deferred/DeferredMath.hpp"

#include <cmath>
#include <stdexcept>

namespace deferred
{
namespace
{
void validatePixel(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0)
    {
        throw std::invalid_argument("Pixel requires nonzero extent");
    }

    if (x >= width || y >= height)
    {
        throw std::out_of_range("Pixel is outside extent");
    }
}
} // namespace

// 把一个像素的中心位置，从屏幕像素空间 转到 ndc 空间中
glm::vec2 pixelCenterNdc(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height)
{
    validatePixel(x, y, width, height);

    return {(static_cast<float>(x) + 0.5f) / static_cast<float>(width) * 2.0f - 1.0f,
            (static_cast<float>(y) + 0.5f) / static_cast<float>(height) * 2.0f - 1.0f};
}

glm::vec3 reconstructWorldPosition(const glm::mat4& inverseViewProjection, const glm::vec2& ndcXY, float depth)
{
    if (!std::isfinite(ndcXY.x) || !std::isfinite(ndcXY.y) || !std::isfinite(depth) || depth < 0.0f || depth > 1.0f)
    {
        throw std::invalid_argument("Invalid NDC or Vulkan depth");
    }

    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (!std::isfinite(inverseViewProjection[col][row]))
            {
                throw std::invalid_argument("Inverse matrix must be finite");
            }
        }
    }

    const glm::vec4 worldH = inverseViewProjection * glm::vec4(ndcXY, depth, 1.0f);
    if (!std::isfinite(worldH.w) || worldH.w == 0.0f)
    {
        throw std::range_error("Invalid homogeneous W");
    }

    const glm::vec3 world = glm::vec3(worldH) / worldH.w;
    if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z))
    {
        throw std::range_error("Reconstructed position must be finite");
    }

    return world;
}

TileGrid makeTileGrid(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return {};
    }
    TileGrid grid{};
    grid.tilesX = width / tileSize + (width % tileSize != 0);
    grid.tilesY = height / tileSize + (height % tileSize != 0);
    grid.count = std::uint64_t{grid.tilesX} * grid.tilesY;
    return grid;
}

std::uint64_t tileIndexForPixel(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height)
{
    validatePixel(x, y, width, height);

    const TileGrid grid = makeTileGrid(width, height);
    // 从左到右，从上到下
    return std::uint64_t{y / tileSize} * grid.tilesX + x / tileSize;
}

} // namespace deferred
