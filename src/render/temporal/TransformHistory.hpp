#pragma once

#include "scene/SceneTypes.hpp"

#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>

struct PreviousObjectTransform
{
    glm::mat4 model{1.0f};
    bool valid = false;
};

struct FrameTransformSnapshot
{
    glm::mat4 view{1.0f};
    glm::mat4 viewProjectionUnjittered{1.0f};
    glm::vec2 jitterUv{0.0f};

    std::unordered_map<SceneObjectId, glm::mat4> models;
};

// 上一成功提交帧的变换记录器
class TransformHistory
{
public:
    // 有没有可用历史
    bool valid() const noexcept;

    // 查某个对象上一帧的变换
    PreviousObjectTransform previousObject(SceneObjectId id, const glm::mat4& currentModel) const noexcept;

    // 读取整份历史快照
    const FrameTransformSnapshot& previous() const noexcept;

    // 宣布历史不可用
    void invalidate() noexcept;

    // 把本帧变成下一帧的历史
    // && 表示这个接口接收可移动的快照，通常将其中数据移入 previous_ 避免复制
    void commit(std::uint64_t submissionId, FrameTransformSnapshot&& snapshot) noexcept;

private:
    FrameTransformSnapshot previous_{};
    std::uint64_t submissionId_ = 0;
    bool valid_ = false;
};
