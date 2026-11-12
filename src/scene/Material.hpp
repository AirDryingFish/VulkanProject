#pragma once

#include "vulkan/resources/VulkanResources.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <array>
#include <cstdint>
#include <cstddef>
#include <stdexcept>

struct ImageResource
{
    std::string name;
    GpuImage image;
};

using ImageHandle = std::shared_ptr<ImageResource>;

// 4 个元素依次保存 magFilter minFilter wrapS wrap T
using SamplerKey = std::array<std::uint16_t, 4>;

struct SamplerResource
{
    std::string name;
    SamplerKey key{};
    GpuSampler sampler;
};

using SamplerHandle = std::shared_ptr<SamplerResource>;

// 运行时纹理槽持有 GPU 图像与采样器；texCoord 选择资产的 UV0 或 UV1。
struct MaterialTextureSlot
{
    ImageHandle image;
    SamplerHandle sampler;
    std::uint32_t texCoord = 0;
};

enum class MaterialAlphaMode
{
    Opaque,
    Mask
};

struct Material
{
    std::string name;

    MaterialTextureSlot baseColorTexture;
    MaterialTextureSlot normalTexture;
    MaterialTextureSlot metallicRoughnessTexture;
    MaterialTextureSlot aoTexture;
    MaterialTextureSlot emissiveTexture;

    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;

    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float occlusionStrength = 1.0f;
    float normalScale = 1.0f;
    glm::vec3 emissiveFactor{0.0f};

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    // 第 i 位表示第 i 个纹理槽是否使用 UV1，槽顺序须与 shader 的材质布局一致。
    std::uint32_t textureUvMask() const
    {
        const std::array<const MaterialTextureSlot*, 5> slots{&baseColorTexture, &normalTexture,
                                                              &metallicRoughnessTexture, &aoTexture, &emissiveTexture};
        std::uint32_t mask = 0;
        for (std::size_t slot = 0; slot < slots.size(); ++slot)
        {
            if (slots[slot]->texCoord > 1u)
            {
                throw std::runtime_error(name + ": supported texture coordinate set");
            }
            if (slots[slot]->texCoord == 1u)
            {
                mask |= (1u << slot);
            }
        }
        return mask;
    }
};

using MaterialHandle = std::shared_ptr<Material>;
