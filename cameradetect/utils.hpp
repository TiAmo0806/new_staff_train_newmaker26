/**
 * utils.hpp —— 通用工具函数
 *
 * 提供类别文件加载、字符串判断等辅助功能的声明。
 * 函数实现位于 utils.cpp。
 */

#ifndef CAMERADETECT_UTILS_HPP_
#define CAMERADETECT_UTILS_HPP_

#include <string>
#include <vector>

// ============================================================
//  加载类别名称（可选）
// ============================================================
bool loadClassNames(const std::string& path, std::vector<std::string>& names);

// ============================================================
//  判断字符串是否为纯整数（用于区分相机索引与文件路径）
// ============================================================
bool isIntegerString(const std::string& s);

#endif  // CAMERADETECT_UTILS_HPP_
