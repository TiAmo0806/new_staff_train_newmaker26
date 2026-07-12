#include "ImgProcessing/VisionTypes.h"

std::string beanToString(BeanType bean)
{
    switch (bean)
    {
    case BeanType::Soybean: return "soybean";               // 黄豆
    case BeanType::MungBean: return "mung_bean";            // 绿豆
    case BeanType::WhiteKidneyBean: return "white_kidney_bean"; // 白芸豆
    default: return "unknown";                              // 未知类型
    }
}

BeanType classIdToBean(int classId)
{
    if (classId == 0) return BeanType::Soybean;             // YOLO class 0 -> 黄豆
    if (classId == 1) return BeanType::MungBean;            // YOLO class 1 -> 绿豆
    if (classId == 2) return BeanType::WhiteKidneyBean;     // YOLO class 2 -> 白芸豆
    return BeanType::Unknown;                                // 非豆子类别
}

int classIdToDigit(int classId)
{
    if (classId >= 3 && classId <= 7) return classId - 2;  // class 3->1, 4->2, ..., 7->5
    return 0;                                                // 非数字箱类别
}

int targetDigitForBean(BeanType bean)
{
    if (bean == BeanType::Soybean) return 1;                // 黄豆 -> 数字 1 箱
    if (bean == BeanType::MungBean) return 2;               // 绿豆 -> 数字 2 箱
    if (bean == BeanType::WhiteKidneyBean) return 3;        // 白芸豆 -> 数字 3 箱
    return 0;                                                // 未知豆子无对应箱
}
