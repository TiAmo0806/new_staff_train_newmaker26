#ifndef VISION_TYPES_H
#define VISION_TYPES_H

#include <opencv2/core.hpp>
#include <string>
#include <vector>

enum class BeanType
{
    Unknown = 0,
    Soybean = 1,          // 黄豆，对应规则中的数字 1 箱
    MungBean = 2,         // 绿豆，对应规则中的数字 2 箱
    WhiteKidneyBean = 3   // 白芸豆，对应规则中的数字 3 箱
};

enum class TargetKind
{
    Unknown = 0,
    Bean = 1,
    DigitBox = 2
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
    int classId = -1;

    // YOLO 置信度。SVM 不提供可靠概率，所以这里只保留 YOLO 分数。
    float score = 0.0f;

    // 检测框，坐标已经从 640x640 输入图还原到原始相机图像。
    cv::Rect box;

    // 显示用标签。若 SVM 复核豆子类别，会被改成 xxx_svm。
    std::string label;

    // 区分这是豆子，还是数字箱。
    TargetKind kind = TargetKind::Unknown;

    // 当 kind == Bean 时有效。
    BeanType bean = BeanType::Unknown;

    // 当 kind == DigitBox 时有效，范围 1~5。
    int digit = 0;
};

struct VisionDecision
{
    // true 表示视觉已经找到稳定目标，可以考虑发给电控。
    bool valid = false;

    // 当前正在处理的豆子类别。
    BeanType bean = BeanType::Unknown;

    // 根据规则推导出的目标箱数字。
    int targetDigit = 0;

    // 目标数字箱中心点，单位是像素。
    cv::Point2f targetCenter{0.0f, 0.0f};

    // 目标中心相对图像中心的水平偏差，后续可换算成 yaw。
    float yawErrorPixel = 0.0f;

    // 调试说明，显示“未识别到豆子/未找到目标数字箱/目标稳定”等。
    std::string reason;
};

inline bool isBeanClass(int classId)
{
    return classId >= 0 && classId <= 2;
}

inline bool isDigitClass(int classId)
{
    return classId >= 3 && classId <= 7;
}

std::string beanToString(BeanType bean);
BeanType classIdToBean(int classId);
int classIdToDigit(int classId);
int targetDigitForBean(BeanType bean);

#endif // VISION_TYPES_H
