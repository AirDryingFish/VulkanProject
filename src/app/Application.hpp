#pragma once

#include "app/CameraController.hpp"
#include "assets/GltfImportTypes.hpp"
#include "core/AppConfig.hpp"
#include "core/DeletionQueue.hpp"
#include "diagnostics/DiagnosticsState.hpp"
#include "render/Environment.hpp"
#include "render/Renderer.hpp"
#include "render/RenderSettings.hpp"
#include "scene/Camera.hpp"
#include "scene/Scene.hpp"
#include "ui/EditorState.hpp"
#include "vulkan/Swapchain.hpp"
#include "vulkan/VulkanCheck.hpp"
#include "vulkan/VulkanContext.hpp"

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

    void run(ScenePreset initialScene = ScenePreset::ShadowPlayground,
             std::uint32_t frameLimit = 0);

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
    struct GltfImportResult
    {
        std::size_t objectCount = 0;
        std::size_t uploadedMeshCount = 0;
        std::size_t reusedMeshCount = 0;
        std::size_t materialCount = 0;
        std::size_t uploadedImageCount = 0;
        std::size_t createdSamplerCount = 0;
    };

    void initializeWindow();
    void initializeRendering();
    void mainLoop();
    void drawFrame();
    void cleanup() noexcept;
    void recreateSwapchain();
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    void initImGui();
    void drawImGui();
    void drawDiagnostics();
    void drawDiagnosticsLive();
    void drawDiagnosticsBaseline();
    void drawDiagnosticsResources();
    void drawTransformGizmo();
    void drawLightOverlays();
    void processModelPicking();

    MeshBuildData buildMeshData(MeshSource source, const std::string& path);
    void addMeshObject(MeshSource source, const std::string& path = {});
    GltfImportResult addGltfMeshObjects(const std::string& path);
    Mesh createMesh(const MeshBuildData& meshData);
    MeshHandle getOrCreateMesh(MeshSource source, const std::string& path);
    void releaseMesh(MeshHandle& mesh);
    void destroyBufferDeferred(GpuBuffer& buffer);
    void createUniformBuffer();
    void updateUniformBuffer(std::uint32_t frameSlot, float deltaTime);

    void createDescriptorPool();
    void createFrameDescriptorSets();
    void createMaterialDescriptorSets();
    void createSkyboxDescriptorSets();
    void ensureMaterialDescriptorCapacity(std::size_t additionalCount) const;
    void allocateMaterialDescriptorSets(const std::vector<MaterialHandle>& materials);
    void writeMaterialDescriptorSet(Material& material);

    DecodedImageData decodeTextureImageFromFileOrFallback(
        const std::string& path, const std::array<unsigned char, 4>& fallbackPixel);
    ImageHandle getOrCreateGltfImage(const GltfImportData& imported, std::size_t imageIndex,
                                   VkFormat format, GltfMaterialUpload& upload);
    GpuImage uploadTexture2D(const DecodedImageData& decoded, VkFormat format,
                            const std::string& debugName);
    ImageHandle createImageResource(const std::string& name, const std::string& path,
                                    VkFormat format,
                                    const std::array<unsigned char, 4>& fallbackPixels);
    MaterialHandle createGltfMaterial(const GltfImportData& imported,
                                      const GltfMaterialData& source,
                                      const std::string& debugName, GltfMaterialUpload& upload);
    SamplerHandle getOrCreateGltfSampler(const GltfSamplerData& source,
                                         const std::string& debugName,
                                         std::vector<SamplerHandle>& library);
    void createMaterialResources();
    void createTextureSampler();

    void loadScenePreset(ScenePreset preset);
    void processPendingScenePreset();
    std::string scenePresetUnavailableReason(ScenePreset preset) const;
    MaterialHandle createPresetMaterial(const std::string& name, const glm::vec4& color,
                                        float metallic, float roughness);
    void buildPresetObjects(ScenePreset preset);
    void releaseSceneMaterialDescriptors(std::size_t first, std::size_t end);
    void pruneSceneCaches();

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
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> frameDescriptorSets;
    std::vector<VkDescriptorSet> skyboxDescriptorSets;
    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;
    DeletionQueue mainDeletionQueue;
};
