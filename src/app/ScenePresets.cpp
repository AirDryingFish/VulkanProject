#include "app/Application.hpp"
#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

std::string Application::scenePresetUnavailableReason(ScenePreset preset) const
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

MaterialHandle Application::createPresetMaterial(
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

void Application::buildPresetObjects(ScenePreset preset)
{
    auto add = [&](MeshSource source, const char* name, const glm::vec3& position,
                   const glm::vec3& scale, const MaterialHandle& material) -> SceneObject& {
        addMeshObject(source);
        auto& object = scene.objects.back();
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
    // 记录导入前 scene.objects 里已经有多少个对象
    const std::size_t firstImportedObject = scene.objects.size();

    addGltfMeshObjects(assetPath(info.assetRelativePath));

    const glm::mat4 presentationTransform = glm::scale(glm::mat4(1.0f), glm::vec3(info.presentationScale));
    for (std::size_t i = firstImportedObject; i < scene.objects.size(); ++i)
    {
        SceneObject& object = scene.objects[i];
        object.assetTransform = presentationTransform * object.assetTransform;
    }
}

void Application::releaseSceneMaterialDescriptors(std::size_t first, std::size_t end)
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

void Application::pruneSceneCaches()
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

void Application::loadScenePreset(ScenePreset preset)
{
    if (renderer.hasActiveFrame())
        throw std::logic_error("Scene replacement requires an idle frame boundary");
    const std::string unavailable = scenePresetUnavailableReason(preset);
    if (!unavailable.empty())
        throw std::runtime_error(unavailable);

    renderer.waitForAllFrames();
    const std::size_t previousMaterialCount = materialLibrary.size();
    const std::size_t previousSamplerCount = samplerLibrary.size();
    const int previousSelection = editor.objectIndex;
    const int previousLightSelection = editor.pointLightIndex;
    const auto previousSelectionType = editor.selection;
    std::vector<SceneObject> previousObjects;
    previousObjects.swap(scene.objects);
    glm::vec3 minimum{}, maximum{};
    glm::vec3 center{};
    float radius = 0.0f;
    Camera preparedCamera = camera;
    preparingScenePreset = true;
    try
    {
        buildPresetObjects(preset);
        scene.bounds(minimum, maximum);
        if (!std::isfinite(glm::length(maximum - minimum)))
            throw std::runtime_error("Scene bounds exceed the supported numeric range");
        if (materialLibrary.size() - previousMaterialCount > sceneSwitchMaterialReserve)
            throw std::runtime_error("Scene preset exceeds its reserved material budget");

        // Fit and validate without touching the live camera. A failed fit must
        // roll back while the old scene and its descriptors are still alive.
        center = minimum + (maximum - minimum) * 0.5f;
        radius = std::max(glm::length(maximum - minimum) * 0.5f, 0.1f);
        const float aspect = swapchain.extent().width / static_cast<float>(swapchain.extent().height);
        const float halfFov = std::atan(std::tan(glm::radians(preparedCamera.fieldOfViewDegrees * 0.5f)) * std::min(aspect, 1.0f));
        const float distance = radius * 1.15f / std::sin(halfFov);
        preparedCamera.lookAt(center + glm::normalize(glm::vec3(0.15f, -1.0f, 0.25f)) * distance, center);
        preparedCamera.nearPlane = std::max(radius * 0.001f, 0.01f);
        preparedCamera.farPlane = std::max(distance + radius * 4.0f, 100.0f);
        preparedCamera.moveSpeed = std::max(radius * 0.4f, 1.0f);
        preparedCamera.scrollSpeed = std::max(radius * 0.1f, 0.1f);
        preparedCamera.panSpeed = std::max(radius * 0.002f, 0.001f);

        if (preset == ScenePreset::MaterialLab)
            preparedCamera.lookAt({0.0f, -13.0f, 5.0f}, {0.0f, 0.0f, 1.3f});
        else if (preset == ScenePreset::ShadowPlayground)
            preparedCamera.lookAt({14.0f, -22.0f, 14.0f}, {0.0f, 15.0f, 0.0f});
        else if (preset == ScenePreset::Original)
            preparedCamera.lookAt({6.0f, -8.0f, 5.0f}, {0.0f, 0.0f, 0.0f});

        const glm::mat4 preparedView = preparedCamera.view();
        const glm::mat4 preparedProjection = preparedCamera.projection(aspect);
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                if (!std::isfinite(preparedView[column][row]) || !std::isfinite(preparedProjection[column][row]))
                    throw std::runtime_error("Scene camera exceeds the supported numeric range");

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
        scene.objects.clear();
        materialLibrary.resize(previousMaterialCount);
        samplerLibrary.resize(previousSamplerCount);
        scene.objects.swap(previousObjects);
        editor.objectIndex = previousSelection;
        editor.pointLightIndex = previousLightSelection;
        editor.selection = previousSelectionType;
        pruneSceneCaches();
        throw;
    }
    preparingScenePreset = false;
    previousObjects.clear();
    pruneSceneCaches();
    camera = preparedCamera;
    cameraController.reset(window);

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

    scene.pointLights.clear();
    scene.directionalLight = DirectionalLight{};
    scene.directionalLight.direction = {-0.6f, 0.7f, -1.0f};
    scene.directionalLight.intensity = 3.0f;
    scene.ambientLightColor = glm::vec3(1.0f);
    scene.ambientLightIntensity = 0.0f;
    scene.iblIntensity = preset == ScenePreset::ShadowPlayground ? 0.0f : 1.0f;
    shadows.enabled = true;
    shadows.pcfEnabled = true;
    display.debugView = DebugView::Lit;
    shadows.showDepth = false;
    shadows.constantBias = 1.25f;
    shadows.slopeBias = 1.75f;
    shadows.receiverBias = 0.0005f;
    shadows.center = center;
    shadows.halfExtent = std::max(radius * 1.05f, 1.0f);

    editor.clearSelection();
    editor.leftMouseWasDown = false;
    editor.sceneClickConsumed = true;
    editor.gltfImportSummary.clear();
    editor.gltfImportError.clear();
    editor.sceneStatusMessage.clear();
    editor.scenePresetError.clear();
    activeScenePreset = editor.selectedScenePreset = preset;
    lastFrameTime = static_cast<float>(glfwGetTime());
    std::cout << "Scene preset: " << scenePresetInfo(preset).id
              << " | objects: " << scene.objects.size()
              << " | material sets: " << allocatedMaterialSetCount << '\n';
}

void Application::processPendingScenePreset()
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
        editor.scenePresetError = error.what();
        std::cerr << "Scene switch failed (previous scene retained): " << error.what() << '\n';
    }
}
