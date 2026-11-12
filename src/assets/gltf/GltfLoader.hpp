#pragma once

#include "assets/gltf/GltfImportTypes.hpp"
#include <filesystem>

// 同步解析静态 glTF/GLB，返回完全拥有的 CPU 数据；不支持的特征或非法引用抛出异常。
[[nodiscard]]
GltfImportData loadGltfCpuData(const std::filesystem::path& path);
