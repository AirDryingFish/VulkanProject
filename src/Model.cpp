#include "TriangleApplication.hpp"

#include <tiny_obj_loader.h>

#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <filesystem>
#include <memory>

namespace
{
constexpr float pi = 3.14159265359f;

std::string fileNameFromPath(const std::string &path)
{
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
    {
        return path;
    }
    return path.substr(slash + 1);
}

std::string makeMeshCacheKey(MeshSource source, const std::string& path)
{
    if (source == MeshSource::Cube)
    {
        return "builtin:cubd";
    }
    if (source == MeshSource::Sphere)
    {
        return "builtin:sphere";
    }

    const std::filesystem::path sourcePath = path.empty() ? std::filesystem::path(MODEL_PATH) : std::filesystem::path(path);
    const std::filesystem::path normalizedPath = std::filesystem::absolute(sourcePath).lexically_normal();

    return "obj:" + normalizedPath.generic_string();
}
}

MeshBuildData TriangleApplication::buildMeshData(MeshSource source, const std::string &path)
{
    MeshBuildData meshData{};
    auto computeBounds = [&]() {
        if (meshData.vertices.empty())
        {
            meshData.boundsValid = false;
            return;
        }

        meshData.boundsMin = meshData.vertices[0].pos;
        meshData.boundsMax = meshData.vertices[0].pos;
        for (const Vertex &vertex : meshData.vertices)
        {
            meshData.boundsMin = glm::min(meshData.boundsMin, vertex.pos);
            meshData.boundsMax = glm::max(meshData.boundsMax, vertex.pos);
        }

        meshData.boundsValid = true;
    };

    if (source == MeshSource::Cube)
    {
        const glm::vec3 positions[8] = {
            {-1.0f, -1.0f, -1.0f},
            {1.0f, -1.0f, -1.0f},
            {1.0f, 1.0f, -1.0f},
            {-1.0f, 1.0f, -1.0f},
            {-1.0f, -1.0f, 1.0f},
            {1.0f, -1.0f, 1.0f},
            {1.0f, 1.0f, 1.0f},
            {-1.0f, 1.0f, 1.0f},
        };

        auto addFace = [&](int a, int b, int c, int d, const glm::vec3 &normal) {
            const uint32_t start = static_cast<uint32_t>(meshData.vertices.size());
            meshData.vertices.push_back({positions[a], glm::vec3(1.0f), glm::vec2(0.0f, 0.0f), normal});
            meshData.vertices.push_back({positions[b], glm::vec3(1.0f), glm::vec2(1.0f, 0.0f), normal});
            meshData.vertices.push_back({positions[c], glm::vec3(1.0f), glm::vec2(1.0f, 1.0f), normal});
            meshData.vertices.push_back({positions[d], glm::vec3(1.0f), glm::vec2(0.0f, 1.0f), normal});
            meshData.indices.insert(meshData.indices.end(), {start, start + 1, start + 2, start + 2, start + 3, start});
        };

        addFace(1, 5, 6, 2, {1.0f, 0.0f, 0.0f});
        addFace(4, 0, 3, 7, {-1.0f, 0.0f, 0.0f});
        addFace(2, 6, 7, 3, {0.0f, 1.0f, 0.0f});
        addFace(0, 4, 5, 1, {0.0f, -1.0f, 0.0f});
        addFace(5, 4, 7, 6, {0.0f, 0.0f, 1.0f});
        addFace(0, 1, 2, 3, {0.0f, 0.0f, -1.0f});

        computeBounds();
        return meshData;
    }

    if (source == MeshSource::Sphere)
    {
        constexpr uint32_t segments = 64;
        constexpr uint32_t rings = 32;

        for (uint32_t y = 0; y <= rings; y++)
        {
            const float v = static_cast<float>(y) / static_cast<float>(rings);
            const float phi = v * pi;
            const float z = std::cos(phi);
            const float ringRadius = std::sin(phi);

            for (uint32_t x = 0; x <= segments; x++)
            {
                const float u = static_cast<float>(x) / static_cast<float>(segments);
                const float theta = u * pi * 2.0f;

                const glm::vec3 normal(
                    ringRadius * std::cos(theta),
                    ringRadius * std::sin(theta),
                    z);

                Vertex vertex{};
                vertex.pos = normal;
                vertex.color = glm::vec3(1.0f);
                vertex.texcoord = glm::vec2(u, v);
                vertex.normal = glm::normalize(normal);
                meshData.vertices.push_back(vertex);
            }
        }

        for (uint32_t y = 0; y < rings; y++)
        {
            for (uint32_t x = 0; x < segments; x++)
            {
                const uint32_t i0 = y * (segments + 1) + x;
                const uint32_t i1 = i0 + 1;
                const uint32_t i2 = i0 + segments + 1;
                const uint32_t i3 = i2 + 1;

                meshData.indices.insert(meshData.indices.end(), {i0, i2, i1, i1, i2, i3});
            }
        }

        computeBounds();
        return meshData;
    }

    const std::string objPath = path.empty() ? MODEL_PATH : path;
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, objPath.c_str()))
    {
        throw std::runtime_error(err.empty() ? "failed to load obj model: " + objPath : err);
    }

    std::unordered_map<Vertex, uint32_t> uniqueVertices{};
    bool needsGeneratedNormals = attrib.normals.empty();

    for (const auto &shape : shapes)
    {
        for (const auto &index : shape.mesh.indices)
        {
            Vertex vertex{};
            vertex.pos = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]};

            if (index.texcoord_index >= 0)
            {
                vertex.texcoord = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]};
            }

            vertex.color = {1.0f, 1.0f, 1.0f};
            if (index.normal_index >= 0)
            {
                vertex.normal = {
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]};
            }
            else
            {
                needsGeneratedNormals = true;
            }

            if (uniqueVertices.count(vertex) == 0)
            {
                uniqueVertices[vertex] = static_cast<uint32_t>(meshData.vertices.size());
                meshData.vertices.push_back(vertex);
            }
            meshData.indices.push_back(uniqueVertices[vertex]);
        }
    }

    if (needsGeneratedNormals)
    {
        for (Vertex &vertex : meshData.vertices)
        {
            vertex.normal = glm::vec3(0.0f);
        }

        for (size_t i = 0; i + 2 < meshData.indices.size(); i += 3)
        {
            Vertex &v0 = meshData.vertices[meshData.indices[i + 0]];
            Vertex &v1 = meshData.vertices[meshData.indices[i + 1]];
            Vertex &v2 = meshData.vertices[meshData.indices[i + 2]];

            glm::vec3 edge1 = v1.pos - v0.pos;
            glm::vec3 edge2 = v2.pos - v0.pos;
            glm::vec3 normal = glm::cross(edge1, edge2);
            if (glm::length(normal) > 0.0001f)
            {
                normal = glm::normalize(normal);
                v0.normal += normal;
                v1.normal += normal;
                v2.normal += normal;
            }
        }

        for (Vertex &vertex : meshData.vertices)
        {
            if (glm::length(vertex.normal) > 0.0001f)
            {
                vertex.normal = glm::normalize(vertex.normal);
            }
            else
            {
                vertex.normal = glm::vec3(0.0f, 0.0f, 1.0f);
            }
        }
    }

    computeBounds();
    return meshData;
}

