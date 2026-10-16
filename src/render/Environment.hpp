#pragma once

#include "vulkan/VulkanResources.hpp"

#include <array>
#include <cstdint>

class Renderer;
class VulkanContext;

// Owns the environment maps and the resources used to precompute them.
// The VulkanContext/device must outlive this object. Application descriptor
// sets borrow the returned views and samplers; they do not own these resources.
class Environment final
{
public:
    Environment() noexcept = default;
    ~Environment() noexcept;

    Environment(const Environment&) = delete;
    Environment& operator=(const Environment&) = delete;
    Environment(Environment&&) = delete;
    Environment& operator=(Environment&&) = delete;

    void initialize(VulkanContext& context, Renderer& renderer);
    // The caller must wait for outstanding GPU work before releasing these
    // borrowed resources, and call before shutting down the VulkanContext.
    // Repeated calls are harmless; shutdown itself does not wait for the device.
    void shutdown() noexcept;

    VkDescriptorImageInfo skyboxDescriptorInfo() const noexcept;
    VkDescriptorImageInfo irradianceDescriptorInfo() const noexcept;
    VkDescriptorImageInfo prefilterDescriptorInfo() const noexcept;
    VkDescriptorImageInfo brdfLutDescriptorInfo() const noexcept;

private:
    static constexpr std::uint32_t prefilterMipLevels = 5;

    // These two collaborators are needed only while initialize() is running.
    VulkanContext* context_ = nullptr;
    Renderer* renderer_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;

    GpuImage skyboxImage_;
    GpuSampler skyboxSampler_;
    GpuImage irradianceImage_;
    GpuSampler irradianceSampler_;
    GpuImage prefilterImage_;
    GpuSampler prefilterSampler_;
    GpuImage brdfLUTImage_;
    GpuSampler brdfLUTSampler_;

    VkRenderPass irradianceRenderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout irradianceDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool irradianceDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet irradianceDescriptorSet_ = VK_NULL_HANDLE;
    VkPipelineLayout irradiancePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline irradiancePipeline_ = VK_NULL_HANDLE;
    std::array<VkImageView, 6> irradianceFaceImageViews_{};
    std::array<VkFramebuffer, 6> irradianceFramebuffers_{};

    VkRenderPass prefilterRenderpass_ = VK_NULL_HANDLE;
    VkPipeline prefilterPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout prefilterPipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout prefilterDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool prefilterDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet prefilterDescriptorSet_ = VK_NULL_HANDLE;
    std::array<std::array<VkImageView, 6>, prefilterMipLevels> prefilterFaceImageViews_{};
    std::array<std::array<VkFramebuffer, 6>, prefilterMipLevels> prefilterFramebuffers_{};

    VkRenderPass brdfLUTRenderPass_ = VK_NULL_HANDLE;
    VkPipeline brdfLUTPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout brdfLUTPipelineLayout_ = VK_NULL_HANDLE;
    VkFramebuffer brdfLUTFramebuffer_ = VK_NULL_HANDLE;

    void createSkyboxImage();
    void createSkyboxSampler();
    void createIrradianceResources();
    void renderIrradianceCubemap();
    void createPrefilterResources();
    void renderPrefilterCubemap();
    void createBRDFLUTResources();
    void renderBRDFLUT();
    void destroyPrecomputeResources() noexcept;
    void transitionImageLayout(VkImage image, VkFormat format,
                               VkImageLayout oldLayout, VkImageLayout newLayout,
                               std::uint32_t mipLevels, std::uint32_t layerCount = 1);
};
