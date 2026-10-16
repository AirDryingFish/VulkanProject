#pragma once

#include "scene/Lighting.hpp"

#include <cstddef>
#include <glm/glm.hpp>

struct GpuPointLight
{
    alignas(16) glm::vec4 position;
    alignas(16) glm::vec4 color;
    alignas(16) glm::vec4 params;
};

struct alignas(16) UniformBufferObject
{
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;

    alignas(16) glm::vec4 cameraPosition;
    alignas(16) glm::vec4 ambientLight;
    alignas(16) glm::ivec4 lightCounts;

    // renderParams:
    // x: IBL intensity
    // y: debug depth near
    // z: debug depth far
    // w: reserved
    alignas(16) glm::vec4 renderParams;

    GpuPointLight pointLights[MAX_POINT_LIGHTS];

    alignas(16) glm::vec4 directionalDirectionEnabled{}; // xyz: 归一化传播方向 w: enabled
    alignas(16) glm::vec4 directionalColorIntensity{}; // xyz: 光的颜色 w: 强度

    // 光空间矩阵，用颜色显示它的覆盖范围
    alignas(16) glm::mat4 lightViewProjection{1.0f};

    // xy: shadow map 单个像素的 UV 尺寸
    // z: 后续使用的深度比较 bias
    // w: 预留
    alignas(16) glm::vec4 shadowParams{};

    // shadowFlags:
    // x: shadows enabled
    // y: shadow projection view
    // z: PCF enabled
    // w: DebugView
    alignas(16) glm::ivec4 shadowFlags{};
};

static_assert(offsetof(UniformBufferObject, proj) == 64);
static_assert(offsetof(UniformBufferObject, cameraPosition) == 128);
static_assert(offsetof(UniformBufferObject, ambientLight) == 144);
static_assert(offsetof(UniformBufferObject, lightCounts) == 160);
static_assert(offsetof(UniformBufferObject, renderParams) == 176);
static_assert(offsetof(UniformBufferObject, pointLights) == 192);
static_assert(offsetof(UniformBufferObject, directionalDirectionEnabled) == 960);
static_assert(offsetof(UniformBufferObject, directionalColorIntensity) == 976);
static_assert(offsetof(UniformBufferObject, lightViewProjection) == 992);
static_assert(offsetof(UniformBufferObject, shadowParams) == 1056);
static_assert(offsetof(UniformBufferObject, shadowFlags) == 1072);
static_assert(sizeof(UniformBufferObject) == 1088);

// 服务于模型绘制，包含模型矩阵和材质参数
struct alignas(16) DrawPushConstants
{
    alignas(16) glm::mat4 model{1.0f};

    alignas(16) glm::vec4 baseColorFactor{1.0f};

    // x: metallic
    // y: roughness
    // z: occlusion strength
    // w: normal scale
    alignas(16) glm::vec4 materialFactors{
        1.0f,
        1.0f,
        1.0f,
        1.0f
    };

    // rgb: 继续保存 emissive
    // w: alphaCutoff
    alignas(16) glm::vec4 emissiveFactor{0.0f};

    // x: 纹理 UV set bit mask
    // y: 当前 Mesh 是否有有效 vertex tangent
    // z: 0 = OPAQUE, 1 = MASK
    // w: reserved
    alignas(16) glm::uvec4 textureInfo{
        0u, 0u, 0u, 0u
    };
};

// 后处理只需要少量显示参数，不需要模型数据
struct alignas(16) PostPushConstants
{
    // x 后续用于曝光 EV: 目前 reserve
    glm::vec4 params{};

    // x 后续用于 tone mapper 选择
    // y 表示是否跳过 tone mapping, 用于数据调试视图
    glm::ivec4 modes{0};
};
static_assert(offsetof(PostPushConstants, params) == 0);
static_assert(offsetof(PostPushConstants, modes) == 16);
static_assert(sizeof(PostPushConstants) == 32);
// offset
// 0
// │
// ├── model                    64 bytes
// │   offset 0 ~ 63
// │
// 64
// ├── baseColorFactor          16 bytes
// │   offset 64 ~ 79
// │
// 80
// ├── materialFactors          16 bytes
// │   offset 80 ~ 95
// │
// 96
// ├── emissiveFactor           16 bytes
// │   offset 96 ~ 111
// │
// 112
static_assert(offsetof(DrawPushConstants, baseColorFactor) == 64);
static_assert(offsetof(DrawPushConstants, materialFactors) == 80);
static_assert(offsetof(DrawPushConstants, emissiveFactor) == 96);
static_assert(offsetof(DrawPushConstants, textureInfo) == 112);
static_assert(sizeof(DrawPushConstants) == 128);
static_assert(sizeof(DrawPushConstants) <= 128);
