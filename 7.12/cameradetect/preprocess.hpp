/**
 * preprocess.hpp —— 图像预处理
 *
 * 提供 letterbox 缩放 + 归一化的预处理函数声明。
 * 函数实现位于 preprocess.cpp。
 */

#ifndef CAMERADETECT_PREPROCESS_HPP_
#define CAMERADETECT_PREPROCESS_HPP_

#include <opencv2/opencv.hpp>

// ============================================================
//  预处理 —— letterbox 缩放 + 归一化
// ============================================================
cv::Mat preprocess(const cv::Mat& src, int& dw, int& dh, float& scale);

#endif  // CAMERADETECT_PREPROCESS_HPP_
