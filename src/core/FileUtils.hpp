#pragma once
#include <string>
#include <vector>

// 以二进制模式读取文件；打开失败时抛出异常。
std::vector<char> readBinaryFile(const std::string& filename);
