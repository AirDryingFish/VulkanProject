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

// 只借用 GPU 句柄；生成绘制快照不会延长底层 Mesh/Material 的生命周期。
struct RenderObjectView
{
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    uint32_t indexCount = 0;

    VkDescriptorSet materialDescriptorSet = VK_NULL_HANDLE;
    DrawPushConstants pushConstants{};

    bool doubleSided = false;
};

// objects/ImDrawData 只在 recordFrame 中同步读取；录制期间不能使指针失效。
// 录制完成后，句柄对应的 GPU 资源仍需保留到使用它们的提交完成。
struct RenderFrameData
{
    const std::vector<RenderObjectView>* objects = nullptr;
    VkDescriptorSet frameDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet skyboxDescriptorSet = VK_NULL_HANDLE;

    ImDrawData* imguiDrawData = nullptr;

    glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};

    bool directionalShadowsEnabled = false;
    ShadowMode shadowMode = ShadowMode::Single;
    float shadowBlendFraction = 0.0f;

    std::uint32_t shadowPreviewLayer = 0;

    // receiver 参数用于诊断快照；shader 仍通过 UBO 接收比较 bias。
    bool shadowPcfEnabled = false;
    float shadowReceiverBiasNormalized = 0.0f; // Single 输入
    float shadowReceiverBiasWorld = 0.0f;      // CSM 输入

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

// 同时携带两个独立索引：frameIndex 选择可复用的命令/逐帧资源，
// imageIndex 选择本次获准写入的交换链图像；两者不能互相替代。
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
