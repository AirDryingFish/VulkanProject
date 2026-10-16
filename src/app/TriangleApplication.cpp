#include "app/TriangleApplication.hpp"
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

void TriangleApplication::run(ScenePreset initialScene, std::uint32_t requestedFrameLimit)
{
    const std::string unavailable = scenePresetUnavailableReason(initialScene);
    if (!unavailable.empty())
        throw std::runtime_error(unavailable);
    activeScenePreset = selectedScenePreset = initialScene;
    frameLimit = requestedFrameLimit;
    renderedFrameCount = 0;
    latestCpuTiming = {};
    gpuBaseline = {};
    baselineStartRequested = false;
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

    swapchain.createFramebuffers(renderer.presentRenderPass());

    initImGui();

    // 创建材质时就要给slot赋sampler，默认sampler必须提前存在
    createTextureSampler();
    createMaterialResources();
    builtinMaterialCount = materialLibrary.size();
    for (const auto& material : materialLibrary)
        builtinMaterialDefaults.push_back(*material);
    createSkyboxImage();
    createSkyboxSampler();
    createIrradianceResources();
    createPrefilterResources();
    createBRDFLUTResources();

    createUniformBuffer();

    createDescriptorPool();
    createFrameDescriptorSets();
    createMaterialDescriptorSets();
    createSkyboxDescriptorSets();

    loadScenePreset(activeScenePreset);

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
    defaultGltfMaterial.reset();
    builtinMaterialDefaults.clear();

    brdfLUTSampler.reset();
    prefilterSampler.reset();
    irradianceSampler.reset();
    skyboxSampler.reset();
    defaultTextureSampler.reset();
    samplerLibrary.clear();

    defaultBaseColorTexture.reset();
    defaultNormalTexture.reset();
    defaultMetallicRoughnessTexture.reset();
    defaultAoTexture.reset();
    defaultEmissiveTexture.reset();

    textureLibrary.clear();
    gltfImageCache.clear();

    skyboxImage.reset();
    irradianceImage.reset();
    prefilterImage.reset();
    brdfLUTImage.reset();

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

void TriangleApplication::recreateSwapChain()
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
        gpuBaseline.cancel("Capture cancelled: window or scene changed.");
        baselineStartRequested = false;
        return;
    }

    if (framebufferResized)
    {
        gpuBaseline.cancel("Capture cancelled: window or scene changed.");
        baselineStartRequested = false;
        recreateSwapChain();
        return;
    }

    // 请求切换场景时立即结束采集，包括重新加载同一场景的请求
    if (pendingScenePreset.has_value())
    {
        gpuBaseline.cancel("Capture cancelled: scene changed.");
        baselineStartRequested = false;
    }

    // UI queues changes; retire the old scene before acquiring an active frame.
    processPendingScenePreset();

    const BeginFrameResult beginResult = renderer.beginFrame();

    // 每次 beginFrame后消费一次 GPU 快照
    gpuBaseline.consume(renderer.gpuTimingSnapshot());

    if (beginResult.status == FrameStatus::Skip)
    {
        return;
    }

    if (beginResult.status == FrameStatus::RecreateSwapchain)
    {
        gpuBaseline.cancel("Capture cancelled: window or scene changed.");
        baselineStartRequested = false;
        recreateSwapChain();
        return;
    }

    const FrameToken &frame = beginResult.frame;

    const float now = static_cast<float>(glfwGetTime());
    float deltaTime = now - lastFrameTime;
    lastFrameTime = now;
    deltaTime = std::min(deltaTime, 0.05f);

    const auto updateBegin = CpuClock::now();

    // 应用层更新
    processCameraInput(deltaTime);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    drawImGui();
    updateUniformBuffer(frame.frameIndex, deltaTime);
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
        view.doubleSided = material.doubleSided;
        view.pushConstants.model = getObjectMatrix(object);
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

    renderData.clearColor = clearColor;
    // 材质、法线、光空间视图：黑色背景
    // LinearDepth：白色背景，表示没有表面。
    // HDR luminance：保留正常场景背景和天空。
    if (isSurfaceDebugView(debugView) || debugView == DebugView::ShadowProjection)
    {
        renderData.clearColor = debugView == DebugView::LinearDepth ? glm::vec4(1.0f) : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }


    renderData.directionalShadowsEnabled = directionalShadowsEnabled && directionalLight.enabled;
    renderData.shadowConstantBias = shadowConstantBias;
    renderData.shadowSlopeBias = shadowSlopeBias;
    renderData.showShadowDepth = showShadowDepth;

    renderData.debugView = debugView;
    renderData.debugDepthNear = cameraNear;
    renderData.debugDepthFar = std::max(debugDepthFar, cameraNear + 0.001f);


    renderData.bypassToneMapping = debugView != DebugView::Lit;
    renderData.sceneId = scenePresetInfo(activeScenePreset).id;
    renderData.exposureEv = exposureEv;
    renderData.toneMapper = toneMapper;

    const auto updateEnd = CpuClock::now();

    const auto recordBegin  = CpuClock::now();
    renderer.recordFrame(frame, renderData);
    const auto recordEnd = CpuClock::now();

    // 上面录制完毕后，endFrame 实际发送给 GPU 执行绘制
    const FrameStatus endStatus = renderer.endFrame(frame);

    latestCpuTiming = {renderer.cpuTimingSnapshot(), cpuElapsedMs(updateBegin, updateEnd), cpuElapsedMs(recordBegin, recordEnd)};

    if (baselineStartRequested)
    {
        baselineStartRequested = false;

        if (endStatus == FrameStatus::Ready && !framebufferResized)
        {
            gpuBaseline.start(
                latestCpuTiming.renderer.submissionId,
                latestCpuTiming.renderer.config
            );
        }
    }

    gpuBaseline.rememberCpu(latestCpuTiming);

    ++renderedFrameCount;
    if (frameLimit != 0 && renderedFrameCount >= frameLimit)
        glfwSetWindowShouldClose(window, GLFW_TRUE);

    if (endStatus == FrameStatus::RecreateSwapchain || framebufferResized)
    {
        gpuBaseline.cancel("Capture cancelled: window or scene changed.");
        baselineStartRequested = false;
        recreateSwapChain();
    }
}
