#include "vulkan/context/VulkanContext.hpp"
#include "core/AppConfig.hpp"
#include "vulkan/VulkanCheck.hpp"
#include "vulkan/context/detail/ApiVersion.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <set>
#include <stdexcept>

namespace
{
using vulkan::detail::apiVersionText;
using vulkan::detail::coreApiVersion;

const char* featureSupportText(bool queried, bool supported)
{
    if (!queried)
    {
        return "not queried";
    }
    return supported ? "yes" : "no";
}

template <typename Function> Function loadDeviceFunction(VkDevice device, const char* name)
{
    const auto function = reinterpret_cast<Function>(vkGetDeviceProcAddr(device, name));

    if (function == nullptr)
    {
        throw std::runtime_error(std::string("Required Vulkan device function unavailable ") + name);
    }

    return function;
}
} // namespace

// 候选设备检查与选择；保留设备枚举顺序和现有拒绝条件。
void VulkanContext::pickPhysicalDevice(const VulkanContextConfig& config)
{
    uint32_t deviceCount = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr));
    if (deviceCount == 0)
    {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    VK_CHECK(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()));

    for (const VkPhysicalDevice& candidate : devices)
    {
        const auto info = inspectDevice(candidate, config);

        std::cout << "GPU candidate: " << info.properties.deviceName
                  << " | device API: " << apiVersionText(info.properties.apiVersion) << '\n';

        if (!info.suitable())
        {
            for (const auto& reason : info.rejectionReasons)
            {
                std::cout << "  rejected: " << reason << '\n';
            }

            continue;
        }
        physicalDevice_ = candidate;
        queueFamilies_ = info.queueFamilies;

        capabilities_.physicalDeviceVersion = info.properties.apiVersion;
        capabilities_.deviceName = info.properties.deviceName;
        capabilities_.deviceApiVersion =
            std::min(coreApiVersion(capabilities_.instanceVersion), coreApiVersion(info.properties.apiVersion));
        capabilities_.features2QueryUsed = info.queriedWithFeatures2;

        capabilities_.samplerAnisotropySupported = info.features.samplerAnisotropy == VK_TRUE;
        capabilities_.vulkan13FeaturesQueried = info.queredVulkan13Features;
        capabilities_.synchronization2Supported = info.synchronization2Supported;
        capabilities_.dynamicRenderingSupported = info.dynamicRenderingSupported;

        msaaSamples_ = getMaxUsableSampleCount();

        std::cout << "MSAA samples: " << msaaSamples_ << std::endl;
        break;
    }
    if (physicalDevice_ == VK_NULL_HANDLE)
    {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
}

VulkanContext::DeviceCandidateInfo VulkanContext::inspectDevice(VkPhysicalDevice candidate,
                                                                const VulkanContextConfig& config) const
{
    DeviceCandidateInfo info{};

    vkGetPhysicalDeviceProperties(candidate, &info.properties);
    if (coreApiVersion(info.properties.apiVersion) < coreApiVersion(config.minimumApiVersion))
    {
        info.rejectionReasons.push_back("device API is below the minimum");
    }

    info.queueFamilies = findQueueFamilies(candidate);
    if (!info.queueFamilies.graphicsFamily.has_value())
    {
        info.rejectionReasons.push_back("missing graphics queue");
    }
    if (!info.queueFamilies.presentFamily.has_value())
    {
        info.rejectionReasons.push_back("missing presentation queue");
    }

    const auto missing = findMissingDeviceExtensions(candidate);
    for (const auto& name : missing)
    {
        info.rejectionReasons.push_back("missing device extension: " + name);
    }
    if (missing.empty())
    {
        const auto support = querySwapchainSupport(candidate);

        if (support.formats.empty())
        {
            info.rejectionReasons.push_back("no surface formats");
        }

        if (support.presentModes.empty())
        {
            info.rejectionReasons.push_back("no presentation modes");
        }
    }

    const auto candidateApiVersion =
        std::min(coreApiVersion(capabilities_.instanceVersion), coreApiVersion(info.properties.apiVersion));
    if (candidateApiVersion >= VK_API_VERSION_1_1)
    {
        const auto getFeature2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceFeatures2"));
        if (getFeature2 == nullptr)
        {
            throw std::runtime_error("Core Features2 entry point is unavailable");
        }

        VkPhysicalDeviceVulkan13Features supported13{};
        supported13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

        VkPhysicalDeviceFeatures2 supported{};
        supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

        const bool queryVulkan13 = candidateApiVersion >= VK_API_VERSION_1_3;
        if (queryVulkan13)
        {
            supported.pNext = &supported13;
        }

        getFeature2(candidate, &supported);

        info.features = supported.features;
        info.queriedWithFeatures2 = true;

        if (queryVulkan13)
        {
            info.queredVulkan13Features = true;
            info.synchronization2Supported = supported13.synchronization2 == VK_TRUE;
            info.dynamicRenderingSupported = supported13.dynamicRendering == VK_TRUE;
        }
    }
    else
    {
        vkGetPhysicalDeviceFeatures(candidate, &info.features);
    }

    if (info.features.samplerAnisotropy != VK_TRUE)
    {
        info.rejectionReasons.push_back("samplerAnisotropy is unsupported");
    }

    return info;
}

