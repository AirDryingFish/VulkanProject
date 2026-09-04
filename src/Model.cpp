#include "TriangleApplication.hpp"
#include "GltfLoader.hpp"

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
        return "builtin:cube";
    }
    if (source == MeshSource::Sphere)
    {
        return "builtin:sphere";
    }
    if (source == MeshSource::Gltf && path.empty())
    {
        throw std::invalid_argument("glTF mesh path must not be empty");
    }

    const std::filesystem::path sourcePath = path.empty() ? std::filesystem::path(MODEL_PATH) : std::filesystem::path(path);
    const std::filesystem::path normalizedPath = std::filesystem::absolute(sourcePath).lexically_normal();

    if (source == MeshSource::Gltf)
    {
        return "gltf:" + normalizedPath.generic_string() + "#mesh=0/primitive=0";
    }

    return "obj:" + normalizedPath.generic_string();
}

std::string makeGltfPrimitiveCacheKey(
    const std::filesystem::path& sourcePath,
    std::size_t meshIndex,
    std::size_t primitiveIndex
)
{
    // gltf:/.../model.gltf#mesh=0/primitive=0
    // gltf:/.../model.gltf#mesh=0/primitive=1
    return "gltf:" + sourcePath.generic_string() + "#mesh=" + std::to_string(meshIndex) + "/primitive=" + std::to_string(primitiveIndex);
}

