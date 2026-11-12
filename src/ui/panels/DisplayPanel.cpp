#include "ui/panels/EditorPanels.hpp"
#include "render/Renderer.hpp"
#include "scene/Camera.hpp"
#include <algorithm>
#include <imgui.h>

void editor_panels::drawDisplay(EditorView& view)
{
    if (ImGui::BeginTabItem("Display"))
    {
        const bool canBuildGBuffer = view.renderer.gBufferGeometryAvailable();

        const bool canUseDeferred = view.renderer.deferredLightingAvailable();

        const char* pathNames[] = {"Forward", "Deferred"};

        int selectedPath = static_cast<int>(view.display.renderPath);

        if (ImGui::Combo("Render path", &selectedPath, pathNames, IM_ARRAYSIZE(pathNames)))
        {
            view.display.renderPath = selectedPath == 1 && canUseDeferred ? RenderPath::Deferred : RenderPath::Forward;

            view.diagnostics.baseline.cancel("Capture cancelled: render path changed.");
            view.diagnostics.startRequested = false;
        }

        if (!canUseDeferred)
        {
            ImGui::TextDisabled("Deferred is unavailable on this configuration.");
        }

        const bool deferredSelected = view.display.renderPath == RenderPath::Deferred && canUseDeferred;

        ImGui::BeginDisabled(!deferredSelected);

        const char* cullingNames[] = {"None (all lights)", "Tiled (frustum + depth)"};

        int cullingMode = static_cast<int>(view.display.lightCullingMode);

        if (ImGui::Combo("Light culling", &cullingMode, cullingNames, IM_ARRAYSIZE(cullingNames)))
        {
            view.display.lightCullingMode = cullingMode == 1 ? LightCullingMode::Tiled : LightCullingMode::None;

            view.diagnostics.baseline.cancel("Capture cancelled: light culling mode changed.");

            view.diagnostics.startRequested = false;
        }

        ImGui::EndDisabled();

        if (deferredSelected && view.display.lightCullingMode == LightCullingMode::Tiled)
        {
            if (!view.renderer.tiledLightingAvailable())
            {
                ImGui::TextWrapped("Using None: %s", view.renderer.tiledUnavailableReason().c_str());
            }
            else
            {
                ImGui::TextWrapped("Each tile uses its frustum and surface depth range. "
                                   "Overflow tiles use the full light loop.");
            }
        }

        bool buildGBuffer = deferredSelected || view.display.buildGBuffer;

        ImGui::BeginDisabled(!canBuildGBuffer || deferredSelected);

        if (ImGui::Checkbox("Build G-buffer", &buildGBuffer))
        {
            view.display.buildGBuffer = buildGBuffer;

            if (!buildGBuffer)
            {
                view.display.gBufferView = GBufferView::Off;
            }

            view.diagnostics.baseline.cancel("Capture cancelled: G-buffer mode changed.");
            view.diagnostics.startRequested = false;
        }

        ImGui::EndDisabled();

        ImGui::BeginDisabled(!canBuildGBuffer || !buildGBuffer);

        const char* gBufferViews[] = {"Rendered output", "World normal",      "Base color", "Metallic",
                                      "Roughness",       "Ambient occlusion", "Emissive",   "Linear depth"};

        int selectedBufferView = static_cast<int>(view.display.gBufferView);

        if (ImGui::Combo("G-buffer attachment", &selectedBufferView, gBufferViews, IM_ARRAYSIZE(gBufferViews)))
        {
            view.display.gBufferView = static_cast<GBufferView>(selectedBufferView);

            view.diagnostics.baseline.cancel("Capture cancelled: G-buffer view changed.");
            view.diagnostics.startRequested = false;
        }

        ImGui::EndDisabled();

        const bool previewActive = canBuildGBuffer && buildGBuffer && view.display.gBufferView != GBufferView::Off;

        ImGui::TextDisabled(deferredSelected ? "Geometry + Lighting (1x)"
                                             : (buildGBuffer ? "G-buffer + Forward reference" : "Forward"));

        ImGui::Separator();

        const bool tileViewsAvailable = deferredSelected && view.display.lightCullingMode == LightCullingMode::Tiled &&
                                        view.renderer.tiledLightingAvailable() && !previewActive;

        if (!tileViewsAvailable && isTileDebugView(view.display.debugView))
        {
            view.display.debugView = DebugView::Lit;

            view.diagnostics.baseline.cancel("Capture cancelled: tile debug view became unavailable.");

            view.diagnostics.startRequested = false;
        }

        ImGui::BeginDisabled(previewActive);

        const char* viewNames[] = {
            "Lit",          "Albedo",        "World normal",      "Metallic",        "Roughness",
            "Linear depth", "HDR luminance", "Shadow projection", "Shadow cascades", "Tile light count",
            "Tile overflow"};

        // 不具备 Tiled 条件时，只显示原来的前九项。
        const int viewCount =
            tileViewsAvailable ? IM_ARRAYSIZE(viewNames) : static_cast<int>(DebugView::TileLightCount);

        int selectedView = static_cast<int>(view.display.debugView);

        if (ImGui::Combo("Debug View", &selectedView, viewNames, viewCount))
        {
            view.display.debugView = static_cast<DebugView>(selectedView);

            view.diagnostics.baseline.cancel("Capture cancelled: debug view changed.");

            view.diagnostics.startRequested = false;
        }

        ImGui::EndDisabled();

        if (tileViewsAvailable && isTileDebugView(view.display.debugView))
        {
            if (view.display.debugView == DebugView::TileLightCount)
            {
                ImGui::TextWrapped("0 black; 1 blue; 16 cyan; 32 green; "
                                   "64 yellow; 128 red.");
            }
            else
            {
                ImGui::TextWrapped("Black: zero candidates. Gray: no overflow.");
            }

            ImGui::TextWrapped("Magenta: overflow (>128), full-light fallback. "
                               "White: invalid tile data.");
        }

        const bool showDepthRange = (!previewActive && view.display.debugView == DebugView::LinearDepth) ||
                                    (previewActive && view.display.gBufferView == GBufferView::LinearDepth);

        if (showDepthRange)
        {
            ImGui::DragFloat("Depth display far", &view.display.debugDepthFar, 0.5f, view.camera.nearPlane + 0.001f,
                             view.camera.farPlane, "%.2f", ImGuiSliderFlags_AlwaysClamp);

            ImGui::Text("Forward distance: %.3f .. %.3f", view.camera.nearPlane,
                        std::max(view.display.debugDepthFar, view.camera.nearPlane + 0.001f));
        }

        if (!previewActive && view.display.debugView == DebugView::Roughness)
        {
            ImGui::TextUnformatted("Shading roughness: clamped to [0.04, 1].");
        }

        if (!previewActive && view.display.debugView == DebugView::HdrLuminance)
        {
            ImGui::TextUnformatted("log2(Y): [-8, +8], scene-linear, before EV.");
        }

        const bool usesToneMapping = previewActive ? view.display.gBufferView == GBufferView::Emissive
                                                   : view.display.debugView == DebugView::Lit;

        ImGui::BeginDisabled(!usesToneMapping);

        ImGui::SliderFloat("Exposure EV", &view.display.exposureEv, -8.0f, 8.0f, "%.2f");

        const char* mapperNames[] = {"Reinhard", "ACES fitted (Narkowicz)"};

        int selectedMapper = static_cast<int>(view.display.toneMapper);

        if (ImGui::Combo("Tone mapper", &selectedMapper, mapperNames, IM_ARRAYSIZE(mapperNames)))
        {
            view.display.toneMapper = static_cast<ToneMapper>(selectedMapper);
        }

        if (ImGui::Button("Reset display"))
        {
            view.display.exposureEv = 0.0f;
            view.display.toneMapper = ToneMapper::Reinhard;
        }

        ImGui::EndDisabled();

        if (!usesToneMapping)
        {
            ImGui::TextUnformatted("Data view: exposure and tone mapping bypassed");
        }

        ImGui::EndTabItem();
    }
}