QueueFamilyIndices VulkanContext::findQueueFamilies(VkPhysicalDevice candidate) const
{
    QueueFamilyIndices indices;
    // 取出queue family的数量，存到queueFamilyCount中
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, queueFamilies.data());

    // 表面已创建，可在逻辑设备创建前查询每个队列族的图形与呈现支持。
    int i = 0;
    for (const auto& queueFamily : queueFamilies)
    {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface_, &presentSupport));

        if (presentSupport)
        {
            indices.presentFamily = i;
        }

        if (indices.isComplete())
        {
            break;
        }

        i++;
    }

    return indices;
}

std::vector<std::string> VulkanContext::findMissingDeviceExtensions(VkPhysicalDevice candidate) const
{
    std::uint32_t count = 0;

    VK_CHECK(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, nullptr));

    std::vector<VkExtensionProperties> available(count);

    VK_CHECK(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, available.data()));

    available.resize(count);

    // 初始化，missing 为所有的所需的 extension
    std::set<std::string> missing(deviceExtensions.begin(), deviceExtensions.end());

    // 在查询到的 extension 找到支持的，就从 missing 中删除
    for (const auto& extentsion : available)
    {
        missing.erase(extentsion.extensionName);
    }

    return std::vector<std::string>(missing.begin(), missing.end());
}

// 逻辑设备与入口：只装载成功启用的核心功能。
void VulkanContext::createLogicalDevice()
{
    assert(queueFamilies_.isComplete());

    const std::set<uint32_t> uniqueFamilies = {queueFamilies_.graphicsFamily.value(),
                                               queueFamilies_.presentFamily.value()};

    float queuePriority = 1.0f;

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    queueCreateInfos.reserve(uniqueFamilies.size());

    for (uint32_t family : uniqueFamilies)
    {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueInfo);
    }
    VkPhysicalDeviceFeatures2 enabled{};
    enabled.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    enabled.features.samplerAnisotropy = VK_TRUE;

    VkPhysicalDeviceVulkan13Features enabled13{};
    enabled13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

    if (capabilities_.vulkan13FeaturesQueried)
    {
        enabled13.synchronization2 = capabilities_.synchronization2Supported ? VK_TRUE : VK_FALSE;
        enabled13.dynamicRendering = capabilities_.dynamicRenderingSupported ? VK_TRUE : VK_FALSE;
        enabled.pNext = &enabled13;
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    createInfo.enabledLayerCount = 0;

    if (capabilities_.deviceApiVersion >= VK_API_VERSION_1_1)
    {
        createInfo.pNext = &enabled;
        createInfo.pEnabledFeatures = nullptr;
    }
    else
    {
        createInfo.pNext = nullptr;
        createInfo.pEnabledFeatures = &enabled.features;
    }

    VK_CHECK(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_));

    capabilities_.samplerAnisotropyEnabled = true;
    capabilities_.synchronization2Enabled = enabled13.synchronization2 == VK_TRUE;
    capabilities_.dynamicRenderingEnabled = enabled13.dynamicRendering == VK_TRUE;

    vkGetDeviceQueue(device_, queueFamilies_.graphicsFamily.value(), 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, queueFamilies_.presentFamily.value(), 0, &presentQueue_);
}

void VulkanContext::loadDeviceFunctions()
{
    const bool needsCore13 = capabilities_.synchronization2Enabled || capabilities_.dynamicRenderingEnabled;
    if (needsCore13 && capabilities_.deviceApiVersion < VK_API_VERSION_1_3)
    {
        throw std::logic_error("Core Vulkan 1.3 functions require device API 1.3");
    }

    VulkanDeviceFunctions functions{};
    if (capabilities_.synchronization2Enabled)
    {
        functions.cmdPipelineBarrier2 = loadDeviceFunction<PFN_vkCmdPipelineBarrier2>(device_, "vkCmdPipelineBarrier2");
        functions.queueSubmit2 = loadDeviceFunction<PFN_vkQueueSubmit2>(device_, "vkQueueSubmit2");
        functions.cmdWriteTimestamp2 = loadDeviceFunction<PFN_vkCmdWriteTimestamp2>(device_, "vkCmdWriteTimestamp2");
    }
    if (capabilities_.dynamicRenderingEnabled)
    {
        functions.cmdBeginRendering = loadDeviceFunction<PFN_vkCmdBeginRendering>(device_, "vkCmdBeginRendering");
        functions.cmdEndRendering = loadDeviceFunction<PFN_vkCmdEndRendering>(device_, "vkCmdEndRendering");
    }

    deviceFunctions_ = functions;
}

