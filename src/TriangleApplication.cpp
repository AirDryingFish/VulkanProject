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
void TriangleApplication::run()
{
    std::snprintf(importModelPath, sizeof(importModelPath), "%s", MODEL_PATH.c_str());
    InitWindow();
    InitVulkan();
    MainLoop();
    Cleanup();
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
    glfwSetFramebufferSizeCallback(window, framebufferRizeCallback);
    glfwSetWindowRefreshCallback(window, windowRefreshCallback);
    glfwSetScrollCallback(window, scrollCallback);
}

void TriangleApplication::InitVulkan()
{
    CreateInstance();
    setupDebugMessenger();
    createSurface();

    pickPhysicalDevice();
    createLogicalDevice();

    createAllocator();

    createSwapChain();
    createImageViews();

    createRenderPass();
    createDescriptorSetLayout();
    createGraphicsPipeline();
    createSkyboxPipeline();

    // createCommandPool();
    createUploadContext();
    createFrameContexts();

    createColorResources();
    createDepthResources();

    createFramebuffers();

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

void TriangleApplication::MainLoop()
{
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        drawFrame();
    }
}

void TriangleApplication::Cleanup()
{
    if (cleanedUp)
    {
        return;
    }
    cleanedUp = true;
    rendererReady = false;

    if (device != VK_NULL_HANDLE)
    {
        const VkResult result = vkDeviceWaitIdle(device);
        if (result != VK_NULL_HANDLE && result != VK_ERROR_DEVICE_LOST)
        {
            std::fprintf(stderr, "vkDeviceWaitIdle failed during cleanup!");
            vkResultToString(result);
            static_cast<int>(result);
        }
    }

    cleanupSwapChain();
    for (SceneObject &object : sceneObjects)
    {
        destroySceneObject(object);
    }
    sceneObjects.clear();
    destroyBuffer(indexBuffer);
    destroyBuffer(vertexBuffer);
    mainDeletionQueue.flush();
    for (FrameContext& frame : frames)
    {
        frame.deletionQueue.flush();
    }

    vmaDestroyAllocator(allocator);
    vkDestroyDevice(device, nullptr);
    if (enableValidationLayers)
    {
        DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
    }
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);

    glfwDestroyWindow(window);
    glfwTerminate();
}
