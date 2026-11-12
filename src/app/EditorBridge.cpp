#include "app/Application.hpp"

// 应用把状态与执行入口借给编辑器；UI 不访问 Application 的私有实现。
void Application::drawImGui()
{
    EditorView view{scene,
                    camera,
                    editor,
                    diagnostics,
                    display,
                    shadows,
                    manyLightsSettings,
                    activeScenePreset,
                    pendingScenePreset,
                    materialLibrary,
                    textureLibrary,
                    defaultMaterial,
                    allocatedMaterialSetCount,
                    maxMaterialCount,
                    renderer,
                    swapchain,
                    context};
    EditorActions actions;
    actions.scenePresetUnavailableReason = [this](ScenePreset preset) { return scenePresetUnavailableReason(preset); };
    actions.addMeshObject = [this](MeshSource source, const std::string& path) { addMeshObject(source, path); };
    actions.addGltfMeshObjects = [this](const std::string& path) { return addGltfMeshObjects(path); };
    actions.releaseMesh = [this](MeshHandle& mesh) { releaseMesh(mesh); };
    actions.applyManyLightsSettings = [this](const ManyLightsSettings& settings) { applyManyLightsSettings(settings); };
    actions.resetCameraInput = [this] { cameraController.reset(window); };
    drawEditor(view, actions);
}
