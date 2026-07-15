#pragma once

#include "BuildConfig.hpp"
#include "VulkanHeaders.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

inline constexpr uint32_t WIDTH = 1920;
inline constexpr uint32_t HEIGHT = 1080;

inline std::string assetPath(const std::filesystem::path &relativePath)
{
    return (std::filesystem::path(VULKAN_PROJECT_ASSET_DIR) / relativePath).lexically_normal().string();
}

inline const std::string MODEL_PATH = assetPath("models/viking_room.obj");
inline const std::string PBR_ALBEDO_PATH = assetPath("textures/pbr/rustediron2_basecolor.png");
inline const std::string PBR_NORMAL_PATH = assetPath("textures/pbr/rustediron2_normal.png");
inline const std::string PBR_METALLIC_PATH = assetPath("textures/pbr/rustediron2_metallic.png");
inline const std::string PBR_ROUGHNESS_PATH = assetPath("textures/pbr/rustediron2_roughness.png");
inline const std::string PBR_AO_PATH = assetPath("textures/pbr/rustediron2_ao.png");
inline const std::string SKYBOX_HDR_PATH = assetPath("textures/pbr/newport_loft.hdr");

inline const std::array<std::string, 6> SKYBOX_FACE_PATHS = {
    assetPath("textures/skybox/right.jpg"),
    assetPath("textures/skybox/left.jpg"),
    assetPath("textures/skybox/top.jpg"),
    assetPath("textures/skybox/bottom.jpg"),
    assetPath("textures/skybox/front.jpg"),
    assetPath("textures/skybox/back.jpg"),
};

inline const std::string MAIN_VERTEX_SHADER_PATH = assetPath("shaders/vert.spv");
inline const std::string MAIN_FRAGMENT_SHADER_PATH = assetPath("shaders/frag.spv");
inline const std::string SKYBOX_VERTEX_SHADER_PATH = assetPath("shaders/skybox.vert.spv");
inline const std::string SKYBOX_FRAGMENT_SHADER_PATH = assetPath("shaders/skybox.frag.spv");
inline const std::string IRRADIANCE_VERTEX_SHADER_PATH = assetPath("shaders/irradiance.vert.spv");
inline const std::string IRRADIANCE_FRAGMENT_SHADER_PATH = assetPath("shaders/irradiance.frag.spv");
inline const std::string PREFILTER_VERTEX_SHADER_PATH = assetPath("shaders/prefilter.vert.spv");
inline const std::string PREFILTER_FRAGMENT_SHADER_PATH = assetPath("shaders/prefilter.frag.spv");

inline constexpr int MAX_FRAMES_IN_FLIGHT = 2;

inline const std::vector<const char *> validationLayers = {
    "VK_LAYER_KHRONOS_validation"};

inline const std::vector<const char *> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
#if VULKAN_PROJECT_PLATFORM_MACOS
    // Required by devices exposed through MoltenVK.
    , "VK_KHR_portability_subset"
#endif
};

inline constexpr bool enableValidationLayers = VULKAN_PROJECT_ENABLE_VALIDATION != 0;
