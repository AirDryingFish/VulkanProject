#include "TriangleApplication.hpp"
#include "FileDialog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

namespace
{
ImVec2 operator+(const ImVec2 &a, const ImVec2 &b)
{
    return ImVec2(a.x + b.x, a.y + b.y);
}

ImVec2 operator-(const ImVec2 &a, const ImVec2 &b)
{
    return ImVec2(a.x - b.x, a.y - b.y);
}

ImVec2 operator*(const ImVec2 &v, float scale)
{
    return ImVec2(v.x * scale, v.y * scale);
}

float dot(const ImVec2 &a, const ImVec2 &b)
{
    return a.x * b.x + a.y * b.y;
}

float length(const ImVec2 &v)
{
    return std::sqrt(dot(v, v));
}

ImVec2 normalize(const ImVec2 &v)
{
    const float len = length(v);
    if (len < 0.0001f)
    {
        return ImVec2(0.0f, 0.0f);
    }
    return v * (1.0f / len);
}

float distancePointToSegment(const ImVec2 &point, const ImVec2 &a, const ImVec2 &b)
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

bool worldToScreen(const glm::vec3 &worldPoint, const glm::mat4 &view, const glm::mat4 &projection, const ImGuiViewport *viewport, ImVec2 &screenPoint)
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

void buildMouseRay(const ImVec2 &mouse, const glm::mat4 &view, const glm::mat4 &projection, const ImGuiViewport *viewport, glm::vec3 &rayOrigin, glm::vec3 &rayDirection)
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

bool intersectRayPlane(const glm::vec3 &rayOrigin, const glm::vec3 &rayDirection, const glm::vec3 &planePoint, const glm::vec3 &planeNormal, glm::vec3 &hitPoint)
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

glm::vec3 makeGizmoDragPlaneNormal(const glm::vec3 &axisDirection, const glm::vec3 &cameraFront, const glm::vec3 &cameraUp)
{
    glm::vec3 planeNormal = cameraFront - axisDirection * glm::dot(cameraFront, axisDirection);
    if (glm::length(planeNormal) > 0.0001f)
    {
        return glm::normalize(planeNormal);
    }

    planeNormal = cameraUp - axisDirection * glm::dot(cameraUp, axisDirection);
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

void drawAxisLine(ImDrawList *drawList, const ImVec2 &origin, const glm::mat3 &viewRotation, const glm::vec3 &axis, ImU32 color, const char *label)
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
    drawList->AddTriangleFilled(
        end,
        end - direction * 8.0f + normal * 4.0f,
        end - direction * 8.0f - normal * 4.0f,
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

const char *axisLabel(int axis)
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
}

void TriangleApplication::initImGui()
{
    // 为什么Imgui需要descriptor？
    // descriptor是渲染的时候需要渲染UI，渲染一个Image或者Text的时候
    // 每张纹理用一个descriptor，Imgui内部维护VkImageView->VkDescriptorSet的映射
    const std::array<VkDescriptorPoolSize, 3> poolSizes{{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
    }};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    VK_CHECK(vkCreateDescriptorPool(context.device(), &poolInfo, nullptr, &imguiDescriptorPool));
    mainDeletionQueue.pushFunction([this, pool = imguiDescriptorPool]() mutable
                                   { vkDestroyDescriptorPool(context.device(), pool, nullptr); });

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    mainDeletionQueue.pushFunction([](){
        ImGui::DestroyContext();
    });

    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForVulkan(window, true))
    {
        throw std::runtime_error("failed to init imgui glfw backend!");
    }
    mainDeletionQueue.pushFunction([](){
        ImGui_ImplGlfw_Shutdown();
    });

    QueueFamilyIndices indices = context.queueFamilies();

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_0;
    initInfo.Instance = context.instance();
    initInfo.PhysicalDevice = context.physicalDevice();
    initInfo.Device = context.device();
    initInfo.QueueFamily = indices.graphicsFamily.value();
    initInfo.Queue = context.graphicsQueue();
    initInfo.DescriptorPool = imguiDescriptorPool;
    initInfo.PipelineInfoMain.RenderPass = renderer.renderPass();
    initInfo.MinImageCount = MAX_FRAMES_IN_FLIGHT;
    initInfo.ImageCount = static_cast<uint32_t>(swapchain.imageCount());
    initInfo.PipelineInfoMain.MSAASamples = context.msaaSamples();

    if (!ImGui_ImplVulkan_Init(&initInfo))
    {
        throw std::runtime_error("failed to init imgui vulkan backend!");
    }
    mainDeletionQueue.pushFunction([](){
        ImGui_ImplVulkan_Shutdown();
    });
}

void TriangleApplication::drawImGui()
{
    ImGuiIO &io = ImGui::GetIO();
    sceneClickConsumed = false;

    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
    mainViewport->WorkPos + ImVec2(12.0f, 12.0f),
    ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(
    ImVec2(std::min(460.0f, std::max(240.0f, mainViewport->WorkSize.x - 24.0f)),
    std::max(240.0f, mainViewport->WorkSize.y - 24.0f)),
    ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Scene Editor"))
    {
        ImGui::Text("%.1f FPS  |  %.2f ms", io.Framerate,
        io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
        ImGui::Separator();

        if (ImGui::BeginTabBar("EditorTabs", ImGuiTabBarFlags_FittingPolicyScroll))
        {
            if (ImGui::BeginTabItem("Scene"))
            {
                ImGui::BeginChild("SceneContent", ImVec2(0.0f, 0.0f));
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
                ImGui::SeparatorText("Scene");
                auto addObject = [&](MeshSource source, const std::string &path = std::string()) {
                    try
                    {
                        addMeshObject(source, path);
                        sceneStatusMessage.clear();
                    }
                    catch (const std::exception &exception)
                    {
                        sceneStatusMessage = exception.what();
                    }
                    sceneClickConsumed = true;
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
                    ImGui::InputText("OBJ Path", importModelPath, sizeof(importModelPath));
                    if (ImGui::Button("Import OBJ"))
                    {
                        addObject(MeshSource::Obj, importModelPath);
                    }
                }

                if (!sceneStatusMessage.empty())
                {
                    ImGui::TextWrapped("Import failed: %s", sceneStatusMessage.c_str());
                }

                if (ImGui::CollapsingHeader("Import glTF / GLB"))
                {
                    ImGui::InputText("glTF/GLB path", importGltfPath, sizeof(importGltfPath));
                    ImGui::Text("Material slots: %u / %u", static_cast<unsigned int>(allocatedMaterialSetCount), static_cast<unsigned int>(maxMaterialCount));
                    bool importRequested = ImGui::Button("Import glTF Scene");
                    std::string requestedPath = importGltfPath;
                    ImGui::SameLine();
                    if (ImGui::Button("Browse and Import..."))
                    {
                        sceneClickConsumed = true;
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
                            gltfImportError = exception.what();
                        }
                    }
                    if (importRequested)
                    {
                        sceneClickConsumed = true;
                        gltfImportError.clear();

                        GltfImportResult importResult{};
                        bool importSucceeded = false;

                        try{
                            importResult = addGltfMeshObjects(requestedPath);
                            importSucceeded = true;
                        }
                        catch(const std::exception& exception)
                        {
                            gltfImportError = exception.what();
                        }

                        if (importSucceeded)
                        {
                            gltfImportSummary =
                            "Objects: " + std::to_string(importResult.objectCount) +
                            ", uploaded meshes: " + std::to_string(importResult.uploadedMeshCount) +
                            ", reused meshes: " + std::to_string(importResult.reusedMeshCount) +
                            ", new materials: " + std::to_string(importResult.materialCount) +
                            ", uploaded images: " + std::to_string(importResult.uploadedImageCount) +
                            ", new samplers: " + std::to_string(importResult.createdSamplerCount);
                        }
                    }

                    if (!gltfImportSummary.empty())
                    {
                        ImGui::TextWrapped("Last successful import: %s", gltfImportSummary.c_str());
                    }

                    if (!gltfImportError.empty())
                    {
                        ImGui::TextWrapped("glTF import failed: %s", gltfImportError.c_str());
                    }

                }

                ImGui::SeparatorText("Objects");
                ImGui::TextDisabled("%zu scene object(s)", sceneObjects.size());
                ImGui::BeginChild("ObjectList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
                for (size_t i = 0; i < sceneObjects.size(); i++)
                {
                    SceneObject &object = sceneObjects[i];
                    ImGui::PushID(static_cast<int>(i));
                    const bool selected = selectedObject == SceneSelection::Model && selectedSceneObjectIndex == static_cast<int>(i);
                    if (ImGui::Selectable(object.name.c_str(), selected))
                    {
                        selectedObject = SceneSelection::Model;
                        selectedSceneObjectIndex = static_cast<int>(i);
                        selectedModel = true;
                        selectedPointLightIndex = -1;
                        sceneClickConsumed = true;
                    }
                    ImGui::PopID();
                }




                ImGui::EndChild();

                ImGui::PopItemWidth();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Inspector"))
            {
                ImGui::BeginChild("InspectorContent", ImVec2(0.0f, 0.0f));
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
                SceneObject* selectedSceneObject = getSelectedSceneObject();
                ImGui::SeparatorText("Object Transform");
                ImGui::Text("Selected: %s", selectedSceneObject != nullptr ? selectedSceneObject->name.c_str() : "None");
                if (selectedObject == SceneSelection::Model && selectedSceneObject != nullptr)
                {
                    Transform& transform = selectedSceneObject->transform;
                    ImGui::Text("Pick Distance: %.3f", modelPickDistance);
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
                        SceneObject& object = sceneObjects[selectedSceneObjectIndex];
                        releaseMesh(object.mesh);
                        selectedSceneObject = nullptr;
                        sceneObjects.erase(sceneObjects.begin() + selectedSceneObjectIndex);
                        selectedSceneObjectIndex = -1;
                        selectedObject = SceneSelection::None;
                        selectedModel = false;
                        modelPickDistance = 0.0f;
                        sceneClickConsumed = true;
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
                    const char* previewName = selectedMaterial && !selectedMaterial->name.empty() ? selectedMaterial->name.c_str() : "None";
                    if (ImGui::BeginCombo("Material", previewName))
                    {
                        for (std::size_t materialIndex = 0; materialIndex < materialLibrary.size(); ++materialIndex)
                        {
                            const MaterialHandle& candidate = materialLibrary[materialIndex];
                            if (!candidate)
                            {
                                continue;
                            }
                            ImGui::PushID(static_cast<int>(materialIndex));
                            const bool isSelected = candidate == selectedMaterial;
                            if (ImGui::Selectable(candidate->name.c_str(), isSelected))
                            {
                                selectedMaterial = candidate;
                                sceneClickConsumed = true;
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
                        ImGui::ColorEdit3("Albedo Tint", &material.baseColorFactor.x); // 修改从&material.baseColorFactor.x地址起的3个分量
                        ImGui::SliderFloat("Metallic multiplier", &material.metallicFactor, 0.0f, 1.0f);
                        ImGui::SliderFloat("Roughness multiplier", &material.roughnessFactor, 0.0f, 1.0f);
                        ImGui::SliderFloat("Occlusion strength", &material.occlusionStrength, 0.0f, 1.0f);
                        ImGui::SliderFloat("Normal scale", &material.normalScale, 0.0f, 2.0f);
                        ImGui::ColorEdit3("Emissive Factor", &material.emissiveFactor.x);
                        std::size_t objectReferenceCount = 0;
                        for (const SceneObject& object : sceneObjects)
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

                    ImGui::Text("Objects: %zu", sceneObjects.size());
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

                    ImGui::Text("Texture Library Entries: %zu", textureLibrary.size());
                    ImGui::Text("Materials: %zu", materialLibrary.size());
                    ImGui::Text("Mip Levels: %u", mipLevels);

                }

                ImGui::PopItemWidth();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Lighting"))
            {
                ImGui::BeginChild("LightingContent", ImVec2(0.0f, 0.0f));
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
                ImGui::SeparatorText("Environment");
                ImGui::ColorEdit3("Ambient Color", &ambientLightColor.x);
                ImGui::DragFloat("Ambient Intensity", &ambientLightIntensity, 0.01f, 0.0f, 2.0f);
                ImGui::DragFloat("IBL Intensity", &iblIntensity, 0.01f, 0.0f, 5.0f);

                ImGui::Separator();
                ImGui::TextUnformatted("Directional Light");
                ImGui::Checkbox("Enabled##DirectionalLight", &directionalLight.enabled);
                ImGui::DragFloat3("Direction##DirectionalLight", &directionalLight.direction.x, 0.02f);
                ImGui::ColorEdit3("Color##DirectionalLight", &directionalLight.color.x);
                ImGui::DragFloat("Intensity##DirectionalLight", &directionalLight.intensity, 0.05f, 0.0f, 20.0f);

                ImGui::SeparatorText("Point lights");
                const bool canAddLight = pointLights.size() < MAX_POINT_LIGHTS;
                if (!canAddLight)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Add Point Light"))
                {
                    PointLight light{};
                    light.position = cameraPos + cameraFront * 2.0f;
                    pointLights.push_back(light);
                    selectedObject = SceneSelection::PointLight;
                    selectedModel = false;
                    selectedSceneObjectIndex = -1;
                    selectedPointLightIndex = static_cast<int>(pointLights.size()) - 1;
                    sceneClickConsumed = true;
                }
                if (!canAddLight)
                {
                    ImGui::EndDisabled();
                }
                ImGui::SameLine();
                ImGui::Text("%zu / %u", pointLights.size(), MAX_POINT_LIGHTS);

                for (size_t i = 0; i < pointLights.size(); i++)
                {
                    PointLight &light = pointLights[i];
                    ImGui::PushID(static_cast<int>(i));

                    const bool selected = selectedObject == SceneSelection::PointLight && selectedPointLightIndex == static_cast<int>(i);
                    const std::string lightLabel = "Point Light " + std::to_string(i + 1);
                    if (ImGui::Selectable(lightLabel.c_str(), selected))
                    {
                        selectedObject = SceneSelection::PointLight;
                        selectedModel = false;
                        selectedSceneObjectIndex = -1;
                        selectedPointLightIndex = static_cast<int>(i);
                        sceneClickConsumed = true;
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

            if (ImGui::BeginTabItem("Shadows"))
            {
                ImGui::BeginChild("ShadowsContent", ImVec2(0.0f, 0.0f));
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
                ImGui::SeparatorText("Directional shadows");
                ImGui::Checkbox("Shadows##DirectionalLight", &directionalShadowsEnabled);
                ImGui::Checkbox("Shadow PCF 3x3", &shadowPcfEnabled);

                ImGui::SeparatorText("Depth bias");
                ImGui::SliderFloat("Shadow constant bias", &shadowConstantBias, 0.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("Shadow slope bias", &shadowSlopeBias, 0.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("Shadow receiver bias", &shadowReceiverBias, 0.0f, 0.005f, "%.6f", ImGuiSliderFlags_AlwaysClamp);
                if (ImGui::Button("Reset shadow bias"))
                {
                    shadowConstantBias = 1.25f;
                    shadowSlopeBias = 1.75f;
                    shadowReceiverBias = 0.0f;
                }
                ImGui::SeparatorText("Debug views");
                ImGui::Checkbox("Show shadow depth", &showShadowDepth);
                // 调试显示
                ImGui::Checkbox("Show light-space coordinates", &showShadowProjection);


                ImGui::TextWrapped("Bias corrects depth comparisons; PCF filters shadow edges. The depth preview shows raw depth, not visibility.");

                ImGui::PopItemWidth();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Camera"))
            {
                ImGui::BeginChild("CameraContent", ImVec2(0.0f, 0.0f));
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
                ImGui::SeparatorText("Camera");
                ImGui::Text("Position: %.2f %.2f %.2f", cameraPos.x, cameraPos.y, cameraPos.z);
                ImGui::Text("Front: %.2f %.2f %.2f", cameraFront.x, cameraFront.y, cameraFront.z);
                ImGui::Text("Yaw/Pitch: %.1f %.1f", cameraYaw, cameraPitch);
                ImGui::DragFloat("Near Plane", &cameraNear, 0.01f, 0.01f, 10.0f);
                ImGui::DragFloat("Far Plane", &cameraFar, 1.0f, 10.0f, 10000.0f);
                ImGui::DragFloat("Move Speed", &cameraMoveSpeed, 0.1f, 0.1f, 30.0f);
                ImGui::DragFloat("Fast Multiplier", &cameraFastMultiplier, 0.1f, 1.0f, 10.0f);
                ImGui::DragFloat("Scroll Speed", &cameraScrollSpeed, 0.1f, 0.1f, 20.0f);
                ImGui::DragFloat("Pan Speed", &cameraPanSpeed, 0.001f, 0.001f, 0.2f, "%.3f");
                ImGui::DragFloat("Mouse Sensitivity", &mouseSensitivity, 0.01f, 0.01f, 2.0f);


                ImGui::PopItemWidth();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Diagnostics"))
            {
                ImGui::BeginChild("DiagnosticsContent", ImVec2(0.0f, 0.0f));
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
                ImGui::Text("FPS: %.1f", io.Framerate);
                ImGui::Text("Frame Time: %.3f ms", io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
                ImGui::Text("Swapchain Images: %zu", swapchain.imageCount());
                ImGui::Text("Extent: %u x %u", swapchain.extent().width, swapchain.extent().height);
                ImGui::Text("MSAA Samples: %d", context.msaaSamples());


                ImGui::SeparatorText("Resources");
                ImGui::Text("Objects: %zu", sceneObjects.size());
                ImGui::Text("Point lights: %zu / %u", pointLights.size(), MAX_POINT_LIGHTS);
                ImGui::Text("Materials: %zu", materialLibrary.size());
                ImGui::Text("Texture library entries: %zu", textureLibrary.size());
                ImGui::Text("Material slots: %u / %u",
                static_cast<unsigned int>(allocatedMaterialSetCount),
                static_cast<unsigned int>(maxMaterialCount));
                ImGui::Text("Shadow resolution: %u x %u",
                directionalShadowResolution, directionalShadowResolution);

                ImGui::PopItemWidth();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    ImDrawList *drawList = ImGui::GetForegroundDrawList();
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float radius = 50.0f;
    const float margin = 24.0f;
    const ImVec2 origin(
    viewport->Pos.x + viewport->Size.x - margin - radius,
    viewport->Pos.y + viewport->Size.y - margin - radius);

    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::mat3 viewRotation(view);

    drawList->AddCircleFilled(origin, radius, IM_COL32(16, 18, 22, 150), 32);
    drawList->AddCircle(origin, radius, IM_COL32(255, 255, 255, 45), 32, 1.0f);
    drawAxisLine(drawList, origin, viewRotation, glm::vec3(1.0f, 0.0f, 0.0f), IM_COL32(230, 60, 60, 255), "X");
    drawAxisLine(drawList, origin, viewRotation, glm::vec3(0.0f, 1.0f, 0.0f), IM_COL32(80, 210, 90, 255), "Y");
    drawAxisLine(drawList, origin, viewRotation, glm::vec3(0.0f, 0.0f, 1.0f), IM_COL32(80, 140, 255, 255), "Z");

    drawLightOverlays();
    drawTransformGizmo();
}

void TriangleApplication::drawLightOverlays()
{
    if (pointLights.empty())
    {
        return;
    }

    ImDrawList *drawList = ImGui::GetForegroundDrawList();
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGuiIO &io = ImGui::GetIO();

    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), swapchain.extent().width / static_cast<float>(swapchain.extent().height), cameraNear, cameraFar);
    projection[1][1] *= -1;

    const ImVec2 mouse = io.MousePos;
    int hoveredLight = -1;
    float bestDistance = 12.0f;

    std::vector<ImVec2> lightScreens(pointLights.size());
    std::vector<bool> lightVisible(pointLights.size(), false);

    for (size_t i = 0; i < pointLights.size(); i++)
    {
        lightVisible[i] = worldToScreen(pointLights[i].position, view, projection, viewport, lightScreens[i]);
        if (!lightVisible[i])
        {
            continue;
        }

        const float distance = length(mouse - lightScreens[i]);
        if (!io.WantCaptureMouse && gizmoHoveredAxis == 0 && gizmoActiveAxis == 0 && distance < bestDistance)
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
            selectedObject = SceneSelection::PointLight;
            selectedModel = false;
            selectedSceneObjectIndex = -1;
            selectedPointLightIndex = hoveredLight;
            sceneClickConsumed = true;
        }
    }

    for (size_t i = 0; i < pointLights.size(); i++)
    {
        if (!lightVisible[i])
        {
            continue;
        }

        const PointLight &light = pointLights[i];
        const bool selected = selectedObject == SceneSelection::PointLight && selectedPointLightIndex == static_cast<int>(i);
        const bool hovered = hoveredLight == static_cast<int>(i);
        const ImVec2 screen = lightScreens[i];
        const ImU32 color = IM_COL32(
            static_cast<int>(std::clamp(light.color.r, 0.0f, 1.0f) * 255.0f),
            static_cast<int>(std::clamp(light.color.g, 0.0f, 1.0f) * 255.0f),
            static_cast<int>(std::clamp(light.color.b, 0.0f, 1.0f) * 255.0f),
            light.enabled ? 255 : 110);

        drawList->AddCircleFilled(screen, selected || hovered ? 7.0f : 5.0f, color, 18);
        drawList->AddCircle(screen, selected ? 10.0f : 7.0f, IM_COL32(255, 255, 255, selected ? 240 : 120), 18, selected ? 2.0f : 1.0f);
        drawList->AddText(screen + ImVec2(10.0f, -8.0f), IM_COL32(255, 255, 255, 180), "Light");
    }
}

void TriangleApplication::drawTransformGizmo()
{
    glm::vec3 *selectedPosition = nullptr;
    glm::vec3 originWorld(0.0f);
    glm::vec3 axisDirections[3] = {
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
    };

    if (selectedObject == SceneSelection::Model)
    {
        SceneObject *object = getSelectedSceneObject();
        if (object == nullptr)
        {
            gizmoHoveredAxis = 0;
            gizmoActiveAxis = 0;
            return;
        }

        selectedPosition = &object->transform.position;
        glm::mat4 model = getObjectMatrix(*object);
        originWorld = glm::vec3(model * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        axisDirections[0] = glm::normalize(glm::vec3(model * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
        axisDirections[1] = glm::normalize(glm::vec3(model * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
        axisDirections[2] = glm::normalize(glm::vec3(model * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
    }
    else if (selectedObject == SceneSelection::PointLight && selectedPointLightIndex >= 0 && selectedPointLightIndex < static_cast<int>(pointLights.size()))
    {
        selectedPosition = &pointLights[selectedPointLightIndex].position;
        originWorld = *selectedPosition;
    }
    else
    {
        gizmoHoveredAxis = 0;
        gizmoActiveAxis = 0;
        return;
    }

    ImDrawList *drawList = ImGui::GetForegroundDrawList();
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGuiIO &io = ImGui::GetIO();

    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), swapchain.extent().width / static_cast<float>(swapchain.extent().height), cameraNear, cameraFar);
    projection[1][1] *= -1;

    const float distanceToCamera = glm::length(cameraPos - originWorld);
    const float axisWorldLength = std::clamp(distanceToCamera * 0.18f, 0.3f, 3.0f);

    ImVec2 originScreen;
    if (!worldToScreen(originWorld, view, projection, viewport, originScreen))
    {
        gizmoHoveredAxis = 0;
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
    gizmoHoveredAxis = 0;
    if (gizmoActiveAxis == 0 && !io.WantCaptureMouse)
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
                gizmoHoveredAxis = i + 1;
            }
        }
    }

    if (gizmoHoveredAxis != 0)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    if (gizmoActiveAxis == 0 && gizmoHoveredAxis != 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const int axisIndex = gizmoHoveredAxis - 1;
        glm::vec3 rayOrigin;
        glm::vec3 rayDirection;
        glm::vec3 hitPoint;
        const glm::vec3 planeNormal = makeGizmoDragPlaneNormal(axisDirections[axisIndex], cameraFront, cameraUp);

        buildMouseRay(mouse, view, projection, viewport, rayOrigin, rayDirection);
        if (intersectRayPlane(rayOrigin, rayDirection, originWorld, planeNormal, hitPoint))
        {
            gizmoActiveAxis = gizmoHoveredAxis;
            gizmoDragStartMouse = glm::vec2(mouse.x, mouse.y);
            gizmoDragStartPosition = *selectedPosition;
            gizmoDragAxis = axisDirections[axisIndex];
            gizmoDragPlaneNormal = planeNormal;
            gizmoDragStartHitPoint = hitPoint;
        }
    }

    if (gizmoActiveAxis != 0)
    {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            gizmoActiveAxis = 0;
        }
        else
        {
            const int axisIndex = gizmoActiveAxis - 1;
            glm::vec3 rayOrigin;
            glm::vec3 rayDirection;
            glm::vec3 hitPoint;

            buildMouseRay(mouse, view, projection, viewport, rayOrigin, rayDirection);
            if (intersectRayPlane(rayOrigin, rayDirection, gizmoDragStartHitPoint, gizmoDragPlaneNormal, hitPoint))
            {
                const float worldDistance = glm::dot(hitPoint - gizmoDragStartHitPoint, gizmoDragAxis);
                *selectedPosition = gizmoDragStartPosition + gizmoDragAxis * worldDistance;
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
        const bool highlighted = axis == gizmoHoveredAxis || axis == gizmoActiveAxis;
        const ImU32 color = axisColor(axis, highlighted);
        const float thickness = highlighted ? 5.0f : 3.0f;
        const ImVec2 direction = normalize(axes[i].end - originScreen);
        const ImVec2 normal(-direction.y, direction.x);

        drawList->AddLine(originScreen, axes[i].end, color, thickness);
        drawList->AddTriangleFilled(
            axes[i].end,
            axes[i].end - direction * 12.0f + normal * 6.0f,
            axes[i].end - direction * 12.0f - normal * 6.0f,
            color);
        drawList->AddText(axes[i].end + direction * 8.0f + ImVec2(-4.0f, -7.0f), color, axisLabel(axis));
    }
}
