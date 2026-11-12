#include "ui/panels/EditorPanels.hpp"
#include "scene/Camera.hpp"
#include "vulkan/presentation/Swapchain.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>

namespace
{
ImVec2 operator+(const ImVec2& a, const ImVec2& b)
{
    return ImVec2(a.x + b.x, a.y + b.y);
}

ImVec2 operator-(const ImVec2& a, const ImVec2& b)
{
    return ImVec2(a.x - b.x, a.y - b.y);
}

ImVec2 operator*(const ImVec2& v, float scale)
{
    return ImVec2(v.x * scale, v.y * scale);
}

float dot(const ImVec2& a, const ImVec2& b)
{
    return a.x * b.x + a.y * b.y;
}

float length(const ImVec2& v)
{
    return std::sqrt(dot(v, v));
}

ImVec2 normalize(const ImVec2& v)
{
    const float len = length(v);
    if (len < 0.0001f)
    {
        return ImVec2(0.0f, 0.0f);
    }
    return v * (1.0f / len);
}

float distancePointToSegment(const ImVec2& point, const ImVec2& a, const ImVec2& b)
{
    const ImVec2 ab = b - a;
    const float abLengthSq = dot(ab, ab);
    if (abLengthSq < 0.0001f)
    {
        return length(point - a);
    }

    const float t = std::clamp(dot(point - a, ab) / abLengthSq, 0.0f, 1.0f);
    const ImVec2 closest = a + ab * t;
    return length(point - closest);
}

bool worldToScreen(const glm::vec3& worldPoint, const glm::mat4& view, const glm::mat4& projection,
                   const ImGuiViewport* viewport, ImVec2& screenPoint)
{
    glm::vec4 clip = projection * view * glm::vec4(worldPoint, 1.0f);
    if (clip.w <= 0.0001f)
    {
        return false;
    }

    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < 0.0f || ndc.z > 1.0f)
    {
        return false;
    }

    screenPoint.x = viewport->Pos.x + (ndc.x * 0.5f + 0.5f) * viewport->Size.x;
    screenPoint.y = viewport->Pos.y + (ndc.y * 0.5f + 0.5f) * viewport->Size.y;
    return true;
}

void buildMouseRay(const ImVec2& mouse, const glm::mat4& view, const glm::mat4& projection,
                   const ImGuiViewport* viewport, glm::vec3& rayOrigin, glm::vec3& rayDirection)
{
    const float ndcX = ((mouse.x - viewport->Pos.x) / viewport->Size.x) * 2.0f - 1.0f;
    const float ndcY = ((mouse.y - viewport->Pos.y) / viewport->Size.y) * 2.0f - 1.0f;

    const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
    glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;

    rayOrigin = glm::vec3(nearPoint);
    rayDirection = glm::normalize(glm::vec3(farPoint - nearPoint));
}

bool intersectRayPlane(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::vec3& planePoint,
                       const glm::vec3& planeNormal, glm::vec3& hitPoint)
{
    const float denominator = glm::dot(rayDirection, planeNormal);
    if (std::abs(denominator) < 0.0001f)
    {
        return false;
    }

    const float distance = glm::dot(planePoint - rayOrigin, planeNormal) / denominator;
    if (distance < 0.0f)
    {
        return false;
    }

    hitPoint = rayOrigin + rayDirection * distance;
    return true;
}

glm::vec3 makeGizmoDragPlaneNormal(const glm::vec3& axisDirection, const glm::vec3& viewDirection,
                                   const glm::vec3& viewUp)
{
    glm::vec3 planeNormal = viewDirection - axisDirection * glm::dot(viewDirection, axisDirection);
    if (glm::length(planeNormal) > 0.0001f)
    {
        return glm::normalize(planeNormal);
    }

    planeNormal = viewUp - axisDirection * glm::dot(viewUp, axisDirection);
    if (glm::length(planeNormal) > 0.0001f)
    {
        return glm::normalize(planeNormal);
    }

    planeNormal = glm::cross(axisDirection, glm::vec3(1.0f, 0.0f, 0.0f));
    if (glm::length(planeNormal) > 0.0001f)
    {
        return glm::normalize(planeNormal);
    }

    return glm::normalize(glm::cross(axisDirection, glm::vec3(0.0f, 1.0f, 0.0f)));
}

