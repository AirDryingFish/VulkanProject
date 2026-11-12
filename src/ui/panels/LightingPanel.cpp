#include "ui/panels/EditorPanels.hpp"
#include "render/ShaderTypes.hpp"
#include "scene/Camera.hpp"
#include <algorithm>
#include <array>
#include <string>
#include <imgui.h>

void editor_panels::drawLighting(EditorView& view, const EditorActions& actions)
{
    if (ImGui::BeginTabItem("Lighting"))
    {
        ImGui::BeginChild("LightingContent", ImVec2(0.0f, 0.0f));
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        ImGui::SeparatorText("Environment");
        ImGui::ColorEdit3("Ambient Color", &view.scene.ambientLightColor.x);
        ImGui::DragFloat("Ambient Intensity", &view.scene.ambientLightIntensity, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("IBL Intensity", &view.scene.iblIntensity, 0.01f, 0.0f, 5.0f);

        ImGui::Separator();
        ImGui::TextUnformatted("Directional Light");
        ImGui::Checkbox("Enabled##DirectionalLight", &view.scene.directionalLight.enabled);
        ImGui::DragFloat3("Direction##DirectionalLight", &view.scene.directionalLight.direction.x, 0.02f);
        ImGui::ColorEdit3("Color##DirectionalLight", &view.scene.directionalLight.color.x);
        ImGui::DragFloat("Intensity##DirectionalLight", &view.scene.directionalLight.intensity, 0.05f, 0.0f, 20.0f);

        if (ImGui::Checkbox("Show point light markers", &view.editor.showPointLightMarkers))
        {
            view.diagnostics.baseline.cancel("Capture cancelled: light markers changed.");
            view.diagnostics.startRequested = false;
        }

        if (view.activeScenePreset == ScenePreset::ManyLights)
        {
            ImGui::PushID("ManyLightsControls");
            ImGui::SeparatorText("Generated lights");

            ManyLightsSettings next = view.manyLightsSettings;
            int count = static_cast<int>(next.count);

            bool regenerate = ImGui::SliderInt("Generated count", &count, 0, static_cast<int>(MAX_POINT_LIGHTS), "%d",
                                               ImGuiSliderFlags_AlwaysClamp);

            next.count = static_cast<std::uint32_t>(count);

            const std::array<std::uint32_t, 6> counts{0u, 1u, 16u, 64u, 256u, 1024u};

            for (std::size_t i = 0; i < counts.size(); ++i)
            {
                if (i != 0)
                {
                    ImGui::SameLine();
                }

                ImGui::PushID(static_cast<int>(i));

                const std::string label = std::to_string(counts[i]);

                if (ImGui::Button(label.c_str()))
                {
                    next.count = counts[i];
                    regenerate = true;
                }

                ImGui::PopID();
            }

            regenerate |=
                ImGui::SliderFloat("Generated range", &next.range, 1.0f, 12.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

            ImGui::Text("Seed: 0x%X", static_cast<unsigned int>(next.seed));

            if (ImGui::Button("Rebuild generated lights"))
            {
                regenerate = true;
            }

            ImGui::TextWrapped("Changing generation settings replaces point lights and their edits.");

            if (regenerate)
            {
                actions.applyManyLightsSettings(next);
            }

            ImGui::PopID();
        }

        ImGui::SeparatorText("Point lights");
        const bool canAddLight = view.scene.pointLights.size() < MAX_POINT_LIGHTS;
        if (!canAddLight)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Add Point Light"))
        {
            PointLight light{};
            light.position = view.camera.position + view.camera.front * 2.0f;
            view.scene.pointLights.push_back(light);
            view.editor.selection = SelectionKind::PointLight;
            view.editor.objectIndex = -1;
            view.editor.pointLightIndex = static_cast<int>(view.scene.pointLights.size()) - 1;
            view.editor.sceneClickConsumed = true;
        }
        if (!canAddLight)
        {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        // ImGui::Text("%zu / %u", scene.pointLights.size(), MAX_POINT_LIGHTS);
        const auto activeLights =
            static_cast<std::size_t>(std::count_if(view.scene.pointLights.begin(), view.scene.pointLights.end(),
                                                   [](const PointLight& light) { return light.enabled; }));
        ImGui::Text("%zu active / %zu listed / %u capacity", activeLights, view.scene.pointLights.size(),
                    MAX_POINT_LIGHTS);

        for (size_t i = 0; i < view.scene.pointLights.size(); i++)
        {
            PointLight& light = view.scene.pointLights[i];
            ImGui::PushID(static_cast<int>(i));

            const bool selected = view.editor.selection == SelectionKind::PointLight &&
                                  view.editor.pointLightIndex == static_cast<int>(i);
            const std::string lightLabel = "Point Light " + std::to_string(i + 1);
            if (ImGui::Selectable(lightLabel.c_str(), selected))
            {
                view.editor.selection = SelectionKind::PointLight;
                view.editor.objectIndex = -1;
                view.editor.pointLightIndex = static_cast<int>(i);
                view.editor.sceneClickConsumed = true;
            }

            if (ImGui::TreeNode("Properties"))
            {
                ImGui::Checkbox("Enabled", &light.enabled);
                ImGui::DragFloat3("Position", &light.position.x, 0.05f);
                ImGui::ColorEdit3("Color", &light.color.x);
                ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 100.0f);
                ImGui::DragFloat("Range", &light.range, 0.05f, 0.1f, 100.0f);

                ImGui::TreePop();
            }
            ImGui::Separator();
            ImGui::PopID();
        }

        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
}
