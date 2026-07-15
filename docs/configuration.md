# Build configuration and macro policy

Project-wide configuration belongs to CMake. Source files consume the generated
`BuildConfig.hpp`; they must not independently infer a platform or build mode.

## Generated configuration

`cmake/BuildConfig.hpp.in` is the single template for these public build values:

| Name | Purpose | Source of truth |
| --- | --- | --- |
| `VULKAN_PROJECT_PLATFORM_WINDOWS` | Windows-specific Vulkan behavior | CMake `WIN32` |
| `VULKAN_PROJECT_PLATFORM_MACOS` | MoltenVK portability behavior | CMake `APPLE` |
| `VULKAN_PROJECT_ENABLE_VALIDATION` | Vulkan validation layer switch | CMake option |
| `VULKAN_PROJECT_ASSET_DIR` | Copied runtime asset root | CMake binary directory |
| `VULKAN_PROJECT_VERSION` | Project version string | `project(... VERSION ...)` |

Set the validation option during configure when an explicit override is needed:

```sh
cmake --preset macos-debug -DVULKAN_PROJECT_ENABLE_VALIDATION=OFF
```

Debug builds enable validation by default; other build types disable it.

## Dependency integration macros

GLM integration macros are target-wide compile definitions in `CMakeLists.txt`.
This guarantees every translation unit sees the same angle, depth-range, and
experimental-feature settings.

Header-only libraries that require one implementation translation unit are
centralized in `src/ThirdPartyImplementations.cpp`:

- `VMA_IMPLEMENTATION`
- `STB_IMAGE_IMPLEMENTATION`

TinyObjLoader is linked through its vcpkg CMake target, so the project must not
define `TINYOBJLOADER_IMPLEMENTATION` itself.

## Rule for new macros

1. Prefer a typed `constexpr` value for application behavior.
2. Use a CMake option plus generated config value for build-time features.
3. Use target compile definitions for dependency integration requirements.
4. Keep third-party implementation macros in `ThirdPartyImplementations.cpp`.
5. Do not add direct `_WIN32`, `__APPLE__`, or `NDEBUG` checks to feature code.
