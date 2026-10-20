#include "vulkan/VulkanContext.hpp"
#include <GLFW/glfw3.h>

#include "core/AppConfig.hpp"
#include "vulkan/DebugUtils.hpp"
#include "vulkan/VulkanCheck.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>

namespace
{
    // 把编码后的版本号转换成人能读懂的文字，并安全查询 Loader
    std::string apiVersionText(std::uint32_t version)
    {
        return std::to_string(VK_API_VERSION_MAJOR(version)) + "." +
               std::to_string(VK_API_VERSION_MINOR(version)) + "." +
               std::to_string(VK_API_VERSION_PATCH(version));
    }

    // 只保留 Vulkan 版本号的主版本和次版本，把 variant 和 patch 清零
    // VK_MAKE_API_VERSION() 把四个字段重新打包成一个 uint32_t 版本号
    std::uint32_t coreApiVersion(std::uint32_t version)
    {
        return VK_MAKE_API_VERSION(
            0,
            VK_API_VERSION_MAJOR(version),
            VK_API_VERSION_MINOR(version),
            0);
    }

    //
    std::uint32_t selectInstanceApiVersion(std::uint32_t preferred, std::uint32_t minimum, std::uint32_t loader)
    {
        if (VK_API_VERSION_VARIANT(preferred) != 0 ||
            VK_API_VERSION_VARIANT(minimum) != 0 ||
            VK_API_VERSION_VARIANT(loader) != 0)
        {
            throw std::invalid_argument("Only standard Vulkan variant 0 is supported");
        }

        preferred = coreApiVersion(preferred);
        minimum = coreApiVersion(minimum);
        loader = coreApiVersion(loader);

        if (minimum < VK_API_VERSION_1_0 ||
            minimum > preferred ||
            preferred > VK_API_VERSION_1_3)
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

    const char* featureSupportText(bool queried, bool supported)
    {
        if (!queried)
        {
            return "not queried";
        }
        return supported ? "yes" : "no";
    }

    template<typename Function> Function loadDeviceFunction(VkDevice device, const char* name)
    {
        const auto function = reinterpret_cast<Function>(vkGetDeviceProcAddr(device, name));

        if (function == nullptr)
        {
            throw std::runtime_error(std::string("Required Vulkan device function unavailable ") + name);
        }

        return function;
    }
}

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
    capabilities_ = {};

