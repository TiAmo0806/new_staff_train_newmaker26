/**
 * visualizer.cpp —— 调试可视化绘制实现
 */

#include "visualizer.hpp"

#include <cstdio>
#include <string>

//  生成颜色表（每个类别一种颜色，通过 HSV 色相均匀分布）
std::vector<cv::Scalar> buildColorTable(int numClasses)
{
    std::vector<cv::Scalar> colors;
    for (int i = 0; i < numClasses; ++i) {
        // 在 HSV 空间中均匀分布色相
        int hue = static_cast<int>(180.0 * i / numClasses);
        cv::Mat hsv(1, 1, CV_8UC3, cv::Scalar(hue, 255, 255));
        cv::Mat bgr;
        cv::cvtColor(hsv, bgr, cv::COLOR_HSV2BGR);
        colors.push_back(cv::Scalar(bgr.at<cv::Vec3b>(0)));
    }
    return colors;
}

void initDebugWindow()
{
    cv::namedWindow("NUC Vision — Debug", cv::WINDOW_NORMAL);
    // 初始设为 960x540，比原图小，确保放大缩小都能拖
    cv::resizeWindow("NUC Vision — Debug", 960, 540);
}

void drawDebug(cv::Mat& frame,
               const std::vector<Detection>& dets,
               const VisionConfig& cfg,
               const std::vector<cv::Scalar>& colors)
{
    // ── 检测框 ──
    for (const auto& d : dets) {
        //防止出现class_name>8的情况，有效防止越界
        cv::Scalar color = colors[d.class_id % colors.size()];
        int thick = std::max(3, cfg.line_thickness);
        cv::rectangle(frame, d.bbox, color, thick);

        // 标签（白底），textSize 用类名模板缓存，不每帧重复计算
        char label[64];
        snprintf(label, sizeof(label), "%s %.2f",
                 CLASS_NAMES[d.class_id].c_str(), d.confidence);
        int base = cv::FONT_HERSHEY_SIMPLEX;
        double fs = std::max(0.5, cfg.font_scale);

        static std::vector<cv::Size> cachedSize(CLASS_NAMES.size());
        cv::Size& ts = cachedSize[d.class_id % cachedSize.size()];
        if (ts.width == 0) {
            char tmpl[64];
            snprintf(tmpl, sizeof(tmpl), "%s 0.00",
                     CLASS_NAMES[d.class_id].c_str());
            ts = cv::getTextSize(tmpl, base, fs, 2, nullptr);
        }
        int ly = std::max(d.bbox.y - 8, ts.height + 4);
        cv::rectangle(frame, cv::Rect(d.bbox.x, ly - ts.height - 4, ts.width + 4, ts.height + 4),
                      cv::Scalar(255, 255, 255), -1);
        cv::putText(frame, label, cv::Point(d.bbox.x + 2, ly),
                    base, fs, color, 2);
    }

    cv::imshow("NUC Vision — Debug", frame);

    // 每帧强制关闭 AUTOSIZE，防止 imshow 把窗口重置为不可调节
    cv::setWindowProperty("NUC Vision — Debug",
                          cv::WND_PROP_AUTOSIZE, 0);
}
