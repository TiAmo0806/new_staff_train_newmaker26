#ifndef VISION_TYPES_H
#define VISION_TYPES_H

#include <opencv2/core.hpp>
#include <string>
#include <vector>

enum class BeanType
{
    Unknown = 0,          // 未识别/不确定
    Soybean = 1,          // 黄豆，对应规则中的数字 1 箱
    MungBean = 2,         // 绿豆，对应规则中的数字 2 箱
    WhiteKidneyBean = 3   // 白芸豆，对应规则中的数字 3 箱
};

enum class TargetKind
{
    Unknown = 0,    // 未知目标类型
    Bean = 1,       // 豆子检测框
    DigitBox = 2    // 数字箱检测框
};

struct Detection
{
    // YOLO 原始类别编号。
    // 当前约定：
    //   0 soybean
    //   1 mung_bean
    //   2 white_kidney_bean
    //   3 data_1
    //   4 data_2
    //   5 data_3
    //   6 data_4
    //   7 data_5
    int classId = -1;               // YOLO 输出的原始类别编号，-1 表示无效

    // YOLO最终置信度。
    float score = 0.0f;             // 置信度分数，范围 0~1

    // 检测框，坐标已经从 640x640 输入图还原到原始相机图像。
    cv::Rect box;                   // 检测框在原图上的像素坐标

    // 显示用YOLO类别标签。
    std::string label;              // 显示用标签，如 "soybean"

    // 区分这是豆子，还是数字箱。
    TargetKind kind = TargetKind::Unknown;  // 目标种类：豆子/数字箱/未知

    // 当 kind == Bean 时有效。
    BeanType bean = BeanType::Unknown;      // 豆子具体类别，仅 kind==Bean 时有效

    // 当 kind == DigitBox 时有效，范围 1~5。
    int digit = 0;                          // 数字箱上的数字，仅 kind==DigitBox 时有效
};

struct VisionDecision
{
    // true 表示视觉已经找到稳定目标，可以考虑发给电控。
    bool valid = false;                     // 决策是否有效（连续稳定帧数达标）

    // 当前正在处理的豆子类别。
    BeanType bean = BeanType::Unknown;      // 当前帧识别到的豆子类别

    // 根据规则推导出的目标箱数字。
    int targetDigit = 0;                    // 豆子应去往的数字箱编号（1~3）

    // 目标数字箱中心点，单位是像素。
    cv::Point2f targetCenter{0.0f, 0.0f}; // 目标箱中心的像素坐标

    // 目标中心相对图像中心的水平偏差，后续可换算成 yaw。
    float yawErrorPixel = 0.0f;            // 水平像素偏差，正=右偏，负=左偏

    // 调试说明，显示”未识别到豆子/未找到目标数字箱/目标稳定”等。
    std::string reason;                     // 决策状态描述，用于调试画面显示
};

// 判断 YOLO 类别编号是否为豆子（0~2）
inline bool isBeanClass(int classId)
{
    return classId >= 0 && classId <= 2;
}

// 判断 YOLO 类别编号是否为数字箱（3~7）
inline bool isDigitClass(int classId)
{
    return classId >= 3 && classId <= 7;
}

// 豆子枚举转字符串，仅用于日志和调试显示
std::string beanToString(BeanType bean);
// YOLO 类别编号 -> BeanType 枚举
BeanType classIdToBean(int classId);
// YOLO 类别编号 -> 数字（3->1, 4->2, ... 7->5）
int classIdToDigit(int classId);
// 比赛规则映射：黄豆->1, 绿豆->2, 白芸豆->3
int targetDigitForBean(BeanType bean);

#endif // VISION_TYPES_H
