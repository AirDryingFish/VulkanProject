#pragma once

#include "vulkan/VulkanHeaders.hpp"

#include <array>

// 描述 Vertex 对应的 GPU 顶点输入；绑定、location 与 shader 的声明保持一致。
VkVertexInputBindingDescription vertexBindingDescription();
std::array<VkVertexInputAttributeDescription, 6> vertexAttributeDescriptions();
