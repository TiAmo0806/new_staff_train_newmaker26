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
  uint8_t target_type : 2;   // 0=无目标 1=箱子 2=豆子 3=空箱子
  uint8_t target_id : 3;     // 箱子: 1/2/3  豆子: 0/1/2
  uint8_t tracking : 1;      // 是否锁定目标
  uint8_t reserved : 2;
  float pitch_cmd;
  float yaw_cmd;
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

}  // namespace rm_serial_driver

#endif  // RM_SERIAL_DRIVER__PACKET_HPP_