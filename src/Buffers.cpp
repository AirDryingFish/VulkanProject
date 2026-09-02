#include "TriangleApplication.hpp"
#include "UploadCommands.hpp"

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
