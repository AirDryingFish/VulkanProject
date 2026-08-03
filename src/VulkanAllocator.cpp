#include "TriangleApplication.hpp"

#include <vk_mem_alloc.h>
#include <stdexcept>

void TriangleApplication::createAllocator()
{
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.instance = instance;
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_0;
    
    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &allocator));
}
