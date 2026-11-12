#pragma once

#include "core/DeletionQueue.hpp"

struct GLFWwindow;
class VulkanContext;
class Renderer;
class Swapchain;

// 管理 ImGui 上下文、平台后端与独立描述符池；不拥有窗口和 VulkanContext。
class ImGuiBackend final
{
public:
    ImGuiBackend() = default;
    ~ImGuiBackend() noexcept;
    ImGuiBackend(const ImGuiBackend&) = delete;
    ImGuiBackend& operator=(const ImGuiBackend&) = delete;
    ImGuiBackend(ImGuiBackend&&) = delete;
    ImGuiBackend& operator=(ImGuiBackend&&) = delete;

    void initialize(VulkanContext& context, const Renderer& renderer, const Swapchain& swapchain, GLFWwindow* window);
    // 调用方先等待 GPU；在设备和窗口销毁前关闭，支持初始化中途失败后的清理。
    void shutdown() noexcept;

private:
    DeletionQueue deletionQueue_;
};
