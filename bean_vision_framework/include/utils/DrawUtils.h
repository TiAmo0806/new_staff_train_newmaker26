#pragma once

#include "core/DetectionTypes.h"
#include "core/AppConfig.h"
#include "core/VisionResult.h"

#include <opencv2/core.hpp>

#include <vector>

class DrawUtils {
public:
    /**
     * @brief 在图像上绘制原始检测框。
     * @param frame 输入输出图像，会被直接绘制修改。
     * @param detections 检测器输出的原始检测结果。
     */
    static void drawDetections(cv::Mat& frame, const std::vector<Detection>& detections);

    /**
     * @brief 在图像上绘制全部固定 ROI 区域。
     * @param frame 输入输出图像，会被直接绘制修改。
     * @param roi ROI 配置，包含 P1/P2/P3 和 L4-L8。
     */
    static void drawRois(cv::Mat& frame, const RoiConfig& roi);

    /**
     * @brief 在图像上绘制 ROI 解析后的固定位置结果。
     * @param frame 输入输出图像，会被直接绘制修改。
     * @param result ROI 解析后的视觉结果。
     */
    static void drawVisionResult(cv::Mat& frame, const VisionResult& result);
};
