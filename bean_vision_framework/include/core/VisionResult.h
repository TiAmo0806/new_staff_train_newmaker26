#pragma once

#include <opencv2/core.hpp>

#include <cstdint>
#include <string>

// 豆子类别的业务枚举。当前主要用于表达含义，任务包里仍使用 0/1/2 编码。
enum class BeanClass : uint8_t {
    Soybean = 0,
    MungBean = 1,
    WhiteKidneyBean = 2,
    Unknown = 3
};

// 数字类别的业务枚举。Unknown 表示这个位置没有识别出有效数字。
enum class DigitClass : uint8_t {
    Unknown = 0,
    Digit1 = 1,
    Digit2 = 2,
    Digit3 = 3,
    Digit4 = 4,
    Digit5 = 5
};

// 一个固定位置的识别结果，例如 P1 是 soybean，或者 L6 是 digit_1。
struct PositionResult {
    bool valid = false;             // 这个固定位置是否识别到了目标。
    std::string position_id;        // 固定位置编号：P1/P2/P3/L4/L5/L6/L7/L8。
    std::string class_name;         // 归一化后的类别名。
    int class_id = -1;              // 原始检测类别编号。
    float confidence = 0.0f;        // 被选中检测框的置信度。
    cv::Rect box;                   // 被选中检测框，单位是图像像素。
    cv::Point center_px;            // 检测框中心点，单位是图像像素。
    cv::Point offset_px;            // 检测框中心相对 ROI 中心的偏移，单位是像素。
};

// VisionResult 是 ROI 解析后的结果。
// 它已经从“很多检测框”变成了“每个固定位置识别到了什么”。
struct VisionResult {
    bool success = false;           // 是否至少识别到了任务生成所需的关键位置。
    std::string reason;             // 失败原因，方便调试和后续上报。

    PositionResult p1;
    PositionResult p2;
    PositionResult p3;

    PositionResult l4;
    PositionResult l5;
    PositionResult l6;
    PositionResult l7;
    PositionResult l8;
};
