#pragma once
#include "VulkanHeaders.hpp"

#include <cstdint>
#include <vk_mem_alloc.h>

struct BufferDesc
{
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    VkMemoryPropertyFlags requiredMemoryProperties = 0;
    const char* debugName = nullptr;
};

struct ImageDesc
{
    VkExtent3D extent{0, 0, 1};
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    VkImageUsageFlags usage = 0;
    VkMemoryPropertyFlags requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkImageCreateFlags flags = 0;
    const char* debugName = nullptr;
};

struct SamplerDesc
{
    VkFilter magFilter = VK_FILTER_LINEAR;
    VkFilter minFilter = VK_FILTER_LINEAR;
    VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkBool32 anisotropyEnable = VK_FALSE;
    float maxAnisotropy = 1.0f;
    VkBorderColor borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    VkBool32 unnormalizedCoordinates = VK_FALSE;
    VkBool32 compareEnable = VK_FALSE;
    VkCompareOp compareOp = VK_COMPARE_OP_ALWAYS;
    VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    float mipLodBias = 0.0f;
    float minLod = 0.0f;
    float maxLod = 0.0f;
    const char* debugName = nullptr;
};

class GpuBuffer
{
private:
    VmaAllocator allocator_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;
    void* mappedData_ = nullptr;

    VkDeviceSize size_ = 0;
    VkBufferUsageFlags usage_ = 0;
    VkMemoryPropertyFlags requiredMemoryProperties_ = 0;

    void moveFrom(GpuBuffer& other) noexcept;

public:
    GpuBuffer() noexcept = default;

    GpuBuffer(
        VmaAllocator allocator,
        VkBuffer buffer,
        VmaAllocation allocation,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags requiredMemoryProperties
    ) noexcept;

    ~GpuBuffer() noexcept;

    GpuBuffer(const GpuBuffer&) = delete; // 拷贝构造
    GpuBuffer& operator=(const GpuBuffer&) = delete; // 拷贝赋值
    GpuBuffer(GpuBuffer&& other) noexcept;
    GpuBuffer& operator=(GpuBuffer&& other) noexcept;

    explicit operator bool() const noexcept; // explicit 避免对象被当成 bool 运算

    VkBuffer get() const noexcept;
    VkResult map(void** data) noexcept;
    void unmap() noexcept;
    void reset() noexcept;

    VkDeviceSize size() const noexcept;
    VkBufferUsageFlags usage() const noexcept;
    VkMemoryPropertyFlags requiredMemoryProperties() const noexcept;

};

class GpuImage
{
private:
    VmaAllocator allocator_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkImageView imageView_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;

    VkExtent3D extent_{0, 0, 0};
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags usage_ = 0;
    uint32_t mipLevels_ = 0;
    uint32_t arrayLayers_ = 0;
    VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;

    void moveFrom(GpuImage& other) noexcept;

public:
    GpuImage() noexcept = default;
    GpuImage(
        VmaAllocator allocator,
        VkDevice device,
        VkImage image,
        VmaAllocation allocation,
        VkExtent3D extent,
        VkFormat format,
        VkImageUsageFlags usage,
        uint32_t mipLevels,
        uint32_t arrayLayers,
        VkSampleCountFlagBits samples
    ) noexcept;

    ~GpuImage() noexcept;

    GpuImage(const GpuImage&) = delete;
    GpuImage& operator=(const GpuImage&) = delete;
    GpuImage(GpuImage&& other) noexcept;
    GpuImage& operator=(GpuImage&& other) noexcept;

    explicit operator bool() const noexcept;
    VkImage get() const noexcept;
    VkImageView view() const noexcept;
    VkExtent3D extent() const noexcept;
    VkFormat format() const noexcept;
    VkImageUsageFlags usage() const noexcept;
    uint32_t mipLevels() const noexcept;
    uint32_t arrayLayers() const noexcept;
    VkSampleCountFlagBits samples() const noexcept;

    void setView(VkImageView imageView) noexcept;
    void reset() noexcept;
};

class GpuSampler
{
private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
public:
    GpuSampler() noexcept = default;
    GpuSampler(VkDevice device, VkSampler sampler) noexcept;
    ~GpuSampler() noexcept;

    GpuSampler(const GpuSampler&) = delete;
    GpuSampler& operator=(const GpuSampler&) = delete;
    GpuSampler(GpuSampler&& other) noexcept;
    GpuSampler& operator=(GpuSampler&& other) noexcept;

    explicit operator bool() const noexcept;
    VkSampler get() const noexcept;
    void reset() noexcept;
};

class UniqueShaderModule
{
private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkShaderModule shaderModule_ = VK_NULL_HANDLE;

public:
    UniqueShaderModule() noexcept = default;
    UniqueShaderModule(VkDevice device, VkShaderModule shaderModule) noexcept;
    ~UniqueShaderModule() noexcept;

    UniqueShaderModule(const UniqueShaderModule&) = delete;
    UniqueShaderModule& operator=(const UniqueShaderModule&) = delete;
    UniqueShaderModule(UniqueShaderModule&& other) noexcept;
    UniqueShaderModule& operator=(UniqueShaderModule&& other) noexcept;

    explicit operator bool() const noexcept;
    VkShaderModule get() const noexcept;

    void reset() noexcept;
};
