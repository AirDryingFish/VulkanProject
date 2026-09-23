#pragma once

#include "VulkanHeaders.hpp"
#include <glm/mat4x4.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>
#include <cstddef>

inline constexpr std::size_t frameImageDescriptorCount = 5;
inline constexpr std::size_t materialImageDescriptorCount = 5;
inline constexpr std::uint32_t directionalShadowResolution = 2048;

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
    alignas(16) glm::vec4 emissiveFactor{0.0f};

    // x: 纹理 UV set bit mask
    // y: 当前 Mesh 是否有有效 vertex tangent
    // z/w: reserved
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

    bool bypassToneMapping = false;
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