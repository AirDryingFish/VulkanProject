#include "ui/backend/ImGuiBackend.hpp"
#include "render/Renderer.hpp"
#include "vulkan/presentation/Swapchain.hpp"
#include "vulkan/context/VulkanContext.hpp"
#include "vulkan/VulkanCheck.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

void ImGuiBackend::initialize(VulkanContext& context, const Renderer& renderer, const Swapchain& swapchain,
                              GLFWwindow* window)
{
    // ImGui Vulkan 后端也要通过描述符采样字体/UI 纹理；独立的 pool 避免占用场景材质预算。
    // 后端关闭必须早于 pool 销毁，因此按依赖顺序注册清理。
    const std::array<VkDescriptorPoolSize, 3> poolSizes{{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
    }};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorPool(context.device(), &poolInfo, nullptr, &imguiDescriptorPool));
    deletionQueue_.pushFunction([device = context.device(), pool = imguiDescriptorPool]()
                                { vkDestroyDescriptorPool(device, pool, nullptr); });

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    deletionQueue_.pushFunction([]() { ImGui::DestroyContext(); });

    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForVulkan(window, true))
    {
        throw std::runtime_error("failed to init imgui glfw backend!");
    }
    deletionQueue_.pushFunction([]() { ImGui_ImplGlfw_Shutdown(); });

    QueueFamilyIndices indices = context.queueFamilies();

    const VkFormat presentFormat = swapchain.format();
    const bool useDynamicPresent = renderer.useDynamicRenderingForPresent();

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = context.capabilities().instanceVersion;
    initInfo.Instance = context.instance();
    initInfo.PhysicalDevice = context.physicalDevice();
    initInfo.Device = context.device();
    initInfo.QueueFamily = indices.graphicsFamily.value();
    initInfo.Queue = context.graphicsQueue();
    initInfo.DescriptorPool = imguiDescriptorPool;

    initInfo.UseDynamicRendering = useDynamicPresent;
    initInfo.PipelineInfoMain.RenderPass = useDynamicPresent ? VK_NULL_HANDLE : renderer.presentRenderPass();

    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.MinImageCount = std::max(2u, swapchain.minImageCount());
    initInfo.ImageCount = static_cast<uint32_t>(swapchain.imageCount());

    if (useDynamicPresent)
    {
        auto& renderingInfo = initInfo.PipelineInfoMain.PipelineRenderingCreateInfo;

        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.viewMask = 0;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &presentFormat;
        renderingInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
        renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
    }

    if (!ImGui_ImplVulkan_Init(&initInfo))
    {
        throw std::runtime_error("failed to init imgui vulkan backend!");
    }
    deletionQueue_.pushFunction([]() { ImGui_ImplVulkan_Shutdown(); });
}

ImGuiBackend::~ImGuiBackend() noexcept
{
    shutdown();
}

void ImGuiBackend::shutdown() noexcept
{
    deletionQueue_.flush();
}
