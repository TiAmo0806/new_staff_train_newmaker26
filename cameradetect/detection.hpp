/**
 * detection.hpp —— 目标检测数据结构与 YOLOv8 输出解析
 *
 * 定义 Detection 结构体以及 parseYOLOv8Output() 函数的声明。
 * 函数实现位于 detection.cpp。
 */

#ifndef CAMERADETECT_DETECTION_HPP_
#define CAMERADETECT_DETECTION_HPP_

#include <opencv2/opencv.hpp>

#include <vector>

// ============================================================
//  检测结果结构体
// ============================================================
struct Detection {
    int         classId;
    float       confidence;
    cv::Rect2f  box;
};

// ============================================================
//  YOLOv8 输出解析
//  模型输出 shape: [1, 12, 8400] (NCHW)
//  通道 0-3: cx, cy, w, h
//  通道 4-11: 8 类得分
// ============================================================
std::vector<Detection> parseYOLOv8Output(const float* data,
                                          int numClasses, int numAnchors,
                                          int imgW, int imgH,
                                          float scale, int dw, int dh);

#endif  // CAMERADETECT_DETECTION_HPP_
