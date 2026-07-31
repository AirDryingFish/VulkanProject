# Project standardization log

This document records each normalization change as a reviewable learning unit.
Each entry states the problem, the chosen convention, and how the result was
verified. New normalization work should add an entry here in the same change.

## STD-001 — Repository hygiene and generated-file boundaries

- **Problem:** CMake's `build/` tree, editor-specific settings, runtime ImGui
  state, and operating-system metadata could be committed as project source.
- **Change:** Replaced the minimal `.gitignore` with categorized rules for
  CMake, vcpkg, compiler outputs, runtime state, editors, operating systems,
  and local environment overrides. Removed already-tracked generated files
  from the Git index without deleting the local build tree.
- **Why:** A clean checkout should contain only reproducible source inputs;
  generated outputs vary by machine and compiler and create noisy reviews.
- **Verification:** `git ls-files build .vscode shaders/imgui.ini` returns no
  tracked generated paths, while the existing local build remains usable.

## STD-002 — Large rendering assets and line-ending policy

- **Problem:** Multi-megabyte textures and HDR images were stored as ordinary
  Git blobs, while cross-platform text files had no explicit line-ending rule.
- **Change:** Added `.gitattributes`; PNG, JPEG, HDR, DDS, and KTX assets now
  use Git LFS, source/configuration files use LF, and Windows batch files use
  CRLF. OBJ models remain ordinary text so their changes stay inspectable.
- **Why:** Git LFS keeps future repository growth manageable without hiding
  diffable model/source data. Explicit line endings prevent Windows/macOS churn.
- **Verification:** `git lfs ls-files` lists the binary assets and
  `git check-attr filter -- assets/textures/pbr/newport_loft.hdr` reports `lfs`.

## STD-003 — Unified asset layout and runtime path resolution

- **Problem:** Models, textures, and shaders lived in separate root folders and
  every source file assumed the process started from a specific build folder by
  using `../` paths.
- **Change:** Moved all runtime data under `assets/`, centralized named asset
  paths in `AppConfig.hpp`, generated the build asset root through CMake, and
  added a post-build copy step that places a complete asset tree beside the
  executable's build output.
- **Why:** Resource ownership is now obvious, and launching from an IDE, script,
  terminal, or a different working directory resolves the same files.
- **Verification:** Build output contains `assets/models`, `assets/textures`,
  and `assets/shaders`; a smoke test launched outside the build directory can
  load the model, textures, and SPIR-V files.

## STD-004 — Centralized platform, feature, and dependency macros

- **Problem:** Platform checks, validation behavior, GLFW/Vulkan inclusion,
  GLM configuration, and third-party implementation macros were spread across
  headers and unrelated source files.
- **Change:** Added a CMake-generated `BuildConfig.hpp`, exposed validation as a
  CMake option, moved GLM settings to target compile definitions, and created
  `ThirdPartyImplementations.cpp` as the sole VMA/stb implementation unit.
  TinyObjLoader now comes only from its linked vcpkg target.
- **Why:** Every translation unit now receives consistent configuration, and a
  new platform or build mode has one clearly documented integration surface.
- **Verification:** A macro scan finds only the generated template, the two
  intentional implementation macros, and normal include guards; Debug and
  Release configuration report the expected validation value.

## STD-005 — Portable presets and tool scripts

- **Problem:** The Windows preset embedded one user's absolute Ninja and MinGW
  paths, preset names did not expose build type, the macOS launcher assumed an
  old build directory, and shader compilation embedded a Vulkan SDK version.
- **Change:** Added PATH-based macOS and Windows MinGW Debug/Release presets,
  standardized build trees under `build/<preset>`, and moved runtime/shader
  helpers into `scripts/` with tool discovery and clear failure messages.
- **Why:** Machine-local installation paths do not belong in version control;
  named presets make build intent explicit and repeatable for every contributor.
- **Verification:** `cmake --list-presets=all` shows the host-valid configure
  presets; shell syntax checks pass, and shader scripts locate `glslc` through
  PATH.

## STD-006 — Reproducible bootstrap and project documentation

- **Problem:** The repository had no complete project introduction or verified
  new-machine path from installing vcpkg through downloading assets, building,
  and selecting the Vulkan runtime on either supported operating system.
- **Change:** Replaced the README with architecture, features, directory map,
  official vcpkg clone/bootstrap steps, Git LFS setup, macOS and Windows build
  commands, shader workflow, resource rules, macro policy, and troubleshooting.
  Pinned the vcpkg registry baseline used by the dependency manifest.
- **Why:** A build guide is reliable only when it declares every prerequisite
  and uses repository-owned commands rather than relying on previous machine
  state or undocumented global integration.
- **Verification:** Commands are replayed from a fresh build tree; manifest
  dependencies resolve at the pinned baseline and both Debug and Release
  presets configure without personal absolute paths.

## STD-007 — Reproducible Linux build workflow

- **Problem:** Linux was not represented in the CMake presets or bootstrap
  guide, so contributors had to infer the vcpkg triplet, native GLFW packages,
  Vulkan driver checks, executable location, and validation behavior.
- **Change:** Added x64 Linux Debug/Release configure and build presets, an
  Ubuntu/Debian setup and troubleshooting guide, a platform-neutral `glslc`
  installation hint, and explicit selection of the host Vulkan loader.
- **Why:** Linux should use the same repository-owned, repeatable workflow as
  macOS and Windows, with system driver failures clearly separated from C++
  package resolution. The Vulkan loader must remain paired with the host ICD;
  vcpkg may install a newer loader transitively through ImGui.
- **Verification:** `cmake --list-presets` exposes both host-valid Linux
  configure presets, `CMakePresets.json` parses successfully, the shader script
  passes `sh -n`, and both Linux builds resolve `Vulkan::Vulkan` to the system
  loader and launch successfully with the installed NVIDIA ICD.
