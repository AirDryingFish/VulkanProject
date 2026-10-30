#include "ShadowCascades.hpp"

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <limits>

std::array<float, shadowCascadeCount> calculateCascadeSplits(float nearPlane, float shadowFar, float lambda)
{
    if (!std::isfinite(nearPlane) ||
        !std::isfinite(shadowFar) ||
        !std::isfinite(lambda)    ||
        nearPlane <= 0.0f         ||
        shadowFar <= nearPlane    ||
        lambda < 0.0f             ||
        lambda > 1.0f)
    {
        throw std::invalid_argument("Invalid cascade split parameters");
    }

    const double n = static_cast<double>(nearPlane);
    const double f = static_cast<double>(shadowFar);
    const double weight = static_cast<double>(lambda);

    std::array<float, shadowCascadeCount> splits{};
    float previous = nearPlane;

    for (std::size_t i = 0; i < shadowCascadeCount; ++i)
    {
        const double t = static_cast<double>(i + 1) / static_cast<double>(shadowCascadeCount);

        const double uniform = n + (f - n) * t;
        const double logarithmic = n * std::pow(f / n, t);

        const double mixed = (1.0 - weight) * uniform + weight * logarithmic;

        const float split = i + 1 == shadowCascadeCount ? shadowFar : static_cast<float>(mixed);

        if (!std::isfinite(split) || split <= previous || split > shadowFar)
        {
            throw std::range_error("Cascade splits are not representable as increasing floats");
        }
        splits[i] = split;
        previous = split;
    }
    return splits;
}

FrustumSliceCorners calculateFrustumSliceCorners(const glm::mat4 &inverseView, float verticalFovDegrees, float aspectRatio, float nearDistance, float farDistance)
{
    if (!std::isfinite(verticalFovDegrees)  ||
        verticalFovDegrees <= 0.0f          ||
        verticalFovDegrees >= 180.0f        ||

        !std::isfinite(aspectRatio)         ||
        aspectRatio <= 0.0f                 ||

        !std::isfinite(nearDistance)        ||
        nearDistance <= 0.0f                ||

        !std::isfinite(farDistance)         ||
        farDistance <= nearDistance)
    {
        throw std::invalid_argument(
            "Invalid frustum slice parameters");
    }

    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (!std::isfinite(inverseView[col][row]))
            {
                throw std::invalid_argument("Inverse view must be finite");
            }
        }
    }

    const float tanHalfFov = std::tan(glm::radians(verticalFovDegrees) * 0.5f);

    const std::array<glm::vec2, 4> signs{{
        {-1.0f, -1.0f}, {1.0f, -1.0f},
        {1.0f, 1.0f}, {-1.0f, 1.0f}
    }};

    FrustumSliceCorners corners{};
    for (std::size_t plane = 0; plane < 2; ++plane)
    {
        const float distance = plane == 0 ? nearDistance : farDistance;

        const float halfY = distance * tanHalfFov;
        const float halfX = halfY * aspectRatio;

        for (std::size_t corner = 0; corner < signs.size(); ++corner)
        {
            // 前方为 -Z，w = 1 表示位置，包含相机平移
            const glm::vec4 viewPoint(
                signs[corner].x * halfX,
                signs[corner].y * halfY,
                -distance,
                1.0f
            );

            // 逆视图是仿射变换，这里没有投影，也不翻转 Y
            const glm::vec3 worldPoint(inverseView * viewPoint);

            if (!std::isfinite(worldPoint.x) ||
                !std::isfinite(worldPoint.y) ||
                !std::isfinite(worldPoint.z))
            {
                throw std::range_error(
                    "Frustum corner is not finite");
            }

            corners[plane * 4 + corner] = worldPoint;
        }
    }
    return corners;
}

