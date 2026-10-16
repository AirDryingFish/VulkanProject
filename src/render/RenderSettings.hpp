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
    ShadowProjection = 7
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

struct ShadowSettings
{
    bool enabled = true;
    bool pcfEnabled = false;
    bool showDepth = false;
    float constantBias = 1.25f;
    float slopeBias = 1.75f;
    float receiverBias = 0.0005f;
    glm::vec3 center{0.0f};
    float halfExtent = 5.0f;
};
