#include "GltfLoader.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
[[noreturn]] void failPrimitive(
    const std::filesystem::path& assetPath,
    std::size_t meshIndex,
    std::size_t primitiveIndex,
    std::string_view semantic,
    std::optional<std::size_t> accessorIndex,
    std::string_view reason
)
{
    std::ostringstream message;
    message << assetPath.string() << ": mesh[" << meshIndex << "]"
            << " primitive[" << primitiveIndex << "]"
            << " semantic=" << semantic
            << " accessor[";
    if (accessorIndex)
    {
        message << *accessorIndex;
    }
    else
    {
        message << "none";
    }
    message << "]: " << reason;
    throw std::runtime_error(message.str());
}

void generateFlatNormals(
    GltfPrimitiveData& data,
    const std::filesystem::path& assetPath,
    std::size_t meshIndex,
    std::size_t primitiveIndex,
    std::optional<std::size_t> indexAccessorIndex
)
{
    if (data.indices.empty() || data.indices.size() % 3 != 0)
    {
        failPrimitive(
            assetPath,
            meshIndex,
            primitiveIndex,
            "indices",
            indexAccessorIndex,
            "flat normal generation requires complete triangles"
        );
    }

    const std::size_t maxGeneratedIndex = data.indices.size() - 1;
    if (maxGeneratedIndex > std::numeric_limits<std::uint32_t>::max())
    {
        failPrimitive(
            assetPath,
            meshIndex,
            primitiveIndex,
            "indices",
            indexAccessorIndex,
            "flat normal split indices do not fit uint32_t"
        );
    }

    std::vector<GltfDecodedVertex> splitVertices;
    std::vector<std::uint32_t> splitIndices;
    splitVertices.reserve(data.indices.size());

    // 按三角形读取并复制顶点
    for (std::size_t indexOffset = 0; indexOffset < data.indices.size(); indexOffset += 3)
    {
        const std::size_t triangleIndex = indexOffset / 3;
        GltfDecodedVertex vertex0 = data.vertices[data.indices[indexOffset + 0]];
        GltfDecodedVertex vertex1 = data.vertices[data.indices[indexOffset + 1]];
        GltfDecodedVertex vertex2 = data.vertices[data.indices[indexOffset + 2]];

        const glm::dvec3 position0{
            static_cast<double>(vertex0.position.x),
            static_cast<double>(vertex0.position.y),
            static_cast<double>(vertex0.position.z)
        };
        const glm::dvec3 position1{
            static_cast<double>(vertex1.position.x),
            static_cast<double>(vertex1.position.y),
            static_cast<double>(vertex1.position.z)
        };
        const glm::dvec3 position2{
            static_cast<double>(vertex2.position.x),
            static_cast<double>(vertex2.position.y),
            static_cast<double>(vertex2.position.z)
        };
        const glm::dvec3 edge1 = position1 - position0;
        const glm::dvec3 edge2 = position2 - position0;
        // 面法线方向，还能反映三角形面积
        const glm::dvec3 crossNormal = glm::cross(edge1, edge2);
        const double lengthSquared = glm::dot(crossNormal, crossNormal);
        if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0)
        {
            std::ostringstream reason;
            reason << "triangle[" << triangleIndex << "] is degenerate and cannot produce a flat normal";
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "NORMAL",
                std::nullopt,
                reason.str()
            );
        }
        const glm::dvec3 faceNormalDouble = crossNormal / std::sqrt(lengthSquared);
        const glm::vec3 faceNormal{
            static_cast<float>(faceNormalDouble.x),
            static_cast<float>(faceNormalDouble.y),
            static_cast<float>(faceNormalDouble.z)
        };

        if (!std::isfinite(faceNormal.x) ||
            !std::isfinite(faceNormal.y) ||
            !std::isfinite(faceNormal.z))
        {
            std::ostringstream reason;

            reason << "triangle[" << triangleIndex
                   << "] produced a non-finite flat normal";

            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "NORMAL",
                std::nullopt,
                reason.str());
        }
        // 给三个顶点写入相同Normal
        vertex0.normal = faceNormal;
        vertex1.normal = faceNormal;
        vertex2.normal = faceNormal;

        // 原tangent是相对资产的normal构造的，现在normal被替换为面法线，旧tangent已经不能保证有效
        const glm::vec4 invalidTangent{
            1.0f, 0.0f, 0.0f, 0.0f
        };
        vertex0.tangent = invalidTangent;
        vertex1.tangent = invalidTangent;
        vertex2.tangent = invalidTangent;

        splitVertices.push_back(std::move(vertex0));
        splitVertices.push_back(std::move(vertex1));
        splitVertices.push_back(std::move(vertex2));
    }
    splitIndices.resize(splitVertices.size());
    std::iota(
        splitIndices.begin(),
        splitIndices.end(),
        std::uint32_t{0}
    );

    data.vertices = std::move(splitVertices);
    data.indices = std::move(splitIndices);
    data.normalsFromAsset = false;
    data.hasTangents = false;
}

