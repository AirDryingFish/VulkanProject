#pragma once

#include "GltfImportTypes.hpp"
#include <cstddef>
#include <string>

[[nodiscard]]
DecodedImageData decodeImage(const std::byte* bytes, std::size_t byteCount, const std::string& debugName);