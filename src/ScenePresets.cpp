#include "TriangleApplication.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

std::string TriangleApplication::scenePresetUnavailableReason(ScenePreset preset) const
{
    const auto& info = scenePresetInfo(preset);
    if (info.unsupportedReason[0] != '\0')
        return std::string(info.label) + ": " + info.unsupportedReason;
    if (info.assetRelativePath[0] != '\0')
    {
        const std::string path = assetPath(info.assetRelativePath);
        if (!std::filesystem::is_regular_file(std::filesystem::u8path(path)))
            return "Missing scene asset: " + path + ". See docs/scene-presets.md.";
    }
    return {};
}

MaterialHandle TriangleApplication::createPresetMaterial(
    const std::string& name, const glm::vec4& color, float metallic, float roughness)
{
    auto material = std::make_shared<Material>();
    material->name = name;
    material->baseColorTexture = {defaultBaseColorTexture, defaultTextureSampler, 0};
    material->normalTexture = {defaultNormalTexture, defaultTextureSampler, 0};
    material->metallicRoughnessTexture = {defaultMetallicRoughnessTexture, defaultTextureSampler, 0};
    material->aoTexture = {defaultAoTexture, defaultTextureSampler, 0};
    material->emissiveTexture = {defaultEmissiveTexture, defaultTextureSampler, 0};
    material->baseColorFactor = color;
    material->metallicFactor = metallic;
    material->roughnessFactor = roughness;
    material->normalScale = 0.0f;

    // Reserve before allocating the raw descriptor handle, so publication cannot fail.
    materialLibrary.reserve(materialLibrary.size() + 1);
    allocateMaterialDescriptorSets({material});
    materialLibrary.push_back(material);
    return material;
}

