#include "render/resources/TextureUpload.hpp"
#include "render/Renderer.hpp"
#include "vulkan/upload/UploadCommands.hpp"
#include "vulkan/context/VulkanContext.hpp"
#include "vulkan/VulkanCheck.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

GpuImage uploadTexture2D(VulkanContext& context, Renderer& renderer, const DecodedImageData& decoded, VkFormat format,
                         const std::string& debugName)
{
    if (decoded.width <= 0 || decoded.height <= 0)
    {
        throw std::invalid_argument(debugName + ": image dimensions must be positive");
    }
    const int texWidth = decoded.width;
    const int texHeight = decoded.height;
    const std::size_t expectedByteCount = static_cast<std::size_t>(texWidth) * static_cast<std::size_t>(texHeight) * 4u;
    if (decoded.rgba8.size() != expectedByteCount)
    {
        throw std::invalid_argument(debugName + ": RGBA8 byte count does not match image dimensions");
    }

    std::string path = decoded.name;
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(expectedByteCount);
    const uint32_t imageMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

    const std::string stagingDebugName = debugName + " staging buffer";
    BufferDesc stagingDesc{};
    stagingDesc.size = imageSize;
    stagingDesc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    stagingDesc.debugName = stagingDebugName.c_str();

    GpuBuffer stagingBuffer = context.createBuffer(stagingDesc);

    void* data = nullptr;
    VK_CHECK(stagingBuffer.map(&data));
    memcpy(data, decoded.rgba8.data(), expectedByteCount);
    VK_CHECK(stagingBuffer.flush(0, imageSize));
    stagingBuffer.unmap();

    const std::string imageDebugName = debugName + " image";
    ImageDesc imageDesc{};
    imageDesc.extent = {static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1};
    imageDesc.mipLevels = imageMipLevels;
    imageDesc.arrayLayers = 1;
    imageDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    imageDesc.format = format;
    imageDesc.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageDesc.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    imageDesc.flags = 0;
    imageDesc.debugName = imageDebugName.c_str();

    GpuImage image = context.createImage(imageDesc);

    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(context.physicalDevice(), image.format(), &formatProperties);

    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        throw std::runtime_error("texture image format does not support linear blitting");
    }

    const VkExtent3D imageExtent = image.extent();

    const bool useSync2 = context.capabilities().synchronization2Enabled;
    const auto pipelineBarrier2 = useSync2 ? context.deviceFunctions().cmdPipelineBarrier2 : nullptr;

    // 连续录制
    renderer.immediateSubmit(
        [&](VkCommandBuffer commandBuffer)
        {
            if (useSync2)
            {
                upload::recordPrepareImageForUpload(commandBuffer, image.get(), image.mipLevels(), image.arrayLayers(),
                                                    pipelineBarrier2);
            }
            else
            {
                upload::recordImageTransition(commandBuffer, image.get(), image.format(), VK_IMAGE_LAYOUT_UNDEFINED,
                                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, image.mipLevels(),
                                              image.arrayLayers());
            }

            upload::recordBufferToImageCopy(commandBuffer, stagingBuffer.get(), image.get(), imageExtent,
                                            image.arrayLayers());
            upload::recordGenerateMipmaps(commandBuffer, image.get(), VkExtent2D{imageExtent.width, imageExtent.height},
                                          image.mipLevels(), image.arrayLayers(), pipelineBarrier2);
        });

    image.setView(context.createImageView(image.get(), image.format(), image.mipLevels()));

    return image;
}
