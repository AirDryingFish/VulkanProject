#include "TriangleApplication.hpp"

#include <array>
#include <stdexcept>

namespace
{
constexpr uint32_t descriptorSetGroupCount = 2; // model + skybox
constexpr uint32_t skyboxImageDescriptorCount = 1;
constexpr uint32_t maxMaterialCount = 128;
}

void TriangleApplication::createDescriptorPool()
{
    const uint32_t frameCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    // Frame 0：Scene set + Skybox set
    // Frame 1：Scene set + Skybox set  
    // 都需要ubo
    poolSizes[0].descriptorCount = frameCount * descriptorSetGroupCount;


    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    // imageDescriptor
    // 1  Base Color
    // 2  Normal
    // 3  Metallic
    // 4  Roughness
    // 5  AO
    // 6  Environment
    // 7  Irradiance
    // 8  Prefilter
    // 9  BRDF LUT
    // skyboxDescriptor
    // 1  Skybox cubemap
    poolSizes[1].descriptorCount =         
        frameCount * 
        static_cast<uint32_t>(pbrImageDescriptorCount + skyboxImageDescriptorCount) + 
        maxMaterialCount * static_cast<uint32_t>(materialImageDescriptorCount);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = frameCount * descriptorSetGroupCount + maxMaterialCount;

    VK_CHECK(vkCreateDescriptorPool(context.device(), &poolInfo, nullptr, &descriptorPool));

    mainDeletionQueue.pushFunction([this, pool = descriptorPool]() mutable {
        vkDestroyDescriptorPool(context.device(), pool, nullptr);
    });
}

void TriangleApplication::createTextureDescriptorSets(
    const std::array<VkDescriptorImageInfo, pbrImageDescriptorCount> &imageInfos,
    std::vector<VkDescriptorSet> &targetDescriptorSets)
{
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, renderer.sceneDescriptorSetLayout());

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
    const Material& material = *defaultMaterial;
    std::array<VkDescriptorImageInfo, pbrImageDescriptorCount> imageInfos{};
    imageInfos[0] = {textureSampler.get(), material.baseColorTexture->image.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[1] = {textureSampler.get(), material.normalTexture->image.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[2] = {textureSampler.get(), material.metallicTexture->image.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[3] = {textureSampler.get(), material.roughnessTexture->image.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[4] = {textureSampler.get(), material.aoTexture->image.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[5] = {skyboxSampler.get(), skyboxImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[6] = {irradianceSampler.get(), irradianceImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[7] = {prefilterSampler.get(), prefilterImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[8] = {brdfLUTSampler.get(), brdfLUTImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    createTextureDescriptorSets(imageInfos, descriptorSets);
}

void TriangleApplication::createMaterialDescriptorSets()
{
    if (materialLibrary.size() > maxMaterialCount)
    {
        throw std::runtime_error("material descriptor capacity exceeded");
    }
    for (const MaterialHandle& material : materialLibrary)
    {
        if (!material)
        {
            throw std::runtime_error("material library contains null material");
        }
        createMaterialDescriptorSet(*material);
    }
}

void TriangleApplication::createMaterialDescriptorSet(Material &material)
{
    if (material.descriptorSet != VK_NULL_HANDLE)
    {
        throw std::logic_error("material descriptor set already exists");
    }
    if (!material.baseColorTexture ||
        !material.normalTexture ||
        !material.metallicTexture ||
        !material.roughnessTexture ||
        !material.aoTexture ||
        !material.emissiveTexture
    )
    {
        throw std::runtime_error("material contains a null texture");
    }

    VkDescriptorSetLayout layout = renderer.materialDescriptorSetLayout();
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;
    
    VK_CHECK(vkAllocateDescriptorSets(context.device(), &allocInfo, &material.descriptorSet));

    std::array<VkDescriptorImageInfo, materialImageDescriptorCount> imageInfos{};

    imageInfos[0] = {
        textureSampler.get(),
        material.baseColorTexture->image.view(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    imageInfos[1] = {
        textureSampler.get(),
        material.normalTexture->image.view(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    imageInfos[2] = {
        textureSampler.get(),
        material.metallicTexture->image.view(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    imageInfos[3] = {
        textureSampler.get(),
        material.roughnessTexture->image.view(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    imageInfos[4] = {
        textureSampler.get(),
        material.aoTexture->image.view(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    imageInfos[5] = {
        textureSampler.get(),
        material.emissiveTexture->image.view(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
 
    std::array<VkWriteDescriptorSet, materialImageDescriptorCount> descriptorWrites{};
    for (uint32_t binding = 0; binding < static_cast<uint32_t>(descriptorWrites.size()); binding++)
    {
        descriptorWrites[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[binding].dstSet = material.descriptorSet;
        descriptorWrites[binding].dstBinding = binding;
        descriptorWrites[binding].dstArrayElement = 0;
        descriptorWrites[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[binding].descriptorCount = 1;
        descriptorWrites[binding].pImageInfo = &imageInfos[binding];
    }
    vkUpdateDescriptorSets(context.device(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void TriangleApplication::createSkyboxDescriptorSets()
{
    // const Material& material = *defaultMaterial;
    // std::array<VkDescriptorImageInfo, pbrImageDescriptorCount> imageInfos{};
    // imageInfos[0] = {skyboxSampler.get(), skyboxImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    // imageInfos[1] = {textureSampler.get(), material.normalTexture->image.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    // imageInfos[2] = {textureSampler.get(), material.metallicTexture->image.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    // imageInfos[3] = {textureSampler.get(), material.roughnessTexture->image.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    // imageInfos[4] = {textureSampler.get(), material.aoTexture->image.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    // imageInfos[5] = {skyboxSampler.get(), skyboxImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    // imageInfos[6] = {irradianceSampler.get(), irradianceImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    // imageInfos[7] = {prefilterSampler.get(), prefilterImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    // imageInfos[8] = {brdfLUTSampler.get(), brdfLUTImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    // createTextureDescriptorSets(imageInfos, skyboxDescriptorSets);
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

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = skyboxSampler.get();
        imageInfo.imageView = skyboxImage.view();
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

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

        vkUpdateDescriptorSets(context.device(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}
