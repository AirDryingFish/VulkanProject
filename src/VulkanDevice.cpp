#include "TriangleApplication.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_map>
void TriangleApplication::createLogicalDevice()
{

    // Step 1: 查询队列族索引： 找到物理设备上支持图形操作的队列族索引
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

    // Step 2: 填写队列配置： 声明要几个队列、用哪几个队列族、优先级多少
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = indices.graphicsFamily.value();
    queueCreateInfo.queueCount = 1;
    float queuePriority = 1.0f;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    // Step 3: 声明设备特性：声明要弃用哪些GPU特性，比如这里我们不需要任何特殊的GPU特性，所以就创建一个默认的结构体。
    VkPhysicalDeviceFeatures deviceFeatures{};

    deviceFeatures.samplerAnisotropy = VK_TRUE;

    // Step 4: 填写设备配置: 把队列配置、设备特性、需要的拓展等信息填入 VkDeviceCreateInfo 结构体，传给 vkCreateDevice 创建逻辑设备。
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;
    // createInfo.enabledExtensionCount = 0;
    // swap chain 是vulkan的一个拓展功能，所以需要在创建logical device的时候告诉vulkan我们需要这个拓展。
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    // Validation layers are instance-scoped. Device layers are legacy and
    // enabledLayerCount must remain zero.
    createInfo.enabledLayerCount = 0;

    // Step 5: 创建逻辑设备：调用 vkCreateDevice 创建逻辑设备，并把得到的设备句柄存储在 device 变量中。
    VK_CHECK(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device));

    // Step 6: 获取队列句柄：调用 vkGetDeviceQueue 获取图形队列的句柄，并存储在 graphicsQueue 变量中，以便后续使用。
    vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
}

void TriangleApplication::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));
    if (deviceCount == 0)
    {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()));

    for (const VkPhysicalDevice &device : devices)
    {
        if (isDeviceSuitable(device))
        {
            physicalDevice = device;
            msaaSamples = getMaxUsableSampleCount();
            std::cout << "MSAA samples: " << msaaSamples << std::endl;
            break;
        }
    }
    if (physicalDevice == VK_NULL_HANDLE)
    {
        throw std::runtime_error("failed to find a suitable GPU!");
    }

    // // multimap allows key to be repeated, so we can store multiple candidates and sort them based on score, then pick the best one.
    // std::multimap<int, VkPhysicalDevice> candidates;
}

bool TriangleApplication::isDeviceSuitable(VkPhysicalDevice device)
{
    QueueFamilyIndices indices = findQueueFamilies(device);
    bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported)
    {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

    return indices.isComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
}

bool TriangleApplication::checkDeviceExtensionSupport(VkPhysicalDevice device)
{
    // return true;
    // step 1: 查询显卡支持哪些拓展，存到 availableExtensions 中。
    uint32_t extensionCount;
    VK_CHECK(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr));
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    VK_CHECK(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data()));

    // step 2: 把需要的拓展，比如这里的Swap Chain的拓展 "VK_KHR_swapchain"，放入一个set中
    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    // step 3: 遍历显卡支持的拓展，每找到一个就从set中删除一个，最后如果set是空的，说明显卡支持我们需要的所有拓展。
    for (const auto &extension : availableExtensions)
    {
        requiredExtensions.erase(extension.extensionName);
    }
    return requiredExtensions.empty();
}

QueueFamilyIndices TriangleApplication::findQueueFamilies(VkPhysicalDevice device)
{
    QueueFamilyIndices indices;
    // 取出queue family的数量，存到queueFamilyCount中
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    // 遍历每个queue family，找到一个支持图形操作的队列族就把它的索引存到indices.graphicsFamily中。因为我们现在还没有创建逻辑设备，所以无法查询哪个队列族支持presentation，所以先不管presentation，后面创建完逻辑设备了再来查询presentation支持哪个队列族。
    int i = 0;
    for (const auto &queueFamily : queueFamilies)
    {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport));

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
