/**
 * visualizer.hpp —— 调试可视化绘制
 * 功能：检测框、状态栏、FPS 等调试信息的画面绘制
 */

#ifndef VISUALIZER_HPP_
#define VISUALIZER_HPP_

#include <opencv2/opencv.hpp>
#include <vector>

#include "detection.hpp"
#include "config.hpp"
#include "stable_tracker.hpp"

/**
 * @brief 在帧上绘制调试信息（检测框 + 状态栏 + FPS）
 * @param frame     输入/输出图像
 * @param dets      当前帧的检测结果
 * @param tracker   稳定跟踪器（读取状态信息）
 * @param cfg       视觉配置（字体大小、线宽等）
 * @param colors    类别颜色表
 * @param modelOk   模型是否正常
 * @param serialOk  串口是否正常
 * @param fps       当前帧率
 */
/**
 * @brief 生成类别颜色表（HSV 色相均匀分布）
 * @param numClasses 类别数量
 * @return 每个类别对应的 BGR 颜色
 */
std::vector<cv::Scalar> buildColorTable(int numClasses);

void drawDebug(cv::Mat& frame,
               const std::vector<Detection>& dets,
               const StableTracker& tracker,
               const VisionConfig& cfg,
               const std::vector<cv::Scalar>& colors,
               bool modelOk, bool serialOk, double fps);

#endif  // VISUALIZER_HPP_
