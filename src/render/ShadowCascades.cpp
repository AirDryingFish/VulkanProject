#include "ShadowCascades.hpp"

#include <cmath>
#include <stdexcept>

std::array<float, shadowCascadeCount> calculateCascadeSplits(float nearPlane, float shadowFar, float lambda)
{
    if (!std::isfinite(nearPlane) ||
        !std::isfinite(shadowFar) ||
        !std::isfinite(lambda)    ||
        nearPlane <= 0.0f         ||
        shadowFar <= nearPlane    ||
        lambda < 0.0f             ||
        lambda > 1.0f)
    {
        throw std::invalid_argument("Invalid cascade split parameters");
    }

    const double n = static_cast<double>(nearPlane);
    const double f = static_cast<double>(shadowFar);
    const double weight = static_cast<double>(lambda);

    std::array<float, shadowCascadeCount> splits{};
    float previous = nearPlane;

    for (std::size_t i = 0; i < shadowCascadeCount; ++i)
    {
        const double t = static_cast<double>(i + 1) / static_cast<double>(shadowCascadeCount);

        const double uniform = n + (f - n) * t;
        const double logarithmic = n * std::pow(f / n, t);

        const double mixed = (1.0 - weight) * uniform + weight * logarithmic;

        const float split = i + 1 == shadowCascadeCount ? shadowFar : static_cast<float>(mixed);

        if (!std::isfinite(split) || split <= previous || split > shadowFar)
        {
            throw std::range_error("Cascade splits are not representable as increasing floats");
        }
        splits[i] = split;
        previous = split;
    }
    return splits;
}

FrustumSliceCorners calculateFrustumSliceCorners(const glm::mat4 &inverseView, float verticalFovDegrees, float aspectRatio, float nearDistance, float farDistance)
{
    if (!std::isfinite(verticalFovDegrees)  ||
        verticalFovDegrees <= 0.0f          ||
        verticalFovDegrees >= 180.0f        ||

        !std::isfinite(aspectRatio)         ||
        aspectRatio <= 0.0f                 ||

        !std::isfinite(nearDistance)        ||
        nearDistance <= 0.0f                ||

        !std::isfinite(farDistance)         ||
        farDistance <= nearDistance)
    {
        throw std::invalid_argument(
            "Invalid frustum slice parameters");
    }

    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (!std::isfinite(inverseView[col][row]))
            {
                throw std::invalid_argument("Inverse view must be finite");
            }
        }
    }

    const float tanHalfFov = std::tan(glm::radians(verticalFovDegrees) * 0.5f);

    const std::array<glm::vec2, 4> signs{{
        {-1.0f, -1.0f}, {1.0f, -1.0f},
        {1.0f, 1.0f}, {-1.0f, 1.0f}
    }};

    FrustumSliceCorners corners{};
    for (std::size_t plane = 0; plane < 2; ++plane)
    {
        const float distance = plane == 0 ? nearDistance : farDistance;

        const float halfY = distance * tanHalfFov;
        const float halfX = halfY * aspectRatio;

        for (std::size_t corner = 0; corner < signs.size(); ++corner)
        {
            // 前方为 -Z，w = 1 表示位置，包含相机平移
            const glm::vec4 viewPoint(
                signs[corner].x * halfX,
                signs[corner].y * halfY,
                -distance,
                1.0f
            );

            // 逆视图是仿射变换，这里没有投影，也不翻转 Y
            const glm::vec3 worldPoint(inverseView * viewPoint);

            if (!std::isfinite(worldPoint.x) ||
                !std::isfinite(worldPoint.y) ||
                !std::isfinite(worldPoint.z))
            {
                throw std::range_error(
                    "Frustum corner is not finite");
            }

            corners[plane * 4 + corner] = worldPoint;
        }
    }
    return corners;
}
