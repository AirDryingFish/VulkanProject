#include "GltfLoader.hpp"

#include <glm/geometric.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    constexpr float epsilon = 0.00001f;

    void require(bool condition, const std::string &message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void requireNear(
        float actual,
        float expected,
        const std::string &label)
    {
        if (!std::isfinite(actual) ||
            std::fabs(actual - expected) > epsilon)
        {
            std::ostringstream message;
            message
                << label
                << ": expected "
                << expected
                << ", got "
                << actual;

            throw std::runtime_error(message.str());
        }
    }

    void requireVec2(
        const glm::vec2 &actual,
        const glm::vec2 &expected,
        const std::string &label)
    {
        requireNear(actual.x, expected.x, label + ".x");
        requireNear(actual.y, expected.y, label + ".y");
    }

    void requireVec3(
        const glm::vec3 &actual,
        const glm::vec3 &expected,
        const std::string &label)
    {
        requireNear(actual.x, expected.x, label + ".x");
        requireNear(actual.y, expected.y, label + ".y");
        requireNear(actual.z, expected.z, label + ".z");
    }

    void requireVec4(
        const glm::vec4 &actual,
        const glm::vec4 &expected,
        const std::string &label)
    {
        requireNear(actual.x, expected.x, label + ".x");
        requireNear(actual.y, expected.y, label + ".y");
        requireNear(actual.z, expected.z, label + ".z");
        requireNear(actual.w, expected.w, label + ".w");
    }

    const GltfPrimitiveData &onlyPrimitive(
        const GltfImportData &data,
        const std::string &assetName)
    {
        require(
            data.primitives.size() == 1,
            assetName + ": expected exactly one primitive");

        return data.primitives.front();
    }

    void requireGeneratedPositiveZNormals(
        const GltfPrimitiveData &primitive,
        const std::string &assetName)
    {
        require(
            !primitive.normalsFromAsset,
            assetName + ": normals should be generated");

        for (std::size_t index = 0;
             index < primitive.vertices.size();
             ++index)
        {
            requireVec3(
                primitive.vertices[index].normal,
                glm::vec3(0.0f, 0.0f, 1.0f),
                assetName +
                    ": normal[" +
                    std::to_string(index) +
                    "]");
        }
    }

    void requireDefaultOptionalAttributes(
        const GltfPrimitiveData &primitive,
        const std::string &assetName)
    {
        require(
            !primitive.hasTexcoord0,
            assetName + ": TEXCOORD_0 should be absent");

        require(
            !primitive.hasTexcoord1,
            assetName + ": TEXCOORD_1 should be absent");

        require(
            !primitive.hasColor0,
            assetName + ": COLOR_0 should be absent");

        require(
            !primitive.hasTangents,
            assetName + ": TANGENT should be absent");

        for (std::size_t index = 0;
             index < primitive.vertices.size();
             ++index)
        {
            const GltfDecodedVertex &vertex =
                primitive.vertices[index];

            const std::string vertexLabel =
                assetName +
                ": vertex[" +
                std::to_string(index) +
                "]";

            requireVec4(
                vertex.color,
                glm::vec4(1.0f),
                vertexLabel + ".color");

            requireVec2(
                vertex.texcoord0,
                glm::vec2(0.0f),
                vertexLabel + ".texcoord0");

            requireVec2(
                vertex.texcoord1,
                glm::vec2(0.0f),
                vertexLabel + ".texcoord1");

            requireVec4(
                vertex.tangent,
                glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
                vertexLabel + ".tangent");
        }
    }

    void testTriangle(
        const std::filesystem::path &fixtureRoot)
    {
        const GltfImportData data =
            loadGltfCpuData(
                fixtureRoot /
                "Triangle" /
                "Triangle.gltf");

        require(data.scenes.size() == 1, "Triangle: scene count");
        require(data.nodes.size() == 1, "Triangle: node count");
        require(data.meshes.size() == 1, "Triangle: mesh count");
        require(data.materialCount == 0, "Triangle: material count");
        require(data.imageCount == 0, "Triangle: image count");
        require(data.textureCount == 0, "Triangle: texture count");
        require(data.bufferCount == 1, "Triangle: buffer count");
        require(data.accessorCount == 2, "Triangle: accessor count");

        const GltfPrimitiveData &primitive =
            onlyPrimitive(data, "Triangle");

        require(
            primitive.vertices.size() == 3,
            "Triangle: vertex count");

        require(
            primitive.indices ==
                std::vector<std::uint32_t>{0u, 1u, 2u},
            "Triangle: decoded indices");

        require(
            !primitive.materialIndex.has_value(),
            "Triangle: material should be absent");

        requireVec3(
            primitive.boundsMin,
            glm::vec3(0.0f, 0.0f, 0.0f),
            "Triangle: boundsMin");

        requireVec3(
            primitive.boundsMax,
            glm::vec3(1.0f, 1.0f, 0.0f),
            "Triangle: boundsMax");

        requireGeneratedPositiveZNormals(
            primitive,
            "Triangle");

        requireDefaultOptionalAttributes(
            primitive,
            "Triangle");
    }

    void testTriangleWithoutIndices(
        const std::filesystem::path &fixtureRoot)
    {
        const GltfImportData data =
            loadGltfCpuData(
                fixtureRoot /
                "TriangleWithoutIndices" /
                "TriangleWithoutIndices.gltf");

        require(
            data.accessorCount == 1,
            "TriangleWithoutIndices: accessor count");

        const GltfPrimitiveData &primitive =
            onlyPrimitive(
                data,
                "TriangleWithoutIndices");

        require(
            primitive.vertices.size() == 3,
            "TriangleWithoutIndices: vertex count");

        require(
            primitive.indices ==
                std::vector<std::uint32_t>{0u, 1u, 2u},
            "TriangleWithoutIndices: generated sequential indices");

        requireVec3(
            primitive.boundsMin,
            glm::vec3(0.0f, 0.0f, 0.0f),
            "TriangleWithoutIndices: boundsMin");

        requireVec3(
            primitive.boundsMax,
            glm::vec3(1.0f, 1.0f, 0.0f),
            "TriangleWithoutIndices: boundsMax");

        requireGeneratedPositiveZNormals(
            primitive,
            "TriangleWithoutIndices");

        requireDefaultOptionalAttributes(
            primitive,
            "TriangleWithoutIndices");
    }

    void requireBoxPrimitive(
        const GltfPrimitiveData &primitive,
        const std::string &assetName)
    {
        require(
            primitive.vertices.size() == 24,
            assetName + ": vertex count");

        require(
            primitive.indices.size() == 36,
            assetName + ": index count");

        require(
            primitive.normalsFromAsset,
            assetName + ": normals should come from asset");

        require(
            primitive.materialIndex.has_value(),
            assetName + ": material index should exist");

        require(
            *primitive.materialIndex == 0,
            assetName + ": material index should be zero");

        requireVec3(
            primitive.boundsMin,
            glm::vec3(-0.5f, -0.5f, -0.5f),
            assetName + ": boundsMin");

        requireVec3(
            primitive.boundsMax,
            glm::vec3(0.5f, 0.5f, 0.5f),
            assetName + ": boundsMax");

        for (std::size_t index = 0;
             index < primitive.vertices.size();
             ++index)
        {
            const float normalLength =
                glm::length(
                    primitive.vertices[index].normal);

            requireNear(
                normalLength,
                1.0f,
                assetName +
                    ": normal length[" +
                    std::to_string(index) +
                    "]");
        }

        requireDefaultOptionalAttributes(
            primitive,
            assetName);
    }

    void testBoxAndBoxInterleaved(
        const std::filesystem::path &fixtureRoot)
    {
        const GltfImportData boxData =
            loadGltfCpuData(
                fixtureRoot /
                "Box" /
                "Box.glb");

        const GltfImportData interleavedData =
            loadGltfCpuData(
                fixtureRoot /
                "BoxInterleaved" /
                "BoxInterleaved.gltf");

        require(boxData.scenes.size() == 1, "Box: scene count");
        require(boxData.nodes.size() == 2, "Box: node count");
        require(boxData.meshes.size() == 1, "Box: mesh count");
        require(boxData.materialCount == 1, "Box: material count");
        require(boxData.bufferCount == 1, "Box: buffer count");
        require(boxData.accessorCount == 3, "Box: accessor count");

        require(
            interleavedData.scenes.size() == 1,
            "BoxInterleaved: scene count");

        require(
            interleavedData.nodes.size() == 2,
            "BoxInterleaved: node count");

        require(
            interleavedData.meshes.size() == 1,
            "BoxInterleaved: mesh count");

        require(
            interleavedData.materialCount == 1,
            "BoxInterleaved: material count");

        require(
            interleavedData.bufferCount == 1,
            "BoxInterleaved: buffer count");

        require(
            interleavedData.accessorCount == 3,
            "BoxInterleaved: accessor count");

        const GltfPrimitiveData &box =
            onlyPrimitive(boxData, "Box");

        const GltfPrimitiveData &interleaved =
            onlyPrimitive(
                interleavedData,
                "BoxInterleaved");

        requireBoxPrimitive(box, "Box");
        requireBoxPrimitive(
            interleaved,
            "BoxInterleaved");

        const std::vector<std::uint32_t> expectedIndices{
            0u, 1u, 2u, 3u, 2u, 1u,
            4u, 5u, 6u, 7u, 6u, 5u,
            8u, 9u, 10u, 11u, 10u, 9u,
            12u, 13u, 14u, 15u, 14u, 13u,
            16u, 17u, 18u, 19u, 18u, 17u,
            20u, 21u, 22u, 23u, 22u, 21u};

        require(
            box.indices == expectedIndices,
            "Box: exact decoded indices");

        require(
            interleaved.indices == expectedIndices,
            "BoxInterleaved: exact decoded indices");

        for (std::size_t index = 0;
             index < box.vertices.size();
             ++index)
        {
            requireVec3(
                interleaved.vertices[index].position,
                box.vertices[index].position,
                "BoxInterleaved: position[" +
                    std::to_string(index) +
                    "]");

            requireVec3(
                interleaved.vertices[index].normal,
                box.vertices[index].normal,
                "BoxInterleaved: normal[" +
                    std::to_string(index) +
                    "]");
        }
    }

    void testSimpleSparseAccessor(
        const std::filesystem::path &fixtureRoot)
    {
        const GltfImportData data =
            loadGltfCpuData(
                fixtureRoot /
                "SimpleSparseAccessor" /
                "SimpleSparseAccessor.gltf");

        require(
            data.scenes.size() == 1,
            "SimpleSparseAccessor: scene count");

        require(
            data.nodes.size() == 1,
            "SimpleSparseAccessor: node count");

        require(
            data.meshes.size() == 1,
            "SimpleSparseAccessor: mesh count");

        require(
            data.accessorCount == 2,
            "SimpleSparseAccessor: accessor count");

        const GltfPrimitiveData &primitive =
            onlyPrimitive(
                data,
                "SimpleSparseAccessor");

        // 原始 POSITION 有 14 个值。
        // 因为缺失 normal，flat normal 生成阶段按三角形拆点，
        // 最终得到 36 个顶点和 36 个顺序 index。
        require(
            primitive.vertices.size() == 36,
            "SimpleSparseAccessor: split vertex count");

        require(
            primitive.indices.size() == 36,
            "SimpleSparseAccessor: index count");

        for (std::size_t index = 0;
             index < primitive.indices.size();
             ++index)
        {
            require(
                primitive.indices[index] ==
                    static_cast<std::uint32_t>(index),
                "SimpleSparseAccessor: sequential index[" +
                    std::to_string(index) +
                    "]");
        }

        requireVec3(
            primitive.boundsMin,
            glm::vec3(0.0f, 0.0f, 0.0f),
            "SimpleSparseAccessor: boundsMin");

        requireVec3(
            primitive.boundsMax,
            glm::vec3(6.0f, 4.0f, 0.0f),
            "SimpleSparseAccessor: boundsMax");

        requireGeneratedPositiveZNormals(
            primitive,
            "SimpleSparseAccessor");

        requireDefaultOptionalAttributes(
            primitive,
            "SimpleSparseAccessor");
    }

    template <typename TestFunction>
    void runTest(
        const std::string &name,
        TestFunction testFunction)
    {
        testFunction();
        std::cout << "[PASS] " << name << '\n';
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr
            << "usage: gltf_loader_tests "
            << "<fixture-root>\n";

        return 2;
    }

    try
    {
        const std::filesystem::path fixtureRoot =
            argv[1];

        require(
            std::filesystem::is_directory(
                fixtureRoot),
            "fixture root is not a directory: " +
                fixtureRoot.string());

        runTest(
            "Triangle",
            [&]()
            {
                testTriangle(fixtureRoot);
            });

        runTest(
            "TriangleWithoutIndices",
            [&]()
            {
                testTriangleWithoutIndices(
                    fixtureRoot);
            });

        runTest(
            "Box and BoxInterleaved",
            [&]()
            {
                testBoxAndBoxInterleaved(
                    fixtureRoot);
            });

        runTest(
            "SimpleSparseAccessor",
            [&]()
            {
                testSimpleSparseAccessor(
                    fixtureRoot);
            });

        std::cout
            << "All glTF loader tests passed.\n";

        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr
            << "[FAIL] "
            << exception.what()
            << '\n';

        return 1;
    }
}