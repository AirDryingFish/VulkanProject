#pragma once

#include <glm/glm.hpp>
#include <cstdint>

// 定义 本帧实际使用的阴影参数
struct ShadowFrameSummary
{
    // single 为1，csm 为 4
    std::uint32_t activeCascadeCount = 0;

    // 主相机的 near
    float cameraNearDistance = 0.0f;
    // 阴影接收远界
    float receiverFarDistance = 0.0f;
    // 本帧使用的 vertical FOV
    float cameraFovDegrees = 0.0f;

    // shader 最终比较深度时实用的 bias
    glm::vec4 receiverBiasNormalized{0.0f};
};
