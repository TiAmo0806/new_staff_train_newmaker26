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

void drawDebug(cv::Mat& frame,
               const std::vector<Detection>& dets,
               const StableTracker& tracker,
               const VisionConfig& cfg,
               const std::vector<cv::Scalar>& colors,
               bool modelOk, bool serialOk, double fps)
{
    // ── 检测框 ──
    for (const auto& d : dets) {
        cv::Scalar color = colors[d.class_id % colors.size()];
        int thick = std::max(3, cfg.line_thickness);
        cv::rectangle(frame, d.bbox, color, thick);

        // 标签（带黑底），textSize 用类名模板缓存，不每帧重复计算
        char label[64];
        snprintf(label, sizeof(label), "%s %.2f (%d,%d %dx%d)",
                 CLASS_NAMES[d.class_id].c_str(), d.confidence,
                 d.bbox.x, d.bbox.y, d.bbox.width, d.bbox.height);
        int base = cv::FONT_HERSHEY_SIMPLEX;
        double fs = std::max(0.5, cfg.font_scale);

        static std::vector<cv::Size> cachedSize(CLASS_NAMES.size());
        cv::Size& ts = cachedSize[d.class_id % cachedSize.size()];
        if (ts.width == 0) {
            char tmpl[64];
            snprintf(tmpl, sizeof(tmpl), "%s 0.00 (0000,0000 0000x0000)",
                     CLASS_NAMES[d.class_id].c_str());
            ts = cv::getTextSize(tmpl, base, fs, 2, nullptr);
        }
        int ly = std::max(d.bbox.y - 8, ts.height + 4);
        cv::rectangle(frame, cv::Rect(d.bbox.x, ly - ts.height - 4, ts.width + 4, ts.height + 4),
                      cv::Scalar(0, 0, 0), -1);
        cv::putText(frame, label, cv::Point(d.bbox.x + 2, ly),
                    base, fs, color, 2);
    }

    // ── 测试标记：画面四角画圆圈，确认绘制有效 ──
    cv::circle(frame, cv::Point(30, 30), 10, cv::Scalar(0, 0, 255), -1);
    cv::circle(frame, cv::Point(frame.cols - 30, 30), 10, cv::Scalar(0, 0, 255), -1);
    cv::circle(frame, cv::Point(30, frame.rows - 30), 10, cv::Scalar(0, 0, 255), -1);
    cv::circle(frame, cv::Point(frame.cols - 30, frame.rows - 30), 10, cv::Scalar(0, 0, 255), -1);

    // ── 顶部状态栏背景 ──
    cv::Rect bar(0, 0, frame.cols, 58);
    cv::rectangle(frame, bar, cv::Scalar(0, 0, 0), -1);
    cv::rectangle(frame, bar, cv::Scalar(80, 80, 80), 1);

    int y = 18;

    // 第一行：型号灯号 + FPS
    std::string yoloIcon = modelOk  ? "🟢 YOLO" : "🔴 YOLO(off)";
    std::string serIcon  = serialOk ? "🟢 串口"  : "🔴 串口(off)";
    char buf[128];
    snprintf(buf, sizeof(buf), "%s  %s  FPS:%.1f", yoloIcon.c_str(), serIcon.c_str(), fps);
    cv::putText(frame, buf, cv::Point(8, y),
                cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(200, 200, 200), 1);

    // 第二行：跟踪状态
    y += 18;
    snprintf(buf, sizeof(buf), "Stable: %s (%d/%d)  Last: %s  Cool: %d/%d",
             tracker.stableName().c_str(),
             tracker.stableCounter(), tracker.threshold(),
             tracker.lastSent().c_str(),
             tracker.cooldownCounter(), tracker.cooldownFrames());
    cv::putText(frame, buf, cv::Point(8, y),
                cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 255, 0), 1);

    // 第三行：检测数量
    y += 18;
    snprintf(buf, sizeof(buf), "Detections: %zu", dets.size());
    cv::putText(frame, buf, cv::Point(8, y),
                cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(180, 180, 180), 1);

    cv::imshow("NUC Vision — Debug", frame);
}
