#include "render/Renderer.hpp"
#include "render/temporal/TemporalTypes.hpp"
#include "vulkan/VulkanCheck.hpp"
#include "vulkan/context/VulkanContext.hpp"
#include "vulkan/presentation/Swapchain.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <glm/matrix.hpp>
#include <iostream>
#include <utility>
#include <vector>


namespace
{
    GpuBuffer createMappedTemporalBuffer(VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage, const char* name, void*& mapped)
    {
        BufferDesc desc{};
        desc.size = size;
        desc.usage = usage;
        desc.requiredMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        desc.debugName = name;

        GpuBuffer buffer = context.createBuffer(desc);

        void* pointer = nullptr;
        VK_CHECK(buffer.map(&pointer));

        mapped = pointer;
        return buffer;
    }
}

// set 0: 原有帧 ubo、点光源 SSBO
// set 1: 材质纹理
// 新增 set 2: 时域 ubo、对象 SSBO
void Renderer::createTemporalGeometryResources()
{
    // 实现路径: Gbuffer 阶段生成 几何信息+运动向量 -> 后续时域处理。所以需要 deferred lighting
    if (temporalGeometryPool_ != VK_NULL_HANDLE || !deferredLightingAvailable())
    {
        return;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context_->physicalDevice(), &properties);
    const auto limits = properties.limits;
    temporalObjectLimit_ = limits.maxStorageBufferRange / sizeof(TemporalObjectData);

    if (limits.maxUniformBufferRange < sizeof(TemporalFrameUniforms) ||
        temporalObjectLimit_ == 0 ||
        limits.maxBoundDescriptorSets < 3 ||
        limits.maxPerStageDescriptorUniformBuffers < 2 ||
        limits.maxPerStageDescriptorStorageBuffers < 1 ||
        limits.maxDescriptorSetUniformBuffers < 2 ||
        limits.maxDescriptorSetStorageBuffers < 2)
    {
        temporalDataUnavailableReason_ = "Device descriptor/buffer limits are insufficient";
        std::cout << "Temporal data unvailable: " << temporalDataUnavailableReason_ << "\n";
        return;
    }

    const VkDevice device = context_->device();

    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    setInfo.pBindings = bindings.data();

    VK_CHECK(vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &temporalGeometrySetLayout_));

    const std::array<VkDescriptorSetLayout, 3> layouts{
        frameDescriptorSetLayout_,
        materialDescriptorSetLayout_,
        temporalGeometrySetLayout_
    };

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push.size = sizeof(DrawPushConstants);

    VkPipelineLayoutCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineInfo.setLayoutCount = static_cast<std::uint32_t>(layouts.size());
    pipelineInfo.pSetLayouts = layouts.data();
    pipelineInfo.pushConstantRangeCount = 1;
    pipelineInfo.pPushConstantRanges = &push;

    VK_CHECK(vkCreatePipelineLayout(device, &pipelineInfo, nullptr, &temporalGeometryPipelineLayout_));
    const auto count = static_cast<std::uint32_t>(temporalGeometryFrames_.size());
    const std::array<VkDescriptorPoolSize, 2> sizes{{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, count},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, count}
    }};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = count;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    poolInfo.pPoolSizes = sizes.data();
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &temporalGeometryPool_));

    std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> setLayouts{};
    setLayouts.fill(temporalGeometrySetLayout_);
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> sets{};
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = temporalGeometryPool_;
    allocateInfo.descriptorSetCount = count;
    allocateInfo.pSetLayouts = setLayouts.data();
    VK_CHECK(vkAllocateDescriptorSets(device, &allocateInfo, sets.data()));


    for (std::size_t i = 0; i < temporalGeometryFrames_.size(); ++i)
    {
        auto& frame = temporalGeometryFrames_[i];
        frame.descriptorSet = sets[i];
        frame.uniforms = createMappedTemporalBuffer(*context_, sizeof(TemporalFrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, "Temporal frame uniforms", frame.uniformsMapped);
        frame.objects = createMappedTemporalBuffer(*context_, sizeof(TemporalObjectData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "Temporal object history", frame.objectsMapped);
        frame.objectCapacity = 1;
        writeTemporalGeometryDescriptors(frame);
    }
    temporalDataUnavailableReason_.clear();

    std::cout << "Temporal GPU data prepared: " << count << " frame slots, UBO=384, object stride=96\n";
}

// 通过 "vkUpdateDescriptorSets()" 让这个 frame 槽位 的 Descriptor set 指向它自己的两个 buffer.
void Renderer::writeTemporalGeometryDescriptors(TemporalGeometryFrame& frame)
{
    std::array<VkDescriptorBufferInfo, 2> buffers{{
        {frame.uniforms.get(), 0, sizeof(TemporalFrameUniforms)},
        {frame.objects.get(), 0, frame.objects.size()}
    }};

    std::array<VkWriteDescriptorSet, 2> writes{};
    for (std::uint32_t binding = 0; binding < writes.size(); ++binding)
    {
        auto& write = writes[binding];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = frame.descriptorSet;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = binding == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &buffers[binding];
    }
    vkUpdateDescriptorSets(context_->device(), static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

// 把 “当前相机状态 + 上一提交帧的变换历史”，整理并上传到当前飞行帧槽位的 gpu buffer，供 geometry shader 使用 （每帧往这些资源里填数据）
void Renderer::prepareTemporalGeometryFrame(const FrameToken& token, const RenderFrameData& data)
{
    auto& frame = temporalGeometryFrames_[token.frameIndex];
    frame.prepared = false;
    if (data.renderPath != RenderPath::Deferred || !deferredLightingAvailable() || temporalGeometryPool_ == VK_NULL_HANDLE)
    {
        return;
    }

    // 有多少条绘制对象记录
    const std::size_t count = data.objects != nullptr ? data.objects->size() : 0;
    if (count > temporalObjectLimit_)
    {
        const char* reason = "Object count exceeds temporal SSBO range";
        if (temporalDataUnavailableReason_ != reason)
        {
            temporalDataUnavailableReason_ = reason;
            std::cerr << "Temporal data unavailable: " << reason << " (" << count << " > " << temporalObjectLimit_ << ")\n";
        }
        return;
    }

    // 在对象数量超过当前容量时，创建一个更大的 buffer 替换旧 buffer，然后更新 Descriptor 的指向
    if (count > frame.objectCapacity)
    {
        const std::size_t capacity = std::min(temporalObjectLimit_, std::max(count, frame.objectCapacity * 2));
        void* mapped = nullptr;
        GpuBuffer objects = createMappedTemporalBuffer(*context_, capacity * sizeof(TemporalObjectData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "Temporal object history", mapped);
        frame.objects = std::move(objects);
        frame.objectsMapped = mapped;
        frame.objectCapacity = capacity;

        writeTemporalGeometryDescriptors(frame);
    }

    TemporalFrameUniforms uniforms{};
    uniforms.currentView = data.camera.view;
    uniforms.currentViewProjectionUnjittered = data.camera.viewProjectionUnjittered;

    // 先用当前矩阵作为历史默认值
    uniforms.previousView = data.camera.view;
    uniforms.previousViewProjectionUnjittered = data.camera.viewProjectionUnjittered;

    // 保存当前带 jitter 的 VP 逆矩阵
    uniforms.inverseCurrentViewProjectionJittered = data.deferredUniforms.inverseViewProjection;

    // 从 view 矩阵得到相机世界位置
    uniforms.cameraPosition = glm::inverse(data.camera.view)[3];
    uniforms.jitterUv = glm::vec4(data.camera.jitterUv, data.camera.jitterUv);

    if (transformHistory_.valid())
    {
        const auto& previous = transformHistory_.previous();
        uniforms.previousView = previous.view;
        uniforms.previousViewProjectionUnjittered = previous.viewProjectionUnjittered;
        uniforms.jitterUv.z = previous.jitterUv.x;
        uniforms.jitterUv.w = previous.jitterUv.y;
    }

    const auto extent = swapchain_->extent();
    uniforms.frameInfo = {extent.width, extent.height, 0u, 0u};

    std::vector<TemporalObjectData> objects(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& object = (*data.objects)[i];
        assert(object.objectId != invalidSceneObjectId);
        assert(object.materialId != invalidMaterialId);

        const auto previous = transformHistory_.previousObject(object.objectId, object.pushConstants.model);

        auto& packed = objects[i];
        packed.previousModel = previous.model;
        packed.surfaceKey = {
            static_cast<std::uint32_t>(object.objectId),
            static_cast<std::uint32_t>(object.objectId >> 32u),
            static_cast<std::uint32_t>(object.materialId),
            static_cast<std::uint32_t>(object.materialId >> 32u),
        };
        packed.flags.x = previous.valid ? 1u : 0u;
    }

    std::memcpy(frame.uniformsMapped, &uniforms, sizeof(uniforms));
    VK_CHECK(frame.uniforms.flush(0, sizeof(uniforms)));

    if (!objects.empty())
    {
        const VkDeviceSize bytes = objects.size() * sizeof(TemporalObjectData);
        std::memcpy(frame.objectsMapped, objects.data(), static_cast<std::size_t>(bytes));
        VK_CHECK(frame.objects.flush(0, bytes));
    }
    frame.prepared = true;
    temporalDataUnavailableReason_.clear();
}

void Renderer::destroyTemporalGeometryResources() noexcept
{
    const VkDevice device = context_->device();

    if (temporalGeometryPool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, temporalGeometryPool_, nullptr);
    }
    if (temporalGeometryPipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, temporalGeometryPipelineLayout_, nullptr);
    }
    if (temporalGeometrySetLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, temporalGeometrySetLayout_, nullptr);
    }

    temporalGeometryPool_ = VK_NULL_HANDLE;
    temporalGeometryPipelineLayout_ = VK_NULL_HANDLE;
    temporalGeometrySetLayout_ = VK_NULL_HANDLE;

    for (auto& frame : temporalGeometryFrames_)
    {
        frame.uniforms.reset();
        frame.objects.reset();
        frame.uniformsMapped = nullptr;
        frame.objectsMapped = nullptr;
        frame.descriptorSet = VK_NULL_HANDLE;
        frame.objectCapacity = 0;
        frame.prepared = false;
    }

    temporalObjectLimit_ = 0;
    temporalDataUnavailableReason_.clear();
}