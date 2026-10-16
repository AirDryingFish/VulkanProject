#include "app/Application.hpp"
#include "render/ShaderTypes.hpp"

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

Mesh Application::createMesh(const MeshBuildData &meshData)
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

void Application::createUniformBuffer()
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

void Application::updateUniformBuffer(uint32_t currentImage, float deltaTime)
{
    scene.updateAnimations(deltaTime);

    UniformBufferObject ubo{};
    ubo.view = camera.view();
    ubo.proj = camera.projection(swapchain.extent().width / static_cast<float>(swapchain.extent().height));

    ubo.cameraPosition = glm::vec4(camera.position, 1.0f);
    ubo.ambientLight = glm::vec4(scene.ambientLightColor, scene.ambientLightIntensity);
    ubo.lightCounts = glm::ivec4(static_cast<int>(std::min<size_t>(scene.pointLights.size(), MAX_POINT_LIGHTS)), 0, 0, 0);
    ubo.renderParams = glm::vec4(scene.iblIntensity, camera.nearPlane, std::max(display.debugDepthFar, camera.nearPlane + 0.001f), 0.0f);

    // -- 计算方向光方向 --
    glm::vec3 direction = scene.directionalLight.direction;
    float lengthSquared = glm::dot(direction, direction);
    if (!std::isfinite(lengthSquared) || lengthSquared < 1e-8f)
    {
        direction = glm::vec3(-1.0f, -1.0f, -2.0f);
        lengthSquared = glm::dot(direction, direction);
    }
    direction /= std::sqrt(lengthSquared);
    // ----

    ubo.directionalDirectionEnabled = glm::vec4(direction, scene.directionalLight.enabled ? 1.0f : 0.0f);
    ubo.directionalColorIntensity = glm::vec4(scene.directionalLight.color, std::max(scene.directionalLight.intensity, 0.0f));

    // -- 计算 shadow 相关
    const float halfExtent = shadows.halfExtent;
    const float lighDistance = 2.0f * halfExtent + 1.0f;
    constexpr float shadowNear = 0.1f;
    const float shadowFar = 4.0f * halfExtent + 2.0f;
    // 虚拟光源相机位置，为了从光的视角去渲染场景
    const glm::vec3 lightPosition = shadows.center - direction * lighDistance;
    const glm::vec3 worldUp{0.0f, 0.0f, 1.0f};
    const glm::vec3 lightUp = std::abs(glm::dot(direction, worldUp)) > 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : worldUp;
    // 里面会进行 cross 操作，如果看向方向和 up 方向平行，cross 为 0，算不出 right 方向
    const glm::mat4 lightView = glm::lookAtRH(lightPosition, shadows.center, lightUp);
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
        shadows.receiverBias,
        0.0f);

    // shadowFlags:
    // x: shadows enabled
    // y: shadow projection view
    // z: PCF enabled
    // w: DebugView
    ubo.shadowFlags = glm::ivec4(
        (shadows.enabled && scene.directionalLight.enabled) ? 1 : 0,
        display.debugView == DebugView::ShadowProjection ? 1 : 0,
        shadows.pcfEnabled ? 1 : 0,
        static_cast<std::uint32_t>(display.debugView));
    // ----

    for (size_t i = 0; i < std::min<size_t>(scene.pointLights.size(), MAX_POINT_LIGHTS); i++)
    {
        const PointLight &light = scene.pointLights[i];
        ubo.pointLights[i].position = glm::vec4(light.position, 1.0f);
        ubo.pointLights[i].color = glm::vec4(light.color, light.intensity);
        ubo.pointLights[i].params = glm::vec4(light.range, light.enabled ? 1.0f : 0.0f, 0.0f, 0.0f);
    }

    memcpy(uniformBufferMapped[currentImage], &ubo, sizeof(ubo));
}

void Application::releaseMesh(MeshHandle& mesh)
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

void Application::destroyBufferDeferred(GpuBuffer& buffer)
{
    if (buffer.get() == VK_NULL_HANDLE)
    {
        return;
    }
    renderer.retireBuffer(std::move(buffer));
}
