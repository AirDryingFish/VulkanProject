#pragma once

#include <glm/glm.hpp>

class Scene;

struct ScenePickResult
{
    int objectIndex = -1;
    float distance = 0.0f;
};

// 对有效网格的世界 AABB 求最近命中；direction 须为单位向量，距离以世界单位计。
ScenePickResult pickSceneObject(const Scene& scene, const glm::vec3& rayOrigin, const glm::vec3& rayDirection);
