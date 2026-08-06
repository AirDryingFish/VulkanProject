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

TriangleApplication::~TriangleApplication() noexcept
{
    Cleanup();
}

void TriangleApplication::MainLoop()
{
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        drawFrame();
    }
}

void TriangleApplication::Cleanup() noexcept
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
        if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
        {
            std::fprintf(
                stderr, 
                "vkDeviceWaitIdle failed during cleanup!",             
                static_cast<int>(result),
                vkResultToString(result)
            );
;
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
    
    for (FrameContext& frame : frames)
    {
        frame.deletionQueue.flush();
    }
    mainDeletionQueue.flush();

    if (allocator != nullptr)
    {
        vmaDestroyAllocator(allocator);
        allocator = nullptr;
    }

    if (device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }

    if (instance != VK_NULL_HANDLE && debugMessenger != VK_NULL_HANDLE)
    {
        DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        debugMessenger = VK_NULL_HANDLE;
    }

    if (instance != VK_NULL_HANDLE && surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
    
    if (instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }

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
