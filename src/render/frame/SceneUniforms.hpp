#pragma once

#include "render/RenderSettings.hpp"
#include "render/ShaderTypes.hpp"
#include "render/shadow/ShadowFrameSummary.hpp"
#include "vulkan/VulkanHeaders.hpp"

#include <array>
#include <cstdint>

class Camera;
class Scene;

struct PackedPointLights
{
    std::array<GpuPointLight, MAX_POINT_LIGHTS> lights{};
    std::uint32_t count = 0;
};

// 一次场景更新生成的完整 CPU 数据；发布到哪个帧槽以及何时 flush 由调用方决定。
struct FrameSceneUniforms
{
    UniformBufferObject frame{};
    DeferredFrameUniforms deferred{};
    ShadowFrameSummary shadow{};
    PackedPointLights pointLights{};
};

// 在动画更新后生成相机、光照和阴影数据；shadows 显式接收与原逻辑相同的参数归一化。
// extent 为本帧实际目标尺寸，调用方保证有效；此函数不提交或等待 GPU。
FrameSceneUniforms makeSceneUniforms(const Scene& scene, const Camera& camera, const DisplaySettings& display,
                                     ShadowSettings& shadows, VkExtent2D extent, std::uint32_t shadowPreviewLayer,
                                     float shadowBlendFraction);