// 包围球固定每级阴影的覆盖尺寸，再将覆盖中心对齐到 texel 网格，减少相机移动和旋转时的阴影抖动
ShadowCascadeXY fitShadowCascadeXY(const FrustumSliceCorners &cornerView, const glm::mat4 &inverseView, const glm::vec3 &lightDirection, std::uint32_t resolution)
{
    // 在包围球的四周，每侧预留 2 个 texel 的安全边距
    constexpr double guardTexels = 2.0;

    if (resolution <= 2.0 * guardTexels)
    {
        throw std::invalid_argument("Shadow resolution is too small");
    }

    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (!std::isfinite(inverseView[col][row]))
            {
                throw std::invalid_argument("Inverse view must be finite");
            }
        }
    }

    // 先在 view space 求球，半径不会随相机平移、旋转而变化
    glm::dvec3 centerView(0.0);

    for (const auto& corner : cornerView)
    {
        if (!std::isfinite(corner.x) ||
            !std::isfinite(corner.y) ||
            !std::isfinite(corner.z))
        {
            throw std::invalid_argument("Frustum corners must be finite");
        }

        centerView += glm::dvec3(corner);
    }

    centerView /= static_cast<double>(cornerView.size());

    double radius = 0.0;
    for (const auto& corner : cornerView)
    {
        radius = std::max(radius, glm::length(glm::dvec3(corner) - centerView));
    }

    if (!std::isfinite(radius) || radius <= 0.0)
    {
        throw std::invalid_argument("Frustum sphere must have positve radius");
    }

    const glm::dvec3 directionInput(lightDirection);
    const double lengthSquared = glm::dot(directionInput, directionInput);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0)
    {
        throw std::invalid_argument("Light direction must be finite and nonzero");
    }

    const glm::vec3 direction(directionInput / std::sqrt(lengthSquared));
    const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);
    // 如果光线方向朝着up，用(0 1 0)作为lightUp
    const glm::vec3 lightUp = std::abs(glm::dot(direction, worldUp)) > 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : worldUp;

    ShadowCascadeXY fit{};
    fit.lightRotation = glm::lookAtRH(glm::vec3(0.0f), direction, lightUp);
    fit.centerWorld = glm::vec3(inverseView * glm::vec4(glm::vec3(centerView), 1.0));

    // 贴图分辨率（多少格）
    const double size = static_cast<double>(resolution);
    // shadow map 覆盖的正方形区域，从中心到一侧边缘的距离
    const double halfExtent = radius / (1.0 - 2.0 * guardTexels / size);

    fit.radius = static_cast<float>(radius);
    fit.halfExtent = static_cast<float>(halfExtent);
    fit.texelWorldSize = static_cast<float>(2 * fit.halfExtent / size);
    if (!std::isfinite(fit.radius) || fit.radius <= 0.0f ||
        !std::isfinite(fit.halfExtent) || fit.halfExtent <= 0.0f ||
        !std::isfinite(fit.texelWorldSize) ||
        fit.texelWorldSize <= 0.0f)
    {
        throw std::range_error(
            "Shadow XY size is not representable");
    }

    // 把包围球球心从 world 转 light
    const glm::vec3 centerLightSpace(fit.lightRotation * glm::vec4(fit.centerWorld, 1.0f));
    if (!std::isfinite(centerLightSpace.x) ||
        !std::isfinite(centerLightSpace.y) ||
        !std::isfinite(centerLightSpace.z))
    {
        throw std::range_error(
            "Shadow center is not finite");
    }

    const double texel = fit.texelWorldSize;
    fit.snappedCenterLightSpace = glm::vec2(
        std::round(static_cast<double>(centerLightSpace.x / texel)) * texel,
        std::round(static_cast<double>(centerLightSpace.y / texel)) * texel);
    if (!std::isfinite(fit.snappedCenterLightSpace.x) ||
        !std::isfinite(fit.snappedCenterLightSpace.y))
    {
        throw std::range_error(
            "Snapped shadow center is not finite");
    }

    return fit;
}

