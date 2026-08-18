#include "VulkanContext.hpp"

#include "AppConfig.hpp"
#include "DebugUtils.hpp"
#include "VulkanCheck.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>

VulkanContext::~VulkanContext() noexcept
{
    shutdown();
}

void VulkanContext::initialize(GLFWwindow *window, const VulkanContextConfig &config)
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
    apiVersion_ = config.apiVersion;

    try{
        createInstance(config);
        setupDebugMessenger();
        createSurface(window);
        pickPhysicalDevice();
        createLogicalDevice();
        createAllocator();
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
        const VkResult result =
            vkDeviceWaitIdle(device_);

        if (result != VK_SUCCESS &&
            result != VK_ERROR_DEVICE_LOST)
        {
            std::fprintf(
                stderr,
                "VulkanContext waitIdle failed: %d\n",
                static_cast<int>(result));
        }
    }

    if (allocator_ != nullptr)
    {
        vmaDestroyAllocator(allocator_);
        allocator_ = nullptr;
    }

    if (device_ != VK_NULL_HANDLE)
    {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    graphicsQueue_ = VK_NULL_HANDLE;
    presentQueue_ = VK_NULL_HANDLE;
    queueFamilies_ = {};

    if (instance_ != VK_NULL_HANDLE &&
        debugMessenger_ != VK_NULL_HANDLE)
    {
        DestroyDebugUtilsMessengerEXT(
            instance_,
            debugMessenger_,
            nullptr);

        debugMessenger_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE &&
        surface_ != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(
            instance_,
            surface_,
            nullptr);

        surface_ = VK_NULL_HANDLE;
    }

    physicalDevice_ = VK_NULL_HANDLE;
    msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;

    if (instance_ != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }

    validationEnabled_ = false;
    apiVersion_ = VK_API_VERSION_1_0;
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

VkInstance VulkanContext::instance() const noexcept
{
    return instance_;
}

VkDebugUtilsMessengerEXT VulkanContext::debugMessenger() const noexcept
{
    return debugMessenger_;
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

const QueueFamilyIndices &VulkanContext::queueFamilies() const noexcept
{
    return queueFamilies_;
}

VkSampleCountFlagBits VulkanContext::msaaSamples() const noexcept
{
    return msaaSamples_;
}

SwapChainSupportDetails VulkanContext::querySwapchainSupport() const
{
    if (physicalDevice_ == VK_NULL_HANDLE)
    {
        throw std::logic_error("physical device is not initialized");
    }
    return querySwapchainSupport(physicalDevice_);
}

VkFormat VulkanContext::findSupportedFormat(const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const
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
    return findSupportedFormat(
        {
            VK_FORMAT_D32_SFLOAT, 
            VK_FORMAT_D32_SFLOAT_S8_UINT, 
            VK_FORMAT_D24_UNORM_S8_UINT
        },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void VulkanContext::setDebugName(VkObjectType objectType, uint64_t handle, const char *name) const noexcept
{
    if (!validationEnabled_ ||
        device_ == VK_NULL_HANDLE ||
        handle == 0 ||
        name == nullptr)
    {
        return;
    }

    const auto function =
        reinterpret_cast<
            PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetDeviceProcAddr(
                device_,
                "vkSetDebugUtilsObjectNameEXT"));

    if (function == nullptr)
    {
        return;
    }

    VkDebugUtilsObjectNameInfoEXT info{};
    info.sType =
        VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = objectType;
    info.objectHandle = handle;
    info.pObjectName = name;

    const VkResult result =
        function(device_, &info);

    if (result != VK_SUCCESS)
    {
        std::fprintf(
            stderr,
            "setDebugName failed: %d\n",
            static_cast<int>(result));
    }
}

AllocatedBuffer VulkanContext::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) const
{
    VkBuffer rawBuffer = VK_NULL_HANDLE;
    VmaAllocation rawAllocation = nullptr;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }
    else
    {
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    }

    VK_CHECK(vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &rawBuffer, &rawAllocation, nullptr));

    return AllocatedBuffer(
        allocator_,
        rawBuffer,
        rawAllocation,
        size,
        usage,
        properties
    );
}

