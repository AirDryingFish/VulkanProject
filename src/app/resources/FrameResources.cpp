#include "app/Application.hpp"
#include "render/frame/SceneUniforms.hpp"
#include "vulkan/VulkanCheck.hpp"

#include <cstring>
#include <stdexcept>
#include <utility>

void Application::createUniformBuffer()
{
    constexpr VkDeviceSize lightBytes = static_cast<VkDeviceSize>(pointLightBufferBytes);

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context.physicalDevice(), &properties);
    if (lightBytes > properties.limits.maxStorageBufferRange ||
        properties.limits.maxPerStageDescriptorStorageBuffers < 1 ||
        properties.limits.maxDescriptorSetStorageBuffers < 1)
    {
        throw std::runtime_error("1024-point-light storage buffer is unsupported");
    }

    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBufferMapped.resize(MAX_FRAMES_IN_FLIGHT);

    pointLightBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    pointLightBufferMapped.resize(MAX_FRAMES_IN_FLIGHT);

    BufferDesc uniformDesc{};
    uniformDesc.size = sizeof(UniformBufferObject);
    uniformDesc.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    uniformDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    uniformDesc.debugName = "frame uniform buffer";

    BufferDesc lightDesc{};
    lightDesc.size = lightBytes;
    lightDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    lightDesc.requiredMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    lightDesc.debugName = "point light storage buffer";

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        uniformBuffers[i] = context.createBuffer(uniformDesc);
        VK_CHECK(uniformBuffers[i].map(&uniformBufferMapped[i]));

        pointLightBuffers[i] = context.createBuffer(lightDesc);
        VK_CHECK(pointLightBuffers[i].map(&pointLightBufferMapped[i]));
    }
}

ShadowFrameSummary Application::updateUniformBuffer(std::uint32_t frameSlot, float deltaTime,
                                                    std::uint32_t shadowPreviewLayer, float shadowBlendFraction,
                                                    DeferredFrameUniforms& deferredUniforms)
{
    // beginFrame 已等待该帧槽 fence；场景只推进一次，然后完整生成 CPU 数据再发布。
    scene.updateAnimations(deltaTime);
    const FrameSceneUniforms uniforms =
        makeSceneUniforms(scene, camera, display, shadows, swapchain.extent(), shadowPreviewLayer, shadowBlendFraction);
    deferredUniforms = uniforms.deferred;

    void* lightMapped = pointLightBufferMapped.at(frameSlot);
    if (lightMapped == nullptr)
    {
        throw std::logic_error("Point light buffer is not mapped");
    }
    if (uniforms.pointLights.count != 0)
    {
        const std::size_t bytes = uniforms.pointLights.count * sizeof(GpuPointLight);
        std::memcpy(lightMapped, uniforms.pointLights.lights.data(), bytes);
        VK_CHECK(pointLightBuffers.at(frameSlot).flush(0, static_cast<VkDeviceSize>(bytes)));
    }

    // ShaderTypes.hpp 约束 CPU/GLSL 的字段偏移和总大小；非 coherent 内存显式 flush。
    std::memcpy(uniformBufferMapped[frameSlot], &uniforms.frame, sizeof(uniforms.frame));
    VK_CHECK(uniformBuffers[frameSlot].flush(0, sizeof(uniforms.frame)));
    return uniforms.shadow;
}
