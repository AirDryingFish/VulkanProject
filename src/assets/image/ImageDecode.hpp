#pragma once

#include "assets/image/ImageData.hpp"
#include <array>
#include <cstddef>
#include <string>

// 输入字节仅在调用期间借用；成功返回拥有独立 RGBA8 像素，失败抛出解码错误。
[[nodiscard]]
DecodedImageData decodeImage(const std::byte* bytes, std::size_t byteCount, const std::string& debugName);

// 内建材质加载策略：路径为空或读取失败时，返回指定的单像素颜色。
DecodedImageData decodeTextureImageFromFileOrFallback(const std::string& path,
                                                      const std::array<unsigned char, 4>& fallbackPixel);
