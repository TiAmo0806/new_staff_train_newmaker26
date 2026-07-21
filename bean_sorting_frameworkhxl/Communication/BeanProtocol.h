#ifndef BEAN_SORTING_PROTOCOL_H_
#define BEAN_SORTING_PROTOCOL_H_

#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include <algorithm>
#include "CRC16.hpp"

namespace bean_sorting {

constexpr uint8_t  HEADER_PACKET   = 0xA5;   // 统一帧头 (数字包+豆子包共用)

// ---- 类别常量 ----
constexpr std::array<int, 3> ALL_BEAN_CLASSES  = {0, 1, 2};
constexpr std::array<int, 5> ALL_DIGIT_CLASSES = {3, 4, 5, 6, 7};

inline bool isBeanClass(int id)  { for (int b : ALL_BEAN_CLASSES)  if (id == b) return true; return false; }
inline bool isDigitClass(int id) { for (int d : ALL_DIGIT_CLASSES) if (id == d) return true; return false; }

enum class BeanType : uint8_t {
    SOYBEAN     = 0,
    MUNG_BEAN   = 1,
    KIDNEY_BEAN = 2,
    DATA_1      = 3,
    DATA_2      = 4,
    DATA_3      = 5,
    DATA_4      = 6,
    DATA_5      = 7,
    UNKNOWN     = 8,
    ERROR       = 9,
};

inline const char* bean_type_name(BeanType t) {
    switch (t) {
        case BeanType::SOYBEAN:     return "黄豆";
        case BeanType::MUNG_BEAN:   return "绿豆";
        case BeanType::KIDNEY_BEAN: return "白芸豆";
        case BeanType::DATA_1:      return "1";
        case BeanType::DATA_2:      return "2";
        case BeanType::DATA_3:      return "3";
        case BeanType::DATA_4:      return "4";
        case BeanType::DATA_5:      return "5";
        case BeanType::ERROR:       return "错误";
        default:                    return "未知";
    }
}

// ============================================================
//  数字包: 0xA5 + 5 digits + CRC16(2) = 固定 8 字节
//  d0~d4: class_id(3-7), 按 X 坐标排序, 最后一个是推理补全的缺失数字
// ============================================================
struct DigitPacket {
    uint8_t header = HEADER_PACKET;   // [0] 0xA5
    uint8_t digits[5] = {0};           // [1]~[5] 5个数字class_id
} __attribute__((packed));

inline std::vector<uint8_t> toVector(DigitPacket& packet) {
    constexpr size_t DATA_LEN  = 6;    // header(1) + digits(5)
    constexpr size_t TOTAL_LEN = 8;    // + CRC16(2)

    uint16_t crc = crc16::Get_CRC16_Check_Sum(
        reinterpret_cast<uint8_t*>(&packet), DATA_LEN, 0xFFFF);

    std::vector<uint8_t> data(TOTAL_LEN);
    std::copy(
        reinterpret_cast<const uint8_t*>(&packet),
        reinterpret_cast<const uint8_t*>(&packet) + DATA_LEN,
        data.begin());
    data[TOTAL_LEN - 2] = static_cast<uint8_t>(crc & 0x00FF);
    data[TOTAL_LEN - 1] = static_cast<uint8_t>((crc >> 8) & 0x00FF);
    return data;
}

// ============================================================
//  豆子包: 0xA5 + 3 beans + CRC16(2) = 固定 6 字节
//  b0~b2: class_id(0-2), 按 X 坐标排序, 最后一个是推理补全的缺失豆子
// ============================================================
struct BeanPacket {
    uint8_t header = HEADER_PACKET;   // [0] 0xA5
    uint8_t beans[3] = {0};           // [1]~[3] 3个豆子class_id
} __attribute__((packed));

inline std::vector<uint8_t> toVector(BeanPacket& packet) {
    constexpr size_t DATA_LEN  = 4;    // header(1) + beans(3)
    constexpr size_t TOTAL_LEN = 6;    // + CRC16(2)

    uint16_t crc = crc16::Get_CRC16_Check_Sum(
        reinterpret_cast<uint8_t*>(&packet), DATA_LEN, 0xFFFF);

    std::vector<uint8_t> data(TOTAL_LEN);
    std::copy(
        reinterpret_cast<const uint8_t*>(&packet),
        reinterpret_cast<const uint8_t*>(&packet) + DATA_LEN,
        data.begin());
    data[TOTAL_LEN - 2] = static_cast<uint8_t>(crc & 0x00FF);
    data[TOTAL_LEN - 1] = static_cast<uint8_t>((crc >> 8) & 0x00FF);
    return data;
}

}  // namespace bean_sorting

#endif
