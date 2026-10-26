#pragma once

#include <array>
#include <cstddef>

inline constexpr std::size_t shadowCascadeCount = 4;

// 以相机位于 0，考虑前方 1~100 区域为例
// lambda = 0: 均匀划分，每段长度都是 24.75
// lambda = 1: 对数划分，相邻边界的距离比值相同，近处更密
// lambda = 0.5: 把两种方式对应的边界取平均
std::array<float, shadowCascadeCount> calculateCascadeSplits(float nearPlane, float shadowFar, float lambda);