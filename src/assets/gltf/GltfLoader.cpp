#include "assets/gltf/GltfLoader.hpp"
#include "assets/gltf/GltfImportInternal.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace
{
// 负责保证后面的 importer 只会处理“当前渲染器真的支持”的 glTF 子集，
// 避免把不支持的动画、骨骼和 morph target 静默加载成错误结果。
void validateSupportedAssetFeatures(const fastgltf::Asset& asset, const std::filesystem::path& assetPath)
{
    const std::string source = assetPath.u8string();

    // 当前只接受静态资产，不自动忽略动画。
    if (!asset.animations.empty())
    {
        throw std::runtime_error(source + ": animation[0]: animations are not supported");
    }

    for (std::size_t nodeIndex = 0; nodeIndex < asset.nodes.size(); ++nodeIndex)
    {
        const fastgltf::Node& node = asset.nodes[nodeIndex];

        if (node.skinIndex)
        {
            const std::string nodeName(node.name.data(), node.name.size());

            throw std::runtime_error(source + ": node[" + std::to_string(nodeIndex) + "] " + nodeName +
                                     ": skinning is not supported");
        }
    }

    for (std::size_t meshIndex = 0; meshIndex < asset.meshes.size(); ++meshIndex)
    {
        const auto& mesh = asset.meshes[meshIndex];

        for (std::size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex)
        {
            const auto& primitive = mesh.primitives[primitiveIndex];

            if (!primitive.targets.empty())
            {
                throw std::runtime_error(source + ": mesh[" + std::to_string(meshIndex) + "] primitive[" +
                                         std::to_string(primitiveIndex) + "]: morph targets are not supported");
            }
        }
    }
}

// 保留节点局部变换和场景根索引；层级实例化由场景层处理。
void decodeSceneHierarchy(const fastgltf::Asset& asset, GltfImportData& result)
{
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

        const auto localMatrix = fastgltf::getTransformMatrix(node);
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                summary.localTransform[column][row] = localMatrix[column][row];
            }
        }

        result.nodes.push_back(std::move(summary));
    }
}
} // namespace

GltfImportData loadGltfCpuData(const std::filesystem::path& path)
{
    const std::filesystem::path normalizedPath = std::filesystem::absolute(path).lexically_normal();
    auto data = fastgltf::GltfDataBuffer::FromPath(normalizedPath);
    if (data.error() != fastgltf::Error::None)
    {
        throw std::runtime_error(normalizedPath.u8string() +
                                 ": read failed: " + std::string(fastgltf::getErrorName(data.error())) + ": " +
                                 std::string(fastgltf::getErrorMessage(data.error())));
    }

    fastgltf::Parser parser;
    const fastgltf::Options options = fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages;
    auto loaded = parser.loadGltf(data.get(), normalizedPath.parent_path(), options);
    if (loaded.error() != fastgltf::Error::None)
    {
        throw std::runtime_error(normalizedPath.u8string() +
                                 ": parse failed: " + std::string(fastgltf::getErrorName(loaded.error())) + ": " +
                                 std::string(fastgltf::getErrorMessage(loaded.error())));
    }

    const fastgltf::Asset& asset = loaded.get();
    const fastgltf::Error validationError = fastgltf::validate(asset);
    if (validationError != fastgltf::Error::None)
    {
        throw std::runtime_error(normalizedPath.u8string() +
                                 ": validation failed: " + std::string(fastgltf::getErrorName(validationError)) + ": " +
                                 std::string(fastgltf::getErrorMessage(validationError)));
    }

    validateSupportedAssetFeatures(asset, normalizedPath);
    GltfImportData result;
    result.sourcePath = normalizedPath;
    result.materialCount = asset.materials.size();
    result.textureCount = asset.textures.size();
    result.bufferCount = asset.buffers.size();
    result.accessorCount = asset.accessors.size();

    gltf_import_detail::decodeImages(asset, normalizedPath, result);
    gltf_import_detail::decodeMaterials(asset, normalizedPath, result);
    decodeSceneHierarchy(asset, result);
    gltf_import_detail::decodeMeshes(asset, normalizedPath, result);

    return result;
}
