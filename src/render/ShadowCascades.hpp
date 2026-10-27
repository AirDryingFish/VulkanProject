#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

inline constexpr std::size_t shadowCascadeCount = 4;

// 以相机位于 0，考虑前方 1~100 区域为例
// lambda = 0: 均匀划分，每段长度都是 24.75
// lambda = 1: 对数划分，相邻边界的距离比值相同，近处更密
// lambda = 0.5: 把两种方式对应的边界取平均
std::array<float, shadowCascadeCount> calculateCascadeSplits(float nearPlane, float shadowFar, float lambda);

// 世界空间角点：0..3为近面，4..7为远面
// 每面顺序：(-x, -y) (+x, -y) (+x, +y) (-x, +y)
using FrustumSliceCorners = std::array<glm::vec3, 8>;

// 距离相机 d 的截面，其半高和半宽为：
// halfY = d * tan(FOV / 2); halfX = aspect * halfY
// 四个角点就是： (-halfX, -halfY, -d) (+halfX, -halfY, -d) (+halfX, +halfY, -d) (-halfX, +halfY, -d)
FrustumSliceCorners calculateFrustumSliceCorners(
    const glm::mat4& inverseView,   // 把视空间位置变换回世界空间
    float verticalFovDegrees,       // 相机垂直视野角
    float aspectRatio,              // 宽高比
    float nearDistance,
    float farDistance
);

struct ShadowCascadeXY
{
    // 世界空间 -> 以世界原点为参考的光空间，只包含旋转
    glm::mat4 lightRotation{1.0f};

    glm::vec3 centerWorld{};

    // 已对齐 texel 网格的光空间 XY 中心
    glm::vec2 snappedCenterLightSpace{};

    float radius = 0.0f;
    float halfExtent = 0.0f;
    float texelWorldSize = 0.0f;
};

ShadowCascadeXY fitShadowCascadeXY(
    const FrustumSliceCorners& cornerView,
    const glm::mat4& inverseView,
    const glm::vec3& lightDirection,
    std::uint32_t resolution
);

// 定义完整矩阵计算结果，区分主相机 near 与光源 near
struct ShadowCascadeMatrices
{
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 viewProjection{1.0f};

    float nearPlane = 0.0f;
    float farPlane = 0.0f;
};

// XY 来自稳定拟合；
// sceneMinimum/sceneMaximum 是世界空间 AABB
// lightNear 是光源虚拟相机的正近面距离
ShadowCascadeMatrices calculateShadowCascadeMatrices(
    const ShadowCascadeXY &xy,
    const glm::vec3& sceneMinimum,
    const glm::vec3& sceneMaximum,
    float depthMargin,
    float lightNear);
