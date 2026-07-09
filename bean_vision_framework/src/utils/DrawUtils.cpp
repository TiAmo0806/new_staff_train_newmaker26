#include "utils/DrawUtils.h"

#include <opencv2/imgproc.hpp>

namespace {

/**
 * @brief 在图像上绘制单个固定位置的解析结果。
 * @param frame 输入输出图像，会被直接绘制修改。
 * @param result 单个固定位置的识别结果。
 */
void drawPosition(cv::Mat& frame, const PositionResult& result) {
    if (!result.valid) {
        return;
    }
    // 用绿色画 ROI 解析后真正选中的目标。
    cv::rectangle(frame, result.box, cv::Scalar(0, 255, 0), 2);
    cv::putText(frame,
                result.position_id + ":" + result.class_name,
                cv::Point(result.box.x, std::max(0, result.box.y - 6)),
                cv::FONT_HERSHEY_SIMPLEX,
                0.5,
                cv::Scalar(0, 255, 0),
                1);
}

/**
 * @brief 绘制一组 ROI。
 * @param frame 输入输出图像。
 * @param rois ROI 映射表。
 * @param color 绘制颜色。
 */
void drawRoiGroup(cv::Mat& frame, const std::map<std::string, cv::Rect>& rois, const cv::Scalar& color) {
    for (const auto& [name, rect] : rois) {
        cv::rectangle(frame, rect, color, 2);
        cv::putText(frame,
                    name,
                    cv::Point(rect.x, std::max(0, rect.y - 6)),
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.6,
                    color,
                    2);
    }
}

}  // namespace

/**
 * @brief 在图像上绘制原始检测框。
 * @param frame 输入输出图像，会被直接绘制修改。
 * @param detections 原始检测结果列表。
 */
void DrawUtils::drawDetections(cv::Mat& frame, const std::vector<Detection>& detections) {
    for (const auto& detection : detections) {
        // 原始检测框用偏蓝色画，方便和解析后的绿色框区分。
        cv::rectangle(frame, detection.box, cv::Scalar(255, 180, 0), 1);
        cv::circle(frame, detection.center(), 4, cv::Scalar(0, 0, 255), cv::FILLED);
        cv::putText(frame,
                    detection.class_name + " " + std::to_string(detection.confidence).substr(0, 4),
                    cv::Point(detection.box.x, std::max(0, detection.box.y - 6)),
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.5,
                    cv::Scalar(255, 180, 0),
                    1);
    }
}

/**
 * @brief 在图像上绘制全部固定 ROI 区域。
 * @param frame 输入输出图像，会被直接绘制修改。
 * @param roi ROI 配置。
 */
void DrawUtils::drawRois(cv::Mat& frame, const RoiConfig& roi) {
    // P 点用紫色，L 点用黄色，便于排查检测框中心是否落入对应区域。
    drawRoiGroup(frame, roi.pickup_rois, cv::Scalar(255, 0, 255));
    drawRoiGroup(frame, roi.place_rois, cv::Scalar(0, 255, 255));
}

/**
 * @brief 在图像上绘制固定位置解析结果。
 * @param frame 输入输出图像，会被直接绘制修改。
 * @param result ROI 解析后的视觉结果。
 */
void DrawUtils::drawVisionResult(cv::Mat& frame, const VisionResult& result) {
    // 逐个画固定位置的解析结果。
    drawPosition(frame, result.p1);
    drawPosition(frame, result.p2);
    drawPosition(frame, result.p3);
    drawPosition(frame, result.l4);
    drawPosition(frame, result.l5);
    drawPosition(frame, result.l6);
    drawPosition(frame, result.l7);
    drawPosition(frame, result.l8);
}
