#include "ImgProcessing/VisionTypes.h"

std::string beanToString(BeanType bean)
{
    switch (bean)
    {
    case BeanType::Soybean: return "soybean";
    case BeanType::MungBean: return "mung_bean";
    case BeanType::WhiteKidneyBean: return "white_kidney_bean";
    default: return "unknown";
    }
}

BeanType classIdToBean(int classId)
{
    if (classId == 0) return BeanType::Soybean;
    if (classId == 1) return BeanType::MungBean;
    if (classId == 2) return BeanType::WhiteKidneyBean;
    return BeanType::Unknown;
}

int classIdToDigit(int classId)
{
    if (classId >= 3 && classId <= 7) return classId - 2;
    return 0;
}

int targetDigitForBean(BeanType bean)
{
    if (bean == BeanType::Soybean) return 1;
    if (bean == BeanType::MungBean) return 2;
    if (bean == BeanType::WhiteKidneyBean) return 3;
    return 0;
}
