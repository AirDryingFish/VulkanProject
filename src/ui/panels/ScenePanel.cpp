#include "ui/panels/EditorPanels.hpp"
#include "ui/FileDialog.hpp"
#include <exception>
#include <string>
#include <imgui.h>

void editor_panels::drawScene(EditorView& view, const EditorActions& actions)
{
    if (ImGui::BeginTabItem("Scene"))
    {
        ImGui::BeginChild("SceneContent", ImVec2(0.0f, 0.0f));
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        ImGui::SeparatorText("Scene Presets");
        ImGui::Text("Active: %s", scenePresetInfo(view.activeScenePreset).label);
        if (ImGui::BeginCombo("Preset", scenePresetInfo(view.editor.selectedScenePreset).label))
        {
            for (const auto& info : scenePresets)
            {
                const bool selected = view.editor.selectedScenePreset == info.preset;
                if (ImGui::Selectable(info.label, selected))
                {
                    view.editor.selectedScenePreset = info.preset;
                    view.editor.sceneClickConsumed = true;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("%s", scenePresetInfo(view.editor.selectedScenePreset).description);
        const std::string unavailable = actions.scenePresetUnavailableReason(view.editor.selectedScenePreset);
        ImGui::BeginDisabled(!unavailable.empty() || view.pendingScenePreset.has_value());
        if (ImGui::Button("Load / Reset Scene"))
        {
            view.pendingScenePreset = view.editor.selectedScenePreset;
            view.editor.sceneClickConsumed = true;
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped("Replaces current objects and resets camera/lighting; edits are not saved.");
        if (!unavailable.empty())
            ImGui::TextWrapped("Unavailable: %s", unavailable.c_str());
        if (!view.editor.scenePresetError.empty())
            ImGui::TextWrapped("Scene switch failed: %s", view.editor.scenePresetError.c_str());
        ImGui::SeparatorText("Scene");
        auto addObject = [&](MeshSource source, const std::string& path = std::string())
        {
            try
            {
                actions.addMeshObject(source, path);
                view.editor.sceneStatusMessage.clear();
            }
            catch (const std::exception& exception)
            {
                view.editor.sceneStatusMessage = exception.what();
            }
            view.editor.sceneClickConsumed = true;
        };

        if (ImGui::Button("Add Cube"))
        {
            addObject(MeshSource::Cube);
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Sphere"))
        {
            addObject(MeshSource::Sphere);
        }
        if (ImGui::CollapsingHeader("Import OBJ##Section"))
        {
            ImGui::InputText("OBJ Path", view.editor.importModelPath, sizeof(view.editor.importModelPath));
            if (ImGui::Button("Import OBJ"))
            {
                addObject(MeshSource::Obj, view.editor.importModelPath);
            }
        }

        if (!view.editor.sceneStatusMessage.empty())
        {
            ImGui::TextWrapped("Import failed: %s", view.editor.sceneStatusMessage.c_str());
        }

        if (ImGui::CollapsingHeader("Import glTF / GLB"))
        {
            ImGui::InputText("glTF/GLB path", view.editor.importGltfPath, sizeof(view.editor.importGltfPath));
            ImGui::Text("Material slots: %u / %u", static_cast<unsigned int>(view.allocatedMaterialSetCount),
                        static_cast<unsigned int>(view.maxMaterialCount));
            bool importRequested = ImGui::Button("Import glTF Scene");
            std::string requestedPath = view.editor.importGltfPath;
            ImGui::SameLine();
            if (ImGui::Button("Browse and Import..."))
            {
                view.editor.sceneClickConsumed = true;
                try
                {
                    const auto path = FileDialog::openGltf();
                    if (path)
                    {
                        requestedPath = *path;
                        importRequested = true;
                    }
                }
                catch (const std::exception& exception)
                {
                    view.editor.gltfImportError = exception.what();
                }
            }
            if (importRequested)
            {
                view.editor.sceneClickConsumed = true;
                view.editor.gltfImportError.clear();

                GltfImportResult importResult{};
                bool importSucceeded = false;

                try
                {
                    importResult = actions.addGltfMeshObjects(requestedPath);
                    importSucceeded = true;
                }
                catch (const std::exception& exception)
                {
                    view.editor.gltfImportError = exception.what();
                }

                if (importSucceeded)
                {
                    view.editor.gltfImportSummary =
                        "Objects: " + std::to_string(importResult.objectCount) +
                        ", uploaded meshes: " + std::to_string(importResult.uploadedMeshCount) +
                        ", reused meshes: " + std::to_string(importResult.reusedMeshCount) +
                        ", new materials: " + std::to_string(importResult.materialCount) +
                        ", uploaded images: " + std::to_string(importResult.uploadedImageCount) +
                        ", new samplers: " + std::to_string(importResult.createdSamplerCount);
                }
            }

            if (!view.editor.gltfImportSummary.empty())
            {
                ImGui::TextWrapped("Last successful import: %s", view.editor.gltfImportSummary.c_str());
            }

            if (!view.editor.gltfImportError.empty())
            {
                ImGui::TextWrapped("glTF import failed: %s", view.editor.gltfImportError.c_str());
            }
        }

        ImGui::SeparatorText("Objects");
        ImGui::TextDisabled("%zu scene object(s)", view.scene.objects.size());
        ImGui::BeginChild("ObjectList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < view.scene.objects.size(); i++)
        {
            SceneObject& object = view.scene.objects[i];
            ImGui::PushID(static_cast<int>(i));
            const bool selected =
                view.editor.selection == SelectionKind::Object && view.editor.objectIndex == static_cast<int>(i);
            if (ImGui::Selectable(object.name.c_str(), selected))
            {
                view.editor.selection = SelectionKind::Object;
                view.editor.objectIndex = static_cast<int>(i);
                view.editor.pointLightIndex = -1;
                view.editor.sceneClickConsumed = true;
            }
            ImGui::PopID();
        }

        ImGui::EndChild();

        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
}
