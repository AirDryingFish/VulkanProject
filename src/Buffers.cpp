#include "TriangleApplication.hpp"
#include "UploadCommands.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_map>

glm::mat4 TriangleApplication::getObjectMatrix(const SceneObject &object) const
{
    const Transform& transform = object.transform;
    glm::mat4 model = glm::translate(glm::mat4(1.0f), transform.position);
    model = model * object.assetTransform;
    model = glm::rotate(model, glm::radians(transform.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(transform.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(transform.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, glm::radians(object.autoRotation), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, transform.scale);
    return model;
}

SceneObject *TriangleApplication::getSelectedSceneObject()
{
    if (selectedSceneObjectIndex < 0 || selectedSceneObjectIndex >= static_cast<int>(sceneObjects.size()))
    {
        return nullptr;
    }
    return &sceneObjects[selectedSceneObjectIndex];
}

const SceneObject *TriangleApplication::getSelectedSceneObject() const
{
    if (selectedSceneObjectIndex < 0 || selectedSceneObjectIndex >= static_cast<int>(sceneObjects.size()))
    {
        return nullptr;
    }
    return &sceneObjects[selectedSceneObjectIndex];
}

Mesh TriangleApplication::createMesh(const MeshBuildData &meshData)
{
    if (meshData.vertices.empty() || meshData.indices.empty())
    {
        throw std::runtime_error("cannot create mesh from empty build data!");
    }

    const VkDeviceSize vertexBufferSize = sizeof(meshData.vertices[0]) * meshData.vertices.size();
    const VkDeviceSize indexBufferSize = sizeof(meshData.indices[0]) * meshData.indices.size();

    // 一次上传vertex and index buffer
    std::vector<BufferUploadRequest> requests;
    requests.reserve(2);
    BufferUploadRequest vertexRequest{};
    vertexRequest.size = vertexBufferSize;
    vertexRequest.data = meshData.vertices.data();
    vertexRequest.destinationUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vertexRequest.debugName = "mesh vertex buffer";
    requests.push_back(vertexRequest);

    BufferUploadRequest indexRequest{};
    indexRequest.size = indexBufferSize;
    indexRequest.data = meshData.indices.data();
    indexRequest.destinationUsage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    indexRequest.debugName = "mesh index buffer";
    requests.push_back(indexRequest);

    std::vector<GpuBuffer> uploadedBuffers = renderer.uploadBuffers(requests);

    if (uploadedBuffers.size() != 2)
    {
        throw std::logic_error("mesh buffer upload returned an unexpected buffer count");
    }

    Mesh mesh{};
    mesh.vertexBuffer = std::move(uploadedBuffers[0]);
    mesh.indexBuffer = std::move(uploadedBuffers[1]);
    mesh.vertexCount = static_cast<uint32_t>(meshData.vertices.size());
    mesh.indexCount = static_cast<uint32_t>(meshData.indices.size());
    mesh.boundsMin = meshData.boundsMin;
    mesh.boundsMax = meshData.boundsMax;
    mesh.boundsValid = meshData.boundsValid;
    mesh.hasTangents = meshData.hasTangents;

    return mesh;
}

void TriangleApplication::createUniformBuffer()
{
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBufferMapped.resize(MAX_FRAMES_IN_FLIGHT);

    BufferDesc uniformDesc{};
    uniformDesc.size = bufferSize;
    uniformDesc.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    uniformDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uniformDesc.debugName = "frame uniform buffer";

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        uniformBuffers[i] = context.createBuffer(uniformDesc);
        VK_CHECK(uniformBuffers[i].map(&uniformBufferMapped[i]));
    }
}

void TriangleApplication::updateUniformBuffer(uint32_t currentImage, float deltaTime)
{
    for (SceneObject &object : sceneObjects)
    {
        if (!object.autoRotate)
        {
            continue;
        }

        object.autoRotation += object.autoRotateSpeed * deltaTime;
        if (object.autoRotation > 360.0f || object.autoRotation < -360.0f)
        {
            object.autoRotation = std::fmod(object.autoRotation, 360.0f);
        }
    }

    UniformBufferObject ubo{};
    ubo.view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    ubo.proj = glm::perspective(glm::radians(45.0f), swapchain.extent().width / static_cast<float>(swapchain.extent().height), cameraNear, cameraFar);
    ubo.proj[1][1] *= -1;
    ubo.cameraPosition = glm::vec4(cameraPos, 1.0f);
    ubo.ambientLight = glm::vec4(ambientLightColor, ambientLightIntensity);
    ubo.lightCounts = glm::ivec4(static_cast<int>(std::min<size_t>(pointLights.size(), MAX_POINT_LIGHTS)), 0, 0, 0);
    ubo.renderParams = glm::vec4(iblIntensity, 0.0f, 0.0f, 0.0f);
    // -- 计算方向光方向 --
    glm::vec3 direction = directionalLight.direction;
    float lengthSquared = glm::dot(direction, direction);
    if (!std::isfinite(lengthSquared) || lengthSquared < 1e-8f)
    {
        direction = glm::vec3(-1.0f, -1.0f, -2.0f);
        lengthSquared = glm::dot(direction, direction);
    }
    direction /= std::sqrt(lengthSquared);
    // ----

    ubo.directionalDirectionEnabled = glm::vec4(direction, directionalLight.enabled ? 1.0f : 0.0f);
    ubo.directionalColorIntensity = glm::vec4(directionalLight.color, std::max(directionalLight.intensity, 0.0f));

    // -- 计算 shadow 相关
    const glm::vec3 shadowCenter{0.0f, 0.0f, 0.0f};
    constexpr float lighDistance = 20.0f;
    constexpr float halfExtent = 10.0f;
    constexpr float shadowNear = 0.1f;
    constexpr float shadowFar = 50.0f;
    // 虚拟光源相机位置，为了从光的视角去渲染场景
    const glm::vec3 lightPosition = shadowCenter - direction * lighDistance;
    const glm::vec3 worldUp{0.0f, 0.0f, 1.0f};
    const glm::vec3 lightUp = std::abs(glm::dot(direction, worldUp)) > 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : worldUp;
    // 里面会进行 cross 操作，如果看向方向和 up 方向平行，cross 为 0，算不出 right 方向
    const glm::mat4 lightView = glm::lookAtRH(lightPosition, shadowCenter, lightUp);
    // 构建一个右手系、深度范围为 [0, 1] 的正交投影矩阵
    glm::mat4 lightProjection = glm::orthoRH_ZO(
        -halfExtent, halfExtent, -halfExtent, halfExtent,
        shadowNear, shadowFar
    );
    // OpenGL framebuffer
    // (0,H) -------- (W,H)
    // |               |
    // |               |
    // |               |
    // (0,0) -------- (W,0)

    // Vulkan framebuffer
    // (0,0) --------→ +X
    // |
    // |
    // ↓
    // +Y
    lightProjection[1][1] *= -1.0f;
    // 世界坐标系变换到 光源裁剪空间中
    ubo.lightViewProjection = lightProjection * lightView;
    ubo.shadowParams = glm::vec4(
        1.0f / static_cast<float>(directionalShadowResolution),
        1.0f / static_cast<float>(directionalShadowResolution),
        0.0005f,
        0.0f);
    ubo.shadowFlags = glm::ivec4(
        0,
        showShadowProjection ? 1 : 0,
        0,
        0);
    // ----

    for (size_t i = 0; i < std::min<size_t>(pointLights.size(), MAX_POINT_LIGHTS); i++)
    {
        const PointLight &light = pointLights[i];
        ubo.pointLights[i].position = glm::vec4(light.position, 1.0f);
        ubo.pointLights[i].color = glm::vec4(light.color, light.intensity);
        ubo.pointLights[i].params = glm::vec4(light.range, light.enabled ? 1.0f : 0.0f, 0.0f, 0.0f);
    }

    memcpy(uniformBufferMapped[currentImage], &ubo, sizeof(ubo));
}

void TriangleApplication::releaseMesh(MeshHandle& mesh)
{
    if (!mesh)
    {
        return;
    }

    if (mesh.use_count() == 1)
    {
        destroyBufferDeferred(mesh->indexBuffer);
        destroyBufferDeferred(mesh->vertexBuffer);
    }
    mesh.reset();
}

void TriangleApplication::destroyBufferDeferred(GpuBuffer& buffer)
{
    if (buffer.get() == VK_NULL_HANDLE)
    {
        return;
    }
    renderer.retireBuffer(std::move(buffer));
}
