#pragma once

#include "VulkanResources.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <array>
#include <cstdint>

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

struct Material
{
    std::string name;

    ImageHandle baseColorTexture;
    ImageHandle normalTexture;
    ImageHandle metallicTexture;
    ImageHandle roughnessTexture;
    ImageHandle aoTexture;
    ImageHandle emissiveTexture;

    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float aoFactor = 1.0f;
    glm::vec3 emissiveFactor{0.0f};

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

using MaterialHandle = std::shared_ptr<Material>;
