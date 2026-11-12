#pragma once

#include <cstdint>
#include <string>
#include <vector>

// CPU 侧拥有的 RGBA8 像素；解码器和 GPU 上传阶段共用，不依赖资产文件格式。
struct DecodedImageData
{
    std::string name;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba8;
};
