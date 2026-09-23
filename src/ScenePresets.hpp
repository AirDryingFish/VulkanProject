#pragma once

#include <array>
#include <optional>
#include <stdexcept>
#include <string_view>

enum class ScenePreset
{
    MaterialLab,
    ShadowPlayground,
    Original,
    MetalRoughSpheres,
    DamagedHelmet,
    Box,
    Sponza,
    MetalRoughSpheresNoTextures,
};

struct ScenePresetInfo
{
    ScenePreset preset;
    const char* id;
    const char* label;
    const char* description;
    const char* assetRelativePath;
    const char* unsupportedReason;

    float presentationScale = 1.0f;
};

inline constexpr std::array<ScenePresetInfo, 8> scenePresets{{
    {ScenePreset::MaterialLab, "material-lab", "Material Lab",
     "Two rows of spheres: dielectric / metal, increasing roughness, plus a floor.", "", ""},
    {ScenePreset::ShadowPlayground, "shadow-playground", "Shadow Playground",
     "Ground, near/far occluders, doorway and ramp. Direct light only; no CSM yet.", "", ""},
    {ScenePreset::Original, "original", "Original Demo",
     "The previous two rusted spheres and TwoPrimitives glTF scene.",
     "models/gltf/TwoPrimitives/TwoPrimitives.gltf", ""},
    {ScenePreset::MetalRoughSpheres, "metal-rough-spheres", "glTF Metal Rough Spheres",
     "Official textured metallic/roughness test asset, with fitted camera.",
     "models/gltf/MetalRoughSpheres/MetalRoughSpheres.gltf", ""},
    {ScenePreset::DamagedHelmet, "damaged-helmet", "glTF Damaged Helmet",
     "Textured PBR asset: normal, metallic/roughness, AO and emissive slots.",
     "models/gltf/DamagedHelmet/DamagedHelmet.glb", ""},
    {ScenePreset::Box, "box", "glTF Box",
     "Small import and camera-framing sanity check.",
     "models/gltf/Box/Box.glb", ""},
    {ScenePreset::Sponza, "sponza", "Sponza",
     "Reference architecture asset retained unchanged for future integration.",
     "models/gltf/Sponza/Sponza.gltf",
     ""},
    {ScenePreset::MetalRoughSpheresNoTextures,
     "metal-rough-spheres-no-textures",
     "glTF Metal Rough Spheres, no textures",
     "Official untextured metallic/roughness material grid.",
     "models/gltf/MetalRoughSpheresNoTextures/MetalRoughSpheresNoTextures.gltf",
     "",
     1000.0f},
}};

inline const ScenePresetInfo& scenePresetInfo(ScenePreset preset)
{
    for (const auto& info : scenePresets)
    {
        if (info.preset == preset)
            return info;
    }
    throw std::invalid_argument("unknown scene preset");
}

inline std::optional<ScenePreset> findScenePreset(std::string_view id)
{
    for (const auto& info : scenePresets)
    {
        if (id == info.id)
            return info.preset;
    }
    return std::nullopt;
}
