#pragma once

#include "scene/Lighting.hpp"
#include "render/ShadowCascades.hpp"

#include <cstddef>
#include <array>
#include <glm/glm.hpp>

// CPU 与 GLSL 共享的二进制布局：字段顺序、类型、对齐都属于接口约定。
// 修改时同步检查使用这些结构的 shader；下方 static_assert 只能验证 C++ 一侧的布局。
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

    // 世界坐标 -> 光源裁剪空间，供阴影深度生成、比较以及覆盖范围调试共同使用。
    alignas(16) glm::mat4 lightViewProjection{1.0f};

    // xy: shadow map 单个像素的 UV 尺寸
    // z: 深度比较时的 receiver bias
    // w: 预留
    alignas(16) glm::vec4 shadowParams{};

    // shadowFlags:
    // x: shadows enabled
    // y: shadow projection view
    // z: PCF enabled
    // w: DebugView
    alignas(16) glm::ivec4 shadowFlags{};

    // 世界空间 -> 每级 light clip space
    // 容量为 4，实际使用数量见 cascadeInfo.x
    alignas(16) std::array<glm::mat4, shadowCascadeCount> cascadeLightViewProjection{};

    // 用一个 vec4 保存四级 CSM 各自的远边界，距离按相机视空间的正向深度表示
    alignas(16) glm::vec4 cascadeSplits{};

    alignas(16) glm::vec4 cascadeTexelWorldSize{};

    // 每级归一化深度比较 bias
    alignas(16) glm::vec4 cascadeReceiverBias{};

    // x: 有效级数
    // y: ShadowMode
    // z: 预览层(调试时显示指定层的 shadow map)
    // w: reserved
    alignas(16) glm::uvec4 cascadeInfo{};

    // 各级向下一级混合的起点，末级等于 far
    alignas(16) glm::vec4 cascadeBlendStarts{};
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
static_assert(
    offsetof(UniformBufferObject, cascadeLightViewProjection) == 1088);

static_assert(
    offsetof(UniformBufferObject, cascadeSplits) == 1344);

static_assert(
    offsetof(UniformBufferObject, cascadeTexelWorldSize) == 1360);

static_assert(
    offsetof(UniformBufferObject, cascadeReceiverBias) == 1376);

static_assert(
    offsetof(UniformBufferObject, cascadeInfo) == 1392);

static_assert(
    offsetof(UniformBufferObject, cascadeBlendStarts) == 1408);

static_assert(sizeof(UniformBufferObject) == 1424);

// 每个 draw 单独推送模型矩阵和材质参数；相机/灯光等整帧共享数据放在 UBO。
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

    // rgb: emissive factor
    // w: alphaCutoff
    alignas(16) glm::vec4 emissiveFactor{0.0f};

    // x: 纹理 UV set bit mask
    // y: 当前 Mesh 是否有有效 vertex tangent
    // z: 0 = OPAQUE, 1 = MASK
    // w: shadow draw 使用 cascade index；scene draw 保持 0
    alignas(16) glm::uvec4 textureInfo{
        0u, 0u, 0u, 0u
    };
};

// 后处理只需要少量显示参数，不需要模型数据
struct alignas(16) PostPushConstants
{
    // x: 曝光 EV；y/z: HDR 亮度调试的 log2 范围；w: 预留。
    glm::vec4 params{};

    // x: ToneMapper；y: 跳过曝光/色调映射；
    // z: DebugView；w: 阴影预览层。
    glm::ivec4 modes{0};
};
static_assert(offsetof(PostPushConstants, params) == 0);
static_assert(offsetof(PostPushConstants, modes) == 16);
static_assert(sizeof(PostPushConstants) == 32);
// model 占 64 字节，其余四组 vec4 各占 16 字节，共 128 字节。
// 这是 Vulkan 保证支持的最小 push constant 容量，新增字段需重新评估布局与设备限制。
static_assert(offsetof(DrawPushConstants, baseColorFactor) == 64);
static_assert(offsetof(DrawPushConstants, materialFactors) == 80);
static_assert(offsetof(DrawPushConstants, emissiveFactor) == 96);
static_assert(offsetof(DrawPushConstants, textureInfo) == 112);
static_assert(sizeof(DrawPushConstants) == 128);
static_assert(sizeof(DrawPushConstants) <= 128);
