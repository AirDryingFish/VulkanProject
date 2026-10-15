#pragma once

#include "VulkanHeaders.hpp"
#include <glm/mat4x4.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>
#include <cstddef>
#include <array>
#include <string>

inline constexpr std::size_t frameImageDescriptorCount = 5;
inline constexpr std::size_t materialImageDescriptorCount = 5;
inline constexpr std::uint32_t directionalShadowResolution = 2048;

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

// -- 定义曝光和 tone mapper --
enum class ToneMapper : std::uint32_t
{
    Reinhard = 0,
    AcesFittedNarkowicz = 1
};

// -- 主画面的显示内容 --
enum class DebugView : std::int32_t
{
    Lit = 0,
    Albedo = 1,
    WorldNormal = 2,
    Metallic = 3,
    Roughness = 4,
    LinearDepth = 5,
    HdrLuminance = 6,
    ShadowProjection = 7
};

inline bool isSurfaceDebugView(DebugView view) noexcept
{
    return view >= DebugView::Albedo && view <= DebugView::LinearDepth;
};

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

struct GpuTimingConfig
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

    GpuTimingConfig config{};
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
    GpuTimingConfig config;

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


struct ImDrawData;

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

struct RenderObjectView
{
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    uint32_t indexCount = 0;

    VkDescriptorSet materialDescriptorSet = VK_NULL_HANDLE;
    DrawPushConstants pushConstants{};

    bool doubleSided = false;
};

struct RenderFrameData
{
    const std::vector<RenderObjectView>* objects = nullptr;
    VkDescriptorSet frameDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet skyboxDescriptorSet = VK_NULL_HANDLE;

    ImDrawData* imguiDrawData = nullptr;

    glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};

    bool directionalShadowsEnabled = false;
    // 这里不放 shadowReceiverBias，因为它通过 UBO 传给 shader
    float shadowConstantBias = 0.0f;
    float shadowSlopeBias = 0.0f;
    bool showShadowDepth = false;

    std::string sceneId;

    bool bypassToneMapping = false;

    float exposureEv = 0.0f;
    ToneMapper toneMapper = ToneMapper::Reinhard;

    DebugView debugView = DebugView::Lit;
    float debugDepthNear = 0.1f;
    float debugDepthFar = 50.0f;
};

// 本帧应该怎么处理
enum class FrameStatus
{
    Ready, // 本帧已经准备完成，可以录制和提交命令
    Skip, // 本帧不渲染，直接返回。例如Renderer尚未初始化、窗口最小化等情况
    RecreateSwapchain // Swapchain 已失效或不再匹配窗口，需要重建
};

// 描述已经开始的这一帧
struct FrameToken
{
    uint32_t frameIndex = 0; // 使用哪个 FrameContext
    uint32_t imageIndex = 0; // swapchain 下标
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE; // 当前 FrameContext 的 command buffer
};

struct BeginFrameResult
{
    FrameStatus status = FrameStatus::Skip;
    FrameToken frame{};
};