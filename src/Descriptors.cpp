#include "TriangleApplication.hpp"

#include <array>
#include <stdexcept>

namespace
{

constexpr uint32_t descriptorSetGroupCount = 2; // model + skybox
}

void TriangleApplication::createDescriptorPool()
{
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) * descriptorSetGroupCount;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) * descriptorSetGroupCount * pbrImageDescriptorCount;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) * descriptorSetGroupCount;

    VK_CHECK(vkCreateDescriptorPool(context.device(), &poolInfo, nullptr, &descriptorPool));

    mainDeletionQueue.pushFunction([this, pool = descriptorPool]() mutable {
        vkDestroyDescriptorPool(context.device(), pool, nullptr);
    });
}

void TriangleApplication::createTextureDescriptorSets(
    const std::array<VkDescriptorImageInfo, pbrImageDescriptorCount> &imageInfos,
    std::vector<VkDescriptorSet> &targetDescriptorSets)
{
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, renderer.descriptorSetLayout());

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    targetDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    VK_CHECK(vkAllocateDescriptorSets(context.device(), &allocInfo, targetDescriptorSets.data()));

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i].get();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        std::array<VkWriteDescriptorSet, pbrImageDescriptorCount + 1> descriptorWrites{};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = targetDescriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        for (uint32_t binding = 1; binding < static_cast<uint32_t>(descriptorWrites.size()); binding++)
        {
            descriptorWrites[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[binding].dstSet = targetDescriptorSets[i];
            descriptorWrites[binding].dstBinding = binding;
            descriptorWrites[binding].dstArrayElement = 0;
            descriptorWrites[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[binding].descriptorCount = 1;
            descriptorWrites[binding].pImageInfo = &imageInfos[binding - 1];
        }

        vkUpdateDescriptorSets(context.device(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void TriangleApplication::createDescriptorSets()
{
    std::array<VkDescriptorImageInfo, pbrImageDescriptorCount> imageInfos{};
    imageInfos[0] = {textureSampler, textureImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[1] = {textureSampler, normalImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[2] = {textureSampler, metallicImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[3] = {textureSampler, roughnessImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[4] = {textureSampler, aoImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[5] = {skyboxSampler, skyboxImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[6] = {irradianceSampler, irradianceImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[7] = {prefilterSampler, prefilterImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[8] = {brdfLUTSampler, brdfLUTImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    createTextureDescriptorSets(imageInfos, descriptorSets);
}

void TriangleApplication::createSkyboxDescriptorSets()
{
    std::array<VkDescriptorImageInfo, pbrImageDescriptorCount> imageInfos{};
    imageInfos[0] = {skyboxSampler, skyboxImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[1] = {textureSampler, normalImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[2] = {textureSampler, metallicImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[3] = {textureSampler, roughnessImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[4] = {textureSampler, aoImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[5] = {skyboxSampler, skyboxImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[6] = {irradianceSampler, irradianceImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[7] = {prefilterSampler, prefilterImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[8] = {brdfLUTSampler, brdfLUTImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    createTextureDescriptorSets(imageInfos, skyboxDescriptorSets);
}
