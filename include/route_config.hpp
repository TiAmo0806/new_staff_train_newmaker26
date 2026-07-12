/**
 * route_config.hpp —— 路径映射配置
 * 从 route_config.txt 加载，支持热重载
 */

#ifndef ROUTE_CONFIG_HPP_
#define ROUTE_CONFIG_HPP_

#include <cstdint>
#include <map>
#include <string>
#include <optional>
#include <fstream>
#include <iostream>
#include <filesystem>

#include "utils.hpp"

namespace fs = std::filesystem;

//  路径指令结构体
struct PathCommand {
    uint8_t first_cmd = 0;      // 0=直行, 1=左转, 2=右转
    uint8_t second_cmd = 2;     // 1=左分支, 2=中分支, 3=右分支
    uint8_t turn_strength = 0; // 转弯强度 10~80
};

//  路径配置类（加载 + 查询 + 热重载）
class RouteConfig {
public:
    /// 从文件加载，失败则使用默认值
    bool load(const std::string& filename = "route_config.txt")
    {
        filepath_ = resolveProjectPath(filename);
        mapping_.clear();

        std::ifstream file(filepath_);
        if (!file.is_open()) {
            std::cerr << "无法打开 " << filepath_ << "，使用默认值" << std::endl;
            mapping_["soybean"] = {0, 1, 30};
            mapping_["mung_bean"] = {1, 2, 50};
            mapping_["white_kidney_bean"] = {2, 3, 60};
            mtime_ = safeLastWriteTime(filepath_);
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

        mtime_ = safeLastWriteTime(filepath_);
        return true;
    }

    // 查询路径指令
    /*
    std::optional 是 "可能包含值，也可能为空"的容器。
    要么返回一个有效的 PathCommand 路线指令，要么返回一个‘空’（表示没找到）
    */
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