void calculateLocalBounds(
    GltfPrimitiveData& data,
    const std::filesystem::path& assetPath,
    std::size_t meshIndex,
    std::size_t primitiveIndex,
    std::size_t positionAccessorIndex
)
{
    if (data.vertices.empty())
    {
        failPrimitive(
            assetPath,
            meshIndex,
            primitiveIndex,
            "POSITION",
            positionAccessorIndex,
            "cannot calculate bounds for an empty primitive"
        );
    }

    glm::vec3 minimum = data.vertices.front().position;
    glm::vec3 maximum = data.vertices.front().position;
    for (std::size_t vertexIndex = 0; vertexIndex < data.vertices.size(); vertexIndex++)
    {
        const glm::vec3& position = data.vertices[vertexIndex].position;
        if (!std::isfinite(position.x) ||
            !std::isfinite(position.y) ||
            !std::isfinite(position.z))
        {
            std::ostringstream reason;
            reason << "decoded POSITION element[" << vertexIndex << "] contains a non-finite value";
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "POSITION",
                positionAccessorIndex,
                reason.str()
            );
        }
        minimum.x = std::min(minimum.x, position.x);
        minimum.y = std::min(minimum.y, position.y);
        minimum.z = std::min(minimum.z, position.z);

        maximum.x = std::max(maximum.x, position.x);
        maximum.y = std::max(maximum.y, position.y);
        maximum.z = std::max(maximum.z, position.z);
    }
    data.boundsMin = minimum;
    data.boundsMax = maximum;
}

const fastgltf::Accessor& checkedAccessor(
    const fastgltf::Asset& asset,
    const std::filesystem::path& assetPath,
    std::size_t meshIndex,
    std::size_t primitiveIndex,
    std::string_view semantic,
    std::size_t accessorIndex)
{
    if (accessorIndex >= asset.accessors.size())
    {
        failPrimitive(
            assetPath,
            meshIndex,
            primitiveIndex,
            semantic,
            std::optional<std::size_t>{accessorIndex},
            "accessor index is out of range");
    }

    return asset.accessors[accessorIndex];
}

