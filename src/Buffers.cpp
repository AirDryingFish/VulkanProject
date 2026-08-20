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

glm::mat4 TriangleApplication::getModelMatrix() const
{
    const SceneObject *object = getSelectedSceneObject();
    if (object != nullptr)
    {
        return getObjectMatrix(*object);
    }

    glm::mat4 model = glm::translate(glm::mat4(1.0f), modelPosition);

    model = glm::rotate(model, glm::radians(modelRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(modelRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(modelRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, glm::radians(modelAutoRotation), glm::vec3(0.0f, 0.0f, 1.0f));

    model = glm::scale(model, modelScale);
    return model;
}

glm::mat4 TriangleApplication::getObjectMatrix(const SceneObject &object) const
{
    glm::mat4 model = glm::translate(glm::mat4(1.0f), object.position);
    model = glm::rotate(model, glm::radians(object.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(object.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(object.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, glm::radians(object.autoRotation), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, object.scale);
    return model;
}

TriangleApplication::SceneObject *TriangleApplication::getSelectedSceneObject()
{
    if (selectedSceneObjectIndex < 0 || selectedSceneObjectIndex >= static_cast<int>(sceneObjects.size()))
    {
        return nullptr;
    }
    return &sceneObjects[selectedSceneObjectIndex];
}

const TriangleApplication::SceneObject *TriangleApplication::getSelectedSceneObject() const
{
    if (selectedSceneObjectIndex < 0 || selectedSceneObjectIndex >= static_cast<int>(sceneObjects.size()))
    {
        return nullptr;
    }
    return &sceneObjects[selectedSceneObjectIndex];
}

void TriangleApplication::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
    renderer.immediateSubmit([&](VkCommandBuffer commandBuffer)
    {
        upload::recordBufferCopy(commandBuffer, srcBuffer, dstBuffer, size);
    });
}

void TriangleApplication::createIndexBuffer()
{
    if (indices.empty())
    {
        return;
    }

    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    BufferDesc stagingDesc{};
    stagingDesc.size = bufferSize;
    stagingDesc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    stagingDesc.debugName = "index staging buffer";

    GpuBuffer stagingBuffer = context.createBuffer(stagingDesc);

    void *data = nullptr;
    VK_CHECK(stagingBuffer.map(&data));
    memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
    stagingBuffer.unmap();

    BufferDesc indexDesc{};
    indexDesc.size = bufferSize;
    indexDesc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    indexDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    indexDesc.debugName = "index buffer";

    indexBuffer = context.createBuffer(indexDesc);

    copyBuffer(stagingBuffer.get(), indexBuffer.get(), bufferSize);

}

void TriangleApplication::createVertexBuffer()
{
    if (vertices.empty())
    {
        return;
    }

    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    BufferDesc stagingDesc{};
    stagingDesc.size = bufferSize;
    stagingDesc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    stagingDesc.debugName = "vertex staging buffer";

    GpuBuffer stagingBuffer = context.createBuffer(stagingDesc);

    void *data = nullptr;
    VK_CHECK(stagingBuffer.map(&data));
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    stagingBuffer.unmap();

    BufferDesc vertexDesc{};
    vertexDesc.size = bufferSize;
    vertexDesc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vertexDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    vertexDesc.debugName = "vertex buffer";

    vertexBuffer = context.createBuffer(vertexDesc);

    copyBuffer(stagingBuffer.get(), vertexBuffer.get(), bufferSize);

}

void TriangleApplication::createObjectBuffers(SceneObject &object, const MeshBuildData &meshData)
{
    if (meshData.vertices.empty() || meshData.indices.empty())
    {
        throw std::runtime_error("cannot create object buffers from empty mesh data!");
    }

    const VkDeviceSize vertexBufferSize = sizeof(meshData.vertices[0]) * meshData.vertices.size();

    BufferDesc vertexStagingDesc{};
    vertexStagingDesc.size = vertexBufferSize;
    vertexStagingDesc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    vertexStagingDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    vertexStagingDesc.debugName = "scene object vertex staging buffer";

    GpuBuffer vertexStagingBuffer = context.createBuffer(vertexStagingDesc);

    void *data = nullptr;
    VK_CHECK(vertexStagingBuffer.map(&data));
    memcpy(data, meshData.vertices.data(), static_cast<size_t>(vertexBufferSize));
    vertexStagingBuffer.unmap();

    BufferDesc vertexDesc{};
    vertexDesc.size = vertexBufferSize;
    vertexDesc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vertexDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    vertexDesc.debugName = "scene object vertex buffer";

    object.vertexBuffer = context.createBuffer(vertexDesc);

    copyBuffer(vertexStagingBuffer.get(), object.vertexBuffer.get(), vertexBufferSize);

    const VkDeviceSize indexBufferSize = sizeof(meshData.indices[0]) * meshData.indices.size();
    BufferDesc indexStagingDesc{};
    indexStagingDesc.size = indexBufferSize;
    indexStagingDesc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    indexStagingDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    indexStagingDesc.debugName = "scene object index staging buffer";

    GpuBuffer indexStagingBuffer = context.createBuffer(indexStagingDesc);

    VK_CHECK(indexStagingBuffer.map(&data));
    memcpy(data, meshData.indices.data(), static_cast<size_t>(indexBufferSize));
    indexStagingBuffer.unmap();

    BufferDesc indexDesc{};
    indexDesc.size = indexBufferSize;
    indexDesc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    indexDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    indexDesc.debugName = "scene object index buffer";

    object.indexBuffer = context.createBuffer(indexDesc);

    copyBuffer(indexStagingBuffer.get(), object.indexBuffer.get(), indexBufferSize);

    object.vertexCount = static_cast<uint32_t>(meshData.vertices.size());
    object.indexCount = static_cast<uint32_t>(meshData.indices.size());
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
    ubo.model = glm::mat4(1.0f);
    ubo.view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    ubo.proj = glm::perspective(glm::radians(45.0f), swapchain.extent().width / static_cast<float>(swapchain.extent().height), cameraNear, cameraFar);
    ubo.proj[1][1] *= -1;
    ubo.cameraPosition = glm::vec4(cameraPos, 1.0f);
    ubo.ambientLight = glm::vec4(ambientLightColor, ambientLightIntensity);
    ubo.lightCounts = glm::ivec4(static_cast<int>(std::min<size_t>(pointLights.size(), MAX_POINT_LIGHTS)), 0, 0, 0);
    ubo.materialAlbedo = glm::vec4(materialAlbedo, 1.0f);
    ubo.materialParams = glm::vec4(
        materialMetallic,
        materialRoughness,
        materialAo,
        iblIntensity
    );

    for (size_t i = 0; i < std::min<size_t>(pointLights.size(), MAX_POINT_LIGHTS); i++)
    {
        const PointLight &light = pointLights[i];
        ubo.pointLights[i].position = glm::vec4(light.position, 1.0f);
        ubo.pointLights[i].color = glm::vec4(light.color, light.intensity);
        ubo.pointLights[i].params = glm::vec4(light.range, light.enabled ? 1.0f : 0.0f, 0.0f, 0.0f);
    }

    memcpy(uniformBufferMapped[currentImage], &ubo, sizeof(ubo));
}


void TriangleApplication::destroyBufferDeferred(GpuBuffer& buffer)
{
    if (buffer.get() == VK_NULL_HANDLE)
    {
        return;
    }
    renderer.retireBuffer(std::move(buffer));
}
