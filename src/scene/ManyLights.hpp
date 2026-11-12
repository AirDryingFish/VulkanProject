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

// 按固定种子生成 Z-up 世界中的灯光；数量和范围在此配置入口验证。
std::vector<PointLight> makeManyLights(const ManyLightsSettings& settings);
