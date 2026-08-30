#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>

#include <exception>
#include <filesystem>
#include <iostream>

// cmd: ./gltf_inspect assets/DamageHelmet.glb
// argc = 2     argv[0] = "./gltf_inspect"      argv[1] = "assets/DamageHelmet.glb"
int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: gltf_inspect <asset.gltf|asset.glb>\n";
        return 2;
    }

    try
    {
        // ::absolute变为绝对路径，.lexically_normal把路径里的. ..整理掉
        const std::filesystem::path path = std::filesystem::absolute(argv[1]).lexically_normal();
        auto data = fastgltf::GltfDataBuffer::FromPath(path);
        if (data.error() != fastgltf::Error::None) // fastgltf::Error::None 表示没有错误
        {
            std::cerr << "read failed: " << fastgltf::getErrorMessage(data.error()) << "\n";
            return 1;
        }
        
        fastgltf::Parser parser;

        auto loaded = parser.loadGltf(data.get(), path.parent_path(), fastgltf::Options::None);
        if (loaded.error() != fastgltf::Error::None)
        {
            std::cerr << "parse failed: " << fastgltf::getErrorMessage(loaded.error()) << "\n";
            return 1;
        }

        const fastgltf::Asset& asset = loaded.get();
        const fastgltf::Error validationError = fastgltf::validate(asset);
        if (validationError != fastgltf::Error::None)
        {
            std::cerr << "validation failed: " << fastgltf::getErrorMessage(validationError) << "\n";
            return 1;
        }

        std::size_t primitiveCount = 0;
        for (const fastgltf::Mesh& mesh : asset.meshes)
        {
            primitiveCount += mesh.primitives.size();
        }

        std::cout << "file: " << path.string() << "\n";
        std::cout << "scenes: " << asset.scenes.size() << '\n';
        std::cout << "nodes: " << asset.nodes.size() << '\n';
        std::cout << "meshes: " << asset.meshes.size() << '\n';
        std::cout << "primitives: " << primitiveCount << '\n';
        std::cout << "materials: " << asset.materials.size() << '\n';
        std::cout << "images: " << asset.images.size() << '\n';
        std::cout << "textures: " << asset.textures.size() << '\n';
        std::cout << "buffers: " << asset.buffers.size() << '\n';
        std::cout << "accessors: " << asset.accessors.size() << '\n';

        std::cout << "default scene: ";
        if (asset.defaultScene.has_value())
        {
            std::cout << *asset.defaultScene << "\n";
        }
        else
        {
            std::cout << "none\n";
        }

        for (size_t nodeIndex = 0; nodeIndex < asset.nodes.size(); ++nodeIndex)
        {
            const fastgltf::Node& node = asset.nodes[nodeIndex];
            std::cout << "node[" << nodeIndex << "]" << " name=\"" << (node.name.empty() ? "<unnamed>" : node.name) << "\" mesh=";
            if (node.meshIndex.has_value())
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
        return 0;
    }
    catch(const std::exception& e)
    {
        std::cerr << "unexpected error: " << e.what() <<'\n';
        return 1;
    }
    
}