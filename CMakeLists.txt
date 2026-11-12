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
find_package(nfd CONFIG REQUIRED)

add_library(gltf_import STATIC
    src/assets/mesh/MeshBuilder.hpp
    src/assets/mesh/MeshBuilder.cpp
    src/assets/ImportResult.hpp
    src/assets/gltf/GltfLoader.cpp
    src/assets/gltf/GltfImages.cpp
    src/assets/gltf/GltfMaterials.cpp
    src/assets/gltf/GltfGeometry.cpp
    src/assets/gltf/GltfImportInternal.hpp
    src/assets/image/ImageData.hpp
    src/assets/image/SkyboxImage.hpp
    src/assets/image/SkyboxImage.cpp
    src/assets/gltf/GltfImportTypes.hpp
    src/assets/gltf/GltfLoader.hpp
    src/assets/image/ImageDecode.hpp
    src/assets/image/ImageDecode.cpp
)

target_include_directories(gltf_import PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE
        ${Stb_INCLUDE_DIR}
)

target_link_libraries(gltf_import
    PUBLIC
        glm::glm
    PRIVATE
        fastgltf::fastgltf
        tinyobjloader::tinyobjloader
)

target_compile_definitions(gltf_import PUBLIC GLM_ENABLE_EXPERIMENTAL)

add_library(frame_diagnostics STATIC
    src/diagnostics/BaselineCapture.cpp
    src/diagnostics/BaselineExport.cpp
)
target_include_directories(frame_diagnostics PUBLIC src)
target_link_libraries(frame_diagnostics PUBLIC glm::glm Vulkan::Vulkan)

add_executable(gltf_inspect
    tools/GltfInspect.cpp
)

target_link_libraries(gltf_inspect PRIVATE
    gltf_import
)

