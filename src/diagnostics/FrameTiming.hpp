#pragma once

#include "render/RenderSettings.hpp"
#include "vulkan/VulkanHeaders.hpp"

#include <array>
#include <cstdint>
#include <string>

// -- GPU计时：定义要测量的三个区间 --
enum class GpuPass : std::uint32_t
{
    Shadow = 0,
    Scene,
    PostAndUi,
    Count
};
inline constexpr std::uint32_t gpuPassCount = static_cast<std::uint32_t>(GpuPass::Count);
// 每个区间需要记录开始、结束两个时间戳，因此一个 pool 需要六个位置
inline constexpr std::uint32_t gpuTimestampQueryCount = gpuPassCount * 2;
// ----

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
    GpuTimingState state = GpuTimingState::Pending;
    double milliseconds = 0.0;
};

struct FrameTimingConfig
{
    std::string sceneId;
    VkExtent2D extent{};
    VkSampleCountFlagBits sceneSamples = VK_SAMPLE_COUNT_1_BIT;

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

struct GpuTimingSnapshot
{
    bool supported = false;
    bool hasSample = false;

    std::uint64_t submissionId = 0;
    std::uint32_t frameSlot = 0;

    FrameTimingConfig config{};
    std::array<GpuPassTiming, gpuPassCount> passes{};
};
// ----

// 统计实际 scene/shadow 绘制量
struct DrawStatistics
{
    std::uint64_t sceneDraws = 0;
    std::uint64_t sceneTriangles = 0;

    std::uint64_t shadowDraws = 0;
    std::uint64_t shadowTriangles = 0;
};

// -- CPU 计时--
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
    std::uint64_t totalBytes = 0;
};
// ----


