#pragma once

#include "vulkan/VulkanResources.hpp"

#include <array>
#include <cstdint>

class Renderer;
class VulkanContext;

// 持有天空盒及 IBL 贴图；预计算的 pipeline/framebuffer 等只在初始化期间使用。
// 返回的 descriptor info 只是借用 image view/sampler，真正的所有权仍在 Environment。
// VulkanContext/device 必须比这里持有的 GPU 资源活得更久。
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
    // 调用方先等待使用贴图的 GPU 工作完成，并处理借用这些资源的 descriptor set。
    // 可重复调用，不在内部等待 device；必须在 VulkanContext 关闭前执行。
    void shutdown() noexcept;

    VkDescriptorImageInfo skyboxDescriptorInfo() const noexcept;
    VkDescriptorImageInfo irradianceDescriptorInfo() const noexcept;
    VkDescriptorImageInfo prefilterDescriptorInfo() const noexcept;
    VkDescriptorImageInfo brdfLutDescriptorInfo() const noexcept;

private:
    static constexpr std::uint32_t prefilterMipLevels = 5;

    // 初始化时借用设备分配/上传能力，预计算成功后不再保留这两个指针。
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

    // 以下为预计算临时资源；烘焙完成即可销毁，上方贴图/采样器留给逐帧渲染使用。
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
