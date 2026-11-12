#pragma once

#include "assets/image/ImageData.hpp"
#include "vulkan/resources/VulkanResources.hpp"

#include <string>

class VulkanContext;
class Renderer;

// 同步上传 RGBA8 并生成 mip；成功返回时图像可被 shader 采样，所有权交给调用方。
GpuImage uploadTexture2D(VulkanContext& context, Renderer& renderer, const DecodedImageData& decoded, VkFormat format,
                         const std::string& debugName);
