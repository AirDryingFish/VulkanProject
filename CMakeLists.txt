cmake_minimum_required(VERSION 3.20)
project(VulkanProject VERSION 0.1.0 LANGUAGES CXX)

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

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)  
find_package(tinyobjloader CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_executable(vulkan
    main.cpp
    src/Buffers.cpp
    src/Camera.cpp
    src/CommandBuffers.cpp
    src/DebugUtils.cpp
    src/Descriptors.cpp
    src/GraphicsPipeline.cpp
    src/IBL.cpp
    src/ImageResources.cpp
    src/Model.cpp
    src/Picking.cpp
    src/Renderer.cpp
    src/SwapChain.cpp
    src/TriangleApplication.cpp
    src/VulkanDevice.cpp
    src/VulkanInstance.cpp
    src/ThirdPartyImplementations.cpp
    src/VulkanAllocator.cpp
    src/ImGuiLayer.cpp
    src/Skybox.cpp
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
    Vulkan::Vulkan
    glfw
    glm::glm
    tinyobjloader::tinyobjloader 
    GPUOpen::VulkanMemoryAllocator
    imgui::imgui
)

add_custom_command(
    TARGET vulkan POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${VULKAN_PROJECT_ASSET_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VULKAN_PROJECT_ASSET_SOURCE_DIR}"
            "${VULKAN_PROJECT_ASSET_DIR}"
    COMMENT "Copying runtime assets"
)
