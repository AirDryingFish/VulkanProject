#include "app/Application.hpp"
#include "render/ShaderTypes.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

void Application::createMaterialDescriptorSets()
{
    allocateMaterialDescriptorSets(materialLibrary);
}

// 启动时传入默认材质，导入时只传入本次新增材质；整批分配/写入失败时统一回收。
void Application::allocateMaterialDescriptorSets(const std::vector<MaterialHandle>& materials)
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

    VK_CHECK(vkAllocateDescriptorSets(context.device(), &allocInfo, sets.data()));
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
    catch (...)
    {
        // 写入失败，这个 set 还没有交给绘制流程
        VK_CHECK(vkFreeDescriptorSets(context.device(), descriptorPool, count, sets.data()));
        allocatedMaterialSetCount -= count;
        for (const MaterialHandle& material : materials)
        {
            material->descriptorSet = VK_NULL_HANDLE;
        }
        throw;
    }
}

void Application::writeMaterialDescriptorSet(Material& material)
{
    // 本函数只写入已分配的 set，分配与异常回收统一由 allocateMaterialDescriptorSets 管理。
    if (material.descriptorSet == VK_NULL_HANDLE)
    {
        throw std::logic_error("material descriptor set has not been allocated");
    }

    // 1. 按 shader binding 顺序收集材质的 5 个纹理槽
    const std::array<const MaterialTextureSlot*, materialImageDescriptorCount> slots{
        &material.baseColorTexture, &material.normalTexture, &material.metallicRoughnessTexture, &material.aoTexture,
        &material.emissiveTexture};
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
        if (slot.image->image.view() == VK_NULL_HANDLE || slot.sampler->sampler.get() == VK_NULL_HANDLE)
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
            slot.sampler->sampler.get(), slot.image->image.view(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL // shader 读取时要求 image 已经被转换到这个 layout
        };
    }
    // 3. 把具体 image view/sampler 写到 set=1 的各个 binding。
    // 更新描述符不会上传图片或执行布局转换，这些工作必须已在资源上传阶段完成。
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
    vkUpdateDescriptorSets(context.device(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0,
                           nullptr);
}

void Application::ensureMaterialDescriptorCapacity(std::size_t additionalCount) const
{
    // 普通导入遵守常驻上限；切换事务可暂用额外预留，直到旧场景描述符被释放。
    const std::uint32_t capacity = preparingScenePreset ? materialPoolCapacity : maxMaterialCount;
    if (allocatedMaterialSetCount > capacity || additionalCount > capacity - allocatedMaterialSetCount)
    {
        throw std::runtime_error(
            "material descriptor capacity exceeded: allocated=" + std::to_string(allocatedMaterialSetCount) +
            ", requested=" + std::to_string(additionalCount) + ", capacity=" + std::to_string(capacity));
    }
}

void Application::releaseSceneMaterialDescriptors(std::size_t first, std::size_t end)
{
    std::vector<VkDescriptorSet> sets;
    sets.reserve(end - first);
    for (std::size_t i = first; i < end; ++i)
        if (materialLibrary[i]->descriptorSet != VK_NULL_HANDLE)
            sets.push_back(materialLibrary[i]->descriptorSet);
    if (sets.empty())
        return;
    VK_CHECK(
        vkFreeDescriptorSets(context.device(), descriptorPool, static_cast<std::uint32_t>(sets.size()), sets.data()));
    allocatedMaterialSetCount -= static_cast<std::uint32_t>(sets.size());
    for (std::size_t i = first; i < end; ++i)
        materialLibrary[i]->descriptorSet = VK_NULL_HANDLE;
}
