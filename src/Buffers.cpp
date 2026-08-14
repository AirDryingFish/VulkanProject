#include "TriangleApplication.hpp"

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
    immediateSubmit([&](VkCommandBuffer commandBuffer) {
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
    });
}

void TriangleApplication::createIndexBuffer()
{
    if (indices.empty())
    {
        return;
    }

    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();
    AllocatedBuffer stagingBuffer = context.createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void *data = nullptr;
    // VK_CHECK(vmaMapMemory(allocator, stagingBuffer.allocation, &data));
    VK_CHECK(stagingBuffer.map(&data));
    memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
    stagingBuffer.unmap();
    // vmaUnmapMemory(allocator, stagingBuffer.allocation);

    indexBuffer = context.createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    copyBuffer(stagingBuffer.get(), indexBuffer.get(), bufferSize);

}

void TriangleApplication::createVertexBuffer()
{
    if (vertices.empty())
    {
        return;
    }

    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
    AllocatedBuffer stagingBuffer = context.createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void *data = nullptr;
    // VK_CHECK(vmaMapMemory(allocator, stagingBuffer.allocation, &data));
    VK_CHECK(stagingBuffer.map(&data));
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    stagingBuffer.unmap();
    // vmaUnmapMemory(allocator, stagingBuffer.allocation);

    vertexBuffer = context.createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    copyBuffer(stagingBuffer.get(), vertexBuffer.get(), bufferSize);

}

void TriangleApplication::createObjectBuffers(SceneObject &object, const MeshBuildData &meshData)
{
    if (meshData.vertices.empty() || meshData.indices.empty())
    {
        throw std::runtime_error("cannot create object buffers from empty mesh data!");
    }

    const VkDeviceSize vertexBufferSize = sizeof(meshData.vertices[0]) * meshData.vertices.size();
    AllocatedBuffer vertexStagingBuffer = context.createBuffer(
        vertexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void *data = nullptr;
    // VK_CHECK(vmaMapMemory(allocator, vertexStagingBuffer.allocation, &data));
    VK_CHECK(vertexStagingBuffer.map(&data));
    memcpy(data, meshData.vertices.data(), static_cast<size_t>(vertexBufferSize));
    // vmaUnmapMemory(allocator, vertexStagingBuffer.allocation);
    vertexStagingBuffer.unmap();

    object.vertexBuffer = context.createBuffer(
        vertexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    copyBuffer(vertexStagingBuffer.get(), object.vertexBuffer.get(), vertexBufferSize);


    const VkDeviceSize indexBufferSize = sizeof(meshData.indices[0]) * meshData.indices.size();
    AllocatedBuffer indexStagingBuffer = context.createBuffer(
        indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // VK_CHECK(vmaMapMemory(allocator, indexStagingBuffer.allocation, &data));
    VK_CHECK(indexStagingBuffer.map(&data));
    memcpy(data, meshData.indices.data(), static_cast<size_t>(indexBufferSize));
    // vmaUnmapMemory(allocator, indexStagingBuffer.allocation);
    indexStagingBuffer.unmap();

    object.indexBuffer = context.createBuffer(
        indexBufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    copyBuffer(indexStagingBuffer.get(), object.indexBuffer.get(), indexBufferSize);

    object.vertexCount = static_cast<uint32_t>(meshData.vertices.size());
    object.indexCount = static_cast<uint32_t>(meshData.indices.size());
}

void TriangleApplication::createUniformBuffer()
{
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBufferMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        uniformBuffers[i] = context.createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
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


void TriangleApplication::destroyBufferDeferred(AllocatedBuffer& buffer)
{
    if (buffer.get() == VK_NULL_HANDLE)
    {
        return;
    }
    renderer.retireBuffer(std::move(buffer));
}
