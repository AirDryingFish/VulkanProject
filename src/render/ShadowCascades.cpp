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