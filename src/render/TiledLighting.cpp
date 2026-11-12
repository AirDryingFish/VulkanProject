#include "core/AppConfig.hpp"
#include "core/FileUtils.hpp"
#include "render/DeferredMath.hpp"
#include "render/Renderer.hpp"
#include "vulkan/Swapchain.hpp"
#include "vulkan/VulkanCheck.hpp"
#include "vulkan/VulkanContext.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

void Renderer::createTileBuffers()
{
    const auto grid = deferred::makeTileGrid(swapchain_->extent().width, swapchain_->extent().height);
    tileCountX_ = grid.tilesX;
    tileCountY_ = grid.tilesY;

    tiledConfigurationSupported_ = true;
    tiledUnavailableReason_.clear();
    const auto reject = [&](const char* reason){
        if (tiledConfigurationSupported_)
        {
            tiledUnavailableReason_ = reason;
        }
        tiledConfigurationSupported_ = false;
    };

    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(context_->physicalDevice(), &familyCount, nullptr);
    // families: 保存所有队列族属性的数组
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(context_->physicalDevice(), &familyCount, families.data());
    // family: 当前选用的 graphics 队列族的索引
    const auto family = context_->queueFamilies().graphicsFamily.value();

    // 例如某个 Gpu 返回：
    // 队列族索引               支持的功能
    //     0                 Graphics Compute Transfer
    //     1                 Compute Transfer
    //     2                 Transfer
    // 那么 families[0] 就是第 0 个队列族的属性，里面的 queueFlags 记录它支持哪些功能
    if (family >= familyCount || (families[family].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0)
    {
        reject("The selected graphics queue does not support Compute.");
    }


    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context_->physicalDevice(), &properties);
    const auto& limits = properties.limits;
    constexpr std::uint32_t requiredShadredBytes = 4u * 16u + 16u;

    if (limits.maxComputeWorkGroupSize[0] < deferred::tileSize ||
        limits.maxComputeWorkGroupSize[1] < deferred::tileSize ||
        limits.maxComputeWorkGroupSize[2] < 1 ||
        limits.maxComputeWorkGroupInvocations < deferred::tileSize * deferred::tileSize ||
        limits.maxComputeSharedMemorySize < requiredShadredBytes)
    {
        reject("The device cannot run a 16 x 16 compute workgroup");
    }
    if (grid.count == 0 ||
        grid.tilesX > limits.maxComputeWorkGroupCount[0] ||
        grid.tilesY > limits.maxComputeWorkGroupCount[1] ||
        limits.maxComputeWorkGroupCount[2] < 1)
    {
        reject("The tile grid exceeds Compute dispatch limits.");
    }
    if (limits.maxPerStageDescriptorStorageBuffers < 3 ||
        limits.maxDescriptorSetStorageBuffers < 3 ||
        limits.maxPerStageDescriptorUniformBuffers < 2)
    {
        reject("The device does not support the required Compute descriptors.");
    }

    constexpr VkDeviceSize headerStride = sizeof(glm::uvec4);
    // 每个 tile 最多保存 128 个灯索引，每个索引占 4 bytes
    constexpr VkDeviceSize indexStride = deferred::maxLightsPerTile * sizeof(std::uint32_t);
    VkDeviceSize headerBytes = headerStride;
    VkDeviceSize indexBytes = indexStride;

    if (grid.count > std::numeric_limits<VkDeviceSize>::max() / indexStride ||
        grid.count > std::numeric_limits<std::uint32_t>::max() / deferred::maxLightsPerTile)
    {
        reject("The tile buffer size exceeds the supported integer range.");
    }
    else if (grid.count != 0)
    {
        const VkDeviceSize requestedHeaders = grid.count * headerStride;
        const VkDeviceSize requestedIndices = grid.count * indexStride;
        if (requestedHeaders > limits.maxStorageBufferRange ||
            requestedIndices > limits.maxStorageBufferRange)
        {
            reject("The tile buffers exceed maxStorageBufferRange.");
        }
        else if (tiledConfigurationSupported_)
        {
            headerBytes = requestedHeaders;
            indexBytes = requestedIndices;
        }
    }


    std::array<GpuBuffer, MAX_FRAMES_IN_FLIGHT> headers{};
    std::array<GpuBuffer, MAX_FRAMES_IN_FLIGHT> indices{};
    for (std::size_t slot = 0; slot < headers.size(); ++slot)
    {
        BufferDesc desc{};
        desc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        desc.requiredMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        desc.size = headerBytes;
        desc.debugName = "Tile light headers";
        headers[slot] = context_->createBuffer(desc);

        desc.size = indexBytes;
        desc.debugName = "Tile light indices";
        indices[slot] = context_->createBuffer(desc);
    }

    for (std::size_t slot = 0; slot < deferredTargets_.size(); ++slot)
    {
        auto& target = deferredTargets_[slot];
        target.tileHeaders = std::move(headers[slot]);
        target.tileIndices = std::move(indices[slot]);
        target.tileListsWritten = false;
    }

    std::cout << "Tile lists: " << tileCountX_ << 'x' << tileCountY_
              << ", capacity=" << deferred::maxLightsPerTile
              << ", bytes per slot=" << headerBytes + indexBytes
              << (tiledConfigurationSupported_ ? ", Compute ready" : ", disabled")
              << '\n';
    if (!tiledConfigurationSupported_)
    {
        std::cout << "Tiled unavailable: " << tiledUnavailableReason_ << '\n';
    }

}

