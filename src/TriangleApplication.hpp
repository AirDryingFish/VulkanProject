#pragma once

#include "AppConfig.hpp"
#include "DeletionQueue.hpp"
#include "VulkanCheck.hpp"
#include "VulkanTypes.hpp"
#include "VulkanResources.hpp"
#include "VulkanContext.hpp"
#include "Renderer.hpp"
#include "RenderTypes.hpp"
#include "Swapchain.hpp"
#include "Mesh.hpp"
#include "SceneTypes.hpp"
#include "GltfImportTypes.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <map>
#include <tuple>

#include <vk_mem_alloc.h>

class TriangleApplication
{
public:
    void run();
    ~TriangleApplication() noexcept;

private:
    // 按“资产路径 + image 索引 + Vulkan 格式” 唯一标识一张 GPU 纹理
    using GltfImageKey = std::tuple<std::string, std::size_t, VkFormat>;
    using GltfImageCache = std::map<GltfImageKey, std::weak_ptr<ImageResource>>;
    struct GltfMaterialUpload
    {
        GltfImageCache imageCache;
        std::vector<SamplerHandle> samplers;
        std::vector<MaterialHandle> materials;
        std::size_t uploadedImageCount = 0;
    };

    struct GltfImportResult{
        std::size_t objectCount = 0;
        std::size_t uploadedMeshCount = 0;
        std::size_t reusedMeshCount = 0;
        std::size_t materialCount = 0;

        std::size_t uploadedImageCount = 0;
        std::size_t createdSamplerCount = 0;
    };

    void InitWindow();
    void InitVulkan();

    void initImGui();
    void drawImGui();
    void drawTransformGizmo();
    void drawLightOverlays();

    void recreateSwapChain();
    static void framebufferResizeCallback(GLFWwindow *window, int width, int height);
    static void scrollCallback(GLFWwindow *window, double xoffset, double yoffset);

    enum class SceneSelection
    {
        None,
        Model,
        PointLight,
    };

    MeshBuildData buildMeshData(MeshSource source, const std::string &path);
    void addMeshObject(MeshSource source, const std::string &path = std::string());
    GltfImportResult addGltfMeshObjects(const std::string& path);
    Mesh createMesh(const MeshBuildData& meshData);

    MeshHandle getOrCreateMesh(MeshSource source, const std::string &path);
    void releaseMesh(MeshHandle& mesh);
    SceneObject *getSelectedSceneObject();
    const SceneObject *getSelectedSceneObject() const;
    void createUniformBuffer();
    void updateUniformBuffer(uint32_t currentImage, float deltaTime);
    void processCameraInput(float deltaTime);
    void processModelPicking();
    glm::mat4 getObjectMatrix(const SceneObject &object) const;

    // 检查实际已经分配的 descriptor set 数量，以后才能正确处理多次导入
    void ensureMaterialDescriptorCapacity(std::size_t additionalCount) const;
    void createDescriptorPool();
    void createMaterialDescriptorSets();
    void allocateMaterialDescriptorSets(const std::vector<MaterialHandle>& materials); // 传入一组新材质，一次申请对应数量的 set。任意材质写入失败就归还这一整批
    void writeMaterialDescriptorSet(Material& material); // 把五个图片和 sampler 写入已有 set

    void createFrameDescriptorSets();

    DecodedImageData decodeTextureImageFromFileOrFallback(
        const std::string &path,
        const std::array<unsigned char, 4> &fallbackPixel);

    // 获取 image
    ImageHandle getOrCreateGltfImage(
        const GltfImportData& imported,
        std::size_t imageIndex,
        VkFormat format,
        GltfMaterialUpload& upload
    );

    GpuImage uploadTexture2D(
        const DecodedImageData& decoded,
        VkFormat format,
        const std::string& debugName
    );

    ImageHandle createImageResource(
        const std::string& name,
        const std::string& path,
        VkFormat format,
        const std::array<unsigned char, 4>& fallbackPixels
    );

    void createMaterialResources();

    // 获取 sampler
    SamplerHandle getOrCreateGltfSampler(const GltfSamplerData& source, const std::string& debugName, std::vector<SamplerHandle>& library);
    void createTextureSampler();

    void createSkyboxImage();
    void createSkyboxSampler();
    void createSkyboxDescriptorSets();

