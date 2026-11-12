#pragma once

#include "scene/Vertex.hpp"

#include <cstdint>
#include <vector>

// CPU 几何构建输入与结果；GPU buffer 的所有权由 Mesh 单独持有。
enum class MeshSource
{
    Obj,
    Cube,
    Sphere,
    Gltf
};

struct MeshBuildData
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    glm::vec3 boundsMin = {0.0f, 0.0f, 0.0f};
    glm::vec3 boundsMax = {0.0f, 0.0f, 0.0f};
    bool boundsValid = false;
    bool hasTangents = false;
};
