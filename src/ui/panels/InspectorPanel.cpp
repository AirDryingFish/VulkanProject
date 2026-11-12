#include "ui/panels/EditorPanels.hpp"
#include "scene/Material.hpp"
#include <cstddef>
#include <imgui.h>

void editor_panels::drawInspector(EditorView& view, const EditorActions& actions)
{
    if (ImGui::BeginTabItem("Inspector"))
    {
        ImGui::BeginChild("InspectorContent", ImVec2(0.0f, 0.0f));
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        SceneObject* selectedSceneObject = view.editor.selectedSceneObject(view.scene);
        ImGui::SeparatorText("Object Transform");
        ImGui::Text("Selected: %s", selectedSceneObject != nullptr ? selectedSceneObject->name.c_str() : "None");
        if (view.editor.selection == SelectionKind::Object && selectedSceneObject != nullptr)
        {
            Transform& transform = selectedSceneObject->transform;
            ImGui::Text("Pick Distance: %.3f", view.editor.pickDistance);
            ImGui::Checkbox("Auto Rotate", &selectedSceneObject->autoRotate);
            ImGui::DragFloat("Auto Rotate Speed", &selectedSceneObject->autoRotateSpeed, 1.0f, -720.0f, 720.0f);
            ImGui::DragFloat3("Position", &transform.position.x, 0.05f);
            ImGui::DragFloat3("Rotation", &transform.rotation.x, 0.5f, -360.0f, 360.0f);
            ImGui::DragFloat3("Scale", &transform.scale.x, 0.02f, 0.01f, 20.0f);
            if (ImGui::Button("Reset Transform"))
            {
                transform.position = glm::vec3(0.0f);
                transform.rotation = glm::vec3(0.0f);
                transform.scale = glm::vec3(1.0f);
                selectedSceneObject->autoRotation = 0.0f;
            }
            if (ImGui::Button("Delete Selected"))
            {
                SceneObject& object = view.scene.objects[view.editor.objectIndex];
                actions.releaseMesh(object.mesh);
                selectedSceneObject = nullptr;
                view.scene.objects.erase(view.scene.objects.begin() + view.editor.objectIndex);
                view.editor.clearSelection();
                view.editor.sceneClickConsumed = true;
            }
        }

        ImGui::SeparatorText("PBR Material");

        if (selectedSceneObject == nullptr)
        {
            ImGui::TextDisabled("Select a scene object to edit its material");
        }
        else
        {
            MaterialHandle& selectedMaterial = selectedSceneObject->material;
            const char* previewName =
                selectedMaterial && !selectedMaterial->name.empty() ? selectedMaterial->name.c_str() : "None";
            if (ImGui::BeginCombo("Material", previewName))
            {
                for (std::size_t materialIndex = 0; materialIndex < view.materialLibrary.size(); ++materialIndex)
                {
                    const MaterialHandle& candidate = view.materialLibrary[materialIndex];
                    if (!candidate)
                    {
                        continue;
                    }
                    ImGui::PushID(static_cast<int>(materialIndex));
                    const bool isSelected = candidate == selectedMaterial;
                    if (ImGui::Selectable(candidate->name.c_str(), isSelected))
                    {
                        selectedMaterial = candidate;
                        view.editor.sceneClickConsumed = true;
                    }

                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }

            if (selectedMaterial)
            {
                Material& material = *selectedMaterial;
                ImGui::Text("Editing: %s", material.name.c_str());
                ImGui::ColorEdit3("Albedo Tint",
                                  &material.baseColorFactor.x); // 修改从&material.baseColorFactor.x地址起的3个分量
                ImGui::SliderFloat("Metallic multiplier", &material.metallicFactor, 0.0f, 1.0f);
                ImGui::SliderFloat("Roughness multiplier", &material.roughnessFactor, 0.0f, 1.0f);
                ImGui::SliderFloat("Occlusion strength", &material.occlusionStrength, 0.0f, 1.0f);
                ImGui::SliderFloat("Normal scale", &material.normalScale, 0.0f, 2.0f);
                ImGui::ColorEdit3("Emissive Factor", &material.emissiveFactor.x);
                std::size_t objectReferenceCount = 0;
                for (const SceneObject& object : view.scene.objects)
                {
                    if (object.material == selectedMaterial)
                    {
                        ++objectReferenceCount;
                    }
                }
                ImGui::TextDisabled("Used by %zu scene object(s)", objectReferenceCount);
            }
        }

        if (ImGui::CollapsingHeader("Mesh and texture details"))
        {
            ImGui::SeparatorText("Resources");

            ImGui::Text("Objects: %zu", view.scene.objects.size());
            if (selectedSceneObject != nullptr && selectedSceneObject->mesh)
            {
                const Mesh& mesh = *selectedSceneObject->mesh;

                if (!selectedSceneObject->sourcePath.empty())
                {
                    ImGui::TextWrapped("Source: %s", selectedSceneObject->sourcePath.c_str());
                }
                ImGui::TextWrapped("Mesh Key: %s", mesh.cacheKey.c_str());
                ImGui::Text("Vertex Tangents: %s", mesh.hasTangents ? "Available" : "Unavailable");

                if (selectedSceneObject->material)
                {
                    // ImGui::Text("Material: %s", selectedSceneObject->material->name.c_str());
                    const Material& material = *selectedSceneObject->material;
                    ImGui::Text("Material: %s", material.name.c_str());
                    if (ImGui::TreeNode("Material Texture Slots"))
                    {
                        auto showSlot = [](const char* label, const MaterialTextureSlot& slot)
                        {
                            ImGui::Text("%s / UV%u", label, static_cast<unsigned int>(slot.texCoord));
                            ImGui::Indent();
                            ImGui::TextWrapped("Image: %s", slot.image ? slot.image->name.c_str() : "None");
                            ImGui::TextWrapped("Sampler: %s", slot.sampler ? slot.sampler->name.c_str() : "None");
                            ImGui::Unindent();
                        };

                        showSlot("Base Color", material.baseColorTexture);
                        showSlot("Normal", material.normalTexture);
                        showSlot("Metallic-Roughness", material.metallicRoughnessTexture);
                        showSlot("Occlusion", material.aoTexture);
                        showSlot("Emissive", material.emissiveTexture);

                        ImGui::TreePop();
                    }
                }
                ImGui::Text("Mesh References: %ld", selectedSceneObject->mesh.use_count());
                ImGui::Text("Vertices: %u", mesh.vertexCount);
                ImGui::Text("Indices: %u", mesh.indexCount);
                ImGui::Text("AABB Min: %.2f %.2f %.2f", mesh.boundsMin.x, mesh.boundsMin.y, mesh.boundsMin.z);
                ImGui::Text("AABB Max: %.2f %.2f %.2f", mesh.boundsMax.x, mesh.boundsMax.y, mesh.boundsMax.z);
            }

            ImGui::Text("Texture Library Entries: %zu", view.textureLibrary.size());
            ImGui::Text("Materials: %zu", view.materialLibrary.size());
            ImGui::Text("Mip Levels: %u", view.defaultMaterial && view.defaultMaterial->baseColorTexture.image
                                              ? view.defaultMaterial->baseColorTexture.image->image.mipLevels()
                                              : 0u);
        }

        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
}
