#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

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


// 单次四级 csm 的输入快照
struct ShadowCascadeInput
{
    // 必须是有效主相机的刚体仿射逆视图: view space -> world space
    glm::mat4 inverseView{1.0f};

    float fovDegrees = 45.0f;
    float aspectRatio = 1.0f;
    float cameraNear = 0.1f;
    float cameraFar = 1000.0f;

    float shadowDistance = 50.0f;
    float splitLambda = 0.5f;
    // csm 两级之间过渡占本级区间的比例，0表示关闭
    float blendFraction = 0.0f;

    glm::vec3 lightDirection{-1.0f, -1.0f, -2.0f};

    glm::vec3 sceneMinimum{std::numeric_limits<float>::max()};
    glm::vec3 sceneMaximum{std::numeric_limits<float>::lowest()};

    // 调用方必须填写实际 shadow map 分辨率
    std::uint32_t resolution = 0;

    float depthMargin = 1.0f;
    float lightNear = 0.1f;

    // 不同级联的光源深度跨度可能不同，对于普通线性正交深度，可转换为归一化深度偏移
    float receiverBiasWorld = 0.01f;
};

// 输出数据，保存某一级 csm 的计算结果
struct ShadowCascadeData
{
    float nearDistance = 0.0f;
    float farDistance = 0.0f;

    // 实际拟合区间 = [fitNearDistance, farDistance]
    // 计算本级覆盖范围时，从哪里开始覆盖
    float fitNearDistance = 0.0f;
    // 本级从哪里开始向下一级混合
    float blendStartDistance = 0.0f;

    ShadowCascadeXY xy{};
    ShadowCascadeMatrices matrices{};

    // 归一化光源深度单位
    float receiverBias = 0.0f;
};


struct ShadowCascadeFrame
{
    std::array<ShadowCascadeData, shadowCascadeCount> cascades{};
};

ShadowCascadeFrame calculateShadowCascadeFrame(const ShadowCascadeInput& input);
