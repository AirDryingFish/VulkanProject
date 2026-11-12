#include "app/Application.hpp"
#include "scene/ScenePicking.hpp"
#include <GLFW/glfw3.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

void Application::processModelPicking()
{
    const bool leftMouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool leftMousePressed = leftMouseDown && !editor.leftMouseWasDown;
    editor.leftMouseWasDown = leftMouseDown;

    if (!leftMousePressed || editor.sceneClickConsumed || scene.objects.empty() || ImGui::GetIO().WantCaptureMouse ||
        editor.gizmoHoveredAxis != 0 || editor.gizmoActiveAxis != 0)
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

    glm::mat4 view = camera.view();
    glm::mat4 projection = camera.projection(swapchain.extent().width / static_cast<float>(swapchain.extent().height));

    // 反投影 Vulkan NDC 的近/远点（深度 0/1），透视除法后得到世界空间射线。
    // projection 已在 Camera 内翻转 Y，这里不再套用 OpenGL 的 Y/深度转换。
    const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
    glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;

    const glm::vec3 rayOrigin = glm::vec3(nearPoint);
    const glm::vec3 rayDirection = glm::normalize(glm::vec3(farPoint - nearPoint));

    const ScenePickResult hit = pickSceneObject(scene, rayOrigin, rayDirection);
    const int hitObjectIndex = hit.objectIndex;
    editor.pickDistance = hit.distance;
    editor.objectIndex = hitObjectIndex;
    if (hitObjectIndex >= 0)
    {
        editor.selection = SelectionKind::Object;
        editor.pointLightIndex = -1;
    }
    else
    {
        editor.selection = SelectionKind::None;
        editor.pointLightIndex = -1;
    }
}
