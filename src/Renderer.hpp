#pragma once

#include "AppConfig.hpp"
#include "RenderTypes.hpp"
#include "VulkanResources.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>
#include <string>

class VulkanContext;
class Swapchain;

class Renderer
{
private:
    struct FrameContext
    {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence renderFence = VK_NULL_HANDLE;

        std::vector<GpuBuffer> retiredBuffers;
    };

    struct UploadContext
    {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
    };

    struct GraphicsPipelineConfig
    {
        std::string vertShaderPath;
        std::string fragShaderPath;
        bool useVertexInput = true;
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        bool depthTest = true;
        bool depthWrite = true;
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
    };

    VulkanContext *context_ = nullptr;
    Swapchain *swapchain_ = nullptr;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
    VkPipeline skyboxPipeline_ = VK_NULL_HANDLE;

    std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> frames_{};

    UploadContext uploadContext_{};

    uint32_t currentFrame_ = 0;
    bool hasActiveFrame_ = false;
    bool hasRecordedFrame_ = false;
    bool initialized_ = false;

    void createUploadContext();
    void createRenderPass();

    void createDescriptorSetLayout();
    void createGraphicsPipeline();
    void createSkyboxPipeline();
    VkPipeline createGraphicsPipelineFromConfig(const GraphicsPipelineConfig &config);

public:
    Renderer() noexcept = default;
    ~Renderer() noexcept;

    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    Renderer(Renderer &&) = delete;
    Renderer &operator=(Renderer &&) = delete;

    void initialize(VulkanContext &context, Swapchain &swapchain);
    void shutdown() noexcept;

    // 让 FrameContext 保持为 Renderer 的私有实现，同时给 TriangleApplication 提供完成一帧所需的最少信息
    BeginFrameResult beginFrame();
    void recordFrame(const FrameToken &token, const RenderFrameData &data);
    FrameStatus endFrame(const FrameToken &token);

    void waitForAllFrames();
    void retireBuffer(GpuBuffer &&buffer);

    uint32_t currentFrameIndex() const noexcept;

    bool hasActiveFrame() const noexcept;

    VkRenderPass renderPass() const noexcept;

    VkDescriptorSetLayout descriptorSetLayout() const noexcept;

    void immediateSubmit(std::function<void(VkCommandBuffer)> &&function);
};
