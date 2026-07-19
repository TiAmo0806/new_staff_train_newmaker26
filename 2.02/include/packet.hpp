/**
 * @file packet.hpp
 * @brief 视觉 ⇄ 电控（MCU）串口通信协议数据结构
 *
 * 定义上位机（NUC / robot_vision）与下位机（STM32F407）之间的数据包格式。
 * 所有多字节字段采用小端序（Little-Endian），整个包不跨字节对齐。
 *
 * 协议速览：
 *   视觉 → 电控: 12 字节 (header + flags + pitch + yaw + CRC)
 *   电控 → 视觉:  4 字节 (header + flags + CRC)
 *
 * @see docs/communication_protocol.md 完整协议说明
 */

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

/**
 * @brief 视觉 → 电控：目标位置与跟踪指令（12 字节）
 *
 * 上位机每次发送一个数据包，告知下位机：
 *   - 目标在哪个区域（取豆区 / 放置区）
 *   - 目标在第几个位置（位置编号 0~4）
 *   - 当前的跟踪状态（预定位 / 锁定执行）
 *
 * 字节布局:
 *   [0]    header     0xA6         1B
 *   [1]    flags      位域编码       1B
 *   [2-5]  pitch_cmd  float（未用）  4B
 *   [6-9]  yaw_cmd    float（未用）  4B
 *   [10-11] checksum  CRC-16/CCITT  2B
 *                        合计       12B
 *
 * flags 字节位域（MSB → LSB）:
 *   bit[7:6] target_type  (2bit) — 目标区域: 0=无, 1=取豆区, 2=放置区
 *   bit[5:3] target_id    (3bit) — 位置编号: 取豆区 0~2, 放置区 0~4
 *   bit[2]   tracking     (1bit) — 0=预定位(请移动), 1=锁定(请执行)
 *   bit[1:0] reserved     (2bit)
 *
 * @note 位域声明顺序需要与 GCC 的 LSB-first 布局反向对应：
 *       协议中 target_type 在 MSB 侧，所以它在位域声明中最后出现。
 */
struct VisionToMCUPacket
{
  uint8_t header = 0xA6;
  // ⭐ 位域顺序修正：GCC LSB-first，协议 MSB-first → 反向声明
  // 协议字节顺序: [target_type(2)][target_id(3)][tracking(1)][reserved(2)] MSB→LSB
  uint8_t reserved : 2;       // 低位（bit[1:0]）
  uint8_t tracking : 1;       // bit[2]
  uint8_t target_id : 3;      // bit[5:3]
  uint8_t target_type : 2;    // 高位（bit[7:6]）
  float pitch_cmd = 0.0f;     // 俯仰角（当前未使用，固定传 0）
  float yaw_cmd = 0.0f;       // 偏航角（当前未使用，固定传 0）
  uint16_t checksum = 0;      // CRC-16/CCITT（对整个包除 checksum 外的 10 字节计算）
} __attribute__((packed));

/**
 * @brief 电控 → 视觉：动作执行反馈（4 字节）
 *
 * 下位机完成一个动作后发送给上位机，告知执行结果：
 *   feedback_type = 1 (抓取完成) → 视觉进入找放置区箱子状态
 *   feedback_type = 2 (放置完成) → 视觉计数 +1，回到识别豆子状态
 *   feedback_type = 3 (移动完成) → 视觉根据当前状态做不同处理
 *   feedback_type = 4 (动作失败) → 视觉忽略，电控自行重试
 *
 * 字节布局:
 *   [0]    header     0x5A          1B
 *   [1]    flags      位域编码        1B
 *   [2-3]  checksum   CRC-16/CCITT  2B
 *                        合计        4B
 *
 * flags 字节位域（MSB → LSB）:
 *   bit[7:5] feedback_type (3bit)
 *   bit[4:0] reserved      (5bit)
 */
struct MCUToVisionPacket
{
  uint8_t header = 0x5A;     // 固定包头，用于字节同步
  // ⭐ 位域顺序修正：协议字节顺序 [feedback_type(3)][reserved(5)] MSB→LSB
  uint8_t reserved : 5;       // 低位（bit[4:0]）
  uint8_t feedback_type : 3;  // 高位（bit[7:5]）
  uint16_t checksum = 0;      // CRC-16/CCITT（对前 2 字节计算）
} __attribute__((packed));

// ============================================================
//  序列化 / 反序列化工具函数
// ============================================================

/**
 * @brief 将字节向量反序列化为 VisionToMCUPacket 结构体
 * @param data 从串口接收到的原始字节（长度应 >= sizeof(VisionToMCUPacket)）
 * @return 解析后的数据包结构体
 */
inline VisionToMCUPacket fromVector(const std::vector<uint8_t> & data)
{
  VisionToMCUPacket packet;
  std::copy(data.begin(), data.end(), reinterpret_cast<uint8_t *>(&packet));
  return packet;
}

/**
 * @brief 将 VisionToMCUPacket 序列化为字节向量，用于串口发送
 * @param data 待发送的数据包
 * @return 可用于 write() 的字节向量
 */
inline std::vector<uint8_t> toVector(const VisionToMCUPacket & data)
{
  std::vector<uint8_t> packet(sizeof(VisionToMCUPacket));
  std::copy(
    reinterpret_cast<const uint8_t *>(&data),
    reinterpret_cast<const uint8_t *>(&data) + sizeof(VisionToMCUPacket),
    packet.begin());
  return packet;
}

/**
 * @brief 安全解析电控反馈包（防止缓冲区积累导致的越界）
 *
 * @note 从串口 read() 时，如果前一帧数据未被消费完，buffer 中可能残留旧字节。
 *       本函数仅取前 sizeof(MCUToVisionPacket)=4 字节进行反序列化，
 *       不会访问超出的内存。
 */
inline MCUToVisionPacket fromMCUVector(const std::vector<uint8_t> & data)
{
  MCUToVisionPacket packet;
  size_t copy_len = std::min(data.size(), sizeof(MCUToVisionPacket));
  std::copy(data.begin(), data.begin() + copy_len, reinterpret_cast<uint8_t *>(&packet));
  return packet;
}

}  // namespace rm_serial_driver

#endif  // RM_SERIAL_DRIVER__PACKET_HPP_