    void createIrradianceResources();
    void renderIrradianceCubemap();
    void createPrefilterResources();
    void renderPrefilterCubemap();
    void createBRDFLUTResources();
    void renderBRDFLUT();

    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t layerCount = 1);

    bool hasStencilComponent(VkFormat format);

    void drawFrame();
    void MainLoop();
    void cleanup() noexcept;

    void destroyBufferDeferred(GpuBuffer &buffer);

    void testSceneInit();

    bool glfwInitialized = false;

    bool cleanedUp = false;

    GLFWwindow *window = nullptr;

    VulkanContext context;

    Swapchain swapchain;

    Renderer renderer;

    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;

    std::vector<SceneObject> sceneObjects;
    std::unordered_map<std::string, std::weak_ptr<Mesh>> meshCache;
    int selectedSceneObjectIndex = -1;
    char importModelPath[1024]{};

    char importGltfPath[1024]{};
    std::string gltfImportSummary;
    std::string gltfImportError;

    std::string sceneStatusMessage;
    bool selectedModel = false;
    bool sceneClickConsumed = false;
    SceneSelection selectedObject = SceneSelection::None;
    int selectedPointLightIndex = -1;
    bool leftMouseWasDown = false;
    float modelPickDistance = 0.0f;
    int gizmoHoveredAxis = 0;
    int gizmoActiveAxis = 0;
    glm::vec2 gizmoDragStartMouse = {0.0f, 0.0f};
    glm::vec3 gizmoDragStartPosition = {0.0f, 0.0f, 0.0f};
    glm::vec3 gizmoDragAxis = {0.0f, 0.0f, 0.0f};
    glm::vec3 gizmoDragPlaneNormal = {0.0f, 0.0f, 0.0f};
    glm::vec3 gizmoDragStartHitPoint = {0.0f, 0.0f, 0.0f};

    std::vector<GpuBuffer> uniformBuffers;
    std::vector<void *> uniformBufferMapped;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> frameDescriptorSets;

    uint32_t mipLevels = 1;

    ImageHandle defaultBaseColorTexture;
    ImageHandle defaultNormalTexture;
    ImageHandle defaultMetallicRoughnessTexture;
    ImageHandle defaultAoTexture;
    ImageHandle defaultEmissiveTexture;

    std::vector<ImageHandle> textureLibrary;
    std::vector<MaterialHandle> materialLibrary;
    MaterialHandle defaultMaterial;

    MaterialHandle defaultGltfMaterial;
    static constexpr std::uint32_t maxMaterialCount = 128;
    std::uint32_t allocatedMaterialSetCount = 0;

    // 缓存里只放 weak_ptr，这样缓存不会强行延长纹理生命周期。一个 gltf image 对应一个 gpu image
    GltfImageCache gltfImageCache;

    SamplerHandle defaultTextureSampler;
    std::vector<SamplerHandle> samplerLibrary;

    DeletionQueue mainDeletionQueue;

    bool rendererReady = false;
    bool framebufferResized = false;

    // camera-related params
    glm::vec3 cameraPos = {2.0f, 2.0f, 2.0f};
    glm::vec3 cameraTarget = {0.0f, 0.0f, 0.0f};
    glm::vec3 cameraFront = {-0.577350f, -0.577350f, -0.577350f};
    glm::vec3 cameraUp = {0.0f, 0.0f, 1.0f};
    float cameraYaw = -135.0f;
    float cameraPitch = -35.264f;
    float cameraNear = 0.1f;
    float cameraFar = 1000.0f;
    float cameraMoveSpeed = 3.0f;
    float cameraFastMultiplier = 3.0f;
    float cameraScrollSpeed = 1.0f;
    float cameraPanSpeed = 0.01f;
    float mouseSensitivity = 0.12f;
    float cameraScrollOffset = 0.0f;
    float lastMouseX = 0.0f;
    float lastMouseY = 0.0f;
    float lastFrameTime = 0.0f;
    bool firstMouse = true;
    int cameraControlMode = 0;

    bool showDemoWindow = false;
    glm::vec4 clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
    std::vector<PointLight> pointLights;
    DirectionalLight directionalLight;
    bool showShadowProjection = false;
    bool directionalShadowsEnabled = true;
    bool shadowPcfEnabled = false;

    bool showShadowDepth = false;
    // Constant bias: 写入shadow map时，提供与深度格式精度相关的偏移
    // Slope bias: 写入shadow map时，随三角形深度斜率增加偏移
    // Receiver bias: 主画面采样比较时，从当前表面的参考深度中减去偏移
    float shadowConstantBias = 1.25f;
    float shadowSlopeBias = 1.75f;
    float shadowReceiverBias = 0.0f;

    glm::vec3 ambientLightColor = {1.0f, 1.0f, 1.0f};
    float ambientLightIntensity = 0.0f;
    float iblIntensity = 1.0f;


    MaterialHandle createGltfMaterial(
        const GltfImportData& imported,
        const GltfMaterialData& source,
        const std::string& debugName,
        GltfMaterialUpload& upload
    );

    // skybox member
    GpuImage skyboxImage;
    GpuSampler skyboxSampler{};
    std::vector<VkDescriptorSet> skyboxDescriptorSets;

    // diffuse IBL irradiance cubemap
    GpuImage irradianceImage;
    GpuSampler irradianceSampler{};
    VkRenderPass irradianceRenderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout irradianceDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool irradianceDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet irradianceDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout irradiancePipelineLayout = VK_NULL_HANDLE;
    VkPipeline irradiancePipeline = VK_NULL_HANDLE;
    std::array<VkImageView, 6> irradianceFaceImageViews{};
    std::array<VkFramebuffer, 6> irradianceFramebuffers{};

    // prefilter cubemap
    GpuImage prefilterImage;
    GpuSampler prefilterSampler{};
    VkRenderPass prefilterRenderpass = VK_NULL_HANDLE;
    VkPipeline prefilterPipeline = VK_NULL_HANDLE;
    VkPipelineLayout prefilterPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout prefilterDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool prefilterDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet prefilterDescriptorSet = VK_NULL_HANDLE;

    static constexpr uint32_t prefilterMipLevels = 5;
    std::array<std::array<VkImageView, 6>, prefilterMipLevels> prefilterFaceImageViews{};
    std::array<std::array<VkFramebuffer, 6>, prefilterMipLevels> prefilterFramebuffers;

    // Split-sum BRDF Integration LUT
    GpuImage brdfLUTImage;
    GpuSampler brdfLUTSampler{};
    VkRenderPass brdfLUTRenderPass = VK_NULL_HANDLE;
    VkPipeline brdfLUTPipeline = VK_NULL_HANDLE;
    VkPipelineLayout brdfLUTPipelineLayout = VK_NULL_HANDLE;
    VkFramebuffer brdfLUTFramebuffer = VK_NULL_HANDLE;
};
