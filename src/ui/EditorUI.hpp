#pragma once

#include "assets/ImportResult.hpp"
#include "diagnostics/DiagnosticsState.hpp"
#include "render/RenderSettings.hpp"
#include "scene/ManyLights.hpp"
#include "scene/ScenePresets.hpp"
#include "ui/EditorState.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

class Camera;
class Renderer;
class Swapchain;
class VulkanContext;

// 仅在本次绘制期间借用应用状态，不拥有设备、资源库或场景。
struct EditorView
{
    Scene& scene;
    Camera& camera;
    EditorState& editor;
    DiagnosticsState& diagnostics;
    DisplaySettings& display;
    ShadowSettings& shadows;
    ManyLightsSettings& manyLightsSettings;
    const ScenePreset& activeScenePreset;
    std::optional<ScenePreset>& pendingScenePreset;
    const std::vector<MaterialHandle>& materialLibrary;
    const std::vector<ImageHandle>& textureLibrary;
    const MaterialHandle& defaultMaterial;
    const std::uint32_t& allocatedMaterialSetCount;
    std::uint32_t maxMaterialCount;
    const Renderer& renderer;
    const Swapchain& swapchain;
    const VulkanContext& context;
};

// 应用提供执行入口；回调同步完成，异常由相应面板展示，不在 UI 层执行 GPU 生命周期操作。
struct EditorActions
{
    std::function<std::string(ScenePreset)> scenePresetUnavailableReason;
    std::function<void(MeshSource, const std::string&)> addMeshObject;
    std::function<GltfImportResult(const std::string&)> addGltfMeshObjects;
    std::function<void(MeshHandle&)> releaseMesh;
    std::function<void(const ManyLightsSettings&)> applyManyLightsSettings;
    std::function<void()> resetCameraInput;
};

// 调用方须已开始 ImGui 帧；绘制会编辑借用状态并同步调用用户操作。
void drawEditor(EditorView& view, const EditorActions& actions);
