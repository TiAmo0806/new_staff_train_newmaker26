// Copyright (C) 2024
// Licensed under the Apache-2.0 License.

#ifndef MVS_OPENVINO_DEMO__PACKET_HPP_
#define MVS_OPENVINO_DEMO__PACKET_HPP_

#include "CRC16.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

/**
 * @brief 检测排序结果发送帧（固定 8 字节）
 *
 * 帧格式（小端序）：
 *   [header 0xA5] [count 1B] [id0][id1][id2][id3] [CRC16 2B]
 *   总计固定 8 字节，未使用的 class_id 填充 9（哨兵值）
 *
 * 使用方式：
 *   SendPacket packet;
 *   packet.count = 3;
 *   packet.class_ids[0] = 1;
 *   packet.class_ids[1] = 3;
 *   packet.class_ids[2] = 4;
 *   // class_ids[3] 自动填充为 9
 *   auto data = toVector(packet);  // 自动计算 CRC16，返回 8 字节
 */
struct SendPacket
{
  uint8_t header = 0xA5;
  uint8_t count = 0;
  uint8_t class_ids[4] = {0};   // 最多 4 个检测物品
} __attribute__((packed));

/**
 * @brief 将 SendPacket 序列化为固定 8 字节数组（自动附加 CRC16）
 *
 * @param packet  已填充的 SendPacket
 * @return        固定 8 字节的串口帧
 *
 * 内部流程：
 *   1. 未使用的 class_ids 位置填充 9（哨兵值，区分类别 0=黄豆）
 *   2. CRC16 对前 6 字节（header + count + 4 个 class_ids）计算校验码
 *   3. 拼接输出：6 字节数据 + CRC16（2 字节，小端序）= 固定 8 字节
 */
inline std::vector<uint8_t> toVector(SendPacket & packet)
{
  constexpr size_t DATA_LEN  = 6;   // header(1) + count(1) + class_ids[4](4)
  constexpr size_t TOTAL_LEN = 8;   // DATA_LEN + CRC16(2)

  // 未使用的 class_id 位置填充 9（避免与真实类别 0~6 混淆）
  for (int i = packet.count; i < 4; ++i)
    packet.class_ids[i] = 9;

  // CRC16 对固定 6 字节计算
  uint16_t crc = crc16::Get_CRC16_Check_Sum(
    reinterpret_cast<uint8_t *>(&packet),
    DATA_LEN,
    0xFFFF);

  // 输出固定 8 字节：6 字节数据 + 2 字节 CRC（小端序）
  std::vector<uint8_t> data(TOTAL_LEN);
  std::copy(
    reinterpret_cast<const uint8_t *>(&packet),
    reinterpret_cast<const uint8_t *>(&packet) + DATA_LEN,
    data.begin());
  data[TOTAL_LEN - 2] = static_cast<uint8_t>(crc & 0x00FF);         // CRC 低字节
  data[TOTAL_LEN - 1] = static_cast<uint8_t>((crc >> 8) & 0x00FF);  // CRC 高字节

  return data;
}

// ============================================================
// MCU→PC 接收帧（双向通信）
// ============================================================

/**
 * @brief MCU→PC 接收帧（固定 4 字节）
 *
 * 帧格式（小端序）：
 *   [header 0x5A] [action 1B] [CRC16 2B]
 *   总计固定 4 字节
 *
 * action 含义：
 *   0 = 停止采集（暂停推理+发送）
 *   1 = 开始采集（恢复推理+发送）
 *
 * 使用方式：
 *   ReceivePacket packet;
 *   if (parseReceivePacket(raw, packet)) {
 *       bool enable = (packet.action == 1);
 *       // 控制推理开关...
 *   }
 */
struct ReceivePacket
{
  uint8_t header;         // [0] 帧头，固定 0x5A
  uint8_t action;         // [1] 动作：0=停止, 1=开始
  uint16_t crc;           // [2-3] CRC16（小端序，对前 2 字节计算）
} __attribute__((packed));

/**
 * @brief 解析原始字节为 ReceivePacket（含 CRC 校验）
 *
 * @param raw     指向至少 4 字节的原始数据
 * @param packet  输出的 ReceivePacket
 * @return        解析成功（CRC 校验通过 且 header == 0x5A）
 *
 * 内部流程：
 *   1. 对 raw[0..3] 做 CRC16 校验（Verify_CRC16_Check_Sum，对前 2 字节计算）
 *   2. 校验通过后拷贝到 packet
 *   3. 最后验证 header 是否为 0x5A
 */
inline bool parseReceivePacket(const uint8_t * raw, ReceivePacket & packet)
{
  if (!crc16::Verify_CRC16_Check_Sum(raw, 4)) return false;
  std::memcpy(&packet, raw, 4);
  return packet.header == 0x5A;
}

#endif  // MVS_OPENVINO_DEMO__PACKET_HPP_
