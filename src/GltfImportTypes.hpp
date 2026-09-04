#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct DecodedImageData
{
    std::string name;
    int width;
    int height;
    std::vector<std::uint8_t> rgba8;
};

struct GltfDecodedVertex
{
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec4 color{1.0f};
    glm::vec2 texcoord0{0.0f};
    glm::vec2 texcoord1{0.0f};
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 0.0f};
};

struct GltfPrimitiveData
{
    std::vector<GltfDecodedVertex> vertices;
    std::vector<std::uint32_t> indices;

    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};

    bool normalsFromAsset = false;
    bool hasTexcoord0 = false;
    bool hasTexcoord1 = false;
    bool hasColor0 = false;
    bool hasTangents = false;

    std::optional<std::size_t> materialIndex;
};

struct GltfMeshData
{
    std::string name;
    std::vector<std::size_t> primitiveIndices;
};

struct GltfNodeSummary
{
    std::string name;
    std::optional<std::size_t> meshIndex;
    std::vector<std::size_t> children;
};

struct GltfSceneSummary
{
    std::string name;
    std::vector<std::size_t> rootNodeIndices;
};

struct GltfImportData
{
    std::filesystem::path sourcePath;

    std::optional<std::size_t> defaultSceneIndex;
    std::vector<GltfSceneSummary> scenes;
    std::vector<GltfNodeSummary> nodes;
    std::vector<GltfMeshData> meshes;
    std::vector<GltfPrimitiveData> primitives;
    std::vector<DecodedImageData> images;

    std::size_t materialCount = 0;
    std::size_t textureCount = 0;
    std::size_t bufferCount = 0;
    std::size_t accessorCount = 0;
};