void TriangleApplication::buildPresetObjects(ScenePreset preset)
{
    auto add = [&](MeshSource source, const char* name, const glm::vec3& position,
                   const glm::vec3& scale, const MaterialHandle& material) -> SceneObject& {
        addMeshObject(source);
        auto& object = sceneObjects.back();
        object.name = name;
        object.transform.position = position;
        object.transform.scale = scale;
        object.material = material;
        return object;
    };

    if (preset == ScenePreset::MaterialLab)
    {
        const auto floor = createPresetMaterial("Lab floor", {0.35f, 0.35f, 0.35f, 1.0f}, 0.0f, 0.8f);
        add(MeshSource::Cube, "Floor", {0.0f, 0.0f, -0.15f}, {5.5f, 3.0f, 0.15f}, floor);
        const std::array<float, 5> roughness{{0.05f, 0.25f, 0.5f, 0.75f, 1.0f}};
        for (int row = 0; row < 2; ++row)
        {
            for (std::size_t column = 0; column < roughness.size(); ++column)
            {
                const std::string name = std::string(row == 0 ? "Dielectric" : "Metal") +
                    " / roughness " + std::to_string(roughness[column]);
                const auto material = createPresetMaterial(name, {0.8f, 0.55f, 0.25f, 1.0f},
                                                          static_cast<float>(row), roughness[column]);
                add(MeshSource::Sphere, name.c_str(),
                    {-3.2f + 1.6f * static_cast<float>(column), 0.0f, 0.7f + 1.6f * row},
                    glm::vec3(0.6f), material);
            }
        }
        return;
    }

    if (preset == ScenePreset::ShadowPlayground)
    {
        const auto floor = createPresetMaterial("Shadow ground", {0.6f, 0.6f, 0.6f, 1.0f}, 0.0f, 0.9f);
        const auto stone = createPresetMaterial("Occluder stone", {0.75f, 0.58f, 0.35f, 1.0f}, 0.0f, 0.7f);
        const auto blue = createPresetMaterial("Blue wall", {0.2f, 0.4f, 0.65f, 1.0f}, 0.0f, 0.65f);
        add(MeshSource::Cube, "Ground", {0.0f, 20.0f, -0.2f}, {18.0f, 32.0f, 0.2f}, floor);
        add(MeshSource::Cube, "Near box", {-2.0f, 0.0f, 0.75f}, glm::vec3(0.75f), stone);
        add(MeshSource::Cube, "Middle box", {2.0f, 14.0f, 1.5f}, glm::vec3(1.5f), stone);
        add(MeshSource::Cube, "Far box", {-1.0f, 35.0f, 3.0f}, glm::vec3(3.0f), stone);
        for (int i = 0; i < 5; ++i)
        {
            const std::string name = "Depth marker " + std::to_string(i + 1);
            add(MeshSource::Cube, name.c_str(), {-6.0f, 2.0f + 10.0f * i, 2.0f},
                {0.35f, 0.35f, 2.0f}, stone);
        }
        add(MeshSource::Cube, "Doorway left", {4.0f, 8.0f, 2.0f}, {0.4f, 0.5f, 2.0f}, blue);
        add(MeshSource::Cube, "Doorway right", {8.0f, 8.0f, 2.0f}, {0.4f, 0.5f, 2.0f}, blue);
        add(MeshSource::Cube, "Doorway lintel", {6.0f, 8.0f, 4.0f}, {2.4f, 0.5f, 0.4f}, blue);
        auto& ramp = add(MeshSource::Cube, "Sloped receiver", {5.0f, -2.0f, 1.0f},
                         {2.0f, 2.5f, 0.15f}, floor);
        ramp.transform.rotation.x = 20.0f;
        add(MeshSource::Sphere, "Sphere", {5.0f, -1.0f, 2.5f}, glm::vec3(0.8f), stone);
        return;
    }

    if (preset == ScenePreset::Original)
    {
        add(MeshSource::Sphere, "Rusted Iron Sphere", {-1.2f, 0.0f, 0.0f}, glm::vec3(1.0f), defaultMaterial);
        add(MeshSource::Sphere, "Variant Sphere", {1.2f, 0.0f, 0.0f}, glm::vec3(1.0f), materialLibrary.at(1));
    }

    const ScenePresetInfo& info = scenePresetInfo(preset);
    // 记录导入前 sceneObjects 里已经有多少个对象
    const std::size_t firstImportedObject = sceneObjects.size();

    addGltfMeshObjects(assetPath(info.assetRelativePath));

    const glm::mat4 presentationTransform = glm::scale(glm::mat4(1.0f), glm::vec3(info.presentationScale));
    for (std::size_t i = firstImportedObject; i < sceneObjects.size(); ++i)
    {
        SceneObject& object = sceneObjects[i];
        object.assetTransform = presentationTransform * object.assetTransform;
    }
}

void TriangleApplication::getSceneBounds(glm::vec3& minimum, glm::vec3& maximum) const
{
    minimum = glm::vec3(std::numeric_limits<float>::max());
    maximum = glm::vec3(std::numeric_limits<float>::lowest());
    bool found = false;
    for (const auto& object : sceneObjects)
    {
        if (!object.mesh || !object.mesh->boundsValid)
            continue;
        const glm::mat4 model = getObjectMatrix(object);
        for (int corner = 0; corner < 8; ++corner)
        {
            const auto& mesh = *object.mesh;
            const glm::vec3 local{
                (corner & 1) ? mesh.boundsMax.x : mesh.boundsMin.x,
                (corner & 2) ? mesh.boundsMax.y : mesh.boundsMin.y,
                (corner & 4) ? mesh.boundsMax.z : mesh.boundsMin.z};
            const glm::vec3 world = glm::vec3(model * glm::vec4(local, 1.0f));
            if (!std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z))
                throw std::runtime_error("Scene contains non-finite world bounds");
            minimum = glm::min(minimum, world);
            maximum = glm::max(maximum, world);
            found = true;
        }
    }
    if (!found)
        throw std::runtime_error("Scene contains no bounded geometry");
}

