#include "core/FileUtils.hpp"
#include <fstream>
#include <stdexcept>

std::vector<char> readBinaryFile(const std::string& filename)
{
    // 二进制模式保留 SPIR-V 字节，初始定位末尾以取得文件大小。
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        throw std::runtime_error("failed to open file!" + filename);
    }

    // 光标位置 = 文件字节数
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);

    file.close();
    return buffer;
}
