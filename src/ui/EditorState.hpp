#pragma once

#include "scene/Scene.hpp"
#include "scene/ScenePresets.hpp"

#include <string>

enum class SelectionKind
{
    None,
    Object,
    PointLight,
};

// Transient editor interaction belongs to the editor, not to scene content.
struct EditorState
{
    SelectionKind selection = SelectionKind::None;
    int objectIndex = -1;
    int pointLightIndex = -1;
    bool sceneClickConsumed = false;
    bool leftMouseWasDown = false;
    float pickDistance = 0.0f;
    int gizmoHoveredAxis = 0;
    int gizmoActiveAxis = 0;
    glm::vec3 gizmoDragStartPosition{0.0f};
    glm::vec3 gizmoDragAxis{0.0f};
    glm::vec3 gizmoDragPlaneNormal{0.0f};
    glm::vec3 gizmoDragStartHitPoint{0.0f};

    char importModelPath[1024]{};
    char importGltfPath[1024]{};
    std::string gltfImportSummary;
    std::string gltfImportError;
    std::string sceneStatusMessage;
    ScenePreset selectedScenePreset = ScenePreset::ShadowPlayground;
    std::string scenePresetError;

    SceneObject* selectedSceneObject(Scene& scene) const noexcept
    {
        return objectIndex >= 0 && objectIndex < static_cast<int>(scene.objects.size())
            ? &scene.objects[objectIndex] : nullptr;
    }

    void clearSelection() noexcept
    {
        selection = SelectionKind::None;
        objectIndex = pointLightIndex = -1;
        pickDistance = 0.0f;
        gizmoHoveredAxis = gizmoActiveAxis = 0;
    }
};
