#pragma once
#include "VulkanHeaders.hpp"
#include <vk_mem_alloc.h>

class AllocatedBuffer
{
private:
    VmaAllocator allocator_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;
    void* mappedData_ = nullptr;

    void moveFrom(AllocatedBuffer& other) noexcept;

public:
    AllocatedBuffer() noexcept = default;

    AllocatedBuffer(
        VmaAllocator allocator,
        VkBuffer buffer,
        VmaAllocation allocation
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

};

class AllocatedImage
{
private:
    VkImage image_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = nullptr;;
    VkImageView imageView_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;
    uint32_t mipLevels_ = 1;

    void moveFrom(AllocatedImage& other) noexcept;
public:
    AllocatedImage() noexcept = default;
    AllocatedImage(
        VmaAllocator allocator,
        VkDevice device,
        VkImage image,
        VmaAllocation allocation,
        uint32_t mipLevels
    ) noexcept;

    ~AllocatedImage() noexcept;

    AllocatedImage(const AllocatedImage&) = delete;
    AllocatedImage& operator=(const AllocatedImage&) = delete;
    AllocatedImage(AllocatedImage&& other) noexcept;
    AllocatedImage& operator=(AllocatedImage&& other) noexcept;

    explicit operator bool() const noexcept;
    VkImage get() const noexcept;
    VkImageView view() const noexcept;
    uint32_t mipLevels() const noexcept;

    void setView(VkImageView imageView) noexcept;
    void reset() noexcept;
};