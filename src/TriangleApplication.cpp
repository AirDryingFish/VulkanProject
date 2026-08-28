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

    renderer.initialize(context, swapchain);

    swapchain.createFramebuffers(renderer.renderPass());

    initImGui();


    createMaterialResources();
    createTextureSampler();
    createSkyboxImage();
    createSkyboxSampler();
    createIrradianceResources();
    createPrefilterResources();
    createBRDFLUTResources();

    testSceneInit();

    createUniformBuffer();

    createDescriptorPool();
    // createDescriptorSets();
    createFrameDescriptorSets();
    createMaterialDescriptorSets();
    createSkyboxDescriptorSets();

    rendererReady = true;
}

void TriangleApplication::testSceneInit()
{
    addMeshObject(MeshSource::Sphere);
    sceneObjects.back().name = "Rusted Iron Sphere";
    sceneObjects.back().transform.position.x = -1.2f;

    addMeshObject(MeshSource::Sphere);
    sceneObjects.back().name = "Variant Sphere";
    sceneObjects.back().material = materialLibrary.at(1);
    sceneObjects.back().transform.position.x = 1.2f;
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
    meshCache.clear();
    materialLibrary.clear();
    defaultMaterial.reset();

    brdfLUTSampler.reset();
    prefilterSampler.reset();
    irradianceSampler.reset();
    skyboxSampler.reset();
    textureSampler.reset();


    defaultBaseColorTexture.reset();
    defaultNormalTexture.reset();
    defaultMetallicTexture.reset();
    defaultRoughnessTexture.reset();
    defaultAoTexture.reset();
    defaultEmissiveTexture.reset();

    textureLibrary.clear();

    skyboxImage.reset();
    irradianceImage.reset();
    prefilterImage.reset();
    brdfLUTImage.reset();

    swapchain.destroyFramebuffersAndAttachments();

    renderer.shutdown();
    swapchain.shutdown();

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

    const SwapchainBuildStatus status = swapchain.rebuildCore();

    if (status != SwapchainBuildStatus::Ready)
    {
        return;
    }

    const VkFormat oldFormat = swapchain.format();

    if (oldFormat != VK_FORMAT_UNDEFINED && oldFormat != swapchain.format())
    {
        throw std::runtime_error(
            "swapchain format changed; renderer "
            "resources must be rebuilt");
    }

    swapchain.createFramebuffers(renderer.renderPass());
}

void TriangleApplication::framebufferResizeCallback(GLFWwindow *window, int width, int height)
{
    auto app = reinterpret_cast<TriangleApplication *>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
}


void TriangleApplication::drawFrame()
{
    if (!rendererReady || renderer.hasActiveFrame())
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

    const FrameToken &frame = beginResult.frame;

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

    std::vector<RenderObjectView> renderObjects;
    renderObjects.reserve(sceneObjects.size());

    for (const SceneObject& object : sceneObjects)
    {
        if (!object.mesh || !object.mesh->valid() || !object.material || object.material->descriptorSet == VK_NULL_HANDLE)
        {
            continue;
        }
        const Mesh& mesh = *object.mesh;
        const Material& material = *object.material;

        RenderObjectView view{};
        view.vertexBuffer = mesh.vertexBuffer.get();
        view.indexBuffer = mesh.indexBuffer.get();
        view.indexCount = mesh.indexCount;
        view.pushConstants.model = getObjectMatrix(object);
        view.pushConstants.baseColorFactor = material.baseColorFactor;
        view.pushConstants.materialFactors = glm::vec4(material.metallicFactor, material.roughnessFactor, material.aoFactor, 0.0f);
        view.pushConstants.emissiveFactor = glm::vec4(material.emissiveFactor, 0.0f);
        view.materialDescriptorSet = material.descriptorSet;

        renderObjects.push_back(view);
    }

    RenderFrameData renderData{};
    renderData.objects = &renderObjects;
    renderData.frameDescriptorSet = frameDescriptorSets.at(frame.frameIndex);
    renderData.skyboxDescriptorSet = skyboxDescriptorSets.at(frame.frameIndex);
    renderData.imguiDrawData = ImGui::GetDrawData();
    renderData.clearColor = clearColor;

    renderer.recordFrame(frame, renderData);

    const FrameStatus endStatus = renderer.endFrame(frame);

    if (endStatus == FrameStatus::RecreateSwapchain || framebufferResized)
    {
        framebufferResized = false;
        recreateSwapChain();
    }
}
