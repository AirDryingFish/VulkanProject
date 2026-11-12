#include "app/Application.hpp"
#include "render/ShaderTypes.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace
{
constexpr uint32_t descriptorSetGroupCount = 2; // frame + skybox
constexpr uint32_t skyboxImageDescriptorCount = 1;
} // namespace

void Application::createDescriptorPool()
{
    const uint32_t frameCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    // 每个飞行帧槽有两个 UBO 描述符，frame/skybox set 引用该槽同一份 UBO。
    poolSizes[0].descriptorCount = frameCount * descriptorSetGroupCount;

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    // pool 容量按描述符数量计算：每帧槽 3 张 IBL + 2 个阴影采样绑定 + 1 张天空盒，
    // 每个材质另需 5 张纹理；maxSets 则单独约束能够分配多少个 set。
    poolSizes[1].descriptorCount =
        frameCount * static_cast<uint32_t>(frameImageDescriptorCount + skyboxImageDescriptorCount) +
        materialPoolCapacity * static_cast<uint32_t>(materialImageDescriptorCount);

    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = frameCount;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    // 允许单独 vkFreeDescriptorSets，以便导入失败回滚和场景切换时回收材质 set。
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = frameCount * descriptorSetGroupCount + materialPoolCapacity;

    VK_CHECK(vkCreateDescriptorPool(context.device(), &poolInfo, nullptr, &descriptorPool));

    mainDeletionQueue.pushFunction([this, pool = descriptorPool]() mutable
                                   { vkDestroyDescriptorPool(context.device(), pool, nullptr); });
}

void Application::createFrameDescriptorSets()
{
    // set=0 提供整帧共享数据；每个槽绑定自己的 UBO/阴影资源，IBL 贴图跨槽共享。
    // 这里的 binding 顺序必须与 GraphicsPipeline.cpp 和 shader 中的声明一致。
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, renderer.frameDescriptorSetLayout());

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();
    frameDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    VK_CHECK(vkAllocateDescriptorSets(context.device(), &allocInfo, frameDescriptorSets.data()));

    std::array<VkDescriptorImageInfo, frameImageDescriptorCount> imageInfos{};
    imageInfos[0] = environment.irradianceDescriptorInfo();
    imageInfos[1] = environment.prefilterDescriptorInfo();
    imageInfos[2] = environment.brdfLutDescriptorInfo();

    for (std::size_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; ++frameIndex)
    {
        imageInfos[3] = renderer.shadowDescriptorInfo(frameIndex);
        imageInfos[4] = renderer.shadowPreviewDescriptorInfo(frameIndex);

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[frameIndex].get();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkDescriptorBufferInfo lightInfo{};
        lightInfo.buffer = pointLightBuffers[frameIndex].get();
        lightInfo.offset = 0;
        lightInfo.range = static_cast<VkDeviceSize>(pointLightBufferBytes);

        std::array<VkWriteDescriptorSet, frameImageDescriptorCount + 2> descriptorWrites{};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = frameDescriptorSets[frameIndex];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        for (uint32_t binding = 1; binding <= static_cast<uint32_t>(frameImageDescriptorCount); ++binding)
        {
            descriptorWrites[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[binding].dstSet = frameDescriptorSets[frameIndex];
            descriptorWrites[binding].dstBinding = binding;
            descriptorWrites[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[binding].descriptorCount = 1;
            descriptorWrites[binding].pImageInfo = &imageInfos[binding - 1];
        }
        descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[6].dstSet = frameDescriptorSets[frameIndex];
        descriptorWrites[6].dstBinding = 6;
        descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[6].descriptorCount = 1;
        descriptorWrites[6].pBufferInfo = &lightInfo;

        vkUpdateDescriptorSets(context.device(), static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }
}

void Application::createSkyboxDescriptorSets()
{
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, renderer.skyboxDescriptorSetLayout());

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    skyboxDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    VK_CHECK(vkAllocateDescriptorSets(context.device(), &allocInfo, skyboxDescriptorSets.data()));

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i].get();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkDescriptorImageInfo imageInfo = environment.skyboxDescriptorInfo();

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = skyboxDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = skyboxDescriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(context.device(), static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }
}