void Renderer::createTilePipeline()
{
    if (!tiledConfigurationSupported_ || tileCullPipeline_ != VK_NULL_HANDLE)
    {
        return;
    }

    auto module = context_->createShaderModule(readBinaryFile(TILE_LIGHTS_COMPUTE_SHADER_PATH));

    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module.get();
    info.stage.pName = "main";
    info.layout = deferredPipelineLayout_;
    VK_CHECK(vkCreateComputePipelines(
        context_->device(),
        VK_NULL_HANDLE,
        1,
        &info,
        nullptr,
        &tileCullPipeline_
    ));
}

// 把 “生成 tile 灯列表” 的 compute 录制到 command buffer，并处理灯列表的 buffer 读写同步
void Renderer::recordTileLightLists(const FrameToken &token, const RenderFrameData &data)
{
    auto& target = deferredTargets_.at(token.frameIndex);
    const auto& functions = context_->deviceFunctions();

    // beforeDispatch: 决定录制 compute 前还是后的 barrier
    const auto barrier = [&](bool beforeDispatch)
    {
        // 一个保护 tileHeaders; 一个保护 tileIndices
        std::array<VkBufferMemoryBarrier2, 2> buffers{};
        const std::array<VkBuffer, 2> handles{target.tileHeaders.get(), target.tileIndices.get()};

        for (std::size_t i = 0; i < buffers.size(); ++i)
        {
            auto& b = buffers[i];
            b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.buffer = handles[i];
            b.size = VK_WHOLE_SIZE;
            // compute 之后： 默认填写 compute写 -> fragment读
            b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            b.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;

            // compute 之前： 旧访问 -> compute写
            if (beforeDispatch)
            {
                b.srcStageMask |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                b.srcAccessMask |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
                b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                b.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            }
        }

        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.bufferMemoryBarrierCount = static_cast<std::uint32_t>(buffers.size());
        dependency.pBufferMemoryBarriers = buffers.data();
        functions.cmdPipelineBarrier2(token.commandBuffer, &dependency);
    };

    // 第一次： target.tileListsWritten = false -> compute 前 barrier
    // 第二次及后续： target.tileListsWritten = true -> compute 后 barrier
    if (target.tileListsWritten)
    {
        barrier(true);
    }

    vkCmdBindPipeline(token.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, tileCullPipeline_);

    // 给 compute shader
    const std::array<VkDescriptorSet, 2> sets{data.frameDescriptorSet, target.inputSet};
    vkCmdBindDescriptorSets(token.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, deferredPipelineLayout_, 0, static_cast<std::uint32_t>(sets.size()), sets.data(), 0, nullptr);
    // 向 command buffer 录制一条 compute 调度命令，启动 tileCountX_ x tileCountY_ 个工作组
    // compute shader 内部，通过 uvec2 tileCoord = gl_WorkGroupID.xy 区分自己负责哪个 tile
    vkCmdDispatch(token.commandBuffer, tileCountX_, tileCountY_, 1);

    barrier(false);
    target.tileListsWritten = true;
}

void Renderer::destroyTileResources() noexcept
{
    if (tileCullPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(context_->device(), tileCullPipeline_, nullptr);
        tileCullPipeline_ = VK_NULL_HANDLE;
    }
    for (auto &target : deferredTargets_)
    {
        target.tileHeaders.reset();
        target.tileIndices.reset();
        target.tileListsWritten = false;
    }
    tiledConfigurationSupported_ = false;
    tiledUnavailableReason_.clear();
    tileCountX_ = 0;
    tileCountY_ = 0;
}

bool Renderer::tiledLightingAvailable() const noexcept
{
    return deferredLightingAvailable() && tiledConfigurationSupported_ && tileCullPipeline_ != VK_NULL_HANDLE;
}

const std::string &Renderer::tiledUnavailableReason() const noexcept
{
    return tiledUnavailableReason_;
}
