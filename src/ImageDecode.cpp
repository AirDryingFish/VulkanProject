#include "ImageDecode.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

// 把一段已经编码好的 PNG/JPEG 二进制数据，交给stb_image 解码成RGBA8 像素，并包装成 DecodedImageData 返回
DecodedImageData decodeImage(const std::byte *bytes, std::size_t byteCount, const std::string &debugName)
{
    if (bytes == nullptr || byteCount == 0)
    {
        throw std::invalid_argument(debugName + ": encoded image data is empty");
    }

    if (byteCount > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        throw std::length_error(debugName + ": encoded image is too large for stb_image");
    }

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(
            reinterpret_cast<const stbi_uc *>(bytes),
            static_cast<int>(byteCount),
            &width,
            &height,
            &sourceChannels,
            STBI_rgb_alpha
        ), &stbi_image_free
    );
    if (!pixels)
    {
        std::string message = debugName + ": image decode failed";
        if (const char* reason = stbi_failure_reason();
            reason != nullptr)
        {
            message += " (";
            message += reason;
            message += ")";
        }
        throw std::runtime_error(message);
    }
    if (width <= 0 || height <= 0)
    {
        throw std::runtime_error(debugName + ": decoded image has invalid dimensions");
    }
    const std::size_t decodedWidth = static_cast<std::size_t>(width);
    const std::size_t decodedHeight = static_cast<std::size_t>(height);

    if (decodedWidth > std::numeric_limits<std::size_t>::max() / decodedHeight / 4u)
    {
        throw std::length_error(debugName + ": decoded RGBA8 image size overflows size_t");
    }

    const std::size_t decodedByteCount = decodedWidth * decodedHeight * 4u;
    DecodedImageData result{};
    result.name = debugName;
    result.width = width;
    result.height = height;
    result.rgba8.assign(pixels.get(), pixels.get() + decodedByteCount);
    return result;
}
