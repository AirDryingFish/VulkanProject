#pragma once

#include "VulkanHeaders.hpp"

#include <array>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <glm/gtx/hash.hpp>

#include <vk_mem_alloc.h>

inline constexpr uint32_t MAX_POINT_LIGHTS = 16;

struct QueueFamilyIndices
{
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const
    {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct Vertex
{
    glm::vec3 pos{0.0f};
    glm::vec3 color{1.0f};
    glm::vec2 texcoord{0.0f};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};

    // tangent.xyz 是切线方向
    // tangent.w = +1 或 -1 表示 bitangent handedness
    // tangent.w = 0 表示当前顶点没有有效 tangent
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 0.0f};

    // 第二套纹理坐标对应 gltf TEXCOORD_1
    glm::vec2 texcoord1{0.0f};

    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 6> getAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 6> attributeDescriptions{};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texcoord);

        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(Vertex, normal);

        attributeDescriptions[4].binding = 0;
        attributeDescriptions[4].location = 4;
        attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[4].offset = offsetof(Vertex, tangent);

        attributeDescriptions[5].binding = 0;
        attributeDescriptions[5].location = 5;
        attributeDescriptions[5].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[5].offset = offsetof(Vertex, texcoord1);

        return attributeDescriptions;
    }

    bool operator==(const Vertex &other) const
    {
        return pos == other.pos &&
            color == other.color &&
            texcoord == other.texcoord &&
            normal == other.normal &&
            tangent == other.tangent &&
            texcoord1 == other.texcoord1;
    }
};

namespace std
{
// 模版特化：当T=Vertex的时候，不要用默认实现而是用这里的实现
template <>
// 如果把 Vertex 放进 std::unordered_map / std::unordered_set，应该怎么计算 Vertex 的哈希值
struct hash<Vertex>
{
    size_t operator()(Vertex const &vertex) const
    {
        size_t seed = 0;

        auto combine = [&seed](size_t value)
        {
            seed ^= value +
                    0x9e3779b9u +
                    (seed << 6u) +
                    (seed >> 2u);
        };

        combine(hash<glm::vec3>()(vertex.pos));
        combine(hash<glm::vec3>()(vertex.color));
        combine(hash<glm::vec2>()(vertex.texcoord));
        combine(hash<glm::vec3>()(vertex.normal));
        combine(hash<glm::vec4>()(vertex.tangent));
        combine(hash<glm::vec2>()(vertex.texcoord1));

        return seed;
    }
};
}

struct PointLight
{
    glm::vec3 position = {0.0f, 0.0f, 1.5f};
    glm::vec3 color = {1.0f, 0.92f, 0.78f};
    float intensity = 4.0f;
    float range = 8.0f;
    bool enabled = true;
};

struct GpuPointLight
{
    alignas(16) glm::vec4 position;
    alignas(16) glm::vec4 color;
    alignas(16) glm::vec4 params;
};

struct alignas(16) UniformBufferObject
{
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;

    alignas(16) glm::vec4 cameraPosition;
    alignas(16) glm::vec4 ambientLight;
    alignas(16) glm::ivec4 lightCounts;

    // x = IBL intensity
    // yzw = reserved
    alignas(16) glm::vec4 renderParams;

    GpuPointLight pointLights[MAX_POINT_LIGHTS];
};

static_assert(offsetof(UniformBufferObject, proj) == 64);
static_assert(offsetof(UniformBufferObject, cameraPosition) == 128);
static_assert(offsetof(UniformBufferObject, ambientLight) == 144);
static_assert(offsetof(UniformBufferObject, lightCounts) == 160);
static_assert(offsetof(UniformBufferObject, renderParams) == 176);
static_assert(offsetof(UniformBufferObject, pointLights) == 192);
static_assert(sizeof(UniformBufferObject) == 960);
