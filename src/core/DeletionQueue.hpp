#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <utility>

struct DeletionQueue
{
    std::deque<std::function<void()>> callbacks;

    // 按创建顺序登记释放；回调捕获的对象须存活到 flush，且不能抛出异常。
    void pushFunction(std::function<void()>&& function)
    {
        callbacks.push_back(std::move(function));
    }

    // 调用方先保证资源已不再使用；按登记逆序释放并清空。
    void flush() noexcept
    {
        // 反向遍历，最后创建的最先销毁
        for (auto it = callbacks.rbegin(); it != callbacks.rend(); it++)
        {
            (*it)();
        }
        callbacks.clear();
    }

    bool empty() const noexcept
    {
        return callbacks.empty();
    }

    std::size_t size() const noexcept
    {
        return callbacks.size();
    }
};