void drawAxisLine(ImDrawList* drawList, const ImVec2& origin, const glm::mat3& viewRotation, const glm::vec3& axis,
                  ImU32 color, const char* label)
{
    glm::vec3 viewAxis = viewRotation * axis;
    ImVec2 direction(viewAxis.x, -viewAxis.y);
    float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (length < 0.001f)
    {
        direction = ImVec2(0.0f, viewAxis.z < 0.0f ? -1.0f : 1.0f);
        length = 1.0f;
    }

    direction = direction * (1.0f / length);
    const float axisLength = 36.0f;
    const ImVec2 end = origin + direction * axisLength;
    const ImVec2 normal(-direction.y, direction.x);

    drawList->AddLine(origin, end, color, 3.0f);
    drawList->AddTriangleFilled(end, end - direction * 8.0f + normal * 4.0f, end - direction * 8.0f - normal * 4.0f,
                                color);
    drawList->AddText(end + direction * 6.0f + ImVec2(-4.0f, -7.0f), color, label);
}

ImU32 axisColor(int axis, bool highlighted)
{
    if (axis == 1)
    {
        return highlighted ? IM_COL32(255, 95, 95, 255) : IM_COL32(230, 60, 60, 255);
    }
    if (axis == 2)
    {
        return highlighted ? IM_COL32(120, 255, 130, 255) : IM_COL32(80, 210, 90, 255);
    }
    return highlighted ? IM_COL32(120, 170, 255, 255) : IM_COL32(80, 140, 255, 255);
}

const char* axisLabel(int axis)
{
    if (axis == 1)
    {
        return "X";
    }
    if (axis == 2)
    {
        return "Y";
    }
    return "Z";
}
} // namespace

namespace
{
void drawLightOverlays(EditorView& editorView)
{
    if (!editorView.editor.showPointLightMarkers || editorView.scene.pointLights.empty())
    {
        return;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGuiIO& io = ImGui::GetIO();

    glm::mat4 view = editorView.camera.view();
    glm::mat4 projection = editorView.camera.projection(editorView.swapchain.extent().width /
                                                        static_cast<float>(editorView.swapchain.extent().height));

    const ImVec2 mouse = io.MousePos;
    int hoveredLight = -1;
    float bestDistance = 12.0f;

    std::vector<ImVec2> lightScreens(editorView.scene.pointLights.size());
    std::vector<bool> lightVisible(editorView.scene.pointLights.size(), false);

    for (size_t i = 0; i < editorView.scene.pointLights.size(); i++)
    {
        lightVisible[i] =
            worldToScreen(editorView.scene.pointLights[i].position, view, projection, viewport, lightScreens[i]);
        if (!lightVisible[i])
        {
            continue;
        }

        const float distance = length(mouse - lightScreens[i]);
        if (!io.WantCaptureMouse && editorView.editor.gizmoHoveredAxis == 0 && editorView.editor.gizmoActiveAxis == 0 &&
            distance < bestDistance)
        {
            bestDistance = distance;
            hoveredLight = static_cast<int>(i);
        }
    }

    if (hoveredLight >= 0)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            editorView.editor.selection = SelectionKind::PointLight;
            editorView.editor.objectIndex = -1;
            editorView.editor.pointLightIndex = hoveredLight;
            editorView.editor.sceneClickConsumed = true;
        }
    }

    for (size_t i = 0; i < editorView.scene.pointLights.size(); i++)
    {
        if (!lightVisible[i])
        {
            continue;
        }

        const PointLight& light = editorView.scene.pointLights[i];
        const bool selected = editorView.editor.selection == SelectionKind::PointLight &&
                              editorView.editor.pointLightIndex == static_cast<int>(i);
        const bool hovered = hoveredLight == static_cast<int>(i);
        const ImVec2 screen = lightScreens[i];
        const ImU32 color =
            IM_COL32(static_cast<int>(std::clamp(light.color.r, 0.0f, 1.0f) * 255.0f),
                     static_cast<int>(std::clamp(light.color.g, 0.0f, 1.0f) * 255.0f),
                     static_cast<int>(std::clamp(light.color.b, 0.0f, 1.0f) * 255.0f), light.enabled ? 255 : 110);

        drawList->AddCircleFilled(screen, selected || hovered ? 7.0f : 5.0f, color, 18);
        drawList->AddCircle(screen, selected ? 10.0f : 7.0f, IM_COL32(255, 255, 255, selected ? 240 : 120), 18,
                            selected ? 2.0f : 1.0f);
        drawList->AddText(screen + ImVec2(10.0f, -8.0f), IM_COL32(255, 255, 255, 180), "Light");
    }
}

