/**
 * utils.cpp —— 通用工具函数实现
 */

#include "utils.hpp"

#include <fstream>

bool loadClassNames(const std::string& path, std::vector<std::string>& names)
{
    std::ifstream file(path);
    if (!file.is_open()) return false;
    names.clear();
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) names.push_back(line);
    }
    return !names.empty();
}

bool isIntegerString(const std::string& s)
{
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
    }
    return true;
}
