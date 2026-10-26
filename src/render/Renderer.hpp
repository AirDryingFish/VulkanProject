#pragma once

#include "core/FrameConfig.hpp"
#include "diagnostics/FrameTiming.hpp"
#include "render/RenderTypes.hpp"
#include "vulkan/VulkanResources.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>
#include <string>

class VulkanContext;
class Swapchain;

struct BufferUploadRequest
{
    const void* data = nullptr;
    VkDeviceSize size = 0;
    VkBufferUsageFlags destinationUsage = 0;
    const char* debugName = nullptr;
};

class Renderer
{
private:
    struct HdrFrameTarget
    {
        GpuImage hdrColor;
        GpuImage msaaColor;
        GpuImage depth;

        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

    struct ShadowTarget
    {
        GpuImage depth;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

    // 帧槽按 CPU 提交顺序循环复用；其索引与 acquire 返回的交换链图像索引独立。
    struct FrameContext
    {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence renderFence = VK_NULL_HANDLE;
        // 一帧在 GPU 上执行时，属于这帧自己的全部运行时资源
        VkQueryPool timestampQueryPool = VK_NULL_HANDLE;
        bool timestampQueriesPending = false; // 是否有已经成功提交但未读回的时间戳结果
        // ----
        // 本次录制中，哪些 pass 已经写入开始和结束时间戳
        // 第 0 位：shadow。第 1 位：scene。第 2 位：postAndUi
        // ----
        std::uint32_t recordedGpuPassMask = 0;
        // 录制与提交分开保存：录完命令不代表 vkQueueSubmit 已经成功。
        FrameTimingConfig recordedTimingConfig{};
        // 成功提交的命令里，哪些 pass 实际录入了有效时间戳
        std::uint32_t submittedGpuPassMask = 0;
        // 这个 frame slot 当前保存的成功提交序号
        std::uint64_t submittedGpuSubmissionId = 0;
        // 成功提交的命令对应副本
        FrameTimingConfig submittedTimingConfig{};

        DrawStatistics recordedDraws{};

        // 持有被移出场景的 buffer，等此槽的 fence 完成后再真正析构。
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

        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;

        bool useDynamicRendering = false;
        VkFormat colorAttachmentFormat = VK_FORMAT_UNDEFINED;
        VkFormat depthAttachmentFormat = VK_FORMAT_UNDEFINED;
        VkFormat stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;

        bool useVertexInput = true;
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        bool depthTest = true;
        bool depthWrite = true;
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
    };

    VulkanContext *context_ = nullptr;
    Swapchain *swapchain_ = nullptr;

    // -- 主绘制 pipeline --
    VkDescriptorSetLayout frameDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout skyboxDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout scenePipelineLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout skyboxPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
    VkPipeline graphicsDoubleSidedPipeline_ = VK_NULL_HANDLE;
    VkPipeline skyboxPipeline_ = VK_NULL_HANDLE;
    // ----

    // -- Renderer 全局成员 --
    std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> frames_{};
    // Renderer共用的设备能力信息
    // 计时资源是否可用
    bool gpuTimingSupported_ = false;
    // 时间戳有效位数，以后处理计数器回绕时使用
    std::uint32_t timestampValidBits_ = 0;
    // 一个时间戳 tick 对应多少纳秒 ns，以后换算成 ms 时使用
    double timestampPeriodNs_ = 0.0;
    // ----

    // -- HDR render pass --
    // Scene pass: 场景 -> HDR 图片
    std::array<HdrFrameTarget, MAX_FRAMES_IN_FLIGHT> hdrTargets_{};
    VkFormat hdrFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat sceneDepthFormat_ = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits sceneSamples_ = VK_SAMPLE_COUNT_1_BIT;
    VkRenderPass sceneRenderPass_ = VK_NULL_HANDLE;
    // Present pass: 采样 HDR 图片 -> tone mapping -> 交换链图片
    VkRenderPass presentRenderPass_ = VK_NULL_HANDLE;
    // -- end --

    // -- 后处理 Descriptor set --
    GpuSampler postSampler_{};
    VkDescriptorSetLayout postDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool postDescriptorPool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> postDescriptorSets_{};

    VkPipelineLayout postPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline tonemapPipeline_ = VK_NULL_HANDLE;
    // -- end --

    // -- shadow map 绘制 --
    std::array<ShadowTarget, MAX_FRAMES_IN_FLIGHT> shadowTargets_{};
    VkFormat shadowDepthFormat_ = VK_FORMAT_UNDEFINED;
    VkRenderPass shadowRenderPass_ = VK_NULL_HANDLE;

    VkPipelineLayout shadowPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline shadowPipeline_ = VK_NULL_HANDLE;
    VkPipeline shadowDoubleSidedPipeline_ = VK_NULL_HANDLE;

    GpuSampler shadowCompareSampler_; // 比较参考深度，输出可见度
    GpuSampler shadowPreviewSampler_; // 读取原始深度，用于调试预览

