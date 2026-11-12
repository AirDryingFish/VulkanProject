#pragma once
#include "vulkan/resources/VulkanResources.hpp"
#include "scene/MeshData.hpp"

#include <cstdint>
#include <memory>
#include <string>

struct Mesh
{
    std::string cacheKey;

    GpuBuffer vertexBuffer;
    GpuBuffer indexBuffer;

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;

    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    bool boundsValid = false;
    bool hasTangents = false;

    bool valid() const noexcept
    {
        return vertexBuffer && indexBuffer && vertexCount > 0 && indexCount > 0;
    }
};

using MeshHandle = std::shared_ptr<Mesh>;
