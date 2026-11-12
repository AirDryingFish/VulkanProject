#include "vulkan/context/VulkanContext.hpp"
#include "vulkan/VulkanCheck.hpp"

#include <cassert>
#include <stdexcept>

// 内存分配器：所有返回的 GPU 资源必须先于它销毁。
void VulkanContext::createAllocator()
{
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.instance = instance_;
    allocatorInfo.physicalDevice = physicalDevice_;
    allocatorInfo.device = device_;
    allocatorInfo.vulkanApiVersion = capabilities_.deviceApiVersion;

    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &allocator_));
}

// 资源创建：校验描述后将所有权交给 RAII 资源。
GpuBuffer VulkanContext::createBuffer(const BufferDesc& desc) const
{
    if (allocator_ == nullptr)
    {
        throw std::logic_error("cannot create buffer before allocator initialization");
    }

    if (desc.size == 0)
    {
        throw std::invalid_argument("buffer size must be greater than zero");
    }

    if (desc.usage == 0)
    {
        throw std::invalid_argument("buffer usage must not be empty");
    }

    VkBuffer rawBuffer = VK_NULL_HANDLE;
    VmaAllocation rawAllocation = nullptr;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.size;
    bufferInfo.usage = desc.usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    if (desc.requiredMemoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }
    else
    {
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    }
    allocInfo.requiredFlags = desc.requiredMemoryProperties;

    VK_CHECK(vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &rawBuffer, &rawAllocation, nullptr));

    VkMemoryPropertyFlags actualMemoryProperties = 0;
    vmaGetAllocationMemoryProperties(allocator_, rawAllocation, &actualMemoryProperties);

    assert((actualMemoryProperties & desc.requiredMemoryProperties) == desc.requiredMemoryProperties);

    setDebugName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(rawBuffer), desc.debugName);

    return GpuBuffer(allocator_, rawBuffer, rawAllocation, desc.size, desc.usage, desc.requiredMemoryProperties,
                     actualMemoryProperties);
}

GpuImage VulkanContext::createImage(const ImageDesc& desc) const
{
    if (allocator_ == nullptr)
    {
        throw std::logic_error("cannot create image before allocator initialization");
    }

    if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0)
    {
        throw std::invalid_argument("image extent components must be greater than zero");
    }

    if (desc.mipLevels == 0)
    {
        throw std::invalid_argument("image mip level count must be greater than zero");
    }

    if (desc.arrayLayers == 0)
    {
        throw std::invalid_argument("image array layer count must greater than zero");
    }

    if (desc.format == VK_FORMAT_UNDEFINED)
    {
        throw std::invalid_argument("image format must be defined");
    }

    if (desc.usage == 0)
    {
        throw std::invalid_argument("image usage must not be empty");
    }

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(desc.extent.width);
    imageInfo.extent.height = static_cast<uint32_t>(desc.extent.height);
    imageInfo.extent.depth = desc.extent.depth;
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.format = desc.format;
    imageInfo.tiling = desc.tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = desc.usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = desc.samples;
    imageInfo.flags = desc.flags;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (desc.requiredMemoryProperties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    {
        allocInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
    if (desc.requiredMemoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    }

    VK_CHECK(vmaCreateImage(allocator_, &imageInfo, &allocInfo, &image, &allocation, nullptr));

    setDebugName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(image), desc.debugName);

    return GpuImage(allocator_, device_, image, allocation, desc.extent, desc.format, desc.usage, desc.mipLevels,
                    desc.arrayLayers, desc.samples);
}

VkImageView VulkanContext::createImageView(VkImage image, VkFormat format, uint32_t mipLevels,
                                           VkImageAspectFlags aspectFlags, VkImageViewType viewType,
                                           uint32_t layerCount, uint32_t baseArrayLayer) const
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = viewType;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = baseArrayLayer;
    viewInfo.subresourceRange.layerCount = layerCount;

    VkImageView imageView = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImageView(device_, &viewInfo, nullptr, &imageView));

    return imageView;
}

GpuSampler VulkanContext::createSampler(const SamplerDesc& desc) const
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = desc.magFilter;
    samplerInfo.minFilter = desc.minFilter;
    samplerInfo.addressModeU = desc.addressModeU;
    samplerInfo.addressModeV = desc.addressModeV;
    samplerInfo.addressModeW = desc.addressModeW;
    samplerInfo.anisotropyEnable = desc.anisotropyEnable;
    samplerInfo.maxAnisotropy = desc.maxAnisotropy;
    samplerInfo.borderColor = desc.borderColor;
    samplerInfo.unnormalizedCoordinates = desc.unnormalizedCoordinates;
    samplerInfo.compareEnable = desc.compareEnable;
    samplerInfo.compareOp = desc.compareOp;
    samplerInfo.mipmapMode = desc.mipmapMode;
    samplerInfo.mipLodBias = desc.mipLodBias;
    samplerInfo.minLod = desc.minLod;
    samplerInfo.maxLod = desc.maxLod;

    VkSampler sampler = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(device_, &samplerInfo, nullptr, &sampler));

    setDebugName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(sampler), desc.debugName);

    return GpuSampler(device_, sampler);
}

UniqueShaderModule VulkanContext::createShaderModule(const std::vector<char>& code) const
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    // SPIR-V的字节码大小(字节数)
    createInfo.codeSize = code.size();
    // 指向SPIR-V字节码的指针。SPIR-V字节码是一个uint32_t数组，所以需要把char*转换成uint32_t*。
    // 因为SPIR-V字节码的大小必须是4的倍数，所以code.size()一定是4的倍数，reinterpret_cast是安全的。
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule));
    return UniqueShaderModule(device_, shaderModule);
}
