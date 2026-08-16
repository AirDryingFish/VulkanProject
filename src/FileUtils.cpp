#include "FileUtils.hpp"
#include <fstream>
#include <stdexcept>

std::vector<char> readBinaryFile(const std::string &filename)
{
    // ate: 从文件末尾开始读取，这样就可以直接得到文件大小，不需要先读一遍来计算大小了
    // binary: 以二进制模式打开文件，SPIR-V 是二进制数据。如果用文本模式，Windows 会把 \r\n 自动转成 \n，破坏二进制内容。
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        throw std::runtime_error("failed to open file!");
    }

    // 光标位置 = 文件字节数
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);

    file.close();
    return buffer;
}