    VkPipeline shadowPreviewPipeline_ = VK_NULL_HANDLE;
    void createShadowPreviewPipeline();
    void recordShadowPreview(const FrameToken& token, const RenderFrameData& data);
    // -- end --

    // -- gpu timing--
    std::uint64_t gpuSubmissionCounter_ = 0;
    GpuTimingSnapshot latestGpuTiming_{};

    void collectGpuTimings(FrameContext& frame, std::uint32_t frameSlot);
    // -- end --

    // -- cpu timing--
    RendererCpuTimingSnapshot currentCpuTiming_{}; // 当前正在构建的半成品
    RendererCpuTimingSnapshot latestCpuTiming_{};  // 最近一次成功提交的完整 Renderer 结果
    // -- end --

    UploadContext uploadContext_{};

    uint32_t currentFrame_ = 0;
    bool hasActiveFrame_ = false;
    bool hasRecordedFrame_ = false;
    bool initialized_ = false;


    void createTimestampQueryPools();
    void createUploadContext();

    void createDescriptorSetLayouts();
    void createPipelineLayouts();

    void createGraphicsPipeline();
    void createSkyboxPipeline();

    // -- hdr --
    void selectHdrConfiguration(); // 选择支持的格式和采样数
    void createHdrTargets(); // 创建图像

    void recordSceneAttachmentsBegin(const FrameToken& token);
    void recordSceneColorForPost(const FrameToken& token);

    bool useDynamicRenderingForScene() const noexcept;

    void beginScenePass(const FrameToken& token, const std::array<VkClearValue, 2>& clearValues);
    void endScenePass(const FrameToken& token);

    void createSceneRenderPass();
    void createPresentRenderPass();
    void createHdrFramebuffers();
    void destroyHdrTargets() noexcept; // 释放图像
    // ----

    // -- post processing --
    void createPostDescriptors();
    void writePostDescriptors();
    void destroyPostDescriptors() noexcept;

    void createPostPipelineLayout();
    void createTonemapPipeline();

    enum class PresentImageTransition
    {
        ToColorAttachment,
        ToPresent
    };

    void recordPresentImageTransition(const FrameToken& token, PresentImageTransition transition);
    void beginPresentPass(const FrameToken& token, const VkClearValue& value);
    void endPresentPass(const FrameToken& token);

    // ----

    // -- shadow --
    void createShadowTargets();
    void destroyShadowTargets() noexcept;

    void createShadowRenderPass();
    void createShadowPipeline();
    void createShadowFramebuffers();
    void recordShadowPass(const FrameToken& token, const RenderFrameData& data);

    bool useDynamicRenderingForShadow() const noexcept;
    enum class ShadowImageTransition
    {
        ToAttachment,
        ToSampling
    };

    void recordShadowImageTransition(const FrameToken& token, ShadowImageTransition transition);

    void beginShadowPass(const FrameToken& token, const VkClearValue& clearValue);
    void endShadowPass(const FrameToken& token);

    // ----

    VkPipeline createGraphicsPipelineFromConfig(const GraphicsPipelineConfig &config);

public:
    Renderer() noexcept = default;
    ~Renderer() noexcept;

    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    Renderer(Renderer &&) = delete;
    Renderer &operator=(Renderer &&) = delete;

    void initialize(VulkanContext &context, Swapchain &swapchain);
    // 调用方先保证 GPU 不再使用这些资源，并在 VulkanContext 关闭之前调用。
    void shutdown() noexcept;

    // 只有 beginFrame 返回 Ready 才能 record，再 end；重建/跳帧不能提交旧 token。
    BeginFrameResult beginFrame();
    void recordFrame(const FrameToken &token, const RenderFrameData &data);
    FrameStatus endFrame(const FrameToken &token);

    void waitForAllFrames();
    void retireBuffer(GpuBuffer &&buffer);


    bool hasActiveFrame() const noexcept;

    bool useDynamicRenderingForPresent() const noexcept;

    VkRenderPass presentRenderPass() const noexcept;
    VkSampleCountFlagBits sceneSamples() const noexcept;

    void recreateHdrTargets();

    VkDescriptorSetLayout frameDescriptorSetLayout() const noexcept;
    VkDescriptorSetLayout skyboxDescriptorSetLayout() const noexcept;
    VkDescriptorSetLayout materialDescriptorSetLayout() const noexcept;

    // 同步上传辅助：成功返回时 GPU 已完成回调录制的命令，临时 staging 才可释放。
    void immediateSubmit(std::function<void(VkCommandBuffer)> &&function);

    std::vector<GpuBuffer> uploadBuffers(const std::vector<BufferUploadRequest>& requests);

    VkDescriptorImageInfo shadowDescriptorInfo(std::size_t frameIndex) const;
    VkDescriptorImageInfo shadowPreviewDescriptorInfo(std::size_t frameIndex) const;

    const GpuTimingSnapshot& gpuTimingSnapshot() const noexcept;
    const RendererCpuTimingSnapshot& cpuTimingSnapshot() const noexcept;

    RenderTargetPayloadEstimate renderTargetPayloadEstimate() const noexcept;
};
