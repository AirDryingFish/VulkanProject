//  VulkanContext 应拥有：
//     VkInstance
//     VkDebugUtilsMessengerEXT
//     VkSurfaceKHR
//     VkPhysicalDevice
//     VkDevice
//     Graphics Queue
//     Present Queue
//     Graphics/Present Queue family indices
//     VmaAllocator
//     MSAA sample count

#pragma once

#include "vulkan/VulkanHeaders.hpp"
#include "vulkan/VulkanTypes.hpp"

#include "vulkan/VulkanResources.hpp"

#include <cstdint>
#include <vector>
#include <string>

#include <vk_mem_alloc.h>

struct GLFWwindow;

struct VulkanContextConfig
{
    const char* applicationName = "VulkanProject";

    // 首选目标
    std::uint32_t apiVersion = VK_API_VERSION_1_3;

    // 当前旧式渲染路径仍接受 1.0
    std::uint32_t minimumApiVersion = VK_API_VERSION_1_0;

    bool enableValidation = false;
};

struct VulkanCapabilities
{
    // 编译时头文件的信息，不代表 GPU 支持范围
    std::uint32_t headerVersion = VK_HEADER_VERSION_COMPLETE;

    // 0 表示尚未查询或尚未创建
    std::uint32_t loaderVersion = 0;            // Loader 支持的 Instance版本
    std::uint32_t instanceVersion = 0;          // 本次创建 Instance 时请求的版本
    std::uint32_t physicalDeviceVersion = 0;    // 所选 GPU / 驱动报告的设备 API 版本

    // 本项目采用的设备核心上限，不代表所有 feature 已启用
    std::uint32_t deviceApiVersion = 0;

    bool features2QueryUsed = false;            // 选中的设备是否通过核心 Features2 查询
    bool samplerAnisotropySupported = false;    // 设备是否支持各向异性过滤
    bool samplerAnisotropyEnabled = false;      // 本次成功创建的 Device 是否启用了它

    bool vulkan13FeaturesQueried = false;

    bool synchronization2Supported = false;
    bool synchronization2Enabled = false;

    bool dynamicRenderingSupported = false;
    bool dynamicRenderingEnabled = false;

    std::string deviceName;
};

// 保存本项目现代渲染路径所需的设备函数入口
struct VulkanDeviceFunctions
{
    PFN_vkCmdPipelineBarrier2 cmdPipelineBarrier2 = nullptr;  // 表达资源访问依赖和布局转换
    PFN_vkQueueSubmit2 queueSubmit2 = nullptr;                // 提交命令缓冲及信号量等待通知
    PFN_vkCmdWriteTimestamp2 cmdWriteTimestamp2 = nullptr;    // 记录 GPU 时间戳

    PFN_vkCmdBeginRendering cmdBeginRendering = nullptr;      // 开始动态渲染
    PFN_vkCmdEndRendering cmdEndRendering = nullptr;          // 结束动态渲染

    bool synchronization2Loaded() const noexcept
    {
        return cmdPipelineBarrier2 != nullptr &&
               queueSubmit2 != nullptr &&
               cmdWriteTimestamp2 != nullptr;
    }

    bool dynamicRenderingLoaded() const noexcept
    {
        return cmdBeginRendering != nullptr && cmdEndRendering != nullptr;
    }
};

class VulkanContext final
{
private:

    struct DeviceCandidateInfo
    {
        VkPhysicalDeviceProperties properties{};
        QueueFamilyIndices queueFamilies{};
        VkPhysicalDeviceFeatures features{};

        bool queriedWithFeatures2 = false;
        std::vector<std::string> rejectionReasons;

        // 让每个候选 gpu 独立保存新功能的查询结果
        bool queredVulkan13Features = false;
        bool synchronization2Supported = false;
        bool dynamicRenderingSupported = false;

        bool suitable() const noexcept
        {
            return rejectionReasons.empty();
        }
    };

    void createInstance(const VulkanContextConfig& config);

    void setupDebugMessenger();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice(const VulkanContextConfig& config);
    void createLogicalDevice();
    void loadDeviceFunctions();
    void createAllocator();

    bool checkValidationLayerSupport() const;

    std::vector<const char*> getRequiredExtensions() const;

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice candidate) const;

    DeviceCandidateInfo inspectDevice(VkPhysicalDevice candidate, const VulkanContextConfig& config) const;

    std::vector<std::string> findMissingDeviceExtensions(VkPhysicalDevice candidate) const;

    SwapChainSupportDetails querySwapchainSupport(VkPhysicalDevice candidate) const;

    VkSampleCountFlagBits getMaxUsableSampleCount() const;

    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) const noexcept;

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
        void* userData
    );

    void queryLoaderVersion();
    void logCapabilities() const;

    bool validationEnabled_ = false;

    VulkanCapabilities capabilities_{};
    VulkanDeviceFunctions deviceFunctions_{};

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    QueueFamilyIndices queueFamilies_{};

    VmaAllocator allocator_ = nullptr;

    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;

public:
    VulkanContext() noexcept = default;
    ~VulkanContext() noexcept;

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    void initialize(GLFWwindow* window, const VulkanContextConfig& config);

    void shutdown() noexcept;

    VkResult waitIdle() const noexcept;

    VkInstance instance() const noexcept;
    VkSurfaceKHR surface() const noexcept;
    VkPhysicalDevice physicalDevice() const noexcept;
    VkDevice device() const noexcept;
    VkQueue graphicsQueue() const noexcept;
    VkQueue presentQueue() const noexcept;
    VmaAllocator allocator() const noexcept;

    const QueueFamilyIndices& queueFamilies() const noexcept;

    const VulkanCapabilities& capabilities() const noexcept;

    const VulkanDeviceFunctions& deviceFunctions() const noexcept;

    VkSampleCountFlagBits msaaSamples() const noexcept;

    SwapChainSupportDetails querySwapchainSupport() const;

    VkFormat findSupportedFormat(
        const std::vector<VkFormat>& candidates,
        VkImageTiling tiling,
        VkFormatFeatureFlags features
    ) const;
    VkFormat findDepthFormat() const;

    void setDebugName(
        VkObjectType objectType,
        uint64_t handle,
        const char* name
    ) const noexcept;

    GpuBuffer createBuffer(const BufferDesc& desc) const;
    GpuImage createImage(const ImageDesc& desc) const;

    VkImageView createImageView(
        VkImage image,
        VkFormat format,
        uint32_t mipLevels,
        VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT,
        VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D,
        uint32_t layerCount = 1
    ) const;

    GpuSampler createSampler(const SamplerDesc& desc) const;

    UniqueShaderModule createShaderModule(const std::vector<char> &code) const;

};
