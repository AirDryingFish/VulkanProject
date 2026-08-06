#pragma once

#include <deque>
#include <functional>
#include <utility>

struct DeletionQueue
{
    std::deque<std::function<void()>> callbacks;

    void pushFunction(std::function<void()> &&function)
    {
        callbacks.push_back(std::move(function));
    }

    void flush()
    {
        // 反向遍历，最后创建的最先销毁
        for (auto it = callbacks.rbegin(); it != callbacks.rend(); it++)
        {
            (*it)();
        }
        callbacks.clear();
    }
};
