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
struct VulkanContextConfig
{
    const char* applicationName = "VulkanProject";
    uint32_t apiVersion = VK_API_VERSION_1_0;
    bool enableValidation = false;
};
