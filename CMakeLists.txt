cmake_minimum_required(VERSION 3.20)
project(VulkanProject VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(VULKAN_PROJECT_VALIDATION_DEFAULT ON)
else()
    set(VULKAN_PROJECT_VALIDATION_DEFAULT OFF)
endif()
option(
    VULKAN_PROJECT_ENABLE_VALIDATION
    "Enable Vulkan validation layers"
    ${VULKAN_PROJECT_VALIDATION_DEFAULT}
)

set(VULKAN_PROJECT_PLATFORM_WINDOWS 0)
set(VULKAN_PROJECT_PLATFORM_MACOS 0)
if(WIN32)
    set(VULKAN_PROJECT_PLATFORM_WINDOWS 1)
elseif(APPLE)
    set(VULKAN_PROJECT_PLATFORM_MACOS 1)
endif()

set(VULKAN_PROJECT_ASSET_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")
set(VULKAN_PROJECT_ASSET_DIR "${CMAKE_CURRENT_BINARY_DIR}/assets")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    cmake/BuildConfig.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/BuildConfig.hpp"
    @ONLY
)

# The Vulkan loader is part of the Linux graphics-driver stack. Keep using the
# system loader even though vcpkg installs another copy as an ImGui dependency;
# mixing a newer vcpkg loader with the host ICD can make surface creation fail.
if(UNIX AND NOT APPLE)
    find_library(
        VULKAN_PROJECT_SYSTEM_VULKAN_LOADER
        NAMES vulkan
        PATHS
            "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
            /usr/lib64
            /usr/lib
        NO_DEFAULT_PATH
        REQUIRED
    )
    set(
        Vulkan_LIBRARY
        "${VULKAN_PROJECT_SYSTEM_VULKAN_LOADER}"
        CACHE FILEPATH
        "System Vulkan loader used by the host graphics driver"
        FORCE
    )
endif()

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)

add_library(gltf_import STATIC
    src/GltfLoader.cpp
    src/GltfImportTypes.hpp
    src/GltfLoader.hpp
)

target_include_directories(gltf_import PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(gltf_import
    PUBLIC
        glm::glm
    PRIVATE
        fastgltf::fastgltf
)

add_executable(gltf_inspect
    tools/GltfInspect.cpp
)

target_link_libraries(gltf_inspect PRIVATE
    gltf_import
)

add_executable(vulkan
    main.cpp
    src/Buffers.cpp
    src/Camera.cpp
    src/DebugUtils.cpp
    src/Descriptors.cpp
    src/GraphicsPipeline.cpp
    src/IBL.cpp
    src/ImageResources.cpp
    src/Model.cpp
    src/Picking.cpp
    src/TriangleApplication.cpp
    src/ThirdPartyImplementations.cpp
    src/ImGuiLayer.cpp
    src/Skybox.cpp
    src/VulkanResources.cpp
    src/VulkanContext.cpp
    src/Swapchain.cpp
    src/Renderer.cpp
    src/FileUtils.cpp
    src/UploadCommands.cpp
    src/Mesh.hpp
)

target_include_directories(vulkan PRIVATE
    src
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    ${Stb_INCLUDE_DIR}
)

target_compile_definitions(vulkan PRIVATE
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_ENABLE_EXPERIMENTAL
)

target_link_libraries(vulkan PRIVATE
    gltf_import
    Vulkan::Vulkan
    glfw
    glm::glm
    tinyobjloader::tinyobjloader
    GPUOpen::VulkanMemoryAllocator
    imgui::imgui
)

if(BUILD_TESTING)
    add_executable(gltf_loader_tests
        tests/GltfLoaderTests.cpp
    )

    target_link_libraries(gltf_loader_tests PRIVATE
        gltf_import
    )

    add_test(
        NAME gltf_loader_tests
        COMMAND gltf_loader_tests
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/gltf"
    )
endif()

add_custom_command(
    TARGET vulkan POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${VULKAN_PROJECT_ASSET_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VULKAN_PROJECT_ASSET_SOURCE_DIR}"
            "${VULKAN_PROJECT_ASSET_DIR}"
    COMMENT "Copying runtime assets"
)
