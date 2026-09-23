#include "TriangleApplication.hpp"
#include "GltfLoader.hpp"

#include <tiny_obj_loader.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/matrix.hpp>

#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <filesystem>
#include <memory>
#include <type_traits>
#include <vector>

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
    return "gltf:" + sourcePath.generic_u8string() + "#mesh=" + std::to_string(meshIndex) + "/primitive=" + std::to_string(primitiveIndex);
}

MeshBuildData buildGltfPrimitiveMeshData(const GltfPrimitiveData& primitive)
{
    MeshBuildData meshData{};
    meshData.vertices.reserve(primitive.vertices.size());

    for (const GltfDecodedVertex& decodedVertex : primitive.vertices)
    {
        Vertex vertex{};
        vertex.pos = decodedVertex.position;
        vertex.color = decodedVertex.color;
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
            meshData.vertices.push_back({positions[a], glm::vec4(1.0f), glm::vec2(0.0f, 0.0f), normal});
            meshData.vertices.push_back({positions[b], glm::vec4(1.0f), glm::vec2(1.0f, 0.0f), normal});
            meshData.vertices.push_back({positions[c], glm::vec4(1.0f), glm::vec2(1.0f, 1.0f), normal});
            meshData.vertices.push_back({positions[d], glm::vec4(1.0f), glm::vec2(0.0f, 1.0f), normal});
            //
            meshData.indices.insert(meshData.indices.end(),
                {start, start + 2, start + 1,
                start, start + 3, start + 2});
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
                vertex.color = glm::vec4(1.0f);
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
            throw std::runtime_error(imported.sourcePath.u8string() + ": asset contains no meshes");
        }
        const GltfMeshData &gltfMesh = imported.meshes.front();
        if (gltfMesh.primitiveIndices.empty())
        {
            throw std::runtime_error(imported.sourcePath.u8string() + ": mesh[0] contains no primitives");
        }
        const std::size_t decodedPrimitiveIndex = gltfMesh.primitiveIndices.front();
        if (decodedPrimitiveIndex >= imported.primitives.size())
        {
            throw std::logic_error(imported.sourcePath.u8string() + ": mesh[0] primitive[0] decoded index is out of range");
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

            vertex.color = {1.0f, 1.0f, 1.0f, 1.0f};
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

TriangleApplication::GltfImportResult TriangleApplication::addGltfMeshObjects(const std::string &path)
{
    if (path.empty())
    {
        throw std::invalid_argument("glTF path must not be empty");
    }

    const GltfImportData imported = loadGltfCpuData(std::filesystem::u8path(path));
    std::size_t primitiveCount = 0;
    for (const GltfMeshData& mesh : imported.meshes)
    {
        primitiveCount += mesh.primitiveIndices.size();
    }
    if (primitiveCount == 0)
    {
        throw std::runtime_error(imported.sourcePath.u8string() + ": asset contains no mesh primitives");
    }

    const std::string normalizedSourcePath = imported.sourcePath.u8string();
    const std::string assetName = imported.sourcePath.filename().u8string();

    //------------------------
    // 1. 从选定 scene 的根结点出发，避免导入其他 scene 的对象
    // 2. 将孩子放到待处理列表末尾，实现广度优先遍历
    // 3. 计算 world = parentWorld * localTransform
    //------------------------
    if (imported.scenes.empty())
    {
        throw std::runtime_error(normalizedSourcePath + ": asset contains no scenes");
    }

    // 优先使用默认场景，未指定时选择 scene 0
    const std::size_t sceneIndex = imported.defaultSceneIndex.value_or(0);
    if (sceneIndex >= imported.scenes.size())
    {
        throw std::runtime_error(normalizedSourcePath + ": scene index is out of range");
    }

    struct NodeInstance{
        std::size_t nodeIndex;
        glm::mat4 worldTransform;
    };

    // 当前要遍历 glTF 节点。以及这个节点对应的变换
    std::vector<NodeInstance> nodeInstances;
    std::vector<bool> visited(imported.nodes.size(), false);
    for (std::size_t rootIndex : imported.scenes[sceneIndex].rootNodeIndices)
    {
        nodeInstances.push_back({rootIndex, glm::mat4(1.0f)});
    }

    // glTF 的 Y-UP 转换为项目的 Z-UP。绕 x 轴旋转90度
    const glm::mat4 rootConversion = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

    std::size_t instancePrimitiveCount = 0;
    for (std::size_t cursor = 0; cursor < nodeInstances.size(); ++cursor)
    {
        // 使用值拷贝，避免后面的 push_back 扩容使引用失效
        const NodeInstance pending = nodeInstances[cursor];
        if (pending.nodeIndex >= imported.nodes.size())
        {
            throw std::runtime_error(normalizedSourcePath + ": node index is out of range");
        }
        const auto& node = imported.nodes[pending.nodeIndex];
        const std::string nodeContext = normalizedSourcePath + ": node[" + std::to_string(pending.nodeIndex) + "]" + node.name;
        if (visited[pending.nodeIndex])
        {
            throw std::runtime_error(nodeContext + ": cycle or repeated node reference");
        }
        visited[pending.nodeIndex] = true;

        const glm::mat4 world = pending.worldTransform * node.localTransform;
        nodeInstances[cursor].worldTransform = world;

        // 没有 mesh 的父节点也必须继续遍历 children
        for (std::size_t childIndex : node.children)
        {
            nodeInstances.push_back({childIndex, world});
        }
        if (!node.meshIndex)
        {
            continue;
        }

        if (*node.meshIndex >= imported.meshes.size())
        {
            throw std::runtime_error(nodeContext + ": mesh index is out of range");
        }

        const glm::mat4 assetTransform = rootConversion * world;

        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                if (!std::isfinite(assetTransform[col][row]))
                {
                    throw std::runtime_error(nodeContext + ": transform contains non-finite values");
                }
            }
        }

        const float determinant = glm::determinant(glm::mat3(assetTransform));

        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f)
        {
            throw std::runtime_error(nodeContext + ": singular or near-singular transform");
        }
        if (determinant < 0.0f)
        {
            throw std::runtime_error(nodeContext + ": mirrored transform is not supported");
        }

        instancePrimitiveCount += imported.meshes[*node.meshIndex].primitiveIndices.size();
    }
    if (instancePrimitiveCount == 0)
    {
        throw std::runtime_error(normalizedSourcePath + ": selected scene contains no mesh instances");
    }
    //------------------------

    // 上传之前先确认 descriptor pool 能容纳本次新增材质
    ensureMaterialDescriptorCapacity(imported.materials.size());
    // 暂存本次导入的资源，成功后再更新应用中的资源库
    GltfMaterialUpload materialUpload{gltfImageCache, samplerLibrary, {}};
    materialUpload.materials.reserve(imported.materials.size());
    for (std::size_t materialIndex = 0; materialIndex < imported.materials.size(); ++materialIndex)
    {
        // imported.materials[i]: 文件中解析出的材质描述
        // materialUpload.materials[i]: 根据该描述创建的运行时材质
        const std::string debugName = normalizedSourcePath + ": material[" + std::to_string(materialIndex) + "]";
        MaterialHandle material = createGltfMaterial(imported, imported.materials[materialIndex], debugName, materialUpload);
        materialUpload.materials.push_back(std::move(material));
    }

    // 导入事务中的临时强引用
    std::unordered_map<std::string, MeshHandle> stagedMeshes;
    std::size_t uploadedMeshCount = 0;
    std::vector<SceneObject> stagedObjects;
    stagedMeshes.reserve(primitiveCount);
    stagedObjects.reserve(instancePrimitiveCount);

    // gltf decode
    // for (std::size_t nodeIndex = 0; nodeIndex < imported.nodes.size(); ++nodeIndex)
    // 只实例化选定场景中遍历到的节点
    for (const NodeInstance& instance : nodeInstances)
    {
        const std::size_t nodeIndex = instance.nodeIndex;
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
            const std::string key = makeGltfPrimitiveCacheKey(imported.sourcePath, meshIndex, primitiveIndex);
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
                    uploadedMesh.cacheKey = key;
                    mesh = std::make_shared<Mesh>(std::move(uploadedMesh));
                    ++uploadedMeshCount;
                }
                stagedMeshes.emplace(key, mesh);
            }

            SceneObject object{};
            object.name = assetName + " / " + nodeName + " / primitive " + std::to_string(primitiveIndex);
            object.source = MeshSource::Gltf;
            object.sourcePath = normalizedSourcePath;
            object.mesh = std::move(mesh);
            object.assetTransform = rootConversion * instance.worldTransform;

            // 根据 primitive 的索引选择材质
            const auto& primitive = imported.primitives[decodedPrimitiveIndex];
            if (primitive.materialIndex.has_value())
            {
                object.material = materialUpload.materials.at(*primitive.materialIndex);
            }
            else
            {
                object.material = defaultGltfMaterial;
            }

            stagedObjects.push_back(std::move(object));
        }
    }

    auto nextMeshCache = meshCache;
    nextMeshCache.reserve(meshCache.size() + stagedMeshes.size());
    for (const auto& [key, mesh] : stagedMeshes)
    {
        nextMeshCache.insert_or_assign(key, mesh);
    }

    // 必须能够移动构造，并且移动构造不会抛异常
    static_assert(std::is_nothrow_move_constructible_v<SceneObject>);
    static_assert(std::is_nothrow_move_constructible_v<MaterialHandle>);

    // 提前完成可能发生内存分配的扩容
    sceneObjects.reserve(sceneObjects.size() + stagedObjects.size());
    materialLibrary.reserve(materialLibrary.size() + materialUpload.materials.size());

    // 为本次导入的新材质分配并写入 descripotr set
    allocateMaterialDescriptorSets(materialUpload.materials);

    GltfImportResult importResult;
    importResult.objectCount = stagedObjects.size(); // 这次最终创建了多少个 SceneObject
    importResult.uploadedMeshCount = uploadedMeshCount; // 这次真正新建并上传到 GPU 的 mesh 数量
    importResult.reusedMeshCount = stagedMeshes.size() - uploadedMeshCount; // 这次复用了多少个已经存在的 mesh，而不是重新上传
    importResult.materialCount = materialUpload.materials.size();           // 这次导入准备了多少个 glTF material
    importResult.uploadedImageCount = materialUpload.uploadedImageCount;
    importResult.createdSamplerCount = materialUpload.samplers.size() - samplerLibrary.size();

    // 分配成功后，将暂存的资源提交给应用
    meshCache.swap(nextMeshCache);
    gltfImageCache.swap(materialUpload.imageCache);
    samplerLibrary.swap(materialUpload.samplers);

    for (MaterialHandle& material : materialUpload.materials)
    {
        materialLibrary.push_back(std::move(material));
    }

    for (SceneObject& object : stagedObjects)
    {
        sceneObjects.push_back(std::move(object));
    }
    selectedSceneObjectIndex = static_cast<int>(sceneObjects.size() - 1);
    selectedObject = SceneSelection::Model;
    selectedModel = true;
    selectedPointLightIndex = -1;
    return importResult;
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
    uploadedMesh.cacheKey = key;
    MeshHandle mesh = std::make_shared<Mesh>(std::move(uploadedMesh));
    meshCache[key] = mesh;
    return mesh;
}
