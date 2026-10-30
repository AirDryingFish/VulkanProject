#pragma once

#include <cstdint>
#include <glm/glm.hpp>

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
    ShadowProjection = 7,
    ShadowCascades = 8
};

inline bool isSurfaceDebugView(DebugView view) noexcept
{
    return view >= DebugView::Albedo && view <= DebugView::LinearDepth;
};

struct DisplaySettings
{
    glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
    DebugView debugView = DebugView::Lit;
    float debugDepthFar = 50.0f;
    float exposureEv = 0.0f;
    ToneMapper toneMapper = ToneMapper::Reinhard;
};

enum class ShadowMode : std::uint32_t
{
    Single = 0,
    Cascaded = 1
};

struct ShadowSettings
{
    ShadowMode mode = ShadowMode::Cascaded;

    bool enabled = true;
    bool pcfEnabled = false;
    bool showDepth = false;
    int previewLayer = 0;


    float constantBias = 1.25f;
    float slopeBias = 1.75f;

    // Single 使用：归一化光源深度 bias
    float receiverBias = 0.0005f;
    glm::vec3 center{0.0f};
    float halfExtent = 5.0f;

    // CSM 使用
    float shadowDistance = 50.0f;
    float splitLambda = 0.5f;

    // CSM 级间过渡比例，0 关闭
    float blendFraction = 0.0f;

    float depthMargin = 1.0f;
    float lightNear = 0.1f;

    float receiverBiasWorld = 0.07f;
};
