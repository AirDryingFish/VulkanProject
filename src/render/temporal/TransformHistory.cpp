#include "render/temporal/TransformHistory.hpp"

#include <cassert>

bool TransformHistory::valid() const noexcept
{
    return valid_;
}

PreviousObjectTransform TransformHistory::previousObject(SceneObjectId id, const glm::mat4& currentModel) const noexcept
{
    if (!valid_)
    {
        return {currentModel, false};
    }

    const auto found = previous_.models.find(id);
    if (found == previous_.models.end())
    {
        return {currentModel, false};
    }

    return {found->second, true};
}

const FrameTransformSnapshot& TransformHistory::previous() const noexcept
{
    assert(valid_);
    return previous_;
}

void TransformHistory::invalidate() noexcept
{
    previous_.models.clear();
    submissionId_ = 0;
    valid_ = false;
}

void TransformHistory::commit(std::uint64_t submissionId, FrameTransformSnapshot&& snapshot) noexcept
{
    assert(submissionId > submissionId_);

    previous_.view = snapshot.view;
    previous_.viewProjectionUnjittered = snapshot.viewProjectionUnjittered;
    previous_.jitterUv = snapshot.jitterUv;
    previous_.models.swap(snapshot.models);

    submissionId_ = submissionId;
    valid_ = true;
}