AllocatedImage VulkanContext::createImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, uint32_t arrayLayers, VkImageCreateFlags flags) const
{
    // AllocatedImage allocatedImage{};
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(width);
    imageInfo.extent.height = static_cast<uint32_t>(height);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = arrayLayers;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = numSamples;
    imageInfo.flags = flags;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    {
        allocInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    }

    VK_CHECK(vmaCreateImage(allocator_, &imageInfo, &allocInfo, &image, &allocation, nullptr));
    // allocatedImage.mipLevels = mipLevels;

    return AllocatedImage(
        allocator_,
        device_, 
        image, 
        allocation,
        VkExtent3D{width, height, 1},
        format,
        usage,
        mipLevels,
        arrayLayers,
        numSamples
    );
}

VkImageView VulkanContext::createImageView(VkImage image, VkFormat format, uint32_t mipLevels, VkImageAspectFlags aspectFlags, VkImageViewType viewType, uint32_t layerCount) const
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = viewType;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = layerCount;

    VkImageView imageView;
    VK_CHECK(vkCreateImageView(device_, &viewInfo, nullptr, &imageView));

    return imageView;
}

UniqueShaderModule VulkanContext::createShaderModule(const std::vector<char> &code) const
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    // SPIR-V的字节码大小(字节数)
    createInfo.codeSize = code.size();
    // 指向SPIR-V字节码的指针。SPIR-V字节码是一个uint32_t数组，所以需要把char*转换成uint32_t*。
    // 因为SPIR-V字节码的大小必须是4的倍数，所以code.size()一定是4的倍数，reinterpret_cast是安全的。
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule));
    return UniqueShaderModule(device_, shaderModule);
}

void VulkanContext::createInstance(const VulkanContextConfig &config) 
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
    appInfo.apiVersion = config.apiVersion;

    VkInstanceCreateInfo createInfo{};
    std::vector<const char *> extensions = getRequiredExtensions();
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
#if VULKAN_PROJECT_PLATFORM_MACOS
    // MoltenVK is a Vulkan portability implementation. The Vulkan loader hides
    // portability devices unless the application explicitly opts into them.
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions;
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
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
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
}

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

void VulkanContext::createSurface(GLFWwindow *window)
{
    VK_CHECK(glfwCreateWindowSurface(instance_, window, nullptr, &surface_));
}

void VulkanContext::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr));
    if (deviceCount == 0)
    {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    VK_CHECK(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()));

    for (const VkPhysicalDevice &candidate : devices)
    {
        if (isDeviceSuitable(candidate))
        {
            physicalDevice_ = candidate;
            queueFamilies_ = findQueueFamilies(candidate);
            msaaSamples_ = getMaxUsableSampleCount();
            std::cout << "MSAA samples: " << msaaSamples_ << std::endl;
            break;
        }
    }
    if (physicalDevice_ == VK_NULL_HANDLE)
    {
        throw std::runtime_error("failed to find a suitable GPU!");
    }

    // // multimap allows key to be repeated, so we can store multiple candidates and sort them based on score, then pick the best one.
    // std::multimap<int, VkPhysicalDevice> candidates;
}

void VulkanContext::createLogicalDevice()
{
    // // Step 1: 查询队列族索引： 找到物理设备上支持图形操作的队列族索引
    // QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);

    // // Step 2: 填写队列配置： 声明要几个队列、用哪几个队列族、优先级多少
    // VkDeviceQueueCreateInfo queueCreateInfo{};
    // queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    // queueCreateInfo.queueFamilyIndex = indices.graphicsFamily.value();
    // queueCreateInfo.queueCount = 1;
    // float queuePriority = 1.0f;
    // queueCreateInfo.pQueuePriorities = &queuePriority;

    // // Step 3: 声明设备特性：声明要弃用哪些GPU特性，比如这里我们不需要任何特殊的GPU特性，所以就创建一个默认的结构体。
    // VkPhysicalDeviceFeatures deviceFeatures{};

    // deviceFeatures.samplerAnisotropy = VK_TRUE;

    // // Step 4: 填写设备配置: 把队列配置、设备特性、需要的拓展等信息填入 VkDeviceCreateInfo 结构体，传给 vkCreateDevice 创建逻辑设备。
    // VkDeviceCreateInfo createInfo{};
    // createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    // createInfo.pQueueCreateInfos = &queueCreateInfo;
    // createInfo.queueCreateInfoCount = 1;
    // createInfo.pEnabledFeatures = &deviceFeatures;
    // // createInfo.enabledExtensionCount = 0;
    // // swap chain 是vulkan的一个拓展功能，所以需要在创建logical device的时候告诉vulkan我们需要这个拓展。
    // createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    // createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    // // Validation layers are instance-scoped. Device layers are legacy and
    // // enabledLayerCount must remain zero.
    // createInfo.enabledLayerCount = 0;

    // // Step 5: 创建逻辑设备：调用 vkCreateDevice 创建逻辑设备，并把得到的设备句柄存储在 device 变量中。
    // VK_CHECK(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_));

    // // Step 6: 获取队列句柄：调用 vkGetDeviceQueue 获取图形队列的句柄，并存储在 graphicsQueue 变量中，以便后续使用。
    // vkGetDeviceQueue(device_, indices.graphicsFamily.value(), 0, &graphicsQueue_);
    // vkGetDeviceQueue(device_, indices.presentFamily.value(), 0, &presentQueue_);

    assert(queueFamilies_.isComplete());

    const std::set<uint32_t> uniqueFamilies = {
        queueFamilies_.graphicsFamily.value(),
        queueFamilies_.presentFamily.value()
    };

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
    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &features;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    createInfo.enabledLayerCount = 0;

    VK_CHECK(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_));
    vkGetDeviceQueue(device_, queueFamilies_.graphicsFamily.value(), 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, queueFamilies_.presentFamily.value(), 0, &presentQueue_);
}

