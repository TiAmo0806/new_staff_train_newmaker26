/**
 * visualizer.hpp —— 调试可视化绘制
 * 功能：检测框 + 标签的绘制
 */

#ifndef VISUALIZER_HPP_
#define VISUALIZER_HPP_

#include <opencv2/opencv.hpp>
#include <vector>

#include "detection.hpp"
#include "config.hpp"

/**
 * @brief 生成类别颜色表（HSV 色相均匀分布）
 * @param numClasses 类别数量
 * @return 每个类别对应的 BGR 颜色
 */
std::vector<cv::Scalar> buildColorTable(int numClasses);

/**
 * @brief 初始化调试窗口（在主循环之前调用一次，创建可拖动调节大小的窗口）
 */
void initDebugWindow();

/**
 * @brief 在帧上绘制检测框和标签
 * @param frame     输入/输出图像
 * @param dets      当前帧的检测结果
 * @param cfg       视觉配置（字体大小、线宽等）
 * @param colors    类别颜色表
 */
void drawDebug(cv::Mat& frame,
               const std::vector<Detection>& dets,
               const VisionConfig& cfg,
               const std::vector<cv::Scalar>& colors);

#endif  // VISUALIZER_HPP_
