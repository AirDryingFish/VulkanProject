#include "vulkan/context/VulkanContext.hpp"
#include <GLFW/glfw3.h>

#include "core/AppConfig.hpp"
#include "vulkan/debug/DebugUtils.hpp"
#include "vulkan/VulkanCheck.hpp"
#include "vulkan/context/detail/ApiVersion.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
using vulkan::detail::coreApiVersion;

std::uint32_t selectInstanceApiVersion(std::uint32_t preferred, std::uint32_t minimum, std::uint32_t loader)
{
    if (VK_API_VERSION_VARIANT(preferred) != 0 || VK_API_VERSION_VARIANT(minimum) != 0 ||
        VK_API_VERSION_VARIANT(loader) != 0)
    {
        throw std::invalid_argument("Only standard Vulkan variant 0 is supported");
    }

    preferred = coreApiVersion(preferred);
    minimum = coreApiVersion(minimum);
    loader = coreApiVersion(loader);

    if (minimum < VK_API_VERSION_1_0 || minimum > preferred || preferred > VK_API_VERSION_1_3)
    {
        throw std::invalid_argument("Expected Vulkan 1.0 <= minimum <= preferred <= 1.3");
    }

    const auto selected = std::min(preferred, loader);

    if (selected < minimum)
    {
        throw std::runtime_error("Vulkan loader API is below the required minimum");
    }

    return selected;
};
} // namespace

// Loader 与 Instance：协商 API 上限，再建立窗口所需扩展与表面。
void VulkanContext::queryLoaderVersion()
{
    // 老 loader 可能没有版本查询入口，此时按 1.0 处理
    capabilities_.loaderVersion = VK_API_VERSION_1_0;

    // 获取名为 vkEnumerateInstanceVersion 的函数地址
    const auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (enumerateVersion != nullptr)
    {
        VK_CHECK(enumerateVersion(&capabilities_.loaderVersion));
    }
}

void VulkanContext::createInstance(const VulkanContextConfig& config)
{
    if (validationEnabled_ && !checkValidationLayerSupport())
    {
        throw std::runtime_error("validation layers requested, but not available!");
    }
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = config.applicationName;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion =
        selectInstanceApiVersion(config.apiVersion, config.minimumApiVersion, capabilities_.loaderVersion);

    VkInstanceCreateInfo createInfo{};
    std::vector<const char*> extensions = getRequiredExtensions();
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
#if VULKAN_PROJECT_PLATFORM_MACOS
    // MoltenVK is a Vulkan portability implementation. The Vulkan loader hides
    // portability devices unless the application explicitly opts into them.
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};

    // createInfo.enabledLayerCount = 0;

    if (validationEnabled_)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    }
    else
    {
        createInfo.enabledLayerCount = 0;

        createInfo.pNext = nullptr;
    }

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &instance_));
    // 总结：vk中创建Instance的步骤：
    // 1. creation info 指针
    // 2. allocator callbacks，通常为nullptr
    // 3. 存储新对象handle的变量的指针

    // 记录真正用于创建 Instance 的请求值
    capabilities_.instanceVersion = appInfo.apiVersion;
}

bool VulkanContext::checkValidationLayerSupport() const
{
    uint32_t layerCount;
    VK_CHECK(vkEnumerateInstanceLayerProperties(&layerCount, nullptr));
    std::vector<VkLayerProperties> availableLayers(layerCount);
    VK_CHECK(vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data()));

    for (const char* layerName : validationLayers)
    {
        bool layerFound = false;
        for (const auto& layerProperties : availableLayers)
        {
            if (strcmp(layerName, layerProperties.layerName) == 0)
            {
                layerFound = true;
                break;
            }
        }

        if (!layerFound)
        {
            return false;
        }
    }
    return true;
}

std::vector<const char*> VulkanContext::getRequiredExtensions() const
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    // To understand extensions: e.g. on Windows, the following extensions are required:
    // glfwExtensions[0] -> "VK_KHR_surface"
    // glfwExtensions[1] -> "VK_KHR_win32_surface"
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (validationEnabled_)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

#if VULKAN_PROJECT_PLATFORM_MACOS
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif

    return extensions;
}

void VulkanContext::createSurface(GLFWwindow* window)
{
    VK_CHECK(glfwCreateWindowSurface(instance_, window, nullptr, &surface_));
}

// Validation 与调试：扩展入口只在对应实例/设备存活期间使用。
void VulkanContext::setupDebugMessenger()
{
    if (!validationEnabled_)
    {
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    populateDebugMessengerCreateInfo(createInfo);

    VK_CHECK(CreateDebugUtilsMessengerEXT(instance_, &createInfo, nullptr, &debugMessenger_));
}

void VulkanContext::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) const noexcept
{
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                            VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                            const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                                            void* userData)
{
    std::cerr << "---------------------\nvalidation layer: " << callbackData->pMessage << std::endl;
    return VK_FALSE;
}

void VulkanContext::setDebugName(VkObjectType objectType, uint64_t handle, const char* name) const noexcept
{
    if (!validationEnabled_ || device_ == VK_NULL_HANDLE || handle == 0 || name == nullptr)
    {
        return;
    }

    const auto function = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetDeviceProcAddr(device_, "vkSetDebugUtilsObjectNameEXT"));

    if (function == nullptr)
    {
        return;
    }

    VkDebugUtilsObjectNameInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = objectType;
    info.objectHandle = handle;
    info.pObjectName = name;

    const VkResult result = function(device_, &info);

    if (result != VK_SUCCESS)
    {
        std::fprintf(stderr, "setDebugName failed: %d\n", static_cast<int>(result));
    }
}
