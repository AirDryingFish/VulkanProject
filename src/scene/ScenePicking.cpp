#include "scene/ScenePicking.hpp"
#include "scene/Scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{
bool intersectRayAabb(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& boundsMin,
                      const glm::vec3& boundsMax, float& hitDistance)
{
    // slab 法：分别求射线位于 X/Y/Z 区间内的 t 范围，三个范围有交集才命中。
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();

    for (int axis = 0; axis < 3; axis++)
    {
        if (std::abs(direction[axis]) < 0.000001f)
        {
            if (origin[axis] < boundsMin[axis] || origin[axis] > boundsMax[axis])
            {
                return false;
            }
            continue;
        }

        const float inverseDirection = 1.0f / direction[axis];
        float t1 = (boundsMin[axis] - origin[axis]) * inverseDirection;
        float t2 = (boundsMax[axis] - origin[axis]) * inverseDirection;
        if (t1 > t2)
        {
            std::swap(t1, t2);
        }

        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        if (tMin > tMax)
        {
            return false;
        }
    }

    hitDistance = tMin;
    return true;
}

void transformAabb(const glm::mat4& transform, const glm::vec3& localMin, const glm::vec3& localMax,
                   glm::vec3& worldMin, glm::vec3& worldMax)
{
    const std::array<glm::vec3, 8> corners = {
        glm::vec3(localMin.x, localMin.y, localMin.z), glm::vec3(localMax.x, localMin.y, localMin.z),
        glm::vec3(localMin.x, localMax.y, localMin.z), glm::vec3(localMax.x, localMax.y, localMin.z),
        glm::vec3(localMin.x, localMin.y, localMax.z), glm::vec3(localMax.x, localMin.y, localMax.z),
        glm::vec3(localMin.x, localMax.y, localMax.z), glm::vec3(localMax.x, localMax.y, localMax.z),
    };

    worldMin = glm::vec3(std::numeric_limits<float>::max());
    worldMax = glm::vec3(std::numeric_limits<float>::lowest());
    for (const glm::vec3& corner : corners)
    {
        const glm::vec3 point = glm::vec3(transform * glm::vec4(corner, 1.0f));
        worldMin = glm::min(worldMin, point);
        worldMax = glm::max(worldMax, point);
    }
}
} // namespace

ScenePickResult pickSceneObject(const Scene& scene, const glm::vec3& rayOrigin, const glm::vec3& rayDirection)
{
    int hitObjectIndex = -1;
    float bestHitDistance = std::numeric_limits<float>::max();
    for (size_t i = 0; i < scene.objects.size(); i++)
    {
        const SceneObject& object = scene.objects[i];
        if (!object.mesh || !object.mesh->valid())
        {
            continue;
        }

        const Mesh& mesh = *object.mesh;
        if (!mesh.boundsValid)
        {
            continue;
        }

        glm::vec3 worldBoundsMin;
        glm::vec3 worldBoundsMax;
        // 与绘制共用 modelMatrix；当前仅测试世界空间 AABB，属于粗略拾取而非逐三角形命中。
        transformAabb(object.modelMatrix(), mesh.boundsMin, mesh.boundsMax, worldBoundsMin, worldBoundsMax);

        float hitDistance = 0.0f;
        if (intersectRayAabb(rayOrigin, rayDirection, worldBoundsMin, worldBoundsMax, hitDistance) &&
            hitDistance < bestHitDistance)
        {
            bestHitDistance = hitDistance;
            hitObjectIndex = static_cast<int>(i);
        }
    }
    return {hitObjectIndex, hitObjectIndex >= 0 ? bestHitDistance : 0.0f};
}
