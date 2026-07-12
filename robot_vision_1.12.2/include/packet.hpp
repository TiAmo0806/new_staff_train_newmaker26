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
  uint8_t target_type : 2;   // 0=无目标 1=取豆区箱子(位置) 2=放置区箱子(位置)
  uint8_t target_id : 3;     // 位置编号
  uint8_t tracking : 1;      // 是否锁定目标
  uint8_t reserved : 2;
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
  uint8_t feedback_type : 3; // 1=抓取完成 2=放置完成 3=移动完成 4=动作失败
  uint8_t reserved : 5;
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

// ⭐ 新增：解析电控反馈包
inline MCUToVisionPacket fromMCUVector(const std::vector<uint8_t> & data)
{
  MCUToVisionPacket packet;
  std::copy(data.begin(), data.end(), reinterpret_cast<uint8_t *>(&packet));
  return packet;
}

}  // namespace rm_serial_driver

#endif  // RM_SERIAL_DRIVER__PACKET_HPP_