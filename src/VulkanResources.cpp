#include "VulkanResources.hpp"
#include <cassert>
#include <utility>


GpuBuffer::GpuBuffer(
    VmaAllocator allocator, 
    VkBuffer buffer, 
    VmaAllocation allocation,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags requiredMemoryProperties
) noexcept: 
    allocator_(allocator),
    buffer_(buffer),
    allocation_(allocation),
    size_(size),
    usage_(usage),
    requiredMemoryProperties_(requiredMemoryProperties)
{
}

GpuBuffer::~GpuBuffer() noexcept
{
    reset();
}

GpuBuffer::GpuBuffer(GpuBuffer &&other) noexcept
{
    moveFrom(other);
}

GpuBuffer &GpuBuffer::operator=(GpuBuffer &&other) noexcept
{
    if (this != &other)
    {
        reset();
        moveFrom(other);
    }
    return *this;
}

GpuBuffer::operator bool() const noexcept
{
    return buffer_ != VK_NULL_HANDLE;
}

VkBuffer GpuBuffer::get() const noexcept
{
    return buffer_;
}

VkResult GpuBuffer::map(void **data) noexcept
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

void GpuBuffer::unmap() noexcept
{
    if (mappedData_ == nullptr)
    {
        return;
    }
    vmaUnmapMemory(allocator_, allocation_);
    mappedData_ = nullptr;
}

void GpuBuffer::reset() noexcept
{
    if (buffer_ != VK_NULL_HANDLE)
    {
        assert(allocator_ != nullptr);
        unmap();
        vmaDestroyBuffer(allocator_, buffer_, allocation_);
    }
    allocator_ = nullptr;
    buffer_ = VK_NULL_HANDLE;
    allocation_ = nullptr;
    mappedData_ = nullptr;
    size_ = 0;
    usage_ = 0;
    requiredMemoryProperties_ = 0;
}

VkDeviceSize GpuBuffer::size() const noexcept
{
    return size_;
}

VkBufferUsageFlags GpuBuffer::usage() const noexcept
{
    return usage_;
}

VkMemoryPropertyFlags GpuBuffer::requiredMemoryProperties() const noexcept
{
    return requiredMemoryProperties_;
}

void GpuBuffer::moveFrom(GpuBuffer &other) noexcept
{
    allocator_ = std::exchange(other.allocator_, nullptr);
    allocation_ = std::exchange(other.allocation_, nullptr);
    buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
    mappedData_ = std::exchange(other.mappedData_, nullptr);
    size_ = std::exchange(other.size_, 0);
    usage_ = std::exchange(other.usage_, 0);
    requiredMemoryProperties_ = std::exchange(other.requiredMemoryProperties_, 0);
}

GpuImage::GpuImage(VmaAllocator allocator, VkDevice device, VkImage image, VmaAllocation allocation, VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, uint32_t mipLevels, uint32_t arrayLayers, VkSampleCountFlagBits samples) noexcept:
    allocator_(allocator),
    device_(device),
    image_(image),
    allocation_(allocation),
    extent_(extent),
    format_(format),
    usage_(usage),
    mipLevels_(mipLevels),
    arrayLayers_(arrayLayers),
    samples_(samples)
{
}

GpuImage::~GpuImage() noexcept
{
    reset();
}

GpuImage::GpuImage(GpuImage &&other) noexcept
{
    moveFrom(other);
}

GpuImage &GpuImage::operator=(GpuImage &&other) noexcept
{
    if (this != &other)
    {
        reset();
        moveFrom(other);
    }
    return *this;
}

GpuImage::operator bool() const noexcept
{
    return image_ != VK_NULL_HANDLE;
}

VkImage GpuImage::get() const noexcept
{
    return image_;
}

VkImageView GpuImage::view() const noexcept
{
    return imageView_;
}

VkExtent3D GpuImage::extent() const noexcept
{
    return extent_;
}

