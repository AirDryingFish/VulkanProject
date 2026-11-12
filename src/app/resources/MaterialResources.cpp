#include "app/Application.hpp"
#include "assets/image/ImageDecode.hpp"
#include "render/resources/TextureUpload.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace
{
VkSamplerAddressMode toSamplerAddressMode(GltfWrap wrap, const std::string& debugName)
{
    switch (wrap)
    {
    case GltfWrap::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case GltfWrap::MirroredRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case GltfWrap::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    throw std::runtime_error(debugName + ": unsupported sampler wrap mode");
}
} // namespace

void Application::createTextureSampler()
{
    defaultTextureSampler = getOrCreateGltfSampler(GltfSamplerData{}, "Default texture sampler", samplerLibrary);
}

void Application::createMaterialResources()
{
    const ImageHandle rustedBaseColorTexture =
        createImageResource("Rusted Iron Base Color", PBR_ALBEDO_PATH, VK_FORMAT_R8G8B8A8_SRGB, {255, 255, 255, 255});

    const ImageHandle rustedNormalTexture =
        createImageResource("Rusted Iron Normal", PBR_NORMAL_PATH, VK_FORMAT_R8G8B8A8_UNORM, {128, 128, 255, 255});

    // 将分开的metallic和roughness贴图打包到一张贴图中
    const DecodedImageData metallic = decodeTextureImageFromFileOrFallback(PBR_METALLIC_PATH, {0, 0, 0, 255});
    const DecodedImageData roughness = decodeTextureImageFromFileOrFallback(PBR_ROUGHNESS_PATH, {255, 255, 255, 255});
    const bool metallicConstant = metallic.width == 1 && metallic.height == 1;
    const bool roughnessConstant = roughness.width == 1 && roughness.height == 1;
    if (!metallicConstant && !roughnessConstant &&
        (metallic.width != roughness.width || metallic.height != roughness.height))
    {
        throw std::runtime_error("Rusted Iron: metallic and roughness dimensions differ");
    }
    DecodedImageData packed{};
    packed.name = "Rusted Iron Metallic-Roughness";
    packed.width = metallicConstant ? roughness.width : metallic.width;
    packed.height = metallicConstant ? roughness.height : metallic.height;
    const std::size_t pixelCount = static_cast<std::size_t>(packed.width) * static_cast<std::size_t>(packed.height);
    packed.rgba8.resize(pixelCount * 4u);
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
    {
        const std::size_t destination = pixel * 4u;
        const std::size_t metallicOffset = metallicConstant ? 0 : destination;
        const std::size_t roughnessOffset = roughnessConstant ? 0 : destination;

        packed.rgba8[destination + 0] = 255;
        packed.rgba8[destination + 1] = roughness.rgba8[roughnessOffset];
        packed.rgba8[destination + 2] = metallic.rgba8[metallicOffset];
        packed.rgba8[destination + 3] = 255;
    }

    ImageHandle rustedMetallicRoughnessTexture = std::make_shared<ImageResource>();
    rustedMetallicRoughnessTexture->name = packed.name;
    rustedMetallicRoughnessTexture->image =
        uploadTexture2D(context, renderer, packed, VK_FORMAT_R8G8B8A8_UNORM, packed.name);
    textureLibrary.push_back(rustedMetallicRoughnessTexture);

    const ImageHandle rustedAoTexture =
        createImageResource("Rusted Iron AO", PBR_AO_PATH, VK_FORMAT_R8G8B8A8_UNORM, {255, 255, 255, 255});

    defaultBaseColorTexture =
        createImageResource("Default Base Color", std::string(), VK_FORMAT_R8G8B8A8_SRGB, {255, 255, 255, 255});

    defaultNormalTexture =
        createImageResource("Default Normal", std::string(), VK_FORMAT_R8G8B8A8_UNORM, {128, 128, 255, 255});

    defaultMetallicRoughnessTexture = createImageResource("Default MetallicRoughness", std::string(),
                                                          VK_FORMAT_R8G8B8A8_UNORM, {255, 255, 255, 255}); // 默认白色

    // 表示缝隙等位置的环境遮蔽，主要影响间接光照
    defaultAoTexture = createImageResource("Default AO", std::string(), VK_FORMAT_R8G8B8A8_UNORM, {255, 255, 255, 255});

    // 表示便面自身发出的颜色，不依赖灯光照亮
    defaultEmissiveTexture =
        createImageResource("Default Emissive", std::string(), VK_FORMAT_R8G8B8A8_SRGB, {255, 255, 255, 255});

    defaultMaterial = std::make_shared<Material>();
    defaultMaterial->name = "Rusted Iron";
    defaultMaterial->baseColorTexture = {rustedBaseColorTexture, defaultTextureSampler, 0u};
    defaultMaterial->normalTexture = {rustedNormalTexture, defaultTextureSampler, 0u};
    defaultMaterial->metallicRoughnessTexture = {rustedMetallicRoughnessTexture, defaultTextureSampler, 0u};
    defaultMaterial->aoTexture = {rustedAoTexture, defaultTextureSampler, 0u};
    defaultMaterial->emissiveTexture = {defaultEmissiveTexture, defaultTextureSampler, 0u};

    MaterialHandle variantMaterial = std::make_shared<Material>();
    variantMaterial->name = "Rusted Iron Variant";
    variantMaterial->baseColorTexture = defaultMaterial->baseColorTexture;
    variantMaterial->normalTexture = defaultMaterial->normalTexture;
    variantMaterial->metallicRoughnessTexture = defaultMaterial->metallicRoughnessTexture;
    variantMaterial->aoTexture = defaultMaterial->aoTexture;
    variantMaterial->emissiveTexture = defaultMaterial->emissiveTexture;
    variantMaterial->baseColorFactor = glm::vec4(0.25f, 1.0f, 0.25f, 1.0f);
    variantMaterial->metallicFactor = 0.25f;
    variantMaterial->roughnessFactor = 0.30f;
    variantMaterial->occlusionStrength = 1.0f;

    materialLibrary.push_back(defaultMaterial);
    materialLibrary.push_back(variantMaterial);

    // gltf 没有指定 material 时使用的默认材质
    defaultGltfMaterial = std::make_shared<Material>();
    defaultGltfMaterial->name = "glTF Default";
    defaultGltfMaterial->baseColorTexture = {defaultBaseColorTexture, defaultTextureSampler, 0u};
    defaultGltfMaterial->normalTexture = {defaultNormalTexture, defaultTextureSampler, 0u};
    defaultGltfMaterial->metallicRoughnessTexture = {defaultMetallicRoughnessTexture, defaultTextureSampler, 0u};
    defaultGltfMaterial->aoTexture = {defaultAoTexture, defaultTextureSampler, 0u};
    defaultGltfMaterial->emissiveTexture = {defaultEmissiveTexture, defaultTextureSampler, 0u};

    materialLibrary.push_back(defaultGltfMaterial);
}

SamplerHandle Application::getOrCreateGltfSampler(const GltfSamplerData& source, const std::string& debugName,
                                                  std::vector<SamplerHandle>& library)
{
    const GltfFilter magFilter = source.magFilter.value_or(GltfFilter::Linear);
    const GltfFilter minFilter = source.minFilter.value_or(GltfFilter::LinearMipmapLinear);

    const SamplerKey key{static_cast<std::uint16_t>(magFilter), static_cast<std::uint16_t>(minFilter),
                         static_cast<std::uint16_t>(source.wrapS), static_cast<std::uint16_t>(source.wrapT)};

    for (const SamplerHandle& resource : library)
    {
        if (resource->key == key)
        {
            return resource;
        }
    }

    // 第一次遇到这个类型的 sampler，就创建对应的 gpusampler
    SamplerDesc desc{};
    desc.addressModeU = toSamplerAddressMode(source.wrapS, debugName);
    desc.addressModeV = toSamplerAddressMode(source.wrapT, debugName);
    desc.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    desc.anisotropyEnable = VK_FALSE;
    desc.maxAnisotropy = 1.0f;
    desc.minLod = 0.0f;
    desc.maxLod = VK_LOD_CLAMP_NONE;
    switch (magFilter)
    {
    case GltfFilter::Nearest:
        desc.magFilter = VK_FILTER_NEAREST;
        break;
    case GltfFilter::Linear:
        desc.magFilter = VK_FILTER_LINEAR;
        break;
    default:
        throw std::runtime_error(debugName + ": invalid magnification filter");
    }
    // LinearMipmapNearest 表示：每层内部线性过滤，mip 层之间选择最近一层
    // 普通 Nearest/Linear 不使用 mipmap。使用 NEAREST mipmapmode 和 maxLod = 0.25f，既限定在第 0 层，又保留缩小过滤的选择
    // 未指定过滤时项目默认使用 Linear + LinearMipmapLinear。
    switch (minFilter)
    {
    case GltfFilter::Nearest:
        desc.minFilter = VK_FILTER_NEAREST;
        desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        desc.maxLod = 0.25f;
        break;
    case GltfFilter::Linear:
        desc.minFilter = VK_FILTER_LINEAR;
        desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        desc.maxLod = 0.25f;
        break;
    case GltfFilter::NearestMipmapNearest:
        desc.minFilter = VK_FILTER_NEAREST;
        desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case GltfFilter::NearestMipmapLinear:
        desc.minFilter = VK_FILTER_NEAREST;
        desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        break;
    case GltfFilter::LinearMipmapNearest:
        desc.minFilter = VK_FILTER_LINEAR;
        desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        break;
    case GltfFilter::LinearMipmapLinear:
        desc.minFilter = VK_FILTER_LINEAR;
        desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        break;

    default:
        throw std::runtime_error(debugName + ": invalid minification filter");
    }

    SamplerHandle resource = std::make_shared<SamplerResource>();
    resource->name = debugName;
    resource->key = key;
    resource->sampler = context.createSampler(desc);
    library.push_back(resource);
    return resource;
}

MaterialHandle Application::createGltfMaterial(const GltfImportData& imported, const GltfMaterialData& source,
                                               const std::string& debugName, GltfMaterialUpload& upload)
{
    if (source.alphaMode == GltfAlphaMode::Blend)
    {
        throw std::runtime_error(debugName + ": BLEND materials are not supported");
    }

    // 把 gltf 里的一个材质纹理引用，转换为 Engine 自己的 MaterialTextureSlot
    // 使用 [&] 捕获，所以它可以直接访问 upload 参数
    auto makeSlot = [&](const std::optional<GltfMaterialTextureSlot>& reference, const ImageHandle& fallback,
                        VkFormat format, const std::string& semantic) -> MaterialTextureSlot
    {
        if (!reference)
        {
            return {fallback, defaultTextureSampler, 0u};
        }

        const std::string context = debugName + ": " + semantic;
        if (reference->textureIndex >= imported.textures.size())
        {
            throw std::runtime_error(context + ": texture index is out of range");
        }
        if (reference->texCoord > 1u)
        {
            throw std::runtime_error(context + ": unsupported UV set");
        }

        const GltfTextureRef& texture = imported.textures[reference->textureIndex];
        SamplerHandle sampler = defaultTextureSampler;

        if (texture.samplerIndex)
        {
            const std::size_t index = *texture.samplerIndex;
            if (index >= imported.samplers.size())
            {
                throw std::runtime_error(context + ": sampler index is out of range");
            }
            sampler = getOrCreateGltfSampler(
                imported.samplers[index], imported.sourcePath.u8string() + ": sampler[" + std::to_string(index) + "]",
                upload.samplers);
        }

        ImageHandle image = getOrCreateGltfImage(imported, texture.imageIndex, format, upload);

        return {image, sampler, reference->texCoord};
    };

    // Material 为完整的材质，包含多个 Material slot
    // 一个 Material slot 对应一个 Texture
    MaterialHandle material = std::make_shared<Material>();
    material->name = source.name.empty() ? debugName : source.name;
    material->baseColorFactor = source.baseColorFactor;
    material->metallicFactor = source.metallicFactor;
    material->roughnessFactor = source.roughnessFactor;
    material->emissiveFactor = source.emissiveFactor;
    material->normalScale = source.normalScale;
    material->occlusionStrength = source.occlusionStrength;
    material->doubleSided = source.doubleSided;
    material->alphaMode = source.alphaMode == GltfAlphaMode::Mask ? MaterialAlphaMode::Mask : MaterialAlphaMode::Opaque;
    material->alphaCutoff = source.alphaCutoff;

    material->baseColorTexture =
        makeSlot(source.baseColorTexture, defaultBaseColorTexture, VK_FORMAT_R8G8B8A8_SRGB, "baseColorTexture");
    material->normalTexture =
        makeSlot(source.normalTexture, defaultNormalTexture, VK_FORMAT_R8G8B8A8_UNORM, "normalTexture");
    material->metallicRoughnessTexture = makeSlot(source.metallicRoughnessTexture, defaultMetallicRoughnessTexture,
                                                  VK_FORMAT_R8G8B8A8_UNORM, "metallicRoughnessTexture");
    material->aoTexture =
        makeSlot(source.occlusionTexture, defaultAoTexture, VK_FORMAT_R8G8B8A8_UNORM, "occlusionTexture");
    material->emissiveTexture =
        makeSlot(source.emissiveTexture, defaultEmissiveTexture, VK_FORMAT_R8G8B8A8_SRGB, "emissiveTexture");
    return material;
}

MaterialHandle Application::createPresetMaterial(const std::string& name, const glm::vec4& color, float metallic,
                                                 float roughness)
{
    auto material = std::make_shared<Material>();
    material->name = name;
    material->baseColorTexture = {defaultBaseColorTexture, defaultTextureSampler, 0};
    material->normalTexture = {defaultNormalTexture, defaultTextureSampler, 0};
    material->metallicRoughnessTexture = {defaultMetallicRoughnessTexture, defaultTextureSampler, 0};
    material->aoTexture = {defaultAoTexture, defaultTextureSampler, 0};
    material->emissiveTexture = {defaultEmissiveTexture, defaultTextureSampler, 0};
    material->baseColorFactor = color;
    material->metallicFactor = metallic;
    material->roughnessFactor = roughness;
    material->normalScale = 0.0f;

    // 先完成可能抛异常的 vector 扩容，再分配裸 descriptor handle，避免发布时泄漏。
    materialLibrary.reserve(materialLibrary.size() + 1);
    allocateMaterialDescriptorSets({material});
    materialLibrary.push_back(material);
    return material;
}
