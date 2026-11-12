#include "render/Renderer.hpp"
#include "vulkan/context/VulkanContext.hpp"
#include "vulkan/presentation/Swapchain.hpp"
#include "vulkan/VulkanCheck.hpp"

#include <stdexcept>
#include <utility>
#include <iostream>

Renderer::~Renderer() noexcept
{
    shutdown();
}

// 按依赖顺序准备布局、帧槽和各条渲染路径；任一步失败都通过 shutdown 回收。
void Renderer::initialize(VulkanContext& context, Swapchain& swapchain)
{
    if (initialized_)
    {
        throw std::logic_error("Renderer is already initialized");
    }
    try
    {
        context_ = &context;
        swapchain_ = &swapchain;

        createDescriptorSetLayouts();
        createPipelineLayouts();

        createFrameContexts();

        // -- gpu 计时 --
        createTimestampQueryPools();
        latestGpuTiming_ = {};
        latestGpuTiming_.supported = gpuTimingSupported_;
        for (GpuPassTiming& timing : latestGpuTiming_.passes)
        {
            timing.state = gpuTimingSupported_ ? GpuTimingState::Pending : GpuTimingState::Unavailable;
        }

        // -- cpu timing --
        currentCpuTiming_ = {};
        latestCpuTiming_ = {};

        uploadContext_.initialize(context);
        // -- 初始化阴影渲染管线 --
        createShadowTargets();
        if (!useDynamicRenderingForShadow())
        {
            createShadowRenderPass();
            createShadowFramebuffers();
        }
        createShadowPipeline();
        // -- hdr 资源--
        createHdrTargets();
        // -- g-buffer --
        createGBufferTargets();
        if (!useDynamicRenderingForScene())
        {
            createSceneRenderPass();
            createHdrFramebuffers();
        }

        createPostDescriptors();
        if (!useDynamicRenderingForPresent())
        {
            createPresentRenderPass();
        }
        createPostPipelineLayout();

        // -- 创建 pipeline --
        createGraphicsPipeline();
        createGBufferPipelines();
        createSkyboxPipeline();
        createShadowPreviewPipeline();
        createTonemapPipeline();
        createGBufferPreviewResources();
        createDeferredResources();

        std::cout << "Present path: " << (useDynamicRenderingForPresent() ? "Dynamic Rendering" : "RenderPass") << "\n";
        std::cout << "Scene path: " << (useDynamicRenderingForScene() ? "Dynamic Rendering" : "RenderPass")
                  << ", samples=" << static_cast<unsigned int>(sceneSamples_) << "\n";
        std::cout << "Shadow path: " << (useDynamicRenderingForShadow() ? "Dynamic Rendering" : "RenderPass") << "\n";
        initialized_ = true;
    }
    catch (...)
    {
        shutdown();
        throw;
    }
}

void Renderer::shutdown() noexcept
{
    if (context_ != nullptr)
    {
        const VkDevice device = context_->device();

        if (device != VK_NULL_HANDLE)
        {
            destroyDeferredResources();
            destroyGBufferPreviewResources();

            if (tonemapPipeline_ != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, tonemapPipeline_, nullptr);
                tonemapPipeline_ = VK_NULL_HANDLE;
            }

            if (postPipelineLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(device, postPipelineLayout_, nullptr);
                postPipelineLayout_ = VK_NULL_HANDLE;
            }

            if (presentRenderPass_ != VK_NULL_HANDLE)
            {
                vkDestroyRenderPass(device, presentRenderPass_, nullptr);
                presentRenderPass_ = VK_NULL_HANDLE;
            }

            if (gBufferPipeline_ != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, gBufferPipeline_, nullptr);
                gBufferPipeline_ = VK_NULL_HANDLE;
            }

            if (gBufferDoubleSidedPipeline_ != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, gBufferDoubleSidedPipeline_, nullptr);
                gBufferDoubleSidedPipeline_ = VK_NULL_HANDLE;
            }

            destroyPostDescriptors();
            destroyGBufferTargets();
            destroyHdrTargets();
            if (sceneRenderPass_ != VK_NULL_HANDLE)
            {
                vkDestroyRenderPass(device, sceneRenderPass_, nullptr);
                sceneRenderPass_ = VK_NULL_HANDLE;
            }
            destroyShadowTargets();

            destroyFrameContexts();

            uploadContext_.shutdown();

            if (skyboxPipeline_ != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, skyboxPipeline_, nullptr);
            }

            if (graphicsPipeline_ != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, graphicsPipeline_, nullptr);
            }

            if (graphicsDoubleSidedPipeline_ != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, graphicsDoubleSidedPipeline_, nullptr);
            }

            if (skyboxPipelineLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(device, skyboxPipelineLayout_, nullptr);
            }

            if (scenePipelineLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(device, scenePipelineLayout_, nullptr);
            }

            if (skyboxDescriptorSetLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device, skyboxDescriptorSetLayout_, nullptr);
            }

            if (materialDescriptorSetLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device, materialDescriptorSetLayout_, nullptr);
            }

            if (frameDescriptorSetLayout_ != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device, frameDescriptorSetLayout_, nullptr);
            }
        }
    }

    gpuSubmissionCounter_ = 0;
    latestGpuTiming_ = {};
    currentCpuTiming_ = {};
    latestCpuTiming_ = {};

    uploadContext_.shutdown();

    skyboxPipeline_ = VK_NULL_HANDLE;
    graphicsPipeline_ = VK_NULL_HANDLE;
    graphicsDoubleSidedPipeline_ = VK_NULL_HANDLE;
    scenePipelineLayout_ = VK_NULL_HANDLE;
    skyboxPipelineLayout_ = VK_NULL_HANDLE;
    materialDescriptorSetLayout_ = VK_NULL_HANDLE;
    frameDescriptorSetLayout_ = VK_NULL_HANDLE;
    skyboxDescriptorSetLayout_ = VK_NULL_HANDLE;
    sceneRenderPass_ = VK_NULL_HANDLE;
    hdrFormat_ = VK_FORMAT_UNDEFINED;
    sceneDepthFormat_ = VK_FORMAT_UNDEFINED;
    sceneSamples_ = VK_SAMPLE_COUNT_1_BIT;

    currentFrame_ = 0;
    gpuTimingSupported_ = false;
    timestampValidBits_ = 0;
    timestampPeriodNs_ = 0.0;

    hasActiveFrame_ = false;
    hasRecordedFrame_ = false;
    initialized_ = false;

    swapchain_ = nullptr;
    context_ = nullptr;
}

// 执行一次性的GPU操作，比如复制Buffer、复制纹理、生成MIPMAP、IBL预计算
void Renderer::immediateSubmit(std::function<void(VkCommandBuffer)>&& function)
{
    if (!initialized_)
    {
        throw std::logic_error("Renderer upload context is not initialized");
    }
    uploadContext_.immediateSubmit(std::move(function));
}

std::vector<GpuBuffer> Renderer::uploadBuffers(const std::vector<BufferUploadRequest>& requests)
{
    if (!initialized_)
    {
        throw std::logic_error("cannot upload buffers before Renderer initialization");
    }
    return uploadContext_.uploadBuffers(requests);
}
