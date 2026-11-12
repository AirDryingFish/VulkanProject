#include "assets/gltf/GltfImportInternal.hpp"

#include <fastgltf/types.hpp>

#include <stdexcept>
#include <string>

namespace
{
// NormalTextureInfo 和 OcclusionTextureInfo 都继承
GltfMaterialTextureSlot decodeMaterialTextureSlot(const fastgltf::TextureInfo& source, std::size_t textureCount,
                                                  const std::string& context)
{
    if (source.textureIndex >= textureCount)
    {
        throw std::runtime_error(context + ": texture index " + std::to_string(source.textureIndex) +
                                 " is out of range");
    }
    if (source.transform)
    {
        throw std::runtime_error(context + ": texture transform is not supported");
    }
    if (source.texCoordIndex > 1)
    {
        throw std::runtime_error(context + ": only TEXTURE_0 and TEXTURE_1 are supported");
    }

    GltfMaterialTextureSlot result;
    result.textureIndex = source.textureIndex;
    result.texCoord = static_cast<std::uint32_t>(source.texCoordIndex);

    return result;
}

// 从 gltf 文件中解析 material，存入 cpu 的 GltfMaterialData 对象中
GltfMaterialData decodeMaterial(const fastgltf::Asset& asset, const std::filesystem::path& assetPath,
                                std::size_t materialIndex)
{
    const fastgltf::Material& source = asset.materials[materialIndex];
    const fastgltf::PBRData& pbr = source.pbrData;
    const std::string context = assetPath.u8string() + ": material[" + std::to_string(materialIndex) + "]";

    GltfMaterialData result;
    result.name.assign(source.name.begin(), source.name.end());
    result.baseColorFactor =
        glm::vec4(pbr.baseColorFactor[0], pbr.baseColorFactor[1], pbr.baseColorFactor[2], pbr.baseColorFactor[3]);
    result.metallicFactor = pbr.metallicFactor;
    result.roughnessFactor = pbr.roughnessFactor;
    result.emissiveFactor = glm::vec3(source.emissiveFactor[0], source.emissiveFactor[1], source.emissiveFactor[2]);
    if (pbr.baseColorTexture)
    {
        result.baseColorTexture =
            decodeMaterialTextureSlot(*pbr.baseColorTexture, asset.textures.size(), context + ": baseColorTexture");
    }
    if (pbr.metallicRoughnessTexture)
    {
        result.metallicRoughnessTexture = decodeMaterialTextureSlot(
            *pbr.metallicRoughnessTexture, asset.textures.size(), context + ": metallicRoughnessTexture");
    }
    if (source.normalTexture)
    {
        result.normalTexture =
            decodeMaterialTextureSlot(*source.normalTexture, asset.textures.size(), context + ": normalTexture");
        result.normalScale = source.normalTexture->scale;
    }
    if (source.occlusionTexture)
    {
        result.occlusionTexture =
            decodeMaterialTextureSlot(*source.occlusionTexture, asset.textures.size(), context + ": occlusionTexture");
        result.occlusionStrength = source.occlusionTexture->strength;
    }
    if (source.emissiveTexture)
    {
        result.emissiveTexture =
            decodeMaterialTextureSlot(*source.emissiveTexture, asset.textures.size(), context + ": emissiveTexture");
    }

    // 保存 alpha 和 双面状态
    switch (source.alphaMode)
    {
    case fastgltf::AlphaMode::Opaque:
        result.alphaMode = GltfAlphaMode::Opaque;
        break;

    case fastgltf::AlphaMode::Mask:
        result.alphaMode = GltfAlphaMode::Mask;
        break;

    case fastgltf::AlphaMode::Blend:
        result.alphaMode = GltfAlphaMode::Blend;
        break;

    default:
        throw std::runtime_error(context + ": invalid alpha mode");
    }

    result.alphaCutoff = source.alphaCutoff;
    result.doubleSided = source.doubleSided;

    return result;
}
} // namespace

void gltf_import_detail::decodeMaterials(const fastgltf::Asset& asset, const std::filesystem::path& normalizedPath,
                                         GltfImportData& result)
{
    // 复制 sampler 参数
    result.samplers.reserve(asset.samplers.size());
    for (const fastgltf::Sampler& source : asset.samplers)
    {
        GltfSamplerData sampler;
        sampler.name.assign(source.name.begin(), source.name.end());
        if (source.magFilter)
        {
            sampler.magFilter = static_cast<GltfFilter>(*source.magFilter);
        }
        if (source.minFilter)
        {
            sampler.minFilter = static_cast<GltfFilter>(*source.minFilter);
        }
        sampler.wrapS = static_cast<GltfWrap>(source.wrapS);
        sampler.wrapT = static_cast<GltfWrap>(source.wrapT);
        result.samplers.push_back(sampler);
    }

    // 保存 texture 引用
    result.textures.reserve(asset.textures.size());
    for (std::size_t textureIndex = 0; textureIndex < asset.textures.size(); ++textureIndex)
    {
        const fastgltf::Texture& source = asset.textures[textureIndex];
        const std::string context = normalizedPath.u8string() + ": texture[" + std::to_string(textureIndex) + "]";
        if (!source.imageIndex)
        {
            throw std::runtime_error(context + ": no supported image source");
        }
        const std::size_t imageIndex = *source.imageIndex;
        if (imageIndex >= result.images.size())
        {
            throw std::runtime_error(context + ": image index " + std::to_string(imageIndex) + " is out of range");
        }

        GltfTextureRef texture;
        texture.name.assign(source.name.begin(), source.name.end());
        texture.imageIndex = imageIndex;
        if (source.samplerIndex)
        {
            const std::size_t samplerIndex = *source.samplerIndex;
            if (samplerIndex >= result.samplers.size())
            {
                throw std::runtime_error(context + ": sampler index " + std::to_string(samplerIndex) +
                                         " is out of range");
            }
            texture.samplerIndex = samplerIndex;
        }
        result.textures.push_back(texture);
    }

    result.materials.reserve(asset.materials.size());
    for (std::size_t materialIndex = 0; materialIndex < asset.materials.size(); ++materialIndex)
    {
        result.materials.push_back(decodeMaterial(asset, normalizedPath, materialIndex));
    }
}
