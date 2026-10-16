#include "app/Application.hpp"
#include <GLFW/glfw3.h>
#include "vulkan/DebugUtils.hpp"
#include "diagnostics/CpuTiming.hpp"

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

void Application::run(ScenePreset initialScene, std::uint32_t requestedFrameLimit)
{
    if (cleanedUp || window != nullptr)
        throw std::logic_error("Application can only run once");
    const std::string unavailable = scenePresetUnavailableReason(initialScene);
    if (!unavailable.empty())
        throw std::runtime_error(unavailable);
    activeScenePreset = editor.selectedScenePreset = initialScene;
    frameLimit = requestedFrameLimit;
    renderedFrameCount = 0;
    diagnostics.latestCpuTiming = {};
    diagnostics.baseline = {};
    diagnostics.startRequested = false;
    std::snprintf(editor.importModelPath, sizeof(editor.importModelPath), "%s", MODEL_PATH.c_str());
    initializeWindow();
    initializeRendering();
    mainLoop();
    cleanup();
}

void Application::initializeWindow()
{
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

void Application::initializeRendering()
{
    VulkanContextConfig contextConfig{};
    contextConfig.applicationName = "VulkanProject";
    contextConfig.enableValidation = enableValidationLayers;
    contextConfig.apiVersion = VK_API_VERSION_1_0;

    context.initialize(window, contextConfig);
    swapchain.initializeCore(context, window);

    renderer.initialize(context, swapchain);

    swapchain.createFramebuffers(renderer.presentRenderPass());

    initImGui();

    // 创建材质时就要给slot赋sampler，默认sampler必须提前存在
    createTextureSampler();
    createMaterialResources();
    builtinMaterialCount = materialLibrary.size();
    for (const auto& material : materialLibrary)
        builtinMaterialDefaults.push_back(*material);
    environment.initialize(context, renderer);

    createUniformBuffer();

    createDescriptorPool();
    createFrameDescriptorSets();
    createMaterialDescriptorSets();
    createSkyboxDescriptorSets();

    loadScenePreset(activeScenePreset);

    rendererReady = true;
}

Application::~Application() noexcept
{
    cleanup();
}

void Application::mainLoop()
{
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        drawFrame();
    }
}

void Application::cleanup() noexcept
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
    scene.objects.clear();
    meshCache.clear();
    materialLibrary.clear();
    defaultMaterial.reset();
    defaultGltfMaterial.reset();
    builtinMaterialDefaults.clear();

    defaultTextureSampler.reset();
    samplerLibrary.clear();

    defaultBaseColorTexture.reset();
    defaultNormalTexture.reset();
    defaultMetallicRoughnessTexture.reset();
    defaultAoTexture.reset();
    defaultEmissiveTexture.reset();

    textureLibrary.clear();
    gltfImageCache.clear();

    environment.shutdown();

    swapchain.destroyFramebuffers();

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

void Application::recreateSwapchain()
{
    framebufferResized = true;

    const VkFormat oldFormat = swapchain.format();
    const VkColorSpaceKHR oldColorSpace = swapchain.colorSpace();
    const uint32_t oldMinImageCount = swapchain.minImageCount();
    const std::size_t oldImageCount = swapchain.imageCount();

    const SwapchainBuildStatus status = swapchain.rebuildCore();

    if (status != SwapchainBuildStatus::Ready)
    {
        return;
    }

    if (oldFormat != swapchain.format() ||
        oldColorSpace != swapchain.colorSpace() ||
        oldMinImageCount != swapchain.minImageCount() ||
        oldImageCount != swapchain.imageCount()
    )
    {
        throw std::runtime_error(
            "swapchain format changed; renderer "
            "resources must be rebuilt");
    }
    renderer.recreateHdrTargets();
    swapchain.createFramebuffers(renderer.presentRenderPass());

    framebufferResized = false;
}

void Application::framebufferResizeCallback(GLFWwindow *window, int width, int height)
{
    auto app = reinterpret_cast<Application *>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
}


