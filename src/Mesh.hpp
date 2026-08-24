#pragma once
#include "VulkanResources.hpp"
#include "VulkanTypes.hpp"

#include <cstdint>
#include <vector>
#include <memory>

enum class MeshSource
{
    Obj,
    Cube,
    Sphere
};

struct MeshBuildData
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    glm::vec3 boundsMin = {0.0f, 0.0f, 0.0f};
    glm::vec3 boundsMax = {0.0f, 0.0f, 0.0f};
    bool boundsValid = false;
};

struct Mesh
{
    GpuBuffer vertexBuffer;
    GpuBuffer indexBuffer;

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;

    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    bool boundsValid = false;

    bool valid() const noexcept
    {
        return vertexBuffer && indexBuffer && vertexCount > 0 && indexCount > 0;
    }
};

using MeshHandle = std::shared_ptr<Mesh>;