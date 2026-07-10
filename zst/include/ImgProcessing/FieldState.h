#ifndef FIELD_STATE_H
#define FIELD_STATE_H

#include "ImgProcessing/VisionTypes.h"
#include <array>
#include <cstdint>

// 保存整场识别结果。
// 这个结构体不直接做识别，只负责“存结果”。
// 豆子和数字箱可以分多次、分多个角度识别，最终都汇总到这里。
// 等 beanReady 和 boxReady 都为 true 后，就可以一次性发送给电控。
struct FieldState
{
    // beanPlaces[0] -> bean_place_1
    // beanPlaces[1] -> bean_place_2
    // beanPlaces[2] -> bean_place_3
    std::array<BeanType, 3> beanPlaces{
        BeanType::Unknown,
        BeanType::Unknown,
        BeanType::Unknown
    };

    // boxPlaces[0] -> box_place_a
    // boxPlaces[1] -> box_place_b
    // boxPlaces[2] -> box_place_c
    // boxPlaces[3] -> box_place_d
    // boxPlaces[4] -> box_place_e
    //
    // 数字范围是 1~5，0 表示未知。
    // 这里存的是“这个固定箱位上识别到的数字”，不是箱位编号本身。
    // 例如 boxPlaces[0] = 4，表示 box_place_a 位置上放的是数字 4 箱。
    std::array<int, 5> boxPlaces{0, 0, 0, 0, 0};

    // 三个豆子位置是否都已经识别完成。
    bool beanReady = false;

    // 五个箱子位置是否都已经识别完成。
    bool boxReady = false;

    // 整场是否识别完成。
    // 只有豆子区和箱子区都完成，才允许发送 valid=1 的结果。
    bool valid() const
    {
        return beanReady && boxReady;
    }
};

// 串口编码约定：
// 0 未知，1 黄豆，2 绿豆，3 白芸豆。
inline uint8_t encodeBeanType(BeanType bean)
{
    if (bean == BeanType::Soybean) return 1;
    if (bean == BeanType::MungBean) return 2;
    if (bean == BeanType::WhiteKidneyBean) return 3;
    return 0;
}

#endif // FIELD_STATE_H
