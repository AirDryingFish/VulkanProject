#include "ui/panels/EditorPanels.hpp"
#include "scene/Camera.hpp"
#include <algorithm>
#include <cmath>
#include <imgui.h>

void editor_panels::drawCamera(EditorView& view, const EditorActions& actions)
{
    if (ImGui::BeginTabItem("Camera"))
    {
        ImGui::BeginChild("CameraContent", ImVec2(0.0f, 0.0f));
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        ImGui::SeparatorText("Camera");

        const bool canSetAuditCamera =
            view.activeScenePreset == ScenePreset::ShadowPlayground && !view.pendingScenePreset.has_value();

        ImGui::BeginDisabled(!canSetAuditCamera);

        if (ImGui::Button("Use shadow test camera"))
        {
            view.diagnostics.baseline.cancel("Capture cancelled: test camera reset.");
            view.diagnostics.startRequested = false;

            view.camera.lookAt({14.0f, -22.0f, 14.0f}, {0.0f, 15.0f, 0.0f});

            view.camera.nearPlane = 0.1f;
            view.camera.farPlane = 100.0f;
            view.camera.fieldOfViewDegrees = 45.0f;
            view.camera.moveSpeed = 2.0f;

            actions.resetCameraInput();
        }

        ImGui::EndDisabled();

        ImGui::Text("Position: %.2f %.2f %.2f", view.camera.position.x, view.camera.position.y, view.camera.position.z);
        ImGui::Text("Front: %.2f %.2f %.2f", view.camera.front.x, view.camera.front.y, view.camera.front.z);
        ImGui::Text("Yaw/Pitch: %.1f %.1f", view.camera.yaw, view.camera.pitch);
        bool clippingEdited =
            ImGui::DragFloat("Near Plane", &view.camera.nearPlane, 0.01f, 0.01f,
                             std::min(10.0f, view.camera.farPlane - 0.01f), "%.3f", ImGuiSliderFlags_AlwaysClamp);
        clippingEdited |=
            ImGui::DragFloat("Far Plane", &view.camera.farPlane, 1.0f, std::max(10.0f, view.camera.nearPlane + 0.01f),
                             10000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        // Text entry must preserve a valid projection too.
        if (clippingEdited)
        {
            view.camera.nearPlane =
                std::clamp(std::isfinite(view.camera.nearPlane) ? view.camera.nearPlane : 0.1f, 0.01f, 10.0f);
            view.camera.farPlane = std::clamp(std::isfinite(view.camera.farPlane) ? view.camera.farPlane : 1000.0f,
                                              std::max(10.0f, view.camera.nearPlane + 0.01f), 10000.0f);
        }
        ImGui::DragFloat("Move Speed", &view.camera.moveSpeed, 0.1f, 0.1f, 30.0f);
        ImGui::DragFloat("Fast Multiplier", &view.camera.fastMultiplier, 0.1f, 1.0f, 10.0f);
        ImGui::DragFloat("Scroll Speed", &view.camera.scrollSpeed, 0.1f, 0.1f, 20.0f);
        ImGui::DragFloat("Pan Speed", &view.camera.panSpeed, 0.001f, 0.001f, 0.2f, "%.3f");
        ImGui::DragFloat("Mouse Sensitivity", &view.camera.mouseSensitivity, 0.01f, 0.01f, 2.0f);

        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
}
