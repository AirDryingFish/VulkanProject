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

#include <cstdint>
#include <vector>

#include <vk_mem_alloc.h>

struct VulkanContextConfig
{
    const char* applicationName = "VulkanProject";
    uint32_t apiVersion = VK_API_VERSION_1_0;
    bool enableValidation = false;
};
