#pragma once

#include "VulkanResources.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <array>
#include <cstdint>
#include <cstddef>
#include <stdexcept>

struct ImageResource{
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

// 运行时渲染数据，持有实际资源
// GltfMaterialTextureSlot
//   │
//   ├─ textureIndex → imported.textures[index]
//   │                   ├─ imageIndex → 上传或复用 GPU image
//   │                   └─ samplerIndex → 创建或复用 GPU sampler
//   │
//   └─ texCoord ───────────────────────────────┐
//                                             ↓ MaterialTextureSlot
struct MaterialTextureSlot
{
    ImageHandle image;
    SamplerHandle sampler;
    std::uint32_t texCoord = 0;
};

struct Material
{
    std::string name;

    MaterialTextureSlot baseColorTexture;
    MaterialTextureSlot normalTexture;
    MaterialTextureSlot metallicRoughnessTexture;
    MaterialTextureSlot aoTexture;
    MaterialTextureSlot emissiveTexture;

    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float occlusionStrength = 1.0f;
    float normalScale = 1.0f;
    glm::vec3 emissiveFactor{0.0f};

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    std::uint32_t textureUvMask() const
    {
        const std::array<const MaterialTextureSlot*, 5> slots{
            &baseColorTexture,
            &normalTexture,
            &metallicRoughnessTexture,
            &aoTexture,
            &emissiveTexture
        };
        std::uint32_t mask = 0;
        for (std::size_t slot = 0; slot < slots.size(); ++slot)
        {
            if (slots[slot]->texCoord > 1u)
            {
                throw std::runtime_error(name + ": supported texture coordinate set");
            }
            if (slots[slot]->texCoord == 1u)
            {
                // UV0: mask = 00001
                // UV1: mask = 00010
                mask |= (1u << slot);
            }
        }
        return mask;
    }
};

using MaterialHandle = std::shared_ptr<Material>;