// 将场景包围盒变换到光空间，建立能容纳遮挡物的完整矩阵
ShadowCascadeMatrices calculateShadowCascadeMatrices(const ShadowCascadeXY &xy, const glm::vec3 &sceneMinimum, const glm::vec3 &sceneMaximum, float depthMargin, float lightNear)
{
    // 数值检查
    if (!std::isfinite(xy.halfExtent) ||
        xy.halfExtent <= 0.0f ||

        !std::isfinite(xy.snappedCenterLightSpace.x) ||
        !std::isfinite(xy.snappedCenterLightSpace.y) ||

        !std::isfinite(depthMargin) ||
        depthMargin < 0.0f ||

        !std::isfinite(lightNear) ||
        lightNear <= 0.0f)
    {
        throw std::invalid_argument("Invalid shadow projection parameters");
    }

    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(sceneMinimum[axis]) ||
            !std::isfinite(sceneMaximum[axis]) ||
            sceneMinimum[axis] > sceneMaximum[axis])
        {
            throw std::invalid_argument("Invalid scene bounds");
        }
    }

    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (!std::isfinite(xy.lightRotation[column][row]))
            {
                throw std::invalid_argument("Light rotation must be finite");
            }
        }
    }


    const glm::dmat4 rotation(xy.lightRotation);
    double minimumZ = std::numeric_limits<double>::max();
    double maximumZ = std::numeric_limits<double>::lowest();

    std::array<glm::vec4, 8> cornersWorld{};


    // 统计 Z 的最大最小值
    // i	二进制 X  Y   Z
    // 0	000	min	min	min
    // 1	001	max	min	min
    // 2	010	min	max	min
    // 3	011	max	max	min
    // 4	100	min	min	max
    // 5	101	max	min	max
    // 6	110	min	max	max
    // 7	111	max	max	max
    for (std::size_t i = 0; i < cornersWorld.size(); ++i)
    {
        cornersWorld[i] = glm::vec4(
            (i & 1) ? sceneMaximum.x : sceneMinimum.x,
            (i & 2) ? sceneMaximum.y : sceneMinimum.y,
            (i & 4) ? sceneMaximum.z : sceneMinimum.z,
            1.0f
        );

        const glm::dvec4 light = rotation * glm::dvec4(cornersWorld[i]);

        minimumZ = std::min(minimumZ, light.z);
        maximumZ = std::max(maximumZ, light.z);
    }

    // margin 是场景前后留的安全距离
    const double margin = depthMargin;
    // 把相机放在场景最大 Z 的外侧，并保证 near 平面和场景之间还有 margin 的间隔
    const double eyeZ = maximumZ + margin + lightNear;
    // 从相机到 far 平面的矩阵。在场景的前后两侧各留一份 margin，所以要 * 2.0
    const double farDistance = (maximumZ - minimumZ) + 2.0 * margin + lightNear;

    ShadowCascadeMatrices matrices{};
    matrices.nearPlane = lightNear;
    matrices.farPlane = static_cast<float>(farDistance);
    if (!std::isfinite(eyeZ) ||
        !std::isfinite(matrices.farPlane) ||
        matrices.farPlane <= matrices.nearPlane)
    {
        throw std::range_error("Shadow depth interval is not representable");
    }

    // 先旋转到 light space，再平移
    const glm::dvec3 translation(
        -static_cast<double>(xy.snappedCenterLightSpace.x),
        -static_cast<double>(xy.snappedCenterLightSpace.y),
        -eyeZ
    );

    matrices.view = glm::mat4(
        glm::translate(glm::dmat4(1.0), translation) * rotation
    );

    const double h = xy.halfExtent;


    matrices.projection = glm::mat4(glm::orthoRH_ZO(
        -h, h, -h, h,
        static_cast<double>(matrices.nearPlane),
        static_cast<double>(matrices.farPlane)
    ));

    // xy 对称，Y 翻转只需要改变这个缩放项(因为 Vulkan viewport (0, 0) 在左上方)
    matrices.projection[1][1] *= -1.0f;
    matrices.viewProjection = matrices.projection * matrices.view;

    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (!std::isfinite(matrices.view[col][row]) ||
                !std::isfinite(matrices.projection[col][row]) ||
                !std::isfinite(matrices.viewProjection[col][row]))
            {
                throw std::range_error("Shadow matrix is not finite");
            }
        }
    }

    for (const auto& world : cornersWorld)
    {
        const glm::vec4 clip = matrices.viewProjection * world;
        if (!std::isfinite(clip.z) ||
            !std::isfinite(clip.w) ||
            clip.w <= 0.0f)
        {
            throw std::range_error("Shadow clip depth is not finite");
        }

        const float depth = clip.z / clip.w;

        if (depth < -1e-4f || depth > 1.0001f)
        {
            throw std::range_error("Scene depth is outside the shadow interval");
        }
    }

    return matrices;
}

