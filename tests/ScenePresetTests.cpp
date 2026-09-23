#include "GltfLoader.hpp"
#include "ScenePresets.hpp"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

namespace
{
    void require(bool condition, const std::string& message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void testRegistry()
    {
        std::set<std::string> ids;
        std::set<ScenePreset> presets;
        for (const ScenePresetInfo& info : scenePresets)
        {
            require(info.id != nullptr && info.id[0] != '\0', "empty preset ID");
            const std::string id = info.id;
            require(ids.insert(id).second, id + ": duplicate ID");
            require(presets.insert(info.preset).second, id + ": duplicate enum value");
            require(info.label != nullptr && info.label[0] != '\0', id + ": empty label");
            require(info.description != nullptr && info.description[0] != '\0', id + ": empty description");
            require(info.assetRelativePath != nullptr, id + ": null asset path");
            require(info.unsupportedReason != nullptr, id + ": null unsupported reason");

            const auto parsed = findScenePreset(id);
            require(parsed.has_value() && *parsed == info.preset, id + ": ID lookup failed");
            require(&scenePresetInfo(*parsed) == &info, id + ": enum lookup failed");
        }
        require(!findScenePreset(""), "empty ID must not select a preset");
        require(!findScenePreset("not-a-scene-preset"), "unknown ID must not select a preset");

        bool rejectedUnknownEnum = false;
        try
        {
            (void)scenePresetInfo(static_cast<ScenePreset>(-1));
        }
        catch (const std::invalid_argument&)
        {
            rejectedUnknownEnum = true;
        }
        require(rejectedUnknownEnum, "unknown enum value must be rejected");
    }

    void requireUnsupportedImport(const ScenePresetInfo& info, const std::filesystem::path& path)
    {
        std::string expected;
        switch (info.preset)
        {
        case ScenePreset::Sponza:
            expected = "only OPAQUE materials are supported";
            break;
        case ScenePreset::MetalRoughSpheresNoTextures:
            expected = "double-sided materials are not supported";
            break;
        default:
            throw std::runtime_error(std::string(info.id) + ": missing expected rejection reason in test");
        }

        bool rejected = false;
        try
        {
            (void)loadGltfCpuData(path);
        }
        catch (const std::exception& error)
        {
            const std::string message = error.what();
            require(message.find(expected) != std::string::npos,
                    std::string(info.id) + ": rejected for an unexpected reason: " + message);
            require(message.find(path.filename().string()) != std::string::npos,
                    std::string(info.id) + ": rejection must identify the asset");
            rejected = true;
        }
        require(rejected, std::string(info.id) + ": unsupported scene unexpectedly imported; review preset support status");
    }

    void testAsset(const ScenePresetInfo& info, const std::filesystem::path& assetRoot)
    {
        const std::string id = info.id;
        const std::filesystem::path relative = info.assetRelativePath;
        if (relative.empty())
        {
            require(info.unsupportedReason[0] == '\0', id + ": procedural preset cannot be marked as a blocked asset");
            return;
        }
        require(!relative.is_absolute(), id + ": asset path must be relative to the asset root");
        for (const auto& part : relative)
            require(part != "..", id + ": asset path must not escape the asset root");

        const std::filesystem::path path = (assetRoot / relative).lexically_normal();
        require(std::filesystem::is_regular_file(path), id + ": missing asset " + path.string());
        if (info.unsupportedReason[0] != '\0')
        {
            requireUnsupportedImport(info, path);
            return;
        }

        const GltfImportData imported = loadGltfCpuData(path);
        require(imported.sourcePath.is_absolute(), id + ": imported source path must be absolute");
        require(std::filesystem::equivalent(imported.sourcePath, path), id + ": imported source path mismatch");
        require(!imported.scenes.empty(), id + ": missing scenes");
        require(!imported.nodes.empty(), id + ": missing nodes");
        require(!imported.meshes.empty(), id + ": missing meshes");
        require(!imported.primitives.empty(), id + ": missing primitives");

        for (const auto& mesh : imported.meshes)
        {
            require(!mesh.primitiveIndices.empty(), id + ": mesh has no primitives");
            for (const auto index : mesh.primitiveIndices)
                require(index < imported.primitives.size(), id + ": invalid mesh primitive reference");
        }
        for (const auto& primitive : imported.primitives)
        {
            require(!primitive.vertices.empty() && !primitive.indices.empty(), id + ": empty primitive geometry");
            if (primitive.materialIndex)
                require(*primitive.materialIndex < imported.materials.size(), id + ": invalid material reference");
        }
        for (const auto& texture : imported.textures)
        {
            require(texture.imageIndex < imported.images.size(), id + ": unresolved texture image");
            if (texture.samplerIndex)
                require(*texture.samplerIndex < imported.samplers.size(), id + ": unresolved sampler");
        }
        for (const auto& material : imported.materials)
        {
            const std::optional<GltfMaterialTextureSlot> slots[] = {
                material.baseColorTexture, material.normalTexture,
                material.metallicRoughnessTexture, material.occlusionTexture,
                material.emissiveTexture};
            for (const auto& slot : slots)
                if (slot)
                    require(slot->textureIndex < imported.textures.size(), id + ": unresolved material texture");
        }
        for (const auto& image : imported.images)
        {
            require(image.width > 0 && image.height > 0, id + ": invalid image dimensions");
            const auto expectedBytes = static_cast<std::uint64_t>(image.width) *
                                       static_cast<std::uint64_t>(image.height) * 4u;
            require(image.rgba8.size() == expectedBytes, id + ": incomplete decoded image");
        }
        if (info.preset == ScenePreset::MetalRoughSpheres || info.preset == ScenePreset::DamagedHelmet)
        {
            require(!imported.materials.empty(), id + ": textured reference scene has no materials");
            require(!imported.images.empty() && !imported.textures.empty(), id + ": textured reference scene has no images/textures");
        }
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: scene_preset_tests <asset-root>\n";
        return 1;
    }
    try
    {
        const auto assetRoot = std::filesystem::absolute(argv[1]).lexically_normal();
        require(std::filesystem::is_directory(assetRoot), "asset root does not exist: " + assetRoot.string());
        testRegistry();
        for (const ScenePresetInfo& info : scenePresets)
        {
            testAsset(info, assetRoot);
            std::cout << "PASS: " << info.id << '\n';
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "scene preset test failed: " << error.what() << '\n';
        return 1;
    }
}
