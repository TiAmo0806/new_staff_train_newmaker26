/**
 * Visualization.cpp —— 检测结果绘制与统计面板实现
 *
 * 参照 cameradetect/visualization.cpp 的设计风格实现。
 */

#include "../include/Visualization.hpp"

#include <cstdio>
#include <string>

// ============================================================
//  颜色常量定义（声明在 Visualization.hpp 中）
// ============================================================
namespace VizConfig {
    const cv::Scalar COLOR_BEAN        = cv::Scalar(0, 255, 0);
    const cv::Scalar COLOR_TARGET      = cv::Scalar(255, 0, 0);
    const cv::Scalar COLOR_IGNORE      = cv::Scalar(0, 0, 255);
    const cv::Scalar COLOR_TARGET_BEAN = cv::Scalar(0, 255, 255);
}

// ============================================================
//  辅助：在 bbox 上方绘制带填充背景的标签
// ============================================================
static void drawLabel(cv::Mat& frame, const cv::Rect& bbox,
                      const std::string& text, const cv::Scalar& color)
{
    int baseline = 0;
    cv::Size textSz = cv::getTextSize(text, VizConfig::FONT,
                                      VizConfig::FONT_SCALE,
                                      VizConfig::FONT_THICKNESS, &baseline);

    // 标签背景矩形（默认在框上方，如果超出画面则移到框内下方）
    cv::Point labelTopLeft(bbox.x, bbox.y - textSz.height - 6);
    cv::Point labelBotRight(bbox.x + textSz.width + 4, bbox.y);

    if (labelTopLeft.y < 0) {
        // 画面顶端放不下 → 移到框内下方
        labelTopLeft.y = bbox.y + bbox.height - textSz.height - 6;
        labelBotRight.y = bbox.y + bbox.height;
        if (labelTopLeft.y < 0) return;  // 实在放不下就跳过
    }

    // 填充标签背景
    cv::rectangle(frame, labelTopLeft, labelBotRight, color, cv::FILLED);

    // 白色文字
    cv::putText(frame, text,
                cv::Point(bbox.x + 2, labelBotRight.y - baseline - 2),
                VizConfig::FONT, VizConfig::FONT_SCALE,
                cv::Scalar(255, 255, 255),
                VizConfig::FONT_THICKNESS, cv::LINE_AA);
}

// ============================================================
//  drawDetections —— 绘制所有 YOLO 检测结果
// ============================================================
void drawDetections(cv::Mat& frame, const RobotVision::ClassificationResult& result)
{
    // —— 绘制豆子（绿色） ——
    for (const auto& det : result.beans) {
        cv::rectangle(frame, det.bbox, VizConfig::COLOR_BEAN, VizConfig::BOX_THICKNESS);

        char label[128];
        snprintf(label, sizeof(label), "%s %.0f%%",
                 det.class_name.c_str(), det.confidence * 100.0f);
        drawLabel(frame, det.bbox, label, VizConfig::COLOR_BEAN);
    }

    // —— 绘制目标数字 data_1~data_3（蓝色） ——
    for (const auto& det : result.target_digits) {
        cv::rectangle(frame, det.bbox, VizConfig::COLOR_TARGET, VizConfig::BOX_THICKNESS);

        char label[128];
        snprintf(label, sizeof(label), "data_%d %.0f%%",
                 det.class_id - 2, det.confidence * 100.0f);  // class_id 3→data_1
        drawLabel(frame, det.bbox, label, VizConfig::COLOR_TARGET);
    }

    // —— 绘制忽略数字 data_4~data_5（红色） ——
    for (const auto& det : result.ignore_digits) {
        cv::rectangle(frame, det.bbox, VizConfig::COLOR_IGNORE, VizConfig::BOX_THICKNESS);

        char label[128];
        snprintf(label, sizeof(label), "IGNORE data_%d %.0f%%",
                 det.class_id - 2, det.confidence * 100.0f);
        drawLabel(frame, det.bbox, label, VizConfig::COLOR_IGNORE);
    }
}

