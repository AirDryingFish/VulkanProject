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
    contextConfig.apiVersion = VK_API_VERSION_1_3;
    contextConfig.minimumApiVersion = VK_API_VERSION_1_0;

    // 从底层依赖开始建立：device -> 交换链 -> render pass/pipeline -> framebuffer。
    context.initialize(window, contextConfig);
    swapchain.initializeCore(context, window);

    renderer.initialize(context, swapchain);

    swapchain.createFramebuffers(renderer.presentRenderPass());

    initImGui();

    // 先创建实际资源，再让 descriptor set 引用它们；描述符本身不拥有纹理/缓冲区。
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
    // 正常退出和析构（包括初始化中途抛异常）共用此入口，因此必须允许重复调用。
    if (cleanedUp)
    {
        return;
    }
    cleanedUp = true;
    rendererReady = false;

    // RAII 只能自动释放 CPU 持有的句柄，不会自动等待 GPU 停止使用这些资源。
    const VkResult result = context.waitIdle();
    if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
    {
        std::fprintf(
            stderr,
            "vkDeviceWaitIdle failed during cleanup: %s (%d)\n",
            vkResultToString(result),
            static_cast<int>(result));
    }

    // 先关闭 ImGui、销毁应用的 descriptor pool，再释放其引用的 UBO/纹理。
    // DeletionQueue 按注册的逆序执行；这里的 GPU 等待由上方统一完成。
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

    // 先清理交换链 framebuffer，再关闭 Renderer，保持本项目的集中清理顺序。
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

    // 当前重建路径只更新尺寸相关资源；格式/图像数量变化还需要重建 pipeline/ImGui 等。
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
    // 回调只记下事件；实际重建留到 drawFrame() 的安全边界处理。
    auto app = reinterpret_cast<Application *>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
}


void Application::drawFrame()
{
    // 每帧阅读顺序：处理延迟变更 -> beginFrame 等待资源 -> 更新/UI -> 录制 -> 提交/呈现。
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

    // 整场景替换要求没有活动帧，并在内部等待旧提交完成，因此要在 beginFrame 前处理。
    processPendingScenePreset();

    const BeginFrameResult beginResult = renderer.beginFrame();

    // beginFrame 会在帧槽 fence 完成后回收旧提交的 GPU 计时。
    // 即使随后 acquire 要求重建交换链，这份已经完成的快照仍应先被消费。
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

    // frameIndex 是可复用资源槽；imageIndex 才是本次 acquire 获得的交换链图像。
    const FrameToken &frame = beginResult.frame;

    const float now = static_cast<float>(glfwGetTime());
    float deltaTime = now - lastFrameTime;
    lastFrameTime = now;
    // 调试暂停或窗口阻塞后限制移动步长，避免相机/动画突然跳跃。
    deltaTime = std::min(deltaTime, 0.05f);

    const auto updateBegin = CpuClock::now();

    // UI 在 UBO 写入前修改场景参数，使这次录制能使用本帧编辑结果。
    cameraController.update(camera, window, deltaTime);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    drawImGui();
    updateUniformBuffer(frame.frameIndex, deltaTime);
    processModelPicking();
    ImGui::Render();

    // 将场景对象整理成渲染所需的只读视图，renderer 无需依赖编辑器或资产导入细节。
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

    // objects/ImGui 数据只在 recordFrame() 同步读取；底层 GPU 资源则必须活到提交完成。
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


    // 调试视图的数值/颜色已有专门含义，不能再经过正常画面的曝光和色调映射。
    renderData.bypassToneMapping = display.debugView != DebugView::Lit;
    renderData.sceneId = scenePresetInfo(activeScenePreset).id;
    renderData.exposureEv = display.exposureEv;
    renderData.toneMapper = display.toneMapper;

    const auto updateEnd = CpuClock::now();

    const auto recordBegin  = CpuClock::now();
    renderer.recordFrame(frame, renderData);
    const auto recordEnd = CpuClock::now();

    // recordFrame 只录制命令；endFrame 提交并请求呈现，返回时 GPU 未必已完成。
    const FrameStatus endStatus = renderer.endFrame(frame);

    diagnostics.latestCpuTiming = {renderer.cpuTimingSnapshot(), cpuElapsedMs(updateBegin, updateEnd), cpuElapsedMs(recordBegin, recordEnd)};

    // 按钮只设置请求；等成功提交后再取真实 submissionId/config 作为采集起点。
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

    // start() 会清空 CPU 缓存，所以必须先 start 再记本次 CPU 数据。
    // 同一帧槽的旧 GPU 数据已在本帧开头消费，之后才能覆盖该槽的 CPU 样本。
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
