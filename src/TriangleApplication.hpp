#pragma once

#include "AppConfig.hpp"
#include "DeletionQueue.hpp"
#include "VulkanCheck.hpp"
#include "VulkanTypes.hpp"
#include "VulkanResources.hpp"
#include "VulkanContext.hpp"
#include "Renderer.hpp"
#include "Swapchain.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

#include <vk_mem_alloc.h>

struct UploadContext
{
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
};


static constexpr uint32_t pbrImageDescriptorCount = 9;

class TriangleApplication
{
public:
    void run();
    ~TriangleApplication() noexcept;

private:
    void InitWindow();
    void InitVulkan();

    void initImGui();
    void drawImGui();
    void drawTransformGizmo();
    void drawLightOverlays();

    void recreateSwapChain();
    static void framebufferResizeCallback(GLFWwindow *window, int width, int height);
    static void windowRefreshCallback(GLFWwindow *window);
    static void scrollCallback(GLFWwindow *window, double xoffset, double yoffset);

    void createRenderPass();
    static std::vector<char> readFile(const std::string &filename);
    void createDescriptorSetLayout();
    void createGraphicsPipeline();
    enum class MeshSource
    {
        Obj,
        Cube,
        Sphere,
    };

    enum class SceneSelection
    {
        None,
        Model,
        PointLight,
    };

