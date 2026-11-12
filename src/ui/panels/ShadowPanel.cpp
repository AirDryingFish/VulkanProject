#include "ui/panels/EditorPanels.hpp"
#include "render/shadow/ShadowCascades.hpp"
#include "scene/Camera.hpp"
#include <algorithm>
#include <imgui.h>

void editor_panels::drawShadows(EditorView& view)
{
    if (ImGui::BeginTabItem("Shadows"))
    {
        ImGui::BeginChild("ShadowsContent", ImVec2(0.0f, 0.0f));
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        ImGui::SeparatorText("Directional shadows");

        const char* shadowModeNames[] = {"Single", "Cascaded"};
        int selectedShadowMode = static_cast<int>(view.shadows.mode);

        if (ImGui::Combo("Shadow mode", &selectedShadowMode, shadowModeNames, IM_ARRAYSIZE(shadowModeNames)))
        {
            view.shadows.mode = static_cast<ShadowMode>(selectedShadowMode);
        }

        ImGui::BeginDisabled(view.shadows.mode == ShadowMode::Single);
        ImGui::SliderFloat("Cascade blend fraction", &view.shadows.blendFraction, 0.0f, 1.0f, "%.2f",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();

        ImGui::Checkbox("Shadows##DirectionalLight", &view.shadows.enabled);
        ImGui::Checkbox("Shadow PCF 3x3", &view.shadows.pcfEnabled);

        ImGui::SeparatorText("Shadow coverage");
        const float minimumShadowDistance = std::min(view.camera.nearPlane + 0.01f, view.camera.farPlane);
        ImGui::DragFloat("Shadow Distance", &view.shadows.shadowDistance, 0.5f, minimumShadowDistance,
                         view.camera.farPlane, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::Text("Receiver far: %.2f", std::min(view.camera.farPlane, view.shadows.shadowDistance));

        ImGui::BeginDisabled(view.shadows.mode == ShadowMode::Single);
        ImGui::SliderFloat("Split lambda", &view.shadows.splitLambda, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();

        ImGui::BeginDisabled(view.shadows.mode != ShadowMode::Single);
        ImGui::DragFloat3("Single center", &view.shadows.center.x, 0.1f);
        ImGui::DragFloat("Single half extent", &view.shadows.halfExtent, 0.1f, 0.1f, 10000.0f, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();

        ImGui::SeparatorText("Depth bias");
        ImGui::SliderFloat("Shadow constant bias", &view.shadows.constantBias, 0.0f, 5.0f, "%.2f",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Shadow slope bias", &view.shadows.slopeBias, 0.0f, 5.0f, "%.2f",
                           ImGuiSliderFlags_AlwaysClamp);
        // ImGui::SliderFloat("Shadow receiver bias", &shadows.receiverBias, 0.0f, 0.005f, "%.6f", ImGuiSliderFlags_AlwaysClamp);

        if (view.shadows.mode == ShadowMode::Cascaded)
        {
            ImGui::SliderFloat("CSM receiver bias (world)", &view.shadows.receiverBiasWorld, 0.0f, 1.0f, "%.5f",
                               ImGuiSliderFlags_AlwaysClamp);
        }
        else
        {
            ImGui::SliderFloat("Single receiver bias (depth)", &view.shadows.receiverBias, 0.0f, 0.005f, "%.6f",
                               ImGuiSliderFlags_AlwaysClamp);
        }

        if (ImGui::Button("Reset shadow bias"))
        {
            view.shadows.constantBias = 1.25f;
            view.shadows.slopeBias = 1.75f;
            view.shadows.receiverBias = 0.0005f;
            view.shadows.receiverBiasWorld = 0.07f;
        }
        ImGui::SeparatorText("Debug views");
        ImGui::Checkbox("Show shadow depth", &view.shadows.showDepth);
        // 调试显示

        int previewLayer = view.shadows.mode == ShadowMode::Single ? 0 : view.shadows.previewLayer;

        ImGui::BeginDisabled(view.shadows.mode == ShadowMode::Single);

        if (ImGui::SliderInt("Shadow preview layer", &previewLayer, 0, static_cast<int>(shadowCascadeCount) - 1, "%d",
                             ImGuiSliderFlags_AlwaysClamp))
        {
            view.shadows.previewLayer = previewLayer;
        }

        ImGui::EndDisabled();

        ImGui::TextWrapped("Bias corrects depth comparisons; PCF filters shadow edges. The depth preview shows raw "
                           "depth, not visibility.");

        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
}
