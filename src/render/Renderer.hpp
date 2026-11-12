#pragma once

#include "core/FrameConfig.hpp"
#include "diagnostics/FrameTiming.hpp"
#include "render/RenderTypes.hpp"
#include "vulkan/resources/VulkanResources.hpp"
#include "vulkan/upload/UploadContext.hpp"
#include "render/shadow/ShadowCascades.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>
#include <string>

class VulkanContext;
class Swapchain;

// 组织帧槽、pass 与提交；上传命令资源由独立 UploadContext 管理。
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

    struct DeferredTarget
    {
        GpuImage hdrColor;

        GpuBuffer uniforms;
        void* mapped = nullptr;

        // 每个 tile 的描述信息，例如 灯数量、是否溢出
        GpuBuffer tileHeaders;
        // 每个 tile 对应的灯光索引
        GpuBuffer tileIndices;
        // 是否已经录制过 compute 写入
        bool tileListsWritten = false;

        VkDescriptorSet inputSet = VK_NULL_HANDLE;
        VkDescriptorSet postSet = VK_NULL_HANDLE;

        bool sampledLayout = false;
    };

    struct GBufferTarget
    {
        // 对应 fragment shader 的 location
        // 0: normal
        // 1: base color
        // 2: material
        // 3: emissive
        std::array<GpuImage, 4> colors{};
        GpuImage depth{};
        // 记录这组 G-buffer 图片当前是否已经转换到了 "供 shader 采样读取" 的 layout
        bool sampledLayout = false;
    };

    struct ShadowTarget
    {
        GpuImage depth;

        // 独占 4 个单层 2D view，销毁 image 前逐个释放
        std::array<VkImageView, shadowCascadeCount> layerViews{};

        std::array<VkFramebuffer, shadowCascadeCount> framebuffers{};
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
        // 本次录制中，哪些 pass 已经写入开始和结束时间戳
        // 第 0 位：shadow。第 1 位：scene。第 2 位：postAndUi
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

    struct GraphicsPipelineConfig
    {
        std::string vertShaderPath;
        std::string fragShaderPath;

        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;

        bool useDynamicRendering = false;
        std::vector<VkFormat> colorAttachmentFormats{};
        VkFormat depthAttachmentFormat = VK_FORMAT_UNDEFINED;
        VkFormat stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;

        bool useVertexInput = true;
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        bool depthTest = true;
        bool depthWrite = true;
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
    };

    VulkanContext* context_ = nullptr;
    Swapchain* swapchain_ = nullptr;

    // -- 主绘制 pipeline --
    VkDescriptorSetLayout frameDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout skyboxDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout scenePipelineLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout skyboxPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
    VkPipeline graphicsDoubleSidedPipeline_ = VK_NULL_HANDLE;
    VkPipeline skyboxPipeline_ = VK_NULL_HANDLE;

    // -- Renderer 全局成员 --
    std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> frames_{};
    // 计时资源是否可用
    bool gpuTimingSupported_ = false;
    // 时间戳有效位数，以后处理计数器回绕时使用
    std::uint32_t timestampValidBits_ = 0;
    // 一个时间戳 tick 对应多少纳秒 ns，以后换算成 ms 时使用
    double timestampPeriodNs_ = 0.0;

    // -- HDR render pass --
    // Scene pass: 场景 -> HDR 图片
    std::array<HdrFrameTarget, MAX_FRAMES_IN_FLIGHT> hdrTargets_{};
    VkFormat hdrFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat sceneDepthFormat_ = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits sceneSamples_ = VK_SAMPLE_COUNT_1_BIT;
    VkRenderPass sceneRenderPass_ = VK_NULL_HANDLE;
    // Present pass: 采样 HDR 图片 -> tone mapping -> 交换链图片
    VkRenderPass presentRenderPass_ = VK_NULL_HANDLE;

    // -- G-buffer: 每个飞行帧槽独立，全部为 1x --
    static constexpr std::array<VkFormat, 4> gBufferColorFormats_{VK_FORMAT_R16G16B16A16_SFLOAT,
                                                                  VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                                                                  VK_FORMAT_R16G16B16A16_SFLOAT};
    static constexpr VkFormat gBufferDepthFormat_ = VK_FORMAT_D32_SFLOAT;

    std::array<GBufferTarget, MAX_FRAMES_IN_FLIGHT> gBufferTargets_{};
    VkPipeline gBufferPipeline_ = VK_NULL_HANDLE;
    VkPipeline gBufferDoubleSidedPipeline_ = VK_NULL_HANDLE;
    bool gBufferAvailable_ = false;
    std::string gBufferUnavailableReason_;

    GpuSampler gBufferPreviewSampler_{};
    VkDescriptorSetLayout gBufferPreviewSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool gBufferPreviewPool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> gBufferPreviewSets_{};
    VkPipelineLayout gBufferPreviewLayout_ = VK_NULL_HANDLE;
    VkPipeline gBufferPreviewPipeline_ = VK_NULL_HANDLE;

    std::array<DeferredTarget, MAX_FRAMES_IN_FLIGHT> deferredTargets_{};
    VkDescriptorSetLayout deferredSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool deferredPool_ = VK_NULL_HANDLE;
    VkPipelineLayout deferredPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline deferredPipeline_ = VK_NULL_HANDLE;

    VkPipeline tileCullPipeline_ = VK_NULL_HANDLE;
    std::uint32_t tileCountX_ = 0;
    std::uint32_t tileCountY_ = 0;

    bool tiledConfigurationSupported_ = false;
    std::string tiledUnavailableReason_;

    // -- 后处理 Descriptor set --
    GpuSampler postSampler_{};
    VkDescriptorSetLayout postDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool postDescriptorPool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> postDescriptorSets_{};

    VkPipelineLayout postPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline tonemapPipeline_ = VK_NULL_HANDLE;

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

    // -- gpu timing--
    std::uint64_t gpuSubmissionCounter_ = 0;
    GpuTimingSnapshot latestGpuTiming_{};

    // -- cpu timing--
    RendererCpuTimingSnapshot currentCpuTiming_{}; // 当前正在构建的半成品
    RendererCpuTimingSnapshot latestCpuTiming_{};  // 最近一次成功提交的完整 Renderer 结果

    UploadContext uploadContext_{};

    uint32_t currentFrame_ = 0;
    bool hasActiveFrame_ = false;
    bool hasRecordedFrame_ = false;
    bool initialized_ = false;

    // 图像布局转换
    enum class PresentImageTransition
    {
        ToColorAttachment,
        ToPresent
    };

    enum class ShadowImageTransition
    {
        ToAttachment,
        ToSampling
    };

    // 帧槽资源
    void createFrameContexts();
    // GPU 已不再使用帧槽后，释放命令、同步和查询资源。
    void destroyFrameContexts() noexcept;

    // 管线配置
    void createDescriptorSetLayouts();
    void createPipelineLayouts();
    // 按既有 layout 和附件配置创建管线，返回句柄由对应功能组释放。
    VkPipeline createGraphicsPipelineFromConfig(const GraphicsPipelineConfig& config);

    // HDR 与 Forward
    // 验证设备支持的 HDR、深度格式和采样数组合，创建目标前调用。
    void selectHdrConfiguration();
    void createHdrTargets();
    void createSceneRenderPass();
    void createHdrFramebuffers();
    void createGraphicsPipeline();
    void createSkyboxPipeline();

    // 绘制天空盒与几何，结束后 HDR 颜色可由后处理采样。
    void recordForwardPass(const FrameToken& token, const RenderFrameData& data,
                           const std::array<VkClearValue, 2>& clearValues);
    // 准备场景附件；布局转换与 legacy RenderPass 路径保持相同输出约定。
    void recordSceneAttachmentsBegin(const FrameToken& token);
    void beginScenePass(const FrameToken& token, const std::array<VkClearValue, 2>& clearValues);
    void endScenePass(const FrameToken& token);
    void recordSceneColorForPost(const FrameToken& token);

    void destroyHdrTargets() noexcept;
    bool useDynamicRenderingForScene() const noexcept;

    // G-buffer 几何
    void createGBufferTargets();
    void createGBufferPipelines();
    // 录制几何并将 G-buffer 转为后续预览或光照可采样的布局。
    void recordGBufferPass(const FrameToken& token, const RenderFrameData& data);
    void destroyGBufferTargets() noexcept;

    // G-buffer 预览
    void createGBufferPreviewResources();
    void writeGBufferPreviewDescriptors();
    void recordGBufferPreview(const FrameToken& token, const RenderFrameData& data);
    void destroyGBufferPreviewResources() noexcept;

    // Deferred Lighting
    void createDeferredResources();
    // 帧槽 fence 完成后更新该槽光照数据；光照录制前 G-buffer 须已转为可采样布局。
    void prepareDeferredFrame(const FrameToken& token, const RenderFrameData& data);
    void recordDeferredLightingPass(const FrameToken& token, const RenderFrameData& data);
    void destroyDeferredResources() noexcept;

    // Tiled light culling
    void createTileBuffers();
    void createTilePipeline();
    // 在 Deferred Lighting 前生成当前帧 tile 灯列表，并同步 compute 写入供片元读取。
    void recordTileLightLists(const FrameToken& token, const RenderFrameData& data);
    void destroyTileResources() noexcept;

    // Shadow 与预览
    void createShadowTargets();
    void createShadowRenderPass();
    void createShadowFramebuffers();
    void createShadowPipeline();
    void createShadowPreviewPipeline();

    // 为各级联录制深度，结束后转换为场景与预览可采样的布局。
    void recordShadowPass(const FrameToken& token, const RenderFrameData& data);
    void recordShadowCascade(const FrameToken& token, const RenderFrameData& data, std::uint32_t cascadeIndex);
    void recordShadowImageTransition(const FrameToken& token, ShadowImageTransition transition,
                                     std::uint32_t cascadeIndex);
    void beginShadowPass(const FrameToken& token, const VkClearValue& clearValue, std::uint32_t cascadeIndex);
    void endShadowPass(const FrameToken& token, std::uint32_t cascadeIndex);
    void recordShadowPreview(const FrameToken& token, const RenderFrameData& data);

    void destroyShadowTargets() noexcept;
    bool useDynamicRenderingForShadow() const noexcept;

    // Post 与 Present
    void createPresentRenderPass();
    void createPostDescriptors();
    void createPostPipelineLayout();
    void createTonemapPipeline();

    // 重写目标图像的描述符前，调用方须保证 GPU 不再使用原描述符。
    void writePostDescriptors();
    // 在已开启的 Present pass 中采样实际渲染路径的 HDR 输出。
    void recordToneMapping(const FrameToken& token, const RenderFrameData& data, bool useDeferred);
    void recordPresentImageTransition(const FrameToken& token, PresentImageTransition transition);
    void beginPresentPass(const FrameToken& token, const VkClearValue& value);
    void endPresentPass(const FrameToken& token);
    void destroyPostDescriptors() noexcept;

    // GPU 计时
    void createTimestampQueryPools();
    // 保存本次实际路径和配置，供该次提交完成后的诊断解释。
    void captureFrameTimingConfig(const FrameToken& token, const RenderFrameData& data, bool useDeferred);
    // 仅在该帧槽 fence 完成后读回成功提交的时间戳，复用查询池前调用。
    void collectGpuTimings(FrameContext& frame, std::uint32_t frameSlot);

public:
    // 生命周期
    Renderer() noexcept = default;
    ~Renderer() noexcept;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    void initialize(VulkanContext& context, Swapchain& swapchain);
    // 已初始化且无活动帧；调用方先等待 GPU 空闲，再按新交换链尺寸重建目标。
    void recreateHdrTargets();
    // 调用方先保证 GPU 不再使用这些资源，并在 VulkanContext 关闭之前调用。
    void shutdown() noexcept;

    // 帧录制与提交
    // 只有 beginFrame 返回 Ready 才能 record，再 end；重建/跳帧不能提交旧 token。
    BeginFrameResult beginFrame();
    void recordFrame(const FrameToken& token, const RenderFrameData& data);
    FrameStatus endFrame(const FrameToken& token);
    void waitForAllFrames();

    // 资源上传与退役
    // 同步上传辅助：成功返回时 GPU 已完成回调录制的命令，临时 staging 才可释放。
    void immediateSubmit(std::function<void(VkCommandBuffer)>&& function);

    // 同步批量上传；返回的目标 buffer 由调用方持有，输入数据须在调用期间有效。
    std::vector<GpuBuffer> uploadBuffers(const std::vector<BufferUploadRequest>& requests);
    // 接管 buffer，延迟到帧槽 fence 完成后释放；未初始化时立即释放。
    void retireBuffer(GpuBuffer&& buffer);

    // 资源与能力查询
    bool hasActiveFrame() const noexcept;
    bool useDynamicRenderingForPresent() const noexcept;
    VkRenderPass presentRenderPass() const noexcept;
    VkSampleCountFlagBits sceneSamples() const noexcept;
    VkDescriptorSetLayout frameDescriptorSetLayout() const noexcept;
    VkDescriptorSetLayout skyboxDescriptorSetLayout() const noexcept;
    VkDescriptorSetLayout materialDescriptorSetLayout() const noexcept;

    // 返回指定帧槽的借用图像句柄；描述符使用期间阴影资源必须保持有效。
    VkDescriptorImageInfo shadowDescriptorInfo(std::size_t frameIndex) const;
    VkDescriptorImageInfo shadowPreviewDescriptorInfo(std::size_t frameIndex) const;

    bool gBufferGeometryAvailable() const noexcept;
    bool deferredLightingAvailable() const noexcept;
    bool tiledLightingAvailable() const noexcept;
    const std::string& tiledUnavailableReason() const noexcept;

    // 诊断查询
    const GpuTimingSnapshot& gpuTimingSnapshot() const noexcept;
    const RendererCpuTimingSnapshot& cpuTimingSnapshot() const noexcept;
    RenderTargetPayloadEstimate renderTargetPayloadEstimate() const noexcept;
};