// ============================================================
//  drawStats —— 在左上角绘制统计面板
// ============================================================
void drawStats(cv::Mat& frame, const std::string& stateName,
               int beanCount, int digitCount,
               double fps, double latencyMs)
{
    char buf[256];
    int y = VizConfig::PANEL_START_Y;

    // 为面板绘制半透明黑色背景
    cv::Rect panelRect(0, 0, 280, VizConfig::PANEL_LINE_H * VizConfig::PANEL_ROWS + 10);
    cv::rectangle(frame, panelRect, cv::Scalar(0, 0, 0), cv::FILLED);

    // 每行文字
    auto putLine = [&](const std::string& text, const cv::Scalar& color) {
        cv::putText(frame, text, cv::Point(VizConfig::PANEL_X, y),
                    VizConfig::FONT, VizConfig::PANEL_FONT_SCALE,
                    color, VizConfig::PANEL_THICKNESS, cv::LINE_AA);
        y += VizConfig::PANEL_LINE_H;
    };

    // 状态（黄色高亮）
    snprintf(buf, sizeof(buf), "State: %s", stateName.c_str());
    putLine(buf, cv::Scalar(0, 255, 255));

    // 检测数量
    snprintf(buf, sizeof(buf), "Beans : %d", beanCount);
    putLine(buf, VizConfig::COLOR_BEAN);

    snprintf(buf, sizeof(buf), "Digits: %d", digitCount);
    putLine(buf, VizConfig::COLOR_TARGET);

    // 性能指标
    snprintf(buf, sizeof(buf), "FPS   : %.1f", fps);
    putLine(buf, cv::Scalar(255, 255, 255));

    snprintf(buf, sizeof(buf), "Latency: %.1f ms", latencyMs);
    putLine(buf, cv::Scalar(200, 200, 200));
}

// ============================================================
//  drawDetectionBoxes —— 绘制带位置信息的检测框
//  (供 VisionController 调试显示使用)
// ============================================================
void drawDetectionBoxes(cv::Mat& frame,
                        const std::vector<BeanBoxDisplayInfo>& beanBoxes,
                        const std::vector<NumberBoxDisplayInfo>& numberBoxes)
{
    // —— 绘制豆子箱子 ——
    for (const auto& box : beanBoxes) {
        cv::Scalar color = box.is_target ? VizConfig::COLOR_TARGET_BEAN
                                         : VizConfig::COLOR_BEAN;

        cv::rectangle(frame, box.bbox, color, VizConfig::BOX_THICKNESS);

        // 主标签：名称 + 置信度 + 目标标记
        std::string label = box.label;
        char buf[64];
        snprintf(buf, sizeof(buf), " %.0f%%", box.confidence * 100.0f);
        label += buf;
        if (box.is_target) label += " ★";

        drawLabel(frame, box.bbox, label, color);

        // 位置编号（在框下方）
        if (box.position >= 0) {
            char posBuf[32];
            snprintf(posBuf, sizeof(posBuf), "pos:%d", box.position);
            cv::putText(frame, posBuf,
                        cv::Point(box.bbox.x, box.bbox.y + box.bbox.height + 15),
                        VizConfig::FONT, 0.4, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
        }
    }

    // —— 绘制数字箱子 ——
    for (const auto& box : numberBoxes) {
        cv::Scalar color = box.is_target ? cv::Scalar(255, 0, 255)    // 品红（目标）
                       : (box.box_number >= 4) ? cv::Scalar(128, 128, 128) // 灰色（忽略）
                       : VizConfig::COLOR_TARGET;                             // 蓝色（普通）

        cv::rectangle(frame, box.bbox, color, VizConfig::BOX_THICKNESS);

        std::string label = box.label;
        char buf[64];
        snprintf(buf, sizeof(buf), " %.0f%%", box.confidence * 100.0f);
        label += buf;
        if (box.is_target) label += " ★";

        drawLabel(frame, box.bbox, label, color);

        if (box.position >= 0) {
            char posBuf[32];
            snprintf(posBuf, sizeof(posBuf), "pos:%d", box.position);
            cv::putText(frame, posBuf,
                        cv::Point(box.bbox.x, box.bbox.y + box.bbox.height + 15),
                        VizConfig::FONT, 0.4, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
        }
    }
}
