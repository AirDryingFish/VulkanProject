#pragma once

#include "app/input/CameraController.hpp"
#include "assets/gltf/GltfImportTypes.hpp"
#include "core/AppConfig.hpp"
#include "core/DeletionQueue.hpp"
#include "diagnostics/DiagnosticsState.hpp"
#include "render/environment/Environment.hpp"
#include "render/Renderer.hpp"
#include "render/RenderSettings.hpp"
#include "scene/Camera.hpp"
#include "scene/Scene.hpp"
#include "ui/EditorState.hpp"
#include "ui/EditorUI.hpp"
#include "ui/backend/ImGuiBackend.hpp"
#include "vulkan/presentation/Swapchain.hpp"
#include "vulkan/VulkanCheck.hpp"
#include "vulkan/context/VulkanContext.hpp"
#include "render/shadow/ShadowFrameSummary.hpp"
#include "scene/ManyLights.hpp"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

// 应用编排入口：连接窗口输入、场景、编辑器和渲染器；GPU 资产上传与描述符发布也由这里协调。
// 阅读主线：run() -> initializeRendering() -> drawFrame() -> cleanup()。
class Application final
{
public:
    Application() = default;
    ~Application() noexcept;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    // 单次运行窗口、渲染与交互流程；frameLimit 为 0 时持续运行至窗口关闭。
    void run(ScenePreset initialScene = ScenePreset::ShadowPlayground, std::uint32_t frameLimit = 0);

private:
    using GltfImageKey = std::tuple<std::string, std::size_t, VkFormat>;
    using GltfImageCache = std::map<GltfImageKey, std::weak_ptr<ImageResource>>;

    struct GltfMaterialUpload
    {
        GltfImageCache imageCache;
        std::vector<SamplerHandle> samplers;
        std::vector<MaterialHandle> materials;
        std::size_t uploadedImageCount = 0;
    };

    // 生命周期与帧编排
    void initializeWindow();
    void initializeRendering();
    void mainLoop();
    void drawFrame();
    // 等待 GPU 后按依赖逆序关闭；正常退出和初始化失败共用，允许重复调用。
    void cleanup() noexcept;
    void recreateSwapchain();

    // 输入与编辑器连接
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    void drawImGui();
    void processModelPicking();

    // 逐帧资源发布：只在 beginFrame 已等待对应帧槽后写入和 flush。
    void createUniformBuffer();
    ShadowFrameSummary updateUniformBuffer(std::uint32_t frameSlot, float deltaTime, std::uint32_t shadowPreviewLayer,
                                           float shadowBlendFraction, DeferredFrameUniforms& deferredUniforms);

    // 场景替换事务：新资源准备成功后发布，失败保留原场景。
    void loadScenePreset(ScenePreset preset);
    void processPendingScenePreset();
    std::string scenePresetUnavailableReason(ScenePreset preset) const;
    void buildPresetObjects(ScenePreset preset);
    void applyManyLightsSettings(const ManyLightsSettings& settings);

    // 网格导入与缓存；CPU 构建、GPU 上传分别委托 assets 与 render。
    MeshBuildData buildMeshData(MeshSource source, const std::string& path);
    MeshHandle getOrCreateMesh(MeshSource source, const std::string& path);
    void addMeshObject(MeshSource source, const std::string& path = {});
    GltfImportResult addGltfMeshObjects(const std::string& path);
    void releaseMesh(MeshHandle& mesh);
    void destroyBufferDeferred(GpuBuffer& buffer);
    void pruneSceneCaches();

    // 图像、采样器与材质资源库
    void createTextureSampler();
    void createMaterialResources();
    ImageHandle createImageResource(const std::string& name, const std::string& path, VkFormat format,
                                    const std::array<unsigned char, 4>& fallbackPixels);
    ImageHandle getOrCreateGltfImage(const GltfImportData& imported, std::size_t imageIndex, VkFormat format,
                                     GltfMaterialUpload& upload);
    SamplerHandle getOrCreateGltfSampler(const GltfSamplerData& source, const std::string& debugName,
                                         std::vector<SamplerHandle>& library);
    MaterialHandle createGltfMaterial(const GltfImportData& imported, const GltfMaterialData& source,
                                      const std::string& debugName, GltfMaterialUpload& upload);
    MaterialHandle createPresetMaterial(const std::string& name, const glm::vec4& color, float metallic,
                                        float roughness);

