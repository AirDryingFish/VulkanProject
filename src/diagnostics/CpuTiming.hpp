#pragma once

#include <chrono>

using CpuClock = std::chrono::steady_clock;

// 统一时钟和单位。steady_clock 用于测量经过的时间，duration<double, std::milli> 保留毫秒的小数部分
inline double cpuElapsedMs(CpuClock::time_point begin, CpuClock::time_point end)
{
    return std::chrono::duration<double, std::milli>(end - begin).count();
}