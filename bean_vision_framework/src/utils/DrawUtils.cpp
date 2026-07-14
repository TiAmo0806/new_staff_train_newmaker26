#include "utils/DrawUtils.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace {

std::string formatConfidence(float confidence) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << confidence;
    return oss.str();
}

cv::Rect frameBounds(const cv::Mat& frame) {
    return cv::Rect(0, 0, frame.cols, frame.rows);
}

cv::Rect expandRectClamped(const cv::Rect& rect, int padding, const cv::Rect& bounds) {
    const int x1 = std::max(bounds.x, rect.x - padding);
    const int y1 = std::max(bounds.y, rect.y - padding);
    const int x2 = std::min(bounds.x + bounds.width, rect.x + rect.width + padding);
    const int y2 = std::min(bounds.y + bounds.height, rect.y + rect.height + padding);
    return cv::Rect(x1, y1, std::max(0, x2 - x1), std::max(0, y2 - y1));
}

void drawFilledLabel(cv::Mat& frame,
                     const std::string& text,
                     const cv::Point& anchor,
                     const cv::Scalar& text_color,
                     const cv::Scalar& background_color,
                     bool prefer_above) {
    constexpr int pad_x = 6;
    constexpr int pad_y = 4;
    constexpr int gap = 4;
    constexpr double font_scale = 0.5;
    constexpr int thickness = 1;

    int baseline = 0;
    const cv::Size text_size =
        cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, font_scale, thickness, &baseline);
    const int label_width = text_size.width + pad_x * 2;
    const int label_height = text_size.height + baseline + pad_y * 2;

    int x = std::clamp(anchor.x, 0, std::max(0, frame.cols - label_width));
    int y = prefer_above ? anchor.y - gap - label_height : anchor.y + gap;

    if (prefer_above && y < 0) {
        y = std::min(std::max(0, anchor.y + gap), std::max(0, frame.rows - label_height));
    } else if (!prefer_above && y + label_height > frame.rows) {
        y = std::max(0, anchor.y - gap - label_height);
    }

    y = std::clamp(y, 0, std::max(0, frame.rows - label_height));

    const cv::Rect label_rect(x, y, label_width, label_height);
    cv::rectangle(frame, label_rect, background_color, cv::FILLED);
    cv::putText(frame,
                text,
                cv::Point(label_rect.x + pad_x, label_rect.y + pad_y + text_size.height),
                cv::FONT_HERSHEY_SIMPLEX,
                font_scale,
                text_color,
                thickness,
                cv::LINE_AA);
}

/**
 * @brief 在图像上绘制单个固定位置的解析结果。
 * @param frame 输入输出图像，会被直接绘制修改。
 * @param result 单个固定位置的识别结果。
 */
void drawPosition(cv::Mat& frame, const PositionResult& result) {
    if (!result.valid) {
        return;
    }
    // 绿色匹配框向外扩一圈，避免和 YOLO 原始框完全重合。
    const cv::Rect match_box = expandRectClamped(result.box, 4, frameBounds(frame));
    cv::rectangle(frame, match_box, cv::Scalar(0, 255, 0), 2);
    drawFilledLabel(frame,
                    "MATCH " + result.position_id + " <- " + result.class_name,
                    cv::Point(match_box.x, match_box.y + match_box.height),
                    cv::Scalar(255, 255, 255),
                    cv::Scalar(0, 96, 0),
                    false);
}

/**
 * @brief 绘制一组 ROI。
 * @param frame 输入输出图像。
 * @param rois ROI 映射表。
 * @param color 绘制颜色。
 */
void drawRoiGroup(cv::Mat& frame,
                  const std::map<std::string, cv::Rect>& rois,
                  const cv::Scalar& color,
                  const cv::Scalar& text_color) {
    for (const auto& [name, rect] : rois) {
        cv::rectangle(frame, rect, color, 2);
        drawFilledLabel(frame,
                        "ROI " + name,
                        cv::Point(rect.x, rect.y),
                        text_color,
                        color,
                        true);
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
        // 原始检测框使用高对比橙色，便于和绿色匹配框区分。
        cv::rectangle(frame, detection.box, cv::Scalar(0, 140, 255), 2);
        cv::circle(frame, detection.center(), 4, cv::Scalar(0, 0, 255), cv::FILLED);
        drawFilledLabel(frame,
                        "YOLO " + detection.class_name + " " + formatConfidence(detection.confidence),
                        cv::Point(detection.box.x, detection.box.y),
                        cv::Scalar(255, 255, 255),
                        cv::Scalar(0, 140, 255),
                        true);
    }
}

/**
 * @brief 在图像上绘制全部固定 ROI 区域。
 * @param frame 输入输出图像，会被直接绘制修改。
 * @param roi ROI 配置。
 */
void DrawUtils::drawRois(cv::Mat& frame, const RoiConfig& roi) {
    // P 点用紫色，L 点用黄色，便于排查检测框中心是否落入对应区域。
    drawRoiGroup(frame, roi.pickup_rois, cv::Scalar(255, 0, 255), cv::Scalar(255, 255, 255));
    drawRoiGroup(frame, roi.place_rois, cv::Scalar(0, 255, 255), cv::Scalar(0, 0, 0));
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