    // 描述符发布与回收；场景事务预留容量，失败时释放尚未发布的材质 set。
    void createDescriptorPool();
    void createFrameDescriptorSets();
    void createSkyboxDescriptorSets();
    void createMaterialDescriptorSets();
    void ensureMaterialDescriptorCapacity(std::size_t additionalCount) const;
    void allocateMaterialDescriptorSets(const std::vector<MaterialHandle>& materials);
    void writeMaterialDescriptorSet(Material& material);
    void releaseSceneMaterialDescriptors(std::size_t first, std::size_t end);

    bool glfwInitialized = false;
    bool cleanedUp = false;
    GLFWwindow* window = nullptr;
    // 成员按声明的逆序析构，context 必须比依赖它的 GPU 资源活得更久。
    // cleanup() 还会先等待 GPU，并显式处理 framebuffer/render pass 等跨对象依赖。
    VulkanContext context;
    Swapchain swapchain;
    Renderer renderer;
    Environment environment;
    Scene scene;
    Camera camera;
    CameraController cameraController;
    EditorState editor;
    DiagnosticsState diagnostics;
    DisplaySettings display;
    ShadowSettings shadows;
    ManyLightsSettings manyLightsSettings{};

    bool rendererReady = false;
    bool framebufferResized = false;
    std::uint32_t frameLimit = 0;
    std::uint32_t renderedFrameCount = 0;
    float lastFrameTime = 0.0f;
    ScenePreset activeScenePreset = ScenePreset::ShadowPlayground;
    // UI 只提交切换意图；下一次 beginFrame() 前才执行，避免录制途中替换资源。
    std::optional<ScenePreset> pendingScenePreset;
    bool preparingScenePreset = false;

    // 弱缓存只负责复用，不延长资源寿命；对象/资源库中的强引用才负责持有资源。
    // 场景切换需让新旧资源短暂共存，准备成功后才能释放旧场景及其描述符。
    std::unordered_map<std::string, std::weak_ptr<Mesh>> meshCache;
    GltfImageCache gltfImageCache;
    std::vector<ImageHandle> textureLibrary;
    std::vector<MaterialHandle> materialLibrary;
    std::vector<SamplerHandle> samplerLibrary;
    ImageHandle defaultBaseColorTexture;
    ImageHandle defaultNormalTexture;
    ImageHandle defaultMetallicRoughnessTexture;
    ImageHandle defaultAoTexture;
    ImageHandle defaultEmissiveTexture;
    SamplerHandle defaultTextureSampler;
    MaterialHandle defaultMaterial;
    MaterialHandle defaultGltfMaterial;
    std::size_t builtinMaterialCount = 0;
    std::vector<Material> builtinMaterialDefaults;
    static constexpr std::uint32_t maxMaterialCount = 128;
    // 切换期间额外预留一整套材质容量，使导入失败时仍能回滚到旧场景。
    static constexpr std::uint32_t sceneSwitchMaterialReserve = maxMaterialCount;
    static constexpr std::uint32_t materialPoolCapacity = maxMaterialCount + sceneSwitchMaterialReserve;
    std::uint32_t allocatedMaterialSetCount = 0;

    // UBO 和 frame/skybox set 按飞行帧资源槽分配，不按交换链图像编号分配。
    std::vector<GpuBuffer> uniformBuffers;
    std::vector<void*> uniformBufferMapped;

    std::vector<GpuBuffer> pointLightBuffers;
    std::vector<void*> pointLightBufferMapped;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> frameDescriptorSets;
    std::vector<VkDescriptorSet> skyboxDescriptorSets;
    ImGuiBackend imguiBackend;
    DeletionQueue mainDeletionQueue;
};
