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
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba8;
};

// Nearest/Linear: 决定单个 mip 层内部怎么采样
// 带 Mipmap 的 4 个：同时决定 mip 层内部怎么采样 + mip 层之间怎么选
// XXXMipmapYYY:
//      XXX = 在某一个 mip 层内部怎么取 texel
//      YYY = 在两个 mip 层之间怎么选
enum class GltfFilter : std::uint16_t
{
    Nearest = 9728,
    Linear = 9729,
    NearestMipmapNearest = 9984,
    LinearMipmapNearest = 9985,
    NearestMipmapLinear = 9986,
    LinearMipmapLinear = 9987
};
enum class GltfWrap : std::uint16_t
{
    ClampToEdge = 33071,
    MirroredRepeat = 33648,
    Repeat = 10497
};

struct GltfSamplerData
{
    std::string name;
    std::optional<GltfFilter> magFilter; // 纹理被放大
    std::optional<GltfFilter> minFilter; // 纹理被缩小
    GltfWrap wrapS = GltfWrap::Repeat;
    GltfWrap wrapT = GltfWrap::Repeat;
};

struct GltfTextureRef
{
    std::string name;
    std::size_t imageIndex = 0;
    std::optional<std::size_t> samplerIndex;
};

struct GltfMaterialTextureSlot
{
    std::size_t textureIndex = 0;
    std::uint32_t texCoord = 0;
};

enum class GltfAlphaMode
{
    Opaque,
    Mask,
    Blend
};

// 材质槽 -> texture -> image + 可选 sampler
struct GltfMaterialData
{
    std::string name;

    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    glm::vec3 emissiveFactor{0.0f};

    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;

    std::optional<GltfMaterialTextureSlot> baseColorTexture;
    std::optional<GltfMaterialTextureSlot> metallicRoughnessTexture;
    std::optional<GltfMaterialTextureSlot> normalTexture;
    std::optional<GltfMaterialTextureSlot> occlusionTexture;
    std::optional<GltfMaterialTextureSlot> emissiveTexture;

    GltfAlphaMode alphaMode = GltfAlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
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

    // 节点相对于父节点的变换。单位矩阵表示没有平移、旋转或缩放
    glm::mat4 localTransform{1.0f};
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
    std::vector<GltfMaterialData> materials;
    std::vector<GltfTextureRef> textures;
    std::vector<DecodedImageData> images;
    std::vector<GltfSamplerData> samplers;


    std::size_t materialCount = 0;
    std::size_t textureCount = 0;
    std::size_t bufferCount = 0;
    std::size_t accessorCount = 0;
};
