#include "render/environment/Environment.hpp"
#include "assets/image/SkyboxImage.hpp"
#include "render/Renderer.hpp"
#include "vulkan/context/VulkanContext.hpp"
#include "vulkan/VulkanCheck.hpp"
#include "core/AppConfig.hpp"
#include "vulkan/upload/UploadCommands.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace
{
constexpr VkFormat skyboxFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
}

// 上传六面 CPU 像素，在同一次同步提交中生成 mip 并转为采样布局。
void Environment::createSkyboxImage()
{
    SkyboxPixels pixels = loadSkyboxImage(SKYBOX_HDR_PATH, SKYBOX_FACE_PATHS);
    const uint32_t skyboxMipLevels =
        static_cast<uint32_t>(std::floor(std::log2(std::max(pixels.width, pixels.height)))) + 1;
    std::string stagingDebugName = SKYBOX_HDR_PATH + " staging buffer";
    BufferDesc stagingDesc{};
    stagingDesc.size = pixels.imageSize();
    stagingDesc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    stagingDesc.debugName = stagingDebugName.c_str();

    GpuBuffer stagingBuffer = context_->createBuffer(stagingDesc);
    void* data = nullptr;
    VK_CHECK(stagingBuffer.map(&data));
    std::memcpy(data, pixels.pixels.data(), static_cast<size_t>(pixels.imageSize()));
    VK_CHECK(stagingBuffer.flush(0, pixels.imageSize()));
    stagingBuffer.unmap();

    const std::string imageDebugName = SKYBOX_HDR_PATH + " image";
    ImageDesc skyboxDecs{};
    skyboxDecs.extent = {pixels.width, pixels.height, 1};
    skyboxDecs.mipLevels = skyboxMipLevels;
    skyboxDecs.arrayLayers = 6;
    skyboxDecs.samples = VK_SAMPLE_COUNT_1_BIT;
    skyboxDecs.format = skyboxFormat;
    skyboxDecs.tiling = VK_IMAGE_TILING_OPTIMAL;
    skyboxDecs.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    skyboxDecs.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    skyboxDecs.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    skyboxDecs.debugName = imageDebugName.c_str();

    skyboxImage_ = context_->createImage(skyboxDecs);

    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(context_->physicalDevice(), skyboxImage_.format(), &formatProperties);

    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        throw std::runtime_error("skybox image format does not support linear blitting");
    }

    const VkExtent3D skyboxExtent = skyboxImage_.extent();

    const bool useSync2 = context_->capabilities().synchronization2Enabled;
    const auto pipelineBarrier2 = useSync2 ? context_->deviceFunctions().cmdPipelineBarrier2 : nullptr;

    renderer_->immediateSubmit(
        [&](VkCommandBuffer commandBuffer)
        {
            if (useSync2)
            {
                upload::recordPrepareImageForUpload(commandBuffer, skyboxImage_.get(), skyboxImage_.mipLevels(),
                                                    skyboxImage_.arrayLayers(), pipelineBarrier2);
            }
            else
            {
                // 1. 6 个 face 的所有 mip 都先作为复制目标
                upload::recordImageTransition(commandBuffer, skyboxImage_.get(), skyboxImage_.format(),
                                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                              skyboxImage_.mipLevels(), skyboxImage_.arrayLayers());
            }

            // 2. 将 staging buffer 中的六张图片分别复制到 cubemap 的六个 array layer 的 mip 0
            std::array<VkBufferImageCopy, 6> regions{};
            for (uint32_t i = 0; i < regions.size(); i++)
            {
                regions[i].bufferOffset = pixels.layerSize() * i;
                regions[i].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                regions[i].imageSubresource.mipLevel = 0;
                regions[i].imageSubresource.baseArrayLayer = i;
                regions[i].imageSubresource.layerCount = 1;
                regions[i].imageExtent = {pixels.width, pixels.height, 1};
            }

            vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.get(), skyboxImage_.get(),
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(regions.size()),
                                   regions.data());

            // 3. 同时为 6 个 face 逐级生成 mipmap，最终将所有 face/mip 转成 shader read only
            upload::recordGenerateMipmaps(commandBuffer, skyboxImage_.get(),
                                          VkExtent2D{skyboxExtent.width, skyboxExtent.height}, skyboxImage_.mipLevels(),
                                          skyboxImage_.arrayLayers(), pipelineBarrier2);
        });

    skyboxImage_.setView(context_->createImageView(skyboxImage_.get(), skyboxFormat, skyboxMipLevels,
                                                   VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_CUBE, 6));
}

void Environment::createSkyboxSampler()
{
    SamplerDesc samplerDesc{};
    samplerDesc.magFilter = VK_FILTER_LINEAR;
    samplerDesc.minFilter = VK_FILTER_LINEAR;
    samplerDesc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.anisotropyEnable = VK_TRUE;
    samplerDesc.maxAnisotropy = 16.0f;
    samplerDesc.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerDesc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerDesc.minLod = 0.0f;
    samplerDesc.maxLod = static_cast<float>(skyboxImage_.mipLevels() - 1);
    samplerDesc.debugName = "skybox sampler";

    skyboxSampler_ = context_->createSampler(samplerDesc);
}
