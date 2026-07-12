// Copyright (C) 2024
// Licensed under the Apache-2.0 License.

#ifndef MVS_OPENVINO_DEMO__PACKET_HPP_
#define MVS_OPENVINO_DEMO__PACKET_HPP_

#include "CRC16.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

/**
 * @brief 检测排序结果发送帧
 *
 * 帧格式（小端序）：
 *   [header 0xA5] [count 1B] [classId0..classIdN] [CRC16 2B]
 *
 * 使用方式：
 *   SendPacket packet;
 *   packet.count = 3;
 *   packet.class_ids[0] = 1;
 *   packet.class_ids[1] = 3;
 *   packet.class_ids[2] = 4;
 *   auto data = toVector(packet);  // 自动计算 CRC16
 */
struct SendPacket
{
  uint8_t header = 0xA5;
  uint8_t count = 0;
  uint8_t class_ids[10] = {0};   // 最多 10 个检测物品
} __attribute__((packed));

/**
 * @brief 将 SendPacket 序列化为字节 vector（自动附加 CRC16）
 *
 * @param packet  已填充的 SendPacket（header、count、class_ids）
 * @return        可直接通过串口发送的字节序列
 *
 * 内部流程：
 *   1. CRC16 校验：对 header + count + class_ids[0..count-1] 计算校验码
 *   2. 拼接输出：数据 + CRC16（2 字节，小端序）
 */
inline std::vector<uint8_t> toVector(SendPacket & packet)
{
  // CRC 计算范围：header(1) + count(1) + 实际 class_ids(count)
  size_t data_len = 2 + packet.count;
  uint16_t crc = crc16::Get_CRC16_Check_Sum(
    reinterpret_cast<uint8_t *>(&packet),
    static_cast<uint32_t>(data_len),
    0xFFFF);

  // 输出：数据 + CRC16 低字节 + CRC16 高字节（小端序）
  size_t total_len = data_len + 2;
  std::vector<uint8_t> data(total_len);
  std::copy(
    reinterpret_cast<const uint8_t *>(&packet),
    reinterpret_cast<const uint8_t *>(&packet) + data_len,
    data.begin());
  data[total_len - 2] = static_cast<uint8_t>(crc & 0x00FF);         // CRC 低字节
  data[total_len - 1] = static_cast<uint8_t>((crc >> 8) & 0x00FF);  // CRC 高字节

  return data;
}

#endif  // MVS_OPENVINO_DEMO__PACKET_HPP_