void TriangleApplication::setSceneCamera(const glm::vec3& eye, const glm::vec3& target)
{
    cameraPos = eye;
    cameraTarget = target;
    cameraUp = {0.0f, 0.0f, 1.0f};
    cameraFront = glm::normalize(target - eye);
    cameraYaw = glm::degrees(std::atan2(cameraFront.y, cameraFront.x));
    cameraPitch = glm::degrees(std::asin(std::clamp(cameraFront.z, -1.0f, 1.0f)));
    cameraScrollOffset = 0.0f;
    cameraControlMode = 0;
    firstMouse = true;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void TriangleApplication::releaseSceneMaterialDescriptors(std::size_t first, std::size_t end)
{
    std::vector<VkDescriptorSet> sets;
    sets.reserve(end - first);
    for (std::size_t i = first; i < end; ++i)
        if (materialLibrary[i]->descriptorSet != VK_NULL_HANDLE)
            sets.push_back(materialLibrary[i]->descriptorSet);
    if (sets.empty())
        return;
    VK_CHECK(vkFreeDescriptorSets(context.device(), descriptorPool,
                                 static_cast<std::uint32_t>(sets.size()), sets.data()));
    allocatedMaterialSetCount -= static_cast<std::uint32_t>(sets.size());
    for (std::size_t i = first; i < end; ++i)
        materialLibrary[i]->descriptorSet = VK_NULL_HANDLE;
}

void TriangleApplication::pruneSceneCaches()
{
    for (auto it = meshCache.begin(); it != meshCache.end();)
        it = it->second.expired() ? meshCache.erase(it) : std::next(it);
    for (auto it = gltfImageCache.begin(); it != gltfImageCache.end();)
        it = it->second.expired() ? gltfImageCache.erase(it) : std::next(it);
    samplerLibrary.erase(std::remove_if(samplerLibrary.begin(), samplerLibrary.end(),
        [&](const SamplerHandle& sampler) {
            return sampler != defaultTextureSampler && sampler.use_count() == 1;
        }), samplerLibrary.end());
}

void TriangleApplication::loadScenePreset(ScenePreset preset)
{
    if (renderer.hasActiveFrame())
        throw std::logic_error("Scene replacement requires an idle frame boundary");
    const std::string unavailable = scenePresetUnavailableReason(preset);
    if (!unavailable.empty())
        throw std::runtime_error(unavailable);

    renderer.waitForAllFrames();
    const std::size_t previousMaterialCount = materialLibrary.size();
    const std::size_t previousSamplerCount = samplerLibrary.size();
    const int previousSelection = selectedSceneObjectIndex;
    const int previousLightSelection = selectedPointLightIndex;
    const auto previousSelectionType = selectedObject;
    const bool previousSelectedModel = selectedModel;
    std::vector<SceneObject> previousObjects;
    previousObjects.swap(sceneObjects);
    glm::vec3 minimum{}, maximum{};
    preparingScenePreset = true;
    try
    {
        buildPresetObjects(preset);
        getSceneBounds(minimum, maximum);
        if (!std::isfinite(glm::length(maximum - minimum)))
            throw std::runtime_error("Scene bounds exceed the supported numeric range");
        if (materialLibrary.size() - previousMaterialCount > sceneSwitchMaterialReserve)
            throw std::runtime_error("Scene preset exceeds its reserved material budget");

        // Keep startup materials, replace all scene-specific/imported materials.
        // Both scenes remain alive until construction succeeds (including descriptors).
        std::vector<MaterialHandle> nextMaterials;
        nextMaterials.reserve(builtinMaterialCount + materialLibrary.size() - previousMaterialCount);
        nextMaterials.insert(nextMaterials.end(), materialLibrary.begin(), materialLibrary.begin() + builtinMaterialCount);
        nextMaterials.insert(nextMaterials.end(), materialLibrary.begin() + previousMaterialCount, materialLibrary.end());
        releaseSceneMaterialDescriptors(builtinMaterialCount, previousMaterialCount);
        materialLibrary.swap(nextMaterials);
    }
    catch (...)
    {
        preparingScenePreset = false;
        releaseSceneMaterialDescriptors(previousMaterialCount, materialLibrary.size());
        sceneObjects.clear();
        materialLibrary.resize(previousMaterialCount);
        samplerLibrary.resize(previousSamplerCount);
        sceneObjects.swap(previousObjects);
        selectedSceneObjectIndex = previousSelection;
        selectedPointLightIndex = previousLightSelection;
        selectedObject = previousSelectionType;
        selectedModel = previousSelectedModel;
        pruneSceneCaches();
        throw;
    }
    preparingScenePreset = false;
    previousObjects.clear();
    pruneSceneCaches();

    // The editor exposes these shared startup materials too. Reset their factors
    // without reallocating or modifying descriptor sets.
    for (std::size_t i = 0; i < builtinMaterialCount; ++i)
    {
        auto& material = *materialLibrary[i];
        const auto& initial = builtinMaterialDefaults[i];
        material.baseColorFactor = initial.baseColorFactor;
        material.metallicFactor = initial.metallicFactor;
        material.roughnessFactor = initial.roughnessFactor;
        material.normalScale = initial.normalScale;
        material.occlusionStrength = initial.occlusionStrength;
        material.emissiveFactor = initial.emissiveFactor;
    }

    const glm::vec3 center = (minimum + maximum) * 0.5f;
    const float radius = std::max(glm::length(maximum - minimum) * 0.5f, 0.1f);
    const float aspect = swapchain.extent().width / static_cast<float>(swapchain.extent().height);
    const float halfFov = std::atan(std::tan(glm::radians(22.5f)) * std::min(aspect, 1.0f));
    const float distance = radius * 1.15f / std::sin(halfFov);
    setSceneCamera(center + glm::normalize(glm::vec3(0.15f, -1.0f, 0.25f)) * distance, center);
    cameraNear = std::max(radius * 0.001f, 0.01f);
    cameraFar = std::max(distance + radius * 4.0f, 100.0f);
    cameraMoveSpeed = std::max(radius * 0.4f, 1.0f);
    cameraScrollSpeed = std::max(radius * 0.1f, 0.1f);
    cameraPanSpeed = std::max(radius * 0.002f, 0.001f);

    if (preset == ScenePreset::MaterialLab)
        setSceneCamera({0.0f, -13.0f, 5.0f}, {0.0f, 0.0f, 1.3f});
    else if (preset == ScenePreset::ShadowPlayground)
        setSceneCamera({14.0f, -22.0f, 14.0f}, {0.0f, 15.0f, 0.0f});
    else if (preset == ScenePreset::Original)
        setSceneCamera({6.0f, -8.0f, 5.0f}, {0.0f, 0.0f, 0.0f});

    pointLights.clear();
    directionalLight = DirectionalLight{};
    directionalLight.direction = {-0.6f, 0.7f, -1.0f};
    directionalLight.intensity = 3.0f;
    ambientLightColor = glm::vec3(1.0f);
    ambientLightIntensity = 0.0f;
    iblIntensity = preset == ScenePreset::ShadowPlayground ? 0.0f : 1.0f;
    directionalShadowsEnabled = true;
    shadowPcfEnabled = true;
    showShadowProjection = showShadowDepth = false;
    shadowConstantBias = 1.25f;
    shadowSlopeBias = 1.75f;
    shadowReceiverBias = 0.0005f;
    shadowCenter = center;
    shadowHalfExtent = std::max(radius * 1.05f, 1.0f);

    selectedObject = SceneSelection::None;
    selectedSceneObjectIndex = selectedPointLightIndex = -1;
    selectedModel = false;
    modelPickDistance = 0.0f;
    gizmoHoveredAxis = gizmoActiveAxis = 0;
    leftMouseWasDown = false;
    sceneClickConsumed = true;
    gltfImportSummary.clear();
    gltfImportError.clear();
    sceneStatusMessage.clear();
    scenePresetError.clear();
    activeScenePreset = selectedScenePreset = preset;
    lastFrameTime = static_cast<float>(glfwGetTime());
    std::cout << "Scene preset: " << scenePresetInfo(preset).id
              << " | objects: " << sceneObjects.size()
              << " | material sets: " << allocatedMaterialSetCount << '\n';
}

void TriangleApplication::processPendingScenePreset()
{
    if (!pendingScenePreset)
        return;
    const ScenePreset requested = *pendingScenePreset;
    pendingScenePreset.reset();
    try
    {
        loadScenePreset(requested);
    }
    catch (const std::exception& error)
    {
        scenePresetError = error.what();
        std::cerr << "Scene switch failed (previous scene retained): " << error.what() << '\n';
    }
}