void VulkanContext::createAllocator()
{
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.instance = instance_;
    allocatorInfo.physicalDevice = physicalDevice_;
    allocatorInfo.device = device_;
    allocatorInfo.vulkanApiVersion = apiVersion_;
    
    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &allocator_));
}

bool VulkanContext::checkValidationLayerSupport() const
{
    uint32_t layerCount;
    VK_CHECK(vkEnumerateInstanceLayerProperties(&layerCount, nullptr));
    std::vector<VkLayerProperties> availableLayers(layerCount);
    VK_CHECK(vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data()));

    for (const char *layerName : validationLayers)
    {
        bool layerFound = false;
        for (const auto &layerProperties : availableLayers)
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

std::vector<const char *> VulkanContext::getRequiredExtensions() const
{
    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    // To understand extensions: e.g. on Windows, the following extensions are required:
    // glfwExtensions[0] -> "VK_KHR_surface"
    // glfwExtensions[1] -> "VK_KHR_win32_surface"
    std::vector<const char *> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

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

bool VulkanContext::isDeviceSuitable(VkPhysicalDevice candidate) const
{
    QueueFamilyIndices indices = findQueueFamilies(candidate);
    bool extensionsSupported = checkDeviceExtensionSupport(candidate);

    bool swapChainAdequate = false;
    if (extensionsSupported)
    {
        SwapChainSupportDetails swapChainSupport = querySwapchainSupport(candidate);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(candidate, &supportedFeatures);

    return indices.isComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
}

bool VulkanContext::checkDeviceExtensionSupport(VkPhysicalDevice candidate) const
{
    // step 1: 查询显卡支持哪些拓展，存到 availableExtensions 中。
    uint32_t extensionCount;
    VK_CHECK(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr));
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    VK_CHECK(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, availableExtensions.data()));

    // step 2: 把需要的拓展，比如这里的Swap Chain的拓展 "VK_KHR_swapchain"，放入一个set中
    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    // step 3: 遍历显卡支持的拓展，每找到一个就从set中删除一个，最后如果set是空的，说明显卡支持我们需要的所有拓展。
    for (const auto &extension : availableExtensions)
    {
        requiredExtensions.erase(extension.extensionName);
    }
    return requiredExtensions.empty();
}

QueueFamilyIndices VulkanContext::findQueueFamilies(VkPhysicalDevice candidate) const
{
    QueueFamilyIndices indices;
    // 取出queue family的数量，存到queueFamilyCount中
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, queueFamilies.data());

    // 遍历每个queue family，找到一个支持图形操作的队列族就把它的索引存到indices.graphicsFamily中。因为我们现在还没有创建逻辑设备，所以无法查询哪个队列族支持presentation，所以先不管presentation，后面创建完逻辑设备了再来查询presentation支持哪个队列族。
    int i = 0;
    for (const auto &queueFamily : queueFamilies)
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
        VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface_, &presentModeCount, details.presentModes.data()));
    }

    return details;
}

VkSampleCountFlagBits VulkanContext::getMaxUsableSampleCount() const
{
    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice_, &physicalDeviceProperties);

    VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
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

void VulkanContext::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo) const noexcept
{
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT *callbackData, void *userData)
{
    std::cerr << "---------------------\nvalidation layer: " << callbackData->pMessage << std::endl;
    return VK_FALSE;
}