void TriangleApplication::addMeshObject(MeshSource source, const std::string &path)
{
    const std::string objPath = source == MeshSource::Obj ? (path.empty() ? MODEL_PATH : path) : std::string();
    SceneObject object{};
    if (source == MeshSource::Cube)
    {
        object.name = "Cube " + std::to_string(sceneObjects.size() + 1);
    }
    else if (source == MeshSource::Sphere)
    {
        object.name = "Sphere " + std::to_string(sceneObjects.size() + 1);
    }
    else
    {
        const std::string objName = fileNameFromPath(objPath.empty() ? MODEL_PATH : objPath);
        object.name = objName.empty() ? "OBJ " + std::to_string(sceneObjects.size() + 1) : objName;
    }
    object.source = source;
    object.sourcePath = objPath;
    object.mesh = getOrCreateMesh(source, path);
    sceneObjects.push_back(std::move(object));

    selectedSceneObjectIndex = static_cast<int>(sceneObjects.size()) - 1;
    selectedObject = SceneSelection::Model;
    selectedModel = true;
    selectedPointLightIndex = -1;
}

MeshHandle TriangleApplication::getOrCreateMesh(MeshSource source, const std::string &path)
{
    const std::string key = makeMeshCacheKey(source, path);
    const auto cacheIt = meshCache.find(key);
    if (cacheIt != meshCache.end())
    {
        if (MeshHandle existingMesh = cacheIt->second.lock())
        {
            return existingMesh;
        }
        meshCache.erase(cacheIt);
    }

    MeshBuildData meshData = buildMeshData(source, path);
    Mesh uploadedMesh = createMesh(meshData);
    MeshHandle mesh = std::make_shared<Mesh>(std::move(uploadedMesh));
    meshCache[key] = mesh;
    return mesh;
}
