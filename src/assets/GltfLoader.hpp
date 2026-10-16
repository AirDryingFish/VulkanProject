#pragma once

#include "assets/GltfImportTypes.hpp"
#include <filesystem>

[[nodiscard]]
GltfImportData loadGltfCpuData(const std::filesystem::path& path);