    struct MeshBuildData
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        glm::vec3 boundsMin = {0.0f, 0.0f, 0.0f};
        glm::vec3 boundsMax = {0.0f, 0.0f, 0.0f};
        bool boundsValid = false;
    };

    struct SceneObject
    {
        std::string name;
        MeshSource source = MeshSource::Obj;
        std::string sourcePath;
        AllocatedBuffer vertexBuffer;
        AllocatedBuffer indexBuffer;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        glm::vec3 localBoundsMin = {0.0f, 0.0f, 0.0f};
        glm::vec3 localBoundsMax = {0.0f, 0.0f, 0.0f};
        bool boundsValid = false;
        glm::vec3 position = {0.0f, 0.0f, 0.0f};
        glm::vec3 rotation = {0.0f, 0.0f, 0.0f};
        glm::vec3 scale = {1.0f, 1.0f, 1.0f};
        bool autoRotate = false;
        float autoRotation = 0.0f;
        float autoRotateSpeed = 90.0f;
    };

    struct GraphicsPipelineConfig
    {
        std::string vertShaderPath;
        std::string fragShaderPath;
        bool useVertexInput = true;
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        bool depthTest = true;
        bool depthWrite = true;
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
    };
    VkPipeline createGraphicsPipelineFromConfig(const GraphicsPipelineConfig &config);

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, uint32_t frameIndex);

    void immediateSubmit(std::function<void(VkCommandBuffer cmd)> &&function);

    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    void loadModel();
    void rebuildMesh(MeshSource source);
    MeshBuildData buildMeshData(MeshSource source, const std::string &path);
    void addMeshObject(MeshSource source, const std::string &path = std::string());
    void createObjectBuffers(SceneObject &object, const MeshBuildData &meshData);
    SceneObject *getSelectedSceneObject();
    const SceneObject *getSelectedSceneObject() const;
    void computeModelBounds();
    void createVertexBuffer();
    void createIndexBuffer();
    void createUniformBuffer();
    void updateUniformBuffer(uint32_t currentImage, float deltaTime);
    void processCameraInput(float deltaTime);
    void processModelPicking();
    glm::mat4 getModelMatrix() const;
    glm::mat4 getObjectMatrix(const SceneObject &object) const;

    void createDescriptorPool();
    void createDescriptorSets();
    void createTextureDescriptorSets(
        const std::array<VkDescriptorImageInfo, pbrImageDescriptorCount> &imageInfos,
        std::vector<VkDescriptorSet> &targetDescriptorSets);

    AllocatedImage createTextureImageFromFile(
        const std::string &path,
        VkFormat format,
        const std::array<unsigned char, 4> &fallbackPixel);
    void createTextureImage();
    void createTextureImageView();
    void createTextureSampler();

    void createSkyboxImage();
    void createSkyboxSampler();
    void createSkyboxPipeline();
    void createSkyboxDescriptorSets();

    void createIrradianceResources();
    void renderIrradianceCubemap();
    void createPrefilterResources();
    void renderPrefilterCubemap();
    void createBRDFLUTResources();
    void renderBRDFLUT();

    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t layerCount = 1);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    void generateMipmaps(VkImage image, VkFormat imageFormat, uint32_t texWidth, uint32_t texHeight, uint32_t mipLevels, uint32_t layerCount = 1);

    bool hasStencilComponent(VkFormat format);

    void drawFrame();
    void MainLoop();
    void cleanup() noexcept;

    void createUploadContext();

    void destroyBufferDeferred(AllocatedBuffer& buffer);


    bool glfwInitialized = false;

    bool cleanedUp = false;

    GLFWwindow *window = nullptr;

    VulkanContext context;

    Swapchain swapchain;

    Renderer renderer;

    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SceneObject> sceneObjects;
    int selectedSceneObjectIndex = -1;
    char importModelPath[1024]{};
    std::string sceneStatusMessage;
    glm::vec3 modelLocalBoundsMin = {0.0f, 0.0f, 0.0f};
    glm::vec3 modelLocalBoundsMax = {0.0f, 0.0f, 0.0f};
    bool modelBoundsValid = false;
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
    AllocatedBuffer vertexBuffer;
    AllocatedBuffer indexBuffer;

    std::vector<AllocatedBuffer> uniformBuffers;
    std::vector<void *> uniformBufferMapped;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    uint32_t mipLevels;

    AllocatedImage textureImage;
    AllocatedImage normalImage;
    AllocatedImage metallicImage;
    AllocatedImage roughnessImage;
    AllocatedImage aoImage;
    VkSampler textureSampler = VK_NULL_HANDLE;

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

    bool rotateModel = false;
    bool showDemoWindow = false;
    glm::vec3 modelPosition = {0.0f, 0.0f, 0.0f};
    glm::vec3 modelRotation = {0.0f, 0.0f, 0.0f};
    glm::vec3 modelScale = {1.0f, 1.0f, 1.0f};
    float modelAutoRotation = 0.0f;
    float modelAutoRotateSpeed = 90.0f;
    glm::vec4 clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
    std::vector<PointLight> pointLights;
    glm::vec3 ambientLightColor = {1.0f, 1.0f, 1.0f};
    float ambientLightIntensity = 0.0f;
    float iblIntensity = 1.0f;
    MeshSource meshSource = MeshSource::Sphere;

    // skybox member
    AllocatedImage skyboxImage;
    VkSampler skyboxSampler = VK_NULL_HANDLE;
    VkPipeline skyboxPipeline = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> skyboxDescriptorSets;

    // diffuse IBL irradiance cubemap
    AllocatedImage irradianceImage;
    VkSampler irradianceSampler = VK_NULL_HANDLE;
    VkRenderPass irradianceRenderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout irradianceDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool irradianceDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet irradianceDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout irradiancePipelineLayout = VK_NULL_HANDLE;
    VkPipeline irradiancePipeline = VK_NULL_HANDLE;
    std::array<VkImageView, 6> irradianceFaceImageViews{};
    std::array<VkFramebuffer, 6> irradianceFramebuffers{};

    // prefilter cubemap
    AllocatedImage prefilterImage;
    VkSampler prefilterSampler = VK_NULL_HANDLE;
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
    AllocatedImage brdfLUTImage;
    VkSampler brdfLUTSampler = VK_NULL_HANDLE;
    VkRenderPass brdfLUTRenderPass = VK_NULL_HANDLE;
    VkPipeline brdfLUTPipeline = VK_NULL_HANDLE;
    VkPipelineLayout brdfLUTPipelineLayout = VK_NULL_HANDLE;
    VkFramebuffer brdfLUTFramebuffer = VK_NULL_HANDLE;

    // pbr params
    glm::vec3 materialAlbedo = {1.0f, 1.0f, 1.0f};
    float materialMetallic = 1.0f;
    float materialRoughness = 1.0f;
    float materialAo = 1.0f;

    // upload context for immediate submit
    UploadContext uploadContext;
};