MeshBuildData buildGltfPrimitiveMeshData(const GltfPrimitiveData& primitive)
{
    MeshBuildData meshData{};
    meshData.vertices.reserve(primitive.vertices.size());

    for (const GltfDecodedVertex& decodedVertex : primitive.vertices)
    {
        Vertex vertex{};
        vertex.pos = decodedVertex.position;
        vertex.color = glm::vec3(decodedVertex.color);
        vertex.texcoord = decodedVertex.texcoord0;
        vertex.texcoord1 = decodedVertex.texcoord1;
        vertex.normal = decodedVertex.normal;
        vertex.tangent = decodedVertex.tangent;

        meshData.vertices.push_back(vertex);
    }
    meshData.indices = primitive.indices;
    meshData.boundsMin = primitive.boundsMin;
    meshData.boundsMax = primitive.boundsMax;
    meshData.boundsValid = true;
    meshData.hasTangents = primitive.hasTangents;

    return meshData;
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

    if (source == MeshSource::Gltf)
    {
        if (path.empty())
        {
            throw std::invalid_argument("glTF mesh path must not be empty");
        }

        const GltfImportData imported = loadGltfCpuData(path);
        if (imported.meshes.empty())
        {
            throw std::runtime_error(imported.sourcePath.string() + ": asset contains no meshes");
        }
        const GltfMeshData &gltfMesh = imported.meshes.front();
        if (gltfMesh.primitiveIndices.empty())
        {
            throw std::runtime_error(imported.sourcePath.string() + ": mesh[0] contains no primitives");
        }
        const std::size_t decodedPrimitiveIndex = gltfMesh.primitiveIndices.front();
        if (decodedPrimitiveIndex >= imported.primitives.size())
        {
            throw std::logic_error(imported.sourcePath.string() + ": mesh[0] primitive[0] decoded index is out of range");
        }
        return buildGltfPrimitiveMeshData(imported.primitives[decodedPrimitiveIndex]);
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
    std::string meshPath;
    if (source == MeshSource::Obj)
    {
        meshPath = path.empty() ? MODEL_PATH : path;
    }
    else if (source == MeshSource::Gltf)
    {
        meshPath = path;
    }

    SceneObject object{};
    if (source == MeshSource::Cube)
    {
        object.name = "Cube " + std::to_string(sceneObjects.size() + 1);
    }
    else if (source == MeshSource::Sphere)
    {
        object.name = "Sphere " + std::to_string(sceneObjects.size() + 1);
    }
    else if (source == MeshSource::Obj)
    {
        const std::string objName = fileNameFromPath(meshPath);
        object.name = objName.empty() ? "OBJ " + std::to_string(sceneObjects.size() + 1) : objName;
    }
    else if (source == MeshSource::Gltf)
    {
        const std::string gltfName = fileNameFromPath(meshPath);
        object.name = gltfName.empty() ? "glTF " + std::to_string(sceneObjects.size() + 1) : gltfName;
    }
    object.source = source;
    object.sourcePath = meshPath;
    object.mesh = getOrCreateMesh(source, meshPath);
    object.material = defaultMaterial;

    sceneObjects.push_back(std::move(object));

    selectedSceneObjectIndex = static_cast<int>(sceneObjects.size()) - 1;
    selectedObject = SceneSelection::Model;
    selectedModel = true;
    selectedPointLightIndex = -1;
}

void TriangleApplication::addGltfMeshObjects(const std::string &path)
{
    if (path.empty())
    {
        throw std::invalid_argument("glTF path must not be empty");
    }

    const GltfImportData imported = loadGltfCpuData(path);
    std::size_t primitiveCount = 0;
    for (const GltfMeshData& mesh : imported.meshes)
    {
        primitiveCount += mesh.primitiveIndices.size();
    }
    if (primitiveCount == 0)
    {
        throw std::runtime_error(imported.sourcePath.string() + ": asset contains no mesh primitives");
    }

    const std::string normalizedSourcePath = imported.sourcePath.string();
    const std::string assetName = imported.sourcePath.filename().string();

    // 场景中被 node 实例化出来的 primitive 的总数量
    std::size_t instancePrimitiveCount = 0;
    for (std::size_t nodeIndex = 0; nodeIndex < imported.nodes.size(); ++nodeIndex)
    {
        const GltfNodeSummary& node = imported.nodes[nodeIndex];
        if (!node.meshIndex)
        {
            continue;
        }
        const std::size_t meshIndex = *node.meshIndex;
        if (meshIndex >= imported.meshes.size())
        {
            throw std::logic_error(normalizedSourcePath + ": node[" + std::to_string(nodeIndex) + "] mesh index is out of range");
        }
        instancePrimitiveCount += imported.meshes[meshIndex].primitiveIndices.size();
    }

    if (instancePrimitiveCount == 0)
    {
        throw std::runtime_error(normalizedSourcePath + ": asset contains no mesh instances");
    }

    // 导入事务中的临时强引用
    std::unordered_map<std::string, MeshHandle> stagedMeshes;
    std::vector<SceneObject> stagedObjects;
    stagedMeshes.reserve(primitiveCount);
    stagedObjects.reserve(instancePrimitiveCount);

    // gltf decode
    for (std::size_t nodeIndex = 0; nodeIndex < imported.nodes.size(); ++nodeIndex)
    {
        const GltfNodeSummary& node = imported.nodes[nodeIndex];
        if (!node.meshIndex)
        {
            continue;
        }
        const std::size_t meshIndex = *node.meshIndex;
        const GltfMeshData& gltfMesh = imported.meshes[meshIndex];
        const std::string nodeName = node.name.empty() ? "node " + std::to_string(nodeIndex) : node.name;

        for (std::size_t primitiveIndex = 0; primitiveIndex < gltfMesh.primitiveIndices.size(); ++primitiveIndex)
        {
            const std::size_t decodedPrimitiveIndex = gltfMesh.primitiveIndices[primitiveIndex];
            if (decodedPrimitiveIndex >= imported.primitives.size())
            {
                throw std::logic_error(normalizedSourcePath + ": mesh[" + std::to_string(meshIndex) + "] primitive[" + std::to_string(primitiveIndex) + "] decoded primitive index is out if range");
            }
            const std::string key = makeGltfPrimitiveCacheKey(normalizedSourcePath, meshIndex, primitiveIndex);
            MeshHandle mesh;
            //先检查当前导入事务
            const auto stagedIt = stagedMeshes.find(key);
            if (stagedIt != stagedMeshes.end())
            {
                mesh = stagedIt->second;
            }
            else
            {
                // 再检查全局 weak cache
                const auto cacheIt = meshCache.find(key);
                if (cacheIt != meshCache.end())
                {
                    mesh = cacheIt->second.lock();
                }
                if (!mesh)
                {
                    MeshBuildData meshData = buildGltfPrimitiveMeshData(imported.primitives[decodedPrimitiveIndex]);
                    Mesh uploadedMesh = createMesh(meshData);
                    mesh = std::make_shared<Mesh>(std::move(uploadedMesh));
                }
                stagedMeshes.emplace(key, mesh);
            }

            SceneObject object{};
            object.name = assetName + " / " + nodeName + " / primitive " + std::to_string(primitiveIndex);
            object.source = MeshSource::Gltf;
            object.sourcePath = normalizedSourcePath;
            object.mesh = std::move(mesh);
            object.material = defaultMaterial;

            // todo: gltf material 映射

            stagedObjects.push_back(std::move(object));
        }
    }

    auto nextMeshCache = meshCache;
    nextMeshCache.reserve(meshCache.size() + stagedMeshes.size());
    for (const auto& [key, mesh] : stagedMeshes)
    {
        nextMeshCache.insert_or_assign(key, mesh);
    }

    static_assert(std::is_nothrow_move_constructible_v<SceneObject>);

    sceneObjects.reserve(sceneObjects.size() + stagedObjects.size());

    meshCache.swap(nextMeshCache);

    for (SceneObject& object : stagedObjects)
    {
        sceneObjects.push_back(std::move(object));
    }
    selectedSceneObjectIndex = static_cast<int>(sceneObjects.size() - 1);
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
