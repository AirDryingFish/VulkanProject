#pragma once
#include "VulkanHeaders.hpp"

#include <cstdint>
#include <vk_mem_alloc.h>

class AllocatedBuffer
{
private:
    VmaAllocator allocator_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;
    void* mappedData_ = nullptr;

    VkDeviceSize size_ = 0;
    VkBufferUsageFlags usage_ = 0;
    VkMemoryPropertyFlags requiredMemoryProperties_ = 0;

    void moveFrom(AllocatedBuffer& other) noexcept;

public:
    AllocatedBuffer() noexcept = default;

    AllocatedBuffer(
        VmaAllocator allocator,
        VkBuffer buffer,
        VmaAllocation allocation,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags requiredMemoryProperties
    ) noexcept;

    ~AllocatedBuffer() noexcept;

    AllocatedBuffer(const AllocatedBuffer&) = delete; // 拷贝构造
    AllocatedBuffer& operator=(const AllocatedBuffer&) = delete; // 拷贝赋值
    AllocatedBuffer(AllocatedBuffer&& other) noexcept;
    AllocatedBuffer& operator=(AllocatedBuffer&& other) noexcept;

    explicit operator bool() const noexcept; // explicit 避免对象被当成 bool 运算

    VkBuffer get() const noexcept;
    VkResult map(void** data) noexcept;
    void unmap() noexcept;
    void reset() noexcept;

    VkDeviceSize size() const noexcept;
    VkBufferUsageFlags usage() const noexcept;
    VkMemoryPropertyFlags requiredMemoryProperties() const noexcept;

};

class AllocatedImage
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

    void moveFrom(AllocatedImage& other) noexcept;

public:
    AllocatedImage() noexcept = default;
    AllocatedImage(
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

    ~AllocatedImage() noexcept;

    AllocatedImage(const AllocatedImage&) = delete;
    AllocatedImage& operator=(const AllocatedImage&) = delete;
    AllocatedImage(AllocatedImage&& other) noexcept;
    AllocatedImage& operator=(AllocatedImage&& other) noexcept;

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
