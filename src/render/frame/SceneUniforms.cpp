#include "render/frame/SceneUniforms.hpp"
#include "render/deferred/DeferredMath.hpp"
#include "render/RenderTypes.hpp"
#include "render/shadow/ShadowCascades.hpp"
#include "scene/Camera.hpp"
#include "scene/Scene.hpp"

#include <glm/matrix.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
// 在调用方已初始化的快照中直接打包，避免额外复制整份最大容量的灯数组。
void packPointLights(const std::vector<PointLight>& lights, PackedPointLights& result)
{
    auto& packed = result.lights;
    std::size_t count = 0;

    const auto finiteVector = [](const glm::vec3& value)
    { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); };

    for (std::size_t index = 0; index < lights.size(); ++index)
    {
        const PointLight& light = lights[index];
        if (!light.enabled)
        {
            continue;
        }

        if (!finiteVector(light.position) || !finiteVector(light.color) || light.color.x < 0.0f ||
            light.color.y < 0.0f || light.color.z < 0.0f || !std::isfinite(light.intensity) || light.intensity < 0.0f ||
            !std::isfinite(light.range))
        {
            throw std::runtime_error("Invalid point light at scene index");
        }

        if (count == packed.size())
        {
            throw std::runtime_error("Enabled point lights exceed GPU capacity");
        }

        GpuPointLight& gpu = packed[count++];
        gpu.position = glm::vec4(light.position, 1.0f);
        gpu.color = glm::vec4(light.color, light.intensity);
        gpu.params = glm::vec4(std::max(light.range, 0.0001f), 1.0f, 0.0f, 0.0f);
    }

    result.count = static_cast<std::uint32_t>(count);
}
} // namespace

