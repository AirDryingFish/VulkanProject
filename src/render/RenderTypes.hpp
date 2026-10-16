#pragma once

#include "vulkan/VulkanHeaders.hpp"
#include "render/RenderSettings.hpp"
#include "render/ShaderTypes.hpp"
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>
#include <cstddef>
#include <string>

inline constexpr std::size_t frameImageDescriptorCount = 5;
inline constexpr std::size_t materialImageDescriptorCount = 5;
inline constexpr std::uint32_t directionalShadowResolution = 2048;

struct ImDrawData;

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
