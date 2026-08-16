#include "TriangleApplication.hpp"
#include "DebugUtils.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

void TriangleApplication::run()
{
    std::snprintf(importModelPath, sizeof(importModelPath), "%s", MODEL_PATH.c_str());
    InitWindow();
    InitVulkan();
    MainLoop();
    cleanup();
}

void TriangleApplication::InitWindow()
{
    // glfwInit();
    if (glfwInit() != GLFW_TRUE)
    {
        throw std::runtime_error("failed to initialize GLFW");
    }
    glfwInitialized = true;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // GLFW_TRUE：代表可以调整窗口大小
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    if (window == nullptr)
    {
        throw std::runtime_error("failed to create GLFW window");
    }

    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    glfwSetWindowRefreshCallback(window, windowRefreshCallback);
    glfwSetScrollCallback(window, scrollCallback);
}

void TriangleApplication::InitVulkan()
{
    VulkanContextConfig contextConfig{};
    contextConfig.applicationName = "VulkanProject";
    contextConfig.enableValidation = enableValidationLayers;
    contextConfig.apiVersion = VK_API_VERSION_1_0;

    context.initialize(window, contextConfig);
    swapchain.initializeCore(context, window);

    createRenderPass();
    createDescriptorSetLayout();
    createGraphicsPipeline();
    createSkyboxPipeline();

    renderer.initialize(context, swapchain);

    swapchain.createFramebuffers(renderPass);

    initImGui();

    createTextureImage();
    createTextureImageView();
    createTextureSampler();
    createSkyboxImage();
    createSkyboxSampler();
    createIrradianceResources();
    createPrefilterResources();
    createBRDFLUTResources();

    addMeshObject(MeshSource::Sphere);
    createUniformBuffer();

    createDescriptorPool();
    createDescriptorSets();
    createSkyboxDescriptorSets();

    rendererReady = true;
}

TriangleApplication::~TriangleApplication() noexcept
{
    cleanup();
}

void TriangleApplication::MainLoop()
{
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        drawFrame();
    }
}

void TriangleApplication::cleanup() noexcept
{
    if (cleanedUp)
    {
        return;
    }
    cleanedUp = true;
    rendererReady = false;

    const VkResult result = context.waitIdle();
    
    renderer.shutdown();
    swapchain.shutdown();

    if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
    {
        std::fprintf(
            stderr,
            "vkDeviceWaitIdle failed during cleanup: %s (%d)\n",
            vkResultToString(result),
            static_cast<int>(result));
    }

    // Destroy raw Vulkan objects which may reference the RAII buffers/images
    // before releasing the VMA-owned resources themselves.
    mainDeletionQueue.flush();

    uniformBufferMapped.clear();
    uniformBuffers.clear();
    sceneObjects.clear();
    indexBuffer.reset();
    vertexBuffer.reset();

    textureImage.reset();
    normalImage.reset();
    metallicImage.reset();
    roughnessImage.reset();
    aoImage.reset();
    skyboxImage.reset();
    irradianceImage.reset();
    prefilterImage.reset();
    brdfLUTImage.reset();

    context.shutdown();

    if (window != nullptr)
    {
        glfwDestroyWindow(window);
        window = nullptr;
    }

    if (glfwInitialized)
    {
        glfwTerminate();
        glfwInitialized = false;
    }
}


void TriangleApplication::recreateSwapChain()
{
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);

    const SwapchainBuildStatus status = swapchain.buildStatus();

    if (status == SwapchainBuildStatus::Deferred ||
        status == SwapchainBuildStatus::WindowClosed)
    {
        return;
    }

    // while (width == 0 || height == 0)
    // {
    //     if (glfwWindowShouldClose(window))
    //     {
    //         return;
    //     }
    //     glfwGetFramebufferSize(window, &width, &height);
    //     glfwWaitEvents();
    // }

    renderer.waitForAllFrames();
    // Frame Fence 只能证明 Graphics Submission完成
    // Present Queue 可能还在使用旧的 SwapChain Image，必须等待 Present Queue 空闲后才能销毁旧的 SwapChain
    VK_CHECK(vkQueueWaitIdle(context.presentQueue()));

    const VkFormat oldFormat = swapchain.format();

    swapchain.shutdown();
    swapchain.initializeCore(context, window);

    if (oldFormat != VK_FORMAT_UNDEFINED && oldFormat != swapchain.format())
    {
        throw std::runtime_error(
            "swapchain format changed; renderer "
            "resources must be rebuilt");
    }

    swapchain.createFramebuffers(renderPass);
}

void TriangleApplication::framebufferResizeCallback(GLFWwindow *window, int width, int height)
{
    auto app = reinterpret_cast<TriangleApplication *>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
}

void TriangleApplication::windowRefreshCallback(GLFWwindow *window)
{
    auto app = reinterpret_cast<TriangleApplication *>(glfwGetWindowUserPointer(window));
    if (app != nullptr && app->rendererReady && !app->renderer.frameInProgress())
    {
        app->drawFrame();
    }
}


void TriangleApplication::drawFrame()
{
    if (!rendererReady || renderer.frameInProgress())
    {
        return;
    }

    int width = 0;
    int height = 0;

    glfwGetFramebufferSize(window, &width, &height);

    if (width == 0 || height == 0)
    {
        return;
    }

    if (framebufferResized)
    {
        framebufferResized = false;
        recreateSwapChain();
        return;
    }

    const BeginFrameResult beginResult = renderer.beginFrame();
    if (beginResult.status == FrameStatus::Skip)
    {
        return;
    }

    if (beginResult.status == FrameStatus::RecreateSwapchain)
    {
        recreateSwapChain();
        return;
    }

    const FrameToken& frame = beginResult.frame;

    const float now = static_cast<float>(glfwGetTime());
    float deltaTime = now - lastFrameTime;
    lastFrameTime = now;
    deltaTime = std::min(deltaTime, 0.05f);

    // 应用层更新
    processCameraInput(deltaTime);

    updateUniformBuffer(frame.frameIndex, deltaTime);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    drawImGui();
    processModelPicking();
    ImGui::Render();

    recordCommandBuffer(frame.commandBuffer, frame.imageIndex, frame.frameIndex);

    const FrameStatus endStatus = renderer.endFrame(frame);

    if (endStatus == FrameStatus::RecreateSwapchain || framebufferResized)
    {
        framebufferResized = false;
        recreateSwapChain();
    }
}