void Application::drawFrame()
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
        diagnostics.baseline.cancel("Capture cancelled: window or scene changed.");
        diagnostics.startRequested = false;
        return;
    }

    if (framebufferResized)
    {
        diagnostics.baseline.cancel("Capture cancelled: window or scene changed.");
        diagnostics.startRequested = false;
        recreateSwapchain();
        return;
    }

    // 请求切换场景时立即结束采集，包括重新加载同一场景的请求
    if (pendingScenePreset.has_value())
    {
        diagnostics.baseline.cancel("Capture cancelled: scene changed.");
        diagnostics.startRequested = false;
    }

    // UI queues changes; retire the old scene before acquiring an active frame.
    processPendingScenePreset();

    const BeginFrameResult beginResult = renderer.beginFrame();

    // 每次 beginFrame后消费一次 GPU 快照
    diagnostics.baseline.consume(renderer.gpuTimingSnapshot());

    if (beginResult.status == FrameStatus::Skip)
    {
        return;
    }

    if (beginResult.status == FrameStatus::RecreateSwapchain)
    {
        diagnostics.baseline.cancel("Capture cancelled: window or scene changed.");
        diagnostics.startRequested = false;
        recreateSwapchain();
        return;
    }

    const FrameToken &frame = beginResult.frame;

    const float now = static_cast<float>(glfwGetTime());
    float deltaTime = now - lastFrameTime;
    lastFrameTime = now;
    deltaTime = std::min(deltaTime, 0.05f);

    const auto updateBegin = CpuClock::now();

    // 应用层更新
    cameraController.update(camera, window, deltaTime);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    drawImGui();
    updateUniformBuffer(frame.frameIndex, deltaTime);
    processModelPicking();
    ImGui::Render();

    std::vector<RenderObjectView> renderObjects;
    renderObjects.reserve(scene.objects.size());

    for (const SceneObject& object : scene.objects)
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
        view.doubleSided = material.doubleSided;
        view.pushConstants.model = object.modelMatrix();
        view.pushConstants.baseColorFactor = material.baseColorFactor;
        view.pushConstants.materialFactors = glm::vec4(material.metallicFactor, material.roughnessFactor, material.occlusionStrength, material.normalScale);
        view.pushConstants.emissiveFactor = glm::vec4(material.emissiveFactor, material.alphaCutoff);
        const bool useVertexTangents = mesh.hasTangents && material.normalTexture.texCoord == 0u;
        view.pushConstants.textureInfo = glm::uvec4(material.textureUvMask(), useVertexTangents, material.alphaMode == MaterialAlphaMode::Mask ? 1u : 0u, 0u);
        view.materialDescriptorSet = material.descriptorSet;

        renderObjects.push_back(view);
    }

    RenderFrameData renderData{};
    renderData.objects = &renderObjects;
    renderData.frameDescriptorSet = frameDescriptorSets.at(frame.frameIndex);
    renderData.skyboxDescriptorSet = skyboxDescriptorSets.at(frame.frameIndex);
    renderData.imguiDrawData = ImGui::GetDrawData();

    renderData.clearColor = display.clearColor;
    // 材质、法线、光空间视图：黑色背景
    // LinearDepth：白色背景，表示没有表面。
    // HDR luminance：保留正常场景背景和天空。
    if (isSurfaceDebugView(display.debugView) || display.debugView == DebugView::ShadowProjection)
    {
        renderData.clearColor = display.debugView == DebugView::LinearDepth ? glm::vec4(1.0f) : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }


    renderData.directionalShadowsEnabled = shadows.enabled && scene.directionalLight.enabled;
    renderData.shadowConstantBias = shadows.constantBias;
    renderData.shadowSlopeBias = shadows.slopeBias;
    renderData.showShadowDepth = shadows.showDepth;

    renderData.debugView = display.debugView;
    renderData.debugDepthNear = camera.nearPlane;
    renderData.debugDepthFar = std::max(display.debugDepthFar, camera.nearPlane + 0.001f);


    renderData.bypassToneMapping = display.debugView != DebugView::Lit;
    renderData.sceneId = scenePresetInfo(activeScenePreset).id;
    renderData.exposureEv = display.exposureEv;
    renderData.toneMapper = display.toneMapper;

    const auto updateEnd = CpuClock::now();

    const auto recordBegin  = CpuClock::now();
    renderer.recordFrame(frame, renderData);
    const auto recordEnd = CpuClock::now();

    // 上面录制完毕后，endFrame 实际发送给 GPU 执行绘制
    const FrameStatus endStatus = renderer.endFrame(frame);

    diagnostics.latestCpuTiming = {renderer.cpuTimingSnapshot(), cpuElapsedMs(updateBegin, updateEnd), cpuElapsedMs(recordBegin, recordEnd)};

    if (diagnostics.startRequested)
    {
        diagnostics.startRequested = false;

        if (endStatus == FrameStatus::Ready && !framebufferResized)
        {
            diagnostics.baseline.start(
                diagnostics.latestCpuTiming.renderer.submissionId,
                diagnostics.latestCpuTiming.renderer.config
            );
        }
    }

    diagnostics.baseline.rememberCpu(diagnostics.latestCpuTiming);

    ++renderedFrameCount;
    if (frameLimit != 0 && renderedFrameCount >= frameLimit)
        glfwSetWindowShouldClose(window, GLFW_TRUE);

    if (endStatus == FrameStatus::RecreateSwapchain || framebufferResized)
    {
        diagnostics.baseline.cancel("Capture cancelled: window or scene changed.");
        diagnostics.startRequested = false;
        recreateSwapchain();
    }
}

void Application::scrollCallback(GLFWwindow* window, double, double yoffset)
{
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (app != nullptr)
        app->cameraController.addScroll(yoffset);
}