// 设备与表面能力查询。
SwapChainSupportDetails VulkanContext::querySwapchainSupport() const
{
    if (physicalDevice_ == VK_NULL_HANDLE)
    {
        throw std::logic_error("physical device is not initialized");
    }
    return querySwapchainSupport(physicalDevice_);
}

SwapChainSupportDetails VulkanContext::querySwapchainSupport(VkPhysicalDevice candidate) const
{
    SwapChainSupportDetails details;

    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(candidate, surface_, &details.capabilities));

    uint32_t formatCount;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface_, &formatCount, nullptr));

    if (formatCount != 0)
    {
        details.formats.resize(formatCount);
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface_, &formatCount, details.formats.data()));
    }

    uint32_t presentModeCount;
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface_, &presentModeCount, nullptr));

    if (presentModeCount != 0)
    {
        details.presentModes.resize(presentModeCount);
        VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface_, &presentModeCount,
                                                           details.presentModes.data()));
    }

    return details;
}

VkSampleCountFlagBits VulkanContext::getMaxUsableSampleCount() const
{
    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice_, &physicalDeviceProperties);

    VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts &
                                physicalDeviceProperties.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_64_BIT)
    {
        return VK_SAMPLE_COUNT_64_BIT;
    }
    if (counts & VK_SAMPLE_COUNT_32_BIT)
    {
        return VK_SAMPLE_COUNT_32_BIT;
    }
    if (counts & VK_SAMPLE_COUNT_16_BIT)
    {
        return VK_SAMPLE_COUNT_16_BIT;
    }
    if (counts & VK_SAMPLE_COUNT_8_BIT)
    {
        return VK_SAMPLE_COUNT_8_BIT;
    }
    if (counts & VK_SAMPLE_COUNT_4_BIT)
    {
        return VK_SAMPLE_COUNT_4_BIT;
    }
    if (counts & VK_SAMPLE_COUNT_2_BIT)
    {
        return VK_SAMPLE_COUNT_2_BIT;
    }

    return VK_SAMPLE_COUNT_1_BIT;
}

VkFormat VulkanContext::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling,
                                            VkFormatFeatureFlags features) const
{
    for (VkFormat format : candidates)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
        {
            return format;
        }
        if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
        {
            return format;
        }
    }

    throw std::runtime_error("failed to find supported format!");
}

VkFormat VulkanContext::findDepthFormat() const
{
    return findSupportedFormat({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
                               VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void VulkanContext::logCapabilities() const
{
    const auto& info = capabilities();

    std::cout << "Vulkan capabilities\n"
              << "  Header: " << apiVersionText(info.headerVersion) << '\n'
              << "  Loader supported: " << apiVersionText(info.loaderVersion) << '\n'
              << "  Instance requested: " << apiVersionText(info.instanceVersion) << '\n'
              << "  Selected GPU: " << info.deviceName << '\n'
              << "  Device supported: " << apiVersionText(info.physicalDeviceVersion) << '\n'
              << "  Device core ceiling: " << apiVersionText(info.deviceApiVersion) << '\n'
              << "  Feature query: " << (info.features2QueryUsed ? "core Features2" : "legacy Features") << '\n'
              << "  Anisotropy supported: " << (info.samplerAnisotropySupported ? "yes" : "no") << '\n'
              << "  Anisotropy enabled: " << (info.samplerAnisotropyEnabled ? "yes" : "no") << '\n'
              << "  Vulkan 1.3 features queried: " << (info.vulkan13FeaturesQueried ? "yes" : "no") << '\n'

              << "  Synchronization2 supported: "
              << featureSupportText(info.vulkan13FeaturesQueried, info.synchronization2Supported) << '\n'

              << "  Synchronization2 enabled: " << (info.synchronization2Enabled ? "yes" : "no") << '\n'

              << "  Dynamic rendering supported: "
              << featureSupportText(info.vulkan13FeaturesQueried, info.dynamicRenderingSupported) << '\n'

              << "  Dynamic rendering enabled: " << (info.dynamicRenderingEnabled ? "yes" : "no") << '\n';

    std::cout << "  Synchronization2 functions loaded: " << (deviceFunctions_.synchronization2Loaded() ? "yes" : "no")
              << '\n'
              << "  Dynamic rendering functions loaded: " << (deviceFunctions_.dynamicRenderingLoaded() ? "yes" : "no")
              << '\n';
}
