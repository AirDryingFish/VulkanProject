#include "scene/ManyLights.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

namespace
{
// 生成可重复的随机整数（线性同余伪随机数生成）
std::uint32_t nextRandom(std::uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}

// 生成 [0, 1) 随机浮点数
float nextUnit(std::uint32_t& state)
{
    // 高 24 位映射到 [0, 1)。
    return static_cast<float>(nextRandom(state) >> 8u) * (1.0f / 16777216.0f);
}

const std::array<glm::vec3, 6> lightColors{glm::vec3{1.0f, 0.20f, 0.12f}, glm::vec3{1.0f, 0.65f, 0.15f},
                                           glm::vec3{0.25f, 1.0f, 0.35f}, glm::vec3{0.15f, 0.75f, 1.0f},
                                           glm::vec3{0.35f, 0.25f, 1.0f}, glm::vec3{1.0f, 0.20f, 0.75f}};
} // namespace

std::vector<PointLight> makeManyLights(const ManyLightsSettings& settings)
{
    if (settings.count > MAX_POINT_LIGHTS)
    {
        throw std::invalid_argument("Many-lights count exceeds GPU capacity");
    }

    if (!std::isfinite(settings.range) || settings.range < 1.0f || settings.range > 12.0f)
    {
        throw std::invalid_argument("Many-lights range must be within [1, 12]");
    }

    std::vector<PointLight> lights;
    lights.reserve(settings.count);

    std::uint32_t state = settings.seed;

    for (std::uint32_t index = 0; index < settings.count; ++index)
    {
        PointLight light{};

        // 世界坐标 Z-up。
        // 每盏灯固定消耗相同数量的随机数。
        light.position.x = -10.0f + 20.0f * nextUnit(state);
        light.position.y = -10.0f + 20.0f * nextUnit(state);
        light.position.z = 1.0f + 3.0f * nextUnit(state);

        const std::uint32_t colorIndex = (nextRandom(state) >> 16u) % static_cast<std::uint32_t>(lightColors.size());

        light.color = lightColors[colorIndex];
        light.intensity = 2.0f;
        light.range = settings.range;
        light.enabled = true;

        lights.push_back(light);
    }

    return lights;
}