VkFormat GpuImage::format() const noexcept
{
    return format_;
}

VkImageUsageFlags GpuImage::usage() const noexcept
{
    return usage_;
}

uint32_t GpuImage::mipLevels() const noexcept
{
    return mipLevels_;
}

uint32_t GpuImage::arrayLayers() const noexcept
{
    return arrayLayers_;
}

VkSampleCountFlagBits GpuImage::samples() const noexcept
{
    return samples_;
}

void GpuImage::setView(VkImageView imageView) noexcept
{
    assert(image_ != VK_NULL_HANDLE);
    assert(device_ != VK_NULL_HANDLE);
    assert(imageView != VK_NULL_HANDLE);
    if (imageView_ != VK_NULL_HANDLE)
    {
        vkDestroyImageView(
            device_, imageView_, nullptr
        );
    }
    imageView_ = imageView;
}

void GpuImage::reset() noexcept
{
    if (imageView_ != VK_NULL_HANDLE)
    {
        assert(device_ != VK_NULL_HANDLE);
        vkDestroyImageView(device_, imageView_, nullptr);
    }
    if (image_ != VK_NULL_HANDLE)
    {
        assert(allocator_ != nullptr);
        vmaDestroyImage(allocator_, image_, allocation_);
    }
    allocator_ = nullptr;
    imageView_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
    allocation_ = nullptr;
    device_ = VK_NULL_HANDLE;

    extent_ = {0, 0, 0};
    format_ = VK_FORMAT_UNDEFINED;
    usage_ = 0;
    mipLevels_ = 0;
    arrayLayers_ = 0;
    samples_ = VK_SAMPLE_COUNT_1_BIT;
}

void GpuImage::moveFrom(GpuImage &other) noexcept
{
    allocator_ = std::exchange(other.allocator_, nullptr);
    allocation_ = std::exchange(other.allocation_, nullptr);
    device_ = std::exchange(other.device_, VK_NULL_HANDLE);
    image_ = std::exchange(other.image_, VK_NULL_HANDLE);
    imageView_ = std::exchange(other.imageView_, VK_NULL_HANDLE);
    extent_ = std::exchange(other.extent_, VkExtent3D{0, 0, 0});
    format_ = std::exchange(other.format_, VK_FORMAT_UNDEFINED);
    usage_ = std::exchange(other.usage_, 0);
    mipLevels_ = std::exchange(other.mipLevels_, 0);
    arrayLayers_ = std::exchange(other.arrayLayers_, 0);
    samples_ = std::exchange(other.samples_, VK_SAMPLE_COUNT_1_BIT);

}

UniqueShaderModule::UniqueShaderModule(
    VkDevice device,
    VkShaderModule shaderModule) noexcept
    : device_(device),
      shaderModule_(shaderModule)
{
}

UniqueShaderModule::~UniqueShaderModule() noexcept
{
    reset();
}

UniqueShaderModule::UniqueShaderModule(UniqueShaderModule&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      shaderModule_(std::exchange(other.shaderModule_, VK_NULL_HANDLE))
{
}

UniqueShaderModule& UniqueShaderModule::operator=(UniqueShaderModule&& other) noexcept
{
    if (this != &other)
    {
        reset();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        shaderModule_ = std::exchange(other.shaderModule_, VK_NULL_HANDLE);
    }

    return *this;
}

UniqueShaderModule::operator bool() const noexcept
{
    return shaderModule_ != VK_NULL_HANDLE;
}

VkShaderModule UniqueShaderModule::get() const noexcept
{
    return shaderModule_;
}

void UniqueShaderModule::reset() noexcept
{
    if (shaderModule_ != VK_NULL_HANDLE)
    {
        assert(device_ != VK_NULL_HANDLE);
        vkDestroyShaderModule(device_, shaderModule_, nullptr);
        shaderModule_ = VK_NULL_HANDLE;
    }

    device_ = VK_NULL_HANDLE;
}