add_executable(vulkan
    main.cpp

    # Application orchestration and resource initialization
    src/app/Application.hpp
    src/app/Application.cpp
    src/app/EditorBridge.cpp
    src/app/input/CameraController.hpp
    src/app/input/CameraController.cpp
    src/app/input/Picking.cpp
    src/app/resources/FrameResources.cpp
    src/app/resources/Descriptors.cpp
    src/app/resources/MaterialDescriptors.cpp
    src/app/resources/MaterialResources.cpp
    src/app/resources/ImageResources.cpp
    src/app/scene/Model.cpp
    src/app/scene/ScenePresets.cpp

    # Scene data and preset registry
    src/scene/Mesh.hpp
    src/scene/MeshData.hpp
    src/scene/ScenePicking.hpp
    src/scene/ScenePicking.cpp
    src/scene/Material.hpp
    src/scene/SceneTypes.hpp
    src/scene/Scene.hpp
    src/scene/Scene.cpp
    src/scene/Camera.hpp
    src/scene/Camera.cpp
    src/scene/Lighting.hpp
    src/scene/Vertex.hpp
    src/scene/ScenePresets.hpp

    # Frame recording and render passes
    src/render/Renderer.hpp
    src/render/Renderer.cpp
    src/render/frame/GpuTiming.cpp
    src/render/frame/FrameSubmission.cpp
    src/render/forward/ForwardRendering.cpp
    src/render/post/PresentPass.cpp
    src/render/deferred/GBufferPreview.cpp
    src/render/resources/RenderTargetDiagnostics.cpp
    src/render/frame/SceneUniforms.hpp
    src/render/frame/SceneUniforms.cpp
    src/render/pipeline/VertexInput.hpp
    src/render/pipeline/VertexInput.cpp
    src/render/resources/MeshUpload.hpp
    src/render/resources/MeshUpload.cpp
    src/render/resources/TextureUpload.hpp
    src/render/resources/TextureUpload.cpp
    src/render/frame/SceneFrameBuilder.hpp
    src/render/frame/SceneFrameBuilder.cpp
    src/render/RenderTypes.hpp
    src/render/RenderSettings.hpp
    src/render/ShaderTypes.hpp
    src/render/environment/Environment.hpp
    src/render/environment/Environment.cpp
    src/render/environment/EnvironmentIBL.cpp
    src/render/environment/EnvironmentSkybox.cpp
    src/render/pipeline/GraphicsPipeline.cpp
    src/render/shadow/ShadowMapping.cpp
    src/render/shadow/ShadowCascades.hpp
    src/render/shadow/ShadowCascades.cpp
    src/render/shadow/ShadowFrameSummary.hpp
    src/render/post/PostProcessing.cpp

    # Vulkan backend and resource lifetime
    src/vulkan/context/VulkanContext.hpp
    src/vulkan/context/VulkanContext.cpp
    src/vulkan/context/VulkanInstance.cpp
    src/vulkan/context/VulkanDevice.cpp
    src/vulkan/resources/VulkanResourceFactory.cpp
    src/vulkan/upload/UploadContext.hpp
    src/vulkan/upload/UploadContext.cpp
    src/vulkan/context/detail/ApiVersion.hpp
    src/vulkan/VulkanHeaders.hpp
    src/vulkan/VulkanTypes.hpp
    src/vulkan/resources/VulkanResources.hpp
    src/vulkan/resources/VulkanResources.cpp
    src/vulkan/VulkanCheck.hpp
    src/vulkan/presentation/Swapchain.hpp
    src/vulkan/presentation/Swapchain.cpp
    src/vulkan/upload/UploadCommands.hpp
    src/vulkan/upload/UploadCommands.cpp
    src/vulkan/debug/DebugUtils.hpp
    src/vulkan/debug/DebugUtils.cpp
    src/vulkan/resources/VmaImplementation.cpp

    # Editor and diagnostics UI
    src/ui/EditorUI.hpp
    src/ui/EditorUI.cpp
    src/ui/panels/EditorPanels.hpp
    src/ui/panels/ScenePanel.cpp
    src/ui/panels/InspectorPanel.cpp
    src/ui/panels/LightingPanel.cpp
    src/ui/panels/ShadowPanel.cpp
    src/ui/panels/CameraPanel.cpp
    src/ui/panels/DisplayPanel.cpp
    src/ui/viewport/ViewportOverlays.cpp
    src/ui/backend/ImGuiBackend.hpp
    src/ui/backend/ImGuiBackend.cpp
    src/ui/panels/Diagnostics.cpp
    src/ui/EditorState.hpp
    src/ui/FileDialog.hpp
    src/ui/FileDialog.cpp

    # Timing and baseline capture
    src/diagnostics/BaselineCapture.hpp
    src/diagnostics/BaselineContext.hpp
    src/diagnostics/BaselineContext.cpp
    src/diagnostics/FrameTiming.hpp
    src/diagnostics/DiagnosticsState.hpp
    src/diagnostics/CpuTiming.hpp

    # Configuration and shared utilities
    src/core/AppConfig.hpp
    src/core/FrameConfig.hpp
    src/core/DeletionQueue.hpp
    src/core/FileUtils.hpp
    src/core/FileUtils.cpp

    # Deferred rendering
    src/render/deferred/DeferredMath.hpp
    src/render/deferred/DeferredMath.cpp
    src/render/deferred/DeferredRendering.cpp
    src/render/deferred/DeferredLighting.cpp
    src/render/deferred/TiledLighting.cpp

    src/scene/ManyLights.hpp
    src/scene/ManyLights.cpp

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
    nfd::nfd
    gltf_import
    frame_diagnostics
    Vulkan::Vulkan
    glfw
    glm::glm
    tinyobjloader::tinyobjloader
    GPUOpen::VulkanMemoryAllocator
    imgui::imgui
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    get_filename_component(
        VULKAN_PROJECT_SYSTEM_VULKAN_DIR
        "${VULKAN_PROJECT_SYSTEM_VULKAN_LOADER}"
        DIRECTORY
    )

    set_property(
        TARGET vulkan
        PROPERTY BUILD_RPATH
        "${VULKAN_PROJECT_SYSTEM_VULKAN_DIR}"
    )
endif()

if(BUILD_TESTING)
    add_executable(camera_tests tests/CameraTests.cpp src/scene/Camera.cpp)
    target_include_directories(camera_tests PRIVATE src)
    target_link_libraries(camera_tests PRIVATE glm::glm)
    add_test(NAME camera_tests COMMAND camera_tests)

    add_executable(scene_tests tests/SceneTests.cpp src/scene/Scene.cpp)
    target_include_directories(scene_tests PRIVATE src)
    target_compile_definitions(scene_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)
    target_link_libraries(scene_tests PRIVATE glm::glm Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
    add_test(NAME scene_tests COMMAND scene_tests)

    add_executable(baseline_capture_tests tests/BaselineCaptureTests.cpp)
    target_include_directories(baseline_capture_tests PRIVATE src)
    target_link_libraries(baseline_capture_tests PRIVATE frame_diagnostics)
    add_test(NAME baseline_capture_tests COMMAND baseline_capture_tests)

    add_executable(shadow_cascade_math_tests
        tests/ShadowCascadeMathTests.cpp
        src/render/shadow/ShadowCascades.cpp
        src/scene/Camera.cpp
    )

    target_include_directories(
        shadow_cascade_math_tests PRIVATE src
    )

    target_link_libraries(
        shadow_cascade_math_tests PRIVATE glm::glm
    )

    add_test(
        NAME shadow_cascade_math_tests
        COMMAND shadow_cascade_math_tests
    )

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

    add_executable(scene_preset_tests tests/ScenePresetTests.cpp)
    target_link_libraries(scene_preset_tests PRIVATE gltf_import)
    add_test(
        NAME scene_preset_tests
        COMMAND scene_preset_tests "${CMAKE_CURRENT_SOURCE_DIR}/assets"
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
