#include "TriangleApplication.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace
{
constexpr uint32_t descriptorSetGroupCount = 2; // frame + skybox
constexpr uint32_t skyboxImageDescriptorCount = 1;
}

void TriangleApplication::createDescriptorPool()
{
    const uint32_t frameCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    // 每个飞行帧：一个 Frame UBO + 一个 Skybox UBO。
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
    // 每帧 3 个 Frame IBL 图片、1 个 Skybox 图片，
    // 每个 Material 6 张纹理。
    poolSizes[1].descriptorCount =
        frameCount *
        static_cast<uint32_t>(frameImageDescriptorCount + skyboxImageDescriptorCount) +
        maxMaterialCount * static_cast<uint32_t>(materialImageDescriptorCount);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    // 后续导入失败时，可以调用 vkFreeeDescriptorSets() 归还尚未发布的材质 set
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = frameCount * descriptorSetGroupCount + maxMaterialCount;

    VK_CHECK(vkCreateDescriptorPool(context.device(), &poolInfo, nullptr, &descriptorPool));

    mainDeletionQueue.pushFunction([this, pool = descriptorPool]() mutable {
        vkDestroyDescriptorPool(context.device(), pool, nullptr);
    });
}

void TriangleApplication::createFrameDescriptorSets()
{
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, renderer.frameDescriptorSetLayout());
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();
    frameDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    VK_CHECK(vkAllocateDescriptorSets(context.device(), &allocInfo, frameDescriptorSets.data()));

    std::array<VkDescriptorImageInfo, frameImageDescriptorCount> imageInfos{};
    imageInfos[0] = {irradianceSampler.get(), irradianceImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[1] = {prefilterSampler.get(), prefilterImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    imageInfos[2] = {brdfLUTSampler.get(), brdfLUTImage.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    for (std::size_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; frameIndex++)
    {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[frameIndex].get();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        std::array<VkWriteDescriptorSet, frameImageDescriptorCount + 1> descriptorWrites{};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = frameDescriptorSets[frameIndex];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        for (uint32_t binding = 1; binding < static_cast<uint32_t>(descriptorWrites.size()); ++binding)
        {
            descriptorWrites[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[binding].dstSet = frameDescriptorSets[frameIndex];
            descriptorWrites[binding].dstBinding = binding;
            descriptorWrites[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[binding].descriptorCount = 1;
            descriptorWrites[binding].pImageInfo = &imageInfos[binding - 1];
        }
        vkUpdateDescriptorSets(context.device(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void TriangleApplication::createMaterialDescriptorSets()
{
    allocateMaterialDescriptorSets(materialLibrary);
}

// 启动时：为 library 中尚未分配 Set 的默认材质创建 descriptor
// allocateMaterialDescriptorSets(materialLibrary);

// 后续导入时：只为本次新创建的材质分配 descriptor
// allocateMaterialDescriptorSets(materialUpload.materials);
void TriangleApplication::allocateMaterialDescriptorSets(const std::vector<MaterialHandle> &materials)
{
    if (materials.empty())
    {
        return;
    }

    if (descriptorPool == VK_NULL_HANDLE)
    {
        throw std::logic_error("descriptor pool has not been created");
    }
    ensureMaterialDescriptorCapacity(materials.size());

    // 1. 检查本批材质都还没有分配 Set
    for (size_t index = 0; index < materials.size(); ++index)
    {
        const MaterialHandle& material = materials[index];
        if (!material)
        {
            throw std::runtime_error("material list contains a null handle");
        }
        if (material->descriptorSet != VK_NULL_HANDLE)
        {
            throw std::logic_error("material descriptor set already exists");
        }

        // 同一个材质对象不能在同一批中重复出现
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (materials[previous] == material)
            {
                throw std::logic_error("material list contains a duplicate handle");
            }
        }
    }

    // 2. 每个材质对应一个 set，所有 set 使用相同的 layout
    const std::uint32_t count = static_cast<std::uint32_t>(materials.size());
    std::vector<VkDescriptorSetLayout> layouts(count, renderer.materialDescriptorSetLayout());
    std::vector<VkDescriptorSet> sets(count, VK_NULL_HANDLE);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = count;
    allocInfo.pSetLayouts = layouts.data();

    VK_CHECK(vkAllocateDescriptorSets(
        context.device(), &allocInfo, sets.data()
    ));
    allocatedMaterialSetCount += count;

    // 3. 将分配结果交给对应材质
    for (std::size_t index = 0; index < materials.size(); ++index)
    {
        materials[index]->descriptorSet = sets[index];
    }
    try
    {
        for (const MaterialHandle& material : materials)
        {
            writeMaterialDescriptorSet(*material);
        }
    }
    catch(...)
    {
        // 写入失败，这个 set 还没有交给绘制流程
        VK_CHECK(vkFreeDescriptorSets(
            context.device(), descriptorPool, count, sets.data()
        ));
        allocatedMaterialSetCount -= count;
        for (const MaterialHandle& material: materials)
        {
            material->descriptorSet = VK_NULL_HANDLE;
        }
        throw;
    }
}

void TriangleApplication::writeMaterialDescriptorSet(Material &material)
{
    // 一个 Material 只能创建一次 DescriptorSet
    if (material.descriptorSet == VK_NULL_HANDLE)
    {
        throw std::logic_error("material descriptor set has not been allocated");
    }

    // 1. 按 shader binding 顺序收集材质的 5 个纹理槽
    const std::array<const MaterialTextureSlot*, materialImageDescriptorCount> slots{
        &material.baseColorTexture,
        &material.normalTexture,
        &material.metallicRoughnessTexture,
        &material.aoTexture,
        &material.emissiveTexture
    };
    std::array<VkDescriptorImageInfo, materialImageDescriptorCount> imageInfos{};

    // 2. 为每个纹理 binding 构造 VkDescriptorImageInfo
    for (std::size_t binding = 0; binding < slots.size(); ++binding)
    {
        const MaterialTextureSlot& slot = *slots[binding];
        const std::string context = "material \"" + material.name + "\" binding[" + std::to_string(binding) + "]";
        if (!slot.image || !slot.sampler)
        {
            throw std::runtime_error(context + ": missing image or sampler");
        }
        if (slot.image->image.view() == VK_NULL_HANDLE ||
            slot.sampler->sampler.get() == VK_NULL_HANDLE)
        {
            throw std::runtime_error(context + ": invalid image view or sampler");
        }
        if (slot.texCoord > 1u)
        {
            throw std::runtime_error(context + ": only UV0 and UV1 are supported");
        }
        // VkDescriptorImageInfo 描述一个 shader 可访问的纹理。
        // 对应 shader 中类似： layout(set = 1, binding = X)
        imageInfos[binding] = {
            slot.sampler->sampler.get(),
            slot.image->image.view(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL // shader 读取时要求 image 已经被转换到这个 layout
        };
    }
    // 3. 从 DescriptorPool 中申请一个 DescripotrSet
    // DescripotrSetLayout 描述这个 set 的结构: binding 0/1 是什么类型

    // 4. 准备 DescriptorSet 的写入
    // DescriptorSet 现在虽然 allocate 出来了，但里面还没有绑定具体的 texture
    // VkWriteDescriptorSet 就是在描述： “把哪个资源写到哪个 binding”
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

void TriangleApplication::ensureMaterialDescriptorCapacity(std::size_t additionalCount) const
{
    if (allocatedMaterialSetCount > maxMaterialCount || additionalCount > maxMaterialCount - allocatedMaterialSetCount)
    {
        throw std::runtime_error("material descriptor capacity exceeded: allocated=" + std::to_string(allocatedMaterialSetCount) +
                                ", requested=" + std::to_string(additionalCount) +
                                ", capacity=" + std::to_string(maxMaterialCount));
    }
}

void TriangleApplication::createSkyboxDescriptorSets()
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
