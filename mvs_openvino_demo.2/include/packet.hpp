// Copyright (C) 2024
// Licensed under the Apache-2.0 License.

#ifndef MVS_OPENVINO_DEMO__PACKET_HPP_
#define MVS_OPENVINO_DEMO__PACKET_HPP_

#include "CRC16.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

// ============================================================
// 类别划分常量
// ============================================================

/// 豆子类别 ID: soybean(0), mung_bean(1), white_kidney_bean(2)
constexpr std::array<int, 3> ALL_BEAN_CLASSES = {0, 1, 2};

/// 数字类别 ID: data_1(3) ~ data_5(7)
constexpr std::array<int, 5> ALL_DIGIT_CLASSES = {3, 4, 5, 6, 7};

/// 判断 class_id 是否为豆子
inline bool isBeanClass(int id) {
    for (int b : ALL_BEAN_CLASSES) if (id == b) return true;
    return false;
}

/// 判断 class_id 是否为数字
inline bool isDigitClass(int id) {
    for (int d : ALL_DIGIT_CLASSES) if (id == d) return true;
    return false;
}

// ============================================================
// 数字包（5 个数字 + CRC16，固定 8 字节）
//
// 帧格式（小端序）：
//   [header 0xA5] [d0] [d1] [d2] [d3] [d4] [CRC_L] [CRC_H]
//
// d0~d3: 实际检测到的数字（按 X 坐标从左到右排序）
// d4:    推理补全的缺失数字
// CRC16: 对前 6 字节（header + 5 digits）计算
// ============================================================

struct DigitPacket
{
    uint8_t header = 0xA5;
    uint8_t digits[5] = {0};
} __attribute__((packed));

/// 将 DigitPacket 序列化为固定 8 字节（自动附加 CRC16）
inline std::vector<uint8_t> toVector(DigitPacket& packet)
{
    constexpr size_t DATA_LEN  = 6;   // header(1) + digits[5](5)
    constexpr size_t TOTAL_LEN = 8;   // DATA_LEN + CRC16(2)

    uint16_t crc = crc16::Get_CRC16_Check_Sum(
        reinterpret_cast<uint8_t*>(&packet), DATA_LEN, 0xFFFF);

    std::vector<uint8_t> data(TOTAL_LEN);
    std::copy(
        reinterpret_cast<const uint8_t*>(&packet),
        reinterpret_cast<const uint8_t*>(&packet) + DATA_LEN,
        data.begin());
    data[TOTAL_LEN - 2] = static_cast<uint8_t>(crc & 0x00FF);         // CRC 低字节
    data[TOTAL_LEN - 1] = static_cast<uint8_t>((crc >> 8) & 0x00FF);  // CRC 高字节

    return data;
}

// ============================================================
// 豆子包（3 个豆子 + CRC16，固定 6 字节）
//
// 帧格式（小端序）：
//   [header 0xA5] [b0] [b1] [b2] [CRC_L] [CRC_H]
//
// b0~b1: 实际检测到的豆子（按 X 坐标从左到右排序）
// b2:    推理补全的缺失豆子
// CRC16: 对前 4 字节（header + 3 beans）计算
// ============================================================

struct BeanPacket
{
    uint8_t header = 0xA5;
    uint8_t beans[3] = {0};
} __attribute__((packed));

/// 将 BeanPacket 序列化为固定 6 字节（自动附加 CRC16）
inline std::vector<uint8_t> toVector(BeanPacket& packet)
{
    constexpr size_t DATA_LEN  = 4;   // header(1) + beans[3](3)
    constexpr size_t TOTAL_LEN = 6;   // DATA_LEN + CRC16(2)

    uint16_t crc = crc16::Get_CRC16_Check_Sum(
        reinterpret_cast<uint8_t*>(&packet), DATA_LEN, 0xFFFF);

    std::vector<uint8_t> data(TOTAL_LEN);
    std::copy(
        reinterpret_cast<const uint8_t*>(&packet),
        reinterpret_cast<const uint8_t*>(&packet) + DATA_LEN,
        data.begin());
    data[TOTAL_LEN - 2] = static_cast<uint8_t>(crc & 0x00FF);         // CRC 低字节
    data[TOTAL_LEN - 1] = static_cast<uint8_t>((crc >> 8) & 0x00FF);  // CRC 高字节

    return data;
}

#endif  // MVS_OPENVINO_DEMO__PACKET_HPP_
