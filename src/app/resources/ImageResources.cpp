#include "app/Application.hpp"
#include "assets/image/ImageDecode.hpp"
#include "render/resources/TextureUpload.hpp"

#include <memory>
#include <stdexcept>

ImageHandle Application::getOrCreateGltfImage(const GltfImportData& imported, std::size_t imageIndex, VkFormat format,
                                              GltfMaterialUpload& upload)
{
    GltfImageCache& cache = upload.imageCache;
    const std::string assetPath = imported.sourcePath.generic_u8string();
    const std::string debugName = assetPath + ": image[" + std::to_string(imageIndex) + "]";
    if (imageIndex >= imported.images.size())
    {
        throw std::runtime_error(debugName + ": image index is out of range");
    }
    if (format != VK_FORMAT_R8G8B8A8_SRGB && format != VK_FORMAT_R8G8B8A8_UNORM)
    {
        throw std::invalid_argument(debugName + ": unsupported RGBA8 upload format");
    }

    const GltfImageKey key{assetPath, imageIndex, format};
    const auto cached = cache.find(key);
    if (cached != cache.end())
    {
        if (ImageHandle gpuImage = cached->second.lock())
        {
            return gpuImage;
        }
    }

    const DecodedImageData& decoded = imported.images[imageIndex];
    ImageHandle gpuImage = std::make_shared<ImageResource>();
    gpuImage->name = debugName + (format == VK_FORMAT_R8G8B8A8_SRGB ? " SRGB" : " UNORM");
    gpuImage->image = uploadTexture2D(context, renderer, decoded, format, gpuImage->name);
    cache.insert_or_assign(key, std::weak_ptr<ImageResource>{gpuImage});
    ++upload.uploadedImageCount;
    return gpuImage;
}

ImageHandle Application::createImageResource(const std::string& name, const std::string& path, VkFormat format,
                                             const std::array<unsigned char, 4>& fallbackPixels)
{
    const DecodedImageData decoded = decodeTextureImageFromFileOrFallback(path, fallbackPixels);

    ImageHandle texture = std::make_shared<ImageResource>();
    texture->name = name;
    texture->image = uploadTexture2D(context, renderer, decoded, format, name);

    textureLibrary.push_back(texture);

    return texture;
}

// 把解析出来的 gltf 材质描述，转换成 engine 绘制时使用的材质对象
// imported: cpu中的图像
// source: cpu侧的材质对象
