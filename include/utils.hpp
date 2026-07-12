/**
 * utils.hpp —— 通用工具函数
 *   - 安全获取文件修改时间
 *   - 解析项目内文件路径
 */

#ifndef UTILS_HPP_
#define UTILS_HPP_

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

// 工具：安全获取文件修改时间，返回值文件最后被修改的那一刻，文件不存在返回零值
inline fs::file_time_type safeLastWriteTime(const std::string& path)
{
    std::error_code ec;
    /*
    C++ 标准库提供的“静默错误”容器。如果下面调用文件系统 API 时出了岔子                                                       
    （比如文件不存在、权限不够），错误信息不会抛异常，而是悄悄 地存进 ec 里，等着我们去检查它。                               
    */
    auto t = fs::last_write_time(path, ec);//立刻返回一个极其古老的时间点
    if (ec) return fs::file_time_type::min();
    return t;
}

/// 解析项目内文件路径（从 build/ 执行时自动找 ../config/ ../model/）
inline std::string resolveProjectPath(const std::string& filename)
{
    std::vector<std::string> candidates = {
        filename,
        "config/" + filename,
        "model/" + filename,
        "../" + filename,
        "../config/" + filename,
        "../model/" + filename,
    };
    for (const auto& c : candidates) {
        if (fs::exists(c)) return c;
    }
    return filename;
}

#endif  // UTILS_HPP_
