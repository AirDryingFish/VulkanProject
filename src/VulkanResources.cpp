#include "VulkanResources.hpp"
#include <cassert>
#include <utility>


AllocatedBuffer::AllocatedBuffer(VmaAllocator allocator, VkBuffer buffer, VmaAllocation allocation) noexcept: 
    allocator_(allocator),
    buffer_(buffer),
    allocation_(allocation) 
{
}

AllocatedBuffer::~AllocatedBuffer() noexcept
{
    reset();
}

AllocatedBuffer::AllocatedBuffer(AllocatedBuffer &&other) noexcept
{
    moveFrom(other);
}

AllocatedBuffer &AllocatedBuffer::operator=(AllocatedBuffer &&other) noexcept
{
    if (this != &other)
    {
        reset();
        moveFrom(other);
    }
    return *this;
}

AllocatedBuffer::operator bool() const noexcept
{
    return buffer_ != VK_NULL_HANDLE;
}

VkBuffer AllocatedBuffer::get() const noexcept
{
    return buffer_;
}

VkResult AllocatedBuffer::map(void **data) noexcept
{
    if (mappedData_ != nullptr)
    {
        *data = mappedData_;
        return VK_SUCCESS;
    }
    const VkResult result = vmaMapMemory(allocator_, allocation_, data);
    if (result == VK_SUCCESS)
    {
        mappedData_ = *data;
    }
    return result;
}

void AllocatedBuffer::unmap() noexcept
{
    if (mappedData_ == nullptr)
    {
        return;
    }
    vmaUnmapMemory(allocator_, allocation_);
    mappedData_ = nullptr;
}

void AllocatedBuffer::reset() noexcept
{
    if (buffer_ == VK_NULL_HANDLE)
    {
        return;
    }
    unmap();
    vmaDestroyBuffer(allocator_, buffer_, allocation_);
    allocator_ = nullptr;
    buffer_ = VK_NULL_HANDLE;
    allocation_ = nullptr;
}


void AllocatedBuffer::moveFrom(AllocatedBuffer &other) noexcept
{
    allocator_ = std::exchange(other.allocator_, nullptr);
    allocation_ = std::exchange(other.allocation_, nullptr);
    buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
    mappedData_ = std::exchange(other.mappedData_, nullptr);
}

AllocatedImage::AllocatedImage(VmaAllocator allocator, VkDevice device, VkImage image, VmaAllocation allocation, uint32_t mipLevels) noexcept:
    allocator_(allocator),
    device_(device),
    image_(image),
    allocation_(allocation),
    mipLevels_(mipLevels)
{
}

AllocatedImage::~AllocatedImage() noexcept
{
    reset();
}

AllocatedImage::AllocatedImage(AllocatedImage &&other) noexcept
{
    moveFrom(other);
}

AllocatedImage &AllocatedImage::operator=(AllocatedImage &&other) noexcept
{
    if (this != &other)
    {
        reset();
        moveFrom(other);
    }
    return *this;
}

AllocatedImage::operator bool() const noexcept
{
    return image_ != VK_NULL_HANDLE;
}

VkImage AllocatedImage::get() const noexcept
{
    return image_;
}

VkImageView AllocatedImage::view() const noexcept
{
    return imageView_;
}

uint32_t AllocatedImage::mipLevels() const noexcept
{
    return mipLevels_;
}

void AllocatedImage::setView(VkImageView imageView) noexcept
{
    assert(image_ != VK_NULL_HANDLE);
    assert(imageView != VK_NULL_HANDLE);
    if (imageView_ != VK_NULL_HANDLE)
    {
        vkDestroyImageView(
            device_, imageView_, nullptr
        );
    }
    imageView_ = imageView;
}

void AllocatedImage::reset() noexcept
{
    if (imageView_ != VK_NULL_HANDLE)
    {
        assert(device_ != VK_NULL_HANDLE);
        vkDestroyImageView(device_, imageView_, nullptr);
        imageView_ = VK_NULL_HANDLE;
    }
    if (image_ != VK_NULL_HANDLE)
    {
        assert(allocator_ != nullptr);
        vmaDestroyImage(allocator_, image_, allocation_);
        image_ = VK_NULL_HANDLE;
        allocation_ = nullptr;
    }
    allocator_ = nullptr;
    device_ = VK_NULL_HANDLE;
    mipLevels_ = 1;
}

void AllocatedImage::moveFrom(AllocatedImage &other) noexcept
{
    allocator_ = std::exchange(other.allocator_, nullptr);
    allocation_ = std::exchange(other.allocation_, nullptr);
    device_ = std::exchange(other.device_, VK_NULL_HANDLE);
    image_ = std::exchange(other.image_, VK_NULL_HANDLE);
    imageView_ = std::exchange(other.imageView_, VK_NULL_HANDLE);
    mipLevels_ = std::exchange(other.mipLevels_, 1);
}