void drawTransformGizmo(EditorView& editorView)
{
    glm::vec3* selectedPosition = nullptr;
    glm::vec3 originWorld(0.0f);
    glm::vec3 axisDirections[3] = {
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
    };

    if (editorView.editor.selection == SelectionKind::Object)
    {
        SceneObject* object = editorView.editor.selectedSceneObject(editorView.scene);
        if (object == nullptr)
        {
            editorView.editor.gizmoHoveredAxis = 0;
            editorView.editor.gizmoActiveAxis = 0;
            return;
        }

        selectedPosition = &object->transform.position;
        glm::mat4 model = object->modelMatrix();
        originWorld = glm::vec3(model * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        axisDirections[0] = glm::normalize(glm::vec3(model * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
        axisDirections[1] = glm::normalize(glm::vec3(model * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
        axisDirections[2] = glm::normalize(glm::vec3(model * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
    }
    else if (editorView.editor.selection == SelectionKind::PointLight && editorView.editor.pointLightIndex >= 0 &&
             editorView.editor.pointLightIndex < static_cast<int>(editorView.scene.pointLights.size()))
    {
        selectedPosition = &editorView.scene.pointLights[editorView.editor.pointLightIndex].position;
        originWorld = *selectedPosition;
    }
    else
    {
        editorView.editor.gizmoHoveredAxis = 0;
        editorView.editor.gizmoActiveAxis = 0;
        return;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGuiIO& io = ImGui::GetIO();

    glm::mat4 view = editorView.camera.view();
    glm::mat4 projection = editorView.camera.projection(editorView.swapchain.extent().width /
                                                        static_cast<float>(editorView.swapchain.extent().height));

    const float distanceToCamera = glm::length(editorView.camera.position - originWorld);
    const float axisWorldLength = std::clamp(distanceToCamera * 0.18f, 0.3f, 3.0f);

    ImVec2 originScreen;
    if (!worldToScreen(originWorld, view, projection, viewport, originScreen))
    {
        editorView.editor.gizmoHoveredAxis = 0;
        return;
    }

    struct AxisScreenData
    {
        ImVec2 end;
        bool visible = false;
        bool interactive = false;
        float screenLength = 0.0f;
    };

    AxisScreenData axes[3];
    constexpr float minInteractiveAxisScreenLength = 24.0f;
    for (int i = 0; i < 3; i++)
    {
        const glm::vec3 axisEndWorld = originWorld + axisDirections[i] * axisWorldLength;
        axes[i].visible = worldToScreen(axisEndWorld, view, projection, viewport, axes[i].end);
        axes[i].screenLength = axes[i].visible ? length(axes[i].end - originScreen) : 0.0f;
        axes[i].interactive = axes[i].screenLength >= minInteractiveAxisScreenLength;
    }

    const ImVec2 mouse = io.MousePos;
    editorView.editor.gizmoHoveredAxis = 0;
    if (editorView.editor.gizmoActiveAxis == 0 && !io.WantCaptureMouse)
    {
        float bestDistance = 12.0f;
        for (int i = 0; i < 3; i++)
        {
            if (!axes[i].visible || !axes[i].interactive)
            {
                continue;
            }

            const float distance = distancePointToSegment(mouse, originScreen, axes[i].end);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                editorView.editor.gizmoHoveredAxis = i + 1;
            }
        }
    }

    if (editorView.editor.gizmoHoveredAxis != 0)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    if (editorView.editor.gizmoActiveAxis == 0 && editorView.editor.gizmoHoveredAxis != 0 &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const int axisIndex = editorView.editor.gizmoHoveredAxis - 1;
        glm::vec3 rayOrigin;
        glm::vec3 rayDirection;
        glm::vec3 hitPoint;
        const glm::vec3 planeNormal =
            makeGizmoDragPlaneNormal(axisDirections[axisIndex], editorView.camera.front, editorView.camera.up);

        buildMouseRay(mouse, view, projection, viewport, rayOrigin, rayDirection);
        if (intersectRayPlane(rayOrigin, rayDirection, originWorld, planeNormal, hitPoint))
        {
            editorView.editor.gizmoActiveAxis = editorView.editor.gizmoHoveredAxis;
            editorView.editor.gizmoDragStartPosition = *selectedPosition;
            editorView.editor.gizmoDragAxis = axisDirections[axisIndex];
            editorView.editor.gizmoDragPlaneNormal = planeNormal;
            editorView.editor.gizmoDragStartHitPoint = hitPoint;
        }
    }

    if (editorView.editor.gizmoActiveAxis != 0)
    {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            editorView.editor.gizmoActiveAxis = 0;
        }
        else
        {
            const int axisIndex = editorView.editor.gizmoActiveAxis - 1;
            glm::vec3 rayOrigin;
            glm::vec3 rayDirection;
            glm::vec3 hitPoint;

            buildMouseRay(mouse, view, projection, viewport, rayOrigin, rayDirection);
            if (intersectRayPlane(rayOrigin, rayDirection, editorView.editor.gizmoDragStartHitPoint,
                                  editorView.editor.gizmoDragPlaneNormal, hitPoint))
            {
                const float worldDistance =
                    glm::dot(hitPoint - editorView.editor.gizmoDragStartHitPoint, editorView.editor.gizmoDragAxis);
                *selectedPosition =
                    editorView.editor.gizmoDragStartPosition + editorView.editor.gizmoDragAxis * worldDistance;
            }
        }
    }

    drawList->AddCircleFilled(originScreen, 5.0f, IM_COL32(245, 245, 245, 255), 16);
    drawList->AddCircle(originScreen, 7.0f, IM_COL32(20, 20, 20, 180), 16, 1.5f);

    for (int i = 0; i < 3; i++)
    {
        if (!axes[i].visible)
        {
            continue;
        }

        const int axis = i + 1;
        const bool highlighted =
            axis == editorView.editor.gizmoHoveredAxis || axis == editorView.editor.gizmoActiveAxis;
        const ImU32 color = axisColor(axis, highlighted);
        const float thickness = highlighted ? 5.0f : 3.0f;
        const ImVec2 direction = normalize(axes[i].end - originScreen);
        const ImVec2 normal(-direction.y, direction.x);

        drawList->AddLine(originScreen, axes[i].end, color, thickness);
        drawList->AddTriangleFilled(axes[i].end, axes[i].end - direction * 12.0f + normal * 6.0f,
                                    axes[i].end - direction * 12.0f - normal * 6.0f, color);
        drawList->AddText(axes[i].end + direction * 8.0f + ImVec2(-4.0f, -7.0f), color, axisLabel(axis));
    }
}
} // namespace

void editor_panels::drawViewport(EditorView& editorView)
{
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float radius = 50.0f;
    const float margin = 24.0f;
    const ImVec2 origin(viewport->Pos.x + viewport->Size.x - margin - radius,
                        viewport->Pos.y + viewport->Size.y - margin - radius);

    glm::mat4 view = editorView.camera.view();
    glm::mat3 viewRotation(view);

    drawList->AddCircleFilled(origin, radius, IM_COL32(16, 18, 22, 150), 32);
    drawList->AddCircle(origin, radius, IM_COL32(255, 255, 255, 45), 32, 1.0f);
    drawAxisLine(drawList, origin, viewRotation, glm::vec3(1.0f, 0.0f, 0.0f), IM_COL32(230, 60, 60, 255), "X");
    drawAxisLine(drawList, origin, viewRotation, glm::vec3(0.0f, 1.0f, 0.0f), IM_COL32(80, 210, 90, 255), "Y");
    drawAxisLine(drawList, origin, viewRotation, glm::vec3(0.0f, 0.0f, 1.0f), IM_COL32(80, 140, 255, 255), "Z");

    drawLightOverlays(editorView);
    drawTransformGizmo(editorView);
}
