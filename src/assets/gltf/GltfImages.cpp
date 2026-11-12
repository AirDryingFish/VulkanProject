#include "assets/gltf/GltfImportInternal.hpp"
#include "assets/image/ImageDecode.hpp"

#include <fastgltf/types.hpp>

#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace
{
struct ByteRange
{
    const std::byte* data = nullptr;
    std::size_t size = 0;
};

std::string imageContext(const std::filesystem::path& assetPath, std::size_t imageIndex, const fastgltf::Image& image)
{
    std::ostringstream message;
    message << assetPath.u8string() << ": image[" << imageIndex << "]";
    if (!image.name.empty())
    {
        message << " name=\"" << image.name << "\"";
    }
    return message.str();
}

[[noreturn]] void failImage(const std::filesystem::path& assetPath, std::size_t imageIndex,
                            const fastgltf::Image& image, const std::string& reason)
{
    throw std::runtime_error(imageContext(assetPath, imageIndex, image) + ": " + reason);
}

// 借用解析器已加载的数据；未加载来源与无效字节范围在此边界报告。
ByteRange loadedDataBytes(const fastgltf::DataSource& source, const std::filesystem::path& assetPath,
                          std::size_t imageIndex, const fastgltf::Image& image, const std::string& owner)
{
    ByteRange result{};
    if (const auto* array = std::get_if<fastgltf::sources::Array>(&source))
    {
        result = {array->bytes.data(), array->bytes.size()};
    }
    else if (const auto* vector = std::get_if<fastgltf::sources::Vector>(&source))
    {
        result = {vector->bytes.data(), vector->bytes.size()};
    }
    else if (const auto* view = std::get_if<fastgltf::sources::ByteView>(&source))
    {
        result = {view->bytes.data(), view->bytes.size()};
    }
    else if (const auto* uri = std::get_if<fastgltf::sources::URI>(&source))
    {
        failImage(assetPath, imageIndex, image, owner + " remains an unloaded URI: " + std::string(uri->uri.string()));
    }
    else if (std::holds_alternative<fastgltf::sources::BufferView>(source))
    {
        failImage(assetPath, imageIndex, image, owner + "unexpectedly refers to another bufferView");
    }
    else if (std::holds_alternative<fastgltf::sources::Fallback>(source))
    {
        failImage(assetPath, imageIndex, image, owner + "is a fallback source without bytes");
    }
    else
    {
        failImage(assetPath, imageIndex, image, owner + " has no data");
    }

    if (result.data == nullptr || result.size == 0)
    {
        failImage(assetPath, imageIndex, image, owner + " is empty");
    }
    return result;
}

// 从具体数据来源取得编码格式，未标注时交由解码器识别。
fastgltf::MimeType sourceMimeType(const fastgltf::DataSource& source)
{
    if (const auto* value = std::get_if<fastgltf::sources::Array>(&source))
    {
        return value->mimeType;
    }
    if (const auto* value = std::get_if<fastgltf::sources::Vector>(&source))
    {
        return value->mimeType;
    }
    if (const auto* value = std::get_if<fastgltf::sources::ByteView>(&source))
    {
        return value->mimeType;
    }
    if (const auto* value = std::get_if<fastgltf::sources::URI>(&source))
    {
        return value->mimeType;
    }
    if (const auto* value = std::get_if<fastgltf::sources::CustomBuffer>(&source))
    {
        return value->mimeType;
    }
    if (const auto* value = std::get_if<fastgltf::sources::BufferView>(&source))
    {
        return value->mimeType;
    }
    return fastgltf::MimeType::None;
}

// 检查这个 MIME 类型是不是当前 stb_image 加载流程允许处理的格式
void validateStbiMimeType(fastgltf::MimeType mimeType, const std::filesystem::path& assetPath, std::size_t imageIndex,
                          const fastgltf::Image& image)
{
    if (mimeType == fastgltf::MimeType::None || mimeType == fastgltf::MimeType::PNG ||
        mimeType == fastgltf::MimeType::JPEG)
    {
        return;
    }

    const std::string_view mimeName = fastgltf::getMimeTypeString(mimeType);
    failImage(assetPath, imageIndex, image,
              "unsupported encoded image MIME type " +
                  (mimeName.empty() ? std::string("<unkwown>") : std::string(mimeName)) +
                  "; only PNG and JPEG are supported");
}

// 解析独立图片或 bufferView 来源，验证字节范围后复制为 RGBA8 像素。
DecodedImageData decodeGltfImage(const fastgltf::Asset& asset, const std::filesystem::path& assetPath,
                                 std::size_t imageIndex)
{
    if (imageIndex >= asset.images.size())
    {
        throw std::logic_error(assetPath.u8string() + ": image index is out of range");
    }

    const fastgltf::Image& image = asset.images[imageIndex];
    const std::string debugName = imageContext(assetPath, imageIndex, image);

    ByteRange encoded{};
    const fastgltf::MimeType mimeType = sourceMimeType(image.data);
    // bufferView 的声明范围和实际已加载范围都须覆盖图片。
    if (const auto* imageView = std::get_if<fastgltf::sources::BufferView>(&image.data))
    {
        if (imageView->bufferViewIndex >= asset.bufferViews.size())
        {
            failImage(assetPath, imageIndex, image,
                      "bufferView index " + std::to_string(imageView->bufferViewIndex) + " is out of range");
        }
        const fastgltf::BufferView& bufferView = asset.bufferViews[imageView->bufferViewIndex];
        if (bufferView.bufferIndex >= asset.buffers.size())
        {
            failImage(assetPath, imageIndex, image,
                      "bufferView buffer index " + std::to_string(bufferView.bufferIndex) + " is out of range");
        }
        const fastgltf::Buffer& buffer = asset.buffers[bufferView.bufferIndex];
        const std::string owner = "buffer[" + std::to_string(bufferView.bufferIndex) + "]";

        const ByteRange bufferBytes = loadedDataBytes(buffer.data, assetPath, imageIndex, image, owner);
        if (buffer.byteLength > bufferBytes.size)
        {
            failImage(assetPath, imageIndex, image,
                      owner + " declares byteLength " + std::to_string(buffer.byteLength) + " but only " +
                          std::to_string(bufferBytes.size) + " bytes were loaded");
        }
        if (bufferView.byteLength == 0)
        {
            failImage(assetPath, imageIndex, image, "image bufferView is empty");
        }
        if (bufferView.byteOffset > buffer.byteLength ||
            bufferView.byteLength > buffer.byteLength - bufferView.byteOffset)
        {
            failImage(assetPath, imageIndex, image, "image bufferView range exceeds declared buffer byteLength");
        }

        if (bufferView.byteOffset > bufferBytes.size ||
            bufferView.byteLength > bufferBytes.size - bufferView.byteOffset)
        {
            failImage(assetPath, imageIndex, image, "image bufferView range exceeds loaded buffer bytes");
        }

        encoded = {bufferBytes.data + bufferView.byteOffset, bufferView.byteLength};
    }
    // image不是以 buffer 存储的，其他格式可以直接取到对应的 data 指针和 size
    else
    {
        encoded = loadedDataBytes(image.data, assetPath, imageIndex, image, "image source");
    }

    validateStbiMimeType(mimeType, assetPath, imageIndex, image);
    DecodedImageData decoded = decodeImage(encoded.data, encoded.size, debugName);
    decoded.name =
        image.name.empty() ? "image " + std::to_string(imageIndex) : std::string(image.name.data(), image.name.size());
    return decoded;
}
} // namespace

void gltf_import_detail::decodeImages(const fastgltf::Asset& asset, const std::filesystem::path& normalizedPath,
                                      GltfImportData& result)
{
    result.images.reserve(asset.images.size());
    for (std::size_t imageIndex = 0; imageIndex < asset.images.size(); ++imageIndex)
    {
        result.images.push_back(decodeGltfImage(asset, normalizedPath, imageIndex));
    }
}