    try{
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

    deviceFunctions_ = {};

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

const QueueFamilyIndices &VulkanContext::queueFamilies() const noexcept
{
    return queueFamilies_;
}

const VulkanCapabilities &VulkanContext::capabilities() const noexcept
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

GpuBuffer VulkanContext::createBuffer(const BufferDesc &desc) const
{
    if (allocator_ == nullptr)
    {
        throw std::logic_error("cannot create buffer before allocator initialization");
    }

    if (desc.size == 0)
    {
        throw std::invalid_argument("buffer size must be greater than zero");
    }

    if (desc.usage == 0)
    {
        throw std::invalid_argument("buffer usage must not be empty");
    }

    VkBuffer rawBuffer = VK_NULL_HANDLE;
    VmaAllocation rawAllocation = nullptr;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.size;
    bufferInfo.usage = desc.usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    if (desc.requiredMemoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }
    else
    {
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    }

    VK_CHECK(vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &rawBuffer, &rawAllocation, nullptr));

    setDebugName(
        VK_OBJECT_TYPE_BUFFER,
        reinterpret_cast<uint64_t>(rawBuffer),
        desc.debugName);

    return GpuBuffer(
        allocator_,
        rawBuffer,
        rawAllocation,
        desc.size,
        desc.usage,
        desc.requiredMemoryProperties
    );
}

GpuImage VulkanContext::createImage(const ImageDesc &desc) const
{
    if (allocator_ == nullptr)
    {
        throw std::logic_error("cannot create image before allocator initialization");
    }

    if (desc.extent.width == 0 ||
        desc.extent.height == 0 ||
        desc.extent.depth == 0)
    {
        throw std::invalid_argument("image extent components must be greater than zero");
    }

    if (desc.mipLevels == 0)
    {
        throw std::invalid_argument("image mip level count must be greater than zero");
    }

    if (desc.arrayLayers == 0)
    {
        throw std::invalid_argument("image array layer count must greater than zero");
    }

    if (desc.format == VK_FORMAT_UNDEFINED)
    {
        throw std::invalid_argument("image format must be defined");
    }

    if (desc.usage == 0)
    {
        throw std::invalid_argument("image usage must not be empty");
    }

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(desc.extent.width);
    imageInfo.extent.height = static_cast<uint32_t>(desc.extent.height);
    imageInfo.extent.depth = desc.extent.depth;
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.format = desc.format;
    imageInfo.tiling = desc.tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = desc.usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = desc.samples;
    imageInfo.flags = desc.flags;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (desc.requiredMemoryProperties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    {
        allocInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
    if (desc.requiredMemoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    }

    VK_CHECK(vmaCreateImage(allocator_, &imageInfo, &allocInfo, &image, &allocation, nullptr));

    setDebugName(
        VK_OBJECT_TYPE_IMAGE,
        reinterpret_cast<uint64_t>(image),
        desc.debugName);

    return GpuImage(
        allocator_,
        device_,
        image,
        allocation,
        desc.extent,
        desc.format,
        desc.usage,
        desc.mipLevels,
        desc.arrayLayers,
        desc.samples
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

GpuSampler VulkanContext::createSampler(const SamplerDesc &desc) const
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = desc.magFilter;
    samplerInfo.minFilter = desc.minFilter;
    samplerInfo.addressModeU = desc.addressModeU;
    samplerInfo.addressModeV = desc.addressModeV;
    samplerInfo.addressModeW = desc.addressModeW;
    samplerInfo.anisotropyEnable = desc.anisotropyEnable;
    samplerInfo.maxAnisotropy = desc.maxAnisotropy;
    samplerInfo.borderColor = desc.borderColor;
    samplerInfo.unnormalizedCoordinates = desc.unnormalizedCoordinates;
    samplerInfo.compareEnable = desc.compareEnable;
    samplerInfo.compareOp = desc.compareOp;
    samplerInfo.mipmapMode = desc.mipmapMode;
    samplerInfo.mipLodBias = desc.mipLodBias;
    samplerInfo.minLod = desc.minLod;
    samplerInfo.maxLod = desc.maxLod;

    VkSampler sampler = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(device_, &samplerInfo, nullptr, &sampler));

    setDebugName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(sampler), desc.debugName);

    return GpuSampler(device_, sampler);
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
    appInfo.apiVersion = selectInstanceApiVersion(
        config.apiVersion,
        config.minimumApiVersion,
        capabilities_.loaderVersion
    );

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

    // 记录真正用于创建 Instance 的请求值
    capabilities_.instanceVersion = appInfo.apiVersion;
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

void VulkanContext::pickPhysicalDevice(const VulkanContextConfig &config)
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
        // VkPhysicalDeviceProperties properties{};
        // vkGetPhysicalDeviceProperties(candidate, &properties);

        const auto info = inspectDevice(candidate, config);

        std::cout
            << "GPU candidate: " << info.properties.deviceName
            << " | device API: "
            << apiVersionText(info.properties.apiVersion)
            << '\n';

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
        capabilities_.deviceApiVersion = std::min(
            coreApiVersion(capabilities_.instanceVersion),
            coreApiVersion(info.properties.apiVersion)
        );
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
    // VkPhysicalDeviceFeatures features{};
    // features.samplerAnisotropy = VK_TRUE;

    // VkDeviceCreateInfo createInfo{};
    // createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    // createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    // createInfo.pQueueCreateInfos = queueCreateInfos.data();
    // createInfo.pEnabledFeatures = &features;
    // createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    // createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    // createInfo.enabledLayerCount = 0;

    // VK_CHECK(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_));

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


// 实现 vulkan 1.3 核心逻辑
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

void VulkanContext::createAllocator()
{
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.instance = instance_;
    allocatorInfo.physicalDevice = physicalDevice_;
    allocatorInfo.device = device_;
    allocatorInfo.vulkanApiVersion = capabilities_.deviceApiVersion;

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

// 按顺序遍历不断更新符合条件（支持 graphics 和 present）的队列族，一旦找到了就立刻退出
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

// 集中检查候选设备，并建立 Features2 查询路径
VulkanContext::DeviceCandidateInfo VulkanContext::inspectDevice(VkPhysicalDevice candidate, const VulkanContextConfig &config) const
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

    const auto candidateApiVersion = std::min(
        coreApiVersion(capabilities_.instanceVersion),
        coreApiVersion(info.properties.apiVersion)
    );
    if (candidateApiVersion >= VK_API_VERSION_1_1)
    {
        const auto getFeature2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceFeatures2")
        );
        if (getFeature2 == nullptr)
        {
            throw std::runtime_error("Core Features2 entry point is unavailable");
        }

        // VkPhysicalDeviceFeatures2 supported{};
        // supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        // getFeature2(candidate, &supported);

        // info.features = supported.features;
        // info.queriedWithFeatures2 = true;

        VkPhysicalDeviceVulkan13Features supported13{};
        supported13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

        VkPhysicalDeviceFeatures2 supported{};
        supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

        const bool queryVulkan13 = candidateApiVersion >= VK_API_VERSION_1_3;
        if (queryVulkan13)
        {
            supported.pNext = & supported13;
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

// 检查具体缺少哪个扩展
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
    for (const auto &extentsion : available)
    {
        missing.erase(extentsion.extensionName);
    }

    return std::vector<std::string>(missing.begin(), missing.end());
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

void VulkanContext::queryLoaderVersion()
{
    // 老 loader 可能没有版本查询入口，此时按 1.0 处理
    capabilities_.loaderVersion = VK_API_VERSION_1_0;

    // 获取名为 vkEnumerateInstanceVersion 的函数地址
    const auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (enumerateVersion != nullptr)
    {
        VK_CHECK(enumerateVersion(&capabilities_.loaderVersion));
    }
}

void VulkanContext::logCapabilities() const
{
    const auto& info = capabilities();

    std::cout
        << "Vulkan capabilities\n"
        << "  Header: "
        << apiVersionText(info.headerVersion) << '\n'
        << "  Loader supported: "
        << apiVersionText(info.loaderVersion) << '\n'
        << "  Instance requested: "
        << apiVersionText(info.instanceVersion) << '\n'
        << "  Selected GPU: "
        << info.deviceName << '\n'
        << "  Device supported: "
        << apiVersionText(info.physicalDeviceVersion) << '\n'
        << "  Device core ceiling: "
        << apiVersionText(info.deviceApiVersion) << '\n'
        << "  Feature query: "
        << (info.features2QueryUsed
                ? "core Features2"
                : "legacy Features")
        << '\n'
        << "  Anisotropy supported: "
        << (info.samplerAnisotropySupported ? "yes" : "no")
        << '\n'
        << "  Anisotropy enabled: "
        << (info.samplerAnisotropyEnabled ? "yes" : "no")
        << '\n'
        << "  Vulkan 1.3 features queried: "
        << (info.vulkan13FeaturesQueried ? "yes" : "no")
        << '\n'

        << "  Synchronization2 supported: "
        << featureSupportText(
               info.vulkan13FeaturesQueried,
               info.synchronization2Supported)
        << '\n'

        << "  Synchronization2 enabled: "
        << (info.synchronization2Enabled ? "yes" : "no")
        << '\n'

        << "  Dynamic rendering supported: "
        << featureSupportText(
               info.vulkan13FeaturesQueried,
               info.dynamicRenderingSupported)
        << '\n'

        << "  Dynamic rendering enabled: "
        << (info.dynamicRenderingEnabled ? "yes" : "no")
        << '\n';

    std::cout
        << "  Synchronization2 functions loaded: "
        << (deviceFunctions_.synchronization2Loaded() ? "yes" : "no")
        << '\n'
        << "  Dynamic rendering functions loaded: "
        << (deviceFunctions_.dynamicRenderingLoaded() ? "yes" : "no")
        << '\n';
}
