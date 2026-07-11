#include "/home/newmaker-11/mvs_openvino_demo/include/spatial.hpp"
#include <stdio.h>
#include <cmath>

// ============================================================
// ObjectCenter
// ============================================================
ObjectCenter ObjectCenter::fromDetection(const Detection& d) {
    ObjectCenter oc;
    oc.class_id   = d.class_id;
    oc.class_name = d.class_name;
    oc.x1 = d.x1;  oc.y1 = d.y1;
    oc.x2 = d.x2;  oc.y2 = d.y2;
    oc.cx = (d.x1 + d.x2) / 2.0f;   // 中心 X
    oc.cy = (d.y1 + d.y2) / 2.0f;   // 中心 Y
    return oc;
}

// ============================================================
// SpatialAnalyzer
// ============================================================
std::vector<ObjectCenter> SpatialAnalyzer::extractCenters(
        const std::vector<Detection>& detections) const {
    std::vector<ObjectCenter> centers;
    centers.reserve(detections.size());
    for (const auto& d : detections) {
        centers.push_back(ObjectCenter::fromDetection(d));
    }
    return centers;
}

std::vector<SpatialRelation> SpatialAnalyzer::analyze(
        const std::vector<Detection>& detections) const {

    // 先提取所有中心点
    auto centers = extractCenters(detections);
    std::vector<SpatialRelation> relations;

    size_t n = centers.size();
    if (n < 2) return relations;  // 少于 2 个对象无需分析

    // 两两比较，生成关系
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            const auto& a = centers[i];
            const auto& b = centers[j];
            float dx = a.cx - b.cx;   // A 相对于 B 的 X 偏移
            float dy = a.cy - b.cy;   // A 相对于 B 的 Y 偏移

            SpatialRelation rel;
            rel.objectA = a.class_name;
            rel.objectB = b.class_name;

            // ---- X 方向 ----
            // A.x < B.x → A 在 B 左边
            // A.x > B.x → A 在 B 右边
            if (dx < -xTolerance) {
                rel.xRelation = "左边";
            } else if (dx > xTolerance) {
                rel.xRelation = "右边";
            } else {
                rel.xRelation = "同一列";
            }

            // ---- Y 方向 ----
            // A.y < B.y → A 比 B 更靠画面上方 → A 在 B"后面"（离相机远）
            // A.y > B.y → A 比 B 更靠画面下方 → A 在 B"前面"（离相机近）
            if (dy < -yTolerance) {
                rel.yRelation = "后面";
            } else if (dy > yTolerance) {
                rel.yRelation = "前面";
            } else {
                rel.yRelation = "同一行";
            }

            relations.push_back(rel);
        }
    }

    return relations;
}

// ============================================================
// 格式化
// ============================================================
std::string SpatialAnalyzer::format(const SpatialRelation& rel) {
    std::string result = rel.objectA + " 在 " + rel.objectB + " 的";

    if (rel.xRelation != "同一列" && rel.yRelation != "同一行") {
        result += rel.xRelation + "，" + rel.yRelation;
    } else if (rel.xRelation != "同一列") {
        result += rel.xRelation;
    } else if (rel.yRelation != "同一行") {
        result += rel.yRelation;
    } else {
        result += "同一位置";
    }

    return result;
}

// ============================================================
// 绘制中心点和连线
// ============================================================
void drawCenters(cv::Mat& frame,
                 const std::vector<ObjectCenter>& centers,
                 const std::vector<SpatialRelation>& relations) {

    // 颜色表（与 visualize 公用一套颜色）
    static const std::vector<cv::Scalar> COLORS = {
        cv::Scalar(0,   255, 0  ),
        cv::Scalar(255, 0,   0  ),
        cv::Scalar(0,   0,   255),
        cv::Scalar(255, 255, 0  ),
        cv::Scalar(255, 0,   255),
        cv::Scalar(0,   255, 255),
        cv::Scalar(128, 0,   128),
        cv::Scalar(255, 165, 0  )
    };

    // 1. 画每个对象的中心点
    for (const auto& oc : centers) {
        cv::Point pt(static_cast<int>(oc.cx), static_cast<int>(oc.cy));
        cv::Scalar color = COLORS[oc.class_id % COLORS.size()];

        // 画实心圆标记中心
        cv::circle(frame, pt, 4, color, cv::FILLED);

        // 在中心点旁标注坐标
        char coord[64];
        snprintf(coord, sizeof(coord), "(%.0f,%.0f)", oc.cx, oc.cy);
        cv::putText(frame, coord,
                    cv::Point(pt.x + 8, pt.y - 8),
                    cv::FONT_HERSHEY_SIMPLEX, 0.35,
                    color, 1);
    }

    // 2. 画位置关系连线（可选）
    if (relations.empty()) return;

    // 需要根据名称查找中心点，先建索引
    // 简化处理：连线画在相同类别的两个对象之间
    for (const auto& rel : relations) {
        // 找到 A 和 B 各自的中心
        const ObjectCenter* ca = nullptr;
        const ObjectCenter* cb = nullptr;
        for (const auto& oc : centers) {
            if (oc.class_name == rel.objectA && !ca) ca = &oc;
            if (oc.class_name == rel.objectB && !cb) cb = &oc;
        }
        if (!ca || !cb) continue;

        cv::Point pa(static_cast<int>(ca->cx), static_cast<int>(ca->cy));
        cv::Point pb(static_cast<int>(cb->cx), static_cast<int>(cb->cy));

        // 连线（白色虚线）
        cv::line(frame, pa, pb, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);

        // 在中点标注关系文字
        cv::Point mid((pa.x + pb.x) / 2, (pa.y + pb.y) / 2);
        std::string relText;
        if (rel.xRelation != "同一列" && rel.yRelation != "同一行")
            relText = rel.xRelation + "," + rel.yRelation;
        else if (rel.xRelation != "同一列")
            relText = rel.xRelation;
        else if (rel.yRelation != "同一行")
            relText = rel.yRelation;
        else
            relText = "同位置";

        cv::putText(frame, relText, mid,
                    cv::FONT_HERSHEY_SIMPLEX, 0.4,
                    cv::Scalar(255, 255, 200), 1);
    }
}
