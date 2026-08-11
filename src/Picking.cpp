#include "TriangleApplication.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{
bool intersectRayAabb(const glm::vec3 &origin, const glm::vec3 &direction, const glm::vec3 &boundsMin, const glm::vec3 &boundsMax, float &hitDistance)
{
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

void transformAabb(const glm::mat4 &transform, const glm::vec3 &localMin, const glm::vec3 &localMax, glm::vec3 &worldMin, glm::vec3 &worldMax)
{
    const std::array<glm::vec3, 8> corners = {
        glm::vec3(localMin.x, localMin.y, localMin.z),
        glm::vec3(localMax.x, localMin.y, localMin.z),
        glm::vec3(localMin.x, localMax.y, localMin.z),
        glm::vec3(localMax.x, localMax.y, localMin.z),
        glm::vec3(localMin.x, localMin.y, localMax.z),
        glm::vec3(localMax.x, localMin.y, localMax.z),
        glm::vec3(localMin.x, localMax.y, localMax.z),
        glm::vec3(localMax.x, localMax.y, localMax.z),
    };

    worldMin = glm::vec3(std::numeric_limits<float>::max());
    worldMax = glm::vec3(std::numeric_limits<float>::lowest());
    for (const glm::vec3 &corner : corners)
    {
        const glm::vec3 point = glm::vec3(transform * glm::vec4(corner, 1.0f));
        worldMin = glm::min(worldMin, point);
        worldMax = glm::max(worldMax, point);
    }
}
}

void TriangleApplication::computeModelBounds()
{
    if (vertices.empty())
    {
        modelBoundsValid = false;
        return;
    }

    modelLocalBoundsMin = vertices[0].pos;
    modelLocalBoundsMax = vertices[0].pos;

    for (const Vertex &vertex : vertices)
    {
        modelLocalBoundsMin = glm::min(modelLocalBoundsMin, vertex.pos);
        modelLocalBoundsMax = glm::max(modelLocalBoundsMax, vertex.pos);
    }

    modelBoundsValid = true;
}

void TriangleApplication::processModelPicking()
{
    const bool leftMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool leftMousePressed = leftMouseDown && !leftMouseWasDown;
    leftMouseWasDown = leftMouseDown;

    if (!leftMousePressed || sceneClickConsumed || sceneObjects.empty() || ImGui::GetIO().WantCaptureMouse || gizmoHoveredAxis != 0 || gizmoActiveAxis != 0)
    {
        return;
    }

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0)
    {
        return;
    }

    const float ndcX = (2.0f * static_cast<float>(mouseX)) / static_cast<float>(width) - 1.0f;
    const float ndcY = (2.0f * static_cast<float>(mouseY)) / static_cast<float>(height) - 1.0f;

    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), swapchain.extent().width / static_cast<float>(swapchain.extent().height), cameraNear, cameraFar);
    projection[1][1] *= -1;

    const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
    glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;

    const glm::vec3 rayOrigin = glm::vec3(nearPoint);
    const glm::vec3 rayDirection = glm::normalize(glm::vec3(farPoint - nearPoint));

    int hitObjectIndex = -1;
    float bestHitDistance = std::numeric_limits<float>::max();
    for (size_t i = 0; i < sceneObjects.size(); i++)
    {
        const SceneObject &object = sceneObjects[i];
        if (!object.boundsValid)
        {
            continue;
        }

        glm::vec3 worldBoundsMin;
        glm::vec3 worldBoundsMax;
        transformAabb(getObjectMatrix(object), object.localBoundsMin, object.localBoundsMax, worldBoundsMin, worldBoundsMax);

        float hitDistance = 0.0f;
        if (intersectRayAabb(rayOrigin, rayDirection, worldBoundsMin, worldBoundsMax, hitDistance) && hitDistance < bestHitDistance)
        {
            bestHitDistance = hitDistance;
            hitObjectIndex = static_cast<int>(i);
        }
    }

    selectedModel = hitObjectIndex >= 0;
    modelPickDistance = selectedModel ? bestHitDistance : 0.0f;
    selectedSceneObjectIndex = hitObjectIndex;
    if (selectedModel)
    {
        selectedObject = SceneSelection::Model;
        selectedPointLightIndex = -1;
    }
    else
    {
        selectedObject = SceneSelection::None;
        selectedPointLightIndex = -1;
    }
}
