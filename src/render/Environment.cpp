#include "render/Environment.hpp"
#include "render/Renderer.hpp"
#include "vulkan/UploadCommands.hpp"
#include "vulkan/VulkanContext.hpp"

#include <stdexcept>

namespace
{
template<typename Handle, typename Destroy>
void destroyHandle(VkDevice device, Handle& handle, Destroy destroy) noexcept
{
    if (handle != VK_NULL_HANDLE)
    {
        destroy(device, handle, nullptr);
        handle = VK_NULL_HANDLE;
    }
}

VkDescriptorImageInfo descriptorInfo(const GpuImage& image, const GpuSampler& sampler) noexcept
{
    return {sampler.get(), image.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
}
}

Environment::~Environment() noexcept
{
    shutdown();
}

void Environment::initialize(VulkanContext& context, Renderer& renderer)
{
    if (device_ != VK_NULL_HANDLE)
    {
        throw std::logic_error("Environment is already initialized");
    }
    if (context.device() == VK_NULL_HANDLE)
    {
        throw std::logic_error("Environment requires an initialized VulkanContext");
    }

    context_ = &context;
    renderer_ = &renderer;
    device_ = context.device();
    try
    {
        createSkyboxImage();
        createSkyboxSampler();
        createIrradianceResources();
        createPrefilterResources();
        createBRDFLUTResources();

        // Every immediateSubmit waits for its fence, including the final image
        // transition. The bake-only objects are no longer used by the GPU.
        destroyPrecomputeResources();

        context_ = nullptr;
        renderer_ = nullptr;
    }
    catch (...)
    {
        // A submission may have succeeded before an immediateSubmit fence wait
        // failed. Only the exceptional initialization path needs an extra wait.
        (void)context.waitIdle();
        shutdown();
        throw;
    }
}

void Environment::destroyPrecomputeResources() noexcept
{
    if (device_ == VK_NULL_HANDLE)
    {
        return;
    }

    destroyHandle(device_, irradiancePipeline_, vkDestroyPipeline);
    destroyHandle(device_, prefilterPipeline_, vkDestroyPipeline);
    destroyHandle(device_, brdfLUTPipeline_, vkDestroyPipeline);
    destroyHandle(device_, irradiancePipelineLayout_, vkDestroyPipelineLayout);
    destroyHandle(device_, prefilterPipelineLayout_, vkDestroyPipelineLayout);
    destroyHandle(device_, brdfLUTPipelineLayout_, vkDestroyPipelineLayout);
    destroyHandle(device_, irradianceDescriptorPool_, vkDestroyDescriptorPool);
    destroyHandle(device_, prefilterDescriptorPool_, vkDestroyDescriptorPool);
    irradianceDescriptorSet_ = VK_NULL_HANDLE;
    prefilterDescriptorSet_ = VK_NULL_HANDLE;
    destroyHandle(device_, irradianceDescriptorSetLayout_, vkDestroyDescriptorSetLayout);
    destroyHandle(device_, prefilterDescriptorSetLayout_, vkDestroyDescriptorSetLayout);

    for (VkFramebuffer& framebuffer : irradianceFramebuffers_)
    {
        destroyHandle(device_, framebuffer, vkDestroyFramebuffer);
    }
    for (auto& mip : prefilterFramebuffers_)
    {
        for (VkFramebuffer& framebuffer : mip)
        {
            destroyHandle(device_, framebuffer, vkDestroyFramebuffer);
        }
    }
    destroyHandle(device_, brdfLUTFramebuffer_, vkDestroyFramebuffer);
    for (VkImageView& view : irradianceFaceImageViews_)
    {
        destroyHandle(device_, view, vkDestroyImageView);
    }
    for (auto& mip : prefilterFaceImageViews_)
    {
        for (VkImageView& view : mip)
        {
            destroyHandle(device_, view, vkDestroyImageView);
        }
    }
    destroyHandle(device_, irradianceRenderPass_, vkDestroyRenderPass);
    destroyHandle(device_, prefilterRenderpass_, vkDestroyRenderPass);
    destroyHandle(device_, brdfLUTRenderPass_, vkDestroyRenderPass);
}

void Environment::shutdown() noexcept
{
    if (device_ != VK_NULL_HANDLE)
    {
        destroyPrecomputeResources();
    }
    brdfLUTSampler_.reset();
    prefilterSampler_.reset();
    irradianceSampler_.reset();
    skyboxSampler_.reset();
    brdfLUTImage_.reset();
    prefilterImage_.reset();
    irradianceImage_.reset();
    skyboxImage_.reset();

    device_ = VK_NULL_HANDLE;
    context_ = nullptr;
    renderer_ = nullptr;
}

void Environment::transitionImageLayout(VkImage image, VkFormat format,
                                       VkImageLayout oldLayout, VkImageLayout newLayout,
                                       std::uint32_t mipLevels, std::uint32_t layerCount)
{
    renderer_->immediateSubmit([&](VkCommandBuffer commandBuffer)
    {
        upload::recordImageTransition(commandBuffer, image, format, oldLayout,
                                      newLayout, mipLevels, layerCount);
    });
}

VkDescriptorImageInfo Environment::skyboxDescriptorInfo() const noexcept
{
    return descriptorInfo(skyboxImage_, skyboxSampler_);
}

VkDescriptorImageInfo Environment::irradianceDescriptorInfo() const noexcept
{
    return descriptorInfo(irradianceImage_, irradianceSampler_);
}

VkDescriptorImageInfo Environment::prefilterDescriptorInfo() const noexcept
{
    return descriptorInfo(prefilterImage_, prefilterSampler_);
}

VkDescriptorImageInfo Environment::brdfLutDescriptorInfo() const noexcept
{
    return descriptorInfo(brdfLUTImage_, brdfLUTSampler_);
}
