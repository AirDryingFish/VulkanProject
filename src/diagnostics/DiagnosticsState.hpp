#pragma once

#include "diagnostics/BaselineCapture.hpp"

#include <string>
#include <vk_mem_alloc.h>

struct DiagnosticsState
{
    CpuFrameTimingSnapshot latestCpuTiming{};
    VmaTotalStatistics cachedVmaStatistics{};
    bool hasVmaStatistics = false;
    std::string vmaStatisticsSceneId;
    BaselineCapture baseline;
    bool startRequested = false;
    char csvPath[256] = "gpu-baseline.csv";
};
