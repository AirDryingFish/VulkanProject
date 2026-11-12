#pragma once

#include "render/RenderSettings.hpp"
#include "vulkan/VulkanHeaders.hpp"
#include "render/ShadowFrameSummary.hpp"

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <string>

enum class GpuPass : std::uint32_t
{
    Shadow = 0,
    Scene = 1,
    PostAndUi = 2,

    Geometry = 3,
    LightCull = 4,
    Lighting = 5,
    FrameGpu = 6,

    Count = 7
};

inline constexpr std::uint32_t gpuPassCount = static_cast<std::uint32_t>(GpuPass::Count);
inline constexpr std::uint32_t gpuTimestampQueryCount = gpuPassCount * 2;

inline constexpr std::array<const char *, gpuPassCount> gpuPassNames{
    "Shadow",
    "Scene (Forward)",
    "Post + UI",
    "Geometry",
    "LightCull",
    "Lighting",
    "Frame GPU"};

inline constexpr std::array<const char *, gpuPassCount> gpuPassCsvColumns{
    "shadow_ms",
    "scene_ms",
    "post_ui_ms",
    "geometry_ms",
    "light_cull_ms",
    "lighting_ms",
    "frame_gpu_ms"};

inline constexpr std::uint32_t gpuPassBit(GpuPass pass) noexcept
{
    return std::uint32_t{1} << static_cast<std::uint32_t>(pass);
}

// -- GPU 计时 --
enum class GpuTimingState
{
    Unavailable, // 设备不支持
    Pending,     // 尚未完成结果
    NotExecuted, // 本次提交没有执行这个 pass
    NotReady,    // 执行记录存在，但读取结果不可用
    Ready
};

struct GpuPassTiming
{
    // 只有 Ready 时数值才有效；不能用是否等于 0 判断查询状态。
    GpuTimingState state = GpuTimingState::Pending;
    double milliseconds = 0.0;
};

// 由 renderer 从当前帧实际使用的图像读取
struct ShadowTargetSummary
{
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::uint32_t arrayLayers = 0;
};

// 配置跟随提交复制，延迟回来的 GPU 样本不能套用此刻 UI 的设置。
struct FrameTimingConfig
{
    std::string sceneId;
    VkExtent2D extent{};
    VkSampleCountFlagBits sceneSamples = VK_SAMPLE_COUNT_1_BIT;

    RenderPath renderPath = RenderPath::Forward;
    LightCullingMode lightCullingMode = LightCullingMode::None;

    bool gBufferGeometry = false;
    GBufferView gBufferView = GBufferView::Off;

    std::uint32_t shadowPreviewLayer = 0;

    ShadowMode shadowMode = ShadowMode::Single;
    float shadowBlendFraction = 0.0f;
    bool shadowPcfEnabled = false;

    float shadowConstantBias = 0.0f;
    float shadowSlopeBias = 0.0f;

    float shadowReceiverBiasNormalized = 0.0f; // Single 归一化深度输入
    float shadowReceiverBiasWorld = 0.0f;      // CSM 世界单位输入

    // 请求距离；实际接收远界还受 camera.farPlane 限制。
    float shadowDistance = 0.0f;
    float shadowSplitLambda = 0.0f;
    float shadowDepthMargin = 0.0f;
    float shadowCsmLightNear = 0.0f;
    glm::vec3 shadowSingleCenter{0.0f};
    float shadowSingleHalfExtent = 0.0f;

    ShadowFrameSummary shadowFrame{};
    ShadowTargetSummary shadowTarget{};

    bool shadowEnabled = false;
    bool shadowPreview = false;
    bool uiDrawn = false;
    bool bypassToneMapping = false;
    float exposureEv = 0.0f;
    ToneMapper toneMapper = ToneMapper::Reinhard;


    DebugView debugView = DebugView::Lit;
    float debugDepthNear = 0.1f;
    float debugDepthFar = 50.0f;
};

// 根据当前渲染配置，生成 “本帧预计会执行哪些 GPU 计时区间” 的位 mask
inline std::uint32_t expectedGpuPassMask(const FrameTimingConfig &config) noexcept
{
    // Shadow 即使关闭阴影，也仍然执行清空。
    std::uint32_t mask =
        gpuPassBit(GpuPass::Shadow) |
        gpuPassBit(GpuPass::PostAndUi) |
        gpuPassBit(GpuPass::FrameGpu);

    if (config.gBufferGeometry)
    {
        mask |= gpuPassBit(GpuPass::Geometry);
    }

    if (config.renderPath == RenderPath::Forward)
    {
        mask |= gpuPassBit(GpuPass::Scene);
    }
    else
    {
        mask |= gpuPassBit(GpuPass::Lighting);

        if (config.lightCullingMode == LightCullingMode::Tiled)
        {
            mask |= gpuPassBit(GpuPass::LightCull);
        }
    }

    return mask;
}

struct GpuTimingSnapshot
{
    bool supported = false;
    bool hasSample = false;

    // Renderer 本次运行中的成功提交序号，CPU/GPU 基线按它配对，而非按刷新次数。
    std::uint64_t submissionId = 0;
    std::uint32_t frameSlot = 0;

    FrameTimingConfig config{};
    std::array<GpuPassTiming, gpuPassCount> passes{};
};
// ----

// 统计实际提交的 scene/shadow mesh 绘制量，不代表通过裁剪或最终可见的三角形。
struct DrawStatistics
{
    std::uint64_t sceneDraws = 0;
    std::uint64_t sceneTriangles = 0;

    std::uint64_t shadowDraws = 0;
    std::uint64_t shadowTriangles = 0;
};

// CPU 结果在主机调用结束后发布，GPU 结果在之后复用帧槽时读回。
// 因此分别读取的最新快照可能属于不同提交，配对时必须核对 submissionId。
struct RendererCpuTimingSnapshot
{
    bool valid = false;

    std::uint64_t submissionId = 0;
    std::uint32_t frameSlot = 0;
    FrameTimingConfig config;

    double fenceWaitMs = 0.0;
    double acquireMs = 0.0;
    double submitMs = 0.0;
    double presentMs = 0.0;

    DrawStatistics draws{};
};

struct CpuFrameTimingSnapshot
{
    RendererCpuTimingSnapshot renderer{};
    double updateMs = 0.0;
    double recordMs = 0.0;
};

// -- 渲染目标开销估算 --
struct RenderTargetPayloadEstimate
{
    bool available = false;
    bool complete = false;

    std::uint64_t hdrColorBytes = 0;
    std::uint64_t msaaColorBytes = 0;
    std::uint64_t sceneDepthBytes = 0;
    std::uint64_t shadowDepthBytes = 0;
    std::uint64_t gbufferBytes = 0;
    std::uint64_t deferredHdrBytes = 0;
    std::uint64_t totalBytes = 0;
};
// ----


