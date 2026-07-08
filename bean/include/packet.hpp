/**
 * packet.hpp —— 串口数据包定义
 * 使用显式 uint16_t + 位运算代替 bit-field，确保跨平台字节布局一致
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
    // cmd_word 位布局 (little-endian):
    //   bit0-1  = first_cmd       (0=直行, 1=左转, 2=右转)
    //   bit2-3  = second_cmd      (1=左分支, 2=中分支, 3=右分支)
    //   bit4-10 = turn_strength   (0~120)
    uint16_t cmd_word = 0;
    uint16_t checksum = 0;      // CRC16
} __attribute__((packed));

static_assert(sizeof(VisionSendPacket) == 5,
              "VisionSendPacket size mismatch — should be exactly 5 bytes");

/// 构建数据包（checksum 初始为 0，调用方自行填充 CRC）
inline VisionSendPacket makePacket(uint8_t first_cmd,
                                   uint8_t second_cmd,
                                   uint8_t turn_strength)
{
    VisionSendPacket p;
    p.header   = 0x5A;
    p.cmd_word = (first_cmd & 0x03)
               | ((second_cmd & 0x03) << 2)
               | ((turn_strength & 0x7F) << 4);
    p.checksum = 0;
    return p;
}

/// 序列化：结构体 → 字节数组（NUC 发送用）
inline std::vector<uint8_t> toVector(const VisionSendPacket& data)
{
    std::vector<uint8_t> packet(sizeof(VisionSendPacket));
    std::copy(
        reinterpret_cast<const uint8_t*>(&data),
        reinterpret_cast<const uint8_t*>(&data) + sizeof(VisionSendPacket),
        packet.begin());
    return packet;
}

/// 反序列化：字节数组 → 结构体（单片机接收用）
inline VisionSendPacket fromVector(const std::vector<uint8_t>& data)
{
    VisionSendPacket packet;
    std::copy(data.begin(), data.end(), reinterpret_cast<uint8_t*>(&packet));
    return packet;
}

// 解包：从 VisionSendPacket / 字节数组提取字段 

/// 从结构体提取 first_cmd（0=直行, 1=左转, 2=右转）
inline uint8_t getFirstCmd(const VisionSendPacket& p) {
    return p.cmd_word & 0x03;
}

/// 从结构体提取 second_cmd（1=左分支, 2=中分支, 3=右分支）
inline uint8_t getSecondCmd(const VisionSendPacket& p) {
    return (p.cmd_word >> 2) & 0x03;
}

/// 从结构体提取 turn_strength（0~120）
inline uint8_t getTurnStrength(const VisionSendPacket& p) {
    return (p.cmd_word >> 4) & 0x7F;
}

/// 一键解包：从字节数组直接提取三个字段（跳过 header + CRC）
inline void unpack(const std::vector<uint8_t>& data,
                   uint8_t& first, uint8_t& second, uint8_t& strength)
{
    auto p = fromVector(data);
    first    = getFirstCmd(p);
    second   = getSecondCmd(p);
    strength = getTurnStrength(p);
}

}  // namespace path_serial_driver

#endif
