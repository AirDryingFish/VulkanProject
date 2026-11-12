#pragma once

#include "vulkan/VulkanHeaders.hpp"

#include <cstdint>
#include <string>

// Instance 版本协商与 Device 能力查询共享的版本表示；不负责设备选择。
namespace vulkan::detail
{
inline std::string apiVersionText(std::uint32_t version)
{
    return std::to_string(VK_API_VERSION_MAJOR(version)) + "." + std::to_string(VK_API_VERSION_MINOR(version)) + "." +
           std::to_string(VK_API_VERSION_PATCH(version));
}

inline std::uint32_t coreApiVersion(std::uint32_t version)
{
    return VK_MAKE_API_VERSION(0, VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version), 0);
}
} // namespace vulkan::detail
