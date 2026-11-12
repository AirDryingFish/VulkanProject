#include "ui/panels/EditorPanels.hpp"

#include <algorithm>
#include <imgui.h>

void drawEditor(EditorView& view, const EditorActions& actions)
{
    ImGuiIO& io = ImGui::GetIO();
    // 每帧重新仲裁点击：UI/灯光覆盖层/gizmo 消费后，Picking 不应再选中背后的模型。
    view.editor.sceneClickConsumed = false;

    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(mainViewport->WorkPos.x + 12.0f, mainViewport->WorkPos.y + 12.0f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::min(460.0f, std::max(240.0f, mainViewport->WorkSize.x - 24.0f)),
                                    std::max(240.0f, mainViewport->WorkSize.y - 24.0f)),
                             ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Scene Editor"))
    {
        ImGui::Text("%.1f FPS  |  %.2f ms", io.Framerate, io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
        ImGui::Separator();

        if (ImGui::BeginTabBar("EditorTabs", ImGuiTabBarFlags_FittingPolicyScroll))
        {
            editor_panels::drawScene(view, actions);

            editor_panels::drawInspector(view, actions);

            editor_panels::drawLighting(view, actions);

            editor_panels::drawShadows(view);

            editor_panels::drawCamera(view, actions);

            if (ImGui::BeginTabItem("Diagnostics"))
            {
                editor_panels::drawDiagnostics(view);
                ImGui::EndTabItem();
            }

            editor_panels::drawDisplay(view);

            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    editor_panels::drawViewport(view);
}
