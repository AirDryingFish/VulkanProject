#pragma once

#include "scene/ManyLights.hpp"
#include "scene/ScenePresets.hpp"
#include "vulkan/VulkanHeaders.hpp"

#include <string>

class VulkanContext;
class Camera;
class Scene;

// 在成功提交的采样起点借用同一份场景输入，生成可随 CSV 归档的配置文本。
struct BaselineContextView
{
    const VulkanContext& context;
    const Camera& camera;
    const Scene& scene;
    VkExtent2D extent;
    ScenePreset activeScenePreset;
    const ManyLightsSettings& manyLightsSettings;
    bool showPointLightMarkers;
};

std::string buildBaselineContext(const BaselineContextView& view);
