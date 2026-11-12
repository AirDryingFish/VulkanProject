#include "render/Renderer.hpp"

#include <optional>

namespace
{
// 图像格式 -> 每个样本的理论字节数
std::optional<std::uint64_t> nominalBytesPerSample(VkFormat format) noexcept
{
    switch (format)
    {
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return 8;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_D32_SFLOAT:
        return 4;

    case VK_FORMAT_D16_UNORM:
        return 2;

    case VK_FORMAT_D24_UNORM_S8_UINT:
        return 4;

    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return 5;

    default:
        return std::nullopt;
    }
}

std::optional<std::uint64_t> estimateImagePayload(const GpuImage& image) noexcept
{
    if (!image)
    {
        return std::uint64_t{0};
    }

    const VkExtent3D extent = image.extent();

    // 限定当前渲染目标使用的形态
    if (image.mipLevels() != 1 || image.arrayLayers() == 0 || extent.depth != 1)
    {
        return std::nullopt;
    }

    const auto bytesPerSample = nominalBytesPerSample(image.format());

    if (!bytesPerSample)
    {
        return std::nullopt;
    }

    return static_cast<std::uint64_t>(extent.width) * extent.height * static_cast<std::uint64_t>(image.samples()) *
           static_cast<std::uint64_t>(image.arrayLayers()) * *bytesPerSample;
}

} // namespace

// 估算当前渲染目标的理论图像载荷，不把它当作分配器实际占用。
RenderTargetPayloadEstimate Renderer::renderTargetPayloadEstimate() const noexcept
{
    RenderTargetPayloadEstimate result{};

    if (!initialized_)
    {
        return result;
    }
    result.available = true;

    const auto addImage = [&](const GpuImage& image, std::uint64_t& destination) -> bool
    {
        const auto bytes = estimateImagePayload(image);
        if (!bytes)
        {
            return false;
        }
        destination += *bytes;
        return true;
    };

    for (const HdrFrameTarget& target : hdrTargets_)
    {
        if (!addImage(target.hdrColor, result.hdrColorBytes) || !addImage(target.msaaColor, result.msaaColorBytes) ||
            !addImage(target.depth, result.sceneDepthBytes))
        {
            return result;
        }
    }

    for (const ShadowTarget& target : shadowTargets_)
    {
        if (!addImage(target.depth, result.shadowDepthBytes))
        {
            return result;
        }
    }
    for (const GBufferTarget& target : gBufferTargets_)
    {
        for (const GpuImage& color : target.colors)
        {
            if (!addImage(color, result.gbufferBytes))
            {
                return result;
            }
        }
        if (!addImage(target.depth, result.gbufferBytes))
        {
            return result;
        }
    }

    for (const DeferredTarget& target : deferredTargets_)
    {
        if (!addImage(target.hdrColor, result.deferredHdrBytes))
        {
            return result;
        }
    }

    result.totalBytes = result.hdrColorBytes + result.msaaColorBytes + result.sceneDepthBytes +
                        result.shadowDepthBytes + result.gbufferBytes + result.deferredHdrBytes;
    result.complete = true;
    return result;
}
