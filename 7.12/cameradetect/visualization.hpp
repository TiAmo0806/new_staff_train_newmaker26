/**
 * visualization.hpp —— 绘制检测结果与统计面板
 *
 * 提供 drawDetections()、drawBeanPositions() 和 drawStats() 函数的声明。
 * 函数实现位于 visualization.cpp。
 */

#ifndef CAMERADETECT_VISUALIZATION_HPP_
#define CAMERADETECT_VISUALIZATION_HPP_

#include "detection.hpp"
#include "config.hpp"

#include <opencv2/opencv.hpp>

#include <vector>
#include <string>
#include <cstdint>

// ============================================================
//  豆子位置结果
// ============================================================
struct BeanPositionResult {
    uint8_t leftBean  = 0xFF;   // 左位豆类别
    uint8_t midBean   = 0xFF;   // 中位豆类别
    uint8_t rightBean = 0xFF;   // 右位豆类别
    uint8_t leftNum   = 0;      // 左位对应数字箱号
    uint8_t midNum    = 0;      // 中位对应数字箱号
    uint8_t rightNum  = 0;      // 右位对应数字箱号
};

// ============================================================
//  绘制检测结果
// ============================================================
void drawDetections(cv::Mat& frame, const std::vector<Detection>& dets,
                    const std::vector<cv::Scalar>& colors,
                    const std::vector<std::string>& classNames,
                    const Config& cfg);

// ============================================================
//  绘制三厢位置信息（左/中/右 + 豆类别 + 对应数字箱）
// ============================================================
void drawBeanPositions(cv::Mat& frame, const BeanPositionResult& pos,
                       const Config& cfg);

// ============================================================
//  统计面板
// ============================================================
void drawStats(cv::Mat& frame, int beanCount, int numberCount,
               double fps, double latencyMs);

#endif  // CAMERADETECT_VISUALIZATION_HPP_