GltfPrimitiveData decodePrimitive(
    const fastgltf::Asset& asset,
    const std::filesystem::path& assetPath,
    std::size_t meshIndex,
    std::size_t primitiveIndex,
    const fastgltf::Primitive& primitive)
{
    if (primitive.type != fastgltf::PrimitiveType::Triangles)
    {
        failPrimitive(
            assetPath,
            meshIndex,
            primitiveIndex,
            "primitive.type",
            std::nullopt,
            "only triangle primitives are supported"
        );
    }

    const auto positionIt = primitive.findAttribute("POSITION");
    if (positionIt == primitive.attributes.cend())
    {
        failPrimitive(
            assetPath,
            meshIndex,
            primitiveIndex,
            "POSITION",
            std::nullopt,
            "primitive is missing POSITION attribute"
        );
    }

    const std::size_t positionAccessorIndex = positionIt->accessorIndex;
    const fastgltf::Accessor& positionAccessor =
        checkedAccessor(
            asset,
            assetPath,
            meshIndex,
            primitiveIndex,
            "POSITION",
            positionAccessorIndex
        );

    if (positionAccessor.type != fastgltf::AccessorType::Vec3)
    {
        failPrimitive(
            assetPath,
            meshIndex,
            primitiveIndex,
            "POSITION",
            positionAccessorIndex,
            "POSITION accessor must be of type VEC3"
        );
    }

    if (positionAccessor.componentType != fastgltf::ComponentType::Float)
    {
        failPrimitive(
            assetPath,
            meshIndex,
            primitiveIndex,
            "POSITION",
            positionAccessorIndex,
            "POSITION accessor must have component type FLOAT"
        );
    }

    if (positionAccessor.normalized)
    {
        failPrimitive(
            assetPath,
            meshIndex,
            primitiveIndex,
            "POSITION",
            positionAccessorIndex,
            "POSITION accessor must not be normalized"
        );
    }

    if (positionAccessor.count == 0)
    {
        failPrimitive(
            assetPath,
            meshIndex,
            primitiveIndex,
            "POSITION",
            positionAccessorIndex,
            "POSITION accessor must have at least one element"
        );
    }

    GltfPrimitiveData result;
    result.vertices.resize(positionAccessor.count);
    fastgltf::iterateAccessorWithIndex<glm::vec3>(
        asset,
        positionAccessor,
        [&](glm::vec3 value, std::size_t vertexIndex)
        {
            if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
            {
                failPrimitive(
                    assetPath,
                    meshIndex,
                    primitiveIndex,
                    "POSITION",
                    positionAccessorIndex,
                    "POSITION accessor contains non-finite values"
                );
            }
            result.vertices[vertexIndex].position = value;
        }
    );

    // NORMAL解码
    const auto normalIt = primitive.findAttribute("NORMAL");
    if (normalIt != primitive.attributes.cend())
    {
        const std::size_t normalAccessorIndex = normalIt->accessorIndex;
        const fastgltf::Accessor& normalAccessor =
            checkedAccessor(
                asset,
                assetPath,
                meshIndex,
                primitiveIndex,
                "NORMAL",
                normalAccessorIndex
            );
        if (normalAccessor.type != fastgltf::AccessorType::Vec3)
        {
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "NORMAL",
                normalAccessorIndex,
                "NORMAL accessor must have type VEC3");
        }

        if (normalAccessor.componentType != fastgltf::ComponentType::Float)
        {
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "NORMAL",
                normalAccessorIndex,
                "NORMAL accessor must have component type FLOAT");
        }

        if (normalAccessor.normalized)
        {
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "NORMAL",
                normalAccessorIndex,
                "NORMAL accessor must not be normalized");
        }

        if (normalAccessor.count != positionAccessor.count)
        {
            std::ostringstream reason;
            reason << "NORMAL accessor count (" << normalAccessor.count
                   << ") must match POSITION accessor count (" << positionAccessor.count << ")";
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "NORMAL",
                normalAccessorIndex,
                reason.str());
        }

        fastgltf::iterateAccessorWithIndex<glm::vec3>(
            asset,
            normalAccessor,
            [&](glm::vec3 value, std::size_t vertexIndex)
            {
                if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
                {
                    failPrimitive(
                        assetPath,
                        meshIndex,
                        primitiveIndex,
                        "NORMAL",
                        normalAccessorIndex,
                        "NORMAL accessor contains non-finite values"
                    );
                }
                const float lengthSquared = glm::dot(value, value);
                if (!std::isfinite(lengthSquared) || lengthSquared < 1e-6f)
                {
                    failPrimitive(
                        assetPath,
                        meshIndex,
                        primitiveIndex,
                        "NORMAL",
                        normalAccessorIndex,
                        "NORMAL accessor contains zero-length or non-finite vector"
                    );
                }
                result.vertices[vertexIndex].normal = value;
            }
        );
        result.normalsFromAsset = true;
    }

    auto decodeTexcoord = [&](std::string_view semantic, bool writeTexcoord1) -> bool
    {
        const auto attributeIt = primitive.findAttribute(semantic);
        if (attributeIt == primitive.attributes.cend())
        {
            return false;
        }
        const std::size_t accessorIndex = attributeIt->accessorIndex;
        const fastgltf::Accessor& accessor =
            checkedAccessor(
                asset,
                assetPath,
                meshIndex,
                primitiveIndex,
                semantic,
                accessorIndex
            );
        if (accessor.type != fastgltf::AccessorType::Vec2)
        {
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                semantic,
                accessorIndex,
                "texture coordinate accessor must have type VEC2");
        }

        const bool isFloatEncoding = accessor.componentType == fastgltf::ComponentType::Float && !accessor.normalized;
        const bool isNormalizedEncoding =
            accessor.normalized &&
            (accessor.componentType == fastgltf::ComponentType::UnsignedByte
            || accessor.componentType == fastgltf::ComponentType::UnsignedShort);
        if (!isFloatEncoding && !isNormalizedEncoding)
        {
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                semantic,
                accessorIndex,
                "texture coordinate accessor must have component type FLOAT, normalized U8, or normalized U16");
        }
        if (accessor.count != positionAccessor.count)
        {
            std::ostringstream reason;
            reason << semantic << " accessor count (" << accessor.count << ")"
                << " must match POSITION accessor count (" << positionAccessor.count << ")";
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                semantic,
                accessorIndex,
                reason.str());
        }

        fastgltf::iterateAccessorWithIndex<glm::vec2>(
            asset,
            accessor,
            [&](glm::vec2 value, std::size_t vertexIndex)
            {
                if (!std::isfinite(value.x) || !std::isfinite(value.y))
                {
                    failPrimitive(
                        assetPath,
                        meshIndex,
                        primitiveIndex,
                        semantic,
                        accessorIndex,
                        "texture coordinate accessor contains non-finite values"
                    );
                }
                if (writeTexcoord1)
                {
                    result.vertices[vertexIndex].texcoord1 = value;
                }
                else
                {
                    result.vertices[vertexIndex].texcoord0 = value;
                }
            }
        );
        return true;
    };
    result.hasTexcoord0 = decodeTexcoord("TEXCOORD_0", false);
    result.hasTexcoord1 = decodeTexcoord("TEXCOORD_1", true);

    // COLOR_0解码
    const auto colorIt = primitive.findAttribute("COLOR_0");
    if (colorIt != primitive.attributes.cend())
    {
        const std::size_t colorAccessorIndex = colorIt->accessorIndex;
        const fastgltf::Accessor& colorAccessor =
            checkedAccessor(
                asset,
                assetPath,
                meshIndex,
                primitiveIndex,
                "COLOR_0",
                colorAccessorIndex
            );
        const bool supportedType =
            colorAccessor.type == fastgltf::AccessorType::Vec3 ||
            colorAccessor.type == fastgltf::AccessorType::Vec4;
        if (!supportedType)
        {
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "COLOR_0",
                colorAccessorIndex,
                "COLOR_0 accessor must have type VEC3 or VEC4"
            );
        }
        // 如果底层是 float，则必须是 float 类型；如果底层是 uint8 或 uint16，则必须是 normalized
        // 接受的组合只有：
        // VEC3/VEC4 + Float         + normalized=false
        // VEC3/VEC4 + UnsignedByte  + normalized=true
        // VEC3/VEC4 + UnsignedShort + normalized=true
        const bool isFloatEncoding = colorAccessor.componentType == fastgltf::ComponentType::Float && !colorAccessor.normalized;
        const bool isNormalizedEncoding =
            colorAccessor.normalized &&
            (colorAccessor.componentType == fastgltf::ComponentType::UnsignedByte
            || colorAccessor.componentType == fastgltf::ComponentType::UnsignedShort);
        if (!isFloatEncoding && !isNormalizedEncoding)
        {
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "COLOR_0",
                colorAccessorIndex,
                "COLOR_0 accessor must have component type FLOAT, normalized U8, or normalized U16"
            );
        }
        if (colorAccessor.count != positionAccessor.count)
        {
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "COLOR_0",
                colorAccessorIndex,
                "COLOR_0 accessor count must match POSITION accessor count"
            );
        }
        if (colorAccessor.type == fastgltf::AccessorType::Vec3)
        {
            fastgltf::iterateAccessorWithIndex<glm::vec3>(
                asset,
                colorAccessor,
                [&](glm::vec3 value, std::size_t index){
                    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
                    {
                        failPrimitive(
                            assetPath,
                            meshIndex,
                            primitiveIndex,
                            "COLOR_0",
                            colorAccessorIndex,
                            "COLOR_0 accessor contains non-finite values"
                        );
                    }
                    result.vertices[index].color = glm::vec4(value, 1.0f);
                }
            );
        }
        else
        {
            fastgltf::iterateAccessorWithIndex<glm::vec4>(
                asset,
                colorAccessor,
                [&](glm::vec4 value, std::size_t index){
                    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z) || !std::isfinite(value.w))
                    {
                        failPrimitive(
                            assetPath,
                            meshIndex,
                            primitiveIndex,
                            "COLOR_0",
                            colorAccessorIndex,
                            "COLOR_0 accessor contains non-finite values"
                        );
                    }
                    result.vertices[index].color = value;
                }
            );
        }
        result.hasColor0 = true;
    }

    const auto tangentIt = primitive.findAttribute("TANGENT");
    // 如果资产中没有提供normal，不使用资产的tangent
    if (result.normalsFromAsset && tangentIt != primitive.attributes.cend())
    {
        const std::size_t tangentAccessorIndex = tangentIt->accessorIndex;
        const fastgltf::Accessor& tangentAccessor =
            checkedAccessor(
                asset,
                assetPath,
                meshIndex,
                primitiveIndex,
                "TANGENT",
                tangentAccessorIndex
            );
        if (tangentAccessor.type != fastgltf::AccessorType::Vec4)
        {
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "TANGENT",
                tangentAccessorIndex,
                "TANGENT accessor must have type VEC4"
            );
        }

        if (tangentAccessor.componentType != fastgltf::ComponentType::Float)
        {
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "TANGENT",
                tangentAccessorIndex,
                "TANGENT accessor must have component type FLOAT"
            );
        }

        if (tangentAccessor.normalized)
        {
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "TANGENT",
                tangentAccessorIndex,
                "TANGENT accessor must not be normalized"
            );
        }

        if (tangentAccessor.count != positionAccessor.count)
        {
            std::ostringstream reason;
            reason << "TANGENT accessor count (" << tangentAccessor.count << ")"
                   << " must match POSITION accessor count (" << positionAccessor.count << ")";
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "TANGENT",
                tangentAccessorIndex,
                reason.str()
            );
        }

        fastgltf::iterateAccessorWithIndex<glm::vec4>(
            asset,
            tangentAccessor,
            [&](glm::vec4 value, std::size_t index)
            {
                if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z) || !std::isfinite(value.w))
                {
                    failPrimitive(
                        assetPath,
                        meshIndex,
                        primitiveIndex,
                        "TANGENT",
                        tangentAccessorIndex,
                        "TANGENT accessor contains non-finite values"
                    );
                }
                const glm::vec3 tangentDirection{value.x, value.y, value.z};
                const float lengthSquared = glm::dot(tangentDirection, tangentDirection);
                if (!std::isfinite(lengthSquared) || lengthSquared < 1e-6f)
                {
                    std::ostringstream reason;
                    reason << "TANGENT element[" << index << "] has a zero-length or non-finite tangent vector: " << tangentDirection.x << ", " << tangentDirection.y << ", " << tangentDirection.z;
                    failPrimitive(
                        assetPath,
                        meshIndex,
                        primitiveIndex,
                        "TANGENT",
                        tangentAccessorIndex,
                        reason.str()
                    );
                }

                const bool validHandedness = value.w == 1.0f || value.w == -1.0f;
                if (!validHandedness)
                {
                    std::ostringstream reason;
                    reason << "TANGENT element[" << index << "] has w=" << value.w << "; expected +1 or -1";
                    failPrimitive(
                        assetPath,
                        meshIndex,
                        primitiveIndex,
                        "TANGENT",
                        tangentAccessorIndex,
                        reason.str()
                    );
                }
                result.vertices[index].tangent = value;
            }
        );
        result.hasTangents = true;
    }
    // index解码
    std::optional<std::size_t> indexAccessorIndex;
    if (primitive.indicesAccessor)
    {
        indexAccessorIndex = *primitive.indicesAccessor;
        const fastgltf::Accessor& indexAccessor =
            checkedAccessor(
                asset,
                assetPath,
                meshIndex,
                primitiveIndex,
                "indices",
                *indexAccessorIndex
            );
        if (indexAccessor.type != fastgltf::AccessorType::Scalar)
        {
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "indices",
                *indexAccessorIndex,
                "indices accessor must be of type SCALAR"
            );
        }
        const bool supportedComponentType =
            indexAccessor.componentType == fastgltf::ComponentType::UnsignedByte ||
            indexAccessor.componentType == fastgltf::ComponentType::UnsignedShort ||
            indexAccessor.componentType == fastgltf::ComponentType::UnsignedInt;
        if (!supportedComponentType)
        {
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "indices",
                *indexAccessorIndex,
                "indices accessor must have component type UNSIGNED_BYTE, UNSIGNED_SHORT, or UNSIGNED_INT"
            );
        }
        if (indexAccessor.normalized)
        {
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "indices",
                *indexAccessorIndex,
                "indices accessor must not be normalized"
            );
        }

        if (indexAccessor.count == 0)
        {
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "indices",
                *indexAccessorIndex,
                "indices accessor must have at least one element"
            );
        }

        result.indices.resize(indexAccessor.count);
        fastgltf::iterateAccessorWithIndex<std::uint32_t>(
            asset,
            indexAccessor,
            [&](std::uint32_t value, std::size_t index)
            {
                result.indices[index] = value;
            }
        );
    }
    else
    {
        // 自动生成 indices
        const std::size_t maxGeneratedIndex = positionAccessor.count - 1;
        if (maxGeneratedIndex > std::numeric_limits<std::uint32_t>::max())
        {
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "indices",
                std::nullopt,
                "primitive has no indices and POSITION accessor count exceeds maximum index value"
            );
        }
        result.indices.resize(positionAccessor.count);
        std::iota(result.indices.begin(), result.indices.end(), std::uint32_t{0});
    }

    // 检查index数量
    if (result.indices.size() % 3 != 0)
    {
        std::ostringstream reason;
        reason << "triangle primitive index count must be divisible by three, got " << result.indices.size();
        failPrimitive(
            assetPath,
            meshIndex,
            primitiveIndex,
            "indices",
            indexAccessorIndex,
            reason.str()
        );
    }

    // 检查每个 index 是否越界
    for (std::size_t indexElement = 0; indexElement < result.indices.size(); ++indexElement)
    {
        const std::uint32_t indexValue = result.indices[indexElement];
        if (indexValue >= result.vertices.size())
        {
            std::ostringstream reason;
            reason << "index element[" << indexElement << "] has value "
                   << indexValue << " but only " << result.vertices.size() << " vertices available";
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "indices",
                indexAccessorIndex,
                reason.str()
            );
        }
    }

    // 自定义计算normal，使用面normal作为vertex normal
    if (!result.normalsFromAsset)
    {
        generateFlatNormals(
            result,
            assetPath,
            meshIndex,
            primitiveIndex,
            indexAccessorIndex
        );
    }

    // 计算mesh primitive边界
    calculateLocalBounds(
        result,
        assetPath,
        meshIndex,
        primitiveIndex,
        positionAccessorIndex
    );

    // 验证并保存material
    if (primitive.materialIndex)
    {
        const std::size_t materialIndex = *primitive.materialIndex;
        if (materialIndex >= asset.materials.size())
        {
            std::ostringstream reason;
            reason << "material index " << materialIndex << " is out of range for " << asset.materials.size() << " materials";
            failPrimitive(
                assetPath,
                meshIndex,
                primitiveIndex,
                "materialIndex",
                std::nullopt,
                reason.str()
            );
        }
        result.materialIndex = materialIndex;
    }


    return result;
}

}

