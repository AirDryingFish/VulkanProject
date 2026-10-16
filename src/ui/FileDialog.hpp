#pragma once

#include <optional>
#include <string>

namespace FileDialog
{
// Call on the UI/main thread after GLFW initialization.
// Returns a UTF-8 path, nullopt on cancellation, or throws on failure.
std::optional<std::string> openGltf();
}
