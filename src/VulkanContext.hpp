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

#include "VulkanHeaders.hpp"
#include "VulkanTypes.hpp"

#include "VulkanResources.hpp"

#include <cstdint>
#include <vector>

#include <vk_mem_alloc.h>

struct VulkanContextConfig
{
    const char* applicationName = "VulkanProject";
    uint32_t apiVersion = VK_API_VERSION_1_0;
    bool enableValidation = false;
};

class VulkanContext final
{
private:
    void createInstance(const VulkanContextConfig& config);
    
    void setupDebugMessenger();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createAllocator();
    
    bool checkValidationLayerSupport() const;

    std::vector<const char*> getRequiredExtensions() const;

    bool isDeviceSuitable(VkPhysicalDevice candidate) const;
    bool checkDeviceExtensionSupport(VkPhysicalDevice candidate) const;
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice candidate) const;

    SwapChainSupportDetails querySwapchainSupport(VkPhysicalDevice candidate) const;
    
    VkSampleCountFlagBits getMaxUsableSampleCount() const;

    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) const noexcept;

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
        void* userData
    );

    bool validationEnabled_ = false;
    uint32_t apiVersion_ = VK_API_VERSION_1_0;

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
    VkDebugUtilsMessengerEXT debugMessenger() const noexcept;
    VkSurfaceKHR surface() const noexcept;
    VkPhysicalDevice physicalDevice() const noexcept;
    VkDevice device() const noexcept;
    VkQueue graphicsQueue() const noexcept;
    VkQueue presentQueue() const noexcept;
    VmaAllocator allocator() const noexcept;

    const QueueFamilyIndices& queueFamilies() const noexcept;

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


    AllocatedBuffer createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties) const;

    AllocatedImage createImage(
        uint32_t width,
        uint32_t height,
        uint32_t mipLevels,
        VkSampleCountFlagBits numSamples,
        VkFormat format,
        VkImageTiling tiling,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties,
        uint32_t arrayLayers = 1,
        VkImageCreateFlags flags = 0);

};
