/**
 * packet.hpp —— 串口数据包定义
 * 使用 bit-field 简化读写，static_assert 保证跨平台布局一致
 */

#ifndef PATH_SERIAL_DRIVER__PACKET_HPP_
#define PATH_SERIAL_DRIVER__PACKET_HPP_

#include <algorithm>
#include <cstdint>
#include <vector>

namespace path_serial_driver
{

struct VisionSendPacket
{
    uint8_t  header   = 0x5A;  // 帧头

    uint16_t first_cmd      : 2;   // bit 0-1  (0=直行, 1=左转, 2=右转)
    uint16_t second_cmd     : 2;   // bit 2-3  (1=左分支, 2=中分支, 3=右分支)
    uint16_t turn_strength  : 7;   // bit 4-10 (0~120)
    uint16_t reserved       : 5;   // bit 11-15

    uint16_t checksum = 0;        // CRC16
} __attribute__((packed));

// 编译时校验布局 —— 错位则编译不通过
static_assert(sizeof(VisionSendPacket) == 5,
              "VisionSendPacket size mismatch — should be exactly 5 bytes");
static_assert(offsetof(VisionSendPacket, checksum) == 3,
              "VisionSendPacket checksum offset wrong");

/// 构造数据包
inline VisionSendPacket makePacket(uint8_t first_cmd,
                                   uint8_t second_cmd,
                                   uint8_t turn_strength)
{
    VisionSendPacket p;
    p.header         = 0x5A;
    p.first_cmd      = first_cmd;
    p.second_cmd     = second_cmd;
    p.turn_strength  = turn_strength;
    p.reserved       = 0;
    p.checksum       = 0;
    return p;
}

/// 序列化：结构体 → 字节数组
inline std::vector<uint8_t> toVector(const VisionSendPacket& data)
{
    std::vector<uint8_t> packet(sizeof(VisionSendPacket));
    std::copy(
        reinterpret_cast<const uint8_t*>(&data),
        reinterpret_cast<const uint8_t*>(&data) + sizeof(VisionSendPacket),
        packet.begin());
    return packet;
}

/// 反序列化：字节数组 → 结构体
inline VisionSendPacket fromVector(const std::vector<uint8_t>& data)
{
    VisionSendPacket packet;
    std::copy(data.begin(), data.end(), reinterpret_cast<uint8_t*>(&packet));
    return packet;
}

}  // namespace path_serial_driver

#endif
