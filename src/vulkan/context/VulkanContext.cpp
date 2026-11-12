#include "vulkan/context/VulkanContext.hpp"
#include "vulkan/debug/DebugUtils.hpp"

#include <cstdio>
#include <stdexcept>

// 生命周期：按依赖顺序建立上下文，异常时释放已创建资源。
VulkanContext::~VulkanContext() noexcept
{
    shutdown();
}

void VulkanContext::initialize(GLFWwindow* window, const VulkanContextConfig& config)
{
    if (window == nullptr)
    {
        throw std::invalid_argument("VulkanContext requires a window");
    }

    if (instance_ != VK_NULL_HANDLE)
    {
        throw std::logic_error("Vulkan is already initialized");
    }

    validationEnabled_ = config.enableValidation;
    capabilities_ = {};

    try
    {
        queryLoaderVersion();

        createInstance(config);
        setupDebugMessenger();
        createSurface(window);
        pickPhysicalDevice(config);
        createLogicalDevice();
        loadDeviceFunctions();
        createAllocator();

        logCapabilities();
    }
    catch (...)
    {
        shutdown();
        throw;
    }
}

void VulkanContext::shutdown() noexcept
{
    if (device_ != VK_NULL_HANDLE)
    {
        const VkResult result = vkDeviceWaitIdle(device_);

        if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
        {
            std::fprintf(stderr, "VulkanContext waitIdle failed: %d\n", static_cast<int>(result));
        }
    }

    if (allocator_ != nullptr)
    {
        vmaDestroyAllocator(allocator_);
        allocator_ = nullptr;
    }

    deviceFunctions_ = {};

    if (device_ != VK_NULL_HANDLE)
    {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    graphicsQueue_ = VK_NULL_HANDLE;
    presentQueue_ = VK_NULL_HANDLE;
    queueFamilies_ = {};

    if (instance_ != VK_NULL_HANDLE && debugMessenger_ != VK_NULL_HANDLE)
    {
        DestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);

        debugMessenger_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE && surface_ != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);

        surface_ = VK_NULL_HANDLE;
    }

    physicalDevice_ = VK_NULL_HANDLE;
    msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;

    if (instance_ != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }

    capabilities_ = {};

    validationEnabled_ = false;
}

VkResult VulkanContext::waitIdle() const noexcept
{
    // 没有 device，因此也没有任何 gpu 工作需要等待
    if (device_ == VK_NULL_HANDLE)
    {
        return VK_SUCCESS;
    }
    return vkDeviceWaitIdle(device_);
}

// 借用句柄与能力快照；资源持有方必须先于上下文释放。
VkInstance VulkanContext::instance() const noexcept
{
    return instance_;
}

VkSurfaceKHR VulkanContext::surface() const noexcept
{
    return surface_;
}

VkPhysicalDevice VulkanContext::physicalDevice() const noexcept
{
    return physicalDevice_;
}

VkDevice VulkanContext::device() const noexcept
{
    return device_;
}

VkQueue VulkanContext::graphicsQueue() const noexcept
{
    return graphicsQueue_;
}

VkQueue VulkanContext::presentQueue() const noexcept
{
    return presentQueue_;
}

VmaAllocator VulkanContext::allocator() const noexcept
{
    return allocator_;
}

const QueueFamilyIndices& VulkanContext::queueFamilies() const noexcept
{
    return queueFamilies_;
}

const VulkanCapabilities& VulkanContext::capabilities() const noexcept
{
    return capabilities_;
}

const VulkanDeviceFunctions& VulkanContext::deviceFunctions() const noexcept
{
    return deviceFunctions_;
}

VkSampleCountFlagBits VulkanContext::msaaSamples() const noexcept
{
    return msaaSamples_;
}
