#pragma once

#include "VulkanHeaders.hpp"

#include <stdexcept>
#include <string>

inline const char* vkResultToString(
    VkResult result)
{
    switch (result)
    {
    case VK_SUCCESS:
        return "VK_SUCCESS";

    case VK_NOT_READY:
        return "VK_NOT_READY";

    case VK_TIMEOUT:
        return "VK_TIMEOUT";

    case VK_EVENT_SET:
        return "VK_EVENT_SET";

    case VK_EVENT_RESET:
        return "VK_EVENT_RESET";

    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";

    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";

    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";

    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";

    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";

    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";

    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";

    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";

    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";

    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";

    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";

    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";

    case VK_ERROR_FRAGMENTED_POOL:
        return "VK_ERROR_FRAGMENTED_POOL";

    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";

    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";

    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";

    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";

    default:
        return "UNKNOWN_VK_RESULT";
    }
}

inline void checkVkResult(VkResult result, const char* expression, const char* file, int line)
{
    if (result == VK_SUCCESS)
    {
        return;
    }

    throw std::runtime_error(
        std::string("Vulkan call failed: ") +
        expression +
        " returned " +
        vkResultToString(result) +
        " (" +
        std::to_string(
            static_cast<int>(result)) +
        ") at " +
        file +
        ":" +
        std::to_string(line));
}

#define VK_CHECK(expression)                     \
    checkVkResult(                               \
        (expression),                            \
        #expression,                             \
        __FILE__,                                \
        __LINE__)

#define VK_CHECK_RESULT(result, operation)        \
    checkVkResult(                               \
        (result),                                \
        (operation),                             \
        __FILE__,                                \
        __LINE__)
