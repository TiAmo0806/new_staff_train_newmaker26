/**
 * Visualization.hpp —— 检测结果绘制与统计面板
 *
 * 参照 cameradetect 的可视化模块设计，为 robot_vision 提供统一的绘制接口。
 * 包含目标框绘制、标签渲染（带填充背景）和性能统计面板。
 */

#ifndef ROBOT_VISION_VISUALIZATION_HPP_
#define ROBOT_VISION_VISUALIZATION_HPP_

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>

#include "RobotVision.hpp"

// ============================================================
//  显示信息结构体（供 VisionController 的 drawDebugInfo 使用）
// ============================================================
struct BeanBoxDisplayInfo {
    int         class_id;
    int         position;       // -1 表示未映射
    float       confidence;
    cv::Rect    bbox;
    std::string label;
    bool        is_target;      // 是否为当前目标
};

struct NumberBoxDisplayInfo {
    int         box_number;
    int         position;       // -1 表示未映射
    float       confidence;
    cv::Rect    bbox;
    std::string label;
    bool        is_target;      // 是否为当前目标
};

// ============================================================
//  绘制配置常量（可在此调整视觉风格）
// ============================================================
namespace VizConfig {
    // 字体
    constexpr int    FONT             = cv::FONT_HERSHEY_SIMPLEX;
    constexpr double FONT_SCALE       = 0.45;
    constexpr int    FONT_THICKNESS   = 2;

    // 边框粗细
    constexpr int    BOX_THICKNESS    = 2;

    // 类别颜色（BGR 格式）—— 定义在 Visualization.cpp
    //   豆子                    → 绿色
    //   目标数字 (data_1~3)     → 蓝色
    //   忽略数字 (data_4~5)     → 红色
    //   当前目标豆子            → 黄色
    extern const cv::Scalar COLOR_BEAN;
    extern const cv::Scalar COLOR_TARGET;
    extern const cv::Scalar COLOR_IGNORE;
    extern const cv::Scalar COLOR_TARGET_BEAN;

    // 统计面板位置与样式
    constexpr int    PANEL_X          = 10;
    constexpr int    PANEL_START_Y    = 28;
    constexpr int    PANEL_LINE_H     = 28;
    constexpr double PANEL_FONT_SCALE = 0.7;
    constexpr int    PANEL_THICKNESS  = 2;
    constexpr int    PANEL_ROWS       = 5;  // 最大行数，用于背景半透明遮罩
}

// ============================================================
//  在 frame 上绘制所有检测框 + 类别标签 + 置信度
//  每个框包含：
//    - 彩色矩形边框
//    - 带填充背景的标签（类别名 + 置信度）
//    - 豆子绿色、目标数字蓝色、忽略数字红色
// ============================================================
void drawDetections(cv::Mat& frame, const RobotVision::ClassificationResult& result);

// ============================================================
//  在 frame 左上角绘制统计面板
//  显示状态名称、豆子/数字检测数量、FPS、延迟
// ============================================================
void drawStats(cv::Mat& frame, const std::string& stateName,
               int beanCount, int digitCount,
               double fps, double latencyMs);

// ============================================================
//  在 frame 上绘制带位置编号和目标标记的检测框
//  供 VisionController 在调试显示中使用
// ============================================================
void drawDetectionBoxes(cv::Mat& frame,
                        const std::vector<BeanBoxDisplayInfo>& beanBoxes,
                        const std::vector<NumberBoxDisplayInfo>& numberBoxes);

#endif  // ROBOT_VISION_VISUALIZATION_HPP_
