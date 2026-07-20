#include "visualize.hpp"
#include "detector.hpp"   // for Detection struct

#include <stdio.h>

// 类别颜色表 (BGR)
static const std::vector<cv::Scalar> CLASS_COLORS = {
    cv::Scalar(0,   255, 0  ),   // 绿色
    cv::Scalar(255, 0,   0  ),   // 蓝色
    cv::Scalar(0,   0,   255),   // 红色
    cv::Scalar(255, 255, 0  ),   // 青色
    cv::Scalar(255, 0,   255),   // 品红
    cv::Scalar(0,   255, 255),   // 黄色
    cv::Scalar(128, 0,   128),   // 紫色
    cv::Scalar(255, 165, 0  )    // 橙色
};

// 类别名称（用于绘制标签）
static std::vector<std::string> g_classNames;

void setClassNames(const std::vector<std::string>& names) {
    g_classNames = names;
}

const std::vector<cv::Scalar>& getClassColors() {
    return CLASS_COLORS;
}

void drawDetections(cv::Mat& frame, const std::vector<Detection>& detections) {
    for (const auto& det : detections) {
        int clsId = det.class_id % CLASS_COLORS.size();
        cv::Scalar color = CLASS_COLORS[clsId];

        // 画检测框
        cv::Point pt1(static_cast<int>(det.x1), static_cast<int>(det.y1));
        cv::Point pt2(static_cast<int>(det.x2), static_cast<int>(det.y2));
        cv::rectangle(frame, pt1, pt2, color, 2);

        // 构造标签文字
        char label[128];
        snprintf(label, sizeof(label), "%s %.2f",
                 det.class_name.c_str(), det.confidence);

        // 测量文字尺寸
        int baseline = 0;
        cv::Size textSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX,
                                            0.5, 1, &baseline);

        // 标签背景
        cv::rectangle(frame,
                      cv::Point(pt1.x, pt1.y - textSize.height - baseline - 4),
                      cv::Point(pt1.x + textSize.width, pt1.y),
                      color, cv::FILLED);

        // 标签文字（白色）
        cv::putText(frame, label,
                    cv::Point(pt1.x, pt1.y - baseline - 2),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    cv::Scalar(255, 255, 255), 1);
    }
}
