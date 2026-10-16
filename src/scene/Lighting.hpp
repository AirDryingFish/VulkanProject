#pragma once

#include <cstdint>
#include <glm/glm.hpp>

inline constexpr uint32_t MAX_POINT_LIGHTS = 16;

struct PointLight
{
    glm::vec3 position = {0.0f, 0.0f, 1.5f};
    glm::vec3 color = {1.0f, 0.92f, 0.78f};
    float intensity = 4.0f;
    float range = 8.0f;
    bool enabled = true;
};

struct DirectionalLight
{
    glm::vec3 direction{-1.0f, -1.0f, -2.0f};
    glm::vec3 color{1.0f};
    float intensity = 3.0f;
    bool enabled = true;
};