FrameSceneUniforms makeSceneUniforms(const Scene& scene, const Camera& camera, const DisplaySettings& display,
                                     ShadowSettings& shadows, VkExtent2D extent, std::uint32_t shadowPreviewLayer,
                                     float shadowBlendFraction)
{
    FrameSceneUniforms result{};
    UniformBufferObject& ubo = result.frame;
    DeferredFrameUniforms& deferredUniforms = result.deferred;
    ubo.view = camera.view();
    const float aspectRatio = extent.width / static_cast<float>(extent.height);
    ubo.proj = camera.projection(aspectRatio);

    const deferred::TileGrid grid = deferred::makeTileGrid(extent.width, extent.height);

    deferredUniforms = {};

    deferredUniforms.inverseViewProjection = glm::inverse(ubo.proj * ubo.view);

    deferredUniforms.inverseProjection = glm::inverse(ubo.proj);

    deferredUniforms.extentAndTiles = {extent.width, extent.height, grid.tilesX, grid.tilesY};

    const float minimumShadowDistance = std::min(camera.nearPlane + 0.01f, camera.farPlane);
    if (!std::isfinite(shadows.shadowDistance))
    {
        shadows.shadowDistance = 50.0f;
    }

    shadows.shadowDistance = std::max(shadows.shadowDistance, minimumShadowDistance);
    shadows.splitLambda = std::clamp(std::isfinite(shadows.splitLambda) ? shadows.splitLambda : 0.5f, 0.0f, 1.0f);
    shadows.halfExtent = std::clamp(std::isfinite(shadows.halfExtent) ? shadows.halfExtent : 5.0f, 0.1f, 10000.0f);
    if (!std::isfinite(shadows.center.x) || !std::isfinite(shadows.center.y) || !std::isfinite(shadows.center.z))
    {
        shadows.center = glm::vec3(0.0f);
    }

    ubo.cameraPosition = glm::vec4(camera.position, 1.0f);
    ubo.ambientLight = glm::vec4(scene.ambientLightColor, scene.ambientLightIntensity);
    packPointLights(scene.pointLights, result.pointLights);
    const std::uint32_t pointLightCount = result.pointLights.count;
    ubo.lightCounts = glm::ivec4(pointLightCount, 0, 0, 0);
    ubo.renderParams = glm::vec4(scene.iblIntensity, camera.nearPlane,
                                 std::max(display.debugDepthFar, camera.nearPlane + 0.001f), 0.0f);

    // UI 可能输入零向量或无效值；先兜底再归一化，防止 NaN 传播到整个阴影矩阵。
    glm::vec3 direction = scene.directionalLight.direction;
    float lengthSquared = glm::dot(direction, direction);
    if (!std::isfinite(lengthSquared) || lengthSquared < 1e-8f)
    {
        direction = glm::vec3(-1.0f, -1.0f, -2.0f);
        lengthSquared = glm::dot(direction, direction);
    }
    direction /= std::sqrt(lengthSquared);

    ubo.directionalDirectionEnabled = glm::vec4(direction, scene.directionalLight.enabled ? 1.0f : 0.0f);
    ubo.directionalColorIntensity =
        glm::vec4(scene.directionalLight.color, std::max(scene.directionalLight.intensity, 0.0f));

    // 把方向光视为一台正交相机：从光的视角写深度，再供主场景做遮挡比较。
    const float halfExtent = shadows.halfExtent;
    const float lighDistance = 2.0f * halfExtent + 1.0f;
    constexpr float shadowNear = 0.1f;
    const float shadowFar = 4.0f * halfExtent + 2.0f;
    // 虚拟光源相机位置，为了从光的视角去渲染场景
    const glm::vec3 lightPosition = shadows.center - direction * lighDistance;
    const glm::vec3 worldUp{0.0f, 0.0f, 1.0f};
    const glm::vec3 lightUp = std::abs(glm::dot(direction, worldUp)) > 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : worldUp;
    // 里面会进行 cross 操作，如果看向方向和 up 方向平行，cross 为 0，算不出 right 方向
    const glm::mat4 lightView = glm::lookAtRH(lightPosition, shadows.center, lightUp);
    // 构建一个右手系、深度范围为 [0, 1] 的正交投影矩阵
    glm::mat4 lightProjection =
        glm::orthoRH_ZO(-halfExtent, halfExtent, -halfExtent, halfExtent, shadowNear, shadowFar);
    // 阴影投影在这里独立构造，也要适配 Vulkan 的 Y 方向。
    // 主相机的投影已在 Camera::projection() 内翻转，调用处不能重复翻转。
    lightProjection[1][1] *= -1.0f;
    // 世界坐标系变换到 光源裁剪空间中
    ubo.lightViewProjection = lightProjection * lightView;
    ubo.shadowParams = glm::vec4(1.0f / static_cast<float>(directionalShadowResolution),
                                 1.0f / static_cast<float>(directionalShadowResolution), shadows.receiverBias, 0.0f);

    // shadowFlags:
    // x: shadows enabled
    // y: shadow projection view
    // z: PCF enabled
    // w: DebugView
    ubo.shadowFlags = glm::ivec4((shadows.enabled && scene.directionalLight.enabled) ? 1 : 0,
                                 display.debugView == DebugView::ShadowProjection ? 1 : 0, shadows.pcfEnabled ? 1 : 0,
                                 static_cast<std::uint32_t>(display.debugView));

    ubo.cascadeLightViewProjection.fill(ubo.lightViewProjection);

    const float effectiveShadowDistance = std::min(camera.farPlane, shadows.shadowDistance);
    ubo.cascadeSplits = glm::vec4(effectiveShadowDistance);
    ubo.cascadeBlendStarts = ubo.cascadeSplits;

    const float shadowTexelWorldSize = 2.0 * halfExtent / static_cast<float>(directionalShadowResolution);

    ubo.cascadeTexelWorldSize = glm::vec4(shadowTexelWorldSize);
    ubo.cascadeReceiverBias = glm::vec4(shadows.receiverBias);
    // x: 有效级数
    // y: ShadowMode
    // z: 预览层
    // w: reserved
    ubo.cascadeInfo = glm::uvec4(1u, //表示当前只有一级有效
                                 static_cast<uint32_t>(ShadowMode::Single), 0u, 0u);

    if (shadows.mode == ShadowMode::Cascaded)
    {
        ShadowCascadeInput input{};

        input.inverseView = glm::inverse(ubo.view);
        input.fovDegrees = camera.fieldOfViewDegrees;
        input.aspectRatio = aspectRatio;
        input.cameraNear = camera.nearPlane;
        input.cameraFar = camera.farPlane;

        input.shadowDistance = shadows.shadowDistance;
        input.splitLambda = shadows.splitLambda;
        input.blendFraction = shadowBlendFraction;

        input.lightDirection = direction;

        // 根据场景中所有物体的当前变换，计算一个能包住它们的世界空间 AABB，通过 minimum、maximum 返回
        scene.bounds(input.sceneMinimum, input.sceneMaximum);

        input.resolution = directionalShadowResolution;
        input.depthMargin = shadows.depthMargin;
        input.lightNear = shadows.lightNear;
        input.receiverBiasWorld = shadows.receiverBiasWorld;

        const ShadowCascadeFrame cascadeFrame = calculateShadowCascadeFrame(input);

        for (std::size_t i = 0; i < shadowCascadeCount; ++i)
        {
            const ShadowCascadeData& cascade = cascadeFrame.cascades[i];
            // 使用 glm 的索引类型，方便访问 glm::vec4
            const auto component = static_cast<glm::length_t>(i);
            ubo.cascadeLightViewProjection[i] = cascade.matrices.viewProjection;
            ubo.cascadeSplits[component] = cascade.farDistance;
            ubo.cascadeBlendStarts[component] = cascade.blendStartDistance;
            ubo.cascadeTexelWorldSize[component] = cascade.xy.texelWorldSize;
            ubo.cascadeReceiverBias[component] = cascade.receiverBias;
        }

        // 旧字段暂时镜像第 0 级
        ubo.lightViewProjection = ubo.cascadeLightViewProjection[0];
        ubo.shadowParams.z = ubo.cascadeReceiverBias.x;
        ubo.cascadeInfo = glm::uvec4(static_cast<std::uint32_t>(shadowCascadeCount),
                                     static_cast<std::uint32_t>(ShadowMode::Cascaded), 0u, 0u);
    }

    ubo.cascadeInfo.z = shadowPreviewLayer;

    ShadowFrameSummary& summary = result.shadow;
    summary.activeCascadeCount = ubo.cascadeInfo.x;
    summary.cameraNearDistance = ubo.renderParams.y;
    summary.receiverFarDistance = ubo.cascadeSplits.w;
    summary.cameraFovDegrees = camera.fieldOfViewDegrees;
    summary.receiverBiasNormalized = ubo.cascadeReceiverBias;

    return result;
}
