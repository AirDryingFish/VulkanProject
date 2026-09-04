#include "GltfLoader.hpp"

#include <cstddef>
#include <exception>
#include <iostream>

// 匿名 namespace：让里面的函数、变量、类型只在当前.cpp文件内部可见
// 假如A.cpp和B.cpp中都有printVec3，不放namespace中链接就会报错
namespace
{
void printVec3(const glm::vec3& value)
{
    std::cout << "(" <<
        value.x << ", " <<
        value.y << ", " <<
        value.z << ")";
}

const char* yesNo(bool value)
{
    return value ? "yes" : "no";
}

}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: gltf_inspect <asset.gltf|asset.glb>\n";
        return 2;
    }

    try
    {
        const GltfImportData imported = loadGltfCpuData(argv[1]);
        std::cout << "file: " << imported.sourcePath.string() << "\n";
        std::cout << "scenes: " << imported.scenes.size() << "\n";
        std::cout << "nodes: " << imported.nodes.size() << "\n";
        std::cout << "meshes: " << imported.meshes.size() << "\n";
        std::cout << "primitives: " << imported.primitives.size() << "\n";
        std::cout << "materials: " << imported.materialCount << "\n";
        std::cout << "images: " << imported.images.size() << "\n";
        std::cout << "textures: " << imported.textureCount << "\n";
        std::cout << "buffers: " << imported.bufferCount << "\n";
        std::cout << "accessors: " << imported.accessorCount << "\n";

        std::cout << "default scene: ";
        if (imported.defaultSceneIndex)
        {
            std::cout << *imported.defaultSceneIndex << "\n";
        }
        else
        {
            std::cout << "none\n";
        }

        for (std::size_t imageIndex = 0; imageIndex < imported.images.size(); ++imageIndex)
        {
            const DecodedImageData& image = imported.images[imageIndex];
            std:: cout << "image[" << imageIndex << "] name=\"" << image.name << "\" size=" <<
                        image.width << "x" << image.height << " rgba8-bytes=" << image.rgba8.size() << "\n";
        }

        for (std::size_t nodeIndex = 0; nodeIndex < imported.nodes.size(); ++nodeIndex)
        {
            const GltfNodeSummary& node = imported.nodes[nodeIndex];
            std::cout << "node[" << nodeIndex << "] name=\"";
            if (node.name.empty())
            {
                std::cout << "<unnamed>";
            }
            else
            {
                std::cout << node.name;
            }

            std::cout << "\" mesh=";
            if (node.meshIndex)
            {
                std::cout << *node.meshIndex;
            }
            else
            {
                std::cout << "none";
            }

            std::cout << " children=[";
            for (std::size_t childIndex = 0; childIndex < node.children.size(); ++childIndex)
            {
                if (childIndex > 0)
                {
                    std::cout << ", ";
                }
                std::cout << node.children[childIndex];
            }
            std::cout << "]\n";
        }

        for (std::size_t meshIndex = 0; meshIndex < imported.meshes.size(); ++meshIndex)
        {
            const GltfMeshData& mesh = imported.meshes[meshIndex];
            for (std::size_t primitiveIndex = 0; primitiveIndex < mesh.primitiveIndices.size(); ++primitiveIndex)
            {
                const std::size_t decodedPrimitiveIndex = mesh.primitiveIndices[primitiveIndex];
                const GltfPrimitiveData& primitive = imported.primitives.at(decodedPrimitiveIndex);
                // primitive有几套uv坐标。第一套可能给 base color / normal map 用，第二套可能给 lightmap、AO 或其他纹理使用。
                const unsigned int uvSetCount = static_cast<unsigned int>(primitive.hasTexcoord0) + static_cast<unsigned int>(primitive.hasTexcoord1);
                std::cout << "mesh " << meshIndex << " primitive " << primitiveIndex << "\n";
                std::cout << "  vertices: " << primitive.vertices.size() << "\n";
                std::cout << "  indices: " << primitive.indices.size() << "\n";
                std::cout << "  normals: " << (primitive.normalsFromAsset ? "from asset" : "generated") << "\n";
                std::cout << "  uv sets: " << uvSetCount << "\n";
                std::cout << "  color0: " << yesNo(primitive.hasColor0) << "\n";
                std::cout << "  tangents: " << yesNo(primitive.hasTangents) << "\n";
                std::cout << "  material: ";
                if (primitive.materialIndex)
                {
                    std::cout << *primitive.materialIndex << "\n";
                }
                else
                {
                    std::cout << "none\n";
                }
                std::cout << "  bounds min: ";
                printVec3(primitive.boundsMin);
                std::cout << "\n";
                std::cout << "  bounds max: ";
                printVec3(primitive.boundsMax);
                std::cout << "\n";
            }
        }
        return 0;
    }
    catch(const std::exception& e)
    {
        std::cerr << "import failed: " << e.what() << '\n';
        return 1;
    }

}