GltfImportData loadGltfCpuData(const std::filesystem::path &path)
{
    const std::filesystem::path normalizedPath = std::filesystem::absolute(path).lexically_normal();
    auto data = fastgltf::GltfDataBuffer::FromPath(normalizedPath);
    if (data.error() != fastgltf::Error::None)
    {
        throw std::runtime_error(
            normalizedPath.string() + ": read failed: " +
            std::string(fastgltf::getErrorName(data.error())) + ": " +
            std::string(fastgltf::getErrorMessage(data.error()))
        );
    }

    fastgltf::Parser parser;
    auto loaded = parser.loadGltf(data.get(), normalizedPath.parent_path(), fastgltf::Options::LoadExternalBuffers);
    if (loaded.error() != fastgltf::Error::None)
    {
        throw std::runtime_error(
            normalizedPath.string() + ": parse failed: " +
            std::string(fastgltf::getErrorName(loaded.error())) + ": " +
            std::string(fastgltf::getErrorMessage(loaded.error()))
        );
    }

    const fastgltf::Asset& asset = loaded.get();
    const fastgltf::Error validationError = fastgltf::validate(asset);
    if (validationError != fastgltf::Error::None)
    {
        throw std::runtime_error(
            normalizedPath.string() + ": validation failed: " +
            std::string(fastgltf::getErrorName(validationError)) + ": " +
            std::string(fastgltf::getErrorMessage(validationError))
        );
    }

    GltfImportData result;
    result.sourcePath = normalizedPath;
    result.materialCount = asset.materials.size();
    result.imageCount = asset.images.size();
    result.textureCount = asset.textures.size();
    result.bufferCount = asset.buffers.size();
    result.accessorCount = asset.accessors.size();
    if (asset.defaultScene)
    {
        result.defaultSceneIndex = *asset.defaultScene;
    }
    result.scenes.reserve(asset.scenes.size());
    for (const fastgltf::Scene& scene : asset.scenes)
    {
        GltfSceneSummary summary;
        summary.name.assign(scene.name.data(), scene.name.size());
        summary.rootNodeIndices.assign(scene.nodeIndices.begin(), scene.nodeIndices.end());
        result.scenes.push_back(std::move(summary));
    }
    result.nodes.reserve(asset.nodes.size());
    for (const fastgltf::Node& node : asset.nodes)
    {
        GltfNodeSummary summary;
        summary.name.assign(node.name.data(), node.name.size());
        if (node.meshIndex)
        {
            summary.meshIndex = *node.meshIndex;
        }
        summary.children.assign(node.children.begin(), node.children.end());
        result.nodes.push_back(std::move(summary));
    }
    result.meshes.reserve(asset.meshes.size());
    for (std::size_t meshIndex = 0; meshIndex < asset.meshes.size(); ++meshIndex)
    {
        const fastgltf::Mesh& sourceMesh = asset.meshes[meshIndex];
        GltfMeshData meshData;
        meshData.name.assign(sourceMesh.name.data(), sourceMesh.name.size());
        meshData.primitiveIndices.reserve(sourceMesh.primitives.size());
        for (std::size_t primitiveIndex = 0; primitiveIndex < sourceMesh.primitives.size(); ++primitiveIndex)
        {
            GltfPrimitiveData primitiveData = decodePrimitive(
                asset,
                normalizedPath,
                meshIndex,
                primitiveIndex,
                sourceMesh.primitives[primitiveIndex]
            );
            meshData.primitiveIndices.push_back(result.primitives.size());
            result.primitives.push_back(std::move(primitiveData));
        }
        result.meshes.push_back(std::move(meshData));
    }


    return result;
}