ShadowCascadeFrame calculateShadowCascadeFrame(const ShadowCascadeInput &input)
{
    if (!std::isfinite(input.cameraNear)            ||
        input.cameraNear <= 0.0f                    ||

        !std::isfinite(input.cameraFar)             ||
        input.cameraFar <= input.cameraNear         ||

        !std::isfinite(input.shadowDistance)        ||
        input.shadowDistance <= input.cameraNear    ||

        !std::isfinite(input.receiverBiasWorld)     ||
        input.receiverBiasWorld < 0.0f              ||

        !std::isfinite(input.blendFraction)         ||
        input.blendFraction < 0.0f                  ||
        input.blendFraction > 1.0f
    )
    {
        throw std::invalid_argument(
            "Invalid cascade frame parameters");
    }

    const float effectiveShadowFar = std::min(input.shadowDistance, input.cameraFar);

    const auto splits = calculateCascadeSplits(input.cameraNear, effectiveShadowFar, input.splitLambda);

    ShadowCascadeFrame frame{};
    float sliceNear = input.cameraNear;


    for (std::size_t i = 0; i < frame.cascades.size(); ++i)
    {
        auto& cascade = frame.cascades[i];

        cascade.nearDistance = sliceNear;
        cascade.farDistance = splits[i];

        // 本级在什么时候开始提供阴影，应该在上一级开始混合的地方就开始提供
        cascade.fitNearDistance = i == 0 ? input.cameraNear : frame.cascades[i - 1].blendStartDistance;

        cascade.blendStartDistance = cascade.farDistance;

        if (i + 1 < frame.cascades.size() && input.blendFraction > 0.0f)
        {
            const double fraction = static_cast<double>(input.blendFraction);
            const double blendStart = (1.0 - fraction) * static_cast<double>(cascade.farDistance) + fraction * static_cast<double>(cascade.nearDistance);

            cascade.blendStartDistance = static_cast<float>(blendStart);

            if (!std::isfinite(cascade.blendStartDistance) || cascade.blendStartDistance < cascade.nearDistance || cascade.blendStartDistance >= cascade.farDistance)
            {
                throw std::range_error("Cascade blend interval is not representable");
            }

        }


        // inverseView 用单位阵，是因为后续在 view space 中计算包围球中心和半径
        const auto cornersView = calculateFrustumSliceCorners(glm::mat4(1.0f), input.fovDegrees, input.aspectRatio, cascade.fitNearDistance, cascade.farDistance);
        cascade.xy = fitShadowCascadeXY(cornersView, input.inverseView, input.lightDirection, input.resolution);
        // 给当前级 csm 计算完整的 light camera 矩阵和 深度范围
        cascade.matrices = calculateShadowCascadeMatrices(cascade.xy, input.sceneMinimum, input.sceneMaximum, input.depthMargin, input.lightNear);

        // 世界单位 bias -> 本级光源归一化深度单位
        // 把 “world space 中偏移多远” 换算成 “Shadow map 深度偏移多少”
        const double depthSpan = static_cast<double>(cascade.matrices.farPlane) - static_cast<double>(cascade.matrices.nearPlane);
        cascade.receiverBias = static_cast<float>(static_cast<double>(input.receiverBiasWorld) / depthSpan);
        if (!std::isfinite(cascade.receiverBias) ||
            cascade.receiverBias < 0.0f)
        {
            throw std::range_error("Cascade receiver bias is not representable");
        }

        sliceNear = splits[i];
    }
    return frame;
}
