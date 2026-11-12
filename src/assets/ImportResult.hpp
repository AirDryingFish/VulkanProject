#pragma once

#include <cstddef>

// 导入事务成功发布后的统计；UI 不持有导入中间资源。
struct GltfImportResult
{
    std::size_t objectCount = 0;
    std::size_t uploadedMeshCount = 0;
    std::size_t reusedMeshCount = 0;
    std::size_t materialCount = 0;
    std::size_t uploadedImageCount = 0;
    std::size_t createdSamplerCount = 0;
};
