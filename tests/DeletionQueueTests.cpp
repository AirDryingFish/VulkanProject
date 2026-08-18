#include "DeletionQueue.hpp"
#include "VulkanResources.hpp"

#include <cassert>
#include <type_traits>
#include <vector>

static_assert(!std::is_copy_constructible_v<GpuBuffer>);
static_assert(std::is_nothrow_move_constructible_v<GpuBuffer>);
static_assert(!std::is_copy_constructible_v<GpuImage>);
static_assert(std::is_nothrow_move_constructible_v<GpuImage>);
static_assert(!std::is_copy_constructible_v<UniqueShaderModule>);
static_assert(std::is_nothrow_move_constructible_v<UniqueShaderModule>);

static_assert(!std::is_copy_assignable_v<GpuBuffer>);
static_assert(std::is_nothrow_move_assignable_v<GpuBuffer>);

static_assert(!std::is_copy_assignable_v<GpuImage>);
static_assert(std::is_nothrow_move_assignable_v<GpuImage>);

int main()
{
    DeletionQueue queue;
    assert(queue.empty());
    assert(queue.size() == 0);

    std::vector<int> order;
    order.reserve(3);
    queue.pushFunction([&order]() noexcept { order.push_back(1); });
    queue.pushFunction([&order]() noexcept { order.push_back(2); });
    queue.pushFunction([&order]() noexcept { order.push_back(3); });

    assert(!queue.empty());
    assert(queue.size() == 3);

    queue.flush();
    assert((order == std::vector<int>{3, 2, 1}));
    assert(queue.empty());
    assert(queue.size() == 0);

    queue.flush();
    assert((order == std::vector<int>{3, 2, 1}));

    return 0;
}
