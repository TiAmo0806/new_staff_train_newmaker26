// Copyright (C) 2022 ChenJun
// Copyright (C) 2024 Zheng Yu
// Licensed under the Apache-2.0 License.

#ifndef RM_SERIAL_DRIVER__PACKET_HPP_
#define RM_SERIAL_DRIVER__PACKET_HPP_

#include <algorithm>
#include <cstdint>
#include <vector>

namespace rm_serial_driver
{

// ============================================
// 视觉 → 电控（目标信息）
// ============================================
struct VisionToMCUPacket
{
  uint8_t header = 0xA6;
  // ⭐ 位域顺序修正：GCC LSB-first，协议 MSB-first → 反向声明
  // 协议字节顺序: [target_type(2)][target_id(3)][tracking(1)][reserved(2)] MSB→LSB
  uint8_t reserved : 2;       // 低位
  uint8_t tracking : 1;
  uint8_t target_id : 3;
  uint8_t target_type : 2;    // 高位
  float pitch_cmd = 0.0f;
  float yaw_cmd = 0.0f;
  uint16_t checksum = 0;
} __attribute__((packed));

// ============================================
// ⭐ 新增：电控 → 视觉（动作反馈）
// ============================================
struct MCUToVisionPacket
{
  uint8_t header = 0x5A;     // 固定包头
  // ⭐ 位域顺序修正：协议字节顺序 [feedback_type(3)][reserved(5)] MSB→LSB
  uint8_t reserved : 5;       // 低位
  uint8_t feedback_type : 3;  // 高位
  uint16_t checksum = 0;
} __attribute__((packed));

// ============================================
// 工具函数
// ============================================

inline VisionToMCUPacket fromVector(const std::vector<uint8_t> & data)
{
  VisionToMCUPacket packet;
  std::copy(data.begin(), data.end(), reinterpret_cast<uint8_t *>(&packet));
  return packet;
}

inline std::vector<uint8_t> toVector(const VisionToMCUPacket & data)
{
  std::vector<uint8_t> packet(sizeof(VisionToMCUPacket));
  std::copy(
    reinterpret_cast<const uint8_t *>(&data),
    reinterpret_cast<const uint8_t *>(&data) + sizeof(VisionToMCUPacket),
    packet.begin());
  return packet;
}

// ⭐ 解析电控反馈包（仅取前 sizeof(MCUToVisionPacket) 字节，防止越界）
inline MCUToVisionPacket fromMCUVector(const std::vector<uint8_t> & data)
{
  MCUToVisionPacket packet;
  size_t copy_len = std::min(data.size(), sizeof(MCUToVisionPacket));
  std::copy(data.begin(), data.begin() + copy_len, reinterpret_cast<uint8_t *>(&packet));
  return packet;
}

}  // namespace rm_serial_driver

#endif  // RM_SERIAL_DRIVER__PACKET_HPP_