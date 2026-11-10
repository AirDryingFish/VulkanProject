#pragma once

#include "scene/Lighting.hpp"

#include <cstdint>
#include <vector>

struct ManyLightsSettings
{
    std::uint32_t count = 256;
    float range = 3.0f;
    std::uint32_t seed = 0x5EEDu;
};

std::vector<PointLight> makeManyLights(const ManyLightsSettings& settings);