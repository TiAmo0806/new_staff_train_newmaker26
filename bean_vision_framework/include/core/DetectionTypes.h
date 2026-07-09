#pragma once

#include <opencv2/core.hpp>

#include <string>

// Detection 是检测器输出的“原始结果”。
// 它只描述模型看到了什么，不关心这个目标属于 P1 还是 L5。
struct Detection {
    int class_id = -1;              // 模型输出的类别编号。
    std::string class_name;         // 归一化后的类别名，例如 soybean 或 digit_1。
    float confidence = 0.0f;        // 置信度，范围通常是 0.0 到 1.0。
    cv::Rect box;                   // 检测框，单位是图像像素。

    /**
     * @brief 计算检测框中心点。
     * @return 检测框中心点，单位为图像像素。
     */
    cv::Point center() const;
};
