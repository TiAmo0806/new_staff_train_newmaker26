/**
 * @file BeanProtocol.hpp
 * @brief 抓豆分拣协议定义 - 枚举、结构体、序列化声明
 */

#ifndef BEAN_SORTER__PROTOCOL_HPP_
#define BEAN_SORTER__PROTOCOL_HPP_

#include <cstdint>//提供固定大小的整数类型，确保在不同平台上的字节数一致
#include <vector>
#include <cstring>
#include <iostream>
#include <iomanip>//提供格式化输出功能，控制输出格式

#include "CRC16.hpp"
#include "ByteConverter.h"

namespace bean_sorter
{

// ======================== Enums ========================

enum BeanType : uint8_t//定义识别类型
{
  BEAN_NONE      = 0,
  BEAN_SOY       = 1,   // 黄豆
  BEAN_MUNG      = 2,   // 绿豆
  BEAN_KIDNEY    = 3,   // 白芸豆
  DATA_1         = 4,   // 1号箱标签
  DATA_2         = 5,   // 2号箱标签
  DATA_3         = 6,   // 3号箱标签
  DATA_4         = 7,   // 4号箱标签
  DATA_5         = 8,   // 5号箱标签
  BEAN_UNKNOWN   = 9,
};

enum SystemState : uint8_t//定义系统状态
{
  STATE_IDLE       = 0,
  STATE_DETECTING  = 1,
  STATE_MOVING     = 2,
  STATE_GRIPPING   = 3,
  STATE_PLACING    = 4,
  STATE_COMPLETED  = 5,
  STATE_ERROR      = 6,
};

enum ErrorCode : uint8_t//定义错误码含义
{
  ERR_NONE         = 0,
  ERR_GRIPPER      = 1,
  ERR_BIN_FULL     = 2,
  ERR_TIMEOUT      = 3,
  ERR_COMM         = 4,
  ERR_UNKNOWN      = 5,
};

// ======================== Packet Constants ========================

// DetectionPacket (Vision -> Control): 18 bytes
// [0xAA] [bean_type:1] [target_bin:1] [confidence:1] [pos_x:4] [pos_y:4] [size:4] [crc:2]
static const uint8_t DETECTION_HEADER = 0xAA;//检测帧头
static const uint32_t DETECTION_PACKET_SIZE = 18;//总字节数

// StatusPacket (Control -> Vision): 11 bytes
// [0xBB] [state:1] [flags:1] [current_bin:1] [error:1] [timestamp:4] [crc:2]
static const uint8_t STATUS_HEADER = 0xBB;//状态帧头
static const uint32_t STATUS_PACKET_SIZE = 11;//总字节数

// Flag bit masks
static const uint8_t FLAG_GRIPPER_OPEN = 0x01;//定义标志位，用单个字节的不同位来表示多个开关状态
static const uint8_t FLAG_READY        = 0x02;

// ======================== Packet Structs ========================

struct DetectionPacket//定义检测包
{
  uint8_t  bean_type;      // BeanType
  uint8_t  target_bin;     // 1-5, 0=pending
  uint8_t  confidence;     // 0-100
  float    position_x;     // normalized [-1, 1]
  float    position_y;     // normalized [-1, 1]
  float    size;           // normalized size
};

struct StatusPacket//定义状态包
{
  uint8_t  system_state;   // SystemState
  uint8_t  flags;          // bit0:gripper_open, bit1:ready
  uint8_t  current_bin;    // 1-5, 0=none
  uint8_t  error_code;     // ErrorCode
  uint32_t timestamp_ms;   // timestamp for debug
};

// ======================== Serialization (declarations) ========================

std::vector<uint8_t> encodeDetection(const DetectionPacket & pkt);
bool decodeDetection(const uint8_t * frame, DetectionPacket & pkt);

std::vector<uint8_t> encodeStatus(const StatusPacket & pkt);
bool decodeStatus(const uint8_t * frame, StatusPacket & pkt);

bool scanFrame(const std::vector<uint8_t> & buffer,
               uint8_t expected_header, uint32_t expected_size,
               std::vector<uint8_t> & out_frame);

// ======================== Inline Helpers ========================

inline const char * beanTypeName(BeanType type)//豆种转字符串
{
  switch (type) {
    case BEAN_SOY:    return "(Soybean)";
    case BEAN_MUNG:   return "(Mung bean)";
    case BEAN_KIDNEY: return " (White kidney bean)";
    case DATA_1:      return "(Data1)";
    case DATA_2:      return " (Data2)";
    case DATA_3:      return "(Data3)";
    case DATA_4:      return "(Data4)";
    case DATA_5:      return "(Data5)";
    case BEAN_NONE:   return "no";
    default:          return "unkown";
  }
}

inline const char * systemStateName(SystemState state)
{
  switch (state) {
    case STATE_IDLE:      return "空闲";
    case STATE_DETECTING: return "检测中";
    case STATE_MOVING:    return "移动中";
    case STATE_GRIPPING:  return "夹取中";
    case STATE_PLACING:   return "放置中";
    case STATE_COMPLETED: return "完成";
    case STATE_ERROR:     return "错误";
    default:              return "未知";
  }
}

inline uint8_t beanTypeToBin(BeanType type)//豆种→箱子编号
{
  switch (type) {
    case BEAN_SOY:    return 1;
    case BEAN_MUNG:   return 2;
    case BEAN_KIDNEY: return 3;
    case DATA_1:      return 1;
    case DATA_2:      return 2;
    case DATA_3:      return 3;
    case DATA_4:      return 4;
    case DATA_5:      return 5;
    default:          return 0;
  }
}

inline void hexDump(const uint8_t * data, uint32_t len)//十六进制打印
{
  std::ios oldState(nullptr);
  oldState.copyfmt(std::cout);
  for (uint32_t i = 0; i < len; ++i)
    std::cout << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(data[i]) << " ";
  std::cout.copyfmt(oldState);
}

}  // namespace bean_sorter

#endif  // BEAN_SORTER__PROTOCOL_HPP_
