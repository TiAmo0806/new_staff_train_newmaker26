#include "spatial.hpp"
#include <algorithm>
#include <stdio.h>

// ============================================================
// ObjectCenter
// ============================================================
ObjectCenter ObjectCenter::fromDetection(const Detection& d) {
    ObjectCenter oc;
    oc.class_id   = d.class_id;
    oc.class_name = d.class_name;
    oc.cx = (d.x1 + d.x2) / 2.0f;
    oc.cy = (d.y1 + d.y2) / 2.0f;
    return oc;
}

// ============================================================
// SpatialSorter
// ============================================================
std::vector<Detection> SpatialSorter::sortLeftToRight(
        const std::vector<Detection>& detections) {
    std::vector<Detection> sorted(detections);
    std::sort(sorted.begin(), sorted.end(),
              [](const Detection& a, const Detection& b) {
                  float cxA = (a.x1 + a.x2) / 2.0f;
                  float cxB = (b.x1 + b.x2) / 2.0f;
                  return cxA < cxB;  // 升序：左边 → 右边
              });
    return sorted;
}

std::vector<ObjectCenter> SpatialSorter::sortedCenters(
        const std::vector<Detection>& detections) {
    auto sorted = sortLeftToRight(detections);
    std::vector<ObjectCenter> centers;
    centers.reserve(sorted.size());
    for (const auto& d : sorted) {
        centers.push_back(ObjectCenter::fromDetection(d));
    }
    return centers;
}

std::string SpatialSorter::formatOrder(const std::vector<Detection>& sorted) {
    std::string result;
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (i > 0) result += " ";  // 空格分隔，方便阅读
        result += std::to_string(sorted[i].class_id);
    }
    return result;
}

// ============================================================
// drawCenters —— 绘制中心点
// ============================================================
void drawCenters(cv::Mat& frame,
                 const std::vector<ObjectCenter>& centers) {

    static const std::vector<cv::Scalar> COLORS = {
        cv::Scalar(0,   255, 0  ),   // 绿色
        cv::Scalar(255, 0,   0  ),   // 蓝色
        cv::Scalar(0,   0,   255),   // 红色
        cv::Scalar(255, 255, 0  ),   // 青色
        cv::Scalar(255, 0,   255),   // 品红
        cv::Scalar(0,   255, 255),   // 黄色
        cv::Scalar(128, 0,   128),   // 紫色
        cv::Scalar(255, 165, 0  )    // 橙色
    };

    for (size_t i = 0; i < centers.size(); ++i) {
        const auto& oc = centers[i];
        cv::Point pt(static_cast<int>(oc.cx), static_cast<int>(oc.cy));
        cv::Scalar color = COLORS[oc.class_id % COLORS.size()];

        // 实心圆标记中心
        cv::circle(frame, pt, 4, color, cv::FILLED);

        // 在中心点旁标注序号（从左到右的排名）和坐标
        char label[64];
        snprintf(label, sizeof(label), "#%zu (%d)", i + 1, oc.class_id);
        cv::putText(frame, label,
                    cv::Point(pt.x + 8, pt.y - 8),
                    cv::FONT_HERSHEY_SIMPLEX, 0.4,
                    color, 1);
    }
}
