#pragma once

#include <glm/glm.hpp>
#include <cstddef>

// shader 的时序数据
// object A   ->    使用同一份 frameData    ->  objectData[0]: A 上一帧模型矩阵、ID
// object B   ->    使用同一份 frameData    ->  objectData[1]: B 上一帧模型矩阵、ID
// shader 通过各自的缓冲区绑定读取他们，再组合计算

// 这一帧的共用数据
struct alignas(16) TemporalFrameUniforms
{
    glm::mat4 currentView{1.0f};
    glm::mat4 currentViewProjectionUnjittered{1.0f};
    glm::mat4 previousViewProjectionUnjittered{1.0f};
    glm::mat4 previousView{1.0f};
    glm::mat4 inverseCurrentViewProjectionJittered{1.0f};

    glm::vec4 cameraPosition{0.0f};

    // xy: 当前 jitter uv
    // zw: 上一次提交 jitter uv
    glm::vec4 jitterUv{0.0f};

    // 历史权重、绝对深度阈值、相对深度阈值、法线阈值
    glm::vec4 resolveParams{0.9f, 0.02f, 0.01f, 0.85f};

    // xy: extent
    // z: 颜色历史有效
    // w: jitter 样本序号
    glm::uvec4 frameInfo{0u};
};

static_assert(offsetof(TemporalFrameUniforms, currentView) == 0);
static_assert(offsetof(TemporalFrameUniforms, currentViewProjectionUnjittered) == 64);
static_assert(offsetof(TemporalFrameUniforms, previousViewProjectionUnjittered) == 128);
static_assert(offsetof(TemporalFrameUniforms, previousView) == 192);
static_assert(offsetof(TemporalFrameUniforms, inverseCurrentViewProjectionJittered) == 256);
static_assert(offsetof(TemporalFrameUniforms, cameraPosition) == 320);
static_assert(offsetof(TemporalFrameUniforms, jitterUv) == 336);
static_assert(offsetof(TemporalFrameUniforms, resolveParams) == 352);
static_assert(offsetof(TemporalFrameUniforms, frameInfo) == 368);
static_assert(alignof(TemporalFrameUniforms) == 16);
static_assert(sizeof(TemporalFrameUniforms) == 384);

// 每个对象独有的数据
struct alignas(16) TemporalObjectData
{
    glm::mat4 previousModel{1.0f};
    // object id low/high, material id low/high
    glm::uvec4 surfaceKey{0u};

    // x: 该对象上一提交变换是否有效
    glm::uvec4 flags{0u};
};

static_assert(offsetof(TemporalObjectData, previousModel) == 0);
static_assert(offsetof(TemporalObjectData, surfaceKey) == 64);
static_assert(offsetof(TemporalObjectData, flags) == 80);
static_assert(alignof(TemporalObjectData) == 16);
static_assert(sizeof(TemporalObjectData) == 96);