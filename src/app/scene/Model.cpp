#include "app/Application.hpp"
#include "assets/gltf/GltfLoader.hpp"
#include "assets/mesh/MeshBuilder.hpp"
#include "render/resources/MeshUpload.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <filesystem>
#include <memory>
#include <type_traits>
#include <vector>

namespace
{
std::string fileNameFromPath(const std::string& path)
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

    const std::filesystem::path sourcePath =
        path.empty() ? std::filesystem::path(MODEL_PATH) : std::filesystem::path(path);
    const std::filesystem::path normalizedPath = std::filesystem::absolute(sourcePath).lexically_normal();

    if (source == MeshSource::Gltf)
    {
        return "gltf:" + normalizedPath.generic_string() + "#mesh=0/primitive=0";
    }

    return "obj:" + normalizedPath.generic_string();
}

std::string makeGltfPrimitiveCacheKey(const std::filesystem::path& sourcePath, std::size_t meshIndex,
                                      std::size_t primitiveIndex)
{
    // gltf:/.../model.gltf#mesh=0/primitive=0
    // gltf:/.../model.gltf#mesh=0/primitive=1
    return "gltf:" + sourcePath.generic_u8string() + "#mesh=" + std::to_string(meshIndex) +
           "/primitive=" + std::to_string(primitiveIndex);
}

} // namespace

// 默认资产位置属于应用配置；CPU 构建模块只接收解析后的路径。
MeshBuildData Application::buildMeshData(MeshSource source, const std::string& path)
{
    return ::buildMeshData(source, path.empty() && source != MeshSource::Gltf ? MODEL_PATH : path);
}

void Application::addMeshObject(MeshSource source, const std::string& path)
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
        object.name = "Cube " + std::to_string(scene.objects.size() + 1);
    }
    else if (source == MeshSource::Sphere)
    {
        object.name = "Sphere " + std::to_string(scene.objects.size() + 1);
    }
    else if (source == MeshSource::Obj)
    {
        const std::string objName = fileNameFromPath(meshPath);
        object.name = objName.empty() ? "OBJ " + std::to_string(scene.objects.size() + 1) : objName;
    }
    else if (source == MeshSource::Gltf)
    {
        const std::string gltfName = fileNameFromPath(meshPath);
        object.name = gltfName.empty() ? "glTF " + std::to_string(scene.objects.size() + 1) : gltfName;
    }
    object.source = source;
    object.sourcePath = meshPath;
    object.mesh = getOrCreateMesh(source, meshPath);
    object.material = defaultMaterial;

    scene.objects.push_back(std::move(object));

    editor.objectIndex = static_cast<int>(scene.objects.size()) - 1;
    editor.selection = SelectionKind::Object;
    editor.pointLightIndex = -1;
}

GltfImportResult Application::addGltfMeshObjects(const std::string& path)
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

    struct NodeInstance
    {
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
        const std::string nodeContext =
            normalizedSourcePath + ": node[" + std::to_string(pending.nodeIndex) + "]" + node.name;
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
        MaterialHandle material =
            createGltfMaterial(imported, imported.materials[materialIndex], debugName, materialUpload);
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
                throw std::logic_error(normalizedSourcePath + ": mesh[" + std::to_string(meshIndex) + "] primitive[" +
                                       std::to_string(primitiveIndex) + "] decoded primitive index is out if range");
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
                    Mesh uploadedMesh = uploadMesh(renderer, meshData);
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
    scene.objects.reserve(scene.objects.size() + stagedObjects.size());
    materialLibrary.reserve(materialLibrary.size() + materialUpload.materials.size());

    // 为本次导入的新材质分配并写入 descripotr set
    allocateMaterialDescriptorSets(materialUpload.materials);

    GltfImportResult importResult;
    importResult.objectCount = stagedObjects.size();    // 这次最终创建了多少个 SceneObject
    importResult.uploadedMeshCount = uploadedMeshCount; // 这次真正新建并上传到 GPU 的 mesh 数量
    importResult.reusedMeshCount =
        stagedMeshes.size() - uploadedMeshCount; // 这次复用了多少个已经存在的 mesh，而不是重新上传
    importResult.materialCount = materialUpload.materials.size(); // 这次导入准备了多少个 glTF material
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
        scene.objects.push_back(std::move(object));
    }
    editor.objectIndex = static_cast<int>(scene.objects.size() - 1);
    editor.selection = SelectionKind::Object;
    editor.pointLightIndex = -1;
    return importResult;
}

MeshHandle Application::getOrCreateMesh(MeshSource source, const std::string& path)
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
    Mesh uploadedMesh = uploadMesh(renderer, meshData);
    uploadedMesh.cacheKey = key;
    MeshHandle mesh = std::make_shared<Mesh>(std::move(uploadedMesh));
    meshCache[key] = mesh;
    return mesh;
}

void Application::releaseMesh(MeshHandle& mesh)
{
    if (!mesh)
    {
        return;
    }

    // 其他对象可能复用同一 Mesh；只有最后一个强引用释放时才转交 buffer 的所有权。
    // retireBuffer 把销毁延后到帧槽安全复用时，避免 UI 删除后 GPU 还在访问已释放的内存。
    if (mesh.use_count() == 1)
    {
        destroyBufferDeferred(mesh->indexBuffer);
        destroyBufferDeferred(mesh->vertexBuffer);
    }
    mesh.reset();
}

void Application::destroyBufferDeferred(GpuBuffer& buffer)
{
    if (buffer.get() == VK_NULL_HANDLE)
    {
        return;
    }
    renderer.retireBuffer(std::move(buffer));
}

void Application::pruneSceneCaches()
{
    for (auto it = meshCache.begin(); it != meshCache.end();)
        it = it->second.expired() ? meshCache.erase(it) : std::next(it);
    for (auto it = gltfImageCache.begin(); it != gltfImageCache.end();)
        it = it->second.expired() ? gltfImageCache.erase(it) : std::next(it);
    samplerLibrary.erase(std::remove_if(samplerLibrary.begin(), samplerLibrary.end(), [&](const SamplerHandle& sampler)
                                        { return sampler != defaultTextureSampler && sampler.use_count() == 1; }),
                         samplerLibrary.end());
}
