#include "render/frame/SceneFrameBuilder.hpp"
#include "scene/Camera.hpp"
#include "scene/Scene.hpp"

#include <algorithm>

std::vector<RenderObjectView> buildRenderObjects(const Scene& scene)
{
    std::vector<RenderObjectView> renderObjects;
    renderObjects.reserve(scene.objects.size());

    for (const SceneObject& object : scene.objects)
    {
        if (!object.mesh || !object.mesh->valid() || !object.material ||
            object.material->descriptorSet == VK_NULL_HANDLE)
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
        view.pushConstants.materialFactors = glm::vec4(material.metallicFactor, material.roughnessFactor,
                                                       material.occlusionStrength, material.normalScale);
        view.pushConstants.emissiveFactor = glm::vec4(material.emissiveFactor, material.alphaCutoff);
        const bool useVertexTangents = mesh.hasTangents && material.normalTexture.texCoord == 0u;
        view.pushConstants.textureInfo = glm::uvec4(material.textureUvMask(), useVertexTangents,
                                                    material.alphaMode == MaterialAlphaMode::Mask ? 1u : 0u, 0u);
        view.materialDescriptorSet = material.descriptorSet;

        renderObjects.push_back(view);
    }
    return renderObjects;
}

RenderFrameData buildSceneRenderData(const Scene& scene, const Camera& camera, const DisplaySettings& display,
                                     const ShadowSettings& shadows)
{
    RenderFrameData renderData{};

    renderData.clearColor = display.clearColor;
    renderData.buildGBuffer = display.buildGBuffer;
    renderData.gBufferView = display.gBufferView;
    renderData.renderPath = display.renderPath;
    renderData.lightCullingMode = display.lightCullingMode;
    // 材质、法线、光空间视图：黑色背景
    // LinearDepth：白色背景，表示没有表面。
    // HDR luminance：保留正常场景背景和天空。
    if (isSurfaceDebugView(display.debugView) || display.debugView == DebugView::ShadowProjection ||
        display.debugView == DebugView::ShadowCascades)
    {
        renderData.clearColor =
            display.debugView == DebugView::LinearDepth ? glm::vec4(1.0f) : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    renderData.directionalShadowsEnabled = shadows.enabled && scene.directionalLight.enabled;
    renderData.shadowMode = shadows.mode;
    renderData.shadowPcfEnabled = shadows.pcfEnabled;
    renderData.shadowReceiverBiasNormalized = shadows.receiverBias;
    renderData.shadowReceiverBiasWorld = shadows.receiverBiasWorld;

    renderData.shadowConstantBias = shadows.constantBias;
    renderData.shadowSlopeBias = shadows.slopeBias;
    renderData.showShadowDepth = shadows.showDepth;

    renderData.shadowDistance = shadows.shadowDistance;
    renderData.shadowSplitLambda = shadows.splitLambda;
    renderData.shadowDepthMargin = shadows.depthMargin;
    renderData.shadowCsmLightNear = shadows.lightNear;
    renderData.shadowSingleCenter = shadows.center;
    renderData.shadowSingleHalfExtent = shadows.halfExtent;

    renderData.debugView = display.debugView;
    renderData.debugDepthNear = camera.nearPlane;
    renderData.debugDepthFar = std::max(display.debugDepthFar, camera.nearPlane + 0.001f);

    // 调试视图的数值/颜色已有专门含义，不能再经过正常画面的曝光和色调映射。
    renderData.bypassToneMapping = display.debugView != DebugView::Lit;
    renderData.exposureEv = display.exposureEv;
    renderData.toneMapper = display.toneMapper;

    return renderData;
}
