/**
 * route_config.hpp —— 路径映射配置
 * 从 route_config.txt 加载，支持热重载
 */

#ifndef ROUTE_CONFIG_HPP_
#define ROUTE_CONFIG_HPP_

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <optional>
#include <fstream>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

// ============================================================
//  路径指令结构体
// ============================================================
struct PathCommand {
    uint8_t first_cmd = 0;      // 0=直行, 1=左转, 2=右转
    uint8_t second_cmd = 1;     // 1=左分支, 2=中分支, 3=右分支
    uint8_t turn_strength = 30; // 转弯强度 10~80
};

// ============================================================
//  工具：安全获取文件修改时间（文件不存在返回零值）
// ============================================================
inline fs::file_time_type safeLastWriteTime(const std::string& path)
{
    std::error_code ec;
    auto t = fs::last_write_time(path, ec);
    if (ec) return fs::file_time_type::min();
    return t;
}

// ============================================================
//  工具：解析项目内文件路径
//  从 build/ 执行时自动找 ../  和 ../config/ ../model/
// ============================================================
inline std::string resolveProjectPath(const std::string& filename)
{
    // 候选搜尋順序
    std::vector<std::string> candidates = {
        filename,                          // 直接路径 / 当前目录
        "config/" + filename,              // config/ 子目录
        "model/" + filename,               // model/ 子目录
        "../" + filename,                  // 上级目录
        "../config/" + filename,           // 上级 config/
        "../model/" + filename,            // 上级 model/
    };
    for (const auto& c : candidates) {
        if (fs::exists(c)) return c;
    }
    // 都不存在，返回原始路径让后续 open 报错
    return filename;
}

// ============================================================
//  路径配置类（加载 + 查询 + 热重载）
// ============================================================
class RouteConfig {
public:
    /// 从文件加载，失败则使用默认值
    bool load(const std::string& filename = "route_config.txt")
    {
        std::string filepath = resolveProjectPath(filename);
        filepath_ = filepath;
        mapping_.clear();

        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "无法打开 " << filepath << "，使用默认值" << std::endl;
            mapping_["soybean"] = {0, 1, 30};
            mapping_["mung_bean"] = {1, 2, 50};
            mapping_["white_kidney_bean"] = {2, 3, 60};
            mtime_ = safeLastWriteTime(filepath);
            return false;
        }

        std::string name;
        int first, second, strength;
        while (file >> name >> first >> second >> strength) {
            mapping_[name] = {
                static_cast<uint8_t>(first),
                static_cast<uint8_t>(second),
                static_cast<uint8_t>(strength)};
            std::cout << "路径规则: " << name
                      << " → " << first << second
                      << " 强度:" << strength << std::endl;
        }

        mtime_ = safeLastWriteTime(filepath);
        return true;
    }

    /// 查询路径指令
    std::optional<PathCommand> lookup(const std::string& name) const
    {
        auto it = mapping_.find(name);
        if (it != mapping_.end()) return it->second;
        return std::nullopt;
    }

    /// 检查文件是否被修改（用于热重载）
    bool fileChanged() const
    {
        return safeLastWriteTime(filepath_) != mtime_;
    }

private:
    std::string filepath_;
    std::map<std::string, PathCommand> mapping_;
    fs::file_time_type mtime_;
};

#endif  // ROUTE_CONFIG_HPP_
