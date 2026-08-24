#pragma once

#include "VulkanResources.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>

struct TextureResource{
    std::string name;
    GpuImage image;
};

using TextureHandle = std::shared_ptr<TextureResource>;

struct Material
{
    std::string name;

    TextureHandle baseColorTexture;
    TextureHandle normalTexture;
    TextureHandle metallicTexture;
    TextureHandle roughnessTexture;
    TextureHandle aoTexture;
    TextureHandle emissiveTexture;

    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float aoFactor = 1.0f;
    glm::vec3 emissiveFactor{0.0f};

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

using MaterialHandle = std::shared_ptr<Material>;
